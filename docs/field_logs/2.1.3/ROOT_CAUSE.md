# Hub FW 2.1.3 field defects: root-cause analysis for 2.1.4

**Status:** approved analysis (Phase A/B of the 2.1.4 bug-fix release).

**Code base:** `master` @ `6b84ae3`, which equals `ae4d59a` (PROJECT_VER 2.1.3) plus a docs-only commit. Every `file:line` below refers to that tree.

**Inputs:**
- `UART logs.txt` and `IoT hub monitor.txt` in this folder, both read in full. 58 of the 59 D2C messages are byte-identical to their UART `Pub` line. The one difference is a UART capture glitch at line 528 (`"lea_age_s"`).
- `ANALYSIS.md`, the pre-analysis. Each hypothesis is re-verified in §5.
- Every line of `main/`, read by four independent readers, with the key sites re-read by hand.
- A 2026-09-25 bench boot of a hub provisioned with two BLE sensors and no valve.

**Not re-confirmed:** the valve-side battery facts (FW 2.2.0: >20 % Good, 11–20 % Low, ≤10 % Critical, auto-close and refuse open). The valve repo was not available, so they are taken as given in the release brief.

| Short | File |
|---|---|
| A | `main/iothub/app_iothub.c` |
| T | `main/telemetry/telemetry_v2.c` |
| H | `main/health_engine/health_engine.c` |
| H.h | `main/health_engine/health_engine.h` |
| V | `main/ble_valve/app_ble_valve.c` |
| S | `main/ble_leak_scanner/app_ble_leak.c` |
| R | `main/rules_engine/rules_engine.c` |
| P | `main/provisioning_manager/provisioning_manager.c` |
| F | `main/rgb/fleet_led.c` |

---

## 1. Evidence timeline

| Uptime | What the logs show | Defect |
|---|---|---|
| 77 s | provision valve + 4 BLE; `Device table loaded: 5 device(s)`; `PROV pulse armed` | — (UI sync works) |
| 110 s | valve GAP-connected mid-pairing; snapshot valve `state:"unknown", battery:0, rating:critical` | BUG-1 trap |
| 678 s | `Roll-up grace expired (600 s) — 2 unheard device(s) now count`; `FLEET_LED … RED`. **No snapshot and no alert**: the cloud sees `critical` only at the 969 s heartbeat | visibility gap |
| 1193 s | decommission `2b:a5`; `Device table loaded: 4`; next snapshot shows all 3 survivors `connected:false, rating:critical, battery/rssi/fw_version:null`, "Syncing - waiting for 3 devices"; **LED RED→WHITE and system critical→excellent** although `b6:8e` had not been heard for ~1100 s | **BUG-2** (masks a real offline sensor) |
| 1343 / 1583 / 2956 / 3238 / 3453 s | `Boot sync: timeout (150 s) … still excused for a further 449 s` 150 s after each removal, then a `boot` snapshot, then `commission` refreshes | BUG-2 (removal re-arms both windows) |
| 1801 s | last BLE sensor removed (valve remains): `Boot sync: all devices seen`, then `SNAP trigger=boot`, then `SNAP clamped +4645 ms`, then `event:decommission` 5 s later | two owners for one snapshot |
| 1855 s | valve removed, hub goes UNPROVISIONED; `SNAP trigger=boot` with the **stale live valve** (`open, 65, connected:true`, no rating), "All devices healthy"; the command EVENT is clamped +4609 ms and then wiped | **BUG-3** |
| 1855 → 2379 s | no snapshot for ~9 min | **BUG-6** |
| 3303 s | valve removed with 2 BLE sensors left; `Valve identity unavailable (live=0 provisioned=0)`; `valve:{"state":"disconnected","connected":false}` | **BUG-5** |
| whole log | MONITOR `min_ever=2972`, `largest_blk=7680` from ~90 s (BLE pairing + TLS + provision) | heap budget |
| bench boot (sensors only) | `BLE_LEAK: Task started, waiting for NimBLE...` and never `NimBLE ready`; both sensors unheard at 230 s; FAST snapshot `valve:{"state":"disconnected",…}` | **P0-b**, BUG-5 |

**Security note:** UART lines 137 and 147, and every boot, print the site Wi-Fi password. The lines come from the managed `wifi_manager` / `http_server` components at INFO level. Redact the committed log before it leaves the building.

---

## 2. Root causes

### BUG-1: the valve's critical battery never reaches `critical`

