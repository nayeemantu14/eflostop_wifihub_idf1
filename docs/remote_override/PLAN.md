# PLAN — Remote App-Initiated Water-Access Override (`override_enable`)

Gate 3 phased plan. **No source file is touched until you approve the specific phase.** Phase 3 (valve FW)
is **dropped** per Gate 2 Q10. Authority: `DECISIONS.md` (Gate 2 + review amendments + **LEAN scope**) + this plan.

> **LEAN SCOPE (user directive 2026-06-12):** ship *only* the override feature reachable from the app. Where
> this plan lists broader items (full D1–D6 event reconciliation, expanded notification matrix, enhanced audit
> record), they are **DEFERRED** per `DECISIONS.md` §SCOPE — LEAN. The firmware (Phase 2) was already minimal
> and is unchanged; the trimming is in the **SRS edit (Phase 3)**, now reduced to ~1 page.

Phases are independently approvable and testable:

| Phase | Title | Code? | Gate to start |
|---|---|---|---|
| **1** | Contract design freeze + Watts spec + **bench-verify prerequisite** | No firmware | your approval |
| **2** | Hub firmware implementation + bench tests (incl. button-vs-remote parity) | Yes (hub only) | your approval of Phase 1 outputs |
| **3** | Documentation + handover (SRS inserts, spec finalize, test evidence, TEST_PLAN.md) | No | your approval of Phase 2 evidence |

Both sub-agent reviews are attached **verbatim** in the Appendix; their launch-blockers are folded into
`DECISIONS.md` (Gate 3 amendments) and the phase scopes below.

---

## Phase 1 — Contract design freeze + Watts spec draft

**Goal:** Freeze the wire contract and produce `WATTS_DIGITAL_SPEC.md` (paste-ready for the SRS, **zero LoRa**),
so Watts can start backend + app work without firmware access. Includes the bench prerequisite that the whole
rollout story depends on.

### Scope
1. **OPEN-VERIFY-1 (hard prerequisite, you run it).** On a hub flashed with **released fw 1.3.0**, confirm the
   unknown-command behaviour before we document the capability-fallback. This was **read from code, not bench-verified**.
2. Freeze the final contract (command, cmd_ack frozen `error.detail` strings per Condition A option (a),
   event field set, snapshot, capability rule, notification matrix, audit requirements, all user-facing copy).
3. Write `docs/remote_override/WATTS_DIGITAL_SPEC.md`.

### Files (docs only — no firmware)
- `docs/remote_override/WATTS_DIGITAL_SPEC.md` (new)
- `docs/remote_override/DECISIONS.md` (already updated; finalize if the bench result forces a change)

### Frozen contract (the spec encodes exactly this)
- **Command** `override_enable` (envelope-only, no payload, `id` required).
- **`water_access_override_enabled`** event payload → `{event, trigger:"button"|"c2d_command", expires_ts (UTC epoch s), remaining_s}`. `remaining_s`/snapshot `override_remaining_s` = the **countdown basis**; `expires_ts` = absolute display only. `duration_h` removed; `expiry_epoch`→`expires_ts`. Retrofit `trigger:"button"` on both physical paths.
- **cmd_ack** `ok` = accepted/executed at hub (water-on confirmed later by `valve_state_changed{open}`). **Error signalling = option (a), full §5.2.7 alignment (DECISIONS Condition A):** `error.code`=`override_enable`, `error.detail` = one of **5 frozen human-readable strings** (DECISIONS A3) rendered verbatim by the app per **APP-FR-054 (untouched)**. **No `error.token` field; §5.2.7 unchanged.** Old hub: shipped `error.detail:"unknown command"`.
- **Snapshot** `override_active`/`override_remaining_s` unchanged (remote window surfaces for free).
- **Capability rule:** primary gate on `gateway.fw ≥ 1.4.0`; `unknown command` error ack = confirmation; **never** auto-fallback on a bare timeout; legacy `leak_reset→valve_open` fallback only behind an extra confirmation that names the re-close-while-wet caveat.
- **UI split:** override **banner** driven by the `water_access_override_enabled` event; **"water is ON"** driven only by `valve_state_changed{open}`/snapshot; a timed-out command reconciles against the next snapshot/event before showing failure.
- **Notification matrix (APP-FR-116/117):** push to **all household members**, on a **non-suppressible** safety channel, for **start (both triggers)**, **cancel**, **expiry (P1, copy branches wet/dry)**; **12 h mid-window P1 re-prompt** if a remote window is still wet. `auto_close_blocked_override` is timeline-only, never a per-minute push.
- **Audit (backend):** per accepted `override_enable`, store immutably: actor+auth, command ts + hub-exec ts, **leak-state snapshot at override time** (wet sensors + locations + incident id/age), **versioned confirmation copy accepted**, trigger, cancel/expiry+actor; show in in-app history.
- **Confirm flow:** 2-step (hazard → hold-to-confirm with leak location named); reuse banner/countdown/cancel; copy strings from product review §1/§6.

