# Claude Code (VS Code) prompt: `valve_battery_critical` event + coupled snapshot

> Paste everything below the line into Claude Code in VS Code, opened at the repo root
> (`eFloStop_WiFiHub_idf1`). Start in **Plan mode** (Shift+Tab until it says plan mode). The
> phrase "use a workflow" in Phase 3 is deliberate: it opts in to multi-agent orchestration.
>
> The facts in "Verified code facts" were checked against the tree at FW 2.1.3 (commit `ae4d59a`)
> by a multi-agent review. Line numbers are approximate and drift, so re-verify before relying on one.

---

## Role

You are a **principal firmware engineer** and the **orchestrator** for this change on the eFloStop 2
Wi-Fi Hub (ESP32, ESP-IDF v5.5.1, NimBLE, esp-mqtt to Azure IoT Hub, schema `eflostop.v2`). You do
not work alone. You direct **subagents as your subordinates**: you brainstorm with them, hand them
research and implementation, and have them attack your work. You own the plan, the decisions, the
integration and the final call. Subagents report to you and you check what they return. A subagent's
claim is a lead until you or another agent have confirmed it against the code.

## The feature

Add a new D2C event, **`valve_battery_critical`**, published when the valve's battery reaches a
critical level. It must be **followed by a snapshot** so the app UI shows the new battery value right
away. Events feed the app's log and push notifications; `type:"snapshot"` is the single source of
truth that refreshes the UI.

Push delivery is decided by the **backend (Watts Digital)**, not by the firmware. `cmd_ack` is also
`type:"event"` (`docs/telemetry/messages_data.py` ~318-324), so the backend chooses push triggers by
`data.event` name. A new event name produces no push until the backend is told about it (see D12).

Reference only, for the envelope and style (a BLE sensor `leak_cleared`; **not** the new payload):

```json
{
  "schema": "eflostop.v2",
  "ts": 1790653747,
  "gateway": { "id": "GW-7C4FADAE69C8", "short_id": "69C8", "fw": "2.1.4", "uptime_s": 495 },
  "type": "event",
  "data": {
    "event": "leak_cleared",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:2A:CB:B6",
    "leak_state": false,
    "battery": 93,
    "location": { "code": "bathroom", "label": "Main Bathroom" },
    "rssi": -50
  }
}
```

**Required payload for the new event. I have approved this shape; it is the contract, not a
suggestion:**

```json
{
  "schema": "eflostop.v2",
  "ts": 1790653800,
  "gateway": { "id": "GW-7C4FADAE69C8", "short_id": "69C8", "fw": "<bumped PROJECT_VER>", "uptime_s": 548 },
  "type": "event",
  "data": {
    "event": "valve_battery_critical",
    "source_type": "valve",
    "valve_id": "00:80:E1:27:F7:BB",
    "leak_state": false,
    "battery": 9,
    "valve_state": "open",
    "rmleak": false,
    "fw_version": "x.y.z"
  }
}
```

Payload rules:
- Same envelope as every other event (`build_envelope("event")`: `schema`, `ts`, `gateway{…}`, `type`).
- `data` carries **exactly these keys, in this order**: `event`, `source_type`, `valve_id`,
  `leak_state`, `battery`, `valve_state`, `rmleak`, `fw_version`. There is **no `location`** and
  **no `rssi`**. No other key may be added without my approval (for example a `threshold`).
- Key types and values:
  - `event`: the literal `"valve_battery_critical"`.
  - `source_type`: `"valve"`, from `leak_source_to_str(LEAK_SOURCE_VALVE)`, not a new literal.
  - `valve_id`: the uppercase valve MAC, under the key from `identity_key_for_source(LEAK_SOURCE_VALVE)`.
  - `leak_state`: bool, the valve flood-probe state.
  - `battery`: integer percent, the reading that triggered the event.
  - `valve_state`: `"open"`, `"closed"` or `"unknown"`.
  - `rmleak`: bool.
  - `fw_version`: the valve firmware string. It is **omitted** (the key is absent, never `null`) when
    the DIS read failed, the same rule the valve leak event uses (`vlk_have_fw`). It is the only
    conditional key, so the valid forms are the 8-key list above or the same list without its final
    `fw_version`.
- Every value comes from **one sample taken in `iothub_task`** in the same loop iteration that
  evaluates the threshold (`vlk_mac`, `vlk_batt`, `vlk_wet`, `vlk_state`, `vlk_rmleak`, `vlk_fw` +
  `vlk_have_fw`). Nothing is re-read live at publish time. `battery` must equal the value that tripped
  the threshold. **Today that sample is only taken for `BLE_UPD_LEAK`** (F2); extending it is part of
  this work.
