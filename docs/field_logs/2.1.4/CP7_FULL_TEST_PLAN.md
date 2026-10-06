# CP7 bench test plan: eFloStop II Wi-Fi Hub, `b651701` (from 2026-10-07)

**For:** the hub firmware lead, alone at the bench. Claude reads the captures after each block and answers with a verdict per test.
**Firmware under test:** CP7 = commit **`b651701`** (`b651701a908af6e20760a5fd4aef350e4319f0a3`, 2026-10-07 06:37:11 +1100) on `fix/2.1.4`: phases 1-3 of the 2.1.4 radio/portal plan (WP3-WP8, M4) on top of CP6's `545b8f2`. Version string `2.1.4`. A bench image (`APP_BENCH_DIAG=y`). Every commit after `b651701` is docs only (4.2 checks it).
**Hub:** `GW-7C4FADAE69C8` on COM30. Spare hub: optional (later days).
**Written:** 2026-10-07, read-only, from the untracked CP6 plan (`CP6_FULL_TEST_PLAN.md`, never run: CP7 replaces it) and its gatherers' notes, `HANDOFF.md` §15q-§15x (the CP7 procedure in §15t; the bench-watch lists of §15q, §15r, §15s, §15u, §15w; G1 in §15v; the decisions in §15x), `WP9_GM_PROCEDURE.md`, `MANUAL_TEST_PLAN.md` as re-baselined by WP10, and the code at `b651701`. **Every quoted firmware line was checked against the code at `b651701`** (a script over all 1,088 `ESP_LOGx` formats; lines from ESP-IDF or the Wi-Fi driver are marked **(ESP-IDF)**).

**No bench data exists for anything built since CP6.** The radio policy, the pulses, the reset portal that keeps scanning (D2), the paced Connect (M4), WP4's portal and WP3's `sdkconfig` lines run on hardware for the first time today.

---

## 0. Open questions for you (answer before B0 if you can; the plan's default is in brackets)

