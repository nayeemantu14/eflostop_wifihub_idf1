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

## Status
- **IMPLEMENT (Change 1):** ✅ `aae5c8b` (fast snapshot) + `777fc65` (anchor window to provision)
  + sync-snapshot ordering fix (this change). Merged up to current master (`1380d5f`).
- **BENCH (anchoring fix `777fc65`):** ✅ provision at uptime 48 s → snapshot at uptime 124 s (~76 s),
  sensor `connected:true` — window correctly restarted from the provision, dry sensor heard within it.
- **BENCH (ordering fix `0aab312`):** ✅ re-flashed + tested. Commission snapshot at uptime 94 s
  (~63 s after provision, early-send path) with **all fields populated** (batt 60, rssi -33, fw 1.1.0).
  UART confirms `Event: BLE Leak` (cache update) now precedes `Publishing sync snapshot`.
- **BUILD/BENCH (full TEST_PLAN):** 🟡 headline tests **C0/C1/C2/C3 PASS**. C4 (degraded/missing
  sensor), C5 (re-entrancy), C6 (5-min periodic intact) not yet exercised.
- **Change 2:** ⏳ blocked on `DeviceBrand`/`DeviceType` strings + Azure DPS enrollment edit (no firmware).
- **MERGE:** ⏳ after bench pass → `git merge --no-ff feature/fast-commission-snapshot` into master (1.4.4).

## Open items
- Get `DeviceBrand` / `DeviceType` values from the app team for Change 2.
- Confirm the app sends `provision` at end of barcode scanning (it does today — the only device-add path).