### Contract deltas vs today
| Item | Today | After |
|---|---|---|
| Command set | …`override_cancel` | + `override_enable` |
| `water_access_override_enabled` | `{expiry_epoch, duration_h}` (undocumented drift) | `{trigger, expires_ts, remaining_s}` |
| `gateway.fw` | `1.3.0` | `1.4.0` (capability signal) |

### Risks + rollback
- **R1 (high): OPEN-VERIFY-1 fails** (old hub silently drops unknown cmd) → capability *soft-probe* is impossible; spec rewrites to **min-fw gating only**. Mitigation: run it first; the spec's capability section is written *after* the result. Rollback: none needed (no code yet).
- **R2: spec drifts from firmware.** Mitigation: spec is generated from `DECISIONS.md` + FINDINGS cites; Phase 2 cross-checks each example against real device output before sign-off.
- **R3: LoRa leakage** into the Watts doc. Mitigation: explicit grep of the spec for `lora|sx1262|trigger.?mask bit 1` before delivery; trigger-mask documented as bits 0/2 only (matches current SRS).

### Acceptance criteria
- [ ] OPEN-VERIFY-1 executed on fw 1.3.0; result recorded in `DECISIONS.md` (pass → keep soft-probe; fail → min-fw-only).
- [ ] `WATTS_DIGITAL_SPEC.md` exists, contains: feature/user story; `override_enable` JSON in §5.3 style; cmd_ack success + every error example + the **frozen `error.detail` string table (DECISIONS A3, no token field, §5.2.7 unchanged)**; event JSON in `eflostop.v2` style (incl. retrofitted `trigger`); snapshot behaviour; §6.5-style step table; backend responsibilities (audit, push matrix, capability algorithm); APP-FR-043..048 + APP-FR-116/117 rows with priority + acceptance criteria; TC-012..014 + TC-N07..N09; traceability row; proposed SRS section numbers (§4.4.3, §4.10, §5.2.5, §2.7 note).
- [ ] Spec contains **zero** LoRa references (grep-clean).
- [ ] Both reviewer launch-blockers (App F1–F5, Product 1–5) each map to a spec row or an explicit "out of scope/deferred" note.

### Test procedure (Phase 1)
```bash
# OPEN-VERIFY-1 — on a hub running released fw 1.3.0:
az iot device c2d-message send -n <hub> -d <deviceId> \
  --data '{"schema":"eflostop.cmd","ver":1,"id":"verify-unknown","cmd":"override_enable"}'
az iot hub monitor-events -n <hub> -d <deviceId> --properties anno sys --timeout 30
# PASS: a D2C event {"data":{"event":"cmd_ack","id":"verify-unknown","cmd":"override_enable",
#                    "status":"error","error":{"code":"override_enable","detail":"unknown command"}}}
# FAIL: no cmd_ack within 30 s  -> capability story becomes min-fw-gating only.
```

---

## Phase 2 — Hub firmware implementation

**Goal:** Implement `override_enable` on the hub, reusing the existing window/persistence machinery, with full
parity to the physical button proven on the bench. Hub-only; no valve/sensor changes.