1. **Start time and day length.** [B0 at 10:30, a long day 1 to about 20:40. To stop at about 19:00, cut in 5.3's order: S1, half of the day's power cycles and three contract checks move to day 2.]
2. **The power-cycle gate (HANDOFF 15x item 2, 15u B2).** The model's quantity is **power-on → `BLE_VALVE: ║ GAP CONNECT EVENT` with `[CONNECT] status=0`** (its "link" is the first advert the initiator hears, with no valve boot time). `SETUP COMPLETE` comes 10-20 s after CONNECT on every link (the full GATT discovery, CCCD writes and reads; 14.9 s on 2.1.3's one recorded relink), so a gate on power-on → `SETUP COMPLETE` fails by construction. "Every relink ≤ 10 s" cannot be met with certainty either (model, N_HUNT 0.45 s: 0.07 % / 1.7 % / 9.9 % of cycles over 10 s, P(all 100 ≤ 10 s) = 0.93 / 0.18 / 0.00 at valve p_loss 0.1 / 0.3 / 0.5). [Judge CP7 against **at most 2 of 100 over 10 s from power-on to CONNECT** (the reviewers' proposal). Its operating characteristic: with the firmware exactly as modelled it passes with probability 1.00 / 0.76 / 0.002 at p_loss 0.1 / 0.3 / 0.5, so it measures the bench's RF as much as the firmware; p_loss is therefore estimated apart from the gated data and the decision rule is fixed in advance (6.6). CONNECT → `SETUP COMPLETE` is recorded per cycle as a separate check. G6's "power-on → linked ≤ 5 s" uses the same CONNECT endpoint, judged per mode once there are 20 (6.6, 7.2); every relink under a leak response (CP7-L8, L8b, L9, D5, then G6) also records the safety span power-on → `Applying pending RMLEAK command=1` → `[DATA] Valve State=0 (CLOSED)`, each ≤ 30 s (E-09's ceiling). Record the full distributions either way.] Also: N_HUNT's 1M slot is 0.45 s in CP7; trying 0.3 s needs a firmware change and another build [record only; decide on the numbers].
3. **The 1M sensor.** With a sensor known on 1M in the device list, NORMAL always scans N_MIXED and never N_HUNT, and every AP mode runs AP_K1M (code: `radio_policy_exec_row()`). B2's power-cycle model and G-CNA's "every PHY Coded" S2 runs assume no 1M sensor. [Leave the 1M sensor (`…CB:B6`, expected) out of the list from B1's middle to the end of day 1 (`SS-VC`, 3.2), run the 1M cases on day 2, and put it back for the overnight soak.]
4. **The bench AP.** The reset portal's SoftAP is now on **channel 11** (D7), so the bench AP must be on **channel 1 or 6** (P-8's channel switch needs a different channel; CP6's "6 or 11" no longer holds). Which 3.3 option can you set up (A, A2, B, C)? Does it take the two DROP rules and a static lease, with flow offloading off?
5. **Phones and laptop.** Day 1 needs the iPhone and one Android. G-CNA and G1 need four phone classes (iPhone; Pixel, Android 15/16; Samsung, One UI 6/7; an Android 10-11 phone) on later days: which do you have? Is a Windows laptop with `curl.exe` and Python 3 at hand (P-13, P-14, the idle and connecting forget, G-FAULT)? A second ESP32 (G-FAULT F-2)? An RF shield (T3-11)?
6. **LoRa.** A LoRa leak sensor with the bench key, heard by this hub? [No: the LoRa tests are `N/A (no LoRa HW)` under the EC-2 waiver.]
7. **Spare hub, `decommission` `all`, `erase-flash`.** HANDOFF 15k allows `erase-flash` and `decommission` `all` only on a spare hub. Is there one? If not, may the main hub take a `decommission` `all` on a later day (it then re-registers with DPS)? [`erase-flash` stays spare-hub only.]
8. **Overnight soak tonight** on the main hub, until the SAS renewal (about 18 h after the day's last boot)? [Yes, 5.4.]
9. **The valve's Low and Critical PSU voltages:** on record? [Needed only for T2 on a later day.]
10. **COM port.** `HANDOFF.md` 15v (G1) and `WP9_GM_PROCEDURE.md` use `COM5`; this plan and `MANUAL_TEST_PLAN.md` use `COM30`. [COM30.]
11. **A code observation carried from CP6 (still true at `b651701`, `rules_engine.c:1597-1622`):** `override_enable` waits up to 10 s for an unreachable valve and only then checks that there is a leak to override; a leak latched during the wait passes the check, so a valve that links inside the wait would start a 24 h override for a leak that began after the request. (The wait runs on the esp-mqtt task; `iothub_task` latches the leak meanwhile.) Day 1 avoids it (no `override_enable` with the valve unreachable). Fix it in the next firmware commit? RC-11 needs it fixed or accepted in writing; 7.2's T5-15b shows it on the bench.
12. **Captures since CP5.** No CP6 bench happened (CP7 replaces it). Send any G0 or CP5 capture made since 2026-10-01; G0's open measurements are folded into CP7 (HANDOFF 15t item 9: Claude extracts Ta and the burst counts from every day-1 capture; Ta has a pass rule, CP7-B0-Ta in 4.5).

Decisions that wait for the bench data (15x A, B): B1-B5, the SUBMIT rule and M4's 45 s pacing (judged on S2's Connect success rate and time to `Connected! IP`), PH-2's tail (decided after S2: P-6, P-8 and P-9 are blocking there), D4 (only if a leak → CLOSE exceeds 200 ms), the list-scan pulses (G1). 10 lists which test feeds which.

---

## How to read this plan

- **Priorities.** **P1:** day 1, never cut. **P2:** day 1 if time allows (cut order 5.3). **P3:** a later day (7). **OBS:** observe and record; `Known-limit` when the hub behaves as written here, `Fail` only if it is worse (a reboot, invalid JSON on the wire, the valve opening by itself, a message cut short).
- **Test IDs.** `CP7-B0-n` (build), `CP7-Ln` (leak core), `CP7-Dn` (D2 in the reset portal), `CP7-Gn` (phones), `CP7-Rn` (router), `CP7-Cn` (cloud), `CP7-Vn` (valve power cycles). `S-n`, `T…`, `DEC-…`, `VAL-…`, `G-CNA`, `P-n`, `X-n`, `F-n`: `MANUAL_TEST_PLAN.md`'s IDs (the "test plan"). `15x` items: `HANDOFF.md`.
- **Log lines** are quoted as `TAG: text`. `<…>` is a value the hub prints; `…` stands for text left out. printf fields are rendered where the value is fixed. Match the fixed text, not the values. Some lines use an em dash (`—`), others a hyphen (`-`), exactly as the code has them: search for a distinctive part.
- **Timing.** Measure every interval from the ESP's own `(N)` milliseconds at the start of each line. `cloud_tx` prints the `TELEMETRY_V2: Pub …`, `Offline — buffering …`, `OFFLINE_BUF: …` and `IOTHUB: Twin reported …` lines: they can print before or after the line that caused them. Take the order of messages from the IoT Hub capture. **Never fail a step on UART line order alone.**
- **Results:** `Pass`, `Fail`, `Blocked`, `N/A`, `Known-limit` (test plan 0.17). A Blocked P1 test is reported like a Fail.
- **Words.** **VA:** valve A (provisioned, on the bench PSU, batteries out). **VB:** valve B (powered, never provisioned). **U:** valve unpowered (PSU output off, still provisioned). **L:** valve linked. **Bench AP:** the access point of the day (3.3). **Reset:** the 10 s Wi-Fi reset (hold the button 10 s). **Wet / dry:** a wet paper towel on the probe pads / removed and the pads wiped. **First wet:** the first wet report of an incident (only it prints `AUTO-CLOSE + RMLEAK triggered`, 8 item 1).

---

## 1. Purpose and scope

### 1.1 What CP7 is

CP7 is the first image with the whole 2.1.4 radio/portal plan built together: WP3 (the seven `sdkconfig` lines, membership in one hold, the busy-lock, twin, MQTT-stop and snapshot residuals), WP4 (the portal: gzipped page, Connect committed at its IP, Finish, the channel switch, the list cache), WP5/WP6 (one BLE scan executor, NORMAL de-locked, valve claims, persisted PHY), WP8 (`radio_policy`: BLE leak scanning keeps running in the reset portal, every Wi-Fi need gets a bounded pulse; the portal window, the radio holds and the health hold are deleted) and phase 3 (M4: a SUBMIT or JOIN within 45 s of the last is paced; three stale-stamp fixes). WP7's lab image (G1) and WP9's memory lines (G-M) are separate builds for later days.

### 1.2 What day 1 proves

1. **The build is exactly CP7** (15t: the `sdkconfig` regeneration, the four known warnings, DIRAM `.text` 113,387 B, the map searches, phase 3's strings) and its first boots show 15t's lines.
2. **Leak safety on the new radio:** every source (BLE Coded, BLE 1M, the valve's own probe, LoRa if at hand) gives `LEAK INCIDENT latched` → RMLEAK → CLOSE with the valve linked; with the valve unpowered the close is pended, the leak response scans (NORMAL_LR), the 600 s cap holds, and the relink writes RMLEAK before CLOSE; P11 (a leak across a boot) and P14 (a valve missing at boot, RED at about 180 s); the interlock and override regressions (T5-04, T5-05, T5-06).
3. **D2:** leak protection keeps running in the reset portal: a leak with no phone (AP_IDLE), a leak while a phone uses the setup page (SERVE), a leak with the valve unpowered (LR_AP), and the events delivered after setup.
4. **The phones:** G-CNA's smoke pass (3 runs per phone) in S2 with **P-6, P-8 and P-9 blocking** (PH-2), P-6 both ways (the paced and the first Connect, M4), Finish, Forget, S3 with a wrong password that keeps the saved network, and S1.
5. **Router outage:** a RETRY pulse 30-35 s after each failed attempt ends (6.4), the page's deferral, the rejoin within about 40 s, cloud admission only after the SoftAP stops.
6. **The cloud:** TLS and MQTT with 2 KB records and no kept peer certificate; the contract (acks, error texts, twin); **LS-1** (a leak closes within 200 ms during a WAN black-hole) and **the D4 measurement** (leak → CLOSE against 200 ms).
7. **The valve power cycles** against the statistical gate (B2; question 2): cycles 1-60 on day 1, 61-100 on day 2.

### 1.3 What day 1 cannot prove

- **The release.** CP7 is a development checkpoint with `APP_BENCH_DIAG` on. The release candidate (2.6) is a later build.
- **G1's rung choice, G2-G7 in full, G-M, the soaks, upgrade and rollback, the spare-hub items, the bench-only images:** later days (7). **Without a spare hub, and without question 7's OK for a `decommission` `all` on the main hub, RC-4b (a DPS registration on the new TLS settings and a full hub's snapshot) cannot run, and it blocks the RC.**
- **The 1M sensor in the AP modes (AP_K1M) and in G-CNA's second S2 set:** day 2 (question 3).
- **LoRa,** unless question 6 is yes.

### 1.4 Status at CP7

| Item | Status |
|---|---|
| 2.1.4 field fixes (phases A-G, `d9fa9c8`) and the plan's WP-V … WP8 + phase 3 | **Done in code**, each reviewed and voted SHIP (HANDOFF 15h-15w). **Bench: open.** No P0 test has a full Pass on any image. |
| CP6 (`545b8f2`) | Built nowhere, benched nowhere: CP7 replaces it. |
| G0 baselines | Open: folded into CP7's captures (question 12). The model re-run WP8 owes waits for them (15x item 28). |
| WP7 (G1 lab image), WP9 (G-M lines, staged off by default) | Built: their bench procedures are 15v and `WP9_GM_PROCEDURE.md` (later days, 7.3, 7.6). |
| WP10 | Done (docs). Release steps still open: `APP_BENCH_DIAG` default n, `C2D_COMMANDS.md` (WB-CLOUD-1), the `sdkconfig.wp9/` corrections, the `app_ble_leak.c:870-875` comment (15x D). |
| Open findings after the reviews (none blocks CP7) | WB-CONC-1 (a failed claim's blind span up to about 3.4 s: an `I2:` line can print), LEAK-WB-1 (a valve swap during a live incident), LEAK-WB-2 (no-BLE-sensor hubs: a pended CLOSE's claim waits 6-7 s), WB-CRASH-3 and the 49.7-day `ticks_ms()` wrap, WB-CLOUD-1 (cmd_ack order when MQTT refuses it). 15x item 21-22. |
| Pre-push clean-up | Redact `6b84ae3`'s Wi-Fi password (EC-12); the over-long subjects and wrong types (15x E). |

---

## 2. Go / no-go

Claude gives the verdict from the captures at the end of each day.

### 2.1 Day 1: "GO pending" (CP7 is a sound base for G1 and G-M) when all of these hold

| # | Criterion | Shown by |
|---|---|---|
| G1 | **Build.** 4.3's B1-B14 all pass: the `sdkconfig` hash `9E13270C…AAE0412D`, the 25-line `Compare-Object`, exactly the four known warnings, DIRAM `.text` exactly 113,387 B, the map searches (13 lines, then none), both phase-3 strings `True`. The first boots show 4.4's lines; the self-test passed. **CP7-B0-Ta** (4.5): every listed sensor's shortest burst `dT` ≤ 448 ms (the premise of I1 and of every leak-latency figure). | CP7-B0 |
| G2 | **No unplanned reboot, panic, stack overflow, watchdog or `abort()` all day** (each reset's own reboot excepted), and **none of 3.8's "report at once" lines** on a normal run. | every block |
| G3 | **Leak safety.** Every first wet from a listed source, with the valve linked: RMLEAK written before CLOSE; `leak=1` → `AUTO-CLOSE + RMLEAK triggered` ≤ 200 ms; `leak=1` → `BLE_VALVE: [CMD] Writing Valve=0` ≤ 1 s. **Detection:** every wetting of a listed BLE sensor → its first `BLE_LEAK: eleak <MAC> — leak=1` ≤ 20 s in NORMAL (any row) and in NORMAL_LR (≤ 60 s for CP7-L8's second sensor), by the notes' time or the `d` marker (a wet at a boot banner, L9 and L10, counts from `BLE_LEAK: Extended passive scan started (1M + Coded PHY)`); none over 60 s in any mode (2.3). Pended closes (valve unpowered) applied RMLEAK first at the relink. The 600 s cap ends on time; claims go on after it. P11 (L and U), P14 and CP7-L8b (the valve power-cycled in a live incident) pass. T5-04, T5-05, T5-06 pass. No false override; the valve never opens by itself. | CP7-L1 … L13 |
| G4 | **D2.** In the reset portal: a wet heard within 20 s in AP_IDLE and within 35 s in SERVE and in LR_AP (CP7-D4's second sensor), each closing the linked valve (RMLEAK first); with the valve unpowered, `RADIO: Mode LR_AP …`, then the relink with RMLEAK first; every event still in the offline ring (16 entries) delivered after setup, before the lifecycle, and every overwrite classified (CP7-D6); no `device_offline` for a live listed sensor from D1 to D6 (the valve's only during D4/D5). No line of the deleted window or holds. | CP7-D1 … D8 |
| G5 | **Phones (smoke pass).** 3 runs per phone in S2 (CP7-G1 … G6), 2 in S3 (CP7-G7): every run ends with the hub on Wi-Fi, 0 reboots; **P-6, P-8 and P-9 pass in every S2 run** (else PH-2's tail decision, 15x item 9); Finish stops the SoftAP by max(IP + 5 s, tap + 3 s) (designed: tap + 2 s; 1 s of tolerance); Forget erases; a wrong password keeps the saved network. Every pulse ≤ 2,800 ms (paced ≤ 1,500 ms); every `[SUMMARY]` `(0 over 2900 ms)` and `at most Y s in 60 s` with Y ≤ 12.0. | CP7-G1 … G8 |
| G6 | **Router.** RETRY: 30-35 s from each `APP_WIFI: WiFi Disconnected. Reason: 201` (the previous attempt's end) to the next `RADIO: RETRY pulse: BLE off for up to 1500 ms`; 38 s still passes (wifi_task's 1 s pass and the ≤ 2 s grant wait); start to start about 33-39 s, recorded, not judged. `Connected! IP` ≤ 40 s after the SSID returns (45 s still passes); with no station, the SoftAP stops 0.5 s after the IP and MQTT is up ≤ 5 s after it; nothing cloud before `cloud admitted`; `allocfail` 0. | CP7-R1 … R6 |
| G7 | **Cloud.** TLS and MQTT up with no `esp-tls` or mbedTLS error; the contract checks as listed; **LS-1:** in all 3 runs the first wet's `leak=1` → `AUTO-CLOSE + RMLEAK triggered` ≤ 200 ms and `leak=1` → `Writing Valve=0` within the L-spread (2.4), with at least 2 first wets inside a stalled write; every event missing at IoT Hub classified (R5 or a ring overflow); the validator 0 FAIL once duplicates are classified. | CP7-C1 … C10 |
| G8 | **Valve power cycles** (the cycles run that day, 60 planned): 0 `[SCAN] ble_gap_connect rc=2` or `rc=6`, 0 `[SCAN] Already connected`, 0 host resets; power-on → CONNECT within 6.6's interim rule for the cycles run so far (question 2's gate at 100); CONNECT → `SETUP COMPLETE` ≤ 25 s in every cycle. | CP7-V1 |

### 2.2 GO WITH FINDINGS (CP7 stays the base; each finding goes to the next firmware commit or to your decision)

**Default:** a 2.1 criterion that is not met and is not listed below or in 2.3 is **NO-GO** for G1-G4; for G5-G8 it needs your written acceptance (Claude names which criterion), else it is NO-GO.

- **D4:** a first wet with the valve linked whose `leak=1` → `Writing Valve=0` is over 200 ms (and at most 1 s), with the extra time before the valve task starts (2.4): D4's reordering (15x item 12).
- The `I2:` line inside WB-CONC-1's exemption (2.3; record N, the claim lines, and the exempt hits per 100 claims). **More than 5 exempt hits per 100 claims** (B6, G6b) makes the WB-CONC-1 fix (15x item 21) an RC-11 precondition.
- Phones: a sign-in window that does not open by itself; a P-1 … P-5 time over its bound in fewer than 2 of 6 runs; a paced Connect that needs a second tap; `SUBMIT pulse not granted before its Connect ended …` lines (the M4 cost, 15x item 6).
- B2 (6.6): relinks over 10 s (power-on → CONNECT) within the gate or the interim rule's "findings" band (record). Over the gate, decided by the p_loss estimated from the claims (6.6), not from the gated re-find: above 0.3, move the valve closer and re-run before judging; at or below 0.3, a firmware finding (B2's 0.3 vs 0.45 s slot, 15x item 2), decided before G1.
- The six V5 audit findings behaving as documented (CP6 plan 6.8; unchanged by CP7): `Known-limit`.

### 2.3 NO-GO (any one)

- A reboot, panic or stack overflow not caused by a reset or a test's own power cycle. One unexplained case is enough until Claude has analysed it.
- **Safety:** a wet report from a listed source, with the valve linked, that does not close the valve; CLOSE written or applied before RMLEAK (also `[CMD] Applying pending valve command=0` before `[CMD] Applying pending RMLEAK command=1`): **stop-ship**; `leak=1` → `Writing Valve=0` over 1 s with the valve linked at the `leak=1` line; `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window` with nobody at the valve button; the hub opening the valve during a latched leak with no override asked for; `leak_reset`, `valve_open` or `valve_set_state` open acked `ok` while a listed sensor is wet or RMLEAK is latched with no override; `RULES_ENGINE: Leak from <type> sensor <id> ignored - not in this hub's device list` for a sensor provisioned on this hub (15q: a release blocker); a leak response that never ends or never relinks a powered valve.
- **Safety, also** (G3 criteria the line above does not name):
  - P14: with the valve unpowered at boot, no `FLEET_LED: rating=critical color=RED effect=SOLID` or no `critical` snapshot naming the valve by 300 s (CP7-L7);
  - with the valve unlinked and a CLOSE pended: no `BLE_VALVE: [LR] Leak response pending …` within 2 s of the `AUTO-CLOSE + RMLEAK triggered` line, or the leak response ending before 590 s or after 610 s other than with `(valve linked)` or, after the last source's `leak=0`, `(leak response written or withdrawn)`;
  - `override_cancel` acked `ok` with an active leak and the valve linked, and no `Writing RMLEAK=1` then `Writing Valve=0` within 3 s of the ack (CP7-L13);
  - a linked valve with no `BLE_VALVE: [DATA] RMLEAK=1 (ACTIVE)` within 5 s of a `Writing RMLEAK=1` (the link still up 5 s later; T5-10), or an incident whose last RMLEAK read-back is 0 while a listed sensor is wet and no override is active (T5-04);
  - `RULES_ENGINE: Reconnected: hub incident + valve open + RMLEAK clear — inferring physical override, starting 24h window`, or an IoT Hub `water_access_override_enabled` with `"trigger":"button"`, with no valve-button press in the notes.
- **Detection, any mode:** a wetting of a listed sensor with no `leak=1` within 60 s (a wet at a boot banner counts from `BLE_LEAK: Extended passive scan started (1M + Coded PHY)`; Claude first rules out a sensor fault from that sensor's own burst lines and heartbeats). **D2:** a wet sensor in the reset portal that does not close the linked valve.
- **Radio invariants:** `RADIO: Profile self-test FAILED …`; a `[SUMMARY]` with `(N over 2900 ms)`, N > 0, outside WB-CONC-1's exemption; `RADIO: Duty watchdog: …`; a `[CLAIM] … 2 s past its pulse` line; any line of the deleted window or holds; **CP7-B0-Ta:** a listed sensor whose shortest burst `dT` is over 448 ms (4.5: the I1 asserts and the 15u/15w leak figures do not hold for it until `RP_ADV_SMAX_MS` and the model are re-run, 15x item 28).
  - **WB-CONC-1's exemption** (the only one): N ≤ 3,500 ms; the `RADIO: I2: …` line within 1.5 s after the claim pulse's end (the `BLE_VALVE: [CLAIM] Connecting to the valve: pulse up to <P> ms` line + P); and each minute's `(N over 2900 ms)` count at most its number of claim lines (the `I2:` line prints only the minute's first breach).
- **Phones:** either phone cannot finish setup after a reset in two tries; a reset or a Forget that does not erase; a wrong password that replaces or erases the saved network.
- TLS beside the SoftAP (anything cloud before `cloud admitted`), or a failed allocation (`allocfail` rising) at a rejoin.
- LS-1: a first wet with `leak=1` → `AUTO-CLOSE + RMLEAK triggered` over 200 ms.
- A build-gate mismatch nobody can explain: stop before the bench.

### 2.4 Definitions used above

- **L-spread:** the largest `leak=1` → `BLE_VALVE: [CMD] Writing Valve=0` of CP7-L1 … L3 (valve linked, normal link), plus 100 ms, **and never more than 300 ms**. LS-1 (CP7-C2, C3) and CP7-R4 are judged against it; CP7-D2 and D3 record against it. If L1-L3's largest is over 200 ms, D4 has triggered (2.2) and LS-1, R4 and C3 are judged against 300 ms; a first wet with Δc over 300 ms in C2, C3 or R4 is then a D4 finding to decide before the RC (RC-3), not a pass by a looser spread.
- **Δw** (per wetting, Claude): the notes' wet time (or the `d` marker) → that sensor's first `leak=1`. Recorded per row as G2's day-1 preview: N_MIXED (L1-L3), N_CODED (L12, L13, R4, C2, C3), N_HUNT (L9, from the scan start), NORMAL_LR (L8's second sensor), AP_IDLE (D2, R3), SERVE (D3), LR_AP (D4's second sensor).
- **The D4 figures, per first wet** (Claude extracts them; you only write the wet's time in the notes): Δa = `leak=1` → `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by …`; Δp = `leak=1` → `BLE_VALVE: [TASK] CMD: SET_RMLEAK` (the valve task has the post); Δr = → `BLE_VALVE: [CMD] Writing RMLEAK=1`; Δc = → `BLE_VALVE: [CMD] Writing Valve=0`; Δs = → `BLE_VALVE: [DATA] Valve State=0 (CLOSED)`. D4 is about Δp (the stdout and NVS terms on `iothub_task` before the posts, 15n R2/R3); Δc − Δr is the RMLEAK write's own round trip, which D4 does not change. **D4 triggers** when a first wet has Δc > 200 ms and Δp carries the excess; next to it Claude lists the `TELEMETRY_V2: Pub snapshot: …` and `OFFLINE_BUF: Stored event [ob_<nn>] (<n> bytes), <n> buffered` lines between the `leak=1` line and the posts.

### 2.5 CP7 accepted as the base (after the later days)

CP7 becomes the base for G1's production change and the G-M lines once day 1 is "GO pending" **and** these have passed on CP7: G-CNA's full S2 (10 runs per phone class, P-6/P-8/P-9 blocking) or PH-2's decision taken; G6b ×20; the 100 power cycles within the chosen gate (power-on → CONNECT, 6.6); G3b (30 router cycles); the overnight soak with its SAS renewal; T6-11 at CP7 (stack margins: `ble_leak_scan` 3,072 B, `cloud_tx` ≥ 1 KB free); **the hold image at `b651701`** (7.8; HANDOFF 15q "What to watch", 15k item 17): 4 or more valve-probe flaps inside a ≥ 1 s rules hold, then a sensor wet → `LEAK INCIDENT latched`, `Writing RMLEAK=1` before `Writing Valve=0`, no `RULES_ENGINE: Rules lock busy - … is lost` line; a valve swap during a hold prints `RULES_ENGINE: Valve replaced: …` once and the new dry valve is not closed at its first link.

### 2.6 Release-candidate go / no-go

The release candidate (RC) is **not** CP7. It is the build after G1's production change (the SERVE rung, the AP_IDLE row, K1 if needed), the G-M lines you approve, `APP_BENCH_DIAG` default n, and the fixes you choose from 15x. **The RC is GO only when all of these hold** (record each Met / Not met with its evidence):

| # | Criterion | Evidence |
|---|---|---|
| RC-1 | **Build.** VAL-01 on the RC commit: the four known warnings only; DIRAM `.text` 113,387 B (or the new baseline if W1 landed, `WP9_GM_PROCEDURE.md` 5.1); the RC's `sdkconfig` hash recorded; no `APP_BENCH_DIAG` warning at boot; the production tool's boot-log parse re-run (15x item 23). *The test plan's EC-1 still names CP5's `0f08d32`: read it as this row.* | VAL-01 (RC) |
| RC-2 | **Every P0 and fix test passes** (test plan EC-2) on CP7 or the RC, with the RC's smoke subset passing on the RC itself. LoRa: the EC-2 waiver if no LoRa hardware. *EC-2's T4-10 LoRa-waiver paragraph describes the deleted portal window: T4-10 as rewritten for CP7 has no LoRa steps.* | results sheet |
| RC-3 | **Leak safety:** G3 and G4 of 2.1 on the RC; LS-1 ×3 on the RC; D4 decided (built or not needed); the hold image (2.5, 7.8) passed at `b651701` or on the RC's code; no NO-GO item open. | RC captures |
| RC-4 | **Radio gates:** G1 decided and its change built; G-CNA S1, S2, S3 full on the RC's settings (10 runs per device per state, four phone classes); G2 (7.2: per row, at least 19 of 20 drips within the row's bound and none over 60 s: N_CODED (`SS-VC`, valve linked) ≤ 5 s, N_MIXED (1M sensor listed) and N_HUNT (valve unlinked) within their model p99, AP_IDLE ≤ 20 s, SERVE ≤ 35 s; and with the valve unpowered and one sensor wet, 10 drips each, NORMAL_LR ≤ 60 s and LR_AP ≤ 35 s); G5; G6 and G6b; G7; the 100 power cycles within the gate (power-on → CONNECT); G-FAULT; G8 and G8x. | 7.1-7.4 |
| RC-4b | **First-time paths of WP3's TLS and snapshot changes:** a DPS registration on the RC's TLS settings succeeds (`DPS: Assigned hub=<host> device=<id>`, then `IOTHUB: Connected to Azure IoT Hub!`, no esp-tls or mbedTLS error), on the spare hub, or on the main hub after `decommission` `all` with your OK (question 7); and the full hub's snapshot (A.3, `ascii` and `quote`) builds and publishes with no `TELEMETRY_V2: Snapshot not built - out of memory`. Without a spare hub and without question 7's OK, this row blocks the RC. | 7.7 |
| RC-5 | **Memory:** G-M per landed line and G4b if the floor is claimed; VAL-13 (heap like-for-like ≥ 2.1.3, test plan EC-4); T6-11 margins (every task ≥ 512 B free, `iothub_task` ≥ 2 KB, `cloud_tx` ≥ 1 KB). | 7.6, 7.8 |
| RC-6 | **Soaks:** VAL-14 ≥ 19 h with a SAS renewal and 0 reboots (EC-6); T6-12 (sensors-only, spare hub); G4 (24 h NORMAL) and the AP_IDLE soak (router off, 3 dry sensors, valve unpowered): 0 false offlines, no `Duty watchdog`, flat heap. | 7.4 |
| RC-7 | **Upgrade and rollback:** VAL-02 (runs A-D, the PHY table) and VAL-03 (EC-7), with the 2.1.3 baseline built from `..\sdkconfig.pre_cp7` (UPG-1). | 7.5 |
| RC-8 | **Contract:** VAL-15 0 FAIL (EC-5); WB-CLOUD-1 resolved (`C2D_COMMANDS.md` §3.5 corrected, or the cmd_ack routed through `cloud_tx`); the V5 delta applied to the app document (15x items 22, 24, 25). | VAL-15 |
| RC-9 | **App:** VAL-04 … VAL-12 both halves (EC-8; field rollout waits for an app-half fix, the tag does not). | VAL-04 … 12 |
| RC-10 | **Hygiene:** EC-10 (no password or passkey on UART; the production-tool lines byte for byte); EC-9 known limitations recorded; EC-11 no open Fail. | T4-14, the grep |
| RC-11 | **Your decisions:** 15x A ratified (B1-B5, SUBMIT/M4, the NORMAL claim length, the I10 heap exception); 15x B decided (PH-2, PH-7, the list-scan pulses, D4, each G-M line); 15x C either fixed or accepted in writing (WB-CONC-1, LEAK-WB-1, LEAK-WB-2, WB-CRASH-3, the 49.7-day wrap, the QoS 0 twin, `network.timeout_ms`, 15i residual 6, the open SoftAP; WB-CONC-1 must be fixed if B6 or G6b counted more than 5 exempt `I2:` hits per 100 claims, 2.2); **question 11** (`override_enable` checks for an incident only after its ≤ 10 s valve wait): fixed (the incident and RMLEAK read at receipt, refused if neither held then; 7.2's T5-15b passes) or accepted in writing. | 15x |
| RC-12 | **Before any push:** EC-12 (`6b84ae3` redacted) and 15x E's history rewrite. Nothing pushed until you ask. | `git log -p` |

---

## 3. Bench set-up

### 3.1 Equipment (tick before B0)

| # | Item | Needed for | Notes |
|---|---|---|---|
| E1 | Hub `GW-7C4FADAE69C8`, USB to the capture PC (COM30) | everything | **Never `erase-flash` this hub.** |
| E2 | Valve A, FW 2.2.0, **batteries out, on the bench PSU** at 6.00 V | every valve test | With batteries in, switching the PSU off does not unpower it. Its output button is the valve's power switch (U). |
| E3 | Bench PSU, 10 mV steps, current limit above the motor stroke | U runs, B6 | Never below 5.0 V. |
| E4 | Valve B, powered, within 2 m, never provisioned | S-8 (smoke) | |
| E5 | 4 working BLE leak sensors, FW 1.1.0 | `SS-V4` | `…2B:A5` is dead: bring its replacement (`<BLE4>`). `…CB:B6` is expected on 1M (3.2). |
| E6 | **Bench AP** on the desk, 2.4 GHz on **channel 1 or 6** (3.3) | every outage, S3, LS-1, P-8 | |
| E7 | iPhone and one Android phone, mobile data on, charged; both forget `WiFi-Hub-69C8` and turn auto-join off for it | B2, B3 | Record model and OS of each. |
| E8 | **nRF Connect** on one of the phones (or a third device): **required for B6** | B6 | Logs the valve's advert interval and its power-on → first advert once (6.6): the p_loss estimate and the model comparison need both. |
| E9 | RF shield (a metal box or an unplugged microwave) | T3-11 (CP7-L11) | Without it, T3-11 moves to day 2. |
| E10 | Cup of water, paper towels, a dry cloth | wetting | Keep water away from the PSU leads and the hub's USB. |
| E11 | Capture PC: ESP-IDF 5.5.1 PowerShell; Git Bash with `az` and the `azure-iot` extension (`az login` done); Python 3 | everything | If `az` is missing: `winget install -e --id Microsoft.AzureCLI`, then `az extension add --name azure-iot` and `az login` (CP6 plan N1). |
| E12 | Day folder `C:\Work\Projects\EfloStop 2\Firmware\Production\2.1.4_bench\2026-10-07_cp7\` | all captures | Outside the repo. |
| E13 | *Wanted:* a Windows laptop with `curl.exe` and Python 3 (and `dns_check.py`, Appendix A.1) | P-13, P-14 SoftAP half, the idle forget | Without it these move to day 2. Delete its `WiFi-Hub-69C8` profile after use (`netsh wlan delete profile name="WiFi-Hub-69C8"`) and switch its Wi-Fi off: CP7-R2 needs 0 stations on the SoftAP. |
| E14 | *Optional:* a spare hub, its own COM port and gateway ID | 7.7 | |
| E15 | *Optional:* a LoRa leak sensor with the bench key (question 6) | CP7-L5 | |
| — | **Keep switched off:** the Windows laptop that remembers `WiFi-Hub-69C8` ("E2") | — | It auto-joins the fallback SoftAP and floods it (2026-09-29). |

### 3.2 Identities and the two device sets

Confirm the MACs against the boot's `PROVISIONING: …` lines before the first send, and correct `payloads.txt` (3.6) if they differ.

| Placeholder | Value |
|---|---|
| IoT Hub, resource group | `resi-apex-iot-dev`, `resi-apex-rg-dev` |
| `<GW>` | `GW-7C4FADAE69C8` (COM30); setup SoftAP `WiFi-Hub-69C8`, portal `http://10.10.0.1/` |
| `<VALVE>` (VA) | `00:80:E1:27:F7:BB` |
| `<VALVE_B>` | `00:80:E1:27:7E:C5` |
| `<BLE1>` "Sink" | `00:80:E1:2A:29:FC` |
| `<BLE2>` "Washer" | `00:80:E1:2A:B6:8E` |
| `<BLE3>` "Ensuite" | `00:80:E1:2A:CB:B6`, expected on **1M** |
| `<BLE4>` "Main" | the replacement sensor: fill in |
| `<BLE1M>` | the sensor CP7-B0's first boot learns on 1M (`BLE_LEAK: eleak <MAC> PHY learned: 1M (was unknown)`): expected `<BLE3>`. If none is on 1M, `SS-VC` = `SS-V4` and no switch is needed. If more than one is, leave all of them out of `SS-VC`. |
| `<LORA1>` | fill in if question 6 is yes |

**Device sets.**
- **`SS-V4`:** VA + `<BLE1>` … `<BLE4>`, labelled (`ss-v4m-…`), all heard and dry, valve open, battery Good, mask 7, no incident, no override, LED GREEN, twin interval 60 s. With `<BLE1M>` listed, NORMAL scans `N_MIXED` and the AP modes `AP_K1M` for good.
- **`SS-VC`:** `SS-V4` less `<BLE1M>` (removed with `dec-1m-…`, 3.6). Every listed PHY Coded: NORMAL scans `N_CODED` (valve linked) or `N_HUNT` (valve unlinked), the AP modes `AP_IDLE` / `SERVE`. Day 1 uses `SS-VC` from CP7-L6 to the end of B6.

### 3.3 Making outages without the physical router

Two kinds: **Wi-Fi gone** (the hub's network disappears) and **WAN black-hole with Wi-Fi up** (the internet silently cut). The capture PC must stay online through both.

| Option | Wi-Fi gone | WAN black-hole | Notes |
|---|---|---|---|
| **A (best):** a travel or spare router on the desk, ideally OpenWrt, WAN by Ethernet to the home router, 2.4 GHz on **channel 1 or 6**, its own SSID | its Wi-Fi toggle or power switch | the two DROP rules below, or pull its WAN cable | The PC is not on this AP's LAN: P-14's LAN half runs with the hub on the home router (CP7-G3). |
| **A2:** a travel router (OpenWrt) as a Wi-Fi repeater (uplink on the home router's 5 GHz) | `wifi down radio0` / `wifi up radio0` (check the 2.4 GHz radio with `uci show wireless`) | the two DROP rules | Run the black-hole check before LS-1. |
| B: the home router's admin page | turn off its 2.4 GHz radio only (PC on Ethernet or 5 GHz) | a per-device "block internet", if it DROPs | **Every household 2.4 GHz device goes offline: warn the others.** |
| C: the Android phone's hotspot, 2.4 GHz, on its charger | hotspot off | mobile data off, hotspot on | The Android cannot be a portal phone meanwhile. Not for the soak. |

**OpenWrt DROP rules (22.03 or later), always both** (one rule lets Azure's packets on the open session through):
```sh
nft insert rule inet fw4 forward ip saddr <hub IP> tcp dport 8883 drop
nft insert rule inet fw4 forward ip daddr <hub IP> tcp sport 8883 drop
nft -a list chain inet fw4 forward          # both rules, with their handles
nft delete rule inet fw4 forward handle <n> # remove each, by handle (twice)
```
Older OpenWrt: `iptables -I FORWARD -s <hub IP> -p tcp --dport 8883 -j DROP` and `iptables -I FORWARD -d <hub IP> -p tcp --sport 8883 -j DROP`; `-D` removes each.

**Make the rules hit the hub:** a static lease (`uci add dhcp host; uci set dhcp.@host[-1].mac='<hub STA MAC>'; uci set dhcp.@host[-1].ip='<IP>'; uci commit dhcp; /etc/init.d/dnsmasq restart`); `uci get firewall.@defaults[0].flow_offloading` prints `0` or nothing; before every DROP compare `<hub IP>` with the last `APP_WIFI: Connected! IP: <IP>` line and list the chain. **Remove both rules** before the hub needs the internet again, and confirm with `nft -a list chain inet fw4 forward`.

**Black-hole check (3 min, the start of B5):** with the hub connected 2 min, start the DROP and press `d` in the monitor (a marker). A silent drop keeps the session until esp-mqtt gives up: `IOTHUB: Disconnected.` about 10-90 s later, often after `IOTHUB: Pub <kind> took <s> s (msg_id=<id>)`. `IOTHUB: Disconnected.` within about 5 s means the method answers (REJECT or a reset): fix it before LS-1. The outage is real only if no hub message reaches IoT Hub after T0 + 5 s.

### 3.4 Terminals and captures

| Terminal | Use |
|---|---|
| T1: ESP-IDF 5.5.1 PowerShell, **in the project folder** (CP7 is built there, 4.2) | build, flash, UART monitor (backtraces decode with CP7's ELF) |
| T2: Git Bash | IoT Hub monitor (UTF-8) |
| T3: Git Bash | C2D and twin (3.6) |
| T4: PowerShell (B6 only) | the power-cycle metronome (Appendix A.2) |

**UART:**
```powershell
cd "C:\Work\Projects\EfloStop 2\Firmware\Production\eFloStop_WiFiHub_idf1"
idf.py -p COM30 monitor --no-reset --timestamps --timestamp-format "%Y-%m-%d %H:%M:%S.%f"
```
- `--no-reset` whenever you re-attach to a running hub (a plain `monitor` resets the chip).
- **A boot from its first line:** start the monitor with `--no-reset`, start the log (Ctrl+T Ctrl+L), then Ctrl+T Ctrl+R (the hub resets with the log running).
- Ctrl+T Ctrl+L starts and stops `log.eFloStop_WiFiHub_idf1.<time>.txt` **in the project folder**. **Close it and start a new one at every block boundary, and move each closed file to the day folder at once** (left in the project folder it shows in `git status`; never commit one).
- Typed keys go to the hub. **`d` is the time marker:** within about 0.1 s it prints `APP_LORA: Stats: RX=<n>, ACKs=<n>, LastRSSI=<n>`. Never press `a` (LoRa ACKs; press it again until `APP_LORA: ACK ENABLED` if you did), `s` or `r`.

**IoT Hub (one file per block):**
```bash
cd "/c/Work/Projects/EfloStop 2/Firmware/Production/2.1.4_bench/2026-10-07_cp7"
az iot hub monitor-events -n resi-apex-iot-dev -g resi-apex-rg-dev -d GW-7C4FADAE69C8 \
   --content-type application/json --properties sys --timeout 0 | tee -i "B1_1125_leak_iothub.txt"
```
- `--properties sys` adds `iothub-enqueuedtime` (CP7-C7 needs it). `tee -i` ignores Ctrl+C so the file is flushed.
- At a block's end, wait until its last expected message is in the file (`tail -n 3 <file>` in another tab), then Ctrl+C. **Backfill** a dead monitor with `--enqueued-time <epoch ms>` (for example `$(date -d "2026-10-07 13:10" +%s%3N)`) and `--timeout 90`.

**File names** (day folder; `B<n>` is the block, `HHMM` its start): `B<n>_<HHMM>_<scope>_uart.txt`, `B<n>_<HHMM>_<scope>_iothub.txt`, `B<n>_notes.md`, `twin_<label>.json`, `sent_GW-7C4FADAE69C8.tsv`, phone screenshots `B<n>_<testid>_<ios|android>_<what>.png`, build files `build_cp7.log`, `size_cp7.txt`, `mapsearch_cp7.txt`, `hashes_cp7.txt`, `B6_cycles_pc.csv`.

**Notes template** (one per block; **never write a Wi-Fi password into a notes file**):
```text
# B<n> <scope> - 2026-10-07 (CP7 b651701)
ELF SHA256 first 9 hex: ________   PC clock offset: ____ s   Device set: SS-V4 | SS-VC | empty
Bench AP: SSID ______, channel __, outage method ______ (black-hole check: silent / fast)
Valve: L | U (PSU off) | shielded;  PSU __.__ V
HH:MM:SS  action or observation  (one line per wet, dry, AP off/on, WAN out/in, PSU change,
          reset press, valve-button press, phone tap, what the phone showed, LED colour)
Tests run / skipped / deviations / anomalies:
Phones: model, OS, joined from Settings or the sign-in window, opened by itself? when?
```

### 3.5 Sending results to Claude

**After each block:**
1. Close the UART log (Ctrl+T Ctrl+L) and stop the `az` monitor; move both into the day folder under the block's name; save `B<n>_notes.md`.
2. Type one line in the Claude session, for example: *"B2 done 14:31, files in 2.1.4_bench\2026-10-07_cp7; ran D1-D6, skipped D3b; odd thing at 14:12, see notes."*
3. Start the next block's captures and carry on.

Claude then runs `docs/telemetry/validate_capture.py` and its own checks (every quoted line by the ESP times, the D4 figures, the pulse and `[SUMMARY]` bounds), and answers with a Pass / Fail / Known-limit table, the figures each test asks for, and anything that changes a later block.

| After | Wait for Claude's answer? |
|---|---|
| B0 | **Yes, for the build gate** (about 10 min). Start B1 meanwhile; B1 is discarded if the gate fails. |
| B1 | No, but read its verdict before B2's reset: a safety Fail stops the day. |
| B2-B6 | No. Claude's answer may add a re-run to a later block. |
| Soak | The first 10 min the same evening; the rest the next morning. |

### 3.6 C2D, twin and the payload files

In T3 (Git Bash), once:
```bash
HUB=resi-apex-iot-dev; RG=resi-apex-rg-dev; GW=GW-7C4FADAE69C8
cd "/c/Work/Projects/EfloStop 2/Firmware/Production/2.1.4_bench/2026-10-07_cp7"
c2d() {   # c2d '<one-line json>'   or   c2d @file.json      (DEV=<GW2> c2d ... for the spare hub)
  local d="$1"; shift; local dev="${DEV:-$GW}"; local body="$d"
  [ "${d#@}" != "$d" ] && body="$(cat "${d#@}")"
  printf '%s\t%s\n' "$(date -u +%s)" "$(printf '%s' "$body" | tr -d '\r\n')" >> "sent_${dev}.tsv"
  az iot device c2d-message send -n "$HUB" -g "$RG" -d "$dev" --data "$d" "$@"
}
twin()    { az iot hub device-twin show -n "$HUB" -g "$RG" -d "${DEV:-$GW}" > "twin_$1.json"; echo "saved twin_$1.json"; }
desired() { az iot hub device-twin update -n "$HUB" -g "$RG" -d "${DEV:-$GW}" --desired "$1" > /dev/null && date -u; }
az iot device c2d-message purge -n "$HUB" -g "$RG" -d "$GW"     # anything queued from earlier days
twin before
desired '{"snapshot_interval_s":60}'
```
Expect `IOTHUB: Twin desired patch: <json>` and `IOTHUB: Twin: snapshot_interval_s = 60`; `TELEMETRY_V2: Snapshot interval set to 60s (persisted)` only if the value changed. Confirm in `twin_before.json` that `properties.reported.snapshot_interval_s` is 60.

**Rules:**
- A C2D sent while the hub is offline is queued and delivered at the next connect. Send nothing during outages, portals or reboots unless the test says so.
- Paste JSON from the payload files, never from Word or Outlook: a smart quote breaks the envelope, and a broken envelope runs the legacy keyword scan (test plan 0.8). A `IOTHUB: C2D cmd='<cmd>' ver=0 id=''` line means a message fell through to the legacy parser: stop and check it (except CP7-C6 row 1).
- **Never put a legacy keyword** (`VALVE_OPEN`, `VALVE_CLOSE`, `LEAK_RESET`, `OVERRIDE_CANCEL`, `DECOMMISSION…`) in an `id`, label or name: use `vo-1`, not `valve_open-1`.
- Make every `id` unique: add `-2`, `-3` … when you send a line again.

**`payloads.txt`** (fill in `<BLE4>` and the hub's name; one line each):
```text
{"schema":"eflostop.cmd","ver":1,"id":"ss-v4m-1","cmd":"provision","payload":{"valve_id":"00:80:E1:27:F7:BB","ble_leak_sensors":["00:80:E1:2A:29:FC","00:80:E1:2A:B6:8E","00:80:E1:2A:CB:B6","<BLE4>"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"00:80:E1:2A:29:FC","location_code":"kitchen","label":"Sink"},{"sensor_type":"ble_leak_sensor","sensor_id":"00:80:E1:2A:B6:8E","location_code":"laundry","label":"Washer"},{"sensor_type":"ble_leak_sensor","sensor_id":"00:80:E1:2A:CB:B6","location_code":"bathroom","label":"Ensuite"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE4>","location_code":"bathroom","label":"Main"}],"auto_close_enabled":true}}
{"schema":"eflostop.cmd","ver":1,"id":"ss-vc-1","cmd":"provision","payload":{"valve_id":"00:80:E1:27:F7:BB","ble_leak_sensors":["00:80:E1:2A:29:FC","00:80:E1:2A:B6:8E","<BLE4>"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"00:80:E1:2A:29:FC","location_code":"kitchen","label":"Sink"},{"sensor_type":"ble_leak_sensor","sensor_id":"00:80:E1:2A:B6:8E","location_code":"laundry","label":"Washer"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE4>","location_code":"bathroom","label":"Main"}],"auto_close_enabled":true}}
{"schema":"eflostop.cmd","ver":1,"id":"dec-1m-1","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"00:80:E1:2A:CB:B6"}}
{"schema":"eflostop.cmd","ver":1,"id":"rc-def-1","cmd":"rules_config","payload":{"auto_close_enabled":true,"trigger_mask":7}}
{"schema":"eflostop.cmd","ver":1,"id":"vo-1","cmd":"valve_open"}
{"schema":"eflostop.cmd","ver":1,"id":"lr-1","cmd":"leak_reset"}
{"schema":"eflostop.cmd","ver":1,"id":"oe-1","cmd":"override_enable"}
{"schema":"eflostop.cmd","ver":1,"id":"oc-1","cmd":"override_cancel"}
{"schema":"eflostop.cmd","ver":1,"id":"name-1","cmd":"set_hub_name","payload":{"name":"<original hub name>"}}
```
`dec-1m-…` and `ss-vc-…` assume `<BLE1M>` = `<BLE3>`: change the MAC if CP7-B0 finds another. `ss-vc-…` is for an empty hub (CP7-G8); on `SS-V4`, use `dec-1m-…`.

**`payloads_destructive.txt`** (opened only for CP7-G8's smoke trio; every MAC filled in; the trio's own lines from the test plan are rewritten for `SS-VC` in 6.3):
```text
{"schema":"eflostop.cmd","ver":1,"id":"smoke-6","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE4>"}}
{"schema":"eflostop.cmd","ver":1,"id":"smoke-8","cmd":"decommission","payload":{"target":"valve"}}
{"schema":"eflostop.cmd","ver":1,"id":"smoke-8-vo","cmd":"valve_open"}
{"schema":"eflostop.cmd","ver":1,"id":"dec-11-s1","cmd":"rules_config","payload":{"auto_close_enabled":true,"trigger_mask":3}}
{"schema":"eflostop.cmd","ver":1,"id":"smoke-9a","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"00:80:E1:2A:29:FC"}}
{"schema":"eflostop.cmd","ver":1,"id":"smoke-9c","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"00:80:E1:2A:B6:8E"}}
```

### 3.7 State check at the start and end of every block (2 min)

From the last snapshot (`TELEMETRY_V2: Pub snapshot: {…}` or the IoT Hub capture): valve linked and open, `rmleak` false, no override; `"rules":{"auto_close_enabled":true,"trigger_mask":7}`; `system_health.reason` "All devices healthy"; the last `FLEET_LED: rating=<r> color=GREEN effect=SOLID`. Also: every sensor dry, PSU at 6.00 V, VB powered, interval 60 s, the hub on the right network, no phone or laptop on `WiFi-Hub-69C8`, and the device set the block expects (3.2).

To restore: valve closed → `vo-…` (refused while latched: wait for the auto-clear, or `lr-…` once every sensor is dry); an override → `oc-…`; after any mask change → `rc-def-…`, then `RULES_ENGINE: Config updated: auto_close=enabled triggers=0x07`; interval not 60 s (the overnight soak sets 300 s) → `desired '{"snapshot_interval_s":60}'`, then `TELEMETRY_V2: Snapshot interval set to 60s (persisted)`.

### 3.8 Report at once

**Stop, keep the monitor running 30 s, save both captures and send them** if you see any of these:
- **A reboot you did not cause:** a `rst:` line other than a reset's own; the `(N)` counter dropping back; `Guru Meditation`, `abort() was called`, `***ERROR*** A stack overflow in task …`, `assert failed`, a task watchdog **(ESP-IDF)**.
- **Safety:**
  - `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window` with nobody at the valve button (day 1 presses it nowhere; T3-15 on day 2 does: write each press time in the notes, that press is exempt);
  - `RULES_ENGINE: Reconnected: hub incident + valve open + RMLEAK clear — inferring physical override, starting 24h window`;
  - `BLE_VALVE: [CMD] Writing Valve=0` before `BLE_VALVE: [CMD] Writing RMLEAK=1` for one incident, or `BLE_VALVE: [CMD] Applying pending valve command=0` before `BLE_VALVE: [CMD] Applying pending RMLEAK command=1`;
  - `RULES_ENGINE: Leak from <type> sensor <id> ignored - not in this hub's device list` for a sensor provisioned on this hub;
  - a valve that did not close on a wet report from a listed sensor; the valve open (`BLE_VALVE: [DATA] Valve State=1 (OPEN)`) for 10 s or more while its own probe reads `BLE_VALVE: [DATA] Leak=1 (LEAK)`.
- **The radio policy (15t, 15u, 15w "never"):**
  - `RADIO: Profile self-test FAILED (<row>: <why>) - pinned to NORMAL (N_CODED), no Wi-Fi pulses`;
  - `RADIO: I2: BLE went <N> ms with no Coded scan (limit 2800 ms) - send this log` (**WB-CONC-1:** N ≤ 3,500 ms within 1.5 s after a claim pulse's end, above all a failed claim, is the known open finding, exempt as 2.3 defines it: send it all the same, with the claim lines);
  - a `RADIO: [SUMMARY] pulses …` line with `(N over 2900 ms)`, N > 0, or `at most Y s in 60 s` with Y > 12.0;
  - `RADIO: Duty watchdog: BLE scanning ran <p> % of its expected time for <n> min (below 80 %)`;
  - `RADIO: Sensor-starvation guard: a sensor unheard 250 s or more (<kind>) - <row> for 110 s` with every listed sensor live (CP7-D8's battery-out sensor is exempt: there it is the expected line);
  - `BLE_VALVE: [CLAIM] NimBLE lost the valve connect in flight (still in flight 2 s past its pulse) - …` or `BLE_VALVE: [CLAIM] A connect this module no longer tracks (cancelled, or NimBLE's own) still runs 2 s past its pulse - …` (the host-reset path has never run on hardware: check `BLE_VALVE: [HOST] NimBLE stack reset: reason=<n>` and `BLE_LEAK: Extended passive scan started (1M + Coded PHY)` within about 1 s; a `BLE_VALVE: [CONNECT] Connect not started by this module (NimBLE re-attempt) cancelled` right after is its companion);
  - `APP_WIFI: radio policy: SoftAP <up|down> by the Wi-Fi mode but <…> by the wifi_manager callbacks - the radio modes follow the callbacks`;
  - a repeating `BLE_LEAK: Failed to start ext scan: <rc>, will retry`; `BLE_LEAK: Scan overdue: …`; `BLE_LEAK: Scan not active (external cancel?), restarting`;
  - `BLE_VALVE: [DISCONNECT] Handle <h> is not the tracked link (<t>) - ignored`; `BLE_VALVE: [GAP] Unhandled event: 8 (0x08)` or `: 19 (0x13)`; `BLE_VALVE: [SCAN] Already connected`; `BLE_VALVE: [SCAN] ble_gap_connect rc=<n>`;
  - any `NimBLE:` INFO line other than `ogf=0x…, ocf=0x…, hci_err=0x…` (and each of those is a finding in itself); `GAP procedure initiated` must be gone;
  - `APP_WIFI: Wi-Fi list scan not started: internal DMA free <N> B (needs 24576) - the page asks again later` with only one phone on the SoftAP;
  - **a line of the deleted window or holds:** `portal priority`, `Scan paused - Wi-Fi setup portal has the radio`, `Scan resumed - Wi-Fi setup portal closed`, `Valve scan held`, `[PORTAL]`, `Wi-Fi radio hold`, `HEALTH_ENGINE: BLE scanning paused`.
- **Cloud, never on a normal run:** `TELEMETRY_V2: TX queue full (<n>) - <kind> not sent`; `called on iothub_task - refused`; `TELEMETRY_V2: Pub <kind> not confirmed (msg_id=<id>) - not kept`; `IOTHUB: cloud_tx: creation failed - rebooting`; `IOTHUB: SNAP result outstanding for <s> s - cloud_tx busy`; `MONITOR: LOW HEAP WARNING: <n> bytes free (watermark=8192)`; `IOTHUB: SAS: esp_mqtt_set_config failed (<err>) — client destroyed, rebooting`; `TELEMETRY_V2: Snapshot not built - out of memory` (except within 5 s of a first `set_hub_name`); `TELEMETRY_V2: Snapshot is <N> B, over the …` on this 4-sensor hub; two `boot` or `fast` snapshots on one connect; an `esp-tls` write error, mbedTLS −0x7200 or −0x7780, or a failed handshake **(ESP-IDF)**; `IOTHUB: cloud admitted … whatever the heap …` or `… below the heap gate …`; `IOTHUB: MQTT stop refused <n> times (<why>) - taken as stopped, no client task seen` followed later by esp-mqtt's `Client has started` **(ESP-IDF)**. (A lone esp-mqtt `Client asked to stop, but was not started` followed by `IOTHUB: MQTT stop refused 1 time(s) - the client had just started` is expected, 15q.)
- **Busy locks (WP2d, WP3), never on a normal run:** `RULES_ENGINE: Rules lock busy for 1 s - …`, `RULES_ENGINE: Provisioning busy for 1 s - leak from …`, `RULES_ENGINE: Rules lock busy - <n> leak reports already kept, the <kind> one from <type> sensor <id> is lost`, `IOTHUB: Boot: rules engine missed the device list or rules - reading them again in the loop`, `PROVISIONING: Failed to take mutex in get_rules_config`.
- **Twin order (WP2e):** a session whose last `IOTHUB: Twin reported (<n>): {…}` misses a change applied in that session, with the session still up about 7 s later.
- **LoRa** (only with a LoRa sensor): `APP_LORA: Rx Queue Full! Packet dropped.`

---

## 4. B0: build, flash and boot (P1, 50 min)

### 4.1 Before the build (5 min)

- PC: `powercfg /change standby-timeout-ac 0`; note the PC clock offset (`w32tm /stripchart /computer:time.windows.com /samples:1`).
- Create the day folder and both payload files (3.6), with `<BLE4>` filled in.
- `..\sdkconfig.pre_cp7` (the 2.1.3 baseline's `sdkconfig`, UPG-1) is created by the build block below if it does not exist yet.

### 4.2 The build (HANDOFF 15t's block, with a `cd` and the log's copy added; ESP-IDF 5.5.1 PowerShell, project folder)

Save the outputs as `build_cp7.log`, `size_cp7.txt`, `mapsearch_cp7.txt` and `hashes_cp7.txt` in the day folder. `idf.py fullclean` needs registry access (offline: `Remove-Item -Recurse -Force build`).

```powershell
cd "C:\Work\Projects\EfloStop 2\Firmware\Production\eFloStop_WiFiHub_idf1"
git log --oneline -1
git diff --stat b651701 HEAD -- main components sdkconfig.defaults CMakeLists.txt partitions.csv dependencies.lock ':(exclude)*.md'
git status --short
Get-FileHash sdkconfig
(Get-Content sdkconfig).Count
New-Item -ItemType Directory -Force "$env:TEMP\ref_cp7_before" | Out-Null
Copy-Item build\eFloStop_WiFiHub_idf1.map, build\eFloStop_WiFiHub_idf1.elf, build\eFloStop_WiFiHub_idf1.bin, sdkconfig "$env:TEMP\ref_cp7_before\"
if (-not (Test-Path ..\sdkconfig.pre_cp7)) { Copy-Item sdkconfig ..\sdkconfig.pre_cp7 }
# 1. The sdkconfig regeneration: delete the fourteen lines, let the defaults fill them
$re = '^(# )?CONFIG_(MBEDTLS_SSL_OUT_CONTENT_LEN|MBEDTLS_SSL_KEEP_PEER_CERTIFICATE|ESP_WIFI_DYNAMIC_RX_BUFFER_NUM|ESP_WIFI_DYNAMIC_TX_BUFFER_NUM|BT_NIMBLE_ENABLE_CONN_REATTEMPT|BT_NIMBLE_MAX_CONN_REATTEMPT|DEFAULT_AP_MAX_CONNECTIONS|DEFAULT_AP_CHANNEL|BT_NIMBLE_LOG_LEVEL(_NONE|_ERROR|_WARNING|_INFO|_DEBUG)?)[ =]'
(Get-Content sdkconfig) | Where-Object { $_ -match $re }
(Get-Content sdkconfig) | Where-Object { $_ -notmatch $re } | Set-Content sdkconfig
(Get-Content sdkconfig).Count
idf.py reconfigure
Compare-Object (Get-Content "$env:TEMP\ref_cp7_before\sdkconfig") (Get-Content sdkconfig)
Get-FileHash sdkconfig
Get-FileHash build\config\sdkconfig.h
# 2. The build
idf.py fullclean
idf.py build *> "$env:TEMP\build_cp7.log" ; "exit=$LASTEXITCODE"
Select-String -Path "$env:TEMP\build_cp7.log" -Pattern 'warning:|error:' | ForEach-Object Line
Select-String -Path "$env:TEMP\build_cp7.log" -Pattern 'gz_asset\.py' | ForEach-Object Line
idf.py size
Get-FileHash sdkconfig
Select-String -Path build\eFloStop_WiFiHub_idf1.map -Pattern '^\s+0x\w+\s+(ble_hs_synced|ble_hs_sched_reset|esp_wifi_set_scan_parameters|httpd_sess_trigger_close|cJSON_PrintBuffered|cJSON_PrintPreallocated|radio_policy_init|radio_policy_exec_wifi_grant|wifi_manager_set_scan_gate|ble_valve_claim_overrun|_binary_index_html_gz_start|_binary_code_js_gz_start|_binary_style_css_gz_start)\s*$' | ForEach-Object Line
Select-String -Path build\eFloStop_WiFiHub_idf1.map -Pattern '^\s+0x\w+\s+(ble_adv_reattempt|ble_conn_reattempt|reattempt_conn|_binary_index_html_start|_binary_code_js_start|_binary_style_css_start|radio_lab_init|radio_lab_key|wifi_manager_lab_pre_start|app_wifi_portal_priority_active|app_wifi_radio_hold_active|health_set_ble_scan_paused)\s*$' | ForEach-Object Line
# 3. Phase 3 is in the image (both print True)
Select-String -Path build\eFloStop_WiFiHub_idf1.bin -SimpleMatch -Quiet -Pattern 'paced: a SUBMIT or JOIN pulse began less than 45 s before'
Select-String -Path build\eFloStop_WiFiHub_idf1.bin -SimpleMatch -Quiet -Pattern 'SUBMIT pulse not granted before its Connect ended'
(Get-Item build\eFloStop_WiFiHub_idf1.bin).LastWriteTime ; (Get-Item build\eFloStop_WiFiHub_idf1.bin).Length
(Get-FileHash build\eFloStop_WiFiHub_idf1.elf).Hash
git status --short
Copy-Item "$env:TEMP\build_cp7.log" "C:\Work\Projects\EfloStop 2\Firmware\Production\2.1.4_bench\2026-10-07_cp7\"
```

If `sdkconfig`'s hash already is `9E13270C4A2D05B0781A88841318160C588683CE06D1574935C17308AAE0412D` (regenerated before), skip step 1.

### 4.3 Build pass criteria (VAL-01 steps 1-7; HANDOFF 15t)

| # | Check | Pass | If not |
|---|---|---|---|
| B1 | `git log`, the `git diff --stat` | HEAD is `b651701` or a later commit; the diff prints **nothing** | a firmware change after CP7: stop |
| B2 | `git status --short` before | only the usual entries (`.vscode/settings.json`, the two 2.1.3 docs, `.adsum/`, `main/.adsum/`, `docs/telemetry/app_req_v5/`, `docs/telemetry/build_app_requirements_v5.py`, `docs/field_logs/2.1.4/CP6_FULL_TEST_PLAN.md`) | report it |
| B3 | `sdkconfig` before | `98F3B2CC…AE759767` (2,841 lines), or `3B5BDCEE…C0E209` (2,842 lines) if a phase-2 configure ran; `..\sdkconfig.pre_cp7` has the same hash | another hash: stop, send it |
| B4 | the filter | exactly **14** lines; 2,827 lines left; 2,840 after the reconfigure | stop |
| B5 | `Compare-Object` | exactly the **25** lines of 15t (24 if the hash was `3B5BDCEE…`) | stop: send it |
| B6 | hashes after step 1 | `sdkconfig` **`9E13270C4A2D05B0781A88841318160C588683CE06D1574935C17308AAE0412D`**; `build\config\sdkconfig.h` **`602CC2C4BA96B4EBA0EFDA8944671B7A78284A4A139B1503F4E24D48BF695FB1`** | record and send; a different hash needs a look before the build |
| B7 | configure and build | `Processing 4 dependencies:`, no re-solve, `dependencies.lock` unchanged, `-- Components:` lists `wifi_portal`; `exit=0` | an `#error "I14: …"` or `"I5: …"`: step 1 did not take |
| B8 | warnings | **exactly the four known:** `app_ble_valve.c:106:9`, `app_lora.cpp:194:5` ×2, `app_lora.cpp:160:13`; no `error:` | any other warning is a Fail |
| B9 | `gz_asset.py` | three lines: `index.html 17134 B -> index.html.gz 3712 B`, `code.js 17973 B -> code.js.gz 5678 B (33 comment lines left out, 55 blank lines, the others trimmed)`, `style.css 17099 B -> style.css.gz 3766 B`; no `FAILED`, no `more than one 5760 B TCP send buffer` | stop |
| B10 | IRAM | DIRAM `.text` **exactly 113,387 B**; IRAM 16,384 B (100 %) | stop; send both maps |
| B11 | sizes (record) | `.bss` about **41,872 B** (41,810-41,930; about 43,880 B means the re-attempt option is still on); `.data` about **21,595 B** (21,587-21,603); flash `.text` 1,066,450-1,069,850 B; `.rodata` 343,350-345,050 B; total image 1,562,400-1,565,900 B (about 25.5 % of 2 MB free) | outside: look, not a Fail |
| B12 | map searches | the first prints **13 lines** (ten at `0x420…`, three `_gz_start` at `0x3c…`); the second prints **nothing** | stop |
| B13 | phase 3 | both `Select-String … -Quiet` print `True` | no output: not CP7 (probably `52ef6a2`): stop |
| B14 | `.bin`, ELF | `.bin` newer than 2026-10-07 06:37:11 +1100; record its length and the ELF SHA256; `git status --short` as in B2; `sdkconfig` still `9E13270C…` | report |

**Send:** `build_cp7.log`, the `idf.py size` output, the filter's 14 lines, the `Compare-Object` output, the three hashes, both map searches, the `.bin` length and time, the ELF SHA256.

### 4.4 Flash and the first boot (VAL-01 step 8; HANDOFF 15t "The boot")

**Flash without the reset**, so that CP7's real first boot is the captured one (`idf.py flash` resets the chip at its end, `CONFIG_ESPTOOLPY_AFTER="hard_reset"`, and that uncaptured boot would save the first PHYs learned at once and connect to the cloud). In T1, from the build folder (esptool 4.11 in the IDF environment; keep the single quotes: a bare `@name` is splatting in PowerShell):
```powershell
cd build; python -m esptool --chip esp32s3 -p COM30 -b 460800 --before default_reset --after no_reset write_flash '@flash_args'; cd ..
```
Then start a fresh IoT Hub capture, the monitor with `--no-reset`, its log (Ctrl+T Ctrl+L), and press Ctrl+T Ctrl+R: that is the first app boot. **Never `erase-flash`.** *If `idf.py -p COM30 flash` was used anyway:* the captured boot is the second. If it prints `BLE_LEAK: PHY table loaded: <k> of 4 …`, the uncaptured boot saved k PHYs: item 7 reads that line instead of "no `PHY table loaded` line", item 8 counts 4 − k `PHY learned` lines, the first NVS write on `ble_leak_scan` was not captured (4.5's save is then the first one watched), and item 10's `lifecycle` can show twice at IoT Hub.

**Expect, in boot order** (the hub has `SS-V4` provisioned, Wi-Fi saved on the bench AP). If the boot's `PROVISIONING: …` lines show another device set (the dead `…2B:A5` still listed, for example), send `ss-v4m-1` once the cloud is up and read items 7-9 from its `BLE_LEAK: Whitelist reloaded: 4 sensor(s)` on; if the hub is not on the bench AP yet, set it up with a reset and a phone first (that reset is not in 5.2's ledger).
1. `app_init: App version:      2.1.4` and `app_init: ELF file SHA256:` with the first 9 hex of B14's hash **(ESP-IDF)**.
2. `HUB_IDENT: Firmware version: v2.1.4`, `HUB_IDENT: Gateway ID : GW-7C4FADAE69C8`, `HUB_IDENT: WiFi STA MAC: 7C:4F:AD:AE:69:C8`, `APP_WIFI: AP SSID: WiFi-Hub-69C8`.
3. `PROVISIONING: Loaded existing config from NVS`, `PROVISIONING: State: PROVISIONED`, `PROVISIONING: BLE leak sensors: 4`.
4. `APP_WIFI: bench build (APP_BENCH_DIAG): Wi-Fi driver log at INFO - not for release` (W).
5. `IOTHUB: cloud_tx started (stack 5120 B, priority 3)`, once, before `APP_WIFI: Connected! IP: <IP>`; `IOTHUB: Starting BLE (valve=00:80:E1:27:F7:BB, BLE sensors=4)`.
6. `BLE_VALVE: [HOST] NimBLE host task started`, `BLE_VALVE: [SM] Fixed Passkey: configured (not logged)`, `APP_LORA: Initializing LoRa Driver...`.
7. BLE, in this order: `BLE_LEAK: NimBLE ready, initializing scanner`; **`RADIO: Profile self-test passed: 14 rows hold I1, I2, I8 and the period rule (SERVE rung SERVE-A)`**; `BLE_LEAK: Whitelist reloaded: 4 sensor(s)`; **no** `PHY table loaded` line (CP7's first boot has no PHY table); `RADIO: Mode NORMAL (SoftAP down, or STA connected)`; `BLE_LEAK: Extended passive scan started (1M + Coded PHY)`; `BLE_LEAK: Scan mode N_MIXED (a sensor on 1M, or a sensor's PHY not known yet): 1 s on 1M and 1 s on Coded in turn, each next after 0-100 ms`.
8. One `BLE_LEAK: eleak <MAC> PHY learned: <Coded|1M> (was unknown)` per sensor, then `BLE_LEAK: PHY table saved: <N> sensor(s) known, <M> on 1M` (**the first NVS write on `ble_leak_scan`: watch for a stack-canary panic**). **Write down which sensor learned `1M`: that is `<BLE1M>`.**
9. The valve: `BLE_VALVE: [SCAN] Target MAC matched - connecting to provisioned valve: 00:80:E1:27:F7:BB`, `BLE_VALVE: [CLAIM] Connecting to the valve: pulse up to <N> ms` (N 1500-2500), the `GAP CONNECT EVENT` and `SETUP COMPLETE - READY FOR GATT` banners.
10. Cloud: `APP_WIFI: Connected! IP: <IP>`, then `IOTHUB: cloud admitted <s> s after the IP (internal DMA free <X> B, largest <Y> B)` with X about 50-53 KB, then `IOTHUB: Connected to Azure IoT Hub!`. No SoftAP at this boot. IoT Hub: the `lifecycle` once, the twin, one `boot` or `fast` snapshot.
11. Every 60 s, the two `RADIO: [SUMMARY]` lines: `RADIO: [SUMMARY] modes NORMAL <s> s, NORMAL_LR 0 s, …; BLE scanning <a> of <b> s (<Z> %); LR overlay 0 s; PHY changes <n>; adverts <id>=<n><C|M|?> …` with Z ≥ 90, and `RADIO: [SUMMARY] pulses JOIN 0, SUBMIT 0, … longest Coded gap <N> ms (0 over 2900 ms); …`.

**The bench keys:** `d` → `APP_LORA: Stats: RX=<n>, ACKs=<n>, LastRSSI=<n>` within about 0.1 s. (Leave `a`, `s`, `r` alone.)

**Pass:** items 1-11 hold; **no** `RADIO_LAB:` line; none of 3.8; none of `IOTHUB: QueueSet: <q> queue could not be added…`, `IOTHUB: cloud admission deferred: internal DMA free …`, `IOTHUB: lifecycle not built in 1 s - live messages go first`.

### 4.5 The second boot and the heap baseline (10 min)

1. Wait until every sensor has its `PHY learned` line **and** a `BLE_LEAK: PHY table saved: 4 sensor(s) known, <M> on 1M` line has printed after the last of them (saves are at most one a minute). Then Ctrl+T Ctrl+R.
2. Expect `BLE_LEAK: PHY table loaded: 4 of 4 sensor(s) known, <M> on 1M`, and with M ≥ 1 the N_MIXED line again (it stays while a sensor is known on 1M); with M = 0, `BLE_LEAK: Scan mode N_CODED: 1M 20 % + Coded 80 %, 1 s scans, each next after 0-100 ms`.
3. **Heap at rest** (connected, SoftAP down, 5 min untouched): record the median `free` of `MONITOR: heap: free=<n> min_ever=<n> largest_blk=<n> uptime=<s>s` and the `idma:` line's `free`, `largest`, `min_ever` and `allocfail` (`MONITOR: idma: free=<n> min=<n> largest=<n> min_largest=<n> allocfail=0 min_ever=<n>`). `allocfail` must stay 0 all day. Claude compares with CP5's (15t expects `free` about 8.6-11.6 KB above CP5's in the same state). This is WP9's M-heap baseline too.
4. **CP7-B0-Ta (Claude, from B0 and B1; G0's Ta check).** Per sensor, over at least 5 of its `BLE_LEAK: eleak <MAC> burst: n=<n> in <s> s, dT <a>-<b> ms, phy=<phy>` lines (only the first 4 tracked sensors log bursts), the smallest `<a>` must be **≤ 448 ms** (`RP_ADV_SMAX_MS`: FW 1.1.0's Ta of at most 437.5 ms plus advDelay 10 ms, `SENSOR_FW_1_1_0_TIMINGS.md` T1). Claude records Ta per sensor and n per burst per mode (G0's burst counts). A smallest `dT` over 448 ms on a sensor whose bursts show n ≥ 4 is **NO-GO** (2.3); with fewer adverts per burst (a weak sensor that may miss every other advert) Claude collects more bursts before judging. The results sheet has its row.
5. The 3.7 state check. **Send the B0 files.**

---

## 5. Timeline

### 5.1 Day 1

The times assume B0 at 10:30; shift them all if it starts later. Each block's length is the sum of its tests' minutes. A day of about 10 hours; 5.3 says what to cut to stop at about 19:00.

| Block | Time | Length | Runs [minutes] | Set, valve, network | Send at the end |
|---|---|---|---|---|---|
| **B0** Build and boot | 10:30-11:20 | 50 | 4.1 [5], 4.2 build [25], 4.3, 4.4 [10], 4.5 [10] | `SS-V4`, L, bench AP | build files, `B0_*`. **Wait for the build verdict.** |
| hand-off | 11:20-11:25 | 5 | 3.5, 3.7 | | |
| **B1** Leak-safety core | 11:25-13:15 | 110 | 6.1: L1-L4 [26], L5 LoRa [10, or N/A], L6 switch to `SS-VC` [4], L7 P14 [12], L8 leak response and the 600 s cap [14], L8b the valve power-cycled in the incident ×3 [6], L9 P11-U [8], L10 P11-L [6], L11 T3-11 [5], L12 T5-04 + T5-05 [10], L13 T5-06 [9] = 110 | `SS-V4` → `SS-VC`; L, U | `B1_*` |
| Lunch | 13:15-13:45 | 30 | Claude reads B0-B1. Leave the hub untouched (heap at rest again). | | — |
| **B2** D2 in the reset portal | 13:45-14:30 | 45 | 6.2: D1 reset #1 [4], D2 [5], D3 + D3b [10], D4 with a second sensor [7], D5 [4], D6 [9], D7 [3], D8 the starvation guard, inside D1-D3 [1] = 43 | `SS-VC`; L, then U, then L | `B2_*`, phone notes |
| hand-off | 14:30-14:35 | 5 | | | |
| **B3** Phones | 14:35-16:35 | 120 | 6.3: G1-G6 S2 runs ×6 (resets #2-#7; G4 first Connect, G5 wrong-then-right, G6 Forget, G3 P-14 LAN half, G2 two phones) [50], G7 S3 ×4 incl. the wrong password [30], G8 smoke trio + S1 ×4 (resets #8-#11) + `ss-vc-…` [36] = 116 | `SS-VC` → empty → `SS-VC`; L | `B3_*`, phone notes, screenshots |
| hand-off | 16:35-16:40 | 5 | | | |
| **B4** Router outage | 16:40-17:30 | 50 | 6.4: R1 S-2 [7], R2 S-3 + rejoin [6], R3 fallback leak + RETRY [10], R4 leak at the pull [8], R5 G3-lite ×2 [12], R6 reset #12 idle [6] = 49 | `SS-VC`; L; bench AP off/on | `B4_*` |
| hand-off | 17:30-17:35 | 5 | | | |
| **B5** Cloud and WAN black-hole | 17:35-19:15 | 100 | 6.5: C1 black-hole check [3], C2 LS-1 ×3 [45], C3 quiet hub [5], C4-C9 contract [45], C10 Claude = 98 | `SS-VC`; L; DROP rules | `B5_*`, T0 of each run, `twin_*.json` |
| hand-off | 19:15-19:20 | 5 | | | |
| **B6** Valve power cycles | 19:20-20:25 | 65 | 6.6: set-up with nRF Connect [8], V1 cycles 1-60 × 55 s [55] = 63 (61-100 on day 2) | `SS-VC`; PSU on/off | `B6_*`, `B6_cycles_pc.csv` |
| End of day | 20:25-20:40 | 15 | T4-14's grep [5]; `ss-v4m-…` back; soak start (5.4) [10] | `SS-V4` | grep hit counts; the soak's first 10 min |

### 5.2 Reset ledger (the 10 s reset ×10 gate, T4-10 Part H; HANDOFF 15k item 2)

| # | Block | STA state at the press | Phone, network set up | Also |
|---|---|---|---|---|
| 1 | B2 D1 | connected | iPhone, bench AP (in D6) | D2 |
| 2-7 | B3 G1-G6 | connected | iPhone ×3, Android ×3; **#5 (Android) sets up the home router** (P-14's LAN half), #6 back to the bench AP | G-CNA S2 |
| 8-11 | B3 G8 | connected (empty hub) | iPhone ×2, Android ×2 | G-CNA S1 |
| 12 | B4 R6 | **idle** (router-fallback SoftAP, between two retries) | iPhone, bench AP | X-5 idle |
| 13-14 | day 2 | idle | | |
| 15-17 | day 2 | **connecting** (inside a router retry: start the hold 21-22 s after the last `APP_WIFI: WiFi Disconnected. Reason: <n>`) | | X-5 connecting |

**Every run passes** when: `RESET_BTN: Button pressed — starting 10000 ms hold timer`, `RESET_BTN: 10-second hold confirmed — executing WiFi reset`, `RESET_BTN: === LONG PRESS CONFIRMED — CLEARING WIFI CREDENTIALS ===`, `RESET_BTN: Erasing WiFi credentials, then rebooting into AP (commissioning preserved in nvs_prov)...`, `RESET_BTN: Wi-Fi credentials erased from NVS` about 2 s later, `RESET_BTN: Rebooting into AP mode...`; one reboot; then `APP_WIFI: SoftAP up with no saved Wi-Fi credentials (setup portal) - BLE scanning, if any, stays on beside it`; never `RESET_BTN: Wi-Fi NVS lock busy for 3 s - erasing without it` or `RESET_BTN: Wi-Fi credential erase failed (<err>) - rebooting anyway`. With the STA connected, `APP_WIFI: WiFi Disconnected. Reason: 8` prints before the erase line.

### 5.3 If the day runs late: cut in this order (the first first)

1. **B3 G8** (the smoke trio and S1) → day 2's first block [saves 36 min].
2. **B6:** stop after cycle 30 (6.6's interim rule at 30); cycles 31-100 → day 2 (same set-up, same script with `-Start 31`) [about 28 min].
3. **B5 C4, C6, C9** (envelope rules, no-ack and size, mask traps) → day 2 [15 min].
4. **B4 R5's second pull and R6** → day 2 [12 min].
5. **B3 G2** (two phones) [5 min].
6. **B2 D3b** (a wet during a Connect) and **D8** (the starvation guard) [6 min].

**Never cut:** B0; B1 L1-L4 and L6-L10 (with L8b), L12, L13; B2 D1-D6; B3 G1, G3-G7; B4 R1-R4; B5 C1-C3 (LS-1 ×3 and the D4 figures), C5, C7, C8; B6's first 30 cycles.

### 5.4 Overnight soak (P2, from the end of day 1)

1. `SS-V4` back: send `ss-v4m-2` (re-adds `<BLE1M>`; its PHY is unknown again until heard: N_MIXED). Wait for every sensor and the 3.7 check.
2. `desired '{"snapshot_interval_s":300}'`; expect `TELEMETRY_V2: Snapshot interval set to 300s (persisted)`.
3. New UART log with `--no-reset`; new `az` monitor with `--timeout 0`. One activity round: wet and dry `<BLE2>` (auto-close, auto-clear), `lr-…`, `vo-…`.
4. Leave the PC awake, nothing on COM30 but the monitor, no phone on `WiFi-Hub-69C8`, PSU at 6.00 V. **Write the time of the day's last boot** (R6's reboot, reset #12; if R6 was cut, R1's Ctrl+T Ctrl+R; any later reboot, planned or not, counts instead): the SAS renewal comes about 18 h after that boot's first token (`IOTHUB: SAS: within 6 h of expiry — renewing`, then `IOTHUB: SAS: token renewed (valid 24 h, expires ts=<ts>)`). After R1's EN reset the clock is lost, so its first token comes at R2's rejoin (`IOTHUB: SAS: clock valid (ts=<ts>) — minting first token, starting MQTT`): count from that line.

The next morning Claude checks: no reboot; `heap:` and `idma:` flat; one heartbeat every 300 s (−1/+3 s); `[SUMMARY]` every minute with Z ≥ 90 and `(0 over 2900 ms)`; no 3.8 line; after the renewal `IOTHUB: MQTT client stopped on wifi_task in <s> s` and `IOTHUB: Connected to Azure IoT Hub!`. **Recommended:** keep it running until the renewal and use that morning for the hub-free items of 7 (the G1 and G-M builds, the spare-hub lane). It is a CP7 check, not VAL-14.

### 5.5 Later days (detail in 7)

| Day | Block | Content |
|---|---|---|
| Day 2 | D2-1 | The soak's end (after the SAS renewal); then `desired '{"snapshot_interval_s":60}'` (the soak left 300 s; 3.7); anything cut from day 1 (5.3); T2-01 (the smoke's S-7, needs the valve's Critical voltage) |
| | D2-2 | **The 1M cases:** `SS-V4`; G-CNA S2 with a sensor on 1M (AP_K1M); a wet of `<BLE1M>` in AP_K1M and in SERVE |
| | D2-3 | **G-CNA full** (10 runs per device per state, S1/S2/S3, every phone class at hand), the wrong-then-right ×10 per phone, the second phone within 45 s; G8x X-1 … X-7; P-13 and P-14 with the laptop; resets #13-#17 |
| | D2-4 | **Valve and radio gates:** G6b ×20; G6 (a dead valve during a leak with a phone on the portal, router off then on, with the starvation guard's leak-response variant); the remaining power cycles (61-100); CP7-L8b ×3 more and its dry variant; T3-15, T3-17 Part A, CP6-24-style `override_enable` with the valve away (T5-15) and T5-15b (question 11), T5-07; T6-15 (flapping); T3-06 (valve swap during a live incident: LEAK-WB-1); D4b (optional) |
| | D2-5 | **G3b** (30 router power cycles), G5 (forget/rejoin loop with a wet sensor), G7 (list completeness, channel change, hidden SSID, blips ×10), G2 (20 drips per state) |
| Day 3 | D3-1 | **G1 on the lab image** (7.3): build, boot, the ladder's smoke pass (steps 0-5, 3 runs per phone) |
| Day 4-5 | D4 | G1 full (four phone classes, 10 runs per step); the bench-only images (7.8: T6-11 first); **WP9 G-M** (7.6: the pool baseline, W4, W3, W2, W1, W5; W8 after T6-11; never W6) |
| Later | — | Upgrade and rollback (7.5), the spare-hub lane (7.7), the 24 h soaks (7.4), the DEC subset and the T2/T3/T4 rest (7.9), then the RC (2.6) |

---

## 6. Day 1 blocks

**One rule for every block:** inside one incident only the first wet prints `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by <type> sensor <id>` (a later wet, while the valve reads closed with RMLEAK=1 or within 10 s of the last AUTO-CLOSE, returns at DEBUG). Never re-wet a sensor that has not yet reported `leak=0` (a dry report can take 110 s): use another sensor. **At every wetting, in every block, press `d` and write the time in the notes** (Claude's Δw, 2.4; the detection bound of G3/G4).

**The leak lines** (L1's list; later tests say "the leak sequence"):
- `BLE_LEAK: eleak <MAC> — leak=1 batt=<b>% rssi=<r> fw=<fw>`, at most 20 s after the wetting (NORMAL);
- `RULES_ENGINE: LEAK INCIDENT latched by ble_leak_sensor sensor <MAC>` (the first source of an incident);
- `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <MAC>`;
- `BLE_VALVE: [TASK] CMD: SET_RMLEAK`, `BLE_VALVE: [CMD] Writing RMLEAK=1`, **then** `BLE_VALVE: [TASK] CMD: CLOSE_VALVE`, `BLE_VALVE: [CMD] Writing Valve=0`;
- `BLE_VALVE: [DATA] RMLEAK=1 (ACTIVE)` and `BLE_VALVE: [DATA] Valve State=0 (CLOSED)`; then `BLE_VALVE: [LR] The valve confirms the interlock for this incident (RMLEAK=1, CLOSED)`;
- `IOTHUB: Event: BLE Leak <MAC> leak=1 batt=<b>`;
- `FLEET_LED: rating=critical color=RED effect=SOLID`;
- IoT Hub: `leak_detected`, then `auto_close` (`"rmleak_asserted":true`), then an `event` snapshot (valve closed, `rmleak` true, `critical`, "Leak detected: <label>").

**The clear lines** (after drying): `leak=0` within 110 s; `RULES_ENGINE: All sensors clear — auto-clear timer started (10s)`; 10-12 s later `RULES_ENGINE: AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`, `BLE_VALVE: [TASK] CMD: CLEAR_RMLEAK`, `BLE_VALVE: [CMD] Writing RMLEAK=0`, `BLE_VALVE: [DATA] RMLEAK=0 (CLEAR)`; **D1:** about 5 s later `IOTHUB: SNAP trigger=event:rmleak` and a snapshot with `"rmleak":false`; IoT Hub `leak_cleared`, then `rmleak_auto_cleared` (`clear_after_seconds` 10); LED RED → YELLOW → GREEN. The valve stays closed. **Restore:** `vo-…` → ack `ok`, `BLE_VALVE: [TASK] CMD: OPEN_VALVE`, `BLE_VALVE: [CMD] Writing Valve=1`, `BLE_VALVE: [DATA] Valve State=1 (OPEN)`.

### 6.1 B1: the leak-safety core (P1, 110 min)

**Start:** `SS-V4`, VA linked, NORMAL in N_MIXED (4.5). One UART file and one IoT Hub capture for the block. Press `d` and write the time in the notes at every wetting.

#### CP7-L1 … L3: a BLE leak, valve linked, three sensors (P1, 3 × 6 min; = T5-03, 15k-6; the L-spread)

Run L1 with `<BLE1>` (Coded), L2 with `<BLE1M>` (1M), L3 with `<BLE2>` (Coded). Each: wet → the leak sequence → dry → the clear lines → `vo-…`.

**Pass (each run):** Δa ≤ 200 ms; Δc ≤ 1 s; RMLEAK written before CLOSE; IoT Hub order `leak_detected` → `auto_close` → snapshot; D1's snapshot; `valve_open` acked `ok` and the valve opens. **Record** (Claude): Δa, Δp, Δr, Δc, Δs (2.4) per run; the L-spread. **Also in L2:** the 1M sensor heard in N_MIXED (`leak=1` ≤ 20 s).

#### CP7-L4: the valve's own flood probe (P1, 8 min; CP6-23a)

Lay the valve's probe on a wet cloth (keep the battery compartment and PSU leads dry).
- `BLE_VALVE: [DATA] Leak=1 (LEAK)`; `RULES_ENGINE: LEAK INCIDENT latched by valve sensor <id>`; `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by valve sensor <id>`; `Writing RMLEAK=1` before `Writing Valve=0` (the valve may already have closed itself: record it); IoT Hub `leak_detected` and `auto_close` with `"source_type":"valve"`. LED RED.
- `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp7-l4-s2","cmd":"override_enable"}'` → `RULES_ENGINE: override_enable: valve flood probe wet — refusing`; ack error "Water detected at the valve. It can't be opened remotely until the valve area is dry."
- Dry the probe: `RULES_ENGINE: AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`; `rmleak_auto_cleared`; `vo-…` → `ok`, the valve opens.

**Pass:** as listed.

#### CP7-L5: a LoRa leak (P1 if question 6 is yes, else `N/A (no LoRa HW)`; 10 min; T5-13)

Wet `<LORA1>`: `APP_LORA: Verified: ID=0x<id>, Batt=<b>%, Leak=0x<l>, Sent=<n>, Ack=<n>`, `IOTHUB: Event: LoRa Packet from 0x<id>`, `RULES_ENGINE: LEAK INCIDENT latched by lora sensor <id>`, the rest of the leak sequence. No `APP_LORA: Rx Queue Full! Packet dropped.`. Dry; clear; `vo-…`.

#### CP7-L6: switch to `SS-VC` (P1, 4 min)

`c2d` the `dec-1m-1` line. Expect `IOTHUB: !!! DECOMMISSION_BLE: <BLE1M> !!!`, `PROVISIONING: BLE leak sensor <BLE1M> removed successfully`, `HEALTH_ENGINE: Device table loaded: 4 device(s) (+0 added, -1 removed)`, `BLE_LEAK: Whitelist reloaded: 3 sensor(s)` within about 10 s, then `BLE_LEAK: Scan mode N_CODED: 1M 20 % + Coded 80 %, 1 s scans, each next after 0-100 ms` and, within a minute, `BLE_LEAK: PHY table saved: 3 sensor(s) known, 0 on 1M`. Snapshot: 3 sensors, survivors unchanged (BUG-2). From here to the end of B6 the set is `SS-VC`.

#### CP7-L7: P14, a valve missing at boot (P1, 12 min)

1. PSU off (U); all sensors dry. Ctrl+T Ctrl+R; write the time.
2. **Expect:** `BLE_LEAK: Scan mode N_CODED with a valve hunt (valve not linked): 1 s of 1M 20 % + Coded 80 %, then 0.45 s on 1M, each next after 0-100 ms`; LED WHITE after boot (syncing), **RED at about 180 s** (`FLEET_LED: rating=critical color=RED effect=SOLID`), not at 600 s; a snapshot `critical` with "Valve offline" in its reason at about 180 s; the LED never WHITE or GREEN for the whole run after that.
3. **Keep the valve off past 10 min after the boot:** `RADIO: Discovery backed off: its reasons are 10 min old (a PHY unknown, the valve unlinked)` and `BLE_LEAK: Scan mode N_CODED with a valve hunt, backed off (valve unlinked 10 min or more): 1 s of 1M 20 % + Coded 80 %, and every 6 periods 0.45 s more on 1M, each next after 0-100 ms`.

**Pass:** RED and the `critical` snapshot at about 180 s (180-215 s); the backed-off hunt line after 10 min. Go straight to L8 with the valve still off.

**Use the wait** (about 7 min between RED and the back-off line, nothing to do): save `dns_check.py` (A.1) and `pc_cycles.ps1` (A.2) in the day folder, and make CP7-C6's two files (6.5).

#### CP7-L8: a leak with the valve unpowered: the leak response and its 600 s cap (P1, 14 min)

1. Valve still U (L7's end). Wet `<BLE1>` with a folded, soaked towel (it must stay wet 11 min or more); write the time. **Expect:**
   - `leak=1`, `RULES_ENGINE: LEAK INCIDENT latched by ble_leak_sensor sensor <BLE1>`, `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <BLE1>` (Δa ≤ 200 ms);
   - `RULES_ENGINE: AUTO-CLOSE: valve not connected — scanning; close deferred to reconnect reconciliation`;
   - `BLE_VALVE: [CMD] RMLEAK write not ready. Queuing val=1`, then `BLE_VALVE: [CMD] Valve write not ready. Queuing val=0`;
   - `BLE_VALVE: [LR] Leak response pending, valve not linked (RMLEAK/CLOSE pended) - leak-response scanning, at most 600 s for this incident`;
   - `RADIO: Mode NORMAL_LR (a leak response, the valve not linked)` and `BLE_LEAK: Scan mode NORMAL_LR (leak response, valve not linked): 1 s on 1M and 0.6 s on Coded in turn, each next after 0-100 ms`;
   - IoT Hub: `leak_detected`, `auto_close` with `"rmleak_asserted":false`.
2. **Keep `<BLE1>` wet and the valve off for 11 min.** At about 5 and 9 min add a few drops of water to the towel without lifting it. If `<BLE1>` reports `leak=0` before the PSU goes on, L8 is Blocked: redo it (a dry report cancels the pended pair and ends the leak response early, `(leak response written or withdrawn)`).
   - **A second sensor in NORMAL_LR:** about 60 s after `RADIO: Mode NORMAL_LR …`, wet `<BLE2>`; press `d`. Expect its `leak=1` **≤ 60 s** (the row's model p99.9) and `IOTHUB: Event: BLE Leak <BLE2> leak=1 batt=<b>`; no new `LEAK INCIDENT latched`; with the valve unreachable a second `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <BLE2>` and `auto_close` are expected (8 item 1: count them). Dry `<BLE2>` (its `leak=0` well before the cap); `<BLE1>` stays wet, and no `All leaks resolved — pending auto-close cancelled`.
   - Exactly 600 s after the `[LR] Leak response pending` line: `BLE_VALVE: [LR] Leak-response scanning ended after 600 s (the cap per incident - normal scanning, claims go on)`, then `RADIO: Mode NORMAL (SoftAP down, or STA connected)`, `RADIO: Discovery at its full rate again` (L7 left it backed off) and `BLE_LEAK: Scan mode N_CODED with a valve hunt (valve not linked): …` (**not** the backed-off form: an incident is latched).
   - About 5 min after the wet, the sensor's 5 min heartbeat may give a second `AUTO-CLOSE + RMLEAK triggered` and a second `auto_close` (audit finding 2): **count them** (`Known-limit`).
   - Use the wait (B5's router preparation, 3.3): the static lease, the `flow_offloading` check, and the two DROP rules typed with the hub's IP (not inserted).
3. PSU on. **Expect:** `BLE_VALVE: [SCAN] Target MAC matched - connecting to provisioned valve: 00:80:E1:27:F7:BB`; `BLE_VALVE: [CLAIM] Connecting to the valve: pulse up to 2500 ms (leak response pending)`; the `GAP CONNECT EVENT` banner; `RULES_ENGINE: Valve reconnected with 1 active leak(s) — executing auto-close` (or `RULES_ENGINE: Reconnected with 1 active leak(s) — valve already closed + RMLEAK asserted, nothing to do` if the valve closed itself); **`BLE_VALVE: [CMD] Applying pending RMLEAK command=1` before `BLE_VALVE: [CMD] Applying pending valve command=0`** (or the `Replaying pending …` pair, RMLEAK first); `BLE_VALVE: [DATA] RMLEAK=1 (ACTIVE)`, `BLE_VALVE: [DATA] Valve State=0 (CLOSED)`; IoT Hub `device_recovered`, `auto_close` with `"cause":"reconnect"`.
4. Run CP7-L8b now (`<BLE1>` still wet). Then dry `<BLE1>`; the clear lines; `vo-…`.

**Pass:** the LR lines; `<BLE2>`'s `leak=1` ≤ 60 s; the cap line 600 s (±2 s) after the LR line; claims go on after the cap; RMLEAK before CLOSE at the relink; power-on → `Applying pending RMLEAK command=1` and → `[DATA] Valve State=0 (CLOSED)` each ≤ 30 s (E-09's ceiling). **Record:** power-on → `Target MAC matched` → CONNECT (`GAP CONNECT EVENT`, the gated quantity: question 2; past the cap, record only) → `SETUP COMPLETE`; the `Writing` lines at the relink (the pended pair once, plus at most the reconnect's own pair).

#### CP7-L8b: the valve power-cycled during a live incident, after it confirmed the interlock (P1, 6 min; 3 runs)

**Why:** the valve browning out at a motor stroke on a weak battery, or a battery swap during a leak: it comes back with RMLEAK=0 (BKP0R lost) while the hub still holds the incident and its interlock confirmation. Three rules paths meet there (rules_engine.c: the reconnect's Priority 1 and 2, the tick's Check 2), and each could start a false 24 h override. No other test reaches this state (L8, L9, D4 unpower the valve before the incident; G6b cuts power before the confirmation).

Right after L8 step 3, with `<BLE1>` still wet and `BLE_VALVE: [LR] The valve confirms the interlock for this incident (RMLEAK=1, CLOSED)` printed. Three times, each started 60 s or more after the last `SETUP COMPLETE`: PSU off 10 s, then on (write both times).
- **Expect, in order:** `BLE_VALVE: [DISCONNECT] reason=0x208`; the relink (`Target MAC matched`, the claim, the `GAP CONNECT EVENT` banner); the setup's `BLE_VALVE: [DATA] RMLEAK=<v>` and `BLE_VALVE: [DATA] Valve State=<s>` (record both: the valve's state after a cold boot; RMLEAK 0 expected); then RMLEAK asserted before any close: `RULES_ENGINE: Valve reconnected with 1 active leak(s) — executing auto-close`, and/or `BLE_VALVE: [CMD] Applying pending RMLEAK command=1` before `Applying pending valve command=0` (only if the hub's 5 min re-report of the wet sensor fell in the outage and pended the pair: 8 item 1); every `Writing RMLEAK=1` (or applied RMLEAK) before any `Writing Valve=0`; `BLE_VALVE: [DATA] RMLEAK=1 (ACTIVE)` and `BLE_VALVE: [DATA] Valve State=0 (CLOSED)` within 5 s of `SETUP COMPLETE`. Record any `[LR] …` line (the incident's cap is used up).
- **For 60 s after `SETUP COMPLETE`, never:** `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window`, `RULES_ENGINE: Reconnected: hub incident + valve open + RMLEAK clear — inferring physical override, starting 24h window`, an IoT Hub `water_access_override_enabled` (each NO-GO, 2.3).
- If the cold-booted valve reads OPEN (`[DATA] Valve State=1 (OPEN)` in its setup), record it and raise it with the valve firmware owner: with a sensor wet, Priority 1 still closes it, but the override inference needs only every sensor dry (7.2's dry variant).

**Pass:** each run as listed. **Record:** power-on → CONNECT → `SETUP COMPLETE` → the `RMLEAK=1` read-back. Then L8 step 4.

#### CP7-L9: P11-U, a leak across a boot with the valve unpowered (P1, 8 min)

1. PSU off and wait for `BLE_VALVE: [DISCONNECT] reason=0x208`; all dry; the valve was last open. Ctrl+T Ctrl+R; write the reset time; **as soon as the boot banner prints, wet `<BLE2>`** (within the boot's first minute, while the LED is still WHITE; wetting before the reset would latch the incident before the boot).
2. **Expect:** `<BLE2>`'s first `leak=1` → `LEAK INCIDENT latched` → `AUTO-CLOSE + RMLEAK triggered` (Δa ≤ 200 ms) → the pended pair and `[LR] Leak response pending …` (as L8); LED RED once `leak=1` is heard, never back to WHITE while wet; **no** `IOTHUB: Boot: rules engine missed the device list or rules - reading them again in the loop`.
3. After about 2 min, PSU on: the 2500 ms claim, `Applying pending RMLEAK command=1` before `Applying pending valve command=0`, the valve closed; power-on → `Applying pending RMLEAK command=1` and → `[DATA] Valve State=0 (CLOSED)` each ≤ 30 s. **Record** power-on → CONNECT (`GAP CONNECT EVENT`; NORMAL_LR: G6's 5 s, judged statistically once there are 20, question 2, 7.2) and CONNECT → `SETUP COMPLETE`.
4. Dry, clear, `vo-…`.

#### CP7-L10: P11-L, a leak across a boot with the valve linked (P1, 6 min)

As L9 (reset, then wet `<BLE2>` at the boot banner) with the valve powered and open: after the boot, either live (after `SETUP COMPLETE - READY FOR GATT`: `Writing RMLEAK=1`, then `Writing Valve=0`) or pended (the L8 lines, then `Applying pending …` RMLEAK first at the setup). Record which. `BLE_VALVE: [DATA] Valve State=0 (CLOSED)` and `BLE_VALVE: [DATA] RMLEAK=1 (ACTIVE)` within 5 s of `SETUP COMPLETE`. IoT Hub `leak_detected`, `auto_close`, a `critical` snapshot naming "Washer" (or the wetted sensor's label). Dry, clear, `vo-…`.

#### CP7-L11: T3-11, the hub's own clear pended while the valve is out of range (P1 with the RF shield, else day 2; 5 min; smoke S-5)

Test plan T3-11, one run: shield the valve (powered), not power it off. **Pass:** the clear is applied at the relink and never read as a press: no `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window`, no `water_access_override_enabled`.

#### CP7-L12: T5-04 (re-wet at the clear) and T5-05 (the interlock) (P1, 10 min; smoke S-4)

- **T5-04, one run** (test plan T5-04): wet `<BLE1>`, dry it, re-wet it about 10-12 s after its `leak=0`, timed to land at the auto-clear. IoT Hub `rmleak_auto_cleared` → `leak_detected` → `auto_close`; the valve ends closed with `BLE_VALVE: [DATA] RMLEAK=1 (ACTIVE)`; never `RMLEAK cleared externally`. A re-wet that lands before the clear does not count: redo it.
- **T5-05** (test plan T5-05 (a), (c), (e); CP6 plan's version): with `<BLE1>` wet and latched, send `t5-05-reset-wet` (`leak_reset`), `t5-05-open-wet` (`valve_open`) and `t5-05-set-open-wet` (`valve_set_state` open), about 3 s apart:
  ```text
  {"schema":"eflostop.cmd","ver":1,"id":"t5-05-reset-wet","cmd":"leak_reset"}
  {"schema":"eflostop.cmd","ver":1,"id":"t5-05-open-wet","cmd":"valve_open"}
  {"schema":"eflostop.cmd","ver":1,"id":"t5-05-set-open-wet","cmd":"valve_set_state","payload":{"state":"open"}}
  ```
  Expect `RULES_ENGINE: LEAK_RESET refused — 1 leak source(s) still active (use override to open during a leak)`, `IOTHUB: VALVE_OPEN refused — Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.`, `IOTHUB: VALVE_SET_STATE open refused — <same text>`; **no** `Writing RMLEAK=0` or `Writing Valve=1`; acks "A leak is still active. Fix the leak first, or use override to open the valve during a leak." then twice "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.". Then dry `<BLE1>`; as soon as `RULES_ENGINE: All sensors clear — auto-clear timer started (10s)` prints send `{"schema":"eflostop.cmd","ver":1,"id":"t5-05-reset-dry","cmd":"leak_reset"}`: `RULES_ENGINE: LEAK_RESET: clearing incident (hub_latch=<h>, valve_rmleak=<v>, override=0)`, `IOTHUB: Leak incident cleared, RMLEAK reset`, `Writing RMLEAK=0`; IoT Hub `rmleak_cleared`, no `rmleak_auto_cleared` (missed the window: redo). Then `{"schema":"eflostop.cmd","ver":1,"id":"t5-05-open-ok","cmd":"valve_open"}` → `ok`, the valve opens.

#### CP7-L13: T5-06, the remote override: opens during a leak, blocks a new leak, its cancel re-closes (P1, 9 min)

```text
{"schema":"eflostop.cmd","ver":1,"id":"t5-06-ovr-none","cmd":"override_enable"}
{"schema":"eflostop.cmd","ver":1,"id":"t5-06-ovr-on","cmd":"override_enable"}
```
1. `t5-06-ovr-none` with nothing wet: `RULES_ENGINE: override_enable: no active incident to override`; ack "No active leak to override. Use the normal Open Valve control."
2. Wet `<BLE1>`: the leak sequence.
3. `t5-06-ovr-on`: `RULES_ENGINE: OVERRIDE WINDOW STARTED: auto-close blocked for 24h (expiry=<ts>)`, `RULES_ENGINE: override_enable: 24h override started remotely — RMLEAK cleared, valve opening`, `Writing RMLEAK=0` **before** `Writing Valve=1`; no `RMLEAK cleared externally`; IoT Hub ack `ok`, `water_access_override_enabled` (`trigger` `c2d_command`, `remaining_s` 86400), `valve_state_changed` open.
4. Wet `<BLE2>`: `RULES_ENGINE: Override active — auto-close BLOCKED for ble_leak_sensor sensor <BLE2> (remaining=<s>s)`; no AUTO-CLOSE, no `Valve=0` write; IoT Hub `leak_detected`, `auto_close_blocked_override`; the valve stays open.
5. `oc-…` with both wet: `IOTHUB: Command: OVERRIDE_CANCEL`, `RULES_ENGINE: OVERRIDE WINDOW CANCELLED (remaining_s=<s>)`, `RULES_ENGINE: Override cancelled with 2 active leak(s) — executing auto-close`, `Writing RMLEAK=1` **before** `Writing Valve=0`; IoT Hub ack `ok`, `auto_close_reenabled` (`reason` `c2d_command`), `valve_state_changed` closed with `rmleak` true; the valve closed within 3 s of the ack.
6. Dry both, clear, `vo-…`.

**Send:** `B1_*`. Claude's B1 verdict is read before B2's reset.

### 6.2 B2: D2, leak protection in the reset portal (P1, 45 min; T4-10 Part B)

**Why:** WP8's main leak-safety gain, never benched: the reset portal keeps scanning (AP_IDLE, SERVE, LR_AP). **Start:** `SS-VC`, VA linked, all dry, the iPhone on its home Wi-Fi with `WiFi-Hub-69C8` forgotten.

#### CP7-D1: reset #1 and the portal's boot (P1, 4 min)

Hold the reset 10 s (5.2's lines). After the reboot, no phone:
- `APP_WIFI: SoftAP up with no saved Wi-Fi credentials (setup portal) - BLE scanning, if any, stays on beside it`;
- `APP_WIFI: Wi-Fi channel at AP start: radio 11 (SoftAP configured 11), router not joined since boot`;
- `RADIO: Mode AP_IDLE (SoftAP up, STA not connected): Coded 0.6 s / Wi-Fi 0.3 s, discovery every 2 periods`;
- `BLE_LEAK: Extended passive scan started (1M + Coded PHY)`; the valve hunted and linked: `[SCAN] Target MAC matched …`, `BLE_VALVE: [CLAIM] Connecting to the valve: pulse up to 1500 ms` (exactly 1500 in AP_IDLE), `SETUP COMPLETE - READY FOR GATT`;
- within 3 min every sensor's `eleak … leak=0` and non-zero `adverts` in `[SUMMARY]`, `AP_IDLE` seconds rising, BLE scanning about 67 %, `(0 over 2900 ms)`.
**Pass:** as listed; LED WHITE (syncing) → GREEN once every device is heard; none of 3.8. *(Then CP7-D8 step 1, if it is run.)*

#### CP7-D2: a leak in AP_IDLE, no phone (P1, 5 min)

Wet `<BLE1>`; write the time. **Expect:** `leak=1` **≤ 20 s** after the wetting; the leak sequence (RMLEAK before CLOSE; Δa ≤ 200 ms and Δc ≤ 1 s; record Δc against the L-spread: the SoftAP's Wi-Fi slots can lengthen the valve's writes); the events kept: `TELEMETRY_V2: Offline — buffering <kind> event` and `OFFLINE_BUF: Stored event [ob_<nn>] (<n> bytes), <n> buffered` (the reset's software restart keeps the clock), or, if the clock was lost, `TELEMETRY_V2: Time not synced (ts=<ts>) - holding <kind> for replay; stamped when the clock syncs`. Dry: the clear lines (the valve stays closed). Leave it closed until D6.

#### CP7-D3: a leak while a phone uses the setup page (P1, 6 min) and D3b, a leak during a Connect (P2, 4 min)

1. The iPhone joins `WiFi-Hub-69C8` from Settings (tap time in the notes). Expect `APP_WIFI: SoftAP: station <MAC> joined, AID=<n>`, `RADIO: JOIN pulse for station <MAC>: BLE off for up to <N> ms` (N ≤ 2800), `APP_WIFI: SoftAP: station <MAC> got 10.10.0.<x>, <ms> ms after joining`, `RADIO: Mode SERVE (SoftAP up, STA not connected; a setup page in use, a Connect or a new lease): Coded 0.6 s / Wi-Fi 0.6 s (rung SERVE-A), discovery every 3 periods`, `RADIO: JOIN pulse over after <ms> ms (<why>)`.
2. Keep the page in use: tap Rescan every 20-30 s. Wet `<BLE2>` (with D8 running: only after `RADIO: Sensor-starvation guard over`, since the guard's row is not SERVE's). **Expect** `leak=1` **≤ 35 s** (SERVE), `LEAK INCIDENT latched` (a new incident: D2's dried and cleared), the leak sequence. Dry `<BLE2>`; the clear lines.
3. *(D3b, P2)* Choose a network on the page and type a **wrong** password first; then wet `<BLE1>` and tap Connect within 2 s: `APP_WIFI: Wi-Fi setup page: Connect - attempt started` and a SUBMIT line (`RADIO: SUBMIT pulse: BLE off for up to <N> ms`, paced or not, or `RADIO: SUBMIT pulse not granted before its Connect ended (asked <ms> ms before) - the attempt ran beside BLE`). `leak=1` ≤ 35 s and the close as above. Dry, clear.

#### CP7-D4: a leak in the portal with the valve unpowered: LR_AP (P1, 7 min)

PSU off; wait for `BLE_VALVE: [DISCONNECT] reason=0x208` (a supervision timeout: HCI 0x08 plus NimBLE's 0x200) and 1 min more; keep the iPhone on the page. Wet `<BLE1>` with a folded, soaked towel (keep it wet until D6: add a few drops without lifting it every 4 min; a `leak=0` before D5's relink makes D4-D5 Blocked). **Expect:** `leak=1`, `AUTO-CLOSE + RMLEAK triggered`, the pended pair; `BLE_VALVE: [LR] Leak response pending, valve not linked (RMLEAK/CLOSE pended) - leak-response scanning, at most 600 s for this incident`; `RADIO: Mode LR_AP (a leak response, the valve not linked; SoftAP up, STA not connected): 1M 1.2 s / Coded 0.6 s in the first 30 s, then 1M 0.6 / Wi-Fi 0.3 / Coded 0.6 / Wi-Fi 0.3 s` (W). In its first 30 s a page Rescan or a join gives `RADIO: <KIND> pulse not granted (a leak response's first 30 s) - Wi-Fi goes on beside BLE` (at most one per 10 s): tap Rescan once about 10 s after the `RADIO: Mode LR_AP …` line to provoke it. LED RED.
- **A second sensor in LR_AP:** about 40 s after the `RADIO: Mode LR_AP …` line (its first 30 s over), wet `<BLE2>` the same way; press `d`. Expect its `leak=1` **≤ 35 s** and `IOTHUB: Event: BLE Leak <BLE2> leak=1 batt=<b>`; no new `LEAK INCIDENT latched`; with the valve unreachable a second `AUTO-CLOSE + RMLEAK triggered` (8 item 1). Keep both wet through D5.

#### CP7-D5: the valve back (P1, 4 min)

PSU on (keep `<BLE1>` and `<BLE2>` wet). **Expect:** `[SCAN] Target MAC matched`; `BLE_VALVE: [CLAIM] Connecting to the valve: pulse up to 2500 ms (leak response pending)`; the `GAP CONNECT EVENT` banner; `BLE_VALVE: [LR] Leak-response scanning ended after <s> s (valve linked)`; **`Applying pending RMLEAK command=1` before `Applying pending valve command=0`**; the valve closed; power-on → `Applying pending RMLEAK command=1` and → `[DATA] Valve State=0 (CLOSED)` each ≤ 30 s. **Record** power-on → CONNECT (LR_AP: recorded, not judged, 7.2) and CONNECT → `SETUP COMPLETE`. **Any CLOSE before RMLEAK is a stop-ship.**

#### CP7-D6: setup finishes; the events arrive (P1, 9 min)

Dry `<BLE1>` and `<BLE2>`; the clear lines. Then on the iPhone: choose the bench AP, type its password, Connect; then tap **Finish**.
- `APP_WIFI: Wi-Fi setup page: Connect - attempt started`; the SUBMIT line; `wifi_manager: user connect: the candidate got its IP - it is the network in use now, saved` (W); `APP_WIFI: Connected! IP: <IP>`; `APP_WIFI: Wi-Fi channel at IP: radio <r>, router <r>`; `RADIO: Mode NORMAL (SoftAP down, or STA connected)`; `APP_WIFI: SoftAP tail after a setup-page Connect (stations on it: 1) - it stops 60 s after the IP, or 15 s after the last station leaves (not before 15 s)`; `IOTHUB: cloud admission deferred: SoftAP up - no TLS or DPS until it stops`.
- Finish: `APP_WIFI: Wi-Fi setup page: Finish - the SoftAP stops <s> s after the IP` (≥ 5.0); `APP_WIFI: SoftAP tail: Finish on the setup page - it stops <s> s after the IP`; `APP_WIFI: SoftAP stopped (its servers too) <s> s after the IP`; `IOTHUB: cloud admitted <s> s after the IP (internal DMA free <X> B, largest <Y> B)`; `IOTHUB: Connected to Azure IoT Hub!`.
- IoT Hub, before the `lifecycle`, oldest first, each with a real `ts` (none below 1704067200): **the events still in the offline ring** (`leak_detected`, `auto_close`, `leak_cleared`, `rmleak_auto_cleared`, `valve_state_changed`, the health events); then the `lifecycle` (`"reset_reason":"software"`) and a snapshot. A byte-identical duplicate is allowed. Send `vo-…`. **The ring holds 16 events and B2 raises about 21-25** (D1 0-1, D2 5, D3 4, D3b 4, D4 4-5 with the second sensor, plus `device_offline` once the valve is off 180 s, D5 1-3, D6 3), so expect several `OFFLINE_BUF: Buffer full, oldest event overwritten` lines, with D2's and D3's events among the lost (their proof is the UART: their `Stored event` lines).
- **The delivery rule (Claude):** count the `OFFLINE_BUF: Stored event [ob_<nn>] (<n> bytes), <n> buffered` lines from D2 to the IP. If 16 or fewer: every stored event delivered once, in order, before the `lifecycle`. If more: exactly (count − 16) `Buffer full, oldest event overwritten` lines, the oldest that many events missing (`Known-limit`, 8 item 17), the rest delivered in order before the `lifecycle`. Every UART `Stored event` is matched either to IoT Hub or to an overwrite.

**Pass:** as listed; the phone back on its own Wi-Fi ≤ 15 s after Finish; the sign-in window closes by itself (record).

#### CP7-D7: the capture check (P1, 3 min, Claude)

From D1 on: exactly one boot banner per reset; none of 3.8; every `JOIN`, `SUBMIT`, `LIST` pulse ≤ 2,800 ms (paced ≤ 1,500 ms); every `[SUMMARY]` within I2 and I2b; RMLEAK before CLOSE every time; **no `device_offline` for a live listed sensor from D1 to D6** (`HEALTH_ENGINE: ALERT: ble_leak_sensor <MAC> <r> -> <r> (device_offline)` on the UART, or in the replayed events: before D2 the portal paused BLE and false offlines were the regression D2 removes); the valve's `device_offline` only during D4/D5.

#### CP7-D8: the sensor-starvation guard (P2, inside D1-D3; 1 min of handling)

**Why:** WP8's guard (a listed sensor unheard 250 s or more, in AP_IDLE or SERVE, gets AP_IDLE density with discovery suspended for 110 s, at most once per 600 s) has no other test.
1. As soon as D1 passes (every sensor heard, LED GREEN), take `<BLE4>`'s battery out; write the time.
2. **Expect,** 250 s after `<BLE4>`'s last heard advert (its last `burst:` line; about 150-250 s after the removal), in AP_IDLE or SERVE: `RADIO: Sensor-starvation guard: a sensor unheard 250 s or more (on Coded) - AP_IDLE density, no discovery for 110 s`; 110 s later `RADIO: Sensor-starvation guard over`; every `[SUMMARY]` `(0 over 2900 ms)` meanwhile. (`AP_K1M` in the line means `<BLE4>`'s PHY was not known: record it.)
3. Refit the battery 7 min after the removal (before 10 min unheard gives its `device_offline`): `<BLE4>` heard again.

**Pass:** as listed; no `device_offline` for `<BLE4>`. **Record:** the guard's start after the last heard advert, its kind and row.

**Send:** `B2_*`, the phone notes.

### 6.3 B3: the phones (P1, 120 min; G-CNA's smoke pass, `MANUAL_TEST_PLAN.md` 7.1)

**Start:** `SS-VC`, VA linked, the hub on the bench AP (**channel 1 or 6**), both phones on their home Wi-Fi, `WiFi-Hub-69C8` forgotten on both. Before the first run record the boot's `BLE_LEAK: PHY table loaded: 3 of 3 sensor(s) known, 0 on 1M` (every PHY Coded: AP_IDLE, not AP_K1M).

**One run (P-1 … P-9)** (from the reset to the phone back home; about 6 min):
1. Reset (5.2's lines). Wait for the setup-portal line and `RADIO: Mode AP_IDLE …`, and 60 s with no phone on the SoftAP.
2. Settings → Wi-Fi → tap `WiFi-Hub-69C8`; start a stopwatch (or a screen recording beside the monitor) at the tap.
3. Record: joined (s), any "Unable to join"; the sign-in window opening by itself (s after the join); the page rendered (s after the sign-in opened); the network list non-empty (s), or "No networks found" at 8 s.
4. Choose the bench AP, type its password, Connect; keep the page in front. Record the result (s after the tap) and what the phone showed.
5. Tap **Finish**. Record when the phone is back on its home Wi-Fi.
6. Forget `WiFi-Hub-69C8` on the phone.

**Expect per run (UART):**
- P-1/P-2: `APP_WIFI: SoftAP: station <MAC> joined, AID=<n>`; `RADIO: JOIN pulse for station <MAC>: BLE off for up to <N> ms` (N ≤ 2800; a join within 45 s of the last SUBMIT or JOIN pulse: `RADIO: JOIN pulse for station <MAC>: BLE off for up to 1500 ms (paced: a SUBMIT or JOIN pulse began less than 45 s before)`); `APP_WIFI: SoftAP: station <MAC> got 10.10.0.<x>, <ms> ms after joining` (≤ 3000); `RADIO: JOIN pulse over after <ms> ms (its lease and first page or 302)` (or `(the station left)`, `(its deadline)`).
- P-3/P-4: `APP_WIFI: portal client 10.10.0.<x>: first DNS query, <ms> ms after joining`; `… first captive probe (302 sent), …`; `… first page request, …`.
- P-5: `RADIO: LIST pulse: BLE off for up to 2500 ms` and `RADIO: LIST pulse over after <ms> ms (its requester ended it)`, **or** `APP_WIFI: Wi-Fi list scan without a BLE pulse (not granted) - it runs beside BLE scanning` (or `(no answer in 2 s)`); possibly first `APP_WIFI: Wi-Fi list scan not started: a station is joining the SoftAP - the page asks again later`. **Record which**, and how many APs above −80 dBm the list shows (COEX-5: pulse and no-pulse apart).
- P-6: `APP_WIFI: Wi-Fi setup page: Connect - attempt started`; then **one of** (record which, and the grant latency = the Connect line → the pulse line):
  - (a) paced (the usual flow, 10-45 s after the phone's own JOIN pulse): `RADIO: SUBMIT pulse waits for the pulse spacing (<x> of <y> s): paced, a SUBMIT or JOIN pulse began less than 45 s before`, then `RADIO: SUBMIT pulse: BLE off for up to 1500 ms (paced: a SUBMIT or JOIN pulse began less than 45 s before)`;
  - (b) first Connect: `RADIO: SUBMIT pulse: BLE off for up to <N> ms` at once (N ≤ 2800, no `paced`);
  - (c) `RADIO: SUBMIT pulse not granted before its Connect ended (asked <ms> ms before) - the attempt ran beside BLE`.
  Then `RADIO: SUBMIT pulse over after <ms> ms (the Connect's outcome)` (or `(its deadline)`); `wifi_manager: user connect: the candidate got its IP - it is the network in use now, saved`; `APP_WIFI: Connected! IP: <IP>`; `RADIO: Mode NORMAL (SoftAP down, or STA connected)`; the tail line (60 / 15 / 15). **Result ≤ 10 s; 0 reboots.**
- P-8: the driver's channel switch **(ESP-IDF)**: a `wifi:` line with `csa` and `csa_count:3` when the SoftAP (11) follows the router (1 or 6). Pass: either no `APP_WIFI: SoftAP: station <MAC> left, AID=<n>, reason=<r>` between the Connect and the IP, or the re-opened sign-in shows **success** ≤ 10 s after the re-join.
- P-9: `APP_WIFI: Wi-Fi setup page: Finish - the SoftAP stops <s> s after the IP`; `APP_WIFI: SoftAP stopped (its servers too) <s> s after the IP` by max(IP + 5 s, tap + 3 s) (designed: tap + 2 s, `AP_TAIL_FINISH_MS`; 1 s of tolerance); the phone on home Wi-Fi ≤ 15 s; the sign-in window closes by itself; then `IOTHUB: cloud admitted …` (≤ 5 s after the stop) and `IOTHUB: Connected to Azure IoT Hub!`.
- P-15 (read by Claude): `idma` `min` ≥ 8 KB, `allocfail` 0; the two `[SUMMARY]` lines of each minute within I2 and I2b.

**Pass thresholds (plan 7.5):** joined ≤ 5 s, 0 "Unable to join"; lease ≤ 3 s; first DNS ≤ 3 s after the lease; sign-in opens by itself (iPhone ≤ 8 s after the join; Pixel/Samsung ≤ 10 s; Android 10-11 notification ≤ 15 s); page ≤ 3 s; list ≤ 5 s (or "No networks found" at 8 s); result ≤ 10 s. **P-6, P-8, P-9 are blocking in S2** (PH-2): a failure there goes to the tail decision (15x item 9).

#### CP7-G1 … G6: S2, three runs per phone (P1, 50 min; resets #2-#7)

| Run | Reset | Phone | Variant (each run is the one-run procedure above, with its P-steps recorded) |
|---|---|---|---|
| 1 | #2 | iPhone | **G1**, the usual flow: the Connect 10-45 s after the phone's own JOIN pulse, P-6 (a) |
| 2 | #3 | iPhone | **G4**, the first-Connect path, P-6 (b) |
| 3 | #4 | iPhone | **G5**, wrong, then right |
| 4 | #5 | Android | **G1** and **G3**: the usual flow, setting up the **home router** (P-14's LAN half) |
| 5 | #6 | Android | **G4**, back to the bench AP |
| 6 | #7 | Android | **G5**, then **G6** (Forget) before Finish; then **G2** (two phones) for the set-up again |

**CP7-G3: P-14's LAN half (run 4).** The capture PC is on the home LAN. Before the run, **pre-type this one line** in a PowerShell window in the day folder, all but the IP (the SoftAP and its servers can stop 15 s after the Android leaves, as at P-8's channel switch, so there is no time to type it after the IP):
```powershell
$hub = '<IP>'; curl.exe -s -o NUL -w "%{http_code} exit=%{exitcode}\n" "http://$hub/"; curl.exe -s -o NUL -w "%{http_code} exit=%{exitcode}\n" -X DELETE "http://$hub/connect.json"; python dns_check.py $hub
```
**The moment `APP_WIFI: Connected! IP: <IP>` prints**, with the Android still on the SoftAP and its page open (do not tap Finish yet), type that IP into `$hub` and run the line. Expect `000` with a non-zero exit (the LAN connection shut at accept: no 403 body, no hang; typically exit 52 or 56) and one W `httpd_txrx: httpd_sock_err: error in recv : 128` **(ESP-IDF)** per request; `dns_check.py`: `no reply (timed out)` for all six queries; **no** `APP_WIFI: WiFi Disconnected. Reason: 8` (the DELETE did not make the hub forget). **It counts only if both `curl.exe` calls finished, and `dns_check.py` started, before `APP_WIFI: SoftAP stopped (its servers too) …`**; a set after it (`000` with no `httpd_txrx` line, typically exit 7) is void: redo it in a later run (day 2's P-14). Then tap Finish; after `APP_WIFI: SoftAP stopped (its servers too) …` the same `curl.exe` calls print `000` (connection refused, exit 7). **A `200` to the DELETE is a Fail.**

**CP7-G4: the first-Connect path (runs 2 and 5; P-6 (b)).** The Connect **45 s or more after any SUBMIT or JOIN pulse**: wait on the page, without touching it, 45 s after the `JOIN pulse over` line (a Rescan's LIST pulse does not count). Expect (b): `RADIO: SUBMIT pulse: BLE off for up to <N> ms`, no `paced`, N ≤ 2800. Record the time to `Connected! IP` and the result; Claude compares them with the (a) runs (15x item 6's evidence).

**CP7-G5: wrong, then right (runs 3 and 6; UX-REPEAT-PW, UX-M4-1; ×10 per phone on day 2).** Connect with a **wrong** password: record the page's verdict and its time ("Wrong password" ≤ 20 s); no `wifi_manager: user connect: the candidate got its IP …`; the SoftAP stays up. Then **within 45 s** Connect with the right one: the second SUBMIT is paced, or not granted; record which, the time to `Connected! IP` and the result.

**CP7-G6: Forget with the STA connected (run 6; G8x X-5's connected case; D9).** After the successful Connect, **before Finish**: tap the "Connected to" banner → Connection Details → **Disconnect** → confirm.
- Expect `APP_WIFI: WiFi Disconnected. Reason: 8`; the page goes back to the network list; `RADIO: Mode AP_IDLE …` (or SERVE) again; no `APP_WIFI: router fallback: retrying the configured network (attempt <n>)` afterwards (nothing saved).
- **Prove the erase:** first forget `WiFi-Hub-69C8` on the Android (still on the SoftAP, it would rejoin the open setup portal at once after the reboot and swap G2's join order); then Ctrl+T Ctrl+R. The boot shows `APP_WIFI: SoftAP up with no saved Wi-Fi credentials (setup portal) - BLE scanning, if any, stays on beside it`.
- **Pass:** erased; 0 reboots other than yours. *(The idle and connecting forgets have no page button (the Disconnect is on the Connection Details view, shown only while connected): the laptop's `curl.exe -X DELETE http://10.10.0.1/connect.json` on the fallback portal, day 2, G8x X-5.)*

**CP7-G2: two phones within 45 s (P2, after G6's reboot).** The iPhone joins; within 45 s the Android joins too. The second JOIN is paced (`… up to 1500 ms (paced: a SUBMIT or JOIN pulse began less than 45 s before)`), owed until the first assist ends, or `RADIO: JOIN pulse not granted (not granted within 10 s) - Wi-Fi goes on beside BLE`; both phones reach P-5. Record join → lease for each. Then the iPhone sets the bench AP up (Connect, Finish).

#### CP7-G7: S3, the router-fallback portal, and a wrong password that keeps the saved network (P1, 30 min)

Four runs: iPhone run 1 and Android run 1 plain (steps 1-4 below); iPhone run 2 and Android run 2 with the wrong password (steps W1-W4).

1. Bench AP Wi-Fi off. Expect `APP_WIFI: WiFi Disconnected. Reason: <n>`, `IOTHUB: cloud admission withdrawn (WiFi down)`, `IOTHUB: WiFi down — stopping MQTT client (free TLS heap for AP/captive portal)`, `TELEMETRY_V2: MQTT connected = false`, `IOTHUB: MQTT client stopped on wifi_task in <s> s`, then the Wi-Fi manager's own retries, then `APP_WIFI: SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on` and `RADIO: Mode AP_IDLE …`; 30-35 s after each `APP_WIFI: WiFi Disconnected. Reason: 201` (the previous attempt's end; 38 s still passes) `RADIO: RETRY pulse: BLE off for up to 1500 ms`, `APP_WIFI: router fallback: retrying the configured network (attempt <n>)`, `RADIO: RETRY pulse over after 1500 ms (its deadline)` (start to start about 33-39 s: recorded, not judged).
2. The phone joins `WiFi-Hub-69C8` and uses the page (P-1 … P-5, as S2; the SoftAP is on the router's last channel). While it is used: `APP_WIFI: router fallback: retry deferred - the Wi-Fi setup page is open`, and no RETRY.
3. Bench AP on; put the phone down (no touch). The page's polls stop 60 s after the last touch; a RETRY follows within about 10 s of the page's last request (and 30-35 s after the last attempt's end). Expect `APP_WIFI: Connected! IP: <IP>`, `APP_WIFI: SoftAP tail after an automatic rejoin (stations on it: 1) - it stops 20 s after the IP, or 10 s after the last station leaves`, `IOTHUB: cloud admission deferred: SoftAP up - no TLS or DPS until it stops`, `APP_WIFI: SoftAP stopped (its servers too) <s> s after the IP` (≤ 20 s), `IOTHUB: cloud admitted …` 0.5-1.5 s after it, `IOTHUB: WiFi up — restarting MQTT client`, `IOTHUB: Connected to Azure IoT Hub!`, the replay then the `lifecycle`. **As soon as `APP_WIFI: Connected! IP: <IP>` prints, tap the page once** (it went idle and polls no more): within about 4 s it shows "WiFiHub reconnected to" «<SSID>»; record that time. The SoftAP still stops 20 s after the IP.
4. **P-11:** the page showed "Connection lost" (WiFiHub lost its connection to «<SSID>». It keeps retrying.) in step 2, then the banner "WiFiHub reconnected to" <SSID> after step 3's tap; record both times.

**The wrong password (G8x X-3, T4-10 D5):**
- W1. Bench AP off; wait for the fallback SoftAP; the phone joins and opens the page.
- W2. Choose **another network that is on** (the home router), type a **wrong** password, Connect. Expect `APP_WIFI: Wi-Fi setup page: Connect - attempt started`, its SUBMIT line, `APP_WIFI: WiFi Disconnected. Reason: <n>`, the page's "Wrong password" (or its reason) ≤ 20 s, **no** `wifi_manager: user connect: the candidate got its IP …`. While the page stays in use (its list polls go on for 60 s after the last touch) `APP_WIFI: router fallback: retry deferred - the Wi-Fi setup page is open`; once the page has been left untouched for 60 s, the next `APP_WIFI: router fallback: retrying the configured network (attempt <n>)` is for the **bench AP** (the saved network, not the typed one).
- W3. Put the phone down (no touch, as step 3). Bench AP on: `APP_WIFI: Connected! IP: <IP>` on the bench AP ≤ 40 s after the later of the SSID's return and the page's last request (its polls stop 60 s after the last touch).
- W4. Ctrl+T Ctrl+R: the hub rejoins the bench AP at boot (the saved network kept; NVS unchanged).
- **Pass:** the saved network kept and rejoined; 0 reboots.

#### CP7-G8: the smoke trio, then S1 on the empty hub (P1 if time; 36 min; resets #8-#11)

**The trio empties the hub** (smoke S-6, S-8, S-9 of the test plan section S, rewritten for `SS-VC`; `payloads_destructive.txt`). Before S-6, wait 5 min after the last provision (DEC-03's sync window).
- **S-6 (DEC-03 steps 1, 2, 3, 5; step 4 cut to 60 s):** `smoke-6` removes `<BLE4>`. The snapshot after it equals the one before minus `<BLE4>`; survivors keep `battery`, `rssi`, `fw_version`, `rating`; no `HEALTH_ENGINE: Boot sync: timeout …`, no pulse; the twin's `ble_leak_sensor_count` 2.
- **S-8 (T3-01 steps 1-4 and 9):** `smoke-8` removes the valve (VB powered within 2 m): `IOTHUB: !!! DECOMMISSION_VALVE !!!`, `PROVISIONING: Valve removed successfully`; snapshot `"valve":{}`; for 1 min no `BLE_VALVE: [SCAN] Target MAC matched …` and no `BLE_VALVE: [CONNECT] MAC=<mac>, handle=<h>` (VB is never linked). Wet `<BLE1>`: `leak_detected` and **no** `auto_close`; `RULES_ENGINE: AUTO-CLOSE: no provisioned valve - auto_close event not published`; `RULES_ENGINE: AUTO-CLOSE: no provisioned valve - nothing to close`. `smoke-8-vo`: `IOTHUB: VALVE_OPEN refused — No valve is set up for this hub.`. Dry `<BLE1>`; `rmleak_auto_cleared` without `valve_id`.
- **S-9 (DEC-11 short form):** `dec-11-s1` (`triggers=0x03`); `smoke-9a` removes `<BLE1>`; wet `<BLE2>`; `smoke-9c` removes `<BLE2>` while wet. Expect, within about 10 s (the whitelist reload), `RADIO: Mode BLE_IDLE (no BLE leak sensor listed; no valve, or its link is up) - no BLE scan`; one `event` snapshot of the empty shape with `"rules":{"auto_close_enabled":true,"trigger_mask":7}` and no "Leak interlock latched"; `FLEET_LED: rating=unprovisioned color=WHITE effect=SOLID`; `IOTHUB: Hub is now EMPTY - no devices provisioned; heartbeat-only snapshots`; a heartbeat 60 s later (−1/+3 s). Dry `<BLE2>`.

**S1 (×2 per phone, resets #8-#11):** the one-run procedure on the empty hub. After each reboot: the setup-portal line; **no `RADIO:` line at all** (no BLE starts on an empty hub; no `IOTHUB: Starting BLE …`), so no JOIN, LIST or SUBMIT pulse lines; P-1 … P-9 as in S2, the same thresholds.

**Then re-provision `SS-VC`:** `c2d` the `ss-vc-1` line (id `ss-vc-2` if sent before). Expect `IOTHUB: Starting BLE (valve=00:80:E1:27:F7:BB, BLE sensors=3)`, `BLE_VALVE: [INIT] Signal received. Starting BLE stack...`, the RADIO self-test line, the PHY lines, the valve linked; the 3.7 check (send `rc-def-…` if the mask is not 7; `vo-…`).

**Send:** `B3_*`, the phone notes and screenshots (one per P-step failure, and the success page of each run).

### 6.4 B4: router outage and rejoin (P1, 50 min)

**Start:** `SS-VC`, VA linked, the hub on the bench AP, no phone on the SoftAP, the cloud up 2 min or more.

#### CP7-R1: S-2 = T4-02 rows 1-4, protection with no Wi-Fi and no clock (P1, 7 min)

Bench AP Wi-Fi off; Ctrl+T Ctrl+R (an EN reset: the clock is lost, as at a power cut; a 10 s reset's software restart keeps it); wait for the valve and the sensors; wet `<BLE1>`, then dry it. **Expect:** the leak sequence and the clear lines; every event `TELEMETRY_V2: Time not synced (ts=<ts>) - holding <kind> for replay; stamped when the clock syncs` (never `Offline — buffering` here); `APP_WIFI: SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`; `RADIO: Mode AP_IDLE …`; a RETRY pulse 30-35 s after each `APP_WIFI: WiFi Disconnected. Reason: 201` (38 s still passes; R3's rule); no `IOTHUB: Connected to Azure IoT Hub!` and no `DPS:` line while the AP is off. LED RED → YELLOW → GREEN.

#### CP7-R2: S-3 = T4-03 rows 1-3, the rejoin (P1, 6 min)

Bench AP on. **Expect:** `RADIO: RETRY pulse: BLE off for up to 1500 ms`, `APP_WIFI: router fallback: retrying the configured network (attempt <n>)`, `APP_WIFI: Connected! IP: <IP>` **≤ 40 s after the SSID reappears** (45 s still passes); `APP_WIFI: SoftAP tail after an automatic rejoin (no station on it) - it stops 0.5 s after the IP`; `IOTHUB: cloud admission deferred: SoftAP up - no TLS or DPS until it stops`; the driver's `wifi:mode : sta (<MAC>)` **(ESP-IDF)** at about 0.5-0.7 s; `APP_WIFI: SoftAP stopped (its servers too) <s> s after the IP` (about 0.6-1.3 s); `IOTHUB: cloud admitted <s> s after the IP (internal DMA free <X> B, largest <Y> B)` (about 1.5-3 s); `IOTHUB: Connected to Azure IoT Hub!` (≤ 5 s after the IP); `TELEMETRY_V2: Draining <n> offline event(s) before lifecycle...`, then the `lifecycle`. IoT Hub: R1's held events with real `ts` (≥ 1704067200), in order, before the `lifecycle`. Then `vo-…`.

#### CP7-R3: a leak on the fallback SoftAP, and the RETRY cadence (P1, 10 min; T4-10 D1-D2)

Bench AP off; wait 2 min. Wet `<BLE2>` (AP_IDLE): `leak=1` ≤ 20 s, the leak sequence, `TELEMETRY_V2: Offline — buffering <kind> event` and `OFFLINE_BUF: Stored event [ob_<nn>] (<n> bytes), <n> buffered`. Dry, clear. **Record** the RETRY spacing and each `APP_WIFI: WiFi Disconnected. Reason: 201`. **RETRY:** 30-35 s from each `APP_WIFI: WiFi Disconnected. Reason: 201` (the previous attempt's end: the retry counts from it, `app_wifi.c`) to the next `RADIO: RETRY pulse: BLE off for up to 1500 ms`; 38 s still passes (wifi_task's 1 s pass and the ≤ 2 s grant wait); start to start about 33-39 s (each attempt scans 2-3 s before its 201), recorded, not judged. A `APP_WIFI: router fallback: retry without a BLE pulse (<why>) - its connect runs beside BLE scanning` on every retry is a finding (once after a host reset is expected). Bench AP on; the R2 rejoin lines; the replay (`OFFLINE_BUF: Replayed [ob_<nn>] (<n> bytes)` in order, then `TELEMETRY_V2: Offline drain complete: <n> event(s) replayed`). `vo-…`.

#### CP7-R4: a leak at the pull: the leak task never waits for the MQTT stop (P1, 8 min; WP2b, 15k-10)

Bench AP off; **wet `<BLE1>` as soon as `IOTHUB: WiFi down — stopping MQTT client (free TLS heap for AP/captive portal)` prints.** Expect `TELEMETRY_V2: MQTT connected = false` within about 50 ms of the stop line; `leak=1` → `AUTO-CLOSE + RMLEAK triggered` ≤ 200 ms and → `Writing Valve=0` within the L-spread, wherever `IOTHUB: MQTT client stopped on wifi_task in <s> s` falls; an esp-mqtt `Client asked to stop, but was not started` **(ESP-IDF)** followed by `IOTHUB: MQTT stop refused 1 time(s) - the client had just started` is expected, not a finding. Bench AP on; replay; dry; clear; `vo-…`. **Fail:** rules lines held until just after the `stopped` line.

#### CP7-R5: G3-lite, the rejoin with no station, ×2 (P1 first pull, P2 second; 12 min; 15k-7.1)

Twice: bench AP off 2 min (no phone), then on. **Pass (each):** `allocfail` unchanged from before the pull to 60 s after `Connected to Azure IoT Hub!`; the SoftAP down ≤ 1 s and MQTT up ≤ 5 s after the IP; nothing cloud between `Connected! IP` and `cloud admitted`; the `lifecycle` (`"event":"online"`) after the rejoin; no reboot. **Findings:** `IOTHUB: cloud admission deferred: internal DMA free <X> B, largest <Y> B (needs <a> / <b>)` right after `SoftAP stopped` (a single 1 s hold after a stop is expected); `IOTHUB: Subscribe refused (<a> <b> <c>, outbox <n> B) - reconnecting` repeating for more than 30 s; `idma` minima below 8 KB free or 4.5 KB largest. **Record** IP → AP down → `SoftAP stopped` → `cloud admitted` → `Connected to Azure IoT Hub!`, and the channel lines (`APP_WIFI: Wi-Fi channel at link loss: radio <r>, router was on <r>`, the AP-start line, `APP_WIFI: Wi-Fi channel at IP: radio <r>, router <r>`).

#### CP7-R6: reset #12 with the STA idle (P2, 6 min; X-5 idle, T4-10 D6)

Bench AP off; wait for the fallback SoftAP; start the 10 s hold within 5 s after an `APP_WIFI: WiFi Disconnected. Reason: <n>` line (the confirm lands between two retries). Expect 5.2's reset lines; after the reboot **the setup portal** (`SoftAP up with no saved Wi-Fi credentials …`), not the fallback. Bench AP on; set Wi-Fi up from the iPhone (Connect, Finish). Write the boot time (the soak's SAS renewal counts from the day's last boot).

**Send:** `B4_*`.

### 6.5 B5: the cloud and the WAN black-hole (P1, 100 min)

**Start:** `SS-VC`, VA linked, the hub on the bench AP with its static lease, the cloud up 2 min or more, both DROP rules ready (3.3).

#### CP7-C1: the black-hole check (P1, 3 min)

3.3's check. Record: silent or fast; `<hub IP>` matched the last `APP_WIFI: Connected! IP: <IP>`; both rules listed.

#### CP7-C2: LS-1, a leak closes the valve within 200 ms during a WAN black-hole with Wi-Fi up; the D4 measurement (P1, 3 × 15 min; 15k-12, T6-21)

**Set-up:** both DROP rules (or the WAN cable pulled), Wi-Fi up. T0 = the moment the outage starts (write it; press `d`).

1. **Wets:**
   - **Run 1 (classification):** `<BLE1>` at T0 + 1 s, `<BLE2>` at T0 + 5 s, `<BLE4>` at T0 + 15 s (`<LORA1>` instead with a LoRa sensor). *(P2, CP7-C2b:* only after `IOTHUB: Disconnected.` and at least 90 s after T0, send `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp7-c2-late","cmd":"rules_config","payload":{"auto_close_enabled":true}}'` and `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp7-c2-exp","cmd":"valve_close"}' --expiry $(( ($(date +%s) + 30) * 1000 ))` (first check the flag with `az iot device c2d-message send --help`): the first is acked after the reconnect, the second never delivered (no `IOTHUB: C2D cmd='valve_close' ver=1 id='cp7-c2-exp'`). Send both at once after `IOTHUB: Disconnected.` (and ≥ 90 s after T0), and **keep the outage at least 60 s after the second send**, whatever step 2 allows; write the send time in the notes.)*
   - **Runs 2 and 3 (in-stall):** wet nothing before T0 + 20 s; the first wet is `<BLE1>`, within 1 s after the first `IOTHUB: SNAP trigger=<t>` line at or after T0 + 20 s; then `<BLE2>` 5 s later and `<BLE4>` 15 s later. Run 2: press `d` once during the stall (it answers at once). **Run 3:** only after `IOTHUB: Disconnected.` and at least 90 s after T0, `desired '{"hub_name":"CP7 TW1"}'` (TW-1, 15k-14(g)(1)).
2. Hold the outage at least 2 min after the last wet (run 1: until T0 + 150 s or later).
3. **Expect, the first wet:** `leak=1`; `RULES_ENGINE: LEAK INCIDENT latched by …`; `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by …`; `Writing RMLEAK=1`, then `Writing Valve=0`. **Each later wet:** `leak=1` and `IOTHUB: Event: BLE Leak <MAC> leak=1 batt=<b>` only.
4. From `cloud_tx`, when its stalled write times out: `IOTHUB: Disconnected.` and `TELEMETRY_V2: MQTT connected = false`; `IOTHUB: Pub <kind> took <s> s (msg_id=<id>)`; `TELEMETRY_V2: Pub <kind> failed (msg_id=<id>)`; `TELEMETRY_V2: Pub <kind> not confirmed (msg_id=<id>) - kept for replay, a duplicate is possible`; for the items still queued `TELEMETRY_V2: Offline — buffering <kind> event` and `OFFLINE_BUF: Stored event [ob_<nn>] (<n> bytes), <n> buffered`.
5. Remove both rules (or re-plug the WAN) and confirm. Expect the reconnect; `TELEMETRY_V2: Draining <n> offline event(s) before lifecycle...` and `OFFLINE_BUF: Replayed [ob_<nn>] (<n> bytes)` in order; `TELEMETRY_V2: Pub lifecycle: {…}`; `IOTHUB: Twin reported (<n>): {…}`; the live events; a second `Twin reported` after the twin GET; exactly one `boot` or `fast` snapshot. **Run 3 (TW-1):** `IOTHUB: Twin GET requested (rid=<n>)`, `IOTHUB: Twin GET: applying desired properties from full document`, `IOTHUB: Twin: hub_name = 'CP7 TW1'`, a `Twin reported` with the new name; then `twin tw1`. **Pass:** the last `Twin reported` and Azure's reported twin show "CP7 TW1". Put the name back (`desired '{"hub_name":"<original or empty>"}'`).
6. Dry everything, clear, `vo-…`, wait 2 min. Next run.

**Pass during the outage:** the first wet: Δa **≤ 200 ms**, Δc within the L-spread; each later wet: `leak=1` → `IOTHUB: Event: BLE Leak …` ≤ 200 ms; no rules line held until a `Pub … took` line; no `TELEMETRY_V2: TX queue full …`. **In-stall:** a first wet counts as in-stall only if a `Pub … took` line's span (its ESP time minus the time it gives) covers its `leak=1`; at least 2 in-stall first wets among runs 2, 3 and C3 (else Claude names a 12 min repeat run). **After the restore:** the first copy of each event the UART shows published or stored arrives in the UART's build order; each sensor's last event matches the UART's last; byte-identical duplicates allowed (keep the first). **Classify a missing event** before calling it a failure: `OFFLINE_BUF: Buffer full, oldest event overwritten` first (the ring holds 16); a `Pub event:` line before `Disconnected.` with no `not confirmed` or `Stored event` line for it is R5 (the PUBACK-window loss; run 1's T0 + 1 s and + 5 s wets are likely that); in outages under 30 s first copies can swap (R6).

**Record per run (Claude):** 2.4's D4 figures for the first wet, in-stall or not; the largest later-wet `leak=1` → `Event`; the largest `Pub … took`; next to any Δc over 200 ms, the `OFFLINE_BUF: Stored event` and `TELEMETRY_V2: Pub snapshot` lines between `leak=1` and the posts; the classified misses.

#### CP7-C3: a quiet hub in a black-hole (P1, 5 min; 15k-14(a))

Both rules, no other traffic; wet `<BLE1>` at T0 + 45 s: Δa ≤ 200 ms, Δc within the L-spread; esp-mqtt ends the session by itself within about 30-90 s (`IOTHUB: Disconnected.`). Restore, dry, clear, `vo-…`. It counts as an in-stall sample when a `Pub … took` line spans the wet.

#### The contract checks (CP7-C4 … C9; send nothing wet meanwhile)

Unchanged firmware paths since CP6 except the twin and lifecycle builders (WP3: one locked summary). Each `c2d` line below is the full message. After each `ok`: `RULES_ENGINE: Config updated: auto_close=<a> triggers=0x<m>` (for `rules_config`), an `IOTHUB: Twin reported (<n>): {…}`, `IOTHUB: SNAP trigger=event:<t>` and an `event` snapshot (several `ok` within 5 s give one snapshot).

##### CP7-C4: the cmd_ack envelope rules (P2, 5 min; CP6-29)

| # | Send | Expected |
|---|---|---|
| 1 | `{"schema":"eflostop.cmd","ver":1,"id":"","cmd":"rules_config","payload":{"auto_close_enabled":true}}` | `C2D_CMD: Envelope cmd='rules_config' ver=1 id='' payload=<json>`; ack `data` `{"event":"cmd_ack","cmd":"rules_config","status":"ok"}`, **no** `id` key |
| 2 | `{"schema":"eflostop.cmd","ver":1,"id":12345,"cmd":"rules_config","payload":{"auto_close_enabled":true}}` | a non-string id is dropped: ack without `id` |
| 3 | `{"schema":"eflostop.cmd.v1","id":"cp7-c4-s3","cmd":"rules_config","payload":{"auto_close_enabled":true}}` | the older envelope: `ver=1` in the `C2D_CMD` line; ack `ok` with `id` `cp7-c4-s3` |
| 4 | `{"schema":"eflostop.cmd","ver":2,"id":"cp7-c4-s4","cmd":"rules_config","payload":{"auto_close_enabled":true}}` | `ver` is not checked: ack `ok` |
| 5 | `{"schema":"eflostop.cmd","ver":1,"id":"cp7-c4-s5","cmd":"Set_Hub_Name","payload":{"name":"CC Case"}}` | `IOTHUB: Unknown command: Set_Hub_Name`; ack error `"code":"Set_Hub_Name","detail":"unknown command"`; name unchanged; no snapshot, no twin report |
| 6 | `{"schema":"eflostop.cmd","ver":1,"id":"cp7-c4-s6","cmd":"rules_config","payload":{"auto_close_enabled":true},"sent_ts":1770000000,"extra":{"a":1}}` | unknown keys ignored: ack `ok` |

##### CP7-C5: every `error.detail`, verbatim (P1, 10 min; CP6-30)

Send about 5 s apart, then put the name back with `name-…`:
```json
{"schema":"eflostop.cmd","ver":1,"id":"cp7-c5-s1","cmd":"valve_set_state"}
{"schema":"eflostop.cmd","ver":1,"id":"cp7-c5-s2","cmd":"valve_set_state","payload":{"state":1}}
{"schema":"eflostop.cmd","ver":1,"id":"cp7-c5-s3","cmd":"valve_set_state","payload":{"state":"OPEN"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp7-c5-s4","cmd":"valve_set_state","payload":{"state":"close"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp7-c5-s5","cmd":"rules_config"}
{"schema":"eflostop.cmd","ver":1,"id":"cp7-c5-s6","cmd":"sensor_meta","payload":{"sensor_id":"00:80:E1:2A:29:FC","label":"Sink"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp7-c5-s7","cmd":"sensor_meta","payload":{"sensor_type":"valve","sensor_id":"00:80:E1:27:F7:BB","label":"Sink"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp7-c5-s8","cmd":"provision","payload":{}}
{"schema":"eflostop.cmd","ver":1,"id":"cp7-c5-s9","cmd":"provision","payload":{"valve_id":"not-a-mac"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp7-c5-s10","cmd":"set_hub_name","payload":{}}
{"schema":"eflostop.cmd","ver":1,"id":"cp7-c5-s11","cmd":"set_hub_name","payload":{"name":"CC-01234567890123456789012345678"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp7-c5-s12","cmd":"set_hub_name","payload":{"name":"\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp7-c5-s13","cmd":"set_hub_name","payload":{"name":"\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9"}}
```

| # | UART (besides `IOTHUB: C2D cmd='<cmd>' ver=1 id='<id>'`) | Ack `error.detail` (exact) |
|---|---|---|
| 1, 2 | — | `missing 'state' field (expected "open" or "closed")` |
| 3, 4 | — | `invalid state value (expected "open" or "closed")` (upper case and `close` refused) |
| 5 | `IOTHUB: Command: RULES_CONFIG` | `rules config update failed` |
| 6 | `IOTHUB: Command: SENSOR_META`, `SENSOR_META: Missing sensor_type` | `sensor metadata update failed` |
| 7 | `SENSOR_META: Unknown sensor_type: valve` | `sensor metadata update failed` |
| 8 | `IOTHUB: Provisioning JSON detected`, `PROVISIONING: No valid provisioning data in JSON` | `provisioning failed` |
| 9 | `PROVISIONING: provision: 'valve_id' is not a valid MAC string`, `PROVISIONING: No usable valve identifier in provision payload` | `provisioning failed`; the stored valve and sensors unchanged |
| 10 | `IOTHUB: Command: SET_HUB_NAME` | `missing 'name' field` |
| 11 | (32 ASCII characters) | `name too long (max 31 chars)` |
| 12 | (16 × "é" = 32 bytes) | `name too long (max 31 chars)`: the limit is 31 **bytes** |
| 13 | (15 × "é" = 30 bytes) `IOTHUB: Hub name set to: '<name>'` | `ok`; the ack, the next snapshot's `gateway.name` and the twin's `hub_name` show it in UTF-8 |

**Pass:** each `detail` byte-exact with `error.code` equal to `cmd`; after each error no `SNAP trigger=event:` and no `Twin reported` for it; row 13 `ok`. A first `set_hub_name` within 5 s of a snapshot build may print `TELEMETRY_V2: Snapshot not built - out of memory` once: `Known-limit` if the 5 s retry publishes it.

##### CP7-C6: messages that get no ack, and the 8,192 B C2D limit (P2, 5 min; CP6-31)

| # | Send | Expected |
|---|---|---|
| 1 | `{"schema":"eflostop.cmd","ver":1,"id":"cp7-c6-s1"}` (no `cmd`) | `C2D_CMD: Envelope missing 'cmd' field`, then the legacy parser: `IOTHUB: C2D cmd='provision' ver=0 id=''` (the one planned `ver=0`), `IOTHUB: Provisioning JSON detected`, `PROVISIONING: No valid provisioning data in JSON`; **no ack**; nothing changes |
| 2 | `{"schema":"eflostop.cmd","ver":1,"id":"cp7-c6-s2","cmd":""}` | `C2D_CMD: Envelope missing 'cmd' field`, `C2D_CMD: C2D has 'cmd' but unrecognized schema — ignoring`, `IOTHUB: Unrecognized C2D payload`; **no ack** |
| 3 | the 6 KB file | `IOTHUB: Inbound message spans fragments: <n> of <N> bytes — reassembling`, `IOTHUB: Received C2D Message! (<N> bytes)`; ack `ok` (`cp7-c6-s3`) |
| 4 | the 9 KB file | `IOTHUB: Inbound C2D message DROPPED: exceeds the reassembly limit (<N> bytes, limit 8192)`; ack error `exceeds the reassembly limit: <N> bytes exceeds the 8192 byte limit` (`cp7-c6-s4`, code `rules_config`); not executed |

Make the files (during L7's wait), then send:
```bash
python -c "import json;print(json.dumps({'schema':'eflostop.cmd','ver':1,'id':'cp7-c6-s3','cmd':'rules_config','payload':{'auto_close_enabled':True},'pad':'x'*6000},separators=(',',':')))" > cp7_c6_6k.json
python -c "import json;print(json.dumps({'schema':'eflostop.cmd','ver':1,'id':'cp7-c6-s4','cmd':'rules_config','payload':{'auto_close_enabled':True},'pad':'x'*9000},separators=(',',':')))" > cp7_c6_9k.json
c2d @cp7_c6_6k.json ; sleep 5 ; c2d @cp7_c6_9k.json
```
If `az` does not expand `@file`, paste the file's content into VS Code's "Send C2D Message to Device".

##### CP7-C7: the ack before the twin, on IoT Hub's own clock (P1, 10 min; CP6-32; WB-CLOUD-1)

1. `twin c7-0`.
2. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp7-c7-s2","cmd":"set_hub_name","payload":{"name":"CC Order 1"}}'`; wait 15 s; `twin c7-2`. Expect `IOTHUB: Command: SET_HUB_NAME`, `IOTHUB: Hub name set to: 'CC Order 1'`, a `Twin reported` with `"hub_name":"CC Order 1"`, `IOTHUB: SNAP trigger=event:<t>`.
3. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp7-c7-s3","cmd":"rules_config","payload":{"trigger_lora":false}}'`; wait 15 s; `twin c7-3`: `triggers=0x05`, twin `trigger_mask` 5.
4. Back to back: `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp7-c7-s4","cmd":"rules_config","payload":{"trigger_lora":true}}'` and `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp7-c7-s5","cmd":"set_hub_name","payload":{"name":"CC Order 2"}}'`; wait 15 s; `twin c7-4`: `trigger_mask` 7, `hub_name` "CC Order 2".
5. `name-…`.

**Pass:** for each changed property `$lastUpdated` ≥ the ack's `iothub-enqueuedtime`; the ack before the `event` snapshot; the last twin current. **Record** ack → `$lastUpdated` (normally under 1 s, up to about 7 s in contract). *WB-CLOUD-1: the order holds only while MQTT accepts the ack; an ack refused (−2) is replayed within 10 s, after its twin and snapshot. Not expected on this link: record it if seen.*

##### CP7-C8: the twin's desired `hub_name` and `snapshot_interval_s` (P1, 10 min; CP6-33 a-f)

After each `desired '…'`, wait 10 s, then `twin c8-<letter>`.

| # | `desired '<json>'` | Expected UART | Expected reported |
|---|---|---|---|
| a | `{"hub_name":"CC Twin"}` | `IOTHUB: Twin desired patch: <json>`, `IOTHUB: Twin: hub_name = 'CC Twin'`, `HUB_IDENT: Hub name set: CC Twin`, a `Twin reported` | `"CC Twin"`; the next message's `gateway.name` too |
| b | `{"hub_name":"CC-01234567890123456789012345678"}` | `IOTHUB: Twin: hub_name too long (32 chars, max 31)` | unchanged |
| c | `{"hub_name":"\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9"}` | the same "too long" line (bytes are counted) | unchanged |
| d | `{"hub_name":null}` | the patch line only | unchanged: **null does not clear the name** |
| e | `{"hub_name":""}` | `IOTHUB: Twin: hub_name = ''`, `HUB_IDENT: Hub name cleared` | `""`; no `gateway.name` |
| f1 | `{"snapshot_interval_s":3601}` | `TELEMETRY_V2: Snapshot interval 3601s rejected — outside [60..3600], keeping 60s`, `IOTHUB: Twin: snapshot_interval_s 3601 rejected — reported will show 60` | 60 |
| f2 | `{"snapshot_interval_s":90.7}` | `IOTHUB: Twin: snapshot_interval_s = 90`, `TELEMETRY_V2: Snapshot interval set to 90s (persisted)` | **90 while desired is 90.7** |
| f3 | `{"snapshot_interval_s":"120"}` | the patch line only | unchanged (90) |
| f4 | `{"snapshot_interval_s":60}` | `TELEMETRY_V2: Snapshot interval set to 60s (persisted)` | 60 |

Afterwards put the desired `hub_name` back to the bench's value (`""` if none). **Pass:** each reported value as listed, one `Twin reported` per patch.

##### CP7-C9: mask writes, type traps and range (P2, 5 min; CP6-34)

| # | Send | Expected |
|---|---|---|
| 1 | `{"schema":"eflostop.cmd","ver":1,"id":"cp7-c9-s1","cmd":"rules_config","payload":{"trigger_mask":0,"trigger_ble_leak":true}}` | `triggers=0x01` (the boolean wins for its bit) |
| 2 | `{"schema":"eflostop.cmd","ver":1,"id":"cp7-c9-s2","cmd":"rules_config","payload":{"auto_close_enabled":"false"}}` | ack **`ok`**, rules unchanged (a string ignored, no error) |
| 3 | `{"schema":"eflostop.cmd","ver":1,"id":"cp7-c9-s3","cmd":"rules_config","payload":{"trigger_mask":"7"}}` | ack `ok`, mask still 1 |
| 4 | `{"schema":"eflostop.cmd","ver":1,"id":"cp7-c9-s4","cmd":"rules_config","payload":"7"}` | ack `ok`, unchanged |
| 5 (OBS) | `{"schema":"eflostop.cmd","ver":1,"id":"cp7-c9-s5a","cmd":"rules_config","payload":{"trigger_mask":255}}`, then id `cp7-c9-s5b` with `256`, then id `cp7-c9-s5c` with `-1` | `triggers=0xFF`, then **`0x00`**, then `0xFF`: no range check (`Known-limit`, audit finding) |
| 6 | `{"schema":"eflostop.cmd","ver":1,"id":"cp7-c9-s6","cmd":"rules_config","payload":{"auto_close_enabled":true,"trigger_mask":7}}` | `triggers=0x07`. **Never skip this restore.** |

Send step 5's three messages last and step 6 straight after them (between them the hub is partly or fully disarmed). Before any later wet the last `Config updated` must read `enabled` and `0x07`.

#### CP7-C10: the day-wide contract analysis (P1, Claude, end of day)

On every IoT Hub capture: `python docs\telemetry\validate_capture.py <capture>` (VAL-15 step 1) and Claude's contract checker with the UART and `sent_*.tsv`: duplicates byte-identical; every UART `Pub event` found at IoT Hub or classified; at most one `boot`/`fast` snapshot per connect; every ack matched to its command with byte-exact texts; the `data.rules` and `gateway.name` timelines; **T5-10** (every `Writing RMLEAK=<v>` followed by its `[DATA] RMLEAK=<v>` read-back); the twin-order items of 3.8. **Pass:** 0 FAIL after classification.

**Send:** `B5_*`, the T0 of each run, every `twin_*.json`.

### 6.6 B6: the valve power cycles against the statistical gate (P1, 65 min; test plan 7.6; HANDOFF 15u B2)

**Why:** B2's valve re-find (N_HUNT, 0.45 s on 1M; the back-off from the 2nd empty claim) has never run on hardware. **The model's quantity is power-on → CONNECT** (the `GAP CONNECT EVENT` banner with `BLE_VALVE: [CONNECT] status=0`; its link is the first advert the initiator hears, with no valve boot time): p50 about 1.6-2.1 s, and 0.07 % / 1.7 % / 9.9 % of cycles over 10 s at valve p_loss 0.1 / 0.3 / 0.5. `SETUP COMPLETE` follows CONNECT by another 10-20 s on every link (the full GATT discovery, CCCD writes and reads, a 1 s security delay; 14.9 s on 2.1.3's recorded relink): it is recorded as a check of its own, not gated against 10 s. Day 1 runs cycles 1-60; 61-100 run on day 2 (`-Start 61`).

**Start:** **`SS-VC`** (no 1M sensor listed: with one, NORMAL hunts in N_MIXED and this is not B2's gate), VA linked, NORMAL, all dry, no phone on the SoftAP. The metronome (Appendix A.2: OFF 10 s, ON 45 s) in T4, started from the day folder with `-Cycles 60`. **nRF Connect (E8, required here),** on one of the phones, before cycle 1: (1) 2 min of the valve's adverts: record the advert interval it shows (T_adv); (2) the valve's power-on → its first advert, 5 times, from a screen recording with the PSU's output button in view. The image prints no valve adverts per 1M slot (`[SUMMARY]`'s adverts list only the tracked leak sensors), so p_loss comes from the claims (Pass, below) and T_adv; Claude subtracts the median boot time before comparing power-on → CONNECT with the model.

**Each cycle** (the metronome beeps; press the PSU output button **at** each beep: the CSV logs the beep, not the press, so note any press more than 1 s late with its cycle number): PSU **off** for 10 s, then **on** for 45 s.
- After off: the GAP disconnect banner and `BLE_VALVE: [DISCONNECT] reason=0x208` within about 3 s (0x208 = HCI 0x08, a supervision timeout, plus NimBLE's 0x200; `HANDOFF.md` 15s/15u and the test plan's 7.6 write `0x08`), then `BLE_LEAK: Scan mode N_CODED with a valve hunt (valve not linked): 1 s of 1M 20 % + Coded 80 %, then 0.45 s on 1M, each next after 0-100 ms`. If the disconnect has not printed by the ON beep, note the cycle.
- After on: `BLE_VALVE: [SCAN] Target MAC matched - connecting to provisioned valve: 00:80:E1:27:F7:BB`; `BLE_VALVE: [CLAIM] Connecting to the valve: pulse up to <N> ms` (1500-2500); the `GAP CONNECT EVENT` banner with `BLE_VALVE: [CONNECT] status=0`; `SETUP COMPLETE - READY FOR GATT`; `BLE_LEAK: Scan mode N_CODED: 1M 20 % + Coded 80 %, 1 s scans, each next after 0-100 ms`.
- Empty claims: the first `BLE_VALVE: [CLAIM] Valve claim failed (<why>), 1 in a row - the next needs only the pulse spacing, no back-off`; the second `… 2 in a row - next claim in 10 s (no back-off while a leak response is pending)`. `RADIO: [CLAIM] Valve claim waits for the pulse-rate limit (I2b): …` after a pulse is normal.
- **If `SETUP COMPLETE` has not printed 40 s into the ON leg,** press **P** in the metronome window *before* the OFF beep: it writes a `PAUSE` row and waits. Leave the PSU on; press Enter once `SETUP COMPLETE` has printed: it writes a `RESUME` row and the OFF beep follows 5 s later. That cycle's CONNECT → `SETUP COMPLETE` counts as over 25 s.

**Pass (gate, question 2):**
- **The re-find:** power-on (the CSV's `ON` time) → the `GAP CONNECT EVENT` with `[CONNECT] status=0`: **at most 2 of 100 over 10 s** (or the gate you chose). **Interim rule, fixed in advance** (in brackets: the chance of that count or more with the firmware exactly as modelled, at p_loss 0.3): after 30 cycles, 0-1 over 10 s: go on; 2: GO WITH FINDINGS, finish the 100 before deciding (0.09); 3 or more: stop and send (0.014). After 60 cycles, 0-2: go on; 3: GO WITH FINDINGS (0.08); 4 or more: stop and send (0.019).
- **p_loss, estimated apart from the gated data:** from the claims themselves: each `BLE_VALVE: [CLAIM] Connecting to the valve: pulse up to <N> ms` → its `GAP CONNECT EVENT`, in units of T_adv (geometric in 1 − p_loss), and the empty-claim rate (P(empty) ≈ p_loss^(N / T_adv)). **Decision:** p_loss ≤ 0.3 with more than 2 of 100 over 10 s is a firmware finding (B2's 0.3 vs 0.45 s slot, 15x item 2); p_loss above 0.3: move the valve closer to the hub and re-run before judging.
- **The setup:** CONNECT → `SETUP COMPLETE` ≤ 25 s in every cycle; its median within ±3 s of the first 10 cycles' median; an upward drift over the run is a finding.
- **0** `BLE_VALVE: [SCAN] ble_gap_connect rc=<n>` (rc=2 or 6), **0** `BLE_VALVE: [SCAN] Already connected`, **0** host resets, **0** `RADIO: I2: …` outside WB-CONC-1's exemption (2.3), every `[SUMMARY]` `(0 over 2900 ms)` outside it; the exempt hits per 100 claims recorded (2.2: more than 5 makes the WB-CONC-1 fix an RC precondition).

**Record** (Claude, from the UART's host times and `B6_cycles_pc.csv`): per cycle power-on → `Target MAC matched` (the first hearing) → CONNECT → `SETUP COMPLETE`; p50, p90 and the count over 10 s of power-on → CONNECT; empty claims and their reasons; the claim lengths; T_adv, the boot time and the p_loss estimate; the `PAUSE`/`RESUME` rows and the late presses.

**Send:** `B6_*` and `B6_cycles_pc.csv` (after cycle 30 too, so Claude can apply the interim rule).

### 6.7 End of day (15 min)

1. **T4-14's grep** (5 min): in PowerShell in the day folder, for the bench AP's and the home router's passwords (both went through the portal), typed at the prompt, never written to a file; report only the counts:
   ```powershell
   $s = Read-Host -AsSecureString "password"
   $p = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($s))
   (Select-String -Path .\*_uart.txt -SimpleMatch -Pattern $p).Count
   Remove-Variable p, s
   ```
   Expect `0` for each. Also `(Select-String -Path .\*_uart.txt -Pattern 'Fixed Passkey: configured \(not logged\)').Count` ≥ 1 and no passkey digits.
2. The soak (5.4).
3. `git status --short` in the project folder: no `log.*.txt` left there.

---

## 7. Later days

Every later block starts with 3.7's check and ends with 3.5's hand-off. Procedures not written out here are in the test plan or HANDOFF; this list gives the CP7 changes.

### 7.1 Day 2: carry-over, the 1M cases, G-CNA in full

- **D2-1:** the soak's end; anything cut from day 1; **T2-01** (smoke S-7; the valve's Critical voltage, question 9).
- **D2-2, the 1M sensor (`SS-V4`):** after a boot, record `PHY table loaded: 4 of 4 sensor(s) known, 1 on 1M`. G-CNA S2 ×3 per phone (AP_K1M throughout: it has no mode line of its own, and the `RADIO: Mode AP_IDLE …` and `Mode SERVE …` lines still print with their own geometry, but the row is AP_K1M; `[SUMMARY]` shows `<BLE1M>`'s adverts with `M`); wet `<BLE1M>` once in AP_IDLE and once in SERVE (≤ 20 s / ≤ 35 s); one S2 run with a sensor whose PHY is unknown (just re-added: AP_K1M for 10 min, B3).
- **D2-3, G-CNA full** (test plan 7.1): 10 runs per device per state for S1, S2 (with P-6/P-8/P-9 blocking), S3, on every phone class at hand; P-6 both ways per device; the wrong-then-right ×10 per phone; the second phone within 45 s; P-7 (open network, `Café 5`, an emoji SSID, a trailing-space password, 8/63/64-hex passwords, a 32-character SSID, a wrong password, Latin-1 neighbours); P-10, P-12; resets #13-#17 (idle ×2, connecting ×3). **S1 also picks the httpd socket cap** (4, 5 or 7: needs builds; record page loads with 5).
- **D2-3, the laptop:** P-13 (`dig @10.10.0.1 …` or `python dns_check.py`), P-14's SoftAP half (`curl -I`, `--compressed`, identity → 406, foreign `Host` → 302 with `Connection: close`, 20 parallel connections), the idle and connecting Forget (`curl.exe -X DELETE http://10.10.0.1/connect.json`; idle: `wifi_manager: ORDER_DISCONNECT_STA: the STA is not connected - the saved network is erased now`), G8x X-1 … X-7 (X-1: G8 ×10, Submit with the router off; X-2: a Connect during a router retry ×10; X-4: a switch in the tail and back), G-FAULT F-3 … F-5 (DNS fuzz, header fuzz, the `%` fuzz), F-2 with a second ESP32. Afterwards delete the laptop's `WiFi-Hub-69C8` profile.

### 7.2 Day 2: valve and radio gates

- **G6b ×20** (test plan 7.4): cut the valve's power within about 0.2 s of `[CLAIM] Connecting to the valve: pulse up to <N> ms`, on again after 2 s; the relink is power-on → CONNECT (`GAP CONNECT EVENT` with `[CONNECT] status=0`; 6.6), ≤ 10 s with at most 1 of 20 over (p_loss estimated as in 6.6); CONNECT → `SETUP COMPLETE` ≤ 25 s; 0 `[SCAN] Already connected`; with a CLOSE pended, RMLEAK first and power-on → `[DATA] Valve State=0 (CLOSED)` ≤ 30 s; the exempt `I2:` hits counted (2.2).
- **G6** (test plan 7.6): a dead valve during a leak with a phone on the portal, the router off then on: LR_AP, a phone's lease ≤ 33 s after the incident, the cloud alert after the router returns, the 600 s cap, then the relink, RMLEAK first. **"Linked ≤ 5 s" is power-on → CONNECT, judged per mode once there are 20 relinks of that mode** (with L9's and G6b's): NORMAL_LR at most 2 of 20 over 5 s; LR_AP and past the cap recorded, not judged (the model's chance of ≤ 2 of 20 at p_loss 0.3 is 0.91 in NORMAL_LR, 0.35 in LR_AP, 0.18 past the cap). Every one: power-on → `Applying pending RMLEAK command=1` → `[DATA] Valve State=0 (CLOSED)` ≤ 30 s (E-09's ceiling). **The starvation guard under a leak response (LEAK-2, `1fd5579`):** after the cap, with the valve off and a third sensor's battery out 250 s or more (AP_IDLE or SERVE): `RADIO: Sensor-starvation guard: a sensor unheard 250 s or more (on Coded) - AP_K1M (a leak response: the valve's 1M slot stays) for 110 s`, and the claim at power-on not held off by the guard; refit the battery within 9 min.
- **The remaining power cycles** (`pc_cycles.ps1 -Start 61`, or `-Start 31` after a cut), same set-up and rules as 6.6.
- **CP7-L8b ×3 more, and its dry variant:** as L8b; then dry `<BLE1>` and power-cycle the valve (PSU off 10 s) right after `RULES_ENGINE: All sensors clear — auto-clear timer started (10s)`. Expect the AUTO-CLEAR with its clear pended, then at the relink the owed clear (`RULES_ENGINE: RMLEAK clear read back - the hub's own clear, not a valve override`, or `RULES_ENGINE: Reconnected: valve RMLEAK active, hub incident clear - RMLEAK clear owed by the hub, not re-latching`), or, if the latch is still held at the relink, `RULES_ENGINE: Reconnected: hub incident active, valve closed + RMLEAK clear — re-asserting`; **never** an override line. A cold-booted valve that reads OPEN goes to the valve firmware owner.
- **Rules regressions not run on day 1:** T3-15 (a genuine valve-button press still starts the window; write each press time), T3-17 Part A (the valve shielded), T5-15 / T6-18 (`override_enable` with the valve unpowered, then a leak within 2 s: Δa ≤ 200 ms; keep the PSU off until `RULES_ENGINE: override_enable: valve unreachable after reconnect window`, question 11), T5-07, T6-15 (a sensor flapping every 15-20 s for 15 min), **T3-06 with the old valve wet during a live incident** (LEAK-WB-1: the new valve gets no leak-response treatment until a source reports again; it is still closed at its first link while a sensor is wet).
- **T5-15b, question 11's hazard** (run once before its fix to record it, then on the fix): the valve unpowered, nothing wet, no incident. Power the valve on; about 5 s after its `GAP CONNECT EVENT` (its GATT setup takes 10-20 s more) send `{"schema":"eflostop.cmd","ver":1,"id":"t5-15b-1","cmd":"override_enable"}`; as soon as `RULES_ENGINE: override_enable: valve not ready — reconnecting (<=10000ms)` prints, wet `<BLE1>` (press `d`); `<BLE1>`'s `leak=1`, then `SETUP COMPLETE`, must both land inside the 10 s wait (record them; else redo). **Before the fix** (the hazard): `LEAK INCIDENT latched`, then `RULES_ENGINE: OVERRIDE WINDOW STARTED …`, `Writing RMLEAK=0`, `Writing Valve=1` with `<BLE1>` wet: record, then `oc-…` at once. **After the fix:** `RULES_ENGINE: override_enable: no active incident to override` (ack "No active leak to override. Use the normal Open Valve control."), no `OVERRIDE WINDOW STARTED`, and the valve closed with `Writing RMLEAK=1` before `Writing Valve=0`.
- **D4b (optional, P3; LR-CLAIM-GAP, 15w):** in LR_AP with the valve heard but kept unlinkable (shielded, or powered at the edge of range), the iPhone taps Connect with a wrong password every 8-15 s for 3 min. Record the first claim after `[LR] Leak response pending …` (≤ about 3 s), the claim-to-claim gaps (≤ about 45 s), and RMLEAK before CLOSE at the link.
- **G3b** (test plan 7.5): 30 router power cycles, half with a phone on the fallback SoftAP at the return: no downward heap trend over 1 KB, 0 escape lines, admission ≤ 5 s after the SoftAP stops.
- **G2** (RC-4): 20 drips at random times per row, each row named, the valve linked unless stated: N_CODED (`SS-VC`) ≤ 5 s, N_MIXED (a 1M sensor listed) and N_HUNT (valve unlinked) within their model p99 (Claude gives the figures), AP_IDLE ≤ 20 s, SERVE ≤ 35 s; **pass per row: at least 19 of 20 within the bound, none over 60 s**; CLOSE within 2 s of `leak=1` where the valve is linked. **And with the valve unpowered and one sensor wet** (a second sensor dripped), 10 drips each: NORMAL_LR ≤ 60 s, LR_AP ≤ 35 s.
- **G5:** a phone forget/rejoin loop for 5 min while a sensor is wet: ≤ 2 assists per 60 s per phone, every pulse ≤ 2.8 s with its recovery, the rejoin ≤ 40 s.
- **G7:** list completeness (P-5 data), a router channel change (rejoin ≤ 75 s), a hidden SSID, a 10 s router blip ×10 (rejoin ≥ 95 % per attempt).

### 7.3 Day 3-5: G1 on the lab image (WP7; HANDOFF 15v; test plan 7.7)

- **Build** in its own folder (never the project's `build\`), after CP7: HANDOFF 15v step 1. Pass: `exit=0` with the four known warnings; `Compare-Object` shows only `=> CONFIG_APP_RADIO_LAB=y` / `<= # CONFIG_APP_RADIO_LAB is not set`; the lab `sdkconfig` SHA256 `0A3704F0A13553E231AE92C5A2F760794C2F1406EA3B4BCAE3954F63EDB3981C`; DIRAM `.text` 113,387 B; code about +4.0 KB, `.rodata` +2,185 B, `.bss` +82 B, `.data` +1 B against CP7; the project's `sdkconfig` hash and `git status` unchanged. **Flash with `-p COM30`** (15v says `COM5`: question 10).
- **Boot:** `RADIO_LAB: radio lab image (APP_RADIO_LAB, the G1 ladder) - not for release` (W); the `[LAB] settings at boot - …` line; `RADIO: Profile self-test passed: 16 rows hold I1 and I2, all but the lab's two SERVE-C rows I8 and the period rule (SERVE rung SERVE-A, the lab's)`.
- **The ladder** (S2, every sensor's PHY known **and none on 1M**, `SS-VC`: with a 1M sensor listed every AP mode runs AP_K1M and the rung under test never runs; the valve unpowered first, then linked): step 0 `x`; steps 1-5 the rungs `1`-`5`; 6 `i`; 7 `j`; 8 `k` (only if Android sign-in > 10 s p90); 9 `c`; 10 `b` + a reboot. A smoke pass of 3 runs per phone per step first (day 3, iPhone + Android, steps 0-5: about 3.5 h), then 10 runs on the four phone classes (days 4-5). One step also with a 1M or unknown-PHY sensor (PH-6). The CSV of 15v per step.
- **The rule:** the rung with the highest Coded share that passes in S2 on all four phone classes (tap → join ≤ 5 s p95 with 0 "Unable to join"; join → lease ≤ 3 s p95; sign-in by itself iPhone ≤ 8 s, Pixel/Samsung ≤ 10 s, Android 10-11 ≤ 15 s; page ≤ 3 s; list ≤ 5 s; result ≤ 10 s p90; ping loss ≤ the rung's BLE duty + 10 points). K1's assists are JOINs and are paced within 45 s too.
- **Stop and send the log** on any `I2:` line, `longest Coded gap` over 2,900 ms, `Duty watchdog`, `Profile self-test FAILED`, a repeating `Failed to start ext scan`, a live sensor with no adverts for 5 minutes.
- **After G1:** reflash production from the project's `build\` (`idf.py -p COM30 flash`) and check no `RADIO_LAB:` line. The result is a production change and another build checkpoint (CP8), which the RC builds on (2.6).

### 7.4 The soaks

| Soak | Image, set | Pass |
|---|---|---|
| Overnight day 1 (5.4) | CP7, `SS-V4`, 300 s | no reboot, flat heap, the SAS renewal reconnects, `[SUMMARY]` within bounds |
| G4 NORMAL 24 h | CP7 (or the W3+W4 variant, 7.6), `SS-V4` | 0 false offlines; `[SUMMARY]` Z ≥ 90; no `Duty watchdog`; flat heap; decides `RP_JITTER_MS` with 1 s scans (or the 3 s fallback) |
| AP_IDLE 24 h | CP7, router off, 3 dry sensors, the valve unpowered | 0 false offlines; `[SUMMARY]` within I2 and I2b; no `Duty watchdog`; flat heap |
| VAL-14 ≥ 19 h | the RC | EC-6 |
| T6-12 ≥ 19 h | the RC, sensors-only, spare hub | EC-6 |

### 7.5 Upgrade and rollback (VAL-02, VAL-03; 1.5 h)

- **The 2.1.3 image:** a worktree at `ae4d59a` built with **`..\sdkconfig.pre_cp7`** (UPG-1; never the project's regenerated `sdkconfig`), test plan 0.4: `.bss` 36,120 B, `.data` 21,556 B, `.bin` 1,502,240 B. Flash with `app-flash` only; **never `erase-flash`**.
- **VAL-02:** T6-03 run A (devices, metadata, name, interval, opt-out, an override), run B (a latched incident), run C (a button override with a real expiry), **run D (the PHY table, UPG-2):** the first CP7 boot over 2.1.3 has no `PHY table loaded` line, runs N_MIXED, learns each PHY and prints `PHY table saved: …`; a reboot prints `PHY table loaded: N of N sensor(s) known, M on 1M`. Flash CP7 over 2.1.3 without the reset (4.4's esptool line with `--after no_reset`, here `write_flash 0x20000 eFloStop_WiFiHub_idf1.bin` for an app-only flash), so that run D's first boot is the captured one.
- **VAL-03:** T6-04; after the rollback 2.1.3 boots with the `ble_phy` namespace present (`PROVISIONING: Loaded existing config from NVS`, same counts, no NVS error); after re-flashing CP7, `PHY table loaded: N of N …` (the table survived).
- **CP6-41:** CP7 reads 2.1.3's Wi-Fi credentials (no `SoftAP up with no saved Wi-Fi credentials …` at the first CP7 boot) and drains 2.1.3's offline events (`OFFLINE_BUF: Init: <n> buffered event(s) pending from before reboot`, then the replay before CP7's lifecycle, `gateway.fw` `"2.1.3"` on them).
- **CP6-42:** 2.1.3 after a CP7 10 s reset opens its own portal (an empty SSID is "no credentials"), accepts credentials, connects; CP7 then boots connected.

### 7.6 WP9: the G-M lines (`WP9_GM_PROCEDURE.md`)

- **Preconditions (section 2):** CP7 built, VAL-01 passed, `sdkconfig` `9E13270C…`; **CP7's baselines from days 1-2:** M-heap (4.5), `idf.py size`, G-CNA S1/S2 p90 per step and phone, TLS and DPS times (boot, 10 router power cycles, a first commissioning with DPS on the spare hub, a SAS renewal, a full snapshot), LS-1, 30 min of `[SUMMARY]` adverts per sensor, T6-11 (`cloud_tx` at 5,120 B).
- **Order:** the pool-diagnostic baseline (5.0; worktree + `gm_nimble_pool_diag.patch`: `MONITOR: WP9 nimble pools, lowest free/blocks: …`), then W4 and W3 (BLE bench: G6b ×20, P11, P14, VAL-01, adverts ±5 %, a shared 24 h G4 soak), W2 and W1 (phones: G-CNA p90 within +20 %; W1 moves DIRAM `.text` to about 95,800 B), W5 (with the 100 power cycles, 0 `rc=6`), W8 only after T6-11 shows ≥ 1,536 B free. **W6 never** (it removes the hub's receipt of every valve notification, WP9-ADV-1); **W7 skip** (frees 0 B).
- **Every variant:** the recipe of section 4 with `$port = 'COM30'`; the variant `sdkconfig` hash of its table; M-build, M-size, M-boot, M-heap; "pays" = the median `idma: free` rises by at least half the estimate (your call, 15x item 13); landing per line only with your approval (section 7).

### 7.7 The spare-hub lane (or the main hub with your OK, question 7)

**Two items here are RC gates (RC-4b):** a DPS registration on the new TLS settings (2 KB records, no kept peer certificate: a combination no IDF 5.5.1 CI configuration runs, 15q; DPS runs at every new install and every `decommission` `all`) and the full hub's snapshot (its 27 KB cJSON tree and 8.6 KB print block). **Without a spare hub, and without question 7's OK for a `decommission` `all` on the main hub (it then registers with DPS again), they cannot run and they block the RC.** The DPS pass: `DPS: Submitting registration for '<id>'...`, `DPS: Assigned hub=<host> device=<id>`, then `IOTHUB: Connected to Azure IoT Hub!`, with no esp-tls or mbedTLS error.

| Item | What | CP7 change |
|---|---|---|
| 15k-8(a) = G0-C | first commissioning with DPS (`erase-flash` allowed here only) | TLS with 2 KB records and no kept certificate; S1's real first setup |
| 15k-8(b), T4-06 | the router pulled mid-DPS; DPS blocked | `DPS: DPS registration aborted after <s> s: Wi-Fi lost or SoftAP up - tried again once the cloud is admitted again` (record the exact line) |
| A full hub's snapshot (CP6-37, 15q) | 16 BLE + 16 LoRa labels (Appendix A.3) | realistic about 7,475 B, worst case (quotes) 9,575 B; **control characters now print as spaces** (no longer refused for ever); no `TELEMETRY_V2: Snapshot is <N> B, over the …` line below 10,240 B; no `Snapshot not built - out of memory`; record the `idma` minimum at each build; a full hub's boot admitted without the heap gate's deferral |
| CP6-38, CP6-39 | the empty hub's refusals; sensor metadata and LoRa ids | unchanged |
| 15k-14(b), DEC-18 | `decommission` `all` on a normal link | the clear through `cloud_tx`: `OFFLINE_BUF: Buffer cleared`, `IOTHUB: Decommissioned — restarting in 3s...`; never `IOTHUB: decommission: cloud_tx did not finish in <n> s - offline buffer erased here` |
| T6-02 | a power cut during `decommission` `all` | destructive |
| T6-12 | the sensors-only ≥ 19 h soak | on the RC |
| G-FAULT F-6 | NVS full while the portal forgets a network, then the reset | destructive (fills the default `nvs` partition) |

### 7.8 Bench-only images (each in its own worktree at `b651701`, with CP7's `sdkconfig` copied in first, never shipped; flash CP7 back after each)

- **T6-11 first** (test plan T6-11, `2.1.4-hwm`): every task ≥ 512 B free, `iothub_task` ≥ 2 KB, **`cloud_tx` ≥ 1 KB** (below it raise to 6,144 B before release; ≥ 1,536 B lets W8 trim it), `ble_leak_scan` (3,072 B) after a JOIN or SUBMIT pulse, a claim, a `PHY table saved` and a `[SUMMARY]`; `wifi_manager`, `wifi_task`, `httpd`, `sys_evt`, the NimBLE host, the default event loop.
- **The hold image** (15k-17 at CP7's commit; 15q; **a gate: 2.5 and RC-3**, not optional): the kept reports and the owed purge (flap the valve probe 4+ times inside a ≥ 1 s rules hold, then wet a sensor: `LEAK INCIDENT latched`, `Writing RMLEAK=1` before `Writing Valve=0`, no `RULES_ENGINE: Rules lock busy - … is lost`); a valve swap during a hold prints `RULES_ENGINE: Valve replaced: …` once and the new dry valve is not closed at its first link. It has no `MANUAL_TEST_PLAN.md` test ID yet (10.3), so EC-2 does not count it: this plan's rows do.
- **The fault image** (15k-15), **the short-SAS image** (CP6-43, only if the soaks missed a renewal), **T5-09** (300 s override, with the mask's expiry path).
- **The wrap image** (WB-CRASH-1 `1ec70c7`, WB-CRASH-2 `dbaae0f` and `b651701`, WB-CRASH-3 and the 49.7-day `ticks_ms()` wrap: so far tested by reasoning only). IDF 5.5.1's FreeRTOS takes a non-zero `configINITIAL_TICK_COUNT` (`FreeRTOS.h:1032`, `#ifndef`): in the worktree's top-level `CMakeLists.txt`, between `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` and `project(…)`, add `idf_build_set_property(COMPILE_DEFINITIONS "configINITIAL_TICK_COUNT=0x7FFF7360U" APPEND)` (2^31 ticks minus 6 min at 100 Hz). Across the wrap, about 6 min after boot: (1) BLE_IDLE (a valve-only device set, the valve linked), then a valve drop and a probe leak: the claim, then RMLEAK before CLOSE (WB-CRASH-1); (2) an AP session with a lease: AP_IDLE returns 60 s after the lease (WB-CRASH-2); (3) every `[SUMMARY]` within its bounds, no `I2:` line. A second variant at `0x19990CFAU` (the `ticks_ms()` wrap at 2^32 ms, minus 6 min) checks the 49.7-day wrap the same way (no `I2:` line, RECOVERY rows intact): a failure there records WB-CRASH-3's hazard for 15x item 21.

### 7.9 The rest of the test plan

The P0 DEC subset (DEC-06, 07, 09, 11 in full, 12, 13, 14, 15, 16), T2 (T2-04, 05, 08; the override at a critical battery, audit finding 4), T3-05, T3-19, T4-05, the audit findings 3 and 5 (CP6 plan's CP6-21, CP6-22, CP6-23b), the app tests VAL-04 … VAL-12 with the app build: on the RC, or on CP7 where the code they test is unchanged.

---

## 8. Known limitations and expected warnings at CP7 (record, do not fail on them)

1. **Only the first wet of an incident prints AUTO-CLOSE.** A later wet while the valve reads closed with RMLEAK=1, or within the 10 s cooldown, prints nothing from the rules engine; its `leak=1` and `IOTHUB: Event: BLE Leak …` lines still print. With the valve unreachable each later wet report after 10 s gives another AUTO-CLOSE and `auto_close` (audit finding 2).
2. **The paced Connect (M4, 15w):** a Connect or a second phone's join within 45 s of the last SUBMIT or JOIN pulse gets a 1.5 s pulse 0.4-8 s later, or none (`SUBMIT pulse not granted before its Connect ended …`): the attempt runs beside BLE. Measure, do not fail (15x item 6).
3. **WB-CONC-1:** an `I2:` line after a failed claim, N up to about 3,400 ms, inside 2.3's exemption (N ≤ 3,500 ms, within 1.5 s after the claim pulse's end, at most one per claim): send it, and count the hits per 100 claims (2.2).
4. **List scans without a pulse:** about 43 % in the model (`Wi-Fi list scan without a BLE pulse (…)`), and `Wi-Fi list scan not started: a station is joining the SoftAP …` right after the phone's own join; the page orders again 10 s later.
5. **PH-2:** the tail (STA connected, SoftAP up) runs NORMAL (N_CODED) while the phone still needs the result page, the channel switch and Finish. P-6, P-8, P-9 are blocking in S2: a failure there is the tail decision, not a known limit.
6. **PH-7:** "Other Network" after a success can start a switch late in the 60 s tail.
7. **A sign-in window that does not open by itself, or a phone that drops at the channel switch** (P-8: record the driver's `csa_count`).
8. **`httpd_txrx: httpd_sock_err: error in recv : 128`** **(ESP-IDF)**: one per refused session (a LAN connection during the tail, a low-heap refusal).
9. **R5, R6, at-least-once delivery, W1** (as at CP6): the PUBACK-window loss in a silently dying session; swapped first copies in outages under 30 s; byte-identical duplicates; a QoS 1 resend putting an older twin report after a newer one on a slow link (record `$lastUpdated`).
10. **WB-CLOUD-1:** an ack MQTT refuses (−2) is replayed within 10 s, after its twin and snapshot.
11. **LEAK-WB-1, LEAK-WB-2, WB-CRASH-3, the 49.7-day wrap:** not provokable on day 1 (T3-06 on day 2 shows LEAK-WB-1).
12. **The six V5 audit findings** (CP6 plan 6.8): unchanged by CP7.
13. **The bench image prints the Wi-Fi driver's `wifi:` lines** and `APP_WIFI: bench build (APP_BENCH_DIAG): Wi-Fi driver log at INFO - not for release`; longer UART output stretches log-bound times: CP7 bench figures, not release figures.
14. **A first `set_hub_name` within 5 s of a snapshot build** fails that one snapshot with `TELEMETRY_V2: Snapshot not built - out of memory`; the 5 s retry publishes it.
15. **One 1 s admission hold right after a SoftAP stop**, and a second STOP_AP with no second `SoftAP stopped` line, are expected.
16. **Accepted by decision:** `ble_leak_scan` at priority 6; the 12 KB outbox limit; the 500 ms settle after an AP stop. **Pending your decision** (15x item 8; recommended: approve): the `ble_phy` NVS entry (about 30 B of heap, the I10 exception).
17. **The offline ring holds 16 events** (`OFFLINE_BUF_MAX_ENTRIES`); a 17th stored event overwrites the oldest (`OFFLINE_BUF: Buffer full, oldest event overwritten`), which never reaches IoT Hub. B2's portal session raises about 21-25 events (CP7-D6), so the oldest of them (D2's, D3's) are lost by design; their proof is the UART. Claude matches every `Stored event [ob_<nn>]` line either to IoT Hub or to an overwrite (CP7-D6, C2).

---

## 9. Results sheet

Fill one row per test as you go ("Log" = the block's UART file stem). Claude fills in the verdicts and figures it reads from the logs.

| ID | Pri | Block | Result | Time | Log | Notes (figures, counts, deviations) |
|---|---|---|---|---|---|---|
| CP7-B0 build B1-B14 (VAL-01 1-7) | P1 | B0 | | | `build_cp7.log` | hashes; `.bss`, `.data`, flash; ELF SHA256 |
| CP7-B0 first boot (VAL-01 8; 4.4) | P1 | B0 | | | | `<BLE1M>` = ____; `cloud admitted` s, idma X/Y |
| CP7-B0 second boot, heap at rest (4.5) | P1 | B0, lunch | | | | PHY table line; `heap free`, `idma free/largest` |
| CP7-B0-Ta (4.5 item 4) | P1 | Claude (B0-B1) | | | | per sensor: shortest `dT` ms (≤ 448), bursts used; n per burst per mode |
| CP7-L1 BLE1 linked | P1 | B1 | | | | Δa, Δp, Δr, Δc, Δs |
| CP7-L2 BLE1M linked (N_MIXED) | P1 | B1 | | | | Δa … Δs |
| CP7-L3 BLE2 linked | P1 | B1 | | | | Δa … Δs; **L-spread** = ____ ms |
| CP7-L4 valve probe | P1 | B1 | | | | self-closed before the hub's write? |
| CP7-L5 LoRa | P1 / N/A | B1 | | | | |
| CP7-L6 switch to `SS-VC` | P1 | B1 | | | | |
| CP7-L7 P14 (+ backed-off hunt) | P1 | B1 | | | | RED at ___ s; backed-off line at ___ s |
| CP7-L8 leak response + 600 s cap | P1 | B1 | | | | cap at ___ s; `auto_close` count; `<BLE2>` Δw (NORMAL_LR); power-on → CONNECT → `Applying pending RMLEAK` → `Valve State=0` |
| CP7-L8b valve power-cycled in the incident ×3 | P1 | B1 | | | | per run: the valve's RMLEAK / state after its cold boot; path taken; RMLEAK=1 read back s after `SETUP COMPLETE`; no override line |
| CP7-L9 P11-U | P1 | B1 | | | | power-on → CONNECT (NORMAL_LR) → `Valve State=0` |
| CP7-L10 P11-L | P1 | B1 | | | | live or pended |
| CP7-L11 T3-11 (S-5) | P1 / day 2 | B1 | | | | |
| CP7-L12 T5-04 (S-4), T5-05 | P1 | B1 | | | | the 4 refusals byte-exact |
| CP7-L13 T5-06 | P1 | B1 | | | | cancel ack → `Valve State=0` s |
| CP7-D1 reset #1, portal boot | P1 | B2 | | | | claim 1500 ms? adverts all non-zero by ___ |
| CP7-D2 leak in AP_IDLE | P1 | B2 | | | | wet → `leak=1` s; Δc |
| CP7-D3 leak in SERVE | P1 | B2 | | | | wet → `leak=1` s |
| CP7-D3b leak during a Connect | P2 | B2 | | | | SUBMIT kind |
| CP7-D4 LR_AP | P1 | B2 | | | | not-granted lines; `<BLE2>` Δw (LR_AP) |
| CP7-D5 relink, RMLEAK first | P1 | B2 | | | | power-on → CONNECT (LR_AP) → `Valve State=0` |
| CP7-D6 setup, events before lifecycle | P1 | B2 | | | | Finish → stop s; `Stored event` count, overwrites, delivered |
| CP7-D7 capture check | P1 | Claude | | | | no `device_offline` for a live sensor? |
| CP7-D8 starvation guard | P2 | B2 | | | | guard start after the last heard advert; kind, row; over after ___ s |
| CP7-G1 S2 runs 1-6 (iPhone ×3, Android ×3) | P1 | B3 | | | | per run: P-1 … P-9 times; JOIN/LIST/SUBMIT kinds; list count; P-6/P-8/P-9 Pass? |
| CP7-G2 two phones within 45 s | P2 | B3 | | | | JOIN kinds; join → lease each |
| CP7-G3 P-14 LAN half (run 4, reset #5) | P1 | B3 | | | | curl codes/exit; DNS |
| CP7-G4 first-Connect path (runs 2, 5) | P1 | B3 | | | | (b) N ms; → IP s |
| CP7-G5 wrong, then right (runs 3, 6) | P1 | B3 | | | | verdict s; second SUBMIT kind; → IP s |
| CP7-G6 Forget, connected (run 6) | P1 | B3 | | | | erased after the reboot? |
| CP7-G7 S3 ×2 per phone, incl. the wrong password | P1 | B3 | | | | rejoin s; saved network kept |
| CP7-G8 smoke trio (S-6, S-8, S-9) | P1 if time | B3 | | | | |
| CP7-G8 S1 ×2 per phone | P1 if time | B3 | | | | |
| CP7-R1 S-2 (T4-02) | P1 | B4 | | | | |
| CP7-R2 S-3 (T4-03) | P1 | B4 | | | | SSID back → IP s |
| CP7-R3 fallback leak, RETRY cadence | P1 | B4 | | | | RETRY spacing min/max |
| CP7-R4 leak at the pull (WP2b) | P1 | B4 | | | | stop → `connected = false` ms; Δa, Δc |
| CP7-R5 G3-lite ×2 | P1/P2 | B4 | | | | IP → AP down → stopped → admitted → MQTT |
| CP7-R6 reset #12 idle | P2 | B4 | | | | last boot time ____ |
| CP7-C1 black-hole check | P1 | B5 | | | | silent / fast |
| CP7-C2 LS-1 run 1 (+C2b) | P1 | B5 | | | | D4 figures; misses classified |
| CP7-C2 LS-1 run 2 | P1 | B5 | | | | in-stall? D4 figures |
| CP7-C2 LS-1 run 3 (+TW-1) | P1 | B5 | | | | in-stall? D4; last `Twin reported` and Azure |
| CP7-C3 quiet hub | P1 | B5 | | | | in-stall? |
| CP7-C4 envelope rules | P2 | B5 | | | | |
| CP7-C5 error texts | P1 | B5 | | | | |
| CP7-C6 no-ack, 8,192 B limit | P2 | B5 | | | | |
| CP7-C7 ack before twin | P1 | B5 | | | | ack → `$lastUpdated` |
| CP7-C8 twin desired a-f | P1 | B5 | | | | |
| CP7-C9 mask traps (+ restore 7) | P2 | B5 | | | | |
| CP7-C10 contract analysis, T5-10, VAL-15 | P1 | Claude | | | all | FAILs, duplicates, RMLEAK read-backs |
| CP7-V1 power cycles 1-30 | P1 | B6 | | | | power-on → CONNECT p50, p90, over 10 s (interim rule); CONNECT → `SETUP COMPLETE` max, median; T_adv, boot time |
| CP7-V1 power cycles 31-60 | P1 | B6 | | | | as above; interim rule at 60; p_loss estimate |
| CP7-V1 power cycles 61-100 | P1 / day 2 | D2-4 | | | | as above; the gate at 100; empty claims; exempt `I2:` hits per 100 claims |
| 10 s reset ledger (#1-#12) | P1 | B2-B4 | | | | per run state |
| T4-14 grep | P1 | end | | | all | counts only |
| **Row coverage (15t item 4)** | P1 | all | | | | N_MIXED (B0), N_CODED (L6), N_HUNT (L7, V1), N_HUNT backed off (L7), NORMAL_LR (L8), RECOVERY (after any claim: `[SUMMARY]`'s `recovery` seconds; it has no scan-mode line), AP_IDLE (D1), SERVE (D3), LR_AP (D4), BLE_IDLE (G8, after S-9); no BLE at all (G8's S1 boots); the starvation guard's row (D8); AP_K1M: day 2 |
| Overnight soak | P2 | 5.4 | | | | SAS renewal time; heap trend |
| Day 2-5 items (7) | P3 | — | | | | |

---

## 10. Traceability

### 10.1 Each change since CP6 → its day-1 tests

Day-1 tests are named `CP7-…`; plain `G1`, `G2` … in the "Later" column are the plan's gates (7).

| Area (`545b8f2` → `b651701`) | Day 1 | Later |
|---|---|---|
| WP3 core: the seven `sdkconfig` lines (TLS 2 KB records, no kept certificate, 16/16 Wi-Fi buffers, channel 11, NimBLE re-attempt off and its log at WARN) | CP7-B0 (hashes, sizes, no NimBLE INFO), every TLS connect (4.4, CP7-R2, R5, the S2 runs), CP7-D1 (channel 11) | G3 (E2 flood), G-M |
| WP3 core: membership in one hold, kept reports, owed purge, twin/lifecycle summary, MQTT-stop retries, snapshot sanitising | CP7-L1 … L13 (no `Leak from … ignored`), CP7-R4 (stop refused lines), CP7-C2 (twin), C7, C8 | the hold image (7.8; a gate, 2.5 and RC-3), the full-hub snapshot and DPS (7.7; RC-4b) |
| WP4: portal intake, gzipped page, C8 Connect ownership, Finish, C13 channel switch, list cache, D9 Forget, LAN closed at accept | CP7-D6, CP7-G1 … G8 (P-1 … P-9; G3 P-14's LAN half; G5; G6 Forget; G7 S3 and the wrong password) | G-CNA full, G8x, G-FAULT, P-13 and P-14 with the laptop |
| WP5/WP6: one scan executor, NORMAL de-lock, claims, persisted PHY, widened LR trigger, 10 min cap | CP7-B0 (PHY lines, CP7-B0-Ta), CP7-L6 … L9 (rows, LR, cap, L8b), CP7-V1 | G6b, G6, the 1M cases (7.1), G4 |
| WP8: `radio_policy` (modes, rows, pulses, I1/I2/I2b, the sensor-starvation guard), D2, deletions | CP7-D1 … D8, CP7-G1 … G8, CP7-R1 … R3, every `[SUMMARY]`, every Δw (2.4) | G1, G2 (with NORMAL_LR and LR_AP), G5, G6 (the guard under a leak response), G7, G3b, the AP_IDLE soak |
| Phase 3: M4 pacing (one stamp, 45 s), `384affc`'s line, the stale-stamp fixes | CP7-G1, G2, G4, G5 (paced, first, not granted), CP7-D3b | G-CNA full (wrong-then-right ×10, two phones), D4b (LR-CLAIM-GAP), the wrap image (7.8) |
| CP6-era code never benched (WP1, WP2, WP2b-e) | CP7-R1 … R6, CP7-C1 … C10, CP7-L8 (`auto_close` repeats) | 15k-13(a), 15k-14, the fault image |

### 10.2 15x's decisions → the bench data that decides them

| 15x item | Decision | Data |
|---|---|---|
| 1, 5, 7 | B1, B5, the NORMAL claim length | CP7-L8, L9, D5 (claims under a leak response), CP7-V1 (claim lengths, empty claims), G6b |
| 2 | B2 and the power-cycle gate; 0.3 or 0.45 s | CP7-V1 (100 cycles, power-on → CONNECT, p_loss from the claims), G6 |
| 3 | B3, M5 | CP7-B0 (N_MIXED at the first boot), the 1M cases (7.1) |
| 6 | SUBMIT rule and M4's 45 s | CP7-G1, G4, G5 (paced vs first: success rate, time to IP, not-granted count), CP7-G2, G-CNA full |
| 9 | PH-2 (the tail) | S2's P-6, P-8, P-9 in CP7-G1 … G6 |
| 10 | PH-7 | G-CNA full (Other Network in the tail) |
| 11 | list-scan pulses | P-5 records in CP7-G1 … G7 (pulse or not; list completeness), the G1 ladder's step 9 |
| 12 | D4 | CP7-L1 … L3, R4, C2, C3 (2.4's figures) |
| 13 | the G-M lines | 7.6 |
| 17, 18, 19 | claim room before a leak response, LEAK-6, the B1 interplay | CP7-L8, D4, D5, G6 |
| 21 | WB-CONC-1 and the others | any `I2:` line after a claim (CP7-V1, G6b: the exempt hits per 100 claims); T3-06 (LEAK-WB-1, 7.2); the wrap image (WB-CRASH-3, the 49.7-day wrap, 7.8) |
| 22 | WB-CLOUD-1 | CP7-C7 |
| 28 | the model re-run with G0's Ta and p_loss | CP7-B0-Ta, CP7-V1's p_loss, every Δw |
| question 11 | `override_enable`'s late incident check | T5-15b (7.2), RC-11 |

### 10.3 Changes this plan owes `MANUAL_TEST_PLAN.md` (for its owner; WP10's register)

Until they are made there, this plan's text rules where the two differ:
1. **7.6, the power cycles:** the gated relink is power-on → CONNECT, with CONNECT → `SETUP COMPLETE` a separate check (6.6); p_loss comes from the claims and nRF Connect's advert interval, not from `[SUMMARY]`'s adverts (those list only the tracked leak sensors); the interim rule and the operating characteristic (6.6); G6's "linked ≤ 5 s" per mode.
2. **7.8, G2:** each row named, at least 19 of 20 per row; NORMAL_LR ≤ 60 s and LR_AP ≤ 35 s, 10 drips each, with the valve unpowered and one sensor wet (RC-4).
3. **A test ID for the hold image** (7.8 here; 15k item 17), so that EC-2 counts it.
4. **A test for the valve power-cycled during a live incident** (CP7-L8b and its dry variant), and **T5-15b** (question 11).
5. *No change, for the reader:* E-09's `SETUP COMPLETE` ≤ 30 s after power-on is the ceiling this plan uses for power-on → the pended RMLEAK and CLOSE under a leak response.

---

## Appendix A. Bench scripts

### A.1 `dns_check.py` (P-13, and P-14's LAN check; standard library only)

Save it in the day folder (and on the laptop). `python dns_check.py` sends the six P-13 queries to 10.10.0.1; `python dns_check.py <LAN IP>` runs the LAN check. Its parser was self-tested offline; it has not yet run against the hub.

```python
import socket, struct, sys

SERVER = (sys.argv[1] if len(sys.argv) > 1 else '10.10.0.1', 53)
RCODES = {0: 'NOERROR', 1: 'FORMERR', 2: 'SERVFAIL', 3: 'NXDOMAIN', 4: 'NOTIMP', 5: 'REFUSED'}

def qname(name):
    out = b''
    for label in name.split('.'):
        out += bytes([len(label)]) + label.encode('ascii')
    return out + b'\x00'

def query(qid, name, qtype, edns, rd):
    hdr = struct.pack('>HHHHHH', qid, 0x0100 if rd else 0x0000, 1, 0, 0, 1 if edns else 0)
    body = qname(name) + struct.pack('>HH', qtype, 1)
    if edns:
        body += b'\x00' + struct.pack('>HHIH', 41, 1232, 0, 0)
    return hdr + body

def skip_name(b, i):
    while True:
        n = b[i]
        if n == 0:
            return i + 1
        if n & 0xC0 == 0xC0:
            return i + 2
        i += 1 + n

def parse(b):
    qid, fl, qd, an, ns, ar = struct.unpack('>HHHHHH', b[:12])
    i = 12
    for _ in range(qd):
        i = skip_name(b, i) + 4
    recs = []
    for _ in range(an + ns + ar):
        i = skip_name(b, i)
        rtype, rclass, ttl, rdlen = struct.unpack('>HHIH', b[i:i + 10])
        i += 10
        rdata = b[i:i + rdlen]
        if len(rdata) != rdlen:
            raise ValueError('record runs past the end')
        i += rdlen
        recs.append((rtype, rclass, ttl, rdata))
    return fl, an, ns, ar, recs, len(b) - i

def describe(b):
    try:
        fl, an, ns, ar, recs, extra = parse(b)
    except Exception as e:
        return 'MALFORMED reply (%d B): %s' % (len(b), e)
    names = [n for bit, n in ((0x8000, 'qr'), (0x0400, 'aa'), (0x0200, 'tc'), (0x0100, 'rd'), (0x0080, 'ra')) if fl & bit]
    parts = ['status: %s' % RCODES.get(fl & 0xF, fl & 0xF), 'flags: ' + ' '.join(names),
             'ANSWER: %d AUTHORITY: %d ADDITIONAL: %d' % (an, ns, ar)]
    for rtype, rclass, ttl, rdata in recs:
        if rtype == 1 and len(rdata) == 4:
            parts.append('A %s TTL %d' % (socket.inet_ntoa(rdata), ttl))
        elif rtype == 41:
            parts.append('OPT udp: %d' % rclass)
        else:
            parts.append('type %d (%d B)' % (rtype, len(rdata)))
    parts.append('extra bytes at end: %d' % extra)
    return ', '.join(parts)

CASES = [
    ('A, EDNS, rd (dig default)', 'captive.apple.com', 1, True, True),
    ('A, +noedns +norecurse', 'captive.apple.com', 1, False, False),
    ('AAAA', 'captive.apple.com', 28, True, True),
    ('HTTPS (TYPE65)', 'captive.apple.com', 65, True, True),
    ('SVCB (TYPE64)', '_dns.resolver.arpa', 64, True, True),
    ('A, 250-character name', '.'.join(['a' * 63, 'b' * 63, 'c' * 63, 'd' * 58]), 1, True, True),
]

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(2)
for n, (label, name, qtype, edns, rd) in enumerate(CASES, 1):
    q = query(0x5100 + n, name, qtype, edns, rd)
    s.sendto(q, SERVER)
    try:
        print('%-28s (%3d B query) -> %s' % (label, len(q), describe(s.recv(2048))))
    except OSError:
        print('%-28s (%3d B query) -> no reply (timed out)' % (label, len(q)))
```

On the SoftAP the first query should print `status: NOERROR, flags: qr aa rd ra, ANSWER: 1 AUTHORITY: 0 ADDITIONAL: 1, A 10.10.0.1 TTL 60, OPT udp: 512, extra bytes at end: 0`; AAAA, HTTPS and SVCB NOERROR with `ANSWER: 0`; on the LAN IP, `no reply (timed out)` six times.

### A.2 `pc_cycles.ps1`: the power-cycle metronome (B6)

Save it in the day folder and run it in its own PowerShell window (T4), **from the day folder** (the CSV path is relative):
```powershell
cd "C:\Work\Projects\EfloStop 2\Firmware\Production\2.1.4_bench\2026-10-07_cp7"
powershell -ExecutionPolicy Bypass -File .\pc_cycles.ps1 -Cycles 60
```
(day 2: `-Start 61`; after a cut at 30: `-Start 31`). It beeps low at OFF and high at ON; press the PSU output button at each beep. It writes every beep's PC time to `B6_cycles_pc.csv` (the UART's host timestamps use the same clock). **P** pauses: it writes a `PAUSE` row and waits for Enter, which writes a `RESUME` row, and the next beep comes 5 s later (6.6 says when). Ctrl+C stops.

```powershell
param([int]$Cycles = 100, [int]$OffS = 10, [int]$OnS = 45, [int]$Start = 1, [string]$Csv = "B6_cycles_pc.csv")
if (-not (Test-Path $Csv)) { "cycle,event,pc_time" | Out-File -Encoding ascii $Csv }
function Stamp([int]$n, [string]$what) {
    $t = Get-Date
    "$n,$what,$($t.ToString('yyyy-MM-dd HH:mm:ss.fff'))" | Out-File -Append -Encoding ascii $Csv
    return $t
}
function Mark([int]$n, [string]$what, [int]$hz) {
    $t = Stamp $n $what
    [Console]::Beep($hz, 300)
    Write-Host ("{0:D3} {1,-3} {2:HH:mm:ss.fff}  - switch the PSU output {1} now" -f $n, $what, $t)
}
function Wait-Or-Pause([int]$s, [int]$n) {
    $until = (Get-Date).AddSeconds($s)
    while ((Get-Date) -lt $until) {
        if ([Console]::KeyAvailable -and [Console]::ReadKey($true).Key -eq 'P') {
            [void](Stamp $n "PAUSE")
            Write-Host "Paused (cycle $n): leave the PSU as it is; press Enter once the valve has linked (SETUP COMPLETE)"
            [void](Read-Host)
            [void](Stamp $n "RESUME")
            $until = (Get-Date).AddSeconds(5)
        }
        Start-Sleep -Milliseconds 100
    }
}
for ($n = $Start; $n -le $Cycles; $n++) {
    Mark $n "OFF" 440
    Wait-Or-Pause $OffS $n
    Mark $n "ON" 880
    Wait-Or-Pause $OnS $n
}
Write-Host "Done: cycles $Start-$Cycles. Send $Csv with the B6 UART file."
```

### A.3 `full_hub_gen.py`: the full-hub provisions (7.7; from CP6 plan A.3)

It writes two C2D files, each under 8,192 B, each replacing one sensor array with 16 sensors and their labels: 15 fake BLE sensors plus the real one given as the second argument (wet it once to show leak handling on a full hub), and 16 fake LoRa sensors. Modes: `ascii` (realistic, a snapshot of about 7.5 KB), `quote` (31 `"` per label, the worst case, about 9.6 KB), `ctrl` (31 control characters per label: since WP3 they print as spaces, so the snapshot is no longer refused for ever). Run it on the spare hub (`DEV=<GW2> c2d @full_hub_ble.json`, then `@full_hub_lora.json`); clean up with `decommission` `all` (15k-14(b)).

```python
import json, sys
mode = sys.argv[1]                      # ascii | quote | ctrl
real = sys.argv[2].upper() if len(sys.argv) > 2 else None   # a real BLE sensor's MAC, AA:BB:CC:DD:EE:FF
lab = {"ascii": lambda k, i: ("%s %02d long label ABCDEFGHIJKLMNOP" % (k, i))[:31],
       "quote": lambda k, i: '"' * 31,
       "ctrl":  lambda k, i: "\x01" * 31}[mode]
ble = ["02:00:00:00:00:%02X" % i for i in range(1, 17)]
if real:
    ble[0] = real                       # a sensor that can really be wetted on the full hub
lora = ["0x7E0000%02X" % i for i in range(1, 17)]
def env(part, pl):
    return json.dumps({"schema": "eflostop.cmd", "ver": 1, "id": "fh-%s-%s" % (mode, part),
                       "cmd": "provision", "payload": pl}, separators=(",", ":"))
b = env("ble", {"ble_leak_sensors": ble, "sensor_meta": [
    {"sensor_type": "ble_leak_sensor", "sensor_id": s, "location_code": "living_room", "label": lab("BLE", n)}
    for n, s in enumerate(ble)]})
l = env("lora", {"lora_sensors": lora, "sensor_meta": [
    {"sensor_type": "lora", "sensor_id": s, "location_code": "living_room", "label": lab("LoRa", n)}
    for n, s in enumerate(lora)]})
for name, txt in (("full_hub_ble.json", b), ("full_hub_lora.json", l)):
    open(name, "w", encoding="utf-8").write(txt)
    print(name, len(txt.encode()), "bytes (C2D limit 8192)")
```

Expect, per mode: `PROVISIONING: Provisioning completed successfully!`, `IOTHUB: Provision: applied 16 inline sensor_meta entry(ies)`, acks `ok`; record the byte length of the last `TELEMETRY_V2: Pub snapshot: {…}` line's JSON, the lowest `idma` `min_largest`, and any `TELEMETRY_V2: Snapshot is <N> B, over the …` line (W over 10,240 B, E over 12,288 B); the IoT Hub copy the same size as the UART line (never cut).
