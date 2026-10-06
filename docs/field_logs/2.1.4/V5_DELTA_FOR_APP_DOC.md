# V5 delta: app- and cloud-visible hub changes after 545b8f2

> **In the repo since 2026-10-07 (WP10, `HANDOFF.md` 15w).** Copied from the V5-delta stage's list
> (`…\scratchpad\finish3\v5delta\V5_DELTA.md`) for the person who updates the Watts Digital app-requirements
> document V5. Three things changed after it was written:
> - **CP7 is now of `b651701`**, not `52ef6a2`. Phase 3 added seven firmware commits (`d6c8b69` … `b651701`, `HANDOFF.md`
>   15w): the pacing of the setup page's Connect and a phone's join (radio timing inside the hub) and three fixes
>   that act only after 248.5 days of uptime. **None of them changes D1-D6 or N1-N7** below; nothing they change is
>   visible to the app or the cloud except as setup-page timing, which V5 does not describe (a Connect can get its
>   radio pulse several seconds later, or run beside BLE; the page's 30 s Connect timeout is unchanged). WP9 (the memory set) is
>   staged and off, and changes no image.
> - **WB-CLOUD-1 (the whole-branch review, open):** the order "cmd_ack, twin report, snapshot" below (D1, P593) and
>   in `C2D_COMMANDS.md` §3.5 holds only while MQTT accepts the cmd_ack. The cmd_ack is published directly from the
>   command handler; one that MQTT refuses (its outbox full) is kept and sent again within about 10 s, after the
>   command's twin report and snapshot have gone out. V5 already tells the app not to depend on the order of the
>   twin report and the snapshot (P542); its text on the cmd_ack's order should carry the same caveat until the
>   firmware or the contract changes (`HANDOFF.md` 15x).
> - **Bench status:** still none. Apply these once CP7 is benched, or mark them provisional.

**What this is.** The Watts Digital app-requirements document V5
(`C:\Work\Projects\EfloStop 2\Documents\eFloStop_2_App_Requirements_Watts_Digital_Update-V5.docx`) describes hub
firmware 2.1.4 as built at `545b8f2` (CP6). This file lists every behaviour that the app or the cloud can see and
that phases 1-3 changed after that commit. For each one it gives the V5 places it affects and the corrected
statement. It edits nothing: V5, its builder (`docs/telemetry/build_app_requirements_v5.py`) and `canon.md` are
unchanged.

**Scope and method.**
- Diff `545b8f2..HEAD` over `main/` and `components/`. HEAD was `d0d2bd3` when this was written. The last firmware
  commit is `52ef6a2`, which was then the CP7 image (HANDOFF section 15t; CP7 is now `b651701`). That is 116 commits that touch `main/` or `components/`: phase 1 (WP3 core, WP4, WP3 valve,
  WP5, WP6; HANDOFF 15q-15t) and phase 2 (WP7, WP8; 15u). **Phase 3 (WP9 and the white-box reviews) had no commit
  when this was checked at the end of this pass** (it has since: see the note at the top). WP9 is the memory set (plan section 8), and none of its lines is cloud-visible
  by design. Any phase-3 firmware commit must be checked against this list before V5 is reissued.
- Every claim below was read in the code at HEAD and compared with `545b8f2`, and with `master` (2.1.3) for the
  version notes. Code references are `path:line` at HEAD.
- V5 text source: `…\scratchpad\appreq\final_check\after_dump.txt`. The `appreq\fix\v5_dump.txt` named in the task
  does not exist. Paragraph and table IDs below (P###, T##/R#) are that dump's. Canon terms (`canon.md` C2) and the
  "since 2.1.4" rule (C1) are used throughout: "since 2.1.4" here means the 2.1.4 release as it now stands,
  against 2.1.3. In every case below, 2.1.3 behaved as V5 now describes 545b8f2.
- **Status: no bench data exists for any of this.** These are CP7 facts from code review. Apply them to V5 once
  CP7 has been benched, or mark them provisional. Timings marked (model) come from host models, not measurements.

## Summary

| # | Change (phase, commits) | Kind | V5 places |
|---|---|---|---|
| D1 | The twin report and the lifecycle never carry placeholder values or leave the rules out. A busy device list delays them by about 5 s. (WP3: `505eddc`, `1762a65`, `fdb8c28`, `8c2a997`, `ea1cbb4`, `c6ca926`, `afa5c10`, `6bfee44`, `c4e1511`) | **V5 now wrong** | 4.8.1 P211; 5.2.1 T24 R3, R7; 5.3 P542; 5.4.1 T34 R10, R11, P593, P599; 5.2 P256 / 5.2.1 P258 (order, rare case) |
| D2 | override_enable answers "No active leak to override..." right after the hub's own RMLEAK clear, while the valve still reads RMLEAK set. (WP3: `71e6d45`, `39ad9c4`, `f649193`) | **V5 incomplete; app must act** | 4.4.3 P183, T10 R1 (APP-FR-043); 5.2.7 T31 R8; 5.3 T33 R11; 10.2 T45 R7 (TC-N07) |
| D3 | Control characters in labels, the hub name and the valve firmware string are sent as spaces in telemetry. Twin reported hub_name keeps the exact value. (WP3: `bef24b3`) | **V5 incomplete; app input rule** | 2.5 T4 R5; 3.3 P104; 4.6 T12 R3 (APP-FR-068); 5.2 T23 R7; 5.2.2 T26 R3, R13, R23; 5.2.3 T27 R7; 5.2.4 P399; 5.2.5 T28 R1; 5.4.1 T34 R4; 5.4.2 T35 R2; A.1 P692 |
| D4 | Leak protection keeps running while the setup network is open, including after the 10-second Wi-Fi reset. BLE leak sensors and the valve are watched there, and the old BLE pause and its health hold are gone. (WP8 D2: `dd19d6b` and the radio_policy commits) | **V5 incomplete (silent by canon C21)** | 1.4 P46; 2.6 T5 R6; 4.4 P172; 5.5 P606; 9 T43 R2 |
| D5 | The setup network can close 5 s after the join: the setup page's Finish stops it 2 s after the tap. (WP4 C12: `e7a7c9a`) | **V5 now wrong (lower bound)** | 2.6 T5 R6; 4.2 T7 R4 (APP-FR-007); 5.5 P606; 6.4 T36 R5; 9 T43 R2; 1.4 P46 (help text) |
| D6 | The router retry now comes 30 to 35 s after the last attempt (plus jitter), and waits up to 10 s while a phone joins the setup network. A failed Connect on the setup page keeps the working network. (WP4 C8: `7dc35ff`; WP8: `13ae5b2`, `aef31c0`) | Optional precision, help text | 2.6 T5 R6; 1.4 P46 |
| N1-N7 | Changes with no V5 wording change: valve claims and their back-off, the snapshot build and size bound, membership-checked leak decisions, kept valve reports, the health hold, the MQTT stop retry, the setup page itself | Notes | none required (optional refinements given) |

Unchanged and verified: all 27 cmd_ack error.detail texts in 5.2.7 T31 (each found verbatim at HEAD), the RMLEAK
automatic clear (10 s, `clear_after_seconds` 10), the trigger-mask rules, the leak and rules event shapes, the
health-event rules (C6), the snapshot reasons (C9), the offline buffer (C13) and the C2D handling (C15, C19).

---

## D1. Twin report and lifecycle: no placeholder values, rules always present, about 5 s later when busy

**Code.** `provisioning_get_summary()` (`main/provisioning_manager/provisioning_manager.c:969`) reads provisioned,
valve_id, both counts and the rules in one hold of the provisioning mutex (1 s timeout). For 2 s after a timeout it
answers "busy" at once. `build_twin_reported()` (`main/iothub/app_iothub.c:1789`) and
`telemetry_v2_build_lifecycle()` (`main/telemetry/telemetry_v2.c:938`) build nothing when it is busy, and they
always add `auto_close_enabled` and `trigger_mask` otherwise (`app_iothub.c:1836-1837`, `telemetry_v2.c:975-979`).
The twin report is owed and built again after `TWIN_REPORT_RETRY_MS` 5 s (`app_iothub.c:1774`, `:1929-1962`). The
lifecycle is built again after `LIFECYCLE_RETRY_MS` 5 s (`app_iothub.c:131`, `:4423-4431`). A device-set change's snapshot
waits once for its delayed twin, so the order stays cmd_ack, twin, snapshot (`app_iothub.c:1758-1770`, `:4826`)
(while MQTT accepts the cmd_ack: WB-CLOUD-1, the note at the top).
On a connect, cloud_tx replays the kept events first. It then waits at most 1 s for the lifecycle, and after that
live messages go first (`app_iothub.c:3498-3511`, `:3614-3623`).

**545b8f2 and 2.1.3.** Each value was read with its own 1 s timeout. A busy read went out as provisioned false,
no valve_id (twin: null), counts 0, and no rules.

**What the cloud sees now.** A lifecycle or twin report never carries those placeholder values and always carries
the rules. When the hub's device list stays busy for over 1 s, which is rare, the report comes about 5 s later. The
snapshot is unchanged: it can still leave out `data.rules` (and read `override_active` false once) when the hub is
busy, as V5 already says (T26 R26, R27; T43 R21).

| V5 place | V5 now says | Corrected statement |
|---|---|---|
| 5.2.1 T24 R3 (`data.provisioned`), last sentence | "Rarely, when the hub is busy for over 1 s, one lifecycle reads provisioned false with no valve_id and counts of 0; the next snapshot is authoritative." | "Since 2.1.4 the hub never sends a lifecycle with placeholder values. When its device list is busy for over 1 s, it builds the lifecycle again about 5 s later. A 2.1.3 hub can, rarely, send one lifecycle that reads provisioned false with no valve_id and counts of 0. The next snapshot is authoritative." |
| 5.2.1 T24 R7 (`data.rules.auto_close_enabled`) | "The rules object is in every lifecycle unless the hub could not read its rules within 1 s: then keep the last known values." | "Since 2.1.4 the rules object is in every lifecycle. A 2.1.3 hub leaves it out when it cannot read its rules within 1 s: then keep the last known values." (The rest of the cell stays.) |
| 4.8.1 P211 | "...each next to auto_close_enabled: snapshot data.rules.trigger_mask, lifecycle data.rules.trigger_mask, and twin reported trigger_mask ... If the hub cannot read its rules within 1 s, it leaves them out of that message: keep the last known values." | Keep the first sentence. Replace the second with: "If the hub cannot read its rules within 1 s, it leaves data.rules out of that snapshot: keep the last known values. Since 2.1.4 a lifecycle and a twin report always carry them: when the hub is that busy it builds them again about 5 s later instead. A 2.1.3 hub leaves the rules out of those too." |
| 5.4.1 T34 R10 (`auto_close_enabled`) | "Left out of a report when the hub cannot read its rules within 1 s; the twin then keeps the previous value." | "Auto-close master switch (4.8.1). Since 2.1.4 it is in every report. A 2.1.3 hub leaves it out of a report when it cannot read its rules within 1 s; the twin then keeps the previous value." |
| 5.4.1 T34 R11 (`trigger_mask`) | "Left out in the same case as auto_close_enabled." | "Auto-close trigger bits, default 7 (4.8.1). Present whenever auto_close_enabled is." |
| 5.4.1 P593 | "If the hub's device list is busy for more than 1 s while a report is built, that one report can read provisioned false, valve_id null and both counts 0; the next report corrects it. Do not remove devices..." | "Since 2.1.4, when the hub's device list is busy for more than 1 s, the hub builds no report and builds it again about 5 s later, so a report never carries placeholder values. The snapshot that a provision or decommission causes waits for that report, so the order stays cmd_ack, twin report, snapshot. A 2.1.3 hub can, rarely, send one report that reads provisioned false, valve_id null and both counts 0 and has no auto_close_enabled or trigger_mask; its next report corrects it. Do not remove devices from the backend registry on a twin report alone: use the decommission cmd_ack and the snapshot." |
| 5.4.1 P599 | "The report normally follows within a second, but can take several seconds while the hub has many messages waiting to be sent." | "...but can take several seconds while the hub has many messages waiting to be sent, and about 5 s when its device list is busy." |
| 5.3 P542, last sentence | "Do not depend on the order of the twin report and the snapshot: the twin report waits while 8 or more messages are queued on the hub." | "...the twin report waits while 8 or more messages are queued on the hub, and about 5 s when the hub's device list is busy." |
| 5.2 P256 (Order) and 5.2.1 P258 | "After a reconnect, the events the hub kept while offline go out oldest first, before that connection's lifecycle; new messages follow it." | Add: "Rarely, when the hub cannot build its lifecycle within 1 s of the connect (its device list busy), new messages go out after the kept events and the lifecycle follows about 5 s later." (Note, not for V5: the 1 s hold and this fallback existed at 545b8f2 for an out-of-memory build. A busy device list now takes it too, where it used to send placeholder values.) |

No change needed: 7.1 P664 ("sometimes a few seconds later"), A.5 T51 R5 ("normally within a second"), 9 T43 R21
(snapshot-only), 5.2.2 T26 R26 and R27 (snapshot behaviour unchanged).

---

## D2. override_enable right after the hub's own RMLEAK clear: "No active leak to override"

**Code.** `rules_engine_enable_override_remote()` (`main/rules_engine/rules_engine.c:1584`). After the existing
checks, it checks again under the rules lock (`:1642-1654`): no window, no incident latched, and not "valve RMLEAK
reads 1 with no clear of the hub's own still owed". When the hub has just cleared RMLEAK itself (the automatic
clear, an accepted leak_reset), it owes that clear to the valve (`g_rmleak_clear_owed`) until the valve reports
RMLEAK 0. Until then the valve's cached RMLEAK 1 no longer counts as something to override.

**545b8f2 and 2.1.3.** Precondition 3 was checked only before the lock, on the valve's cached RMLEAK (2.1.3
`master:main/rules_engine/rules_engine.c:1003-1013`). An override_enable that arrived after the automatic clear or a
leak_reset, while the valve still read RMLEAK 1, therefore went through. The hub started a 24-hour window, which
pauses automatic shutoff, and opened the valve, with no leak left to override.