- The key list is the valve `leak_detected` payload with `location` removed. Neither existing
  publisher produces it as-is:
  - `telemetry_v2_publish_leak_event()` always emits `location`, via `add_location_for_source()`.
  - `telemetry_v2_publish_valve_event()` uses a different key order (`valve_state` before `battery`)
    and re-reads live values through the `ble_valve_get_*()` getters.
  How to produce it without a drifting hand-rolled copy is decision **D8**.
- Additive only. The schema stays `eflostop.v2`, and no existing message changes shape or key order.

## Known conflict: raise it at GATE 1, do not resolve it silently

`docs/valve_link/SCOPE.md` (the agreed scope for hub FW 2.2.0) records:
- **valve_link D7 "No new event names"**: the wire surface is frozen (~line 92).
- `device_offline` as the only push trigger the backend is asked to use (§4.1, ~170-173).
- **valve_link D8 "`rating` drives the app tile colour"** (~93).
- That no semantic change ships without a DOC item and a **named notice to Watts Digital**, which
  is release-blocking (~399).

`valve_battery_critical` is a new event name, so it conflicts with valve_link D7. Do not drop the
feature, and do not quietly ignore valve_link D7. Present it at GATE 1 as **D12**, with your recommendation
(expected: proceed as a documented exception, with the Watts Digital handoff below), and let me decide.

## Verified code facts (re-verify, then build on them)

- **F1 Hook.** `main/iothub/app_iothub.c` (valve BLE-update dispatch, ~2605) says
  `// BLE_UPD_BATTERY: no event — included in snapshot`. The `BLE_UPD_LEAK` branch is the pattern
  to copy: publish the event from the Phase 2 sample, then
  `snap_request(SNAP_EVENT, SNAP_TIER_HIGH, "<event>")`, gated by a delta latch (`s_valve_pub_wet`).
  The `BLE_UPD_STATE` sibling also latches (`s_valve_pub_state`) but re-reads
  `ble_valve_get_state()` live, so do not copy that part. Update the F1 comment when you add the event.
- **F2 The sample only exists for LEAK.** Only `vlk_mac`/`vlk_mac_ok` are filled for every valve
  update type (~2319-2341). `vlk_wet`, `vlk_state`, `vlk_rmleak`, `vlk_batt`, `vlk_fw` and
  `vlk_have_fw` are filled **only** inside `if (has_valve && ble_upd_type == BLE_UPD_LEAK)`
  (~2343-2367), the same block that calls `rules_engine_evaluate_leak()`. For any other type they
  keep their initializers (`vlk_batt = 0`, `vlk_state = -1`, `vlk_have_fw = false`, ~2270-2277). Extend
  the value sample to `BLE_UPD_BATTERY` and to the D4 link-up evaluation **without** running
  `rules_engine_evaluate_leak()` for those types. An unsampled `vlk_batt` of 0 must never reach the
  threshold check.
- **F3 Zeros that are not readings.** In `main/ble_valve/app_ble_valve.c`, `g_val_battery` starts at
  0 and is reset to 0 at **both** ends of a link: the GAP **CONNECT** handler (~1409) and the
  **DISCONNECT** handler (~1464). After the connect-time reset, `g_valve_mac` is set at once (~1416),
  so `vlk_mac_ok` is already true during security, discovery and setup while battery is still 0.
  - A 0 can also survive to *ready*. That happens when the setup battery read fails (the read-failure
    path leaves the cache untouched, and setup still advances) or when the battery characteristic was
    not discovered (`h_batt_char == 0` skips steps 4 and 8).
  - A `BLE_UPD_BATTERY`/`LEAK` update still queued from the previous session can be dequeued after
    the reconnect's CONNECT, with `vlk_mac_ok` true and battery 0.
  - **A 0 from a reset or a missing read is not a battery reading.** `vlk_mac_ok`, "after ready" and
    "value != 0" are each insufficient on their own (D5).
- **F4 Change-only notify.** `BLE_UPD_BATTERY` is posted only when the value **changes**, and never
  during setup (`!g_setup_in_progress`, ~560). A valve that links up already critical produces no
  battery update.
- **F5 Boot ordering can lose link-up edges.** `iothub_task` starts the valve link
  (`app_ble_valve_signal_start()`, ~2043) before SNTP and the bounded DPS boot loop (~2073-2079).
  It then drains `ble_update_queue` just before the event loop (~2094). On a slow boot (uncached DPS,
  no WAN or clock yet), the valve reaches ready first, and its link-up updates are discarded. The
  fast-snapshot arming avoids this by polling `ble_valve_is_ready()` every iteration (~2658). The
  existing leak link-up announcement has the same gap: report it, do not fix it here (D11).
