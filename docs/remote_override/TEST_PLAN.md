# TEST PLAN — Remote Water-Access Override (`override_enable`)

Bench procedure you can follow to validate the feature end-to-end. The Watts app isn't built yet, so you
**simulate the app** by sending the same C2D command the backend will send (`az iot`) and watching D2C
telemetry. Each test maps to the app action it represents (last column). States that telemetry can't show
(offline / no-network) are confirmed on the **serial console**.

> Hub firmware under test: this branch (`gateway.fw = 1.4.0`). The override command is `override_enable`; it is
> the remote twin of the physical valve button — both must end in the identical state.

---

## 0. Prerequisites & setup

**Hardware**
- Hub flashed with this branch, **provisioned** with a valve **and** ≥1 BLE leak sensor, **online** (MQTT up,
  SNTP synced — confirm a snapshot shows a real `ts` and `gateway.fw":"1.4.0"`).
- Valve powered, bonded (passkey 222900), BLE-connected to the hub.
- A way to **wet/dry the leak sensor** (dip the probe / short its contacts, then dry it).
- A way to **wet/dry the valve's own flood probe** (the probe on the valve itself).
- Ability to power-cycle the hub and the valve independently, and to drop the hub's WiFi.

**Tooling**
```bash
az extension add --name azure-iot          # once
# convenience vars (fill in):
HUB=<your-iot-hub-name>
DEV=<your-device-id>                        # = gateway id, e.g. GW-34B7DA6AAD54
```

**Two terminals**
- **T-MON** (leave running): `az iot hub monitor-events -n $HUB -d $DEV --properties anno sys --timeout 0`
- **T-SEND**: used to fire commands (below).
- **Serial**: `idf.py -p <PORT> monitor` — watch tags `RULES_ENGINE` (override logic) and `IOTHUB` (`Command:
  OVERRIDE_ENABLE`). Useful confirmation lines this firmware prints (all under `RULES_ENGINE`):
  - `override_enable: 24h override started remotely — RMLEAK cleared, valve opening`
  - `override_enable: no active incident to override`
  - `override_enable: valve flood probe wet — refusing`
  - `override_enable: valve unreachable after reconnect window`
  - `OVERRIDE WINDOW STARTED` / `OVERRIDE WINDOW REFRESHED` / `OVERRIDE WINDOW EXPIRED`

**Send helper** (envelope identical to what the backend sends):
```bash
send () { az iot device c2d-message send -n "$HUB" -d "$DEV" \
          --data "{\"schema\":\"eflostop.cmd\",\"ver\":1,\"id\":\"$1\",\"cmd\":\"$2\"}" ; }
# usage: send <correlation-id> <command>
```

**Reading state:** the hub sends a snapshot every 5 min; to get one on demand, just watch T-MON after any
command (a `valve_state_changed` event and the next snapshot confirm truth). Optionally lower
`snapshot_interval_s` to 60 via a Device Twin desired update for faster snapshots during testing.

---

## 1. Test matrix

Legend: **P** = pass criteria. Fill the result column at the bottom.