**What the app sees now.** In that window (from rmleak_auto_cleared or the leak_reset cmd_ack until the valve
confirms, typically a few seconds, and longer while the valve is disconnected) override_enable gets cmd_ack
"error" with "No active leak to override. Use the normal Open Valve control." This is a refusal, so the water stays
off. The app can still be showing "Open with 24h Override" then, because APP-FR-043 shows it while valve.rmleak is
true, and a disconnected valve keeps its last known rmleak true (APP-FR-038).

One rare stuck case (HANDOFF 15q residual 5): if the hub's clear never lands while the valve keeps reporting
RMLEAK 1, valve_open is refused with the RMLEAK text and override_enable with "No active leak to override...".
A leak_reset sends the clear again, and the RMLEAK text already tells the user to send it.

| V5 place | Corrected statement |
|---|---|
| 5.2.7 T31 R8 ("When" cell) | "No leak incident latched, no valve RMLEAK and no window open. Since 2.1.4 also right after the hub has cleared RMLEAK itself (rmleak_auto_cleared, or an accepted leak_reset), until the valve confirms the clear, even while the valve still reports RMLEAK set (a 2.1.3 hub started a 24-hour window then)." |
| 4.4.3 P183 | After "...when there is nothing to override": add "(since 2.1.4 this includes the moments after the hub's own RMLEAK clear, rmleak_auto_cleared or an accepted leak_reset, before the valve has confirmed it: the incident is over, and the user opens the valve with Open Valve)". |
| 4.4.3 T10 R1 (APP-FR-043), requirement and acceptance | Add: "Once rmleak_auto_cleared or rmleak_cleared has arrived, or a leak_reset has been acked "ok", newer than the latest snapshot, the app shall hide "Open with 24h Override" and offer Open Valve, even while the last known valve.rmleak is still true: the hub then refuses override_enable with "No active leak to override. Use the normal Open Valve control." (since 2.1.4)." Acceptance: add "hidden after rmleak_auto_cleared or rmleak_cleared". |
| 5.3 T33 R11 (override_enable error scenarios) | "...there is no active leak to override (no leak incident latched, no valve RMLEAK and no window open; since 2.1.4 also right after the hub's own RMLEAK clear, until the valve confirms it)..." |
| 10.2 T45 R7 (TC-N07), optional new step | "(d) Let the hub clear RMLEAK by itself (TC-016) and send override_enable within 2 s of rmleak_auto_cleared: cmd_ack error "No active leak to override. Use the normal Open Valve control."; no water_access_override_enabled; the valve does not move." |
| "Changes in V5" P22 (Leak handling), optional | Append: "After rmleak_auto_cleared or rmleak_cleared, hide "Open with 24h Override" and offer Open Valve (APP-FR-043)." |

