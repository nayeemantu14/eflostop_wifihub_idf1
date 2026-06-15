# FEATURE TRACKER — Remote App-Initiated Water-Access Override

> Continuity file so a fresh chat can resume without re-reading everything. Update at every gate/phase boundary.

## What we're building
A C2D command path that lets the **mobile app open the shut-off valve during an active leak incident**
and start the **same 24-hour water-access override window** that today only the physical valve button can
start. Full telemetry so the app's existing override banner / countdown / cancel work for the remote case.

## Branch
`feature/remote-water-access-override` (hub repo). Valve repo: `feature/remote-water-access-override` **only if** a valve change is approved (Gate 1 says none is required).

## Repos / key paths
- Hub (ESP32-S3, ESP-IDF v5.5.1): `c:\Work\Projects\EfloStop 2\Firmware\Production\eFloStop_WiFiHub_idf1`
- Valve (STM32WB): `C:\Work\Projects\ST Workspace\DK-Servo_Motor`
- Sensor (STM32WBA): `C:\Work\Projects\ST Workspace\LR_BLE_Leak Sensor\BLE_HR_P2PServer`
- SRS docx: `C:\Work\Projects\EfloStop 2\Documents\eFloStop_2_App_Requirements_Watts_Digital_Update-V2.docx`
- Figma pdf: `C:\Work\Projects\EfloStop 2\Documents\Figma Designs\🟢 WATTS HOME designs.pdf`
- Converted text cache: `%TEMP%\eflostop_srs\SRS.md`, `figma.txt` (regen via python-docx / PyMuPDF; pandoc/pdftotext NOT installed)

## Deliverables (status)
| File | Purpose | Status |
|---|---|---|
| `docs/remote_override/FINDINGS.md` | Gate 1 discovery (cited) | ✅ DONE |
| `docs/remote_override/FEATURE_TRACKER.md` | this file | ✅ live |
| `docs/remote_override/DECISIONS.md` | Gate 2 Q&A + decision record | ⏳ after answers |
| `docs/remote_override/PLAN.md` | Gate 3 phased plan | ⏳ |
| `docs/remote_override/WATTS_DIGITAL_SPEC.md` | external handover (NO LoRa) | ✅ DONE (lean) |
| `docs/remote_override/TEST_PLAN.md` | bench procedures | ✅ DONE |

