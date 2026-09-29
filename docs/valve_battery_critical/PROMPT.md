# Claude Code (VS Code) prompt: `valve_battery_critical` event + coupled snapshot

> Paste everything below the line into Claude Code in VS Code, opened at the repo root
> (`eFloStop_WiFiHub_idf1`). Start in **Plan mode** (Shift+Tab until it says plan mode). The
> phrase "use a workflow" in Phase 3 is deliberate: it opts in to multi-agent orchestration.

---

## Role

You are a **principal firmware engineer** and the **orchestrator** for this change on the eFloStop 2
Wi-Fi Hub (ESP32, ESP-IDF v5.5.1, NimBLE, esp-mqtt to Azure IoT Hub, schema `eflostop.v2`). You do
not work alone. You direct **subagents as your subordinates**: you brainstorm with them, hand them
research and implementation, and have them attack your work. You own the plan, the decisions, the
integration and the final call. Subagents report to you and you check what they return. A subagent's
claim is a lead until you or another agent have confirmed it against the code.

## The feature

Push notifications in the eFloStop app are **event-driven**: the cloud turns `type:"event"` messages
into pushes, and `type:"snapshot"` messages are the single source of truth that refreshes the UI.
Add a new event, **`valve_battery_critical`**, published when the valve's battery reaches a critical
level. It must be **followed by a snapshot** so the UI shows the new battery value right away.

It follows the envelope and style of the existing leak events. Reference (a BLE sensor `leak_cleared`):

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
  - `fw_version`: the valve firmware string. It is **omitted** when the DIS read failed, the same
    rule the valve leak event uses. It is the only conditional key.
- Every value comes from **one sample**: the Phase 2 snapshot of valve state in `iothub_task`
  (`vlk_mac`, `vlk_batt`, `vlk_wet`, `vlk_state`, `vlk_rmleak`, `vlk_fw`). Nothing is re-read live at
  publish time. `battery` must equal the value that tripped the threshold.
- The key list is the valve `leak_detected` payload with `location` removed. Neither existing
  publisher produces it as-is:
  - `telemetry_v2_publish_leak_event()` always emits `location`, via `add_location_for_source()`.
  - `telemetry_v2_publish_valve_event()` uses a different key order (`valve_state` before `battery`)
    and re-reads live values through the `ble_valve_get_*()` getters.
  How to produce it without a drifting hand-rolled copy is decision **D8** below.
- Additive only. The schema stays `eflostop.v2`, and no existing message changes shape or key order.

## What I already know about the code (verify, then build on it)

- `main/iothub/app_iothub.c` (valve BLE-update dispatch, around line 2599) says
  `// BLE_UPD_BATTERY: no event — included in snapshot`. That is the natural hook. The sibling
  branches show the pattern to copy: sample once in Phase 2 (`vlk_batt`, `vlk_mac`, `vlk_mac_ok`),
  publish the event, then `snap_request(SNAP_EVENT, SNAP_TIER_HIGH, "<event>")`, gated by a delta
  latch (`s_valve_pub_wet`, `s_valve_pub_state`).
- `main/ble_valve/app_ble_valve.c`: `g_val_battery` is **reset to 0 on disconnect** (two reset
  sites, around 1409 and 1464) and starts at 0 before the first read. **A 0 from a reset is not a
  battery reading.** Do not raise a critical alarm on it.
- `BLE_UPD_BATTERY` is only posted when the value **changes** and is **suppressed during setup**
  (`!g_setup_in_progress`, around line 560). A valve that connects already at a critical level
  therefore produces no battery update. Handle the link-up/ready case, the same way the leak path
  handles a probe that is already wet at link-up.
- Thresholds live in `main/health_engine/health_engine.h`: `HEALTH_BATTERY_WARN_PCT 20`,
  `HEALTH_BATTERY_GOOD_PCT 35`. Put the new critical threshold there, as the single source of truth.
- The snapshot scheduler (`snap_request`, SNAP_MIN_INTERVAL_MS rate cap, coalescing tiers) only runs
  in `iothub_task`. Never call it from the NimBLE host task or the esp-mqtt event task. Events are
  published **before** the coupled snapshot (see the ordering invariant in the flush block). Keep it.