`canon.md` C12's row for this text needs the same addition.

---

## D3. Control characters print as spaces in telemetry

**Code.** `telemetry_v2_printable()` (`main/telemetry/telemetry_v2.c:124-130`) turns every byte below 0x20 into a
space. It runs on the copy printed for: gateway.name in every message's envelope (`:183-185`); every
`location.label` (`add_location_obj()`, `:478`: snapshot sensor entries, and leak events through
`add_location_for_source()`, `:493`, `:1489`); the sensor name in
system_health.reason "Leak detected: <name>" (`leak_label_for()`, `:565`; its existing comma-to-semicolon swap is
unchanged); the auto_close event's location label (`rules_engine.c:737`); and the valve's fw_version in the
snapshot, valve_state_changed and valve leak events (`:1138`, `:1459`, `:1502`). Twin reported `hub_name` is
written exactly as stored (`app_iothub.c:1806-1810`). Nothing is rejected at intake (`sensor_meta.c` and
`hub_identity.c` are unchanged), so the stored value keeps the control characters. DEL (0x7F) and UTF-8 bytes
(0x80 and above) are not touched.

**545b8f2 and 2.1.3.** They were sent as six-byte `\u00XX` escapes (valid JSON). At 545b8f2, labels full of them
could push a full hub's snapshot to about 13.9 KB, over the 12,288 B MQTT outbox limit. The hub could then never
send a snapshot. They could also push an event over the offline buffer's 512 B entry.

