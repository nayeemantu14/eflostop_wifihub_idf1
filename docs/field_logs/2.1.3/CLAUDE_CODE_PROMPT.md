# Claude Code prompt — eFloStop Wi-Fi Hub 2.1.3 → 2.1.4 bug-fix release

> **How to use:** see `README.md`. Switch Claude Code to **Plan mode** (Shift+Tab), run
> `/effort xhigh`, then paste everything between the markers, with the hub repo
> (`eflostop_wifihub_idf1` @ `master`, 2.1.3) open as the workspace root.

=== BEGIN PROMPT ===

ultracode

Workflow / multi-agent orchestration is authorised **only in Phases D–F, after I approve
the plan**. Phases A–C use only read-only Explore agents, `AskUserQuestion` and plan mode.

You are a principal embedded-systems engineer (ESP-IDF v5.5.1, ESP32-S3, FreeRTOS SMP,
NimBLE, esp-mqtt, Azure IoT Hub, cJSON, NVS) owning the eFloStop Wi-Fi Hub firmware.
Fix the bugs below and release them as **2.1.4**. Aim for **correctness, not speed**. This
is safety firmware for a water shut-off system: a fix that hides a leak, fails to close
the valve, closes the wrong valve, or silently stops telemetry is worse than no fix.

## 0. Ground rules

1. **Workspace = this repo at 2.1.3.** Sanity checks first:
   - `PROJECT_VER` in `CMakeLists.txt` is `2.1.3`;
   - `git log` contains `ae4d59a` (or a later commit), and the working tree is clean;
   - `main/` contains `"Syncing - waiting for"`, `"PROV pulse armed"` and `"still excused for a further"`;
   - `idf.py --version` works. If it is not on PATH, source `$IDF_PATH/export.sh` (`export.ps1` on Windows).

   If any check fails, STOP and tell me.
2. **The valve firmware (STM32WB, FW 2.2.0) is read-only.** If `../elfostop_ble_valve`
   exists you may read it to re-confirm the §2 BUG-1 facts.
3. **Make no assumptions.** Prove every root cause from the code on disk (`file:line`) and
   tie it to a log line. `docs/field_logs/2.1.3/ANALYSIS.md` is a pre-analysis of this exact
   commit. It lists **verified hypotheses, candidate fix directions and ⚓ grep anchors**.
   Re-confirm each hypothesis you rely on (CONFIRMED / CHANGED / REFUTED). Do not adopt a
   candidate fix without proving it against the code.
4. **Do not modify** `managed_components/`, `sdkconfig`/`sdkconfig.defaults` (the version
   bump needs no change there), the partition table, DPS/SAS/crypto code, or NVS
   namespaces/keys/layout. Field units upgrade by OTA from 2.1.3 and must keep their
   provisioning.
5. **Wire-contract changes are limited to §2 plus my Phase B answers.** Any other
   outbound JSON change needs my approval.
6. **No heap regressions.** Field units hit `min_ever` free heap of **2972 B** (largest
   block 7680). Add no steady-state heap and no large stack buffers, and handle every
   cJSON/malloc NULL.
7. **Do not regress 2.1.3 features.** In particular: leak-aware health, the carried leak
   state, the post-provision pulse, the fast/boot/commission snapshots, the settle gate,
   the RMLEAK read-back, and the **UI-sync behaviour in §2 BUG-2**.
8. Match the repo's style: comment density, "why" comments, log tags, and levels
   (`CONFIG_LOG_MAXIMUM_LEVEL=3`, so `ESP_LOGD` is compiled out).
9. Do not flash or run hardware. Anything that needs hardware goes into the bench test (Phase G).

## 1. Inputs (already in the repo)

- `docs/field_logs/2.1.3/UART_logs.txt` (hub console) and
  `docs/field_logs/2.1.3/IoT_hub_monitor.txt` (Azure D2C). Read both in full. They cover:
  - provision of 1 valve + 4 BLE leak sensors;
  - four single-sensor decommissions;
  - valve decommission → hub UNPROVISIONED, then ~9 min of silence;
  - re-provision;
  - two sensor decommissions;
  - valve-only decommission with 2 sensors remaining.
- `docs/field_logs/2.1.3/ANALYSIS.md`: architecture/sequence diagrams, log timeline,
  per-bug root causes (§3), required snapshot shapes (§4), latent-bug register (§6).

## 2. Bugs and required behaviour

**BUG-1: the hub rating must go critical at the valve's critical-battery shut-off point.**
Valve facts (FW 2.2.0, `app_main.c` `readBatteryADC()` / `batt_voltage_to_percent()`):
- **Signal:** the Battery Level characteristic 0x2A19 notifies a uint8 **percent** on
  every reading. There is no battery-state flag and no hysteresis.