- Docs and contract: `docs/telemetry/` (TELEMETRY_REFERENCE.md event table, telemetry_messages.md,
  `schemas/`, `fields.json`, `field_registry.csv`, the `*_data.py` catalogue generators,
  `validate_capture.py`). The hub version is `PROJECT_VER` in the top-level `CMakeLists.txt` **only**.
- `validate_capture.py` treats `location` as "required core, every source" for `LEAK_EVENTS`. **Do
  not** add `valve_battery_critical` to `LEAK_EVENTS`, or the validator will fail every capture of it.
  Give it its own rule: the exact key list and order above, `source_type == "valve"`, the identity
  `valve_id`, no `location`, no `rssi`, and `battery` at or below the critical threshold. Anywhere the
  docs say "every device event carries `location`", add this event as a documented exception.
- Feature docs convention: `docs/<feature>/{PLAN,DECISIONS,FEATURE_TRACKER,TEST_PLAN}.md`. Use
  `docs/valve_battery_critical/`. Read `docs/event_snapshots/` and `docs/health_leak_led/` first: they
  are the house style for plans, trackers and test plans.

## Workflow: follow these phases in order and respect every gate

### Phase 0: Research fan-out (parallel read-only subagents)

Launch these **in parallel** (Explore or general-purpose agents, read-only). Each returns a
`file:line` map and facts only, with no proposals:

1. **Event pipeline**: `telemetry_v2.c/.h` event builders, `build_envelope`, `publish_json` (QoS,
   return value, offline behavior), `offline_buffer/` (are events buffered offline, in what order,
   and replayed before or after snapshots?).
2. **Valve battery path**: every read and write of `g_val_battery`, the GATT read and notify flow,
   `g_setup_in_progress`, the ready vs connected distinction (`ble_valve_is_ready()`), the
   `ble_update_queue` depth and drop behavior, and whether the valve can legitimately report 0%.
3. **Snapshot scheduler and latches**: `snap_request`, tiers, rate caps, where `s_valve_pub_*`
   latches are reset (connect, disconnect, decommission, reprovision, valve MAC change) and why.
4. **Health and contract**: how the health engine rates valve battery, whether a health event will
   also fire near the same moment, and every doc, schema or validator file that enumerates event
   names and must learn the new one.

Write the merged findings to `docs/valve_battery_critical/PLAN.md` § Research.

### Phase 1: Brainstorm, then plan as principal engineer (Plan mode, no code edits)

- Spawn **2–3 Plan agents**, each told to design a different approach (for example: A, minimal hook at
  `BLE_UPD_BATTERY` plus link-up evaluation; B, drive it from the health engine's battery rating; C,
  whatever they find better). Each design must address every pitfall above.
- Critique them against each other. Pick or synthesize one and explain why in the plan.
- The plan must **explicitly decide**, each with a recommendation and a one-line why, the points
  below. I approve or change them at the gate:
  - **D1 Threshold.** Recommend `HEALTH_BATTERY_CRITICAL_PCT 10`. Compare with ≤ vs <.
  - **D2 Re-arm and hysteresis.** Fire once per crossing. Re-arm only after a clear recovery (for
    example ≥ WARN, meaning the battery was replaced), so a value jittering 9↔10↔11 does not spam pushes.
  - **D3 Latch lifetime.** Across reconnects (recommend: survives, no repeat on link flaps); across
    reboot (RAM, so one reminder per boot, vs NVS); reset on decommission, reprovision or a
    different valve MAC.
  - **D4 Already critical at link-up or boot.** Recommend firing once after the valve is *ready*, not
    merely GAP-connected.
  - **D5 Valid-reading rule.** How a disconnect or reset 0 is told apart from a real 0% reading.
  - **D6 Recovery event** (for example `valve_battery_ok`). Recommend none, since the snapshot
    already carries battery, but state the tradeoff for the app team.
  - **D7 Offline behavior.** Buffered and replayed like leak events, with the event ordered before
    its snapshot.
  - **D8 Publisher.** How to emit the approved payload (no `location`, exact key order) while reusing
    the existing core instead of copying it. Options: factor the shared core out of
    `telemetry_v2_publish_leak_event()` behind a helper that lets the caller skip `location`, or add
    a dedicated `telemetry_v2_publish_valve_battery_event()` built on that same helper. Existing leak
    and `valve_state_changed` payloads must stay **byte-for-byte identical**. State which one you
    chose and prove the no-change claim.
  - **D9 Test injection.** How I can force battery values on the bench without draining a real
    battery: a Kconfig-gated debug hook (default **off**, compiled out of production) or a C2D debug
    command. It must be impossible to enable by accident in a release build.
  - **D10 Version bump.** Read the current `PROJECT_VER` and bump one patch. My sample payload shows
    `2.1.4`, so the tree may already be there; do not assume.
  - **D11 Scope.** LED and health rating unchanged unless you argue otherwise.