**What the cloud sees now.** A label "Under\tsink" comes back as "Under sink" in every telemetry message. A hub
name with a line break shows a space in gateway.name and the exact value in twin reported hub_name. So a label
or name the app sent with a control character never reads back equal in telemetry.

| V5 place | Corrected statement |
|---|---|
| 4.6 T12 R3 (APP-FR-068), requirement | Add: "The app shall not send control characters (U+0000 to U+001F, such as a tab or a line break) in a label: strip them before sending. Since 2.1.4 every message prints each one as a space (a 2.1.3 hub sends them escaped), so a label that held one never reads back as sent." |
| 3.3 P104 and A.1 P692 | Add to the 31-byte rule: "and no control characters (since 2.1.4 the hub sends each as a space)". |
| 5.2.2 T26 R23 (`location.label`) | Add: "Since 2.1.4 each control character (U+0000 to U+001F) in a stored label is sent as a space, here and in every message that carries the label (leak events, auto_close, system_health.reason)." |
| 5.2.2 T26 R3 (`system_health.reason`) | After "(the sensor's label, else its sensor_id, or "valve")": add "(a comma in the label is sent as ";" and, since 2.1.4, a control character as a space)". The comma rule is older, but V5 does not state it. |
| 5.2 T23 R7 (`gateway.name`), 2.5 T4 R5, 5.4.2 T35 R2 | Add: "Since 2.1.4 a control character in the name is sent as a space in gateway.name; twin reported hub_name keeps the exact value. The backend shall not write control characters into desired.hub_name." |
| 5.4.1 T34 R4 (`hub_name`) | Add: "Exactly as stored, control characters included (gateway.name prints them as spaces since 2.1.4)." |
| 5.2.2 T26 R13, 5.2.3 T27 R7, 5.2.4 P399 (valve `fw_version`) | Add: "As the valve reports it, with any control character sent as a space (since 2.1.4)." Valve firmware strings are plain in practice: low priority. |
| 5.2.5 T28 R1 (auto_close location) | Covered by the T26 R23 sentence ("every message that carries the label"); no separate change. |

