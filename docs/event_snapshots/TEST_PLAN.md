# TEST PLAN — Event-coupled snapshots + unified snapshot scheduler (FW 1.5.0)

Validate that (1) every **state-changing event** is followed by a fresh **complete snapshot** of
post-event state, (2) a burst (leak → auto_close → valve_state_changed, or several sensors) coalesces
into a **minimal** number of snapshots reflecting **post-burst** state, (3) **every** snapshot publish
(heartbeat / commission / boot / event) re-arms the 5-min heartbeat so the next heartbeat is ≥5 min
after the *last snapshot of any kind* — and the re-arm happens **only on a snapshot that actually
reached esp-mqtt**, never one dropped offline, and (4) snapshots are **complete** (now carry
`data.rules`, `data.reason`, and `data.expires_ts`).

> Hub under test: `feature/event-snapshots`, `gateway.fw = 1.5.0`. New scheduler lives entirely in
> `iothub_task` (lock-free monotonic deadline + single flush block); `publish_json`/`publish_snapshot`
> now return a real "reached esp-mqtt" bool; the 5-min timer is demoted to a fixed liveness backstop.

---

## 0. Prerequisites & tooling

| Item | Value |
|---|---|
| Hub branch / version | `feature/event-snapshots`, **1.5.0** |
| Valve MAC | `00:80:E1:27:F7:BB` |
| Leak sensor A (keep LIVE) | `00:80:e1:2a:3f:59` |
| Leak sensor B (OFFLINE-case / multi-sensor burst) | `00:80:e1:2a:3b:00` |
| Serial port | `COM4` (adjust if different) |
| Device / gateway ID | `GW-34B7DA6AAD54` |

**Tools to have running before each test:**
1. **Serial monitor** — watch `IOTHUB`, `TELEMETRY_V2`, `RULES_ENGINE`, `HEALTH_ENGINE`, `BLE_LEAK`:
   ```
   idf.py -p COM4 monitor
   ```
2. **Cloud monitor** — VS Code *Azure IoT Hub → Start Monitoring Built-in Event Endpoint*, or:
   ```
   az iot hub monitor-events -n wd-core-iothub-poc -d GW-34B7DA6AAD54 --timeout 0
   ```
3. **A stopwatch** for the latency/heartbeat tests.

**New serial markers introduced by this feature (assert these):**
> Every snapshot now logs a **pair**:
> `IOTHUB: SNAP trigger=<heartbeat | event:<name> | commission | boot>`
> `IOTHUB: SNAP heartbeat=reset interval_ms=300000`  (on a confirmed publish)
> or `IOTHUB: SNAP heartbeat=suppressed (publish-failed)`  (connected but outbox rejected)
> The underlying publish still logs `TELEMETRY_V2: Pub snapshot: {…}` (or `Pub … failed (msg_id=…)`).

**C2D command envelopes** (send via the app, the Azure IoT Toolkit, or `az iot device c2d-message send
-n wd-core-iothub-poc -d GW-34B7DA6AAD54 --data '<json>'`):
- **Decommission-all** (clean state): `{"schema":"eflostop.cmd","ver":1,"id":"decom","cmd":"decommission","payload":{"target":"all"}}`
- **Provision — 1 live sensor**: `{"schema":"eflostop.cmd","ver":1,"id":"prov-1","cmd":"provision","payload":{"valve_mac":"00:80:E1:27:F7:BB","ble_leak_sensors":["00:80:e1:2a:3f:59"]}}`
- **valve_close / valve_open** (state-change without a physical leak): `{"schema":"eflostop.cmd","ver":1,"id":"vc","cmd":"valve_close","payload":{}}`
- **rules_config** (toggle auto_close): `{"schema":"eflostop.cmd","ver":1,"id":"rc","cmd":"rules_config","payload":{"auto_close_enabled":false}}`
- **Twin desired** (interval): set `snapshot_interval_s` to `120` on the device twin desired properties.

**Clean-state reset (before each test):** send decommission-all → wait for `cmd_ack decommission status:ok`
and a fresh boot banner (`Hub is UNPROVISIONED`) → confirm `IOTHUB: Connected to Azure IoT Hub!`.

---

