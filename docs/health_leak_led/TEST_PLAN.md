# FW 2.1.2 — manual bench test plan

Validates the fixes in `PLAN.md`. Written so that **you run the tests and capture data, and the captured
data alone is enough for someone else to verify every result** — every assertion below is a literal log
string or a JSON path, not a judgement call.

Nothing here needs a debugger or a logic analyser. You need the hub, the valve, 4 sensors, water, and the
two log captures described in §2.

---

## 1. Setup

| Item | Value |
|---|---|
| Hub | ESP32-S3, FW **2.1.2**, provisioned: 1 valve + 4 BLE leak sensors |
| Valve | `00:80:E1:27:F7:BB`, FW 2.2.0 |
| Sensor A | `00:80:E1:2A:2B:A5` — bathroom / "Ensuite" |
| Sensor B | `00:80:E1:2A:CB:B6` — bathroom / "Main Bathroom" |
| Sensor C | `00:80:E1:2A:B6:8E` — kitchen / "dishwasher" |
| Sensor D | `00:80:E1:2A:29:FC` — laundry / "under washer" |
| Rules | `auto_close_enabled: true`, `trigger_mask: 0x07` |
| Fleet LED | **GPIO 48** — solid colour, system health |
| Network LED | **GPIO 38** — animated, network status (must stay independent) |

Build and flash:

```powershell
$env:IDF_PATH = 'C:\Users\antun\esp\v5.5.1\esp-idf'
idf.py build
idf.py -p COM30 flash
```

**Wet a sensor** = lay a damp cloth across the probe pads, or drop water on them. Prefer standing water
over a damp finger — a finger bridge flaps (see `NOTES_D7_D8.md` D8) and will muddy T1's event count.
**Dry a sensor** = wipe both pads dry and wait for its next dry report (up to ~100 s).

---

## 2. Capture — do this once, before you start

Two logs, both running for the whole session. Everything in §4 is verified from these.

**(a) UART.** Log to a file, do not just watch it scroll:

```powershell
idf.py -p COM30 monitor | Tee-Object -FilePath uart_2.1.2.log
```

**(b) Telemetry.** VS Code → Azure IoT Hub → right-click the device → **Start Monitoring Built-in Event
Endpoint**. At the end, copy the whole output pane to `telemetry_2.1.2.txt`.

**Annotate as you go.** This is the one thing that cannot be recovered afterwards. Keep a third plain-text
file, `bench_notes.txt`, and for every test write down:

```
T3  15:42:10  wet sensor D
T3  15:42:14  LED went RED
T5  15:43:02  dried sensor D
T5  15:43:05  LED went YELLOW
T5  15:43:36  LED went GREEN
```

The LED colour and its wall-clock time are the only observations **not** in either log. Without them,
half the tests cannot be verified. The UART `FLEET_LED:` lines corroborate the colour but your eyes on
GPIO 48 are what actually proves the hardware did it.

---

## 3. How to read the assertions

- `UART:` a literal substring of a UART line. `UART-NOT:` must never appear in the stated window.
- `TELEM:` a JSON path in a `snapshot` or `event` message, with the required value.
- Where a count is given (`exactly one`, `zero`), count over the stated time window — that is the assertion.

Useful greps once you have the logs:

```powershell
Select-String -Path uart_2.1.2.log -Pattern 'FLEET_LED|HEALTH_ENGINE|RULES_ENGINE|\[SETUP\] Step|SETUP COMPLETE'
Select-String -Path telemetry_2.1.2.txt -Pattern '"cause":"reconnect"'        # must be 0 in T1
Select-String -Path telemetry_2.1.2.txt -Pattern '"system_health"' -Context 0,2
```

---

## 4. Test cases

### Group A — the storm (the headline regression)

#### T1 — no auto_close storm
**Why:** 2.1.1 emitted 35+ `auto_close` events in 40 s for one wet sensor.

1. Confirm the valve link is up and settled (`UART: SETUP COMPLETE - READY FOR GATT`).
2. Note the time. Wet **sensor D**.
3. Keep it wet, undisturbed, for **90 s**.
4. Note the time again. This 90 s window is the assertion window.

