# DECISIONS — Remote App-Initiated Water-Access Override (Gate 2 record)

Captures the Gate 2 Q&A with final answers and the resulting design decision record. Authority order:
firmware behaviour is ground truth; this document + the approved `PLAN.md` govern the build; the SRS is
updated to match (Phase 3 / §4.4.3, §4.10).

Date: 2026-06-12. Status: **Gate 2 closed; feeding Gate 3 plan.**

---

## Final command identity

- **Command name: `override_enable`** (verb form, symmetric with `override_cancel`). *(Q1 — renamed from
  the proposed `water_access_override`.)*
- Envelope: canonical `eflostop.cmd`. Payload: **none** for MVP (fixed 24 h). Correlation `id` required by
  the app so a `cmd_ack` is always returned.
- Header constant: `#define C2D_CMD_OVERRIDE_ENABLE "override_enable"`.
- **No legacy-text alias** (envelope-only, like `valve_set_state`/`set_hub_name`) — legacy text yields no
  `cmd_ack`, which is unacceptable for a safety action.

---

## SCOPE — LEAN (user directive, 2026-06-12): *add the override feature through the app, nothing more*

This **supersedes** the broader Gate-2/Gate-3 expansions where they exceed the feature. The only goal is to
let the app trigger the existing 24 h override remotely. Minimum to ship:

**IN (this feature):**
- Firmware: `override_enable` command + handler (preconditions, valve-flood pre-check, window→RMLEAK→open);
  add `trigger` (+`expires_ts`,`remaining_s`) to `water_access_override_enabled` only; Kconfig duration; fw `1.4.0`.
  *(Phase 2 was already minimal — unchanged.)*
- SRS: §5.3 one command row + example; compact §4.4.3 (**APP-FR-043..045** = Open-with-Override action when
  `valve.rmleak=true`, double-confirm + liability copy, reuse the existing banner/countdown/cancel); §5.2.5 a
  **single additive note** that `water_access_override_enabled` now carries `trigger`/`expires_ts`/`remaining_s`;
  **one** notification row **APP-FR-116** (push on remote override start); §2.7 one-line additive note;
  **TC-012** + **TC-N07**; §11 one traceability row.

**DEFERRED (product backlog / opt-in — NOT built, NOT in this SRS edit):**
- **§5.2.5 D1–D6 drift reconciliation** for unrelated events (`auto_close`, `rmleak_cleared`, etc.) — left as-is.
- **Expanded notifications** (household-wide, non-suppressible safety channel, cancel push, 12 h mid-window
  re-prompt, wet/dry expiry copy) — keep only the single remote-start push; the rest defer.
- **Enhanced audit record** (leak-state snapshot + versioned confirmation copy) — rely on the existing
  **APP-NF-003** (user + action + timestamp) only.
- **Richer per-error dialogs** (Contact-support affordance) — app renders `error.detail` verbatim (APP-FR-054).

> ⚠️ Safety note: the deferred notification/audit items came from the product-safety review as recommendations
> for a deliberately-protection-weakening action. Deferring them is acceptable for MVP **as an explicit choice**;
> any one can be pulled back individually later without touching the firmware contract. The hard *firmware*
> safety floors (valve-flood reject, critical-battery close, leaks still reported, auto-close at expiry) remain
> — they are not part of this scope toggle.

The decision rows below stand, but **read them through this lean lens**: where a row added breadth beyond the
feature (Q8 notifications, P-2/P-3/P-4 audit+notify amendments), the LEAN scope above governs.

---

## Decision table