## E0 — Build & version sanity
1. `idf.py build` → reports version **1.5.0**.
2. `idf.py -p COM4 flash monitor`.
3. **P:** boot banner `App version: 1.5.0`; first lifecycle/snapshot shows `"fw":"1.5.0"`.

## E1 — AC4: complete snapshot fields
1. Clean state, then **Provision — 1 live sensor**; wait for the boot/commission snapshot.
2. Inspect the `type:"snapshot"` payload on the cloud monitor.
3. **P:** `data.reason` present (`"boot"` or `"commission"`); `data.rules` = `{auto_close_enabled, trigger_mask}`;
   `override_active` present; legacy `override_remaining_s` unchanged when an override is active.
4. Send **rules_config** `auto_close_enabled:false`; trigger any snapshot.
5. **P:** the next snapshot's `data.rules.auto_close_enabled = false` (the app can now learn auto-close
   state from a snapshot — DEFECT #2 fixed).

## E2 — AC1: event-coupled snapshot (single leak)
1. Provisioned + live. Wet leak sensor A.
2. **P (serial order):** `TELEMETRY_V2: Pub event: {…"event":"leak_detected"…}` → `IOTHUB: SNAP
   trigger=event:leak_detected` → `TELEMETRY_V2: Pub snapshot: {…}` → `IOTHUB: SNAP heartbeat=reset…`.
3. **P (cloud):** the event message AND a snapshot with `lora`/`ble` sensor `leak_state:true` arrive,
   the snapshot within ~0.3 s after the event. Dry the sensor → symmetric `leak_cleared` + snapshot.

## E3 — AC3: burst coalescing (leak → auto_close → valve_state_changed)
1. Ensure `auto_close_enabled:true`, valve connected & open. Wet a sensor that triggers auto-close.
2. **P (serial):** the three events publish (`leak_detected`, rules `auto_close`, `valve_state_changed`),
   then **exactly one** `SNAP trigger=event:*` ~300 ms after the **last** event, then one `Pub snapshot`,
   then `SNAP heartbeat=reset`. **Not** three snapshots.
3. **P (cloud):** the single snapshot shows `valve.state:"closed"`, `valve.rmleak:true`, the wet sensor
   `leak_state:true` — i.e. **post-burst** state.

## E4 — AC2: heartbeat suppression / re-arm
1. Provisioned, idle, online. Note the wall-clock T of any snapshot (`SNAP heartbeat=reset`).
2. Within the next minute, cause a leak event (→ event snapshot, new reset at T2).
3. **P:** the next **`SNAP trigger=heartbeat`** is **≥300 s after T2** (the last snapshot), not at T+300 s.
   No back-to-back heartbeat near an event/commission snapshot.

## E5 — AC2/AC5: offline → online re-arm
1. Provisioned + connected. Drop Wi-Fi (or `iothub_suspend_mqtt` via WiFi reset). Wet a sensor.
2. **P (offline):** event is buffered (`Offline — buffering event`); the snapshot flush is **skipped**
   (no `SNAP heartbeat=reset`); **no** re-arm.
3. Restore Wi-Fi. **P (reconnect):** `Connected…` → drain offline events → lifecycle → **one** `SNAP
   trigger=…` → `Pub snapshot` → `SNAP heartbeat=reset`. Confirm the **first heartbeat after reconnect**
   is ≥300 s later (never stuck >5 min, never a redundant burst).

## E6 — Invariant 5: no offline tight-spin
1. Stay offline (Wi-Fi down) with a pending event snapshot (from E5 step 1).
2. **P:** the loop idles at the **30 s** floor — serial shows no busy logging; CPU stays low. On
   reconnect the loop wakes immediately (`telemetry_v2_wake_snapshot`) and flushes.

## E7 — AC2: outbox saturation (connected but publish fails)
1. Force a publish failure while connected (e.g. saturate by flooding events on a constrained link, or
   temporarily lower the broker; otherwise inspect via a transient disconnect race).
2. **P:** `TELEMETRY_V2: Pub snapshot failed (msg_id=-1)` → `IOTHUB: SNAP heartbeat=suppressed
   (publish-failed)`; heartbeat **not** re-armed; a retry follows ~5 s later (no tight-spin).

