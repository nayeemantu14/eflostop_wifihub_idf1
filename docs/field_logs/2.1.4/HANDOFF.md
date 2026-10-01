# Hub FW 2.1.4: handoff notes (end of Friday 2026-09-25)

Written for the user and for the next Claude Code session. It records where the 2.1.4 fix job stands and how to pick it up again.

> **Resume here.** Read §15 first, then §14, §13, then §12-§12c, then §1, §7, §10 and §11.
>
> **Update, Thursday 2026-10-01 (latest): the 2.1.4 rework has started (WP-V, WP0); Build checkpoint 5 is next, now of `31b4c9f`.** The user approved the council's 2.1.4 proposal ("start with WP-V/WP0"; every recommended option except D9: the portal's Forget/Disconnect stays). WP-V carries the Wi-Fi manager in the tree as `components/wifi_portal` (`4f14a23`, `b74a891`); WP0 adds instrumentation with no behaviour change (`607df82`, `fbd8537`, `818ce43`, `afe77c1`, `41eb044`); their review fixes are `9f2d061`, `bd64e83`, `9c27644` and `31b4c9f`. With the page round of 2026-09-30 (`695283a` … `520b17a`), none of it is built yet. **The last firmware commit is now `31b4c9f`**, and §15b gives the build and its expectations, which differ from §14g's: the `git diff` pathspec now includes `components` and `dependencies.lock`; `idf.py fullclean` needs registry access (or delete `build\` instead); `sdkconfig` gains one line, `CONFIG_APP_BENCH_DIAG=y`, so its hash changes once; the link order changes (`wifi_portal` after `main`); `.bss` about +121 B. Then the bench: the WP-V gate's 10 s reset ×3 and G0.
>
> **Update, Wednesday 2026-09-30 (later): the user's four decisions on the router-rejoin fix; Build checkpoint 5 is next, now of `0f08d32`.** Round 1's open items went to the user, who decided (§14c): (1) an open portal page's chained scan pauses stop after 30 s, and BLE then gets 15 s with no pause of any kind; (2) each connect attempt's pause is capped at about 3 s of BLE off (2.5 s holds, the router retry's 0.5 s lead included, never back to back outside a page's chain); (3) the router retry runs whenever Wi-Fi is down with credentials saved, not only from the fallback SoftAP, which also recovers the Wi-Fi manager's idle state with no SoftAP; (4) a mistyped password on the fallback page is accepted and documented. Firmware `288db73`, `d0d07b9`, `06c0739`, then the fixer's `3d120b4`, `80be3e0` and `0f08d32` (comments only), after a review and a second 5/5 SHIP council on the whole fix (§14e). The last firmware commit is now **`0f08d32`**; CP5 covers it and the D1 fix, and §14g gives the new expectations (`.bss` 36,336 or 36,344 B). Next: VAL-01 for 🔨 Build checkpoint 5 of `0f08d32`, then T4-10 Part F, the valve-hub A-steps and Part D, now D1-D13 (§14i). Open for the user before a field release (§14f): a dry BLE sensor can be reported offline while a portal page stays open about 10 min or more; the page counts as closed 10 s after its last request, so a submit made as a phone comes back from the background can meet a retry and reboot the hub (the Wi-Fi specialist suggests 60 s); a submit in the 15 s listening time runs with BLE on; and §13e's heap floor, now analysed: pre-existing (the open fallback SoftAP), not caused by this fix.
>
> **Update, Wednesday 2026-09-30 (morning): the router-rejoin fix; Build checkpoint 5 was then of `8fb8340` (now `0f08d32`, see the latest update above).** The 17:20 session of 2026-09-29 (§13e) showed two defects that are also in 2.1.3: after a router outage the hub never rejoined its router (the Wi-Fi manager's START_AP patch `6ad1d7b` stops its retry timer, and nothing else retried), and the fallback portal's network list stayed empty (the BLE scans starved the Wi-Fi scans). The user chose an app-side fix: a router retry every 30 s from the fallback SoftAP, and short BLE pauses around each Wi-Fi scan and connect attempt while Wi-Fi is down, with no health hold. Firmware `727e6c1`, `58b0606`, `cd6ab28`, `8fb8340`, after three reviews, one fixer round and a 5/5 SHIP council (§14). The last firmware commit was then **`8fb8340`**, and CP5 was to cover it and the D1 fix together. Next: VAL-01 for 🔨 Build checkpoint 5 of `8fb8340`, then T4-10 Part F, the valve-hub A-steps and Part D (§14i). Four design decisions are open for the user before a field release (§14f), and §13e's heap floor is still not analysed.
>
> **Update, Tuesday 2026-09-29 (evening): CP4 passed, first bench session, D1 fixed; Build checkpoint 5 is next.** Build checkpoint 4 of `46a1f0a` passed (§13). The first bench session on it found one defect, D1 (T5-03 step 3: after the auto-clear the cloud kept `"rmleak":true` until the next heartbeat; the same code is in 2.1.3). The user chose a firmware fix: `4e6fe71` and `f424d65` (comments only), so the last firmware commit is now **`f424d65`**. The user also waived, for 2.1.4, the T4-10 steps that need a LoRa sensor (D2): "go red" is covered by code review and the 5/5 council only. The captive-portal test (T4-10) has still not run. Next: VAL-01 for 🔨 Build checkpoint 5 of `f424d65` (an incremental build of it from 17:00 already has the expected sizes, §13), then T4-10 Part F, the valve-hub A-steps and Part D (§13 "Run next"). A later session's logs (17:20) show `min_ever` 1,184 B on the router-outage fallback portal, not yet analysed (§13).
>
> **Update, Tuesday 2026-09-29 (later still): "go red"; Build checkpoint 4 is still next.** The user decided §12a's open question 2: once a pended leak response has made the hub hunt for its valve in the portal window and that hunt has not reached the valve, the valve's health hold ends 180 s after the hunt, so the valve counts (RED, "Valve offline") while setup is still running. Firmware commits `9951bf4` … `46a1f0a` (§12b); the last firmware commit is now **`46a1f0a`**. Next: 🔨 Build checkpoint 4 of `46a1f0a` (§12b), then the smoke subset and T4-10 Parts A-I.
>
> **Update, Tuesday 2026-09-29 (later): portal follow-up; Build checkpoint 4 is still next.** The user decided that the 10 s reset must always erase the Wi-Fi credentials, superseding "leave it": during a router outage it used to keep them, and the fallback portal it rebooted into keeps BLE scanning, so a changed router password could lock the customer out. The lead decided to close the portal window when the setup SoftAP stops (about 60 s after the IP) instead of at the IP, so the phone can load the success page, and to hold the valve's health verdicts during the pause like the BLE sensors'. Firmware commits `93b8629` … `cc66d72` (§12a); the last firmware commit was then **`cc66d72`** (now `46a1f0a`, see the latest update above). Next: 🔨 Build checkpoint 4 (§12a, §12b), then the smoke subset (step 10 is the portal) and T4-10 Parts A-H. The questions still open for the user are at the end of §12a.
>
> **Update, Tuesday 2026-09-29: captive portal fix; Build checkpoint 4 is next.** The bench capture (`C:\Users\antun\Desktop\UART logs.txt`, hub `GW-7C4FADAE69C8`, 2 BLE sensors, no valve) showed that after the 10 s reset a phone could not join the SoftAP: no DHCP lease was ever given. BLE scanning, which 2.1.4 starts at boot, starved the SoftAP of radio time. The fix is the portal priority window (§12). The last firmware commit was then **`ca4835f`** (now `cc66d72`, see the later update above). Next: 🔨 Build checkpoint 4 (same commands as CP3), then T4-10 (rewritten) and the smoke subset. The test plan's VAL-01 still pins `d9fa9c8` and is re-baselined at CP4 (see its header note). §12 has the evidence and the first-round user decisions; §12a has the follow-up, the bench checks, the CP4 procedure and, at its end, the questions still open for the user.
>
> **STATUS, Sunday 2026-09-27: phases A–G are COMPLETE.** Firmware `d9fa9c8` passed Build checkpoint 3 (§4b). The council voted 5/5 SHIP (§11). `MANUAL_TEST_PLAN.md` is committed (`d595633`). What remains is the user's bench campaign: start with the smoke subset in section S of the test plan, then the full plan. Nothing is pushed, and there is no PR. **Before any push, redact the Wi-Fi password in `6b84ae3`.**
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
| F: 5-specialist council | **done**: round 1 BLOCK (F-01, B1), both fixed; final vote 5/5 SHIP on `d9fa9c8` (§11) |
| Captive-portal regression (2026-09-29) | fixed in `5b5d70e` … `ca4835f` (docs `b7783d0`, `ac73cc6`); follow-up `93b8629` … `cc66d72` and its docs commit (§12a); "go red" `9951bf4` … `46a1f0a` and its docs commit (§12b); Build checkpoint 4 of `46a1f0a` **passed** (§13); the D1 fix `4e6fe71`, `f424d65`; the router-rejoin fix `727e6c1` … `8fb8340` and, for the user's decisions of 2026-09-30, `288db73` … `0f08d32` (§14); the page round `695283a` … `520b17a`; the 2.1.4 rework's WP-V and WP0 with their review fixes, `4f14a23` … `31b4c9f` (§15); 🔨 **Build checkpoint 5, now of `31b4c9f`, next** (§15b) |
| G: MANUAL_TEST_PLAN.md + summary | **done**: version grep clean; CP3 build summary in §4b; `docs/field_logs/2.1.4/MANUAL_TEST_PLAN.md` (`d595633`, 114 tests, traceability matrix, ~30 min smoke subset) |

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
- **Neighbour valve** (the second valve `00:80:E1:27:7E:C5` on the bench). Test with no valve provisioned, and with a different valve provisioned. The hub must never connect, never send CLOSE, never answer the passkey. Expect `[SCAN] No provisioned valve - not scanning for valves` or `[SCAN] Starting scan for provisioned valve <MAC>`. *(Corrected 2026-09-29, §13: with no valve provisioned the line is `[CMD] CONNECT refused - no provisioned valve`, once at boot, as in the next item; no valve scan is requested.)*
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
     - **Remove and re-add a wet sensor within 10 s:** `leak_detected` arrives at once; `auto_close` only if the 10 s auto-clear had already fired (otherwise the valve is still closed with RMLEAK set and nothing more is needed).
     - **The full, code-verified procedure for all of the above is `docs/field_logs/2.1.4/MANUAL_TEST_PLAN.md`** (114 tests, traceability matrix, ~30 min smoke subset in its section S). Where this list and the test plan differ, the test plan wins.
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
- **Off-limits.** Do not touch `managed_components/`, `sdkconfig*`, the partition table, DPS/SAS/crypto, or NVS namespaces, keys or layout. (Since 2026-10-01 the Wi-Fi manager is the local component `components/wifi_portal`, which the 2.1.4 rework edits by the user's decision D11, §15; the rest stands.) Field units OTA from 2.1.3 and must keep their provisioning; rolling back to 2.1.3 must keep it too.
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
| E-21 | A stale Wi-Fi reset comment (the portal is not BLE-free any more). Revised 2026-09-29: BLE scanning pauses beside a no-credentials portal (§12) | `5788eaf`, `f65a95b` |
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

---

## 12. Captive-portal regression (2026-09-29)

**Symptom.** On the CP3 image (`d9fa9c8`), after the 10 s Wi-Fi reset on a hub with a valve or a BLE sensor, the phone listed the SoftAP (`WiFi-Hub-69C8`) but iOS said "Unable to join the network". The hub never gave a DHCP lease, so the setup portal could not be used.

**Log evidence** (`C:\Users\antun\Desktop\UART logs.txt`, 2026-09-29; hub `GW-7C4FADAE69C8`, no valve, 2 BLE sensors; the ELF matches the `d9fa9c8` build):
- Boot 2, after the reset: `esp_netif_lwip: DHCP server started on interface WIFI_AP_DEF with IP: 10.10.0.1` at 735 ms, `dns_server: DNS Server listening on 53/udp` at 835 ms, `IOTHUB: Starting BLE (valve=none, BLE sensors=2)` at 875 ms, `BLE_LEAK: Extended passive scan started (1M + Coded PHY)` at 3095 ms.
- About 500 s of portal followed with **no** `DHCP server assigned IP to a client` and **no** `dns_server: Replying to DNS request` line. On 2.1.3 (`docs/field_logs/2.1.3/UART logs.txt`) the lease came 1.2 s after the phone joined, and the first DNS reply about 2 s later.
- The heap was flat (`free` about 51 KB, `min_ever` 47,796 B, `largest_blk` 31,744 B), with no reboot and no stalled task. The failure is below IP (association or DHCP), not heap or CPU.
- Boot 1: the reset was pressed while the STA was idle (the hub was already on its SoftAP). No `APP_WIFI: WiFi Disconnected` line printed between `Erasing WiFi credentials …` and `Rebooting into AP mode...`, so the erase did not run. Nothing was saved there, but this is the evidence for the reset-button limitation fixed in §12a.

**Cause** (RCA in the session scratchpad, `portal_rca.json` / `portal_rca.txt`, not committed). 2.1.4 starts BLE at boot (N1, P0-b). Its continuous 1M + Coded leak scan, and on a valve hub the valve hunt and the 100 %-duty connect initiator, take the single 2.4 GHz radio from an idle-STA SoftAP under software coexistence; ESP-IDF rates a SoftAP connecting beside a BLE scan as unstable. 2.1.3 never started BLE in AP mode. Secondary, not fixed: the heap beside NimBLE (about 51 KB free against about 126 KB on 2.1.3), and the portal page's own Wi-Fi scan every 3.8 s (managed component).

**User decisions (2026-09-29, first round).** Items marked *superseded* were changed later the same day (§12a).
- Pause BLE scanning (the leak scan, the valve hunt and new connects) only while the portal is up and **no** Wi-Fi credentials are saved. NimBLE stays up, and an established valve link is kept. No time cap before setup. *(Superseded in part: the window first closed when the STA got an IP (`cb_connection_ok`); it now closes when the setup SoftAP stops, lead decision B in §12a.)*
- Do not open the window for the router-outage fallback AP (credentials still saved): BLE leak protection stays on there.
- Hold the BLE sensors' health timeouts over the pause. Do not touch the LoRa sensors. *(Superseded for the valve: lead decision C in §12a holds the valve too.)*
- Raise the `wifi_manager` task priority for the window.
- *(Superseded by the user decision in §12a.)* Keep the 10 s reset's erase behaviour as it is: it erased the credentials only while the hub was connected to the router.
- Accepted protection trade-off: in the window BLE sensors are not scanned and a lost valve link is not re-found; LoRa sensors, the rules engine and an established valve link keep working. That is still more than 2.1.3, which ran no leak protection at all until Wi-Fi was set up.
- **Still to confirm:** the review added one exception (`b548d50`). While a leak close (RMLEAK, then CLOSE) is pended for an unlinked valve, the hunt and the connect run in the window anyway, until the valve takes it. To revert it, make `portal_holds_valve()` return `app_wifi_portal_priority_active()` only.

**Fix commits.**

| SHA | What |
|---|---|
| `5b5d70e` | Health hold: `health_set_ble_scan_paused()`; a BLE sensor's offline and unheard verdicts are held while paused and restart 600 s from the resume |
| `f65a95b` | The window: START_AP/STOP_AP callbacks, `app_wifi_portal_priority_active()` (`portal_priority.h`), the leak-scanner and valve-hunt gates, the SoftAP station join/leave log, the E-21 comment in `reset_button.c` |
| `03c69a7` | The `wifi_manager` task runs at priority 8 in the window. `httpd` and `dns_server` are not raised |
| `b548d50` | Review fix: a pended leak close still hunts and links the valve; level-triggered cancels (NimBLE's own connect re-attempt included); a held hunt always restarts; `is_scanning` set before the scan starts |
| `529f6d1` | Review fix: the close restores `WIFI_MANAGER_TASK_PRIORITY`, not a sampled (possibly inherited) priority |
| `efd6d84` | Review fix: the snapshot gate waits for BLE sensors not yet heard, for the pause and 180 s after the resume |
| `ca4835f` | Review fix: log when the command task stops a valve hunt (`[PORTAL] Valve hunt stopped …`) |
| `b7783d0`, `ac73cc6` | Docs: T4-10 rewritten (Parts A-G), T6-13, smoke step 10, the traceability rows; the CHANGELOG *Safety* entry, the new log lines and the known limitations; this section |

Static RAM added by this first round: about 11 B of `.bss` (health 4 B, `app_wifi` 5 B, valve 2 B) plus alignment; about 40 B of permanent heap for the SoftAP station-log event handler; no IRAM.

### 12a. Follow-up (2026-09-29, later): the reset always erases, the window closes at the AP stop, the valve is held

**User decision (2026-09-29).** The 10 s reset button must **always** erase the saved Wi-Fi credentials and reboot into the setup portal, whatever the STA is doing: connected, idle, on the router-outage fallback portal, or already in the portal. This supersedes the first-round "leave the erase unchanged". Re-confirmed at the same time: no time cap before setup; the window pauses BLE only while no credentials are saved, and the fallback AP (credentials saved) keeps BLE scanning; the BLE sensors' unheard/offline verdicts are held through the pause and get a fresh timeout from the resume (already built); `b548d50` is kept unless the user objects.

**Lead decisions.**
- **B. Close the window when the setup AP stops, not at the STA IP.** wifi_manager keeps the SoftAP up for 60 s after the IP (`CONFIG_WIFI_MANAGER_SHUTDOWN_AP_TIMER`), and the phone that submitted the credentials is still on it, loading the portal's success status ("Connected!"). Resuming the dual-PHY leak scan at the IP would starve the SoftAP again during that minute. Two safety nets keep BLE from staying paused while the hub is on Wi-Fi: the window closes at once if the STA loses that Wi-Fi before the SoftAP stops (the SoftAP then stays up as a fallback portal with the credentials saved, which keeps BLE scanning), and if the window is still open 75 s after the IP, `wifi_task` sends the STOP_AP itself (for when wifi_manager started no shutdown timer). BLE therefore resumes at most about 80 s after the IP.
- **C. Hold the valve's health verdicts during the pause, like the BLE sensors'.** The valve hunt is paused, so a valve hub should not read critical (fleet LED RED) during Wi-Fi setup only because it stopped looking. A valve not linked yet stays excused ("syncing"), and a valve whose link drops stays in its WARNING grace ("Valve disconnected"), until 180 s after the resume. A valve already offline stays offline; the snapshot's valve `connected` and `last_seen_age_s` keep their real values. *(Amended by the user decision "go red", §12b: after a leak-response hunt in the window, the valve's hold ends 180 s after that hunt.)*

**Why the reset needed its own erase.** wifi_manager erases the credentials only in its STA_DISCONNECTED handler, after `wifi_manager_disconnect_async()`. An idle STA never gets that event, and on the router-outage fallback portal the STA is idle (the START_AP local patch stops the retry timer). So a reset there rebooted the hub with the old credentials, back onto the fallback portal, which keeps BLE scanning: a phone may not join it, and a customer whose router SSID or password changed was locked out (boot 1 of the 2026-09-29 capture shows the idle-STA case). The reset now overwrites the existing `ssid` (32 B) and `password` (64 B) blobs of the `espwifimgr` namespace with zeros, SSID first, after the 2 s wait. That is the state wifi_manager's own erase leaves, and `wifi_manager_fetch_wifi_sta_config()` reads it as nothing saved (it returns `ssid[0] != '\0'`), so the portal opens about 0.7 s after the reboot. It holds wifi_manager's NVS mutex (`nvs_sync_lock`, 3 s timeout) through `esp_restart()`, so a late GOT_IP cannot save the credentials back. The keys are not erased: a missing key makes the fetch return without `nvs_close()`, which would leak about 50 B of heap on every boot until Wi-Fi is set up (the first cut, `93b8629`, did that; `eeee9ca` replaced it). No key is created, `settings` is kept, and no NVS layout changes.

**Commits.**

| SHA | What |
|---|---|
| `93b8629` | fix(reset): the 10 s reset always erases the Wi-Fi credentials: `erase_wifi_credentials()` in `reset_button.c`, run between the 2 s wait and the reboot, with wifi_manager's NVS mutex held through the restart; four new `RESET_BTN` lines |
| `73b4483` | fix(portal): the window closes at STOP_AP (`cb_ap_stopped`), not at GOT_IP. `s_setup_ok_tick` records the GOT_IP; `cb_connection_lost` closes the window ("Wi-Fi lost after setup") when the STA drops after setup with an SSID still set; `portal_priority_net()` on `wifi_task` sends STOP_AP 75 s after the IP; two new `APP_WIFI` lines |
| `a4894af` | fix(health): the valve's verdicts are held while BLE scanning is paused: `valve_offline_held()` keeps a dropped valve in its WARNING grace, `unheard_hold_left_s()` holds a never-linked valve's excuse, and the snapshot gate also waits for a never-linked valve |
| `eeee9ca` | fix(reset): zero the saved credentials instead of erasing the keys (the per-boot heap leak above), SSID first |
| `38200ac` | fix(health): the two `HEALTH_ENGINE` pause/resume lines are back to their `5b5d70e` bench text |
| `b2369a8` | docs(portal): the window comments name the valve's timeouts too (comments only) |
| `cc66d72` | docs(health): the valve hold's edge after a leak response (comments only) |
| the docs commit after `cc66d72` | Docs: the CHANGELOG (the *Safety* entry, the serial-log list, the known limitations), MANUAL_TEST_PLAN.md (T4-10 Parts A-H, smoke step 10, 0.15, M.2, M.4, Appendices A.2, B.1, B.2), this section |

**Memory.** `.bss` +4 B (`s_setup_ok_tick`), so about 15 B for the portal work in all. Flash `.rodata` about +80 B (the 64 B zero blob and the key table) plus the new code and log text. IRAM: nothing new. `xTaskGetTickCount` is already linked in IRAM, and `nvs_sync_lock`, `nvs_get_blob`, `nvs_set_blob`, `nvs_erase_key`, `nvs_commit`, `nvs_open`, `nvs_close`, `wifi_manager_send_message` and `wifi_manager_get_wifi_sta_config` are already linked in flash (build map of 2026-09-27). No new heap, timers or tasks: the reset's two NVS handles live only during the reset. The `reset_btn` stack stays 3072 B (about 32 B of new locals; not measured).

**New and changed log lines** (checked against the code at `cc66d72`):
- `RESET_BTN` (new), one of them just before `Rebooting into AP mode...`: `Wi-Fi credentials erased from NVS`; `No Wi-Fi credentials saved - nothing to erase`; the error `Wi-Fi credential erase failed (%s) - rebooting anyway`; before them, the warning `Wi-Fi NVS lock busy for 3 s - erasing without it`. The last two should never appear.
- `APP_WIFI` (new): `portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops (about %d s)` (prints 60), right after `Connected! IP: %s` in the window; the warning `portal priority: setup AP still up %u s after Wi-Fi connected - stopping it` (the safety net; should never appear).
- `APP_WIFI` `portal priority OFF (%s) - BLE scanning resumed`: same format, reasons now `AP stopped` or `Wi-Fi lost after setup`. `Wi-Fi connected` no longer prints.
- `HEALTH_ENGINE` `BLE scanning paused - BLE sensor timeouts held` and `BLE scanning resumed - BLE sensor timeouts restart now (%d s)`: unchanged text (restored by `38200ac`); they now also hold and restart the valve's 180 s.
- Same text, new conditions: `Boot sync: timeout …` also waits for a never-linked valve; `Roll-up grace expired …` for a never-linked valve comes no earlier than 180 s after the resume. *(For the valve, "go red" amends both after a leak-response hunt, §12b.)*

**Bench checks** (the full procedure is `MANUAL_TEST_PLAN.md` T4-10 Parts A-H, and smoke step 10):
1. **The phone joins and the success page loads** ("Connected!", "Your WiFiHub is now on your home network") while the SoftAP is still up after the save: on a sensors-only hub (T4-10 F2, F4) and on valve hubs, valve linked (A8) and valve not linked (E4, smoke step 10).
2. **BLE resumes about 60 s after the IP:** `Connected! IP`, `portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops (about 60 s)`, then 60 s (±5 s) later `HEALTH_ENGINE: BLE scanning resumed - BLE sensor timeouts restart now (600 s)` and `portal priority OFF (AP stopped) - BLE scanning resumed`; no `setup AP still up` (A8, F4). A wrong password keeps the window (F3).
3. **Router outage:** the fallback AP logs `SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`, never `portal priority ON`, and a BLE leak still closes the valve (D1, D2). The phone join there stays observe-and-record (D3).
4. **The 10 s reset during a router outage** (STA idle on the fallback AP): normally no `WiFi Disconnected. Reason: 8`, then `RESET_BTN: Wi-Fi credentials erased from NVS` and `Rebooting into AP mode...`; after the reboot the no-credentials portal (`DHCP server started on interface WIFI_AP_DEF` and the window-open lines within about 1 s of boot, no fallback line), and the phone can set the hub up again (D5, D6). On a connected hub the reset also prints `Wi-Fi credentials erased from NVS` (A2).
5. **Sensors heard again after setup, no false `device_offline`:** every BLE sensor's `eleak … — leak=0` within about 100 s of `portal priority OFF`, and no `device_offline` for them (A8, E4, F4).
6. **Valve hub LED not RED during setup** (no leak-response hunt; B3 now checks "go red", §12b): WHITE ("syncing") with the valve not linked, with no `Roll-up grace expired` for it (A3, B1, E2); YELLOW ("Valve disconnected") for a valve that drops in the window, still YELLOW 4 min later (H2).
7. **Connected, then Wi-Fi lost within the 60 s:** `portal priority OFF (Wi-Fi lost after setup) - BLE scanning resumed` within 1 s of `WiFi Disconnected`, BLE scanning with the SoftAP still up, no `portal priority ON`; the router's return prints no portal line (H3, H4).
8. **A 12 min window:** no `device_offline` and no `Roll-up grace expired` for the pause; the gate times out 180 s and the BLE grace expires 600 s after `portal priority OFF`, never earlier (E2-E4).
9. **Heap:** `free`, `min_ever` and `largest_blk` while the phone loads the page (the T4-10 heap table).

**Next: 🔨 Build checkpoint 4 (CP4)**, now of firmware **`46a1f0a`** ("go red" came after `cc66d72`, §12b), the last commit that changes `main/`; everything after it is docs. Use the CP2/CP3 commands (§7 item 2) with `build_cp4.log`, and compare against §4b:
- no new warnings under `-Wall -Werror=all`: only the four from `master`;
- IRAM unchanged (16,384 B, 100 %): no `IRAM_ATTR`, and no newly linked IRAM function;
- `.bss` about +19 B over CP3 (36,280) plus alignment (15 B here and 4 B in §12b), so 36,280–36,312 is a pass, and more than about +40 B needs a look; `.data` 21,572; flash a few KB more than CP3;
- the version reads 2.1.4;
- then flash, run the smoke subset (its step 10 is the portal), then T4-10 Parts A-I. Re-baseline VAL-01 and EC-1 with the CP4 figures (the test plan's header notes: the firmware under test is `46a1f0a`).

**Open for the user:**
1. **`b548d50` (the leak-response hunt in the window) is kept unless you object.** To revert it, make `portal_holds_valve()` return `app_wifi_portal_priority_active()` only. Its subject is 73 characters; it joins the list in §10 and §11 (reword only if the branch is squashed).
2. ~~**The valve hold after a leak-response hunt (question from the fix review).**~~ **Decided 2026-09-29: "go red"** (the 4-byte stamp; built in §12b). A valve that a leak-response hunt in the window has not reached counts 180 s after that hunt, with setup still running, and the fleet LED can turn RED during Wi-Fi setup after even a short failed leak-close hunt. The CHANGELOG lists that as a known limitation.
3. **`httpd` and `dns_server` are not raised.** Their handles are private to the managed component, and the one lookup by name, `xTaskGetHandle()`, is not linked and is IRAM-resident: linking it would cost 512 B of heap.
4. **Pre-existing, not the portal (2.1.5).** With `CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT`, a valve link that fails with 0x3E is re-attempted by NimBLE with no DISCONNECT event to the app. If the re-attempt cannot be issued (for example `BLE_HS_EBUSY` while the leak scan runs, since `CONFIG_BT_NIMBLE_HOST_ALLOW_CONNECT_WITH_SCAN` is off), `valve_conn_handle` is left stale and later relinks are refused as "Already connected".
5. **The wifi_manager component (read-only, not changed).** The portal page's own disconnect button has the same idle-STA limit as the old reset: on the fallback portal it erases nothing (a CHANGELOG known limitation; the 10 s reset is the way out). The component also logs the Wi-Fi password at INFO (`http_app.c` on the portal POST, `wifi_manager.c` on the fetch); 2.1.4 keeps both hidden by setting the `wifi_manager` and `http_server` tags to WARN (`main.c`).

Fixed by §12a and removed from this list: the reconfiguration lockout (the reset kept the credentials during a router outage), the valve hub reading RED during a boot-time window, and BLE resuming at `Connected! IP` while the phone still needed the SoftAP for the success page.

### 12b. "Go red" (2026-09-29, latest): a leak-response hunt ends the valve's hold

**User decision (2026-09-29), answering §12a open question 2.** During Wi-Fi setup, once a leak response has actually made the hub hunt for the valve in the portal window (the `b548d50` exception) and the valve has not been reached, the valve's health hold ends 180 s after that hunt started, so the valve counts although setup is still going. A never-linked valve's excuse latches (CRITICAL in the roll-up, RED fleet LED, "Valve offline"); a dropped valve goes CRITICAL/LINK once its own 180 s grace has run too (`device_offline`). That stays so after the leak clears, until the valve links. Without a leak response (or a pended CLOSE) nothing changes: setup shows no RED for the valve. Any pended CLOSE runs the hunt, so a cloud `valve_close` pended while the setup AP is still up after the IP stamps too: accepted, the hub tried to close the valve and could not reach it.

**Why.** Decision C holds the valve because the hub stops looking for it during setup, so the fleet LED must not read RED only because the hub stopped looking. A leak-response hunt is the hub looking for the valve while there is water to shut off. If it cannot reach the valve, that is the fault the fleet LED and the cloud must show. The old hold hid it for as long as setup lasted, plus 180 s. The cost, which the user accepted, is a RED during Wi-Fi setup after even a short failed leak-close hunt. The CHANGELOG lists it as a known limitation.

**Design.** `s_valve_hunt_s` (`health_engine.c`, 4 B `.bss`) holds the monotonic second (`now_s()`, forced non-zero; 0 = none) a valve hunt for a pended leak response started in the current scan pause, with the valve not linked since. `health_note_valve_leak_hunt()` (`health_engine.h`) sets it lock-free, only while scanning is paused and no stamp is held, and logs one line. It is cleared by `health_set_ble_scan_paused(true)` before the pause is stored, by a CONNECTED applied while `ble_valve_is_ready()`, and when the reconcile removes the valve; the resume keeps it, so after the resume the valve's hold ends at the earlier of the two ends. `valve_hold_left_s()` (the pause hold, capped at 180 s after the stamp) drives `valve_offline_held()`, `unheard_hold_left_s()` for the valve and the valve's share of `ble_gate_held_locked()`, so a never-linked valve whose hold ended no longer keeps the snapshot gate shut. BLE sensors are unchanged. `app_ble_valve.c` notes the hunt where `start_scan()` lets it run despite the window (beside `[PORTAL] Leak response pending …`), and `portal_priority_poll()` notes on every pass while the window is open, a leak response is pending, the valve is not linked and a hunt or connect runs. That covers a hunt already running when the window opens, one running on across a window closed and reopened between two polls, and a stamp lost to a CONNECTED applied just as the link dropped (the clear is a lock-free check then store).

**Commits.**

| SHA | What |
|---|---|
| `9951bf4` | fix(health): the stamp, the note and `valve_hold_left_s()`; the note in `start_scan()`'s exception path and at the poll's window edge |
| `885acbe` | fix(portal): the poll notes on every pass, not only at the window edge (fix review: a drop between the CONNECTED clear's check and its store lost the stamp for the rest of setup) |
| `d0550db` | docs(health): the stamp restarts after the valve links (comments only) |
| `46a1f0a` | docs(portal): `app_wifi.c` says a leak-response hunt ends the valve's hold early (comments only) |
| `ec6a2b0` | Docs: the CHANGELOG (*Safety*, the serial-log list, the known limitations), MANUAL_TEST_PLAN.md (header notes, 0.15, T4-10 A6, B2-B4, H2, the new Part I, the pass criteria, T6-13, M.2, M.4, M.7, B.2, Appendices A.2 and B.1), this section |
| the docs commit after `ec6a2b0` | Docs: every quoted line re-checked against `46a1f0a`; MANUAL_TEST_PLAN.md (the first header note now pins `46a1f0a`, T4-10 A10 counts the new line, I5 replays the I1-I4 events, B.2 greps the new line); §12a's decision C and log lines point here; this section's reason, memory note, bench check 5, residuals and open items |

**Memory.** `.bss` +4 B at most (`s_valve_hunt_s`), so about 19 B above CP3 for the portal work in all. In the 2026-09-29 build map, `health_engine.c`'s `.bss` is laid out in reverse declaration order and starts with `s_ble_listen_s` (0x3fcaa714), and it has 4 B of alignment fill at 0x3fcaa724, before the 8-aligned `s_boot_start_ms`. The new word would come first and shift the next words into that fill, for 0 B net. Only the CP4 build can confirm this. No heap, timer or task. IRAM: nothing new (no `IRAM_ATTR`; `esp_timer_get_time` is already linked in IRAM, and `ble_gap_conn_active` and `ble_valve_is_ready` in flash, build map of 2026-09-29). Flash: the new code and one log string.

**New and changed log lines** (checked against the code at `46a1f0a`):
- `HEALTH_ENGINE` (new, info): `Valve hunt for a pended leak response during the scan pause - valve timeouts count from now (%d s)` (prints `180`), right after `BLE_VALVE: [PORTAL] Leak response pending - valve hunt runs despite the Wi-Fi setup portal` or `[PORTAL] Valve hunt not paused - a leak response is pending`. In the same window it prints again only after the valve has linked and dropped (or, very rarely, when the poll restores a stamp that such a CONNECTED wiped). As rarely, it prints with no `[PORTAL]` line just before it: the poll's own note for a hunt that ran on across a window closed and reopened between two polls.
- Same text, new conditions: `Roll-up grace expired (180 s) — 1 unheard device(s) now count` for a never-linked valve 180 s after that line, with the window still open; `ALERT: valve <VALVE_MAC> warning -> critical (device_offline)` for a dropped valve 180-210 s after it (the 30 s tick); `Boot sync: timeout …` no longer waits for a never-linked valve whose hold that hunt ended.

**Bench checks** (`MANUAL_TEST_PLAN.md` T4-10):
1. **Never-linked valve (B2-B4):** the new line right after B2's `[PORTAL] Leak response pending`; L dried within 60 s; at the line + 180 s (±2 s), with the window still open, `Roll-up grace expired (180 s) — 1 unheard device(s) now count` and `FLEET_LED: rating=critical color=RED effect=SOLID`; no `device_offline`; B4 prints no second line, and its link ends the RED from the valve.
2. **Dropped valve (Part I, new, about 15 min):** the valve linked in the window (I1), powered off (I2: YELLOW, held), then L wet (I3: the line) and dried (I4): `ALERT: valve … warning -> critical (device_offline)` and RED 180-210 s after the line, window still open; after setup (I5) the replayed `device_offline` and a live `device_recovered` at the link.
3. **A valve that links in time (A6, H1):** the line prints, the valve links, no RED comes from it; H2 stays YELLOW (no leak response pending).
4. **No leak (A3, B1, E2, smoke step 10):** unchanged: WHITE, no `Roll-up grace expired` for the valve.
5. **The line only where expected:** Appendix B.2's grep finds `Valve hunt for a pended leak response` only in T4-10 A6, B2, H1, I1 and I3 (A10 expects exactly one in Part A).

**Residual (disclosed).**
- The CONNECTED clear can still lose a stamp to a drop in the few instructions between its check and its store. The poll restores it within 1 s, but only while the leak response is still pending. If that response is withdrawn in the same second, the valve keeps the pause hold (the pre-"go red" behaviour) for that hunt.
- `start_scan()` notes the hunt before it starts the scan. If `ble_gap_ext_disc()` then fails, the stamp stays, so the error is towards RED, never towards a missed RED.
- A dropped valve turns CRITICAL at the first 30 s health tick after the hold ends, so up to 30 s late, like any grace expiry. A never-linked valve's excuse latches within the fleet LED's 250 ms poll.

**Open items.**
1. The body of `9951bf4` says the note stamps "only once" and that the poll notes only at the window edge. `885acbe` (a note on every poll pass) and `d0550db` (the stamp restarts after the valve links) changed both. The history is not amended; the table above and the code comments are right.
2. `app_wifi.c` (`46a1f0a`) states the exception; `portal_priority.h` does not describe the health hold at all, so it needs no change.
3. Nothing here was built or flashed: CP4 and the bench checks above are still to do.

### 12c. Council on the whole portal fix (2026-09-29): 5/5 SHIP

Five specialists (RTOS, memory/build, BLE/coexistence, Wi-Fi/portal, leak safety) reviewed `d714614..36ddd4b` (firmware `46a1f0a`) and all voted SHIP, with no blocking issue. Their CP4 gates and bench risks:

- **Sizes, measured.** A build of `cc66d72` (the map in `build/`, 13:19) already gives IRAM 16,384 B, DIRAM `.text` 113,387 B (identical to CP3), `.data` 21,572, `.bss` 36,304 (+24 B over CP3), `.bin` 1,533,280 B, with no `-Wall` warning. "Go red" adds one 4-byte word. **CP4 expects `.bss` 36,304–36,308 (a pass up to 36,312), `.data` 21,572, DIRAM `.text` exactly 113,387, `.bin` a few hundred bytes above 1,533,280.** IRAM always reads 16,384 B (100 %) on the ESP32-S3, so the real IRAM gate is DIRAM `.text`: any change there means a newly linked IRAM function, so stop and look.
- **Warnings:** only the four from `master`. `9951bf4` and `885acbe` are the only code not yet compiled.
- **Heap:** about 70–80 B lower than CP3 everywhere (the `.bss` plus the SoftAP station-log handler's event node, registered on every hub). Repeated 10 s resets must show a stable `free` at each portal boot.
- **Watch:** no stack-overflow panic in `sys_evt` (2,304 B) when a phone joins (the new 8-argument station log) nor in `reset_btn` (3,072 B) at the erase; `Wi-Fi credentials erased from NVS` then the ROM banner within about 1 s; the safety-net line `setup AP still up … stopping it` should never print; `[SCAN] ble_gap_disc rc=2` while a leak-response hunt should run in the window (a pre-existing cancel/start race between the leak scanner and the valve hunt, now also hit at the window's edges) means the hunt died, so record it; exactly one `Valve hunt for a pended leak response …` line per hunt; after `portal priority OFF`, `Scan resumed` within about 0.5 s and the valve hunt within about 1 s; a phone that drops at `Connected! IP` on a router not on channel 1 points to the SoftAP following the router's channel, not BLE, so record the router's 2.4 GHz channel next to A8/F4/D6.
- **Pre-existing, not a 2.1.4 regression: the hub may not rejoin the router after an outage.** On the 4th failed retry wifi_manager sends START_AP, and the local patch in START_AP (`6ad1d7b`, already in 2.1.3) stops the retry timer, so on the router-outage fallback AP the STA can sit idle and never reconnect when the router returns, until a power cycle, a portal submit or (since 2.1.4) the 10 s reset. It depends on a timer race, so it may or may not show. T4-03 row 1 and T4-10 D4 expect `Connected! IP` within about 20 s of the router's return: if the hub does not rejoin, record it as this known issue. Candidate fix: stop the retry timer only while no credentials are saved. **Update 2026-09-30: it showed on the bench (the 17:20 session, §13e) and is fixed app-side, without touching the component: from the fallback SoftAP the hub retries its router every 30 s (§14). T4-03 row 1 and T4-10 D4 now expect the rejoin within about 40 s of the router's return, and a missed rejoin is a FAIL.**
- **Test plan:** the T4-10 pass criterion "no `device_offline` for the valve within 180 s after `portal priority OFF`" now excepts a valve dropped in the window after a leak-response hunt that came before the resume (due at T + 180-210 s by design).

**Next: 🔨 Build checkpoint 4 (CP4)** of firmware **`46a1f0a`**, the last commit that changes `main/`; everything after it is docs. Use the §12c gates above (they supersede §12a's size line), then flash, run the smoke subset, then T4-10 Parts A-I. Re-baseline VAL-01 and EC-1 with the CP4 figures (the firmware under test is `46a1f0a`). Open for the user: §12a items 1 and 3-5 (item 2 is decided), and the router-rejoin issue above (fixed since: §14).

---

## 13. CP4 build and first bench session (2026-09-29)

### 13a. Build checkpoint 4: PASS

The user built firmware `46a1f0a` (clean build through the VS Code ESP-IDF extension, `.bin` written 14:48:06 +1000) and flashed it. No `build_cp4.log` was saved. The whole build is in `C:\Users\antun\AppData\Roaming\Code\logs\20260925T101823\window1\exthost\output_logging_20260925T122215\4-ESP-IDF.log`: the warnings at L2061-2084, `[1298/1298]` at L2102, no `error:`, `Flash Done` at L2165 (the "Build task failed" at L1327-1328 are the earlier compiler-crash runs). The boot banner's ELF SHA256 matched the build, and the version reads 2.1.4. The test plan's VAL-01 and EC-1 now record these figures.

| | CP3 (`d9fa9c8`) | CP4 (`46a1f0a`) | CP4 vs CP3 |
|---|---|---|---|
| Warnings | the four from `master` | the same four: `app_ble_valve.c:106:9` `BLE_HS_ATT_ERR` redefined; `app_lora.cpp:185:5` ×2 (`rx_flow_ctrl_thresh`, `flags`); `app_lora.cpp:160:13` `switch_sync_word` unused | none new |
| IRAM | 16,384 (100 %) | 16,384 (100 %) | 0 |
| DIRAM `.text` / `.data` / `.bss` | 113,387 / 21,572 / 36,280 | 113,387 / 21,572 / 36,304 | 0 / 0 / +24 (+0 over `cc66d72`: the "go red" word landed in alignment padding, as §12b predicted) |
| Flash `.text` / `.rodata` | 1,016,642 / 360,188 | 1,019,510 / 362,364 | +2,868 / +2,176 |
| App `.bin` | 1,528,576 | 1,533,616 (0x1766B0); 563,536 B = 26.87 % of the partition free (`idf.py` prints "27%") | +5,040 (+336 over `cc66d72`) |
| ELF SHA256 | — | `5549e78ac872a599d3b7d8c484e7e55e2003a6c629b3e12f240c75866bcf26a2` | |
| `sdkconfig` SHA256 | not recorded | `ef9757960686328ba288f9c7532a6ffc96ad3dac61e683ed7f0780e988ef98c3` (dated 2026-06-26): the reference from now on | |

Heap on the CP4 image (valve + up to 4 BLE sensors): 59,916 / 59,728 / 31,744 at boot, then 33,384 / **20,576** / 17,408 at 10 s, after TLS and MQTT (`free` / `min_ever` / `largest_blk`). `free` stayed at 33,384-33,428 whatever the device set, and `min_ever` never fell below 20,576 in 1,520 s. The lowest `largest_blk` was 8,192 at 650 s, during a snapshot with the valve and 4 BLE sensors, and it recovered (the floor is 7,168). Sensors-only (1 sensor, 60 s): 33,388 / 20,576 / 17,408. T6-07, T6-08 and VAL-13 were not run, so there is no like-for-like figure yet.

### 13b. The first bench session

One POWERON boot of hub `GW-7C4FADAE69C8`, about 1,520 s. The full validation (every test the session touched, with UART and IoT Hub line evidence) is `cp4_validation_report.md` in the session scratchpad (`C:\Users\antun\AppData\Local\Temp\claude\c--Work-Projects-EfloStop-2-Firmware-Production-eFloStop-WiFiHub-idf1\ee520eed-0dc0-45ef-bdfe-91bf0b44762d\scratchpad\`, not committed). Its line numbers refer to the Desktop captures as they were at 15:47. **Both Desktop files were overwritten at 17:20 by a later session (13e).** The CP4 captures survive only in that scratchpad: `cp4val\uart_trunc.txt` (the UART log, line-numbered) and `cp4val\cloud.json` (the 74 IoT Hub messages; `d1docs\cp4_iothub_rebuilt.txt` is a capture rebuilt from them for the validator).

- **Pass:** VAL-01 / EC-1 for CP4 and smoke step 1; the validator (74 of 74, no ORDERING block, no no-valve `auto_close` block; again 74 of 74 with the `0e7f3c3` checks); the boot lines, the B.2 grep and the E lines (only the known gpio ISR line; the one B.2 hit is the genuine valve press below); C2D §4.9 prov-003 (a category replace keeps the valve) and prov-004 (lower-case MACs: upper-case on the wire, a case-insensitive reconcile).
- **Partial, every step that ran passed:** T3-02, T4-01 (sensors-only variant), T3-01, T5-10 (three read-backs in 0.88-1.22 s), T3-15 = T5-08 (a genuine valve long-press started the 24 h window; the valve firmware sends an unsolicited RMLEAK=0 only from `processLongPress`), T5-07, T5-01, DEC-01, DEC-03, DEC-04 (the P0 removals inside the window, steps 3, 4 and 8, were not run), DEC-07 (dry re-add only), DEC-09, DEC-10, DEC-14, T6-05 / T6-06, T6-09, T4-14 / EC-10, T5-14.
- **Fail:** T5-03 step 3 (D1).
- **Not run:** T4-10 (the captive portal, the top priority: saved credentials got an IP at 2.9 s, and there was no reset press) and smoke steps 2-10; DEC-19; 91 of the plan's 114 test headings in all.

**Defects and decisions.**

| # | Finding | Decision / state |
|---|---|---|
| D1 | T5-03 step 3: after the 10 s auto-clear the only snapshot (`event:rules`, 0.35 s after `AUTO-CLEAR`) read the closed valve as `"rmleak":true`. The valve's RMLEAK=0 read-back came 0.88 s later and requested no snapshot (`// BLE_UPD_RMLEAK: handled by rules engine events`), and the settle barrier ends when the write is issued. The cloud showed the valve locked until the next heartbeat (the next snapshot came only because the valve was opened, 15.7 s later). Moderate, no safety impact; the same code is in 2.1.3. DEC-06 step 3, T5-05 (c) and VAL-09 step 3 have the same gap. | **User: fix the firmware.** `4e6fe71`: the valve's report of a new RMLEAK value requests an `event` snapshot labelled `rmleak`, with no D2C event and no new static; `f424d65`: two comments `4e6fe71` left stale. Plan, CHANGELOG and TELEMETRY_REFERENCE §9.3 updated. Needs CP5 (13c). |
| D2 | T4-10 depends on a LoRa sensor: A6, A7, B2-B6, G2, H1-H2 and I1-I5 wet L, because BLE sensors are not scanned in the window, and none was provisioned (`LoRa sensors: 0`; `LoRa Task Started` prints whether or not a radio is fitted). That includes both "go red" checks (B3, I4). EC-2 did not name T4-10, and it said the production board has no SX1262. | **User: waive them for 2.1.4.** They are recorded `Blocked (waived)`, and "go red" is covered by code review and the 5/5 portal council only (§12b, §12c). The waiver is written into T4-10 (preconditions and pass rule), M.2 and EC-2. |
| D3 | Bench: sensor `2B:A5` is dead (on the whitelist about 856 s, never heard). The firmware handled it correctly: the grace expired at 599.86 s, the LED went RED with no `device_offline`, then GREEN after its removal. | Replace it, or run with 3 sensors and record the deviation. It blocks the `SS-V4` smoke start state and T4-10 A3/A8, not Part F. |
| D4 | Tooling: `validate_capture.py` did not check the snapshot sensor-array elements (the garbled UART line passed), and its ORDERING block did not test F-08 although the plan said it did. | Fixed in `0e7f3c3` (element checks against the schema; the F-08 release order, with the MQTT-reconnect exception). |
| D5 | Plan and docs text, each checked against the code: the T4-01 sensors-only line (and §3a above); the T3-01 snapshot label and line order; DEC-09's `0x216`; the unheard grace (599-605 s); DEC-01's `valve_linked` taken by the boot snapshot; T5-03's "within 5 s"; VAL-01's `:106` and 26.87 %; the IoT Hub name (`resi-apex-iot-dev`, resource group `resi-apex-rg-dev`, not `wd-core-iothub-poc`); `C2D_COMMANDS.md` (no lifecycle follows a provision); `TELEMETRY_REFERENCE.md` (the removed provisioning gate). | Fixed in the docs commit after `0e7f3c3`. |
| D6 | Procedure: the UART log was a terminal copy without `--timestamps` (U:176, U:1027 and U:1064 are corrupted: use the IoT copies); the snapshot interval stayed at 300 s; the IoT capture came from VS Code and its first 5 messages were from the previous image; no `build_cp4.log`. | Bench notes below. |

Checked and cleared in the report: the `RMLEAK cleared externally` at about 244 s was a real valve long-press, handled as designed; `Link was not the provisioned valve - hub not notified`, `decom-b-002` accepted twice (no id de-duplication), no `device_offline` for the never-heard `2B:A5`, and the twin's null `valve_mac` / `valve_device_id` are all by design. To watch, not yet explained: a permanent 188 B step in `free` at about 1,235 s; a 201 s gap in `29:FC`'s bursts while the valve link was up; orphaned `sensor_meta` entries after sensors are replaced by a `provision` (pre-existing).

### 13c. The D1 fix and Build checkpoint 5

| SHA | What |
|---|---|
| `4e6fe71` | fix(iothub): request a snapshot when the valve reports a new RMLEAK. `app_iothub.c`, in the provisioned-valve (`vlk_mac_ok`) block: `else if (ble_upd_type == BLE_UPD_RMLEAK) snap_request(SNAP_EVENT, SNAP_TIER_HIGH, "rmleak")`. Comment corrections in `app_ble_valve.c`/`.h` and `rules_engine.c`: the cache changes only at the valve's report, and the settle barrier ends when the write is issued |
| `f424d65` | docs(iothub): two comments in `app_iothub.c` that `4e6fe71` left stale (comments only) |
| `0e7f3c3` | tools(telemetry): the validator's sensor-array element checks and F-08 order (D4) |
| the docs commit after `0e7f3c3` | Docs: MANUAL_TEST_PLAN.md (header note, 0.2, 0.5, 0.7-0.9, 0.15, 0.18, 5.0, T5-03, DEC-01, DEC-04, DEC-06, DEC-08, DEC-09, T3-01, T3-07, T4-01, T4-10, T5-05, T5-10, T5-13, VAL-01, VAL-09, VAL-15, EC-1, EC-2, M.2, A.2, B.1), CHANGELOG (*Fixed*, the serial-log label, the heap figure), `C2D_COMMANDS.md` §4.9, `TELEMETRY_REFERENCE.md` (lifecycle gate, §9.3), this section |

**Behaviour.** `notify_hub_update(BLE_UPD_RMLEAK)` posts only when the valve's report changes the cached value (`on_notify()`, never during GATT setup) and only for the provisioned valve. `snap_request()` only pulls a deadline earlier, so the request coalesces with one already pending (the rules event's own `rules` snapshot keeps its label) and is otherwise held to 5 s after the last publish. After the auto-clear the cloud now gets the release snapshot (which can still read `true`), then `SNAP trigger=event:rmleak` about 5 s later with `"rmleak":false`; when the read-back lands first there is one snapshot, already `false`. The same holds for `leak_reset`, `override_enable`, a reconnect's re-assert or clear, and an RMLEAK set on a valve already closed. No log line is new or changed; the label prints through `SNAP trigger=event:%s`. Memory: 0 B of `.bss`/`.data`, no IRAM, about 20 B of flash `.text`; the `"rmleak"` string can merge with the telemetry key.

*(Superseded by §14g: CP5 is now of `0f08d32`, with new size expectations.)* **CP5 (VAL-01 of `f424d65`).** A full rebuild (`idf.py fullclean`, then `idf.py build *> "$env:TEMP\build_cp5.log"`) and the VAL-01 steps. Expected: the same four warnings; DIRAM `.text` exactly 113,387 (any change is a newly linked IRAM function: stop and look); `.bss` 36,304 ± a few bytes; `.data` 21,572; flash `.text` about +20 B and `.rodata` +0 to +7 B over CP4; `.bin` about 1,533,632 B; `sdkconfig` SHA256 `ef97579…c3`; record the ELF SHA256 and check the boot banner's prefix.

**Found after this section was drafted (17:21):** `build\` already holds an **incremental** build from 17:00:12 of the working tree at `f424d65` (no uncommitted change under `main/`), flashed (the VS Code log L2168-2269, `Flash Done` at L2269). It recompiled the 8 files that changed or include `app_ble_valve.h` and printed only the known `app_ble_valve.c:106` warning (`app_lora.cpp` was not recompiled, so its three warnings could not print). Its figures, from the log and the ELF: flash `.text` 1,019,526 (+16), `.rodata` 362,364 (+0), DIRAM `.text` 113,387, `.bss` 36,304, `.data` 21,572, IRAM 16,384 (100 %); `.bin` 1,533,632 B (0x1766C0, 26.87 % free); ELF SHA256 `9dc3a9e9a3d849a2132f523389152897c0180cb45788ae356e6c3bb332eae05e`; `sdkconfig` unchanged. Every size matches the CP5 expectation. VAL-01 still needs the full rebuild for the warning check (steps 2-3) and the step 1 record.

### 13d. Bench notes for the next session

- **Capture:** `idf.py -p COM30 monitor --timestamps`, then Ctrl+T Ctrl+L. Start the IoT capture after flashing, with the Azure CLI on the right hub: `az iot hub monitor-events -n resi-apex-iot-dev -g resi-apex-rg-dev -d GW-7C4FADAE69C8 --content-type application/json --properties sys --timeout 0` in Git Bash (plan 0.7). Keep a copy of each session's logs before the next one overwrites them.
- **Twin snapshot interval 60 s** (plan 0.9); it stayed at 300 s in this session.
- **Sensor `2B:A5` is dead** (D3). First-heard times on this bench were 133 s (`29:FC`) and 163 s (`B6:8E`), longer than the plan's "about 100 s": allow about 170 s.
- Forget `WiFi-Hub-69C8` on the phone before a portal run, and don't touch the valve button outside T3-15.
- For every portal boot, record the heap rows (A1, A3, A4, A5, A8, A9, F2) and check that `free` is stable across repeated resets (§12c). `min_ever` below 8,192 B or `largest_blk` below 7,168 B is a finding.

### 13e. A later session on the CP5 image (logs of 17:20; the Wi-Fi half analysed in §14)

The Desktop captures were overwritten at 17:20 by a later session on the 17:00 image (boot banner `ELF file SHA256: 9dc3a9e9a…`; its compile time still reads 14:45:55 because the app descriptor was not recompiled). Valve hub with 2 BLE sensors: Wi-Fi was lost at 50 s and the router-outage fallback AP came up at 80 s (`SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`, no `portal priority ON`); two leaks were closed and auto-cleared offline (RMLEAK before CLOSE, the events buffered); a PC joined the fallback SoftAP (DHCP lease 10.10.0.3, many DNS replies); a reset press at 446 s was released early. **MONITOR `min_ever` fell from 20,992 B to 18,204 B (320 s), 10,380 B (380 s) and 1,184 B (390 s), with `largest_blk` 8,192 B at 390 s (11,776 B from 400 s).** By the T4-10 heap rule that is a finding; it is analysed in §14f item 4 (pre-existing: a laptop flooding the open fallback SoftAP). The IoT capture holds 3 messages (lifecycle and 2 snapshots) and validates 3 of 3. Its Wi-Fi half (no rejoin after the outage, and an empty network list on the fallback portal) is analysed and fixed in §14; the heap floor is open for the user (§14f item 4).

### 13f. Run next (in order)

*(Superseded by §14i.)*

0. **VAL-01 for Build checkpoint 5** (13c), then flash that build. Before more fallback-portal runs, look at 13e's heap floor.
1. **T4-10 Part F, F1-F4, about 15 min.** The hub is already sensors-only (3 BLE sensors, no valve). F1: hold the reset 10 s; expect `RESET_BTN: Wi-Fi credentials erased from NVS` before `Rebooting into AP mode...`, then `portal priority ON (no Wi-Fi credentials) - BLE scanning paused`, `prio 5 -> 8`, `BLE_LEAK: Scan paused - Wi-Fi setup portal has the radio`, no `Extended passive scan started`, LED WHITE. F2: the phone joins and gets `DHCP server assigned IP` within 5 s, and the page loads; **no lease is the 2026-09-29 failure**. F3: a wrong password keeps the window. F4: the real credentials, then `portal priority OFF (AP stopped)` 60 ± 5 s after `Connected! IP`, each sensor's `eleak … leak=0`, `Boot sync: all devices seen`, no `device_offline`, "Connected!" on the phone.
2. **Smoke step 10 / T4-10 A1-A5 and A8-A11 on a valve hub, about 15 min.** Provision the valve with `{"schema":"eflostop.cmd","ver":1,"id":"s10-valve","cmd":"provision","payload":{"valve_id":"00:80:E1:27:F7:BB"}}` (the 3 sensors stay), wait for SETUP COMPLETE and GREEN, then run it with 3 sensors and record the deviation. Also expect `Valve scan held` and WHITE in the window, no `Roll-up grace expired`, and after OFF `[PORTAL] Valve hunt resumed`, then `SETUP COMPLETE`.
3. **T4-10 Part D (router outage, D1-D6).** It also covers T4-02 row 1 and T5-12 step 1. D1 must show the fallback AP with scanning on and **no** `portal priority ON`. If D4 hits the known rejoin issue (§12c), record it and power-cycle.
4. T4-10 B1, Part E (`B6:8E` as sensor D), H3-H4, T6-14 (Part C), and T4-14 rows 1 and 4 (grep the portal capture for the site password; clear the valve bond first so that a fresh passkey pairing happens).
5. T5-03 on CP5 (D1: `SNAP trigger=event:rmleak` with `"rmleak":false` about 5 s after the release snapshot); replace `2B:A5` and run the chained `SS-V4` smoke, steps 2-11, in one capture (step 7 needs the PSU); then sections 1-6 and VAL-02…VAL-14.

---

## 14. Router rejoin and Wi-Fi radio holds (2026-09-29 and 2026-09-30)

Round 1: firmware `727e6c1`, `58b0606`, `cd6ab28`, `8fb8340` (2026-09-30 morning) and docs `cca12d6`. Round 2, the user's decisions of 2026-09-30: firmware `288db73`, `d0d07b9`, `06c0739`, `3d120b4`, `80be3e0`, `0f08d32`, and the docs commit after them. Nothing here was built or flashed yet: it all goes into CP5 (§14g).

### 14a. Bench evidence

The 17:20 capture of 2026-09-29 (`C:\Users\antun\Desktop\UART logs.txt`, §13e). The image was the 17:00 incremental build of `f424d65` (boot banner `ELF file SHA256: 9dc3a9e9a…`, §13c), whose Wi-Fi code is CP4's (`46a1f0a`). Valve hub `GW-7C4FADAE69C8` with 2 BLE sensors and saved credentials.
- `Connected! IP` at 2.8 s. At 50.6 s `APP_WIFI: WiFi Disconnected. Reason: 1` (router off), then `Reason: 201` (NO_AP_FOUND) at 60.4, 70.2 and 80.0 s: the Wi-Fi manager's 3 retries, about 10 s apart (its 5 s timer plus an attempt of about 5 s). At 80.1 s `SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`, with no `portal priority ON`, as designed.
- Leak protection kept working offline: `eleak … leak=1` at 57.1 s and 344.7 s, each latched, RMLEAK then the close, the auto-clear, and every event buffered (`OFFLINE_BUF` `ob_11` … `ob_05`, 11 in all).
- After 80 s the STA made one more attempt (220.7 s, `Reason: 201`; its cause is not in the log, since the `wifi_manager` and `http_server` tags are at WARN) and none after it up to the end of the capture (470 s), although the user says the router was back on. **The hub never rejoined.**
- A phone (`02:B4:94:E9:C3:FF`, a private address) joined the fallback SoftAP at 106.4 s and got its DHCP lease only at 123.0 s. The portal page then stayed on "Scanning for networks..." (the user's screenshot), in an office with many networks. A Windows laptop (`8C:E9:EE:29:56:5D`) joined by itself at 203.8 s and again from 311 s to 385 s, and ran connectivity probes (`msftconnecttest` DNS lookups).
- The `MONITOR` `min_ever` fall to 1,184 B (§13e) happened while that laptop was on the SoftAP. Analysed in §14f item 4.

### 14b. Root causes

1. **No rejoin (pre-existing since `6ad1d7b`, in 2.1.3 too).** In `wifi_manager.c` (read only), the lost-connection branch of `WM_EVENT_STA_DISCONNECTED` arms the one-shot 5 s `wifi_manager_retry_timer` at every disconnect, and on the 4th failure with no AP up sends `WM_ORDER_START_AP`. The LOCAL PATCH in `WM_ORDER_START_AP` (`6ad1d7b`) stops that timer. Only three things ever send `CONNECT_STA`: the retry timer, a portal submit (`wifi_manager_connect_async()`) and the boot's `LOAD_AND_RESTORE`. So on the fallback SoftAP the STA idled until a power cycle, a submit or the 10 s reset. §12c's "may or may not show" was this.
2. **Starvation (a strong hypothesis, the §12 mechanism on a smaller scale).** With the STA not connected, the continuous dual-PHY leak scan (extended passive, 1M + Coded, interval 160 / window 80 each: about 100 % of the radio) and on a valve hub the valve hunt leave a Wi-Fi scan almost no air time. Every connect attempt starts with a scan for the SSID, so an attempt can end 201 with the router back, and the page's own scans (`GET /ap.json` → `wifi_manager_scan_async()`, about every 3.8 s) returned nothing. CP5's Part D is the proof: if attempts still end 201 while a hold is logged, the cause lies elsewhere (APSTA off-channel time, or a station on the SoftAP).

### 14c. User decisions (binding)

**2026-09-29** (superseded in part on 2026-09-30, below: the retry no longer needs the SoftAP, and the pauses are capped):
- Fix it app-side only: no change to `managed_components/`.
- While the router-fallback SoftAP is up with credentials saved and the STA is not connected, retry the router every 30 s.
- While Wi-Fi is down, pause BLE scanning (the leak scanner and the valve hunt) briefly, only for each STA connect attempt and each Wi-Fi scan (the portal page's), then resume. No health hold for these short pauses: the sensor and valve timers keep running. The pended-leak-response valve-hunt exception still applies. This supersedes "the router-fallback AP keeps BLE fully on" (§12, §12a).
- The no-credential portal window is unchanged.

**2026-09-30**, on round 1's open items (the four items of round 1's §14f):
1. **Open setup page: cap the chained page-scan holds.** After 30 s of back-to-back scan holds, BLE gets a guaranteed 15 s listening window (no scan hold in it; the page's scans still run, and the list may fill more slowly), then scan holds may pause BLE again. A wet sensor's 15 s heartbeat bursts are then heard within about 45 s.
2. **Cap each connect attempt's hold at about 3 s of BLE off** (shorter than the sensor's 4 s onset burst), for the Wi-Fi manager's own attempts and the router retry, the retry's pre-pause included (shortened to 0.5 s; the valve hunt's 1 s poll may overlap the start of the attempt). The rest of the attempt runs with BLE on. Holds never run back to back, except inside the approved 30 s page chain.
3. **Retry the router whenever the STA is down with credentials saved** (an SSID in the STA config), not only while the fallback SoftAP is up. Kept: not in the no-credential window, no attempt in flight, 30 s or more since the last attempt's start or end or a disconnect, and the page-open deferral (at most 5 min), with a page-open test that the listening window cannot end early.
4. **A mistyped password on the fallback page: accept and document** (no code): power-cycle the hub, or submit the right password.

Everything else stays: the no-credential window, the health hold, the priority raise, the 10 s reset erase, "go red", the D1 RMLEAK snapshot, the leak-response valve-hunt exception, one writer per variable (wifi_manager task or wifi_task), the deadline bound (`hold_running`), and the ON/OFF lines printed by wifi_task.

### 14d. The fix (as at `0f08d32`)

**Wi-Fi radio holds** (`app_wifi.c`, `app_wifi_radio_hold_active()` in `portal_priority.h`). Tick deadlines (0 = none), never flags, each with one writer and counted only while at most `RADIO_HOLD_MAX_MS` (6 s) ahead (`hold_running()`), so each always ends:
- `s_connect_until` (wifi_manager task): a `CONNECT_STA` order holds until its `STA_DISCONNECTED` or its `GOT_IP`, at most `RADIO_HOLD_CONNECT_MS` 2.5 s; the disconnect ends a running hold at its own tick, which is kept for the gap rule. The 1 s tail of round 1 is gone.
- `s_retry_until` (wifi_task): the router retry's, from `RADIO_HOLD_RETRY_LEAD_MS` 0.5 s before its connect order, `RADIO_HOLD_RETRY_MS` 2.5 s in all; its attempt takes no hold of its own.
- `s_scan_until` (wifi_manager task): a `START_WIFI_SCAN` order holds until 4 s after its `SCAN_DONE`, at most 6 s, so the page's requests (every 3.8 s) chain.
- **Gap rule** (`RADIO_HOLD_GAP_MS` 1.5 s, `hold_near()`, `radio_hold_near()`): no new hold while another is on or ended less than 1.5 s ago, so BLE hears at least about 1 s between two. Exceptions: the page chain's own scans, and a router retry that has waited out the 5 min page deferral, which may join a running scan hold. A connect attempt that starts in a gap runs with BLE on (the accepted cost of keeping holds apart).
- **Page chain** (decision 1): a chain counts from its first scan hold (`s_chain_start`) and holds only for `RADIO_HOLD_CHAIN_MS` 30 s. `scan_hold_set()` clamps each scan hold to that limit, and a hold that reaches it sets the listening time (`s_listen_from`) there; a scan order 30-45 s into the chain sets it too (`80be3e0`: the page's request came late). For `RADIO_HOLD_LISTEN_MS` 15 s `radio_listening()` makes `app_wifi_radio_hold_active()` read false whatever deadlines run: no hold of any kind pauses BLE. From 45 s the page's next scan order starts a new chain, if no hold is near. A page closed and opened again within its chain's 45 s rejoins it. With the page's 3.8 s timer the cycle is about 45.6 s: 30 s held, about 15.6 s listening.

None is set while the STA is connected (`app_wifi_radio_hold_active()` then reads false) or in the portal window. The BLE modules are as in `727e6c1` (comments only since): they treat (window OR hold) alike at every gate, each cancelling only its own scan or connect, and the leak-response exception covers the hold. Only the window holds the health timeouts, raises the task priority, stamps the valve's "go red" hunt and prints the `[PORTAL]` and portal lines. The callbacks for `WM_ORDER_CONNECT_STA`, `WM_ORDER_START_WIFI_SCAN` and `WM_EVENT_SCAN_DONE` only store ticks and flags. wifi_task prints one line at each hold's start and end and runs every second while the STA is down.

**Router retry** (`router_retry()` on wifi_task). `router_fallback()`: the STA down, no window, an SSID in the STA config, SoftAP up or not (decision 3; `s_ap_up` is gone). It sends `wifi_manager_connect_async()` (`CONNECTION_REQUEST_USER`: a failure arms no retry timer and sends no `START_AP`, it only marks the portal status failed; a success saves the config only if it changed) when all of these hold:
- the Wi-Fi manager has made its first attempt (`s_attempt_tick` not 0; new in `06c0739`, the boot guard the SoftAP test used to give);
- no attempt is in flight, and no retry sent is still untaken (`retry_pending`, cleared when `s_attempt_tick` moves or the STA gets its IP, with no time bound, since a second queued order would reach a connecting STA);
- no attempt has started or ended, and no disconnect come, for `ROUTER_RETRY_MS` 30 s (`cd6ab28`);
- the page is not open (`page_open()`: a scan order less than `ROUTER_RETRY_PAGE_OPEN_MS` 10 s ago, stamped in `s_scan_asked` on every order, independent of the scan hold), or the deferral has lasted `ROUTER_RETRY_PAGE_MAX_MS` 5 min since the last attempt; the "retry deferred" line prints once;
- no listening time runs or starts within the retry's 2.5 s hold (`3d120b4`): it waits, at most 17.5 s, also after the 5 min cap;
- no hold is near, unless a scan hold is running (a retry past the 5 min cap then joins it).

It sets its hold, prints the `ON (router retry)` line, waits 0.5 s and checks again: if an attempt started (a portal submit) or ended meanwhile, or the STA, the window or the config changed, it sends nothing and lets its hold run out, which then covers that attempt. Then it prints the retry line and sends. The count restarts whenever `router_fallback()` goes false. With the router off a retry comes about every 33-36 s (30 s from the failed attempt's end, the 0.5 s lead, an attempt of 2-5 s, the 1 s pass).
- **A second connect while one is in flight reboots the hub:** `WM_ORDER_CONNECT_STA` calls `ESP_ERROR_CHECK(esp_wifi_set_config())` (`wifi_manager.c:1074`), which returns `ESP_ERR_WIFI_STATE` on a connecting STA (`esp_wifi.h`; `libnet80211.a` holds "sta is connecting, cannot set config"). Hence the in-flight flag (set at `CONNECT_STA`, cleared at `GOT_IP` and `STA_DISCONNECTED`), the page deferral, and `cd6ab28`: every lost-link disconnect arms the component's 5 s timer (its only start site, `wifi_manager.c:1194`) just before our `STA_DISCONNECTED` callback, which stamps the attempt tick too, so the retry waits 30 s from the last attempt's start **or end**. By then the timer has fired (its attempt counts) or `START_AP` has stopped it.
- **The idle state with no SoftAP (decision 3).** `wifi_manager.c` sets `REQUEST_STA_CONNECT_BIT` (line 1065) before it checks whether the STA is connected (1072), so a portal submit while connected (the setup SoftAP's last minute, or the 60 s after a rejoin) leaves the bit set and replaces the RAM config. After the SoftAP stops, the next link loss takes the user branch (1154-1163): no retry timer, no `START_AP`. Round 1 needed the SoftAP, so the hub stayed offline until a reboot. Now the retry runs there too, 30 s after the disconnect and then every 33-36 s, and the hub rejoins if what was submitted matches the router; if not, only a restart or the 10 s reset recovers it, since no SoftAP comes up to submit from. The user branch arms no timer, so nothing can collide with the retry there.
- `esp_wifi_disconnect()` has no error for a connecting STA, so the 10 s reset during a retry's attempt is safe: it aborts the attempt, and `reset_button.c` erases NVS itself.

| SHA | What |
|---|---|
| `727e6c1` | fix(portal): the Wi-Fi radio holds. `app_wifi.c` (the deadlines, `s_sta_connected`, the three callbacks, `radio_hold_log()`, the 1 s wifi_task pass), `portal_priority.h`, `app_ble_leak.c` (the start gate and the loop's pause and resume), `app_ble_valve.c` (`portal_holds_valve()`, `start_scan()`, `handle_valve_disc()`, `portal_priority_poll()`'s `ended_now`) |
| `58b0606` | fix(portal): the router retry (`s_retry_until`, `s_ap_up`, `s_attempt_tick`, `s_attempt_in_flight`, `router_fallback()`, `router_retry()`). Its message says "the 30 s rule never adds one beside them" (the component's retries), which was wrong for the first link loss with the SoftAP still up; `cd6ab28` fixed it in code, and history is not amended |
| `cd6ab28` | fix(portal): the retry also counts 30 s from the last disconnect (round 1 fixer's F1, the reboot path above); the post-lead re-check also drops a retry when a disconnect lands in the lead |
| `8fb8340` | fix(portal): the retry line says "configured network", not "saved" (round 1 fixer's F2: after a failed submit the RAM config holds what was typed) |
| `cca12d6` | docs: round 1 in the CHANGELOG, MANUAL_TEST_PLAN.md and this section |
| `288db73` | fix(portal): decision 2. Connect holds 2.5 s with no tail, the retry's hold 2.5 s with a 0.5 s lead, the 1.5 s gap rule (`hold_near()`, `radio_hold_near()`), `retry_pending` replaces the 13 s pending bound, `RADIO_HOLD_MAX_MS` 13 s → 6 s |
| `d0d07b9` | fix(portal): decision 1. `s_chain_start`, `s_listen_from`, `s_scan_asked`, `scan_hold_set()`, `radio_listening()`, `page_open()`; the new limit line in `radio_hold_log()` |
| `06c0739` | fix(portal): decision 3. `router_fallback()` without the SoftAP, `s_ap_up` removed, the `s_attempt_tick == 0` boot guard |
| `3d120b4` | fix(portal): the router retry waits out a listening time (round 2 fixer's F1) |
| `80be3e0` | fix(portal): a late page request starts the listening time too (round 2 fixer's F2) |
| `0f08d32` | docs(portal): comments only, in `app_wifi.c`, `portal_priority.h`, `app_ble_leak.c` and `app_ble_valve.c` (H1, H2, WSM-1): the gap case, "a few seconds, or up to 30 s while a setup page is open", the ignored submit. **The last firmware commit** |
| the docs commit after `0f08d32` | Docs: round 2 in the CHANGELOG (*Fixed*, *Safety*, the serial-log list, the known limitations, the heap figure), MANUAL_TEST_PLAN.md (a header note, 0.2, 0.15, 4.0, smoke step 1, M.2, M.4, M.7, T4-01, T4-02, T4-03, T4-10 with Part D rewritten as D1-D13, 4.x, T5-12, T6-12, VAL-01, EC-1, 9.5, Appendices A.2, B.1, B.2), this section and §13e |

**Memory.** `.bss`: at `0f08d32`, `app_wifi.c` has 7 `TickType_t` statics (`s_scan_until`, `s_connect_until`, `s_retry_until`, `s_chain_start`, `s_listen_from`, `s_attempt_tick`, `s_scan_asked`) and 2 lone `bool`s (`s_sta_connected`, `s_attempt_in_flight`), all new since CP4: 30 B, 36 B with the fill after each lone bool, as the CP4 map lays that file out in reverse declaration order (map lines 50178-50185). `s_radio` (1 B, `app_ble_valve.c`) most likely sits in the 3 B of fill after `s_paused` (map 0x3fca9725). `.dram0.bss` ends with `ALIGN(8)`, so CP5 should read 36,336 or 36,344 B (the memory specialist; round 1's "about 24 B" came before round 2's three tick statics). `.data` unchanged: every new static is zero-initialised. No heap, task, timer, queue or semaphore (every added line grepped). No `IRAM_ATTR` and no newly linked IRAM function: `xTaskGetTickCount` and `vTaskDelay` are already in IRAM, `wifi_manager_connect_async` and `wifi_manager_get_wifi_sta_config` already in flash (CP4 map), the 64-bit tick math in ROM (`__muldi3`, `__udivdi3`). wifi_task (4,096 B stack) gains a 24 B state struct and two shallow frames; `vfprintf` already ran on it. Flash: about 100 B more `.rodata` than round 1 (the one new line) and a few hundred bytes more `.text`.

**Log lines** (checked against the code at `0f08d32`). New, all `APP_WIFI`, info, none with an SSID or a credential:
- `Wi-Fi radio hold ON (%s) - BLE scanning paused`, `%s` = `connect attempt`, `router retry` or `Wi-Fi scan`: within about 1 s of the pause starting; for the router retry at the start of its 0.5 s lead, just before its retry line.
- `Wi-Fi radio hold OFF after %u s - BLE scanning resumed`: within about 1 s of the pause ending (±1 s): 1-4 s for a connect attempt or a retry, 30 s for a page's chain. Chained pauses print one pair; a retry past the 5 min cap that joins a running scan pause prints no `ON` of its own.
- `Wi-Fi radio hold: %d s limit for the setup page's scans - BLE listens %d s with no hold` (`30`, `15`; new in `d0d07b9`): right after the `OFF` of a chain that reached its limit, and also after a connect hold's `OFF` when a late page request starts the listening time (`80be3e0`); nothing prints when no hold was on.
- `router fallback: retrying the configured network (attempt %u)`: with the SoftAP up or down, never in a listening time; the count restarts whenever the fallback state ends.
- `router fallback: retry deferred - the Wi-Fi setup page is open`: once per page session in which a retry falls due, and again after each forced retry while the page stays open.

Every other format string is unchanged (checked by script by the round 2 memory specialist: 11 in `app_wifi.c`, 22 in `app_ble_leak.c`, 272 in `app_ble_valve.c`). Now window-only: `[SCAN] Valve scan held …` and the `[PORTAL]` cancel lines. More often: `Extended passive scan started (1M + Coded PHY)` and `[SCAN] Starting scan for provisioned valve …` after each hold, `WiFi Disconnected. Reason: 201` and `NET_STATUS: wifi=0 mqtt=0` per failed retry (about 6-7 lines per retry in all, about 700 lines an hour, UART only). `Scan resumed - Wi-Fi setup portal closed` prints when scanning really resumes, so later than `portal priority OFF` if a hold overlaps it. The kept `SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on` is only approximately true (a bench anchor, kept byte for byte). Every boot with saved credentials prints one `connect attempt` pair near `Connected! IP`, and its first leak scan and valve hunt come at most about 3-4 s after boot.

### 14e. Reviews, fixers and councils

**Round 1** (`727e6c1` … `8fb8340`). Three reviewers (Wi-Fi flows, build safety, BLE) raised 8 findings; the fixer confirmed all of them and rejected none:
- **F1 (both reviews, one bug): fixed in `cd6ab28`** (the reboot path in §14d).
- **F2 (Wi-Fi flows): minimum fix in `8fb8340`** (the log text and a comment). Restoring the saved credentials before each retry needs `nvs_open` (heap) and a third, unsynchronised writer of the component's STA config; the user accepted the limitation (decision 4).
- **F3 (Wi-Fi flows): the pre-existing idle state**, decided by the user (decision 3).
- **BLE-1 and F3 (build safety): the leak latency.** Confirmed; the numbers were corrected against sensor FW 1.1.0 (`807b132`, `app_leak_detection.c`): a 4.0 s burst on each leak edge, a 2.5 s burst every 15 s while wet and every 100 s while dry, 312.5-437.5 ms apart inside a burst. Decided by the user (decision 2).
- **BLE-2 and F2 (build safety): an open page kept BLE off.** Decided by the user (decision 1).

**Council 1 (on `8fb8340`): 5/5 SHIP** (RTOS, memory/build, BLE, Wi-Fi, safety), no blocking issue. Its open items became the user's four decisions (§14c). Corrections kept from it: "grep for `Retry Timer Tick!`" cannot work, because that line is `wifi_manager` INFO and the tag is at WARN (`main.c:69`), so T4-10 D1 spots the timer race from the `WiFi Disconnected` cadence; and the component logs the Wi-Fi password at INFO (`http_app.c:177` on a portal POST, `wifi_manager.c:267` when a save finds a change), which 2.1.4 keeps hidden with the `wifi_manager` and `http_server` tags at WARN (`main.c:69-70`).

**Round 2** (the decisions: `288db73`, `d0d07b9`, `06c0739`). A review raised five findings; the fixer confirmed all five, and none needed a user decision:
- **F1 (minor): fixed in `3d120b4`.** The retry never looked at the listening time: one that fell in it ran its whole connect with BLE on (the 2026-09-29 NO_AP_FOUND condition) and printed no `ON` line. It now waits, at most 17.5 s, also after the 5 min page cap (the fixer read the cap as bounding only the page-open wait).
- **F2 (nit): fixed in `80be3e0`.** A chain whose last hold ended just short of its limit, with the page's next request late, got no listening time; a scan order 30-45 s into a chain now sets it.
- **H1, H2, WSM-1 (nits): comments in `0f08d32`.** The gap case; "a few seconds, or up to 30 s while a setup page is open" (the no-health-hold reasoning now rests on 30 s against the minutes-long timeouts); the RAM config can also hold a submit the Wi-Fi manager ignored because the STA was connected. WSM-1's docs half is in the CHANGELOG known limitations and T4-10 D11.
- The fixer's disclosed rare case: a router retry already running when a hold reaches the chain limit, or when F2 sets a late listening time, has its hold cut short: at most one attempt partly with BLE on, then another retry about 30 s later.

**Council 2 (on `b2cc3c6..0f08d32`, the whole router-rejoin fix, 10 firmware commits): 5/5 SHIP** (RTOS, memory/build, BLE, Wi-Fi, safety), no blocking issue. Their main notes:
- **RTOS:** every callback runs on the wifi_manager task and only stores ticks and flags; each deadline has one writer (the wifi_manager task for scans and connects, wifi_task for the retry), all aligned 32-bit volatiles; the BLE readers are lock-free; the deadline and listening arithmetic is right at 100 Hz, every stamp forced non-zero; decision 2 is met; nothing can deadlock (`connect_async()` runs only on wifi_task and holds nothing while it waits; no wifi_manager path waits on wifi_task). Optional hardening: `router_retry()` reads `s_attempt_tick` twice (for the age, `app_wifi.c:662`, and for `retry_mark`, `:685`); a true dual-core interleave with `cb_connection_lost()` (well under 1 µs per 5 s pass, about 1e-7 per link loss; a same-core preemption cannot do it) could send a retry about 0.5 s after a lost-link disconnect, beside the component's 5 s timer; the fix is three lines (read it once). Doc nit: a page closed and opened again inside its chain's 45 s rejoins it without the gap check, so the header's "no hold within 1.5 s" is broader than the code (within decision 1's exception). Pre-existing: the timer race at `START_AP` (the timer task runs at priority 1), which the 30 s stamp keeps clear of the retry.
- **Memory/build:** CP5 `.bss` 36,336 or 36,344 B (round 1's 36,328 was stale; the ±40 B guard is restated around the new centre, §14g); `.rodata` about 362,780-362,830 B and flash `.text` a few hundred bytes above round 1's band ("moved, explained", not a failure); DIRAM `.text` exactly 113,387, `.data` 21,572; the same four warnings (no new candidate: `{ 0 }` is exempt from `-Wmissing-field-initializers`, every new static function is used, the `app_ble_valve.c:106:9` warning keeps its line); the heap analysis is §14f item 4.
- **BLE:** BLE is off at most 3.0 s per hold and on at least about 1 s between holds, so at least about 1 s (2-3 advertisements) of a 4 s onset burst falls outside any hold, although an advertisement is not caught every time (the scan splits its time between 1M and Coded, and Wi-Fi shares the radio), so an onset burst on a hold is still missed a noticeable share of the time and heard at the next wet burst. With a page open a wet sensor is heard within about 46 s. F1 and F2 can only add BLE-on time. The valve side (the leak-response exception, the window-only go-red stamp, the hunt restart on every hold edge through `ended_now` and `s_hunt_held`) is intact. A bounded corner: a post-cap retry that joins a scan hold before any hold has reached the limit can extend a chain by up to 2.5 s.
- **Wi-Fi:** the 30 s stamp cannot collide with the component's 5 s timer; retries are user requests (no timer and no `START_AP` on a failure; a success clears the user bit); the idle state is recovered when the RAM config matches the router; the no-credential window and the 10 s reset are untouched. Residuals: the 10 s page-open window (§14f item 2), a submit in a listening time (item 3), and a flapping router, where the component's endless loop takes a 2.5 s hold every 7-10 s for the whole outage (inside decision 2). The retry adds a trigger for the component's password-logging save (`wifi_manager.c:267`, hidden at WARN): redact bench captures anyway.
- **Safety:** leak protection under the approved trade-offs checks out by reading, with every exception intact (the leak-response hunt, window-only go-red and health hold, linked-valve commands, LoRa); the worst BLE-off run is about 33 s, when a post-cap retry joins a chain's end, within decision 1. The "forget" path, pre-existing but now reachable: the page's disconnect on an idle STA leaves the component's disconnect bit set with no event, the retry rejoins with the old credentials, and the next link loss erases them and opens the setup portal (very rare: the button shows only if the page saw the STA connected; documented). With a page open the rejoin can wait up to 5 min + 17.5 s + the attempt; only the cloud side waits (the lifecycle, snapshot and offline replay deliver the state afterwards).
- **Top CP5 bench risk (safety, Wi-Fi):** the 2.5 s cap may not cover an APSTA connect's scan for the router (the SoftAP's home-channel returns slow it). Three or more retries in a row ending `Reason: 201` with the router's SSID visible is a FAIL in T4-10 D4 and reopens decision 2.

### 14f. Open for the user (before a field release, not before CP5)

Round 1's four open items (an open page kept BLE off, the leak latency and the flap, the failed submit's config, the idle state) were decided on 2026-09-30 (§14c). Council 2 leaves these:

1. **Dry BLE sensors reported offline while a page stays open (RTOS, BLE; follows from decision 1's parameters).** `code.js` asks every 3.8 s (`setInterval(refreshAP, 3800)`), and a new chain starts at the first request at least 45 s after the last one began, so the cycle is about 45.6 s: 30 s held, about 15.6 s listening. A dry sensor's 2.5 s burst every 100 s moves only 100 mod 45.6 = 8.8 s per beat, so 4-6 beats in a row can fall in the held part; at a 4.0 s cadence (a slow HTTP server, a browser that rounds its timers) 7-8. The RTOS specialist's simulation (1 h runs, each advertisement heard with p = 0.5): at 3.8 s the median longest silence is about 500 s and the worst 600-1,000 s with 100 ms of jitter; at 3.9 s, 3 of 40 runs pass 600 s; at 4.0 s, 40 of 40. Past `HEALTH_BLE_LEAK_TIMEOUT_MS` (600 s) the sensor is rated CRITICAL/LINK: red roll-up, and `device_offline` once the cloud is back. A false alarm only: a wet sensor is heard within about 46 s. Now in the CHANGELOG known limitations and observed in T4-10 D8. Options: (a) accept and document (the current state); (b) a listening time of at least 102.5 s every few minutes while chains repeat; (c) a BLE-sensor health hold while chains are active.
2. **The 10 s page-open window can reboot the hub (Wi-Fi; recommended before a field release).** `page_open()` counts the page closed 10 s after its last scan order (`ROUTER_RETRY_PAGE_OPEN_MS`). A phone that puts the page in the background or locks its screen stops `code.js`'s timer, so a due retry fires about 10.5-11.5 s after the last request: about when a user who stepped away for the new router password comes back and taps Connect. That submit reaches a connecting STA, `ESP_ERROR_CHECK(esp_wifi_set_config())` (`wifi_manager.c:1074`) aborts, the hub reboots and what was typed is lost. This is the fallback page's main use (the router's password changed); round 1 had no retries on the fallback AP. The Wi-Fi specialist suggests about 60 s (one constant, no RAM; within the 5 min cap; it delays only the first retry after the page really closes, and cuts SoftAP channel moves under a phone still on the page). Not changed in this docs round; T4-10 D13 (optional) measures it.
3. **A submit in the 15 s listening time runs with BLE on (Wi-Fi).** By the 2026-09-29 mechanism it may end 201 with the right password, and the page shows "failed": about 1 in 3 submits made 30-45 s into a chain. The router retry later joins with what was typed once the page closes. It follows from reading decision 1 strictly (the listening time masks every hold, which keeps the wet-sensor guarantee). Option: let a submit take one gap-spaced 2.5 s hold inside the listening time. Documented; T4-10 D12 observes it.
4. **The heap floor on the open fallback SoftAP (memory; pre-existing, not caused by this fix).** The 17:20 capture of 2026-09-29 (§13e; its image had no radio holds and no retry): a Windows laptop joined the fallback SoftAP at 203.8 s and re-associated at 311, 368, 383, 384.7 and 385.4 s; `dns_server` answered its Teams, SharePoint and other lookups with 10.10.0.1 (TTL 0); `dns_server: UDP sendto failed: -1` once at 320.1 s and 13 times at 384.6-388.4 s (TX allocation failures); `MONITOR` `min_ever` went 20,992 → 18,204 B (320 s) → 10,380 B (380 s) → **1,184 B (390 s)**, with `largest_blk` 8,192 B at 390 s, and `free` was back to 30,716 B by 420 s. The 2.1.3 field minimum is 2,972 B. Causes: `CONFIG_DEFAULT_AP_PASSWORD` is 7 characters, so the component starts the SoftAP open and any device that once joined it can rejoin by itself; the DNS hijack answers every name; the 32 dynamic RX and 32 dynamic TX Wi-Fi buffers and lwIP's out-of-sequence queue use heap; BLE takes much of the air time. The fix shortens the exposure: the DNS hijack stops at `GOT_IP` (`dns_server_stop()`, `wifi_manager.c:1265`) and the SoftAP 60 s later, so the flood ends within about 33-40 s of the router's return (up to 5 min + 17.5 s with a page open), where on CP4 it lasted until a reboot. While the router stays down it may add small dips (a connect scan every 33-36 s, fuller scan lists with BLE held, and MQTT/TLS starting while the SoftAP is still up after a rejoin). At zero, the component's unchecked event-handler mallocs (`wifi_manager.c:585, 661, 731`) would panic and reboot the hub. Options, all outside 2.1.4's current rules: (1) a WPA2 SoftAP password of 8 characters or more (it changes the setup instructions and labels); (2) `CONFIG_DEFAULT_AP_MAX_CONNECTIONS` 4 → 1-2; (3) fewer dynamic RX buffers, or TCP out-of-sequence queuing off; (4) limit the DNS hijack in router-fallback mode; (5) accept it for 2.1.4, document it (done: CHANGELOG known limitations) and measure it on the bench (T4-10 heap rows D1, D3, D4, D7, D8, with a laptop on the SoftAP). An app-side, rule-compliant log trim, `esp_log_level_set("dns_server", ESP_LOG_WARN)` beside `main.c:69-70`, cuts UART volume, not heap.
5. **Decided and documented, no code:** a mistyped password on the fallback page (decision 4); the idle state rejoins only if the ignored submit matches the router, otherwise a restart or the 10 s reset (WSM-1); the "forget" path (§14e, safety).
6. **Residual risks, disclosed:** a submit during a retry's attempt reboots the hub (item 2; also a page left open more than 5 min); the component's timer race at `START_AP` (T4-10 D1 records it); the fixer's rare case (one attempt partly with BLE on); a flapping router keeps the component retrying every 7-10 s with a 2.5 s hold each, BLE about 25-33 % paused, for the whole outage.
7. **Optional hardening:** read `s_attempt_tick` once in `router_retry()` (council 2, RTOS); stamp `s_attempt_tick` before clearing `s_sta_connected` in `cb_connection_lost()` (council 1). Council 1's third item (no new retry until the stamp has moved off `retry_mark`) is in the code since `288db73` (`retry_pending`).

### 14g. CP5 (both fixes) and its expectations

*(Superseded by §15b: CP5 is now of `31b4c9f`, with new expectations. The `git diff` below now has the pathspec of §15b; against `0f08d32` it lists every firmware change since.)* CP5 was then of **`0f08d32`**, the last commit that changes `main/`, and covers the D1 fix too; it supersedes §13c's CP5 figures and round 1's (no build of `8fb8340` was made). In PowerShell, with the ESP-IDF 5.5.1 environment, in the project folder (the test plan's VAL-01 pins `0f08d32`):

```powershell
git log --oneline -1
git diff --stat 0f08d32 HEAD -- main components CMakeLists.txt partitions.csv sdkconfig.defaults dependencies.lock managed_components
Get-FileHash sdkconfig
idf.py fullclean
idf.py build *> "$env:TEMP\build_cp5.log" ; "exit=$LASTEXITCODE"
Select-String -Path "$env:TEMP\build_cp5.log" -Pattern 'warning:|error:' | ForEach-Object Line
idf.py size
```

Then VAL-01 steps 5-6 (the `.bin` time and size, the ELF hash, flash, the boot lines). Expected:
- the `git diff` prints nothing; `exit=0`;
- the same four warnings (`app_ble_valve.c:106:9` `BLE_HS_ATT_ERR` redefined; `app_lora.cpp:185:5` ×2; `app_lora.cpp:160:13` `switch_sync_word` unused), no `error:`;
- DIRAM `.text` exactly 113,387 B (any change is a newly linked IRAM function: stop and look); IRAM 16,384 B (100 %);
- `.bss` 36,336 or 36,344 B (CP4's 36,304 B plus the 36 B of `app_wifi.c` statics, rounded by the section's `ALIGN(8)`); below 36,320 or above 36,360 B needs a look;
- `.data` 21,572 B;
- flash `.text` about 1,020,800-1,021,700 B and `.rodata` about 362,780-362,830 B (over the 17:00 build's 1,019,526 and 362,364: round 1's code and strings plus round 2's; they move, and are recorded, not failed);
- `.bin` about 1,535,400-1,536,250 B, about 26.8 % (at least 26.7 %) of the 2 MB partition free, and newer than the `0f08d32` commit (2026-09-30 11:57:49 +1000);
- `sdkconfig` SHA256 `ef97579…c3` (unchanged); record the ELF SHA256 and check the boot banner's prefix; `HUB_IDENT: Firmware version: v2.1.4`.

### 14h. Bench checks (`MANUAL_TEST_PLAN.md` T4-10 Part D, D1-D13)

1. **Router off 2 min, then on: the rejoin** (D1, D4; also T4-02, T4-03, T5-12, T6-12, DEC-12). With the router off: 4 `WiFi Disconnected. Reason: 201` about 10 s apart, each attempt with a `Wi-Fi radio hold ON (connect attempt)` / `OFF after` 1-4 s pair, then the fallback line, then about every 33-36 s `ON (router retry)`, 0.5 s later `router fallback: retrying the configured network (attempt N)`, `OFF after` 2-4 s, `Extended passive scan started` and `Reason: 201`. With the router back: `Connected! IP` within about 40 s of its SSID reappearing, MQTT and the replay; the fallback SoftAP stops about 60 s later; no hold lines after that. A hub still offline 60 s after the SSID reappears is a FAIL, and so are three or more retries in a row ending `201` with the SSID visible (the 2.5 s cap: reopens decision 2).
2. **Router off 30 min** (D7): about 50-55 retries, no reboot, no `device_offline`, `free` flat between retries; the `OFF after` values over 10 min add up to about 7-10 % of the time, none above 4 s.
3. **The fallback page** (D3, D8): the list fills by the 3rd refresh; `OFF after 30 s`, then `Wi-Fi radio hold: 30 s limit for the setup page's scans - BLE listens 15 s with no hold`, no `ON` for about 15 s, the next `ON` 15-19 s after the limit line; `retry deferred` once; after the page closes, the retry 6-11 s later (up to 17.5 s more) with its own `ON (router retry)` line. D8 (12 min): a wet sensor heard within about 46 s, the forced retry at most 5 min 20 s after the last attempt, and any dry-sensor `device_offline` recorded (§14f item 1).
4. **A leak during an outage** (D2, D7, D9): `eleak … leak=1` within 15 s of the wetting, within 20 s when a pause falls on its first 4 s; once, wet right after an `ON (router retry)` line: expect it within about 5 s (at least about 1 s of the onset burst is outside the pause), the next wet burst (15-17 s) is a `Known-limit`, later than 20 s a FAIL. RMLEAK before CLOSE.
5. **The idle state** (D11): submit the correct credentials on `http://10.10.0.1` right after a rejoin from the fallback SoftAP, let the SoftAP stop, switch the router off: no component retry and no SoftAP, the first router retry 30-32 s after the disconnect, then every 33-36 s, and the rejoin when the router is back.
6. **The no-credential portal is unchanged** (T4-10 A3, D5-D6, Part F): no `Wi-Fi radio hold` or `router fallback` line in the window; the window lines, the health hold, the join, the lease and the success page as on CP4.
7. **Observe for §14f:** D9 (a flap: the component's 7-10 s loop, BLE about 25-33 % paused), D10 (a mistyped password: no rejoin until a restart or the right password), D12 (a submit in a listening time), D13 (optional and last: a submit right after the phone returns from the background; it can reboot the hub).
8. **Watch:** any reboot (`ESP_ERROR_CHECK failed` with `ESP_ERR_WIFI_STATE`, in the B.2 grep), above all near a portal submit on the fallback page; a panic in the Wi-Fi manager's event handler; `min_ever` and `largest_blk` with a phone or laptop on the fallback SoftAP (heap rows D1, D3, D4, D7, D8; count `dns_server: UDP sendto failed`); `[CONNECT] Failed status=` and `Scan not active (external cancel?), restarting` counts on a valve hub with the valve not linked; an unlinked valve relinking in a listening time with a page open (well before its 3 min timeout); the boot pause (T4-01 row 3: the first leak scan at most about 3-4 s after boot).

### 14i. Run next (in order; supersedes §13f)

0. **VAL-01 for Build checkpoint 5, now of `31b4c9f`** (§15b, which replaces §14g), then flash that build.
1. **T4-10 Part F, F1-F4** (sensors-only hub, about 15 min), as §13f item 1, and also **no** `Wi-Fi radio hold` line in the window.
2. **Smoke step 10 / T4-10 A1-A5 and A8-A11 on a valve hub** (about 15 min), as §13f item 2.
3. **T4-10 Part D, D1-D8 and D11** (router outage, about 110 min with D7's 30 min and D8's 12 min), then the observe steps D9, D10 and D12, and D13 last if wanted. They also cover T4-02 row 1 and T5-12 step 1.
4. T4-10 B1, Part E, H3-H4, T6-14 (Part C), and T4-14 rows 1 and 4, as §13f item 4.
5. T5-03 on CP5 (D1), then the `SS-V4` smoke steps 2-11 in one capture, sections 1-6 and VAL-02…VAL-14, as §13f item 5.
6. Take §14f items 1-4 to the user with the D3, D8, D12 and D13 results and the heap rows.

## 15. The 2.1.4 rework: WP-V and WP0 (2026-10-01), and the next build

### 15a. What changed

On 2026-10-01 the user approved the council's 2.1.4 proposal ("Approve, start with WP-V/WP0"): the recommended option on every decision except D9, so the portal's Forget/Disconnect (`DELETE /connect.json` and the page's Disconnect) stays. The user's answers: some leak sensors in the field advertise on 1M, so mixed fleets are real (D1); nothing but the setup page calls the portal's HTTP API (D10); leak protection must keep running in the reset portal (a later package). The proposal is not in the repo: it is `C:\Users\antun\AppData\Local\Temp\claude\c--Work-Projects-EfloStop-2-Firmware-Production-eFloStop-WiFiHub-idf1\ee520eed-0dc0-45ef-bdfe-91bf0b44762d\scratchpad\council\PROPOSAL_2_1_4.md` (a temp folder: copy it somewhere lasting); its packages run WP-V, WP0, WP1 … WP10, each behind a bench gate.

Firmware after `0f08d32`, none of it built yet (the build in `build\`, 2026-09-30 13:39, is of `520b17a`, and is the reference below):
- **The page round** (2026-09-30): `695283a` (the page polls its network list only while it is used), `0c9b942` (a page's chain gives BLE a 4 s window every 12 s, the user's Connect first), `9a680cf` (the portal's forget erases Wi-Fi with the STA idle), `520b17a` (its chain-end line). Not yet recorded in §14 or the test plan: the commit messages describe them. Its review's open minor issues are superseded by WP4 (C8, C9) and WP8.
- **WP-V** (D11: the Wi-Fi manager is carried in the tree): `4f14a23` moves `managed_components/ankayca__esp32-wifi-manager` to `components/wifi_portal` (byte-identical files); `b74a891` drops the registry entry from `main/idf_component.yml` and `dependencies.lock`. Nothing can silently restore the pristine 0.0.4 any more, and the component's local patches are plain tracked code.
- **WP0** (instrumentation, no behaviour change): `607df82` (C1: no Wi-Fi password in any log line, SSID and `pwd_len` only), `fbd8537` (C10a: the portal's activity hook), `818ce43` (internal-DMA heap sampled every second, failed allocations counted), `afe77c1` (each phone's portal timeline, the Wi-Fi channels, `APP_BENCH_DIAG`), `41eb044` (each leak sensor's advert burst).
- **WP-V/WP0 review fixes:** `9f2d061` (`APP_BENCH_DIAG` has no prompt, so its Kconfig default decides in every build, and a DIAG build says so at boot), `bd64e83` (burst times kept for the first 4 sensor slots: `.bss`), `9c27644` (comment: the failed-allocation hook needs the flash cache on), `31b4c9f` (comments: "portal component"). **The last firmware commit is `31b4c9f`.**

New log lines (WP0 and its fixes; formats as in the code). No existing line changed its text: C1's changed lines are in the `wifi_manager` and `http_server` tags, which `main.c` still caps at WARN.
- `MONITOR: idma: free=%lu min=%lu largest=%lu min_largest=%lu allocfail=%lu`, right after each `MONITOR: heap:` line; ` (last: %lu B, caps 0x%lx, %lu B free, %s)` is appended when the count changed.
- `APP_WIFI: Wi-Fi channel at AP start: radio %u (SoftAP configured %u), router last seen on %u` (or `…, router not joined since boot`); `APP_WIFI: Wi-Fi channel at IP: radio %u, router %u`; `APP_WIFI: Wi-Fi channel at link loss: radio %u, router was on %u` (only for a link that was up).
- `APP_WIFI: SoftAP: station %02X:…:%02X got <IP>, %lu ms after joining` (or `… got <IP> (join not seen)`).
- `APP_WIFI: portal client <IP>: first %s, %lu ms after joining` (or `… first %s (no SoftAP join seen)`), once per client and kind: `DNS query`, `captive probe (302 sent)`, `page request`, `Connect/Disconnect request`, `network list request`, `status request`.
- `BLE_LEAK: eleak <MAC> burst: n=%u in %u.%02u s, dT %u-%u ms, phy=%s` (or `… burst: n=1, phy=%s`), `phy` one of `1M`, `Coded`, `1M+Coded`, `other` and their combinations; for the sensors in the first 4 tracking slots only (the first heard).
- `W … APP_WIFI: bench build (APP_BENCH_DIAG): Wi-Fi driver log at INFO - not for release`, once at boot, and the Wi-Fi driver's own `wifi:` lines (channel switches and CSA with `csa_count`, station join and leave, the STA's connect states).

### 15b. The next build: 🔨 Build checkpoint 5, now of `31b4c9f` (the WP-V gate and G0's image)

In PowerShell, with the ESP-IDF 5.5.1 environment, in the project folder. Save the `520b17a` reference first: `fullclean` deletes `build\`.

```powershell
git log --oneline -1
git diff --stat 31b4c9f HEAD -- main components CMakeLists.txt partitions.csv sdkconfig.defaults dependencies.lock managed_components
Get-FileHash sdkconfig
New-Item -ItemType Directory -Force "$env:TEMP\ref_520b17a" | Out-Null
Copy-Item build\eFloStop_WiFiHub_idf1.map, build\eFloStop_WiFiHub_idf1.elf, sdkconfig "$env:TEMP\ref_520b17a\"
idf.py fullclean
idf.py build *> "$env:TEMP\build_cp5.log" ; "exit=$LASTEXITCODE"
Select-String -Path "$env:TEMP\build_cp5.log" -Pattern 'warning:|error:' | ForEach-Object Line
idf.py size
Compare-Object (Get-Content "$env:TEMP\ref_520b17a\sdkconfig") (Get-Content sdkconfig)
Get-FileHash sdkconfig
git status --short
```

Expected:
- **The `git diff` prints nothing.** Its pathspec now includes `components` and `dependencies.lock` (WPV-2): the old one (`main … managed_components`, in §14g and the test plan's 0.2 and VAL-01 until this round) could not see the portal component at all. The `sdkconfig` hash before the build is still `ef97579…c3`.
- **`idf.py fullclean` needs the network** (WPV-3). It deletes the three registry components left in `managed_components\` (`espressif__led_strip`, `espressif__mdns`, `jgromes__radiolib`: 451 tracked files; their hashes match, so the component manager removes them), and the build downloads them again. That was already so before WP-V: `fullclean` deleted those three, then stopped with `ComponentModifiedError` on the patched Wi-Fi manager copy (plan §6.1). With no registry access, replace `idf.py fullclean` with `Remove-Item -Recurse -Force build`: the same full rebuild, and `managed_components\` is left alone. Either way, `git status --short` afterwards lists only what it listed before (`.vscode/settings.json`, the two 2.1.3 docs, `.adsum/`), no deleted file.
- The configure prints `Processing 4 dependencies` (5 before), with no `Solving dependencies` and no change to `dependencies.lock`. A rewritten lock means the manager re-solved (then it needed the network): keep the new lock aside and report it.
- `exit=0`; the same four warnings (`app_ble_valve.c:106:9`, `app_lora.cpp:185:5` ×2, `app_lora.cpp:160:13`), no `error:`.
- **`Compare-Object` lists exactly one line, `CONFIG_APP_BENCH_DIAG=y` (`=>`)** (WPV-4): the new option, written at the first configure (no menu, no comment lines, since `9f2d061`). **Record the new `sdkconfig` SHA256: it is the reference from now on**, in place of CP4's `ef97579…c3`. It returns to `ef97579…c3` only when WP10 sets the default to n. For the 2.1.3 worktree of the test plan's 0.4, copy either file: 2.1.3's configure drops the unknown symbol, so the 2.1.3 image is the same; its hash check compares with the file copied.
- **DIRAM `.text` exactly 113,387 B**; IRAM 16,384 B (100 %). No WP0 object has an IRAM section (each compiled with the project's flags), and the new links are flash functions (`esp_wifi_get_channel`, `esp_wifi_sta_get_ap_info`, `lwip_getpeername`, `httpd_req_to_sockfd`, `heap_caps_register_failed_alloc_callback`). If it moves, stop: the link order changed (next item), so compare the IRAM input lists of the saved and the new map (the `.iram1` and `.iram0.text` input sections, not their `*fill*`) before calling it a newly linked IRAM function. Send both maps to the next session.
- **The link order changed** (WPV-1; `b74a891`'s message says "unchanged", which is wrong). The map's first-pass `LOAD` lines end `… wifi_provisioning, espressif__led_strip, espressif__mdns, jgromes__radiolib, main, wifi_portal`; at `520b17a` they ended `… wifi_provisioning, espressif__mdns, ankayca__esp32-wifi-manager, espressif__led_strip, jgromes__radiolib, main`. With the registry entry gone, the component manager no longer puts the Wi-Fi manager into `main`'s requirements (it lists them sorted, `led_strip` first), and `wifi_portal`, found in `components/`, is expanded after `main`. `main` still links it and sees its headers (no `REQUIRES` in `main/CMakeLists.txt`, so `main` depends on every component). No other library defines any of the portal's global symbols, so behaviour cannot change, and none of the reordered libraries has IRAM input; only the alignment fill between input sections moves, a few bytes in flash `.text`, `.rodata`, `.data` and `.bss`.
- `.bss` about **36,472-36,480 B**: the `520b17a` build's 36,352 B plus WP0's 121 B (15c), rounded by the section's `ALIGN(8)`; the new link order can move the fill by ±8 B. Below 36,456 or above 36,496 B needs a look.
- `.data` about **21,580 B** (21,572 + the 8 B spinlock), ±8 B.
- Flash `.text` about 1,026,300-1,027,100 B (+3.4 KB of WP0 code in the objects, about +0.9-1.2 KB of newly linked library functions, `esp_wifi_sta_get_ap_info` and `lwip_getpeername` with their helpers the largest) and `.rodata` about 368,100-368,350 B (+1.4 KB of strings, −48 B of shorter `__FILE__` paths); `.bin` about 1,546,300-1,547,800 B, about 26.2 % of the 2 MB partition free. They move with the code and the link order: record them, they are not a Fail.
- `520b17a`'s figures, for the comparison: DIRAM `.text` 113,387, `.bss` 36,352, `.data` 21,572, flash `.text` 1,022,302, `.rodata` 366,828 B; `.bin` 1,540,880 B.
- The boot shows `HUB_IDENT: Firmware version: v2.1.4`, the `bench build (APP_BENCH_DIAG)` warning after `AP SSID:`, and from then on the driver's `wifi:` lines.

A separate build of `b74a891` alone (plan §11's WP-V gate, "identical `.text` / `.bss`") is optional: it would show the same link-order change and the same few bytes of fill, DIRAM `.text` 113,387 B and an unchanged `sdkconfig`. This build covers it. The bench then runs the WP-V gate's 10 s reset ×3, and G0 (plan §12: the E4 replay, the E1 re-run, first setup and the 10 s reset) records the plan §2.4 baselines from the lines of 15a.

### 15c. WP0's memory ledger and its limits

- **Static RAM** (against `520b17a`, objects compiled with the project's flags): `.bss` **+121 B**: `http_app.c` +4 (the activity hook), `monitoring.c` +20 (the failed-allocation record), `app_wifi.c` +65 (the portal client table, 4 × 16 B, and the router's channel), `app_ble_leak.c` +32 (burst times, 4 slots × 8 B). `.data` **+8 B** (the client table's spinlock). WP0's budget was about +60-150 B; as first written it was +217 B (the burst times in all 16 slots, +128 B), and `bd64e83` cut it (WP0 review). Plan §10's per-file figures for the later packages stand; WP6's per-sensor counters (+80 B) should replace the burst times, not add to them.
- **Permanent heap** (WP0 review): registering `ap_lease_event_handler` for `IP_EVENT_AP_STAIPASSIGNED` allocates three small blocks in the default event loop (the handler node, its context and a node for that event id), about 40-60 B with the allocator's overhead, once. `free` at rest reads that much below `520b17a`'s, before any other change.
- **No task, timer or queue; no IRAM.** The monitor task now wakes every second (one heap walk for the largest internal-DMA block, core 1, priority 1).
- **`APP_BENCH_DIAG` is bench-only** (WP0 review). Its connect line, `wifi:connected with <SSID>, aid = …, channel …, bssid = <MAC>`, meets the production tool's Wi-Fi MAC rule (`functional_test.py`: "wifi" or "sta", "mac" and a colon-MAC on one line) when the router's SSID contains "mac" in any case, and the tool would then record the router's BSSID as the hub's Wi-Fi MAC. Never run a DIAG image through the production tool with Wi-Fi credentials saved (its flash erases them, so its normal flow does not reach the connect). WP10 sets the default to n; since `9f2d061` that reaches every machine, because a stored `sdkconfig` value no longer overrides an option with no prompt. Check a release candidate's boot log for the absence of the `bench build (APP_BENCH_DIAG)` warning. None of the firmware's own new lines has the word "mac" (the station and sensor lines carry a colon-MAC, but hex digits cannot spell it).
- **The failed-allocation hook** (`818ce43`, comment fixed in `9c27644`) is flash-resident while heap_caps calls it from its IRAM failure path: it relies on no allocation failing with the flash cache disabled. None does in this image; code that ever allocates in an IRAM-safe ISR must unregister it first. It stays in every build, since G3, G-M and G4b pass on the failed-allocation count.