---

## D4. Leak protection while the setup network is open (WP8 decision D2)

**Code.** `dd19d6b` deleted the portal priority window, every Wi-Fi radio hold, the page chain and the health
engine's BLE hold (`health_set_ble_scan_paused()`, `health_note_valve_leak_hunt()`). `health_engine.{c,h}` are now
byte-identical to `5b5d70e~1`. `radio_policy` now shares the radio (`main/radio_policy/radio_policy.h:176-187`).
With the setup network up and the STA not connected, it runs AP_IDLE (Coded 0.6 s / Wi-Fi 0.3 s), SERVE while a
phone uses the page, or LR_AP during a leak response. BLE stops only for bounded pulses of at most 2.8 s each and
12 s per minute. The valve is hunted and claimed there too. Log anchor: `APP_WIFI: SoftAP up with no saved Wi-Fi
credentials (setup portal) - BLE scanning, if any, stays on beside it` (`main/app_wifi/app_wifi.c:404`). The leak
path itself (iothub_task and the rules engine) has run without Wi-Fi since 2.1.4 (canon C21), and is unchanged.

**545b8f2.** While the no-credentials setup network was up (after the 10-second reset), BLE scanning and the valve
hunt were paused, except for a pended leak response. BLE leak sensors were not heard, so a BLE leak did not close
the valve. The health engine held those sensors online and the valve in its grace. V5 says nothing about this
(canon C21 kept it out pending WP8).

