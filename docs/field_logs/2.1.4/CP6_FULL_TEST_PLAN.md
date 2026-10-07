# CP6 bench test plan: eFloStop II Wi-Fi Hub, `545b8f2` (bench day Wednesday 2026-10-07)

**For:** the hub firmware lead, alone at the bench. Claude reads the captures after each block and answers with a verdict per test.
**Firmware under test:** CP6 = commit **`545b8f2`** on `fix/2.1.4`. Version string `2.1.4`. It is a bench image (`APP_BENCH_DIAG=y`).
**Hub:** `GW-7C4FADAE69C8` on COM30. Spare hub: optional (day 2).
**Written:** 2026-10-06, read-only, from `MANUAL_TEST_PLAN.md`, `HANDOFF.md` §15, `RADIO_PORTAL_PLAN.md` and the code at `545b8f2`. Revised the same evening after a coverage, safety and feasibility review (LS-1 now lands its leak inside a stalled write; the interlock and override tests, P11 and P14 are on day 1; two-way DROP rules; a timeline that adds up).

This is a run sheet. Where a procedure already exists in `MANUAL_TEST_PLAN.md` (the "test plan") or `HANDOFF.md`, this plan gives the CP6 changes and a pointer, not a copy. New tests are written out in full.

## How to read this plan

- **Priorities.**
  - **P1:** run tomorrow. Never cut.
  - **P2:** tomorrow if time allows. The cut order is in 5.3.
  - **P3:** day 2 or later.
  - **OBS:** observe and record. The result is `Known-limit` when the hub behaves as written here. It is `Fail` only if it is worse: a reboot, invalid JSON on the wire, the valve opening by itself, a message cut short.
- **Test IDs.**
  - `DEC-`, `T2-` … `T6-`, `VAL-`: the test plan's IDs. `S-n` is step n of the test plan's smoke subset (section S).
  - `15k-n`: item n of HANDOFF §15k (the CP6 gates). `15j` is HANDOFF §15j (the CP6 build check).
  - `G0-A`, `G0-B`, `G0-C`, `G0-D`: the G0 runs of HANDOFF §15d.
  - **`CP6-20` … `CP6-44`: new tests, written in this plan.** (Numbers 1-19 are not used, so they cannot be mixed up with the 15k items.)
- **Log lines.** Quoted as `TAG: text`, as in the test plan 0.14.
  - `%d`, `%s`, `%lu` … are printf fields: match the fixed text around them.
  - `<…>` is a value the hub prints or you fill in. `…` stands for text left out.
  - Every firmware line in this plan was checked against the code at `545b8f2`.
  - Lines from ESP-IDF or the Wi-Fi driver are marked **(ESP-IDF)**.
- **Timing.**
  - Measure every interval from the ESP's own `(N)` milliseconds at the start of each line, not from the PC's timestamps.
  - Since WP2c, the `TELEMETRY_V2: Pub …`, `Offline — buffering …`, `OFFLINE_BUF: …` and `IOTHUB: Twin reported …` lines print from the `cloud_tx` task. They can print before or after the line that caused them.
  - Take the order of messages from the IoT Hub capture. **Never fail a step on UART line order alone.**
- **Results:** `Pass`, `Fail`, `Blocked`, `N/A`, `Known-limit`, as the test plan 0.17. A Blocked P1 test is reported like a Fail.
- **Words used below.**
  - **VA:** valve A, the provisioned valve, on the bench PSU.
  - **VB:** valve B, the neighbour valve, never provisioned.
  - **U:** valve unpowered (PSU output off, still provisioned). **L:** valve linked.
  - **Bench AP:** the access point the hub uses for the day (3.3).
  - **Reset:** the 10 s Wi-Fi reset (hold the button 10 s).

---

## 1. Purpose and scope

### 1.1 What tomorrow proves

CP6 is the first image with every 2.1.4 fix so far built together. Tomorrow shows that CP6 is a sound, regression-free base for the next work packages (WP3-WP10):

1. CP6 builds exactly as predicted (sizes, warnings, map).
2. The 2.1.4 field fixes still work: leak → RMLEAK → CLOSE within fixed bounds (≤ 200 ms to the rules line, ≤ 1 s to the CLOSE write), no false override, the battery rules, device removal, the empty hub (smoke and the safety block). The interlock holds (no open while locked: T5-05), the remote override blocks and its cancel re-closes RMLEAK first (T5-06), a real valve-button press still starts the window (T3-15), a leak across a hub boot closes (P11), and a valve missing at boot turns the LED RED at about 180 s (P14).
3. **WP1:** the setup portal does not reboot the hub, on an iPhone and on an Android. Under bad input (15k-3, 15k-4(c)(d)) only if the laptop is ready (E13); otherwise on day 2.
4. **WP2:** the cloud (TLS, DPS) starts only after the setup SoftAP has stopped, with no failed memory allocation.
5. **WP2b:** leak handling never waits for the MQTT client to stop.
6. **WP2c (its acceptance test, LS-1):** a leak closes the valve within 200 ms while the internet is cut and Wi-Fi stays up, with the leak landing inside a stalled cloud write. No event is lost except the known, classified cases.
7. **WP2d:** a busy lock never drops a wet report. **WP2e:** the last twin report written is the newest.
8. The cloud contract the app and backend teams build on (acks, error texts, twin, duplicates).
9. G0's missing radio baselines (G0-A, G0-B, G0-D), so the WP8 leak model can be run again.
10. Audit finding 2 on hardware tomorrow; finding 3 (relink and cancel paths) and finding 4 if B2's P2 time and B7 run, otherwise day 2; finding 5 on day 2; finding 1 on day 2 (spare hub); finding 3's expiry path on day 3; finding 6 cannot be provoked (noted only).

**Day 1 ends in "GO pending", not GO** (2.4). Several of HANDOFF §15k's gate items need the spare hub, the laptop, a second ESP32 or a bench-only image, and run on day 2 or 3.

### 1.2 What tomorrow cannot prove

- **The release.** CP6 is a development checkpoint and is never shipped. The release campaign runs after WP10, on an image with `APP_BENCH_DIAG` off.
- **WP3-WP10.** None of them is in CP6 (1.4).
- **The known CP6 limitation:** no BLE leak detection while the no-credential setup portal is open after a 10 s reset. WP8 fixes it. Tomorrow only measures it (CP6-26).
- **LoRa, unless a LoRa sensor is at hand.** If open question 8 is "no", the LoRa tests are `N/A (no LoRa HW)` under the EC-2 waiver. If it is "yes", they run (6.2, "the LoRa leak path").
- **Long and bench-only items:** the 19 h soak with a SAS renewal, the stack high-water marks (15k-16), the fault and hold images (15k-15, 15k-17), upgrade and rollback, the app tests VAL-04 … VAL-12. They are on the day-2 list (5.5) or wait for the release campaign (9.3).

### 1.3 Is 2.1.4 ready to release? No.

| Item | Status |
|---|---|
| 2.1.4 field-defect and P0 safety fixes (Phases A-G, `d9fa9c8`) | **Done in code**, reviewed (council 5/5 SHIP). **Bench: open.** No P0 bench test has a full Pass on any image; the only full Pass is VAL-01, the build gate, on CP4. |
| Radio/portal plan WP-V, WP0, WP1, WP2, WP2b, WP2c, WP2d, WP2e | **Done in code** (CP6 = `545b8f2`), each council SHIP. **Never built or benched together:** tomorrow. |
| Fixes never seen on a bench | D1 (`4e6fe71`, a valve RMLEAK report asks for a snapshot), the router-rejoin retry, everything in WP1-WP2e. Tomorrow tests them; the bench-only image checks of WP2c-WP2e follow on day 3. |
| G0 baselines (HANDOFF §15d runs D, A, B ×2, C) | **Open.** Only one B-type run (iPhone, valve unpowered, CP5) was analysed. |
| WP3 (sdkconfig lines, valve link fix, WP2d/WP2e residuals) | **In progress** on `fix/2.1.4` (commits after `545b8f2`, 1.4). Not in CP6. |
| WP4 (portal intake and phone UX, Finish button, channel switch) | **In progress** on `fix/2.1.4` (1.4), with uncommitted edits in the working tree. Not in CP6. Its gates (G8x, G-CNA) come later. |
| WP5-WP8 (radio policy; WP8 closes the no-credential BLE gap) | **WP5 and WP6 in progress** on the branch (`d596424`, `ab7eae8`, `f04f2db`, 1.4). WP7 and WP8 not started. |
| WP9 (memory), WP10 (docs, test plan rewrite) | Not started. |
| Release campaign: VAL-01 on a DIAG-off image, upgrade and rollback (VAL-02/03), heap like-for-like (VAL-13), 19 h soak (VAL-14), app tests (VAL-04 … 12), full validator (VAL-15), exit criteria EC-1 … EC-11 | After WP10. |
| Pre-push clean-up | **Open:** remove the Wi-Fi password from commit `6b84ae3` (EC-12), and reword `9583236`'s 74-character subject. |
| Decisions waiting for you | The QoS 1 twin resend (W1; QoS 0 in WP3?); the final word on the 12 KB outbox limit (HANDOFF 15i decision 2); the `cloud_tx` stack size (after 15k-16); whether `decommission` `all` may run on the bench hub (5.5); `override_enable` checks for a leak only after its 10 s reconnect wait, so a leak latched during that wait passes the check (CP6-24 step 2; open question 10). |
| Pre-existing issues, not fixed by 2.1.4 | The legacy keyword scan that can wipe the hub from a malformed message; the cmd_ack gaps; the six V5 audit findings (partly measured on day 1; see 6.8). Finding 1 has a fix on the branch after CP6 (`bef24b3`, `8971f3b`). |

### 1.4 Important: the branch has moved since CP6

WP3-WP6 work is landing on `fix/2.1.4` now, while this plan is written. At 20:39 on 2026-10-06 the branch was at `d674c1c`: 46 commits after `1707588` (the CP6 docs commit), all made since 19:01 that evening, and still growing; the working tree had uncommitted portal edits too. Examples:
- `fa05390`: `sdkconfig.defaults` lines with compile guards (WP3). With today's `sdkconfig` (CP5's), HEAD stops at those guards by design;
- `4e22719`: the valve link fix (WP3); `1afb368`, `8680b32`, `e1ae26d`: rules fixes (WP3, WP2d residuals); `505eddc`, `1762a65`, `fdb8c28`: the twin's provisioning reads (W2, which 15k-14(g)(6) measures on CP6);
- `bef24b3`, `8971f3b`: a full hub's snapshot bounded within the MQTT outbox (audit finding 1, which CP6-37 measures on CP6);
- `d596424`, `ab7eae8`, `f04f2db`: one BLE scan owner, bounded valve claims, de-locked scanning (WP5, WP6);
- `7dc35ff` … `f84500e`: portal intake, Finish, channel switches, the reworked setup page (WP4); later `fix(ble)` commits (WP6).

`git log --oneline 545b8f2..HEAD` lists them all. A CP6 finding that one of these commits already addresses is still recorded: it shows whether the fix was needed.

**So do not build CP6 in the project folder.** Build it in its own worktree at `545b8f2` (4.4). That build is not affected by the work going on in the project folder, and it leaves the CP5 build there untouched.

---

## 2. Go / no-go exit criteria for CP6

Claude gives the verdict from the captures at the end of the day. Day 2 can change it.

### 2.1 GO pending: day 1 passes when all of these hold

CP6 becomes WP3's base only when these hold **and** the gate items still open after day 1 (2.4) have passed.