- **Thresholds:** `>20` Good (re-read every 10 min); `11..20` Low (beep and re-read every
  60 s); **`<=10` Critical** (beep and re-read every 20 s, auto-closes, refuses open).
- **Required:** when the provisioned valve's last **real** battery reading is `<= 10`, the
  valve rating and `system_health.rating` are `critical`, with a distinct reason (e.g.
  "Valve battery critical", not "Valve offline" or "Valve battery low"). This applies to
  the snapshot, the health events and the fleet LED (RED). It recovers when a reading is
  above 10. The valve needs its own thresholds: the current 20/35 % are shared with the
  sensors and don't match the valve's 10/20.
- **Known trap:** the hub stores 0 for an unknown valve battery (`g_val_battery`). That
  0 reaches the health engine whenever the battery characteristic is missing, the read
  fails, or `reseed_valve_health_if_connected()` runs mid-setup, and the snapshot
  publishes it while only GAP is up. Unknown must never rate as 0 %.

**BUG-2: decommissioning one device erases every other device's data in the snapshot.**
After each removal, survivors show `connected:false, rating:critical, battery/rssi/
fw_version:null` and "Syncing - waiting for N devices". The cause is that
`health_engine_reload_devices()` wipes the whole table and re-arms the windows for everyone.

**Preserve the intended UI-sync behaviour:** after a provision, sensors that have not yet
been heard are shown as "Syncing - waiting for N devices" for up to the 600 s grace,
instead of offline. Every leak-sensor advertisement updates the data and triggers a
snapshot (the post-provision pulse), so the UI fills in fast.

Required:
- Removing a device (a sensor of any type, or the valve) leaves every other device's
  state untouched: `last_seen`, battery, RSSI, fw, rating, `leak_state`/leaking,
  `ever_seen`.
- A removal does **not** push already-heard survivors back into "syncing" and does not
  restart any window for them. The next snapshot equals the previous one minus the
  removed device.
- The grace, the "Syncing" reason and the per-advertisement snapshots apply to devices
  **not yet heard** since they were provisioned. That includes devices newly added by a
  re-provision.

**BUG-3 / BUG-6: an empty hub sends no snapshots.**
Today, after the last device is removed:
- one snapshot goes out with **stale** valve data (`open`, `battery 65`, `connected:true`) and "All devices healthy";
- then nothing until re-provision.

Required:
- **When to publish the empty snapshot:**
  - once, promptly, on the transition to "no devices";
  - at every heartbeat interval while unprovisioned;
  - after boot or MQTT (re)connect while unprovisioned, once SNTP **and** MQTT are up (no busy-wait). Snapshots stay unbuffered, as today.
- **No snapshot storms:** the scheduler must not re-fire BOOT or FAST on an empty hub. The
  unprovisioned branch resets those flags on every pass, and an empty table always reads as
  "sync complete".
- **Shape** (`reason` per my Phase B answer):
```json
"data": {
  "reason": "heartbeat",
  "system_health": { "rating": "excellent", "reason": "No devices provisioned" },
  "valve": {},
  "lora_sensors": [],
  "ble_leak_sensors": [],
  "rules": { "auto_close_enabled": true, "trigger_mask": 7 },
  "override_active": false
}
```
  The envelope is unchanged: `schema`, `ts`, `gateway{id, short_id, fw:"2.1.4", uptime_s}`, `type:"snapshot"`.
- **Fleet LED:** stays WHITE (unprovisioned) on an empty hub.
- **"Decommission all" (reboots):** it has its own unscheduled publish
  (`telemetry_v2_publish_snapshot("decommission")`). Its final snapshot and the
  post-reboot heartbeats must also have the empty shape.

**BUG-5: valve decommissioned while sensors remain → `"valve": {}`.**
Today the hub sends `{"state":"disconnected","connected":false}`. Required: `{}` whenever
**no valve is provisioned**, even if the BLE link has not finished tearing down. A valve
that IS provisioned but offline keeps today's shape (`valve_id`, `state:"disconnected"`,
`connected:false`, `rating`, `last_seen_age_s`). Live valve data is trusted only when the
live MAC equals the provisioned MAC.

**RELEASE:** `PROJECT_VER "2.1.4"`. It is the single source of truth. Also update the
docs, schemas and validators that state a version (`snapshot.schema.json` is stale: it
requires `valve.state` and lists `mac`), and add a CHANGELOG entry.

## 3. Workflow (in order; do not skip ahead)