**PASS:**
- `TELEM:` **exactly one** `auto_close` event in the window, and it has `data.source_type == "ble_leak_sensor"`, `data.sensor_id == "00:80:E1:2A:29:FC"`.
- `TELEM:` **zero** events with `data.cause == "reconnect"`.
- `UART-NOT:` `[SETUP] Step 11` — and no `Step 12`, `Step 13`, … at all.
- `UART:` `SETUP COMPLETE - READY FOR GATT` appears **zero** additional times in the window (it was logged once at link-up, before the window).
- `UART:` `VALVE RECONNECT RECONCILIATION` appears **zero** times in the window.
- `UART:` `AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor 00:80:E1:2A:29:FC` appears **exactly once**.
- The valve audibly closes **once**.

**FAIL signature (2.1.1):** `[READY]` → `VALVE RECONNECT RECONCILIATION` → `executing auto-close` → `CMD: SET_RMLEAK`, repeating every ~1.5 s.

#### T2 — the RMLEAK interlock is confirmed, not just sent
**Why:** in 2.1.1 every snapshot reported `rmleak: false` while the event claimed it was asserted.

Same run as T1, no extra steps.

**PASS:**
- `UART:` `[CMD] Writing RMLEAK=1` then `[CMD] RMLEAK write rc=0`.
- `UART:` `[DATA] RMLEAK=1 (ACTIVE)` — this is the read-back landing, and is the actual assertion.
- `TELEM:` the next `snapshot` after that has `data.valve.rmleak == true`.
- `UART-NOT:` `RMLEAK read-back rc=` (a non-zero read-back rc means the interlock is unconfirmed).

#### T3 — Azure message budget
**Why:** objective cost gate. Same window as T1.

**PASS:** total messages from the device in T1's 90 s window is **≤ 8**. (2.1.1 produced 60+ for the same
cycle.) Report the actual number.

#### T4 — NVS write rate
**PASS:** `UART:` `NVS: incident=1 saved` appears **at most twice** in T1's window. (This is a debug-level
line; if your build does not print it, record "not visible" — T3 already covers the symptom.)

---

### Group B — leak drives system health (the requirement)

#### T5 — a wet sensor forces CRITICAL and RED
While sensor D is still wet from T1.

**PASS:**
- **GPIO 48 is RED.** Record the time in `bench_notes.txt`.
- `UART:` `FLEET_LED: rating=critical color=RED effect=SOLID`.
- `TELEM:` `data.system_health.rating == "critical"`.
- `TELEM:` `data.system_health.reason` contains `Leak detected: under washer`.
- `TELEM:` in `data.ble_leak_sensors[]`, the entry for `00:80:E1:2A:29:FC` has `leak_state == true` **and** `rating == "critical"`. (In 2.1.1 this sensor reported `rating: "excellent"` while leaking — that is the specific bug.)

#### T6 — a leak beats an active override
**Why:** explicitly required. The override must still keep water on, but health must still scream.

1. Start with everything dry, valve open, LED green.
2. Enable the 24 h window: single long-press on the valve (or the `override_enable` C2D command).
3. Confirm `TELEM: data.override_active == true` and `data.valve.state == "open"`.
4. Now wet **sensor C**.

**PASS:**
- `TELEM:` `data.valve.state` stays `"open"` — the override is still honoured. **This must not have changed.**
- `TELEM:` an `auto_close_blocked_override` event is published.
- `TELEM:` `data.system_health.rating == "critical"` and `reason` contains `Leak detected: dishwasher`.
- **GPIO 48 is RED** while `override_active` is `true`.
- `TELEM:` `leak_detected` for sensor C is published as normal.

Then dry sensor C and cancel the override (`override_cancel`) before continuing.

#### T7 — the valve's own flood probe behaves the same as a sensor
Trigger the **valve's** flood input (its own probe, not a sensor).

**PASS:**
- `TELEM:` `data.system_health.rating == "critical"`, `reason` contains `Leak detected: valve`.
- **GPIO 48 is RED.**
- `UART:` `[DATA] Leak=1 (LEAK)`.
- The valve closes.

#### T7b — valve probe ALREADY WET at link-up  ← **highest-value new test**
**Why:** this found a real safety hole. `rules_engine_evaluate_leak(LEAK_SOURCE_VALVE, …)` runs only on a
`BLE_UPD_LEAK` event, and that event is suppressed while setup is in progress. So a probe that was already
wet *before* the link came up produced no dry→wet edge for anyone to act on: the hub reported critical and
lit the LED red while **leaving the valve open indefinitely**. 2.1.2 announces it once setup completes.

This is the one test that cannot be reached by wetting things in the normal order — the wetness has to
pre-date the BLE link.