| # | Criterion | Shown by |
|---|---|---|
| G1 | The build matches 15j: DIRAM `.text` exactly 113,387 B, `.bss` 43,244-43,276 B, `.data` 21,566-21,574 B, the 4 known warnings, the 3 map searches, `sdkconfig` unchanged. The first boot shows 15k-11's items 1-9, and the bench keys answer. | 4.5, 4.6 (15k-11) |
| G2 | The smoke subset passes, **S-7 included**. If S-7 is `Blocked` (no PSU), the verdict is at most "GO pending T2-01", and T2-01 runs first on day 2, before CP6 is accepted. | 6.1 |
| G3 | No unplanned reboot, panic, stack overflow, watchdog or `abort()` all day (the 10 s reset's own reboot excepted). None of the "report at once" lines (3.7) on a normal run. | every block |
| G4 | **Safety core.** Every wet report from an included sensor, with the valve linked, gives RMLEAK then CLOSE. In every 15k-6 run, `leak=1` → `AUTO-CLOSE + RMLEAK triggered` ≤ 200 ms and `leak=1` → the `Valve=0` write ≤ 1 s. No false override; the valve never opens by itself; D1 works. The interlock holds: no open while locked (T5-05). The override blocks a new leak, and its cancel re-closes RMLEAK first (T5-06). A real valve-button press starts the window (T3-15). A leak across a hub boot closes (P11). A valve missing at boot is RED at about 180 s (P14). The valve's own probe closes and auto-clears (CP6-23a). Scanning resumes after every pause (CP6-25), and a sensor wet through a portal's end closes the valve (CP6-26). | 15k-6, T5-04, T5-05, T5-06, T3-15, P11, P14, CP6-20, CP6-23a, CP6-24, CP6-25, CP6-26 |
| G5 | **LS-1 (WP2c acceptance).** In all 3 runs, the run's first wet: `leak=1` → `AUTO-CLOSE + RMLEAK triggered` ≤ 200 ms, and → the `Valve=0` write within the 15k-6 spread. At least 2 of those first wets land inside a stalled write (a `Pub … took` line spans them; runs 2-3 and 15k-14(a)). Every later wet: `leak=1` → `IOTHUB: Event: BLE Leak %s leak=%d batt=%d` ≤ 200 ms. Every event missing at IoT Hub is classified (R5 or a ring overflow). | 15k-12, 15k-14(a) |
| G6 | **WP2b:** the MQTT stop is only asked (≤ about 50 ms). A leak at a router pull closes within the 15k-6 spread. | 15k-10 |
| G7 | **WP1:** both phones put the hub back on Wi-Fi after a reset. Every day-1 reset ends in the no-credential portal. G8 gives 0 reboots with 10 Submits. 15k-3 and 15k-4(c)(d) pass, if the laptop was ready. | 15k-1, 15k-2, 15k-5, 15k-3, 15k-4 |
| G8 | **WP2:** no `DPS:` line, MQTT start or `Connected to Azure IoT Hub!` before `cloud admitted`. In G3-lite: `allocfail` unchanged, AP down ≤ 1 s and MQTT ≤ 5 s after the IP with 0 stations, the lifecycle after every rejoin. The LAN check gives 403, then 000, and no DNS answer. | 15k-7, 15k-9 |
| G9 | **WP2e:** after a change, the last `Twin reported` line and Azure's reported twin show the newest value, also after an outage (TW-1). | 15k-14(g)(1), CP6-32 |
| G10 | **Contract:** the validator gives 0 FAIL once duplicates are classified. Every `error.detail` is byte-exact. | CP6-35, CP6-30 |

**The "15k-6 spread"** is the largest of 15k-6's three `leak=1` → `Valve=0` write times, plus 100 ms, and never more than 1 s.

### 2.2 GO WITH FINDINGS

CP6 is still the base, and each finding goes into WP3's or WP4's scope:
- In an LS-1 run, `leak=1` → AUTO-CLOSE ≤ 200 ms, but → the `Valve=0` write over the 15k-6 spread and at most 1 s, with the R2/R3 signature between the two lines (a `TELEMETRY_V2: Pub %s: %s` snapshot line, or `OFFLINE_BUF: Stored event [%s] (%u bytes), %d buffered` lines, printing between the AUTO-CLOSE line and the write): WP3 takes decision D4's fixes.
- `IOTHUB: cloud admission deferred: internal DMA free %u B, largest %u B (needs %u / %u)` on a normal boot: the 36 / 12 KB gate is too strict.
- Portal behaviour inside the documented limits (section 7): the sign-in window does not open by itself, a phone drops at the channel switch, slow leases on the router-fallback SoftAP.
- The audit findings behaving as documented (`Known-limit`): these are decisions for you, not CP6 regressions. They are all in 2.1.3 too.
- A genuine valve-button press missed inside T3-15 step 6's documented fail-closed window (a press within about 1 s of the close write).
- G0 figures that change the WP8 model.

### 2.3 NO-GO: any one of these

- A reboot, panic or stack overflow not caused by the 10 s reset. One unexplained case is enough, until Claude has analysed it.
- A safety failure:
  - a wet report from an included sensor, with the valve linked, that does not close the valve;
  - `leak=1` → the `Valve=0` write over 1 s, with the valve linked at the `leak=1` line, for the first wet of any incident on day 1;
  - CLOSE written before RMLEAK;
  - `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window` with nobody pressing the valve button, or `RULES_ENGINE: Reconnected: hub incident + valve open + RMLEAK clear — inferring physical override, starting 24h window` where no test expects it;
  - the hub opening the valve during a latched leak with no override asked for;
  - `leak_reset`, `valve_open` or `valve_set_state` open acked `ok`, or `Valve=1` written, while an included sensor is wet or RMLEAK is latched with no override (T5-05);
  - `override_enable` accepted with the valve's flood probe wet;
  - `override_cancel`, with an included sensor still wet, that does not write RMLEAK=1 before `Valve=0`; a `leak_reset` that does not end an override window;
  - BLE scanning or the valve hunt not resumed after a pause (a CP6-25 Fail), or a sensor wet at the end of a portal that does not close the valve (CP6-26);
  - a wet report lost and not classified.
- LS-1: a run's first wet with `leak=1` → AUTO-CLOSE over 200 ms (WP2c not accepted); or an LS-1 `Valve=0` write over 1 s, or over the 15k-6 spread with no R2/R3 signature.
- Either phone cannot finish Wi-Fi setup after a reset in two tries, or a reset does not erase the credentials.
- TLS beside the SoftAP, or a failed allocation in 15k-7.
- The twin left stale after a change on a stable link (TW-1 back).
- A build-gate mismatch nobody can explain. Stop before the bench in that case.
- A smoke Fail. This stops the day (test plan section S).

### 2.4 Not decided tomorrow

- The release exit criteria EC-1 … EC-12 (test plan 9.4).
- Upgrade and rollback, the overnight soak, the SAS renewal: day 2 (5.5).

**HANDOFF §15k gate items still open after day 1.** CP6 is accepted as WP3's base only once WP1's gate (15k-2 ×10, 15k-3, 15k-4, 15k-5) and WP2's gate (15k-7, 15k-8, 15k-9) have passed, as HANDOFF §15k defines them, together with the WP2c-WP2e items below.

| Gate item | Package | Where |
|---|---|---|
| 15k-2: idle resets #7 and #8, connecting resets #9-#11 (5.2) | WP1 | D2-2 |
| 15k-3 and 15k-4(c)(d), if not run in B4 (no laptop) | WP1 | D2-6 |
| 15k-4(a)'s E2-laptop run; 15k-4(b) (beacons from a second ESP32) | WP1 | D2-6 |
| 15k-5 linked (L), and its E2-laptop run (step 7) | WP1 | D2-6 |
| 15k-7.1 linked runs 2-3 | WP2 | covered by 15k-10(b) runs 2-3 (9.3) |
| 15k-8(a) first commissioning, 15k-8(b) the DPS router pull | WP2 | D2-5 (spare hub) |
| 15k-13(a), if cut from B6; 15k-13(b) | WP2c | D2-2; D3 (15k-16's image) |
| 15k-14(b) decommission-all; (c) 4-sensor provision; (d) interval change; (e) the outbox-full burst, where practical; (g)(2) | WP2c, WP2e | D2-5; D2-3 |
| 15k-14(f) (DEC-14) and (g)(3)-(5) | WP2c, WP2e | read in every capture (CP6-35); DEC-14 in D2-3 |
| 15k-15, 15k-16, 15k-17 (bench-only images). 15k-16 decides the `cloud_tx` stack size (≥ 1 KB free, or raise it to 6,144 B before release). | WP2c, WP2d | D3 |

---

## 3. Equipment, identities, captures and hand-off

### 3.1 Equipment checklist (tick before 08:45)

| # | Item | Needed for | Notes |
|---|---|---|---|
| E1 | Hub `GW-7C4FADAE69C8`, USB to the capture PC (COM30) | everything | **Never `erase-flash` this hub.** |
| E2 | Valve A, FW 2.2.0, **batteries out, on the bench PSU** | all valve tests | With batteries in, switching the PSU off does not unpower it. |
| E3 | Bench PSU, 10 mV steps, current limit above the motor stroke | T2-xx, CP6-24, CP6-36, every U run | Never below 5.0 V. Its output button is the valve's power switch. |
| E4 | Valve B, powered, within 2 m, never provisioned | S-8 | Batteries are fine. |
| E5 | 4 working BLE leak sensors | `SS-V4` | `…2B:A5` is dead: bring a replacement, or run with 3 and record it. `…CB:B6` advertises on 1M PHY (G0-D needs it). |
| E6 | **Bench AP** on the desk, within reach (3.3) | every outage block | The biggest time-saver of the day. |
| E7 | iPhone and Android phone, mobile data on, charged | portal blocks; nRF Connect | Forget `WiFi-Hub-69C8` on both, and turn auto-join off. |
| E8 | nRF Connect (on a phone not in use, or a third device) | G0-D | It logs `CB:B6`'s adverts with timestamps. |
| E9 | RF shield (a metal box or an unplugged microwave) | S-5 (T3-11) | Keeps the valve powered but out of range. |
| E10 | Cup of water, paper towels, a dry cloth | wetting | Keep water away from the PSU leads and the hub's USB. |
| E11 | Capture PC: ESP-IDF 5.5.1 PowerShell; Git Bash with `az` and the `azure-iot` extension; Python 3 | everything | **`az` is not installed on this PC yet:** tonight's task N1. |
| E12 | Day folder `C:\Work\Projects\EfloStop 2\Firmware\Production\2.1.4_bench\2026-10-07\` | all captures | Outside the repo. Never capture to fixed names on the Desktop. |
| E13 | **Wanted:** a laptop with `curl.exe` (built into Windows) and Python 3, plus `dig` (WSL) or Appendix A's `dns_check.py` | 15k-3, 15k-4(c)(d) (P1 when the laptop is ready) | Without it, both move to day 2 (D2-6) and WP1's gate stays open (2.4). **When its tests are done (end of reset #3), run `netsh wlan delete profile name="WiFi-Hub-69C8"` on it and switch its Wi-Fi off before B6:** G0-A, G8 and 15k-7.1 need 0 stations on the SoftAP. |
| E14 | *Optional:* a spare hub, its own COM port and gateway ID | 15k-8, CP6-37 … 39, DEC-18, T6-02 | Without it, these wait for day 2 and your decision (5.5). |
| E15 | *Optional:* a LoRa leak sensor with the bench key (open question 8) | T5-13, LS-1 run 1, CP6-24's LoRa variant | Without it, the LoRa tests are N/A (6.2). |
| — | **Keep switched off:** the Windows laptop that remembers `WiFi-Hub-69C8` ("E2") | — | It auto-joins the fallback SoftAP and floods it (2026-09-29). Only 15k-5 step 7 uses it, on day 2. If E2 is also E13, delete its profile after B4 as E13 says, and join by hand on day 2. |

### 3.2 Bench identities

The MACs come from earlier bench logs. Confirm them against the boot's `PROVISIONING: …` lines before the first send, and correct `payloads.txt` (3.5) if they differ.

| Placeholder | Value |
|---|---|
| IoT Hub, resource group | `resi-apex-iot-dev`, `resi-apex-rg-dev` |
| `<GW>` | `GW-7C4FADAE69C8` (COM30) |
| Setup SoftAP | `WiFi-Hub-69C8`, portal at `http://10.10.0.1/` |
| `<VALVE>` (valve A) | `00:80:E1:27:F7:BB` |
| `<VALVE_B>` | `00:80:E1:27:7E:C5` |
| `<BLE1>` "Sink" | `00:80:E1:2A:29:FC` |
| `<BLE2>` "Washer" | `00:80:E1:2A:B6:8E` |
| `<BLE3>` "Ensuite" | `00:80:E1:2A:CB:B6` (1M PHY) |
| `<BLE4>` "Main" | the replacement sensor: fill in |
| `<LORA1>` | fill in if open question 8 is yes (its `0x…` id); otherwise none |
| Valve's Low and Critical PSU voltages | fill in (test plan 0.10 rule 4: step down 20 mV at a time from 5.55 V). Defaults about 5.45 V and 5.35 V. |

### 3.3 Making outages without the physical router

The tests need two kinds of outage:
- **Wi-Fi gone:** the hub's network disappears.
- **WAN black-hole with Wi-Fi up:** the internet is cut silently while the hub stays joined.

The capture PC must stay online through both, or the `az` monitor and C2D stop.

| Option | Wi-Fi gone | WAN black-hole | Notes |
|---|---|---|---|
| **A (best): a bench AP on the desk.** A travel or spare router, ideally OpenWrt. WAN by Ethernet to the home router. 2.4 GHz on channel 6 or 11 (not 1: the setup SoftAP is on 1). Its own SSID. | its Wi-Fi toggle or power switch | pull its WAN cable, or (OpenWrt) the two DROP rules below | Every outage kind; both phones free; the PC is unaffected. The PC is not on this AP's LAN, which is why 15k-9 runs with the hub on the home router. |
| **A2 (no cable to the bench):** a travel router (GL.iNet or other OpenWrt) in Repeater/WISP mode: its uplink over the home router's 5 GHz Wi-Fi, its own 2.4 GHz SSID on channel 6 or 11. | `wifi down radio0` / `wifi up radio0` on its console (check which radio is the 2.4 GHz one with `uci show wireless`) | the two DROP rules below (there is no WAN cable to pull) | As A. Run the black-hole check tonight. |
| B: the home router's admin page | turn off its 2.4 GHz radio only (the PC must be on Ethernet or 5 GHz) | a per-device "block internet", if it has one | **Every household 2.4 GHz device goes offline too: warn the others.** A block may REJECT rather than DROP: check it as below. Rebooting the router also cuts the PC. |
| C: the Android phone's hotspot, band 2.4 GHz, on its charger | hotspot off | mobile data off, hotspot on | The Android cannot be a portal phone while it is the hub's AP. Not for the overnight soak. |

Never give the bench AP the home router's SSID and password.

**OpenWrt DROP rules (22.03 or later), on the bench AP's console. Always both, so the outage is two-way.** With only the first rule, Azure's packets on the open session still reach the hub (the firewall accepts an established flow's return traffic), so a C2D or desired-property patch sent in the first 90 s or so (1.5 × the 60 s keepalive) would run on the hub mid-"outage":
```sh
nft insert rule inet fw4 forward ip saddr <hub IP> tcp dport 8883 drop
nft insert rule inet fw4 forward ip daddr <hub IP> tcp sport 8883 drop
nft -a list chain inet fw4 forward          # both rules, with their handles
nft delete rule inet fw4 forward handle <n> # remove each, by handle (twice)
```
On older OpenWrt: `iptables -I FORWARD -s <hub IP> -p tcp --dport 8883 -j DROP` and `iptables -I FORWARD -d <hub IP> -p tcp --sport 8883 -j DROP`; `-D` in place of `-I` removes each.

**Make the rules hit the hub (tonight, N3):**
- Give the hub a fixed address: `uci add dhcp host; uci set dhcp.@host[-1].mac='<hub STA MAC>'; uci set dhcp.@host[-1].ip='<IP>'; uci commit dhcp; /etc/init.d/dnsmasq restart`. Without it, a re-join after a reset or a router power-off (the lease file lives in `/tmp`) can bring a new IP that the rules miss.
- Check that `uci get firewall.@defaults[0].flow_offloading` prints `0` or nothing. With flow offloading on, an established flow bypasses the forward chain and the rules never apply.
- Before every DROP: compare `<hub IP>` with the last `APP_WIFI: Connected! IP: %s` line, and run `nft -a list chain inet fw4 forward` to see both rules.

**Remove the rules before the hub needs the internet again:** before the Wi-Fi comes back in 15k-7.3, before the phone submits in a reset during a DROP (15k-13(a)), and at every LS-1 restore. Confirm with `nft -a list chain inet fw4 forward`. A router power-off clears runtime rules by itself.

**Check that a "black-hole" really is one (3 min, at the start of B6; once tonight too).**
1. With the hub connected for 2 min, start the WAN outage and press `d` in the monitor (a marker).
2. A silent drop keeps the session up until esp-mqtt gives up: `IOTHUB: Disconnected.` comes about 10-90 s later. A stuck publish first prints `IOTHUB: Pub %s took %lu.%lu s (msg_id=%d)`.
3. If `IOTHUB: Disconnected.` prints within about 5 s, the method answers (REJECT or a reset). That is a fast outage, not LS-1's case. Use it for LS-1 only if it is silent.
4. **The outage is real only if** no hub message reaches IoT Hub after T0 + 5 s (the next heartbeat is missing in the `az` capture), and `IOTHUB: Pub %s took %lu.%lu s (msg_id=%d)` or `IOTHUB: Disconnected.` follows within 90 s. Otherwise fix the rule (the IP, flow offloading) before LS-1.

### 3.4 Terminals and captures

| Terminal | Use |
|---|---|
| T1: ESP-IDF 5.5.1 PowerShell, **in the CP6 worktree `..\hub_cp6`** | flash, UART monitor. Running the monitor there makes backtraces decode with CP6's ELF. |
| T2: Git Bash | IoT Hub monitor (UTF-8; PowerShell's `>` writes UTF-16, which the validator cannot read) |
| T3: Git Bash | C2D and twin (3.5) |

**UART (test plan 0.6, with two changes):**
```powershell
cd "C:\Work\Projects\EfloStop 2\Firmware\Production\hub_cp6"
idf.py -p COM30 monitor --no-reset --timestamps --timestamp-format "%Y-%m-%d %H:%M:%S.%f"
```
- Use `--no-reset` whenever you re-attach to a running hub. A plain `monitor` resets the chip, which counts as a reboot.
- **To capture a boot from its first line** (after a flash, or for 15k-11): start the monitor with `--no-reset`, start the log (Ctrl+T Ctrl+L), then press Ctrl+T Ctrl+R, which resets the hub with the log already running.
- Ctrl+T then Ctrl+L starts the log file `log.eFloStop_WiFiHub_idf1.<time>.txt` in the worktree folder. Ctrl+T Ctrl+L again closes it. **Close it and start a new one at every block boundary.** Move the closed file to the day folder under its block name.
- Typed keys go to the hub. `d` is read-only: within about 0.1 s it prints `APP_LORA: Stats: RX=%ld, ACKs=%ld, LastRSSI=%d`. **Use `d` as a time marker** when you wet a sensor or switch the AP. Never press `a` (it toggles the LoRa ACKs; press it again if you did, until you see `APP_LORA: ACK %s` with `ENABLED`). Never press `s` or `r` outside 15k-11 (`s` only on a LoRa bench, open question 8).

**IoT Hub (test plan 0.7, option B):**
```bash
cd "/c/Work/Projects/EfloStop 2/Firmware/Production/2.1.4_bench/2026-10-07"
az iot hub monitor-events -n resi-apex-iot-dev -g resi-apex-rg-dev -d GW-7C4FADAE69C8 \
   --content-type application/json --properties sys --timeout 0 | tee -i "B3_1200_contract_iothub.txt"
```
- `--properties sys` adds IoT Hub's own arrival time (`iothub-enqueuedtime`), which CP6-32 needs.
- `tee -i` ignores Ctrl+C, so `tee` keeps writing until `az` itself has exited and flushed.
- One file per block. At the block's end, first wait until the block's last expected message is in the file (look with `tail -n 3 <file>` in another tab), then Ctrl+C, then start the next file. If the last messages are missing, backfill (below).
- **Backfill** if the monitor died: add `--enqueued-time <epoch ms>` (for example `$(date -d "2026-10-07 10:45" +%s%3N)`) and `--timeout 90`.
- **Without `az`** (test plan 0.7 option A): VS Code, right-click the device, "Start Monitoring Built-in Event Endpoint" **after** the flash. At each block's end select the OUTPUT panel, Ctrl+A, Ctrl+C, and paste into the block's `_iothub.txt` saved as UTF-8. There is no backfill then, and nothing records IoT Hub's arrival time.

**File names** (all in the day folder; `B<n>` is the block of 5.1, `HHMM` its start):

| File | Name |
|---|---|
| UART | `B<n>_<HHMM>_<scope>_uart.txt` |
| IoT Hub | `B<n>_<HHMM>_<scope>_iothub.txt` |
| Notes | `B<n>_notes.md` (template below) |
| Twin | `twin_<label>.json` (the `twin` helper writes it) |
| Commands sent | `sent_GW-7C4FADAE69C8.tsv` (the `c2d` helper appends to it) |
| Phone screenshots | `B<n>_<testid>_<ios or android>_<what>.png` |
| Build | `build_cp6.log`, `size_cp6.txt`, `mapsearch_cp6.txt`, `hashes_cp6.txt` |

Notes template (one per block). **Never write the site Wi-Fi password into a notes file.**
```text
# B<n> <scope> - 2026-10-07
Image CP6 545b8f2, ELF SHA256 first 9 hex: ________   PC clock offset: ____ s   UTC offset: +__:__
Bench AP: SSID ______, channel __, outage method: ______ (black-hole check: silent / fast)
Valve: L | U (PSU off) | shielded;  PSU __.__ V
HH:MM:SS  action or observation   (one line per wet, dry, AP off/on, WAN out/in, PSU change,
                                   reset press, phone tap, LED colour, what the phone showed)
Tests run / skipped / deviations / anomalies:
Phones: model, OS, joined from Settings or the sign-in window, opened by itself? when?
```

### 3.5 C2D, twin and the payload file

In terminal T3 (Git Bash), once:
```bash
HUB=resi-apex-iot-dev; RG=resi-apex-rg-dev; GW=GW-7C4FADAE69C8
cd "/c/Work/Projects/EfloStop 2/Firmware/Production/2.1.4_bench/2026-10-07"
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
After the last line expect `IOTHUB: Twin desired patch: %s` and `IOTHUB: Twin: snapshot_interval_s = %d` (60). `TELEMETRY_V2: Snapshot interval set to %ds (persisted)` prints only if the value changed: the bench has run at 60 s, so it will usually not print (an unchanged value is skipped at DEBUG), and IoT Hub may send no patch at all. Either way, confirm in `twin_before.json` that `properties.reported.snapshot_interval_s` is 60.

Rules:
- **A C2D sent while the hub is offline is queued and delivered at the next connect,** possibly in the middle of a later step. Send nothing during outages, portals or DPS registrations unless the test says so. Purge the queue after a destructive block.
- Paste JSON from `payloads.txt`, never from Word or Outlook. A smart quote breaks the envelope, and a broken envelope runs the legacy keyword scan (test plan 0.8). If a `IOTHUB: C2D cmd='%s' ver=%d id='%s'` line shows `ver=0`, a message fell through to the legacy parser: stop and check it. **Except in CP6-31 row 1, where it is expected.**
- **Never put a legacy keyword** (`VALVE_OPEN`, `VALVE_CLOSE`, `LEAK_RESET`, `OVERRIDE_CANCEL`, `DECOMMISSION…`) in an `id`, label or name: use `vo-1`, not `valve_open-1`.
- Make every `id` unique: add `-2`, `-3` … when you send a line again.
- Desired twin properties are re-applied at every connect. If a test changes `hub_name`, put the original back at the end of the block.

**`payloads.txt`** (save in the day folder; fill in `<BLE4>` and the hub's name; one line each; the commands pasted many times a day):
```text
{"schema":"eflostop.cmd","ver":1,"id":"ss-v4m-1","cmd":"provision","payload":{"valve_id":"00:80:E1:27:F7:BB","ble_leak_sensors":["00:80:E1:2A:29:FC","00:80:E1:2A:B6:8E","00:80:E1:2A:CB:B6","<BLE4>"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"00:80:E1:2A:29:FC","location_code":"kitchen","label":"Sink"},{"sensor_type":"ble_leak_sensor","sensor_id":"00:80:E1:2A:B6:8E","location_code":"laundry","label":"Washer"},{"sensor_type":"ble_leak_sensor","sensor_id":"00:80:E1:2A:CB:B6","location_code":"bathroom","label":"Ensuite"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE4>","location_code":"bathroom","label":"Main"}],"auto_close_enabled":true}}
{"schema":"eflostop.cmd","ver":1,"id":"rc-def-1","cmd":"rules_config","payload":{"auto_close_enabled":true,"trigger_mask":7}}
{"schema":"eflostop.cmd","ver":1,"id":"vo-1","cmd":"valve_open"}
{"schema":"eflostop.cmd","ver":1,"id":"lr-1","cmd":"leak_reset"}
{"schema":"eflostop.cmd","ver":1,"id":"oc-1","cmd":"override_cancel"}
{"schema":"eflostop.cmd","ver":1,"id":"name-1","cmd":"set_hub_name","payload":{"name":"<original hub name>"}}
```
If only 3 sensors work, drop `<BLE4>` and its `sensor_meta` entry, and record the deviation. With a LoRa sensor (open question 8), add `"lora_sensors":["<LORA1>"]` and its `sensor_meta` entry (`"sensor_type":"lora"`) to `ss-v4m-1`.

**`payloads_destructive.txt`** (a separate file, opened only for S-6, S-8, S-9 and the day-2 DEC tests; every MAC filled in tonight). The smoke's own lines come from the test plan (section S step 9, DEC-11 step 1, T3-01 and T2-01): copy them in with the MACs filled in, so nothing is typed at the bench.
```text
{"schema":"eflostop.cmd","ver":1,"id":"dec-ble-1","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<MAC>"}}
{"schema":"eflostop.cmd","ver":1,"id":"dec-v-1","cmd":"decommission","payload":{"target":"valve"}}
```
After any decommission outside the smoke, re-send `ss-v4m-…` and run the 3.6 check.

### 3.6 State check at the start and end of every block (2 min)

From the last snapshot line (`TELEMETRY_V2: Pub %s: %s` with `snapshot`), or the IoT Hub capture:
- valve linked and open, `rmleak` false, no override;
- `"rules":{"auto_close_enabled":true,"trigger_mask":7}`;
- `system_health.reason` "All devices healthy";
- `FLEET_LED: rating=%s color=%s effect=SOLID` with GREEN.

Also: every sensor dry, PSU at 6.00 V, valve B powered, twin interval 60 s, the hub on the right network (5.2), no phone or laptop on `WiFi-Hub-69C8`.

To restore:
- valve closed: `vo-…` (refused while latched: wait for the auto-clear, or send `lr-…` once every sensor is dry);
- override active: `oc-…`;
- after any mask test: `rc-def-…`. Then check that `RULES_ENGINE: Config updated: auto_close=%s triggers=0x%02X` reads `0x07`.

Skipping this check is the main way one block's state spoils the next.

### 3.7 Sending results to Claude

**After each block:**
1. Close the UART file (Ctrl+T Ctrl+L) and stop the `az` monitor.
2. Move both into the day folder under the block's name, and save `B<n>_notes.md`.
3. Type one line in the Claude session, for example: *"B4 done 14:21, files in 2.1.4_bench\2026-10-07; ran 15k-1 Android, 15k-9, resets 2-4; skipped 15k-3 (no laptop); odd thing at 13:52, see notes."*
4. Start the next block's captures and carry on.

Claude then:
- runs `docs/telemetry/validate_capture.py` and its own contract checker on the captures;
- checks each test's lines and timings by the ESP times;
- answers with a Pass / Fail / Known-limit table, the figures 15k asks for, and anything that changes a later block.

| After | Wait for Claude's answer? |
|---|---|
| B0 | **Yes, for the build gate** (about 10 min). Start B1 meanwhile; B1 is discarded if the gate fails. |
| B1 | Start B2 at once, but **read the smoke verdict as soon as it comes** (about 20 min). A smoke Fail stops the day. |
| B2-B7 | No. Claude's answer may add a re-run to a later block. |
| Soak | The first 10 min the same evening; the rest the next morning. |

**Report at once.** Stop, keep the monitor running 30 s, save both captures and send them if you see any of these:
- **A reboot you did not cause:**
  - a `rst:` line other than the 10 s reset's own;
  - the `(N)` counter dropping back;
  - `Guru Meditation`, `abort() was called`, `***ERROR*** A stack overflow in task …`, or a task watchdog **(ESP-IDF)**.
- **Never on a normal run:**
  - `TELEMETRY_V2: TX queue full (%d) - %s not sent` (with any `%s`, the twin included);
  - `IOTHUB: %s called on iothub_task - refused` or `TELEMETRY_V2: %s called on iothub_task - refused` (grep the tag-free text `called on iothub_task - refused`);
  - `TELEMETRY_V2: Pub %s not confirmed (msg_id=%d) - not kept`;
  - `IOTHUB: cloud_tx: creation failed - rebooting`;
  - `IOTHUB: SNAP result outstanding for %lu s - cloud_tx busy`;
  - `IOTHUB: decommission: cloud_tx did not finish in %d s - offline buffer erased here`;
  - `MONITOR: LOW HEAP WARNING: %lu bytes free (watermark=%d)`;
  - `IOTHUB: SAS: esp_mqtt_set_config failed (%s) — client destroyed, rebooting`;
  - `TELEMETRY_V2: Snapshot not built - out of memory`, except within 5 s of a first `set_hub_name` (test plan 0.17);
  - two `boot` or `fast` snapshots on one connect (in the IoT Hub capture).
- **WP2d's busy-lock lines, which a normal run never prints:**
  - `RULES_ENGINE: Rules lock busy for 1 s - wet report from %s sensor %s kept, evaluated again next pass`;
  - `RULES_ENGINE: Provisioning busy for 1 s - leak from %s sensor %s decided on the last known devices and rules (%s)`;
  - `IOTHUB: Boot: rules engine missed the device list or rules - reading them again in the loop`.
- **Twin order (WP2e; HANDOFF §15k's report-at-once list):**
  - a session whose last `IOTHUB: Twin reported (%d): %s` misses a change applied in that session (a `hub_name`, `auto_close_enabled`, `trigger_mask`, `snapshot_interval_s` or device count the UART shows applied), with the session still up about 7 s later;
  - `IOTHUB: Twin report %u not taken by MQTT (msg_id=%d) - built again after %d s` with no fresh `Twin reported` within about 7 s while still connected.
- **A rules read that fell back to the defaults (audit finding 6):** `PROVISIONING: Failed to take mutex in get_rules_config`.
- **LoRa (only with a LoRa sensor):** `APP_LORA: Rx Queue Full! Packet dropped.`
- **Cloud admission:**
  - an escape line `… below the heap gate …` or `… whatever the heap …`;
  - `IOTHUB: cloud admission: the SoftAP's stop still not finished after %d s - the heap gate decides`.
- **esp-mqtt start/stop hand-over (ESP-IDF):** `Client asked to stop, but was not started` or `Client has started`.
- **Safety:**
  - `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window` with nobody at the valve button (T3-15's presses are exempt: write each press time in the notes);
  - `RULES_ENGINE: Reconnected: hub incident + valve open + RMLEAK clear — inferring physical override, starting 24h window` (no day-1 test expects it);
  - a valve that did not close on a wet report from an included sensor;
  - the valve open (`BLE_VALVE: [DATA] Valve State=%d (%s)` = 1) for 10 s or more while its own probe reads wet (`BLE_VALVE: [DATA] Leak=%d (%s)` = 1).

---

## 4. Pre-flight

### 4.1 Tonight (saves about an hour tomorrow)

| # | Task | Why |
|---|---|---|
| N1 | **Install the Azure CLI:** `winget install -e --id Microsoft.AzureCLI`. Then, in a new Git Bash: `az extension add --name azure-iot`, `az login`, and one test: `az iot hub monitor-events -n resi-apex-iot-dev -g resi-apex-rg-dev -d GW-7C4FADAE69C8 --content-type application/json --properties sys --timeout 0 \| tee -i test_iothub.txt`, with `tail -f test_iothub.txt` in a second tab: one heartbeat must appear in the file as it arrives, not only at Ctrl+C. Answer `y` if it asks to install a dependency. | Every capture, C2D and twin step assumes `az`. Without it, use the VS Code fallback (3.4). |
| N2 | Decide G0 (4.2). Then save the CP5 kit (4.3) and **build CP6 in its worktree (4.4) tonight.** Send the build material. | Claude checks the build gate tonight. B0 then shrinks to flash and boot. |
| N3 | Set up the bench AP (3.3; option A or A2). Note its SSID and channel. Give the hub a static lease, check that flow offloading is off, and run the black-hole check once with both DROP rules (or the WAN pull). | Every outage block. |
| N4 | **After CP6 is flashed** (tonight if N2 built it): put the hub on the bench AP with a reset and a phone set-up. Count it as reset #0. If you do it on CP5 and the phone cannot finish, leave it: in B0, flash CP6, set the bench AP up from its portal (that is reset #0), and capture 15k-11 on the next boot (Ctrl+T Ctrl+R). | The smoke's outage steps switch the bench AP off. |
| N5 | A replacement for sensor `…2B:A5`. Take the batteries out of valve A and wire the PSU. Find the valve's Low and Critical voltages if they are not on record (about 20 min). | `SS-V4`; S-7; B7. |
| N6 | Create the day folder, `payloads.txt` and `payloads_destructive.txt` (3.5), with the smoke's JSON lines (test plan section S step 9, DEC-11 step 1, T3-01, T2-01) copied in and every MAC filled in. Also every C2D of this plan's new tests (T5-05, T5-06, T3-15, CP6-20 … CP6-34). | No typing of JSON at the bench. |
| N7 | *Optional, for day 2:* build the 2.1.3 image in its worktree (test plan 0.4) and check its sizes: `.bss` 36,120 B, `.data` 21,556 B, `.bin` 1,502,240 B. | Upgrade and rollback (6.9). |
| N8 | The laptop for 15k-3/4 (E13): WSL `dig` (`sudo apt install bind9-dnsutils`) or Appendix A's `dns_check.py`, plus `dns_probe.py` from HANDOFF §15k item 3, saved as `$env:TEMP\dns_probe.py`. Save `dns_check.py` on the capture PC too (15k-9's LAN check). | 15k-3, 15k-4 (P1 when the laptop is ready), 15k-9. |
| N9 | Answer the open questions at the end of this plan (G0, the image, spare hub, decommission-all on the bench hub, soak, LoRa). | The day's order depends on them. |

### 4.2 Decision: run G0's remaining runs on CP6, or on CP5 first?

HANDOFF §15j and §15l say: finish G0 on CP5, then build CP6, because CP6's `fullclean` deletes the CP5 build. That reason no longer holds: CP6 is built in its own worktree (4.4), and the CP5 build stays where it is.

**Recommendation: run G0's remaining runs on CP6, folded into CP6's own runs.**
- G0-D runs in block B3 (its valve-unpowered dry step 4 at the start of B5, beside P14), G0-B in B4 (Android resets), G0-A in B5 (first phone run of 15k-7.2, with its 5 min connected before the pull and after the rejoin).
- G0-C (first setup on an empty hub) is 15k-8(a) on the spare hub, on day 2.
- **This reverses HANDOFF's order, so it is your decision.**

Evidence (from `git diff 31b4c9f 545b8f2`: 27 files):

| What G0 measures | Changed between CP5 and CP6? | So |
|---|---|---|
| Sensor advert interval and adverts per burst, connected | BLE scanner: only its heartbeat log line. Valve code: no change. `sdkconfig` and `sdkconfig.defaults`: no change. | Same on CP6 |
| Adverts per burst and leases on the router-fallback SoftAP | Radio-hold and page constants: no change. Wi-Fi scan, AP channel, CSA, DTIM and power save: no change. | Same on CP6 |
| The AP tail after a rejoin | **Changed (WP2):** 0.5 s with no station, 20 s with one, 60 s after a setup Connect; no TLS beside the SoftAP | CP6 is what WP8 builds on: the better baseline |
| Lease → first DNS → page; the sign-in window | **Changed (WP1, WP2):** new captive DNS; web server only while the SoftAP is up | CP6 is the baseline for G1 and G-CNA, which run on CP6-based images |
| The heap dip at AP start | Different make-up, and now measured properly (`min_ever` on the `idma:` line) | Better on CP6 |

What is lost by not running G0 on CP5:
1. An Android before/after comparison of the portal timings around the DNS change. CP5's figures exist only for one iPhone. *Mitigation:* the CP5 kit (4.3). Re-flash CP5 for a targeted A/B only if CP6 shows an Android sign-in problem (about 10 min).
2. Telling WP-V's change from WP1's if a reset fails. 15k-2 (×10) replaces WP-V's ×3 as the no-reboot check; only the attribution is lost.
3. G0-A's "start from a fragmented heap" variant: a CP5-only state whose figures are already on file.
4. CP5's 60 s AP tail as an advert-loss state. CP6's tails replace it.

**The alternative:** G0 on CP5 first (runs D, A, B ×2: about 2-2.5 h), then flash CP6. 5.6 has that day's timetable. B6, B7 and part of B5 then move to day 2.

### 4.3 Save the CP5 kit (2 min, in the project folder)

**Do this before anyone builds HEAD in the project folder:** that build regenerates `sdkconfig` and overwrites `build\`. First check that `(Get-FileHash sdkconfig).Hash` still reads `98F3B2CCE31FD3AF0FA61879C4E5591753E04986A2BD9494A721D862AE759767`. If it does not, stop and tell Claude: a copy with that hash was saved on 2026-10-06 in Claude's session scratchpad.

```powershell
cd "C:\Work\Projects\EfloStop 2\Firmware\Production\eFloStop_WiFiHub_idf1"
New-Item -ItemType Directory -Force "$env:TEMP\ref_31b4c9f" | Out-Null
Copy-Item build\eFloStop_WiFiHub_idf1.map, build\eFloStop_WiFiHub_idf1.elf, build\eFloStop_WiFiHub_idf1.bin, sdkconfig "$env:TEMP\ref_31b4c9f\"
$kit = "C:\Work\Projects\EfloStop 2\Firmware\Production\2.1.4_bench\cp5_kit_31b4c9f"
New-Item -ItemType Directory -Force "$kit\bootloader", "$kit\partition_table" | Out-Null
Copy-Item build\flash_args, build\eFloStop_WiFiHub_idf1.bin, build\eFloStop_WiFiHub_idf1.elf, build\eFloStop_WiFiHub_idf1.map, build\ota_data_initial.bin, sdkconfig $kit
Copy-Item build\bootloader\bootloader.bin "$kit\bootloader\"
Copy-Item build\partition_table\partition-table.bin "$kit\partition_table\"
```
- The project's `build\` holds CP5 today: `.bin` 1,546,144 B, written 2026-10-01 11:07. Building HEAD in the project folder later would overwrite it, so keep the kit.
- To flash CP5 back for an A/B: in the ESP-IDF PowerShell, `cd $kit`, then `python -m esptool --chip esp32s3 -p COM30 -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"`. Never `erase-flash`. NVS is compatible both ways: WP1-WP2e add no NVS key. That has not been tested on the bench.
- Also send any CP5 UART or IoT Hub captures made since 2026-10-01, and CP5's build log if you still have it.

### 4.4 Build CP6 in a worktree (HANDOFF §15j, adapted)

In the ESP-IDF 5.5.1 PowerShell:
```powershell
cd "C:\Work\Projects\EfloStop 2\Firmware\Production\eFloStop_WiFiHub_idf1"
(Get-FileHash sdkconfig).Hash      # expect 98F3B2CCE31FD3AF0FA61879C4E5591753E04986A2BD9494A721D862AE759767
git worktree add ..\hub_cp6 545b8f2
Copy-Item sdkconfig ..\hub_cp6\sdkconfig
cd ..\hub_cp6
git log --oneline -1               # 545b8f2 fix(cloud): keep the twin's staleness check within the reports queued
git status --short                 # prints nothing
(Get-FileHash sdkconfig).Hash      # the same hash as above
idf.py build *> "$env:TEMP\build_cp6.log" ; "exit=$LASTEXITCODE"
Select-String -Path "$env:TEMP\build_cp6.log" -Pattern 'warning:|error:' | ForEach-Object Line
Select-String -Path "$env:TEMP\build_cp6.log" -Pattern 'Processing \d+ dependencies|Manifest files have changed' | ForEach-Object Line
idf.py size
Compare-Object (Get-Content "$env:TEMP\ref_31b4c9f\sdkconfig") (Get-Content sdkconfig)
(Get-FileHash sdkconfig).Hash
Select-String -Path build\eFloStop_WiFiHub_idf1.map -Pattern '^\s+0x\w+\s+(esp_wifi_scan_get_ap_records?|esp_wifi_clear_ap_list|exit|__call_exitprocs|esp_mqtt_client_disconnect|esp_mqtt_client_get_outbox_size|esp_wifi_ap_get_sta_list|wifi_get_sta_list_process|lwip_getsockname|iothub_suspend_mqtt|iothub_resume_mqtt)\s*$' | ForEach-Object Line
Select-String -Path build\eFloStop_WiFiHub_idf1.map -Pattern '^\s+0x\w+\s+(xTaskCreateStaticPinnedToCore|xQueueGenericCreateStatic|xQueueCreateMutexStatic|offline_buffer_pending|offline_buffer_erase_for_restart|telemetry_v2_tx_post|telemetry_v2_post_snapshot|telemetry_v2_publish_snapshot|telemetry_v2_publish_lifecycle|iothub_pub_begin|provisioning_get_rules_and_state|rules_engine_retry_kept_reports)\s*$' | ForEach-Object Line
Select-String -Path build\eFloStop_WiFiHub_idf1.map -Pattern '^\s*\.text\.(twin_request|twin_build|twin_posted|post_twin_reported|publish_twin_reported)(\s|$)' | ForEach-Object Line
(Get-Item build\eFloStop_WiFiHub_idf1.bin).Length ; (Get-Item build\eFloStop_WiFiHub_idf1.bin).LastWriteTime ; (Get-FileHash build\eFloStop_WiFiHub_idf1.elf).Hash
git status --short
```
- **If the project's `sdkconfig` hash is not `98F3B2CC…AE759767`** (a build of HEAD regenerated it), copy the kit's `sdkconfig` into the worktree instead, if its hash is right; otherwise ask Claude for the saved copy. Never build CP6 with another `sdkconfig`.
- A fresh worktree needs no `fullclean`.
- Save the outputs as `size_cp6.txt`, `mapsearch_cp6.txt` and `hashes_cp6.txt` in the day folder, and copy `build_cp6.log` there.

### 4.5 Build pass criteria (VAL-01 steps 1, 5, 6 and HANDOFF §15j)

| # | Check | Pass | If not |
|---|---|---|---|
| B1 | `git log` and `git status` in the worktree | `545b8f2`; status prints nothing | wrong tree: stop |
| B2 | `sdkconfig` hash before the build | `98F3B2CCE31FD3AF0FA61879C4E5591753E04986A2BD9494A721D862AE759767` (CP5's) | stop: the configuration is not CP5's |
| B3 | configure | `Processing 4 dependencies:` (`espressif/led_strip` 3.0.2, `espressif/mdns` 1.9.1, `idf` 5.5.1, `jgromes/radiolib` 7.5.0); no `Manifest files have changed, solving dependencies`; `-- Components:` lists `wifi_portal` | a re-solve to the same four versions is not a failure: report it, commit nothing |
| B4 | build | `exit=0`. **Exactly 4 warnings:** `app_ble_valve.c:106:9` (`BLE_HS_ATT_ERR` redefined), `app_lora.cpp:194:5` ×2 (missing `uart_config_t` initializers), `app_lora.cpp:160:13` (`switch_sync_word` unused). No `error:`. | a warning in `components/wifi_portal`, `app_wifi.c`, `app_iothub.c`, `dps_client.c`, `telemetry_v2.c`, `offline_buffer.c`, `rules_engine.c`, `provisioning_manager.c`, `health_engine.c`, `app_ble_leak.c`, `monitoring.c` or `reset_button.c` is a finding |
| B5 | `Compare-Object` and the hash after the build | prints nothing; hash unchanged | stop: `sdkconfig` changed (WP3 is the first package allowed to change it) |
| B6 | IRAM | DIRAM `.text` **exactly 113,387 B**; IRAM 16,384 B (100 %) | stop and send both maps |
| B7 | map search 1 | **exactly 7 lines**, all at `0x420…`: `esp_wifi_scan_get_ap_record`, `esp_wifi_clear_ap_list`, `esp_mqtt_client_disconnect`, `esp_mqtt_client_get_outbox_size`, `esp_wifi_ap_get_sta_list`, `wifi_get_sta_list_process`, `lwip_getsockname` | `esp_wifi_scan_get_ap_records`, `exit`, `__call_exitprocs`, `iothub_suspend_mqtt` or `iothub_resume_mqtt` present: CP5 code |
| B8 | map search 2 | **exactly 10 lines:** `xTaskCreateStaticPinnedToCore` `0x40387ef0`, `xQueueGenericCreateStatic` `0x40384078`, `xQueueCreateMutexStatic` `0x403843e4`, and 7 at `0x420…`: `offline_buffer_pending`, `offline_buffer_erase_for_restart`, `telemetry_v2_tx_post`, `telemetry_v2_post_snapshot`, `iothub_pub_begin`, `provisioning_get_rules_and_state`, `rules_engine_retry_kept_reports` | `telemetry_v2_publish_snapshot` or `telemetry_v2_publish_lifecycle` present: pre-WP2c code |
| B9 | map search 3 | **exactly 4:** `.text.twin_request`, `.text.twin_build`, `.text.twin_posted`, `.text.post_twin_reported`; **no** `.text.publish_twin_reported` | `.text.publish_twin_reported` alone: `fcc0979` or earlier |
| B10 | `.bss` | **43,244-43,276 B** (includes the approved +6,301 B for `cloud_tx`) | outside 43,229-43,294 B: look. About 42,754 B: WP2d missing. About 36,453 B: WP2c missing. |
| B11 | `.data` | **21,566-21,574 B** | outside 21,562-21,578 B: look |
| B12 | flash, record only | `.text` 1,043,200-1,044,400 B; `.rodata` 373,700-374,500 B; total image 1,568,700-1,570,600 B; `.bin` 1,569,000-1,570,800 B (about 25 % of the 2 MB partition free) | not a Fail |
| B13 | `.bin` time, ELF hash | newer than 2026-10-02 16:01:57 +1000; record the ELF SHA256 | — |
| B14 | `git status --short` after the build | prints nothing; `dependencies.lock` unchanged | report it |

**Send:** `build_cp6.log`, the `idf.py size` output, the three map searches, both `sdkconfig` hashes, the `.bin` length and time, and the ELF SHA256. Send both maps if DIRAM `.text` moved.

### 4.6 Flash and first boot: 15k-11 (P1, 10 min)

**Flash** from the worktree: `idf.py -p COM30 flash`. **Never `erase-flash`** on this hub. Start a fresh IoT Hub capture. Then start the monitor with `--no-reset`, start its log (Ctrl+T Ctrl+L), and press Ctrl+T Ctrl+R so the whole boot is in the file (3.4).

**Expect, in boot order:**
1. `HUB_IDENT: Firmware version: v%s` → `v2.1.4`.
2. `PROVISIONING: Loaded existing config from NVS`, with the same device counts as before the flash.
3. `APP_WIFI: bench build (APP_BENCH_DIAG): Wi-Fi driver log at INFO - not for release` (W).
4. `IOTHUB: cloud_tx started (stack %u B, priority %u)` → `stack 5120 B, priority 3`, once, **before** `APP_WIFI: Connected! IP: %s`.
5. `APP_LORA: LoRa Task Started. Listening (encrypted mode)...` (unchanged; the production tool matches it).
6. Every `MONITOR: idma: free=%lu min=%lu largest=%lu min_largest=%lu allocfail=%lu min_ever=%lu` line ends in `min_ever=`.
7. Right after `APP_WIFI: Connected! IP: %s`: `IOTHUB: cloud admitted %lu.%lu s after the IP (internal DMA free %u B, largest %u B)`.
   - About 0.N s after the IP.
   - Free about 48-51 KB; largest about 28-30 KB or more.
8. Then `IOTHUB: Connected to Azure IoT Hub!`. No SoftAP at this boot.
9. IoT Hub: the `lifecycle` exactly once, the twin, then exactly one `boot` or `fast` snapshot.
   - Each snapshot line (`TELEMETRY_V2: Pub %s: %s` with `snapshot`) is followed within about 1 s (by the ESP times, not necessarily on the next line) by `IOTHUB: SNAP heartbeat=reset interval_ms=%lld`.

**The bench keys** (now read by `lora_task`). Type each into the monitor; each answers within about 0.1 s:
- `d` → `APP_LORA: Stats: RX=%ld, ACKs=%ld, LastRSSI=%d`;
- `a` → `APP_LORA: ACK %s` (`DISABLED`), then `a` again → `ENABLED`. **Leave the ACKs enabled.**
- `r` → `APP_LORA: Restarting RX...`.
- `s` only on a LoRa bench (open question 8) → `APP_LORA: Sending Test Packet`. Otherwise do not type it.
- **LoRa check (open question 8):** `APP_LORA: Initializing LoRa Driver...` prints with or without a radio, so it proves nothing. A LoRa sensor is usable only if its packets print `APP_LORA: Verified: ID=0x%lX, Batt=%d%%, Leak=0x%X, Sent=%u, Ack=%u` and `IOTHUB: Event: LoRa Packet from 0x%08lX`, and `d`'s `RX=` count rises.

**Pass:**
- items 1-9 hold;
- none of these prints:
  - `IOTHUB: Boot: rules engine missed the device list or rules - reading them again in the loop`;
  - `IOTHUB: QueueSet: %s queue could not be added%s` (a broken boot order: S1);
  - `IOTHUB: %s called on iothub_task - refused` or `TELEMETRY_V2: %s called on iothub_task - refused`;
  - `IOTHUB: cloud admission deferred: internal DMA free %u B, largest %u B (needs %u / %u)`;
  - `IOTHUB: lifecycle not built in 1 s - live messages go first`.

**Finding (not a Fail):** the deferral line on this boot means the 36 / 12 KB heap gate would add 60-180 s to every boot of a provisioned hub.

### 4.7 Baseline heap at rest (record; 5 min, repeated at lunch)

With the hub connected, the SoftAP down, and nothing happening for 5 min, record:
- the `free` of `MONITOR: heap: free=%lu min_ever=%lu largest_blk=%lu uptime=%lus`. Expected about 5.2-6.2 KB above CP5's figure in the same state. Only about +0.5 KB would mean the web server still runs in STA mode.
- the `idma:` line's `free` (expected about 29.5-32 KB; CP5 had 25,224 B), `largest`, `min_ever` and `allocfail`. `allocfail` must stay 0 all day on a normal run.

---

## 5. Timeline

### 5.1 Day 1 (Wednesday 2026-10-07)

**This is a 10-hour day: 08:30-18:30, with a 30 min lunch and a 5 min hand-off between blocks.** Every block's length is the sum of its tests' own minutes (in brackets), not a guess. If CP6 was built and flashed tonight (N2), B0 takes 20 min and every later block starts 25 min earlier; spend that time on B2's P2 items (CP6-21, CP6-22). If you would rather stop at about 17:30, cut from 5.3 in order.

The afternoon runs **B4 → B6 → B5**: reset #4 leaves the hub on the bench AP, which B6 needs, and LS-1 (WP2c's acceptance test, never cut) then runs before the long router block instead of at the end of the day.

| Block | Time | Length | Runs (section) [minutes] | Valve, network | Send at the end |
|---|---|---|---|---|---|
| **B0** Pre-flight | 08:30-09:15 | 45 min | PC prep (no sleep: `powercfg /change standby-timeout-ac 0`; note the clock offset); 4.3 CP5 kit; 4.4 build (unless built tonight); meanwhile the 3.1 checklist, the 3.5 Azure set-up, PSU 6.00 V, nRF Connect check; 4.6 flash and 15k-11 [10]; 4.7 baseline [5]; `ss-v4m-1` only if the device set differs | L; bench AP | build material, `B0_*`. **Wait for the build verdict.** |
| hand-off | 09:15-09:20 | 5 min | 3.7 steps 1-4 and the 3.6 check | | |
| **B1** Smoke | 09:20-10:10 | 50 min | 6.1: S-1 [2], S-2 [6], S-3 [3], S-4 [3], S-5 [4], S-6 [3], S-7 [5], S-8 [4], S-9 [4], S-10 = 15k-1 on the iPhone, reset #1 [15, with drying `<BLE3>`] = 49 | L; bench AP off/on in S-2/S-3 | `B1_*`, iPhone notes. The smoke verdict comes during B2: **a Fail stops the day.** |
| hand-off | 10:10-10:15 | 5 min | | | |
| **B2** Safety core | 10:15-11:55 | 100 min | 6.2, in this order: 15k-6 ×3 [20], 15k-14(g)(6) [5], T5-04 ×2 [8], T5-05 [6], T5-06 [8], T3-15 steps 1-4 [8], P11 [6], CP6-20 steps 1-4 and 6-7 [15], CP6-23a [8], CP6-24 [15] = 99. *P2 if time:* CP6-21 [10], CP6-22 [12], CP6-20 step 8 [4]. *With a LoRa sensor:* T5-13 [15] in place of the P2 items. | L, then U in CP6-24 | `B2_*` |
| hand-off | 11:55-12:00 | 5 min | | | |
| **B3** Cloud contract + G0-D | 12:00-13:00 | 60 min | 6.5 and 6.3: G0-D step 1 (35 min dry, nRF Connect) **while** sending CP6-29 [5], CP6-30 [10], CP6-31 [5], CP6-33 a-f [10], CP6-34 [5] = 35; then the mask check of CP6-34 [1]; then G0-D step 2 (2 wets of `<BLE3>`) [10]; then CP6-32 [10] = 56 | L; nothing wet until G0-D step 2 | `B3_*`, the nRF Connect export, `twin_*.json` |
| Lunch | 13:00-13:30 | 30 min | 4.7 repeated (the hub untouched). *Optional:* take `<BLE4>`'s battery out at 13:00 and back at 13:20: `device_offline` 600-635 s later, then `device_recovered` (VAL-11's sensor half). Claude analyses B0-B3. | L | — |
| **B4** Setup portal | 13:30-14:45 | 75 min | 6.3: reset #2 (Android → home router) [8] with 15k-9 in its tail [10]; reset #3: CP6-26's wet [1], then 15k-3 [15] and 15k-4(c)(d) [10] if the laptop is ready, the laptop's profile deleted [1], then the iPhone sets the bench AP up [8], CP6-26's resume close and restore [5]; reset #4 (Android → bench AP) [8] = 66. CP6-25 read on every run. | L | `B4_*`, phone notes, laptop outputs |
| hand-off | 14:45-14:50 | 5 min | | | |
| **B6** WAN black-hole and cloud path | 14:50-16:10 | 80 min | 6.5: the black-hole check (3.3) [3]; 15k-12 LS-1 ×3 [45] (run 1 with CP6-28; run 2 with the `d` key; run 3 with 15k-14(g)(1)); 15k-14(a) [5]; 15k-13(a) with reset #5 [25] = 78 | L; DROP rules or WAN pull | `B6_*` with T0 of each run in the notes |
| hand-off | 16:10-16:15 | 5 min | | | |
| **B5** Router outage and rejoin | 16:15-18:20 | 125 min | 6.4. **U:** P14 with G0-D step 4 and G0-A step 1 [10]; 15k-7.1 ×3 [18]; 15k-7.2 run 1 = G0-A (iPhone) with one CP6-27 wet [15], then G0-A step 6 [5]; 15k-5 G8 [25]. **L:** PSU on, `vo-…` [2]; 15k-7.1 L = 15k-10(b) run 1 [6 + 3 restore]; 15k-7.2 run 2 = 15k-10(b) run 2 [12 + 3]; 15k-7.3 = 15k-10(b) run 3 [12 + 3]; reset #6 (STA idle) [8] = 122. 15k-10(a) at every pull. | U, then L; bench AP off/on | `B5_*` |
| End of day | 18:20-18:30 | 10 min | hand-off; T4-14's grep (6.11) [5] | | `B5_*`, the grep's hit counts |
| **B7** Battery (P2) | only if B5 ended by 17:45 | 30 min | 6.7: T2-05, CP6-36. Otherwise D2-7. | PSU at Critical, then 6.00 V | `B7_*` |
| **B8** Soak start (P2) | after the last block, if before 18:45 | 15 min | 5.4. Otherwise start it on day 2's evening. | L | the soak's first 10 min |

Sums: 45 + 5 + 50 + 5 + 100 + 5 + 60 + 30 + 75 + 5 + 80 + 5 + 125 + 10 = 600 min (08:30-18:30), plus B7 and B8 when time allows.

**Slack is thin:** 1 min in B1 and B2, 2-4 min in B3, B5 and B6, 9 min in B4. A redo the plan asks for (a T5-04 re-wet before the clear, T5-05's missed 10 s window, a phone's second try, a re-run Claude asks for) comes out of the cut list (5.3), in order, never out of a P1 test.

**Use the waits:**
- the build (10-20 min): the 3.1 checklist, the 3.5 Azure set-up, `payloads.txt`;
- CP6-24's 6 min with the sensor wet and the valve away: make CP6-31's two files, set nRF Connect up next to `CB:B6` for B3;
- G0-D's 35 min dry phase: the contract checks of B3 (they send commands but wet nothing);
- lunch: the heap at rest (4.7) and, if you want, VAL-11's sensor half;
- each 2 min router-off wait in B5: write the notes line for that pull.

### 5.2 Reset ledger (15k-2 needs 10 runs: 4 with the STA connected, 3 idle, 3 connecting)

Numbered in the order they happen. #5 is a fifth connected run (an extra one).

| # | Block | STA state | Phone, network it sets up | Also |
|---|---|---|---|---|
| 0 | tonight (N4), on CP6 | connected | either, bench AP | not counted |
| 1 | B1 (S-10) | connected | iPhone, bench AP | 15k-1 iPhone |
| 2 | B4 | connected | Android, **home router** | 15k-1 Android, G0-B, 15k-9 |
| 3 | B4 | connected | iPhone, bench AP | CP6-26; 15k-3, 15k-4(c)(d) if the laptop is ready |
| 4 | B4 | connected | Android, bench AP | G0-B (second Android sample) |
| 5 | B6 | connected, during a `cloud_tx` stall | iPhone, bench AP | 15k-13(a) (P2) |
| 6 | B5 (last item) | **idle** (router-fallback SoftAP, between two router retries) | iPhone, bench AP (switch it on first) | — |
| 7-8 | D2-2 | idle | iPhone, bench AP | — |
| 9-11 | D2-2 | **connecting** (inside a router retry) | iPhone, bench AP | — |

Each run passes when it ends in the no-credential portal, as 15k-2 says (6.3).

### 5.3 If the day runs late

**Cut in this order** (the first item is cut first):
1. B8 (start the soak on day 2 instead);
2. B7 (T2-05, CP6-36) → D2-7;
3. B2's P2 items (CP6-21, CP6-22, CP6-20 step 8) → D2-2;
4. 15k-13(a) and reset #5 → D2-2;
5. 15k-7.2 run 2 (keep 15k-10(b)'s third run, the one inside a long stop);
6. G0-A step 6's 5 min, and G0-D step 4's 4 min beyond P14's 6 (keep P14);
7. reset #6 → D2-2 (with #7 and #8).

**Never cut:**
- B0's build gate, 15k-11, B1 smoke;
- 15k-6 (the baseline), 15k-14(g)(6), T5-04, T5-05, T5-06, T3-15 steps 1-4, P11, CP6-20 steps 1-4 and 6-7, CP6-23a, CP6-24;
- 15k-1 on both phones, 15k-9, CP6-26's resume close; 15k-3 and 15k-4(c)(d) when the laptop is ready;
- P14, 15k-7.1 (U) ×3, G0-A, one G8 pass, 15k-7.1 L and 15k-7.3 (15k-10(b) with a leak inside a stop), G0-D;
- the black-hole check, 15k-12 LS-1 ×3, 15k-14(a), 15k-14(g)(1).

### 5.4 Overnight soak (optional, P2)

Start at the end of day 1 on the main hub, if B8 runs:
1. `SS-V4` restored and checked (3.6).
2. Set the interval for the soak: `desired '{"snapshot_interval_s":300}'`. Expect `TELEMETRY_V2: Snapshot interval set to %ds (persisted)` (the value changes from 60).
3. New UART file with `--no-reset`, new `az` monitor with `--timeout 0`.
4. Do one activity round before leaving: wet and dry one sensor (auto-close, then auto-clear), `lr-…`, `vo-…`.
5. Leave the PC awake, nothing on COM30 but the monitor, no phone on `WiFi-Hub-69C8`, PSU at 6.00 V.

The next morning, Claude checks:
- no reboot (the `(N)` counter never drops back);
- `heap:` and `idma:` flat;
- one heartbeat every 300 s (−1 s / +3 s);
- no line from 3.7.

**The SAS renewal comes about 18 h after the hub's last boot:** `IOTHUB: SAS: within %d h of expiry — renewing`, then `IOTHUB: SAS: token renewed (valid %d h, expires ts=%ld)`. Every 10 s reset reboots the hub, so day 1's last boot is reset #6 at the end of B5 (about 18:00-18:15). The renewal is then due about 12:00-12:30 on day 2. Write the time of the last boot in the notes. After the renewal, Claude checks for `IOTHUB: MQTT client stopped on wifi_task in %lu.%lu s` and `IOTHUB: Connected to Azure IoT Hub!`.

Two ways to end it (open question 6):
- **Run it until the renewal** (about 12:30 on day 2). Use the morning for things that do not need the main hub: the worktree builds of D3 and N7, and the spare-hub lane (D2-5). This is the recommendation.
- **Stop it at 08:30** (about 14 h) if you need the main hub in the morning. CP6-43 (the short-SAS image) then covers the renewal.

Either way it is a CP6 check, not VAL-14 (the release's ≥ 19 h soak).

### 5.5 Day 2 and after (priority order)

| # | Block | Items | Needs | Time |
|---|---|---|---|---|
| D2-1 | Soak | Stop it after the SAS renewal (about 12:30), or at 08:30 (5.4). Send the files. Until it stops, work on D2-5 (spare hub) and the D3 worktree builds, which do not need the main hub. | — | 15 min of the engineer's time |
| D2-2 | Day-1 leftovers and safety extras | If S-7 was Blocked: T2-01 first. Anything cut from 5.3. 15k-2 resets #7-#8 (idle) and #9-#11 (connecting) [40]. CP6-23b (finding 5) [12]. T3-17 Part A ×1 (the valve shielded, not unpowered) [15] and T6-18 reps 2-3 [10]. T5-07 [6] and T3-15 step 5 (a press after a hub restart) [8]. T6-14 (a full offline ring, then a Wi-Fi reset and a power cycle; bench AP) [40]. CP6-27 ×3 [15]. T5-04 runs 4-5 [6]. With a LoRa sensor: CP6-24 step 6 with `<LORA1>` wet and the router off [20]. | bench AP; RF shield | 2.5-3 h |
| D2-3 | **P0 device-set changes** (6.6) | DEC-06, DEC-07, DEC-09, DEC-11 (in full) then DEC-12, DEC-13, DEC-14, DEC-15 (the WAN pulled on the bench AP), DEC-16; 15k-14(c)(d)(g)(2); the WP2d normal path (15k-17's last paragraph) | bench AP | 3-3.5 h |
| D2-4 | Upgrade and rollback (6.9) | T6-03 run A, CP6-41, T6-04, CP6-42 | the 2.1.3 worktree image (N7) | 1.5 h |
| D2-5 | Spare hub | **15k-8(a)(b): spare hub only, never `GW-7C4FADAE69C8`** (HANDOFF 15k item 8; `erase-flash` is allowed only there). In this order: 15k-8(a) (first commissioning = G0-C); CP6-38 (while empty); CP6-39; CP6-37; 15k-14(b) (decommission-all on a normal link, which also cleans up CP6-37); 15k-8(b) (DPS router pull), then T4-06 (DPS blocked, the same block) [45]; T6-02 (power cut during decommission-all) [30]. **CP6-37, CP6-39 and 15k-14(b) on the main hub only with your OK, and then ending with CP6-37's restore box.** DEC-18 in full needs the valve: main hub, after your OK. | spare hub; a DPS block on the bench AP | 3.5 h |
| D2-6 | Portal extras | CP6-44; 15k-5 linked and with the E2 laptop (15k-4(a)'s laptop run); 15k-4(b) (a second ESP32); 15k-3/4(c)(d) if not run | laptop, second ESP32 | 1.5 h |
| D2-7 | Battery and others | B7 if not run (T2-05, CP6-36); T2-04, T2-08, T6-15, T3-06, T3-05 [25], T3-19 [5], T4-05 (NTP blocked on the bench AP) [15]; T4-14's fresh-passkey pairing part; with a LoRa sensor, DEC-08 A, T3-08 and T4-13 | VB, flap rig, bench AP firewall | 2.5-3 h |
| D3 | **Bench-only images**, each in its own worktree at `545b8f2`, never shipped; flash CP6 back after them | 15k-16 first (T6-11 stack marks: it decides the `cloud_tx` stack size), then 15k-15 (fault), 15k-17 (hold), CP6-43 (short SAS), T5-09 (300 s override, with CP6-22's expiry variant) | worktree builds (they can run while other tests use COM30) | 6-8 h |
| Later | The rest of the test plan's procedures; the release campaign after WP10 (9.3) | — | the app build; the 2.1.3 image | — |

### 5.6 If G0 runs on CP5 first (option A of 4.2)

| Time | Block |
|---|---|
| 08:30-08:45 | PC prep, Azure set-up; CP5 still flashed |
| 08:45-10:45 | G0 on CP5 (HANDOFF §15d): run D (valve linked, nRF Connect), run A once (valve U), run B ×2 (iPhone, Android) |
| 10:45-11:15 | flash CP6 (built tonight in the worktree), 15k-11, 4.7 |
| 11:20-12:10 | B1 smoke |
| 12:10-12:40 | lunch |
| 12:40-14:20 | B2 safety core |
| 14:25-15:10 | B3 without G0-D (its contract checks and CP6-32) |
| 15:15-16:30 | B4 portal |
| 16:35-17:55 | B6 (LS-1 first: it is WP2c's acceptance test) |
| 18:00-18:40 | B5 cut to P14, 15k-7.1 (U) ×3 and 15k-7.3 (15k-10(b) run 3) |
| — | The rest of B5 (G8, 15k-7.1 L, 15k-7.2 run 2, reset #6), plus B7 and B8, move to day 2 |

---

## 6. The blocks, in priority order

Each entry gives: priority, time, start state (preconditions), steps, expected UART and IoT Hub, pass, evidence and the full procedure. "Start state `SS-V4`" means valve A plus `<BLE1>` … `<BLE4>`, labelled (`ss-v4m-1`), all heard and dry, valve open, battery Good, mask 7, no incident, no override, LED GREEN, interval 60 s. **Evidence** is always the block's UART file and IoT Hub capture, plus anything listed.

### 6.1 Block S: smoke (B1, P1, 50 min)

**Full procedure:** test plan section S. **Start state:** `SS-V4`, valve B powered within 2 m, PSU at 6.00 V. One UART file and one IoT Hub capture for the whole run.

| Step | Run | Time | CP6 expectations that differ from the test plan |
|---|---|---|---|
| S-1 | VAL-01 steps 1, 5, 6 | 2 min | Done by 4.5 and 4.6: the image is `545b8f2`, not `31b4c9f`. |
| S-2 | T4-02 rows 1-4: bench AP Wi-Fi off, power-cycle the hub, wait for the valve and the sensors, wet `<BLE1>`, then dry it | 6 min | Each event logs `TELEMETRY_V2: Time not synced (ts=%ld) - holding %s for replay; stamped when the clock syncs` (never `TELEMETRY_V2: Offline — buffering %s event` here). `APP_WIFI: SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`, with no `APP_WIFI: portal priority ON (no Wi-Fi credentials) - BLE scanning paused`. `APP_WIFI: Wi-Fi radio hold ON (%s) - BLE scanning paused` and `APP_WIFI: Wi-Fi radio hold OFF after %u s - BLE scanning resumed` come in pairs. No `IOTHUB: Connected to Azure IoT Hub!` and no `DPS:` line while the AP is off. |
| S-3 | T4-03 rows 1-3: bench AP on, then `vo-…` | 3 min | `APP_WIFI: router fallback: retrying the configured network (attempt %u)`, then `APP_WIFI: Connected! IP: %s` within about 40 s of the AP's return. Then `APP_WIFI: SoftAP tail after an automatic rejoin (no station on it) - it stops %d.%d s after the IP` (`0.5`), `IOTHUB: cloud admission deferred: SoftAP up - no TLS or DPS until it stops`, `APP_WIFI: SoftAP stopped (its servers too) %lu.%lu s after the IP`, `IOTHUB: cloud admitted %lu.%lu s after the IP (internal DMA free %u B, largest %u B)`, `IOTHUB: Connected to Azure IoT Hub!`. **The test plan's "the fallback SoftAP stops about 60 s later" is stale.** IoT Hub: the held events with real `ts` (≥ 1704067200), in order, before the `lifecycle`. A byte-identical duplicate is allowed. |
| S-4 | T5-04, one run | 3 min | Take the order from IoT Hub: `rmleak_auto_cleared` → `leak_detected` → `auto_close`. The valve ends closed with `BLE_VALVE: [DATA] RMLEAK=%d (%s)` = 1. |
| S-5 | T3-11, one run (shield the valve; do not power it off) | 4 min | none |
| S-6 | DEC-03 steps 1, 2, 3, 5 (step 4 cut to 60 s) | 3 min | The twin's `ble_leak_sensor_count` is 3 after the ack. The twin report can print on UART before the ack's `Pub event` line. |
| S-7 | T2-01 steps 1, 3-5 (PSU fast path) | 5 min | none. Needs the valve's Critical voltage (3.2). |
| S-8 | T3-01 steps 1-4 and 9 | 4 min | none |
| S-9 | DEC-11 short form | 4 min | The empty-shape `event` snapshot is published from `cloud_tx`. Paste `dec-11-s1` and `smoke-9a/b/c` from `payloads_destructive.txt`. |
| S-10 | **Replaced by 15k-1 on the iPhone (reset #1, 6.3).** First **dry `<BLE3>` fully** (S-9 removed it while wet: a damp sensor would latch a leak and close the valve in the middle of the portal run). Then re-provision with `ss-v4m-…`, check that `<BLE3>`'s first `BLE_LEAK: eleak %s — leak=%d batt=%d%% rssi=%d fw=%s` reads `leak=0`, and wait until all five devices are heard. Then the reset. | 15 min | The test plan's S-10 text (`portal priority OFF` "60 s ±5 s after `Connected! IP`") is stale. 15k-1 has the CP6 lines, and keeps S-10's portal-window checks (the hold lines, the negatives, the WHITE LED). |
| S-11 | VAL-15 step 1 on the smoke capture (Claude) | — | Claude classifies A-1 duplicates using the UART's `TELEMETRY_V2: Pub %s not confirmed (msg_id=%d) - kept for replay, a duplicate is possible`. |

**Smoke pass rule** (test plan section S, plus 3.7):
- every step passes; S-7 may be `Blocked (no PSU)` only, and then the day's verdict is at most "GO pending T2-01" (G2);
- none of these anywhere in the smoke UART file: `Guru Meditation`, `abort()`, a stack overflow, a watchdog, an unplanned reboot, `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window`, an `rc=6` write, `TELEMETRY_V2: Snapshot not built - out of memory`, or any line of 3.7.

**A smoke Fail stops the day.** Send the files and wait for Claude.

### 6.2 Block L: leak → auto-close safety core (B2, 100 min)

Start state `SS-V4`, valve linked. Run the tests in this order; each starts from the previous one's end state: 15k-6 ×3, 15k-14(g)(6), T5-04 ×2, T5-05, T5-06, T3-15 steps 1-4, P11, CP6-20 steps 1-4, *(CP6-21, P2)*, CP6-20 steps 6-7, *(CP6-20 step 8, P2)*, *(CP6-22, P2)*, CP6-23a, CP6-24. CP6-23b is on day 2.

**One rule for every test below:** inside one incident only the first wet prints `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s`. A later wet, while the valve reads closed with RMLEAK=1 or within 10 s of the last AUTO-CLOSE, returns at DEBUG (`rules_engine.c:912`, `:921`), which this image does not print. And never re-wet a sensor that has not yet reported `leak=0`: it gives no new `leak=1` (a dry report can take 110 s). Use another sensor.

#### 15k-6 / T5-03: leak → RMLEAK → CLOSE → dry → auto-clear → open, ×3 (P1, 20 min)

**Proves:** the leak path is unchanged by WP1-WP2e, within fixed bounds. It is also the baseline for 15k-10 and 15k-12, and it checks D1 (`4e6fe71`, never benched). 2.1.3's never-run bench cases P11 and P14 (provision_pulse TEST_PLAN) run separately: P11 here in B2, P14 at the start of B5.

**Steps** (run 1 with `<BLE1>`, run 2 with `<BLE2>`, run 3 with `<BLE3>`):
1. Press `d`, then wet the sensor.
2. **Expect** (by the ESP times):
   - `BLE_LEAK: eleak %s — leak=%d batt=%d%% rssi=%d fw=%s` with `leak=1`, at most 20 s after the wetting;
   - `RULES_ENGINE: LEAK INCIDENT latched by %s sensor %s`;
   - `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s`;
   - `BLE_VALVE: [TASK] CMD: SET_RMLEAK` and `BLE_VALVE: [CMD] Writing %s=%u` (`RMLEAK=1`), **before** `BLE_VALVE: [TASK] CMD: CLOSE_VALVE` and `BLE_VALVE: [CMD] Writing %s=%u` (`Valve=0`);
   - `BLE_VALVE: [DATA] RMLEAK=%d (%s)` (1) and `BLE_VALVE: [DATA] Valve State=%d (%s)` (0);
   - `IOTHUB: Event: BLE Leak %s leak=%d batt=%d`.
   - **IoT Hub:** `leak_detected`, then `auto_close` (`"rmleak_asserted":true`), then an `event` snapshot (valve closed, `rmleak` true, health critical). LED RED.
3. Dry the sensor.
   - `leak=0` within 110 s.
   - `RULES_ENGINE: AUTO-CLEAR: all sensors clear for %ds — clearing RMLEAK` 10-12 s after `leak=0`.
   - IoT Hub: `leak_cleared`, then `rmleak_auto_cleared` (`clear_after_seconds` 10).
4. **D1:** about 5 s after the release, `IOTHUB: SNAP trigger=event:%s` (renders `event:rmleak`) and a snapshot with the valve's `"rmleak":false`.
5. Send `vo-…`. Expect the ack `ok`, `BLE_VALVE: [TASK] CMD: OPEN_VALVE`, then `BLE_VALVE: [DATA] Valve State=%d (%s)` with 1. LED GREEN.

**Pass:**
- in every run, `leak=1` → `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s` **≤ 200 ms**, and `leak=1` → `BLE_VALVE: [CMD] Writing %s=%u` (`Valve=0`) **≤ 1 s** (the project notes give under 50 ms as typical);
- RMLEAK is written before CLOSE in every run;
- IoT Hub order `leak_detected` → `auto_close` → snapshot;
- D1's snapshot in every run;
- `valve_open` is acked `ok` and the valve opens.

**Record per run** (Claude does it from the log):
- `leak=1` → `AUTO-CLOSE + RMLEAK triggered` (milliseconds);
- `leak=1` → `Writing Valve=0`;
- `leak=1` → `Valve State=0`.
- **The "15k-6 spread"** is the largest `leak=1` → `Writing Valve=0` of the three runs, plus 100 ms, and never more than 1 s (2.1). 15k-10, 15k-12, CP6-27 and 15k-14(g)(6) are judged against it.

**Full procedure:** test plan T5-03; HANDOFF §15k item 6.

#### 15k-14(g)(6): a leak while a twin is owed and provisioning is busy (P1, 5 min, after 15k-6 run 3)

**Why:** WP2e's own leak-latency risk (W2): a twin build waits up to 1 s on each of its five provisioning reads. It is a separate wet and does not count toward 15k-6's spread. (W2 has a fix on the branch after CP6: `505eddc`, `1762a65`, `fdb8c28`.)
1. Send `ss-v4m-…` (the 4-sensor provision, devices unchanged) and `rc-def-…` back to back.
2. Wet `<BLE1>` at once, and press `d`.
3. Record `leak=1` → `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s` → the `Valve=0` write.
4. Restore: dry `<BLE1>`, wait for the auto-clear, `vo-…`.

**Pass:** `leak=1` → the `Valve=0` write within the 15k-6 spread. 1 s or more is a finding for W2.

**Full procedure:** HANDOFF §15k item 14(g)(6).

#### T5-04: re-wet at the clear, 2 more runs (P1, 8 min)

Smoke S-4 was run 1.
- **Expect** in IoT Hub: `rmleak_auto_cleared` → `leak_detected` → `auto_close`.
- The valve ends closed with RMLEAK = 1.
- Never `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window`.
- A run whose re-wet lands before the clear does not count: redo it.

**Full procedure:** test plan T5-04. Runs 4-5 on day 2.

#### T5-05: the interlock: nothing opens the valve while it is locked (P1, 6 min)

**Proves:** `leak_reset` is refused while any source is wet; `valve_open` and `valve_set_state` open are refused while locked, also with the valve disconnected (E-06); `leak_reset` is accepted once all are dry; the valve then opens. **Start:** `SS-V4` (T5-04 restored). The ids and JSON are the test plan's (T5-05).

1. **(a) Wet and locked.** Wet `<BLE1>`; wait for `BLE_VALVE: [DATA] RMLEAK=%d (%s)` (1) and `BLE_VALVE: [DATA] Valve State=%d (%s)` (0). Send `t5-05-reset-wet` (`leak_reset`), `t5-05-open-wet` (`valve_open`) and `t5-05-set-open-wet` (`valve_set_state` open), about 3 s apart.
   - `IOTHUB: Command: LEAK_RESET`, then `RULES_ENGINE: LEAK_RESET refused — %u leak source(s) still active (use override to open during a leak)`;
   - `IOTHUB: Command: VALVE_OPEN`, then `IOTHUB: VALVE_OPEN refused — %s`;
   - `IOTHUB: Command: VALVE_SET_STATE -> open`, then `IOTHUB: VALVE_SET_STATE open refused — %s`;
   - **no** `BLE_VALVE: [CMD] Writing %s=%u` with `RMLEAK=0` or `Valve=1`;
   - acks, `detail` exact: "A leak is still active. Fix the leak first, or use override to open the valve during a leak." (`leak_reset`), then twice "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak." LED RED.
2. **(b) Locked, valve disconnected.** Keep `<BLE1>` wet. PSU off; wait for `BLE_VALVE: [DISCONNECT] reason=0x%02x`. Send `t5-05-open-off` (`valve_open`): the same RMLEAK refusal (it comes from the latched incident; the disconnect cleared the valve's RMLEAK cache). PSU on within 3 min. At `SETUP COMPLETE - READY FOR GATT`: `RULES_ENGINE: Reconnected with %d active leak(s) — valve already closed + RMLEAK asserted, nothing to do` or `RULES_ENGINE: Valve reconnected with %d active leak(s) — executing auto-close`; the valve ends closed with RMLEAK=1.
3. **(c) Dry, inside the 10 s window.** Dry `<BLE1>`. As soon as `RULES_ENGINE: All sensors clear — auto-clear timer started (%ds)` prints, send `t5-05-reset-dry` (pasted and ready).
   - `RULES_ENGINE: LEAK_RESET: clearing incident (hub_latch=%d, valve_rmleak=%d, override=%d)`, `IOTHUB: Leak incident cleared, RMLEAK reset`, `BLE_VALVE: [CMD] Writing %s=%u` (`RMLEAK=0`);
   - IoT Hub: the ack `ok`, then `rmleak_cleared`; no `rmleak_auto_cleared`.
   - If the window was missed (`rmleak_auto_cleared` arrives instead), redo (a) and (c).
4. **(e) Released.** Send `t5-05-open-ok` (`valve_open`): ack `ok`, the valve opens. LED GREEN.

**Pass:** the three refusals in (a) and the one in (b) byte-exact, with no `RMLEAK=0` or `Valve=1` write while locked; (c) and (e) as listed. (d), the no-op `leak_reset`, is left out.

**Full procedure:** test plan T5-05.

#### T5-06: the remote override: it opens during a leak, blocks a new leak, and its cancel re-closes (P1, 8 min)

**Start:** `SS-V4`. The ids and JSON are the test plan's (T5-06).
1. `t5-06-ovr-none` (`override_enable`, nothing wet): `RULES_ENGINE: override_enable: no active incident to override`; ack "No active leak to override. Use the normal Open Valve control."
2. Wet `<BLE1>`: the 15k-6 sequence (RMLEAK then CLOSE).
3. `t5-06-ovr-on` (`override_enable`, `<BLE1>` still wet):
   - `RULES_ENGINE: OVERRIDE WINDOW STARTED: auto-close blocked for 24h (expiry=%ld)`, `RULES_ENGINE: override_enable: 24h override started remotely — RMLEAK cleared, valve opening`;
   - `BLE_VALVE: [CMD] Writing %s=%u`: `RMLEAK=0` **before** `Valve=1`; no `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window`;
   - IoT Hub: ack `ok`, `water_access_override_enabled` (`trigger` `c2d_command`, `remaining_s` 86400), `valve_state_changed` open.
4. Wet `<BLE2>`:
   - `RULES_ENGINE: Override active — auto-close BLOCKED for %s sensor %s (remaining=%lds)`; **no** `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by …`; no `Valve=0` write;
   - IoT Hub: `leak_detected` (`<BLE2>`), `auto_close_blocked_override`; the valve stays open.
5. `oc-…` with both still wet:
   - `IOTHUB: Command: OVERRIDE_CANCEL`, `RULES_ENGINE: OVERRIDE WINDOW CANCELLED (remaining_s=%ld)`, `RULES_ENGINE: Override cancelled with %d active leak(s) — executing auto-close` (2);
   - `BLE_VALVE: [CMD] Writing %s=%u`: `RMLEAK=1` **before** `Valve=0`, then `[DATA] RMLEAK=1` and `[DATA] Valve State=0`;
   - IoT Hub: ack `ok`, `auto_close_reenabled` (`reason` `c2d_command`), `valve_state_changed` closed with `rmleak` true; no `auto_close` event (expected on this path).
6. Restore: dry both, wait for the auto-clear, `vo-…`.

**Pass:** steps 1 and 3-5 as listed; the valve closed within 3 s of the cancel's ack.

**Full procedure:** test plan T5-06 (its step 5, `valve_open` during the window, is left out).

#### T3-15: a genuine valve-button press still starts the 24 h window (P1, steps 1-4, 8 min)

**Why:** the counterpart of the F-01 fix: the hub must not read its own clears as a press, but a real press must still count. **Write the time of each press in the notes:** it exempts that press's `RMLEAK cleared externally` line from 3.7.
1. Wet `<BLE1>` and keep it wet. Wait for `BLE_VALVE: [DATA] RMLEAK=%d (%s)` (1), then 10 s more (past the 5 s grace).
2. Long-press valve A's button. Expect the valve's `[DATA] RMLEAK=0` and `[DATA] Valve State=1`; within 30 s (normally 2 s) `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window` and `RULES_ENGINE: OVERRIDE WINDOW STARTED: auto-close blocked for 24h (expiry=%ld)`; IoT Hub `water_access_override_enabled` with `trigger` `button`. LED stays RED.
3. Wet `<BLE2>` (not `<BLE1>` again): `RULES_ENGINE: Override active — auto-close BLOCKED for %s sensor %s (remaining=%lds)`, `auto_close_blocked_override`; the valve stays open.
4. Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-15-cancel","cmd":"override_cancel"}`: `RULES_ENGINE: OVERRIDE WINDOW CANCELLED (remaining_s=%ld)`, `RULES_ENGINE: Override cancelled with %d active leak(s) — executing auto-close`, `RMLEAK=1` then `Valve=0`.
5. Restore: dry both, auto-clear, `vo-…`.

**Pass:** steps 2-4 as listed. Step 5 of the test plan (a press after a hub restart) is on day 2 (D2-2); step 6 (a press within about 1 s of the close write) is the documented fail-closed residual (2.2).

**Full procedure:** test plan T3-15.

#### P11 / T3-16: a leak across a hub boot (P1, 6 min)

**Why:** 2.1.3's never-run bench case P11 (provision_pulse TEST_PLAN): a sensor wet within the first minute of a boot, while the LED is still WHITE, must turn it RED and close the valve. It also runs the boot's pended-close path (T3-16) and WP2d's boot read.
1. Valve linked and open, all dry. Wet `<BLE1>` and keep it wet. Within 10 s press Ctrl+T Ctrl+R (the hub resets with the log running). Write the reset time in the notes.
2. Expect:
   - `<BLE1>`'s first `leak=1` → `RULES_ENGINE: LEAK INCIDENT latched by %s sensor %s` and `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s` ≤ 200 ms;
   - either live (after `SETUP COMPLETE - READY FOR GATT`: `RMLEAK=1`, then `Valve=0`) or pended (`RULES_ENGINE: AUTO-CLOSE: valve not connected — scanning; close deferred to reconnect reconciliation`, two `BLE_VALVE: [CMD] %s write not ready. Queuing val=%u` lines, then at setup `BLE_VALVE: [CMD] Applying pending %s command=%d`, RMLEAK first). Record which;
   - `BLE_VALVE: [DATA] Valve State=%d (%s)` (0) and `BLE_VALVE: [DATA] RMLEAK=%d (%s)` (1) within 5 s of `SETUP COMPLETE - READY FOR GATT`;
   - the LED RED once `leak=1` is heard, and never back to WHITE while `<BLE1>` is wet (`FLEET_LED: rating=%s color=%s effect=SOLID`);
   - IoT Hub: `leak_detected`, `auto_close`; a snapshot `critical` that names "Sink";
   - **no** `IOTHUB: Boot: rules engine missed the device list or rules - reading them again in the loop`.
3. Restore: dry, auto-clear, `vo-…`.

**Pass:** as listed. T3-16's ×5 repeat is for later.

**Full procedures:** `docs/provision_pulse/TEST_PLAN.md` P11; test plan T3-16.

#### CP6-20: a sensor the mask excludes, a monitor-only hub (P1; step 8 P2; 15 min)

**Proves:**
- `rules_config` writes and their echo;
- a sensor whose trigger bit is off gives `leak_detected` and no close;
- a masked wet sensor still blocks `leak_reset`;
- a monitor-only hub (auto-close off) never closes, even at a valve relink (WP2d's normal path, 15k-17).

**Steps:**
1. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-20-s1","cmd":"rules_config","payload":{"trigger_ble_leak":false}}'`
   - `RULES_ENGINE: Config updated: auto_close=%s triggers=0x%02X` (`enabled`, `0x06`).
   - Ack `ok` within about 1 s; it may take up to 5 s while the rules lock is busy.
   - An `event` snapshot with `"rules":{"auto_close_enabled":true,"trigger_mask":6}`; the twin's `trigger_mask` is 6.
2. Wet `<BLE1>`.
   - `leak=1` and `IOTHUB: Event: BLE Leak %s leak=%d batt=%d`.
   - **No** `RULES_ENGINE: LEAK INCIDENT latched by …` and **no** `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by …`.
   - IoT Hub: `leak_detected` (`sensor_id` `<BLE1>`), **no `auto_close`**.
   - The valve stays open. The snapshot's `system_health` is `critical`, "Leak detected: Sink". LED RED.
3. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-20-s3","cmd":"leak_reset"}'`
   - Ack error, `detail` exactly: "A leak is still active. Fix the leak first, or use override to open the valve during a leak."
4. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-20-s4","cmd":"override_enable"}'`
   - Ack error: "No active leak to override. Use the normal Open Valve control."
5. **If CP6-21 runs (P2), keep `<BLE1>` wet and go straight to it.** Otherwise dry `<BLE1>`, wait for its `leak=0`, send `rc-def-…` and check `triggers=0x07`.

**After CP6-21, or step 5** (all dry, valve open, mask 7):

6. Make the hub monitor-only: `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-20-s6","cmd":"rules_config","payload":{"auto_close_enabled":false}}'`.
   - Ack within about 1 s.
   - The snapshot shows `"auto_close_enabled":false`.
7. Wet `<BLE2>`.
   - `leak_detected` only: no RMLEAK, no CLOSE, no `auto_close`.
   - Switch the PSU off for 10 s, then on. **Keep `<BLE2>` wet** until the reconnect banner prints `RULES_ENGINE: ║ Hub:   incident=%d leaks=%d override=%s` with `leaks=1` (the relink check means nothing if the sensor has already reported dry). Then expect **no** `RULES_ENGINE: Valve reconnected with %d active leak(s) — executing auto-close` and no `BLE_VALVE: [CMD] Writing %s=%u` (this path checks `auto_close_enabled`).
   - Only then dry `<BLE2>`.
   - Send `rc-def-…`; `triggers=0x07`, auto-close enabled.
8. *(P2)* **`leak_reset`, then an immediate wet** (15k-17's normal path):
   1. Wet `<BLE1>` (it closes).
   2. Dry it. Within the 10 s before the auto-clear, send `lr-…`.
   3. Wet `<BLE1>` again right after the ack.
   - IoT Hub: `rmleak_cleared` before `auto_close`. The valve ends closed.
   - Restore: dry, auto-clear, `vo-…`.

**Pass:**
- steps 1-4 and 6-7 as listed;
- none of WP2d's busy-lock lines (3.7).

**Full procedure:** here (for steps 6-8, HANDOFF §15k item 17, last paragraph).

#### CP6-21: the hub closes at a valve relink for a sensor the mask excludes (P2 + OBS, audit finding 3; 10 min; B2 if time, else D2-2)

**Mechanism** (`rules_engine.c` at `545b8f2`): a masked wet sensor is still counted as an active leak, and the reconnect path checks `auto_close_enabled`, not the mask. 2.1.3 does the same.

**Start:** CP6-20 step 4 (mask 6, `<BLE1>` wet, valve open).
1. PSU off. Wait for `BLE_VALVE: [DISCONNECT] reason=0x%02x`, then 10 s more. PSU on.
2. **Expect, per the code:**
   - `RULES_ENGINE: Valve reconnected with %d active leak(s) — executing auto-close`;
   - `BLE_VALVE: [CMD] Writing %s=%u` with `RMLEAK=1`, then `Valve=0`;
   - IoT Hub: `auto_close` with `"cause":"reconnect"`, `"rmleak_asserted":true`, `"active_leak_count":1`.
   - The correct behaviour would be no write at the relink.
3. `vo-…`: refused with "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."
4. Restore: dry `<BLE1>`, wait for the auto-clear, `vo-…`, `rc-def-…` (check `0x07`).

**Result:**
- `Known-limit` if it closes as described (the safe side).
- `Fail` only for a reboot or the valve opening by itself.
- **Record** whether it closed.

**Full procedure:** here.

#### CP6-22: `override_cancel` closes for a sensor the mask excludes (P2 + OBS, audit finding 3; 12 min; B2 if time, else D2-2)

1. Mask 7. Wet `<BLE1>`: the valve closes.
2. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-22-s2","cmd":"override_enable"}'`
   - `RULES_ENGINE: override_enable: 24h override started remotely — RMLEAK cleared, valve opening`.
   - IoT Hub: `water_access_override_enabled` (`trigger` `c2d_command`, `remaining_s` 86400).
   - The valve opens.
3. Dry `<BLE1>`; wait for `leak=0`.
4. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-22-s4","cmd":"rules_config","payload":{"trigger_ble_leak":false}}'` → `triggers=0x06`.
5. Wet `<BLE2>` (excluded by the mask, but tracked).
6. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-22-s6","cmd":"override_cancel"}'`
   - **Expect, per the code:** `RULES_ENGINE: Override cancelled with %d active leak(s) — executing auto-close`.
   - IoT Hub: `auto_close_reenabled` (`reason` `c2d_command`), and `valve_state_changed` `closed` with `rmleak` true.
   - No `auto_close` event.
7. Restore: dry `<BLE2>`, auto-clear, `vo-…`, `rc-def-…`.

**Result:** `Known-limit` if it closes; record it. The third path, the override expiry, needs T5-09's 300 s image (day 3).

**Full procedure:** here.

#### CP6-23a: the valve's own flood probe as a leak source (P1, 8 min; mask 7)

**Why:** the valve's probe is a leak source of its own, with its own trigger bit, RMLEAK path, auto-clear and override floor (`rules_engine.c:1480-1485`). **To wet it:** lay the probe on a wet cloth (test plan 3.0); keep the battery compartment and the PSU leads dry.
1. Check that the last `RULES_ENGINE: Config updated: auto_close=%s triggers=0x%02X` reads `0x07`. Wet the probe.
   - `BLE_VALVE: [DATA] Leak=%d (%s)` (1);
   - `RULES_ENGINE: LEAK INCIDENT latched by %s sensor %s` and `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s` (both `valve`);
   - `RMLEAK=1` before `Valve=0` (the valve may already have closed itself: record it);
   - IoT Hub: `leak_detected` and `auto_close` with `"source_type":"valve"`, `valve_id`, `"rmleak_asserted":true`. LED RED.
2. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-23a-s2","cmd":"override_enable"}'` → `RULES_ENGINE: override_enable: valve flood probe wet — refusing`, and the ack error "Water detected at the valve. It can't be opened remotely until the valve area is dry."
3. Dry the probe.
   - `RULES_ENGINE: AUTO-CLEAR: all sensors clear for %ds — clearing RMLEAK`;
   - `rmleak_auto_cleared` with `valve_id`;
   - `vo-…` gives `ok`, and the valve opens.

**Pass:** as listed.

#### CP6-23b: the valve's own flood probe with `trigger_valve_flood` off (OBS, audit finding 5; 12 min; D2-2, or B2 if time)

1. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-23-s1","cmd":"rules_config","payload":{"trigger_valve_flood":false}}'` → `triggers=0x03`.
2. *(OBS)* Wet the valve's flood probe.
   - `BLE_VALVE: [DATA] Leak=%d (%s)` with 1.
   - **No** `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by …`.
   - IoT Hub: `leak_detected` with `source_type` `valve` and `valve_id`; no `auto_close`.
   - **Record:** does the valve close by itself (`BLE_VALVE: [DATA] Valve State=%d (%s)` with 0, `valve_state_changed` `closed`), and which value does `BLE_VALVE: [DATA] RMLEAK=%d (%s)` report?
3. *(OBS)* `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-23-s3","cmd":"valve_open"}'` with the probe still wet. The hub refuses an open only for RMLEAK, a latched incident or a critical battery, not for a wet probe. So:
   - if step 2 showed RMLEAK=0: expect ack `ok` and `BLE_VALVE: [CMD] Writing %s=%u` (`Valve=1`);
   - if it showed RMLEAK=1 (the valve set its own): expect the ack error "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.";
   - record which, and whether the valve refuses the open or closes again.
   - **Stop and report at once (3.7, Safety)** if `BLE_VALVE: [DATA] Valve State=%d (%s)` reads 1 for 10 s or more while `BLE_VALVE: [DATA] Leak=%d (%s)` reads 1: a valve open while standing in water is a valve-FW and hub safety finding for your decision, not a known limit. A brief open, then a re-close, is `Known-limit`: record how long it stayed open.
4. *(OBS)* `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-23-s4","cmd":"override_enable"}'`
   - Either "No active leak to override. Use the normal Open Valve control."
   - Or "Water detected at the valve. It can't be opened remotely until the valve area is dry."
   - Record which.
5. Dry the probe; wait 15 s. **Record** whether the valve reopens by itself. IoT Hub: `leak_cleared` (`valve`).
6. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-23-s6","cmd":"rules_config","payload":{"trigger_valve_flood":true}}'` → `0x07`. Then `vo-…` if the valve is closed.

**Result:** steps 1 and 6 as listed; steps 2-5 `Known-limit` with the observations, except step 3's stop rule.

**For the app:** clearing bit 2 does not keep the water on when the valve itself is wet.

#### CP6-24: a leak while the valve is unreachable (P1 + OBS; T6-18, T3-17's relink order, audit finding 2; 15 min)

**Proves:**
- **T6-18 on CP6:** `iothub_task` no longer waits behind a blocking C2D, so the leak is evaluated in ≤ 200 ms (it was up to 10 s);
- the `override_enable` reachability refusal;
- the valve's 180 s grace;
- RMLEAK first at the relink.

It also measures audit finding 2: `auto_close` repeats while the valve is away.

**Start:** `SS-V4`, mask 7, all dry, valve open.
1. **T0:** PSU off.
   - `BLE_VALVE: [DISCONNECT] reason=0x%02x`.
   - The next snapshot's valve: `"state":"disconnected","connected":false`, with no `battery`, `leak_state`, `rmleak` or `fw_version` keys.
   - `system_health` `warning`, "Valve disconnected". LED YELLOW.
2. **T0 + 20 s:** `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-24-s2","cmd":"override_enable"}'`
   - `IOTHUB: Command: OVERRIDE_ENABLE`.
   - `RULES_ENGINE: override_enable: valve not ready — reconnecting (<=%dms)`: its time is Tc.
   - **Keep the PSU off until `RULES_ENGINE: override_enable: valve unreachable after reconnect window` has printed.** `override_enable` waits up to 10 s for the valve and checks for a leak only after that wait (`rules_engine.c:1453-1478`). Powering the valve inside the wait would start a 24 h override on the leak you are about to make, and open the valve. (A code observation for your decision: open question 10.)
3. **Within 2 s of Tc, wet `<BLE1>`.** Record:
   - Ts = `BLE_LEAK: eleak %s — leak=%d batt=%d%% rssi=%d fw=%s` (`leak=1`);
   - Ta = `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s`. It is followed by `RULES_ENGINE: AUTO-CLOSE: valve not connected — scanning; close deferred to reconnect reconciliation` and two `BLE_VALVE: [CMD] %s write not ready. Queuing val=%u` lines (RMLEAK, then Valve).
   - Also record when `leak_detected` arrives at IoT Hub (up to about 10 s; it waits behind the handler).
4. **About Tc + 10 s:**
   - `RULES_ENGINE: override_enable: valve unreachable after reconnect window`;
   - ack error "The valve isn't responding. Check its power and connection, then try again.";
   - no `water_access_override_enabled`. LED RED.
5. **Keep `<BLE1>` wet.** At T0 + 180-215 s:
   - IoT Hub `device_offline` for the valve (`rating` `critical`, `offline_duration_s` 180-215, the last real `battery`);
   - the snapshot reason includes "Valve offline".
6. *(OBS, finding 2)* About 5 min after the wet, the sensor's telemetry heartbeat:
   - `IOTHUB: Event: BLE Leak %s leak=%d batt=%d` (`leak=1`) again;
   - a second `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s`;
   - a second `auto_close` (`"rmleak_asserted":false`), with no new `leak_detected`.
   - **Count them.**
7. **About 6 min after the wet:** PSU on.
   - `RULES_ENGINE: Valve reconnected with %d active leak(s) — executing auto-close`.
   - The pended RMLEAK goes before the valve command: `BLE_VALVE: [CMD] Applying pending %s command=%d` or `BLE_VALVE: [CMD] Replaying pending %s command=%d`.
   - `BLE_VALVE: [DATA] RMLEAK=%d (%s)` (1) and `BLE_VALVE: [DATA] Valve State=%d (%s)` (0).
   - IoT Hub: `device_recovered`, then `auto_close` with `"cause":"reconnect"`.
   - Count the `BLE_VALVE: [CMD] Writing %s=%u` lines at the relink: the pended pair once (RMLEAK first), plus at most the reconnect's own pair. More than that is a finding.
8. Dry `<BLE1>`; auto-clear; `vo-…` gives `ok`.

**Pass:**
- **Ta − Ts ≤ 200 ms** (more is a Fail on CP6: the test plan's T6-18 note at its line 7028);
- the ack text exact;
- `device_offline` at 180-215 s with `offline_duration_s`;
- RMLEAK before CLOSE at the relink;
- the valve ends closed, then opens on `valve_open`.

**The Ta − Ts check counts only if Ts falls inside the wait:** between Tc and the `RULES_ENGINE: override_enable: valve unreachable after reconnect window` line. With the valve unpowered only the valve-hunt scan hears sensors; if the 4 s edge burst is missed, `leak=1` comes about 15 s later, after the wait, and proves nothing. Then, after step 4, send `{"schema":"eflostop.cmd","ver":1,"id":"cp6-24-s2b","cmd":"override_enable"}` and wet `<BLE2>` 1 s after its `RULES_ENGINE: override_enable: valve not ready — reconnecting (<=%dms)` line. That wet prints its own `AUTO-CLOSE + RMLEAK triggered` line (the 10 s cooldown has passed, and the disconnect cleared the valve's RMLEAK cache), and Ta − Ts is measured on it. Keep the PSU off through that wait too.

**Record:** the number of `auto_close` events while the valve was away (`Known-limit`).

**Day 2 (D2-2):** T3-17 Part A once, with the valve **shielded** (powered and open, out of range: the stale-OPEN case of the B1(b) fix), and T6-18 reps 2-3. With a LoRa sensor, step 6 again with `<LORA1>` wet and the router off: count `auto_close` per packet and any `OFFLINE_BUF: Buffer full, oldest event overwritten`.

**Full procedures:** test plan T6-18 (and its CP6 note), T3-17, T2-08.

#### T5-13, DEC-08, T3-08, T4-13: the LoRa leak path (only if open question 8 is yes)

**If no LoRa sensor with the bench key is at hand,** or the hub hears none (4.6's LoRa check): `N/A (no LoRa HW)`, under the EC-2 waiver. 15k-11 then checks only the LoRa task's boot line and the keys.

**If one is** (WP2c changed `lora_task`, which now reads the bench keys, and the `lora_rx_queue` path; a dropped LoRa packet is S1 in a LoRa home):
- **T5-13 in B2** (15 min, in place of B2's P2 items): test plan T5-13. Each packet prints `APP_LORA: Verified: ID=0x%lX, Batt=%d%%, Leak=0x%X, Sent=%u, Ack=%u` and `IOTHUB: Event: LoRa Packet from 0x%08lX`; the leak closes the valve as 15k-6, within its bounds.
- **LS-1 run 1:** wet `<LORA1>` at T0 + 15 s in place of `<BLE3>`; LS-1's outage pass adds "no `APP_LORA: Rx Queue Full! Packet dropped.`".
- **D2-2:** CP6-24 step 6 with `<LORA1>` wet and the router off (audit finding 2 at the LoRa cadence: one `auto_close` per packet can overflow the 16-entry offline ring).
- **D2-7:** DEC-08 A, T3-08, T4-13.
- **15k-11:** the `s` key too.

### 6.3 Block P: Wi-Fi setup portal (B4, 75 min; S-10 in B1)

**Do not wet a sensor to test protection while the no-credential portal is open:** BLE scanning is paused from the reboot until the SoftAP stops (section 7, item 1). CP6-26 measures that gap on purpose.

#### 15k-1: portal smoke, iPhone (S-10, reset #1) and Android (reset #2) (P1, 15 min per phone)

**Proves:** after a reset each phone can put the hub back on Wi-Fi (the top-priority bug of 2026-09-29), with WP2's tail and admission order, and no reboot.

**Steps:**
1. Hold the reset button 10 s.
2. The phone joins `WiFi-Hub-69C8` from Settings. Note the tap time.
3. The sign-in window should open by itself, and the page should list networks.
4. Choose the network, type its password, tap **Connect once**. Keep the page in front while it shows "Connecting…".
5. Let the phone leave the SoftAP after the success page (tap Done, or let it rejoin its usual network), except on reset #2, where 15k-9 needs the phone to stay 60 s.

**Expect:**
1. Before the reboot:
   - `RESET_BTN: === LONG PRESS CONFIRMED — CLEARING WIFI CREDENTIALS ===`;
   - `RESET_BTN: Wi-Fi credentials erased from NVS`;
   - `RESET_BTN: Rebooting into AP mode...`.
2. After the reboot:
   - `APP_WIFI: portal priority ON (no Wi-Fi credentials) - BLE scanning paused`;
   - `APP_WIFI: portal priority: wifi_manager task prio %u -> %u (httpd, dns_server not raised)`;
   - `BLE_LEAK: Scan paused - Wi-Fi setup portal has the radio`;
   - `BLE_VALVE: [SCAN] Valve scan held - Wi-Fi setup portal has the radio`;
   - `APP_WIFI: Wi-Fi channel at AP start: radio %u (SoftAP configured %u), router not joined since boot%s`;
   - `dns_server: DNS Server listening on 53/udp`;
   - no `dns_server: captive DNS: …` line (neither "failed" nor "not started").

   2a. **While the portal is open** (S-10's checks, kept; the only evidence of the portal hold):
   - no `BLE_LEAK: Extended passive scan started (1M + Coded PHY)`;
   - no `BLE_VALVE: [SCAN] Starting scan for provisioned valve %s...`;
   - no `HEALTH_ENGINE: Roll-up grace expired (%lu s) — %d unheard device(s) now count`;
   - the fleet LED stays WHITE, never RED (no `FLEET_LED: rating=%s color=%s effect=SOLID` with RED).
3. When the phone joins:
   - `APP_WIFI: SoftAP: station <MAC> joined, AID=%u`;
   - `APP_WIFI: SoftAP: station <MAC> got <IP>, %lu ms after joining`;
   - `APP_WIFI: portal client <IP>: first …, %lu ms after joining`, where `…` is in turn `DNS query`, `captive probe (302 sent)`, `page request`, `Connect/Disconnect request`.
4. After Connect:
   - `APP_WIFI: Connected! IP: %s`;
   - `APP_WIFI: portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops (about %d s)`;
   - `APP_WIFI: SoftAP tail after a setup-page Connect (stations on it: %d) - it stops %d s after the IP, or %d s after the last station leaves (not before %d s)` (renders 60, 15, 15);
   - `IOTHUB: cloud admission deferred: SoftAP up - no TLS or DPS until it stops`.
5. When the phone leaves: `APP_WIFI: SoftAP tail: %s - it stops %lu.%lu s after the IP` (`no station left on it`).
6. Then:
   - `APP_WIFI: SoftAP stopped (its servers too) %lu.%lu s after the IP`;
   - `APP_WIFI: portal priority OFF (%s) - BLE scanning resumed` (`AP stopped`): 15 s after the phone left, never before IP + 15 s, at most IP + 60 s.
7. Then:
   - `IOTHUB: cloud admitted %lu.%lu s after the IP (internal DMA free %u B, largest %u B)`;
   - `IOTHUB: Connected to Azure IoT Hub!`;
   - the `lifecycle` in IoT Hub.
8. CP6-25's resume lines (below).

**Pass:**
- `Connected to Azure IoT Hub!` on both phones;
- items 2 and 2a hold (the portal hold);
- 0 reboots other than the reset's;
- no `DPS:` line, no `IOTHUB: WiFi up — restarting MQTT client` and no `Connected to Azure IoT Hub!` before `cloud admitted`.

**Not failures (record them):**
- a sign-in window that does not open by itself;
- a phone that drops before the success page (no channel-switch handling until WP4's C13).

**Record (G0-B):**
- the phone note (3.4);
- the driver's channel-switch lines, from `wifi:` … `start CSA timer` to `wifi:` … `switch to channel` **(ESP-IDF)**: the time between them, and whether a `<connect>csa` line prints at all;
- the first `MONITOR: idma: …` line after `APP_WIFI: Wi-Fi channel at AP start: …` (the AP-start heap dip);
- the time from `SoftAP stopped` to `cloud admitted`.

**Full procedure:** HANDOFF §15k items 1 and 2; G0-B in §15d.

#### 15k-9: the web server does not answer on the home LAN (P1, 10 min, inside reset #2's tail)

**Proves:** the web server runs only while the SoftAP is up and answers only SoftAP clients (WP2's C3).

0. **Before reset #2,** set `$hub` in a PowerShell window on the capture PC, from the hub's last home-router `APP_WIFI: Connected! IP: %s` line (or a DHCP reservation on the home router), and type the block below ready to paste. The window is short: if the Android leaves the no-internet SoftAP after the success page, the SoftAP stops 15 s after it leaves, not 60 s.
1. On reset #2 the Android sets up the **home router**, and stays on `WiFi-Hub-69C8` with the page open and its screen on, so the SoftAP stays up 60 s.
2. **The moment `APP_WIFI: Connected! IP: %s` prints,** paste the block (capture PC, on the home LAN). If the IP differs from `$hub`, fix `$hub` and run it again while the SoftAP is still up.
   ```powershell
   $hub = '<LAN IP>'
   curl.exe -s -o NUL -w "%{http_code}\n" "http://$hub/"
   curl.exe -s -o NUL -w "%{http_code}\n" -X POST "http://$hub/connect.json"
   curl.exe -s -o NUL -w "%{http_code}\n" -X DELETE "http://$hub/connect.json"
   python dns_check.py $hub
   ```
   Expect `403` three times, and `no reply (timed out)` for all six DNS queries (the captive DNS answers only SoftAP clients). In the same minute the phone still loads `http://10.10.0.1/`.
3. After `APP_WIFI: SoftAP stopped (its servers too) %lu.%lu s after the IP`, the three `curl.exe` commands print `000`.

**Pass:**
- 403, then 000;
- the LAN IP gets no DNS answer;
- no `APP_WIFI: WiFi Disconnected. Reason: %d` with reason 8 (the DELETE did not make the hub forget its network);
- no reboot.

**Fail:** a `200` to the DELETE. The hub forgot its Wi-Fi: set it up again.

**Full procedure:** HANDOFF §15k item 9.

#### 15k-2: the 10 s reset ×10 (P1: connected ×4, idle ×1 tomorrow; idle ×2 and connecting ×3 on day 2)

**Proves:** the reset always erases and opens the no-credential portal, from every STA state, with no reboot but its own. This is WP1's gate; it replaces WP-V's ×3.

**Runs:** the ledger in 5.2. The router retry comes 30 s after the previous attempt **ended** (its `APP_WIFI: WiFi Disconnected. Reason: %d` line), plus a 0.5 s radio-hold lead; with the AP off an attempt lasts about 3-6 s, so retries come about every 33-36 s.
- **Idle runs (#6 tomorrow; #7-#8 on day 2):**
  1. Switch the bench AP off.
  2. Wait for `APP_WIFI: SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`.
  3. Start the 10 s hold within 5 s after an `APP_WIFI: WiFi Disconnected. Reason: %d` line. The confirm then lands 10-15 s into the 30 s gap, with no attempt in flight.
  4. Switch the bench AP on before the phone submits it.
- **Connecting runs (#9-#11, day 2):** start the 10 s hold **21-22 s** after the last `WiFi Disconnected. Reason:` line, so that `RESET_BTN: === LONG PRESS CONFIRMED — CLEARING WIFI CREDENTIALS ===` lands about 0.5-2 s after the next `APP_WIFI: router fallback: retrying the configured network (attempt %u)`, inside its attempt. A run whose confirm line is not within 3 s after that line counts as idle: repeat it. To widen the window, change the bench AP's password for these runs and leave the AP on: each attempt then runs until the handshake times out. Set the password back afterwards, and give the phone the bench AP's current password at each set-up.

**Expect in every run:**
- the three `RESET_BTN:` lines of 15k-1;
- after the reboot: `APP_WIFI: portal priority ON (no Wi-Fi credentials) - BLE scanning paused`, the AP-start channel line, and `dns_server: DNS Server listening on 53/udp`;
- a phone can open the page.

**Pass:**
- every run ends in the no-credential portal;
- no `RESET_BTN: Wi-Fi NVS lock busy for 3 s - erasing without it`;
- no `RESET_BTN: Wi-Fi credential erase failed (%s) - rebooting anyway`;
- no reboot but the reset's own.

**Record:** each run's STA state as the log shows it.

**Full procedure:** HANDOFF §15k item 2.

#### 15k-3: the captive DNS (P-13) (P1 when the laptop is ready, else D2-6; 15 min; laptop on the reset #3 portal)

Run it in the 10 s-reset portal, where the laptop gets a lease at once. The laptop is joined to `WiFi-Hub-69C8`:
```bash
dig @10.10.0.1 captive.apple.com A
dig @10.10.0.1 captive.apple.com A +noedns +norecurse
dig @10.10.0.1 captive.apple.com AAAA
dig @10.10.0.1 captive.apple.com HTTPS          # older dig: TYPE65
dig @10.10.0.1 _dns.resolver.arpa SVCB          # older dig: TYPE64
dig @10.10.0.1 $(python3 -c "print('.'.join(['a'*63, 'b'*63, 'c'*63, 'd'*58]))") A
```
Without `dig`, run `python dns_check.py` (Appendix A): the same six queries. Then run `python $env:TEMP\dns_probe.py odd` (datagrams of 1, 11 and 600 B).

**Pass:**
- **A:** `status: NOERROR`, `flags: qr aa rd ra`, `ANSWER: 1`, `captive.apple.com. 60 IN A 10.10.0.1`. With EDNS: `ADDITIONAL: 1`, an OPT with `udp: 512`, no "extra bytes" warning, nothing malformed.
- **`+noedns +norecurse`:** `flags: qr aa ra`, `ADDITIONAL: 0`, the same answer.
- **AAAA, HTTPS, SVCB:** `NOERROR`, `ANSWER: 0` (CP5 answered them with an A record).
- **The 250-character name:** NOERROR with the A answer.
- **`odd`:** `no reply (pass)` three times.
- **No reboot.** The hub logs one `APP_WIFI: portal client <IP>: first …, %lu ms after joining` for the laptop; per-query lines are compiled out.

**Full procedure:** HANDOFF §15k item 3. Its LAN-IP check runs in 15k-9 (`python dns_check.py $hub`), from the capture PC on the home LAN.

#### 15k-4 (c)(d): DNS fuzz and HTTP header fuzz (P1 when the laptop is ready, else D2-6; 10 min, same portal)

**(c) DNS fuzz:** `python $env:TEMP\dns_probe.py` (1,000 datagrams).

Pass:
- no reboot and no stack-overflow line;
- the script's `most in one second` ≤ 20;
- `dig @10.10.0.1 captive.apple.com A` still answered right after;
- `allocfail` not rising.

**(d) HTTP header fuzz** (PowerShell, laptop on the portal):
```powershell
curl.exe -s -o NUL -w "%{http_code}\n" -H "Host: $('a'*100)" http://10.10.0.1/
curl.exe -s -o NUL -w "%{http_code}\n" -H "Host: $('a'*2000)" http://10.10.0.1/
curl.exe -s -o NUL -w "%{http_code}\n" -H "Host:" http://10.10.0.1/
curl.exe -s -o NUL -w "%{http_code}\n" -X POST http://10.10.0.1/connect.json
curl.exe -s -o NUL -w "%{http_code}\n" -X POST -H "X-Custom-ssid: $('s'*33)" -H "X-Custom-pwd: 12345678" http://10.10.0.1/connect.json
curl.exe -s -o NUL -w "%{http_code}\n" -X POST -H "X-Custom-ssid: test" -H "X-Custom-pwd: $('p'*65)" http://10.10.0.1/connect.json
curl.exe -s -o NUL -w "%{http_code}\n" -X POST -H "X-Custom-ssid: test" http://10.10.0.1/connect.json
```
Pass, in order: `302`, `431`, `200`, `400`, `400`, `400`, `400`; no reboot. **Never send a valid SSID and password here:** that starts a connect.

**When (c) and (d) are done:** run `netsh wlan delete profile name="WiFi-Hub-69C8"` on the laptop and switch its Wi-Fi off before the iPhone sets up. G0-A, G8 and 15k-7.1 later need 0 stations on the SoftAP.

**(a) the heap hold:** its phone half is read from 15k-5 and 15k-7.2 on day 1 (6.4, "Record (15k-4(a))"); only the E2-laptop run waits for day 2 (D2-6). **(b) the beacons:** day 2 (D2-6). **Full procedure:** HANDOFF §15k item 4.

#### CP6-25: BLE scanning and the valve hunt resume after every pause (P1, read-only, every portal and outage run)

**Why:** CP6 pauses BLE in four ways: the no-credential portal, the radio holds around each Wi-Fi attempt, the setup page's scan holds on the fallback SoftAP, and the SoftAP tail. A pause that never ends is silent until every sensor goes offline 600 s later.

**Check after each pause** (Claude reads it; you only note the times of the pauses):
1. The end line:
   - after a portal: `APP_WIFI: portal priority OFF (%s) - BLE scanning resumed`, `BLE_LEAK: Scan resumed - Wi-Fi setup portal closed`, `BLE_VALVE: [PORTAL] Valve hunt resumed - Wi-Fi setup portal closed`, `HEALTH_ENGINE: BLE scanning resumed - BLE sensor timeouts restart now (%d s)`;
   - after a radio hold: `APP_WIFI: Wi-Fi radio hold OFF after %u s - BLE scanning resumed`;
   - after the setup page on the fallback SoftAP: `APP_WIFI: Wi-Fi setup page idle, closed or done after %u s (%u of its scans stopped for BLE) - %s`.
2. The next scanner heartbeat has the normal form, `BLE_LEAK: [HEARTBEAT] Scanner alive, whitelist=%d sensors`, not the paused form `BLE_LEAK: [HEARTBEAT] Scanner alive, whitelist=%d sensors, scanning paused for %lu s (%s)`.
3. Every present sensor prints `BLE_LEAK: eleak %s — leak=%d batt=%d%% rssi=%d fw=%s` within 110 s.
4. On a valve hub: `BLE_VALVE: [SCAN] Starting scan for provisioned valve %s...`, then the `SETUP COMPLETE - READY FOR GATT` banner within 60 s.

**Pass:** no `device_offline` in IoT Hub for a present sensor in the 15 min after any pause.

**Fail:**
- a paused heartbeat with no pause in progress;
- a `APP_WIFI: Wi-Fi radio hold ON (%s) - BLE scanning paused` with no `OFF` line within 8 s. Allow longer while an MQTT stop holds `wifi_task` (15k-13).

#### CP6-26: a sensor wet through the no-credential portal, and the gap measured (P1 for the close at the resume; OBS for the gap; inside reset #3)

**Why:** CP6 pauses BLE scanning, with no time cap, while the no-credential portal is open; WP8 fixes it. Until then the only mitigation is that a sensor still wet when the pause ends is heard at once and closes the valve. This run checks that, and records the gap.

1. After reset #3's reboot, before any phone or laptop joins, wet `<BLE1>` (on a wet cloth) and **keep it wet until after `APP_WIFI: portal priority OFF (%s) - BLE scanning resumed`.** 15k-3 and 15k-4 run meanwhile if the laptop is ready.
2. *(OBS)* **Expect and record** while the portal is open:
   - no `BLE_LEAK: eleak %s — leak=%d batt=%d%% rssi=%d fw=%s` line;
   - the paused heartbeat `BLE_LEAK: [HEARTBEAT] Scanner alive, whitelist=%d sensors, scanning paused for %lu s (%s)`;
   - no close; the LED WHITE.
3. *(P1)* After the iPhone's set-up and the resume:
   - `BLE_LEAK: Scan resumed - Wi-Fi setup portal closed`;
   - `<BLE1>`'s `leak=1` within 20 s of that line;
   - `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s`, live or pended (`RULES_ENGINE: AUTO-CLOSE: valve not connected — scanning; close deferred to reconnect reconciliation`: the valve hunt was held too);
   - the valve closed, `BLE_VALVE: [DATA] RMLEAK=%d (%s)` (1) and `BLE_VALVE: [DATA] Valve State=%d (%s)` (0), within 60 s of the resume (after the `SETUP COMPLETE - READY FOR GATT` banner);
   - IoT Hub: `leak_detected`, `auto_close`. LED RED.
4. *(OBS)* Record the time from `portal priority OFF` to `<BLE1>`'s first `eleak` line.
5. Restore: dry `<BLE1>`, wait for the auto-clear, `vo-…`.

**Result:** step 3 is Pass or Fail (a Fail is NO-GO, 2.3). Steps 2 and 4 are `Known-limit` (section 7, item 1): the WP8 baseline, not a CP6 failure.

#### G0-D: wet-sensor burst statistics with the scanner's own scan (P1, B3: 35 min dry + 10 min; step 4 in B5)

**Proves:** the advert loss and advert interval WP8's leak model needs. On 2026-10-01 every burst was `n=1`, with the valve unpowered (valve-hunt scan only). This run has the valve linked. **The WP8 model re-run waits for it.**

**Set-up:**
- valve linked;
- `<BLE3>` (`CB:B6`, 1M) and a Coded sensor among the first 4 sensors heard (only those get `burst:` lines);
- nRF Connect next to `CB:B6`, logging its adverts with timestamps;
- write each wetting and drying time in the notes.

1. Connected, all dry, **35 min** (the B3 contract checks take that long). They run in parallel: they send commands but wet nothing.
   - Expect `BLE_LEAK: eleak <MAC> burst: n=…` lines about every 100 s per sensor.
2. **First** check that the last `RULES_ENGINE: Config updated: auto_close=%s triggers=0x%02X` reads `enabled` and `0x07` (CP6-34 restored the mask) and that the twin's `trigger_mask` is 7. Then wet `<BLE3>` for about 1 min, then dry it; twice.
   - The edge burst (about 4 s), one wet burst every 15 s, then dry bursts.
   - RMLEAK then CLOSE as in 15k-6.
   - `vo-…` between the wettings.
3. *(Inside G0-A, block B5)* one more wetting with the setup page in use (= CP6-27's single wet).
4. *(Block B5, beside P14)* 10 min connected and dry with the valve unpowered: the hunt-scan reception figure WP8's model needs.

**Pass:** none (a baseline). A reboot or an `allocfail` rise is a finding.

**Record:**
- Ta per sensor (the smallest `dT` in its burst lines);
- `n` per burst by state;
- nRF Connect's count for the same bursts;
- each sensor's `phy=`.

**Send:** the nRF Connect export. **Full procedure:** HANDOFF §15d run D.

### 6.4 Block R: router outage and rejoin (B5, 125 min)

"Router off/on" means the bench AP's Wi-Fi off/on (3.3). Run the U items first (PSU off), then the L items. Order: P14 (with G0-D step 4 and G0-A step 1), 15k-7.1 ×3 (U), 15k-7.2 run 1 = G0-A, 15k-5 (U); then PSU on, 15k-7.1 L, 15k-7.2 run 2, 15k-7.3, and reset #6 last.

#### P14 with G0-D step 4: a valve missing at boot (P1, 10 min, start of B5)

**Why:** 2.1.3's never-run bench case P14 (provision_pulse TEST_PLAN): the 600 s roll-up grace is for the advertising sensors; a valve the hub cannot reach at boot must not be excused for 10 minutes.
1. PSU off (the valve unpowered, still provisioned); all sensors dry. Ctrl+T Ctrl+R (the hub resets with the log running). Write the time in the notes.
2. Watch 10 min, touching nothing. **Expect:**
   - the LED WHITE after the boot, then RED at about 180 s (`FLEET_LED: rating=%s color=%s effect=SOLID`), **not** at 600 s;
   - a snapshot `critical` naming the valve (offline, not connected), at about 180 s;
   - the LED never sits WHITE or GREEN for the whole 10 minutes.
3. The same 10 min are G0-D step 4 (connected, dry, the valve unpowered: the hunt-scan reception figures) and G0-A step 1 (connected before the first pull). Nothing to do but leave the sensors alone.

**Pass:** RED and the `critical` snapshot at about 180 s.

**Full procedure:** `docs/provision_pulse/TEST_PLAN.md` P14.

#### 15k-7: G3-lite, the E4 replay (P1; includes G0-A)

**Proves:**
- no TLS beside the SoftAP;
- with 0 stations, the AP is down ≤ 1 s and MQTT up ≤ 5 s after the IP;
- 0 failed allocations;
- the lifecycle after every rejoin;
- the largest heap block recovers (CP5 stayed stuck at 6,400 B after TLS beside the AP).

**7.1, no phone or laptop on `WiFi-Hub-69C8`, ×3 with U, ×1 with L** (6 min each). HANDOFF asks for each item with U, then L; the L state's runs 2-3 are covered by 15k-10(b) runs 2-3 (7.2 run 2 and 7.3), and are not repeated here (9.3).
1. Router off. Expect:
   - `APP_WIFI: WiFi Disconnected. Reason: %d`;
   - `IOTHUB: cloud admission withdrawn (%s)` (`WiFi down`);
   - `IOTHUB: %s — stopping MQTT client (free TLS heap for AP/captive portal)` (W, `WiFi down`);
   - `TELEMETRY_V2: MQTT connected = %s` (`false`) in the same pass;
   - `IOTHUB: MQTT client stopped on wifi_task in %lu.%lu s` (about 1 s; up to 5 s);
   - about 30 s after the loss, `APP_WIFI: SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`.
2. Wait 2 min, then router on. Expect, timed from `APP_WIFI: Connected! IP: %s`:
   - `APP_WIFI: SoftAP tail after an automatic rejoin (no station on it) - it stops %d.%d s after the IP` (0.5);
   - `IOTHUB: cloud admission deferred: SoftAP up - no TLS or DPS until it stops`;
   - the driver's `wifi:mode : sta (<MAC>)` line **(ESP-IDF)**, with nothing after the STA's MAC (no `+ softAP (…)`), at about 0.5-0.7 s: the AP is down;
   - `APP_WIFI: SoftAP stopped (its servers too) %lu.%lu s after the IP` (about 0.6-1.3 s);
   - `IOTHUB: cloud admitted %lu.%lu s after the IP (internal DMA free %u B, largest %u B)` (about 1.5-3 s);
   - `IOTHUB: WiFi up — restarting MQTT client`;
   - `IOTHUB: Connected to Azure IoT Hub!` (about 3-5 s);
   - the replay of held events, then the `lifecycle` and one snapshot in IoT Hub.

**7.2, with a phone, ×2.** Run 1 is **G0-A** (U, iPhone). Run 2 is L (Android) and is 15k-10(b) run 2.
1. Router off. Wait for the fallback SoftAP.
2. The phone joins `WiFi-Hub-69C8` from Settings.
   - G0-A: leave it 5 min untouched (does a lease come? `APP_WIFI: SoftAP: station <MAC> got <IP>, %lu ms after joining`).
3. Use the page 2-3 min: scroll, let the list refresh, no Submit.
   - Expect `APP_WIFI: Wi-Fi setup page in use - its scans pause BLE scanning, BLE listens %d s of every %d s`.
   - **G0-A: wet `<BLE1>` once here** (CP6-27, G0-D step 3). The valve is U, so record only wetting → `leak=1`. Dry it at once, and wait for `RULES_ENGINE: AUTO-CLEAR: all sensors clear for %ds — clearing RMLEAK` before going on.
4. Router on, with the phone still joined, the page untouched. The page stops polling 60 s after the last touch; the router retry waits while it polls, up to 5 min. Expect:
   - `APP_WIFI: router fallback: retrying the configured network (attempt %u)`;
   - `APP_WIFI: Connected! IP: %s`;
   - `APP_WIFI: SoftAP tail after an automatic rejoin (stations on it: %d) - it stops %d s after the IP, or %d s after the last station leaves` (20, 10);
   - `IOTHUB: cloud admission deferred: SoftAP up - no TLS or DPS until it stops`;
   - `APP_WIFI: SoftAP stopped (its servers too) %lu.%lu s after the IP` (≤ 20 s);
   - `IOTHUB: cloud admitted …` 0.5-1.5 s after that line;
   - then MQTT and the `lifecycle`.
5. *G0-A only:* 5 min connected after the rejoin, touching nothing (G0-A step 6).

**Record (15k-4(a), its phone half; in 7.2 and in 15k-5):** every `wifi_manager: AP up without its %s - tried again every %d s` must be followed by `wifi_manager: AP servers running again (HTTP and DNS)` before the run ends, and `wifi_manager: network list: no memory for its %u B - the page lists no network yet` may print at most once per AP start.

**7.3 (= 15k-10(b) run 3, L, 12 min):**
1. Black-hole the WAN about 2 min with Wi-Fi up (both DROP rules, or the WAN cable; 3.3's check already ran at the start of B6).
2. Then switch the router's Wi-Fi off, and wet `<BLE2>` as soon as `IOTHUB: %s — stopping MQTT client (free TLS heap for AP/captive portal)` prints.
3. **While the Wi-Fi is off, remove both DROP rules** (`nft delete rule inet fw4 forward handle <n>`, twice) or re-plug the WAN, and confirm with `nft -a list chain inet fw4 forward`. Otherwise the hub gets its IP back but never the cloud, and the rejoin figures are lost.
4. Record N in `IOTHUB: MQTT client stopped on wifi_task in %lu.%lu s`, any `IOTHUB: MQTT stop waits for cloud_tx's publish`, any `IOTHUB: cloud admission deferred: the last MQTT stop is still under way`, and `leak=1` → CLOSE.

**Pass (every run):**
- `allocfail` unchanged from before the pull to 60 s after `Connected to Azure IoT Hub!`;
- with 0 stations, AP down ≤ 1 s and MQTT ≤ 5 s after the IP;
- the `lifecycle` (`"event":"online"`) after every rejoin;
- nothing cloud between `Connected! IP` and `cloud admitted`;
- no reboot.

**Findings:**
- `IOTHUB: cloud admission deferred: internal DMA free %u B, largest %u B (needs %u / %u)` right after `SoftAP stopped` (except a single 1 s hold after a stop);
- an escape line;
- `IOTHUB: Subscribe refused (%d %d %d, outbox %d B) - reconnecting` repeating for more than 30 s;
- any `TELEMETRY_V2: Outbox full - %s not kept`;
- `idma` minima below 8 KB free or 4.5 KB largest.

**Record:**
- IP → AP down → `SoftAP stopped` → `cloud admitted` → `Connected to Azure IoT Hub!`, per rejoin;
- every `idma:` line from the pull to 60 s after the reconnect;
- G0-A's phone note, the lease time and the three channel lines (`APP_WIFI: Wi-Fi channel at link loss: radio %u, router was on %u%s`, the AP-start line, `APP_WIFI: Wi-Fi channel at IP: radio %u, router %u%s`).

**Full procedure:** HANDOFF §15k item 7; G0-A in §15d.

#### 15k-10: WP2b, the leak task never waits for the MQTT stop (P1)

**(a) At every router pull of 15k-5 and 15k-7, with the cloud connected** (no extra time):
- `IOTHUB: %s — stopping MQTT client (free TLS heap for AP/captive portal)` → `TELEMETRY_V2: MQTT connected = %s` (`false`) **≤ about 50 ms**.
- **Fail:** a gap of 1 s or more, or `MQTT connected = false` printing only after `IOTHUB: MQTT client stopped on wifi_task in %lu.%lu s`.

**(b) A leak at the pull, ×3, valve L:** 15k-7.1's linked run, 15k-7.2 run 2, and 15k-7.3.
1. Switch the router off.
2. Wet a sensor as soon as the stop line prints.
3. Between runs: dry, router back, `vo-…` once the cloud is back.

Expect:
- `RULES_ENGINE: LEAK INCIDENT latched by %s sensor %s` and `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s` within 15k-6's milliseconds;
- RMLEAK, then CLOSE, within 15k-6's spread;
- `TELEMETRY_V2: Offline — buffering %s event` and `OFFLINE_BUF: Stored event [%s] (%u bytes), %d buffered`;
- after the rejoin, `OFFLINE_BUF: Replayed [%s] (%u bytes)`, with both events in IoT Hub (a duplicate is allowed).

**Pass:**
- in every run, `leak=1` → AUTO-CLOSE and `leak=1` → CLOSE stay within 15k-6's figures, wherever `MQTT client stopped on wifi_task` falls;
- in at least one run, `leak=1` and `AUTO-CLOSE + RMLEAK triggered` both print before `MQTT client stopped on wifi_task` (if none does, repeat run 3).

**Fail:** rules lines held until just after the `stopped` line, or a leak-to-CLOSE time that grows with the stop's length.

**Full procedure:** HANDOFF §15k item 10.

#### 15k-5: G8, rejoin with a phone, a pull in the tail, Submit ×10 (P1 with U; L and the E2 laptop on day 2; 25 min)

1. Hub connected. Router off.
   - Expect the 15k-7.1 loss lines, the component's 3 retries, then `APP_WIFI: SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`.
2. A phone joins `WiFi-Hub-69C8` and opens the page.
3. Router on. Expect:
   - `APP_WIFI: router fallback: retrying the configured network (attempt %u)`, then `APP_WIFI: Connected! IP: %s`;
   - the automatic tail with 1 station;
   - `IOTHUB: cloud admission deferred: SoftAP up - no TLS or DPS until it stops`.
4. **Within 10 s of `Connected! IP`, router off again.** The SoftAP stays up. Expect:
   - `APP_WIFI: WiFi Disconnected. Reason: %d`;
   - **no** `IOTHUB: cloud admission withdrawn (%s)` (nothing was admitted) and **no** `SoftAP stopped`;
   - then only `APP_WIFI: router fallback: retrying the configured network (attempt %u)` every 33-36 s, or `APP_WIFI: router fallback: retry deferred - the Wi-Fi setup page is open`;
   - no `Reason:` lines about 10 s apart (one retry owner).
5. Keep the page open 5 min and Submit ×10: the right password, at least 10 s apart. With the router off, each ends "Connection failed".
   - Put at least 5 of them 0-3 s after a `router fallback: retrying …` line. Expect `wifi_manager: ORDER_CONNECT_STA: %s failed (%s) - attempt not started` and `APP_WIFI: WiFi Disconnected. Reason: %d` (205), and the page showing the failure at once.
   - **Do not tap Disconnect in this window** (the forget hazard, section 7).
6. Router on. Expect:
   - `APP_WIFI: Connected! IP: %s`;
   - `APP_WIFI: SoftAP stopped (its servers too) %lu.%lu s after the IP` at most 20 s later;
   - `IOTHUB: cloud admitted …`, `IOTHUB: WiFi up — restarting MQTT client`, and the cloud back.

**Pass:**
- **0 reboots;**
- no automatic attempt less than 30 s after the previous attempt's start or end, Submits included;
- each Submit's page result matches the log.

**Findings:**
- `allocfail` rising in a tail;
- anything cloud before `cloud admitted`;
- a reboot in the tail.

**Record (15k-4(a), its phone half):** as in 15k-7.2.

**Full procedure:** HANDOFF §15k item 5.

#### CP6-27: a leak while the router-fallback setup page is in use (P2; 1 wet in G0-A on day 1, ×3 on day 2)

**Why:** with the page in use, its scan holds keep BLE off up to 8 s of every 12 s. A sensor's 4 s wet edge and its 2.5 s wet bursts every 15 s can then be missed: about 45 s worst case. Nothing else measures this on CP6.

1. Router off; wait for the fallback SoftAP.
2. A phone joins and keeps scrolling the page, so the list refreshes. Expect `APP_WIFI: Wi-Fi setup page in use - its scans pause BLE scanning, BLE listens %d s of every %d s` (4, 12).
3. Wet `<BLE1>` at a random moment; press `d` at the wetting. Dry it and wait for `leak=0`. Three times on day 2.
4. For each wet expect `leak=1`, `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s`, RMLEAK, then CLOSE (valve L).

**Record:** wetting → `leak=1`, and `leak=1` → CLOSE.

**Pass:**
- wetting → `leak=1` ≤ 45 s;
- `leak=1` → CLOSE within 15k-6's spread.

Over 45 s is a finding. Between 20 and 45 s, record it for WP8's model.

### 6.5 Block C: cloud path (B6 and B3)

#### 15k-12 / T6-21: LS-1, a leak closes the valve within 200 ms during a WAN black-hole with Wi-Fi up (P1, 3 × 15 min, B6)

**WP2c's acceptance test.** The case WP2c was built for is a leak that lands while `cloud_tx` is stuck in a stalled write. Runs 2 and 3 aim the first wet there; run 1 keeps HANDOFF's sequence for classifying lost events (R5).

**Set-up:**
- valve L; `<BLE1>` … `<BLE4>` dry; cloud connected ≥ 2 min;
- the bench AP **DROPs** the hub's TCP 8883 **both ways** (3.3), or the WAN cable is pulled, with its Wi-Fi up;
- the method passed 3.3's check (silent) at the start of B6; before each run, `<hub IP>` matches the last `APP_WIFI: Connected! IP: %s` and `nft -a list chain inet fw4 forward` shows both rules;
- note T0, the moment the outage starts.

**Only the first wet of a run prints AUTO-CLOSE.** Once the valve reads closed with RMLEAK=1 (or within 10 s of the AUTO-CLOSE), a later wet in the same incident returns at DEBUG (`rules_engine.c:912`, `:921`), which this image does not print. Nothing can open the valve again during the outage (no C2D gets through), so each run gives exactly **one** AUTO-CLOSE sample: its first wet. Never re-wet a sensor that has not reported `leak=0`: it gives no new `leak=1`. Use another sensor.

**Steps:**
1. **Wets:**
   - **Run 1 (classification; HANDOFF's sequence, every wet on its own sensor):** `<BLE1>` at T0 + 1 s, `<BLE2>` at T0 + 5 s, `<BLE3>` at T0 + 15 s (`<LORA1>` instead, with a LoRa sensor), and `<BLE4>` right after the first `IOTHUB: SNAP trigger=%s` line after T0 + 20 s (one comes every minute). Plus CP6-28's two sends.
   - **Runs 2 and 3 (in-stall):** wet nothing before T0 + 20 s. The run's first wet is `<BLE1>`, within 1 s after the first `IOTHUB: SNAP trigger=%s` line at or after T0 + 20 s. Then `<BLE2>` 5 s later and `<BLE3>` 15 s later. Run 2: press `d` once during the stall (it must answer at once). Run 3: 15k-14(g)(1).
   - Press `d` at each wet as a marker, and write each wet's time in the notes.
2. Hold the outage at least 2 min after the last wet: esp-mqtt sees the session dead within about 10-90 s. Run 1: until T0 + 150 s or later, and at least 60 s after CP6-28's sends.
3. **Expect, for the run's first wet:** `BLE_LEAK: eleak %s — leak=%d batt=%d%% rssi=%d fw=%s` (`leak=1`); `RULES_ENGINE: LEAK INCIDENT latched by %s sensor %s`; `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s`; `BLE_VALVE: [CMD] Writing %s=%u` with `RMLEAK=1`, then `Valve=0`.
   **For each later wet:** only `BLE_LEAK: eleak …` (`leak=1`) and `IOTHUB: Event: BLE Leak %s leak=%d batt=%d`. No AUTO-CLOSE line is expected (above).
4. From `cloud_tx`, when its stalled write times out:
   - `IOTHUB: Disconnected.` and `TELEMETRY_V2: MQTT connected = %s` (`false`);
   - `IOTHUB: Pub %s took %lu.%lu s (msg_id=%d)`;
   - `TELEMETRY_V2: Pub %s failed (msg_id=%d)`;
   - `TELEMETRY_V2: Pub %s not confirmed (msg_id=%d) - kept for replay, a duplicate is possible`;
   - for the items still queued, `TELEMETRY_V2: Offline — buffering %s event` and `OFFLINE_BUF: Stored event [%s] (%u bytes), %d buffered`.
5. Remove both DROP rules (or re-plug the WAN) and confirm. Expect:
   - the reconnect;
   - `TELEMETRY_V2: Draining %d offline event(s) before lifecycle...` and `OFFLINE_BUF: Replayed [%s] (%u bytes)` in order;
   - `TELEMETRY_V2: Pub %s: %s` with `lifecycle`;
   - `IOTHUB: Twin reported (%d): %s`, the live events, a second `Twin reported` after the twin GET;
   - exactly one `boot` or `fast` snapshot.
6. Dry everything, wait for the auto-clear, `vo-…`, wait 2 min. Three runs.

**Pass during the outage:**
- the run's first wet: `leak=1` → `AUTO-CLOSE + RMLEAK triggered` **≤ 200 ms**, and `leak=1` → the `Valve=0` write within the 15k-6 spread;
- each later wet: `leak=1` → `IOTHUB: Event: BLE Leak %s leak=%d batt=%d` **≤ 200 ms** (`iothub_task` evaluates the rules and then prints this line in the same pass);
- no rules line held until a `Pub … took` line;
- no `TELEMETRY_V2: TX queue full (%d) - %s not sent`; with a LoRa sensor, no `APP_LORA: Rx Queue Full! Packet dropped.`.

**In-stall or not (Claude decides after the block):** a first wet counts as in-stall only if a `IOTHUB: Pub %s took %lu.%lu s (msg_id=%d)` line's span (its ESP time minus the time it gives) covers the wet's `leak=1` time. G5 needs at least 2 in-stall first wets among runs 2, 3 and 15k-14(a). If there are fewer, Claude names a repeat run (12 min), which takes 15k-13(a)'s place in B6.

**Pass after the restore (also T5-12's replay order):**
- in IoT Hub, the first copy of each event the UART shows as published or stored arrives in the UART's build order;
- each sensor's last event matches the UART's last;
- byte-identical duplicates are allowed: keep the first;
- a `ts` that differs by about 1 s is a pre-sync entry re-stamped at a replay.

**Classify a missing event before calling it a failure:**
1. First look for `OFFLINE_BUF: Buffer full, oldest event overwritten` (the ring holds 16).
2. An event with a `Pub event:` line before `Disconnected.`, and no `not confirmed` or `Stored event` line for it, is the known PUBACK-window loss **R5**. Run 1's T0 + 1 s and + 5 s wets are likely to be this.
3. In outages under 30 s, the first copies can arrive in swapped order (R6).

**Record per run** (Claude does it):
- the first wet's `leak=1` → AUTO-CLOSE and `leak=1` → the `Valve=0` write, and whether it was in-stall;
- the largest `leak=1` → `Event: BLE Leak` of the later wets;
- the largest `Pub … took`;
- next to any `Valve=0` write over the 15k-6 spread, the `OFFLINE_BUF: Stored event` lines and any `Pub snapshot` line between the AUTO-CLOSE line and the write (R2, R3: 2.2);
- the classified misses.

**Full procedure:** HANDOFF §15k item 12 (its step 1 wording re-wets `<BLE1>` at T0 + 15 s, which gives no new `leak=1`; this plan uses another sensor); test plan T6-21 and T5-12.

#### CP6-28: commands sent during an outage: late delivery and expiry (P2, inside LS-1 run 1)

**Send only after `IOTHUB: Disconnected.` has printed, and at least 90 s after T0** (IoT Hub keeps the session about 1.5 × the 60 s keepalive after the hub's last packet; with the two-way DROP nothing it sends then reaches the hub). Then send, at least 60 s before the restore:
1. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-28-late","cmd":"rules_config","payload":{"auto_close_enabled":true}}'`
   - Expect it to be delivered and run after the reconnect; the hub has no age check.
   - Its ack's `ts` is the reconnect time.
2. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-28-exp","cmd":"valve_close"}' --expiry $(( ($(date +%s) + 30) * 1000 ))`
   - First confirm the flag with `az iot device c2d-message send --help` (milliseconds since the epoch).
   - Expect it **never delivered:** no `IOTHUB: C2D cmd='%s' ver=%d id='%s'` line for `cp6-28-exp`, no ack, the valve does not move. This is V5's 30 s expiry rule for the four valve commands.

**Pass:** the first is acked after the reconnect; the second is never seen.

**Not a hub Fail:** with a one-way rule (only the hub's outbound packets dropped), a C2D can be run mid-outage and redelivered after the reconnect, which gives a duplicate ack. If that happens, record it and repeat with the two-way rules.

#### 15k-14(a): a quiet hub in a black-hole (P1, 5 min, after LS-1)

1. Start the DROP (both rules) with no other traffic.
2. Wet `<BLE1>` at T0 + 45 s: `leak=1` → `AUTO-CLOSE + RMLEAK triggered` ≤ 200 ms, and the `Valve=0` write within the 15k-6 spread.
3. esp-mqtt ends the session by itself within about 30-90 s: `IOTHUB: Disconnected.`.
4. Restore: remove the rules, dry, auto-clear, `vo-…`.

It counts as an in-stall sample for G5 when a `Pub … took` line spans the wet.

**Full procedure:** HANDOFF §15k item 14(a).

#### 15k-13(a): the MQTT stop waits behind `cloud_tx`'s publish (P2, 25 min, the LS-1 set-up)

15 s or more into a DROP stall, do one of each, in three stalls:
- the router's Wi-Fi off;
- the router's power off (if it has a separate switch);
- the 10 s reset (reset #5).

**Remove both DROP rules** (or re-plug the WAN) while the Wi-Fi is off, or while the hub reboots after the reset, before the phone submits. Confirm with `nft -a list chain inet fw4 forward`. The power-off clears them by itself, but then check the hub's IP at the re-join (the static lease, 3.3).

Expect:
1. `IOTHUB: MQTT stop waits for cloud_tx's publish`;
2. `IOTHUB: Pub %s took %lu.%lu s (msg_id=%d)`;
3. `IOTHUB: MQTT client stopped on wifi_task in %lu.%lu s`, with no other `Pub` line between the stop's request and that line;
4. the SoftAP on time, and the portal usable.

**Anything that disturbs the portal is a finding for the top-priority portal bug.** The reset reboots about 2 s after the press is confirmed: its pass is no abort or heap-corruption backtrace before `RESET_BTN: Rebooting into AP mode...`, and a usable portal after the reboot.

**Full procedure:** HANDOFF §15k item 13.

#### 15k-14(g)(1): TW-1, a desired-property change during an outage (P1, about 1 min inside LS-1 run 3)

G9 (WP2e) depends on it: this is the outage case where TW-1 first failed.
1. During the DROP, **only after `IOTHUB: Disconnected.` has printed and at least 90 s after T0** (so the patch cannot reach the hub mid-outage; 3.3), change the desired name: `desired '{"hub_name":"CP6 TW1"}'`. Remove both DROP rules at the run's restore (LS-1 step 5).
2. Expect:
   - `TELEMETRY_V2: Draining %d offline event(s) before lifecycle...`, the `Replayed` lines, the lifecycle;
   - `IOTHUB: Twin reported (%d): %s` (it may still carry the old name);
   - `IOTHUB: Twin GET requested (rid=%d)`;
   - `IOTHUB: Twin GET: applying desired properties from full document`;
   - `IOTHUB: Twin: hub_name = '%s'`;
   - a second `Twin reported` with the new name.
3. Then `twin tw1`.

**Pass:** the last `Twin reported` **and** Azure's reported twin show `"CP6 TW1"`. Put the original name back afterwards (desired `hub_name`, or `""` if none).

**Not TW-1:** an older value in Azure after a reconnect on a slow link (W1, the QoS 1 resend). Record `$lastUpdated`.

**Full procedure:** HANDOFF §15k item 14(g).

#### The cloud-contract checks (B3, while G0-D's dry phase runs)

These check what the app and backend teams rely on. Every command below parses, and none contains a legacy keyword. **Wet nothing during them** (G0-D is measuring).

##### CP6-29: the cmd_ack envelope rules (P1, 5 min)

Each is a `rules_config` that repeats the current setting, so nothing changes.

| # | Send | Expected |
|---|---|---|
| 1 | `{"schema":"eflostop.cmd","ver":1,"id":"","cmd":"rules_config","payload":{"auto_close_enabled":true}}` | `C2D_CMD: Envelope cmd='%s' ver=%d id='%s' payload=%s` with `id=''`; the ack (the full `eflostop.v2` envelope, test plan 0.8) has a `data` of `{"event":"cmd_ack","cmd":"rules_config","status":"ok"}`, with **no** `id` key |
| 2 | `{"schema":"eflostop.cmd","ver":1,"id":12345,"cmd":"rules_config","payload":{"auto_close_enabled":true}}` | a non-string id is dropped: ack without `id` |
| 3 | `{"schema":"eflostop.cmd.v1","id":"cp6-29-s3","cmd":"rules_config","payload":{"auto_close_enabled":true}}` | the older envelope is acked: `ver=1` in the `C2D_CMD` line, ack `ok` with `id` `cp6-29-s3` |
| 4 | `{"schema":"eflostop.cmd","ver":2,"id":"cp6-29-s4","cmd":"rules_config","payload":{"auto_close_enabled":true}}` | `ver` is not checked: ack `ok` |
| 5 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-29-s5","cmd":"Set_Hub_Name","payload":{"name":"CC Case"}}` | `cmd` is case-sensitive: `IOTHUB: Unknown command: %s`; ack error `"code":"Set_Hub_Name","detail":"unknown command"`; the name is unchanged |
| 6 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-29-s6","cmd":"rules_config","payload":{"auto_close_enabled":true},"sent_ts":1770000000,"extra":{"a":1}}` | unknown envelope keys are ignored: ack `ok` |

After each `ok`:
- `RULES_ENGINE: Config updated: auto_close=%s triggers=0x%02X`;
- an `IOTHUB: Twin reported (%d): %s`;
- `IOTHUB: SNAP trigger=event:%s`;
- an `event` snapshot. Several `ok` within 5 s give one snapshot.

After step 5: no snapshot trigger and no twin report.

**Pass:** each ack as listed.

##### CP6-30: every `error.detail`, verbatim (P1, 10 min)

Send these about 5 s apart:

```json
{"schema":"eflostop.cmd","ver":1,"id":"cp6-30-s1","cmd":"valve_set_state"}
{"schema":"eflostop.cmd","ver":1,"id":"cp6-30-s2","cmd":"valve_set_state","payload":{"state":1}}
{"schema":"eflostop.cmd","ver":1,"id":"cp6-30-s3","cmd":"valve_set_state","payload":{"state":"OPEN"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp6-30-s4","cmd":"valve_set_state","payload":{"state":"close"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp6-30-s5","cmd":"rules_config"}
{"schema":"eflostop.cmd","ver":1,"id":"cp6-30-s6","cmd":"sensor_meta","payload":{"sensor_id":"00:80:E1:2A:29:FC","label":"Sink"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp6-30-s7","cmd":"sensor_meta","payload":{"sensor_type":"valve","sensor_id":"00:80:E1:27:F7:BB","label":"Sink"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp6-30-s8","cmd":"provision","payload":{}}
{"schema":"eflostop.cmd","ver":1,"id":"cp6-30-s9","cmd":"provision","payload":{"valve_id":"not-a-mac"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp6-30-s10","cmd":"set_hub_name","payload":{}}
{"schema":"eflostop.cmd","ver":1,"id":"cp6-30-s11","cmd":"set_hub_name","payload":{"name":"CC-01234567890123456789012345678"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp6-30-s12","cmd":"set_hub_name","payload":{"name":"\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9"}}
{"schema":"eflostop.cmd","ver":1,"id":"cp6-30-s13","cmd":"set_hub_name","payload":{"name":"\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9"}}
```
Then put the name back with `name-…`.

| # | UART (besides the `IOTHUB: C2D cmd='%s' ver=%d id='%s'` line) | Ack `error.detail` (exact) |
|---|---|---|
| 1, 2 | — | `missing 'state' field (expected "open" or "closed")` |
| 3, 4 | — | `invalid state value (expected "open" or "closed")`. Upper case and `close` are refused. |
| 5 | `IOTHUB: Command: RULES_CONFIG` | `rules config update failed` |
| 6 | `IOTHUB: Command: SENSOR_META`, `SENSOR_META: Missing sensor_type` | `sensor metadata update failed` |
| 7 | `SENSOR_META: Unknown sensor_type: %s` | `sensor metadata update failed` |
| 8 | `IOTHUB: Provisioning JSON detected`, `PROVISIONING: No valid provisioning data in JSON` | `provisioning failed` |
| 9 | `PROVISIONING: provision: 'valve_id' is not a valid MAC string`, `PROVISIONING: No usable valve identifier in provision payload` | `provisioning failed`. The stored valve and sensors are unchanged. |
| 10 | `IOTHUB: Command: SET_HUB_NAME` | `missing 'name' field` |
| 11 | (32 ASCII characters) | `name too long (max 31 chars)` |
| 12 | (16 × "é" = 32 bytes) | `name too long (max 31 chars)`: the limit is 31 **bytes** |
| 13 | (15 × "é" = 30 bytes) `IOTHUB: Hub name set to: '%s'` | `ok`. The ack, the next snapshot's `gateway.name` and the twin's `hub_name` all show the name, in UTF-8. |

**Where the other texts are provoked:**
- "No valve is set up for this hub.": S-8 and CP6-38.
- The RMLEAK refusal: T5-05 (B2), CP6-21 (P2).
- "Valve battery critical (≤10 %): the valve will not open. Replace the batteries.": S-7 and T2-05.
- "A leak is still active. …": T5-05, CP6-20.
- "No active leak to override. …": T5-06, CP6-20.
- "Water detected at the valve. …": CP6-23a (and CP6-23b).
- "The valve isn't responding. …": CP6-24.
- The size refusal: CP6-31.
- The command-queue text: T6-17 (day 2+).
- Not provokable: the decommission and override mutex failures, and out of memory.

**Pass:**
- each `detail` byte-exact, with `error.code` equal to `cmd`;
- after each error, no `SNAP trigger=event:` line and no `Twin reported` for it;
- step 13 `ok`.

A first `set_hub_name` within 5 s of a snapshot build may print `TELEMETRY_V2: Snapshot not built - out of memory` once: `Known-limit` if the 5 s retry publishes it.

##### CP6-31: messages that get no ack, and the 8,192 B C2D limit (P1, 5 min)

| # | Send | Expected |
|---|---|---|
| 1 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-31-s1"}` (no `cmd`) | `C2D_CMD: Envelope missing 'cmd' field`, then the legacy parser takes it as a provision: `IOTHUB: C2D cmd='%s' ver=%d id='%s'` (renders `cmd='provision' ver=0 id=''`: the one planned `ver=0` of the day, 3.5), `IOTHUB: Provisioning JSON detected`, `PROVISIONING: No valid provisioning data in JSON`. **No ack** (no envelope, no id). Nothing changes. |
| 2 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-31-s2","cmd":""}` | `C2D_CMD: Envelope missing 'cmd' field`, `C2D_CMD: C2D has 'cmd' but unrecognized schema — ignoring` (the empty `cmd` is still a string; the schema is not mistyped), `IOTHUB: Unrecognized C2D payload`. **No ack.** |
| 3 | the 6 KB file below | `IOTHUB: Inbound message spans fragments: %d of %d bytes — reassembling`, `IOTHUB: Received C2D Message! (%d bytes)`; ack `ok` (id `cp6-31-s3`) |
| 4 | the 9 KB file below | `IOTHUB: Inbound %s message DROPPED: %s (%d bytes, limit %d)`; ack error `exceeds the reassembly limit: <N> bytes exceeds the 8192 byte limit` (id `cp6-31-s4`, code `rules_config`). Not executed. |

Make the two files in Git Bash, then send them:
```bash
python -c "import json;print(json.dumps({'schema':'eflostop.cmd','ver':1,'id':'cp6-31-s3','cmd':'rules_config','payload':{'auto_close_enabled':True},'pad':'x'*6000},separators=(',',':')))" > cp6_31_6k.json
python -c "import json;print(json.dumps({'schema':'eflostop.cmd','ver':1,'id':'cp6-31-s4','cmd':'rules_config','payload':{'auto_close_enabled':True},'pad':'x'*9000},separators=(',',':')))" > cp6_31_9k.json
c2d @cp6_31_6k.json ; sleep 5 ; c2d @cp6_31_9k.json
```
If `az` does not expand `@file` (an error, or the text `@cp6_31_6k.json` arriving as the message), paste the file's content into VS Code's "Send C2D Message to Device" box instead.

**Pass:** as listed; no reboot; the next command works.

##### CP6-32: the ack before the twin, on IoT Hub's own clock (P1, 10 min)

**Why:** the UART cannot show the wire order (`Twin reported` can print before the ack's `Pub event` line). IoT Hub's arrival time and the twin's `$lastUpdated` can.

1. `twin cp6-32-0`.
2. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-32-s2","cmd":"set_hub_name","payload":{"name":"CC Order 1"}}'`; wait 15 s; `twin cp6-32-2`.
   - Expect `IOTHUB: Command: SET_HUB_NAME`, `IOTHUB: Hub name set to: '%s'`, a `Twin reported` with `"hub_name":"CC Order 1"`, and `IOTHUB: SNAP trigger=event:%s`.
3. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-32-s3","cmd":"rules_config","payload":{"trigger_lora":false}}'`; wait 15 s; `twin cp6-32-3`.
   - Expect `triggers=0x05`, and the twin's `trigger_mask` 5.
4. Send these two back to back:
   - `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-32-s4","cmd":"rules_config","payload":{"trigger_lora":true}}'`
   - `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-32-s5","cmd":"set_hub_name","payload":{"name":"CC Order 2"}}'`
   - Wait 15 s; `twin cp6-32-4`. Expect `trigger_mask` 7 and `hub_name` "CC Order 2".
5. Put the name back with `name-…`.

**Pass:**
- for each changed property, `$lastUpdated` ≥ the ack's `iothub-enqueuedtime`;
- the ack arrives before the `event` snapshot;
- the last twin is current.

**Record** the delay from the ack to `$lastUpdated` (normally under 1 s; up to about 7 s is in contract).

##### CP6-33: the twin's desired `hub_name` and `snapshot_interval_s` (P1 a-f, OBS g; 10 min)

Desired properties get no cmd_ack: the reported echo is the only answer. After each `desired '…'`, wait 10 s, then `twin cp6-33-<letter>`.

| # | `desired '<json>'` | Expected UART | Expected reported |
|---|---|---|---|
| a | `{"hub_name":"CC Twin"}` | `IOTHUB: Twin desired patch: %s`, `IOTHUB: Twin: hub_name = '%s'`, `HUB_IDENT: Hub name set: %s`, a `Twin reported` | `"CC Twin"`; the next message's `gateway.name` too |
| b | `{"hub_name":"CC-01234567890123456789012345678"}` (32 bytes) | `IOTHUB: Twin: hub_name too long (%d chars, max %d)` | unchanged: desired differs from reported |
| c | `{"hub_name":"\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9"}` (32 bytes) | the same "too long" line: bytes are counted | unchanged |
| d | `{"hub_name":null}` | the patch line only; no `Twin: hub_name` line | unchanged: **null does not clear the name** |
| e | `{"hub_name":""}` | `IOTHUB: Twin: hub_name = '%s'` (empty), `HUB_IDENT: Hub name cleared` | `""`; no `gateway.name` in the next messages |
| f1 | `{"snapshot_interval_s":3601}` | `TELEMETRY_V2: Snapshot interval %ds rejected — outside [%d..%d], keeping %ds`, `IOTHUB: Twin: snapshot_interval_s %d rejected — reported will show %d` | 60 |
| f2 | `{"snapshot_interval_s":90.7}` | `IOTHUB: Twin: snapshot_interval_s = %d` (90), `TELEMETRY_V2: Snapshot interval set to %ds (persisted)` | **90 while desired is 90.7**: the app must write integers |
| f3 | `{"snapshot_interval_s":"120"}` | the patch line only | unchanged (90): a string is ignored |
| f4 | `{"snapshot_interval_s":60}` | `TELEMETRY_V2: Snapshot interval set to %ds (persisted)` | 60 (the day's setting) |
| g (OBS) | `{"hub_name":"CC Twin"}`, then `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-33-g","cmd":"set_hub_name","payload":{"name":"CC C2D"}}'`; look at the next reconnect of the day (B5) | after the reconnect: `IOTHUB: Twin GET: applying desired properties from full document`, `IOTHUB: Twin: hub_name = '%s'` (`CC Twin`) | the name goes back to the desired one: `set_hub_name` and the desired `hub_name` both write it, and the desired one wins at every connect |

After (g) has been seen (in B5), put the desired `hub_name` back to the bench's value (`""` if none).

**Pass (a-f):** each reported value as listed, one `Twin reported` per patch.

##### CP6-34: mask writes, their echo, type traps and range (P1, 5 min)

| # | Send | Expected |
|---|---|---|
| 1 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-34-s1","cmd":"rules_config","payload":{"trigger_mask":0,"trigger_ble_leak":true}}` | `triggers=0x01`: the boolean is applied after the mask and wins for its bit |
| 2 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-34-s2","cmd":"rules_config","payload":{"auto_close_enabled":"false"}}` | ack **`ok`**, rules unchanged (`true` / 1): a string is ignored with no error |
| 3 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-34-s3","cmd":"rules_config","payload":{"trigger_mask":"7"}}` | ack `ok`, mask still 1 |
| 4 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-34-s4","cmd":"rules_config","payload":"7"}` | ack `ok`, unchanged: a payload that is not an object acks `ok` |
| 5 (OBS) | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-34-s5a","cmd":"rules_config","payload":{"trigger_mask":255}}`, then the same with id `cp6-34-s5b` and `256`, then id `cp6-34-s5c` and `-1` | `triggers=0xFF`, then **`0x00`**, then `0xFF`: no range check. 256 silently disarms every source while `auto_close_enabled` stays `true` (audit finding, small). |
| 6 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-34-s6","cmd":"rules_config","payload":{"auto_close_enabled":true,"trigger_mask":7}}` | `triggers=0x07`. **Do not skip this restore.** |

Each line above is the full message to send with `c2d '…'`; for step 5, send the three messages separately. **Send step 5's three messages last in B3's contract checks, and step 6 straight after them:** between them the hub is partly or fully disarmed.

**Before any wet (G0-D step 2):** the last `RULES_ENGINE: Config updated: auto_close=%s triggers=0x%02X` reads `enabled` and `0x07`, and the twin's `trigger_mask` is 7. If not (an `az` error), send `rc-def-…` and check again.

**Pass:** 1-4 and 6 as listed; the twin's last `trigger_mask` is 7. Step 5: `Known-limit`.

**For the backend:** read `data.rules` or the twin back after every `rules_config`; `ok` does not mean the field was understood.

#### 15k-14 (c)(d)(g)(2): normal-link regressions (P3; day 2, D2-3). (g)(6) runs in B2 (6.2).

- **(c)** Provision 4 sensors (test plan T5-01):
  - the pulse every 30 s and on each packet;
  - each cmd_ack before its snapshot;
  - `IOTHUB: SNAP trigger=%s` and `IOTHUB: SNAP heartbeat=reset interval_ms=%lld` keep pairing;
  - never `IOTHUB: SNAP result outstanding for %lu s - cloud_tx busy`.
- **(d)** A twin interval change (T5-11): `IOTHUB: SNAP heartbeat=re-aimed …`, or the new interval at the next result.
- **(g)(2)** Re-send the 4-sensor provision, then a `set_hub_name` right after its ack. The last `Twin reported` and Azure show the new name.
- **(g)(6)**: on day 1, in B2 (6.2). Its second run, with the valve's flood probe wet, is for day 2.

**Full procedure:** HANDOFF §15k item 14.

#### CP6-35: the day-wide contract analysis (P1, Claude, end of day)

Claude runs, on every IoT Hub capture of the day:
- `python docs\telemetry\validate_capture.py <capture>` (VAL-15 step 1, T5-14);
- its contract checker, with the UART and `sent_*.tsv`.

The checks:
- every duplicate byte-identical (a `ts` ±1 s pair only for a pre-sync re-stamp);
- every UART `Pub event` found at IoT Hub, or classified (R5, ring overflow);
- at most one `boot`/`fast` snapshot per connect;
- every ack matched to its command, with byte-exact texts;
- `data.rules` and `gateway.name` timelines;
- the largest snapshot size;
- **T5-10, the RMLEAK write and read-back audit** (no bench time): every `BLE_VALVE: [CMD] Writing %s=%u` of RMLEAK in B2's captures (15k-6, T5-04, T5-05, T5-06, T3-15, P11, CP6-20 … CP6-24) followed by its `BLE_VALVE: [DATA] RMLEAK=%d (%s)` read-back with the same value;
- **15k-14(e):** each `TELEMETRY_V2: Outbox full - %s kept for replay` replayed, in order;
- **15k-14(f):** after `TELEMETRY_V2: Pub %s not sent - the device set or session changed during its line; built again`, that snapshot is not counted; after `IOTHUB: SNAP published while the device set changed - the reconciled one follows now`, the next snapshot shows the change;
- **15k-14(g)(4):** `IOTHUB: Twin report %u not sent - report %u, built after it, went first` only where that session's last `Twin reported` is current;
- the twin-order items of 3.7.

**Pass:** validator 0 FAIL after classification; the checker shows no FAIL; every RMLEAK write read back.

### 6.6 Block D: device-set changes (P0 DEC subset)

**Tomorrow:** S-6 (DEC-03 short form) and S-9 (DEC-11 short form) in the smoke. **Day 2, first thing (D2-3):** the P0 DEC tests that WP2c, WP2d or WP2e touched hardest. Full procedures are in the test plan, section 1. CP6 changes:

| ID | Pri | Time | Start | What to watch on CP6 |
|---|---|---|---|---|
| DEC-06 | P3 (day 2, first) | 15 min | `SS-V4` labels; wet towel | WP2d: removing a sensor while it is wet; a dry report behind a kept wet one. **D1 at steps 3 and 8** (never benched). |
| DEC-07 | P3 | 10 min | DEC-06's end: `<BLE4>` unprovisioned and wet, ≥ 20 s after its removal | A fresh `leak_detected` and a re-close. The wet steps have never run. |
| DEC-09 | P3 | 10 min + re-provision | `SS-V4` labels; VB within 2 m | `"valve":{}`; no `valve_unlinked`; no rescan for 240 s; the twin's `valve_id` `null`; a kept valve report is dropped on removal. |
| DEC-11 (in full) | P3 (P0 in the test plan) | 10 min | DEC-10's end (`<BLE1>` and `<BLE4>` only); without DEC-10, provision just those two first | WP2c: the empty-shape snapshot and the heartbeat re-armed at the result; WP2e: the twin's `provisioned` false. The smoke ran only its short form. It sets up DEC-12. |
| DEC-12 | P3 | 20 min | DEC-11's end (empty hub); router control | Exactly one `boot` snapshot per connect, through WP2's admission and the lifecycle republish. Two `boot`/`fast` on one connect is a 3.7 item. |
| DEC-13 | P3 (P0 in the test plan) | 20 min | DEC-12's end | Re-provisions the empty hub (with `SS-V4`'s devices): it restores the bench. Then the 3.6 check. |
| DEC-14 | P3 | 10 min | pulse window open | `TELEMETRY_V2: Pub %s not sent - the device set or session changed during its line; built again` (that snapshot does not count) and `IOTHUB: SNAP published while the device set changed - the reconciled one follows now` (the next snapshot must show the change). |
| DEC-15 | P3 | 10 min | `SS-V4` labels; the bench AP's WAN pulled (Wi-Fi up) | A decommission while MQTT is offline: WP2/2b/2c's offline path, and the cmd_ack's try-store. Rated H on CP6. |
| DEC-16 | P3 | 25 min | `SS-V4` labels; command pairs ready to send within 1 s | No busy-lock line (3.7); the cmd_ack before the twin on the wire. |
| DEC-18 | P3 (spare hub, or your OK) | 20 min + DPS | `SS-V4` labels | The clear goes through `cloud_tx`: `OFFLINE_BUF: Buffer cleared`, then `IOTHUB: Decommissioned — restarting in 3s...`; never `IOTHUB: decommission: cloud_tx did not finish in %d s - offline buffer erased here` on a normal link. **HANDOFF 15k allows `decommission` `all` only on the spare hub:** decide (open question 3). |
| DEC-03 (in full) | later | — | — | S-6 already runs its BUG-2 core (steps 1, 2, 3 and 5, step 4 cut to 60 s); the full form waits for the release campaign. |
| DEC-01, 02, 04, 05, 10, 17, 19 | later | — | — | Not this checkpoint; the release campaign. |

### 6.7 Block V: valve battery (S-7 in B1; B7, P2, 30 min, only if B5 ends by 17:45, else D2-7)

**S-7 = T2-01** (P1, in the smoke; it must pass, G2): critical at ≤ 10 % → "Valve battery critical", RED, and the `valve_open` refusal "Valve battery critical (≤10 %): the valve will not open. Replace the batteries."; recovery to GREEN.

#### T2-05: `valve_open` refused at ≤ 10 %, linked and disconnected, and the refusal order (P2, 15 min)

**Full procedure:** test plan T2-05. No CP6 change. Use the power-cycle fast path: PSU to the valve's Critical voltage, PSU off/on, wait for `BLE_VALVE: [DATA] Battery=%s` ≤ 10.

#### CP6-36: `override_enable` at a critical valve battery (P2 + OBS, audit finding 4; 10 min, at the end of T2-05)

**Mechanism:** `override_enable` checks provisioning, reachability, a leak and the flood probe, but not the battery, which `valve_open` checks. Valve FW 2.2.0 refuses to open at ≤ 10 %.

1. With the valve at Critical (it has closed itself), wet `<BLE1>`:
   - `RULES_ENGINE: LEAK INCIDENT latched by %s sensor %s`;
   - `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s`;
   - `BLE_VALVE: [DATA] RMLEAK=%d (%s)` (1).
2. `vo-…`: refused with the RMLEAK text (RMLEAK comes before the battery in the refusal order).
3. `c2d '{"schema":"eflostop.cmd","ver":1,"id":"cp6-36-s3","cmd":"override_enable"}'`. **Expect, per the code:**
   - ack `ok`;
   - `RULES_ENGINE: OVERRIDE WINDOW STARTED: auto-close blocked for 24h (expiry=%ld)`;
   - `RULES_ENGINE: override_enable: 24h override started remotely — RMLEAK cleared, valve opening`;
   - `BLE_VALVE: [CMD] Writing %s=%u` (`RMLEAK=0`, then `Valve=1`);
   - the valve stays shut;
   - IoT Hub: `water_access_override_enabled`, and snapshots with `override_active:true` and the valve `closed`.
4. Clean-up: `oc-…` with `<BLE1>` still wet (`RULES_ENGINE: Override cancelled with %d active leak(s) — executing auto-close`); dry; auto-clear; PSU to 6.00 V; power-cycle the valve; `vo-…`.

**Result:** `Known-limit` if as above. The valve fails safe (closed).

**For the app:** an `ok` override with the valve still closed and a critical battery means "replace the batteries", not "water on".

#### T2-04, T2-08 (P3, day 2)

T2-08's `offline_duration_s` is also read in CP6-24.

### 6.8 Block A: this week's audit findings (V5 app requirements)

All six are in 2.1.3 too: they are not CP6 regressions. Each is measured and reported for your decision.

| # | Finding (code at `545b8f2`) | Test | When |
|---|---|---|---|
| 1 | A full hub's snapshot can be large. `MQTT_TX_MAX_MESSAGE` (8,192, `app_iothub.c:2251`) is **not** a send cap. It only sizes esp-mqtt's outbox limit, 12,288 B (`:2253`). A snapshot is never cut: it goes out whole, or is refused (−2) and retried 5 s later. (Bounded on the branch after CP6: `bef24b3`, `8971f3b`.) | CP6-37 | day 2 (D2-5, spare hub) |
| 2 | `auto_close` repeats while the valve is unreachable (10 s cooldown, then every wet report: a BLE sensor's 5 min heartbeat, or each LoRa packet) | CP6-24 step 6 (and its LoRa variant on day 2) | B2 |
| 3 | No mask check when the hub closes at a valve relink, at `override_cancel` and at override expiry | CP6-21, CP6-22 (P2); the expiry with T5-09's 300 s image | B2 if time, else D2-2; day 3 |
| 4 | `override_enable` has no valve-battery check | CP6-36 | B7 if time, else D2-7 |
| 5 | The valve closes itself on its own flood probe whatever the hub's mask | CP6-23b | D2-2 (B2 if time) |
| 6 | A `rules_config` whose rules read times out (1 s) merges onto the defaults | CP6-40 | note only |

#### CP6-37: the largest snapshot: size, refusal, never cut (OBS, audit finding 1; 40 min; spare hub, day 2)

**Estimates** for 16 BLE plus 16 LoRa fake sensors, never heard, no valve:
- 31-byte ASCII labels: about 7.7-7.8 KB;
- labels of 31 `"` characters: about 8.7-9 KB;
- labels of 31 control characters: about 12.6-12.8 KB, over the outbox limit, so never accepted.

With a valve and every sensor heard, ASCII labels give about 8 KB.

1. On the spare hub (`DEV=<GW2>`), set the interval: `DEV=<GW2> desired '{"snapshot_interval_s":60}'`.
2. Generate two provisions with Appendix A's `cp6_37_gen.py`. Each is under 8,192 B and replaces one array.
3. Run `python cp6_37_gen.py ascii <REAL>` (`<REAL>`: one real BLE sensor's MAC, within range of the spare hub), then `DEV=<GW2> c2d @cp6_37_ble.json` and `DEV=<GW2> c2d @cp6_37_lora.json`. Expect:
   - `PROVISIONING: Provisioning completed successfully!`;
   - `IOTHUB: Provision: applied %d inline sensor_meta entry(ies)` (16 each);
   - acks `ok`.
   - Let 5 heartbeats pass.
   - **Wet `<REAL>` once:** `leak_detected` at the spare hub's IoT Hub within 20 s (the leak handling still works on a full hub; the spare hub has no valve, so no `auto_close`). If `<REAL>` is also provisioned on the main hub, the main hub closes its valve too: run this while the main hub is idle (or its soak runs, and note it), then `vo-…` on the main hub.

   3a. **A full hub's boot.** Power-cycle the spare hub (unplug it, or Ctrl+T Ctrl+R in its monitor). Expect `IOTHUB: cloud admitted %lu.%lu s after the IP (internal DMA free %u B, largest %u B)` about 0.N s after the IP. `IOTHUB: cloud admission deferred: internal DMA free %u B, largest %u B (needs %u / %u)` on that boot is a finding (risk map AP-1 step 6: the heap gate would add 60-180 s to every boot of a full hub).
4. The same with `quote`, then with `ctrl`. 5 heartbeats each.
5. For each mode, record:
   - the byte length of the last `TELEMETRY_V2: Pub %s: %s` snapshot line's JSON;
   - the counts of `TELEMETRY_V2: Pub %s failed (msg_id=%d)`, `IOTHUB: SNAP heartbeat=suppressed (publish-failed)` and `TELEMETRY_V2: Snapshot not built - out of memory`;
   - the heartbeats at IoT Hub and their size;
   - the lowest `idma` `min_largest`.
6. *Backlog variant (after `ascii`):* black-hole the WAN 3 min, so events buffer; restore. A `-2` refusal of the snapshot followed within about 10 s by a delivered snapshot is a pass.
7. Clean up with `decommission` `all` on the spare hub (this is also 15k-14(b)).

**Expected:**
- ascii and quote: snapshots arrive, and the IoT Hub copy has the same size as the UART line (never cut);
- ctrl: no snapshot reaches IoT Hub; every 5 s a `Pub snapshot` line, then a −2 refusal; no reboot.

**Result:** `Known-limit`. `Fail` if a snapshot arrives cut or as invalid JSON, or the hub reboots.

**For the backend:** accept snapshots up to about 12 KB; reject control characters in labels.

**If CP6-37 ran on the main hub (only with your OK), restore it before any further test:**
1. After the hub's reboot and DPS re-registration (the `decommission` `all` clean-up wipes the devices, the sensor metadata, the hub name, the DPS cache and the rules state, `app_iothub.c` at its `DECOMMISSION_ALL` branch), `c2d @…` the `ss-v4m-…` line (it re-sends the labels).
2. Put the hub name back (`name-…`, or the desired `hub_name`).
3. `desired '{"snapshot_interval_s":60}'`.
4. The 3.6 check, then one T5-03 run (wet → close → auto-clear → `vo-…`).

#### CP6-38: the empty hub and the no-valve refusals (P3, 10 min; spare hub while empty, day 2)

**Shapes to check:**
- **Snapshot:** `"system_health":{"rating":"excellent","reason":"No devices provisioned"}`, `"valve":{}`, `"lora_sensors":[]`, `"ble_leak_sensors":[]`, `"rules":{"auto_close_enabled":true,"trigger_mask":7}`, `"override_active":false`.
- **Lifecycle:** `"provisioned":false`, and no `valve_id` key.
- **Twin:** `"provisioned":false`, `"valve_id":null`, both counts 0.

**Never send a `provision` here:** even a rules-only one marks the hub provisioned.

| # | Send (`DEV=<GW2> c2d '…'`) | Expected |
|---|---|---|
| 1 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-38-s1","cmd":"valve_close"}` | `IOTHUB: VALVE_CLOSE refused — %s`; "No valve is set up for this hub." |
| 2 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-38-s2","cmd":"valve_set_state","payload":{"state":"open"}}` | `IOTHUB: VALVE_SET_STATE open refused — %s`; the same text |
| 3 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-38-s3","cmd":"override_enable"}` | `RULES_ENGINE: override_enable: no valve provisioned`; the same text |
| 4 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-38-s4","cmd":"decommission","payload":{"target":"valve"}}` | `PROVISIONING: No valve provisioned - nothing to remove`; `valve decommission failed` |
| 5 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-38-s5","cmd":"decommission","payload":{"target":"ble_leak_sensor"}}` | `ble sensor decommission failed` |
| 6 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-38-s6","cmd":"leak_reset"}` | `ok`, no rules event, an empty-shape snapshot |
| 7 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-38-s7","cmd":"override_cancel"}` | `RULES_ENGINE: override_cancel: no active override window`; `ok` |

**Pass:** the shapes and acks as listed; the LED stays WHITE.

#### CP6-39: sensor metadata and LoRa ids as the hub stores them (P3 + OBS, 20 min; spare hub, before CP6-37)

| # | Send (`DEV=<GW2> c2d '…'`) | Expected |
|---|---|---|
| 1 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-39-s1","cmd":"sensor_meta","payload":{"sensor_type":"ble_leak_sensor","sensor_id":"02:00:00:00:00:AA","location_code":"kitchen","label":"CC orphan"}}` | `SENSOR_META: Set metadata: type=%d id=%s loc=%s label="%s"`; `ok` although the sensor is not provisioned |
| 2 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-39-s2","cmd":"sensor_meta","payload":{"sensor_type":"ble_leak_sensor","sensor_id":"02:00:00:00:00:AA","location_code":"Kichen"}}` | `SENSOR_META: unrecognized location_code '%s' — keeping existing`; `ok`; the code stays `kitchen` (`C2D_COMMANDS.md` §4.8 says `unknown`) |
| 3 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-39-s3","cmd":"provision","payload":{"lora_sensors":["0xabcd","0xZZ"]}}` | `PROVISIONING: LoRa Sensor[%d]: 0x%08lX` twice: `0x0000ABCD` and **`0x00000000`**. The backend must validate ids itself. |
| 4 (OBS) | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-39-s4","cmd":"provision","payload":{"ble_leak_sensors":["02:00:00:00:00:AB"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"02:00:00:00:00:AB","location_code":"kitchen","label":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\u00e9"}]}}` | 32 bytes cut at 31: the stored label ends in half a UTF-8 character. **Record** what IoT Hub and the monitor show. |
| 5 | `{"schema":"eflostop.cmd","ver":1,"id":"cp6-39-s5","cmd":"provision","payload":{"ble_leak_sensors":["02:00:00:00:00:AA","02:00:00:00:00:AB"],"lora_sensors":[]}}`, then `{"schema":"eflostop.cmd","ver":1,"id":"cp6-39-s6","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"02:00:00:00:00:AA"}}`, then `{"schema":"eflostop.cmd","ver":1,"id":"cp6-39-s7","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"02:00:00:00:00:AB"}}` | `SENSOR_META: Removed metadata for %s (type=%d), %d entries remain` after each removal. A `provision` that drops a sensor from its list keeps that sensor's label. |

**For the app:**
- cut labels at 31 bytes on a UTF-8 character boundary;
- remove a sensor with `decommission`, not by leaving it out of a `provision`, or its label stays in the hub's 32-entry table.

#### CP6-40: a `rules_config` whose rules read times out (note only)

If `provisioning_get_rules_config()` cannot take its lock within 1 s, `rules_engine_handle_config_command()` merges the command onto `auto_close_enabled` true and mask 7. Nothing on the CP6 image holds that lock for 1 s on another task while a `rules_config` runs, so it cannot be provoked. Record `N/A (not provokable on CP6)`. Claude greps every capture for `PROVISIONING: Failed to take mutex in get_rules_config` (3.7): it must never print.

### 6.9 Block U: upgrade 2.1.3 → CP6 and rollback (P3, day 2, 1.5 h)

**Needs:** the 2.1.3 image built in its worktree (N7; test plan 0.4).
- Flash with `app-flash` only, and **never `erase-flash`**.
- CP6 from `..\hub_cp6`: `idf.py -p COM30 app-flash`. 2.1.3 from `..\hub_2_1_3`: the same command.
- NVS namespaces, keys and the partition table are the same in 2.1.3 and CP6.

#### T6-03 run A: upgrade with devices, metadata, hub name, interval, opt-out and an override (P3, 30 min)

**Full procedure:** test plan T6-03.

**CP6 additions to its boot checks:**
- `IOTHUB: cloud_tx started (stack %u B, priority %u)` before `APP_WIFI: Connected! IP: %s`;
- then `IOTHUB: cloud admitted %lu.%lu s after the IP (internal DMA free %u B, largest %u B)`;
- `PROVISIONING: Loaded existing config from NVS` with the same counts as on 2.1.3;
- `DPS: Loaded cached assignment from NVS` (no registration).

Run B (a latched incident) and VAL-02 run C come with the release campaign.

#### CP6-41: CP6 reads 2.1.3's Wi-Fi credentials and drains 2.1.3's offline events (P3, 15 min)

**Why:** WP1 rewrote the credential load. A credential not read after the upgrade sends a field hub into the no-credential portal, where BLE leak detection is paused: the worst combination.

1. On 2.1.3, connected: pull the bench AP's WAN (Wi-Fi up). Wait for `TELEMETRY_V2: MQTT connected = %s` (`false`). Wet and dry `<BLE2>`, so 2.1.3 buffers about 4 events.
2. Flash CP6 (`app-flash`), with the WAN still out.
3. On CP6's boot expect:
   - `OFFLINE_BUF: Init: %d buffered event(s) pending from before reboot` (about 4);
   - **no** `APP_WIFI: portal priority ON (no Wi-Fi credentials) - BLE scanning paused`;
   - `APP_WIFI: Connected! IP: %s`, then `IOTHUB: cloud admitted …`.
4. Plug the WAN in. Expect:
   - `TELEMETRY_V2: Draining %d offline event(s) before lifecycle...`;
   - `OFFLINE_BUF: Replayed [%s] (%u bytes)` for each;
   - `TELEMETRY_V2: Offline drain complete: %d event(s) replayed`;
   - then CP6's lifecycle.
   - Then `vo-…` if the valve is closed.

**Pass:**
- CP6 joins the router with no portal;
- IoT Hub gets the events with `gateway.fw` `"2.1.3"`, in order, before CP6's lifecycle;
- the validator finds no malformed JSON.

#### T6-04: rollback CP6 → 2.1.3 (P3, 25 min)

**Full procedure:** test plan T6-04. On CP6, buffered events print `TELEMETRY_V2: Offline — buffering %s event` and `OFFLINE_BUF: Stored event [%s] (%u bytes), %d buffered` from `cloud_tx`. 2.1.3 must read them: no `OFFLINE_BUF: Read '%s' failed: %s, skipping`.

#### CP6-42: 2.1.3 after a CP6 10 s reset (P3, 15 min)

**Why:** CP6's reset overwrites the stored SSID and password with zeros instead of erasing the keys. 2.1.3 should read an empty SSID as "no credentials" and open its portal. This checks it on hardware.

1. On CP6, hold the reset 10 s. Expect:
   - `RESET_BTN: === LONG PRESS CONFIRMED — CLEARING WIFI CREDENTIALS ===`;
   - `RESET_BTN: Wi-Fi credentials erased from NVS`;
   - `RESET_BTN: Rebooting into AP mode...`.
2. Before setting Wi-Fi up, flash 2.1.3 (`app-flash`).
3. Join its AP from a phone and set Wi-Fi up.
4. Flash CP6 back.

**Pass:**
- 2.1.3 opens its portal (not a connect loop to an empty SSID), accepts the credentials and connects;
- CP6 then boots connected, with `IOTHUB: Hub is PROVISIONED`.

### 6.10 Block H: heap and fault checks

| ID | What | Pri | When |
|---|---|---|---|
| 4.7 | Heap and internal DMA at rest; `allocfail` 0 all day | P1 | B0 and lunch |
| 15k-7 | `idma` minima at every rejoin, against G3's floor of 8 KB free and 4.5 KB largest | P1 | B5 |
| **15k-16 / T6-11** | Stack high-water marks on a bench-only `2.1.4-hwm` image at `545b8f2`, with `cloud_tx`, `wifi_task` and `lora_task`, comprehensive heap poisoning and the `outbox at CONNECTED` line. Gates: every task ≥ 512 B free, `iothub_task` ≥ 2 KB, **`cloud_tx` ≥ 1 KB** (below that, raise it to 6,144 B before release). Includes 15k-13(b) and a full-hub provision (CP6-37's sizes). | P3 | day 3, first image |
| 15k-15 | The fault image `2.1.4-fault`: a 15 s publish hold plus 20 KB of ballast; health alerts held from 8 queued items (SAFE-1), then delivered; a deferred twin not lost; leak ≤ 200 ms | P3 | day 3 |
| 15k-17 | The hold image `2.1.4-hold`: a leak during a 4 s rules or provisioning hold is still acted on | P3 | day 3 |
| CP6-43 | SAS renewal on a short-SAS image (below) | P3 | day 2-3, if the soak missed the renewal |
| T5-09 | Override expiry on a 300 s image, with CP6-22's mask variant (audit finding 3, expiry path) | P3 | day 3 |
| T6-07, T6-08, T6-09, VAL-13 | Heap like-for-like against 2.1.3 | release | after WP9: WP3 and WP9 change the figures |

**Full procedures:** HANDOFF §15k items 15-17; test plan T6-11, T5-09. Each image is built in its own worktree at `545b8f2`, with `sdkconfig` copied in first, its unmodified size checked against 4.5's B10/B11, and **never shipped**. Flash CP6 back after each.

#### CP6-43: SAS renewal on a short-SAS bench image (P3; 1.5 h unattended, 20 min of reading)

**Why:** WP2b changed the renewal path: `iothub_task` asks `wifi_task` to stop MQTT, then re-keys on a later pass. A failure takes a field hub offline once a day.

**Build:**
1. A worktree at `545b8f2` (`..\hub_sas`), `sdkconfig` copied in first, `PROJECT_VER "2.1.4-sas"`.
2. In `main/iothub/app_iothub.c`, change `#define SAS_TTL_SEC (24 * 3600)` to `(5400)`, and `#define SAS_RENEW_MARGIN_SEC (6 * 3600)` to `(3600)`. The first renewal then falls 30 min after the first token, and every 30 min after that.

**Steps:**
1. Flash it; connected; valve linked. Note `IOTHUB: SAS: clock valid (ts=%ld) — minting first token, starting MQTT`.
2. Leave it 1.5 h (3 renewals).
3. Wet `<BLE1>` right after the second renewal's `renewing` line.
4. Switch the router's Wi-Fi off for 60 s around the third renewal.

**Expect at each renewal, in order:**
- `IOTHUB: SAS: within %d h of expiry — renewing` (`1 h`);
- `TELEMETRY_V2: MQTT connected = %s` (`false`);
- `IOTHUB: MQTT client stopped on wifi_task in %lu.%lu s`;
- `IOTHUB: SAS: token renewed (valid %d h, expires ts=%ld)`;
- `IOTHUB: Connected to Azure IoT Hub!`.

**Pass:**
- three renewals, no reboot;
- no `IOTHUB: SAS: esp_mqtt_set_config failed (%s) — client destroyed, rebooting`;
- no `IOTHUB: SAS: mint failed (out of memory)`;
- the wet during a renewal closes within 15k-6's spread.

### 6.11 Block X: other high risks

#### CP6-44: real-world Wi-Fi credentials through the portal (P3, 30 min, day 2)

**Why:** WP1 bounded the portal's input: SSID 33 B, password 65 B, JSON escaping of SSIDs. Real routers use long passphrases and punctuation.

Use the Android hotspot as the network: its SSID and password are easy to change, and the bench AP keeps its settings. For each case: a reset, then set up from the iPhone. At the end, a last reset puts the hub back on the bench AP.
1. SSID of 32 characters, password of 63 characters: printable ASCII including space, `"`, `\`, `'`, `%`, `&`, `+`.
2. An SSID with a space and an apostrophe (`Tom's Phone`), and an 8-character password.
3. A 64-character hex key, if the AP allows it.
4. *(OBS)* A non-ASCII SSID (`Café`): WP4 addresses it.

**Expect:**
- `APP_WIFI: portal client <IP>: first …, %lu ms after joining` (`Connect/Disconnect request`);
- `APP_WIFI: Connected! IP: %s`;
- the setup-page tail line;
- the cloud.

**Pass:**
- cases 1-3 connect and reach the cloud, with no reboot;
- neither password appears in the UART file. Check this yourself with `Select-String -SimpleMatch`, without writing the password into any file.

#### Other existing tests rated high by the risk map, scheduled later

| ID | Why high | When |
|---|---|---|
| 15k-8 (a)(b) = G0-C | First commissioning with DPS; the router pulled mid-DPS gives `DPS: DPS registration aborted after %lu s: Wi-Fi lost or SoftAP up - tried again once the cloud is admitted again` and no back-off. Spare hub only (`erase-flash` is allowed there only). | day 2 (D2-5) |
| T4-14 | Log hygiene: no Wi-Fi password or passkey on UART (a release gate under EC-10). **At the end of day 1 (5 min):** grep every day-1 UART file yourself, in PowerShell, with `Select-String -SimpleMatch` for the bench AP's and the home router's passwords (resets #1-#6 saved them through the portal), typed at the prompt and never written to a file; report only the hit count. The bench image prints the driver's `wifi:` lines with the SSID: never run it through the production tool. | the grep: end of day 1; the fresh-passkey pairing part: D2-7 |
| T6-15 | A sensor flapping every 15-20 s for 15 min (the 10 s auto-clear's write rate; WP2d) | day 2 (D2-7) |
| T3-06 | Valve swap with the old valve flooded: `RULES_ENGINE: Valve replaced: old valve leak source %s, %u source(s) still wet, incident %s`, and the new valve not closed at its first link | day 2 (D2-7) |
| T3-05, T3-19 | Re-provision a different valve MAC while linked (S22, P0-a), and a bonded valve's reconnect (P0-a identity): the "never links to its own valve" risk | day 2 (D2-7) |
| T4-05, T4-06 | NTP blocked (the SNTP fallback); DPS blocked (in-loop back-off; the same block as 15k-8(b)) | day 2: T4-05 in D2-7 (bench AP firewall), T4-06 in D2-5 (spare hub) |
| T6-14 | A full offline ring (16), then a Wi-Fi reset and a power cycle: WP2's refusals enter the ring, WP2c stores from `cloud_tx`. Rated H on CP6. | day 2 (D2-2) |
| T6-02 | A power cut during decommission-all: WP2c hands the clear to `cloud_tx`. Rated H on CP6; destructive. | day 2 (D2-5, spare hub), or the release campaign |
| T4-02 … T4-04, T4-07, T4-09, T4-11, T4-12 | Pre-sync behaviour and restarts during leaks (S-2/S-3 cover T4-02/03's core) | later |
| T6-12 | Router off/on ×5 and a ≥ 19 h soak, sensors-only (second hub) | release |

---

## 7. Known limitations and expected warnings: record them, do not fail on them

1. **No BLE leak detection while the no-credential portal is open** (after a 10 s reset, before Wi-Fi is set up).
   - BLE scanning is paused from the reboot until the SoftAP stops, with no time cap.
   - LoRa, a linked valve and the hunt for a pended close keep working.
   - The paused heartbeat `BLE_LEAK: [HEARTBEAT] Scanner alive, whitelist=%d sensors, scanning paused for %lu s (%s)` is a pass there.
   - WP8 fixes it. This is why no checkpoint before WP8 is shipped.
2. **Slow or no phone lease on the router-fallback SoftAP** while BLE keeps scanning (E1: none in 500 s; E4: 95 s). G0-A measures it; WP8 fixes it. Run laptop items in the 10 s-reset portal instead.
3. **The setup page's known issues** (WP4 replaces them):
   - a Connect that meets a running attempt fails gracefully (`wifi_manager: ORDER_CONNECT_STA: %s failed (%s) - attempt not started` and `APP_WIFI: WiFi Disconnected. Reason: %d` with 205);
   - the list can re-order: check the network name on the password view;
   - an open network gives HTTP 400 and an endless spinner;
   - **the forget hazard:** do not tap Disconnect, then Connect, within one router retry;
   - a Forget in an automatic tail erases the credentials and opens the uncapped pause.
4. **A sign-in window that does not open by itself, or a phone that drops at the channel switch.** There is no channel-switch handling until WP4's C13.
5. **R5:** an event esp-mqtt accepted into a silently dying session is lost when its PUBACK never comes. Classified in LS-1.
6. **At-least-once delivery:** byte-identical duplicates are normal. The cloud dedupes on an identical payload. `TELEMETRY_V2: Pub %s not confirmed (msg_id=%d) - kept for replay, a duplicate is possible` is the UART's mark.
7. **W1:** a QoS 1 resend can put an older twin report after a newer one on a slow or reconnecting link.
8. **CONC-1 (accepted, D2):** a live DPS registration (only without a DPS cache) still blocks `iothub_task` up to 60 s per attempt.
9. **A first `set_hub_name` within 5 s of a snapshot build** fails that one snapshot with `TELEMETRY_V2: Snapshot not built - out of memory`; the 5 s retry publishes it.
10. **A second STOP_AP with no second `SoftAP stopped` line** is expected. One 1 s admission hold right after a stop is expected (the idle task freeing the ended esp-mqtt task's stack).
11. **The six audit findings** (6.8): `Known-limit` when they behave as written.
12. **2.1.4 known limitations already in the CHANGELOG** (test plan Appendix A): F-02 (a restart during a live leak), F-03 (a leak latched while the valve was away, then a restart), a hanging valve connect, the legacy keyword scan.
13. **The bench image prints the Wi-Fi driver's `wifi:` lines** and its warning `APP_WIFI: bench build (APP_BENCH_DIAG): Wi-Fi driver log at INFO - not for release`. Longer UART output also stretches log-bound times: they are CP6 bench figures, not release figures.
14. **Accepted by decision, not findings:** `.bss` +6,301 B (`cloud_tx`, the I10 exception) and +477 B (WP2d); the 12 KB outbox limit; the 500 ms settle after an AP stop.
15. **Only the first wet of an incident prints AUTO-CLOSE.** A later wet, while the valve reads closed with RMLEAK=1 or within the 10 s cooldown, is skipped at DEBUG and prints nothing from the rules engine (`rules_engine.c:912`, `:921`). Its `leak=1` and `IOTHUB: Event: BLE Leak %s leak=%d batt=%d` lines still print. While the valve is unreachable the cooldown ends after 10 s and each later wet report gives another AUTO-CLOSE (audit finding 2).

---

## 8. Results sheet

Fill one row per test as you go. "Log file" is the block's UART file name (the IoT Hub file has the same stem). Claude fills in the verdicts it reads from the logs.

| ID | Pri | Block | Result | Time | Log file | Notes (figures, counts, deviations) |
|---|---|---|---|---|---|---|
| VAL-01 / 15j build (B1-B14) | P1 | B0 | | | `build_cp6.log` | `.bss`, `.data`, DIRAM, ELF SHA256 |
| 15k-11 boot and keys | P1 | B0 | | | | `cloud admitted` s, idma free/largest; LoRa check (open question 8) |
| 4.7 heap at rest | P1 | B0, lunch | | | | `heap free`, `idma free`, `largest` |
| S-1 VAL-01 | P1 | B1 | | | | |
| S-2 T4-02 | P1 | B1 | | | | |
| S-3 T4-03 | P1 | B1 | | | | rejoin s after the AP returned |
| S-4 T5-04 run 1 | P1 | B1 | | | | |
| S-5 T3-11 | P1 | B1 | | | | |
| S-6 DEC-03 | P1 | B1 | | | | |
| S-7 T2-01 | P1 (must pass, G2) | B1 | | | | Critical V |
| S-8 T3-01 | P1 | B1 | | | | |
| S-9 DEC-11 | P1 | B1 | | | | |
| S-10 = 15k-1 iPhone (reset #1) | P1 | B1 | | | | phone note; `<BLE3>` dry before the re-provision |
| S-11 VAL-15 step 1 | P1 | B1 | | | | |
| 15k-6 / T5-03 ×3 (+D1) | P1 | B2 | | | | leak→AUTO-CLOSE ms, leak→`Valve=0` write ms ×3; the 15k-6 spread |
| 15k-14(g)(6) (W2) | P1 | B2 | | | | leak→`Valve=0` write ms |
| T5-04 runs 2-3 | P1 | B2 | | | | |
| T5-05 interlock (a)(b)(c)(e) | P1 | B2 | | | | the 4 refusals; reconnect line in (b) |
| T5-06 override, block, cancel | P1 | B2 | | | | cancel ack → `Valve State=0` s |
| T3-15 steps 1-4 (genuine press) | P1 | B2 | | | | press time; press → window s |
| P11 / T3-16 (leak across a boot) | P1 | B2 | | | | live or pended; `SETUP COMPLETE` → `Valve State=0` s |
| CP6-20 steps 1-4, 6-7 | P1 | B2 | | | | |
| CP6-20 step 8 | P2 | B2 if time | | | | |
| CP6-21 relink close, masked | P2/OBS | B2 if time, else D2-2 | | | | closed? |
| CP6-22 cancel close, masked | P2/OBS | B2 if time, else D2-2 | | | | closed? |
| CP6-23a the valve's probe as a source | P1 | B2 | | | | self-closed before the hub's write? |
| CP6-24 / T6-18 valve unreachable | P1/OBS | B2 | | | | Ta−Ts ms (inside the wait?); `auto_close` count |
| T5-13 (LoRa leak) | P1 if open question 8 is yes | B2 | | | | N/A if no LoRa sensor (EC-2 waiver) |
| G0-D steps 1-2 | P1 | B3 | | | | Ta per sensor, n per burst, nRF count |
| CP6-29 ack envelope | P1 | B3 | | | | |
| CP6-30 error texts | P1 | B3 | | | | |
| CP6-31 no-ack and size | P1 | B3 | | | | N of the 9 KB message |
| CP6-32 ack before twin | P1 | B3 | | | | ack→`$lastUpdated` delays |
| CP6-33 twin desired a-f | P1 | B3 | | | | |
| CP6-33 g | OBS | B3/B5 | | | | |
| CP6-34 mask echo and traps (+ mask 7 before G0-D's wets) | P1/OBS | B3 | | | | |
| 15k-1 Android (reset #2) + G0-B | P1 | B4 | | | | phone note, CSA ms, AP-start dip |
| 15k-9 LAN 403/000, no DNS answer | P1 | B4 | | | | |
| CP6-26 wet through the portal | P1 (close) / OBS (gap) | B4 | | | | resume → `leak=1` s; resume → `Valve State=0` s |
| 15k-3 P-13 DNS | P1 if the laptop is ready | B4 | | | | |
| 15k-4 (c) DNS fuzz | P1 if the laptop is ready | B4 | | | | most in one second |
| 15k-4 (d) header fuzz | P1 if the laptop is ready | B4 | | | | codes; laptop profile deleted |
| 15k-2 resets #1-#5 connected | P1 (#5 P2) | B1, B4, B6 | | | | |
| CP6-25 resume after every pause | P1 | B1, B4, B5, B6 | | | | |
| 3.3 black-hole check | P1 | B6 | | | | silent / fast; IP and both rules checked |
| 15k-12 LS-1 run 1 (+CP6-28) | P1 | B6 | | | | first-wet AUTO-CLOSE ms; later wets → `Event` ms; misses |
| 15k-12 LS-1 run 2 (+`d`) | P1 | B6 | | | | in-stall? first-wet ms |
| 15k-12 LS-1 run 3 (+14(g)(1)) | P1 | B6 | | | | in-stall? first-wet ms |
| T5-12 replay order (from LS-1) | P1 | B6 | | | | |
| CP6-28 late and expired C2D | P2 | B6 | | | | sent after `Disconnected.`, ≥ T0 + 90 s? |
| 15k-14(g)(1) TW-1 | P1 | B6 | | | | last `Twin reported` and Azure's value |
| 15k-14(a) quiet hub | P1 | B6 | | | | in-stall? ms |
| 15k-13(a) + reset #5 | P2 | B6 | | | | waits→stopped s; rules removed before the rejoin |
| P14 + G0-D step 4 + G0-A step 1 | P1 | B5 | | | | RED at s; `critical` snapshot at s |
| 15k-7.1 ×3 (U) | P1 | B5 | | | | IP→AP down→stopped→admitted→MQTT |
| 15k-7.2 run 1 = G0-A (U), + step 6 | P1 | B5 | | | | lease s, channel R = C?; 15k-4(a) phone half |
| CP6-27 one wet (in G0-A) | P2 | B5 | | | | wetting→leak=1 s |
| 15k-5 G8 (U) | P1 | B5 | | | | reboots, Submit results; 15k-4(a) phone half |
| 15k-7.1 L = 15k-10(b) run 1 | P1 | B5 | | | | |
| 15k-7.2 run 2 = 15k-10(b) run 2 | P2 | B5 | | | | |
| 15k-7.3 = 15k-10(b) run 3 | P1 | B5 | | | | N s; rules removed while the Wi-Fi was off |
| 15k-10(a) at every pull | P1 | B5 | | | | stop → `connected = false` ms |
| 15k-2 reset #6 idle | P1 | B5 | | | | confirm s after `Disconnected` |
| T4-14 grep (day-1 UART) | P1 | end of day | | | all | hit count only |
| T2-05 | P2 | B7, else D2-7 | | | | |
| CP6-36 override at Critical | P2/OBS | B7, else D2-7 | | | | ack, valve state |
| Overnight soak | P2 | B8 | | | | reboots, heap trend, SAS renewal; last boot time |
| CP6-35 / VAL-15 / T5-14 / T5-10 analysis | P1 | Claude | | | all | FAILs, duplicates, RMLEAK read-backs |
| 15k-2 resets #7-#8 idle, #9-#11 connecting | P2→day 2 | D2-2 | | | | |
| CP6-23b (finding 5), T3-17 Part A, T6-18 reps 2-3, T5-07, T3-15 step 5, T6-14, CP6-27 ×3, T5-04 runs 4-5 | P3 | D2-2 | | | | |
| DEC-06, 07, 09, 11, 12, 13, 14, 15, 16 | P3 | D2-3 | | | | |
| 15k-14 (c)(d)(g)(2), WP2d normal path | P3 | D2-3 | | | | |
| T6-03 run A, CP6-41, T6-04, CP6-42 | P3 | D2-4 | | | | |
| 15k-8 (a)(b) = G0-C, CP6-38, CP6-39, CP6-37 (+ step 3a, real-sensor wet), 15k-14(b), T4-06, T6-02, DEC-18 | P3 | D2-5 | | | | snapshot bytes; full-hub boot admission s |
| CP6-44, 15k-5 L + E2, 15k-4 (a) laptop run, (b) | P3 | D2-6 | | | | |
| T2-04, T2-08, T6-15, T3-06, T3-05, T3-19, T4-05, T4-14 pairing part | P3 | D2-7 | | | | |
| DEC-08 A, T3-08, T4-13 (LoRa) | P3 if open question 8 is yes | D2-7 | | | | N/A if no LoRa sensor |
| 15k-16, 15k-15, 15k-17, CP6-43, T5-09 | P3 | D3 | | | | `cloud_tx` stack free |
| CP6-40 | note | — | N/A (CP6) | | | |

---

## 9. Traceability

Every "day 2" below names a D2-n row of 5.5.

### 9.1 Every change area from 2.1.3 (`ae4d59a`) to CP6 → its tests

| Area (2.1.3 → CP6) | What changed | Tests tomorrow | Later |
|---|---|---|---|
| Rules engine | P0-a/b/c; BUG-2 rules purge; E-05; E-06 refusal; 10 s auto-clear; F-01, F-08; override on uptime; WP2d kept reports; `Valve replaced` purge | S-2, S-4, S-5, S-8, S-9, 15k-6, T5-04, T5-05, T5-06, T3-15, P11, CP6-20, CP6-23a, CP6-24 (CP6-21, CP6-22 if time), 15k-10(b), 15k-12 | DEC-06/07/16 (D2-3), T3-06 (D2-7), T5-07 (D2-2), T3-13/14, T4-07/09 (later), T6-15 (D2-7), 15k-17 (D3) |
| Health engine, fleet LED | BUG-1 bands, `null` battery; reconciled table; reachability events; portal holds; alerts held | S-6, S-7, S-9, P11, P14, CP6-24 (offline, recovered), 15k-1 (the portal hold) and CP6-25 (the resume) | T2-02 … 08 (D2-7, later), 15k-15 (D3) |
| Telemetry, `cloud_tx`, offline buffer | WP2c sender task; at-least-once; async snapshots; pre-sync hold; outbox limit; WP2e twin order | 15k-11, S-2/S-3, 15k-12 (+T5-12), 15k-14(a), 15k-10(b), CP6-29 … CP6-35, 15k-14(g)(1), 15k-14(g)(6) | 15k-14(c)(d)(g)(2) (D2-3), DEC-14, DEC-15 (D2-3), T6-14 (D2-2), CP6-37 (D2-5), 15k-15, 15k-16 (D3) |
| IoT Hub task: admission, MQTT stop, SAS, C2D, DPS | WP2 admission; WP2b stop on `wifi_task`; SAS through the stop; DPS abort hook | 15k-1, 15k-5, 15k-7, 15k-10(a), 15k-13(a), CP6-28, overnight soak (SAS) | 15k-8, T4-06 (D2-5), CP6-43 (D3), T6-12 (release) |
| Provisioning, sensor metadata | transactional provisioning, clamped counts, one locked rules read | S-6, S-9, CP6-30 (refusals), CP6-34, 15k-14(g)(6) | DEC subset (D2-3), CP6-38/39 (D2-5), T6-02 (D2-5), T6-01 (release), T6-03 (D2-4) |
| BLE valve | MAC-only identity; retried writes; RMLEAK-first replay; GATT waits; hunt held in the portal | 15k-6, S-5, S-8, P11, CP6-23a, CP6-24 (relink order), T5-05(b), CP6-25 | T3-17 Part A (D2-2), T3-05/06/19 (D2-7), T3-07, T6-16 (later) |
| BLE leak scanner | BLE before Wi-Fi; pauses (portal, radio holds, page); burst statistics | S-2, 15k-1, CP6-25, CP6-26, G0-D, CP6-27 (1 run) | CP6-27 ×3 (D2-2) |
| LoRa | bench keys on `lora_task`; the `lora_rx_queue` path | 15k-11 (keys); with a LoRa sensor, T5-13 and LS-1 run 1 | CP6-24's LoRa variant (D2-2), DEC-08, T3-08, T4-13 (D2-7), if open question 8 is yes |
| Wi-Fi portal and reset button (WP-V, WP1, WP2's C3 and AP tail, router retry, page round) | vendored component; no reboot path; new DNS; one retry owner; web server only with the SoftAP; tail policy; reset always erases | S-3, S-10 = 15k-1, 15k-1 Android, 15k-2 (#1-#6), 15k-9, 15k-5, 15k-7, 15k-3/4(c)(d) (with the laptop), 15k-13(a) | 15k-2 #7-#11 (D2-2), 15k-4 (a)(b) (D2-6), CP6-44 (D2-6), CP6-42 (D2-4) |
| Boot, monitoring, memory | `cloud_tx` statics (+6.8 KB `.bss`); `min_ever`; alloc-fail hook | 4.5, 15k-11, 4.7, 15k-7 idma | 15k-16 (D3), T6-07/08 (release) |
| NVS and upgrade | no key, namespace or partition change | — | T6-03, CP6-41, T6-04, CP6-42 (D2-4) |
| D1 fix (`4e6fe71`) | a valve RMLEAK report asks for a snapshot | 15k-6 step 4 | DEC-06 steps 3 and 8 (D2-3) |
| Router-rejoin fix | retry every 30 s while the STA is down | S-3, 15k-5, 15k-7 | — |

### 9.2 High risks from the risk map → tests

| Risk (field symptom) | Sev | Covered by |
|---|---|---|
| A leak does not close the valve (kept-report path, membership gate, busy-provisioning copy) | S1 | 15k-6, P11, S-2, 15k-10(b), 15k-12, CP6-24, CP6-26. Tomorrow shows the kept and busy paths are not triggered on a normal run (3.7's busy-lock lines never print); the paths themselves run only on 15k-17's hold image (D3). |
| A false valve-button override | S1 | S-4, S-5, T5-04; `RMLEAK cleared externally` and the cross-reboot inference line grepped in every run. A restart mid-incident (T3-13/T3-14): not tomorrow (later). |
| The override does not block a new leak, or its cancel or `leak_reset` does not re-close; a genuine press not detected | S1 | T5-05, T5-06, T3-15 (B2); T5-07, T3-15 step 5 (D2-2) |
| A leak handled late because a task waits on the cloud | S1 | 15k-10, 15k-12, 15k-14(a), CP6-24 (T6-18), 15k-13(a), 15k-14(g)(6) (W2) |
| The hub never links to its own valve, or drives a neighbour's | S1 | S-8, 15k-6, CP6-24 relink; T3-05, T3-19 (D2-7) |
| A stale OPEN after a leak's CLOSE, or CLOSE before RMLEAK | S1 | 15k-6, CP6-24 step 7 (the relink after a PSU-off; the shielded relink, T3-17 Part A, is D2-2) |
| Scanning not resumed after a pause | S1 | 15k-1 (the hold), CP6-25 on every portal and outage run, CP6-26 |
| A leak edge lost in the fallback page's holds | S1 | CP6-27 (1 run tomorrow, 3 in D2-2) |
| No BLE detection in the no-credential portal | S1 (known) | CP6-26 (measured; WP8), and its resume close (P1) |
| The QueueSet incomplete at boot (a broken boot order) | S1 | 15k-11 (`IOTHUB: QueueSet: %s queue could not be added%s` never prints), S-2, P11; T4-01 later |
| LoRa packets dropped, so a LoRa leak is not closed | S1 (LoRa homes) | if open question 8 is yes: T5-13, LS-1 run 1 (no `Rx Queue Full`), CP6-24's LoRa variant (D2-2); otherwise N/A under EC-2 |
| `valve_open` refused by a stale latch | S2 | every `vo-…` after an auto-clear (15k-6 step 5 ×3, T5-04, T5-05(e), CP6-23a step 3, CP6-24 step 8); T5-05 (c); T3-09 later |
| The 10 s auto-clear churning on a flapping sensor | S2 | T6-15 (D2-7) |
| An override window ending too early or too late (uptime basis, re-base at sync, restart) | S2 | not tomorrow: T4-07, T4-09 (later); T5-09 (D3) |
| A wrong rating or LED | S2 | S-7, S-9, 15k-6 (RED, GREEN), P11, P14, CP6-20 step 2, CP6-24 steps 1 and 5; T2-02 … 07 later |
| A phone cannot finish Wi-Fi setup after a reset | S2 | 15k-1 on both phones, 15k-2 |
| The hub reboots in the portal | S2 | 15k-2, 15k-5 (Submit ×10), 15k-3/4(c)(d) with the laptop |
| No rejoin after an outage, or a late rejoin (heap gate) | S2 | S-3, 15k-5, 15k-7 |
| A full hub boots with its admission held by the heap gate | S2 | CP6-37 step 3a (D2-5) |
| A reset that does not erase in some STA state | S2 | 15k-2 (connected ×5 and idle ×1 tomorrow; idle ×2 and connecting ×3 in D2-2) |
| Events lost or reordered at the cloud | S2 | 15k-12, T5-12, CP6-35 |
| Heartbeats stop (snapshot outstanding, refused) | S2 | 15k-11, CP6-35 (gaps); CP6-37 (D2-5) |
| SAS renewal fails after about 18 h | S2 | overnight soak; CP6-43 (D3) |
| First commissioning fails at DPS | S2 | 15k-8 (D2-5, spare hub) |
| `cloud_tx` stack overflow under a big replay | S2 | 15k-16 (D3) |
| Heap at rest too low | S2 | 4.7, 15k-7 |
| The failed-allocation hook running with the cache off | S2 | `allocfail` 0 on every `MONITOR: idma: free=%lu min=%lu largest=%lu min_largest=%lu allocfail=%lu min_ever=%lu` line all day (no direct test) |
| A removal resets the other devices (BUG-2) | S2 | S-6; DEC-03 in full later; DEC-04, 05 later |
| A power cut mid-change leaves a mixed or empty set | S2 | not covered tomorrow: T6-02 (D2-5, spare hub), T6-01 (release) |
| A valve that keeps failing writes is never relinked (the cap) | S2 | not covered: T6-16 (later; it needs a provoked ATT stall) |
| 2.1.3 configuration or credentials not read after the upgrade | S2 | T6-03, CP6-41 (D2-4) |
| Real-world credentials refused by the new portal input | S2 | CP6-44 (D2-6) |
| A full hub's snapshot refused for ever | S2 | CP6-37 (D2-5) |

### 9.3 Deliberately not covered tomorrow, and why

| Not covered | Why | Where it goes |
|---|---|---|
| LoRa tests (T5-13, DEC-08, T3-08, T4-13; T4-10's LoRa steps), if open question 8 is no | no LoRa sensor with the bench key | EC-2 waiver; LoRa hardware later |
| T4-10 in full (130 min) and T6-13 | the test is written for code CP5 and WP1 changed; WP10 rewrites it for WP8 | 15k-1, 15k-2, 15k-5 and 15k-7 cover the portal on CP6 |
| App tests VAL-04 … VAL-12 | no app build; the app reads a contract WP3-WP10 may still change | release campaign |
| Heap like-for-like (T6-07/08/09, VAL-13) | needs the 2.1.3 image and 3 h per image; WP3 and WP9 change the figures | release campaign |
| VAL-14 / T6-12 ≥ 19 h soak | release gate; tomorrow's overnight soak is a CP6 check only | release campaign |
| Bench-only images (15k-15, 15k-16, 15k-17, T5-09, CP6-43, T4-08) | each needs its own worktree build and several hours | D3 |
| Spare-hub items (15k-8, 15k-14(b), DEC-18, CP6-37 … 39, T6-02, T4-06) | destructive (`decommission` `all`, `erase-flash`); HANDOFF limits them to the spare hub | D2-5, with a spare hub or your OK |
| Upgrade and rollback (T6-03, T6-04, CP6-41, CP6-42; VAL-02/03) | needs the 2.1.3 image; 1.5 h | D2-4 |
| T4-05 (NTP blocked), T4-06 (DPS blocked) | need firewall or DNS rules on the router | T4-05 in D2-7, T4-06 in D2-5 |
| 15k-7.1 linked runs 2-3 | the linked state is also covered by 15k-10(b) runs 2-3 (15k-7.2 run 2, 15k-7.3) | dropped; Claude asks for them only if run 1 L shows a problem |
| T5-06's step 5, T5-05's step (d) | low value beside the rest of each test | release campaign |
| T5-07, T6-05, T6-06 | T5-07 is on day 2; T6-05 and T6-06 are partly covered by CP6-29, CP6-30 and CP6-31, and rated L on CP6 | T5-07: D2-2; T6-05, T6-06: release campaign |
| T5-10 | no bench time: Claude runs it on B2's captures | CP6-35 |
| T6-14 (a full offline ring, then a Wi-Fi reset and a power cycle) | H on CP6, but 40 min | D2-2 |
| T6-01 (power cut during a single-device decommission) | never run on any image; rated L on CP6 | release campaign |
| T6-02 (power cut during decommission-all) | H on CP6 (WP2c hands the clear to `cloud_tx`), destructive | D2-5 (spare hub), or the release campaign |
| T6-16/17 (ATT stall, queue saturation script), T6-19/20 | T6-17 needs a script; T6-16 needs a provoked ATT stall | later |
| DEC-03 in full; DEC-15 | DEC-03: the smoke runs its BUG-2 core; DEC-15: time | DEC-03: release campaign; DEC-15: D2-3 |
| The remaining DEC, T2, T3 and T4 tests | one engineer, one day | D2-3, D2-7 and the release campaign |
| G3, G3b, G4, G5, G6, G7, G-CNA, G1 (radio/portal plan gates) | they gate WP4-WP8, not CP6 | with those packages |

---

## Open questions for you (answer tonight if you can)

1. **G0:** has any G0 run already happened on CP5 since 2026-10-01 (if so, send its captures)? May the remaining runs be folded into CP6 (4.2, recommended), or must G0 run on CP5 first (5.6)?
2. **Which image:** this plan tests CP6 = `545b8f2`, built in the `..\hub_cp6` worktree (4.4), not HEAD. HEAD (`d674c1c` at 20:39 on 2026-10-06, and still moving) already carries 46 commits of WP3, WP4, WP5 and WP6 work, plus uncommitted portal edits (1.4). Is CP6 still what you want? A later checkpoint would make several expectations here stale (the AP tail, Finish, the channel-switch handling, the scan owner) and bring other gates (WP3's G6b, WP4's G8x and G-CNA, WP5-WP6's).
3. **`decommission` `all` and `erase-flash`:** HANDOFF 15k allows `erase-flash` (15k-8) only on the spare hub, never on `GW-7C4FADAE69C8`, and `decommission` `all` (15k-14(b), DEC-18, CP6-37's clean-up, T6-02) only there too. The test plan's DEC-18 runs it on the bench hub. Is there a spare hub? If not, may the bench hub take a `decommission` `all` on day 2 (it then re-registers with DPS, and needs CP6-37's restore box)? `erase-flash` stays spare-hub only either way.
4. **The bench AP:** which option of 3.3 can you set up tonight (A with a WAN cable, A2 as a Wi-Fi repeater, B or C)? Can it take the two DROP rules, a static lease for the hub, and is its flow offloading off?
5. **Hardware:** a replacement for sensor `…2B:A5` (how many BLE sensors work)? A laptop with Python 3 (and `dig` if possible) for 15k-3/4, which otherwise move to day 2? A second ESP32 for 15k-4(b)? An RF shield for S-5 and T3-17 Part A?
6. **The overnight soak:** run it (5.4)? If so, keep it until the SAS renewal (about 12:00-12:30 on day 2), or stop it at 08:30?
7. **The valve's Low and Critical PSU voltages:** are they on record? If not, N5 tonight.
8. **LoRa:** is a LoRa leak sensor with the bench key at hand, and does this hub hear it (an SX1262 fitted: its packets print `APP_LORA: Verified: ID=0x%lX, Batt=%d%%, Leak=0x%X, Sent=%u, Ack=%u`, and `d`'s `RX=` count rises)? If yes, the LoRa branch of 6.2 runs; if no, the LoRa tests are N/A.
9. **The day's length:** the plan is a 10-hour day (08:30-18:30) for one engineer. Shorter? Then cut from 5.3 in order, and the cut items move to day 2.
10. **A code observation for your decision:** `override_enable` waits up to 10 s for an unreachable valve before it checks that there is a leak to override (`rules_engine.c:1453-1478`). A leak that latches during that wait passes the check, so a valve that becomes reachable in the wait would start a 24 h override and open for a leak that began after the request. Tomorrow's CP6-24 avoids it by keeping the PSU off. Should WP3 re-check the incident after the wait?

---

## Appendix A: bench scripts

### A.1 `dns_probe.py`

Use the copy in HANDOFF §15k item 3 verbatim. Save it outside the repo, as `$env:TEMP\dns_probe.py`.

### A.2 `dns_check.py`: 15k-3 without `dig` (standard library only)

Its parser was self-tested offline. It has not yet been run against the hub. Save it as `dns_check.py` on the laptop:
- `python dns_check.py` sends the six P-13 queries to 10.10.0.1;
- `python dns_check.py <LAN IP>` runs the LAN check.

The first query should print `status: NOERROR, flags: qr aa rd ra, ANSWER: 1 AUTHORITY: 0 ADDITIONAL: 1, A 10.10.0.1 TTL 60, OPT udp: 512, extra bytes at end: 0`.

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

### A.3 `cp6_37_gen.py`: the big-snapshot provisions (CP6-37)

It writes two C2D files, each under 8,192 B. Each replaces one sensor array, with 16 sensors and their labels: 15 fake BLE sensors plus the real one given as the second argument (CP6-37 step 3), and 16 fake LoRa sensors.

```python
import json, sys
mode = sys.argv[1]                      # ascii | quote | ctrl
real = sys.argv[2].upper() if len(sys.argv) > 2 else None   # a real BLE sensor's MAC, AA:BB:CC:DD:EE:FF
lab = {"ascii": lambda k, i: ("%s %02d long label ABCDEFGHIJKLMNOP" % (k, i))[:31],
       "quote": lambda k, i: '"' * 31,
       "ctrl":  lambda k, i: "\x01" * 31}[mode]
ble  = ["02:00:00:00:00:%02X" % i for i in range(1, 17)]
if real:
    ble[0] = real                       # a sensor that can really be wetted on the full hub
lora = ["0x7E0000%02X" % i for i in range(1, 17)]
def env(part, pl):
    return json.dumps({"schema": "eflostop.cmd", "ver": 1, "id": "cp6-37-%s-%s" % (mode, part),
                       "cmd": "provision", "payload": pl}, separators=(",", ":"))
b = env("ble", {"ble_leak_sensors": ble, "sensor_meta": [
    {"sensor_type": "ble_leak_sensor", "sensor_id": s, "location_code": "living_room", "label": lab("BLE", n)}
    for n, s in enumerate(ble)]})
l = env("lora", {"lora_sensors": lora, "sensor_meta": [
    {"sensor_type": "lora", "sensor_id": s, "location_code": "living_room", "label": lab("LoRa", n)}
    for n, s in enumerate(lora)]})
for name, txt in (("cp6_37_ble.json", b), ("cp6_37_lora.json", l)):
    open(name, "w", encoding="utf-8").write(txt)
    print(name, len(txt.encode()), "bytes (C2D limit 8192)")
```

Run CP6-39 first. Once the `ctrl` labels are in, that hub publishes no snapshot until the `decommission` `all` clean-up.