- **F6 Health and thresholds.** Thresholds live in `main/health_engine/health_engine.h`
  (`HEALTH_BATTERY_WARN_PCT 20`, `HEALTH_BATTERY_GOOD_PCT 35`). Put the new critical threshold there,
  as the single source of truth. The health engine raises alerts only on transitions into or out of
  **CRITICAL** (`health_engine.c` ~336). A valve battery rates **WARNING** at most (~212-217). So the
  battery crossing never raises a health event, and the snapshot's `valve.rating` stays `"warning"`
  while the push says critical.
- **F7 Scheduler and labels.** `snap_request` runs only in `iothub_task`. Never call it from the
  NimBLE host task or the esp-mqtt event task. Events are published **before** the coupled snapshot
  (the flush block's ordering invariant); keep it. `snap_request()` keeps the first label unless a
  later request has a strictly earlier deadline, and SNAP_FAST (armed on every boot and MQTT
  reconnect once the valve is ready) always overwrites it (~720-772; FIX 7 in
  `docs/event_snapshots/FEATURE_TRACKER.md`). So `SNAP trigger=event:valve_battery_critical` is
  guaranteed only for a lone crossing on a live link.
- **F8 The snapshot reads battery live.** At flush time the snapshot calls `ble_valve_get_battery()`
  (`telemetry_v2.c` ~712), up to `SNAP_MIN_INTERVAL_MS` (5 s) after the event. It emits `battery` only
  while `ble_valve_get_mac()` succeeds; otherwise the valve shows `"disconnected"` with no battery.
- **F9 Delivery is not guaranteed.**
  - Before the first SNTP sync, `build_envelope()` returns NULL and the event is destroyed, never
    buffered (`telemetry_v2.c` ~79-84). `iothub_task` can enter its loop after `initialize_sntp()`
    gives up (~120 s), so this is reachable on a boot with no WAN.
  - Online, a publish with `msg_id < 0` is only logged. The offline NVS buffer is a 16-slot ring that
    overwrites its oldest entry.
  - Until esp-mqtt notices a dead link (60 s keepalive), `publish_json()` takes the online branch and
    gets `msg_id >= 0`. esp-mqtt later deletes unacked outbox items after 30 s
    (`OUTBOX_EXPIRED_TIMEOUT_MS`) and on `esp_mqtt_client_stop()`. So `msg_id >= 0` does not mean
    delivered.
  - Event publishers return `void`. `publish_json()`'s bool is true only for "sent" (false for
    buffered), and the snapshot heartbeat re-arm depends on that meaning, so do not change it.
  - The valve leak branch writes its latch **before** publishing (~2566).
- **F10 Logging.** This build compiles out `ESP_LOGD` (`CONFIG_LOG_MAXIMUM_LEVEL=3`; see the
  `SNAP deferred` comment in `app_iothub.c`). Every marker the test plan asserts must be INFO or WARN.
  Log suppressions on the edge only, so they stay low-rate.
- **F11 C2D context.** `decommission` and `provision` run in `handle_c2d_command` on the **esp-mqtt
  event task** (~941-944, ~1121). An `iothub_task`-owned latch must not be written there; use the
  flag-and-consume pattern (~101-114).
  - `provision` also adds sensors and changes rules, and it keeps a connected valve linked
    (`reseed_valve_health_if_connected()`, ~628-641, ~1039), so no link-up follows.
  - `decommission` target `valve` does not reboot (~874-886). `decommission all` reboots (~927-953).
- **F12 Build config.** The repo has no project `Kconfig.projbuild` and no release/debug profile.
  `sdkconfig` is gitignored and only `sdkconfig.defaults` is tracked. A Kconfig option that
  "defaults to off" still persists silently in a developer's local `sdkconfig` after one menuconfig
  toggle.