1. **No battery→CRITICAL branch.** `compute_valve_rating` (H:193-221) checks, in order:
   1. leak;
   2. the disconnect grace (WARNING, then CRITICAL after 180 s);
   3. never-seen (CRITICAL);
   4. `≤ HEALTH_BATTERY_WARN_PCT` (20) → WARNING;
   5. `≤ HEALTH_BATTERY_GOOD_PCT` (35) → GOOD.

   The thresholds are shared with the sensors (H.h:20-21) and do not match the valve's 10/20.
2. **Unknown is stored as 0.**
   - `g_val_battery = 0` at init, CONNECT and DISCONNECT (V:142, 1409, 1464).
   - A missing battery characteristic or service leaves it 0 (V:1080-1111), and a missing service also skips DIS discovery. A failed setup read leaves it 0 (V:674-689).
   - `reseed_valve_health_if_connected` posts it while only GAP is up (A:628-641).
   - The health engine drops only 0xFF (H:529), so **0 is rated as 0 %**.
   - The snapshot (T:712), `valve_state_changed` (T:962) and valve leak events (`vlk_batt`, A:2366/2574) all publish 0.
3. **Battery reaches health only on change.** `on_notify` posts it via `notify_hub_update` behind the delta gate (V:555-561). A steady critical reading, re-notified every 20 s, is never re-posted, so a dropped link-up post is never corrected.
4. **Alerting.** Every non-leak CRITICAL is `device_offline` (H:1022-1024); only a leak is suppressed (H:369-370). The 60 s debounce commits the rating but drops the alert, with no trailing edge (H:377-381). A valve at critical battery that then disconnects would emit a spurious `device_recovered` on entering the WARNING grace.
5. **Reason text.** A connected valve at the system rating is always "Valve battery low" (T:326-333, 375); there is no "Valve battery critical". The LED maps CRITICAL→RED (F:85).
6. **Commands.** The valve refuses to open at ≤10 %, but C2D `valve_open` is acked `ok` (A:805-815; GATT writes have no completion callback).

### BUG-2: removing one device wipes every other device

1. **The reload is a wipe.** `health_engine_reload_devices()` (H:797-920):
   - `memset(s_devices…)` (H:833);
   - re-seeds everyone as unseen CRITICAL with battery 0xFF and RSSI 0;
   - carries only `leaking`/`crit_is_leak` (H:820-830, 898-906);
   - re-arms **both** the 180/150 s snapshot gate and the 600 s roll-up grace for everyone (H:879-887).

   Survivors lose `last_seen`, battery, RSSI, rating, `ever_seen`, `last_alert_ms` and the valve's `disconnect_ms`. The last one means an offline valve becomes "never seen" and is excused for 150 s.
2. **Callers:** A:877 (valve), A:894 (LoRa), A:915 (BLE), A:939 (all), A:1038 (provision). All run on the **esp-mqtt event task**. Also H:958 (init).
3. **Mutex misuse.** The take result is ignored (H:800) and the give is unconditional (H:919). If the take timed out while another task holds `s_mutex`, the give asserts in `xTaskPriorityDisinherit`. The function also calls `provisioning_get_*` (1 s mutex each) while holding `s_mutex`, and puts **1162 B** of arrays on the 6 KB esp-mqtt stack.
4. **The grace is global.** `rollup_unheard_locked` (H:254-292) keys on the single `s_rollup_grace_done`, so a removal re-excuses devices that were heard long ago and hides real offline ones (see the 1193 s evidence).
5. **Ghost state in other modules:**
   - Scanner `s_sensors[]` is keyed by whitelist **index** (S:196) and never reset on a whitelist change. `reload_whitelist` publishes the count before the entries (S:110-113) and blanks the list on a getter timeout (S:123-125).
   - Telemetry caches are never purged (A:253-254, 364, 411).
   - The rules active-leak set has no purge path (R:423-470). A removed wet sensor blocks `leak_reset` (R:820-825) and makes override cancel/expiry re-close the valve (R:925-946, 1317-1337).
   - The valve change detectors `s_valve_pub_wet`/`s_valve_pub_linked` are never reset (A:218, 238).
6. **Provisioning getters return `false` both for "none" and for a mutex timeout** (P:750-762). A reconcile must never read a timeout as a removal.

### BUG-3 / BUG-6: an empty hub sends a stale snapshot, then nothing

