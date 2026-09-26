# Hub FW 2.1.4: handoff notes (end of Friday 2026-09-25)

Written for the user and for the next Claude Code session. It records where the 2.1.4 fix job stands and how to pick it up again.

> **Resume here.** Read §1, §7, §10 and §11 first.
>
> **Update, Saturday 2026-09-26.**
> - **Build checkpoint 2 passed.** The user built `00beb81` at 07:20 (§4a).
> - **Phase E (adversarial review) is done, and its fixes are committed.** Findings, decisions and commits are in §10.
> - **The user asked for a new change: the RMLEAK auto-clear is now 10 s** (`75a4a59`).
> - **Update, Sunday 2026-09-27:** at the user's request (before flashing) a second adversarial round plus the Phase F council ran (§11). Three council members voted BLOCK on two issues. Both are fixed, reviewed and committed.
> - **Next: 🔨 Build checkpoint 3** (§7). The last firmware commit is **`d9fa9c8`**; everything after it is docs only. After Build checkpoint 3 comes the council's final vote, then Phase G. This was E/F round 2 of the allowed 2, so any BLOCK left after the final vote goes to the user with its evidence.
>
> *(Friday: Phase D completed after the 20:42 resume; §3 records how workflow 2 was resumed.)*

---

## 1. Status in one paragraph

We're working on branch `fix/2.1.4`, from `master` @ `ae4d59a` = 2.1.3. The job follows `docs/field_logs/2.1.3/CLAUDE_CODE_PROMPT.md`, phases A–G.

- **Phases A–C** (discovery, questions, plan) are done, and the user approved the plan.
- **Phase D groups 1–3 are done.** They fix BUG-2 (the device table is reconciled, not wiped), BUG-3/5/6 (the empty hub publishes; `valve {}` when no valve is provisioned), and BUG-1 (valve battery ≤10 % is critical; an unknown battery is `null`, not 0). Each group was reviewed, and two review-fix rounds went in.
- **🔨 Build checkpoint 1 passed**: no errors and no new warnings. The user flashed it and bench-tested it (§5).
- **Phase D groups 4–5 were started as a background workflow at ~16:52** (§3):
  - G4a: P0 valve identity;
  - G4b: protection before Wi-Fi;
  - G4c: write reliability;
  - G4d: robustness;
  - G5: release docs.

  At the user's request the workflow was stopped right after G4a committed (`2b16bdf`, 17:15). It was resumed at 20:42 as a new workflow. That workflow reviewed G4a and implemented and reviewed G4b → G5 (all committed by 21:58).
- **Review-fix round (committed):** every confirmed finding from the G4b, G4c+G4d and G5 reviews was fixed in four code groups plus docs. Each group was adversarially reviewed, and fixed again where needed, before it was committed (§2).
- **🔨 Build checkpoint 2 passed** (§4a). **Phase E is done**: 7 reviewers produced 16 verified findings plus 6 nits. Every one is fixed or documented, and the user made 4 decisions (§10).
- **Still to do:** 🔨 Build checkpoint 3, Phase F (council), and Phase G (test plan and summary). Nothing is pushed, and no PR has been opened.

| Phase | State |
|---|---|
| A: discovery | done → `docs/field_logs/2.1.3/ROOT_CAUSE.md` (commit `f350d77`) |
| B: questions | done (decisions in ROOT_CAUSE.md and the plan) |
| C: plan | approved → `C:\Users\antun\.claude\plans\happy-floating-boole.md` |
| D: G1–G3 + reviews + fixes | done; 🔨 Build checkpoint 1 passed |
| D: G4a | committed `2b16bdf`; reviewed OK (nits, fixed in G4b/G4c or carried into the test plan) |
| D: G4b–G4d + G5 | committed and reviewed (`2c012b7`, `e823c09`, `d76241d`, `0e7cd44`) |
| D: review-fix round | committed (`6e496f5`, `7ae131d`, `abd7d9d`, `24782b5`, `00beb81`) |
| 🔨 Build checkpoint 2 | **passed** (build of `00beb81`, §4a) |
| E: adversarial review | **done**; fixes committed `095b5d6` … `b245d94` plus docs (§10) |
| 🔨 Build checkpoint 3 | **passed** (build of `d9fa9c8`, §4b) |
| F: 5-specialist council | not started |
| G: MANUAL_TEST_PLAN.md + summary | not started |

---

## 2. Commits on `fix/2.1.4` (oldest first)

