# FINDINGS — Remote App-Initiated Water-Access Override (Gate 1 Discovery)

**Feature:** Let the mobile app open the shut-off valve during an active leak incident, starting the
same 24-hour water-access override window that today is reachable only by physically pressing the
button on the valve.

**Status:** Gate 1 (discovery) complete. No design proposals here — current-state only.
**Author:** Lead embedded firmware engineer.
**Date:** 2026-06-12.

> Every claim below cites `path :: function() :: Llines`. Items I could not find are marked **NOT
> LOCATED**. Firmware behavior is ground truth; where the SRS disagrees, the firmware wins and the
> discrepancy is recorded in §13.

> ⚠️ **CONFIDENTIAL — internal only.** This file may mention LoRa (SX1262) where genuinely relevant.
> **Nothing LoRa-related may appear in any Watts Digital deliverable** (`WATTS_DIGITAL_SPEC.md`,
> event tables, diagrams). See §13 D9.

---

## 0. Sources read

| Source | Path | Coverage |
|---|---|---|
| App SRS (contract ground truth) | `…/Documents/eFloStop_2_App_Requirements_Watts_Digital_Update-V2.docx` | Full (converted via python-docx) |
| Figma designs | `…/Documents/Figma Designs/🟢 WATTS HOME designs.pdf` | Full 109 pages (converted via PyMuPDF) |
| Hub C2D reference | `C2D_COMMANDS.md` (repo root) | Full |
| Hub firmware | `main/` (ESP-IDF) | `rules_engine.c`, `app_iothub.c`, `c2d_commands.c/.h`, `app_ble_valve.c/.h`, `telemetry_v2.c/.h`, `offline_buffer.c/.h` read in full |
| Valve firmware | `…/ST Workspace/DK-Servo_Motor` | `app_main.c`, `custom_app.c`, `custom_stm.c`, `state_persist.c`, `main.c`, `app_entry.c`, `Servo.c`, `Alerts.c`, `app_conf.h` |
| Sensor firmware | `…/ST Workspace/LR_BLE_Leak Sensor/BLE_HR_P2PServer` | `app_leak_detection.c`, `app_battery.c`, `app_ble.c` |