- **F13 Docs and contract.**
  - The live D2C wire spec is `docs/telemetry/messages_data.py`. `python docs/telemetry/build_messages.py`
    (needs `python-docx`) renders it into `telemetry_messages.md` and
    `eFloStop2_Telemetry_Messages_v<DOC_VERSION>.docx`. **Never hand-edit `telemetry_messages.md`**;
    it is generated. Whether to bump `DOC_VERSION`/`PREV_*` is a GATE 1 decision.
  - `TELEMETRY_REFERENCE.md`, `telemetry_catalogue.md`, the catalogue .docx, `fields.json`,
    `field_registry.csv` and `schemas/*.json` are **stale records of FW 1.8.0**, deliberately left
    alone (`docs/telemetry/TEST_PLAN_1_9_0.md` §6). Do not edit them, and do not run
    `build_catalogue.py`: it re-stamps every output with the current `PROJECT_VER` and enforces a
    91-field count. `TELEMETRY_REFERENCE.md` §9.3 (~2079-2083, also ~1522, ~1799) says a valve battery
    change produces no event. List those statements in the plan and let me decide at GATE 1 whether
    to annotate them.
  - No doc says every device event carries `location`. The "required core" statements
    (`telemetry_v2.h` ~147-150, `telemetry_v2.c` ~165-169, `messages_data.py` ~185-193,
    `validate_capture.py` ~148-153) cover only `leak_detected`/`leak_cleared` and must stay unchanged.
    Document `valve_battery_critical` as a **valve-family** event next to `valve_state_changed` (which
    also has no `location`), not as a leak-family exception.
  - The hub version is `PROJECT_VER` in the top-level `CMakeLists.txt` **only**.
- **F14 Validator (`docs/telemetry/validate_capture.py`).**
  - `LEAK_EVENTS` requires `location` (~152). **Do not** add the new event to it. Give it its own
    rule:
    - `list(data)` equals the approved list, or that list without `fw_version`; `fw_version` is
      never `null`;
    - `source_type == "valve"`, identity `valve_id`, no `location`, no `rssi`;
    - `leak_state` and `rmleak` bool, `battery` int, `valve_state` in {open, closed, unknown};
    - `battery` compared with the D1 operator against the threshold **parsed from**
      `#define HEALTH_BATTERY_CRITICAL_PCT` in `health_engine.h`, never hard-coded.
    - Add a self-test with both the 8-key and the 7-key forms.
  - `EXPECTED_FW = "2.1.0"` is hard-coded (~21) and fails every message whose `gateway.fw` differs
    (~93-94, "stale flash?"). As shipped, it fails every capture from this feature. Read
    `PROJECT_VER` instead, reusing `fw_version()` from `build_messages.py` (~64-69), with a
    `--fw X.Y.Z` override for older captures. Until this is fixed, a `gateway.fw` mismatch is not a
    firmware defect.
  - It opens captures as UTF-8. Make it detect a UTF-16 or UTF-8 BOM (Windows PowerShell `>` writes
    UTF-16LE). Also make it unwrap a `payload` that arrives as a JSON **string**: the hub sends no
    content-type, so `az` prints escaped payloads unless run with `--content-type application/json`.
- **Feature docs convention.** `docs/<feature>/{PLAN,DECISIONS,FEATURE_TRACKER,TEST_PLAN}.md`. Use
  `docs/valve_battery_critical/`. Read `docs/event_snapshots/` and `docs/health_leak_led/` first: they
  are the house style for plans, trackers and test plans.

## Workflow: follow these phases in order and respect every gate

### Phase 0: Research fan-out (parallel read-only subagents)

Launch these **in parallel** (Explore or general-purpose agents, read-only). Each confirms or
corrects the relevant F-facts and returns a `file:line` map and facts only, with no proposals:

1. **Event pipeline and delivery** (F7, F8, F9): event builders, `build_envelope`, `publish_json`,
   `offline_buffer/`, the replay order vs lifecycle and snapshot, and the esp-mqtt outbox settings.
2. **Valve battery path** (F2–F5): every read and write of `g_val_battery`, the GATT read and notify
   flow, the setup steps, ready vs connected, `ble_update_queue` depth and drop behavior, the boot
   drain, and whether the valve can legitimately report 0%.
3. **Scheduler, latches and C2D** (F1, F7, F11): `snap_request`, tiers, rate caps, where
   `s_valve_pub_*` latches are reset and why, and the cross-task flag patterns.
4. **Health, contract and docs** (F6, F13, F14): the valve battery rating, `messages_data.py`
   structure, and the validator branches that will see the new event.

Put the merged findings in a **Research** section of your plan. Plan mode can only write its own plan
file, not repo files. In Phase 2 that section becomes `docs/valve_battery_critical/PLAN.md` § Research.

### Phase 1: Brainstorm, then plan as principal engineer (Plan mode, no code edits)

- Spawn **2–3 Plan agents**, each told to design a different approach (for example: A, a minimal
  hook at `BLE_UPD_BATTERY` plus a level check at ready; B, a small valve-battery alarm state machine
  in `iothub_task` fed by every valve sample; C, whatever they find better). Each design must address
  F1–F14.