1. **Stale `provisioned`.** It is sampled at A:2230, before the blocking select (A:2257). The re-sample at A:2418 only catches false→true, so a decommission (true→false) runs Phase 3 once with a stale `true`.
2. **Arming regardless of what remains.** `arm_commission_snapshot(false)` (A:880, 896, 917) clears `g_boot_snapshot_sent`. An empty table counts as "all seen" (H:608-619), so `SNAP_BOOT` fires (A:2672-2673). It overwrites the clamped command EVENT (A:721-728) and publishes the **live** valve (T:683), which is still up because the disconnect is only queued (V:2201). The reason is "All devices healthy" (T:268-274).
3. **The unprovisioned branch skips everything.** A:2418-2440 frees the rules JSON, calls `snap_rearm_heartbeat()` (which wipes the pending command EVENT), resets the boot/fast/commission/pulse flags on every pass, and then `continue`s. That skips lifecycle, twin and offline drain (A:2443-2451), alert popping (A:2461-2478), every event publisher, all arming, and **the only scheduled flush** (A:2755).
4. **Decommission-all** publishes directly (`telemetry_v2_publish_snapshot("decommission")`, A:2164; result ignored) and reboots. After the reboot the hub is unprovisioned, so it falls into (3) and goes silent. `rules_engine_clear_persistent_state()` does not reset the in-RAM override (R:1495-1521), so the final snapshot can still claim `override_active`.
5. **Interlock floor on an empty table.** H:313 applies with zero devices, so "warning / Leak interlock latched" can appear while the LED shows WHITE (F:173-176 checks `total==0` first).
6. **PROVISIONED with zero devices** is reachable via a rules-only provision payload (P:584-591), and must behave like "empty".

### BUG-5: the valve block when no valve is provisioned

The valve block is keyed on the live GAP link (`vconn = ble_valve_get_mac()`, T:683). The else branch emits `{"state":"disconnected","connected":false}` whether or not a valve is provisioned (T:722-725). The live MAC is never compared with the provisioned one; Phase 2 compares them (A:2322), but only to gate D2C events.

### P0 safety defects in the same state

- **P0-a: connect to any valve.**
  - The scan callback matches `(g_has_target_mac && mac_match) || (!g_has_target_mac && name_match)` (V:1316), and the passkey is fixed at 222900 (V:48).
  - CONNECT never checks the peer (V:1393-1437).
  - After a valve decommission the target is cleared (A:878 → V:2250) and NimBLE keeps running.
  - Every path that can start a connect:
    - C2D `valve_open`/`close`/`set_state` (A:812-841);
    - auto-close (R:705, gated only on `provisioning_is_provisioned()`, R:568);
    - override cancel (R:945);
    - override expiry (R:1336, running even on an empty hub because `rules_engine_tick()` sits at A:2260, before A:2418);
    - the BLE starter (V:2107);
    - the implicit write paths (V:1784-1787, 1865-1868), `on_stack_sync` (V:1975) and the DISCONNECT rescan (V:1502).
  - Not identity-gated either: the reconnect reconcile (A:2376), the valve flood evaluation (A:2372) and tick Check 2 (R:1387). A neighbour's valve can therefore be **closed and RMLEAK-latched**, and a C2D `valve_open` can **open** it.
  - `wire_device_id` prefers the live MAC (R:366-374).
- **P0-b: BLE never starts without a valve.** `app_ble_valve_signal_start()` is reached only inside `if (provisioning_get_valve_mac(…))` (A:1719-1725, A:2039-2043), and the leak scanner is started only by the BLE starter (V:2110). Reproduced on the bench.
- **P0-c: queued commands replay.**
  - `ble_cmd_queue` (depth 10) exists from boot (V:2139), but its consumer is created only when BLE starts (V:2106).
  - Pending commands become `g_pending_*` and are written to **whichever valve connects next** (V:1007-1008).
  - `g_pending_*` also survive `set_target_mac(NULL)` and DISCONNECT.

---

## 3. Snapshot scheduler (2.1.3 as found)

**State:** `s_snap_due_ms`, `s_snap_reason` (HEARTBEAT, EVENT, then the urgent COMMISSION/BOOT/FAST), `s_snap_tier` (LOW 2 s / HIGH 300 ms), `s_snap_last_pub_ms`, `s_snap_retry_until_ms`.