Tooling note: `pandoc`/`pdftotext` are not installed on this machine; `python-docx` + `PyMuPDF`
(`fitz`) were used to convert the binary docs to text under `%TEMP%\eflostop_srs\`.

---

## 1. Physical button: detection & debounce (Q1)

Three-stage state machine **EXTI ISR → HW_TS timer ISR → sequencer task**, button on **PC13** (active-low).

- EXTI wiring: `Core/Src/main.c :: MX_GPIO_Init() :: L605-L609` (PC13 `IT_RISING_FALLING`, pull-up), NVIC `EXTI15_10_IRQn` prio 0.
- ISR: `STM32_WPAN/App/app_main.c :: HAL_GPIO_EXTI_Callback() :: L83-L110` — falling edge starts a 50 ms debounce timer; rising edge while armed records a short press.
- Debounce + long/short discrimination: `app_main.c :: BUTTON_timer_ISR() :: L120-L152`. `BUTTON_DEBOUNCING` (50 ms, `DEBOUNCE_DURATION`, `app_main.h L41`) → if still held, → `BUTTON_WAITING_LONG_PRESS` and arm `LONG_PRESS_DURATION`. When that fires and the pin is still LOW → `UTIL_SEQ_SetTask(CFG_TASK_PROC_LONG_PRESS)` (L144).
- **Long press = held ≥ `LONG_PRESS_DURATION` = 1 s** (`app_main.h L39`; the code comment says "2 seconds" but the value is `1000*1000/CFG_TS_TICK_VAL` = 1 s — doc/code mismatch).
- Short press → `processShortPress() :: L191-L199` → battery read only (does **not** touch the valve).
- Long press → `processLongPress() :: L202-L252` (deferred task context).

---

## 2. What the valve does locally on a post-auto-close press (Q1) — the canonical behavior to replicate

`app_main.c :: processLongPress() :: L202-L252`. When valve is **closed**, no local flood, battery not Critical, **and** `remote_leak_active` is set (L211-L234):

```c
if (remote_leak_active) {
    remote_leak_active = 0;                 // (b) clear local latch
    state_persist_save_rmleak();            //     persist to BKP0R
    if (ble_status == connected)
        Custom_STM_App_Update_Char(CUSTOM_STM_REMOTE_LEAK, &rmleak_val); // (c) RMLEAK=0 NOTIFY
    clearLeakAlert();                       // (d) double-chirp
}
valve_state = valve_open;
servo_open(&htim2, TIM_CHANNEL_1);          // (a) drive motor OPEN
```

Then (L243-L251): VALVESTATE NOTIFY (=open), `state_persist_save_valve()`.

| Effect | Done? | Cite |
|---|---|---|
| Drive motor open | **YES** | `app_main.c L233` |
| Clear local `remote_leak_active` latch (+persist) | **YES** | `app_main.c L222-L223` |
| Emit RMLEAK=0 NOTIFY over BLE | **YES** (only if connected) | `app_main.c L224-L228` |
| Double-chirp ("override accepted, leak unresolved") | **YES** | `Alerts.c :: clearLeakAlert() L31-L46` |

**Local safety floor:** if the valve's **own** flood probe is wet (`leak_state == leak`), the long-press
**refuses to open** and only chirps (`app_main.c L235-L241`). See §6.

---

## 3. How the hub learns a *physical* open happened (Q2)

The valve state and RMLEAK live on **two separate 128-bit GATT characteristics**, both **NOTIFY** (CCCD `0x0001`):

- Valve-state char `h_valve_char` in `UUID_SVC_VALVE` — `app_ble_valve.c L61-L64, L1022`.
- RMLEAK char `h_rmleak_char` inside the **FLOOD service** — `app_ble_valve.c L66-L73, L947`.
- CCCD subscribe: `app_ble_valve.c :: setup_next_step() L706-L748`; payload `{0x01,0x00}` at `L547-L549`.

**Chain (button → cloud):**

1. Valve `servo_open()` + two NOTIFYs (valve-state=open, RMLEAK=0) — §2.
2. Hub `on_notify()` (`app_ble_valve.c L437-L490`) dispatches by `attr_handle`: caches `g_val_state` (L455) / `g_val_rmleak` (L471); on delta (and not during setup) enqueues `BLE_UPD_STATE` / `BLE_UPD_RMLEAK` via `notify_hub_update()` (L420-L435) onto `ble_update_queue`.
3. The iothub loop drains the queue (`app_iothub.c`, QueueSet incl. `ble_update_queue`) and **calls `rules_engine_tick()` every iteration** (`app_iothub.c L988`). `BLE_UPD_STATE` → `telemetry_v2_publish_valve_event("valve_state_changed")` (`app_iothub.c L1128-L1129`). `BLE_UPD_RMLEAK` carries **no direct telemetry** — comment `// BLE_UPD_RMLEAK: handled by rules engine events` (`app_iothub.c L1132`).
4. **The 1→0 detection is a poll, not an event handler.** `rules_engine_tick()` **Check 2** (`rules_engine.c L994-L1029`) reads the *cached* RMLEAK (`ble_valve_get_rmleak_state()`); when override INACTIVE, valve ready, incident latched, grace elapsed, and cached RMLEAK == false → clears incident latch and calls `start_override_window()` (L1017-L1028) → emits `water_access_override_enabled`.

> The RMLEAK queue event only *wakes* the loop so the next `rules_engine_tick()` runs promptly; the
> decision is made by polling the cache. Tick runs every loop iteration regardless, so the wake is sufficient.

**Offline/reboot catch:** if the hub was rebooting at the NOTIFY moment, `rules_engine_on_valve_connected()` **Priority 2** infers the override on reconnect from `hub_incident && valve_state==OPEN && valve_rmleak==CLEAR` (`rules_engine.c L857-L878`).

Mermaid sequence in §12.

---

## 4. Where the hub starts / stores / expires the 24h window (Q3)

`main/rules_engine/rules_engine.c`:

| Aspect | Detail | Cite |
|---|---|---|
| Start fn | `start_override_window()` (static, mutex held): `g_override_state=ACTIVE`, `g_override_window_expiry = now + 86400`, NVS save, emits `water_access_override_enabled` | `L180-L210` |
| Duration | `OVERRIDE_WINDOW_DURATION_S = 24*60*60 = 86400` | `L25` |
| NVS namespace | `"rules_eng"` | `L27` |
| NVS keys | `"ovr_state"` (u8), `"ovr_expiry"` (u32 **absolute Unix epoch**, valid to 2038), `"incident"` (u8) | `L28-L33` |
| Remaining | `rules_engine_get_override_remaining_s()` → `expiry-now`; full duration if time unsynced; 0 if expired-unticked; -1 inactive | `L1052-L1072` |
| Expiry check | `rules_engine_tick()` expiry block, guarded by `now >= EPOCH_VALID_THRESHOLD (1704067200)` | `L908-L960` |
| Re-arm on expiry | If `g_active_leak_count>0` & auto-close on → immediate RMLEAK+close; else normal eval resumes | `L935-L956` |
| Expiry event | `water_access_override_expired` `{auto_close_resumed, active_leak_count}` | `L922-L931` |
| Boot restore | `override_load_from_nvs()` restores or clears-if-expired (no telemetry on boot-expire) | `L135-L178` |

---

## 5. Where auto-close is blocked during the window (Q4)

`rules_engine.c :: rules_engine_evaluate_leak() :: L439-L469`. Order inside the function:

1. `track_leak_source()` runs first, unconditionally (`L388`) — leak tracking continues during the window.
2. Incident latched **always**, even during override (`L426-L434`).
3. Trigger-mask / auto-close-enabled checks (`L410-L424`) gate *before* the block.
4. If `g_override_state == ACTIVE` → emit `auto_close_blocked_override` (rate-limited) and **`return` before** the close/RMLEAK writes at `L489-L518`.

- Event fields: `source_type`, `sensor_id`, and `override_remaining_s` (`L454-L465`).
- Cooldown `OVERRIDE_BLOCKED_COOLDOWN_MS = 60000` (1/min) gates **only** the telemetry, not the block (`L26, L441-L443`).

Leak detection + cloud eventing keep running; only the valve closure is suppressed. ✔ Confirmed.

---

## 6. CRITICAL — valve autonomy (Q6)

> Highest-priority discovery item. **VERDICT: the valve CAN close itself autonomously, and RMLEAK
> set on the valve BLOCKS a hub open command locally.** This reshapes the design (see §11).

Every `servo_close()` site in `app_main.c`:

| Trigger | Autonomous close? | Cite |
|---|---|---|
| **(a) Valve's own flood probe (PA12) wet** | **YES** — runtime + boot | `processLeak() L367-L372`; `checkValveStatus() L304-L315, L342-L345` |
| **(b) Critical battery (≤10%)** | **YES** | `readBatteryADC() L432-L440` |
| **(c) Boot reconciliation** re-closes if persisted-closed / leak / RMLEAK-set | YES (re-applies prior safety state) | `checkValveStatus() L336-L345` |
| (d) BLE link loss / disconnect | **NO** (only logs) | `custom_app.c :: CUSTOM_DISCON_HANDLE_EVT L273-L277` |
| (e) Low battery 11–20% | **NO** (beeps only) | `readBatteryADC() L420-L425` |
| (f) Watchdog / bare timer | **NO** directed close (IWDG → reset → reconcile) | `main.c MX_IWDG_Init L344-L366` |

**RMLEAK blocks a hub OPEN locally** — `app_main.c :: processValveBLEWrite() :: L267-L281`:

```c
if (valve_state == valve_open && leak_state == no_leak && app_batt.batt_state != Critical) {
    if (remote_leak_active) {                 // <-- RMLEAK interlock
        valve_state = valve_close;            // refuse open, force back closed
        customBLEValveState = (uint8_t)valve_close;
    } else { servo_open(...); }
}
```