- Critique them against each other. Pick or synthesize one and explain why in the plan.
- The plan must **explicitly decide** each of the points below, with a recommendation and a one-line
  why. I approve or change them at the gate:
  - **D1 Threshold.** Recommend `HEALTH_BATTERY_CRITICAL_PCT 10` in `health_engine.h`. Compare
    ≤ vs <; the validator and tests follow whichever I approve.
  - **D2 Re-arm and hysteresis.** Fire once per crossing. Re-arm only after a clear recovery (for
    example ≥ WARN, meaning the battery was replaced), so a value jittering 9↔10↔11 does not spam
    pushes.
  - **D3 Latch lifetime.**
    - Across reconnects: recommend it survives (no repeat on link flaps).
    - Across reboot: RAM (one reminder per boot) vs NVS.
    - Reset only when the valve is removed (`decommission` target `valve` or `all`) or the
      provisioned valve MAC changes, **not** on every `provision` (F11).
    - Say whether a valve that is still critical fires again after a reset, and what triggers that
      evaluation while the link stays up.
    - Never write the latch from the C2D handler (F11).
  - **D4 Already critical at link-up or boot.** Recommend firing once per ready session, after the
    valve is *ready* (not merely GAP-connected). Evaluate **by level** in `iothub_task`, polling
    `ble_valve_is_ready()` like the fast-snapshot arming, with a sample taken in the same iteration.
    Do not rely only on a queued link-up update (F5).
  - **D5 Valid-reading rule.** Reject reset zeros and never-read values (F3) while still letting a
    real 0% fire. For example, a battery-valid flag set only when a value actually arrives in
    `on_notify()` / the setup read, cleared at both reset sites, and sampled with `vlk_batt`.
  - **D6 Recovery event** (for example `valve_battery_ok`). Recommend none, since the snapshot
    already carries battery. State the tradeoff for the app team in the D12 handoff.
  - **D7 Delivery and latch commit** (F9). Decide when the latch commits. Recommendation: the new
    publisher reports whether the event was **sent or buffered** (a new signal; do not change
    `publish_json()`'s meaning). Commit the latch only on that result. Otherwise keep the alarm
    pending and retry it, with its coupled snapshot, on a later `iothub_task` iteration from the
    triggering sample. Keep the event ordered before its snapshot. State the pre-SNTP behavior and the
    undetected-outage window (accept the same exposure as leak events, or commit only on PUBACK via
    `MQTT_EVENT_PUBLISHED`).
  - **D8 Publisher.** How to emit the approved payload (no `location`, exact key order) while reusing
    the existing core instead of copying it. Options:
    - factor the shared core out of `telemetry_v2_publish_leak_event()` behind a helper that lets the
      caller skip `location`;
    - add a dedicated `telemetry_v2_publish_valve_battery_event()` built on that same helper.

    Existing leak and `valve_state_changed` payloads must stay **byte-for-byte identical**. State
    which option you chose and prove the no-change claim.
  - **D9 Test injection.** A way for me to force battery values on the bench, without draining a
    real battery, that drives **every** Phase 6 B case. Show how, per B case. The forced value must:
    - (a) enter through the same paths a real reading takes (the `on_notify()` change check and the
      setup read), not by writing `vlk_batt` or calling the evaluator, so the snapshot, the health
      engine and the event agree (F8) and the disconnect reset (B5) is not hidden;
    - (b) stay applied across valve reconnects (B6, B7);
    - (c) survive a hub reboot (B8);
    - (d) be changeable while Wi-Fi or MQTT is down (B9), which a C2D command alone cannot do.

    State whether it holds against real valve notifies (a sticky override) and how B2–B4 account for
    that. It must be **compiled out of the shipped image** in a way that a local `sdkconfig` cannot
    re-enable by accident (F12), and B13 must prove it.
  - **D10 Version bump.** Read the current `PROJECT_VER` and bump one patch. The reference payload
    shows `2.1.4`, so the tree may already be there; do not assume.
  - **D11 Scope.** LED and health rating are unchanged, so `valve.rating` stays `"warning"` (F6).
    Report, do not fix, the pre-existing gaps you find (the F5 leak link-up drain gap, the F9 outbox
    loss for all events).
  - **D12 valve_link D7 conflict and the Watts Digital handoff.** See "Known conflict". If I approve
    proceeding, the deliverable is `docs/valve_battery_critical/APP_HANDOFF.md`, written in Phase 3
    and reviewed by the Contract Owner seat in Phase 5. It states:
    - that the event is push-eligible, with the exact payload;
    - exactly when it fires (D1 operator and threshold, D4 link-up, D2 re-arm, D3 per-reboot
      repeat, D7 replay and loss cases), so the backend can dedupe;
    - that there is no recovery event, and the alert clears from the snapshot's `valve.battery`;
    - that `valve.rating` stays `"warning"` while the push says critical (valve_link D8).
  - **D13 Docs.** Whether this additive event bumps `DOC_VERSION`, and whether to annotate the stale
    `TELEMETRY_REFERENCE.md` statements (F13).
- Include an implementation step list (small, independently compilable steps), the files touched,
  risks, and the test matrix outline.
- **GATE 1: stop and present the plan (ExitPlanMode). Do not edit code until I approve.**

### Phase 2: Branch and checkpoint

- `git checkout master && git pull`, then `git checkout -b feature/valve-battery-critical`.
- Write and commit these three files. This is checkpoint **C0**.
  - `docs/valve_battery_critical/PLAN.md`: the approved plan, including § Research.
  - `DECISIONS.md`: D1–D13 as I approved or changed them at GATE 1.
  - `FEATURE_TRACKER.md`: a status table per step, like `docs/event_snapshots/FEATURE_TRACKER.md`.

### Phase 3: Implementation (use a workflow: multi-agent orchestration)

- Use a workflow to run the approved steps. Give each implementer agent **one step** with its exact
  files, the decision and F IDs it implements, and the invariants it must not break. Steps that touch
  the same file run sequentially. `messages_data.py`, validator and handoff work can run in parallel
  with firmware.
- Match the surrounding code: its comment density (this codebase explains *why* in comments), naming,
  cJSON idioms and log tags. Keep the diff minimal. Do not refactor anything unrelated.
- New UART markers are **INFO** (F10), greppable, and documented in the test plan. At minimum:
  - one line when the event fires, with the battery %, the threshold, and the reason (crossing or
    link-up);
  - one line on the edge when a candidate is suppressed by the latch, an invalid reading or
    hysteresis;
  - one line when the latch re-arms or resets;
  - one line when a pending alarm is retried (D7).
- Contract work in this phase:
  - update `messages_data.py` and regenerate with `build_messages.py`;
  - make the F14 validator changes (the new rule, `EXPECTED_FW` from `PROJECT_VER`, BOM detection,
    string-payload unwrap, and the self-test) **before** any Phase 6 verification;
  - update the F1 comment;
  - write `APP_HANDOFF.md` if D12 is approved.
- After **each** step:
  1. `idf.py build` must pass with **zero new warnings**. If you cannot run the build in this
     environment, ask me to run it and paste the output.
  2. Commit the checkpoint (`feat(valve-battery): …`, `docs(telemetry): …`, matching the repo's
     commit style).
  3. Update `FEATURE_TRACKER.md`.

### Phase 4: Adversarial review (independent skeptic subagents on the actual diff)

Spawn **at least 6 skeptic agents in parallel**. Each gets `git diff master...HEAD` and one lens.
Each must try to **break** the change and report concrete failure scenarios (inputs or state →
wrong output) with `file:line`:

1. **False alarms**: connect-time and disconnect-time zeros, a failed or skipped setup read, a stale
   update from the previous session, reprovision to a different valve, a `provision` that only adds a
   sensor, the test hook present in the shipped image.
2. **Missed alarms**: already critical at boot or link-up (including the F5 boot drain), notify
   suppressed during setup, `ble_update_queue` full, a latch that never re-arms.
3. **Delivery**: a pre-SNTP drop, an online `msg_id < 0`, ring overflow, the undetected-outage
   window, and a latch committed on an event that was never sent or buffered (D7).
4. **Concurrency and ordering**: the task context of every new call, NimBLE preemption between
   sample and publish, the C2D-task latch reset, event-before-snapshot ordering (online and offline
   replay), and the rate cap and coalescing with a simultaneous `device_recovered` or leak event.
5. **Contract drift**:
   - `data` keys exactly as approved, in order (8-key or 7-key form), with the stated types;
   - no `location`, no `rssi`, no `sensor_id`;
   - existing leak and `valve_state_changed` payloads byte-for-byte unchanged;
   - `messages_data.py` and the regenerated outputs consistent with the firmware;
   - the validator rule correct, and the new event kept out of `LEAK_EVENTS`;
   - the stale 1.8.0 artifacts untouched (unless D13 says otherwise);
   - `APP_HANDOFF.md` accurate against the code.
6. **Regression**: `leak_detected/cleared`, `valve_state_changed`, the `valve_linked/unlinked`
   snapshots, heartbeat re-arm, the fast boot snapshot, offline drain, and the memory and stack of
   `iothub_task`.

For each finding, have a **separate verifier agent** confirm or refute it against the code before
you act, so noise does not drive changes. Fix confirmed findings, re-build, add a checkpoint commit,
and re-review the fixed areas. Log every finding and its outcome in `FEATURE_TRACKER.md` (the
"Diff-review fixes folded in" format).

### Phase 5: Agent council: SHIP vs BLOCK

Convene a council of **5 agents**, each with a distinct seat: **Firmware Architect**, **BLE/Valve-link
Specialist**, **Cloud/App Contract Owner** (who also reviews `APP_HANDOFF.md`), **QA/Test Lead**, and
**Field Reliability Skeptic**. Each reads the diff, the plan, the decisions and the review log, then
votes **SHIP** or **BLOCK** with reasons.
- A BLOCK must cite a concrete defect or missing evidence (`file:line` plus a scenario). "I'd prefer"
  is not a BLOCK.
- Any valid BLOCK means you fix it and reconvene. After 2 rounds without consensus, stop and bring me
  the disagreement, both positions, and your recommendation.
- Record the vote table in `FEATURE_TRACKER.md`.
- **GATE 2: show me the council result and the final diff summary.** Commit, but **do not push or
  merge** until I say so. Merge to `master` with `--no-ff` only after my bench pass.

### Phase 6: Manual test plan (I run it; you verify my data)

Write `docs/valve_battery_critical/TEST_PLAN.md` in the house style (`docs/event_snapshots/TEST_PLAN.md`,
`docs/network_led/TEST_PLAN.md`):

- **Prerequisites table**: branch, fw version, valve MAC, sensor MACs, COM port, gateway/device ID,
  IoT Hub name, and how to enable, set and disable the D9 test hook.
- **Tooling**, exactly as I should run it:
  - UART: `idf.py -p COMx monitor`, saving the whole session to a file (say how). Keep the monitor's
    millisecond timestamps.
  - Cloud: `az iot hub monitor-events -n <hub> -d <device-id> --content-type application/json --properties all --timeout 0 | Out-File -Encoding utf8 iothub_<test>.log`
    (PowerShell).
    - `--content-type application/json` is required: without it, `az` prints escaped string
      payloads (F14).
    - `Out-File -Encoding utf8` avoids UTF-16 output.
    - VS Code *Azure IoT Hub → Start Monitoring Built-in Event Endpoint* is a fallback.
  - `ts` has 1-second resolution. Prove event→snapshot order from capture order or
    `annotations.x-opt-sequence-number` and UART ms timestamps, never from `ts` alone.
- **Serial markers to assert**: the new markers, plus the existing sequence `TELEMETRY_V2: Pub event:`
  → `IOTHUB: SNAP trigger=…` → `TELEMETRY_V2: Pub snapshot:` → `IOTHUB: SNAP heartbeat=reset`.
  - The label `SNAP trigger=event:valve_battery_critical` is guaranteed **only** for a lone crossing
    on a live link (B1, B4; F7).
  - The coupled snapshot can correctly log `event:valve_linked` at link-up (B6), `fast` after a hub
    reboot or reconnect (B8, B9), or another event's label when coalesced (B12, for example
    `event:health`). There, assert the first `Pub snapshot:` after the event and its valve `battery`,
    not the label.
  - Offline (B9), there is no `Pub event:`. Expect `TELEMETRY_V2: Offline — buffering event event`
    at the crossing. On reconnect, expect `Draining N offline event(s)…` and `OFFLINE_BUF: Replayed
    […]`, both before the snapshot.