1. Everything dry, hub running, valve linked. Confirm LED GREEN.
2. **Wet the valve's own flood probe** and keep it wet.
3. Wait for the valve to close and the LED to go RED (this is the ordinary T7 path).
4. Now **power-cycle the VALVE only** (leave the hub running), keeping the probe wet throughout.
5. Let the hub re-link to the valve. Watch for ~60 s.

**PASS:**
- `UART:` during re-setup, `[SETUP] Read FLOOD` then `[DATA] Leak=1 (LEAK)`.
- `UART:` `[READY] Flood probe already WET at link-up — announcing for evaluation` ← **the fix firing**.
- `UART:` `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by valve` shortly after.
- `TELEM:` a `leak_detected` event with `data.source_type == "valve"`.
- **The valve ends CLOSED**, `TELEM: data.valve.state == "closed"`, `data.valve.rmleak == true`.
- `TELEM:` `rating == "critical"`, `reason` contains `Leak detected: valve`.
- **GPIO 48 RED.**

**FAIL (the bug):** LED red and `rating: "critical"`, but `data.valve.state` stays `"open"` and no
`AUTO-CLOSE` line ever appears. If you see that, stop and tell me — it means the announcement did not fire.

A second, closer-to-real variant if you have time: with the probe wet, **power-cycle the HUB** instead of
the valve (step 4). Same assertions. This is the scenario that actually matters in the field — a flood at
the valve that starts while the hub is down.

---

### Group C — the three-stage LED (the agreed UX)

#### T8 — RED → YELLOW → GREEN across one incident
**Why:** this is the behaviour you chose: amber while the water is still off.

1. From T1/T5: sensor D wet, valve closed, LED RED.
2. Dry sensor D. Wait for its dry report.
3. Watch GPIO 48 **continuously** for the next 60 s and log every colour change with its time.

**PASS, in this order:**
- LED **RED** while wet.
- After the dry report: `UART:` `RULES_ENGINE: All sensors clear — auto-clear timer started (30s)`, and LED turns **YELLOW**.
- `TELEM:` a snapshot in the amber phase with `data.system_health.rating == "warning"` and `reason` containing `Leak interlock latched`.
- ~30 s later: `UART:` `RULES_ENGINE: AUTO-CLEAR: all sensors clear for 30s — clearing RMLEAK` and `HEALTH_ENGINE: Interlock released`, and LED turns **GREEN**.
- `TELEM:` `rmleak_auto_cleared` event; the following snapshot has `rating` back to `"excellent"` or `"good"`.

Record all three colour-change times. The YELLOW phase should last roughly 30 s.

#### T9 — the interlock survives a reboot
**Why:** the amber state is persisted, so a reboot mid-incident must not read as "normal".

1. Wet sensor D → valve closes. Dry it.
2. While the LED is **YELLOW** (before the 30 s auto-clear fires), **power-cycle the hub**.

**PASS:**
- `UART:` `RULES_ENGINE: NVS: restored incident latch — pending reconcile with valve`.
- **GPIO 48 becomes YELLOW** after boot — not GREEN (which would claim normality) and not RED (no leak is active).
- `TELEM:` the boot snapshot has `rating == "warning"`, `reason` containing `Leak interlock latched`.
- The auto-clear then completes and the LED goes **GREEN**.

---

### Group D — quiet boot (the false-offline fix)

#### T10 — a healthy hub does not go RED at boot
**Why:** 2.1.1 showed RED for 14 minutes on a fully healthy hub.

1. All 4 sensors present, dry, in range. Valve powered.
2. Power-cycle the hub. **Watch GPIO 48 from the instant it powers up** and log every colour change.

**PASS:**
- GPIO 48 goes **WHITE** (after a brief dark period while the health engine starts) — **never RED**.
- `UART:` `FLEET_LED: rating=syncing color=WHITE effect=SOLID`.
- `TELEM:` the first snapshot has `data.system_health.reason` matching `Syncing - waiting for N device(s)`.
- As sensors report in, `UART:` `HEALTH_ENGINE: Boot sync: all devices seen`.
- GPIO 48 then goes **GREEN**.
- `UART-NOT:` `Boot sync: timeout` (all four should be heard inside the 180 s window).
- `TELEM:` **zero** `device_offline` events for any sensor.
- **Total time RED: zero.** Record the time from power-up to GREEN.

#### T11 — a genuinely absent sensor is still escalated
**Why:** proves T10 did not achieve a quiet boot by going blind.

1. **Remove sensor C's battery.** Leave A, B, D healthy.
2. Power-cycle the hub. Watch GPIO 48 and log colour changes with times.