- Include an implementation step list (small, independently compilable steps), the files touched,
  risks, and the test matrix outline.
- **GATE 1: stop and present the plan (ExitPlanMode). Do not edit code until I approve.**

### Phase 2: Branch and checkpoint

- `git checkout master && git pull`, then `git checkout -b feature/valve-battery-critical`.
- Commit the approved `PLAN.md`, `DECISIONS.md` and `FEATURE_TRACKER.md` (a status table per step,
  like `docs/event_snapshots/FEATURE_TRACKER.md`). This is checkpoint **C0**.

### Phase 3: Implementation (use a workflow: multi-agent orchestration)

- Use a workflow to run the approved steps. Give each implementer agent **one step** with its exact
  files, the decision IDs it implements, and the invariants it must not break. Steps that touch the
  same file run sequentially. Docs, schema and validator updates can run in parallel with firmware.
- Match the surrounding code: its comment density (this codebase explains *why* in comments), naming,
  cJSON idioms and log tags. Keep the diff minimal. Do not refactor anything unrelated.
- New UART markers must be greppable and documented in the test plan. At minimum:
  - one INFO line when the event fires, with the battery %, the threshold, and the reason (crossing
    or link-up);
  - one DEBUG line when a candidate is suppressed by the latch, invalid reading or hysteresis;
  - one INFO line when the latch re-arms.
