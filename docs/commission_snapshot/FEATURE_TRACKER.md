# FEATURE TRACKER — Fast First Snapshot After Commissioning (+ DPS twin tags)

> Continuity file so a fresh chat can resume without re-reading everything. Update at every milestone.

## What we're building
Two related cloud/commissioning changes:
- **Change 1 (firmware):** make the **first snapshot after commissioning arrive within 120 s** (ideally
  seconds) instead of waiting up to the 5-min periodic timer — a bad setup experience.
- **Change 2 (Azure-side, NO firmware):** apply device-twin **tags** (`DeviceBrand` / `DeviceModel` /
  `DeviceType`) at DPS provisioning time, mirroring the thermostat convention.

## Branch / version
`feature/fast-commission-snapshot`, **FW 1.4.4**. Built on top of `master` (LED 1.4.2 + WiFi-only-reset 1.4.3
merged in via `1380d5f`), so this branch is the full combined firmware. Merge order: **1.4.3 reset merged
first** (done), this is 1.4.4 next.

## Decisions (user-approved)
- **Trigger = hook the existing `provision` C2D command** (commission == provision, per user). No new command.
- **120 s = best-effort cap**, not a hard guarantee: send early when all commissioned devices are heard, else a
  partial snapshot at the 120 s deadline. No sensor-firmware change (a hard guarantee would need a fast-advertise
  commissioning mode in the sensor FW — out of scope).
- **Change 2 tags = `DeviceBrand` + `DeviceModel` + `DeviceType`** (DeviceModel = "eFloStop"; **Brand/Type
  strings still TBD from app team**). Applied via the DPS enrollment-group `initialTwin` (external Azure config);
  device firmware cannot write tags by design.

## How Change 1 works (cited)
Reuses the existing health-engine "sync snapshot" machinery rather than a new blocking window:
- `health_engine_reload_devices()` (run by the `provision` handler) already re-arms boot-sync: clears the device
  table (`ever_seen=false`), `s_boot_sync_done=false`, resets the window clock — "all devices seen, else 2-min
  (120 s) timeout".
- The iothub event loop publishes a snapshot when `!g_boot_snapshot_sent && health_is_boot_sync_complete()`.
- **The fix:** the `provision` branch in `app_iothub.c` now also sets `g_boot_snapshot_sent = false` (commit
  `aae5c8b`), so the loop publishes as soon as the re-armed window completes. Non-blocking (no `rules_engine_tick`
  starvation), gated to fire once (no double-send), snapshot schema unchanged.

## Change set
- `main/iothub/app_iothub.c`: `provision` branch re-arms `g_boot_snapshot_sent=false` + log
  `Commission: fast snapshot armed…`; sync-snapshot site logs `Publishing sync snapshot…`. Comment on
  `g_boot_snapshot_sent` updated (boot **or** commission).
- `main/health_engine/health_engine.c`: `health_engine_reload_devices()` now also stamps
  `s_boot_start_ms = now_ms()` so the 120 s window is **anchored to the `provision`**, not to power-on
  (commit `777fc65`). Without this, provisioning >120 s into uptime fired the snapshot near-instantly.
