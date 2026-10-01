# Hub FW 2.1.4: handoff notes (end of Friday 2026-09-25)

Written for the user and for the next Claude Code session. It records where the 2.1.4 fix job stands and how to pick it up again.

> **Resume here.** Read §15 first, then §14, §13, then §12-§12c, then §1, §7, §10 and §11.
>
> **Update, Thursday 2026-10-01 (latest): WP2 is committed, reviewed and voted 5/5 SHIP; 🔨 Build checkpoint 6 now builds WP1 and WP2 together, still only after G0.** WP2 (plan §11: cloud admission and the AP lifecycle, C3, C12's API) is `dc9db75` … `30fb932`, its review fixes `e09f2ac` … `0aae305`, its docs this commit (§15i). The cloud's TLS (MQTT and DPS) now starts only once the SoftAP and its servers are down and the internal DMA-capable heap has 36 KB free with a 12 KB block; the SoftAP stops 0.5 s after an automatic rejoin with no phone on it (20 s, or 10 s after the last station leaves, with one; 15-60 s after a setup-page Connect); the web server runs only while the SoftAP is up and refuses the home LAN (403). **The last firmware commit is `0aae305`, and CP6 is of it** (§15j: `.bss` about 36,528 B, heap at rest about 8 KB above CP5's). Finish G0 on CP5 first (§15d, §15f), then build CP6 and run the gates of §15k on it: WP1's, plus WP2's G3-lite (the E4 replay), the DPS router-pull test, first commissioning and P-14's LAN check (order in §15l). For the user (§15i): whether to accept that the MQTT stop at a link loss now stalls `iothub_task` for about 1-5 s (ADM-3; the council recommends accepting and measuring it), the 12 KB outbox limit (the plan said about 4 KB), the 500 ms settle, and where G3 and G3b measure. Scratch compile checks are now allowed (user decision, §8).
>
> **Update, Thursday 2026-10-01 (afternoon): WP1 is committed, reviewed and voted 5/5 SHIP; 🔨 Build checkpoint 6 comes only after G0.** WP1 (plan §11: the setup portal can no longer reboot the hub; C2 a-h, C2b, C4, C5, and the plan updates (i)-(iii) of 2026-10-01) is `97ce041` … `703fa22`, its review fixes `b67e5bf`, `bf45701`, `5bd0762` and `9d31927` (comments only), its docs `1ef04bb` and `6225f13` (§15g, §15h). **The last firmware code commit was then `5bd0762`; the last commit that touched firmware sources was `9d31927` (comments), and CP6 was to be of it (now `0aae305`, WP1 and WP2, see the latest update above).** Nothing of WP1 is built: Build checkpoint 5 of `31b4c9f` was built at 11:07 and G0 is running on it. Finish G0 on CP5 first (§15d, §15f), then build CP6 (§15j) and run WP1's gates on it: G-FAULT (WP1 subset), G8, P-13, the 10 s reset ×10, and a portal smoke on an iPhone and an Android (§15k; order in §15l). For the user (§15h): the WP1 implementers and fixer compiled single objects into the scratchpad, against §8's no-compiler rule (nothing reached `build\`); `9583236`'s subject has 74 characters (§15g); G-FAULT's heap hold has no injection hook in this image.
>
> **Update, Thursday 2026-10-01 (morning): the 2.1.4 rework has started (WP-V, WP0); Build checkpoint 5 is next, now of `31b4c9f`.** The user approved the council's 2.1.4 proposal ("start with WP-V/WP0"; every recommended option except D9: the portal's Forget/Disconnect stays). WP-V carries the Wi-Fi manager in the tree as `components/wifi_portal` (`4f14a23`, `b74a891`); WP0 adds instrumentation with no behaviour change (`607df82`, `fbd8537`, `818ce43`, `afe77c1`, `41eb044`); their review fixes are `9f2d061`, `bd64e83`, `9c27644` and `31b4c9f`. With the page round of 2026-09-30 (`695283a` … `520b17a`), none of it is built yet. **The last firmware commit is now `31b4c9f`**, and §15b gives the build and its expectations, which differ from §14g's: the `git diff` pathspec now includes `components` and `dependencies.lock`; `idf.py fullclean` needs registry access (or delete `build\` instead); `sdkconfig` gains one line, `CONFIG_APP_BENCH_DIAG=y`, so its hash changes once; the link order changes (`wifi_portal` after `main`); `.bss` about +121 B. Then the bench: the WP-V gate's 10 s reset ×3 and G0. The plan is now in the repo, `docs/field_logs/2.1.4/RADIO_PORTAL_PLAN.md`; §15 records the approval and the user's answers, the state of each package, the G0 procedure (§15d), this image's known issues (§15e) and the run order (§15f).
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
| Captive-portal regression (2026-09-29) | fixed in `5b5d70e` … `ca4835f` (docs `b7783d0`, `ac73cc6`); follow-up `93b8629` … `cc66d72` and its docs commit (§12a); "go red" `9951bf4` … `46a1f0a` and its docs commit (§12b); Build checkpoint 4 of `46a1f0a` **passed** (§13); the D1 fix `4e6fe71`, `f424d65`; the router-rejoin fix `727e6c1` … `8fb8340` and, for the user's decisions of 2026-09-30, `288db73` … `0f08d32` (§14); the page round `695283a` … `520b17a`; the 2.1.4 rework's WP-V and WP0 with their review fixes, `4f14a23` … `31b4c9f` (§15); 🔨 Build checkpoint 5 of `31b4c9f` built (2026-10-01 11:07), the WP-V gate and G0 on it running (§15b, §15d, §15f); WP1 `97ce041` … `9d31927`, council 5/5 SHIP (§15h); WP2 `dc9db75` … `0aae305`, council 5/5 SHIP (§15i); 🔨 **Build checkpoint 6 of `0aae305` (WP1 and WP2) after G0** (§15j), then the WP1 and WP2 gates (§15k, §15l) |
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

- **Builds.** Claude never runs `idf.py`, never writes to `build\` and never flashes; the user builds and flashes at each 🔨 checkpoint. **Scratch compile checks are allowed** (user decision, 2026-10-01): an agent may compile single files with the project's own flags (each file's command from `build\compile_commands.json`) into the session scratchpad, to check that a commit compiles with no new warning and to measure object sizes. Only the scratchpad receives output (no depfile or object in `build\`, nothing in the repo); the figures are unofficial, and the build checkpoint is the authority.
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
| Approved radio/portal plan of 2026-10-01 (packages WP-V … WP10, invariants, gates, decisions D1-D14) | `docs/field_logs/2.1.4/RADIO_PORTAL_PLAN.md` (state in §15) |
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
4. **The heap floor on the open fallback SoftAP (memory; pre-existing, not caused by this fix).** The 17:20 capture of 2026-09-29 (§13e; its image had no radio holds and no retry): a Windows laptop joined the fallback SoftAP at 203.8 s and re-associated at 311, 368, 383, 384.7 and 385.4 s; `dns_server` answered its Teams, SharePoint and other lookups with 10.10.0.1 (TTL 0); `dns_server: UDP sendto failed: -1` once at 320.1 s and 13 times at 384.6-388.4 s (TX allocation failures); `MONITOR` `min_ever` went 20,992 → 18,204 B (320 s) → 10,380 B (380 s) → **1,184 B (390 s)**, with `largest_blk` 8,192 B at 390 s, and `free` was back to 30,716 B by 420 s. The 2.1.3 field minimum is 2,972 B. Causes: `CONFIG_DEFAULT_AP_PASSWORD` is 7 characters, so the component starts the SoftAP open and any device that once joined it can rejoin by itself; the DNS hijack answers every name; the 32 dynamic RX and 32 dynamic TX Wi-Fi buffers and lwIP's out-of-sequence queue use heap; BLE takes much of the air time. The fix shortens the exposure: the DNS hijack stops at `GOT_IP` (`dns_server_stop()`, `wifi_manager.c:1265`) and the SoftAP 60 s later, so the flood ends within about 33-40 s of the router's return (up to 5 min + 17.5 s with a page open), where on CP4 it lasted until a reboot. From the WP1 image on, C4 (`d300079`) keeps the hijack up until STOP_AP (plan I11), so the flood lasts until the SoftAP stops, about 60 s after the IP, with TLS starting beside it until WP2 (15g). While the router stays down it may add small dips (a connect scan every 33-36 s, fuller scan lists with BLE held, and MQTT/TLS starting while the SoftAP is still up after a rejoin). At zero, the component's unchecked event-handler mallocs (`wifi_manager.c:585, 661, 731`) would panic and reboot the hub (up to the CP5 image; WP1's C2a, `97ce041`, removed them, and C2/C4 leave no reboot on NO_MEM in the portal stack, §15h). Options, all outside 2.1.4's current rules: (1) a WPA2 SoftAP password of 8 characters or more (it changes the setup instructions and labels); (2) `CONFIG_DEFAULT_AP_MAX_CONNECTIONS` 4 → 1-2; (3) fewer dynamic RX buffers, or TCP out-of-sequence queuing off; (4) limit the DNS hijack in router-fallback mode; (5) accept it for 2.1.4, document it (done: CHANGELOG known limitations) and measure it on the bench (T4-10 heap rows D1, D3, D4, D7, D8, with a laptop on the SoftAP). An app-side, rule-compliant log trim, `esp_log_level_set("dns_server", ESP_LOG_WARN)` beside `main.c:69-70`, cuts UART volume, not heap.
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

*(Superseded by §15f.)*

0. **VAL-01 for Build checkpoint 5, now of `31b4c9f`** (§15b, which replaces §14g), then flash that build.
1. **T4-10 Part F, F1-F4** (sensors-only hub, about 15 min), as §13f item 1, and also **no** `Wi-Fi radio hold` line in the window.
2. **Smoke step 10 / T4-10 A1-A5 and A8-A11 on a valve hub** (about 15 min), as §13f item 2.
3. **T4-10 Part D, D1-D8 and D11** (router outage, about 110 min with D7's 30 min and D8's 12 min), then the observe steps D9, D10 and D12, and D13 last if wanted. They also cover T4-02 row 1 and T5-12 step 1.
4. T4-10 B1, Part E, H3-H4, T6-14 (Part C), and T4-14 rows 1 and 4, as §13f item 4.
5. T5-03 on CP5 (D1), then the `SS-V4` smoke steps 2-11 in one capture, sections 1-6 and VAL-02…VAL-14, as §13f item 5.
6. Take §14f items 1-4 to the user with the D3, D8, D12 and D13 results and the heap rows.

## 15. Approved radio/portal plan (2026-10-01), CP5, WP1, WP2 and CP6

**The approval.** On 2026-10-01 the user approved the council's 2.1.4 radio and portal proposal: "Approve, start with WP-V/WP0", with the recommended option on every decision of its §13 **except D9: Forget/Disconnect stays** (`DELETE /connect.json` and the page's Disconnect button are kept: WP4's C9 keeps the button, where the plan drops it, and C8's forget, which erases an idle STA directly, serves both). The user's answers:
- **D1: some leak sensors in the field are in 1M mode,** so mixed fleets are real. Per-sensor PHY learning (WP6) and the known-1M profiles (AP_K1M, N_MIXED) are needed, and plan §5.5 item 5 (an unknown-PHY 1M sensor in an AP mode, covered only by discovery slots and not modelled) is a real case. Benches include a 1M sensor where one is at hand.
- **D2: leak protection must keep running in the reset portal** (option (c): keep scanning under station-keyed modes). It lands in WP8, which removes the health hold in the same commit; until then the image keeps the uncapped no-credential pause (15e).
- **D10: nothing but the setup page calls the portal's HTTP API,** so WP4 may change it (cache-only `/ap.json`, `POST /scan.json`, gzip, percent-encoded intake); raw `X-Custom-*` headers keep working anyway.
- **Every other decision as recommended:** D3 httpd only while the AP is up and only on its interface; D4 cloud only after the AP stops; D5 the bounded, widened dead-valve response; D6 NimBLE connect re-attempt off, with the stale-handle check; D7 channel 11 for the no-credential AP; D8 the SERVE rung rule (provisionally SERVE-A); D11 vendor the component (done, WP-V); D12 the seven `sdkconfig.defaults` lines with compile guards; D13 Finish; D14 the memory set as the last package.

With D9 kept, plan §9's "DELETE → erase → uncapped pause" row is closed by D2 alone (the erase reopens a portal that keeps scanning), and D3 keeps the DELETE off the home LAN. Anyone in range of the open setup AP can still make the hub forget its Wi-Fi: it drops off the cloud until Wi-Fi is set up again, and leak protection keeps running, like plan §9's accepted Submit residual. Approving the plan also supersedes §14c decisions 1-2 (the page chain and the connect holds, per council D3); that code stays until WP8 replaces it with the radio policy.

**The plan** is now in the repo: `docs/field_logs/2.1.4/RADIO_PORTAL_PLAN.md`, copied unchanged from the session scratchpad (`C:\Users\antun\AppData\Local\Temp\claude\c--Work-Projects-EfloStop-2-Firmware-Production-eFloStop-WiFiHub-idf1\ee520eed-0dc0-45ef-bdfe-91bf0b44762d\scratchpad\council\PROPOSAL_2_1_4.md`) under a header that records the approval and D9. Its line numbers are at `520b17a`, and its `managed_components/ankayca__esp32-wifi-manager` paths are now `components/wifi_portal`. Twelve packages in one release, each behind a bench gate (plan §11, §12):

| WP | Content (plan §11) | Gate (plan §12) | State |
|---|---|---|---|
| **WP-V** | The Wi-Fi manager moved to `components/wifi_portal`; manifest and lock (D11) | identical `.text` / `.bss`; 10 s reset ×3 | **Done:** `4f14a23`, `b74a891`; its review fixes are docs and comments (`31b4c9f`, `8f8f399`). Gate: CP5 (15b), then the 10 s reset ×3 (15f) |
| **WP0** | Instrumentation, no behaviour change: failed-allocation count, internal-DMA sampler, `AP_STAIPASSIGNED`, advert-burst statistics, the C10a hook, C1, the channel lines, `APP_BENCH_DIAG` | **G0** | **Done:** `607df82`, `fbd8537`, `818ce43`, `afe77c1`, `41eb044`; review fixes `9f2d061`, `bd64e83`, `9c27644`. Gate: G0 (15d) |
| **WP1** | The portal cannot reboot the hub: C2 (a-h), C2b, C4, C5; plan updates (i)-(iii) of 2026-10-01 | G-FAULT (WP1 subset), G8, P-13, 10 s reset ×10 | **Done:** `97ce041`, `488c13d`, `541eaa4`, `87c3178`, `6c69ac3`, `9583236`, `c8b8c7d`, `4d67b79`, `d300079`; updates `300e2bc`, `56c2c4d`, `703fa22`; review fixes `b67e5bf`, `bf45701`, `5bd0762`, `9d31927` (comments). Council 5/5 SHIP (15h). Gate: CP6 (15j, with WP2), then 15k |
| **WP2** | Cloud admission and AP lifecycle (flag-only callbacks, admission, DPS abort hook, outbox limit, lifecycle republish, the AP-tail policy); C3; C12 (API only) | G3-lite (E4 replay), DPS router-pull test, first commissioning, P-14 LAN part | **Done:** `dc9db75`, `20346a6`, `69e7bc3`, `45a2cdf`, `c9cdc4a`, `33f8208` (cloud side); `611b8d3`, `0939a4d`, `1a5ca5c`, `62983bb`, `30fb932` (AP lifecycle, C3, C12's API); review fixes `e09f2ac`, `5ea0ab4`, `86bee44`, `663230e`, `7152145`, `0e135ad`, `ba9fb7c`, `a45fc1f`, `8cc3eea`, `a139be5`, `0aae305`. Council 5/5 SHIP (15i). Gate: CP6 (15j, with WP1), then 15k |
| WP3 | The seven `sdkconfig.defaults` lines and their compile guards (plan §4.9, D12); the valve stale-handle check and `REATTEMPT_COUNT` handling | TLS / DPS / C2D / snapshot regression; heap table; G6b | pending |
| WP4 | Portal intake and phone UX: C6, C7, C8, C9, C10b, C12 (Finish), C13, channel 11; plan §6.4's WP4 deletions; the `reset_button.c` forget comment. **D9: Forget/Disconnect kept** | G8x, G-CNA in S1 (fixes the httpd socket cap), P-7, G3 (E2) | pending |
| *Tag* | Development checkpoint after WP4, **never shipped as is**: it still has the uncapped no-credential pause | – | – |
| WP5 | Single BLE scan executor, behaviour-equivalent | VAL-01, P11, P14; 100 valve power cycles | pending |
| WP6 | NORMAL de-lock (N_CODED / N_MIXED, 1 s dither), valve claim policy, persisted PHY bits (D1), the widened LR trigger with its 10 min cap | G2 NORMAL; G4 NORMAL 24 h | pending |
| WP7 | Lab image: the SERVE ladder (`APP_RADIO_LAB`), C11 | G1 (fixes the SERVE rung) | pending |
| WP8 | `radio_policy` in production (D2); the holds, the page chain and the health hold deleted; the model re-run with G0's Ta and p_loss first | G2, G3, G3b, G4, G5, G6, G7, G-CNA S2 / S3, regression | pending |
| WP9 | Memory set, one line per build | G-M; G4b | pending |
| WP10 | Documents: CHANGELOG, this handoff, `MANUAL_TEST_PLAN.md` (T4-10 rewritten; G-CNA, G-FAULT, G8x, G6b, G3b), the component change register, the FW 1.1.0 timing assumptions; `APP_BENCH_DIAG` default n | – | pending |

### 15a. What changed

Firmware after `0f08d32`, none of it built yet (the build in `build\`, 2026-09-30 13:39, is of `520b17a`, and is the reference below):
- **The page round** (2026-09-30): `695283a` (the page polls its network list only while it is used), `0c9b942` (a page's chain gives BLE a 4 s window every 12 s, the user's Connect first), `9a680cf` (the portal's forget erases Wi-Fi with the STA idle), `520b17a` (its chain-end line). Not yet recorded in §14 or the test plan: the commit messages describe them. Its review's open minor issues (15e) are superseded by WP4 (C8, C9) and WP8.
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

As §7's build checkpoints: `idf.py fullclean`, the build into `build_cp5.log`, `idf.py size`. In PowerShell, with the ESP-IDF 5.5.1 environment, in the project folder, with registry access (below). Save the `520b17a` reference first: `fullclean` deletes `build\`. It is a bench image: `APP_BENCH_DIAG=y` is the default during development (15c), and G0 needs the driver lines it enables; WP10 turns it off for the release.

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
- **The component manager and `dependencies.lock`.** The plan (§6.1) expected the lock to be re-solved once, with the network, at this build. WP-V did that step offline instead: `b74a891` removed the `ankayca/esp32-wifi-manager` entry by hand and set `manifest_hash` (`af51fade…01f5`) with the installed manager's own code, which reproduces `520b17a`'s hash from its manifest, and the manager's own check, run offline, found nothing to solve. So the configure should print `Processing 4 dependencies:` (5 before) for `espressif/led_strip` 3.0.2, `espressif/mdns` 1.9.1, `idf` 5.5.1 and `jgromes/radiolib` 7.5.0, with no `Manifest files have changed, solving dependencies` and no `Updating lock file`, and leave `dependencies.lock` unchanged. If it does re-solve (that needs the network too), it is not a failure while those four versions stay the same: show `git diff dependencies.lock`, keep the new lock aside uncommitted, and report it. `-- Components:` lists `wifi_portal` and no `ankayca__esp32-wifi-manager`; the portal's objects build under `build\esp-idf\wifi_portal`.
- `exit=0`; the same four warnings (`app_ble_valve.c:106:9`, `app_lora.cpp:185:5` ×2, `app_lora.cpp:160:13`), no `error:`.
- **`Compare-Object` lists exactly one line, `CONFIG_APP_BENCH_DIAG=y` (`=>`)** (WPV-4): the new option, written at the first configure (no menu, no comment lines, since `9f2d061`). **Record the new `sdkconfig` SHA256: it is the reference from now on**, in place of CP4's `ef97579…c3`. It returns to `ef97579…c3` only when WP10 sets the default to n. For the 2.1.3 worktree of the test plan's 0.4, copy either file: 2.1.3's configure drops the unknown symbol, so the 2.1.3 image is the same; its hash check compares with the file copied.
- **DIRAM `.text` exactly 113,387 B**; IRAM 16,384 B (100 %). No WP0 object has an IRAM section (each compiled with the project's flags), and the new links are flash functions (`esp_wifi_get_channel`, `esp_wifi_sta_get_ap_info`, `lwip_getpeername`, `httpd_req_to_sockfd`, `heap_caps_register_failed_alloc_callback`). If it moves, stop: the link order changed (next item), so compare the IRAM input lists of the saved and the new map (the `.iram1` and `.iram0.text` input sections, not their `*fill*`) before calling it a newly linked IRAM function. Send both maps to the next session.
- **The link order changed** (WPV-1; `b74a891`'s message says "unchanged", which is wrong). The map's first-pass `LOAD` lines end `… wifi_provisioning, espressif__led_strip, espressif__mdns, jgromes__radiolib, main, wifi_portal`; at `520b17a` they ended `… wifi_provisioning, espressif__mdns, ankayca__esp32-wifi-manager, espressif__led_strip, jgromes__radiolib, main`. With the registry entry gone, the component manager no longer puts the Wi-Fi manager into `main`'s requirements (it lists them sorted, `led_strip` first), and `wifi_portal`, found in `components/`, is expanded after `main`. `main` still links it and sees its headers (no `REQUIRES` in `main/CMakeLists.txt`, so `main` depends on every component). No other library defines any of the portal's global symbols, so behaviour cannot change, and none of the reordered libraries has IRAM input; only the alignment fill between input sections moves, a few bytes in flash `.text`, `.rodata`, `.data` and `.bss`.
- `.bss` about **36,472-36,480 B**: the `520b17a` build's 36,352 B plus WP0's 121 B of statics (15c) = 36,473 B, rounded by the section's `ALIGN(8)`; the new link order can move the fill by ±8 B. Below 36,456 or above 36,496 B needs a look.
- `.data` about **21,580 B** (21,572 + the 8 B spinlock), ±8 B.
- **Flash, against `520b17a`:** `.text` about 1,026,300-1,027,100 B, +4.0-4.8 KB (+3.4 KB of WP0 code in the objects, about +0.9-1.2 KB of newly linked library functions, `esp_wifi_sta_get_ap_info` and `lwip_getpeername` with their helpers the largest) and `.rodata` about 368,100-368,350 B, +1.3-1.5 KB (+1.4 KB of strings, −48 B of shorter `__FILE__` paths); `.bin` about 1,546,300-1,547,800 B, +5.4-6.9 KB, about 26.2 % of the 2 MB partition free. They move with the code and the link order: record them, they are not a Fail.
- `520b17a`'s figures, for the comparison: DIRAM `.text` 113,387, `.bss` 36,352, `.data` 21,572, flash `.text` 1,022,302, `.rodata` 366,828 B; `.bin` 1,540,880 B. That build ran at 13:38-13:39 while `520b17a` was being committed, and its `app_wifi.c` object predates that commit's one format-string change (the page-round review), so a true `520b17a` build differs by a few tens of bytes of `.rodata` at most.
- The boot shows `HUB_IDENT: Firmware version: v2.1.4`, the `bench build (APP_BENCH_DIAG)` warning after `AP SSID:`, and from then on the driver's `wifi:` lines.

A separate build of `b74a891` alone (plan §11's WP-V gate, "identical `.text` / `.bss`") is optional: it would show the same link-order change and the same few bytes of fill, DIRAM `.text` 113,387 B and an unchanged `sdkconfig`. This build covers it. The bench then runs the WP-V gate's 10 s reset ×3, and G0 (plan §12: the E4 replay, the E1 re-run, first setup and the 10 s reset) records the plan §2.4 baselines from the lines of 15a: the procedure is 15d, the order 15f.

### 15c. WP0's memory ledger and its limits

- **Static RAM** (against `520b17a`, objects compiled with the project's flags): `.bss` **+121 B**: `http_app.c` +4 (the activity hook), `monitoring.c` +20 (the failed-allocation record), `app_wifi.c` +65 (the portal client table, 4 × 16 B, and the router's channel), `app_ble_leak.c` +32 (burst times, 4 slots × 8 B). `.data` **+8 B** (the client table's spinlock). WP0's budget was about +60-150 B; as first written it was +217 B (the burst times in all 16 slots, +128 B), and `bd64e83` cut it (WP0 review). Plan §10's per-file figures for the later packages stand; WP6's per-sensor counters (+80 B) should replace the burst times, not add to them.
- **Permanent heap** (WP0 review): registering `ap_lease_event_handler` for `IP_EVENT_AP_STAIPASSIGNED` allocates three small blocks in the default event loop (the handler node, its context and a node for that event id), about 40-60 B with the allocator's overhead, once. `free` at rest reads that much below `520b17a`'s, before any other change.
- **No task, timer or queue; no IRAM.** The monitor task now wakes every second (one heap walk for the largest internal-DMA block, core 1, priority 1).
- **`APP_BENCH_DIAG` is bench-only** (WP0 review). Its connect line, `wifi:connected with <SSID>, aid = …, channel …, bssid = <MAC>`, meets the production tool's Wi-Fi MAC rule (`functional_test.py`: "wifi" or "sta", "mac" and a colon-MAC on one line) when the router's SSID contains "mac" in any case, and the tool would then record the router's BSSID as the hub's Wi-Fi MAC. Never run a DIAG image through the production tool with Wi-Fi credentials saved (its flash erases them, so its normal flow does not reach the connect). WP10 sets the default to n; since `9f2d061` that reaches every machine, because a stored `sdkconfig` value no longer overrides an option with no prompt. Check a release candidate's boot log for the absence of the `bench build (APP_BENCH_DIAG)` warning. None of the firmware's own new lines has the word "mac" (the station and sensor lines carry a colon-MAC, but hex digits cannot spell it).
- **The failed-allocation hook** (`818ce43`, comment fixed in `9c27644`) is flash-resident while heap_caps calls it from its IRAM failure path: it relies on no allocation failing with the flash cache disabled. None does in this image; code that ever allocates in an IRAM-safe ISR must unregister it first. It stays in every build, since G3, G-M and G4b pass on the failed-allocation count.

### 15d. G0 on the CP5 image: the bench procedure

G0 (plan §12) records the plan §2.4 baselines before any radio geometry is fixed. WP0 changed no behaviour, so G0 measures the branch as it is: the page round's holds on the fallback portal and the uncapped pause in the reset portal (15e). It has no pass figure of its own: a reboot, a panic or a failed allocation is a finding, and the figures feed the leak-model re-run before WP8 (plan §5.5 item 2) and are the baseline for G1 and G-CNA.

**Set-up**
- Hub `GW-7C4FADAE69C8` on COM30 with the CP5 build (`idf.py -p COM30 flash`, never `erase-flash`), provisioning kept. Its boot shows the `bench build (APP_BENCH_DIAG)` warning (15b). Never run this image through the production tool with Wi-Fi credentials saved (15c).
- BLE leak sensors: 3-4 that work (`2B:A5` is dead, §13b D3). Only the first 4 heard get burst lines. If a sensor in 1M mode is at hand (D1), include it: its lines read `phy=1M`.
- The valve: every run first with the valve **unpowered** (provisioned, batteries out: the E3/E4 condition), then again with the valve **linked** (plan §12).
- The router on channel 6 or 11, not 1, with its channel noted: the setup SoftAP is configured on channel 1 (`CONFIG_DEFAULT_AP_CHANNEL`), so a Submit moves it and the driver logs the switch.
- Phones: at least one iPhone and one Android (Pixel or Samsung), mobile data on (plan §7.5). Forget `WiFi-Hub-69C8` on each before every run. Keep the Windows laptop that remembers the SSID switched off (that is E2, for G3); if a laptop joins anyway, note it.
- Twin snapshot interval 60 s (test plan 0.9).
- UART: `idf.py -p COM30 monitor --timestamps`, then Ctrl+T Ctrl+L to log to a file. One file per run, copied aside before the next (§13b: the Desktop captures get overwritten).
- IoT Hub, from the flash on, in Git Bash: `az iot hub monitor-events -n resi-apex-iot-dev -g resi-apex-rg-dev -d GW-7C4FADAE69C8 --content-type application/json --properties sys --timeout 0` (test plan 0.7), saved per run.
- A note per phone join: the wall-clock time of the tap on the SSID, whether the sign-in window opened by itself and when, what the page showed, and the phone's model and OS version.

**Runs** (each with the valve unpowered, then linked)
- **A. Router outage with a phone (the E4 replay; its first part re-runs E1).**
  1. Hub connected, sensors dry, 5 min: the connected (NORMAL) burst lines.
  2. Router off. Expect `WiFi Disconnected. Reason: …` with `Wi-Fi channel at link loss: …`, the Wi-Fi manager's 3 retries (`Reason: 201`), then about 30 s after the loss `SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on` and `Wi-Fi channel at AP start: …`.
  3. A phone joins `WiFi-Hub-69C8` from Settings and is left 5 min, the page untouched unless it opens by itself (E1: does a lease come, and when).
  4. Use the page for 2-3 min (scroll, let the list refresh, no Submit). Run D's fallback wetting goes here.
  5. Router on, the phone still joined. Stop touching the page: it stops polling 60 s after the last touch, and the router retry waits while it polls (at most 5 min). Expect `router fallback: retrying the configured network (attempt N)`, `Connected! IP` and `Wi-Fi channel at IP: …`, the cloud back (the first IoT Hub message), and the SoftAP stop about 60 s after the IP with the phone's `SoftAP: station … left`.
  6. 5 min connected.
- **B. The 10 s reset, then a phone (plan state S2; the WP-V gate's 10 s reset, 3 times in all).**
  1. Hub connected. Hold the reset 10 s: the reset's lines (test plan T4-10 F1), `Rebooting into AP mode...`, then after the reboot `portal priority ON (no Wi-Fi credentials) - BLE scanning paused` and `Wi-Fi channel at AP start: radio 1 (SoftAP configured 1), router not joined since boot`.
  2. A phone joins from Settings: the sign-in window, the page, the router chosen, its password, Connect.
  3. Expect `portal client …: first Connect/Disconnect request, …`, `Connected! IP`, `Wi-Fi channel at IP: radio …, router …`, the driver's channel-switch lines, among them `wifi:<connect>csa, newchan=…, old=…, csa_count:…`, and note whether the phone's `SoftAP: station … left` follows. The phone shows the success page; `portal priority OFF (AP stopped)` comes about 60 s after the IP.
  4. Twice more: the other phone, then with the valve linked.
- **C. First setup (only with a spare hub that has no devices provisioned; plan state S1, BLE idle; no valve).** The CP5 build on it with no Wi-Fi credentials saved (a 10 s reset first if needed), then as B steps 2-3. Nothing scans BLE there, so it is the radio-free baseline for B.
- **D. A wet sensor (burst statistics).** Wet one BLE sensor for about 1 min, then dry it: once connected (run A step 1 or 6) and once on the fallback SoftAP with the page in use (run A step 4). Note the wetting and drying times. Expect `eleak … leak=1`, the edge burst's line (about 4 s), one line per wet burst every 15 s, then dry bursts about every 100 s; with the valve linked, RMLEAK then CLOSE as usual.

**What to record** (plan §2.4):

| Quantity | From | Runs |
|---|---|---|
| Ta per sensor | the smallest `dT` over that sensor's `burst:` lines (its advert interval plus 0-10 ms). A sensor at about 404-412 ms sits in the router-beacon band (plan §5.5 item 3) | all |
| Adverts heard per burst, per mode | `n` of each burst line, by state (connected; fallback SoftAP with no phone, with a phone, with the page in use; the AP tail after a rejoin) and by burst (4 s edge, 2.5 s wet, 2.5 s dry). With Ta it gives p_loss | A, D |
| Tap → join, per phone | the tap time against `SoftAP: station … joined` | A, B, C |
| Join → lease | `SoftAP: station … got …, N ms after joining` | A, B, C |
| Lease → DNS → 302 → page | `portal client …: first DNS query / captive probe (302 sent) / page request / network list request / status request, N ms after joining` (each counted from the join: subtract the lease) | A, B, C |
| Sign-in window | the phone note: opened by itself or not, and when | A, B, C |
| The AP-start heap dip | the first `idma:` line after `Wi-Fi channel at AP start` (`min`, `min_largest`) and the `heap:` lines' `min_ever` (E4: 21,288 → 3,344 B) | A, B |
| CSA | `wifi:<connect>csa, … csa_count:N` at the Submit's connect, and whether the phone left | B, C |
| The fallback SoftAP's channel | `Wi-Fi channel at AP start: radio R (SoftAP configured 1), router last seen on C`: R = C means it follows the router (plan §4.5, D7) | A |
| AP and STA channels | the three channel lines | all |
| Internal-DMA minima | each `idma:` line's `min` and `min_largest` (the lowest of the 1 s samples since the line before), above all in the AP tail after a rejoin, when TLS starts beside the SoftAP (E4: 152 B and 4 failed allocations) | all |
| Failed allocations | `allocfail=` (counted from boot) and its `(last: …)` details | all |

Report at once: any reboot (`rst:`, `Guru Meditation`, `ESP_ERROR_CHECK failed`), with the 30 s before it.

**Send back:** each run's UART file and IoT capture, the phone notes, `build_cp5.log`, the `idf.py size` output, both maps (15b) and the new `sdkconfig` hash. Claude then fills the §2.4 table, re-runs the leak model with the measured Ta and p_loss, and checks the constants G0 re-derives (the LIST scan's internal-DMA floor of 24 KB, plan §4.4; cloud admission at 36 / 12 KB, plan §4.6) and the CSA count for C13 (3 or 5).

### 15e. Known issues of this image (G0 measures them; WP1 and later fix them)

The CP5 image is a development build, never for the field. These are known; G0 records them and does not fail on them.
1. **The uncapped no-credential pause.** In a portal with no Wi-Fi credentials saved (after the 10 s reset) on a hub with BLE devices, BLE scanning stays paused until Wi-Fi is set up, with no time cap, and the BLE sensors' health timeouts are held: BLE leak detection is 0 % for as long as the portal is up. LoRa sensors, a linked valve and the hunt for a pended leak close keep working. This is why the checkpoint after WP4 is never shipped as is. D2 fixes it in WP8; G0 run B measures the phone side.
2. **The page round's open review findings** (`695283a` … `520b17a`; WP4's C8 and C9 replace this code):
   - **The Connect guard.** The page's 8 s wait before a Connect (`code.js` `performConnect()`) is computed once. A page hidden during the wait (screen locked, app switched) posts the moment it is back, and a second Connect started during the wait (after a stale "Connection Failed" from an earlier status poll) posts at the same instant as the first. If a router retry is connecting then, `ESP_ERROR_CHECK(esp_wifi_set_config())` (`wifi_manager.c:1074`) reboots the hub, and what was typed is lost. On the bench: keep the page in front while it shows "Connecting…", and tap Connect once.
   - **The list re-order.** The tap that wakes a page idle for 60 s fetches the list at once, and its reply re-renders the re-sorted list within that tap, so the tap can open a neighbouring network (touch) or do nothing (mouse). Check the network name on the password view.
   - Also open: an open (no-password) network gets HTTP 400 and an endless spinner (pre-existing; C8, C9); a Forget that meets a connect attempt which then succeeds leaves the Wi-Fi manager's forget bit set, and the next link loss erases the credentials (`9a680cf`; C8). §14 and T4-10 Part D (D3, D8, D12) still describe the page chain before `0c9b942` (30 s held, then 15 s listening, and a limit line that is gone).
3. **The fallback-portal starvation.** On the router-outage fallback SoftAP (credentials saved, the STA idle) BLE keeps scanning, and with the STA idle coexistence gives Wi-Fi no slot of its own: a phone joins but gets its lease late or never (E1: none in 500 s; E4: 95.2 s; 2026-09-29: 17 s). The page round's holds pause BLE only once the page asks, so the join, the lease, the captive probe and the first page load still compete with the scan. G0 run A measures it; WP8's radio policy (AP_IDLE windows, the join assist) fixes it.
4. **Also in this image:** TLS starts beside the SoftAP after a rejoin (E4: internal heap down to 152 B, 4 failed allocations; WP2); the DNS hijack stops at the IP, its replies to EDNS0 queries are malformed, and a UDP datagram shorter than 12 B to port 53 reboots the hub (WP1, C4), so no `dig` tests and no fuzzing on this image (G-FAULT and P-13 come after WP1).

### 15f. Run next (in order; supersedes §14i)

*(Still the order on the CP5 image. Once G0 is done, §15l continues it with CP6 and the WP1 and WP2 gates.)*

0. **🔨 Build checkpoint 5 of `31b4c9f`** (15b), VAL-01 steps 5-6, then flash.
1. **The WP-V gate:** the 10 s reset ×3. G0 run B's three resets are it: each ends in `Connected! IP` and `portal priority OFF (AP stopped)`, with no reboot but the reset's own.
2. **G0** (15d): runs A, B and D, then C if a spare hub is free; each with the valve unpowered, then linked.
3. If time allows, regression on this image: T5-03 (the D1 RMLEAK snapshot fix, never benched, §13c) and the smoke subset (test plan section S) without step 10. T4-10 is not re-baselined for the page round and will be rewritten in WP10; on this image G0's runs A and B stand in for its Parts D and F.
4. Send the material (15d). Then WP1 (plan §11).

### 15g. WP1 review fixes: notes for the WP1 image's bench and for WP10 (2026-10-01)

WP1 (plan §11: C2 a-h, C2b, C4, C5, and the 2026-10-01 plan updates (i)-(iii)) is `97ce041` … `703fa22`. Its review fixes are `b67e5bf` (the network list is allocated only with room to spare, its warning once per AP start, after the servers), `bf45701` (the AP's HTTP and DNS servers are started again every 5 s while either is down with the AP up), `5bd0762` (a forget whose `esp_wifi_disconnect()` fails on a connected STA is dropped, nothing erased) and `9d31927` (comments). None of it is built yet.

**Known issue of the WP1 image (until WP2).** C4 keeps the captive DNS up from START_AP to STOP_AP, so after a rejoin the DNS task (3,072 B stack plus TCB, about 3.4 KB of internal DMA-capable heap) and its answers to the phones' probes, which then open httpd sessions, last through the 60 s AP tail, while MQTT/DPS TLS still starts beside the SoftAP. WP2's cloud admission (I4) removes that overlap; plan §10 accepts the DNS task in the tail only with it in place. On this image, an idma `allocfail` or a `min_ever` drop in the AP tail after a rejoin is therefore expected (CP5's E4: 152 B and 4 failed allocations): record it and put it down to TLS beside the AP, not to C2 or C4. A reboot there is a WP1 finding. The E2 mitigation of §14f item 4 (the hijack stopped at `GOT_IP`) is gone with it; the E2 bounds the plan relies on in the tail are WP3's 16/16 buffers and WP4's C6. *Since CP6 builds WP1 and WP2 together (15j), no bench image has this overlap: in the CP6 image a failed allocation in the tail is a finding (15k).*

**Additions to the WP1 bench (G8, the 10 s reset ×10, G-FAULT, P-13).**
- G8's rejoin with a phone on the AP, and once with the E2 laptop on the AP and the router back: record the idma `min`, `min_ever` and `allocfail` over the 60 s after `Connected! IP`.
- T4-10 on this image: A4, D3 and F2 expect `dns_server: Replying to DNS request for …`, which is now DEBUG and compiled out. The evidence is `APP_WIFI: portal client <IP>: first DNS query, N ms after joining`, once per client. E2 sees the heartbeat's paused form, `BLE_LEAK: [HEARTBEAT] Scanner alive, whitelist=N sensors, scanning paused for N s (Wi-Fi setup portal)`: a pass.
- `Wi-Fi setup page: Connect sent - …` beside wifi_manager's `ORDER_CONNECT_STA: esp_wifi_set_config failed (…) - attempt not started` (a Submit that met a running attempt) means that no hold took effect and that Submit ran no attempt; the page shows the failure.
- New lines from the review fixes, all W (tag `wifi_manager`): `AP up without its %s - tried again every %d s` (`HTTP server`, `DNS server` or `HTTP and DNS servers`), `AP servers running again (HTTP and DNS)`, and `ORDER_DISCONNECT_STA: esp_wifi_disconnect failed (%s) - still connected, nothing erased`. The E line `dns_server: captive DNS: task not created (no memory)` lost its "- no DNS until the next AP start". `network list: no memory for its %u B - the page lists no network yet` prints at most once per AP start. In G-FAULT's < 1 KB hold, `AP up without its …` followed by `AP servers running again` is a pass; the first without the second by the end of the run is a finding (the portal stayed degraded).

**WP10 follow-up register** (documents that still show pre-WP1 lines):
- `MANUAL_TEST_PLAN.md` T4-10 A4, D3 and F2 (the DNS line above) and E2 (the paused heartbeat form).
- `CHANGELOG.md`'s WP0 entry: the idma line now ends ` min_ever=%lu` (after the `(last: …)` part when there is one); the three channel lines can end `, Wi-Fi scan in flight`; the heartbeat's paused form is new. *Recorded since in the CHANGELOG's WP1 entry (with §15h's docs commit); WP10 folds both entries into the release sections.*
- Removed by C4: E `dns_server: Failed to create socket` and E `dns_server: Failed to bind to 53/udp` (each followed by `exit()`), and I `dns_server: Replying to DNS request for %s from %s` (now DEBUG). They are replaced by W `captive DNS: %s failed (errno %d) - trying again every %d ms` and the activity hook's first-DNS line.
- `reset_button.c`: the `execute_wifi_reset()` comment block in WP4 (forget) and WP8 (window), as plan §6.4 says. `erase_wifi_credentials()`'s comment is updated (`9d31927`).

**For the user's decision: one commit subject over 72 characters.** `9583236`'s subject, "fix(portal): log the remaining runtime ESP_ERROR_CHECKs, check boot allocs", has 74 characters. Every other WP1 subject has 72 or fewer. Rewording it means rewriting history, which the agents do not do. Before any push, either reword it (for example "fix(portal): log runtime ESP_ERROR_CHECKs, check boot allocations", 65 characters) or accept it as it is.

### 15h. WP1: what changed, the council, the residual risks (2026-10-01)

WP1 (plan §11: "the portal cannot reboot the hub", C2 a-h, C2b, C4, C5) and the three plan updates of 2026-10-01 are committed on `fix/2.1.4` after `e6c625e`, with the review fixes of 15g. None of it is built yet (CP6, 15j, now with WP2). Nothing was pushed, amended or rebased; `sdkconfig` and `build\` are untouched.

| SHA | Plan item | What |
|---|---|---|
| `97ce041` | C2a, C2h | The Wi-Fi event handler allocates nothing: a message carries its value (SCAN_DONE its status, STA_DISCONNECTED its reason, GOT_IP the IPv4 address), documented at `wifi_manager_set_callback()`; `app_wifi.c`'s callbacks read it. The queue holds 8 messages (was 3) |
| `488c13d` | C2e | Bounded JSON. `json_print_ssid()` reads at most the 32-byte field, escapes `"` and `\`, writes a control character as `?`, and writes an SSID that is not UTF-8 with `\u00XX` for every byte from 0x80 and `"raw":1` on its entry. The list keeps room for `]\n` and leaves out (W line) an entry that does not fit; an empty list is `[]`. `status.json` is bounded (a 32-character SSID no longer runs into the password); `JSON_IP_INFO_SIZE` 159 → 295 |
| `541eaa4` | C2b, C2 (b) | Scan records read one at a time (64 at most) into a 15 × 35 B stack array: the 15 strongest named networks, one per SSID and auth mode, at the strongest access point's channel and RSSI. `esp_wifi_clear_ap_list()` after every scan, failed ones included. The 1,489 B list exists only from START_AP to STOP_AP; `/ap.json` answers `[]` without it |
| `87c3178` | C2f | The NVS save and load close their handle and give the lock back on every path; the load reads through a 128 B stack buffer; `strncmp`/`strnlen` on the fixed-size fields. NVS layout unchanged (`reset_button.c`'s erase holds) |
| `6c69ac3` | C2g | Host (64 B), SSID (33 B) and password (65 B) headers in stack buffers (a Host over 63 characters gets the 302); the URLs are literals; `status.json` gives the json lock back when it has no buffer; a failed `httpd_start()` is logged |
| `9583236` | C2d | `esp_netif_get_ip_info()`, START_AP's `esp_wifi_set_mode()` (a failure starts no AP service or callback, and the retry timer brings START_AP back), `esp_wifi_disconnect()` and GOT_IP's `abort()` are logged instead of rebooting; `wifi_manager_start()` checks every allocation (boot only) |
| `c8b8c7d` | C5 | The component's retry timer starts only while `AP_STARTED_BIT` is clear, and its callback checks again (I9) |
| `4d67b79` | C2c | `esp_wifi_set_config()` and `esp_wifi_connect()` checked; the request bits are set only once an attempt has started. A connect that cannot start is a failed attempt of its kind (a user's: status FAILED, and a refused config is put back to the driver's; an automatic one or the boot restore: LOST and the retry path), then a synthetic STA_DISCONNECTED (reason 205) ends the app's tracking |
| `d300079` | C4 | The captive DNS rewritten to plan §6.2a: bound to 10.10.0.1:53, run flag and its own socket close, up from START_AP to STOP_AP (the stop at GOT_IP is gone) |
| `300e2bc` | update (i) | `MONITOR: idma:` ends ` min_ever=%lu`, the allocator's own internal-DMA low (the 1 s sampler missed lows by about 20 KB on CP5) |
| `56c2c4d` | update (ii) | The three WP0 channel lines end `, Wi-Fi scan in flight` while a scan runs (`wifi_manager_scan_in_flight()`) |
| `703fa22` | update (iii) | The scanner heartbeat says when and why scanning is paused; the normal form is byte-identical |
| `b67e5bf` | review BLM-2 | The list is allocated only with its 1,489 B plus 4 KB in the largest free block, after httpd and DNS; its W line once per AP start |
| `bf45701` | review WP1-R1 | httpd and the DNS task are started again every 5 s while the AP is up and either is down (needs one 8 KB internal block); no new task or timer |
| `5bd0762` | review WP1-R2 | A failed `esp_wifi_disconnect()` on a connected STA drops the forget: nothing erased |
| `9d31927` | review WP1-R3 | Comments only (`app_wifi.c`, `reset_button.c`) |
| `1ef04bb` | docs | §14f item 4 and §15g |

**What the image does differently** (the user-facing list is the CHANGELOG's WP1 entry):
- **No reboot path in the portal stack (I12).** No `ESP_ERROR_CHECK`, `abort()` or `exit()` on a runtime path of `wifi_manager.c`, `http_app.c`, `json.c` or `dns_server.c`. The ones left run once at boot (`nvs_flash_init`, `nvs_sync_create`, `wifi_manager_start()`'s allocations, the task's set-up before its loop). Closes B3 and B4's reboot (plan §7.2) and the 14 paths of C2.
- **A Connect that meets a running attempt** fails at once instead of rebooting: W `ORDER_CONNECT_STA: esp_wifi_set_config failed (ESP_ERR_WIFI_STATE) - attempt not started`, then `APP_WIFI: WiFi Disconnected. Reason: 205`; the page shows "Connection failed", and its Retry works once the other attempt has ended. That attempt goes on and, at its IP, saves and reports its own network, not what was typed. C8 (WP4) makes the Submit lossless; until then the page keeps its 8 s guard.
- **Captive DNS (C4):** A or ANY → 10.10.0.1 with TTL 60 s; AAAA, HTTPS, SVCB, PTR and the rest → NOERROR with no answer; EDNS0 queries get a minimal OPT (B2); malformed questions get a header-only FORMERR or NOTIMP; dropped with no reply: under 17 B, 300 B or more, QR set, internal DMA-capable heap under 10 KB, more than 20 replies in the current second. Up through the 60 s AP tail, so a phone that joins after a rejoin is still sent to the portal (B1). Per-query logging is DEBUG (compiled out): the evidence is WP0's `portal client <IP>: first DNS query`.
- **One retry owner while the SoftAP is up (C5, I9):** `router_retry()` alone, every 33-36 s, deferred while the page polls (5 min at most). The component's loop in the setup AP's tail after a link loss is gone. With the SoftAP down nothing changes (3 retries about 10 s apart, then the fallback AP).
- **`/ap.json` and `status.json`:** always valid JSON; control characters read `?`; a Latin-1 or GBK SSID has `"raw":1`; `status.json` never holds password bytes. The page needs no change.
- **The AP's servers:** a START_AP whose mode switch fails opens no portal and comes back through the retry timer; an httpd or DNS start that fails at START_AP is tried again every 5 s while the AP is up.
- **Forget (D9 kept) and the 10 s reset:** unchanged for an idle or connecting STA; on a connected STA a failed `esp_wifi_disconnect()` now drops the forget (tap Disconnect again). The reset erases NVS itself as before, and `RESET_BTN: Wi-Fi NVS lock busy for 3 s - erasing without it` should no longer print (C2f).

**Log lines.** `functional_test.py`'s version, Wi-Fi MAC, Gateway, BLE-address and "address" rules match none of these (checked against renderings of every new or changed line by the implementers and the council); no production-tool or bench anchor line changed; no line prints a credential.
- New, tag `wifi_manager` (W or E, so they print under `main.c`'s WARN cap): W `network list: %u access points left out (list buffer full)`; W `esp_wifi_scan_get_ap_record failed (%s) - network list kept`; W `network list: no memory for its %u B - the page lists no network yet` (at most once per AP start); W `Wi-Fi config not saved to flash (%s)`; W `esp_netif_get_ip_info failed (%s) - status without addresses`; E `ORDER_START_AP: esp_wifi_set_mode failed (%s) - no AP, tried again through the retry timer`; W `ORDER_DISCONNECT_STA: esp_wifi_disconnect failed (%s)` (STA idle or connecting) and W `ORDER_DISCONNECT_STA: esp_wifi_disconnect failed (%s) - still connected, nothing erased`; E `could not get access to json mutex in WM_EVENT_STA_GOT_IP` (was an `abort()`); W `ORDER_CONNECT_STA: %s failed (%s) - attempt not started` (`esp_wifi_set_config` or `esp_wifi_connect`); W `AP up without its %s - tried again every %d s` (`HTTP server`, `DNS server` or `HTTP and DNS servers`); W `AP servers running again (HTTP and DNS)`.
- New, tag `http_server`: E `httpd_start failed (%s)`.
- New, tag `dns_server`: W `captive DNS: %s failed (errno %d) - trying again every %d ms` (`socket()`, `setsockopt()` or `bind()`); E `captive DNS: DEFAULT_AP_IP is not an IPv4 address - not started`; W `captive DNS: the stopped task has not ended - not started`; E `captive DNS: task not created (no memory)`; W `captive DNS: task still ending after %d ms - it ends by itself`.
- Kept byte for byte: I `dns_server: DNS Server listening on 53/udp` (once per DNS task, when its socket is bound: at each AP start, not again for a START_AP while the task runs), E `dns_server: UDP sendto failed: %d`, `APP_WIFI: Connected! IP: %s`, `APP_WIFI: WiFi Disconnected. Reason: %d` (now also `205` after a connect that did not start), `wifi_manager: could not get access to json mutex in wifi_scan`, and the normal heartbeat.
- Removed (C4): E `dns_server: Failed to create socket`, E `dns_server: Failed to bind to 53/udp` (each was followed by `exit()`), I `dns_server: Replying to DNS request for %s from %s` (now a DEBUG line per query, compiled out).
- Changed (I): `MONITOR: idma: free=%lu min=%lu largest=%lu min_largest=%lu allocfail=%lu min_ever=%lu`, and with a new failed allocation `… allocfail=%lu (last: %lu B, caps 0x%lx, %lu B free, %s) min_ever=%lu`, so the CP5 text is an exact prefix; `APP_WIFI: Wi-Fi channel at AP start: …`, `… at IP: …` and `… at link loss: …` end `, Wi-Fi scan in flight` while a scan runs (byte-identical otherwise); new paused form `BLE_LEAK: [HEARTBEAT] Scanner alive, whitelist=%d sensors, scanning paused for %lu s (%s)`, with `Wi-Fi setup portal` or `Wi-Fi radio hold`.

**Memory ledger** (WP1 against CP5; from object sizes and the code, so unofficial until CP6 measures it, see "Process" below; plan §10 budgets the whole release, not each package):
- **Static RAM:** `.bss` about **−38 B**: `http_app.c` −32 (the eight URL pointers), `dns_server.c` −6 (socket and task handle out, two 1-byte flags in), libc's `__atexit` −4 (it leaves the link with `exit`), `wifi_manager.c` +4 (`accessp_records` −4; `ap_list_wanted`, `scan_in_flight`, `ap_list_logged`, `ap_servers_down` +1 each; `ap_servers_tick` +4). `.data` about **−6 B** (`ap_num` −2, libc's `__atexit_recursive_mutex` −4). Both round by alignment.
- **Heap at rest:** with Wi-Fi connected and the SoftAP down, about **+2.8 KB free**: the records array (1,380 B) gone, the list (1,489 B) only while the AP is up, the URL copies (about 150 B) gone; against that `JSON_IP_INFO_SIZE` 159 → 295 (+136 B, allocated at boot) and the 8-deep queue (+40 B). With the SoftAP up, about **+1.3 KB**. With no Wi-Fi saved, about 50 B per boot no longer leaked (C2f). (The +136 B and +40 B were disclosed in `488c13d` and `97ce041` but in no ledger until this one; the council asked for them here.)
- **Tasks and timers:** none new. The DNS task (3,072 B stack plus TCB, about 3.4 KB of internal heap) now also lives through the AP tail: up to 60 s after the IP, or as long as the AP stays up after the STA loses the link there. Plan §10 accepts it with WP2's admission in place (15g).
- **Flash:** about +2.5 KB of code and +1.3 KB of read-only data (15j).
- **IRAM: 0.** No `IRAM_ATTR`; the new links (`esp_wifi_scan_get_ap_record`, `esp_wifi_clear_ap_list` and their libnet80211 handlers) are plain `.text.*` sections that the `esp_wifi` linker fragment places in flash, and `heap_caps_get_largest_free_block`, `heap_caps_get_minimum_free_size`, `lwip_setsockopt`, `esp_wifi_get_config` and `strnlen` were already linked in flash. Unlinked: `esp_wifi_scan_get_ap_records`, `exit`, `__call_exitprocs`.
- **Stack** (frames from the objects; no high-water mark is logged, only the canary): `wifi_manager` (4,096 B) about 2.6-2.7 KB at its deepest (the 480 B loop frame, the 688 B scan-record frame, `vprintf` for a W line); `dns_server` (3,072 B) about 2 KB with the activity hook's first-query log line on it, so about 0.7-1 KB of headroom; `httpd` (4,096 B) GET frame 32 → 96 B, POST 64 → 160 B.

**The council** (rtos, memory, wifi-portal, security, safety): **5/5 SHIP, no blocking issue.** Checked and passed: I12 (only boot-time checks remain); every DNS read stays below the received length and the longest reply is 299 B (a `_Static_assert`); the DNS start/stop handshake never puts two tasks on :53, and the stopper waits at most 1 s; C5 checks the bit both where the timer starts and in its callback; the server retry is a bounded queue wait on the existing loop, safe across tick wrap-around; every value the event handler sends is used correctly by every app callback; the new links are flash functions; static RAM down about 40 B; heap at rest up about 2.8 KB; the production tool parses no new line; line endings kept. Their bench risks are in 15k.

**Process, for the user's decision.**
1. **§8's no-compiler rule was broken.** The two WP1 implementers and the fixer compiled single objects with the project's flags (each file's command from `build\compile_commands.json`) into the session scratchpad, to check that every commit compiles with no warning and to measure the sizes in the commit messages and the ledger above. The fixer reported it and stopped when it noticed; the documentation commits were not compiled. Nothing was written to `build\`, `idf.py` was not run, and no repo file came of it. The figures above are therefore unofficial: CP6 is the authority. Your call whether later packages may do the same (scratchpad only) or not; until you say otherwise, §8 stands. *Decided by the user on 2026-10-01: allowed, scratchpad only, never `idf.py`, never `build\`, never a flash; §8 now says so, and WP2 used it.*
2. **`9583236`'s subject has 74 characters** (15g): reword it before any push, or accept it.
3. **G-FAULT's heap hold** ("internal heap held below 1 KB for 5 s", plan §12) cannot be run on this image: nothing in `main/` or `components/` can hold the heap down. 15k runs a proxy (the router-outage fallback with a phone, and the E2 laptop) and records it. Either accept the proxy for WP1, or ask for a bench-only hook (for example under `APP_BENCH_DIAG`) in a later package, so that `bf45701`'s server restart is exercised.

**Residual risks** (none blocks WP1; the owner is in brackets):
1. **A forget and a Submit inside one attempt.** The synthetic 205 (`4d67b79`) ends the app's tracking while the real attempt is still connecting, so a forget pending from that attempt is posted at once; it takes wifi_manager's user-connect-failed branch and the disconnect bit stays armed. If the attempt then gets its IP, the next link loss erases the credentials and opens the uncapped no-credential pause (15e item 1). It needs Disconnect and then Connect within one router-retry attempt; before WP1 the same sequence rebooted the hub (which cleared the RAM-only bit). [C8, WP4; joins 15e item 2's forget-bit hazard]
2. **Two unlocked writers of the STA config:** C2c's restore (`esp_wifi_get_config()`, wifi_manager task) and the POST handler (httpd task). A second Submit landing within microseconds of a first one's "attempt not started" can lose its typed credentials. Pre-existing race class, behind the page's 8 s guard. [C8, WP4]
3. **The POST handler's `memset` of its password copy** is a dead store that `-O2`/`-Os` may remove (it is in the current `-Og` object). Low impact: the same password is in the live config. [WP4: `mbedtls_platform_zeroize()` in C8's intake]
4. **`portMAX_DELAY` queue posts** (pre-existing): the wifi_manager task posts to its own queue (START_AP, LOAD_AND_RESTORE), and an httpd handler can post while the wifi_manager task waits in `httpd_stop()` at START_AP or STOP_AP. The 8-deep queue makes both rarer. [C6, WP4: posts bounded at 200 ms]
5. **STOP_AP ignores `esp_wifi_set_mode(WIFI_MODE_STA)`'s result** (pre-existing): if it fails, the AP stays up with DNS stopped, the server retry cleared and the list freed (I11 broken, nothing repairs it). [WP2: the IP + 75 s backstop should check the mode] *Closed by WP2 (15i): `0939a4d`, `ba9fb7c`.*
6. **GOT_IP handled before `AP_STARTED_BIT` is set** (a rare ordering): no AP shutdown timer starts, so the AP and now its DNS stay up with the STA connected. The app's safety net (`73b4483`) stops it only in the no-credential window. [WP2's backstop] *Closed by WP2 (15i): `1a5ca5c` arms the stop from the mode, and its backstop covers any SoftAP.*
7. **`scan_in_flight` can read false** while a scan that overrode an earlier one is still running (the earlier one's SCAN_DONE clears it). Only the channel lines' suffix is affected; the comment ("errs towards in flight") overstates. [WP10 register]
8. **DNS answers a LAN host that routes 10.10.0.1 through the hub** during the tail (lwIP's weak host model), at most 20 replies a second, nothing secret. The plan's "no LAN answer" holds for the hub's LAN IP. One compare in `dns_serve()` (drop sources outside the AP subnet) would close it. [WP2 with C3, which must also check the peer's subnet and refuse non-v4-mapped IPv6, not only the socket's local address] *Closed by WP2 (15i): `30fb932`.*
9. **The DNS reply budget is global** (20 a second for all clients): a laptop's lookups can use it up and drop a phone's probe (Android retries after about 5 s). Drops are DEBUG, invisible on the bench. [measure in G-CNA and G3's E2; per-source accounting only if needed]
10. **The AP tail until WP2:** the DNS task and its httpd sessions run beside MQTT/DPS TLS (15g), and the sockets are tight there (`CONFIG_LWIP_MAX_SOCKETS=16`: httpd up to 12, DNS 1, MQTT 1, DPS 1 at first commissioning). [WP2 admission; WP4's C6 socket cap] *The TLS half is closed by WP2 (15i); the socket cap stays WP4's.*
11. **The server retry needs one 8 KB internal block:** a heap that recovers its total but stays fragmented keeps the portal without a server, with only the first `AP up without its …` line. [G-FAULT records it, 15k]
12. **Cosmetic, for the component change register:** `dns_server.h` declares `bool dns_server_start();` without `<stdbool.h>` or `(void)`; NOTIMP and FORMERR replies carry OPCODE 0 (RFC 1035 copies the query's); a name of 255 octets plus the root's zero is accepted. [WP10]

### 15i. WP2: what changed, the council, the residual risks (2026-10-01)

WP2 (plan §11: "cloud admission and AP lifecycle: flag-only callbacks, admission, DPS abort hook, `outbox.limit`, lifecycle republish; C3; C12 (API only); the app AP-tail policy") is committed on `fix/2.1.4` after `6225f13`: eleven implementer commits (the cloud side, then the AP lifecycle) and eleven review fixes. **The last firmware commit is `0aae305`.** None of it is built yet: CP6 now builds WP1 and WP2 together (15j). Nothing was pushed, amended or rebased; `sdkconfig` and `build\` are untouched. Every commit was compiled on its own in the scratchpad with the project's flags (§8, user decision of 2026-10-01), with no new warning. As plan §11 says, C12 is the API only: `POST /finish.json` and the page's Finish button come with WP4, and until then a setup-page Connect gets the 60 s tail (15 s after the last station leaves).

| SHA | Plan item | What |
|---|---|---|
| `dc9db75` | §4.6: flag-only callbacks, admission (I4, I6) | `cb_connection_ok()`/`cb_connection_lost()` only set a flag (a loss is also counted) and wake `iothub_task`; `iothub_resume_mqtt()`/`iothub_suspend_mqtt()` leave `app_iothub.h` and become `iothub_task`'s static `mqtt_resume()`/`mqtt_stop_for()`. `cloud_admission()` runs first in `net_maintain()`: a link loss withdraws the admission and stops MQTT; an IP is admitted with the Wi-Fi mode STA and internal DMA ≥ 36 KB free with a ≥ 12 KB block; the escapes after 60 s (≥ 8 KB block, W) and 180 s (any heap, E) are counted; a SoftAP that comes up withdraws it. `s_mqtt_suspended` starts true. 1 s polls while an IP waits; SNTP still starts at the IP |
| `20346a6` | §4.6: DPS abort hook | `dps_register(…, keep_going)` waits in 1 s slices (still 60 s at most); when the hook (`cloud_admission_holds()`) says no, the DPS client is stopped and destroyed and the call returns `ESP_ERR_INVALID_STATE`: no attempt counted, no back-off. A NULL semaphore or client now fails at once |
| `69e7bc3` | §4.6: `outbox.limit` | 12 KB (an 8 KB largest message plus 4 KB; the plan said about 4 KB, see below), set in `build_mqtt_cfg()` so `sas_refresh()` keeps it. An event refused with −2 goes to the offline buffer and is replayed every 10 s while connected; a drain cut short marks a replay owed; a SUBSCRIBE refused at CONNECTED logs an E line and disconnects (esp-mqtt reconnects 10 s later) |
| `45a2cdf` | §4.6: lifecycle republish | `telemetry_v2_publish_lifecycle()` returns whether MQTT took it; one it did not take is sent again every 5 s while connected. Each CONNECTED still publishes its own |
| `c9cdc4a` | fix of `dc9db75` | The loss count is read before the flag (the writer clears the flag first) |
| `33f8208` | I4 | After a SoftAP seen up for this IP, the admission waits until it has been down 1 s (replaced in `a139be5`) |
| `611b8d3` | C12 (API) | `bool wifi_manager_ap_stop_in(uint32_t ms)`: re-arms the single AP shutdown timer (block time 0), only while `WIFI_CONNECTED_BIT` is set, re-reads the bit afterwards and stops the timer if it cleared; STA_DISCONNECTED now stops the timer unconditionally; GOT_IP arms the default period with `xTimerChangePeriod()`. A timer command, not a message: no wait on the wifi_manager task's own queue, and WP4's Finish can call it from httpd |
| `0939a4d` | 15h risk 5 | STOP_AP checks `esp_wifi_set_mode(WIFI_MODE_STA)`: on failure the AP keeps its DNS, httpd and list, the callback is not called, and the stop is tried again 5 s later through the shutdown timer |
| `1a5ca5c` | §4.6: the app's AP-tail policy | `ap_tail_start()` at each IP (wifi_manager task), from facts (the mode, the station list): 0.5 s (automatic rejoin, 0 stations), 20 s (automatic, ≥ 1 station), 60 s (setup-page Connect). `ap_tail_maintain()` on `wifi_task` every second during a tail: once no station is left, the stop moves to leave + 10 s (automatic) or max(IP + 15 s, leave + 15 s) (Connect); a station back moves it to the cap. The IP + 75 s backstop (one STOP_AP per IP, any SoftAP) replaces `portal_priority_net` and `PORTAL_AP_STOP_MARGIN_MS`. Automatic or Connect from `cb_connect_sta()`'s existing bookkeeping (`s_retry_sent`/`s_retry_seen`). `cb_ap_stopped()` is idempotent |
| `62983bb` | C3 (part 1) | httpd only while the AP is up: no `http_app_start()` at boot, none after STOP_AP; a START_AP with the AP already up (the forget) keeps the running server and its sessions |
| `30fb932` | C3 (part 2), 15h risk 8 | `http_app_on_ap()`: local address 10.10.0.1 and peer in 10.10.0.0/24, IPv4 or v4-mapped (native IPv6 fails); GET/HEAD, POST and DELETE check it first and answer `403 Forbidden` with no body otherwise, nothing done or reported to the activity hook. `dns_serve()` drops senders outside the subnet before anything else |
| `e09f2ac` | review ADM-1, WP2-CONC-1, BML-2 | The esp-mqtt task stores a cmd_ack the outbox refused with the new `offline_buffer_try_store()` (no wait): no lock inversion with `iothub_task`'s drain (ob lock, then esp-mqtt's API lock) |
| `5ea0ab4` | review ADM-2 | While a replay is owed, an event from `iothub_task` runs the replay first, and is stored behind what is still waiting: order kept |
| `86bee44` | review ADM-5 | The lifecycle, replay and twin-report block runs only while still connected; `telemetry_v2_drain_offline()` returns at once when not connected |
| `663230e` | review BML-3 | A twin GET refused at the SUBACK is sent again every 5 s while connected; each CONNECTED clears it |
| `7152145` | review WP2-CONC-3 | A registration the link loss ended itself (no assignment) asks the hook once more after the stop: counted as aborted, not as a failed attempt |
| `0e135ad` | review WP2-AP-2, WP2-CONC-5 | A first tail arm the timer did not take (`s_tail_armed`) is set by `wifi_task`'s next pass |
| `ba9fb7c` | review WP2-AP-1 | A STOP_AP that cannot leave APSTA calls its callback with 1: the portal window closes (BLE resumes), no `SoftAP stopped` line, the AP keeps its servers and the 5 s retry |
| `a45fc1f` | review BML-4 | `http_server`'s `POST %s` and `DELETE %s` are logged only after the SoftAP check |
| `8cc3eea` | review BML-1, ADM-4, WP2-CONC-4 | STOP_AP marks its stop under way from just before the mode switch until DNS, httpd and the list are freed; `wifi_manager_ap_stop_done(&ms_since)` reports it |
| `a139be5` | the same | The admission waits until the stop has finished, on every pass (also the first look at an IP), then `ADMIT_AP_SETTLE_MS` 500 ms (was 1 s from the mode switch); a stop unfinished after 10 s no longer holds the cloud (W) |
| `0aae305` | follow-up of `5ea0ab4` | The connection is read again after the replay-first drain (a failed replay write ends the session) |

**What the image does differently** (the user-facing list is the CHANGELOG's WP2 entry):
- **Cloud admission (I4, I6).** `iothub_task` lets the cloud's TLS (MQTT, and DPS's private client) start for an IP only with the Wi-Fi mode STA (the SoftAP down), the SoftAP's stop finished at least 500 ms before (DNS task ended, httpd stopped, list freed), and internal DMA-capable heap ≥ 36 KB free with a ≥ 12 KB block. It looks every second while an IP waits. The heap gate's escapes count from its first hold for that IP: after 60 s with a ≥ 8 KB block (W), after 180 s whatever the heap (E), both counted (G3b expects none). The SoftAP rule has no escape: the tail ends by the IP + 75 s backstop, and a stop still unfinished 10 s after it began no longer holds the cloud (W). Once admitted, the gate is not checked again (esp-mqtt's reconnects and the SAS renewal run as before). A link loss (counted, so one the link is already back from is still seen) or a SoftAP that comes up withdraws the admission and stops MQTT on that pass. SNTP still starts at the IP (UDP, beside the SoftAP).
- **A normal boot with Wi-Fi saved** has no SoftAP: the pass that sees the IP prints `cloud admitted 0.N s after the IP (…)`, and the cloud comes up as fast as before.
- **First setup and the 10 s reset (S1, S2):** no web server until START_AP. After the Connect: `SoftAP tail after a setup-page Connect …` and `cloud admission deferred: SoftAP up …`; the SoftAP stops 60 s after the IP, or 15 s after the phone leaves (never before IP + 15 s); then `SoftAP stopped (its servers too) …` and `portal priority OFF (AP stopped)` (BLE resumes sooner than CP5's 60 s), and `cloud admitted …` about 0.5-1.5 s later, then DPS (cached, or a live registration) and MQTT.
- **Router outage:** the loss prints `cloud admission withdrawn (WiFi down)` and the old stop line, both from `iothub_task`'s next pass; events go to the offline buffer; the fallback SoftAP comes up as before. At the rejoin with no station on the SoftAP it stops at IP + 0.5 s, the cloud is admitted about IP + 1.5-3 s and MQTT connects about IP + 3-5 s (CP5: TLS at IP + 0.64 s beside the SoftAP). With a phone on it: 20 s after the IP, or 10 s after the phone leaves. A link lost again in the tail leaves the SoftAP up (C5) and only counts (nothing was admitted); `router_retry()` rejoins, with a new tail.
- **The SoftAP's stop (C12; 15h risks 5 and 6):** the one shutdown timer, re-armed by `wifi_manager_ap_stop_in()` from the app's policy, which reads facts (the mode, the station list) every second; STA_DISCONNECTED stops it, and a re-arm racing a link loss always ends stopped. A STOP_AP that cannot leave APSTA keeps the AP whole, is tried again every 5 s and closes the portal window. The backstop stops any SoftAP still up with the STA connected at IP + 75 s (the old safety net covered only the no-credential window).
- **C3 on the home LAN:** the web server runs only from START_AP to STOP_AP, and answers only requests to 10.10.0.1 from 10.10.0.0/24 (403 otherwise, nothing done, a DEBUG line only); the DNS drops senders outside that subnet. From the LAN with the SoftAP down, port 80 refuses and port 53 does not answer; with it up, 403 and no DNS answer, also for a host that routes 10.10.0.0/24 through the hub. The forget from a phone on the SoftAP keeps working (D9).
- **DPS (I4):** a live registration asks the hook every second and is stopped within a slice after a link loss or a SoftAP start (or counted as aborted if the loss ended the session itself); no attempt counted, no back-off, and it runs again on the first pass after the next admission. An assignment that arrived as the hook tripped is kept.
- **MQTT outbox and what it refuses:** `outbox.limit` 12 KB. A refused (−2) event goes to the offline buffer and is replayed every 10 s while connected, in order (an `iothub_task` event replays first and waits behind what is left); the esp-mqtt task stores a refused cmd_ack without waiting; a refused snapshot retries after 5 s; refused SUBSCRIBEs at a connect disconnect, and esp-mqtt reconnects 10 s later (stale items expire in 30 s). The lifecycle and the twin GET are sent again every 5 s while connected until MQTT takes them. A normal connect still sends exactly one lifecycle.

**Log lines.** `functional_test.py`'s rules (a version after "ver"/"fw", the Wi-Fi MAC, Gateway, BLE address, "address") match none of these (checked by the implementers, the fixer and the council); none prints at a production-tool boot (no IP); none prints a credential; no production-tool or bench anchor line changed.
- New, `IOTHUB`: I `cloud admitted %lu.%lu s after the IP (internal DMA free %u B, largest %u B)`; I `cloud admission deferred: SoftAP up - no TLS or DPS until it stops` and I `cloud admission deferred: internal DMA free %u B, largest %u B (needs %u / %u)` (once per hold each); I `cloud admission withdrawn (%s)` (`WiFi down` or `SoftAP up`, only after an admission); W `cloud admitted %lu.%lu s after the IP below the heap gate (internal DMA free %u B, largest %u B) - escape %lu since boot`; E `cloud admitted %lu.%lu s after the IP whatever the heap (internal DMA free %u B, largest %u B) - escape %lu since boot`; W `cloud admission: the SoftAP's stop still not finished after %d s - the heap gate decides`; W `SoftAP up — stopping MQTT client (free TLS heap for AP/captive portal)` (the stop line with its new reason); E `Subscribe refused (%d %d %d, outbox %d B) - reconnecting`; W `Lifecycle not taken by MQTT - sent again every %d s while connected`; W `Twin GET not taken by MQTT - sent again every %d s while connected`.
- New, `DPS`: W `DPS registration aborted after %lu s: Wi-Fi lost or SoftAP up - tried again once the cloud is admitted again` (counted from the DPS client's start; it includes that client's stop); E `DPS registration not started (no memory)`; E `DPS registration failed (MQTT client not created)`.
- New, `TELEMETRY_V2` (W): `Outbox full - %s kept for replay`, `Outbox full - %s kept for replay, behind the buffered ones`, `Outbox full - %s not kept`. `OFFLINE_BUF` (W): `store: buffer busy - skipped, not waited for`.
- New, `APP_WIFI` (I unless marked): `SoftAP tail after an automatic rejoin (no station on it) - it stops %d.%d s after the IP`; `SoftAP tail after an automatic rejoin (stations on it: %d) - it stops %d s after the IP, or %d s after the last station leaves`; `SoftAP tail after a setup-page Connect (stations on it: %d) - it stops %d s after the IP, or %d s after the last station leaves (not before %d s)`; `SoftAP tail: %s - it stops %lu.%lu s after the IP` (`no station left on it`, `a station on it again`, `its stop not set at the IP, set now`); `SoftAP stopped (its servers too) %lu.%lu s after the IP` (once per IP); W `SoftAP still up %u s after Wi-Fi connected - stopping it` (the backstop outside the portal window). `portal priority OFF (%s) - BLE scanning resumed` has a new reason, `SoftAP stop failed`.
- New, `wifi_manager` (W/E, so they print under `main.c`'s WARN cap): W `AP stop in %lu ms not set (timer queue full)`; E `ORDER_STOP_AP: esp_wifi_set_mode failed (%s) - AP kept up, stopped again in %d s`. `dns_server`: E `captive DNS: DEFAULT_AP_NETMASK is not an IPv4 netmask - not started`.
- Kept byte for byte, in a new place or at a new time: `IOTHUB: WiFi down — stopping MQTT client (free TLS heap for AP/captive portal)` (now from `iothub_task`, right after `cloud admission withdrawn (WiFi down)`); `IOTHUB: WiFi up — restarting MQTT client` (now at the admission, after `cloud admitted …`; at a first IP since boot no client exists yet, DPS builds it, and this line does not print); `IOTHUB: Connected to Azure IoT Hub!` (after a setup or a rejoin, now after the SoftAP's stop); `IOTHUB: Twin GET requested (rid=%d)` (now only once MQTT took the GET); `APP_WIFI: portal priority: setup AP still up %u s after Wi-Fi connected - stopping it` (now the 75 s backstop in the portal window; it fires at 75-76 s, not 75-80 s); `APP_WIFI: portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops (about %d s)` (still `60`, now the upper bound). `http_server`'s I `POST %s` and `DELETE %s` (WARN-capped, so not printed) are logged only for SoftAP clients; a refused request is a DEBUG line (compiled out).
- Removed: no line. (`PORTAL_AP_STOP_MARGIN_MS` and `portal_priority_net` are gone.)

**Memory ledger** (WP2 against `6225f13`, all 22 commits; the memory specialist compiled the 8 changed `.c` files at both ends with the project's flags, `-Og`, into the scratchpad; unofficial until CP6 measures it):
- **Static RAM:** `.bss` **+86 B**: `app_iothub.c` +65 (the admission's state, loss count, times and escape count; the replay, lifecycle and twin GET retries and their flags), `app_wifi.c` +15 (`s_attempt_submit`, `s_ip_tick`, `s_tail_submit`, `s_tail_cap_ms`, `s_ap_stop_seen`, `s_tail_armed`), `wifi_manager.c` +5 (`ap_stop_busy`, `ap_stop_done_tick`), `telemetry_v2.c` +1 (`s_replay_owed`). `.data` **+1 B** (`s_mqtt_suspended` now starts true, so it moved from `.bss`). Since `520b17a`: WP0 +121, WP1 −38, WP2 +86 = **+169 B**, against plan §10's gate of ≤ +450 B. Plan §10's row for `app_iothub`/`dps_client` estimated +44 B; the rest is the refusal handling, the lifecycle and twin GET retries, the settle and the review fixes.
- **Flash:** object `.text` **+4,948 B** (literals included; the implementers' commits `app_iothub` +1,873, `app_wifi` +954, `dps_client` +279, `http_app` +258, `wifi_manager` +237, `telemetry_v2` +189, `dns_server` +83; the review fixes +1,075), `.rodata` **+2,392 B**, and about **+0.6 KB** of newly linked library code: about **+7.9 KB** in all (plan §10's rows: +1.7 KB for the app side, plus C3's and C12's shares of the component rows).
- **Newly linked, all flash `.text`** (each sat in CP5's discarded sections): `esp_mqtt_client_disconnect` and `esp_mqtt_client_get_outbox_size` (libmqtt has no linker fragment), `esp_wifi_ap_get_sta_list` and `wifi_get_sta_list_process` (libnet80211: its fragment sends only the `wifi_*iram` sections to IRAM), `lwip_getsockname` (`CONFIG_LWIP_IRAM_OPTIMIZATION` off). Their callees were linked already. No longer linked: `iothub_suspend_mqtt` and `iothub_resume_mqtt` (now static).
- **IRAM: 0.** No `IRAM_ATTR`, `DRAM_ATTR` or section attribute. `xTimerGenericCommand`, `xTaskGetCurrentTaskHandle` and `xTaskGetTickCount` were IRAM-resident in CP5 already; `esp_wifi_get_mode`, `heap_caps_get_free_size`, `heap_caps_get_largest_free_block` and `lwip_inet_pton` were linked in flash. DIRAM `.text` should stay exactly 113,387 B.
- **Heap at rest:** **about +5 KB** with Wi-Fi connected and the SoftAP down: httpd no longer runs in STA mode (its 4 KB stack, TCB, session table, control and listen sockets; 2 of the 16 lwIP sockets back). Unchanged with the SoftAP up. No allocation, task, timer, queue or event handler is added; the only transient is the driver's 24 B ioctl block per `esp_wifi_ap_get_sta_list()` call. With WP1, CP6 should read about 7.5-8.5 KB more free at rest than CP5.
- **Stack** (frames from the objects; no high-water mark is logged, only the canary): `iothub_task` (10,240 B) frame 608 B unchanged; an event publish that replays first is about 1.4 KB deep (the snapshot path is 2.4 KB), and on a failed replay write the esp-mqtt DISCONNECTED handler runs inside it. `wifi_manager` (4,096 B): `ap_tail_start()` on the GOT_IP path adds about 0.25 KB (`ap_station_count()` 224 B) to WP1's 2.6-2.7 KB deepest. `wifi_task` frame 80 → 96 B. httpd: GET 96 → 112 B, DELETE 32 → 48 B, `http_app_on_ap()` 112 B. `dns_serve()` 352 → 368 B on the 3,072 B DNS task. `cloud_bringup()` 512 → 528 B; `mqtt_event_handler()` 160 → 176 B on the MQTT task (6 KB).
- **The outbox:** at most 12 KB of internal heap in a stalled session (esp-mqtt counts only payloads, so up to one message's header and topic more, about 0.1-0.25 KB); before WP2 it had no limit. esp-mqtt frees it when its task stops.

**The review and the council.** The review's 18 findings: the fixer fixed 13 in the eleven review-fix commits above; WP2-AP-3 is documentation (item 4 below); ADM-3 and WP2-CONC-2 are the user's decision (item 1 below). The council (rtos, memory, cloud, wifi-portal, safety) voted **5/5 SHIP, no blocking issue.** Checked and passed: the callbacks only set flags, and no new deadlock edge (`s_mqtt_ctl_mutex` is taken only on `iothub_task`, and the esp-mqtt task waits on no lock that `iothub_task` holds); the admission reads facts every pass in an order that cannot admit a lost link or a stop still under way (STOP_AP marks itself busy before its mode switch); the shutdown timer's re-arm and stop are ordered by the event handler's bit clear, and a duplicate STOP_AP is harmless; every hold on the cloud is bounded except a mode switch that keeps failing (by design, I4); the DPS hook runs on the right task and cannot spin; the esp-mqtt behaviours relied on hold in the IDF 5.5.1 source (−2 queues nothing, a failed write aborts the session on the caller, a stop deletes the outbox, items expire after 30 s); the tail implements every plan §4.6 row from facts; C3 refuses by local address and peer subnet, and the DNS drops off-subnet senders; a leak is delayed by the stop's stall at most, never missed; the new links are flash functions; no IRAM, task, timer or heap at rest added; the heap gate passes with 7-15 KB to spare in every measured state; the production tool parses no new line. Their bench risks are in 15k.

**For the user's decision.**
1. **ADM-3 / WP2-CONC-2: the MQTT stop at a link loss now blocks `iothub_task`,** which also evaluates leaks, instead of the wifi_manager task (plan §4.6 puts it there: "`iothub_task` stops MQTT at once on a link loss"). The council differs on the usual length: about 1 s if esp-mqtt's session is still CONNECTED when the loss is seen (rtos, cloud: `esp_mqtt_client_stop()` waits out one 1 s read slice), up to 5 s if esp-mqtt already noticed the loss (safety: the netif's address change aborts the TCP session at once, and the stop waits out the 5 s reconnect slice). About 10-20 s at worst, with a connect in flight (esp-mqtt holds its API lock through DNS, TCP/TLS and CONNACK; network timeout 10 s) or a DISCONNECT write meeting a full send buffer. No deadlock; `iothub_task` is not on the task watchdog. A leak is delayed, not missed: the item dequeued on that pass is evaluated before `net_maintain()`, the valve close runs on the valve task, and the BLE scanner keeps an uncommitted leak change when its queue is full and retries on the next advert. Once per admission (`g_mqtt_running` is false afterwards); before WP2 a live DPS registration could already hold `iothub_task` for up to 60 s. The options: **(a)** accept it, record it here and measure it (15k items 7 and 8); **(b)** a shorter `network.reconnect_timeout_ms` (about 2 s caps the usual stall near 1 s, but retries a dead WAN's TLS every 2 s, the heap churn the stop exists to prevent); **(c)** `network.timeout_ms` 5 s (bounds the connect-in-flight case; may cut slow TLS handshakes); **(d)** change the plan so MQTT stops at START_AP instead of at the link loss. **The council recommends (a), 5/5**; if the bench measures more than about 5 s, the safety specialist names (b) and the wifi-portal specialist (c) as the fix.
2. **The outbox limit is 12 KB, not the plan's "about 4 KB"** (§4.6, §8; D4's alerts). At 4 KB a big hub's snapshot (7.5-8 KB for 16 BLE + 16 LoRa sensors and the valve) would be refused for ever. The council endorses 12 KB (the outbox had no limit before WP2); plan §8's heap table would then read "worst case 12 KB of internal heap during a stalled session". Accept the deviation, or give another figure.
3. **The settle after the SoftAP's stop is 500 ms** (`ADMIT_AP_SETTLE_MS`, the fixer's judgement call in `a139be5`, counted from the end of the teardown; `33f8208` used 1 s from the mode switch). It only covers the idle task freeing the stopped tasks' stacks; 1 s would cost G3 about 0.5 s. The council endorses 500 ms. Restore 1 s if the bench shows `cloud admission deferred: internal DMA free …` right after `SoftAP stopped (its servers too)`, or a `min_ever` dip at the admission.
4. **Where G3 and G3b measure** (plan §12's approved gates; WP2-AP-3). G3's "AP down ≤ 1 s after the IP" is read at the driver's mode switch (`wifi:mode : sta (…)` in this bench build), not at `SoftAP stopped (its servers too) N s after the IP`, which also covers the DNS task's end (up to 0.5 s) and httpd's stop and can read 1.0-1.3 s with the AP off the air at 0.5 s; `wifi_manager`'s `MESSAGE: ORDER_STOP_AP` is INFO under the WARN cap and does not print. G3b's "admission ≤ 5 s" is measured from the IP for the half with 0 stations, and from `SoftAP stopped (its servers too)` for the half with a phone on the AP (its tail is 20 s by design). 15k item 7 records both points; WP10 writes the agreed wording into the test plan.

Still open from 15h: `9583236`'s 74-character subject, and G-FAULT's heap-hold proxy.

**Residual risks** (none blocks WP2; the owner is in brackets):
1. **ADM-3** (above). [the user; measured in 15k items 7 and 8, and at G3b]
2. **A STOP_AP whose mode switch keeps failing** keeps the cloud off while the STA is connected (by design: the SoftAP rule has no escape, I4); BLE resumes (`portal priority OFF (SoftAP stop failed)`), and the stop is tried every 5 s. Double fault: that branch ignores `xTimerChangePeriod()`'s result, so with the timer queue full when the failed STOP_AP was the backstop's (one per IP), nothing tries again until the next IP. [a later WP: log a failed re-arm, or repeat the backstop every N s past 75 s while the SoftAP is up with the STA connected]
3. **A twin reported-property PATCH that esp-mqtt refuses (−2)** is neither retried nor kept (`publish_twin_reported()` ignores the result): stale reported state, for example the `snapshot_interval_s` echo, until the next report. [a later WP: owed and retried like the twin GET]
4. **Order at a full outbox:** at a connect, the lifecycle retry (5 s) can reach the cloud before older buffered events (replay retry 10 s), and a queued snapshot can overtake owed events. It clears within 30 s, and every event carries its own `ts`. [a later WP: hold the lifecycle retry while a replay is owed]
5. **Outbox headroom:** the cloud specialist counts a full hub's worst-case snapshot at about 9.5-10 KB (31-character labels with escaped quotes, LoRa `snr` printed with `%1.17g`, a 192 B health reason), which leaves about 2.5 KB of backlog, not 4 KB. Labels are not sanitised: 32 labels of control characters (6 B each once escaped) could push a snapshot past 12,288 B, refused for ever (no heartbeat); that needs a cloud writing such labels. A stalled session on a full hub holds about 12 KB plus an 8 KB snapshot build out of about 33 KB free: above G3's 8 KB floor, below WP9's 16 KB target, never benched. [WP4/WP10: a W line when a snapshot is refused with an empty outbox, or label sanitising; G-M or G4b for the full-hub stall]
6. **Pre-existing, easier to reach after the 180 s forced admission:** `mqtt_resume()`, `sas_refresh()` and `cloud_bringup()` ignore `esp_mqtt_client_start()`'s result and set `g_mqtt_running` anyway, so a failed start (no memory for the MQTT task's stack) leaves the admission done with no client until the next link loss or the SAS renewal. [a later WP: set it only on `ESP_OK` and let the next pass retry]
7. **Pre-existing: esp-mqtt deletes its outbox when its task stops,** so a QoS 1 event it took (msg_id ≥ 0) but had not had acknowledged before a link-loss stop (or a SAS-refresh stop) is lost: the offline buffer erased it, or never stored it, when esp-mqtt took it. The same for a drain whose session drops in the middle (`drain_locked()` erases a slot at msg_id ≥ 0; `86bee44` narrows the window). The snapshot after the rejoin carries the state, not the event. [a later release: keep each event until `MQTT_EVENT_PUBLISHED`]
8. **`telemetry_v2_drain_offline()` clears the owed flag before `offline_buffer_count()`,** which reads 0 when the buffer's lock times out: those events then wait for the next CONNECTED, not the 10 s retry. Rare. [a later WP]
9. **Between a link loss and `iothub_task`'s next pass,** events it publishes go into the dead session's outbox, and the stop deletes them (no worse than before WP2, whose window ran until the old blocking stop ended). [optional: a flag-only `telemetry_v2_set_connected(false)` in `iothub_on_wifi_lost()`]
10. **I4 by construction:** `dps_register()` starts the DPS client's TLS and first asks the hook after one 1 s slice. Unreachable in practice: the SoftAP starts only after an STA loss, never between the admission and DPS in one pass. [optional: one hook check before `esp_mqtt_client_start()`]
11. **Cosmetic:** `Lifecycle not taken by MQTT - sent again …` can print when a failed replay write ended the session (the next CONNECTED sends it). `publish_json()` treats any task other than `iothub_task` as the esp-mqtt task (try_store, no wait); there is no third publisher today. [WP10 register: a comment or an assert when a publisher task is added]
12. **Minor, harmless:** a SoftAP down at the IP that comes up later has no tail, so `wifi_task`'s 5 s cadence can put the backstop at about 80 s (START_AP runs only after a disconnect, so effectively unreachable); a re-arm racing the tail's own STOP_AP fires a second STOP_AP with the AP already down (no line; at most 0.5 s more for a cloud not yet admitted).
13. **If the largest internal-DMA block does not recover after a tail,** the next suspects are small long-lived allocations made in it: SNTP's PCB (started at the IP beside the SoftAP), the STA's association and DHCP state, and TIME_WAIT PCBs of closed portal sessions. [G3-lite records it, 15k item 7]
14. **Unchanged:** 15h risks 1-4, 7, 9, 11 and 12 (risk 4's `httpd_stop()` at STOP_AP against a handler blocked on a full queue is not made likelier by the 0.5 s tail; WP4's C6). A Forget (D9 kept) tapped in an automatic tail on a router-fallback hub erases the credentials and reopens the uncapped no-credential pause (15e item 1): the development checkpoint's known limit until WP8.

Closed by WP2: 15h risks 5, 6, 8 and 10 (its TLS half).

**WP10 follow-up register** (documents that still show pre-WP2 behaviour; no test-plan edit was made in WP2):
- `MANUAL_TEST_PLAN.md`: T4-10 A2 (the IOTHUB stop line now follows `WiFi Disconnected. Reason: 8`, with `cloud admission withdrawn (WiFi down)`), A8 (`Connected to Azure IoT Hub!` now comes after the AP stop, after `portal priority OFF`), D4 (the cloud comes after the SoftAP's stop), D6, E4, F4, G3, the smoke step 10 row (~line 1221) and ~6742 ("`portal priority OFF (AP stopped)` 60 s (±5 s) after `Connected! IP`" becomes "15 s after the phone leaves, not before 15 s, at most 60 s"; the tail line follows `portal priority: Wi-Fi connected …`); ~604 (`PORTAL_AP_STOP_MARGIN_MS` is gone; the backstop fires at 75-76 s); ~4049 ("the fallback SoftAP stops about 60 s after `Connected! IP`, with no line of its own": now 0.5 s with no station, with `SoftAP stopped (its servers too)`); ~4327 and ~7912 (the `setup AP still up` line is now the backstop in the window), and the `Select-String` at ~7891 adds `SoftAP still up`; ~5532, ~5564 and ~6655-6663 (`WiFi up — restarting MQTT client` now after `cloud admitted …`); the new reason `portal priority OFF (SoftAP stop failed)`; G3's and G3b's measuring points (item 4 above).
- `CHANGELOG.md`'s release sections: under *Fixed*, the router retry ("the fallback SoftAP stops about 60 s after that"); under *Safety*, the portal priority window ("about 60 s after the hub gets an IP address"; the safety net "at most about 80 s after the IP"); under *Upgrade notes*, the serial-log lines of the window (`AP stopped` "normally about 60 s after", "the safety net; should never appear").
- WP4's C8 must keep a request kind the tail policy can use: it replaces `s_retry_sent`/`s_retry_seen`, from which `cb_connect_sta()` tells an automatic rejoin from a page Connect. WP4's Finish is `wifi_manager_ap_stop_in(max(IP + 5 s, now + 2 s) − now)`, called from the httpd task (it does not block).

### 15j. 🔨 Build checkpoint 6, of `0aae305` (WP1 and WP2): only after G0

**CP6 now builds WP1 and WP2 together;** WP1's image alone (`9d31927`) is not built. **Build it only when G0's runs on CP5 are done and their material is saved** (15d): `fullclean` deletes the CP5 build. As 15b: `idf.py fullclean` (it needs registry access; without it, `Remove-Item -Recurse -Force build`), the build into `build_cp6.log`, `idf.py size`. In PowerShell, with the ESP-IDF 5.5.1 environment, in the project folder. CP5's map, ELF, `.bin` and `sdkconfig` are saved first as the reference. If G0 may need the CP5 image again, also copy the whole `build\` folder aside before `fullclean` (it flashes with esptool from its own `flash_args`), or rebuild `31b4c9f` later in a worktree.

```powershell
git log --oneline -1
git diff --stat 0aae305 HEAD -- main components CMakeLists.txt partitions.csv sdkconfig.defaults dependencies.lock managed_components
Get-FileHash sdkconfig
New-Item -ItemType Directory -Force "$env:TEMP\ref_31b4c9f" | Out-Null
Copy-Item build\eFloStop_WiFiHub_idf1.map, build\eFloStop_WiFiHub_idf1.elf, build\eFloStop_WiFiHub_idf1.bin, sdkconfig "$env:TEMP\ref_31b4c9f\"
idf.py fullclean
idf.py build *> "$env:TEMP\build_cp6.log" ; "exit=$LASTEXITCODE"
Select-String -Path "$env:TEMP\build_cp6.log" -Pattern 'warning:|error:' | ForEach-Object Line
idf.py size
Compare-Object (Get-Content "$env:TEMP\ref_31b4c9f\sdkconfig") (Get-Content sdkconfig)
Get-FileHash sdkconfig
Select-String -Path build\eFloStop_WiFiHub_idf1.map -Pattern '^\s+0x\w+\s+(esp_wifi_scan_get_ap_records?|esp_wifi_clear_ap_list|exit|__call_exitprocs|esp_mqtt_client_disconnect|esp_mqtt_client_get_outbox_size|esp_wifi_ap_get_sta_list|wifi_get_sta_list_process|lwip_getsockname|iothub_suspend_mqtt|iothub_resume_mqtt)\s*$' | ForEach-Object Line
git status --short
```

**CP5's figures, the reference.** Read by Claude from `build\eFloStop_WiFiHub_idf1.map` (2026-10-01 11:07, built after `e6c625e` and before WP1's first commit at 12:35; it still links the old DNS's `exit`): DIRAM `.text` **113,387 B** (`.iram0.vectors` 1,028 + `.iram0.text` 128,743 − the 16,384 B of IRAM); `.bss` **36,480 B**; `.data` **21,572 B**; flash `.text` **1,026,154 B**; `.rodata` **368,236 B**; the `.bin` file **1,546,144 B**. The user's CP5 `idf.py size` measured `.bss` 36,480 B, `.data` 21,572 B and a **total image size of 1,545,989 B** (flash `.text` + `.rodata` + `.data` + the 129,771 B of IRAM vectors and code + the 256 B app descriptor, so it matches the map to the byte). The `sdkconfig` in the project folder, written by CP5's configure at 11:04, has SHA256 **`98F3B2CC…AE759767`**: CP5's reference hash.

Expected:
- **The `git diff` prints nothing:** the commits after `0aae305` (this one) touch only `docs/` and `CHANGELOG.md`. The `sdkconfig` hash before the build is CP5's, `98F3B2CC…AE759767`.
- The configure as at CP5: `Processing 4 dependencies:`, no re-solve, `dependencies.lock` unchanged; `-- Components:` lists `wifi_portal`. `git status --short` afterwards lists only the four usual entries (`.vscode/settings.json`, the two 2.1.3 docs, `.adsum/`).
- `exit=0`; **the same four warnings** (`app_ble_valve.c:106:9` `BLE_HS_ATT_ERR` redefined; `app_lora.cpp:185:5` ×2; `app_lora.cpp:160:13` `switch_sync_word` unused), no `error:`. A warning in `components/wifi_portal`, `app_wifi.c`, `app_iothub.c`, `dps_client.c`, `telemetry_v2.c`, `offline_buffer.c`, `app_ble_leak.c`, `monitoring.c` or `reset_button.c` is a finding (the scratchpad compiles of every WP1 and WP2 commit saw none).
- **`Compare-Object` prints nothing, and the hash after the build is still `98F3B2CC…AE759767`:** neither WP1 nor WP2 changes a `sdkconfig` or `sdkconfig.defaults` line (WP3 does).
- **DIRAM `.text` exactly 113,387 B; IRAM 16,384 B (100 %).** The map search prints exactly seven lines, all at `0x420…` addresses (flash): WP1's `esp_wifi_scan_get_ap_record` and `esp_wifi_clear_ap_list`, and WP2's `esp_mqtt_client_disconnect`, `esp_mqtt_client_get_outbox_size`, `esp_wifi_ap_get_sta_list`, `wifi_get_sta_list_process` and `lwip_getsockname`. `esp_wifi_scan_get_ap_records`, `exit`, `__call_exitprocs`, `iothub_suspend_mqtt` and `iothub_resume_mqtt` are gone (on CP5's map the same search prints exactly those five, at `0x420abd9c`, `0x420d8c60`, `0x420dc184`, `0x42016ab4` and `0x42016b2c`; the seven new ones are among its discarded sections). If DIRAM `.text` moves, stop: compare the IRAM input sections of the saved and the new map (the `.iram1` and `.iram0.text` input sections, not their `*fill*`) before calling it a newly linked IRAM function, and send both maps.
- `.bss` about **36,528 B** (CP5's 36,480 − 38 B for WP1 + 86 B for WP2, 15h and 15i), rounded by the section's `ALIGN(8)` and the input-section fill: 36,512-36,544 B. Below 36,500 or above 36,560 B needs a look (`exit` still linked would read about 4 B higher).
- `.data` about **21,567 B** (CP5's 21,572 − 6 B for WP1 + 1 B for WP2): 21,564-21,572 B. Below 21,560 or above 21,576 B needs a look.
- **Flash, against CP5:** `.text` about +7.5-8.6 KB, **1,033,700-1,034,800 B** (WP1 about +2.5 KB, 15h; WP2's objects +4.9 KB and about +0.6 KB of newly linked library code, 15i); `.rodata` about +3.4-4.1 KB, **371,600-372,300 B** (WP1 about +1.3 KB, WP2 +2.4 KB); **total image size about 1,556,900-1,558,700 B**, +10.9-12.7 KB on CP5's 1,545,989 B; the `.bin` about 1,557,200-1,558,900 B, about 25.7 % of the 2 MB partition free. They move with the code: record them, they are not a Fail.
- VAL-01 steps 5-6 as at CP5: the `.bin` newer than `0aae305`'s commit (2026-10-01 19:24:19 +1000); record the ELF SHA256.
- **The boot** as at CP5 (`HUB_IDENT: Firmware version: v2.1.4`, the `bench build (APP_BENCH_DIAG)` warning after `AP SSID:`), and every `MONITOR: idma:` line now ends ` min_ever=N`. With Wi-Fi saved there is no SoftAP: right after `Connected! IP` comes `IOTHUB: cloud admitted 0.N s after the IP (internal DMA free X B, largest Y B)`, X about 50-53 KB and Y about 30 KB or more, then the cloud as at CP5. A `cloud admission deferred: internal DMA free …` line on a normal boot is a finding: the 36 / 12 KB gate would then be too strict for a provisioned hub and add 60-180 s to its boots.
- **At rest** with Wi-Fi connected and the SoftAP down, the `MONITOR: heap:` line's `free` reads about **7.5-8.5 KB above CP5's** in the same state (WP1 +2.8 KB; C3's web server off +5 KB: only +2.8 KB would mean httpd still runs in STA mode), and the `idma:` line about 32-34 KB free (CP5: 25,224 B) with the largest block at 18,432 B or more. Each AP start prints `dns_server: DNS Server listening on 53/udp`, and no `captive DNS: … failed` line.
- CP6 is still a bench image (`APP_BENCH_DIAG=y`): never through the production tool with Wi-Fi credentials saved (15c). The first DIAG-off boot through the tool (WP10) also confirms its parse of the longer `idma:` and channel lines.

**Send back:** `build_cp6.log`, the `idf.py size` output, the map search's output, both `sdkconfig` hashes, and both maps if DIRAM `.text` moved.

### 15k. WP1's and WP2's gates on the CP6 image

WP1's gate (plan §11): **G-FAULT (WP1 subset), G8, P-13 and the 10 s reset ×10**, here with a portal smoke on an iPhone and an Android and a leak check (items 1-6). **Pass for all of them: 0 reboots other than the 10 s reset's own, 0 panics, no invalid list or status JSON, 400s where due** (plan §12). WP2's gate (plan §11): **G3-lite (the E4 replay), the DPS router-pull test, first commissioning and P-14's LAN part** (items 7-9), each with its own pass below. Run each first with the valve **unpowered** (provisioned, batteries out), then **linked**, where the item uses the valve hub.

**Set-up** as 15d (hub `GW-7C4FADAE69C8` on COM30 with the CP6 build, `idf.py -p COM30 flash`, never `erase-flash`; one UART file per run with `--timestamps`; the IoT Hub monitor per run; a note per phone join). Also:
- a Windows laptop with `curl.exe` (built into Windows; in PowerShell type `curl.exe`, since `curl` is an alias for `Invoke-WebRequest`), Python 3, and `dig` (in WSL: `sudo apt install bind9-dnsutils`; or a Linux or macOS machine). The laptop that remembers `WiFi-Hub-69C8` is E2's: keep it off except where a step asks for it;
- a second ESP32 that can beacon chosen SSIDs (item 4b);
- the probe script below, saved outside the repo, for example as `$env:TEMP\dns_probe.py`;
- the spare hub with no devices provisioned (item 8), on its own COM port and IoT Hub monitor (its own gateway ID);
- a PC on the home LAN, not joined to the SoftAP (item 9; the laptop can be it).

**Where to run the laptop items.** On the router-outage fallback SoftAP BLE keeps scanning, and a laptop may get no lease for minutes (15e item 3). Run P-13 and G-FAULT's DNS and HTTP items in the 10 s-reset portal (BLE paused, no credentials) or on a spare hub with no devices provisioned, and note which. The DNS answers only on 10.10.0.1, and only to senders in 10.10.0.0/24. Since WP2 (C3) the web server runs only while the SoftAP is up and answers only requests to 10.10.0.1 from a SoftAP client: item 9 checks the home LAN.

**How the SoftAP stops on this image** (15i): after a setup-page Connect, 60 s after the IP, or 15 s after the last station leaves (never before 15 s); after an automatic rejoin, 0.5 s after the IP with no station on it, or 20 s (10 s after the last station leaves) with one. The cloud is admitted only after that stop. Where an item below needs the SoftAP up after the IP, keep a phone joined to it.

**Report at once:** any `rst:` other than the 10 s reset's, `Guru Meditation`, `***ERROR*** A stack overflow in task` (the canary check is on; above all `dns_server`, `wifi_manager`, `httpd`, `iothub_task`, `wifi_task`), `ESP_ERROR_CHECK failed`, `abort() was called`, with the 30 s before it.

**1. Portal smoke, iPhone then Android (state S2).** After a 10 s reset (item 2's first run can be it), each phone joins `WiFi-Hub-69C8` from Settings; the sign-in window should open by itself; the page lists networks; choose the router, type its password, Connect. Expect `APP_WIFI: portal client <IP>: first DNS query`, `… captive probe (302 sent)`, `… page request`, `… Connect/Disconnect request` (each `…, N ms after joining`), `Connected! IP`, `portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops (about 60 s)`, `SoftAP tail after a setup-page Connect (stations on it: 1) - it stops 60 s after the IP, or 15 s after the last station leaves (not before 15 s)`, `IOTHUB: cloud admission deferred: SoftAP up - no TLS or DPS until it stops`, and the success page. When the phone leaves the SoftAP (or drops at the channel switch), `SoftAP tail: no station left on it - it stops N s after the IP`; if it joins again, `SoftAP tail: a station on it again - it stops 60.0 s after the IP`. Then `SoftAP stopped (its servers too) N s after the IP` and `portal priority OFF (AP stopped) - BLE scanning resumed` (15 s after the phone left, not before IP + 15 s, at most IP + 60 s), then `IOTHUB: cloud admitted N s after the IP (…)` and `Connected to Azure IoT Hub!`. Note the tap time, whether and when the sign-in window opened by itself, what the page showed, whether the phone stayed on the SoftAP and when it left, and the phone's model and OS (as in G0, to compare with CP5). A sign-in window that does not open by itself, or a phone that drops before it shows the success page (no CSA until WP4's C13), is recorded, not a failure here (G-CNA after WP4 has the targets); a reboot is a failure.

**2. The 10 s reset ×10.** With the STA **connected** 4 times; **idle** 3 times (on the router-outage fallback SoftAP, between two router retries); **connecting** 3 times (on that SoftAP, start the 10 s hold about 25 s after the last `WiFi Disconnected. Reason: …`, so that it fires inside the next `router fallback: retrying the configured network (attempt N)` attempt; a run whose `LONG PRESS CONFIRMED` is not within 3 s after that line counts as idle, and is repeated). Each run: `RESET_BTN: === LONG PRESS CONFIRMED — CLEARING WIFI CREDENTIALS ===`, `Wi-Fi credentials erased from NVS`, `Rebooting into AP mode...`; after the reboot `portal priority ON (no Wi-Fi credentials) - BLE scanning paused`, `Wi-Fi channel at AP start: …, router not joined since boot` and `dns_server: DNS Server listening on 53/udp`, and a phone or the laptop can open the page. Set Wi-Fi up again between runs (item 1's steps). **Pass:** every run ends in the no-credential portal; no `Wi-Fi NVS lock busy for 3 s - erasing without it` and no `Wi-Fi credential erase failed`; no reboot but the reset's own.

**3. P-13 (plan §7.5).** The laptop on `WiFi-Hub-69C8` with its lease:

```bash
dig @10.10.0.1 captive.apple.com A
dig @10.10.0.1 captive.apple.com A +noedns +norecurse
dig @10.10.0.1 captive.apple.com AAAA
dig @10.10.0.1 captive.apple.com HTTPS
dig @10.10.0.1 _dns.resolver.arpa SVCB
dig @10.10.0.1 $(python3 -c "print('.'.join(['a'*63, 'b'*63, 'c'*63, 'd'*58]))") A
```

(an older `dig` that knows neither HTTPS nor SVCB takes `TYPE65` and `TYPE64`). Then, in PowerShell, `python $env:TEMP\dns_probe.py odd` (UDP datagrams of 1, 11 and 600 B). Last, once Wi-Fi is set up again from the laptop or a phone that then stays joined to `WiFi-Hub-69C8` (so the SoftAP stays up 60 s after `Connected! IP: <LAN IP>`), within that minute, from a machine on the home LAN: `dig @<LAN IP> captive.apple.com +time=2 +tries=1`.

Pass:
- A: `status: NOERROR`, `flags: qr aa rd ra`, `ANSWER: 1`, `captive.apple.com. 60 IN A 10.10.0.1`; with EDNS (dig's default, with a cookie) `ADDITIONAL: 1` and an `OPT PSEUDOSECTION` with `udp: 512`. No `WARNING: Message has … extra bytes at end` and no `malformed` (CP5's EDNS0 defect, B2).
- `+noedns +norecurse`: `flags: qr aa ra`, `ADDITIONAL: 0`, the same answer.
- AAAA, HTTPS and SVCB: `status: NOERROR`, `ANSWER: 0` (CP5 answered each with an A record).
- The 250-character name (a 291 B query with dig's cookie, under the 300 B drop): NOERROR and the A answer.
- `dns_probe.py odd`: `no reply (pass)` three times. (On Windows an ICMP "port unreachable" also reads as no reply, so run it right after the dig queries have shown the DNS up.)
- The LAN IP: no answer (`timed out` or `connection refused`). A LAN host with a static route that sends to 10.10.0.1 through the hub's STA IP gets no answer either since WP2 (`30fb932` drops senders outside 10.10.0.0/24; 15h risk 8 closed): item 9 tries it.
- **No reboot.** The hub logs nothing per query (DEBUG), only `portal client <laptop IP>: first DNS query` once.

The probe script (Python 3, standard library only):

```python
import os, random, socket, sys, time
HUB = ('10.10.0.1', 53)
Q = bytes.fromhex('123401000001000000000000') + b'\x07captive\x05apple\x03com\x00\x00\x01\x00\x01'
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
if sys.argv[1:] == ['odd']:            # P-13: datagrams of 1, 11 and 600 B
    s.settimeout(2)
    for n in (1, 11, 600):
        s.sendto(bytes(n), HUB)
        try:
            print(n, 'B: reply of', len(s.recv(2048)), 'B (FAIL)')
        except OSError:
            print(n, 'B: no reply (pass)')
    sys.exit()
s.setblocking(False)                   # G-FAULT: 1,000 datagrams, about 30-50 a second
per_s, t0 = {}, time.time()
def drain():
    while True:
        try:
            s.recv(2048)
        except OSError:
            return
        k = int(time.time() - t0)
        per_s[k] = per_s.get(k, 0) + 1
for i in range(1000):
    if i % 2:                          # a real query with 1-4 bytes changed, a quarter cut short
        d = bytearray(Q)
        for _ in range(random.randint(1, 4)):
            d[random.randrange(len(d))] = random.randrange(256)
        if random.random() < 0.25:
            d = d[:random.randint(1, len(d))]
    else:                              # random bytes, 1-600 B
        d = os.urandom(random.randint(1, 600))
    s.sendto(bytes(d), HUB)
    time.sleep(0.02)
    drain()
time.sleep(1)
drain()
print('replies:', sum(per_s.values()), ' most in one second:', max(per_s.values(), default=0))
```

**4. G-FAULT, WP1 subset (plan §12).** Pass: 0 panics, 0 corruption, 400s where due.
- **(a) The heap hold, by proxy** (15h "Process" item 3). The router-outage fallback with a phone polling the page while the router is power-cycled twice, then once with the E2 laptop on the SoftAP. Record every `idma:` line (`min`, `min_largest`, `min_ever`, `allocfail` and its `(last: …)`). Pass: no reboot; any `wifi_manager: AP up without its …` is followed by `AP servers running again (HTTP and DNS)` before the run ends (15g); `network list: no memory for its 1489 B …` at most once per AP start; the page lists networks again once the heap recovers.
- **(b) Beacons.** The second ESP32 beacons close to the hub (the list keeps the 15 strongest): first 10 SSIDs of 31 × 0x01 plus a distinct last byte (`A` … `J`), then 10 SSIDs of 31 × 0xFF plus a distinct last byte. Ten identical SSIDs with one auth mode are one entry since C2b, so the plan's "10 SSIDs of 32 × 0x01" alone no longer tests the bound. With the laptop on the portal, each time: `curl.exe -s http://10.10.0.1/ap.json -o $env:TEMP\ap.json`, `python -m json.tool $env:TEMP\ap.json`, and `curl.exe -s http://10.10.0.1/status.json`; also look at the page on a phone. Pass: valid JSON every time; the 0x01 networks listed as `???…?A` and so on; about 6 of the 0xFF networks listed (about 240 B each), with `ÿ` escapes and `"raw":1`, and `wifi_manager: network list: N access points left out (list buffer full)`; no reboot. Tapping a `?` network submits `?` bytes and fails to connect: expected.
- **(c) DNS fuzz.** `python $env:TEMP\dns_probe.py` (1,000 datagrams). Pass: no reboot and no stack-overflow line; the script's `most in one second` at most 20 (the reply cap); `dig @10.10.0.1 captive.apple.com A` still answered right after; `allocfail` not rising during the run. Then once more while two phones join the SoftAP (their first-DNS log lines run on the DNS task's stack).
- **(d) HTTP header fuzz** (PowerShell):

```powershell
curl.exe -s -o NUL -w "%{http_code}\n" -H "Host: $('a'*100)" http://10.10.0.1/
curl.exe -s -o NUL -w "%{http_code}\n" -H "Host: $('a'*2000)" http://10.10.0.1/
curl.exe -s -o NUL -w "%{http_code}\n" -H "Host:" http://10.10.0.1/
curl.exe -s -o NUL -w "%{http_code}\n" -X POST http://10.10.0.1/connect.json
curl.exe -s -o NUL -w "%{http_code}\n" -X POST -H "X-Custom-ssid: $('s'*33)" -H "X-Custom-pwd: 12345678" http://10.10.0.1/connect.json
curl.exe -s -o NUL -w "%{http_code}\n" -X POST -H "X-Custom-ssid: test" -H "X-Custom-pwd: $('p'*65)" http://10.10.0.1/connect.json
curl.exe -s -o NUL -w "%{http_code}\n" -X POST -H "X-Custom-ssid: test" http://10.10.0.1/connect.json
```

  Expected, in order: `302` (a Host over 63 characters is read as empty and redirected), `431` (httpd's 1,536 B header limit), `200` (no Host gets the page), then `400` four times (no headers; a 33-character SSID; a 65-character password; no password, as an open network still gets until C8). Never send a valid SSID and password here: that starts a connect. Pass: these codes and no reboot.

**5. G8 (plan §12), with the WP1 additions** (valve unpowered, then linked):
1. Hub connected, router on. Router off: `WiFi Disconnected. Reason: …`, `IOTHUB: cloud admission withdrawn (WiFi down)` and `IOTHUB: WiFi down — stopping MQTT client (…)`, the component's 3 retries, then `SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`.
2. A phone joins `WiFi-Hub-69C8` and opens the page.
3. Router on: `router fallback: retrying the configured network (attempt N)`, `Connected! IP`: the AP tail starts, `SoftAP tail after an automatic rejoin (stations on it: 1) - it stops 20 s after the IP, or 10 s after the last station leaves`, and `IOTHUB: cloud admission deferred: SoftAP up - no TLS or DPS until it stops`. Record the `idma:` lines from the IP on (15g).
4. **Within 10 s of `Connected! IP` (the tail lasts 20 s with the phone on the SoftAP, 10 s after it leaves), pull the router again.** The SoftAP stays up with the STA lost. Expect `WiFi Disconnected. Reason: …` and no IOTHUB withdrawal line (nothing was admitted), no `SoftAP stopped` line, then only `router fallback: retrying …` every 33-36 s (`retry deferred - the Wi-Fi setup page is open` while it polls, 5 min at most). No component retry (C5): no `Reason:` lines about 10 s apart (its `Retry Timer Tick!` is under the WARN cap, so check by timing).
5. Keep the page open 5 min and Submit ×10 (the router's correct password, at least 10 s apart; with the router off each ends "Connection failed"). At least 5 of them deliberately 0-3 s after a `router fallback: retrying …` line: expect `wifi_manager: ORDER_CONNECT_STA: esp_wifi_set_config failed (ESP_ERR_WIFI_STATE) - attempt not started` (or `esp_wifi_connect failed`), `APP_WIFI: WiFi Disconnected. Reason: 205`, and the page showing the failure at once. `Wi-Fi setup page: Connect sent - …` beside it means that no hold took effect for that Submit (15g). Do not tap Disconnect in that window (15h risk 1).
6. Router on: `Connected! IP`, the automatic tail as in step 3, `SoftAP stopped (its servers too) N s after the IP` at most 20 s later (10 s after the phone's `SoftAP: station … left`), then `IOTHUB: cloud admitted N s after the IP (…)`, `IOTHUB: WiFi up — restarting MQTT client` and the cloud back (the first IoT Hub message). Record the `idma:` lines from the IP to 30 s after `Connected to Azure IoT Hub!`.
7. Once more steps 1-3 and 6, with the E2 laptop (it remembers the SSID) on the SoftAP when the router comes back: it never leaves, so the SoftAP stops 20 s after the IP.

Pass: **0 reboots**; no automatic attempt (`router fallback: retrying …`) less than 30 s after the previous attempt's start or end, Submits included; each Submit's page result matches the log. On this image (WP2) the cloud starts only after the SoftAP's stop: `allocfail` rising in a tail, a `DPS:` line or `Connected to Azure IoT Hub!` before `cloud admitted …`, or MQTT/DPS socket or connect errors in a tail are findings (they were expected on a WP1-only image, 15g). A reboot in the tail is a finding, whatever its backtrace.

**6. A leak on this image** (P11 and P14 never ran on 2.1.3; WP1 and WP2 do not touch the leak path, but WP2 moves the MQTT stop onto `iothub_task`, 15i). With the valve linked and Wi-Fi connected, wet one BLE sensor: `eleak … leak=1`, RMLEAK then CLOSE, `leak_detected` and `auto_close` in the IoT Hub capture; dry it: `rmleak_auto_cleared` about 10 s later. Note the time from `leak=1` to the valve's CLOSE: it is item 7's baseline. If practical, once more with the wetting inside a G8 tail (item 5 step 6). Any difference from CP5 is a finding.

**7. G3-lite: the E4 replay (WP2; plan §12's G3 without its E2 half).** On `GW-7C4FADAE69C8` with its usual sensors, sensors dry, the valve unpowered, then linked. **Pass:** **0 failed allocations** (`allocfail` unchanged from before the router pull to 60 s after `Connected to Azure IoT Hub!`); with 0 stations on the SoftAP, **AP down ≤ 1 s after the IP and MQTT ≤ 5 s after it**; the **`lifecycle`** (`"event":"online"`) reaches IoT Hub after every rejoin; no TLS beside the SoftAP (no `DPS:` line, `WiFi up — restarting MQTT client` or `Connected to Azure IoT Hub!` between `Connected! IP` and `cloud admitted …`); 0 escape lines; no reboot.
1. **0 stations, 3 times.** No phone or laptop joined to `WiFi-Hub-69C8` (forget it on each, or switch their Wi-Fi off). Router off: `WiFi Disconnected. Reason: …`, `IOTHUB: cloud admission withdrawn (WiFi down)`, `IOTHUB: WiFi down — stopping MQTT client (free TLS heap for AP/captive portal)`, `TELEMETRY_V2: MQTT connected = false`; the 3 retries; about 30 s after the loss, `SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`. Wait 2 min, router on. Expect, timed from `APP_WIFI: Connected! IP: …`: `SoftAP tail after an automatic rejoin (no station on it) - it stops 0.5 s after the IP`, `IOTHUB: cloud admission deferred: SoftAP up - no TLS or DPS until it stops`, the driver's `wifi:mode : sta (…)` at about 0.5-0.7 s (**AP down**; 15i "For the user's decision" item 4), `SoftAP stopped (its servers too) N s after the IP` (about 0.6-1.3 s), `IOTHUB: cloud admitted N s after the IP (internal DMA free X B, largest Y B)` (about 1.5-3 s; X about 43-50 KB, Y about 30 KB or more), `IOTHUB: WiFi up — restarting MQTT client`, `IOTHUB: Connected to Azure IoT Hub!` (**MQTT**, about 3-5 s), the drain of the events held during the outage, then the `lifecycle` and a snapshot in IoT Hub. If the driver prints no mode line there, record `SoftAP stopped` as the upper bound. Record IP → AP down → `SoftAP stopped` → `cloud admitted` → `Connected to Azure IoT Hub!` separately, so that a miss can be placed.
2. **With a phone (the E4 replay proper), twice.** As G0 run A (15d): a phone joins the fallback SoftAP during the outage and uses the page; router on with the phone still joined. Expect `SoftAP tail after an automatic rejoin (stations on it: 1) - it stops 20 s after the IP, or 10 s after the last station leaves`; if the phone leaves, `SoftAP tail: no station left on it - it stops N s after the IP`; `SoftAP stopped (its servers too) …` at most 20 s after the IP, and `cloud admitted …` 0.5-1.5 s after that line (measured from it, not from the IP: item 4 of 15i's decisions), then MQTT and the `lifecycle`.
3. **ADM-3** (15i). At every router pull, record the time from `IOTHUB: WiFi down — stopping MQTT client …` to `TELEMETRY_V2: MQTT connected = false` (the stall of `iothub_task`): about 1-5 s is expected; report the largest. Once, with the valve linked, wet a BLE sensor at the moment of the pull and record `eleak … leak=1` to the valve's CLOSE against item 6's baseline. Once, if practical, unplug the router's WAN with its Wi-Fi still up for 1 min first (esp-mqtt then has a connect in flight: the worst case), then switch its Wi-Fi off, and record the stall.

Record at every rejoin: the `idma:` lines from the pull to 60 s after `Connected to Azure IoT Hub!` (`min`, `min_largest`, `min_ever`, `allocfail`) against G3's floor of 8 KB / 4.5 KB (plan §8; recorded, and a miss is a finding for WP3 and WP9), and the steady `largest` once connected again against a normal boot's (CP5: 18,432 B after a normal boot, stuck at 6,400 B after a TLS start beside the SoftAP; within about 2 KB of a normal boot is the expectation). Findings: `cloud admission deferred: internal DMA free …` right after `SoftAP stopped` (the 500 ms settle may be short, 15i), an escape line (`below the heap gate`, `whatever the heap`), `cloud admission: the SoftAP's stop still not finished after 10 s …`, `ORDER_STOP_AP: esp_wifi_set_mode failed …` with no further STOP_AP within about 5 s, `Subscribe refused (…) - reconnecting` repeating for more than 30 s, any `Outbox full - … not kept`. A second STOP_AP with no second `SoftAP stopped` line is expected (a pass).

**8. First commissioning and the DPS router-pull test (WP2; plan §12's regression row "first commissioning with DPS (router pulled mid-DPS)").** On the spare hub only: never on `GW-7C4FADAE69C8`, whose provisioning the bench keeps.
- **(a) First commissioning.** `idf.py -p <spare port> erase-flash`, then flash CP6 to it (the production tool's starting state; like T4-06's alternative start, this item erases on purpose, and only the spare hub), and set Wi-Fi up from a phone as in item 1. After `Connected! IP`: the setup-page tail line, `IOTHUB: cloud admission deferred: SoftAP up - no TLS or DPS until it stops`, SNTP (`Initializing SNTP...`, `Time synced: …`) beside the SoftAP, `SoftAP stopped (its servers too) …` (15 s after the phone leaves, at most 60 s after the IP), `IOTHUB: cloud admitted …`, `DPS: No cached assignment, performing DPS registration...`, `DPS: Connecting to global.azure-devices-provisioning.net...`, `DPS: Assigned hub=… device=…`, `DPS: Cached assignment in NVS (provisioning epoch …)`, `IOTHUB: Connected to Azure IoT Hub!`; in IoT Hub (the spare's gateway ID) the `lifecycle` with `"provisioned":false` and the empty `boot` snapshot. **Pass:** no `DPS:` line before `cloud admitted`; `allocfail` unchanged; no reboot.
- **(b) The router pulled mid-DPS.** Make the next registration live again: send the spare hub test plan T4-06 step 2's `decommission` `all` (with a new `id`): it clears the DPS cache and reboots, Wi-Fi kept. So that the registration lasts long enough to interrupt, first set T4-06's DPS block on the router (TCP 8883 to `global.azure-devices-provisioning.net` dropped, or its DNS name blocked; the IoT Hub host left reachable): the registration then waits up to 60 s. After the reboot (normal boot, no SoftAP): `cloud admitted 0.N s after the IP`, `DPS: No cached assignment, performing DPS registration...`, `DPS: Connecting to …`. About 5 s later **switch the router's Wi-Fi off** (not its power, so the block can be removed while it is off). Expect `WiFi Disconnected. Reason: …`, then within about 1-4 s (up to about 10 s if the DPS client was still connecting) W `DPS: DPS registration aborted after N s: Wi-Fi lost or SoftAP up - tried again once the cloud is admitted again` (N counts from `Connecting to`), then `IOTHUB: cloud admission withdrawn (WiFi down)`; **no** `IOTHUB: DPS failed (attempt n/…), retry in …` for it. The fallback SoftAP comes up about 30 s after the loss, with no `DPS:` line while it is up. Remove the block, switch the Wi-Fi on: the automatic tail (0.5 s), `cloud admitted …`, and `DPS: No cached assignment, performing DPS registration...` on the next pass (no back-off), then as (a). **Pass:** the abort line and no back-off line; the registration runs again at once after the admission and succeeds; no DPS TLS while the SoftAP is up; no reboot. An E `DPS registration failed (…)` with a back-off line after the pull is a finding.

**9. P-14, the home-LAN part (C3; WP2).** From the PC on the home LAN, with the hub's `<LAN IP>` from `Connected! IP: …`. **Pass:** in a tail, `403` to each request below, nothing done (the hub logs nothing for them at INFO) and the credentials intact (no `WiFi Disconnected. Reason: 8`, and after a power cycle the hub rejoins its router); after the SoftAP's stop, `000` (connection refused); no reboot.
- **In a tail.** Use a setup-page Connect (item 1's smoke, or a 10 s reset and set-up) with the phone left on `WiFi-Hub-69C8`, so that the SoftAP stays up 60 s after the IP. Within that minute, in PowerShell:

```powershell
$hub = '<LAN IP>'
curl.exe -s -o NUL -w "%{http_code}\n" "http://$hub/"
curl.exe -s -o NUL -w "%{http_code}\n" -X POST "http://$hub/connect.json"
curl.exe -s -o NUL -w "%{http_code}\n" -X DELETE "http://$hub/connect.json"
```

  Expected `403` three times. The POST carries no headers on purpose: one that got past the check would read `400`, with nothing started. The DELETE is the forget: a `200` means the hub forgot its Wi-Fi (a finding; set it up again). In the same minute the phone on the SoftAP still loads `http://10.10.0.1/`.
- **After the stop** (`SoftAP stopped (its servers too)`): the same three commands print `000` (nothing listens on port 80 with the SoftAP down).
- **Optional, a route through the hub** (an administrator PowerShell; remove it afterwards): `route add 10.10.0.0 mask 255.255.255.0 <LAN IP>`; then in a tail `curl.exe -s -o NUL -w "%{http_code}\n" http://10.10.0.1/` → `403`, and `nslookup captive.apple.com 10.10.0.1` → `DNS request timed out` (no answer: the DNS drops senders outside 10.10.0.0/24); then `route delete 10.10.0.0`.

**Send back:** each run's UART file and IoT Hub capture (the spare hub's too), the phone notes, the `dig` and probe-script outputs, the `curl.exe` codes and `nslookup` output, the `/ap.json` and `status.json` files of item 4b, the tails' `idma:` lines, and item 7's timings (IP → AP down → `SoftAP stopped` → `cloud admitted` → MQTT; the stop's stall at each pull; leak to CLOSE).

### 15l. Run next (in order; continues §15f once G0 is done)

0. Finish G0 on CP5 (15f items 1-4) and save its material: CP6's `fullclean` deletes the CP5 build.
1. 🔨 **Build checkpoint 6 of `0aae305`** (WP1 and WP2, 15j), VAL-01 steps 5-6, then flash.
2. 15k items 1 and 2: the portal smoke on an iPhone and an Android, and the 10 s reset ×10 (its first run can open the smoke). Item 9's LAN check fits in a smoke run's 60 s tail.
3. 15k items 3 and 4: P-13 and G-FAULT's WP1 subset, in the 10 s-reset portal or on the spare hub.
4. 15k item 5: G8, valve unpowered, then linked; once with the E2 laptop.
5. 15k item 6: a leak with the valve linked (item 7's baseline).
6. 15k item 7: G3-lite, the E4 replay with 0 stations and with a phone, valve unpowered, then linked; ADM-3's stall at every pull.
7. 15k item 8: first commissioning and the DPS router-pull test, on the spare hub.
8. 15k item 9: P-14's LAN part, if it did not run in step 2.
9. Send the material (15j, 15k). Then the user's calls (15i "For the user's decision", and 15h "Process" items 2-3), and WP3 (plan §11).