### T0 — Version / capability sanity
1. Watch T-MON for any telemetry envelope.
- **P:** `gateway.fw` is `"1.4.0"` in every envelope. *(This is the backend's capability gate.)*
- *App equivalent:* backend shows "Open with Override" only for hubs at ≥1.4.0.

### T1 — Happy path: remote override with a wet sensor  *(TC-012)*
1. Wet the **leak sensor**. Wait for auto-close: T-MON shows `leak_detected` then `auto_close`. Confirm a
   snapshot now has `valve.state:"closed"`, `valve.rmleak:true`, `override_active:false`.
2. `send ovr-en-1 override_enable`
- **P, in order on T-MON:**
  - `cmd_ack` `{id:"ovr-en-1", cmd:"override_enable", status:"ok"}`
  - `water_access_override_enabled` `{trigger:"c2d_command", expires_ts:<~now+86400>, remaining_s:86400}`
  - `valve_state_changed` `{valve_state:"open", rmleak:false}`
  - next snapshot: `override_active:true`, `override_remaining_s:~86400`, `valve.state:"open"`, `valve.rmleak:false`
  - serial: `... 24h override started remotely ...`
- *App equivalent:* tap **Open with 24 h Override** → banner appears (from the event) + countdown (from
  `remaining_s`) → valve card flips to **Open** when `valve_state_changed` arrives.
- Leave the sensor wet for T8; otherwise dry it and `send rst leak_reset` to reset between tests.

### T2 — Reject: nothing to override  *(TC-N07a)*
1. Ensure **no** leak/incident and valve **not** locked (dry sensor; `leak_reset` if needed;
   snapshot `valve.rmleak:false`).
2. `send ovr-en-2 override_enable`
- **P:** `cmd_ack status:"error"`, `error.detail` exactly:
  `No active leak to override. Use the normal Open Valve control.` No override event. Valve unchanged.
- *App equivalent:* the override action isn't even offered (it appears only when `valve.rmleak=true`); if
  forced, the user sees this toast.

### T3 — Reject: water at the valve  *(TC-N07b)*
1. Create an incident (wet **leak sensor** → auto-close → `valve.rmleak:true`), **then** also wet the
   **valve's own flood probe**.
2. `send ovr-en-3 override_enable`
- **P:** `cmd_ack status:"error"`, detail exactly:
  `Water detected at the valve. It can't be opened remotely until the valve area is dry.`
  No override event; valve stays closed. Serial: `... valve flood probe wet — refusing`.
- **Parity check:** now physically long-press the valve button — it must **also refuse to open** (chirps only)
  while its probe is wet. Confirms the floor is identical for button and app.
- Dry the valve probe before continuing.

### T4 — Reject: valve unreachable  *(TC-N07c)*
1. Create an incident (wet sensor → auto-close). Then **power off the valve**.
2. `send ovr-en-4 override_enable`
- **P:** within ~12 s, `cmd_ack status:"error"`, detail exactly:
  `The valve isn't responding. Check its power and connection, then try again.`
  Serial: `... reconnecting (<=10000ms)` then `... valve unreachable after reconnect window`. **No** override
  event (window did NOT start — it starts at execution, not receipt).
- Power the valve back on; let it reconnect.

### T5 — Reject: not provisioned (optional)
1. Only if convenient: on an unprovisioned hub (or after `decommission` valve), `send ovr-en-5 override_enable`.
- **P:** `cmd_ack status:"error"`, detail exactly: `No valve is set up for this hub.`
- Re-provision afterwards.

### T6 — Idempotent refresh
1. With a window already active (after T1), `send ovr-en-6 override_enable` again.
- **P:** another `cmd_ack ok` **and a fresh** `water_access_override_enabled` with a **new** `expires_ts`
  (~now+86400) and `remaining_s:86400`. Serial: `OVERRIDE WINDOW REFRESHED`.
- *App equivalent:* re-tapping extends the window to a fresh 24 h; banner countdown resets.

### T7 — Parity: physical button vs `override_enable`  *(mandatory equivalence)*
Run the same scenario two ways and compare.
1. **Button run:** wet sensor → auto-close (locked). **Long-press the valve button.** Capture from T-MON +
   next snapshot: the `water_access_override_enabled` `trigger`, `override_active`, `override_remaining_s`,
   `valve.state`, `valve.rmleak`. `override_cancel`, dry sensor, `leak_reset` to reset.
2. **App run:** wet sensor → auto-close (locked). `send par override_enable`. Capture the same fields.
- **P:** every captured field is **equivalent** (valve `open`, `rmleak:false`, `override_active:true`,
  `override_remaining_s ≈ 86400`) — the **only** difference is `trigger` (`"button"` vs `"c2d_command"`).

### T8 — Window blocks auto-close (leaks still reported)
1. With a remote window active (T1) and the sensor **still wet**, wait ~1 min.
- **P:** T-MON shows `auto_close_blocked_override` `{source_type, sensor_id, override_remaining_s}`,
  rate-limited to ~1/min; the valve **stays open** (`valve.state:"open"` in snapshots); no `auto_close`.
- *App equivalent:* leak alerts still arrive; the override banner stays; water stays on.

### T9 — `override_cancel` during a remote window
1. With a remote window active and the sensor **still wet**, `send ovr-cn override_cancel`.
- **P:** `cmd_ack ok`; `auto_close_reenabled` `{previous_remaining_s, reason:"c2d_command"}`; because a leak is
  active, the valve **closes again** (`auto_close` + `valve_state_changed{closed, rmleak:true}`);
  snapshot `override_active:false`.
- 1b. Repeat but with the sensor **dry** first: cancel → `auto_close_reenabled`, valve **stays open**
  (`override_active:false`, `valve.state:"open"`). *(Confirms cancel ≠ close when no leak.)*

### T10 — Reboot mid-window, network UP (persistence)
1. Start a remote window (T1). Note `override_remaining_s`. **Power-cycle the hub** (WiFi available).
- **P:** after reconnect, the first snapshot has `override_active:true` and `override_remaining_s` ≈ (previous −
  reboot time). Serial on boot: `NVS: restored override window ...`.

### T11 — Reboot mid-window, NO network (clock-safe hold)
*(Use the serial console — no telemetry while offline.)*
1. Start a remote window. **Drop the hub's WiFi** (kill the AP / unplug router), then **power-cycle the hub**
   so it boots with no network and no SNTP.
- **P (serial):** boot log shows the override restored and **held** (not expired) while time is unsynced —
  `NVS: restored override window` and **no** `OVERRIDE WINDOW EXPIRED` while the clock is invalid.
2. Restore WiFi; let SNTP sync.
- **P:** if real time is still before expiry → window continues; if you used the shortened build (T12) and time
  is now past expiry → `OVERRIDE WINDOW EXPIRED` fires on the first synced tick. *(The window never expires on a
  bad clock — it waits for proof.)*

### T12 — Window expiry re-arms auto-close (shortened build)
*Build a debug image with a short window:* in `main/CMakeLists.txt` add
`target_compile_definitions(${COMPONENT_LIB} PRIVATE OVERRIDE_WINDOW_DURATION_S=120)`, rebuild, flash. (Remove
after testing.)
1. Start a remote window with the sensor **still wet**. Wait ~120 s.
- **P:** `water_access_override_expired` `{auto_close_resumed:true, active_leak_count:≥1}`, immediately followed
  by `auto_close` + `valve_state_changed{closed, rmleak:true}` (protection resumes). Serial: `OVERRIDE WINDOW
  EXPIRED: auto-close re-enabled`.
2. Repeat with the sensor **dry** at expiry → `water_access_override_expired{auto_close_resumed:false}`, valve
   stays open.
- **Revert the CMakeLists change before shipping.**

### T13 — Offline event buffering (≤512 B)
1. Drop the hub's WiFi. **Long-press the valve button** to start a physical override (the hub is offline so it
   can't receive a C2D — use the button to generate the event). 
2. Restore WiFi.
- **P:** on reconnect, the buffered `water_access_override_enabled` is replayed **intact** (valid JSON, all
  fields present, `trigger:"button"`) — confirming the enriched event fits the 512-byte offline cap.

### TC-N10 — leak_reset interlock guard (re-flash required)
*Closes the bench-found hole where `leak_reset` + `valve_open` opened the valve during a live remote leak.*
1. Wet the BLE sensor and **keep it wet** → `auto_close` → valve closed, `rmleak:true`.
2. Send `leak_reset`:
```powershell
$b='{"schema":"eflostop.cmd","ver":1,"id":"rst-guard","cmd":"leak_reset"}'; az iot device c2d-message send -n wd-core-iothub-poc -d GW-50787D0E28CC --data $b
```
- **P:** `cmd_ack status:"error"`, `error.detail:"A leak is still active. Fix the leak first, or use override to open the valve during a leak."` Serial: `LEAK_RESET refused — N leak source(s) still active`.
3. Now send `valve_open`:
```powershell
$b='{"schema":"eflostop.cmd","ver":1,"id":"open-guard","cmd":"valve_open"}'; az iot device c2d-message send -n wd-core-iothub-poc -d GW-50787D0E28CC --data $b
```
- **P:** the **hub rejects it up front**: `cmd_ack status:"error"`, `error.detail:"Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."` Nothing is sent to the valve, so there is **no `valve_state_changed` transient** — the valve stays `closed`/`rmleak:true`. Serial: `VALVE_OPEN refused — valve RMLEAK is asserted`. *(Before the hub-side guard this path forwarded the open and the valve briefly opened then re-closed via its own RMLEAK interlock — see the TC-N10 capture 2026-06-24, 10:22:47–48.)*
4. Now **dry** the sensor, wait for `leak_cleared`, then `leak_reset` again → **P:** `cmd_ack ok` + `rmleak_cleared`; a follow-up `valve_open` now opens. *(Confirms the legit after-the-leak recovery still works.)*

### T14 — OPEN-VERIFY-1: unknown command on shipped fw 1.3.0  *(rollout safety — run on an OLD hub)*
1. On a hub still running **released fw 1.3.0**, `send verify-unknown override_enable`.
- **P (record the actual result):** either a `cmd_ack {status:"error", error:{detail:"unknown command"}}`
  within ~30 s **or** no ack at all. **Write down which.** If it's the error ack, the backend can use it as a
  secondary capability confirmation; if it's silent, capability detection relies solely on `gateway.fw < 1.4.0`.
  Either way the primary gate (`gateway.fw`) is unaffected.

---

## 2. Results log

| Test | Result (pass/fail) | Notes / captured JSON |
|---|---|---|
| T0 fw version | PASS — `gateway.fw:"1.4.1"` (lifecycle + snapshot, 2026-06-24) | ≥1.4.0 capability gate satisfied |
| T1 happy path | | |
| T2 no_incident | | |
| T3 valve flood (+ button parity) | | |
| T4 valve disconnected | | |
| T5 not provisioned (opt) | | |
| T6 idempotent refresh | | |
| T7 button vs app parity | | |
| T8 blocks auto-close | | |
| T9 override_cancel | | |
| T10 reboot (network up) | | |
| T11 reboot (no network) | | |
| T12 expiry re-arm (short build) | | |
| T13 offline buffering | | |
| T14 unknown cmd on 1.3.0 | | |
| TC-N10 leak_reset + open guard | PASS (2026-06-24, fw 1.4.1) | Step 2: `leak_reset` refused while wet (exact error). Step 3: `valve_set_state:"open"` rejected at the hub with the RMLEAK error — **no `valve_state_changed` transient**. Step 4: dry recovery opens. Captures: 1.4.0 run showed the transient (10:22:47–48); 1.4.1 run clean (10:54:30). |

---

## 3. When the Watts app is available
Every test above is exactly what the backend will trigger; to validate through the **app**, substitute:
- "`send ovr-en-* override_enable`" → tap **"Open with 24 h Override"** (visible only when `valve.rmleak=true`),
  pass the double-confirm.
- Expected `cmd_ack ok` → spinner clears, **override banner + countdown** appear (banner from the event, valve
  card flips to Open on `valve_state_changed`).
- Expected `cmd_ack error` → the verbatim `error.detail` shown as a toast (e.g. the `valve_flood_active` copy);
  UI unchanged.
- `override_cancel` → tap **Cancel Override** on the banner.
The redundancy is the headline: a physical button press (T7 button run) and the app action (T7 app run) must be
indistinguishable in the app except for the history label ("opened at the device" vs "opened from the app").