**2.1.3.** The 10-second reset restarts the hub, and a 2.1.3 hub that restarts while Wi-Fi is down does not act
on leaks until it is back on Wi-Fi and its cloud start-up has run (V5 already says this for router outages).

**What the cloud sees now.** After a 10-second reset, the hub keeps its devices and keeps protecting. A leak
during setup sends leak_detected and auto_close and closes the valve when auto-close is on. Those events, and any
health events raised meanwhile, are kept (up to 16) and arrive after the hub joins, before its lifecycle,
with their real ts (C13; the pre-sync rules apply if the clock is not set). Because the devices are heard during
setup, the first snapshot after a long setup session shows them as they are. V5's "Starting up" windows (10 min
for a sensor, 3 min for the valve, counted from the restart) now hold exactly; at 545b8f2 the pause stretched
them, which V5 did not say.

| V5 place | Corrected statement |
|---|---|
| 2.6 T5 R6 ("Wi-Fi setup" row) | After "It keeps its devices and settings.": add "Since 2.1.4 it also keeps watching its leak sensors and its valve while the setup network is up, after the 10-second reset too: it closes the valve on a leak when auto-close is on, and keeps up to 16 events to send once it is online. A 2.1.3 hub that restarted, as the 10-second reset does, does not act on leaks until it is back on Wi-Fi and its cloud start-up has run." |
| 5.5 P606 | Append the same two sentences. |
| 4.4 P172 | "...Since 2.1.4 this also holds when the hub restarts while the router is down **and while its Wi-Fi setup network is open, after the 10-second reset too**, and events raised before the hub's clock is set..." |
| 9 T43 R2 ("Hub in Wi-Fi setup"), Expected | Add: "Since 2.1.4 the hub still detects leaks and closes the valve meanwhile (auto-close on), and its events arrive after it joins, before its lifecycle, with their original ts. Keep showing the last known state; a leak in it keeps the Leak Detected banner (APP-FR-021)." |
| 1.4 P46 (help text) | "...the hub keeps its devices and settings **and keeps watching for leaks**." |
| 10.1 T44, optional new TC-023 "Leak during Wi-Fi setup" | Steps: on a hub with a valve and a BLE leak sensor, hold the reset button 10 s; do not set up Wi-Fi; wet the sensor; after 1 minute dry it; then set up Wi-Fi from the setup page. Expected: the valve closes at once (check at the valve). After the hub joins, leak_detected, auto_close and leak_cleared arrive before the lifecycle with their original ts; snapshots follow (2.1.3: no close until Wi-Fi and cloud start-up). |

`canon.md` C21's last paragraph ("V5 says nothing else about BLE scanning during Wi-Fi setup ... WP8 ... changes
it") is superseded: WP8 is built. Rephrase it as the T5 R6 wording above. Do not describe scan rows, pulses or
duty cycles in V5: they are provisional until bench gate G1.

---

## D5. The setup network can close 5 s after the join (Finish)