### Phase A — Discovery (read-only)
1. Run the §0.1 checks. Record the 2.1.3 baseline `idf.py build` warning count and `idf.py size`.
2. Read **every line** of `main/`, the CMake files, `sdkconfig.defaults`, `partitions.csv`
   and `docs/telemetry/schemas/snapshot.schema.json`. Use 4 parallel Explore agents so
   nothing is skimmed:
   - (a) iothub + telemetry_v2 + offline_buffer;
   - (b) health_engine + provisioning_manager + sensor_meta + nvs_store;
   - (c) ble_valve + ble_leak_scanner;
   - (d) rules_engine + commands + app_lora + rgb/fleet_led + the rest.

   Each returns: responsibilities; tasks (stack, priority, core); queues and mutexes;
   cross-task calls; findings with `file:line`.
3. Keep the root-cause analysis in context for the plan. Cover:
   - per bug, the root cause with `file:line` and the proving log lines;
   - the status of each ANALYSIS §3 hypothesis you rely on;
   - a precise description of the snapshot scheduler state machine (boot / fast /
     commission refresh / prov pulse / command-ack / event / heartbeat, the window gate
     and yield rule, the settle gate, the retry floor). Every BUG-2/3/5/6 fix interacts with it;
   - latent bugs with severity (ANALYSIS §6).

### Phase B — Clarifying questions
Use `AskUserQuestion`, at most 4 questions per call, each with concrete options and your
recommendation first. Resolve at least the following, and anything else Phase A leaves
genuinely ambiguous (do not ask what the code answers):
1. Valve thresholds: CRITICAL at `<= 10 %`, WARNING at 11–20 %, and no GOOD band (valve FW
   calls >20 % "Good")? (I observed the valve beeping ~10 s apart at critical; the code
   says 20 s. Tell me if that changes anything.)
2. A battery-critical valve currently raises the health event `device_offline` (every
   non-leak CRITICAL maps to it). Use a distinct event or cause?
3. `reason` values:
   - empty-hub transition = `event`? periodic = `heartbeat`? first after boot/connect = `boot`?
   - the "decommission all" final snapshot uses `decommission`. Keep it?
4. UI sync after a **removal**: survivors keep their state, so there is nothing to
   re-sync. Should a removal still trigger the per-advertisement pulse for any survivor
   not yet heard since provisioning, or only a single removal snapshot?
5. When the hub becomes empty through selective removals, should the rules and the
   rules-engine state (override window and incident latch, in RAM **and NVS**) reset to
   defaults? Today they survive, and a latched interlock can hold a WARNING floor on an
   empty hub.
6. Valve battery unknown → `"battery": null` (today `0`), and `state:"unknown"` until the valve is GATT-ready?
7. While unprovisioned, should lifecycle, twin reported, offline-buffer drain and
   health-alert popping run? Recommendation: yes. Also: should every decommission refresh
   the twin (today none does)?
8. **P0 safety issues in the BUG-5 state** (confirmed on 2.1.3):
   - a. With no target MAC, `ble_valve_connect()` (C2D valve_open/close/set_state,
     auto-close, override cancel/expiry, implicit connects inside the write paths)
     connects to **any** valve advertising `eFloStopV2` by name, with the fixed passkey.
     `rules_engine_on_valve_connected()` then reconciles it with no MAC check, so a
     neighbour's valve can be closed and RMLEAK-latched.
   - b. NimBLE and the leak scanner start only when a valve MAC is provisioned. A
     sensors-only hub never scans, so leaks are missed.
   - c. Valve commands queued while there is no valve task replay when a valve is later
     provisioned.

   **P0-b must not ship without P0-a.** Include all three in 2.1.4, or defer them?
9. Which other latent bugs (ANALYSIS §6, with severity and recommendation) go into 2.1.4?

### Phase C — Plan and approval gate
You should already be in plan mode; if not, call `EnterPlanMode` now. The plan contains:
- **Per bug:** the root cause (`file:line`); the fix design; every function and file
  touched; and why this is the minimal correct fix.