| # | Topic | Final decision | Key rationale / action |
|---|---|---|---|
| 1 | Command shape | Dedicated **`override_enable`**. **Capability-fallback story CONTINGENT on bench verification of unknown-command ack on fw 1.3.0.** | See **OPEN-VERIFY-1** below — must bench-confirm, not infer. |
| 2 | Semantic parity | Identical end-state; single shared code path with a `trigger` param. **Locked order: (1) start window → (2) clear valve RMLEAK → (3) open valve.** | Partial-failure matrix below. Ordering rationale below. |
| 3 | Preconditions | Require active incident **OR** valve RMLEAK asserted. Reject pre-emptive → `no_incident`. Reject unprovisioned/no-valve → `not_provisioned`. **Disconnected: one bounded BLE reconnect ≤10 s, else `valve_disconnected`. Window starts at EXECUTION only, never at receipt. Idempotent refresh allowed, but every refresh emits a fresh `water_access_override_enabled`.** | Resolves SRS↔firmware divergence (D11). Differs from `valve_open`'s queue-forever on purpose. |
| 4 | Flood floor | **Pre-check `valve.leak_state` at command time → reject with `valve_flood_active` + human detail.** No silent "ok". Telemetry-as-truth covers only the post-acceptance wetting race. | Physical-button parity answer below. |
| 5 | cmd_ack semantics | `ok` = accepted/executed-at-hub; snapshot/`valve_state_changed` are truth. App drives banner from the **event**, valve card stays pending until `valve_state_changed`. | Matches `valve_open` + SRS 20 s/snapshot model. |
| 6 | Duration | Fixed **24 h** in production contract. Internally parameterized via **Kconfig** `CONFIG_RULES_OVERRIDE_WINDOW_S` (default 86400) so debug builds can shorten for bench expiry tests. **No duration field on the wire.** | Bench-testable expiry without a contract knob. |
| 7 | Telemetry | Additive **`"trigger":"button"\|"c2d_command"`** on `water_access_override_enabled`, retrofitted on both physical paths. **Field renamed/normalized: emit `expires_ts` (epoch seconds); drop `duration_h`. Seconds everywhere.** 512 B fit confirmed below. | See wire-rationalization note. |
| 8 | Notification | **Push on `water_access_override_enabled` for BOTH triggers** (button is the one the owner may not know about); copy varies by trigger. Expiry push ("auto-close re-enabled") = **P1**. | New row APP-FR-116 (§4.10). |
| 9 | App UX | **Double-confirm** + liability copy; reuse existing banner/countdown/cancel unchanged. When `valve.rmleak=true`, the disabled Open button is paired with an explicit **"Open with 24 h Override"** action. | UI placement guidance into spec. |
| 10 | Valve FW | **No valve FW change.** Phase 2 acceptance MUST include a **bench parity test**: physical button vs `override_enable`, diff all valve GATT state to prove BLE-replicated equivalence. | Phase 3 (valve FW) **dropped**. |
| 11 | Rollout | Spec **min `gateway.fw` 1.4.0** + unknown-command `cmd_ack` runtime fallback (contingent, see #1). Capabilities twin array **deferred (future note)**. Older-hub guidance must honestly state `leak_reset → valve_open` re-closes while sensor wet, plus "update hub firmware". | — |
| 12 | Numbering | Feature rows = **new §4.4.3**, IDs **APP-FR-043..048**. Push rule = **APP-FR-116** in §4.10. Tests **TC-012..014** / **TC-N07..N09**. Add one-line note that this is a **sanctioned additive exception** to the §2.7 "schema will not change during MVP" assumption. | User handles monday.com export + Bartosz email after merge. |

---

## OPEN-VERIFY-1 (integrity flag, Q1) — must bench-verify before relying on it
The claim "an old hub replies `cmd_ack{status:"error", error:{code:"override_enable", detail:"unknown
command"}}`" is **derived from source** (`app_iothub.c :: handle_c2d_command()` final `else` L603-L607 +
ack tail L609-L612), **not** observed on a running fw 1.3.0 hub. Because the **whole backend
capability-detection fallback rests on it**, it is a Phase 1 hard prerequisite:
- Bench step: on a hub flashed with **released fw 1.3.0**, send `{"schema":"eflostop.cmd","id":"verify-unknown","cmd":"override_enable"}` via C2D and capture the D2C with `az iot hub monitor-events`.
- Pass = a `cmd_ack` with `status:"error"`, `error.detail:"unknown command"`, echoed `id`.
- If it instead silently drops (e.g. parser quirk), the rollout story changes to **min-fw gating only** and the app cannot soft-probe — the spec's capability section will be rewritten accordingly.
- Owner: user's build/flash setup (CLI build of this repo is not possible from the assistant environment).

## Q2 — locked execution order + rationale + partial-failure matrix
**Order: (1) `start_override_window("c2d_command")` → (2) `ble_valve_set_rmleak(false)` → (3) `ble_valve_open()`.**

**Why window-first:** setting the override ACTIVE *before* touching the valve makes
`rules_engine_evaluate_leak()` block auto-close immediately, and **skips `rules_engine_tick()` Check 2**
(guarded by `override == INACTIVE`, rules_engine.c L1000) so the hub does **not** misread the RMLEAK 1→0 it
is about to cause as a *physical* ("button") override, and does not race-close on a concurrent leak between
the RMLEAK clear and the window opening. RMLEAK-before-open is mandatory because the valve rejects an open
while `remote_leak_active` is set (app_main.c L267-L281).

**Partial-failure matrix** (BLE writes are write-without-response / fire-and-forget; hub has no synchronous
GATT-level success):

| Scenario | Hub action | App observes |
|---|---|---|
| Preconditions fail (no incident / unprovisioned / flood / disconnected after 10 s) | Reject **before** window start; no window, no valve write | `cmd_ack status:error` (`no_incident` / `not_provisioned` / `valve_flood_active` / `valve_disconnected`); no override event; UI unchanged |
| All ok | window start → RMLEAK=0 → open | `cmd_ack ok` → `water_access_override_enabled{trigger:c2d_command}` (banner) → `valve_state_changed{valve_state:open,rmleak:false}` (valve card) |
| BLE drops **between** RMLEAK clear and open | window active; open stashed as pending; auto-reconnect; applied on reconnect | `cmd_ack ok` → override event (banner) → valve card **pending** until reconnect → eventual `valve_state_changed` / snapshot truth |
| Valve own flood probe wets **after** acceptance | window active; valve refuses/re-closes (own floor) | override event (banner) + `valve.leak_state=true`, `valve.state=closed` in snapshot/valve event — water stays off, override still paused auto-close |
| NVS persist of window fails (rare) | window active in RAM, not persisted; logged | same as "all ok"; a reboot before persist would lose the window (low risk; pre-existing NVS-error tolerance) |

## Q4 — physical-button parity against a wet valve probe
**Answer (from Gate 1): the physical button ALSO cannot open the valve while the valve's own flood probe is
wet** — `processLongPress()` enters the open branch only when `leak_state == no_leak` (app_main.c L211) and
the wet branch just chirps (L235-L241); `processValveBLEWrite()` likewise requires `leak_state == no_leak`
to open (L267). **Parity is preserved: both refuse.** The remote path *improves* on the button's silent
chirp by returning an explicit `cmd_ack` error `valve_flood_active`, so the app can explain why. This is a
strictly-better, parity-consistent divergence.

## Q7 — wire rationalization + 512 B confirmation
Current firmware emits **undocumented drift** `{expiry_epoch, duration_h}` (FINDINGS D1) — **not in the SRS,
not consumed by any backend yet.** Decision: rationalize NOW, before any consumer exists, to the intended
contract **`{event, trigger, expires_ts}`** (epoch seconds; `duration_h` dropped). After this release these
names are frozen. This is a deliberate change to *undocumented* output, not a break of any *documented*
field — consistent with constraint #2 (additive to the **documented** contract).
**512 B check:** wrapped event ≈ `schema(20)+ts(18)+gateway{id,short_id,fw,uptime_s}(~110)+type(15)+
data{event,expires_ts,trigger}(~95)` ≈ **~260 B** (≈300 B with optional `gateway.name`). Comfortably under
the 512 B offline cap (offline_buffer.h L12-L13). Dropping `duration_h` makes it *smaller* than today. ✔

## Q6 — Kconfig knob
Add `CONFIG_RULES_OVERRIDE_WINDOW_S` (int, default 86400) in the rules_engine component Kconfig; replace the
`#define OVERRIDE_WINDOW_DURATION_S (24*60*60)` literal with the Kconfig value. Production = 86400; debug
bench builds may set e.g. 120 to test expiry. The wire contract still states "24 h"; the knob is build-time only.

## PARK FOR PHASE 2 — reboot-without-network mid-window expiry
**Decided behaviour: the override HOLDS until a valid synced clock proves it expired.** This matches existing
firmware: `override_load_from_nvs()` (L154-L173) and `rules_engine_tick()` expiry (L915) only evaluate
expiry when `now >= EPOCH_VALID_THRESHOLD (1704067200)`; with no SNTP the window is conservatively retained,
and `rules_engine_get_override_remaining_s()` reports full duration when time is unsynced (L1063-L1064). The
plan must make this explicit and the test plan must cover "reboot, no network, mid-window → override still
active; after SNTP sync past expiry → `water_access_override_expired`."

## New discrepancy logged
- **D11** — SRS §5.3 lists "BLE not connected" as a `valve_open` *error scenario*, but the firmware
  **queues + auto-reconnects + acks `ok`** (FINDINGS §8/D10). We do **not** change `valve_open`. `override_enable`
  **deliberately diverges** to a bounded reconnect (≤10 s) then `valve_disconnected` error, because starting a
  safety window against a valve we can't reach is unacceptable. Spec will state this difference explicitly.

## Condition A (REVISED per amendment) — error signalling: align, don't tokenize

The earlier draft proposed token-style `error.detail` values. **Superseded.** Tokenizing risks uncontrolled
scope growth into the global §5.2.7 cmd_ack contract. Analysis + decision below.

### A1 — Scope analysis (three options, quantified deltas)

| | (a) Full alignment with §5.2.7 — `error.code`=cmd name, `error.detail`=human-readable string. No new fields. | (b) Additive `error.token`, scoped to `override_enable` | (c) Global token retrofit (all commands) |
|---|---|---|---|
| SRS sections touched | §5.2.7 **unchanged**; §4.4.3 gains a *reference* detail-string table | §5.2.7 **structurally changed** (new optional field in the shared cmd_ack schema — global surface even if "scoped") | §5.2.7 rewritten + every command's error table re-specified |
| New APP-FR rows | **0** (APP-FR-054 already renders `error.detail`) | ~1–2 (app reads/maps `error.token`) | app re-implements all error handling |
| Watts backend work | match on exact frozen strings only where it must branch (else log) | handle a new optional field + token→copy map | re-map every command |
| Firmware changes | dispatcher sets `error_msg` per result enum — **same pattern as every existing command**; 0 new fields | `telemetry_v2_publish_cmd_ack()` (the **shared** ack builder used by ALL commands) gains a token param+field | shared ack builder + every dispatcher branch |
| Test cases added | negative TCs assert exact frozen strings | + token-presence + old-app-ignores-token back-compat | all commands' negatives re-baselined |
| Verdict | **DEFAULT / SELECTED** | only if a P0 blocker provably needs it | **REJECT** (breaks additive-only; no MVP benefit) |

### A2 — Selection: **(a)**. No P0 launch-blocker forces (b).
Walked the frozen P0 launch-blocker list (App F1–F5, Product 1–7). Every one that touches error handling is
satisfied by the app rendering `error.detail` per **APP-FR-054** ("show `cmd_ack.error.detail` in a toast or
banner; keep UI state unchanged"). The cases needing distinct affordances (e.g. Product §6 "Contact support"
on the flood rejection) are satisfied by the app **matching the exact frozen `error.detail` string** — a
client-side presentation choice, not a contract field. No requirement provably fails under (a); therefore (b)
is **not** selected. (If a P0 had failed, the claim would have to cite the exact APP-FR and failure mode — it
does not.)

### A3 — Frozen, user-presentable `error.detail` strings (contract at freeze)
`error.code` = `"override_enable"` for all (per §5.2.7). Hub authors the copy; **app renders verbatim
(APP-FR-054 untouched)**; app MAY additionally key a richer affordance off the *exact* frozen string (optional
enhancement, not a requirement). **No rewording after freeze without a versioned spec note.**

| Failed precondition | `error.detail` (frozen, verbatim) | Expected app behaviour |
|---|---|---|
| No active incident and valve not RMLEAK-locked (pre-emptive) | `No active leak to override. Use the normal Open Valve control.` | Render via APP-FR-054; UI unchanged |
| Valve's own flood probe wet at command time | `Water detected at the valve. It can't be opened remotely until the valve area is dry.` | Render verbatim; app MAY add a Contact-support affordance keyed on this exact string |
| Valve BLE unreachable after ≤10 s reconnect | `The valve isn't responding. Check its power and connection, then try again.` | Render verbatim; offer retry |
| Hub unprovisioned / no valve | `No valve is set up for this hub.` | Render verbatim |
| Internal error (mutex/NVS) | `Something went wrong applying the override. Your water state is unchanged. Try again.` | Render verbatim; offer retry |

### A4 — Old-hub capability detection is written against the OPEN-VERIFY-1 capture only
1.3.0 is shipped firmware; its unknown-command response (expected `error.detail:"unknown command"`) is fixed
in ROM. The backend matches the **captured bytes** from OPEN-VERIFY-1, regardless of any new-firmware contract
addition. No field added in 1.4.0 changes the old-hub match. (Consistent with Q11 + OPEN-VERIFY-1.)

### A5 — Deferred-localization note (recorded)
Option (a) ships **English `error.detail`** rendered verbatim. If Watts later needs **localized** error copy,
that is served by a future **token mechanism = a versioned contract revision** (option (b) at that point),
**documented now, not built**. The frozen-string table (A3) is the matching key the app would localize against
in the interim.

> Supersedes the earlier token-style table. `override_enable failed`/`internal error` are not wire values; the
> internal-error `error.detail` is exactly the A3 string. The five A3 strings + the shipped `"unknown command"`
> are the complete `error.detail` contract for this command.

---

## Bench-test hardening (2026-06-12) — `leak_reset` interlock guard

**Finding (bench).** With a *remote* sensor still actively leaking, the sequence `leak_reset` → `valve_open`
opened the valve — restoring water during a live leak with **no override window and no protection**, bypassing
the sanctioned `override_enable` path and all its guardrails.

**Root cause.** `rules_engine_reset_leak_incident()` cleared RMLEAK **unconditionally**. Because the leak was a
*remote sensor* (the valve's own flood probe was dry), the valve had no local reason to refuse, so the
follow-up `valve_open` succeeded. This was **inconsistent** with the 30 s auto-clear, which *does* require all
sensors clear before releasing RMLEAK. (Pre-existing gap — predates `override_enable`.)

**Decision.** Guard `leak_reset`: **refuse while any leak source is still wet** (`g_active_leak_count > 0`).
- `rules_engine_reset_leak_incident()` now returns `bool` and clears nothing when refused.
- The dispatcher maps `false` → `cmd_ack status:error`, frozen `error.detail`:
  `A leak is still active. Fix the leak first, or use override to open the valve during a leak.`
- **Guarding `leak_reset` alone fully closes the hole**: when refused, RMLEAK stays asserted, and the valve's
  own interlock then blocks the follow-up `valve_open` (force-closes an open while `remote_leak_active` is set).
  No change to `valve_open` needed.

**Resulting model — the three RMLEAK-clear paths are now consistent:**
| Path | Guard |
|---|---|
| Auto-clear (30 s) | all sensors clear ✓ |
| `override_enable` | starts the guarded 24 h window ✓ (the **only** during-leak water path) |
| `leak_reset` | **now: refused while a sensor is wet** (after-leak recovery only) |

**Behaviour change to note for backend/app.** `leak_reset` gains a new (additive) `cmd_ack` error. The app
should route the user to **Override** when it sees it. Also: `leak_reset` sent during an active override window
*with a wet sensor* now refuses — to exit a window, use `override_cancel` (unchanged), not `leak_reset`.

**Files.** `main/rules_engine/rules_engine.c` (guard + `bool` return), `rules_engine.h` (decl + doc),
`main/iothub/app_iothub.c` (cmd_ack mapping). **Spec (Phase 3):** add the one error row to SRS §4.4.1 / §5.3
`leak_reset` error scenarios; reinforce "Override = during-leak water; Leak Reset = after the leak is fixed".

**Verified:** bench-confirm the reported sequence now ends with `leak_reset` → `cmd_ack error` (above) and the
subsequent `valve_open` leaving the valve **closed** (interlock holds). Add as **TC-N10**.

---

## Gate 3 review-driven amendments (adopted from the two sub-agent reviews)

The App/backend and Product/safety reviewers (verbatim in `PLAN.md` Appendix) raised launch-blockers. Adopted:

### Contract / firmware
- **A-1 (adopt): add `remaining_s` to `water_access_override_enabled`.** Final event payload:
  `{event, trigger, expires_ts (UTC epoch seconds), remaining_s}`. `remaining_s`/snapshot `override_remaining_s`
  are the **single, clock-skew-proof countdown basis**; `expires_ts` is for absolute display/notification
  scheduling only. App MUST drive the countdown off `remaining_s`, not `expires_ts`. (Still ≪512 B.)
- **A-2 (adopt, doc-only): flood re-close is reconcilable without a new event.** "override_active=true +
  valve.state=closed + valve.leak_state=true" + the existing `valve_flood_detected` event already lets the app
  explain a post-acceptance flood re-close. Phase 2 must **verify `valve_flood_detected` is actually published**
  on the valve's own-probe close (`app_iothub.c` BLE_UPD_LEAK path); if not, that is a small additive fix.
- **A-3 (adopt): no auto-fallback on ack timeout.** Capability detection = **primary gate on `gateway.fw ≥
  1.4.0`** (present in every envelope) + explicit `unknown command` error ack as confirmation. A bare 20 s
  timeout is ambiguous (offline vs old-silent-hub) and MUST NOT trigger the destructive `leak_reset→valve_open`
  fallback. The legacy fallback is offered only behind an explicit extra confirmation naming the re-close caveat.
- **A-4 (adopt, doc): banner vs valve are two UI elements.** Override **banner** ("auto-close paused") is driven
  by the `water_access_override_enabled` event (true at window start regardless of valve). **"Water is ON"** is
  driven only by `valve_state_changed{open}` / snapshot — never shown on the ack alone. A timed-out command MUST
  be reconciled against the next snapshot/event before showing failure (avoids false-negative toast).

### Notifications (expands APP-FR-116; add APP-FR-117)
- **P-3 (adopt): notify the whole household, on a non-suppressible safety channel, for START / CANCEL / EXPIRY**
  — not just the actor; not silenceable by device mute. Physical-button start is *especially* important (the
  owner may not be the presser). Add `override_cancel` to the notify matrix.
- **P-4 (adopt): de-surprise the time-based window.** (a) Banner copy when leak goes dry mid-window: "No leak
  currently detected, but auto-shutoff stays paused for {hh:mm:ss} — tap Cancel override to restore now." (b) A
  **12 h mid-window P1 re-prompt** if a remote-initiated window is still wet. (c) Expiry copy branches wet vs dry.
- The per-minute `auto_close_blocked_override` is **cloud/timeline only — never a per-minute push.**

### Audit / liability (backend requirements)
- **P-2 (adopt): "user+action+timestamp" is insufficient.** For every accepted `override_enable` the backend
  must store, immutably: actor identity + auth, command ts AND hub-execution ts, **leak-state snapshot at
  override time** (which sensors wet + locations + incident id/age), **the versioned confirmation copy the user
  accepted**, trigger, and the cancel/expiry record with actor. The override must appear in the in-app history
  timeline ("{user} opened the valve during an active leak at {location} on {date}").
- **P-1 (adopt): leak location is mandatory in the confirm flow** (and logged). If the leaking sensor has no
  assigned location, allow but substitute "an active sensor" and log that location was unavailable.

### Open items deliberately NOT adopted now (recorded)
- App-reviewer's "echo originating `id` + actor onto the `water_access_override_enabled` event": **declined.**
  The hub does not know the human identity; the backend correlates via the `cmd_ack` `id` (which carries it) at
  receipt. Keeping the event minimal. (Revisit only if backend correlation proves unreliable.)
- Product-reviewer's "gate remote override on the user having viewed the leak detail <N s": **backend/app policy,
  noted as a recommendation in the spec, not a firmware requirement.**