**`snap_request` (A:717-789):**
- Urgent reasons set `due = max(now, retry)` and **overwrite** the pending reason.
- EVENT uses `due = now + window`, clamped to `last_pub + 5 s`. Updates are pull-in-only, with a LOW→HIGH upgrade and a **yield** rule: an EVENT displaces a reason that the window gate is suppressing.

**Loop order (A:2150-2848):**
1. decommission-all direct publish
2. `g_cmd_snap_pending` → EVENT HIGH
3. pulse-arm consume
4. heartbeat-interval latch
5. sample `provisioned`
6. `commission_pending` (2 s vs 30 s base), offline floor
7. select
8. `rules_engine_tick`
9. Phase 1 receive
10. Phase 2 rules (runs unprovisioned too)
11. `dps_maintain` / `sas_maintain`
12. **the unprovisioned branch** (`continue`)
13. lifecycle
14. alerts → EVENT "health"
15. event publishers (valve link detector A:2527)
16. FAST arm (A:2658)
17. BOOT arm / COMMISSION refresh (A:2672-2680)
18. pulse (A:2697-2747)
19. **single flush** (A:2755)
    - window gate: EVENT/FAST always pass; others wait while boot is unsent and sync is incomplete
    - settle gate: valve command in flight
    - on success: `g_boot/fast_snapshot_sent`, `g_commission_pub_seen`/`until_ms`, then `snap_rearm_heartbeat()`
    - on failure: 5 s retry floor

**Cross-task writers (unlocked):** `arm_commission_snapshot` on the esp-mqtt task writes `g_boot_snapshot_sent`, `g_commission_pub_seen` and the **int64** `g_commission_until_ms`. It also sets the `g_prov_pulse_arm`, `g_cmd_snap_pending` + label, `g_decommission_reboot` and `g_needs_lifecycle` flags.

---

## 4. Other defects found in Phase A

| # | Sev | Defect | Evidence | 2.1.4 |
|---|---|---|---|---|
| N1 | P1 | No leak protection until the first Wi-Fi IP (then SNTP ≤120 s and DPS retries) — provisioning, rules, health and BLE all wait | A:2022-2085 | **fix** |
| N2 | P1 | Pre-loop drain discards the valve CONNECTED/LEAK when BLE started at A:2043, so no boot reconcile | A:2094 | **fix** |
| N3 | P1 | `xQueueAddToSet` after the drain, return ignored; an item arriving in between keeps that queue out of the set until reboot | A:2092-2131 | **fix** |
| N4 | P1 | Foreign or decommissioned LoRa sensors drive the rules engine (Phase 2 runs before the whitelist check; shared master key) | A:2295-2301 vs 2485; R:559/568 | **fix** |
| N5 | P0 | P0-a extras: valve flood evaluation, tick Check 2, C2D open and `leak_reset` act on any connected valve | A:2372, R:1387-1415, A:812-841, R:886 | **fix** |
| N6 | P1 | Re-provisioning a different valve MAC keeps the old link | A:1729-1732, V:1693-1697 | **fix** |
| N7 | P1 | `g_pending_*` survive decommission and are applied to the next valve | V:147-148, 2250-2258, 2012-2017 | **fix** |
| N8 | P1 | A connect in flight survives decommission (no `ble_gap_conn_cancel`) | V:2012-2017 | **fix** |
| N9 | P2 | C2D valve commands ack `ok` when the enqueue fails or no valve exists | A:813-841 | **fix** |
| N10 | P2 | Roll-up grace expiry changes the rating with no snapshot or alert | H:640-667; log 678→969 s | **fix** |
| N13 | P2 | Duplicate or case-variant ids in a provision payload create duplicate health slots (permanent RED) | P:470-515 | **fix** |
| N15 | P2 | Provisioning getters can't tell "none" from a timeout | P:750-762 | **fix** |
| N16 | P2 | The alert queue is not drained while unprovisioned; ratings freeze and stale alerts replay | H:431, A:2418 | **fix** |
| N17 | P2 | A snapshot on a health-mutex timeout publishes empty arrays and reports success | T:653, 672-676 | **fix** |
| N18 | P2 | Offline buffer has no lock (esp-mqtt vs iothub); >512 B entries are truncated into invalid JSON; never cleared | offline_buffer.c:73-120 | **fix** |
| N19 | P2 | Legacy parser keyword-scans JSON bodies before rejecting them | c2d_commands.c:156-290 | defer 2.1.5 |
| N20 | P2 | No command-id de-duplication | A:791 | defer |
| N21 | P2 | Single-slot `g_pending_telemetry` overwrites rules events | R:83 | defer |
| N22 | P2 | `clear_persistent_state` has no mutex and can re-persist the incident after the erase | R:1495-1521 | **fix** |
| N23 | P2 | `MAX_ACTIVE_LEAK_SOURCES` 16 < 33 possible sources | R:86 | defer |
| N24 | P2 | Failed GATT writes are dropped; `apply_pending` clears on a mutex timeout; a `nimble_port_init` failure is permanent | V:1794-1915, 785/814, 2052-2056 | **fix** |
| N25 | P2 | Sensor deafness during a valve connect attempt (≤30 s) | V:1344, S:368-374 | defer |
| N26 | P3 | LoRa driver and replay hazards (`endPacket` spin, SX127x driver on SX1262 HW) | lora.cpp:322, app_lora.cpp:237 | defer |
| N27 | P3 | ~17 KB of removable RAM (idle `wifi_task`, UART1 logger, UART0 debug task with a LoRa-TX backdoor) | app_wifi.c:31, app_uart.c:19-34, app_lora.cpp:186-212 | defer |
| N28 | P3 | Wi-Fi password logged by the managed `wifi_manager`/`http_server` | UART:137,147 | **fix** (log level from `main/`) |