**PASS:**
- WHITE "syncing" during the window, as T10.
- `UART:` `HEALTH_ENGINE: Boot sync: timeout (180 s)` at ~180 s after boot.
- **GPIO 48 turns RED promptly after that line — within ~2 s, not 30 s later.** (This is the specific
  transition-edge fix; a 30 s lag here is a real finding.)
- `TELEM:` `rating == "critical"`, `reason == "1 sensor offline"`.
- Reinsert the battery: once sensor C reports, LED returns to **GREEN**.

> Do **not** expect a `device_offline` event here. Sensor C is seeded CRITICAL at boot and never
> *transitions* into CRITICAL, so there is no edge to alert on. That is pre-existing, unchanged behaviour.
> The RED and the snapshot reason are the assertions.

#### T12 — no false offline during a long idle
**Why:** this is the fix for the "N sensors offline" reports on a healthy hub.

Leave the hub **idle and untouched for 45 minutes**, all sensors healthy. Do not wet anything.

**PASS:**
- `TELEM:` across every snapshot in the window, **every** sensor's `last_seen_age_s` stays **< 240**.
  (2.1.1 routinely reached 320+; report the maximum you observe.)
- `TELEM:` **zero** `device_offline` events.
- `TELEM:` **zero** snapshots whose `reason` contains `offline`.
- GPIO 48 stays **GREEN** for the entire 45 minutes.

---

### Group D2 — provisioning during a live leak (round-2 findings)

#### T12b — a `provision` command while a sensor is wet must not lose the leak
**Why:** `health_engine_reload_devices()` memsets the device table, which wiped the `leaking` bit for every
sensor. Only the valve was re-seeded, so a routine `provision` re-push during a live leak dropped that
sensor out of the roll-up for up to ~100 s — and with `auto_close_enabled:false` the hub published
`rating:"excellent"` with a sensor standing in water. This is the highest-severity round-2 finding, and the
trigger is something an installer plausibly does *while troubleshooting a leak*.

1. Wet **sensor D**. Confirm `rating == "critical"`, `reason` contains `Leak detected: under washer`, LED **RED**.
2. Keeping it wet, send a `provision` C2D command with the **same** device list (a harmless re-push is enough).
3. Watch the next 3 snapshots and the LED continuously.

**PASS:**
- `UART:` `HEALTH_ENGINE: Device table loaded: 5 device(s)`.
- `UART:` `HEALTH_ENGINE: Reload: carried active leak for 00:80:E1:2A:29:FC` ← **the fix firing**.
- **GPIO 48 stays RED throughout** — no flicker to WHITE or YELLOW.
- `TELEM:` every snapshot after the reload still has `rating == "critical"` and `reason` containing `Leak detected`.
- `UART-NOT:` any snapshot with `"rating":"excellent"` while sensor D is wet.

**FAIL (the bug):** LED goes WHITE or YELLOW after the provision, and/or a snapshot reports `excellent` /
`Syncing - waiting for N devices` while D is still wet.

#### T12c — a valve that reconnects dry publishes `leak_cleared`
**Why:** the at-link-up flood announcement now fires for dry as well as wet, which closes a pre-existing
hole (a valve reconnecting dry after a wet episode never published a clear, and never told the rules engine
to drop itself from the active-leak table).

1. Wet the **valve's own probe**. Confirm `leak_detected` with `source_type: "valve"` and the valve closes.
2. **Dry the probe** but do **not** wait for anything.
3. Power-cycle the **valve only**. Let it re-link.

**PASS:**
- `UART:` `[READY] Announcing flood probe state (dry) for reconciliation`.
- `TELEM:` a `leak_cleared` event with `data.source_type == "valve"`.
- `TELEM:` the following snapshot has `data.valve.leak_state == false`.
- The incident then resolves normally (LED YELLOW → GREEN after the 30 s auto-clear).
- `UART-NOT:` a repeated `leak_detected` for the valve on this reconnect.

#### T12d — a valve flapping while wet must not spam `leak_detected`
**Why:** the link-up announcement repeats on every reconnect; a consumer counting leak events would
otherwise see several incidents where there is one.

1. Wet the valve's probe, keep it wet.
2. Power-cycle the valve **three times** in a row, letting it re-link each time.