## Gate status
- **GATE 1 — Discovery:** ✅ COMPLETE. `FINDINGS.md`.
- **GATE 2 — Clarification questions:** ✅ ANSWERED & RECORDED in `DECISIONS.md` (command renamed `override_enable`; +9 modifications; +Gate 3 review amendments).
- **GATE 3 — Phased plan:** 🔵 PRESENTED for approval. `PLAN.md` written, both sub-agent reviews attached verbatim. **Awaiting per-phase approval. Phase 3 (valve FW) dropped.** No firmware touched.
- **PHASE 1 — Contract freeze + Watts spec:** ✅ `WATTS_DIGITAL_SPEC.md` written (lean, LoRa-clean, redundancy-with-button framing). OPEN-VERIFY-1 still pending (user-run; capability story uses `gateway.fw≥1.4.0` as primary gate so it doesn't block).
- **PHASE 2 — Hub firmware:** ✅ IMPLEMENTED (5 files, +152/−6). **Not yet built/flashed/bench-tested** — user does that (assistant cannot run idf.py). Not committed (awaiting user review).
- **PHASE 3 — Docs/handover (SRS inserts, TEST_PLAN, evidence):** ⏳ pending after bench validation.

## Phase 2 firmware change set (implemented, uncommitted)
- `main/telemetry/telemetry_v2.h`: `TELEMETRY_FW_VERSION` `1.3.0`→`1.4.0` (capability signal).
- `main/commands/c2d_commands.h`: `#define C2D_CMD_OVERRIDE_ENABLE "override_enable"`.
- `main/rules_engine/rules_engine.h`: `override_enable_result_t` enum + `rules_engine_enable_override_remote()` decl.
- `main/rules_engine/rules_engine.c`: `start_override_window(const char *trigger)` (+2 callers pass `"button"`);
  event fields now `{trigger, expires_ts, remaining_s}` (dropped `expiry_epoch`/`duration_h`); `#ifndef`
  duration guard (`-D OVERRIDE_WINDOW_DURATION_S=<s>` for bench); new `rules_engine_enable_override_remote()`
  (preconditions→ window→clear RMLEAK→open; bounded ≤10s reconnect).
- `main/iothub/app_iothub.c`: `override_enable` dispatch branch → maps result enum to the 5 frozen `error.detail` strings.
- Known minor transient (noted for TEST_PLAN): if valve was disconnected WITH active leaks and reconnects during
  the ≤10s wait, `on_valve_connected` Pri-1 may briefly close+RMLEAK before the handler opens; end-state is
  correct (open, window active). Acceptable for MVP.

## Bench-test hardening (2026-06-12) — leak_reset interlock guard (implemented, uncommitted)
Found during bench testing: `leak_reset` + `valve_open` with a remote sensor still wet opened the valve,
bypassing the override window. **Fix:** `leak_reset` now refuses while any leak source is wet
(`g_active_leak_count > 0`). `rules_engine_reset_leak_incident()` returns `bool`; dispatcher emits cmd_ack
error `"A leak is still active. Fix the leak first, or use override to open the valve during a leak."`. Guarding
leak_reset alone closes the hole (RMLEAK stays set → valve blocks the open). Files: rules_engine.c/.h,
app_iothub.c. See DECISIONS.md §"Bench-test hardening". **Needs reflash + TC-N10 bench re-test.** Spec note for
Phase 3: add the error row to SRS §4.4.1/§5.3.

## Bench test progress (T0–T14, against the as-built firmware)
PASSED: T0 (fw 1.4.0), T1 (happy path), T2 (no_incident), T3 (valve_flood_active + valve_flood_detected),
T4 (valve_disconnected), T6 (idempotent refresh), T9 (override_cancel wet→close / dry→stay-open), snapshot
override_active/remaining, T10 (reboot persistence, informal). REMAINING: T7 (button↔app parity), TC-N10
(leak_reset guard — needs the reflash above), optional T11 (no-network reboot), T12 (shortened-expiry).

## SCOPE — LEAN (user directive 2026-06-12)
Ship **only** the override feature through the app. SRS edit trimmed to ~1 page: §5.3 command row, compact
§4.4.3 (APP-FR-043..045), §5.2.5 single additive `trigger` note (NO D1–D6 cleanup), one notification row
(APP-FR-116), §2.7 one-liner, TC-012 + TC-N07, §11 row. **DEFERRED:** D1–D6 reconciliation, expanded
notifications/audit (household/cancel/12h re-prompt), richer per-error dialogs. Firmware (Phase 2) unchanged
(was already minimal). See DECISIONS.md §SCOPE — LEAN.

## Condition A (error signalling) — RESOLVED to option (a)
Per user amendment: **no `error.token` field; §5.2.7 cmd_ack contract unchanged.** `override_enable` failures use
**5 frozen human-readable `error.detail` strings** (DECISIONS A3), passed via the existing `error_msg` arg,
rendered verbatim by the app (APP-FR-054 untouched). Localized error copy = future token revision, deferred not built.

## ⚠️ OPEN-VERIFY-1 (must bench-confirm before relying on capability fallback)
Unknown-command `cmd_ack{error,detail:"unknown command"}` on fw 1.3.0 is **inferred from code, NOT bench-verified**.
The whole backend capability-fallback rests on it. User runs the bench step (Phase 1) — assistant cannot flash hardware.

## Hard constraints (carry into every phase)
1. **LoRa is CONFIDENTIAL** — never in any Watts deliverable / diagram / "discrepancy". Internal notes only.
2. **Additive contract changes only** — no renaming/removing existing events/fields/commands.
3. **No regression of the physical button path** (must work hub-offline / BLE-degraded).
4. **Full files, no placeholders** when implementing.
5. Hub = ESP-IDF idioms; Valve = respect CubeMX `USER CODE BEGIN/END`.
6. Commit only at phase boundaries **after explicit approval**; never push.

## Ground-truth facts established in Gate 1 (with cites — see FINDINGS.md for detail)
- Add a C2D cmd = `#define` in `main/commands/c2d_commands.h` + one `else if` in `app_iothub.c :: handle_c2d_command()` (~L546). cmd_ack auto-emitted by tail (L609-L612).
- Old hub → unknown cmd in valid envelope → `cmd_ack{status:error, detail:"unknown command"}` = clean capability signal. Unparseable = silent drop.
- `cmd_ack` suppressed if SNTP not synced (`build_envelope` returns NULL). Offline events buffered (512B cap, 16 entries).
- **Override window machinery** all in `main/rules_engine/rules_engine.c`:
  - `start_override_window()` (static, mutex) L180-L210 — sets ACTIVE, `expiry=now+86400`, NVS save, emits `water_access_override_enabled {expiry_epoch,duration_h:24}`. **No trigger arg today.**
  - NVS ns `"rules_eng"` keys `ovr_state`(u8)/`ovr_expiry`(u32 epoch)/`incident`(u8).
  - Physical-override detect: `rules_engine_tick()` Check 2 (L994-L1029, online) + `rules_engine_on_valve_connected()` Pri 2 (L857-L878, reboot).
  - `rules_engine_cancel_override()` L674-L756; `rules_engine_reset_leak_incident()` L618-L672.
  - snapshot `override_active`/`override_remaining_s` built in `telemetry_v2_publish_snapshot()` L497-L505 → remote window surfaces **for free**.
- **Valve autonomy = YES** (own flood probe PA12, critical battery ≤10%, boot reconcile). NOT on disconnect/low-batt/timer.
- **RMLEAK set on valve BLOCKS hub open** (`app_main.c processValveBLEWrite L267-L281`) → remote path must `ble_valve_set_rmleak(false)` BEFORE `ble_valve_open()`.
- **Valve own flood probe is an absolute floor** — override can't keep water on if PA12 wet.
- Valve BLE API: `ble_valve_open/close/set_rmleak/connect/is_connected/is_ready` in `app_ble_valve.c`.
- Valve FW DIS `0x2A26` = **"2.2.0"** (`custom_stm.c L61`).
- **No sensor FW change required** (pure non-connectable advertiser).
- **Valve FW change NOT strictly required** — hub replicates button via existing GATT writes + hub window. (Pending Gate 2 confirm Q-H.)

## Likely implementation shape (DRAFT — not approved)
New hub C2D command `water_access_override` →
1. `rules_engine_start_remote_override()` (new public fn): under mutex, `g_leak_incident_active=false`+save,
   `start_override_window("c2d_command")`, then `ble_valve_set_rmleak(false)` → `ble_valve_open()`.
2. `start_override_window()` gains a `const char *trigger` param; adds `"trigger"` to the event; 3 call sites updated
   (tick→"button", on_valve_connected→"button", remote→"c2d_command").
3. Dispatcher branch + `#define C2D_CMD_WATER_ACCESS_OVERRIDE`.
4. cmd_ack ok = accepted; snapshot/valve_state_changed carry truth.
Files touched (hub): `c2d_commands.h`, `iothub/app_iothub.c`, `rules_engine/rules_engine.{c,h}`. No valve/sensor change.

## Pending decisions (Gate 2 — answers go to DECISIONS.md)
Q1 command shape · Q2 semantic parity · Q3 preconditions/errors · Q4 duration · Q5 telemetry trigger field ·
Q6 notification policy · Q7 app UX confirm friction · Q8 valve FW phasing · Q9 rollout/versioning · Q10 APP-FR/TC numbering.
Plus Gate-1-surfaced: flood-floor truth handling (Q-C), cmd_ack accepted-vs-confirmed semantics.

## Test anchors (for TEST_PLAN later)
Remote override w/ wet sensor; override while valve BLE-disconnected; hub reboot mid-window (persistence + remaining_s);
window expiry re-arms auto-close (shorten `OVERRIDE_WINDOW_DURATION_S` for debug); `override_cancel` interaction;
physical press during app-window & vice-versa; idempotency; unknown-command on prod FW; offline buffering under 512B.
Bench: `az iot hub invoke-device-method`/C2D send + `az iot hub monitor-events`.