| SHA | What |
|---|---|
| `6b84ae3` | *(pre-existing, local only)* 2.1.3 field logs + pre-analysis. **Contains the site Wi-Fi password** in `docs/field_logs/2.1.3/UART logs.txt` lines 137/147. It must be redacted (history rewrite or a new commit that scrubs the file, the user's decision) **before anything is pushed**. `origin/master` is still `ae4d59a`, so it has not leaked. |
| `f350d77` | docs: ROOT_CAUSE.md (Phase A) |
| `6043693` | G1 / BUG-2: reconcile the device table instead of wiping it. Per-device roll-up excuse, D0 single owner (`apply_device_set_change` on iothub_task), MAC-keyed scanner, cache and rules purges |
| `cb5d2dc` | G2 / BUG-3/5/6: empty hub publishes (event/heartbeat/boot/decommission), `valve {}`, snapshot built attach-first with NULL checks, `rules_engine_reset_all()` |
| `d90bb15` | G3 / BUG-1: valve battery ≤10 % CRITICAL, 11–20 % WARNING; 0xFF = unknown → `null`; alerts report reachability only; rating-sequence snapshots; `valve_open` refused at critical battery |
| `df843cc` | G1 review fixes: valve link resync, seen-count clamp after a removal, bench-log anchor moved, stale comments |
| `9c83b75` | G2 review fixes: **stale rules latch/override cleared on a hub that boots empty** (major); empty-transition reason "event"; link-edge gate; reset under the lock |
| `273f8d5` | G3 review fixes: wake iothub on a rating change; lock-free battery-critical check |
| `84a5d6a` | Review round 2: the resync also restores the valve flood/battery readings; a busy provisioning read is never taken as "no valve"; checked add of the snapshot wake queue to the QueueSet |
| `15128e6` | Found at Build checkpoint 1: the whitelist device set moved off `.bss` (it was +376 B of static RAM) |
| `2b16bdf` | **G4a / P0-a/b/c.** The hub never connects to, commands, or pairs with a valve that isn't provisioned: MAC-only match, a peer check at connect, no passkey or bonding on a foreign link, pending commands flushed on a target change, API calls refused with no target. BLE starts for a sensors-only hub. Membership is three-state (YES/NO/UNKNOWN; UNKNOWN counts as provisioned). C2D valve commands with no valve return an error ack. Reviewed OK. The implementer's report is in §3a |
| `a628356` | docs: this handoff (paused after G4a) |
| `2c012b7` | **G4b / N1–N3.** Leak protection starts before Wi-Fi: provisioning, rules, health and BLE start at boot, and SNTP/DPS run as non-blocking steps in the loop. The QueueSet is built, with checked adds and no boot-time loss, before BLE starts. The BLE apply is retried when provisioning is busy |
| `e823c09` | **G4c.** Valve writes are retried 3× and pended; a failed replay is re-applied; a command generation drops commands meant for a previous valve; RMLEAK is replayed before CLOSE; the flush WARN appears only when something was flushed; `nimble_port_init` is retried |
| `d76241d` | **G4d.** Transactional provisioning (rollback plus NVS rewrite on a failed save), `sensor_meta_get` copy-out, a locked offline buffer that refuses events over 512 B, atomic twin `$rid`, and the Wi-Fi password lines hidden (`wifi_manager`/`http_server` logs set to WARN) |
| `0e7cd44` | **G5.** `PROJECT_VER` 2.1.4, new `CHANGELOG.md`, all 11 schemas rebuilt for 2.1.x, validator, message catalogue v5.0, `C2D_COMMANDS.md` valve error acks, `handle_valve_resync` comment |
| `6e496f5` | Review fix (**major**, found by both the G4b and the G5 review): events raised before the first clock sync were dropped. They are now held in the offline buffer and stamped with their real time (from the uptime) as soon as the clock syncs |
| `7ae131d` | Review fix: an override window started before the clock synced is timed on uptime, then re-based. Before, it ended the instant SNTP landed and never ended without internet |
| `abd7d9d` | Review fix: the BLE target is read and set in one provisioning lock hold (the owed retry could briefly re-target a removed valve); decommission-all clears the target first; the 2 s poll with NTP blocked is bounded; NVS counts are clamped |
| `24782b5` | Review fix: a failed replay is re-applied ahead of newer commands (it could undo them); `s_link_dropping` ordering (it could block every CLOSE for a whole link); forced reconnects capped at 3; TERM_FAILURE handled; the passkey is no longer logged |
| `00beb81` | Docs for the round: CHANGELOG, schemas, validator, catalogues; C2D command catalogue v3.0 |

---

## 3. Workflow 2 (G4a → G4d, G5): state and how to resume

**DONE.** This was resumed at 20:42 (run `wf_d89150cf-931`). The review-fix round then ran as `wf_b48c0e5e-f12`, and both are committed (§2). The steps below are kept as a record.

**State at 17:15.** The workflow was stopped by `TaskStop` right after the G4a commit `2b16bdf`. G4b, G4c, G4d and G5 had not started, and the tree was clean.

**On "resume", do this:**
1. Copy the workflow-2 script (path below). Keep the `RULES`, `INVARIANTS`, `G4B`, `G4C`, `G4D` and `G5` texts unchanged, and drop G4a from `GROUPS`.
2. Launch **one** workflow that does two things:
   - **(a)** runs the G4a reviewer on `2b16bdf`. Use the script's `reviewPrompt(['G4a'], ['2b16bdf…'], [G4A])`, pipelined in parallel with the rest.
   - **(b)** implements G4b → G4c → G4d → G5, with the same reviews as before (G4b; G4c+G4d; G5).

   Before launching, fold in these G4a notes (§3a):
   - G5 must also update `C2D_COMMANDS.md`: `valve_open` / `valve_close` / `valve_set_state` now ack `error` "No valve is set up for this hub." or "The valve command could not be queued. Try again." when there is no valve.
   - G5 must also fix the stale comment in `health_engine.c` `handle_valve_resync()`, which says the valve module "can relink a valve by name".
3. Triage every review. Confirm each finding against the code, and commit the fixes per group (`fix(review): …`).
4. 🔨 Build checkpoint 2 (§7).

### 3a. G4a implementer report (for its review and the test plan)

**Beyond-spec gates it added:**
- A non-target link never gets security, discovery, a passkey answer, repeat pairing, ENC_CHANGE handling or `on_notify` health posts.
- Pending commands are applied only on a target link.
- A normal DISCONNECT posts `BLE_UPD_DISCONNECTED` only if that link was the target.
- A cancelled connect does not rescan unless a connect was requested.
- If `ble_gap_terminate` fails on a rejected link, the sec timer acts as a 60 s backstop.
- `vlk_mac_ok` falls back to `s_det_valve_mac` when the provisioning read is busy, so a valve flood is not skipped.
- `enqueue_cmd` checks for a NULL queue.
- Log wording changed:
  - `[SCAN] Target MAC matched` is merged into the connect line;
  - new warnings `VALVE_CLOSE refused — %s` and `VALVE_SET_STATE closed refused — %s`;
  - with no link and no valve, auto-close now logs "AUTO-CLOSE: no provisioned valve - nothing to close".

**Things to check in review, or on the bench (these go into MANUAL_TEST_PLAN):**
- **Bonded valve.** It must reconnect normally. `[CONNECT] MAC=` (the identity address) must equal the provisioned MAC, and "is not the provisioned valve" must never appear. If the STM32WB valve's identity address ever differs from the MAC it advertises, every reconnect would be rejected.
- **Neighbour valve** (the second valve `00:80:E1:27:7E:C5` on the bench). Test with no valve provisioned, and with a different valve provisioned. The hub must never connect, never send CLOSE, never answer the passkey. Expect `[SCAN] No provisioned valve - not scanning for valves` or `[SCAN] Starting scan for provisioned valve <MAC>`.
- **Sensors-only hub (P0-b).**
  - Expect `Starting BLE (valve=none, BLE sensors=N)`, the scanner hearing the sensors, and `[CMD] CONNECT refused - no provisioned valve` once at boot.
  - A leak logs "AUTO-CLOSE: no provisioned valve - nothing to close".
  - **Product question:** that leak still publishes an `auto_close` event with `rmleak_asserted:false` and no `valve_id`. This predates 2.1.4; should it be suppressed?
- **Decommission the valve while linked, and during a scan or connect.**
  - The link drops silently: "[DISCONNECT] Link was not the provisioned valve - hub not notified", and no `valve_unlinked`.
  - A cancelled connect logs `[CONNECT] Failed status=9` and does not rescan.
- **Change the valve target A→B while A is linked.**
  - Expect `[CMD] Flushed ...` and "[API] Linked to a valve that is no longer the target - disconnecting".
  - Then B links.
  - Nothing queued before the change reaches B.
- **No valve (N9).** `valve_open` / `valve_close` must ack `error` "No valve is set up for this hub.".
- **Stack.** *(Superseded.)* The provision path no longer puts `prov_device_set_t` on the esp-mqtt stack: `provisioning_with_valve_target()` uses an 18 B copy (abd7d9d). Every `prov_device_set_t` user now runs on iothub_task or in the scanner's noinline helper.
- **Noise.** "[CMD] Flushed queued/pending valve commands (valve target changed)" prints at every boot with a valve, because no target → valve counts as a change. Consider logging only when something was actually flushed.
- **Known gaps left for G4b.**
  - Boot still depends on `provisioning_is_provisioned()`: a busy read means "UNPROVISIONED" and no BLE.
  - BLE still starts only after Wi-Fi.
  - `iothub_apply_provisioned_mac()` does nothing when provisioning is busy.
- **Known gap.** `rules_engine_evaluate_leak()` itself returns early if `provisioning_is_provisioned()` or `get_rules_config()` times out. A busy mutex can therefore skip one packet's auto-close; the next packet re-evaluates.
- **Minor.**
  - While a rejected or old link is being torn down, `ble_valve_is_connected()` is still true, so an auto-close in that window can report `rmleak_asserted:true` while the writes are held.
  - The getters return the old valve's cached values until the N6 disconnect lands.
  - A neighbour bond made under 2.1.3's name match may still be in the NimBLE bond store. It is unused and was not deleted.

**Workflow bookkeeping:**
- Task `w8gs3xqqt`, run `wf_ebb92f56-0ee`. It was started in the Claude Code session that ended on 2026-09-25, and stopped at 17:15.
- **Script (it holds every G4/G5 spec verbatim):**
  `C:\Users\antun\.claude\projects\C--Work-Projects-EfloStop-2-Firmware-Production-eFloStop-WiFiHub-idf1\ee520eed-0dc0-45ef-bdfe-91bf0b44762d\workflows\scripts\hub-2-1-4-phase-d-g4-g5-wf_ebb92f56-0ee.js`
- Implementers run one after another, G4a → G4b → G4c → G4d → G5, one commit each. Reviewers run behind them for G4a, G4b, G4c+G4d and G5. Reviewers only read; they never commit.

**If the session was closed before the workflow finished** (a workflow cannot be resumed from another session):

1. `git log --oneline -8` shows which G4/G5 commits landed.
2. `git status` / `git diff --stat -- main` shows whether a group was cut off part-way. Uncommitted edits under `main/` belong to the group that was running and are incomplete. **Look at them before discarding.** Do not bulk-reset the tree: `.vscode/settings.json`, the two `docs/field_logs/2.1.3/*.md` files and `.adsum/` are the user's own uncommitted changes and must stay untouched. Discard only the listed `main/` files, and only with the user's OK.
3. Copy the script. Delete the finished groups from its `GROUPS` array, and set the `reviewAlso` on G4d if G4c already landed. Run it as a new workflow. The specs are unchanged.
4. Then run a review pass over any group whose reviewer never ran, **including already-committed groups**.

**If the workflow finished**, its result lists every commit plus the reviewer findings. Triage those findings as we did for G1–G3:
- confirm each finding against the code;
- fix it, with each group's fixes in their own `fix(review): …` commit;
- carry out-of-scope items to the user.

---

## 4. Build checkpoint 1 result (build of `84a5d6a`)

The build was verified by timestamps: the objects and the `.bin` date from 16:44–16:45, and the commit from 16:37. Every file the branch changes was recompiled.

- **Errors:** none.
- **Warnings:** only these four, and none is in a file this branch changes (all exist on `master`):
  - `app_ble_valve.c:101`: `BLE_HS_ATT_ERR` redefined;
  - `app_lora.cpp:185`: two missing `uart_config_t` initialisers;
  - `app_lora.cpp:160`: unused `switch_sync_word`.

**Sizes.** The 2.1.3 baseline is the first build in the user's paste: its `.bin` is 0x16EC20 = 1,502,240 B, identical to the 2.1.3 image.

| | 2.1.3 | `84a5d6a` | Change |
|---|---|---|---|
| App `.bin` | 1,502,240 | 1,508,848 | +6,608 B (+0.4 %); 28 % of the app partition free |
| Flash `.text` / `.rodata` | 999,990 / 350,524 | 1,004,530 / 352,588 | +4,540 / +2,064 |
| DIRAM `.bss` | 36,120 | 36,536 | +416 (376 of it fixed in `15128e6`; ~+40 expected at Build checkpoint 2) |
| `.data`, DIRAM `.text` | 21,556 / 113,387 | same | 0 |
| IRAM | 16,384 / 16,384 (**100 %**) | same | 0. **No `IRAM_ATTR` code may be added** |

The separate worktree baseline build is **no longer needed**.

## 4a. Build checkpoint 2 result (build of `00beb81`, Saturday 07:20)

The user ran an incremental build through the VS Code extension. The objects of every changed source file are dated 07:20, after the last commit (23:45).

- **Warnings:** only the same four as on `master`. The `BLE_HS_ATT_ERR` warning moved from line 101 to line 105.
- **Version:** `2.1.4`.

| | 2.1.3 | CP2 | Change |
|---|---|---|---|
| App `.bin` | 1,502,240 | 1,524,016 (0x174130) | +21,776 B; 27 % of the partition free |
| Flash `.text` / `.rodata` | 999,990 / 350,524 | 1,013,258 / 359,004 | +13,268 / +8,480 |
| DIRAM `.bss` / `.data` | 36,120 / 21,556 | 36,280 / 21,572 | +160 / +16 |
| DIRAM `.text`, IRAM | 113,387 / 100 % | same | 0 |

---

## 4b. Build checkpoint 3 result (build of firmware `d9fa9c8`, Sunday 2026-09-27 09:10)

The user ran an incremental build through the VS Code extension. The build succeeded, so there are no errors and no `-Wall` warnings (`-Werror=all`). Every source file changed since CP2 was recompiled at 09:10, after `d9fa9c8` (00:35). The `offline_buffer.c` and `provisioning_manager.c` objects are from 26 Sep 22:29, and neither source changed after that. The paste began at the image step, so the non-`-Wall` warning lines were not shown. The four known `master` warnings are expected; the user was asked to confirm.

| | 2.1.3 | CP2 | CP3 | CP3 vs 2.1.3 |
|---|---|---|---|---|
| App `.bin` | 1,502,240 | 1,524,016 | 1,528,576 (0x175300) | +26,336 B (+1.75 %); 27 % of the partition free |
| Flash `.text` / `.rodata` | 999,990 / 350,524 | 1,013,258 / 359,004 | 1,016,642 / 360,188 | +16,652 / +9,664 |
| DIRAM `.bss` / `.data` | 36,120 / 21,556 | 36,280 / 21,572 | 36,280 / 21,572 | +160 / +16 (alignment padding absorbed the council fixes' ~5 B) |
| DIRAM `.text`, IRAM | 113,387 / 100 % | same | same | 0 |

The heap budget against 2.1.3 is about +176 B of static RAM, plus about 50 B of permanent heap for two log-level tag nodes (E-20).

## 5. Bench results: CP1 image `84a5d6a` (UART + IoT Hub monitor logs, 16:45–17:06)

The logs are on the user's Desktop: `UART logs.txt` (the last boot, ELF `d0798149d…`, is this image) and `IoT hub monitor.txt`. The hub is GW-7C4FADAE69C8 on COM30, and it was empty at boot.

| Check | Result | Evidence |
|---|---|---|
| Empty hub boot (BUG-3/6) | ✅ | `boot` snapshot: excellent / "No devices provisioned", `valve {}`, empty arrays, rules, `override_active:false` (16:45:42) |
| Boot rules reset on an empty hub (G2 review fix) | ✅ | "Boot: hub is empty - clearing any persisted rules-engine state" at 2985 ms |
| Provision → reconcile, commission, pulse | ✅ | "Device table loaded: 2 device(s) (+2 added…)", "Commission: fast snapshot armed", "PROV pulse armed"; the re-provision at 224 s shows "(+4 added, -1 removed)"; the pulse sent 13 snapshots; boot at 375 s (150 s window); commission-refresh at 523 s |
| Valve battery unknown = `null` (BUG-1) | ✅ | snapshot at 200 s: valve `battery:null`, `connected:false`; at link-up `battery:64` |
| Removing one sensor keeps the others (BUG-2) | ✅ | 815 s: survivors keep battery 83/80; "Syncing - waiting for 1 device" (not reset); **no** boot/commission and **no** "Boot sync: timeout" afterwards |
| Per-device grace (BUG-2, 1193 s case) | ✅ | "Roll-up grace expired (600 s)" at 824.6 s = 224.9 s + 600 s. The removal at 815 s did **not** re-excuse it |
| Rating-change snapshot (N10) | ✅ | `event:health` snapshot 0.3 s after the grace expiry: critical "1 sensor offline"; LED RED, then GREEN after that sensor was removed |
| Valve removal → `valve {}` (BUG-5) | ✅ | 876 s snapshot `valve:{}`; twin `valve_id:null`; no extra `valve_unlinked` snapshot |
| Heap | ℹ️ | valve + 4 BLE: free ≈33 KB, **min_ever 19,524 B** (the field unit showed 2,972 B under different load). Empty hub before BLE: min_ever 91,048 B |
| **Valve commands with no provisioned valve (P0-a / P0-c)** | ❌ **(expected: fixed by G4a)** | After the valve was decommissioned, `valve_set_state open` at 974 s returned `cmd_ack ok`, scanned **by name**, paired with a **different valve `00:80:E1:27:7E:C5`** using the fixed passkey, and applied the pending OPEN. Later C2D close/open commands drove that valve. Snapshots correctly show `valve {}`, and valve events were suppressed ("Valve identity unavailable (live=1 provisioned=0)") |

**Not yet exercised on the bench** (these go into the Phase G test plan):
- valve battery ≤10 %: S14/S16/S17;
- a hub that becomes empty through removals: S8, with the transition snapshot and rules reset;
- decommission-all: S11;
- MQTT reconnect while empty: S9;
- a wet sensor removed and re-added: S5/S6;
- everything in G4/G5.

---

## 6. Decisions and carried items (details in the plan and ROOT_CAUSE.md)

**User decisions (Phase B and later):**
- **Valve battery bands.** ≤10 % CRITICAL "Valve battery critical"; 11–20 % WARNING; >20 % EXCELLENT. **Valve only**: sensor battery bands (20/35) never reach CRITICAL (memory: `project_battery_critical_valve_only.md`).
- **Battery-critical alert.** No `device_offline` alert for a battery-critical valve; the snapshot and the LED carry it.
- **Unknown battery.** `null` everywhere, and valve `state:"unknown"` until the valve is ready.
- **Snapshot reason field.** The transition to empty is `event`; periodic is `heartbeat`; boot/reconnect is `boot`; decommission-all is `decommission`.
- **Hub becomes empty.** Reset the rules engine's state and put the rules config back to `true`/`7`.
- **`valve_open` at critical battery.** Refused with an error `cmd_ack`.
- **Scope.** All safety fixes go into 2.1.4. The latent bundles in scope are write reliability, visibility, and robustness and hygiene. Parser and rules edge cases are deferred.

**Carried into the G4 spec (already in the workflow-2 script):**
- Provisioning membership becomes three-state (YES/NO/UNKNOWN), so a busy provisioning mutex can't drop a real leak.
- The QueueSet adds are checked for every member.
- The health engine's valve MAC check is covered by G4a's target-only link.

**Accepted in the review-fix round (Claude's call; tell the user):**
- **RMLEAK before CLOSE has one exception.** RMLEAK is always attempted first. If that write itself fails on a live link and no reconnect follows (the reconnect cap has been reached, or the terminate was refused), the CLOSE is still written, because holding back a close during a leak is worse. An OPEN never overtakes a pended RMLEAK.
- **All three log lines that printed the fixed BLE passkey were changed**, not just the INPUT line: `[SM] Fixed Passkey: configured (not logged)` also printed the passkey at every boot. None of these lines is in the production-tool contract.
- **C2D decommission of `valve` on a hub with no valve now acks `error`** ("valve decommission failed"), per S12 in the plan. A cloud retry after a lost ack sees an error instead of an idempotent `ok`.

**Open with the user:** nothing. The `auto_close` question was decided on 2026-09-26 (suppress it; see §10).

**Follow-ups noted (not fixed):**
- The superseded banner in `docs/telemetry/telemetry_catalogue.md` was added by hand to a generated file. `build_catalogue.py` must emit it before the next regeneration.
- A live DPS registration still blocks `iothub_task` for up to 60 s per attempt (first boot, after decommission-all, or after an epoch change). Moving DPS to its own task is future work.
- Between a valve target change and the old link's DISCONNECT, the valve getters return the old valve's cached state. The window is about 0.5 s on the bench and up to the 5 s supervision timeout (E-11 corrected the earlier "a few ms"). The auto_close sample is target-gated, so it no longer claims an interlock on a removed valve.
- The NVS count clamp does not also clamp to the blob length, so a corrupt count above the real entries gives phantom zero ids. This needs a firmware bug to write such a count.

**Deferred to 2.1.5:**
- the legacy keyword scan of C2D payloads;
- `MAX_ACTIVE_LEAK_SOURCES` is 16, below the 33-device table;
- single-slot rules telemetry;
- de-duplication of repeated C2D command ids;
- sensors go unheard during a valve connect;
- queue pressure;
- LoRa driver hardening;
- a ~17 KB RAM reclaim;
- removing the valve bond on decommission;
- `override_enable` on a battery-critical valve (the valve refuses to open anyway).

---

## 7. Remaining steps

1. ~~Finish or resume workflow 2 (§3). Triage its reviews and commit the fixes.~~ Done: `2c012b7` … `00beb81`.
1a. **🔨 Build checkpoint 3 (user), NEXT.** It is required because Phase E changed code (§10). Build the branch at its latest commit. The last firmware commit is `d9fa9c8`; everything after it is docs. Use the same commands and paste-back as Build checkpoint 2 below, with `build_cp3.log`.
   - **Compare against §4a (CP2):**
     - no new warnings (only the four from `master`);
     - IRAM unchanged;
     - `.bss` and `.data` about the same as CP2 (36,280 / 21,572). The Phase E lanes added no statics, so a change of more than about ±40 B needs a look;
     - flash grows a few KB.
   - **Bench after flashing CP3.** Run the CP2 list below, plus:
     - **Re-wet at the clear:** dry a sensor, then wet it again about 10–12 s later. Expect `rmleak_auto_cleared`, then `leak_detected` and `auto_close`, with the valve locked and closed. The exception is a pass that is also an MQTT (re)connect, where `rmleak_auto_cleared` may be missing; `leak_detected` and `auto_close` still appear, and the valve stays locked. There must be no "RMLEAK cleared externally (valve override)".
     - **F-01, the hub's own clear across a relink:**
       - (a) Latch a sensor leak. Take the valve out of RF range; do not power it off. Dry the sensor and wait for `rmleak_auto_cleared`, then restore the link. Expect "Applying pending RMLEAK command=0", then either "Reconnected: valve RMLEAK active, hub incident clear - RMLEAK clear owed by the hub, not re-latching" or "Reconnected: no active incident, valve clear" (this depends on timing). There must be no "re-latching incident", no "RMLEAK cleared externally" and no `water_access_override_enabled`. The next snapshot has no "Leak interlock latched", and shows `valve.rmleak:false` and `override_active:false`. (Do not require "RMLEAK clear read back": it prints only when an incident latches again before the clear is read back.)
       - (b) The same, with `leak_reset` sent while the valve is unlinked.
       - (c) The incident is latched and all sensors are dry; power-cycle the hub. Expect no `water_access_override_enabled`.
     - **Genuine button override still works:** latch a leak with the valve linked (RMLEAK set). Long-press the valve button. Expect "RMLEAK cleared externally (valve override) — starting 24h override window" and `water_access_override_enabled{trigger:"button"}`.
     - **B1, a busy GATT pool:**
       - (a) Power-cycle the hub with a sensor wet and the valve open.
       - (b) Take the valve out of range, wet a sensor, then bring the valve back.
       - (c) The re-wet-at-clear case above.
       - **Pass:** the valve is CLOSED within about 3 s of "SETUP COMPLETE". There must be no "write attempt N/3 failed (rc=6)", no "valve write failed 3 times (rc=6)", no "reconnecting to re-apply", and no second GAP DISCONNECT.
       - **Acceptable lines:** "[CMD] … write: GATT busy - waiting", "… held behind the pending RMLEAK command", "… kept pending - GATT still busy, replaying it" and "Pending valve command=0 kept behind the RMLEAK command (GATT busy)". At setup completion with both slots pended, the host's "valve read-back rc=6 - position unconfirmed" is expected (pre-existing); the position then comes from the valve's own state notify.
     - **RMLEAK before CLOSE across a relink:** take the valve out of range and wet a BLE sensor ("RMLEAK write not ready. Queuing val=1", then "Valve write not ready. Queuing val=0"). Bring the valve back. "Applying pending RMLEAK command=1" (or its Replaying line) must come before the valve command.
     - **CP3 sizes:** `.bss` 36,280–36,288 is a pass (the council fixes' ~5 B can be absorbed by alignment fill); `.data` 21,572. More than about +40 B needs a look.
     - **Auto-clear:** wet a sensor (auto-close), then dry it. You should see "All sensors clear — auto-clear timer started (10s)", then about 10–12 s later "AUTO-CLEAR: all sensors clear for 10s" and `rmleak_auto_cleared {clear_after_seconds:10}`. The LED goes RED → amber for about 10 s → GREEN, and the valve stays closed.
     - **`valve_open` during a leak with the valve powered off:** the ack is `error` with "Valve is locked after a leak (RMLEAK)…".
     - **Sensors-only hub leak:** you see `leak_detected` and **no** `auto_close`, plus the UART line "AUTO-CLOSE: no provisioned valve - auto_close event not published".
     - **Connected valve:** `last_seen_age_s` stays 0 in snapshots.
     - **Valve swap** (flooded valve A → dry valve B): B is not auto-closed.
     - **Remove and re-add a wet sensor within 10 s:** `leak_detected` and `auto_close` arrive at once.
2. ~~**🔨 Build checkpoint 2 (user).**~~ Passed (§4a). Full clean build of the branch at its final commit. That is the last commit **before** this handoff update; the handoff commit is docs only.

   ```powershell
   git log --oneline -1
   idf.py fullclean
   idf.py build *> "$env:TEMP\build_cp2.log" ; "exit=$LASTEXITCODE"
   Select-String -Path "$env:TEMP\build_cp2.log" -Pattern "warning:|error:" | ForEach-Object Line
   idf.py size
   ```

   The user pastes back all four outputs. The VS Code extension's incremental build + flash is also fine, **as long as it runs after the last commit**. Check the `.bin` timestamp against `git log -1 --format=%ci`.
   Compare against §4:
   - no new warnings;
   - IRAM unchanged;
   - the version reads 2.1.4.

   Expected `.bss`: about 36,300 B, against 36,120 B for 2.1.3 and 36,536 B at CP1 (376 B of which was removed in `15128e6`).
   - G1–G3: about +40 B.
   - G4 and the review round: about +130 B in all. The largest item is the offline-buffer `StaticSemaphore_t` and its handle (about 88 B). The rest is small flags and counters (G4b about 20, G4c 5, R1 2, R2 8, R3 −4, R4 5).
   - Anything above about 36,400 B needs a look.

   Heap: `esp_log_level_set` adds about 60 B once at boot.

   **Bench after flashing CP2.** Look for these first; the full list goes into the Phase G test plan.
   - **Boot order:** "Starting BLE (valve=…, BLE sensors=N)" and "QueueSet Initialized" appear **before** "Connected! IP". There is no "QueueSet: … could not be added". At boot with a valve, you should see "[CMD] No valve commands to flush (valve target set)" as INFO, not a WARN.
   - **Router off, then power-cycle, then wet a sensor:** the valve closes with no Wi-Fi. When the router comes back you should see "Time synced", then "Stamped pre-sync event [ob_NN] at clock sync: ts=…". After the MQTT connect, `leak_detected` and `auto_close` arrive with `ts` close to the real leak time. Check this in the **IoT Hub monitor** capture: replayed events never print `Pub event:` on UART.
   - **NTP blocked:** "SNTP initial sync failed" at about 120 s, then the loop idles at 30 s, not 2 s.
   - **Valve churn** (decommission the valve, provision a different one, repeat quickly): every "[SCAN] Target MAC matched - connecting to provisioned valve: X" names the most recent "[API] Target MAC set to: X".
   - **Captive portal:** press the Wi-Fi reset button on a hub with a valve and 4 sensors, then set up Wi-Fi from a phone. The portal must load with no reboot. Record the heap `min_ever`; CP1 was 19,524 B.
   - **Serial log:** no "password" line anywhere, and the `[SM] Fixed Passkey` line prints "configured (not logged)".
   - **Validator:** `python docs/telemetry/validate_capture.py "<IoT Hub monitor capture>"` shows 0 FAIL.
3. ~~**Phase E (adversarial review).**~~ Done (§10). Areas:
   - decommission races;
   - the empty hub;
   - valve lifecycle and identity;
   - the battery sequence unknown → 65 → 10 → 11 → 10 → disconnect → reconnect → 9;
   - concurrency;
   - heap and NULL handling;
   - boot before Wi-Fi.

   Each confirmed finding gets its own `fix(review): …` commit. Anything out of scope goes to the user first.
4. **Phase F (5-specialist council).** Specialists vote SHIP or BLOCK. At most 2 rounds of E/F. 🔨 Build checkpoint 3 after any code change.
5. **Phase G (delivery):**
   - grep for `2\.1\.3` and `1\.8\.0` (current-version claims only);
   - a build summary against §4;
   - write `docs/field_logs/2.1.4/MANUAL_TEST_PLAN.md`: a traceability matrix, scenarios S1–S26 from the plan plus the council's risks, a ~30 min smoke subset, and a like-for-like heap check;
   - a final summary.

   No push and no PR unless the user asks. Remind the user about the `6b84ae3` password before any push.
6. This HANDOFF.md is committed on the branch; nothing else was running when it was committed. Update it at each stop, and fold it into the Phase G docs at the end.

---

## 8. Rules that stay in force (from CLAUDE_CODE_PROMPT.md)

- **Builds.** Claude never runs `idf.py` or any compiler; the user builds at each 🔨 checkpoint.
- **Off-limits.** Do not touch `managed_components/`, `sdkconfig*`, the partition table, DPS/SAS/crypto, or NVS namespaces, keys or layout. Field units OTA from 2.1.3 and must keep their provisioning; rolling back to 2.1.3 must keep it too.
- **RAM and code rules.**
  - No heap regressions and no large stack buffers.
  - Every cJSON/malloc result is NULL-checked.
  - IRAM is full.
  - `CONFIG_LOG_MAXIMUM_LEVEL=3`, so `ESP_LOGD` is compiled out.
- **Staging.** Stage by explicit path only. **Never** stage `.vscode/settings.json`, `docs/field_logs/2.1.3/CLAUDE_CODE_PROMPT.md`, `docs/field_logs/2.1.3/README.md` or `.adsum/`.
- **Commits.** Every commit ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **Log contract.** Keep these lines byte-for-byte:
  - production tool: "Gateway ID", "Firmware version"/"App version", "WiFi STA MAC", "APP_LORA: Initializing LoRa Driver...", "[HOST] NimBLE host task started";
  - bench anchors: "Device table loaded:", "Boot sync: timeout … still excused for a further", "Roll-up grace expired", "PROV pulse armed", "Commission: fast snapshot armed".
- **Process.** Workflows only in Phases D–F. Post a one-line status at each phase boundary.

## 9. Where things are

| What | Path |
|---|---|
| Job instructions | `docs/field_logs/2.1.3/CLAUDE_CODE_PROMPT.md` (between BEGIN/END PROMPT) |
| Pre-analysis | `docs/field_logs/2.1.3/ANALYSIS.md` |
| Root cause + Phase B decisions + latent register N1–N28 | `docs/field_logs/2.1.3/ROOT_CAUSE.md` |
| Approved plan (design G1–G5, JSON before/after, snapshot ownership, scenarios S1–S26, risks) | `C:\Users\antun\.claude\plans\happy-floating-boole.md` |
| Workflow 1 script (G1–G3) | `…\ee520eed-…\workflows\scripts\hub-2-1-4-phase-d-g1-g3-wf_8bac971b-5a7.js` |
| Review-fix workflow script | `…\workflows\scripts\hub-2-1-4-g1-g3-review-fixes-wf_b49c8777-09b.js` |
| Workflow 2 script (G4a–G5 specs) | `…\workflows\scripts\hub-2-1-4-phase-d-g4-g5-wf_ebb92f56-0ee.js` |
| Workflow 2 resume script (G4a review, G4b–G5) | `…\workflows\scripts\hub-2-1-4-phase-d-g4-g5-resume-wf_d89150cf-931.js` |
| Review-fix round script (R1–R4 + docs specs) | `…\workflows\scripts\hub-2-1-4-review-fix-round-wf_b48c0e5e-f12.js` |
| Per-agent results (reviews, reports, bench checks) | `…\subagents\workflows\wf_d89150cf-931\journal.jsonl`, `…\subagents\workflows\wf_b48c0e5e-f12\journal.jsonl` |
| Session transcript | `C:\Users\antun\.claude\projects\c--Work-Projects-EfloStop-2-Firmware-Production-eFloStop-WiFiHub-idf1\ee520eed-0dc0-45ef-bdfe-91bf0b44762d.jsonl` |
| Bench logs of Build checkpoint 1 | `C:\Users\antun\Desktop\UART logs.txt`, `C:\Users\antun\Desktop\IoT hub monitor.txt` |

(`…` = `C:\Users\antun\.claude\projects\C--Work-Projects-EfloStop-2-Firmware-Production-eFloStop-WiFiHub-idf1\ee520eed-0dc0-45ef-bdfe-91bf0b44762d`)

Phase E material:
- review run `wf_c8016931-1db`;
- fix run `wf_3bda8ac7-2db`;
- docs run `wf_4ad93e14-24d`;
- all findings with evidence and refuter verdicts: the session scratchpad `phaseE.json` (copied from the review run's journal).

---

## 10. Phase E (adversarial review), Saturday 2026-09-26

**How it ran.** Seven independent reviewers each attacked one area: decommission, the empty hub, valve lifecycle and identity, valve battery, concurrency, heap/NULL/stack, and boot/clock. They had the diff, the plan and the logs, but not the implementers' reasoning. Their 27 raw findings were merged into 22. Every non-nit was then checked by two refuters.
- **Result:** 13 confirmed, 3 disputed, 0 refuted, 6 nits.
- **Severity:** after verification every one is minor or lower. E-01 was filed as major, and both refuters rated it minor.

**Fixes.** One `fix(review)` commit per finding. They ran in four parallel lanes on disjoint files; each lane was adversarially reviewed, and lane B was fixed again.

| ID | Problem | Commit |
|---|---|---|
| E-02 | A wet sensor removed and re-added within the scanner's 10 s reload gave no event; the interlock auto-cleared while it was still wet | `03fc529` |
| E-03 | A real-epoch override restored after a power cut with no internet never expired, so auto-close stayed blocked | `328d6af` |
| E-04 | The old valve's leak source survived a valve swap and auto-closed the new dry valve. Follow-up `d37eb6c`: the latch is also released on a valve removal | `e2b8fb7`, `f3b65b0`, `d37eb6c` |
| E-05 | The empty-hub rules reset raced a following provision. The reset now happens inside the removal or provision that empties the hub | `20b229b`, `ade686b` |
| E-06 | An OPEN pended while the valve was away was applied before the leak's close. RMLEAK+CLOSE are now pended too; `valve_open` is refused while a leak is latched (user decision), failing closed | `fefe0da`, `2487ff5`, `c4b86e6` |
| E-07a | A command pended just as setup completed was stranded | `1458125` |
| E-08 | Valve health events had no identity across a valve swap | `c902a70` |
| E-09 | A quick decommission and re-provision of the same valve never rescanned | `3f87132` |
| E-10 | A snapshot flushed between a C2D change and the reconcile showed the old table | `9a8a4e5` |
| E-11 | `auto_close` claimed `rmleak_asserted:true` for a just-removed valve | `898e607` |
| E-12 | The valve resync could mark a wet valve dry | `daf670f` |
| E-13 | cJSON children leaked when an attach failed | `095b5d6`, `b3d3fd3` |
| E-14 | A partial snapshot counted as published. Follow-up `12a9b9e`: the hub name is checked too | `c277a4b`, `12a9b9e` |
| E-15 | Every EXCELLENT↔GOOD flip caused a snapshot. Only changes involving WARNING or worse now do | `6b89ab7` |
| E-16 | A connected valve's `last_seen_age_s` and its `offline_duration_s` were inflated (user decision: fix now) | `fe4d0b0` |
| E-17 | A skipped cache purge was never retried, and owed changes polled only every 30 s | `cfbeeb3` |
| E-18 | Unknown devices' check-ins drove pulse snapshots | `a55119f` |
| E-21 | A stale Wi-Fi reset comment (the portal is not BLE-free any more) | `5788eaf` |
| E-22 | A stamped offline event could be written back past 512 B | `307ed19` |
| user | No `auto_close` on a hub with no valve (user decision) | `502178c` |
| user | RMLEAK auto-clear changed from 30 s to 10 s, with a 2 s loop poll while pending (user request, approved) | `75a4a59` |
| docs review | A sensor re-wetting just as the lock was released (auto-clear or `leak_reset`) was misread as a valve-button press: a false 24 h override and auto-close blocked while wet. A newly latched incident now always re-locks and re-closes. The race predates 2.1.4 | `b245d94` |

**Documented, not fixed:**
- **E-01:** a snapshot needs about twice its size in one heap block. With NimBLE running, hubs of about 20 or more devices may fail to publish snapshots, and NimBLE now also runs on sensors-only hubs with a BLE sensor. Heap tuning is 2.1.5; the test plan will include a hub with 20 or more devices.
- **E-03 case B:** an unsynced override followed by a software reset ends at the first sync. This fails toward auto-close.
- **E-07b:** after a TERM_FAILURE the command waits for the next link. This is by design.
- **E-19:** a test-plan note only. At reconnect, `device_recovered` carries the fresh battery reading.
- **E-20:** the heap budget. Static RAM is about +176 B against 2.1.3, plus about 50 B of permanent heap for two log-level tags.

**User decisions, 2026-09-26:**
1. Refuse `valve_open` while a leak incident is latched and no override is active, even with the valve disconnected. The refusal reuses the RMLEAK message.
2. Fix the valve age now: `last_seen_age_s` is 0 while the valve is linked and counts from the drop; `offline_duration_s` is measured from the drop.
3. Suppress `auto_close` on a hub with no valve. This answers the long-open product question.
4. RMLEAK auto-clear is 10 s, with the 2 s poll.

**Accepted deviations:**
- E-03 times an unsynced window by max(uptime, unsynced clock), so a software reset cannot restart the count.
- `d37eb6c` releases the latch on a valve removal as well as on a swap.
- Five commit subjects are 73–79 characters. Reword them only if the branch is ever squashed.

**Still to feed into Phase F and G:**
- A bench check of the NimBLE host-task stack high-water mark. E-08 added about 54 B on the notify path, and the reviewer noted the DISC path too.
- The plan text still says "rating seq bumped on any system-rating change"; E-15 narrowed that.
- `validate_capture.py`'s new valve-less `auto_close` check can flag a leak that happens while provisioning is busy.

---

## 11. Phase E round 2 and Phase F council, Sunday 2026-09-27 (E/F round 2 of 2)

The user asked for this before flashing CP3. It ran as run `wf_40fd522e-eda` (results in the scratchpad file `phaseE2F.json`).
- **Round 2 adversarial review.** Six lenses targeted the post-CP2 code: rules safety, provisioning/empty hub, valve/health, telemetry/offline buffer, concurrency, and a whole-branch regression hunt against 2.1.3. Their 21 raw findings were merged into F-01 … F-15 and checked by two refuters each. Result: 7 confirmed, 3 disputed, 0 refuted, 5 nits.
- **Phase F council.** Five specialists voted on the final diff:

  | Specialist | Vote | Reason |
  |---|---|---|
  | F1 RTOS / concurrency | BLOCK | F-01, and B1 |
  | F2 memory / robustness | SHIP | |
  | F3 cloud contract / UI sync | SHIP | |
  | F4 BLE lifecycle | BLOCK | F-01 |
  | F5 persistence / upgrade | BLOCK | F-01 |

**The blocking issues:**
- **F-01.** The hub's own RMLEAK clear was pended while the valve link was down (auto-clear, `leak_reset`, or a failed clear that G4c pends). At relink, `BLE_UPD_CONNECTED` is posted before that clear is written and read back, so Priority 2 re-latched the incident. When the clear landed, tick Check 2 read it as a valve-button press and opened a false 24 h override, blocking auto-close. This predates 2.1.4; 2.1.4 widened it.
- **B1.** A regression from G4c. The GATT procedure pool holds 4, and each command costs a write plus its read-back. At setup completion plus the rules reconcile, `ble_gattc_write_flat()` returns `BLE_HS_ENOMEM`. G4c counted that as a failure and forced a relink, which discarded the queued replay CLOSE: a leak close was delayed 15–40 s per cycle, and the relink cap never engaged.

**Fixes** (run `wf_cffafc2b-e9d`, two lanes, each adversarially reviewed):

| Commit | What |
|---|---|
| `54c8ed0` | F-01 + F-05: RAM flags `g_rmleak_clear_owed` and `g_interlock_confirmed`, both under g_mutex. The auto-clear and `leak_reset` queue their clear under the rules lock. At reconnect, a clear the hub still owes is re-sent instead of re-latching. Check 2 fires only on a true 1→0 edge of a confirmed lock with no hub clear owed. Priority 2(a) is unchanged (F-03 deferred). +2 B `.bss` |
| `44d3d43` | F-08: the tick's rules event (`rmleak_auto_cleared`) is published before the same pass can overwrite it |
| `ee30f89` | F-15 comment |
| `c2da6be` | B1: GATT busy (ENOMEM) is waited out (250 ms steps, up to 5 s, then a replay token) and never forces a relink. The read-back waits the same way. Only a live write resets the relink cap |
| `d3b9ef8` | A valve command never overtakes an RMLEAK pended during setup (council F4, non-blocking) |
| `38439d2` | `app_ble_valve_signal_start()` claims the start atomically and clears the starter's handle (council F1, non-blocking). About +3 B `.bss` |
| `dcf0e07` | Lane V review fix: a busy RMLEAK replay keeps a pended CLOSE behind it (the RMLEAK-before-CLOSE order could reverse when the busy bound expired) |
| `d9fa9c8` | Retry-log fix after a busy wait |
| `98bec23`, `cc3103d` | Docs: CHANGELOG, schemas, catalogues, app-team docs, known limitations |

**User decisions, 2026-09-27:**
- **F-03 deferred to 2.1.5.** A leak latched while the valve was unreachable, followed by a hub restart (or the valve relinking less than 10 s after every sensor dried), can still be inferred as a button override by Priority 2(a). This is the SRS 4.4.2 cross-reboot inference, unchanged from 2.1.3.
- **F-02: keep 10 s everywhere.** After a restart during a leak, the first dry report starts the 10 s auto-clear before a still-wet sensor is heard again. RMLEAK is then released for up to one wet report (about 15 s for BLE, minutes for LoRa); the valve stays closed.

**Deferred to 2.1.5, documented in the CHANGELOG:**
- F-04: a leak dropped while provisioning is busy for more than 1 s.
- F-06 / F-07: the valve-replaced and empty-hub rules resets are not retried after a rules-mutex timeout.
- F-09: a late `device_offline` when a valve that went offline wet returns dry after more than 180 s.
- F-10: `rmleak` and `leak_state` read false during valve setup; this one is documented in the schema.
- F-11 and F-14.
- The twin (and lifecycle) can read `provisioned:false`, `valve_id` null and counts 0 when provisioning is busy.
- A provision that adds no device no longer re-arms the pulse or commission snapshot.

**Residuals from the lanes (none blocking; they go into the test plan):**
- A stale confirm: a re-wet at the clear whose SET read-back lands more than 5 s after the grace can still start a window. This predates 2.1.4; b245d94 had it too.
- The owed flag can be cleared early if setup step 7's RMLEAK read failed *and* the clear failed.
- A genuine press can be missed if iothub_task is blocked for seconds right after a close. This fails closed.
- The F-08 early publish now runs before Phase 2 in a pass whose slot is non-empty.
- At setup completion with both slots pended, the host's valve read-back gets rc=6. The position then comes from the valve's state notify; this predates 2.1.4.
- Sustained ATT stalls log about 8 lines per 5 s, bounded by NimBLE's 30 s GATT timeout.

**Commit subjects over 72 characters:** `44d3d43` joins the five listed in §10.

**Next:** 🔨 Build checkpoint 3 of `d9fa9c8`, then the council's final vote on it. BLOCKs left after that go to the user. Then Phase G (MANUAL_TEST_PLAN.md, including every residual risk the council listed in `phaseE2F.json`).

**Council final vote, Sunday 2026-09-27** (run `wf_1fd16b3a-fd2`, results in the scratchpad file `council_final.json`): **5/5 SHIP** on firmware `d9fa9c8`. F1, F4 and F5 confirmed that their F-01 and B1 blocks are resolved in the code. The non-blocking notes went into the CHANGELOG as known limitations and 2.1.5 items:
- the stale-confirm false "button" window, in two routes;
- the relink cap engaged with no replay token;
- the live-path hold when the queue is full;
- a busy command task for up to about 10 s;
- the evaluate_leak re-assert queued after the give;
- mbuf ENOMEM not bounded by the 30 s GATT timeout.

The bench expectations were corrected in §7 (F-01(a), the re-wet exception, and the CP3 `.bss` range 36,280–36,288). Two stale comments are left for 2.1.5: the evaluate_leak ordering claim, and the Priority 2 re-latch note about LEAK_RESET. Changing them now would move firmware off the CP3 commit. The events' arrival order at a connect edge can differ from `ts` order, so the backend must order by `ts`. **Phase F is complete, pending a clean CP3.** Next is Phase G.