- **Test cases** (IDs `B0…`), each with preconditions, steps, expected UART lines, expected IoT Hub
  JSON, and explicit **P/F criteria**. Hold each injected value for at least 10 s, and keep the valve
  linked until the coupled `Pub snapshot:` (F8). Cover at least:
  - **B0** Build and version banner.
  - **B1** Cross from above to at/below the threshold:
    - exactly **one** event, then **one** snapshot, with the event first;
    - the snapshot's valve `battery` equals the event's `battery`;
    - the event's `data` keys are exactly the approved list, in order, with no `location` and no
      `rssi`.
  - **B2** Further drops below the threshold: no new event (snapshots and heartbeats still update
    battery).
  - **B3** Jitter around the threshold: no repeat.
  - **B4** Recovery past the re-arm level, then a drop again: fires again.
  - **B5** Valve disconnect: **no** event from the internal reset to 0.
  - **B6** Valve links up already critical: one event after ready.
  - **B6b** Valve reaches ready with the setup battery read failed or no battery characteristic:
    **no** event.
  - **B7** Link flapping while critical: no repeats.
  - **B8** Hub reboot while critical, behavior per D3. Include a power-cycle with the WAN kept down
    until after SNTP gives up (~120 s): the alarm must still arrive once the clock is valid (D7).
  - **B9a** AP off: wait for `IOTHUB: WiFi down — stopping MQTT client` and
    `TELEMETRY_V2: MQTT connected = false`, then cross the threshold.
  - **B9b** WAN (or TCP 8883) blocked with the AP up: wait for `IOTHUB: Disconnected.` (up to ~90 s),
    then cross the threshold.
  - For both B9 variants: the event is buffered at the crossing, then replayed before the snapshot
    on reconnect, with exactly one event. Never cross before the disconnect marker; that window is
    the F9 outbox loss, not a defect of this feature.
  - **B10** Latch reset without a reboot:
    - (a) `decommission` target `valve`, then `provision` the same valve while it is still critical:
      behavior per D3.
    - (b) A `provision` that only adds a sensor, while the valve is critical and linked: no event
      now, and none on the next 1% drop.
    - `decommission all` reboots the hub and proves nothing about a RAM latch, so do not use it as
      B10 evidence.
  - **B11** Regression: leak detect and clear, `valve_state_changed`, heartbeat cadence unchanged,
    and existing payloads byte-for-byte unchanged.
  - **B12** Simultaneous health transition at link-up (F6):
    - With the valve battery already critical, keep the valve disconnected for at least 5 min, until
      `device_offline` is published.
    - Then reconnect it.
    - Expect `device_recovered` and `valve_battery_critical` from the same link-up, each event on the
      wire before the snapshot it couples, and neither missing.
  - **B13** Shipped image: build with the D9 hook disabled exactly as it will ship (a fresh `sdkconfig`
    from `sdkconfig.defaults`), and flash it.
    - Pass only if the hook's lines are absent and its trigger is rejected (or the symbol is absent
      from `build/config/sdkconfig.h`), and B0 and B11 pass on this image.
    - Record the version and the SHA-256 of the `.bin`.
  - If the DIS-failure (7-key) form cannot be forced on the bench, say so and rely on the validator
    self-test.