**PASS:**
- `TELEM:` **exactly one** `leak_detected` with `source_type: "valve"` across all three reconnects.
- `UART:` `[READY] Flood probe already WET at link-up` three times (the announcement fires each time — correct).
- `UART:` `valve leak event suppressed — wet=1 already reported` on the 2nd and 3rd (debug level; if not
  visible in your build, the event count above is the assertion).
- The valve ends **closed** with `rmleak: true` after each reconnect.

### Group E — regression (must not have broken)

#### T13 — valve offline mid-incident, then back
**Why:** the one case where a `cause:reconnect` event is legitimate.

1. Wet **two** sensors (C and D). Confirm the valve closes.
2. Power the valve **down**. Wait 30 s. Power it back **up**.

**PASS:**
- `UART:` `VALVE RECONNECT RECONCILIATION` appears, with `Hub: incident=1 leaks=2`.
- Then **either** exactly one `auto_close` with `cause == "reconnect"` and `active_leak_count == 2`,
  **or** `UART: Reconnected with 2 active leak(s) — valve already closed + RMLEAK asserted, nothing to do`
  with no event. Both are correct.
- **Then silence** — no repetition of either for the rest of the incident. Repetition is a FAIL.
- While the valve is down: `TELEM:` `data.valve.connected == false`, `rating == "warning"` (3-min grace),
  becoming `"critical"` only after ~3 minutes.
- End state: `data.valve.state == "closed"`, `data.valve.rmleak == true`.

#### T14 — the network LED is untouched
Power the WiFi AP down, wait 30 s, power it back up.

**PASS:**
- GPIO 38 still animates: ramp **red** (no internet) → beat **blue** (connecting) → ramp **blue** (connected).
- GPIO 48 is **unaffected** by the WiFi state throughout.
- `UART:` `NET_STATUS: wifi=0 mqtt=0` then `wifi=1 mqtt=1`.

#### T15 — unprovisioned hub
On a factory-fresh or `decommission_all`ed hub (do this **last**, it wipes commissioning).

**PASS:**
- GPIO 48 is **WHITE**. `UART:` `FLEET_LED: rating=unprovisioned color=WHITE`.
- `UART-NOT:` any `Leak interlock latched` or `rating=warning` on the post-decommission snapshot.
- The AP `WiFi-Hub-69C8` appears and WiFi configuration completes normally.

#### T16 — provision / re-commission
Send a `provision` command with a changed sensor set.

**PASS:**
- `UART:` `HEALTH_ENGINE: Device table loaded: N device(s)`.
- GPIO 48 goes WHITE "syncing", then settles GREEN as devices are re-heard.
- `TELEM:` `snapshot` with `reason: "commission"`, then commission-refresh snapshots as late sensors arrive.

#### T17 — version
**PASS:**
- `UART:` `App version: 2.1.2`.
- `TELEM:` `gateway.fw == "2.1.2"` on every message.
- Twin reported `fw_version == "2.1.2"`.

---

## 5. What to send me

1. `uart_2.1.2.log`
2. `telemetry_2.1.2.txt`
3. `bench_notes.txt` — the LED colours and times. **Without this, T8–T11 cannot be verified**, because
   the LED is the thing under test and it does not appear in either log except as a corroborating
   `FLEET_LED:` line.
4. The filled-in results table below (even just P/F/skipped).

If you only have time for a subset, run **T1, T5, T7b, T8, T10, T11** — those six cover the storm, the
leak→red requirement, the already-wet-at-link-up safety hole, the three-stage LED, quiet boot, and the proof
that quiet boot did not go blind. Everything else is regression cover.

Add **T12b** to that subset. **T7b and T12b are the two I would not skip:** both cover paths where the hub
could report a leak while leaving the water on, both are newly written code with no field evidence behind
them, and both were found by review rather than by the logs — so nothing in the captures you already have
would have caught either.

---

## 6. Results