ANALYSIS register L1-L20 is re-verified; its 2.1.4 disposition:
- **fix:** L1-L3, L4-L10, L11 (twin on decommission), L12 (empty-hub reset), L13, L14 (in code touched), L15, L16, L17, L18.
- **partly eased:** L19.
- **defer:** L20 (bond cleanup, idle task, double `gpio_install_isr_service`).

---

## 5. Verdicts on the ANALYSIS.md hypotheses

**All CONFIRMED at the stated lines**, except the entries below. Every one was re-checked on the tree above.

| Hypothesis | Verdict | Note |
|---|---|---|
| BUG-3/6 item 11 ("BOOT re-fires every iteration; FAST every 150 s; the 2 s poll never stops") | **CHANGED** | BOOT would re-fire on every loop wake. FAST re-fires only when the valve is ready, because A:2428 re-stamps `g_fast_arm_ms` on every pass. "The 2 s poll never stops" is wrong: `commission_pending` requires `provisioned` (A:2239). The pulse reset is still needed. |
| P0-b ("`signal_start` is called inside `provisioning_get_valve_mac()`") | **CHANGED (wording)** | The getter has no side effect; the calls are gated on its result (A:1719-1725, 2039-2043). The effect is confirmed. |
| R:93-212 (rules NVS block) | **CHANGED (range)** | The NVS block is R:93-238. |
| T:869-887 | **CHANGED (range)** | The code is at T:875-888. |
| L6 ("carried leak never escalates") | **CONFIRMED, with nuance** | The device stays CRITICAL through leak precedence. What is lost is `connected`/`last_seen_age_s`. |
| Valve battery facts (valve FW 2.2.0) | **NOT RE-CONFIRMED** | Valve repo not available. |

---

## 6. Decisions taken for 2.1.4 (Phase B)

- **Valve battery bands:** ≤10 % CRITICAL ("Valve battery critical"); 11–20 % WARNING ("Valve battery low"); >20 % EXCELLENT. **Sensor bands unchanged.**
- **Battery-critical alerts:** no health alert (suppressed like a leak); the snapshot carries the rating and reason, and each transition requests a snapshot.
- **Unknown battery:** `null` on every message; valve `state:"unknown"` until the valve is GATT-ready.
- **Empty-hub `data.reason`:** `event` (the transition), `heartbeat`, `boot` (after boot or reconnect); decommission-all keeps `decommission`.
- **Removal during the post-provision sync window:** one removal snapshot; the open window continues unchanged.
- **Hub emptied by selective removals:** the rules-engine state (RAM + NVS) and the rules config are reset to defaults.
- **While unprovisioned:** lifecycle, twin, offline drain and alert popping run. Every decommission publishes the twin.
- **C2D `valve_open` / `set_state open` while the valve battery is ≤10 %:** error ack.
- **Scope:** all safety fixes (P0-a/b/c, N1-N9) and the latent bundles for valve write reliability, visibility, and robustness & hygiene. Parser and rules edge cases (N19, N21, N23) are deferred.
- **Docs:** bump every version stamp and fix all schema mismatches.