- **Cloud acceptance (not a B case)**: end-to-end push delivery depends on the backend acting on
  `APP_HANDOFF.md`. List it as an open acceptance item with its owner.
- **"What to send back" section**:
  - For tests that **expect an event**: the UART excerpt from the step-1 marker through the coupled
    snapshot, plus the IoT Hub capture lines, labelled with the test ID.
  - For tests that **expect no event** (B2, B3, B5, B6b, B7, B10b): the **complete, unfiltered** UART
    log and IoT Hub capture for a stated observation window. `TEST_PLAN.md` defines each window (for
    example, from the step-1 marker until one heartbeat after the last step) and names its opening and
    closing lines. Excerpts cannot prove absence.

**Verification protocol** (when I paste logs):
1. Run `docs/telemetry/validate_capture.py` (with the F14 fixes) on each IoT Hub capture. A file
   that is only encoded differently is never a reason for INCONCLUSIVE.
2. For each test ID, produce a table: *Assertion | Expected | Observed (quoted line, sequence
   number or UART timestamp) | PASS/FAIL/INCONCLUSIVE*. Check:
   - event counts;
   - event→snapshot ordering and latency;
   - `gateway.fw`;
   - the exact `data` key list, order and types vs the approved payload;
   - battery consistency between the event and its snapshot.
3. Missing evidence is **INCONCLUSIVE**, never PASS. Tell me exactly what to recapture. The same
   applies when a newer `[DATA] Battery=` line or a valve disconnect lands between the event and its
   snapshot: the equality check is then INCONCLUSIVE, not FAIL (F8).
4. On FAIL: root-cause it from the logs to `file:line`, propose the fix, and send it back through
   Phase 4 (review) before asking me to retest. Behavior that F7 or F9 documents as existing is not
   a FAIL of this feature.
5. Record results in `TEST_PLAN.md` and `FEATURE_TRACKER.md`, then add a checkpoint commit.

## Standing rules

- No code edits before GATE 1 approval. No push or merge before I say so.
- One source of truth for thresholds (`health_engine.h`) and for the version (`PROJECT_VER`); the
  validator reads both and copies neither.
- If an F-fact turns out wrong once you read the code, tell me and correct the plan. Do not quietly
  work around it.
- Keep `FEATURE_TRACKER.md` current at every checkpoint, so a fresh session can resume from it alone.
- Keep me updated in short lines at each phase boundary: what finished, what is next, and anything
  you need from me.