**Code.** `POST /finish.json` (the setup page's Finish button, shown once the hub has joined) calls
`portal_finish()` (`main/app_wifi/app_wifi.c:305-324`). The setup network then stops at
max(join + 5 s, tap + 2 s) (`AP_TAIL_FINISH_MS` 2,000 and `AP_TAIL_FINISH_MIN_MS` 5,000, `:195-196`). Without
Finish, the tail is as before: 15 to 60 s after a setup-page join, at most 75 s (`:191-194`). After an automatic
rejoin it is 0.5 s with no phone on it, otherwise up to 20 s (`:188-190`). The cloud is still admitted only once the
setup network is down.

**545b8f2.** No Finish: the button and `POST /finish.json` came with WP4 (`e7a7c9a`), after 545b8f2. The lower
bound was 15 s.

| V5 place | V5 now says | Corrected statement |
|---|---|---|
| 2.6 T5 R6 | "...after its setup network has closed, 15 to 60 seconds after it joins (at most 75 s)." | "...after its setup network has closed: about 2 s after the user taps Finish on the setup page (never sooner than 5 s after the join), otherwise 15 to 60 seconds after the join (at most 75 s)." |
| 4.2 T7 R4 (APP-FR-007) | "...closed, 15 to 60 seconds after it joins the router (at most 75 s)..." | "...closed, 5 to 60 seconds after it joins the router (5 s when the user taps Finish on the setup page; at most 75 s)..." The two-minute advice stands. |
| 5.5 P606 and 9 T43 R2 | "...15 to 60 seconds after it joins (at most 75 s)." | As in T5 R6. |
| 6.4 T36 R5 | "(15 to 60 s after it joins the router, at most 75 s)" | "(5 to 60 s after it joins the router, 5 s when the user taps Finish, at most 75 s)" |
| 1.4 P46 (help text, optional) | none | "Once the hub has joined, the setup page offers Finish: tapping it closes the setup network a few seconds later and the phone returns to its own Wi-Fi; otherwise the setup network closes by itself within a minute." |

`canon.md` C21's first paragraph needs the same change.

---

## D6. Router retry timing and a failed Connect (optional precision, help text)

**Code.** The router retry comes `ROUTER_RETRY_MS` 30 s plus a random 0-5 s after the last attempt's start or end
(`main/app_wifi/app_wifi.c:106-107`, the comment at `:85-91` gives about every 33-36 s, as at 545b8f2). It waits up
to 10 s while a phone is joining the setup network (`ROUTER_RETRY_JOIN_MAX_MS`, `:109`, new in WP8). It also waits
up to 5 minutes while a phone uses the setup page (`:108`); that was already true at 545b8f2, but V5 does not say
it. Since WP4 C8, a Connect from the setup page is saved only once it gets its IP. A wrong password, a missing
network or no IP within 25 s keeps the network the hub already had, and the retries go on with it.

| V5 place | Corrected statement (optional) |
|---|---|
| 2.6 T5 R6 | "...the hub tries its saved router again about every 35 seconds (later while someone is using the setup page, up to 5 minutes), rejoins it on its own once it is back, and closes its setup network within 20 seconds of rejoining (at most 75 s)." |
| 1.4 P46 (help text) | "A Connect on the setup page that fails (for example a wrong password) keeps the network the hub had: the hub goes on trying it." |

---

## Behaviour changes with no V5 wording change (notes)

**N1. Valve claims (WP6 `ab7eae8`, B2 `e874bf9` `183d361` `d7a918e` `96b09c0`, B5 `7428375`).** The hub now links
the valve with short connects: 1.5 s beside the setup network, 1.5-2.5 s in normal operation, and 2.5 s while a
leak response is pending (it was 30 s). After the second empty claim in a row, claims back off for 10, 30, 60, then
300 s (`main/ble_valve/app_ble_valve.c:167-168`, `:1889-1898`), except while a leak response is pending. A powered
valve in range is typically found again in about 2 s (model). A valve that is heard but cannot link (edge of range)
can now stay unlinked for minutes. It then reads "Valve offline" after 3 minutes and gets device_offline if it was
heard since the hub started. override_enable on such a valve more often fails "The valve isn't responding..." (its
BLE_CMD_CONNECT does not lift the back-off: `app_ble_valve.c:3188-3199`). Any pended CLOSE, a user's valve_close
for an unlinked valve included, counts as a leak response, so it skips the back-off
(`leak_response_pending()`, `lr_poll()` at `app_ble_valve.c:3112-3130`). V5's wording (T39 R2 "can take 30 s or more"; 5.2.6;
T43 R4) stays true. Optional for T39 R2: "...which takes a few seconds for a valve in range, and can take several
minutes for a valve at the edge of range."