### Files to touch (hub)
| File | Change |
|---|---|
| `main/commands/c2d_commands.h` | `#define C2D_CMD_OVERRIDE_ENABLE "override_enable"` |
| `main/iothub/app_iothub.c` | new `else if` branch in `handle_c2d_command()`; map `override_enable_result_t`→the frozen `error.detail` **string** (DECISIONS A3) passed as the existing `error_msg` arg — **same pattern as every other command, no cmd_ack change**. Verify `valve_flood_detected` is published on BLE_UPD_LEAK (A-2); add it if missing (additive) |
| `main/rules_engine/rules_engine.h` | new `typedef enum override_enable_result_t`; declare `rules_engine_enable_override_remote(void)` |
| `main/rules_engine/rules_engine.c` | implement handler; add `const char *trigger` param to `start_override_window()` + update its 2 callers (tick→`"button"`, on_valve_connected→`"button"`); event fields `trigger`/`expires_ts`/`remaining_s`, drop `duration_h`; replace `OVERRIDE_WINDOW_DURATION_S` literal with Kconfig value |
| `main/rules_engine/Kconfig` (new) + `CMakeLists` wiring | `CONFIG_RULES_OVERRIDE_WINDOW_S` int, default 86400 |
| `main/telemetry/telemetry_v2.h` | `TELEMETRY_FW_VERSION "1.3.0"` → `"1.4.0"` (capability signal; also flows to twin reported) |