## E8 — AC5: commission / boot routing
1. Clean state → **Provision — 1 live sensor**.
2. **P:** the first snapshot is `SNAP trigger=boot` (or `commission` for a late device), published via the
   **single flush** only after a confirmed send; `g_boot_snapshot_sent` is set only on success (kill Wi-Fi
   mid-commission and confirm the boot snapshot is **not** lost — it republishes on reconnect).
3. Power sensor B after the window; **P:** `SNAP trigger=commission` (incremental refresh) — prompt, not
   delayed 5 s by the min-interval clamp.

## E9 — AC5: decommission
1. With an override window active (trigger it), send **decommission-all**.
2. **P:** override NVS wiped; subsequent snapshots omit `expires_ts` and `override_active:false` (no stale
   value); clean unprovisioned state; no stranded `SNAP trigger=event:*` with a removed device's label.

## E10 — Twin interval parity
1. Set twin desired `snapshot_interval_s = 120`.
2. **P:** `TELEMETRY_V2: Snapshot interval set to 120s (heartbeat scheduler)`; the next `SNAP
   heartbeat=reset interval_ms=120000`; an in-flight event snapshot still flushes at its 300 ms window
   (unaffected). The fixed liveness timer is **not** reprogrammed.

## E11 — Valve event delta-gate
1. Drive repeated identical valve state notifications (battery notifies / reconnect with same state).
2. **P:** `valve_state_changed` is emitted **only** on a real open/closed change (not every notify); the
   coupled snapshot rate stays ≤12/min.

## E12 — AC6: observability pairing
1. Across all tests above, for **every** snapshot confirm both lines appear: `SNAP trigger=…` and
   `SNAP heartbeat=<reset|suppressed>`. Map each acceptance criterion to its asserted line.

## E13 — Fast boot snapshot (reset → UI ASAP)
*Motivated by the field logs: cold boot with sensor `…3b:00` offline previously left the UI blank for
~120 s waiting on the boot-sync timeout, even though the valve was READY at ~20–26 s.*
1. Power-cycle the hub with `…3b:00` offline and `…3f:59` live (dry).
2. **P:** `SNAP trigger=fast` publishes within ~one HIGH window of `BLE_VALVE: [READY]` valve connect
   (**~20–30 s cold**, sub-second on a warm MQTT reconnect) — **well before** `HEALTH_ENGINE: Boot sync:
   timeout (120 s)`. The `fast` snapshot carries the valve state + any already-heard sensors; a not-yet-heard
   sensor shows `null`.
3. **P:** when `…3f:59` next beacons, a `SNAP trigger=commission` (incremental refresh) fills its data;
   `…3b:00` stays `null` (honestly offline). No separate `reason:"boot"` snapshot fires on this path.
4. **P (regression):** heartbeat re-arm parity holds (every snapshot still logs `SNAP heartbeat=reset`);
   the commission/provision path is unaffected (a `provision` C2D still waits for all-heard/150 s, not valve-ready).

**Fast-boot behavior notes (for the app team):**
- New `data.reason` value **`"fast"`** — the early valve-ready boot snapshot; may carry not-yet-heard
  sensors as `null` (they fill in via subsequent snapshots). Purely additive; schema stays `eflostop.v2`.
- `data.reason:"commission"` snapshots can now also appear **after a plain reboot** (not only after a
  `provision` command) — they are the incremental-refresh fills as each sensor first beacons. Treat any
  snapshot as source-of-truth regardless of `reason`.
- The dedicated complete `reason:"boot"` snapshot no longer fires on the boot/reconnect path (fast +
  incremental fills replace it); `reason:"boot"` still fires for the fully-healthy all-heard case and as
  the ≤120 s fallback when the valve never connects.

---

## Residual checks (document, not blockers)
- The discrete **leak event** stream is uncapped (only snapshots coalesce); confirm the ≤12/min cap
  applies to **snapshots**. (`valve_state_changed` is now delta-gated; LoRa/BLE leak events already are.)
- Rules engine keeps a single pending-telemetry slot — if two rules events land in one tick, the
  intermediate **event label** may be lost; the **snapshot state** is still correct.
- First post-reconnect snapshot latency: with the immediate-wake it should be ~one loop iteration; record it.