Implications:
- A hub `valve_open` while the valve's `remote_leak_active` is set is **rejected** — the valve echoes back `closed`. **The hub must write RMLEAK=0 to the valve *before* opening.**
- Writing RMLEAK=1 over BLE while the valve is open **force-closes it** (`custom_app.c L151-L156`).
- The valve's **own flood probe is an absolute floor**: while PA12 is wet the valve refuses to open
  (long-press `L235-L241`, BLE write requires `leak_state==no_leak` `L267`) and `processLeak()` re-closes it.
  **No override (physical or remote) can keep water on if the valve itself is standing in water.**

---

## 7. Second trigger while a window is active (Q5)

- `start_override_window()` **unconditionally refreshes** to a fresh 24h (`now + 86400`); `prev_state`
  only selects the log line "STARTED" vs "REFRESHED" (`rules_engine.c L186-L198`).
- But both internal callers are guarded so they do **not** re-invoke while active: tick Check 2 requires
  `g_override_state == INACTIVE` (`L1000`); `on_valve_connected` Priority 0 returns early if active (`L803-L813`).
- Net effect today: a second physical press during the window is a practical no-op on the hub (valve already
  open, RMLEAK already 0, hub doesn't re-detect). The "refresh" path is reachable only by a caller that
  invokes `start_override_window()` without the inactive guard — which a new remote command would be.

---

## 8. Command & cmd_ack plumbing (Q7) + valve-disconnected behavior (Q11)

**Dispatch is a flat `strcmp` if/else chain** — no enum, no switch, no registration table.

`override_cancel` trace:
1. `app_iothub.c :: mqtt_event_handler() MQTT_EVENT_DATA :: L755-L776` → `handle_c2d_command(data,len)`.
2. `handle_c2d_command() :: L407-L615` → `c2d_command_parse()` (L410).
3. `c2d_commands.c :: c2d_command_parse() L23-L54` → `parse_envelope()` (`{`-prefixed) or `parse_legacy()`.
4. `parse_envelope() L68-L128` accepts `schema=="eflostop.cmd"` or `"eflostop.cmd.v1"`; extracts `cmd`, optional `id`, optional `payload`.
5. Dispatch branch `app_iothub.c L546-L552` → `rules_engine_cancel_override()`; sets `success`/`error_msg`.
6. Tail (`L609-L612`): `if (cmd.is_envelope || cmd.id[0]) telemetry_v2_publish_cmd_ack(id, cmd, success, error_msg);`

**To add a sibling command** (e.g. `water_access_override`):
- `c2d_commands.h`: add `#define C2D_CMD_WATER_ACCESS_OVERRIDE "water_access_override"` (next to L35-L44).
- `app_iothub.c handle_c2d_command()`: add one `else if` branch (after L546-L552) calling the new handler, setting `success`/`error_msg`. **No separate ack call** — the tail emits it.
- Legacy text alias: optional (`parse_legacy()` L258-L262 pattern). **Not required**; envelope-only commands already exist (`valve_set_state`, `set_hub_name`).

**cmd_ack** — `telemetry_v2.c :: telemetry_v2_publish_cmd_ack() L600-L623`:
`{event:"cmd_ack", id?, cmd, status:"ok"|"error", error?{code:<cmd>, detail:<msg>}}`. `error.code` is the
command name (not a distinct code). **Suppressed entirely if SNTP not synced** (`build_envelope()` returns
NULL, `L51-L56`). Offline → buffered (event type). `id` omitted if empty; legacy-text (no id, not envelope)
→ **no ack at all**.

**Unknown command (Q rollout):** a well-formed envelope with an unknown `cmd` → final `else` (`L603-L607`)
→ `cmd_ack` `status:error`, `detail:"unknown command"`. An **unparseable** payload → `c2d_command_parse()`
returns false → silent drop (local log only). → **An old hub that lacks this feature replies
`cmd_ack {status:"error", error:{code:"water_access_override", detail:"unknown command"}}`** — a clean,
detectable capability signal for the backend.

**valve_open path (Q11):** goes **straight to BLE, not the rules engine** — `app_iothub.c L421-L425`:
`ble_valve_connect(); ble_valve_open();`. When disconnected, `write_valve_command()` stashes a pending
command and starts a scan (`app_ble_valve.c L1496-L1503`); the function returns immediately and **`cmd_ack`
reports `ok` = "accepted/queued", not "physically opened."** There is no completion ack after the deferred
write. The remote override inherits this acceptance semantics unless we decide otherwise.

---

## 9. Telemetry & snapshot construction (Q8, Q9)

- Envelope: `telemetry_v2.c :: build_envelope() L41-L74` (cJSON; adds `schema/ts/gateway/type`).
  **Returns NULL if SNTP unsynced** (L51-L56).
- Rules events are written as a serialized JSON string into the **single** module-global
  `g_pending_telemetry`, drained by `rules_engine_take_pending_telemetry()` (`rules_engine.c L591-L604`)
  and wrapped by `telemetry_v2_publish_rules_event()` (`telemetry_v2.c L561-L579`, which parses the string
  and grafts it as `data`).
- **`water_access_override_enabled` is built cJSON-style inside `start_override_window()` (`L201-L209`)** —
  current fields **`event, expiry_epoch, duration_h:24`** (so the SRS "(none)" is already wrong — D1).
  Adding an additive `trigger`/`remaining_s`/`expires_ts` field = a few `cJSON_Add…` calls between L204-L205;
  to distinguish `button` vs `c2d_command` you must add a parameter to `start_override_window()` and update
  its callers.
- **Snapshot `override_active` / `override_remaining_s`** built in `telemetry_v2_publish_snapshot()` (`L497-L505`),
  querying `rules_engine_is_override_window_active()` + `rules_engine_get_override_remaining_s()`. **A
  remote-activated window flows into the snapshot for free** — the snapshot does not care how the window started.
- **512-byte offline cap** (`offline_buffer.h L12-L13`; enforced `offline_buffer_store() L77-L81`, **silent
  truncation**). `water_access_override_enabled` wrapped is ~230–280 bytes; +70 bytes of additive fields is
  safe (well under 512).
- ⚠ Single-slot `g_pending_telemetry`: a second rules event emitted before the iothub task drains the first
  **overwrites** it (pre-existing limitation). The remote path emits one event; drain cadence is every loop
  iteration. Low risk, noted.

---

## 10. RMLEAK semantics (Q10)

- **Both** a hub-side latch and a valve-side register:
  - Hub: `g_leak_incident_active` (`rules_engine.c L49`), persisted in NVS key `"incident"` (`L33, L104-L133`).
  - Valve register: cached `g_val_rmleak`, written by `ble_valve_set_rmleak(bool)`, read by `ble_valve_get_rmleak_state()` (`app_ble_valve.c L1902-L1911`); a real GATT char on the valve.
- On override **start**, both detection paths set `g_leak_incident_active=false` (+save) then call
  `start_override_window()` (`rules_engine.c L1022-L1027`, `L873-L877`). The valve RMLEAK register is **left
  cleared** (the button already cleared it; the rules engine does not re-assert).
- **Exact parity requirement for the remote path:** to match the button, the hub must (a) clear the valve
  RMLEAK register, (b) open the valve, (c) clear the hub incident latch, (d) start the window. The button does
  (a)+(b) on the valve; the rules-engine tick does (c)+(d). A remote command has to do **all four**.

---

## 11. Valve BLE control surface + disconnected behavior

`main/ble_valve/app_ble_valve.c` (non-blocking; each enqueues onto `ble_cmd_queue`):

| Function | Enqueues | GATT write | Notes |
|---|---|---|---|
| `ble_valve_open()` | `BLE_CMD_OPEN_VALVE` | `h_valve_char ← 1` | `L1800-L1804` |
| `ble_valve_close()` | `BLE_CMD_CLOSE_VALVE` | `h_valve_char ← 0` | `L1806-L1810` |
| `ble_valve_set_rmleak(bool)` | `BLE_CMD_SET/CLEAR_RMLEAK` | `h_rmleak_char ← 1/0` | `L1902-L1906`; silent cache update, no hub event |
| `ble_valve_connect()` | `BLE_CMD_CONNECT` | — (start scan) | `L1812-L1816` |
| `ble_valve_is_connected()` / `is_ready()` | — | — | readiness gates |
| `ble_valve_cancel_pending_close()` | — | — | clears a stale pending CLOSE/RMLEAK (`L1918-L1928`) |

- **Ordering rule (documented):** `set_rmleak()` **before** `close()` so the cached RMLEAK is true when the
  `valve_state_changed` event is built (`rules_engine.c L506-L518`). For the **open** direction the inverse
  matters for the *valve*: `set_rmleak(false)` must land **before** `open()` or the valve rejects the open (§6).
- Disconnected: command queued as pending + auto-reconnect scan; applied at end of setup
  (`apply_pending_*_cmd_if_any()`, `app_ble_valve.c L642-L691, L831-L832`). `rules_engine_on_valve_connected()`
  runs after setup via `BLE_UPD_CONNECTED` (`app_iothub.c L1029-L1031`).
- Passkey/bonding: `BLE_VALVE_FIXED_PASSKEY 222900` (`app_ble_valve.c L41`), Secure Connections + MITM + bonding (`L1708-L1717`).

---

## 12. Mermaid — physical button override path (current state)

```mermaid
sequenceDiagram
    autonumber
    actor U as User (at valve)
    participant V as Valve FW (STM32WB)
    participant B as Hub BLE bridge (app_ble_valve.c)
    participant R as Hub rules_engine.c
    participant I as Hub iothub task
    participant C as Azure IoT Hub
    participant A as App / Backend

    Note over V,R: Precondition: auto-close already fired → valve CLOSED, RMLEAK=1 (hub+valve)
    U->>V: Long press ≥1s (processLongPress)
    alt valve closed, local flood DRY, battery not Critical, remote_leak_active=1
        V->>V: remote_leak_active=0 + persist (BKP0R)
        V->>V: servo_open() + double-chirp
        V-->>B: NOTIFY valve_state=OPEN (h_valve_char)
        V-->>B: NOTIFY RMLEAK=0 (h_rmleak_char)
    else local flood WET
        V->>V: refuse open, chirp only (safety floor)
    end
    B->>B: on_notify(): cache g_val_state=1, g_val_rmleak=0; enqueue BLE_UPD_STATE/RMLEAK
    B->>I: ble_update_queue wakes loop
    I->>I: telemetry_v2_publish_valve_event("valve_state_changed")
    I->>R: rules_engine_tick() (every loop iteration)
    R->>B: ble_valve_get_rmleak_state() == false (Check 2, after grace)
    R->>R: g_leak_incident_active=false; start_override_window()
    R-->>I: g_pending_telemetry = water_access_override_enabled {expiry_epoch,duration_h:24}
    I->>C: D2C valve_state_changed (rmleak:false)
    I->>C: D2C water_access_override_enabled
    C->>A: events → app shows "24h Override Active" banner + countdown
    Note over R: For 24h: leaks still published as auto_close_blocked_override; auto-close suppressed
```

---

## 13. SRS ⇄ firmware discrepancies

| # | Topic | SRS says | Firmware does | Type | Action |
|---|---|---|---|---|---|
| D1 | `water_access_override_enabled` | "(none)" fields | emits `expiry_epoch`, `duration_h:24` | Additive drift | Document fields in spec; we will also add `trigger` |
| D2 | `water_access_override_expired` | "(none)" | emits `auto_close_resumed`, `active_leak_count` | Additive drift | Document |
| D3 | `auto_close_reenabled` | `reason` only | also `previous_remaining_s` | Additive drift | Document |
| D4 | `auto_close_blocked_override` | `source_type`, `sensor_id` | also `override_remaining_s` | Additive drift | Document |
| D5 | `auto_close` | `source_type, sensor_id, location` | also `rmleak_asserted:true`; reconnect variant adds `source_type:"reconnect"`, `active_leak_count` | Additive drift | Document |
| D6 | `rmleak_cleared` | "(none)" | optional `override_cancelled:true` | Additive drift | Document |
| D7 | Valve `fw_version` | example "2.1.0" | valve DIS `0x2A26` = **"2.2.0"** | Stale example | Note; bump on next valve release |
| D8 | §4.4.2 / event table | override = **physical button only**; `water_access_override_enabled` = "User physically opened valve" | feature adds an app-initiated trigger | Contract update | SRS §4.4.2 + §5.2.5 to be extended (Phase 4) |
| D9 | `trigger_mask` glossary | Bit 0 = BLE, Bit 2 = valve flood (Bit 1 omitted) | Bit 1 = **LoRa** exists (`RULES_TRIGGER_LORA`) | **CONFIDENTIAL** | Keep LoRa out of all Watts deliverables; never "fix" the SRS to mention it |
| D10 | `cmd_ack` for valve_open | implies state confirmation | "ok" = command **accepted/queued**, not physically opened | Semantics | Spec must state override ack = accepted; truth via snapshot |

---

## 14. Design-relevant facts (one-liners for Gate 2/3)

1. **Valve closes autonomously: YES** — own flood probe (PA12), critical battery (≤10%), boot reconciliation. NOT on disconnect / low-batt / bare timer.
2. **Valve's own flood probe is an absolute floor** — no override (physical or remote) opens the valve while it stands in water; `processLeak()` re-closes it.
3. **RMLEAK set on the valve blocks a hub open** — remote path **must** `set_rmleak(false)` **before** `open()`.
4. **24h window is purely hub-side** — valve has no window/timer; it only emits RMLEAK 1→0.
5. **Window state survives reboot** — NVS `rules_eng` {`ovr_state`,`ovr_expiry` epoch,`incident`}; snapshot recomputes remaining from epoch.
6. **`start_override_window()` already refreshes to a fresh 24h** if called while active (callers currently guard it).
7. **Adding a C2D command = 1 `#define` + 1 `else if` branch**; cmd_ack auto-emitted; old firmware returns `unknown command` error ack (clean capability signal).
8. **Remote-activated window surfaces in the snapshot for free** (`override_active`/`override_remaining_s`).
9. **`water_access_override_enabled` is cJSON-built** — additive `trigger` field is safe; needs a param on `start_override_window()` + 3 call sites.
10. **cmd_ack semantics inherited from valve_open** — "ok" means accepted/queued; the valve may still refuse to physically open (flood floor / disconnected).
11. **No leak-sensor firmware change required** — pure non-connectable advertiser (Company 0x0030; mfg bytes leak+battery+fwver); confirmed by `app_leak_detection.c` and a grep that found zero override/RMLEAK/valve references.
12. **Valve firmware change is NOT strictly required** — the hub can replicate the button entirely via existing GATT writes (clear RMLEAK, open) + hub-side window. The autonomous closes are *desirable* safety floors to preserve. (Confirm in Gate 2.)

---

## 15. Open questions surfaced by Gate 1 (feed Gate 2)

- **Q-A Command shape:** dedicated `water_access_override` vs `valve_open`/`valve_set_state` + `"override":true` flag. (Old-firmware behavior + auditability favor a dedicated command.)
- **Q-B Preconditions:** valid only during an active incident/RMLEAK, or also pre-emptive (valve already open, no incident)? What cmd_ack errors per rejected case?
- **Q-C Flood-floor truth:** if the valve's own flood probe is wet, the override cannot keep water on. Should the command pre-check and warn, or just ack "accepted" and let the snapshot show the truth?
- **Q-D Duration:** fixed 24h vs optional clamped `duration_s`.
- **Q-E Telemetry:** add `"trigger":"button"|"c2d_command"` (additive) to `water_access_override_enabled`; reflect in cmd_ack?
- **Q-F Notification policy:** should the backend push-notify on remote override activation (safety/audit)?
- **Q-G App UX:** single confirm vs double-confirm (Danger-Zone style) with explicit "leak still active" warning; reuse existing override banner/countdown/cancel (APP-FR-040..042).
- **Q-H Valve FW:** confirm no valve change ships in this release (autonomy is a desirable floor) — phase it.
- **Q-I Rollout/versioning:** backend capability detection — min `gateway.fw` and/or a twin reported flag; what the app does against an older hub.
- **Q-J Numbering:** confirm next free `APP-FR-xxx` / `TC-xxx` blocks for the SRS-ready inserts.

— End of FINDINGS (Gate 1) —