### Handler design (`rules_engine_enable_override_remote`)
Result enum → cmd_ack detail: `OK | NO_INCIDENT | VALVE_FLOOD | VALVE_DISCONNECTED | NOT_PROVISIONED | INTERNAL`.
Sequence (connectivity/reads **outside** the mutex; never hold the mutex during the ≤10 s wait):
1. `provisioning_is_provisioned()` && valve provisioned → else `NOT_PROVISIONED`.
2. **Connectivity:** if `!ble_valve_is_connected()` → `ble_valve_connect()`, poll `ble_valve_is_ready()` with `vTaskDelay(100 ms)` up to **10 s**; still not ready → `VALVE_DISCONNECTED`.
3. **Precondition:** allow if `rules_engine_is_leak_incident_active()` **OR** `ble_valve_get_rmleak_state()` **OR** window already active (idempotent refresh); else `NO_INCIDENT`.
4. **Flood floor:** `ble_valve_get_leak()` (valve's own probe, freshly seeded post-connect) → wet → `VALVE_FLOOD`.
5. **Execute:** take `g_mutex` → `g_leak_incident_active=false`+save, `g_auto_close_triggered=false`, `g_all_clear_since=0`, `start_override_window("c2d_command")` → give mutex → `ble_valve_set_rmleak(false)` → `ble_valve_open()` → `OK`.

**Why this order** (window→RMLEAK→open): the window-ACTIVE state makes `evaluate_leak` block auto-close and
makes `tick()` Check 2 skip (it only fires when override INACTIVE), so the hub does **not** misread the
RMLEAK 1→0 it is about to cause as a *physical* override and does not race-close on a concurrent leak. RMLEAK
must be cleared before the open or the valve rejects it (FINDINGS §6).

### Contract deltas (exact)
- New command `override_enable`; cmd_ack `error.detail` = the 5 frozen human-readable strings (DECISIONS A3) via the existing `error_msg` path — **§5.2.7 cmd_ack schema unchanged, no new field**.
- `water_access_override_enabled` = `{event, trigger, expires_ts, remaining_s}` (was `{event, expiry_epoch, duration_h}`). Retrofitted `trigger:"button"` on both physical paths.
- `gateway.fw`/twin `fw_version` = `1.4.0`.

### Risks + rollback
- **R4: 10 s connectivity wait blocks the iothub loop** (telemetry drain paused up to 10 s). Mitigation: `vTaskDelay`-based poll yields to other tasks (BLE task, health task run normally); only this command path waits; bounded at 10 s. Acceptable for a user-initiated command. Rollback: feature is one isolated branch + one new function — revert the 6 edits.
- **R5: renaming `expiry_epoch`→`expires_ts` / dropping `duration_h`** changes real wire output. Mitigation: fields are undocumented drift with no consumer yet (FINDINGS D1; DECISIONS Q7); rationalized now, frozen after. Rollback: trivial field rename-back.
- **R6: physical-button regression.** Mitigation: the only change to physical paths is adding a `trigger` arg to `start_override_window()`; behaviour identical. Acceptance includes a physical-button regression test with the hub offline.
- **R7: single-slot `g_pending_telemetry` clobber** if two rules events queue before drain. Mitigation: pre-existing; the handler emits one event; the iothub loop drains every iteration. Note only.

### Acceptance criteria
- [ ] Builds clean (`idf.py build`) — **you build/flash** (assistant cannot run `idf.py`).
- [ ] `override_enable` with a wet **sensor** + connected valve → cmd_ack `ok`; `water_access_override_enabled{trigger:"c2d_command", expires_ts, remaining_s≈86400}`; `valve_state_changed{valve_state:open, rmleak:false}`; snapshot `override_active:true`, `override_remaining_s≈86400`, `valve.state:open`.
- [ ] Each precondition returns the right cmd_ack `error.detail` string (DECISIONS A3, verbatim): no-incident, valve-flood, valve-disconnected (after ≤10 s), not-provisioned.
- [ ] Idempotent refresh: second `override_enable` during a window → cmd_ack `ok` + a **fresh** `water_access_override_enabled` (refreshed `expires_ts`).
- [ ] **Parity test (mandatory):** capture all valve GATT state (valve-state, RMLEAK, flood, battery) + hub NVS (`rules_eng`) + telemetry after a **physical button** override; repeat after `override_enable`; **diff must be equivalent** (valve open, RMLEAK clear, window active 24 h, incident cleared). Only the event `trigger` differs (`button` vs `c2d_command`).
- [ ] Reboot mid-window (network up) → snapshot `override_active:true` with correct `override_remaining_s`.
- [ ] **Reboot mid-window, NO network** → override **held**; after SNTP sync past a (debug-shortened) expiry → `water_access_override_expired`.
- [ ] Physical-button override still works with the **hub offline / BLE-degraded** (no regression).
- [ ] Enriched event confirmed ≤512 B in the offline buffer (send while MQTT down, reconnect, verify replay intact).

### Test procedure (Phase 2) — paste-ready
```bash
# Happy path (wet sensor, valve connected, RMLEAK locked):
az iot device c2d-message send -n <hub> -d <dev> \
  --data '{"schema":"eflostop.cmd","ver":1,"id":"ovr-en-001","cmd":"override_enable"}'
az iot hub monitor-events -n <hub> -d <dev> --timeout 40
# expect, in order:
#  cmd_ack            {"id":"ovr-en-001","cmd":"override_enable","status":"ok"}
#  event              {"event":"water_access_override_enabled","trigger":"c2d_command","expires_ts":<epoch+86400>,"remaining_s":86400}
#  valve_state_changed{"valve_state":"open","rmleak":false}

# Negative — valve flood probe wet:
az iot device c2d-message send ... --data '{"schema":"eflostop.cmd","id":"ovr-en-002","cmd":"override_enable"}'
# expect cmd_ack {"status":"error","error":{"code":"override_enable",
#   "detail":"Water detected at the valve. It can't be opened remotely until the valve area is dry."}}

# Negative — valve powered off (>=10 s):
# expect cmd_ack {"status":"error","error":{"code":"override_enable",
#   "detail":"The valve isn't responding. Check its power and connection, then try again."}} within ~12 s

# Pre-emptive (no incident):
# expect cmd_ack {"status":"error","error":{"code":"override_enable",
#   "detail":"No active leak to override. Use the normal Open Valve control."}}

# Debug expiry (build with CONFIG_RULES_OVERRIDE_WINDOW_S=120):
# after 120 s with sensor still wet -> water_access_override_expired{auto_close_resumed:true} + auto_close
```

---

## Phase 3 — Documentation + handover

**Goal:** SRS-ready inserts, finalize the Watts spec against real device evidence, ship the test-evidence pack.

### Files (docs only)
- `docs/remote_override/WATTS_DIGITAL_SPEC.md` (finalize with captured JSON)
- `docs/remote_override/TEST_PLAN.md` (new — full bench procedure)
- SRS-ready insert blocks (delivered in-repo as markdown for you to paste) — **LEAN set**: **§4.4.3** (feature, **APP-FR-043..045**), **§4.10** (**APP-FR-116** remote-start push only), **§5.2.5** (**single additive note**: `water_access_override_enabled` gains `trigger`/`expires_ts`/`remaining_s` — **NOT** the D1–D6 cleanup), **§2.7** (one-line additive note), **§11** traceability row. *(D1–D6 reconciliation, APP-FR-046..048, APP-FR-117, App A.3 = DEFERRED per LEAN scope.)*
- `CHANGELOG` entry per phase.

### Acceptance criteria (LEAN)
- [ ] Every JSON example in the spec/SRS inserts is a **real capture** from Phase 2 (not hand-written).
- [ ] `TEST_PLAN.md` covers: remote override w/ wet sensor; override while valve BLE-disconnected; hub reboot mid-window (persistence + `override_remaining_s`); reboot-no-network hold + post-sync expiry; window expiry re-arms auto-close (debug-shortened, documented); `override_cancel` interaction; physical press during an app window and vice-versa; idempotency; unknown-command on prod fw (OPEN-VERIFY-1); offline buffering ≤512 B.
- [ ] §5.2.5 carries the **single additive note** for `water_access_override_enabled` only; **D1–D6 cleanup NOT done** (deferred); **D9 LoRa never surfaced**.
- [ ] Traceability matrix row added; lean numbering: **APP-FR-043..045 + APP-FR-116 / TC-012 + TC-N07**.

### Risks + rollback
- **R8: SRS edit conflicts** with Watts' own edits. Mitigation: deliver inserts as standalone paste blocks with proposed section numbers; you own the merge + monday.com export + Bartosz briefing.

---

## Cross-phase constraints (apply to all)
1. **LoRa CONFIDENTIAL** — never in any Watts deliverable/diagram/discrepancy.
2. **Additive only** to the **documented** contract; physical-button path must not regress (works hub-offline/BLE-degraded).
3. **Full files, no placeholders** when implementing Phase 2.
4. Hub = ESP-IDF idioms, match neighbouring module style/logging/error handling.
5. Commit only at phase boundaries **after your explicit approval**; never push.

---

# Appendix A — App/Backend Integration Review (verbatim)

> Reviewer persona: senior Watts Digital backend+mobile engineer, no firmware access.

## A. Command + ack ambiguity (zero firmware access)
1. **`status:"ok"` timing vs. valve open is the biggest gap.** Ack = "accepted and executed," but valve-open is confirmed later by `valve_state_changed`/snapshot. No stated upper bound between ack and the confirming event, and no failure event if the valve never opens after an `ok` ack. The app gets a green ack, shows the banner, and the valve silently stays closed (e.g., BLE drops between "accepted" and "executed"). Define (a) max latency for the confirming `valve_state_changed`, and (b) what the hub emits if open fails after an ok ack.
2. **Audit log identity is missing.** Backend must log "per user/action," but neither command nor events carry a user/actor field. `trigger:"c2d_command"` tells you remote, not who. The correlation `id` is app-generated/opaque. State explicitly whether the hub echoes the originating `id` anywhere in `water_access_override_enabled` (it does not today).
3. **`no_incident` predicate is ambiguous.** "no active leak incident AND valve not RMLEAK-locked" — two-condition AND, but the app's gating signal is `valve.rmleak`. If RMLEAK can be false while an incident is "active" (or vice versa) the app shows/hides the override button inconsistently with the hub's accept/reject. The app needs ONE authoritative gating field; tell us which.
4. **`valve_flood_active` recovery is undefined.** Terminal or transient? No telemetry field exposes the valve flood-probe state to drive a live retry-enable, so the user is stuck guessing.
5. **`override_enable failed` is a free-text string, not a code.** Make it `internal_error`.
6. **Idempotent refresh ack is unspecified.** Does refresh return `status:"ok"`? No way to tell a fresh start ack from a refresh ack.
7. **`ver` field** — confirm the ack `ver` and whether unknown future `ver` is rejected or tolerated.

## B. Is every UI state derivable from telemetry?
- pending → derivable (app-local). success(accepted) → from `cmd_ack ok` (but that's "accepted," not "valve open"). each error → from `cmd_ack error.detail` *if* `override_enable failed` is tokenized.
- window-active → contract says the **event** drives the banner, but the **snapshot** also surfaces it — **two sources of truth**; pick one (recommend snapshot for state, event for push/copy).
- **"valve opened but then re-closed by its own flood probe"** → NOT cleanly derivable. After ok+open, if the valve flood goes wet and auto-closes, app sees `valve_state_changed{closed}` while `override_active` is still true → contradictory card. Need an explicit event/reason.
- **"override active but valve still shows closed"** → NOT derivable / stuck-state risk. Reachable via ack-ok-then-open-failed, flood re-close, or BLE drop mid-open. The single most dangerous UI state.

## C. Error enumeration & missing cases
1. Duplicate `cmd_ack` — state the at-least-once guarantee; dedupe on `id`.
2. **Ack-lost-then-snapshot reconciliation** — a lost ack on a succeeded command → the 20 s timer fires a false error toast. Spec must tell the app to reconcile a timed-out command against the next snapshot/`enabled` event before showing failure.
3. Idempotent refresh while a different correlation id is pending — define the dedupe key.
4. `override_enable` racing `override_cancel` — ordering/last-writer undefined.
5. `unknown command` detail (rollout) not in the §2 error list; tokenize → `unknown_command`.
6. No "valve opened but window failed to persist" case.

## D. Forward-compat / naming
1. Verb naming: `override_enable`/`override_cancel` pair is consistent; event family prefixes (`water_access_override_*` vs `auto_close_*`) are not — not blocking.
2. Field-gaining event is fine under "ignore unknown fields"; document additive and not-required.
3. **`expires_ts` (absolute) vs `override_remaining_s` (relative) — real problem.** A countdown off `expires_ts` requires phone/device clock agreement; the snapshot's relative value is clock-independent. **Add `remaining_s` to the event** and make `remaining_s`/`override_remaining_s` the countdown source of truth; keep `expires_ts` for absolute display; state it's UTC epoch seconds.

## E. Capability detection
"unknown command → error ack" is usable only with tokenization AND handling the no-ack case (ambiguous: offline vs old-silent-hub). **Do NOT trigger the destructive fallback on a bare timeout.** Algorithm: (1) read `gateway.fw`; ≥1.4.0 → send; on timeout show "hub unreachable," never fall back. (2) <1.4.0 → don't send; "update firmware"; offer legacy only behind explicit second confirm naming the re-close caveat. (3) fw unknown → treat unsupported. (4) treat only explicit `unknown_command` error ack as a capability signal; cache per-gateway. (5) strict semver parse. Gate on `gateway.fw` as primary; error ack as confirmation; never timeout as a go-signal.

## F. Top 5 concrete changes before sign-off
1. Define the post-ack open-confirmation contract (max latency + explicit failure event if valve doesn't open/re-closes after ok ack).
2. Add an explicit `valve_closed_flood` (or equivalent) event + expose valve flood-probe state in the snapshot.
3. Tokenize every error `detail` (`internal_error`, `unknown_command`, …); add `unknown_command` to the enumeration.
4. Specify reconciliation + idempotency (at-least-once; reconcile timed-out command vs snapshot; echo originating `id` + actor on the enabled event).
5. Add `remaining_s` to `water_access_override_enabled` alongside `expires_ts`; declare the relative value the single countdown basis. Plus: pick one authoritative banner source.

---

# Appendix B — Product/Safety Review (verbatim)

> Reviewer persona: product manager for a residential water-safety device.

## 1. Confirmation friction
Double-confirm is the right shape, but the proposed copy is too soft and the two steps not differentiated — users swipe through. Step 1 = informational + hazard, primary button is the SAFE choice. Step 2 = explicit leak-acknowledging action (hold-to-confirm / checkbox), surfacing the leak location. No bare timed auto-dismiss; don't pre-check the box.

**Step 1 — hazard dialog**
> Title: **Open the water valve while a leak is active?**
> Body: A leak is still being detected by **{leak_location_label}**. Turning the water back on now can let it keep flooding. Automatic shut-off will be paused for 24 hours — the system will keep detecting and reporting leaks, but it will **not** turn the water off again until the 24 hours end or you cancel.
> Buttons: **Keep water off** (primary) · **Continue** (secondary, destructive style)

**Step 2 — acknowledgement**
> Title: **Confirm: you're overriding leak protection**
> Body: You are turning water **on** at **{valve_name}** while **{leak_location_label}** still reports a leak. For the next 24 hours eFloStop will not automatically stop the water for leaks from your sensors. You are responsible for any water damage during this window. You can cancel anytime in the app.
> Control: checkbox **"I understand water may keep flowing during a leak"** → **Hold to override (24 h)** (hold 1.5 s). · Cancel.

**Banner**
> **Leak protection paused — {hh:mm:ss} left.** Water is ON while a leak is still detected at {leak_location_label}. Auto-shutoff resumes automatically. **Cancel override**

If `{leak_location_label}` unknown, substitute "an active sensor" and still proceed — but log location unavailable.

## 2. Worst case + missing guardrail
Kitchen sensor wet (active flood), valve in dry basement crawl space (own probe dry). User taps Open with Override; firmware accepts (valve probe dry), opens, 24 h. Water flows to the burst pipe up to 24 h unattended — exactly what the interlock exists to prevent, with two taps. Not inherently reckless (mirrors the button) but the remote path removes the human-at-the-property guarantee. Missing guardrails: (1) leak-location acknowledgement mandatory; (2) **12 h mid-window P1 re-prompt** if still wet; (3) consider gating remote override behind "user has viewed the leak detail" (backend/app policy). Keep: leaks still detected/latched/reported during window; immediate auto-close at expiry if still wet (both confirmed in firmware).

## 3. Liability wording & logging
"user+action+timestamp" is **not sufficient**. Per accepted `override_enable`, log immutably: actor identity + auth; command ts AND hub-execution ts; **leak-state snapshot at override time** (wet sensors + locations + incident id/age); **the exact versioned confirmation copy the user accepted**; trigger + device/session; expiry + cancel/expiry actor. In-app, show the override in the valve/leak timeline ("{user} opened the valve during an active leak at {location} on {date}"). Hub emits event + trigger; **backend correlates the C2D `id`→user** and stamps the audit record.

## 4. Notification policy
Push-on-both-triggers is correct; physical-button push is arguably more important. (a) **Multi-resident: notify ALL account members**, not just the actor, for start + expiry; the actor's own client may suppress its own toast, but pushes to other members must NOT be suppressible. (b) Expiry P1 copy branches: still leaking → "Auto-shutoff resumed and your water was just shut off — {location} is still leaking."; dry → "Leak-protection override ended. Auto-shutoff is active again." (c) **Cancel should also notify other members**; add `override_cancel` to the matrix. (d) Per-minute `auto_close_blocked_override` must NOT fan out as pushes. (e) Override-start/12 h/expiry pushes must **bypass device mute** (safety channel).

## 5. Parity & surprising states
1. **"I cancelled but water is still on."** Cancel re-enables auto-close; it only closes if a leak is currently wet. Cancel-while-dry leaves water ON. Toast: "Override cancelled. Auto-shutoff is back on. Water stays on because no leak is currently detected." Cancel-while-wet: "Override cancelled. A leak is still active — water has been shut off again."
2. **"Leak fixed but override still counting down."** Window is purely time-based, not lifted on dry. Banner when wet→dry: "No leak currently detected, but auto-shutoff stays paused for {hh:mm:ss}. Fixed the leak? Tap Cancel override to turn protection back on now." (the cheap fix).
3. **"Overrode remotely but valve stayed closed (crawl space)."** Don't show success until `valve_state_changed{open}`; surface re-close: "Override accepted, but the valve detected water at the valve itself and shut off for safety. Your water is OFF. Check the valve area."
4. **Override survives reboot / no-clock hold** — document in installer notes.
5. **Inferred offline physical override starts a window "out of nowhere"** — copy must make the physical origin obvious: "Someone opened the valve at the device during a leak. Auto-shutoff is paused for 24 h."

## 6. `valve_flood_active` — user-facing message
> Title: **Can't override — water detected at the valve**
> Body: The valve's own sensor detects water right at the valve, so eFloStop won't open it — opening now would release water directly into a flooded area. This safety limit can't be overridden remotely. Once the area around the valve is dry, you can try again. If you believe this is wrong, contact support.
> Button: **OK** · (secondary) **Contact support**

Other rejections: `valve_disconnected` → "Can't reach the valve. The valve isn't responding right now, so the override wasn't applied. Check the valve's power/connection and try again." `no_incident` → "No active leak to override. Use the normal Open Valve control." `not_provisioned` → "No valve set up for this hub." `internal_error` → "Something went wrong applying the override. Your water state is unchanged. Try again."

## 7. Top 5 product/safety changes before sign-off
1. Leak-location shown in the confirm flow and captured in the audit record (allow+log if unknown).
2. Audit record includes the versioned confirmation copy accepted + the leak-state snapshot, correlated server-side `id`→user.
3. Notify the whole household, non-suppressible safety channel, for start/cancel/expiry; add `override_cancel`.
4. De-surprise the time-based window (dry-but-paused banner CTA + 12 h re-prompt).
5. Event-driven success only + explicit re-close/rejection messaging; never falsely tell a user with an unreachable valve that water is on.

Conditional sign-off: build with items 1–5 as launch blockers, plus OPEN-VERIFY-1 (engineering prerequisite). The hard firmware floors (`valve_flood_active`, critical-battery auto-close, leak still detected/reported during window, immediate auto-close at expiry) are correctly placed and must remain non-bypassable.