- After **each** step: `idf.py build` must pass with **zero new warnings**. If you cannot run the
  build in this environment, ask me to run it and paste the output. Then commit the checkpoint
  (`feat(valve-battery): …`, `docs(telemetry): …`, matching the repo's commit style) and update
  `FEATURE_TRACKER.md`.

### Phase 4: Adversarial review (independent skeptic subagents on the actual diff)

Spawn **at least 5 skeptic agents in parallel**. Each gets `git diff master...HEAD` and one lens.
Each must try to **break** the change and report concrete failure scenarios (inputs or state →
wrong output) with `file:line`:

1. **False alarms**: disconnect or reset zeros, setup-time reads, stale values after reconnect,
   reprovision to a different valve, the debug hook leaking into a release build.
2. **Missed alarms**: already critical at link-up or boot, notify suppressed during setup,
   `ble_update_queue` full, change while offline, a latch that never re-arms.
3. **Concurrency and ordering**: task context of every new call, NimBLE preemption between sample
   and publish, event-before-snapshot ordering (online and offline replay), snapshot rate cap and
   coalescing with a simultaneous health or leak event.
4. **Contract drift**: `data` keys exactly `event, source_type, valve_id, leak_state, battery,
   valve_state, rmleak, fw_version` in that order, with the types above; no `location`, no `rssi`,
   no `sensor_id`; existing leak and `valve_state_changed` payloads unchanged; docs, schemas,
   `fields.json` and `validate_capture.py` updated and consistent (and the new event kept out of
   `LEAK_EVENTS`).
5. **Regression**: `leak_detected/cleared`, `valve_state_changed`, `valve_linked/unlinked`
   snapshots, heartbeat re-arm, fast boot snapshot, offline drain, and memory or stack of
   `iothub_task`.

For each finding, have a **separate verifier agent** confirm or refute it against the code before
you act, so noise does not drive changes. Fix confirmed findings, re-build, add a checkpoint commit,
and re-review the fixed areas. Log every finding and its outcome in `FEATURE_TRACKER.md` (the
"Diff-review fixes folded in" format).

### Phase 5: Agent council: SHIP vs BLOCK

Convene a council of **5 agents**, each with a distinct seat: **Firmware Architect**, **BLE/Valve-link
Specialist**, **Cloud/App Contract Owner**, **QA/Test Lead**, **Field Reliability Skeptic**. Each
reads the diff, the plan, the decisions and the review log, then votes **SHIP** or **BLOCK** with
reasons.
- A BLOCK must cite a concrete defect or missing evidence (`file:line` plus a scenario). "I'd prefer"
  is not a BLOCK.
- Any valid BLOCK means you fix it and reconvene. After 2 rounds without consensus, stop and bring me
  the disagreement, both positions, and your recommendation.
- Record the vote table in `FEATURE_TRACKER.md`.
- **GATE 2: show me the council result and the final diff summary.** Commit, but **do not push or
  merge** until I say so. Merge to `master` with `--no-ff` only after my bench pass.

### Phase 6: Manual test plan (I run it; you verify my data)

Write `docs/valve_battery_critical/TEST_PLAN.md` in the house style (`docs/event_snapshots/TEST_PLAN.md`):

- **Prerequisites table**: branch, fw version, valve MAC, sensor MACs, COM port, gateway/device ID,
  IoT Hub name, and how to enable and disable the D9 test hook.
- **Tooling**, exactly as I should run it:
  - UART: `idf.py -p COMx monitor`, saving the session to a file (say how).
  - Cloud: `az iot hub monitor-events -n <hub> -d <device-id> --properties all --timeout 0 > iothub_<test>.log`,
    or VS Code *Azure IoT Hub → Start Monitoring Built-in Event Endpoint*.
- **Serial markers to assert**: the new markers plus the existing
  `IOTHUB: SNAP trigger=event:valve_battery_critical` / `SNAP heartbeat=reset` pair and
  `TELEMETRY_V2: Pub event:` / `Pub snapshot:`.
- **Test cases** (IDs `B0…`), each with preconditions, steps, expected UART lines, expected IoT Hub
  JSON, and explicit **P/F criteria**. Cover at least:
  - B0 build and version banner.
  - B1 cross above → at/below threshold: exactly **one** event, then **one** snapshot. The event comes
    first, and the snapshot's valve `battery` equals the event's `battery`. The event's `data` keys
    are exactly the approved list, in order, with no `location` and no `rssi`.
  - B2 further drops below threshold: no new event (snapshot and heartbeat still update battery).
  - B3 jitter around the threshold: no repeat.
  - B4 recovery past re-arm, then a drop again: fires again.
  - B5 valve disconnect: **no** event from the internal reset to 0.
  - B6 valve connects already critical: one event after ready.
  - B7 link flapping while critical: no repeats.
  - B8 hub reboot while critical: behavior per D3.
  - B9 crossing while Wi-Fi or MQTT is down: delivered on reconnect, event before snapshot, no duplicate.
  - B10 decommission and reprovision: latch resets.
  - B11 regression: leak detect and clear, `valve_state_changed`, heartbeat cadence unchanged.
  - B12 simultaneous health WARNING/CRITICAL transition: coalesced snapshot, no lost event.
- **"What to send back" section**: for each test ID, the UART excerpt (from the step-1 marker through
  the snapshot) and the raw IoT Hub JSON lines, labelled with the test ID.

**Verification protocol** (when I paste logs):
1. Run `docs/telemetry/validate_capture.py` (extend it if needed) on the IoT Hub capture where it
   applies.
2. For each test ID, produce a table: *Assertion | Expected | Observed (quoted line or `ts`) |
   PASS/FAIL/INCONCLUSIVE*. Check event counts, event→snapshot ordering and latency (`ts` and UART
   timestamps), the `gateway.fw` value, the exact `data` key list, order and types vs the approved
   payload (no `location`, no `rssi`), and battery consistency between event and snapshot.
3. Missing evidence is **INCONCLUSIVE**, never PASS. Tell me exactly what to recapture.
4. On FAIL: root-cause it from the logs to `file:line`, propose the fix, and send it back through
   Phase 4 (review) before asking me to retest.
5. Record results in `TEST_PLAN.md` and `FEATURE_TRACKER.md`, then add a checkpoint commit.

## Standing rules

- No code edits before GATE 1 approval. No push or merge before I say so.
- One source of truth for thresholds (`health_engine.h`) and for the version (`PROJECT_VER`).
- If a pitfall above turns out wrong once you read the code, tell me and correct the plan.
  Do not quietly work around it.
- Keep `FEATURE_TRACKER.md` current at every checkpoint so a fresh session can resume from it alone.
- Keep me updated in short lines at each phase boundary: what finished, what is next, and anything
  you need from me.