| # | Test | Result | Observed / notes |
|---|---|---|---|
| T1 | No auto_close storm | **PASS** | 0 `cause:reconnect` during the leak (2.1.1: 35+); 1 `auto_close`; `setup_step` stops at 10; `SETUP COMPLETE` x1 |
| T2 | RMLEAK confirmed | **PASS** | `[DATA] RMLEAK=1 (ACTIVE)` at 146.3 s — line absent in 2.1.1; snapshot `rmleak:true` |
| T3 | Azure message budget | **PASS** | 5 msgs in the 90 s wet window, 11 for the whole cycle (2.1.1: 60+) |
| T4 | NVS write rate | n/a | `NVS: incident=%d saved` is ESP_LOGD — not printed at default level. T3 covers the symptom |
| T5 | Wet sensor → CRITICAL/RED | **PASS** | `critical` / `Leak detected: under washer, Leak interlock latched`; LED RED 200 ms after detection |
| T6 | Leak beats override | not run | |
| T7 | Valve flood probe parity | **PASS** | probe wet 662.9 s → `AUTO-CLOSE ... by valve`, `Leak detected: valve`, LED RED, valve closed |
| T7b | Valve probe wet at link-up | **PARTIAL** | Mechanism confirmed: announce fired, reached the rules engine, valve ended closed + RMLEAK. **But the hole was not isolated** — valve returned already closed with `active_leak_count=1`, so Priority 1 would have covered it anyway. See the isolating variant below. **OPEN** |
| T8 | RED → YELLOW → GREEN | **PASS** | run 1: 145.6 / 285.9 / 330.9 s (YELLOW 45 s). run 2: YELLOW 30.5 s. Variance is the idle-loop tick cadence, bounded 30–60 s |
| T9 | Interlock survives reboot | not run | |
| T10 | Quiet boot | **PASS** | OFF → WHITE 2.6 s → GREEN; **never RED** (2.1.1: RED for 14 min). `Boot sync: all devices seen`, not timeout |
| T11 | Absent sensor escalated | not run | **Important** — proves T10 did not go blind |
| T12 | No false offline (45 min) | not run | early signal good: ages 70/0/31/16 s vs 320+ in 2.1.1 |
| T12b | Provision during live leak | not run | **Next** |
| T12c | Valve reconnects dry → leak_cleared | not run | valve returned wet in the T7b run, so untested |
| T12d | Valve flapping wet → no event spam | **PASS** | 1 `leak_detected/valve` across the reconnect; `s_valve_pub_wet` gate held |
| T13 | Valve offline mid-incident | **PASS** | 1 legitimate `cause:reconnect` (`active_leak_count:1`) then silence; ends closed + `rmleak:true`; reason carried all three causes while offline |
| T14 | Network LED untouched | not run | |
| T15 | Unprovisioned hub | not run | do last — wipes commissioning |
| T16 | Provision cycle | not run | |
| T17 | Version | **PASS** | `App version: 2.1.2`, `gateway.fw:"2.1.2"` |

### Open items

- **T7b isolating variant (deferred).** The hole needs the hub to have NO memory of the valve leaking —
  `active_leak_count == 0` and no latched incident at link-up. Steps: confirm LED GREEN / `rating:excellent`
  / valve open → **power the hub OFF** → **then** wet the valve probe (it stays open, nothing commands it) →
  **power the hub ON**. Priority 1 and Priority 2 both no-op in that state, so the link-up announcement is
  the only thing that can act. Pass = `Flood probe already WET at link-up`, then
  `AUTO-CLOSE + RMLEAK triggered by valve`, and the valve going **open → closed**.
- **Snapshot `rmleak:false` ~1 s before the read-back lands** (seen at up=662 and up=713). The 1500 ms
  command-settle gate does not quite cover it. Self-corrects on the next snapshot. Not fixed.
- **`HEALTH_BOOT_SYNC_TIMEOUT_MS` margin.** First boot used 161.7 s of the 180 s window (90%); second used
  98.7 s. Recommend raising to 240 s — the window only gates the boot snapshot, and `SNAP_FAST` already
  escapes at valve-ready (~19 s). Not changed yet.

---

## 7. Known-acceptable observations

Do not raise these as failures:

- `E (nnn) gpio: gpio_install_isr_service(526): GPIO isr service already installed` at every boot. Emitted
  by the IDF driver itself; the service is legitimately already installed by the LoRa driver. Benign.
- GPIO 48 dark for the first ~3 s of boot — the health engine starts from the iothub task, after WiFi.
- `W (nnn) BLE_LEAK: Failed to start ext scan: 15, will retry` around valve connect. Only one BLE scan can
  run at a time; it retries and succeeds.
- A sub-second `leak_cleared` → `leak_detected` pair during a manual wet test. Probe flap, not a hub
  defect — see `NOTES_D7_D8.md` D8. It is also why T1 asks for standing water.
- `SNAP clamped by min-interval` — the snapshot rate limiter working as designed.
- Per-device `rating` disagreeing with `system_health.rating`: legitimate now. A not-yet-heard device reads
  `critical` individually while the roll-up excludes it during the boot window, and the interlock floor can
  make the roll-up worse than every device in it.