**N2. Snapshot build and size (WP3 `8971f3b`, `64bba06`, `bef24b3`).** The snapshot is printed into one block
sized for the hub's device count (`telemetry_v2.c:1349-1384`). At 545b8f2 a full hub's snapshot needed a
contiguous block of about 16.5 KB and could fail to build every time, so no snapshots at all. The model sizes are
about 7.5 KB realistic, 8.5 KB with every label, the hub name and the valve firmware at 31 plain characters, and
9.6 KB at most (every character a quote or backslash) (model). The MQTT limits are 10,240 B per message plus
2,048 B backlog, still 12,288 B in all (`main/iothub/app_iothub.h:41-67`). V5 7.1 P662 and 9 T43 R18 ("about 8 KB,
and can reach about 10 KB with long labels") now hold in every case. Optional: "never more than about 10 KB".

**N3. Leak decisions check membership (WP3 `1afb368`).** A leak report from a sensor removed a moment earlier, or
from a neighbour's sensor seen while provisioning was busy, never closes the valve. The hub logs `Leak from %s
sensor %s ignored - not in this hub's device list` (`rules_engine.c:852-875`). The event gate is unchanged, so in
that race a leak_detected for the just-removed sensor can still arrive with no auto_close. That gate is older than
phase 1, and APP-NF-017 covers health events only. Optional V5 addition: apply APP-NF-017's rule ("ignore events
for a device the hub no longer lists") to leak events too.

**N4. Kept reports on a busy rules lock (WP3 `1afb368`, `8680b32`, `e1ae26d`, `bd575fe`).** The valve's dry flood
reports are now kept like wet ones, and a flapping source can no longer end dry while it is wet. In that rare busy
case, the RMLEAK automatic clear (V5: 10 to 12 s after the last dry report) is no longer withheld by a lost dry
valve report, and never fires while a source is still wet. V5 is unchanged.

**N5. Health hold removed (WP8 `dd19d6b`).** This is covered in D4. The "Starting up" and offline windows in V5
(2.6 T5 R5, 4.7 T14, A.2 T48) now apply as written during Wi-Fi setup as well.

**N6. MQTT stop retry (WP3 `e315eda`, `5a3c605`).** A stop that esp-mqtt refuses is retried. This removes a path
where an unseen MQTT client left the next connect failing ("Client has started"), which made a cloud outage
longer after a link loss, SoftAP start or token renewal. No V5 statement depends on it.

**N7. The setup page itself (WP4).** Pages are gzipped, sessions are bounded, there is a Rescan button, the reason
for a failed Connect is shown, and the SoftAP is on channel 11 with a channel-switch announcement. The setup
page's HTTP API is used only by the page (HANDOFF 15r D10), and V5 does not document it. The page now ends with
"Setup complete: WiFiHub's setup network closes now, and your phone goes back to its own Wi-Fi." and still says
"Return to Watts Home app to complete your WiFiHub setup". The app's help text can match these (D5).

---

## Other documents that carry the same facts

- **`canon.md`** (input of the V5 builder): C3.9 (rules left out "of that message", see D1), C12 (override_enable
  "No active leak" row, see D2), C16 (gateway.name, see D3), C21 (setup timings and the WP8 note, see D4 and D5).
  C11 Ordering could take D1's rare-lifecycle sentence.
- **`C2D_COMMANDS.md`** in the repo was not updated for D1-D3. Its override_enable precondition 3 and error tables
  (lines 307-314 and 768) do not have D2's case. Its section 3.5 twin text says nothing about the busy-device-list
  delay (D1) or the hub_name and gateway.name difference (D3). This is for the WP10 docs owner.

## Suggested Revision History row (if V5 is reissued)

"V5.1 | <date> | Synced with hub firmware 2.1.4 as built for CP7 (b651701): lifecycle and twin reported always
carry the rules and are delayed rather than sent with placeholder values (5.2.1, 5.4.1); override_enable is refused
right after the hub's own RMLEAK clear (4.4.3, 5.2.7); control characters in labels and the hub name are sent as
spaces (5.2.2); leak protection continues while the Wi-Fi setup network is open (2.6, 5.5); the setup network can
close 5 s after the join with Finish (2.6, 4.2). | Nayeem A |"