- **Constraints honoured:**
  - the task context of every changed function (esp-mqtt event task / iothub_task / health task / NimBLE host / timer daemon);
  - mutex lock order and hold times (fix the reload's ignored take / give-without-hold);
  - cross-task scalar writes (a 64-bit write is not atomic on this core);
  - heap and stack (the reload currently puts ~1.2 KB on the esp-mqtt stack);
  - OTA/NVS compatibility;
  - exact before/after JSON for each wire change.
- **Interaction with the snapshot scheduler:** which path owns each snapshot (exactly one
  owner for the decommission snapshot; today two request it), and how storms and
  duplicates are prevented.
- **A scenario test matrix** with the expected log lines and JSON, including the
  UI-sync flow (provision → per-advertisement snapshots → "Syncing" clears as each device
  is heard) and removals during that flow.
- **Risks and rollback.**

Present it with `ExitPlanMode`. That dialog is my approval gate. If I reject it or give
feedback, revise and re-present. **Change no file before I approve.**

### Phase D — Implement (xhigh + Workflow)
1. Create branch `fix/2.1.4`. First commit: `docs/field_logs/2.1.3/ROOT_CAUSE.md` (the approved analysis).
2. Implement one bug-group at a time, in this order:
   - (1) BUG-2 (health-table reconcile, per-device grace, scanner/caches/rules purge);
   - (2) BUG-3/5/6 (serializer, unprovisioned flush, valve presence, scheduler ownership);
   - (3) BUG-1;
   - (4) approved P0/latent fixes;
   - (5) version, docs, schemas, CHANGELOG.

   Groups share `app_iothub.c`, `telemetry_v2.c` and `health_engine.c`, so **never edit the
   same file in parallel**. Each group:
   - keeps a minimal diff;
   - runs `idf.py build` (esp32s3) with **no new warnings**;
   - commits to `fix/2.1.4`.

   A reviewer agent checks that commit (`git show`) against the plan while the next group proceeds.
3. If a host-side unit test is cheap, add it under `test/host/` and run it. Candidates: the
   health_engine rating/reconcile/grace logic and the snapshot serializer, built with host
   gcc against small FreeRTOS/NVS stubs and the in-tree cJSON. Otherwise explain why not.
4. Finish with `idf.py fullclean build` and a size delta against the 2.1.3 baseline.

### Phase E — Adversarial review
Independent reviewers get the diff, the plan and the logs, but not the implementers'
reasoning. Their job is to **break** the fixes by tracing code paths and task
interleavings (nothing runs on hardware). At minimum:
- **Decommission:**
  - each device type, while that device, another sensor or the valve is mid-advertisement, mid-GATT-notify, or leaking;
  - during the post-provision UI-sync window, with some devices heard and some not;
  - while MQTT is offline;
  - the same command id twice (the backend reuses `decom-b-002`);
  - an unknown id.
- **Empty hub:**
  - remove the last device, then boot unprovisioned;
  - MQTT drop and reconnect while unprovisioned;
  - re-provision after 1 and after 10 heartbeats;
  - "decommission all" and its reboot.
- **Valve lifecycle:**
  - valve-only decommission with the link still up;
  - decommission, then re-provision the same valve or a different one (check the `s_valve_pub_wet/linked` detectors).
- **Valve battery:**
  - the sequence unknown→65→10→11→10→disconnect→reconnect (unknown again)→9;
  - a missing battery characteristic;
  - reseed during setup;
  - flapping at 10/11 against the 60 s alert debounce. Debounce has no trailing edge, so prove the cloud ends with the correct final rating.
- **Concurrency:** C2D (esp-mqtt task) against iothub_task, the health task and the NimBLE host. Check lock order, priority inversion and long lock holds.
- **Heap:** snapshot build with cJSON returning NULL; empty snapshots under low heap; pulse snapshots per advertisement under low heap.

Fix each confirmed finding as a separate `fix(review): …` commit. A finding that needs
work outside the approved scope goes to me via `AskUserQuestion` first.

### Phase F — Agent council (regression hunt)
Five independent specialists review the **final** diff in the context of the whole
codebase and vote SHIP / BLOCK with evidence:
1. RTOS and concurrency.
2. Memory and robustness (heap, stack, cJSON ownership, NULL paths).
3. Cloud contract and UI sync. Every D2C message against `docs/telemetry/schemas/*.json`
   and §2: no unapproved changes, and the UI-sync behaviour (Syncing reason,
   per-advertisement snapshots) is intact.
4. BLE lifecycle (valve link and bond, scanner whitelist, NimBLE host-context rules).
5. Persistence and upgrade (OTA from 2.1.3 with devices provisioned, power loss
   mid-decommission, the "decommission all" path).

BLOCKs go back through D/E. **At most 2 E/F rounds.** After that, stop and give me the
remaining BLOCKs with evidence for a decision.

### Phase G — Deliver
1. Version bump, CHANGELOG, and updated docs, schemas and validators (confirm with a grep for `2\.1\.3` and `1\.8\.0`).
2. Build summary: warnings, and the size delta against 2.1.3.
3. `docs/field_logs/2.1.4/BENCH_TEST.md`. For every matrix scenario: the C2D payload, the
   expected UART lines, and the expected IoT Hub JSON (full JSON for the key ones).
   - Valve-battery test: step a bench PSU through ≈5.45 V (Low) before ≈5.35 V (Critical).
     The valve reads every 10 min while Good, then every 60 s once Low, so expect up to 10
     min to the first reading.
4. Do not push and do not open a PR unless I ask.
5. Final summary:
   - what changed and why, with evidence;
   - the hypotheses that were refuted;
   - residual risks;
   - latent bugs deferred to a later release.

Post a one-line status at each phase boundary. When unsure, ask; do not guess.

=== END PROMPT ===