- `main/iothub/app_iothub.c`: **sync-snapshot publish moved to the END of the event loop**, after the
  LoRa/valve/BLE-leak event blocks. The boot-sync "all devices seen" flag is flipped by the scanner task
  the instant an advert arrives (ahead of the iothub loop's telemetry-cache update), so the polled
  snapshot condition could fire in the same iteration as the leak event but **before** the cache was
  refreshed → snapshot emitted `battery/rssi/fw_version = null` for the very sensor that just completed
  the window. Publishing last guarantees fresh data. (The periodic snapshot was unaffected — it is
  queue-triggered and mutually exclusive with leak events in the dequeue.)
- `CMakeLists.txt`: `PROJECT_VER` → **1.4.4**.
- (Change 2: no firmware — DPS enrollment config + optional `az` back-fill, external to repo.)

### C4-driven fixes (verification workflow `wf_c706dee9`, all 4 confirmed real)
C4 passed its own criteria but the bench log exposed four defects (3 adversarially code-confirmed +
1 audit finding). All fixed:
- **#1 valve shown OFFLINE after re-provision (major).** `health_engine_reload_devices()` wipes every
  device's seen-state; an already-connected valve emits no fresh CONNECTED event (NOTIFYs delta-gated),
  so it read `rating:critical/last_seen:null` while `connected:true`, AND the all-seen path could never
  complete → always fell to the 120 s timeout on re-provision. Fix: `reseed_valve_health_if_connected()`
  (`app_iothub.c`) posts a synthetic valve-connected event after every reload that keeps the valve
  (provision + lora/ble decommission).
- **#2 double-send (minor).** Periodic + sync snapshot could both publish in one loop iteration. Fix:
  the periodic block now sets `g_boot_snapshot_sent=true` when the boot snapshot is still pending+complete
  (coalesce — identical content).
- **#3 120 s timeout overshoot to ~149 s (minor).** Deadline was only checked at 30 s health-ticks. Fix:
  `health_is_boot_sync_complete()` evaluates the deadline lazily on read; iothub loop polls at 2 s (not
  30 s) while a snapshot is pending → publish bounded to ~120 s.
- **#4 decommission asymmetry (minor).** Decommission reloaded (resetting the window) but never re-armed
  the snapshot. Fix: the lora/ble/valve decommission branches now set `g_boot_snapshot_sent=false` so a
  fresh snapshot reflects the removed device within the window instead of waiting for the 5-min periodic.

### Combined valve+sensor commission — "sensor data never arrives" (workflow `wf_e8929bf9`)
User report: commissioning valve + sensors together, the sensor data never arrives. 5-track diagnosis:
the data is **not lost** — the BLE scan works and heard the sensor (222 s, 324 s). Root cause = window-vs-
cadence + one-shot snapshot: the dry WBA sensor (fw 1.1.0) advertises a brief burst then sleeps ~100 s;
at the edge of range (−75 dBm) with dual-PHY 50%-duty scanning shared with the valve's 250 ms link + WiFi,
first-hear lands ~180 s after commission — past the 120 s window. The window snapshot showed it offline, and
the snapshot was **one-shot**, so when the sensor was finally heard no further commission snapshot fired.
Ruled out: the 10 s "Whitelist reloaded" does NOT disrupt the scan (pure RAM copy); the scan never stalled.
Fixes (user chose A+B+D+E):
- **A — Incremental commission snapshot (the real fix).** `app_iothub.c`: after the initial snapshot, while
  a bounded grace (`COMMISSION_REFRESH_GRACE_MS`=6 min) is open, publish a refreshed snapshot each time the
  heard-device count increases (`health_get_sync_counts()`), so a late sensor's data reaches the cloud
  immediately. Self-limits once all devices are heard. New helper `arm_commission_snapshot()` sets it up on
  provision + every decommission. Loop polls at 2 s during the grace so the refresh lands within ~2 s.
- **B — Separate commission window.** `HEALTH_COMMISSION_SYNC_TIMEOUT_MS`=**150 s** (vs 120 s boot);
  `health_engine_reload_devices(uint32_t sync_window_ms)` now takes the window length (boot passes 120 s,
  provision/decommission pass 150 s). Tuned to ~1.5× the ~100 s burst cadence: catches an in-range sensor in
  a single clean snapshot, reports an absent device promptly; A's refresh covers the far/slow tail so the
  window need not cover the worst case (was 240 s — shortened after review).
- **D — Cache/health consistency.** `telemetry_v2.c`: only merge cached battery/rssi/fw when
  `health[i].connected` (LoRa + BLE), so a reloaded-but-not-yet-reheard sensor can't show `connected:false`
  with stale `battery/rssi/fw` (the 309 s mismatch).
- **E — Log cleanup.** `app_ble_leak.c`: log "Whitelist reloaded" only when the whitelist actually changes
  (was every 10 s).
- (Not done: C — raising scan duty/1M-only during commission. Deferred; A+B already resolve the report.)

## Status
- **IMPLEMENT (Change 1):** ✅ `aae5c8b` (fast snapshot) + `777fc65` (anchor window to provision)
  + sync-snapshot ordering fix (this change). Merged up to current master (`1380d5f`).
- **BENCH (anchoring fix `777fc65`):** ✅ provision at uptime 48 s → snapshot at uptime 124 s (~76 s),
  sensor `connected:true` — window correctly restarted from the provision, dry sensor heard within it.
- **BENCH (ordering fix `0aab312`):** ✅ re-flashed + tested. Commission snapshot at uptime 94 s
  (~63 s after provision, early-send path) with **all fields populated** (batt 60, rssi -33, fw 1.1.0).
  UART confirms `Event: BLE Leak` (cache update) now precedes `Publishing sync snapshot`.
- **BENCH (C4 degraded):** 🟡 criteria PASS (timeout path, `3B:00` null, `3F:59` full, no crash) but
  exposed defects #1-#4 above — all fixed; **re-flash + re-run C4 (and C5/C6) pending**. Expect on
  re-test: valve `connected:true/rating:excellent`, single snapshot, timeout ~120 s.
- **BENCH (combined valve+sensor commission):** 🟡 reproduced "sensor data never arrives" (far sensor heard
  at 222 s, past the 120 s window; one-shot snapshot). Fixes A+B+D+E implemented — **re-flash + re-test
  pending**. Expect: initial snapshot at all-seen or 150 s; a late sensor triggers a **refresh snapshot**
  within ~2 s of being heard; no `connected:false` + stale `battery/rssi/fw`; `Whitelist reloaded` only on change.
- **BUILD/BENCH (full TEST_PLAN, 150 s build):** ✅ **ALL firmware cases pass** (C0-C9 + C2D-malformed +
  separate-window + stability). C7 incremental refresh **confirmed on HW**: sensor off → 150 s timeout
  snapshot offline → sensor on → `Publishing commission refresh snapshot (device heard, 2/2 seen)` → sensor
  online (UART `UART logs.txt` @732 s). Monitor duplicate @473 s was Azure QoS-1 redelivery, not a firmware
  double-send (UART = single publish).
- **Change 2:** ⏳ blocked on `DeviceBrand`/`DeviceType` strings + Azure DPS enrollment edit (no firmware).
- **MERGE:** ✅ merged `feature/fast-commission-snapshot` → master (1.4.4) via `--no-ff`, tagged `v1.4.4`.
  **Not pushed** (awaiting explicit request).

## Open items
- Get `DeviceBrand` / `DeviceType` values from the app team for Change 2.
- Confirm the app sends `provision` at end of barcode scanning (it does today — the only device-add path).
