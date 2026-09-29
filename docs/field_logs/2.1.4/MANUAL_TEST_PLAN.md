# eFloStop Wi-Fi Hub 2.1.4 - Manual Verification and Validation Test Plan

| | |
|---|---|
| Firmware under test | **2.1.4**, commit **`cc66d72`** (`PROJECT_VER "2.1.4"`), branch `fix/2.1.4`: the last commit that changes `main/` (the portal fix and its follow-up, see the header notes). HEAD adds docs only. **Re-baseline at Build checkpoint 4:** until then VAL-01, EC-1 and the section-local "checked against `d9fa9c8`" notes are still the CP3 build's. |
| Baseline | **2.1.3**, commit `ae4d59a` (`origin/master`) |
| Hardware | ESP32-S3 Wi-Fi Hub, ESP-IDF 5.5.1; eFloStop II valve (STM32WB, FW 2.2.0); eFloStop BLE leak sensors (STM32WBA); LoRa leak sensors where fitted |
| Date | 2026-09-27 (portal notes 2026-09-29) |
| Sources of truth | The firmware at `d9fa9c8` (`main/`), which wins over any document; `CHANGELOG.md` (2.1.4); the approved plan (scenarios S1-S26); `docs/field_logs/2.1.4/HANDOFF.md` §7, §10, §11; the round 1 and round 2 review records and both council votes |

> **Update 2026-09-29: portal priority window (firmware after `d9fa9c8`).** The 2026-09-29 bench capture showed that on `d9fa9c8` a phone could not join the SoftAP portal after a Wi-Fi reset: BLE scanning (the leak scanner, and on a valve hub the valve hunt) took the radio from the SoftAP, and no DHCP lease was ever given. Seven firmware commits on `fix/2.1.4` fix it: `5b5d70e`, `f65a95b`, `03c69a7`, `b548d50`, `529f6d1`, `efd6d84`, `ca4835f` (`main/app_wifi`, `main/ble_leak_scanner`, `main/ble_valve`, `main/health_engine`, `main/wifi_reset`). While the portal is up and no Wi-Fi credentials are saved, BLE scanning pauses: NimBLE stays up, a linked valve stays linked, and a leak close pended for an unlinked valve still hunts it. The BLE sensors' health timeouts and the snapshot gate are held and restart at the resume. The router-outage fallback portal keeps BLE scanning. **Until this plan is re-baselined at Build checkpoint 4:** the firmware under test is not `d9fa9c8` but `cc66d72` (this fix plus the follow-up below); VAL-01's `git diff --stat d9fa9c8 HEAD -- main …` lists exactly those files (run it against `cc66d72` instead, which must print nothing); the size figures of VAL-01 steps 4-5 and EC-1 are CP3's and move by about 15 B of `.bss` and a few KB of flash, with IRAM unchanged (still 16,384 B): record the CP4 figures. Every other quoted line is unchanged, except in T4-10 (rewritten for the window), T6-13, and the portal checks added elsewhere: T4-02 row 1 and T5-12 step 1 (the router-fallback line), T6-14 step 3, the 0.15 row for the window, smoke step 10 (now run on `SS-V4`), the portal rows of M.2 and M.4, and Appendices A.2, B.1 and B.2 (which follow the CHANGELOG).

> **Update 2026-09-29, follow-up (firmware after `ca4835f`).** Three changes, from the user's and the lead's decisions (HANDOFF §12a): (1) the 10 s reset always erases the Wi-Fi credentials, also with the STA idle on the router-outage fallback portal, which used to keep them (`93b8629`, `eeee9ca`, `main/wifi_reset`); (2) the window no longer closes at `Connected! IP` but when the setup SoftAP stops, about 60 s later, so the phone can load the portal's "Connected!" page; it closes at once if that Wi-Fi is lost first (`portal priority OFF (Wi-Fi lost after setup)`), and a safety net stops the SoftAP about 75 s after the IP (`73b4483`, `main/app_wifi`); (3) the valve's health verdicts are held during the pause like the BLE sensors', so a valve hub reads "syncing" (WHITE), not critical (RED), during setup (`a4894af`, `38200ac`, `main/health_engine`). `b2369a8` and `cc66d72` change comments only. **The firmware under test is now `cc66d72`:** run VAL-01's `git diff --stat` against `cc66d72` (it must print nothing), and record the CP4 sizes (`.bss` about 15 B above CP3 in all for the portal work, IRAM unchanged). Changed for it: T4-10 (A2, A3, A8, A9, B1, B3, Part D with the reset during an outage now a pass test, E2, E4, F3-F4, Part G, the new Part H), smoke steps 1 and 10, T6-13, T6-14 step 3, the 0.15 row, M.2 and M.4, and Appendices A.2, B.1 and B.2.

## How to use this document

1. **Read section 0 once** (equipment, flashing, capturing UART and IoT Hub, sending C2D, LED legend, timing constants, result codes). Keep 0.5 (identities), 0.13 (LEDs) and 0.15 (timing) open while you test.
2. **Run VAL-01 first.** It proves the image is the build under test (`cc66d72` until the CP4 re-baseline; see the header notes). Nothing else counts until it passes.
3. **Run the smoke subset (section S, about 30 min)** on every new build before the full campaign. A smoke Fail stops the campaign.
4. **Run sections 1-6.** Each section starts with its own conventions and start states, and ends with its own coverage table. Tests chain inside a section where the section says so. Leave the destructive and long tests for the end: T4-06 (erases provisioning), T6-10 (decommission all, large payloads), T6-11 (bench-only debug image), T4-08 and T5-09 (bench-only short-window builds, never shipped), and the soaks VAL-14 and T6-12 (19 h or more; run them overnight, on a second hub if you have one).
5. **Run the section 9 gates** (VAL-02 to VAL-15) and fill in the exit criteria (9.4).
6. **Check the traceability matrix (section M)** at the end: every item must have a `Pass`, or a `Known-limit` where the matrix says "observe".

**Result codes** are `Pass`, `Fail`, `Blocked` and `Known-limit` (0.17). A known limitation (CHANGELOG "Known limitations", F-02, F-03, the stale-confirm window) is tested as **observe and record**: it fails only if the behaviour is worse than documented.

**Expected log lines** are quoted verbatim from `main/` at `d9fa9c8` as `TAG: text`. A line marked **(format)** has printf fields; match its fixed text. Some lines carry an em dash (`—`) and others a hyphen (`-`), exactly as the code prints them: search on a distinctive substring. Where a line prints only under a timing condition, the test says so.

**Merged tests.** Where two sections wrote the same procedure, one copy was kept and the other now says "run as …". Record the result under both IDs.

| Test | Run as |
|---|---|
| T3-18 | main runs are T5-04; the stale-confirm observe part stays in T3-18 |
| T3-20 | run as T4-12 |
| T3-21 | run as T4-11 Part A |
| T5-02 | run as DEC-02 (with its 6 min extended watch) |
| T5-08 | run as T3-15, with two added checks |
| T5-15 | run as T6-18 |
| T5-16 | run as T6-15 |
| T6-13 | run as T4-10 |
| VAL-02 | runs A and B are T6-03; run C stays in VAL-02 |
| VAL-03 | run as T6-04 |
| VAL-13 | measured with T6-07 and T6-08; the pass rule stays in VAL-13 |
| DEC-18 power-cut variant | T6-02 |
| T4-10 Part C | T6-14 |
| T4-11 Part B | T3-13 |

**Placeholders.** The sections were written with different placeholder names for the same things. They all mean the bench identities of 0.5:

| Meaning | Sections 0, 1, 9 | Section 2 | Section 3 | Section 4 | Section 5 | Section 6 |
|---|---|---|---|---|---|---|
| Gateway ID (IoT Hub device ID) | `<GW>` | `<GW_ID>` | `<GW>` | `<GW>` | `<GW>` | `<GW>`, `<DEVICE_ID>` |
| Short ID (last 4 of the gateway ID) | "last 4" | `<SHORT_ID>` | `<XXXX>` | `<SHORT>` | "last 4" | `<SHORT>` |
| IoT Hub name | `<HUB>` | — | `<IOTHUB_NAME>` | — | — | — |
| Provisioned valve MAC ("valve A") | `<VALVE_MAC>` | `<VALVE_MAC>` | `<VALVE_A>` | `<VALVE>` | `<V>` | `<VALVE>` |
| Second valve (neighbour, or swap target) | `<VALVE_B_MAC>` | — | `<VALVE_B>` | — | — | `<VALVE_B>` |
| Provisioned BLE leak sensors | `<BLE1>`…`<BLE4>` | `<BLE1_MAC>` | `<S1>`, `<S2>` | A…D (`<A>`…`<D>`) | `<A>`…`<D>` | `<BLE1>`…`<BLE4>` |
| Provisioned LoRa sensor | `<LORA1>` (`<LORA2>`) | — | `<L1>` | L | `<L1>` | `<LORA1>` |
| Foreign (not provisioned) sensors | — | — | `<L2>` (LoRa) | N (LoRa), X (BLE) | — | — |
| Never-heard phantom BLE sensors | `<PH1>`…`<PH3>` (section 1) | — | — | — | — | fake `02:00:00:00:00:NN` (T6-10) |

**Safety of C2D messages.** A well-formed canonical envelope (valid JSON, `"schema":"eflostop.cmd"`) is safe: the legacy keyword scan runs only when the envelope fails to parse (0.8). So never send a message that fails to parse (smart quotes, truncation, a wrong `schema`) if it contains a legacy keyword, and never put `DECOMMISSION_ALL`, `DECOMMISSION_VALVE`, `DECOMMISSION_BLE:`, `DECOMMISSION_LORA:`, `VALVE_OPEN`, `VALVE_CLOSE`, `LEAK_RESET` or `OVERRIDE_CANCEL` in an `id`, label or name field. `DECOMMISSION_ALL` anywhere in a malformed message wipes the hub.

**Before any push.** Commit `6b84ae3` on `fix/2.1.4` contains the site Wi-Fi password in a doc (EC-12). Nothing is pushed until it is redacted.

## Contents

- [How to use this document](#how-to-use-this-document)
- [0. Equipment, setup and conventions](#0-equipment-setup-and-conventions)
- [M. Traceability matrix](#m-traceability-matrix)
- [S. Smoke subset (about 30 minutes)](#s-smoke-subset-about-30-minutes)
- [1. Decommission, UI sync and the empty hub (BUG-2, BUG-3, BUG-5, BUG-6; plan S1–S13)](#1-decommission-ui-sync-and-the-empty-hub-bug-2-bug-3-bug-5-bug-6-plan-s1s13)
  - [DEC-01](#dec-01--provision-a-valve-and-4-ble-sensors-on-an-empty-hub-ui-sync-syncing-countdown-pulse-boot-snapshot-s1--p0) Provision a valve and 4 BLE sensors on an empty hub: UI sync, "Syncing" countdown, pulse, …
  - [DEC-02](#dec-02--a-provision-that-adds-no-device-does-not-restart-the-pulse-or-the-commission-snapshot-council-residual--p0) A provision that adds no device does not restart the pulse or the commission snapshot …
  - [DEC-03](#dec-03--remove-one-heard-ble-sensor-after-the-sync-window-the-survivors-keep-their-data-s2-bug-2--p0) Remove one heard BLE sensor after the sync window: the survivors keep their data (S2, …
  - [DEC-04](#dec-04--remove-sensors-during-the-sync-window-heard-and-unheard-no-reset-no-new-window-original-deadlines-s3-bug-2-e-18-n10--p0) Remove sensors during the sync window, heard and unheard: no reset, no new window, …
  - [DEC-05](#dec-05--remove-the-last-unheard-device-during-the-window-one-boot-snapshot-no-second-s4-bug-2--p0) Remove the last unheard device during the window: one boot snapshot, no second (S4, …
  - [DEC-06](#dec-06--remove-a-sensor-while-it-is-wet-the-interlock-releases-leak_reset-is-accepted-an-override-cancel-does-not-re-close-s5-bug-2-rules-purge--p0) Remove a sensor while it is wet: the interlock releases, leak_reset is accepted, an …
  - [DEC-07](#dec-07--re-add-a-sensor-that-is-still-wet-a-fresh-leak_detected-and-a-re-close-s6-e-02-e-17--p0) Re-add a sensor that is still wet: a fresh leak_detected, and a re-close (S6, E-02, …
  - [DEC-08](#dec-08--remove-a-lora-sensor-survivors-unchanged-its-later-packets-ignored-no-pulse-from-them-lora-decommission-n4-e-18) Remove a LoRa sensor: survivors unchanged, its later packets ignored, no pulse from them …
  - [DEC-09](#dec-09--remove-only-the-valve-while-its-link-is-up-valve--no-valve_unlinked-no-rescan-s7-bug-5-p0-c--p0) Remove only the valve while its link is up: "valve": {}, no valve_unlinked, no rescan …
  - [DEC-10](#dec-10--decommission-error-paths-repeated-id-unknown-and-invalid-ids-no-valve-bad-targets-s12) Decommission error paths: repeated id, unknown and invalid ids, no valve, bad targets …
  - [DEC-11](#dec-11--remove-the-last-device-one-event-snapshot-of-the-empty-shape-led-white-rules-reset-then-heartbeats-s8-bug-3-bug-6-e-05--p0) Remove the last device: one event snapshot of the empty shape, LED WHITE, rules reset, …
  - [DEC-12](#dec-12--empty-hub-after-a-reboot-and-across-mqtt-drops-exactly-one-boot-snapshot-per-connect-s9-bug-6--p0) Empty hub after a reboot, and across MQTT drops: exactly one boot snapshot per connect …
  - [DEC-13](#dec-13--re-provision-an-empty-hub-after-1-and-after-10-heartbeats-s10-bug-6-e-10e-17-council-ble-start-claim--p0) Re-provision an empty hub after 1 and after 10 heartbeats (S10, BUG-6, E-10/E-17, …
  - [DEC-14](#dec-14--a-snapshot-that-follows-a-removal-or-an-add-never-shows-the-old-device-set-e-10) A snapshot that follows a removal or an add never shows the old device set (E-10)
  - [DEC-15](#dec-15--decommission-while-mqtt-is-offline-s13) Decommission while MQTT is offline (S13)
  - [DEC-16](#dec-16--back-to-back-empty-the-hub-then-provision-or-rules_config-at-once-the-new-config-survives-e-05-e-09-f-06f-07--p0) Back to back: empty the hub, then provision or rules_config at once; the new config …
  - [DEC-17](#dec-17--a-provision-whose-sensor-arrays-leave-a-sensors-only-hub-empty-e-05-follow-up-bug-6--p0) A provision whose sensor arrays leave a sensors-only hub empty (E-05 follow-up, BUG-6) …
  - [DEC-18](#dec-18--decommission-all-final-snapshot-of-the-empty-shape-override-cleared-offline-buffer-cleared-reboot-empty-boot-heartbeats-s11--p0) decommission all: final snapshot of the empty shape, override cleared, offline buffer …
  - [DEC-19](#dec-19--duplicate-ids-in-a-provision-payload-are-ignored-in-any-case-n13) Duplicate ids in a provision payload are ignored, in any case (N13)
- [2. Valve battery (BUG-1; plan S14-S17)](#2-valve-battery-bug-1-plan-s14-s17)
  - [T2-01](#t2-01--quick-battery-critical-check-about-10-min) Quick battery-critical check (about 10 min)
  - [T2-02](#t2-02--full-descent-good--low--crit-and-back-up-with-the-valves-read-cadence-about-35-min) Full descent GOOD → LOW → CRIT and back up, with the valve's read cadence (about 35 min)
  - [T2-03](#t2-03--plan-s14-sequence-unknown--good--10--11--10--disconnect--reconnect-unknown--10-about-35-min) Plan S14 sequence: unknown → GOOD → 10 → 11 → 10 → disconnect → reconnect (unknown) → ≤10 …
  - [T2-04](#t2-04--disconnect-while-battery-critical-inside-the-grace-and-a-quick-return-after-device_offline-about-12-min) Disconnect while battery-critical: inside the grace, and a quick return after …
  - [T2-05](#t2-05--plan-s17-valve_open-refused-at--10--linked-and-disconnected-and-the-refusal-order-about-15-min) Plan S17: valve_open refused at ≤ 10 %, linked and disconnected, and the refusal order …
  - [T2-06](#t2-06--plan-s15-an-unknown-valve-battery-is-null-never-0-about-5-min-plus-part-b-if-hardware-allows) Plan S15: an unknown valve battery is null, never 0 (about 5 min, plus part B if …
  - [T2-07](#t2-07--plan-s16-flapping-10--11-about-10-min) Plan S16: flapping 10 / 11 (about 10 min)
  - [T2-08](#t2-08--e-16-valve-link-age-while-linked-and-offline_duration_s-from-the-drop-good-battery-about-20-min) E-16: valve link age while linked, and offline_duration_s from the drop (GOOD battery, …
  - [T2-09](#t2-09--sensor-battery-bands-never-reach-critical-optional-needs-a-low-sensor-battery) Sensor battery bands never reach critical (optional; needs a low sensor battery)
- [3. P0 safety and valve lifecycle](#3-p0-safety-and-valve-lifecycle)
  - [T3-01](#t3-01---sensors-only-hub-with-an-unprovisioned-valve-nearby-a-leak-never-touches-it-s18) Sensors-only hub with an unprovisioned valve nearby: a leak never touches it (S18)
  - [T3-02](#t3-02---reboot-a-sensors-only-hub-ble-starts-and-the-sensors-are-scanned-and-reported-s19) Reboot a sensors-only hub: BLE starts and the sensors are scanned and reported (S19)
  - [T3-03](#t3-03---leak-right-after-a-valve-decommission-no-close-no-auto_close-nothing-sent-to-the-removed-valve-e-11) Leak right after a valve decommission: no close, no auto_close, nothing sent to the …
  - [T3-04](#t3-04---valve-commands-refused-with-no-valve-are-never-replayed-on-the-next-valve-p0-c) Valve commands refused with no valve are never replayed on the next valve (P0-c)
  - [T3-05](#t3-05---re-provision-a-different-valve-mac-while-linked-s22) Re-provision a different valve MAC while linked (S22)
  - [T3-06](#t3-06---valve-swap-with-the-old-valve-flooded-e-04-plus-the-removal-and-wet-sensor-variants) Valve swap with the old valve flooded (E-04, plus the removal and wet-sensor variants)
  - [T3-07](#t3-07---quick-decommission-and-re-provision-of-the-same-valve-it-still-relinks-after-its-next-drop-e-09) Quick decommission and re-provision of the same valve: it still relinks after its next …
  - [T3-08](#t3-08---foreign-lora-sensor-packets-are-not-evaluated-by-the-rules-engine-s23-n4) Foreign LoRa sensor: packets are not evaluated by the rules engine (S23, N4)
  - [T3-09](#t3-09---valve_open-is-refused-while-a-leak-is-latched-even-with-the-valve-powered-off-e-06-decision) valve_open is refused while a leak is latched, even with the valve powered off (E-06 …
  - [T3-10](#t3-10---an-open-pended-while-the-valve-is-away-is-overwritten-by-a-leaks-close-e-06) An OPEN pended while the valve is away is overwritten by a leak's close (E-06)
  - [T3-11](#t3-11---f-01a-the-hubs-own-auto-clear-pended-while-the-valve-is-out-of-range-is-not-read-as-a-button-press) F-01(a): the hub's own auto-clear, pended while the valve is out of range, is not read as …
  - [T3-12](#t3-12---f-01b-leak_reset-sent-while-the-valve-is-out-of-range) F-01(b): leak_reset sent while the valve is out of range
  - [T3-13](#t3-13---f-01c-hub-power-cycled-with-the-incident-latched-and-every-sensor-dry) F-01(c): hub power-cycled with the incident latched and every sensor dry
  - [T3-14](#t3-14---f-01d-a-clear-owed-at-a-hub-restart-is-lost-with-ram-the-reconnect-fails-closed-and-the-auto-clear-releases-it) F-01(d): a clear owed at a hub restart is lost with RAM; the reconnect fails closed and …
  - [T3-15](#t3-15---a-genuine-valve-long-press-still-starts-the-24-h-window-srs-442-including-after-a-hub-restart) A genuine valve long-press still starts the 24 h window (SRS 4.4.2), including after a …
  - [T3-16](#t3-16---b1a-hub-power-cycled-with-a-sensor-wet-and-the-valve-open---the-close-is-not-delayed-by-a-busy-gatt-pool) B1(a): hub power-cycled with a sensor wet and the valve open - the close is not delayed …
  - [T3-17](#t3-17---b1b-leak-while-the-valve-is-out-of-range---rmleak-before-close-at-the-relink-no-forced-relink) B1(b): leak while the valve is out of range - RMLEAK before CLOSE at the relink, no …
  - [T3-18](#t3-18---b1c-re-wet-at-the-moment-of-the-auto-clear-f-08-order-stale-confirm-residual) B1(c): re-wet at the moment of the auto-clear (F-08 order, stale-confirm residual)
  - [T3-19](#t3-19---valve-identity-a-bonded-valve-reconnects-normally-identity-address--provisioned-mac) Valve identity: a bonded valve reconnects normally (identity address = provisioned MAC)
  - [T3-20](#t3-20---known-limitation-f-03-deferred-to-215-leak-latched-while-the-valve-was-away-then-a-hub-restart---observe-and-record) Known limitation F-03 (deferred to 2.1.5): leak latched while the valve was away, then a …
  - [T3-21](#t3-21---known-limitation-f-02-accepted-restart-during-a-live-leak-can-release-rmleak-for-one-wet-burst---observe-and-record) Known limitation F-02 (accepted): restart during a live leak can release RMLEAK for one …
- [4. Protection before Wi-Fi, clock and offline (N1-N4; plan S20, S21)](#4-protection-before-wi-fi-clock-and-offline-n1-n4-plan-s20-s21)
  - [T4-01](#t4-01--boot-order-protection-queueset-and-ble-before-wi-fi-about-5-min) Boot order: protection, QueueSet and BLE before Wi-Fi (about 5 min)
  - [T4-02](#t4-02--plan-s20-boot-with-the-router-off-then-a-leak-closes-the-valve-with-no-wi-fi-about-10-min) Plan S20: boot with the router off, then a leak closes the valve with no Wi-Fi (about 10 …
  - [T4-03](#t4-03--pre-sync-events-stamped-at-the-clock-sync-and-replayed-in-order-after-the-connect-about-5-min) Pre-sync events: stamped at the clock sync and replayed in order after the connect (about …
  - [T4-04](#t4-04--a-restart-before-the-clock-syncs-drops-that-boots-pre-sync-events-about-10-min) A restart before the clock syncs drops that boot's pre-sync events (about 10 min)
  - [T4-05](#t4-05--ntp-blocked-the-sntp-fallback-and-leak-protection-without-a-clock-about-15-min) NTP blocked: the SNTP fallback, and leak protection without a clock (about 15 min)
  - [T4-06](#t4-06--dps-unreachable-the-in-loop-backoff-the-5-min-retry-and-the-recovery-destructive-about-45-min) DPS unreachable: the in-loop backoff, the 5 min retry and the recovery (DESTRUCTIVE, …
  - [T4-07](#t4-07--override-started-before-the-clock-synced-timed-on-uptime-re-based-at-the-sync-about-20-min) Override started before the clock synced: timed on uptime, re-based at the sync (about 20 …
  - [T4-08](#t4-08--override-started-before-the-clock-synced-expires-after-24-h-of-uptime-with-no-internet-optional-24-h-or-about-15-min-on-a-bench-only-build) Override started before the clock synced expires after 24 h of uptime with no internet …
  - [T4-09](#t4-09--override-windows-restored-after-a-restart-with-no-internet-e-03-about-30-min) Override windows restored after a restart with no internet (E-03; about 30 min)
  - [T4-10](#t4-10--plan-s21-captive-portal-with-ble-scanning-paused-about-115-min) Plan S21: captive portal with BLE scanning paused (about 115 min)
  - [T4-11](#t4-11--restart-during-a-leak-f-02-accepted-f-01-route-b-about-40-min) Restart during a leak (F-02 accepted; F-01 route b) (about 40 min)
  - [T4-12](#t4-12--f-03-known-limitation-a-leak-latched-while-the-valve-was-unreachable-then-a-hub-restart-observe-and-record-about-30-min) F-03 known limitation: a leak latched while the valve was unreachable, then a hub restart …
  - [T4-13](#t4-13--n4-packets-from-sensors-that-are-not-provisioned-do-not-reach-the-rules-engine-optional-needs-a-neighbour-sensor-about-10-min) N4: packets from sensors that are not provisioned do not reach the rules engine …
  - [T4-14](#t4-14--serial-log-hygiene-no-wi-fi-password-and-no-valve-passkey-on-uart-about-5-min-after-t4-01-and-t4-10) Serial-log hygiene: no Wi-Fi password and no valve passkey on UART (about 5 min, after …
- [5. Regression of 2.1.3 behaviour and new rules behaviour (plan S24)](#5-regression-of-213-behaviour-and-new-rules-behaviour-plan-s24)
  - [T5-01](#t5-01--provision-per-advertisement-snapshots-and-syncing---waiting-for-n-devices-clearing-as-each-device-is-heard) Provision: per-advertisement snapshots, and "Syncing - waiting for N devices" clearing as …
  - [T5-02](#t5-02--a-provision-that-adds-no-device-identical-re-send-one-event-snapshot-no-pulse) A provision that adds no device (identical re-send): one event snapshot, no pulse
  - [T5-03](#t5-03--leak--auto_close-rmleak-before-close--dry--10-s-auto-clear--valve_open) Leak → auto_close (RMLEAK before CLOSE) → dry → 10 s auto-clear → valve_open
  - [T5-04](#t5-04--re-wet-at-the-clear-5-runs-release-then-re-lock-never-a-false-button-override) Re-wet at the clear (5 runs): release, then re-lock, never a false button override
  - [T5-05](#t5-05--the-leak_reset--valve_open-guard-matrix) The leak_reset / valve_open guard matrix
  - [T5-06](#t5-06--override_enable-c2d-during-a-leak--blocked-leak--override_cancel-with-the-leak-still-active-re-close) override_enable (C2D) during a leak → blocked leak → override_cancel with the leak still …
  - [T5-07](#t5-07--override_cancel-with-no-leak-d4-with-no-window-d5-and-leak_reset-cancelling-a-window) override_cancel with no leak (D4), with no window (D5), and leak_reset cancelling a window
  - [T5-08](#t5-08--valve-long-press-physical-override-live-after-a-hub-restart-and-pressed-right-after-the-close) Valve long-press (physical) override: live, after a hub restart, and pressed right after …
  - [T5-09](#t5-09--override-expiry-bench-only-shortened-build) Override expiry (bench-only shortened build)
  - [T5-10](#t5-10--rmleak-write-and-read-back-audit-from-the-t5-03--t5-09-uart-capture) RMLEAK write and read-back audit (from the T5-03 … T5-09 UART capture)
  - [T5-11](#t5-11--heartbeat-interval-change-through-the-twin-desired-property) Heartbeat interval change through the twin desired property
  - [T5-12](#t5-12--offline-event-buffering-and-reconnect-replay-order) Offline event buffering and reconnect replay order
  - [T5-13](#t5-13--lora-leak-path-end-to-end-only-if-lora-sensors-and-the-sx1262-are-fitted) LoRa leak path end to end (only if LoRa sensors and the SX1262 are fitted)
  - [T5-14](#t5-14--contract-validator-over-the-full-section-capture-0-fail) Contract validator over the full section capture: 0 FAIL
  - [T5-15](#t5-15--override_enable-with-an-unreachable-valve-while-a-leak-starts-leak-handling-delay-of-10-s-or-less) override_enable with an unreachable valve while a leak starts: leak handling delay of 10 …
  - [T5-16](#t5-16--flapping-sensor-soak-with-the-10-s-auto-clear-15-min) Flapping sensor soak with the 10 s auto-clear (15 min)
- [6. Robustness, upgrade and heap (plan S25, S26; council residual risks)](#6-robustness-upgrade-and-heap-plan-s25-s26-council-residual-risks)
  - [T6-01](#t6-01--power-cut-during-a-single-device-decommission) Power cut during a single-device decommission
  - [T6-02](#t6-02--power-cut-during-decommission-all) Power cut during decommission-all
  - [T6-03](#t6-03--upgrade-213--214-without-erase) Upgrade 2.1.3 → 2.1.4 without erase
  - [T6-04](#t6-04--rollback-214--213-without-erase) Rollback 2.1.4 → 2.1.3 without erase
  - [T6-05](#t6-05--repeated-command-ids) Repeated command IDs
  - [T6-06](#t6-06--unknown-device-ids-unknown-targets-and-commands-malformed-messages) Unknown device IDs, unknown targets and commands, malformed messages
  - [T6-07](#t6-07--heap-like-for-like-valve--4-ble-hub-rebooted-with-wi-fi-213-vs-214) Heap like-for-like: valve + 4 BLE hub rebooted with Wi-Fi (2.1.3 vs 2.1.4)
  - [T6-08](#t6-08--heap-like-for-like-commissioning-from-empty-the-field-scenario) Heap like-for-like: commissioning from empty (the field scenario)
  - [T6-09](#t6-09--heap-on-a-sensors-only-ble-hub-reported-separately) Heap on a sensors-only BLE hub (reported separately)
  - [T6-10](#t6-10--large-hubs-20-and-33-devices-and-16-ble-sensors-with-31-character-labels-e-01-known-limitation) Large hubs: 20 and 33 devices, and 16 BLE sensors with 31-character labels (E-01, known …
  - [T6-11](#t6-11--task-stack-high-water-marks-bench-only-debug-image-never-shipped) Task stack high-water marks (bench-only debug image, never shipped)
  - [T6-12](#t6-12--router-offon--5-and-a--19-h-soak-with-a-sas-renewal-sensors-only-ble-hub) Router off/on × 5 and a ≥ 19 h soak with a SAS renewal (sensors-only BLE hub)
  - [T6-13](#t6-13--wi-fi-reset-and-captive-portal-beside-ble-heap-protection-credentials) Wi-Fi reset and captive portal beside BLE: heap, protection, credentials
  - [T6-14](#t6-14--nvs-pressure-a-full-offline-buffer-then-a-wi-fi-reset-and-a-power-cycle) NVS pressure: a full offline buffer, then a Wi-Fi reset and a power cycle
  - [T6-15](#t6-15--a-sensor-flapping-every-1520-s-for-15-min-the-10-s-auto-clear-write-rate) A sensor flapping every 15–20 s for 15 min (the 10 s auto-clear write rate)
  - [T6-16](#t6-16--sustained-att-stall-on-a-live-valve-link-bounded-by-the-30-s-gatt-timeout) Sustained ATT stall on a live valve link, bounded by the 30 s GATT timeout
  - [T6-17](#t6-17--valve-command-queue-saturation) Valve command queue saturation
  - [T6-18](#t6-18--iothub_task-stalled-behind-a-blocking-c2d-override_enable-with-the-valve-off-then-a-leak-within-2-s) iothub_task stalled behind a blocking C2D (override_enable with the valve off, then a …
  - [T6-19](#t6-19--a-leak-sensor-unheard-during-a-hanging-valve-connect--30-s-known-limitation) A leak sensor unheard during a hanging valve connect (≤ 30 s, known limitation)
  - [T6-20](#t6-20--ble-start-claimed-twice-at-once-boot-apply-and-c2d-provision) BLE start claimed twice at once (boot apply and C2D provision)
- [9. Validation and exit criteria](#9-validation-and-exit-criteria)
  - [VAL-01](#val-01-build-checkpoint-3-gate-d9fa9c8-p0) Build checkpoint 3 gate (d9fa9c8) (P0)
  - [VAL-02](#val-02-upgrade-213--214-in-place-keeps-provisioning-rules-the-incident-latch-and-the-override-p0) Upgrade 2.1.3 → 2.1.4 in place keeps provisioning, rules, the incident latch and the …
  - [VAL-03](#val-03-rollback-214--213-keeps-provisioning-and-213-reads-the-214-offline-buffer) Rollback 2.1.4 → 2.1.3 keeps provisioning, and 2.1.3 reads the 2.1.4 offline buffer
  - [VAL-04](#val-04-the-app-shows-the-right-devices-after-a-provision-with-the-syncing-state-p0) The app shows the right devices after a provision, with the syncing state (P0)
  - [VAL-05](#val-05-the-app-shows-the-survivors-unchanged-after-one-device-is-removed-p0) The app shows the survivors unchanged after one device is removed (P0)
  - [VAL-06](#val-06-the-apps-empty-state-last-device-removed-heartbeats-reboot-p0) The app's empty state: last device removed, heartbeats, reboot (P0)
  - [VAL-07](#val-07-the-app-shows-no-valve-for-a-sensors-only-hub-valve--and-the-no-valve-command-refusal-p0) The app shows "no valve" for a sensors-only hub (valve {}), and the no-valve command …
  - [VAL-08](#val-08-the-app-shows-valve-battery-unknown-low-and-critical-and-the-battery-refusal-p0) The app shows valve battery unknown, Low and Critical, and the battery refusal (P0)
  - [VAL-09](#val-09-the-app-during-a-leak-locked-the-refused-open-the-10-s-auto-clear-and-the-re-wet-order-p0) The app during a leak: locked, the refused open, the 10 s auto-clear, and the re-wet …
  - [VAL-10](#val-10-the-apps-override-state) The app's override state
  - [VAL-11](#val-11-the-app-after-a-device-goes-offline-and-comes-back-with-a-connected-valves-age-at-0) The app after a device goes offline and comes back, with a connected valve's age at 0
  - [VAL-12](#val-12-the-app-reads-every-new-214-wire-shape-without-an-error-app-parser-sweep) The app reads every new 2.1.4 wire shape without an error (app parser sweep)
  - [VAL-13](#val-13-heap-like-for-like-214-against-213-p0) Heap like-for-like, 2.1.4 against 2.1.3 (P0)
  - [VAL-14](#val-14-soak-no-reboot-sas-renewal-p0) Soak, no reboot, SAS renewal (P0)
  - [VAL-15](#val-15-validator-on-the-full-capture-p0) Validator on the full capture (P0)
- [Appendix A. Known limitations (CHANGELOG 2.1.4, verbatim)](#appendix-a-known-limitations-changelog-214-verbatim)
- [Appendix B. UART log lines: new and changed in 2.1.4](#appendix-b-uart-log-lines-new-and-changed-in-214)

## 0. Equipment, setup and conventions

This section applies to every test in the plan. Read it once before the first run, and keep 0.5 (identities), 0.13 (LED legend) and 0.15 (timing) open while you test.

Every log line and JSON shape quoted in this plan was checked against the firmware at `d9fa9c8` (`main/`), not against the docs. Where a doc and the code disagree, the code wins, and so does this plan.

### 0.1 Hardware

| # | Item | Qty | Notes |
|---|---|---|---|
| H1 | eFloStop II Wi-Fi Hub (ESP32-S3), USB-C to the PC | 1 (2 recommended) | The bench unit is `GW-7C4FADAE69C8` on `COM30`. A second hub lets you keep a 2.1.3 reference unit for the like-for-like heap check (VAL-13). |
| H2 | eFloStop II valve, STM32WB, FW **2.2.0** | 1 | This is the provisioned valve ("valve A"). Power it from the bench PSU (H4) for the battery tests, and from batteries or the PSU at 6.0 V otherwise. |
| H3 | Second eFloStop II valve, FW 2.2.0 ("valve B", the neighbour) | 1 | Powered, **not** provisioned. It is used by the P0-a/SAF tests and the valve-swap tests. Keep it within 2 m of the hub. |
| H4 | Bench PSU, 0–10 V, adjustable in 10 mV steps, with a display you can read to 10 mV | 1 | Replaces the valve's battery pack. See 0.10. |
| H5 | BLE leak sensors (STM32WBA, eleak), current release FW | 4 (minimum) | `<BLE1>`…`<BLE4>`. Up to 16 are needed for the E-01 large-hub case. |
| H6 | LoRa leak sensors (SX1262 link), where available | 1–2 | `<LORA1>`, `<LORA2>`. If none is available, the LoRa-packet cases (DEC-08 variant A, T3-08, T4-13, T5-13) are recorded `N/A (no LoRa HW)` under the EC-2 LoRa exception (a written waiver, 9.4), and DEC-08 variant B (no radio) is run instead. |
| H7 | Wi-Fi router whose WAN you can unplug, and whose power you can switch | 1 | Used for the no-Wi-Fi and MQTT-drop cases. Unplug the **WAN** cable to drop MQTT while Wi-Fi stays up; power the router off to drop Wi-Fi too. |
| H8 | PC (Windows) with ESP-IDF 5.5.1, VS Code with the ESP-IDF and Azure IoT Hub extensions, Azure CLI with the `azure-iot` extension, Python 3 | 1 | See 0.2. |
| H9 | A phone with the eFloStop app build that the app team supplies for 2.1.4 validation | 1 | Record the app version on the results sheet. Used in section 9 and for the captive portal. |
| H10 | Water: a cup of tap water and a paper towel | — | Wet a sensor by bridging its probe pads with a wet paper towel, and dry it by removing the towel and wiping the pads. Do not immerse the sensor. |
| H11 | RF shield: a metal box or a closed microwave oven (unpowered), or 20 m of distance | 1 | Used to take the valve "out of range" without powering it off. |
| H12 | Stopwatch, or the timestamps in the logs (0.6, 0.7) | — | Time every measurement from the log timestamps, not by eye. |

### 0.2 Firmware and software versions

| Component | Version under test | How to confirm it |
|---|---|---|
| Hub firmware | **2.1.4**, branch `fix/2.1.4`, firmware commit **`d9fa9c8`**. HEAD may be later, but only with docs changes. | UART at boot: `HUB_IDENT: Firmware version: v2.1.4`. Every D2C message: `"gateway": {..., "fw": "2.1.4", ...}`. Twin reported: `"fw_version": "2.1.4"`. |
| Hub baseline (for regression, rollback and heap) | **2.1.3**, commit `ae4d59a` (`origin/master`) | UART: `HUB_IDENT: Firmware version: v2.1.3`. |
| Valve firmware | **2.2.0** | Snapshot `data.valve.fw_version` is `"2.2.0"` once the valve is linked and its readings are in. |
| BLE leak sensor firmware | The current eleak release. Record it per sensor. | Snapshot `data.ble_leak_sensors[].fw_version`. Record the value for each sensor on the results sheet. |
| LoRa sensor firmware | Record it | Record it from the sensor label or its own tool. The hub does not report a LoRa sensor's FW. |
| ESP-IDF | v5.5.1 at `C:\Users\antun\esp\v5.5.1\esp-idf` | `idf.py --version` |
| Validator | `docs/telemetry/validate_capture.py` at HEAD of `fix/2.1.4` | Its docstring says "wire contract" for "firmware 2.1.4", and it checks `EXPECTED_FW = "2.1.4"`. |
| IoT Hub | The bench hub is `wd-core-iothub-poc`; the device identity is the Gateway ID, for example `GW-7C4FADAE69C8`. | VS Code Azure IoT Hub explorer, or `az iot hub device-identity show -n <HUB> -d <GW>` |

**Check that the image really is `d9fa9c8` before you test.** Run this once in the project folder (PowerShell):

```powershell
git log --oneline -1
git diff --stat d9fa9c8 HEAD -- main CMakeLists.txt partitions.csv sdkconfig.defaults managed_components
(Get-Item build\eFloStop_WiFiHub_idf1.bin).LastWriteTime
(Get-Item build\eFloStop_WiFiHub_idf1.bin).Length
git log -1 --format=%ci d9fa9c8
Get-FileHash sdkconfig
```

- The `git diff` must print nothing: HEAD differs from `d9fa9c8` in docs only.
- `sdkconfig` is **git-ignored and untracked** (`.gitignore`), so no `git diff` can show a change to it. Record its `Get-FileHash` (SHA256) on the results sheet, and compare it with the hash of the `sdkconfig` used for CP3 and with the copy in the 2.1.3 worktree (0.4). They must all be the same file (the current one is dated 2026-06-26, before both commits).
- The `.bin` must be **newer** than the `d9fa9c8` commit time (2026-09-27 00:35:07 +1000). When this plan was written, `build\eFloStop_WiFiHub_idf1.bin` was dated **2026-09-27 09:11:23 +1000** and was **1,528,576 B** (0x175300): that is the Build checkpoint 3 image of `d9fa9c8` (HANDOFF §4b), and it can be flashed as is. VAL-01 still does one full rebuild, only to capture the warning lines the CP3 paste omitted.

### 0.3 Flash 2.1.4 over USB (bench hub, from the build folder)

Use this procedure for a unit whose flash contents do not matter, or one that is already on 2.1.4.

1. Build (skip this step if VAL-01 has just built this tree):
   ```powershell
   idf.py fullclean
   idf.py build *> "$env:TEMP\build_cp3.log" ; "exit=$LASTEXITCODE"
   ```
2. Flash and open the monitor:
   ```powershell
   idf.py -p COM30 flash monitor --timestamps
   ```
   The VS Code ESP-IDF extension is equivalent: select port `COM30` (it is already set in `.vscode/settings.json` as `idf.port`), flash method UART, then "ESP-IDF: Build, Flash and Monitor". If you use the extension, still capture the UART to a file as in 0.6: the VS Code terminal scrollback is 10,000 lines, and a soak overflows it.
3. Confirm the boot lines in 0.14.

`idf.py flash` writes four regions only: the bootloader at `0x0`, the partition table at `0x8000`, `ota_data_initial.bin` at `0x11000` and the app at `0x20000` (these offsets are from `build/flasher_args.json`). It does **not** touch `nvs` (`0x9000`, 16 KB: Wi-Fi credentials, the DPS cache, the offline buffer, the rules state) or `nvs_prov` (`0xD000`, 16 KB: the valve and sensors, the rules config, sensor metadata, the snapshot interval). **Provisioning survives `idf.py flash`. Only `idf.py erase-flash` (or `esptool erase_flash`) wipes it.** Never run `erase-flash` in this plan unless a test tells you to.

### 0.4 Upgrade from 2.1.3 with devices provisioned (the field path), and rollback

**There is no OTA client in 2.1.4.** A search of `main/` and `components/` finds no `esp_ota_*`, `esp_https_ota` or other update code. The partition table has `ota_0` and `ota_1` slots, but nothing writes them, so the hub always boots the `factory` slot at `0x20000`. Azure OTA is still a deferred item (project memory: `dps_ota_plan.md`). The CHANGELOG's "OTA from 2.1.3 keeps provisioning" is really a statement about data compatibility: there is no change to any NVS namespace, key or layout, nor to the partition table.

So the field upgrade is simulated by **flashing the app over a provisioned 2.1.3 hub without erasing flash**. This is exactly what an update does to NVS (nothing), and it is the procedure that VAL-02 (with T6-03) and VAL-03 (T6-04) use.

**Getting a 2.1.3 image.** Build `ae4d59a` in a separate worktree, so the `fix/2.1.4` tree and its `build\` are left alone. `managed_components/` (with the local wifi_manager patches), `partitions.csv` and `sdkconfig.defaults` are tracked in git. **`sdkconfig` is not**: it is git-ignored (`.gitignore`), and `sdkconfig.defaults` (736 B) holds only lwIP/HTTPD/mbedTLS keys, with no `CONFIG_BT_ENABLED`/NimBLE, no custom partition table and no 16 MB flash setting. A fresh worktree built without the real `sdkconfig` gets a default configuration: the build fails with NimBLE off, or the image is not like-for-like. So copy the project's `sdkconfig` into the worktree **before** the first build:

```powershell
git worktree add ..\hub_2_1_3 ae4d59a
Copy-Item sdkconfig ..\hub_2_1_3\sdkconfig
cd ..\hub_2_1_3
(Get-FileHash sdkconfig).Hash      # must equal the hash recorded in 0.2
idf.py build
idf.py size
(Get-Item build\eFloStop_WiFiHub_idf1.bin).Length
# 2.1.3 image = ..\hub_2_1_3\build\eFloStop_WiFiHub_idf1.bin
```

Before using this image, confirm it is the real 2.1.3 build (HANDOFF §4b): `idf.py size` gives `.bss` **36,120 B** and `.data` **21,556 B**, and the `.bin` is **1,502,240 B** (0x16EC20). Any other figure means the configuration differs: stop and fix the `sdkconfig` copy. The same rule (copy `sdkconfig` first, then build) applies to every other worktree in this plan, for example the `..\hub_hwm` debug worktree of T6-11.

**Upgrade 2.1.3 → 2.1.4, app only (preferred: it writes nothing but the app):**

```powershell
cd "C:\Work\Projects\EfloStop 2\Firmware\Production\eFloStop_WiFiHub_idf1"
idf.py -p COM30 app-flash monitor --timestamps
```

**Rollback 2.1.4 → 2.1.3, app only:**

```powershell
cd ..\hub_2_1_3
idf.py -p COM30 app-flash monitor --timestamps
```

**Equivalent esptool command** (for a prebuilt `.bin`, for example one handed to production):

```powershell
python -m esptool --chip esp32s3 -p COM30 -b 460800 --before default_reset --after hard_reset write_flash 0x20000 <path>\eFloStop_WiFiHub_idf1.bin
```

Rules for both directions:
- **Never** use `erase-flash`, `erase_region`, or the "Erase flash" button of the VS Code extension.
- `idf.py flash` (full) is also safe for NVS (see 0.3). `app-flash` is preferred because it is the closest to what an update does.
- After flashing, the first boot must show `PROVISIONING: Loaded existing config from NVS`, with the same counts as before the flash (0.14).

### 0.5 Bench identities (fill in before the first test)

Every C2D JSON in this plan uses these placeholders. Replace them with the real values. MACs are upper-case with colons (`XX:XX:XX:XX:XX:XX`, 17 characters), and LoRa IDs are `0x` plus 8 upper-case hex digits.

| Placeholder | Meaning | Value on this bench |
|---|---|---|
| `<HUB>` | IoT Hub name | `wd-core-iothub-poc` |
| `<GW>` | Hub Gateway ID (device ID in IoT Hub) | `GW-7C4FADAE69C8` |
| `<COM>` | Hub serial port | `COM30` |
| `<VALVE_MAC>` | Valve A (provisioned) BLE MAC | |
| `<VALVE_B_MAC>` | Valve B (neighbour, never provisioned unless a swap test says so) | |
| `<BLE1>` … `<BLE4>` | BLE leak sensor MACs | |
| `<LORA1>`, `<LORA2>` | LoRa sensor IDs | |

### 0.6 Capturing the UART log to a file, with timestamps

Every test needs a UART log with a host timestamp on each line.

**Option A: `idf.py monitor` with the built-in logger (preferred).**

1. Start the monitor with timestamps:
   ```powershell
   idf.py -p COM30 monitor --timestamps --timestamp-format "%Y-%m-%d %H:%M:%S.%f"
   ```
2. Press **Ctrl+T**, then **Ctrl+L**. The monitor prints `Logging is enabled into file log.eFloStop_WiFiHub_idf1.<YYYYmmddHHMMSS>.txt`. The file is written to the current directory, and the timestamps go into the file too.
3. At the end of the test, press Ctrl+T, Ctrl+L again to close the file.
4. Rename the file to `<TESTID>_uart.txt` (for example `DEC-03_uart.txt`) and store it with the results (0.17).

**Option B: the VS Code ESP-IDF monitor.** Enable timestamps in the extension settings, and use Ctrl+T, Ctrl+L in the monitor terminal as above. Do not rely on copying the terminal: it keeps only 10,000 lines.

Notes:
- The device's own `(NNNNN)` number in each line is milliseconds since boot. Use it to measure intervals inside one boot. Use the host timestamp to line UART up with the IoT Hub capture.
- A reboot restarts the `(NNNNN)` counter. A counter that drops back to a small number, without a power cycle you did yourself, **is an unplanned reboot** and fails the soak criterion (9.4, EC-6).
- The ANSI colour codes stay in the file. That does not matter for grep or for the validator.

### 0.7 Capturing the IoT Hub (D2C) stream to a file

**Option A: VS Code Azure IoT Hub extension.**
1. In the Azure IoT Hub explorer, right-click the device `<GW>` and choose **Start Monitoring Built-in Event Endpoint**.
2. The messages appear in the OUTPUT panel, channel "Azure IoT Hub", one block per message, each preceded by a header like `[IoTHubMonitor] [10:24:34 AM] Message received from [GW-7C4FADAE69C8]:`. The header time is the PC's local time.
3. At the end of the test, click in the OUTPUT panel, press Ctrl+A, Ctrl+C, paste into a new file, and save it as **UTF-8** with the name `<TESTID>_iothub.txt`.
4. Stop monitoring with **Stop Monitoring Built-in Event Endpoint** before you start the next test, or keep one capture running for a whole session and save it at the end.

**Option B: Azure CLI (preferred for anything over 30 min, and for the soak).** Run it in **Git Bash**, not PowerShell 5.1: PowerShell's `>` and `Tee-Object` write UTF-16, which the validator cannot read.

```bash
az iot hub monitor-events -n wd-core-iothub-poc -d GW-7C4FADAE69C8 \
   --content-type application/json --properties sys --timeout 0 \
   | tee "SOAK_iothub_$(date +%Y%m%d_%H%M%S).txt"
```

- `--content-type application/json` makes the CLI print each payload as JSON. The hub publishes with no content-type property, and without this flag the payload is printed as an escaped string that the validator cannot extract.
- `--properties sys` adds `iothub-enqueuedtime` to each message. That is the cloud arrival time, which you need when a replayed event's `ts` is older than its arrival.
- `--timeout 0` runs until you press Ctrl+C.

Which time to use: `ts` in the message is the hub's clock when it built the message (epoch seconds, UTC). Use `ts` differences for every interval measured "on the wire" (heartbeats, auto-clear, battery edges). Use the header or enqueued time only to prove when a message arrived (offline replay).

### 0.8 Sending C2D commands

**The envelope.** Every command in this plan uses the canonical envelope (`main/commands/c2d_commands.h`: `C2D_CMD_SCHEMA "eflostop.cmd"`):

```json
{"schema":"eflostop.cmd","ver":1,"id":"<test-id>-s<step>","cmd":"<command>","payload":{}}
```

Omit `payload` for commands that take none (`valve_open`, `valve_close`, `leak_reset`, `override_enable`, `override_cancel`).

**How to send.**
- **VS Code:** right-click `<GW>` → **Send C2D Message to Device** → paste the JSON **on one line** into the input box → Enter.
- **Azure CLI** (Git Bash): `az iot device c2d-message send -n wd-core-iothub-poc -d GW-7C4FADAE69C8 --data '<json>'`

**What the hub logs** when it gets a command (all verified in the code):

| Tag | Line | Meaning |
|---|---|---|
| `IOTHUB` | `Received C2D Message! (%d bytes)` (format) | The message arrived. |
| `C2D_CMD` | `Envelope cmd='%s' ver=%d id='%s' payload=%s` (format) | The envelope parsed. `payload=(none)` when there is no payload. |
| `IOTHUB` | `Command: VALVE_OPEN`, `Command: VALVE_CLOSE`, `Command: LEAK_RESET`, `Command: OVERRIDE_ENABLE`, `Command: OVERRIDE_CANCEL`, `Command: VALVE_SET_STATE -> open` … | The handler ran. |
| `TELEMETRY_V2` | `Pub event: {"schema":"eflostop.v2",...,"data":{"event":"cmd_ack",...}}` (format) | The `cmd_ack`. |

**The ack.** Every canonical-envelope command gets a `cmd_ack`:

```json
{"schema":"eflostop.v2","ts":<epoch>,"gateway":{"id":"<GW>","short_id":"<last 4>","fw":"2.1.4","uptime_s":<n>},"type":"event","data":{"event":"cmd_ack","id":"<your id>","cmd":"<command>","status":"ok"}}
```

On a refusal, `status` is `"error"`, and `data.error` is `{"code":"<command>","detail":"<exact text>"}`. `gateway.name` is present only when a hub name is set. The `detail` texts are exact; this plan quotes them verbatim.

**Rules for the `id`, and safety.**
- Use `<TESTID>-s<step>` in lower case, for example `dec-03-s4`. For the repeated-id tests, use exactly the id the test gives.
- **A well-formed canonical envelope is safe.** `c2d_command_parse()` tries `parse_envelope()` first; the legacy text parser `parse_legacy()` runs **only when that fails**: invalid JSON, or a `schema` other than `eflostop.cmd` / `eflostop.cmd.v1`, or no `cmd`. `parse_legacy()` upper-cases the whole text and does a substring scan, so the ordinary `"cmd":"decommission"`, `"valve_open"`, `"leak_reset"` … used in this plan are harmless inside a valid envelope, but dangerous inside a message that fails to parse. The known unfixed hazard (`project_c2d_legacy_wipe_hazard`, "legacy keyword scan" deferred in the CHANGELOG): a message with a broken `schema` or broken JSON that contains `DECOMMISSION_ALL` anywhere wipes the hub.
- **Never** send a message that fails to parse (smart quotes, truncation, a wrong `schema`) if it contains a legacy keyword, and **never** put `DECOMMISSION_ALL`, `DECOMMISSION_VALVE`, `DECOMMISSION_BLE:`, `DECOMMISSION_LORA:`, `VALVE_OPEN`, `VALVE_CLOSE`, `LEAK_RESET` or `OVERRIDE_CANCEL` (any case) in an `id`, label or name field.
- Paste JSON on one line with ASCII double quotes. A "smart quote" pasted from a word processor makes the envelope fail to parse, and then what happens depends on the text:
  - text with **no** legacy keyword: ignored, `C2D_CMD: Malformed C2D JSON — ignoring (not valid JSON)`, no ack;
  - a mangled `valve_open`, `valve_close`, `leak_reset` or `override_cancel`: **still executed** by `parse_legacy()` (subject to the valve guards), with no id and **no ack**: `IOTHUB: C2D cmd='valve_open' ver=0 id=''` (format `C2D cmd='%s' ver=%d id='%s'`);
  - a mangled message containing `DECOMMISSION_VALVE`, `DECOMMISSION_BLE:` or `DECOMMISSION_ALL`: **runs that decommission** (`all` wipes the hub).
  If you see `ver=0` in the `C2D cmd=` line, a message fell through to the legacy parser: stop and check what you pasted.
- The hub rejects any C2D message over 8,192 bytes (`MQTT_RX_MAX_MESSAGE`).
- `decommission` with `"target":"all"` wipes provisioning, sensor metadata, the hub name, the DPS cache and the rules state, then restarts after 3 s (`IOTHUB: Decommissioned — restarting in 3s...`). The next boot re-registers with DPS, which blocks `iothub_task` for up to 60 s per attempt (a known limitation). Send it only in the tests that call for it.

**Standard provisioning payloads** (used by the start states in 0.16):

Valve + 4 BLE sensors (`SS-V4`):
```json
{"schema":"eflostop.cmd","ver":1,"id":"ss-v4","cmd":"provision","payload":{"valve_id":"<VALVE_MAC>","ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"],"auto_close_enabled":true}}
```

With labels (optional; use them in section 9 so the app shows readable names):
```json
{"schema":"eflostop.cmd","ver":1,"id":"ss-v4m","cmd":"provision","payload":{"valve_id":"<VALVE_MAC>","ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE1>","location_code":"kitchen","label":"Sink"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE2>","location_code":"laundry","label":"Washer"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE3>","location_code":"bathroom","label":"Ensuite"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE4>","location_code":"bathroom","label":"Main"}],"auto_close_enabled":true}}
```

Sensors only, no valve (`SS-S2`):
```json
{"schema":"eflostop.cmd","ver":1,"id":"ss-s2","cmd":"provision","payload":{"ble_leak_sensors":["<BLE1>","<BLE2>"],"auto_close_enabled":true}}
```

Remove one BLE sensor, remove the valve:
```json
{"schema":"eflostop.cmd","ver":1,"id":"<test-id>-s<n>","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE1>"}}
{"schema":"eflostop.cmd","ver":1,"id":"<test-id>-s<n>","cmd":"decommission","payload":{"target":"valve"}}
```

### 0.9 The device twin: heartbeat interval

The snapshot heartbeat defaults to 300 s (`SNAPSHOT_INTERVAL_MS`). The twin's `snapshot_interval_s` sets it within 60–3600 s. A value outside that range is discarded with a warning, not clamped. The value is kept in NVS.

To make the heartbeat tests shorter, set it to 60 s before them:

```bash
az iot hub device-twin update -n wd-core-iothub-poc -d GW-7C4FADAE69C8 --desired '{"snapshot_interval_s": 60}'
```

In VS Code: right-click `<GW>` → **Edit Device Twin** → set `properties.desired.snapshot_interval_s` → save → **Update Device Twin**.

Expected UART: `IOTHUB: Twin desired patch: %s` (format), then `TELEMETRY_V2: Snapshot interval set to 60s (persisted)`. Twin reported then shows `"snapshot_interval_s": 60`.

Set it back to 300 at the end of the session. Note that `decommission` `all` resets the interval to the default by itself (`telemetry_v2_clear_settings()`), but the twin keeps its desired value, so the next twin GET sets it again.

### 0.10 Valve battery on the bench PSU

The valve reports its battery as a percentage (0x2A19). Its own bands (valve FW 2.2.0): above 20 % Good, re-read every **10 min**; 11–20 % Low, beeps, re-read every **60 s**; ≤ 10 % Critical, beeps, re-read about every **20 s**, auto-closes and refuses to open.

On this bench, with the PSU in place of the battery pack:

| PSU voltage | Valve reads | Hub 2.1.4 valve rating | `system_health.reason` | Fleet LED |
|---|---|---|---|---|
| 6.00 V | Good (> 20 %) | `excellent` | "All devices healthy" (if nothing else is wrong) | GREEN |
| about 5.45 V | Low (11–20 %) | `warning` | "Valve battery low" | YELLOW |
| about 5.35 V | Critical (≤ 10 %) | `critical` | "Valve battery critical" | RED |

Procedure and rules:
1. Set the current limit high enough that the motor stroke never drives the PSU into constant-current mode. A stroke in CC mode sags the voltage and gives a false reading. Watch the CC indicator during every open and close.
2. Change the voltage in one step, then **wait up to 10 min 30 s for the first new reading** while the valve is in Good (the 10 min cadence, plus the hub's 30 s health tick). Once it reads Low, readings come every 60 s (allow 70 s); at Critical, about every 20 s (allow 30 s).
3. The hub uses the **last real reading**. An unknown battery is `null` in the snapshot (or the key is absent while the valve is disconnected), never 0.
4. The exact switching voltages vary by valve. Before the first battery test, find this valve's Low and Critical points by stepping down 20 mV at a time from 5.55 V. Record both voltages on the results sheet, and use them instead of 5.45 V and 5.35 V.
5. Never go below 5.0 V. Never reverse the polarity.

### 0.11 Leak sensors: making them wet and dry, and their timing

- **Wet:** lay a wet paper towel across the probe pads. **Dry:** remove it and wipe the pads dry.
- A BLE sensor bursts about every **15 s while wet** and about every **100 s while dry**. The hub's BLE scanner forwards a changed value at once, and a telemetry heartbeat for an unchanged sensor every 5 min (`BLE_LEAK_HEARTBEAT_MS`). The sensor normally advertises the edge itself within a few seconds, but the worst case is the next burst. **One rule for the whole plan:** a wet edge must reach the hub (`BLE_LEAK: eleak ... leak=1`) within **20 s**, and a dry edge (`leak=0`) within **110 s** (one dry burst + 10 s). Measure every later interval (auto-clear, LED, events) from the hub's `leak=0` line, not from when you dried the probe. Any section that says "at once" or "about 15 s" for a dry edge uses these windows.
- A LoRa sensor reports on its own cadence. Record the cadence of your units, and use one full LoRa period plus 10 s as the tolerance.
- To make a sensor "offline", take its battery out. The hub declares it offline 600 s after it was last heard (0.15).

### 0.12 Reading the heap: the MONITOR line

The `MONITOR` task (`main/systemservices/monitoring.c`) logs every **10 s**:

```
I (123456) MONITOR: heap: free=33120 min_ever=19524 largest_blk=14336 uptime=123s
```

Its format is `heap: free=%lu min_ever=%lu largest_blk=%lu uptime=%lus` (format).

| Field | What it is | How to use it |
|---|---|---|
| `free` | `esp_get_free_heap_size()` now | Trend only. |
| `min_ever` | `esp_get_minimum_free_heap_size()`: the lowest free heap **since this boot** | This is the number for the heap criterion. It only goes down within a boot, so the **last** MONITOR line of a run is that run's minimum. |
| `largest_blk` | The largest free block (`MALLOC_CAP_DEFAULT`) | A snapshot needs about twice its printed size in **one** block (known limitation E-01). Record the lowest value you see. |
| `uptime` | Seconds since boot | Proves no reboot. |

At boot it logs `MONITOR: System monitoring started (interval=10s)`. It also warns:
- `MONITOR: LOW HEAP WARNING: %lu bytes free (watermark=8192)` (format) when free heap is under 8,192 B;
- `MONITOR: Heap dropped %ld bytes since last check` (format) when free heap fell by more than 4,096 B in 10 s. That happens normally during a TLS handshake or a DPS registration; record it, but it is not a failure by itself.

To pull the last MONITOR line out of a log (PowerShell):

```powershell
Select-String -Path .\<TESTID>_uart.txt -Pattern "MONITOR: heap:" | Select-Object -Last 1
```

To get the lowest `largest_blk` in a log:

```powershell
Select-String -Path .\<TESTID>_uart.txt -Pattern "largest_blk=(\d+)" | ForEach-Object { [int]$_.Matches[0].Groups[1].Value } | Measure-Object -Minimum
```

**Like-for-like.** A heap figure can only be compared with one taken on the same hardware, with the same devices provisioned, at the same point in the same sequence, and over the same length of run. Reference figures: the 2.1.3 field unit reached `min_ever` 2,972 B during commissioning; the 2.1.4 CP1 bench image (valve + 4 BLE) reached 19,524 B; an empty hub before BLE starts was 91,048 B. These are **not** like-for-like with each other. The release criterion is measured by VAL-13.

### 0.13 LED legend

**Fleet / system-status LED (GPIO 48, `main/rgb/fleet_led.c`).** It shows one solid colour, re-evaluated every 250 ms. The first rule that matches, from the top, wins:

| Order | Condition | Colour (R,G,B) | UART `reason` field |
|---|---|---|---|
| 0 | Health engine not up yet (first ~3 s of boot) | OFF | `startup` |
| 1 | No device provisioned | **WHITE** (25,25,25) | `unprovisioned` |
| 2 | System rating `critical`: a leak anywhere, a device offline past its grace, **valve battery ≤ 10 %**, or a valve never heard after its grace | **RED** (50,0,0) | `critical` |
| 3 | System rating `warning`: a sensor's low battery or weak signal, **valve battery 11–20 %**, the valve disconnected inside its 180 s grace, or the interlock still latched after a leak dried ("Leak interlock latched") | **YELLOW** (50,35,0), reads amber | `warning` |
| 4 | Some provisioned device has not been heard yet (sync window) | **WHITE** | `syncing` |
| 5 | Rating `excellent` or `good` | **GREEN** (0,50,0) | `excellent` or `good` |

Every colour change logs one line: `FLEET_LED: rating=%s color=%s effect=SOLID` (format). For example `FLEET_LED: rating=critical color=RED effect=SOLID` or `FLEET_LED: rating=unprovisioned color=WHITE effect=SOLID`. The production tool matches this exact form. Use the UART line as the evidence, and confirm the colour by eye.

A leak therefore reads **RED while wet → YELLOW for about 10–12 s once every source is dry (the interlock is still latched) → GREEN once the auto-clear releases it**. The valve stays closed through all three.

**Network status LED (GPIO 38, `main/net_status/net_status.c`)**, for reference: ramping red = no Wi-Fi (`LED_CMD_NO_INTERNET`), beating blue = Wi-Fi up but not connected to IoT Hub (`LED_CMD_CONNECTING`), ramping blue = connected (`LED_CMD_CONNECTED`).

### 0.14 Log line conventions, and the boot lines to check on every flash

ESP-IDF log lines look like `I (12345) TAG: text` (`I` for info, `W` for warning, `E` for error). This plan quotes lines as `TAG: text`. A line marked **(format)** has printf fields (`%d`, `%s` …) that change from run to run; match its fixed text only. Where the firmware prints a line only under a timing condition, the test says so. Some lines contain an em dash (`—`) and some a hyphen (`-`), exactly as the code has them. Search for a distinctive part of the line rather than the whole line.

After **every** flash or reboot, check these lines (in boot order) and tick them on the results sheet:

| Line | Where it comes from |
|---|---|
| `app_init: App version:      2.1.4` (ESP-IDF banner; the spacing may differ) | ESP-IDF, from `PROJECT_VER` |
| `HUB_IDENT: Firmware version: v2.1.4` | `hub_identity.c` (the production tool matches this) |
| `HUB_IDENT: Gateway ID : GW-7C4FADAE69C8` (format) | `hub_identity.c` |
| `HUB_IDENT: WiFi STA MAC: 7C:4F:AD:AE:69:C8` (format) | `hub_identity.c` |
| `PROVISIONING: Loaded existing config from NVS`, then `PROVISIONING: State: PROVISIONED` (format), `PROVISIONING: Valve MAC: %s`, `PROVISIONING: LoRa sensors: %d`, `PROVISIONING: BLE leak sensors: %d`, `PROVISIONING: Rules: auto_close=%s triggers=0x%02X` (format) | `provisioning_manager.c`. On a never-provisioned hub: `PROVISIONING: No existing config found, starting UNPROVISIONED` |
| `HEALTH_ENGINE: Device table loaded: %d device(s) (+%u added, -%u removed)` (format) | `health_engine.c` (a bench anchor) |
| `IOTHUB: Starting BLE (valve=%s, BLE sensors=%u)` (format), printed **before** `APP_WIFI: Connected! IP: %s` | `app_iothub.c`. Printed only when a valve or at least one BLE sensor is provisioned (P0-b). |
| `IOTHUB: Boot: hub is empty - clearing any persisted rules-engine state` | Only on a hub with no devices |
| `BLE_VALVE: [INIT] Signal received. Starting BLE stack...` (exactly once per boot) and `BLE_VALVE: [HOST] NimBLE host task started` | `app_ble_valve.c` |
| `BLE_VALVE: [SM] Fixed Passkey: configured (not logged)` | The passkey itself must never be printed. |
| `APP_LORA: Initializing LoRa Driver...` | `app_lora.cpp` (production tool) |
| `MONITOR: System monitoring started (interval=10s)` | `monitoring.c` |
| `IOTHUB: Connected to Azure IoT Hub!` | `app_iothub.c` |
| `TELEMETRY_V2: Pub lifecycle: {...}` (format) | The lifecycle `online` message |

How to see what the hub published: every message that goes out live is printed as `TELEMETRY_V2: Pub snapshot: {...}`, `TELEMETRY_V2: Pub event: {...}` or `TELEMETRY_V2: Pub lifecycle: {...}` (format `Pub %s: %s`). Events replayed from the offline buffer do **not** print `Pub event:`; they print `OFFLINE_BUF: Replayed [%s] (%u bytes)` (format), so check those in the IoT Hub capture.

### 0.15 Timing constants and tolerances

All the figures below come from the code at `d9fa9c8`. A test's expected time is "nominal (tolerance)". Pass if the measured time falls inside the tolerance window. When a measurement is outside the window by less than 2 s, and the log shows the hub was busy at that moment (a DPS registration, a TLS reconnect, a burst of C2D commands), record it as `Pass` with a note; otherwise it is `Fail`.

| What | Constant (file) | Nominal | Tolerance window to use |
|---|---|---|---|
| Heartbeat snapshot | `SNAPSHOT_INTERVAL_MS` 300 s, twin `snapshot_interval_s` 60–3600 (`telemetry_v2.h`) | interval, measured from the **previous snapshot of any reason** (every confirmed publish re-arms it) | interval −1 s / +3 s, as a `ts` difference |
| Event snapshot after a rating change or a command | `SNAP_HIGH_WINDOW_MS` 300 ms, `SNAP_LOW_WINDOW_MS` 2 s, `SNAP_MIN_INTERVAL_MS` 5 s (`app_iothub.c`) | ≤ 2 s | ≤ 7 s after the trigger (allow 10 s when another snapshot went out in the 5 s before) |
| Snapshot retry after a failed publish | `SNAP_RETRY_FLOOR_MS` 5 s | 5 s | 5–7 s |
| Fast boot snapshot, ceiling | `SNAP_FAST_CEILING_MS` 150 s | ≤ 150 s after boot | ≤ 155 s |
| Boot sync snapshot gate | `HEALTH_BOOT_SYNC_TIMEOUT_MS` 180 s | 180 s after boot | 180–185 s |
| Commission sync window after a `provision` | `HEALTH_COMMISSION_SYNC_TIMEOUT_MS` 150 s | 150 s | 150–155 s |
| Per-device "not heard yet" grace (Syncing, then counted) | `HEALTH_ROLLUP_UNHEARD_MS` 600 s | 600 s after the device was added or the hub booted | 600–605 s ("Roll-up grace expired" is evaluated on read) |
| Commission refresh grace | `COMMISSION_REFRESH_GRACE_MS` 6 min | 360 s | — |
| Post-provision snapshot pulse | `PROV_PULSE_WINDOW_MS` 5 min, `PROV_PULSE_PERIOD_MS` 30 s, `PROV_PULSE_MAX_SNAPS` 40 | every 30 s, plus one per sensor packet, for 300 s | at most 40 snapshots |
| Valve disconnect grace (warning "Valve disconnected", then critical "Valve offline" + `device_offline`) | `HEALTH_VALVE_DISC_TIMEOUT_MS` 180 s; health tick `HEALTH_TICK_INTERVAL_MS` 30 s | 180 s after the link drop | 180–215 s |
| BLE / LoRa sensor offline (`device_offline`) | `HEALTH_BLE_LEAK_TIMEOUT_MS`, `HEALTH_LORA_TIMEOUT_MS` 600 s | 600 s after the last packet heard | 600–635 s |
| Health alert debounce | `HEALTH_ALERT_DEBOUNCE_MS` 60 s | ≥ 60 s between alerts for one device | — |
| RMLEAK auto-clear after every source is dry | `AUTO_CLEAR_TIMEOUT_MS` 10 s; 2 s loop poll while pending | 10 s after the last dry report | `rmleak_auto_cleared` 10–12 s after it (allow 14 s end to end in the IoT Hub capture) |
| Auto-close cooldown | `AUTO_CLOSE_COOLDOWN_MS` 10 s | — | — |
| RMLEAK read-back grace | `RMLEAK_GRACE_PERIOD_MS` 5 s | — | — |
| Override window | `OVERRIDE_WINDOW_DURATION_S` 86400 s | `remaining_s` 86400 at the start | `override_remaining_s` in a snapshot = 86400 − elapsed, ±2 s |
| `override_enable` bounded reconnect | `OVERRIDE_CONNECT_TIMEOUT_MS` 10 s | — | ack within 12 s |
| "blocked by override" event rate limit | `OVERRIDE_BLOCKED_COOLDOWN_MS` 60 s | ≤ 1 per minute | — |
| `iothub_task` idle | 30 s idle cap; 2 s while an auto-clear, a device-set change or a BLE apply is pending | — | — |
| BLE scanner whitelist reload | `WHITELIST_RELOAD_MS` 10 s | — | — |
| BLE sensor telemetry heartbeat | `BLE_LEAK_HEARTBEAT_MS` 5 min | — | — |
| MONITOR heap line | `MONITORING_INTERVAL_MS` 10 s | 10 s | 10 s ±1 s |
| SNTP | initial window 120 s (2 s poll), then a 60 s retry timer | — | `SNTP initial sync failed — starting 60s retry timer` about 120 s after boot with NTP blocked |
| DPS registration (no DPS cache only) | — | up to 60 s per attempt, blocking `iothub_task` (known limitation) | — |
| `decommission` `all` restart | 3 s | 3 s after the ack | 3–5 s |
| Offline buffer | `OFFLINE_BUF_MAX_ENTRIES` 16, `OFFLINE_BUF_MAX_JSON_LEN` 512 B | — | — |
| SAS token | `SAS_TTL_SEC` 24 h, `SAS_RENEW_MARGIN_SEC` 6 h | renewed at about 18 h uptime (`IOTHUB: SAS: token renewed (valid %d h, expires ts=%ld)`, format) | — |
| cmd_ack after the C2D arrives | — | < 1 s | ≤ 5 s (≤ 12 s for `override_enable`) |
| MQTT keepalive (WAN-only outage detection) | `cfg->session.keepalive = 60` (`app_iothub.c`) | esp-mqtt can take up to about 2× keepalive to notice a WAN-only outage (Wi-Fi still associated) | `IOTHUB: Disconnected.` within **≤ 120 s** of unplugging the WAN. Keep the WAN out for **at least 3 min** in any test that needs the drop. "No `Pub snapshot`" rules apply only **after** the drop is logged (`IOTHUB: Disconnected.` and `TELEMETRY_V2: MQTT connected = false`); a publish before that is not a failure. |
| Portal priority window (Wi-Fi setup portal with no credentials saved) | `PORTAL_TASK_PRIORITY` 8 and `PORTAL_AP_STOP_MARGIN_MS` 15 s (`app_wifi.c`); `CONFIG_WIFI_MANAGER_SHUTDOWN_AP_TIMER` 60 s (`sdkconfig`); `wifi_task`'s 5 s loop; `PORTAL_POLL_MS` 1 s (`app_ble_valve.c`); the scan task's 500 ms loop (`app_ble_leak.c`); `HEALTH_BLE_LEAK_TIMEOUT_MS` 600 s, `HEALTH_VALVE_DISC_TIMEOUT_MS` 180 s and the 180 s gate, all from the resume (`health_engine.c`) | opens at the SoftAP start, about 0.7 s after boot; no time cap before setup; closes when the setup SoftAP stops, 60 s after `Connected! IP`, or at once if that Wi-Fi is lost first; the safety net stops the SoftAP 75-80 s after `Connected! IP` | `portal priority OFF (AP stopped)` 60 s (±5 s) after `Connected! IP`, never later than about 80 s; the leak scanner reacts ≤ 0.5 s and the valve task ≤ 1 s after a window edge; a BLE sensor's `device_offline` or `Roll-up grace expired` no earlier than 600 s after `portal priority OFF`, the valve's no earlier than 180 s; with a BLE sensor or the valve still unheard, `Boot sync: timeout` 180–185 s after it |
| Valve battery reading (valve FW) | — | Good 10 min, Low 60 s, Critical ~20 s | first reading ≤ 10 min 30 s after a PSU change; then ≤ 70 s (Low), ≤ 30 s (Critical) |

### 0.16 Test IDs, priorities and standard start states

**Test ID prefixes:**

| Prefix | Area |
|---|---|
| `DEC-` | Section 1: decommission, UI sync and the empty hub (BUG-2, BUG-3, BUG-5, BUG-6, decommission-all, re-provision) |
| `T2-` | Section 2: valve battery (BUG-1, unknown battery `null`, the valve-only bands, the battery refusal) |
| `T3-` | Section 3: P0 safety and valve lifecycle (P0-a, P0-b, P0-c, the `valve_open` refusals, RMLEAK/CLOSE ordering, F-01, B1, the valve swap) |
| `T4-` | Section 4: protection before Wi-Fi (N1-N4), pre-sync events, the override window before the clock syncs, the captive portal |
| `T5-` | Section 5: regression of 2.1.3 behaviour and the new rules behaviour (provision and sync, leak -> auto-close -> reset, override, twin, offline replay) |
| `T6-` | Section 6: robustness, upgrade and heap (power cuts, repeated and unknown ids, heap, stack, sustained RF stress, council residual risks) |
| `VAL-` | Section 9: validation (build gate, upgrade and rollback, app/UI end to end, heap and soak gates, validator) |

IDs are numbered `NN` within a prefix (`DEC-01`, `T2-01`, …). The ~30 min smoke run is section S. A "Smoke" note inside sections 1-6 only marks a part that section S borrows or that its author suggested; section S is the list to run. **P0** marks a test that verifies a bug fix, a P0 safety fix or a council-blocking item. Every P0 test must pass for release (9.4).

**Standard start states.** A test that names one of these starts from it:

| ID | State | How to reach it |
|---|---|---|
| `SS-EMPTY` | 2.1.4, no devices, clock synced, MQTT connected | From any state, decommission each device (0.8), or `decommission` `all` if the test allows it (this costs a DPS re-registration). Check the snapshot `data.valve` is `{}`, both arrays are `[]`, and the LED is WHITE (`rating=unprovisioned`). |
| `SS-V4` | Valve A + `<BLE1>`…`<BLE4>` provisioned, all heard, all dry, valve open, battery Good, no incident, no override | From `SS-EMPTY`, send `ss-v4` (0.8); open the valve if it is closed (`valve_open`). Wait for `system_health.reason` "All devices healthy" and the LED GREEN. |
| `SS-V4L` | `SS-V4` plus `<LORA1>` | Add `"lora_sensors":["<LORA1>"]` to the `ss-v4` payload. |
| `SS-S2` | `<BLE1>` and `<BLE2>` only, no valve; valve B powered nearby | From `SS-EMPTY`, send `ss-s2`. |
| `SS-213-V4` | Hub on **2.1.3** (`ae4d59a`) with the `SS-V4` devices | Flash 2.1.3 (0.4), then provision as for `SS-V4`. |

Before each test, also set: snapshot interval 60 s unless the test says otherwise (0.9); the router on; the UART log (0.6) and the IoT Hub capture (0.7) running.

### 0.17 Recording results

Each test has a result table with a **Result** cell and a **Notes** cell. Use exactly one of these results:

| Result | Meaning |
|---|---|
| `Pass` | Every expected line, message, LED state and timing was seen, and nothing forbidden was seen. |
| `Fail` | An expectation was not met, or a forbidden line or message appeared. Write down what you saw, the log timestamps, and whether it repeats. |
| `Blocked` | The test could not run (missing hardware, a failed precondition, the app not available). Write the reason. A Blocked P0 test blocks the release just as a Fail does. |
| `N/A` | Only where a test says so (for example `N/A (no LoRa HW)`, or a valve build without the Battery Service in T2-06 Part B). For the LoRa-packet tests it needs the written EC-2 waiver (9.4); without it, the result is `Blocked`. |
| `Known-limit` | Only for the steps marked **observe and record**: a documented known limitation (CHANGELOG 2.1.4 "Known limitations", or F-02 / F-03) behaved as documented. Record what happened. A Known-limit step fails only if the behaviour is **worse** than documented (for example the valve opened by itself, or an override window lasted more than 24 h). |

**Merged tests.** A few tests are run as another test, to avoid running the same procedure twice (the list is in "How to use this document"). Their entry says which test to run. Record that test's result under both IDs.

**A known limitation that looks like a section-wide Fail.** A hub name set for the first time while a snapshot is being built fails that one snapshot with `TELEMETRY_V2: Snapshot not built - out of memory` (CHANGELOG, 2.1.5 list). If that line appears within 5 s of a first `set_hub_name` and the 5 s retry publishes the snapshot, record `Known-limit`, not `Fail`.

**Evidence, for every test:**
- the UART log `<TESTID>_uart.txt` and the IoT Hub capture `<TESTID>_iothub.txt`, or one session capture with the test's time range noted;
- the validator output for the capture (0.18);
- a photo of the fleet LED for every step with an LED expectation (a phone photo is enough; name it `<TESTID>_s<step>_led.jpg`);
- for section 9, a screenshot of the app for each UI step.

Suggested location (not in the repo, unless you decide to commit it later): `…\2.1.4_bench\<date>\<TESTID>_*`. Record the hub Gateway ID, the firmware commit, the valve and sensor FW versions and the app version at the top of the results sheet.

### 0.18 Running the capture validator

`docs/telemetry/validate_capture.py` pulls every `eflostop.v2` JSON object out of a capture (the VS Code or `az` IoT Hub output, or a UART log with `Pub ...:` lines) and checks each one against the 2.1.4 contract: the `fw` is 2.1.4, the key shapes and types, the valve `{}` rule, the ratings and reasons, the identity keys, retired keys, `ts` ≥ 1704067200, cause-before-consequence ordering, and no `auto_close` from a hub whose last snapshot showed no valve. It uses the Python standard library only.

```powershell
python docs\telemetry\validate_capture.py ".\<TESTID>_iothub.txt"
```

Read the output:
- a block `--- message N  (<type> / <event>)` followed by `FAIL  <reason>` lines for each failing message;
- `--- ORDERING (cause before consequence) ---` if an ordering rule was broken;
- `--- AUTO_CLOSE FROM A HUB WITH NO VALVE ---` if a hub with `valve:{}` sent `auto_close`;
- the summary line `N messages checked, P pass, F fail` (plus the ordering and auto-close counts when they are not 0);
- `message mix:`, a count per message type;
- `not exercised by this capture (cannot be validated from it):` for types it did not see. That is information, not a failure.

The exit code is 0 only when there is no FAIL at all.

Rules:
- Run it on the **IoT Hub** capture. A UART log is accepted, but it misses every event replayed from the offline buffer (those do not print `Pub event:`).
- The validator takes one file per run; run it once per capture file.
- A capture that contains 2.1.3 messages (VAL-02, VAL-03, `SS-213-V4`) fails the `fw` check for those messages. Cut those messages out, or accept only the FAILs that say the `fw` is not 2.1.4.
- One false positive is known (HANDOFF §10): the no-valve `auto_close` check can flag a leak that happened while provisioning was busy. If you see `AUTO_CLOSE FROM A HUB WITH NO VALVE`, check the UART at that time. If it shows a provisioning-busy line, annotate the result; otherwise it is a real failure.

## M. Traceability matrix

Every requirement of this release maps to the tests that verify it (a normal Pass/Fail test) or observe it (a known limitation, result `Known-limit`). "Main" tests carry the pass criterion; "also" tests check the same thing on the way. A test that is **run as** another test (see the merged-tests list) is traced under both IDs. M.7 lists every test ID with what it covers; M.8 lists what has no test.

### M.1 Field defects of 2.1.3

| Item | What 2.1.4 must do | Main tests | Also |
|---|---|---|---|
| BUG-1 | The valve battery rates the valve and the hub: ≤ 10 % `critical` "Valve battery critical", 11-20 % `warning` "Valve battery low", > 20 % `excellent`; valve only (sensor bands unchanged, never critical); an unknown battery is `null` (absent while disconnected), never 0; a battery edge publishes an `event` snapshot, and no health event | T2-01, T2-02, T2-03, T2-06, T2-07, VAL-08 | T2-04, T2-05, T2-09 (sensor bands), VAL-12, smoke step 7 |
| BUG-2 | Removing one device leaves every other device's state alone (no `null`s, no "Syncing", no `Boot sync: timeout`, no new pulse or `boot`/`commission`); the UI sync after an adding `provision` still works for unheard devices only | DEC-03, DEC-04, DEC-05, DEC-06, DEC-01, T5-01, VAL-05 | VAL-04, T6-01, T6-05 step 3, smoke step 6 |
| BUG-3 / BUG-6 | A hub emptied by removals publishes one `event` snapshot of the empty shape, then heartbeats, one `boot` per boot or MQTT connect, lifecycle and twin with `provisioned:false`; `decommission` `all` publishes a final empty `decommission` snapshot | DEC-11, DEC-12, DEC-13, DEC-17, DEC-18, VAL-06 | DEC-16, T6-02, smoke step 9 |
| BUG-5 | `data.valve` is `{}` when no valve is provisioned | DEC-09, VAL-06, VAL-07, T3-01 | DEC-14, VAL-12, T6-09, T6-12 |

### M.2 Approved P0 safety fixes, latent fixes and user decisions

| Item | What 2.1.4 must do | Main tests | Also |
|---|---|---|---|
| P0-a | Link to, pair with and command only the provisioned valve MAC (identity address); never a neighbour | T3-01, T3-05, T3-19, DEC-09, VAL-07 | T3-07, VAL-04, T6-11, smoke step 8 |
| P0-b | BLE starts when a valve **or** a BLE sensor is provisioned; a sensors-only hub hears its sensors | T3-02, VAL-07, T6-09 | T4-01 (variant), T6-12, T6-10 run 3 |
| P0-c | Valve commands with no valve are refused; commands queued for an old target are flushed and never replayed on the next valve | T3-03, T3-04, T3-05, DEC-09, VAL-07 | T3-01 |
| N1 | Provisioning, rules, health, the offline buffer and BLE start at boot, before the Wi-Fi IP; SNTP and DPS run from the loop | T4-01, T4-02, T4-05, T4-06 | T6-03 step 9, T6-08, T4-10 A3, smoke step 2 |
| N2/N3 | The QueueSet is complete, and every add checked, before BLE starts | T4-01, T3-02 | — |
| N4 | LoRa packets from sensors that are not provisioned never reach the rules engine | T3-08, T4-13, DEC-08 variant A (all need LoRa hardware) | T5-13 (control); DEC-08 variant B (provision/removal path with no radio). Without LoRa hardware: EC-2 LoRa exception (written waiver) |
| Pre-sync events | Events raised before the first clock sync are held, stamped at the sync, replayed in order before the lifecycle; one left by a restart before the sync is dropped; `ts` is never below 1704067200 | T4-02, T4-03, T4-04, T4-05 | T3-13 step 3, T4-10 A3/A8, VAL-12, VAL-15, smoke steps 2-3 |
| Override clock | A window started before the sync is timed on uptime, has no `expires_ts`, is re-based within about 30 s of the sync, and expires on uptime with no internet; a real-epoch window restored after a power-on with no clock is timed from the power-on | T4-07, T4-08, T4-09 | T6-03 run A, VAL-12 |
| Boot empty | A hub that boots with no devices clears any persisted leak latch and override | T6-02, DEC-18, VAL-06 step 4 | DEC-12, T4-06 |
| User decision: no `auto_close` without a valve | A leak on a hub with no provisioned valve publishes `leak_detected` only (the latch and `rmleak_auto_cleared` without `valve_id` are unchanged) | T3-01, T3-03, DEC-09, DEC-11, VAL-12 | T3-02, T6-12, VAL-15 (validator rule), smoke step 8 |
| User decision: `valve_open` refused while locked | `valve_open` and `valve_set_state` open refused with the RMLEAK detail while RMLEAK is set or an incident is latched with no override, even with the valve disconnected (E-06) | T3-09, T5-05, VAL-09 | T3-10, T4-11 A3, T4-12 row 3, T6-03 run B, DEC-07 |
| User decision: battery refusal | `valve_open` refused at a last real battery ≤ 10 %, also disconnected; refusal order no valve → RMLEAK/incident → battery → queue | T2-01, T2-05, VAL-08 | smoke step 7 |
| User decision: no valve | Every valve command and `override_enable` on a hub with no valve: "No valve is set up for this hub."; `decommission` `valve` there: "valve decommission failed" | T3-01, T3-04, DEC-09, DEC-10, VAL-07 | T6-06 |
| User decision: valve age | `data.valve.last_seen_age_s` is 0 while linked and counts from the drop; `device_offline.offline_duration_s` is from the drop (about 180) | T2-08, T2-03, VAL-11 | T2-02, T2-04, T5-03, T3-04, T3-06, T3-11, T3-17 |
| User decision: 10 s auto-clear | RMLEAK auto-clears 10 s after every source is dry (lands 10-12 s), `clear_after_seconds:10`; the valve stays closed; LED RED → YELLOW → GREEN | T5-03, T4-02, T3-11, VAL-09 | T3-01, T5-12, T5-13, T6-15, DEC-06, smoke step 2 |
| Queue-full refusal | "The valve command could not be queued. Try again." | T6-17 Part B | — |
| Valve write reliability (G4c) | Retries; GATT busy (rc=6) waited out and replayed, never a forced relink; forced relinks capped at 3; replays ahead of newer commands; RMLEAK before the valve command on every path, including across the end of setup; a command held at setup replayed at once; `nimble_port_init` retried; BLE start claimed atomically | T3-16, T3-17, T5-10, T6-16, T6-17, T6-20 | T3-10, T5-03, T5-06, T5-09, T4-08, T4-10 B4, DEC-13, T6-11 |
| Robustness (G4d) | Offline buffer under a mutex, too-large events refused, cleared after decommission all; provisioning saves transactional; valve apply under one lock hold; scanner forgets on an adding change; purge retried; twin refreshed after every device-set change; snapshots never partial; log hygiene | T5-12, T6-14, DEC-18, T6-01, DEC-07, T4-14 | T3-05 Part C, T5-01, DEC-03, DEC-14, T6-10; untraced parts in M.8 |
| Health reliability | A debounced alert is sent late, not dropped; a rating change into, out of or within warning/critical publishes an `event` snapshot within seconds; valve health events carry their link's MAC | T2-04, T2-02, T5-03, T3-05 | DEC-04 (N10) |

**Approved plan latent-fix IDs, one row each** (plan `happy-floating-boole.md`; the grouped rows above, "Valve write reliability (G4c)" and "Robustness (G4d)", are these IDs together). IDs already named elsewhere in M (N1-N4, N10, N12, L9, L17) keep their rows there.

| ID | What 2.1.4 must do | Main tests | Also / how checked |
|---|---|---|---|
| N6 | A target change or clear disconnects a link that is not to the (new) target | DEC-09, T3-05 | T3-03 |
| N7 | A target change or clear flushes the valve command queue, the pended commands and the in-flight count (P0-c) | T3-03, T3-05, DEC-09 | T3-04 |
| N8 | `BLE_CMD_DISCONNECT` also cancels a connect in progress (`ble_gap_conn_cancel()`) | T3-05, DEC-09 | T3-03 |
| N13 | Duplicate ids in one `provision` payload are ignored: BLE MACs in any case, LoRa ids as parsed values (`... duplicate in payload - ignored`) | DEC-19 | — |
| N17 | If the health-table copy fails (lock busy), no snapshot is published (retry floor); never a snapshot with empty arrays | Section-wide fail rule: any snapshot whose device arrays are empty while devices are provisioned is a Fail in every test (B.2) | T6-10 |
| N18 / L17 | Offline buffer under a static mutex; an event over 512 B refused, not truncated; `offline_buffer_clear()` on `decommission` `all` | DEC-18, T6-14 | T5-12 |
| N22 | `rules_engine_reset_all()` resets RAM **and** NVS under the rules lock (empty hub, decommission all) | DEC-11, DEC-18 | DEC-12 block A, T6-02 |
| L7 | `rules_engine_forget_unprovisioned()`: a removed wet sensor leaves the active-leak set, so the all-clear timer starts | DEC-06 | DEC-11 |
| L8 | BLE scanner state keyed by MAC; whitelist swap and prune under a critical section; old list kept on a timeout | DEC-07 | DEC-03, DEC-13 |
| L10 | `sync_valve_detectors()` presets the valve publish state on a valve change or removal: no duplicate `valve_unlinked` | DEC-09 (no `valve_unlinked` after the removal) | T3-05 |
| L13 | Provisioning removals restore RAM if the NVS save fails; `provisioning_decommission` erases NVS before RAM | T6-01 | T6-02 |
| L14 | Every container `cJSON_Create*` NULL-checked; on failure no partial publish and no leak | — (M.8 #5, allocation fault needed) | T6-10 (indirect) |
| L15 | Device ratings and the syncing count sampled under the same lock | DEC-04 step 6 (the grace-expiry snapshot's rating and "Syncing" agree) | DEC-01 |
| L16 | `sensor_meta_get()` copies metadata out under its mutex | — (M.8 #9, internal) | every labelled snapshot |
| L18 | `wire_device_id` prefers the **provisioned** valve MAC in rules events | T3-05, T3-06 | T5-03 |

**Phase E findings (round 1)**

| Finding | Subject | Main tests | Also |
|---|---|---|---|
| E-01 | Snapshot needs about twice its size in one block (known limitation) | T6-10 (observe) | VAL-13, VAL-14 (`largest_blk`) |
| E-02 | Scanner delta state on remove and re-add within 10 s | DEC-07 | — |
| E-03 | Restored override window timed by the wrong rule | T4-09 | T6-03 run A |
| E-04 | Old valve's leak source survives a valve replacement | T3-06 | — |
| E-05 | Empty-hub rules reset ordered against a following `provision` | DEC-11, DEC-16, DEC-17 | VAL-12 |
| E-06 | A pended OPEN written ahead of a leak's close | T3-10, T3-09 | T5-05, VAL-09 |
| E-07 | A command pended at setup completion stranded on a live link (disputed) | T3-17 Part B | T6-17 |
| E-08 | Valve health events from the old valve after a swap | T3-05 | — |
| E-09 | Quick decommission and re-provision of the same valve never rescanned | T3-07, DEC-14, DEC-16 | T5-01, DEC-02 |
| E-10 | A snapshot between a C2D change and the reconcile shows the old table | DEC-14, DEC-03, DEC-13 | T5-01 |
| E-11 | `auto_close` claims `rmleak_asserted:true` for a just-removed valve | T3-03 | — |
| E-12 | Valve re-check can mark a wet valve dry | — (see M.8) | — |
| E-13 / E-14 | cJSON add failures leak or ship a partial snapshot | — (see M.8) | T6-10 (indirect) |
| E-15 | Rating-change snapshots on every excellent/good flip | T2-09, T5-03 | — |
| E-16 | A connected valve's `last_seen_age_s` grew without bound | T2-08, VAL-11 | T2-02, T2-03, T2-04, T5-03 |
| E-17 | Owed device-set work does not always converge | DEC-07, DEC-13 | — |
| E-18 | Check-ins from devices not in the table drive the pulse | DEC-08 variant A, T3-08 (LoRa hardware needed for the exact check) | DEC-04 (BLE, observe); without LoRa hardware: EC-2 LoRa exception (written waiver) |
| E-19 | Reconnect `device_recovered` carries the fresh battery | T2-03 | VAL-12 |
| E-20 | Heap budget (static RAM and two log-level nodes) | VAL-01, VAL-13 | T6-07 |
| E-21 | Captive portal no longer BLE-free: NimBLE is up beside it, and BLE scanning pauses while no Wi-Fi credentials are saved (portal priority window, 2026-09-29) | T4-10 | T6-13 (= T4-10) |
| E-22 | Rollback: stamped pre-sync entries over 512 B; unstamped ones | T4-03, VAL-03 (= T6-04) | — |

**Portal priority window (the 2026-09-29 captive-portal fix, commits `5b5d70e` … `ca4835f`, and its follow-up `93b8629` … `cc66d72`)**

| Item | What 2.1.4 must do | Main tests | Also |
|---|---|---|---|
| Window opens only with no credentials | `HEALTH_ENGINE: BLE scanning paused - BLE sensor timeouts held`, `APP_WIFI: portal priority ON (no Wi-Fi credentials) - BLE scanning paused` and `portal priority: wifi_manager task prio 5 -> 8 (httpd, dns_server not raised)` at the SoftAP start after the 10 s reset or at first setup; never after a boot with saved credentials | T4-10 A3, E1, F1 | T4-10 A11, D5, H1, T6-14 step 3, smoke step 10 |
| 10 s reset always erases (follow-up) | `RESET_BTN: Wi-Fi credentials erased from NVS` before `RESET_BTN: Rebooting into AP mode...` in every Wi-Fi state, with or without `WiFi Disconnected. Reason: 8`; after a reset on the router-outage fallback portal the hub boots into the no-credentials portal (window lines ≤ 1 s after boot), and the phone can set it up again | T4-10 D5, D6 | T4-10 A2, T6-14 step 3, smoke step 10 |
| Phone joins the portal, valve hub | `APP_WIFI: SoftAP: station … joined, AID=%u`, then `esp_netif_lwip: DHCP server assigned IP to a client` within 5 s, DNS replies, the page loads and saves; valve powered (A) and powered off (B) | T4-10 A4, B1 | T6-14 step 3, smoke step 10 |
| Phone joins the portal, sensors-only hub | the same on the 2026-09-29 unit (`GW-7C4FADAE69C8`, 2 BLE sensors, no valve) | T4-10 F2 | — |
| SoftAP station log | `SoftAP: station … joined, AID=%u` and `… left, AID=%u, reason=%u` on any SoftAP | T4-10 A4, F2 | T4-10 D3 |
| Scanning paused in the window | no `Extended passive scan started` and no `[SCAN] Starting scan for provisioned valve` between `portal priority ON` and `OFF` (the minute after `Connected! IP` included), except the leak-response hunt; NimBLE stays up; a linked valve stays linked | T4-10 A3, A8, A10, F1 | T4-10 G1, G4 |
| Leak response outranks the portal | a LoRa leak in the window hunts and links an unlinked valve (`[PORTAL] Leak response pending …`) and closes it RMLEAK first; that hunt stops when the close is withdrawn (`[PORTAL] Valve hunt stopped …`) | T4-10 A6, B2-B4 | T4-10 G2 |
| Router-fallback AP keeps BLE | `SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`, **no** `portal priority ON`, scanning and a BLE leak close continue | T4-10 D1, D2 | T4-02 row 1, T5-12 step 1, T4-10 H3 |
| Window closes when the setup AP stops (follow-up) | `Connected! IP`, then `portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops (about 60 s)`; 60 s later `BLE scanning resumed - BLE sensor timeouts restart now (600 s)` and `portal priority OFF (AP stopped) - BLE scanning resumed`; the leak scan and the valve hunt resume within 1 s; no `setup AP still up` | T4-10 A8, F4 | T4-10 D6, E4, G3, G5, T6-14 step 3, smoke step 10 |
| Success page after the save (follow-up) | the phone, still on the SoftAP, shows the portal's "Connected!" page before the SoftAP stops: valve linked (A8), valve not linked (E4, smoke step 10), sensors-only hub (F4) | T4-10 A8, F4 | T4-10 D6, E4, smoke step 10 |
| Window closes when Wi-Fi is lost after setup (follow-up) | the STA drops before the SoftAP stops: `portal priority OFF (Wi-Fi lost after setup) - BLE scanning resumed` ≤ 1 s after `WiFi Disconnected`; the SoftAP stays up with BLE scanning; no `portal priority ON` | T4-10 H3 | T4-10 H4 |
| A failed connect keeps the window (follow-up) | a wrong password: `WiFi Disconnected. Reason: %d`, no `portal priority OFF`, BLE still paused | T4-10 F3 | — |
| Sensors heard again after setup | every BLE sensor's `eleak … — leak=0` within about 100 s of the resume; a sensor wetted in the window reports its leak in its first burst after it | T4-10 A8, E4, F4 | T4-10 H3, smoke step 10 |
| No false `device_offline` (health hold) | no `device_offline` and no BLE `Roll-up grace expired` for the pause; fresh 600 s from `portal priority OFF`; the snapshot gate waits for BLE sensors not yet heard; an absent sensor goes offline 600 s after the resume, never earlier | T4-10 E2-E4 | T4-10 A10, F4, smoke step 10 |
| Valve held in the pause (follow-up, lead decision C) | a valve not linked stays "syncing" (fleet WHITE, no `Roll-up grace expired` for it) for the whole window, also after a leak-response hunt that could not reach it; a valve that drops in the window stays YELLOW "Valve disconnected", never RED; each gets 180 s from `portal priority OFF`; no `device_offline` for it | T4-10 A3, B1, B3, E2, H2 | T4-10 H3, smoke step 10 |

### M.3 Round 2 findings (fixed) and other F-findings

| Item | What 2.1.4 must do | Main tests | Also |
|---|---|---|---|
| F-01 route (a) | The hub's auto-clear pended while the valve is out of range is applied at the relink; no re-latch, no false button override | T3-11 | T6-15 step 2, smoke step 5 |
| F-01 route (b) with F-02 | Hub power-cycled with the latch and every sensor dry: exactly one auto-clear, no override | T3-13 (T4-11 Part B) | — |
| F-01 route (c) | `leak_reset` while the valve is unlinked | T3-12 | — |
| F-01 owed clear lost at restart | The reconnect re-latches (fail closed), then the 10 s auto-clear releases it with no window | T3-14 | T6-03 step 10 and run B (no owed line after a boot) |
| F-01 genuine press | A real valve long-press still starts the 24 h window (confirmed edge), also after a restart; a press right after the close may be missed (fails closed, observe) | T3-15 (T5-08) | T4-07 step 3, T4-09 A1 |
| F-01 online paths | The hub's own clear on a healthy link is never an override | T5-03, T5-05, T5-10 | T5-04 |
| F-05 | `leak_reset` queues its clear under the rules lock | T3-12, T5-05 | — |
| F-08 | A tick rules event is published before the pass's leak handling: `rmleak_auto_cleared` precedes the re-wet's `leak_detected` and `auto_close`, online and offline (MQTT-reconnect pass excepted) | T5-04, T5-12 | T3-18, VAL-09, T6-14, T6-18 step 7, VAL-15, smoke step 4 |
| `b245d94` | A re-wet at the release always re-locks the valve | T5-04 | T3-18, VAL-09 |
| B1 case (a) | Hub power-cycled with a sensor wet and the valve open: CLOSED within about 3 s of `SETUP COMPLETE`, no rc=6 forced relink | T3-16 | T6-11 step 5 |
| B1 case (b) | Leak while the valve is out of range: RMLEAK before CLOSE at the relink, no forced relink; setup-straddle hold | T3-17 | T4-10 B4, T6-17 Part A, smoke (T4-02 is the in-range case) |
| B1 case (c) | Re-wet at the clear within a 4-procedure GATT pool | T5-04 | T3-18 |
| B1 sustained stall | A valve that stops answering ATT is dropped by the 30 s GATT timeout; logs bounded | T6-16 | — |
| RMLEAK ordering | RMLEAK before the valve command on every path: auto-close, `override_enable`, `override_cancel`, expiry, reconnect replay, setup straddle | T5-10, T3-17, T3-10 | T5-03, T5-06, T5-09, T4-08, T6-16, T6-17 |
| F-02 (accepted) | Restart during a live leak can release RMLEAK for one wet burst (observe) | T4-11 Part A (T3-21) | T3-15 step 5 (T5-08 addition 1) |
| F-03 (deferred) | Leak latched while the valve was away, then a restart: reconnect infers a button press (observe) | T4-12 (T3-20) | — |
| F-06 / F-07 | Rules reset after a valve replacement or an emptied hub not retried when the lock is busy (observe) | DEC-16 | — |
| F-09 (deferred) | Late `device_offline`, then `device_recovered`, for a valve that went offline wet and returns dry after more than 180 s | — (see M.8) | — |
| F-10 | `rmleak`/`leak_state` read `false` while the valve state is `"unknown"` during GATT setup (observe) | T2-06 | T3-04, VAL-08 step 1, VAL-12 |
| F-04 (2.1.5 known limitation) | A leak evaluated while provisioning is busy for more than 1 s is dropped until the sensor reports again (observe) | — (M.8 #11; no deterministic trigger) | the validator false positive of 0.18 (annotate, do not fail) |
| F-11 (nit, kept) | Health DISCONNECTED is still gated on `was_target`: a surviving valve entry can stay `connected`, `excellent`, `last_seen_age_s` 0 after its link is gone (observe) | T3-07 (observe), DEC-16 (observe) | — |
| F-12 (2.1.5 known limitation) | A hub emptied by a `provision` stays `provisioned:true` (twin and lifecycle), unlike one emptied by removals (observe and record) | DEC-17, VAL-12 | — |
| F-13 (doc only) | Schema text for `cmd_ack` / lifecycle: an allocation failure now drops the message (no envelope without `data`) | — doc note, no bench test | — |
| F-14 (2.1.5 known limitation) | A first-time hub name set while a snapshot is built fails that snapshot with `TELEMETRY_V2: Snapshot not built - out of memory`; the 5 s retry publishes it (`Known-limit`, 0.17) | 0.17 rule, DEC-18 step 1, T6-03 run A step 3 | — |
| F-15 (doc only) | Stale comment in `sync_valve_detectors()` (cites the old 30 s all-clear) | — doc note, no bench test | — |

### M.4 Known limitations (CHANGELOG 2.1.4), all observe and record

| Known limitation | Observed in |
|---|---|
| A live DPS registration blocks `iothub_task` for up to 60 s per attempt | T4-06, DEC-18, T6-02, T6-08 |
| C2D checks can read the old valve's cache between a target change and the DISCONNECT | DEC-09, T3-05 |
| Snapshot heap (E-01): a hub with about 20 or more devices may fail to publish snapshots | T6-10 |
| Heap budget: about 181 B more static RAM and about 50 B of permanent heap | VAL-01, VAL-13 |
| An override started before the sync and restored after a software reset ends at the first sync (E-03 case B) | T4-09 Part B |
| An override restored after a power cut with no internet ends 24 h after the power-on | T4-09 A2, A6 |
| The captive portal runs beside NimBLE with less free heap; while no credentials are saved, and for about a minute after setup, BLE sensors are not heard and a lost valve link is not re-found (unless a leak close is pended); the pause has no cap before setup; the valve reads "syncing" or "Valve disconnected" until 180 s after the resume, also after a leak-response hunt that could not reach it; the first snapshot after setup waits for the resume | T4-10 (Parts A, B, E, H; heap table) |
| The router-fallback portal keeps BLE scanning, so a phone may fail to join it; the 10 s reset is the way out | T4-10 D3 (D5, D6) |
| The portal page's own disconnect erases nothing on the fallback portal (idle Wi-Fi) | T4-10 D3 |
| Up to 16 events fit in the offline buffer; the oldest is overwritten | T6-14, T4-08 step 5 |
| F-03: a leak latched while the valve was out of reach can be read as a button press at the reconnect | T4-12 (T3-20) |
| F-02: after a restart during a leak the interlock can be released for one wet report | T4-11 Part A (T3-21), T3-15 step 5 |
| A slow RMLEAK read-back can be read as a button press (stale confirm) | T3-18 (observe), T5-04 (read-back gap) |
| 2.1.5: with the relink cap engaged a live command stays pended; a command held behind RMLEAK can wait if the queue is full; a busy pool holds the valve task about 10 s | T6-16, T6-17 |
| 2.1.5: a leak re-assert queued after the rules lock is overridden by a `leak_reset` in that gap (fails closed) | — (see M.8) |
| 2.1.5: a leak evaluated while provisioning is busy for more than 1 s is dropped | — (see M.8; the validator false positive in 0.18) |
| 2.1.5: the rules reset after a valve replacement, or of an emptied hub, not retried when the lock is busy | DEC-16 |
| 2.1.5: F-09 late `device_offline` for a valve that went offline wet | — (see M.8) |
| 2.1.5: decommission and re-provision of the same valve in one pass can stay connected with no link | T3-07, DEC-16 |
| 2.1.5: a hub name set for the first time while a snapshot is built fails it with `Snapshot not built - out of memory` | DEC-18 step 1, T6-03 run A step 3 (incidental; 0.17) |
| 2.1.5: twin reported and lifecycle can read `provisioned:false` when provisioning is busy for more than 1 s | DEC-03, DEC-11 (observe) |
| 2.1.5: a `provision` that adds no device no longer restarts the commission snapshot and the pulse | DEC-02 (T5-02), T6-05, VAL-04 step 3 |
| 2.1.5: a reconnect replaying both held commands gets `valve read-back rc=6 - position unconfirmed` | T3-16, T3-17, T6-16 |
| Not changed: a sensors-only leak latches and sends `rmleak_auto_cleared` (and `rmleak_cleared`) without `valve_id` | T3-01 step 9, T3-02, T6-12 |
| Deferred: command-id de-duplication | DEC-10, T6-05 |
| Deferred: the single-slot rules-event buffer (the MQTT-reconnect exception of F-08) | T5-04, T3-18, VAL-09 |
| Deferred: sensors unheard during a valve connect attempt | T6-19 |
| Deferred: removing the valve bond on decommission (a bond made under 2.1.3's name match is kept but never used) | T3-01, T6-08 |
| Deferred: legacy keyword scan, more than 16 simultaneous leak sources, LoRa driver hardening | — (see M.8) |

### M.5 Council residual risks

"E2F c[i]#j" is `phaseE2F.json` `council[i].r.residual_risks[j]` (round 2 council). "Final v[i]#j" is `council_final.json` `votes[i].r.residual_risks[j]` (final council, 5/5 SHIP). The risk text is shortened; the JSON has the full text and bench steps.

**Round 2 council (phaseE2F.json)**

| Ref | Member | Residual risk (short) | Verified or observed by |
|---|---|---|---|
| E2F c[0]#0 | F1-rtos | Stack headroom on the tasks running new code. | T6-11; VAL-14 (no canary panic) |
| E2F c[0]#1 | F1-rtos | GATT procedure-pool exhaustion (B1). This must be re-checked after the fix, because the pool (4) and the ATT round trip (0.45-0.75 s) are fixed by sdkconfig and by the … | T3-16, T3-17, T5-04 (case c), T6-16 |
| E2F c[0]#2 | F1-rtos | F-01's false override. If the council does not fix it, it remains live on marginal-RF valves. | T3-11, T3-12, T3-13, T3-14 |
| E2F c[0]#3 | F1-rtos | iothub_task stalls behind a blocking C2D handler (esp-mqtt holds MQTT_API_LOCK during dispatch), which delays leak evaluation by up to about 10 s. | T6-18 (T5-15 is run as it) |
| E2F c[0]#4 | F1-rtos | BLE now starts at boot beside the SoftAP captive portal: continuous 50 %-duty 1M + Coded scanning with duplicate filtering off, under software coexistence and with less … | T4-10 (T6-13 is run as it) |
| E2F c[1]#0 | F2-memory | The heap profile after G4b (BLE started at boot, before Wi-Fi and TLS) has never been measured. | VAL-13 via T6-07 and T6-08; T4-10 heap table |
| E2F c[1]#1 | F2-memory | Sensors-only BLE hubs now run NimBLE (about 69 KB), leaving about 33 KB free. | T6-09 (heap), T6-12 (router cycles and SAS renewal), VAL-14 |
| E2F c[1]#2 | F2-memory | Captive portal with BLE running is new (E-21): about 69 KB less heap than 2.1.3's portal, plus Wi-Fi/BLE coexistence while the BLE leak scanner and valve link run beside … | T4-10 |
| E2F c[1]#3 | F2-memory | Task stack headroom is unmeasured. NimBLE host (4096 B at -Og) gained about 36-54 B on the notify and DISCONNECT paths and runs SMP crypto under the app's ble_gap_event … | T6-11 |
| E2F c[1]#4 | F2-memory | E-01 (known): a snapshot needs about twice its printed size in one block. | T6-10 (observe) |
| E2F c[1]#5 | F2-memory | Pressure on the default 'nvs' partition (16 KB, 3 usable pages). | T6-14 (T4-10 Part C is run as it) |
| E2F c[1]#6 | F2-memory | The 10 s auto-clear (user decision) raises the clear/re-latch write rate for a sensor flapping at its threshold. | T6-15 (T5-16 is run as it) |
| E2F c[2]#0 | F3-cloud-ui | F-08: the re-wet-at-clear test shows auto_close without a preceding rmleak_auto_cleared in about half the runs. | T5-04, T3-18, VAL-09 step 4 |
| E2F c[2]#1 | F3-cloud-ui | The empty-hub scheduling (S8/S9) has not been exercised on a bench since the removals-to-empty path and on_hub_emptied changed. | DEC-11, DEC-12, VAL-06 |
| E2F c[2]#2 | F3-cloud-ui | The decommission-all final snapshot and reboot path (S11) is unexercised. | DEC-18, T6-02 |
| E2F c[2]#3 | F3-cloud-ui | Pre-sync event stamping (N1): the events must arrive after connect with a correct ts, in order, and never with ts < 1704067200. | T4-03, T4-05, VAL-15 |
| E2F c[2]#4 | F3-cloud-ui | F-10: during valve GATT setup the snapshot shows `rmleak:false`/`leak_state:false` beside "Leak interlock latched". | T2-06, T3-04, VAL-08 step 1, VAL-12 (observe) |
| E2F c[2]#5 | F3-cloud-ui | F-09: a late device_offline, then device_recovered, when a valve that went offline wet returns dry after more than 180 s. | **none** (F-09, deferred): see M.8 |
| E2F c[2]#6 | F3-cloud-ui | F-01 on the wire: a false `water_access_override_enabled{trigger:"button"}` after the hub's own RMLEAK clear is pended while the valve is unlinked (pre-existing). | T3-11, T3-12 |
| E2F c[2]#7 | F3-cloud-ui | F-02 on the wire: a false `rmleak_auto_cleared` after a hub restart while a BLE sensor is still wet. | T4-11 Part A (T3-21 is run as it) |
| E2F c[2]#8 | F3-cloud-ui | A provision that adds no device no longer restarts the 5-minute placement pulse (UI-sync change from 2.1.3). | DEC-02, T6-05, VAL-04 step 3 |
| E2F c[2]#9 | F3-cloud-ui | The valve battery-critical wire contract (S14/S17) has not been run on a bench. | T2-01, T2-02, T2-03, T2-05, VAL-08 |
| E2F c[3]#0 | F4-ble | F-01 regression paths once fixed: a pended hub clear across a relink or boot-time setup | T3-11, T3-12, T3-13, T3-14 |
| E2F c[3]#1 | F4-ble | RMLEAK-before-CLOSE when both are pended across a relink | T3-17, T3-10, T6-16, T6-17 |
| E2F c[3]#2 | F4-ble | Valve identity and target change while a link or connect is in flight (P0-a/c) | T3-05 (Part C churn), T3-07, DEC-16 |
| E2F c[3]#3 | F4-ble | Wi-Fi/BLE coexistence and heap while the captive portal runs, now that BLE starts at boot with no Wi-Fi gate | T4-10 Part B |
| E2F c[3]#4 | F4-ble | Leak sensors are unheard while a valve connect is pending. | T6-19 |
| E2F c[3]#5 | F4-ble | NimBLE host task stack margin (4096 B). The recursive setup_next_step chain and deep GAP DISC dispatch now carry extra frames: mac[18] in on_notify and … | T6-11 |
| E2F c[3]#6 | F4-ble | F-09: false device_offline when a valve that dropped while its flood probe was wet reconnects dry after more than 180 s | **none** (F-09, deferred): see M.8 |
| E2F c[3]#7 | F4-ble | F-10: snapshots during valve GATT setup publish rmleak:false and leak_state:false next to 'Leak interlock latched' | T2-06 (observe) |
| E2F c[3]#8 | F4-ble | F-08: the CP3 'wet again right at the clear' step can publish auto_close without rmleak_auto_cleared | T5-04, T3-18 |
| E2F c[3]#9 | F4-ble | Scanner delta state when a wet sensor is removed and re-added within 10 s (S6) | DEC-07 |
| E2F c[4]#0 | F5-persistence | F-01, route (a): a hub RMLEAK clear pended while the valve is out of range becomes a false 24 h button override at reconnect | T3-11 |
| E2F c[4]#1 | F5-persistence | F-01, route (b), with F-02: a hub power-cycled during an incident restores the latch, and a dry report auto-clears while the valve is still in GATT setup | T3-13 (T4-11 Part B is run as it) |
| E2F c[4]#2 | F5-persistence | F-02: after a restart during a live leak, an auto-clear fires before the still-wet sensor is heard again | T4-11 Part A |
| E2F c[4]#3 | F5-persistence | F-03: after a hub reboot, the reconnect infers a physical override on an open valve with RMLEAK clear while a LoRa sensor is still wet | T4-12 (T3-20 is run as it) |
| E2F c[4]#4 | F5-persistence | Upgrade and rollback: provisioning, rules opt-out, a latched incident or an active override might not survive the 2.1.3 -> 2.1.4 -> 2.1.3 reflash | VAL-02 (T6-03 runs A and B, plus run C), VAL-03 (T6-04) |
| E2F c[4]#5 | F5-persistence | A power cut mid-decommission-all could leave stale rules or offline state | T6-02, DEC-18 |
| E2F c[4]#6 | F5-persistence | Pre-sync events and a software reset before the clock syncs | T4-04 |
| E2F c[4]#7 | F5-persistence | An override restored across a power-on with no internet is timed from the power-on | T4-09 Part A |

**Final council (council_final.json)**

| Ref | Member | Residual risk (short) | Verified or observed by |
|---|---|---|---|
| Final v[0]#0 | F1-rtos | Stack headroom on tasks that run changed code, with no high-water-mark instrumentation beyond the FreeRTOS canary: - ble_valve (4096 B): the new chain ble_valve_task -> … | T6-11; VAL-14 |
| Final v[0]#1 | F1-rtos | B1 timing on the bench radio: the busy-pool waits have to absorb setup completion's replay plus the rules reconcile. | T3-16, T3-17 |
| Final v[0]#2 | F1-rtos | A sustained ATT stall on a link that stays up (marginal RF, or the valve application busy). | T6-16 |
| Final v[0]#3 | F1-rtos | Stale-confirm false 'button' window (lane R's Residual 1, plus its Priority 1-with-owed reconnect variant). | T3-18 (observe), T5-04 (read-back gap) |
| Final v[0]#4 | F1-rtos | At setup completion with both slots pended, the host's valve read-back always gets rc=6 (a known, pre-existing limit). | T3-16, T3-17, T6-16 (the `valve read-back rc=6` line is acceptable) |
| Final v[0]#5 | F1-rtos | F-01 fix end to end, plus the genuine-press path now gated on a confirmed 1 (a missed press fails closed). | T3-11, T3-12, T3-13, T3-14, T3-15 |
| Final v[0]#6 | F1-rtos | iothub_task can stall behind a blocking C2D handler (esp-mqtt holds MQTT_API_LOCK). | T6-18 |
| Final v[0]#7 | F1-rtos | Command-queue saturation (depth 10) while the command task waits out a busy GATT pool. | T6-17 |
| Final v[0]#8 | F1-rtos | The BLE start claim, now reachable from two tasks at once (boot apply on iothub and `provision` on esp-mqtt). | T6-20, DEC-13 |
| Final v[0]#9 | F1-rtos | BLE starts at boot beside the SoftAP captive portal (carried from the previous round). | T4-10 |
| Final v[1]#0 | F2-memory | The heap profile after G4b (BLE started at boot, before Wi-Fi/TLS) is still unmeasured. | VAL-13 via T6-07 and T6-08 |
| Final v[1]#1 | F2-memory | Sustained ATT stall on a live valve link, which is new with B1: the replay token cycles every 5-10 s and commands wait behind it until NimBLE's 30 s GATT timeout drops … | T6-16 |
| Final v[1]#2 | F2-memory | Task stack headroom after the fix round: the ble_valve task (4096 B) gains a frame of about 48-64 B on its deepest logging chain (read_back_when_free, or drop_link then … | T6-11 |
| Final v[1]#3 | F2-memory | F-08 double publish: in a re-wet-at-clear pass, two rules events plus their coupled snapshot share the outbox until PUBACK. | T5-04 (online); T5-12, T6-14 (offline) |
| Final v[1]#4 | F2-memory | Command-queue saturation during setup or a busy pool (the d3b9ef8 live-path hold without a queue-full guard). | T6-17 Part B |
| Final v[1]#5 | F2-memory | Sensors-only BLE hubs now run NimBLE (about 69 KB), so SAS renewal and Wi-Fi reconnect TLS handshakes run with less heap. | T6-12, T6-09 |
| Final v[1]#6 | F2-memory | Captive portal with BLE running (E-21): about 69 KB less heap than the 2.1.3 portal, plus Wi-Fi/BLE coexistence. | T4-10 |
| Final v[1]#7 | F2-memory | E-01: a snapshot needs about twice its printed size in one heap block. | T6-10 (observe) |
| Final v[1]#8 | F2-memory | Pressure on the default 'nvs' partition (16 KB): the offline ring shares it with Wi-Fi config, PHY calibration, NimBLE bonds and the DPS cache; pre-sync events are now … | T6-14 |
| Final v[1]#9 | F2-memory | Write rate from the 10 s auto-clear (user decision) together with the owed-clear re-queue at every relink (54c8ed0): each flap cycle writes RMLEAK 0 then 1 to the valve, … | T6-15 |
| Final v[2]#0 | F3-cloud-ui | F-08 new order at the clear: the tick's `rmleak_auto_cleared` must now precede the re-wet's `leak_detected` and `auto_close`, and the app must render release then … | T5-04, T3-18, VAL-09 step 4 (app) |
| Final v[2]#1 | F3-cloud-ui | F-08 early take while offline: tick events buffered at the loop top must replay in order with the leak events. | T5-12, T6-14 |
| Final v[2]#2 | F3-cloud-ui | C2D-written rules events now publish at the next pass top instead of Phase 3. | T5-05, T5-06, T5-07 |
| Final v[2]#3 | F3-cloud-ui | F-01 fix on the wire: the hub's own clear pended across a relink must produce no re-latch and no override. | T3-11, T3-12 |
| Final v[2]#4 | F3-cloud-ui | The genuine button override must still reach the cloud, now gated on the lock having been seen set, including a quick press right after the close. | T3-15 (steps 2, 5 and 6) |
| Final v[2]#5 | F3-cloud-ui | Pre-sync event stamping (N1): the events must arrive after connect with a correct ts, in order, and never with ts < 1704067200. | T4-03, T4-05 |
| Final v[2]#6 | F3-cloud-ui | Empty-hub scheduling and shapes (S8/S9) and decommission-all (S11) have no bench run yet. | DEC-11, DEC-12, DEC-18, VAL-06 |
| Final v[2]#7 | F3-cloud-ui | F-10: during valve GATT setup the snapshot shows rmleak:false and leak_state:false beside "Leak interlock latched". | T2-06 (observe) |
| Final v[2]#8 | F3-cloud-ui | F-02 (accepted): a false `rmleak_auto_cleared` after a hub restart while a BLE sensor is still wet. | T4-11 Part A |
| Final v[2]#9 | F3-cloud-ui | F-09 (deferred): a late device_offline, then device_recovered, when a valve that went offline wet returns dry after more than 180 s. | **none** (F-09, deferred): see M.8 |
| Final v[2]#10 | F3-cloud-ui | B1 on the wire: with GATT busy at setup completion, the valve read-back gets rc=6. | T3-16, T3-17 |
| Final v[2]#11 | F3-cloud-ui | A provision that adds no device no longer restarts the placement pulse or commission snapshot (a UI-sync change from 2.1.3). | DEC-02, T6-05, VAL-04 step 3 |
| Final v[2]#12 | F3-cloud-ui | The valve battery-critical wire contract (S14/S17) has not been run on a bench. | T2-01, T2-02, T2-03, T2-05, VAL-08 |
| Final v[3]#0 | F4-ble | F-01 fix: a pended hub clear across a relink must never re-latch or read as a button press | T3-11, T3-12, T3-13 |
| Final v[3]#1 | F4-ble | The new confirmed-edge requirement must not suppress a genuine valve-button override (SRS 4.4.2) | T3-15 |
| Final v[3]#2 | F4-ble | B1 regression: the reconnect replay plus the Priority 1 reconcile must close the valve without a forced relink | T3-16, T3-17 Part A |
| Final v[3]#3 | F4-ble | Setup-straddle hold: RMLEAK before CLOSE when the CLOSE is dequeued as setup completes | T3-17 Part B |
| Final v[3]#4 | F4-ble | Stale confirm on a slow link: a re-wet at the clear, or Priority 1 with a clear owed, can start a false window if the SET read-back is late | T3-18 (observe), T5-04 |
| Final v[3]#5 | F4-ble | Sustained busy GATT pool (the valve stops answering ATT while the link stays up): the token cycle must end at NimBLE's GATT timeout | T6-16 |
| Final v[3]#6 | F4-ble | Host-side valve read-back rc=6 at setup completion when both slots are pended: the position depends on the valve's VALVESTATE notify | T3-17, T6-16 |
| Final v[3]#7 | F4-ble | Override path: a clear from override_enable that pends when the link drops re-latches through Priority 0 on the stale 1 | **none**: see M.8 |
| Final v[3]#8 | F4-ble | Atomic BLE start claim with concurrent callers | T6-20 |
| Final v[3]#9 | F4-ble | Carry-over: Wi-Fi/captive-portal coexistence and heap with BLE started at boot | T4-10 |
| Final v[3]#10 | F4-ble | Carry-over: stack margin of the NimBLE host and ble_valve tasks, including the new read_back_when_free frame on the 4096 B ble_valve stack | T6-11 |
| Final v[3]#11 | F4-ble | Carry-over: leak sensors go unheard while a valve connect is pending (up to the 30 s connect timeout) | T6-19 |
| Final v[4]#0 | F5-persistence | F-01 route (a), after the fix: a hub clear (auto-clear or leak_reset) pended while the valve is out of range must be re-sent at reconnect, not re-latched and then read … | T3-11, T3-12 |
| Final v[4]#1 | F5-persistence | F-01 route (b) with F-02: a hub power-cycled during an incident restores the latch, and a dry report auto-clears while the valve is still in GATT setup | T3-13 |
| Final v[4]#2 | F5-persistence | A clear owed when the hub restarts is lost with RAM: the reconnect re-latches (fail closed), and the 10 s auto-clear must then release it with no window | T3-14 |
| Final v[4]#3 | F5-persistence | A genuine valve-button press after a hub restart must still start the window: g_interlock_confirmed is false after every boot and needs a confirmed RMLEAK 1 first | T3-15 step 5 |
| Final v[4]#4 | F5-persistence | A press shortly after a live auto-close needs a tick to have seen the RMLEAK read-back of 1 first (the missed-press residual) | T3-15 step 6 (observe) |
| Final v[4]#5 | F5-persistence | F-08 offline: the tick's rmleak_auto_cleared must be buffered ahead of the re-wet's leak_detected and auto_close, and replayed in that order | T5-12, T6-14 |
| Final v[4]#6 | F5-persistence | Upgrade and rollback: provisioning, the rules opt-out, a latched incident and an active override must survive 2.1.3 -> 2.1.4 -> 2.1.3 without erasing flash | VAL-02 (T6-03 plus run C), VAL-03 (T6-04) |
| Final v[4]#7 | F5-persistence | A power cut mid-decommission-all could leave stale rules or offline state (code unchanged since my last vote) | T6-02 |
| Final v[4]#8 | F5-persistence | Pre-sync events across a software reset before the clock syncs (code unchanged; the F-08 publish order moved) | T4-04 |
| Final v[4]#9 | F5-persistence | An override restored across a power-on with no internet is timed from the power-on (documented) | T4-09 Part A |

**Non-blocking issues raised by the councils** (not required, traced where a test exists)

Round 2 council:

| Ref | Member | Non-blocking issue (short) | Test |
|---|---|---|---|
| E2F c[0] NB0 | F1-rtos | The forced-relink cap is reset by every setup-completion replay | T6-16 (observe `valve writes keep failing`) |
| E2F c[0] NB1 | F1-rtos | app_ble_valve_signal_start() uses a non-atomic check-then-act, now reachable from two tasks | T6-20 |
| E2F c[0] NB2 | F1-rtos | Blocking C2D handlers run under esp-mqtt's MQTT_API_LOCK and stall iothub_task's publishes (pre-existing) | T6-18 |
| E2F c[0] NB3 | F1-rtos | The CP3 re-wet-at-clear bench step exceeds the 4-procedure GATT pool | T5-04, T3-18 |
| E2F c[1] NB0 | F2-memory | About 50-60 B of permanent heap from the two log-level tags (E-20) | VAL-13 |
| E2F c[1] NB1 | F2-memory | Static RAM +176 B vs 2.1.3 (CP2) is within the plan; CP3 should not move | VAL-01 |
| E2F c[1] NB2 | F2-memory | The nimble_port_init retry does not cover an NPL allocation failure (ESP-IDF asserts) | T6-11 (observe) |
| E2F c[1] NB3 | F2-memory | HANDOFF stack note about prov_device_set_t on the esp-mqtt stack is stale | doc note, no bench test |
| E2F c[1] NB4 | F2-memory | At persistent low heap the snapshot is rebuilt and dropped every 5 s | T6-10 |
| E2F c[2] NB0 | F3-cloud-ui | The docs promise an rmleak_auto_cleared that the wire often omits for a re-wet at the clear (F-08) | T5-04 |
| E2F c[2] NB1 | F3-cloud-ui | A provision that adds no device no longer re-arms the commission snapshot and placement pulse; not documented | DEC-02 |
| E2F c[2] NB2 | F3-cloud-ui | The snapshot schema does not document the rmleak default during valve GATT setup (F-10) | T2-06 |
| E2F c[2] NB3 | F3-cloud-ui | A valve reconnecting dry after a wet >180 s outage raises a late device_offline, then device_recovered (F-09) | **none** (F-09): see M.8 |
| E2F c[2] NB4 | F3-cloud-ui | The twin reported can publish valve_id:null and counts of 0 when the provisioning mutex is busy | DEC-03, DEC-11 (observe) |
| E2F c[2] NB5 | F3-cloud-ui | Schema text on allocation failures is stale (F-13) | doc note, no bench test |
| E2F c[3] NB0 | F4-ble | A narrow straddle of setup completion can put a live CLOSE on the air ahead of a pended RMLEAK SET | T3-17 Part B |
| E2F c[4] NB0 | F5-persistence | An override restored after a power-on with no internet restarts its 24 h at every power-on, so repeated power-ons with no clock sync can … | T4-09 Part A (observe) |
| E2F c[4] NB1 | F5-persistence | A power cut between the decommission-all ack and iothub_task's offline_buffer_clear() leaves the old deployment's buffered events to replay … | T6-02 step 3 (observe) |
| E2F c[4] NB2 | F5-persistence | A power cut during a provisioning save can leave a mix of old and new keys; the new re-save covers only NVS errors | T6-01 (observe) |
| E2F c[4] NB3 | F5-persistence | CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y, but the app never calls esp_ota_mark_app_valid_cancel_rollback() | T6-03 (note for the OTA work) |

Final council:

| Ref | Member | Non-blocking issue (short) | Test |
|---|---|---|---|
| Final v[0] NB0 | F1-rtos | The evaluate_leak comment says 'Both clears are queued under g_mutex, so this re-assert always lands after them', but the SET+CLOSE is … | code comment, no bench test |
| Final v[0] NB1 | F1-rtos | Stale-confirm false 'button' window, reconnect variant: Priority 1 with a clear owed | T3-18 (observe) |
| Final v[0] NB2 | F1-rtos | The early rules-event publish (44d3d43) runs ahead of Phase 2's leak evaluation within a pass | T6-18 |
| Final v[0] NB3 | F1-rtos | With the relink cap engaged, a command pended on a live link after a genuine write failure gets no replay token | T6-16 (observe) |
| Final v[0] NB4 | F1-rtos | The command task can now be busy for about 10 s per command, and the 10-deep command queue can fill | T6-16, T6-17 |
| Final v[1] NB0 | F2-memory | d3b9ef8's live-path hold has no queue-full guard, unlike the replay path | T6-17 Part B |
| Final v[1] NB1 | F2-memory | The CP3 .bss may read exactly 36,280 (+0), not 36,285; both are expected | VAL-01 |
| Final v[1] NB2 | F2-memory | A live command can now hold the ble_valve task for about 10 s under a busy pool (a CLOSE behind an RMLEAK pre-check, about 20 s) | T6-16, T6-17 |
| Final v[1] NB3 | F2-memory | The F-08 early take adds one more rules-mutex wait (up to 1 s) and a possibly blocking publish before Phase 2 | T6-18 |
| Final v[1] NB4 | F2-memory | Carried: the nimble_port_init retry cannot recover an NPL allocation failure (ESP-IDF asserts) | T6-11 (observe) |
| Final v[2] NB0 | F3-cloud-ui | HANDOFF §7 F-01(a)/(b) expects a log line that cannot print in those cases | T3-11 (pass criteria already corrected) |
| Final v[2] NB1 | F3-cloud-ui | The HANDOFF re-wet exception wording says only auto_close may appear on the reconnect pass | T5-04, T3-18 |
| Final v[2] NB2 | F3-cloud-ui | A connect edge can put the early-taken tick event on the wire ahead of the offline replay and lifecycle (tiny, same class as every existing … | T5-12 (observe) |
| Final v[3] NB0 | F4-ble | Stale-confirm residual: a second route, and it is missing from the CHANGELOG known limitations | T3-18 (observe); Appendix A |
| Final v[3] NB1 | F4-ble | An ENOMEM from mbuf exhaustion (no GATT procedure allocated) is not bounded by NimBLE's 30 s GATT timeout | T6-16 (observe) |
| Final v[3] NB2 | F4-ble | After the relink cap engages, a failed live command is pended with no replay token | T6-16 (observe) |
| Final v[3] NB3 | F4-ble | The live-path hold depends on the replay token being queued (lane-disclosed) | T6-17 |
| Final v[4] NB0 | F5-persistence | If on_valve_replaced() times out (F-06, deferred), the owed flag can survive into the next valve | DEC-16 (observe) |
| Final v[4] NB1 | F5-persistence | Stale comment in Priority 2's re-latch branch | code comment, no bench test |
| Final v[4] NB2 | F5-persistence | F-08: while offline, the early publish's offline-buffer NVS write now precedes Phase 2 in that pass | T6-14, T5-12 |
| Final v[4] NB3 | F5-persistence | Carried from my previous vote; the code is unchanged at d9fa9c8 | see the E2F F5 rows above |

### M.6 Plan scenarios S1-S26

| Scenario | Tests |
|---|---|
| S1 provision and UI sync | DEC-01, T5-01, VAL-04 |
| S2 remove one heard sensor after the window | DEC-03, VAL-05 |
| S3 removals inside the sync window | DEC-04 |
| S4 remove the last unheard device in the window | DEC-05 |
| S5 remove a wet sensor | DEC-06 |
| S6 re-add a wet sensor | DEC-07 |
| S7 remove the valve only | DEC-09 |
| S8 remove the last device | DEC-11, DEC-16, VAL-06 |
| S9 empty hub across reboot and MQTT drops | DEC-12, DEC-17, VAL-06 |
| S10 re-provision an empty hub | DEC-13 |
| S11 decommission all | DEC-18, T6-02 |
| S12 decommission error paths and repeated id | DEC-10, T6-05, T6-06 |
| S13 decommission while MQTT is offline | DEC-15 |
| S14 valve battery sequence | T2-03, VAL-08 |
| S15 unknown valve battery | T2-06 |
| S16 flapping 10/11 % | T2-07 |
| S17 `valve_open` refused at ≤ 10 % | T2-01, T2-05, VAL-08 |
| S18 sensors-only hub with a neighbour valve | T3-01, VAL-07 |
| S19 reboot a sensors-only hub | T3-02, VAL-07 |
| S20 boot with the router off | T4-02 |
| S21 captive portal with BLE (scanning paused while no credentials are saved) | T4-10 (T6-13) |
| S22 re-provision a different valve while linked | T3-05 |
| S23 foreign LoRa sensor | T3-08, T4-13 |
| S24 regression (leak, reset, override, read-back, twin, offline replay) | T5-03 … T5-13, VAL-10, VAL-11 |
| S25 power loss mid-decommission, upgrade, rollback | T6-01, T6-02, T6-03, T6-04, VAL-02, VAL-03 |
| S26 heap like-for-like | VAL-13, T6-07, T6-08, T6-09 |

### M.7 Test index: every test ID and what it covers

| Test | Title | Covers (from the section coverage reports) | Merged |
|---|---|---|---|
| DEC-01 | Provision a valve and 4 BLE sensors on an empty hub: UI sync, "Syncing" countdown, pulse, boot snapshot (S1) — P0 | BUG-2; S1 | — |
| DEC-02 | A `provision` that adds no device does not restart the pulse or the commission snapshot (council residual) — P0 | council:provision adding no device no longer restarts pulse/commission | also runs T5-02 |
| DEC-03 | Remove one heard BLE sensor after the sync window: the survivors keep their data (S2, BUG-2) — P0 | BUG-2; S2; E-10; known-limit:twin/lifecycle provisioned:false when provisioning busy >1 s (observe) | — |
| DEC-04 | Remove sensors during the sync window, heard and unheard: no reset, no new window, original deadlines (S3, BUG-2, E-18, N10) — P0 | BUG-2; S3; E-18; N10 (rating-change event snapshot at grace expiry) | — |
| DEC-05 | Remove the last unheard device during the window: one `boot` snapshot, no second (S4, BUG-2) — P0 | BUG-2; S4 | — |
| DEC-06 | Remove a sensor while it is wet: the interlock releases, `leak_reset` is accepted, an override cancel does not re-close (S5, BUG-2 rules purge) — P0 | BUG-2; S5; user: RMLEAK auto-clear 10 s (10-12 s) | — |
| DEC-07 | Re-add a sensor that is still wet: a fresh `leak_detected`, and a re-close (S6, E-02, E-17) — P0 | S6; E-02; E-17; L9 (telemetry-cache purge on removal); user: valve_open refused (RMLEAK detail) while a leak incident is latched; council:scanner delta state on remove+re-add within 10 s (S6) | — |
| DEC-08 | Remove a LoRa sensor: survivors unchanged, its later packets ignored, no pulse from them (LoRa decommission, N4, E-18) | E-18; N4; variant B (no radio, fake id 0x1A2B3C4D): LoRa provision/removal path, survivors unchanged | — |
| DEC-09 | Remove only the valve while its link is up: `"valve": {}`, no `valve_unlinked`, no rescan (S7, BUG-5, P0-c) — P0 | BUG-5; S7; P0-a (no valve scan/connect after the valve is removed); P0-c (valve commands flushed on valve removal); user: no auto_close on a hub with no provisioned valve; user: valve_open refused with 'No valve is set up for this hub.'; user: RMLEAK auto-clear 10 s (10-12 s); known-limit:C2D checks can read old valve cache between target change and DISCONNECT | — |
| DEC-10 | Decommission error paths: repeated id, unknown and invalid ids, no valve, bad targets (S12) | S12; known-limit:no command-id de-duplication | — |
| DEC-11 | Remove the last device: one `event` snapshot of the empty shape, LED WHITE, rules reset, then heartbeats (S8, BUG-3, BUG-6, E-05) — P0 | BUG-3; BUG-6; S8; E-05; user: no auto_close on a hub with no provisioned valve; council:empty-hub scheduling S8/S9 never benched; known-limit:twin/lifecycle provisioned:false when provisioning busy >1 s (observe) | — |
| DEC-12 | Empty hub after a reboot, and across MQTT drops: exactly one `boot` snapshot per connect (S9, BUG-6) — P0 | BUG-6; S9; council:empty-hub scheduling S8/S9 never benched | — |
| DEC-13 | Re-provision an empty hub after 1 and after 10 heartbeats (S10, BUG-6, E-10/E-17, council: BLE start claim) — P0 | BUG-6; S10; E-10; E-17; council:atomic BLE start claim (provision on an empty hub) | — |
| DEC-14 | A snapshot that follows a removal or an add never shows the old device set (E-10) | BUG-5; E-09; E-10 | — |
| DEC-15 | Decommission while MQTT is offline (S13) | S13 | — |
| DEC-16 | Back to back: empty the hub, then `provision` or `rules_config` at once; the new config survives (E-05, E-09, F-06/F-07) — P0 | S8; E-05; E-09; known-limit:same-valve decommission+re-provision in one pass can stay 'connected' with no link; known-limit:F-06/F-07 empty-hub rules reset not retried after mutex timeout | — |
| DEC-17 | A `provision` whose sensor arrays leave a sensors-only hub empty (E-05 follow-up, BUG-6) — P0 | BUG-6; S9; E-05; known-limit:hub emptied by a provision reports provisioned:true | — |
| DEC-18 | `decommission` `all`: final snapshot of the empty shape, override cleared, offline buffer cleared, reboot, empty `boot`, heartbeats (S11) — P0 | BUG-3; BUG-6; S11; L17 (offline buffer cleared by decommission all); council:decommission-all path S11 never benched; council:power cut mid decommission-all (optional variant); known-limit:DPS registration blocks iothub_task up to 60 s after decommission all | — |
| DEC-19 | Duplicate ids in a `provision` payload are ignored, in any case (N13) | N13 (duplicate BLE MACs, any case, and LoRa ids ignored) | — |
| T2-01 | Quick battery-critical check (about 10 min) | BUG-1 (valve battery <=10% critical / 11-20% warning / >20% excellent, valve only); S17 valve_open / valve_set_state open refused at <=10% with the exact detail, no command sent, valve_close allowed; council:valve battery-critical wire contract (S14/S17) never bench-run (F3, phaseE2F and council_final) | — |
| T2-02 | Full descent GOOD → LOW → CRIT and back up, with the valve's read cadence (about 35 min) | BUG-1 (valve battery <=10% critical / 11-20% warning / >20% excellent, valve only); E-16 valve last_seen_age_s 0 while linked, counts from the drop; offline_duration_s from the drop (180-215); wire: device_offline/device_recovered are reachability only; no health event for battery; device_recovered may be critical; prev_rating may equal rating; CHANGELOG: every valve battery edge publishes an event snapshot within seconds; in-band changes and steady re-notifies send none; council:valve battery-critical wire contract (S14/S17) never bench-run (F3, phaseE2F and council_final) | — |
| T2-03 | Plan S14 sequence: unknown → GOOD → 10 → 11 → 10 → disconnect → reconnect (unknown) → ≤10 (about 35 min) | BUG-1 (valve battery <=10% critical / 11-20% warning / >20% excellent, valve only); BUG-1 unknown battery is null, never 0 (and absent while disconnected); S14 unknown -> good -> 10 -> 11 -> 10 -> disconnect -> reconnect (unknown) -> <=10; E-19 reconnect device_recovered carries the fresh link-up battery reading; E-16 valve last_seen_age_s 0 while linked, counts from the drop; offline_duration_s from the drop (180-215); wire: device_offline/device_recovered are reachability only; no health event for battery; device_recovered may be critical; prev_rating may equal rating; plan F2/N12: a battery-critical valve that drops is held critical through the 180 s grace (no spurious device_recovered); council:valve battery-critical wire contract (S14/S17) never bench-run (F3, phaseE2F and council_final) | — |
| T2-04 | Disconnect while battery-critical: inside the grace, and a quick return after `device_offline` (about 12 min) | E-16 valve last_seen_age_s 0 while linked, counts from the drop; offline_duration_s from the drop (180-215); wire: device_offline/device_recovered are reachability only; no health event for battery; device_recovered may be critical; prev_rating may equal rating; plan F2/N12: a battery-critical valve that drops is held critical through the 180 s grace (no spurious device_recovered); Health reliability: debounced alert sent late (trailing edge), not dropped | — |
| T2-05 | Plan S17: `valve_open` refused at ≤ 10 %, linked and disconnected, and the refusal order (about 15 min) | S17 valve_open / valve_set_state open refused at <=10% with the exact detail, no command sent, valve_close allowed; user: valve_open refused at <=10% even while the valve is disconnected; refusal order RMLEAK before battery; council:valve battery-critical wire contract (S14/S17) never bench-run (F3, phaseE2F and council_final) | — |
| T2-06 | Plan S15: an unknown valve battery is `null`, never 0 (about 5 min, plus part B if hardware allows) | BUG-1 unknown battery is null, never 0 (and absent while disconnected); S15 missing battery characteristic / unknown battery (part B is N/A on a production FW 2.2.0 valve); F-10 (observe): rmleak/leak_state false while valve state is unknown during GATT setup | — |
| T2-07 | Plan S16: flapping 10 / 11 (about 10 min) | BUG-1 (valve battery <=10% critical / 11-20% warning / >20% excellent, valve only); S16 flapping 10/11: at most one health snapshot per flip, final state correct, no alerts; CHANGELOG: every valve battery edge publishes an event snapshot within seconds; in-band changes and steady re-notifies send none | — |
| T2-08 | E-16: valve link age while linked, and `offline_duration_s` from the drop (GOOD battery, about 20 min) | E-16 valve last_seen_age_s 0 while linked, counts from the drop; offline_duration_s from the drop (180-215) | — |
| T2-09 | Sensor battery bands never reach critical (optional; needs a low sensor battery) | E-15 excellent<->good change waits for the heartbeat (sensor battery 21-35%); user: sensor battery bands unchanged (20/35), never critical | — |
| T3-01 | Sensors-only hub with an unprovisioned valve nearby: a leak never touches it (S18) | S18 sensors-only hub + unprovisioned neighbour valve: no scan/connect/command; P0-a valve matched by provisioned MAC only (no name match, no link to neighbour); User decision: no auto_close on a hub with no provisioned valve (502178c); leak_detected still sent; N9 / 2.1.4 wire change: valve_open/valve_close/valve_set_state/override_enable refused with 'No valve is set up for this hub.'; decommission valve on a valve-less hub -> 'valve decommission failed'; RMLEAK auto-clear 10 s (lands 10-12 s) and amber/YELLOW interlock floor timing | — |
| T3-02 | Reboot a sensors-only hub: BLE starts and the sensors are scanned and reported (S19) | User decision: no auto_close on a hub with no provisioned valve (502178c); leak_detected still sent; S19 / P0-b BLE starts on a sensors-only hub; sensors scanned and reported after reboot; N2/N3 boot order: Starting BLE and QueueSet Initialized before Wi-Fi IP | — |
| T3-03 | Leak right after a valve decommission: no close, no `auto_close`, nothing sent to the removed valve (E-11) | User decision: no auto_close on a hub with no provisioned valve (502178c); leak_detected still sent; E-11 auto_close/rmleak_asserted for a just-removed valve; old link not counted; P0-c commands refused/flushed are never replayed on the next valve | — |
| T3-04 | Valve commands refused with no valve are never replayed on the next valve (P0-c) | N9 / 2.1.4 wire change: valve_open/valve_close/valve_set_state/override_enable refused with 'No valve is set up for this hub.'; decommission valve on a valve-less hub -> 'valve decommission failed'; P0-c commands refused/flushed are never replayed on the next valve; Valve last_seen_age_s 0 while linked (E-16) in snapshots | — |
| T3-05 | Re-provision a different valve MAC while linked (S22) | P0-a valve matched by provisioned MAC only (no name match, no link to neighbour); P0-c commands refused/flushed are never replayed on the next valve; S22 re-provision a different valve MAC while linked: old link dropped, flush line with counts, nothing reaches the new valve; council:F4 valve identity and target change while a link or connect is in flight (CP2 valve churn); E-08 valve health events from the old valve dropped after a swap | — |
| T3-06 | Valve swap with the old valve flooded (E-04, plus the removal and wet-sensor variants) | E-04 old valve leak source dropped on swap; new dry valve not auto-closed; leak_reset accepted; d37eb6c latch released on removal of a flooded valve (no rmleak_auto_cleared); Sensor still wet across a swap: new valve closed on first link (Priority 1); Valve last_seen_age_s 0 while linked (E-16) in snapshots | — |
| T3-07 | Quick decommission and re-provision of the same valve: it still relinks after its next drop (E-09) | E-09 quick decommission + re-provision of the same valve: rescan after next drop; Known limitation: decommission+re-provision in one pass can leave health connected/excellent with no link (observe) | — |
| T3-08 | Foreign LoRa sensor: packets are not evaluated by the rules engine (S23, N4) | S23 / N4 foreign LoRa sensor not evaluated by rules; E-18 no prov_pkt snapshot | — |
| T3-09 | `valve_open` is refused while a leak is latched, even with the valve powered off (E-06 decision) | E-06 user decision: valve_open/valve_set_state open refused with the RMLEAK message while latched, valve powered off; RMLEAK auto-clear 10 s (lands 10-12 s) and amber/YELLOW interlock floor timing | — |
| T3-10 | An OPEN pended while the valve is away is overwritten by a leak's close (E-06) | E-06 user decision: valve_open/valve_set_state open refused with the RMLEAK message while latched, valve powered off; E-06 pended OPEN overwritten by the leak's pended RMLEAK+CLOSE; RMLEAK first at reconnect | — |
| T3-11 | F-01(a): the hub's own auto-clear, pended while the valve is out of range, is not read as a button press | F-01 route (a): hub auto-clear pended while valve out of range - no re-latch, no false button override (pass criteria per HANDOFF s7 / 49defb4 / council_final F3 note); RMLEAK auto-clear 10 s (lands 10-12 s) and amber/YELLOW interlock floor timing; Valve last_seen_age_s 0 while linked (E-16) in snapshots | — |
| T3-12 | F-01(b): `leak_reset` sent while the valve is out of range | F-01 route (c) leak_reset while valve unlinked; F-05 clear queued under the rules lock | — |
| T3-13 | F-01(c): hub power-cycled with the incident latched and every sensor dry | F-01 route (b) with F-02: hub power-cycled with latch and sensors dry - exactly one auto-clear, no override; RMLEAK auto-clear 10 s (lands 10-12 s) and amber/YELLOW interlock floor timing | also runs T4-11 Part B |
| T3-14 | F-01(d): a clear owed at a hub restart is lost with RAM; the reconnect fails closed and the auto-clear releases it | council:clear owed lost at hub restart -> re-latch (fail closed) then 10 s auto-clear releases it; RMLEAK auto-clear 10 s (lands 10-12 s) and amber/YELLOW interlock floor timing | — |
| T3-15 | A genuine valve long-press still starts the 24 h window (SRS 4.4.2), including after a hub restart | council:genuine valve long-press still starts the 24 h window (SRS 4.4.2), incl. after hub restart; press right after close fails closed (observe) | also runs T5-08 |
| T3-16 | B1(a): hub power-cycled with a sensor wet and the valve open - the close is not delayed by a busy GATT pool | B1 case (a): hub power-cycle with sensor wet and valve open - CLOSED within ~3 s of SETUP COMPLETE, no rc=6 forced relink | — |
| T3-17 | B1(b): leak while the valve is out of range - RMLEAK before CLOSE at the relink, no forced relink | B1 case (b) + RMLEAK-before-CLOSE across a relink (dcf0e07); council:setup-straddle hold, RMLEAK before CLOSE when the close is dequeued at setup completion (d3b9ef8); Valve last_seen_age_s 0 while linked (E-16) in snapshots | — |
| T3-18 | B1(c): re-wet at the moment of the auto-clear (F-08 order, stale-confirm residual) | B1 case (c) re-wet at the clear; F-08 rmleak_auto_cleared published before the re-latch; b245d94 re-lock; council:stale-confirm false 'button' window on a slow RMLEAK read-back (observe, 2.1.5) | main runs are T5-04; the stale-confirm observe part stays in T3-18 |
| T3-19 | Valve identity: a bonded valve reconnects normally (identity address = provisioned MAC) | P0-a valve matched by provisioned MAC only (no name match, no link to neighbour); Plan risk: bonded valve reconnects (identity address desc.peer_id_addr = provisioned MAC) | — |
| T3-20 | Known limitation F-03 (deferred to 2.1.5): leak latched while the valve was away, then a hub restart - observe and record | F-03 known limitation (deferred): reconnect infers button override after latch-while-unreachable + restart (observe and record) | run as T4-12 |
| T3-21 | Known limitation F-02 (accepted): restart during a live leak can release RMLEAK for one wet burst - observe and record | F-02 known limitation (accepted): restart during a live leak can release RMLEAK for one wet burst (observe and record) | run as T4-11 Part A |
| T4-01 | Boot order: protection, QueueSet and BLE before Wi-Fi (about 5 min) | N1 (protection before Wi-Fi: boot order, no wait on IP/SNTP/DPS); N2/N3 (QueueSet complete and every add checked before BLE starts; no 'could not be added'); Log hygiene: Wi-Fi password and BLE passkey never on UART; P0-b (sensors-only hub starts BLE at boot) - optional variant; HANDOFF §7 CP2/CP3 bench items: boot order, router off, NTP blocked, captive portal, serial log | — |
| T4-02 | Plan S20: boot with the router off, then a leak closes the valve with no Wi-Fi (about 10 min) | N1 (protection before Wi-Fi: boot order, no wait on IP/SNTP/DPS); S20 (boot with router off; leak auto-closes with no Wi-Fi; events buffered and replayed); N1 pre-sync event hold + stamping at clock sync + ordered replay before lifecycle; HANDOFF §7 CP2/CP3 bench items: boot order, router off, NTP blocked, captive portal, serial log | — |
| T4-03 | Pre-sync events: stamped at the clock sync and replayed in order after the connect (about 5 min) | S20 (boot with router off; leak auto-closes with no Wi-Fi; events buffered and replayed); N1 pre-sync event hold + stamping at clock sync + ordered replay before lifecycle; E-22 (stamped pre-sync entry never written back over 512 B; no 'too long' drop); council:pre-sync event stamping (ts correct, in order, never < 1704067200); HANDOFF §7 CP2/CP3 bench items: boot order, router off, NTP blocked, captive portal, serial log | — |
| T4-04 | A restart before the clock syncs drops that boot's pre-sync events (about 10 min) | council:pre-sync events dropped after a restart before the clock syncs | — |
| T4-05 | NTP blocked: the SNTP fallback, and leak protection without a clock (about 15 min) | N1 (protection before Wi-Fi: boot order, no wait on IP/SNTP/DPS); N1 pre-sync event hold + stamping at clock sync + ordered replay before lifecycle; council:pre-sync event stamping (ts correct, in order, never < 1704067200); SNTP out of the boot path: 120 s fallback, 60 s re-poll, HANDOFF §7 NTP blocked; HANDOFF §7 CP2/CP3 bench items: boot order, router off, NTP blocked, captive portal, serial log | — |
| T4-06 | DPS unreachable: the in-loop backoff, the 5 min retry and the recovery (DESTRUCTIVE, about 45 min) | N1 (protection before Wi-Fi: boot order, no wait on IP/SNTP/DPS); DPS in-loop backoff 5-25 s, 6 boot attempts, 5 min retry; known limitation: live DPS registration blocks loop up to 60 s | — |
| T4-07 | Override started before the clock synced: timed on uptime, re-based at the sync (about 20 min) | Override started before clock sync: uptime timing, no expires_ts, re-base within ~30 s of sync (R10 wire shape) | — |
| T4-08 | Override started before the clock synced expires after 24 h of uptime with no internet (optional; 24 h, or about 15 min on a bench-only build) | Override started before clock sync expires after 24 h uptime with no internet (RMLEAK before CLOSE) | — |
| T4-09 | Override windows restored after a restart with no internet (E-03; about 30 min) | council:pre-sync events dropped after a restart before the clock syncs; E-03 (real-epoch override restored after power-on with no internet timed from power-on; auto_close_blocked_override carries override_remaining_s); council:override restored across a power-on with no internet; known limitation E-03 case B (unsynced window restored after reset ends at first sync) | — |
| T4-10 | Plan S21: captive portal with BLE scanning paused (about 115 min) | S21 (Wi-Fi reset / captive portal, heap recorded, leak during portal); 2026-09-29 portal fix: the portal priority window (phone joins and gets a lease, scanning paused, leak-response hunt, health hold and resume, router-fallback AP keeps BLE scanning); its follow-up (the window closes when the setup AP stops and the phone sees the success page, or when Wi-Fi is lost first; the valve held in the pause; the 10 s reset erases during a router outage); E-21 (captive portal no longer BLE-free); council:BLE beside SoftAP captive portal (heap free/min_ever/largest_blk, coexistence, worst case valve scan); council:NVS pressure - full offline ring next to portal credential save; council:heap profile with BLE started at boot (portal session figures recorded vs CP1 19,524 B / field 2,972 B); B1 RMLEAK-before-CLOSE replay at relink after portal (valve powered off during leak); Log hygiene: Wi-Fi password and BLE passkey never on UART; HANDOFF §7 CP2/CP3 bench items: boot order, router off, NTP blocked, captive portal, serial log | also runs T6-13; its Part C is T6-14 |
| T4-11 | Restart during a leak (F-02 accepted; F-01 route b) (about 40 min) | F-02 accepted (restart during a leak can release RMLEAK for one wet burst) - observe and record; council:F-02 on the wire; council:F-01 route (b) with F-02 (hub power-cycled mid-incident, sensors dried; exactly one AUTO-CLEAR, no false button override); E-06 / user decision: valve_open refused while incident latched, valve disconnected | Part A also runs T3-21; Part B is T3-13 |
| T4-12 | F-03 known limitation: a leak latched while the valve was unreachable, then a hub restart (observe and record; about 30 min) | F-03 deferred known limitation (reconnect button inference after restart) - observe and record; E-06 / user decision: valve_open refused while incident latched, valve disconnected | also runs T3-20 |
| T4-13 | N4: packets from sensors that are not provisioned do not reach the rules engine (optional; needs a neighbour sensor; about 10 min) | N4 (LoRa packets from unprovisioned sensors not evaluated by the rules engine) | — |
| T4-14 | Serial-log hygiene: no Wi-Fi password and no valve passkey on UART (about 5 min, after T4-01 and T4-10) | Log hygiene: Wi-Fi password and BLE passkey never on UART; HANDOFF §7 CP2/CP3 bench items: boot order, router off, NTP blocked, captive portal, serial log | — |
| T5-01 | Provision: per-advertisement snapshots, and "Syncing - waiting for N devices" clearing as each device is heard | S1 / BUG-2 preserved UI sync: per-advertisement pulse snapshots and 'Syncing - waiting for N devices' clearing as each device is heard; survivors keep battery/rssi/fw/last_seen; BUG-2 (removal does not re-arm sync or boot/commission; no 'Boot sync: timeout' 150 s after removal); E-09 (a provision naming the linked valve always requests CONNECT; '[SCAN] Already connected'); E-10 (command snapshot after provision/decommission shows the reconciled table); Twin reported refreshed after every device-set change | — |
| T5-02 | A provision that adds no device (identical re-send): one `event` snapshot, no pulse | BUG-2 (removal does not re-arm sync or boot/commission; no 'Boot sync: timeout' 150 s after removal); council:provision adding no device no longer restarts pulse/commission snapshot (documented 2.1.4 change); E-09 (a provision naming the linked valve always requests CONNECT; '[SCAN] Already connected'); Twin reported refreshed after every device-set change | run as DEC-02 (with its 6 min extended watch) |
| T5-03 | Leak → auto_close (RMLEAK before CLOSE) → dry → 10 s auto-clear → valve_open | S24 (plan) regression: leak -> auto_close -> leak_reset; override enable/cancel/expiry; RMLEAK read-back; twin heartbeat; offline replay; User decision / 75a4a59: RMLEAK auto-clear 10 s ('All sensors clear — auto-clear timer started (10s)', 'AUTO-CLEAR: all sensors clear for 10s', rmleak_auto_cleared clear_after_seconds:10 about 10-12 s after last dry; valve stays closed; LED RED->YELLOW->GREEN); F-01 (the hub's own RMLEAK clear never read as a valve-button override, online paths); RMLEAK applied before the valve command on every path (auto-close, override_enable, override_cancel, expiry, reconnect replay); feedback_auto_close_write_order; RMLEAK read-back after every hub RMLEAK write (cache freshness; 2.1.1 field defect); App-team flows B1/B2/C1/D1/D4/D5/D6/D8/E1/E2/E3/F2; E-15 (rating change into/out of warning publishes a prompt event snapshot); E-16 / user decision: valve last_seen_age_s is 0 while linked; P0-a wire identity: rules events valve_id is always the provisioned MAC | — |
| T5-04 | Re-wet at the clear (5 runs): release, then re-lock, never a false button override | User decision / 75a4a59: RMLEAK auto-clear 10 s ('All sensors clear — auto-clear timer started (10s)', 'AUTO-CLEAR: all sensors clear for 10s', rmleak_auto_cleared clear_after_seconds:10 about 10-12 s after last dry; valve stays closed; LED RED->YELLOW->GREEN); F-08 (tick rules event published before the pass's leak evaluation; rmleak_auto_cleared precedes the re-wet's leak_detected/auto_close; MQTT-reconnect-pass exception); docs-review b245d94 (re-wet at the release always re-asserts RMLEAK+CLOSE, no false button window); F-01 (the hub's own RMLEAK clear never read as a valve-button override, online paths); council:stale-confirm false 'button' window (record SET write->read-back gap) | also the main runs of T3-18 |
| T5-05 | The leak_reset / valve_open guard matrix | S24 (plan) regression: leak -> auto_close -> leak_reset; override enable/cancel/expiry; RMLEAK read-back; twin heartbeat; offline replay; F-01 (the hub's own RMLEAK clear never read as a valve-button override, online paths); F-05 (leak_reset queues its clear under the rules lock); User decision / E-06: valve_open and valve_set_state open refused with the RMLEAK message while RMLEAK set or incident latched, including valve disconnected; leak_reset refused while wet (exact detail text); accepted when dry; no-op reset; council:C2D-written rules events publish at the next pass top (rmleak_cleared / water_access_override_enabled / auto_close_reenabled after cmd_ack); App-team flows B1/B2/C1/D1/D4/D5/D6/D8/E1/E2/E3/F2; P0-a wire identity: rules events valve_id is always the provisioned MAC | — |
| T5-06 | override_enable (C2D) during a leak → blocked leak → override_cancel with the leak still active (re-close) | S24 (plan) regression: leak -> auto_close -> leak_reset; override enable/cancel/expiry; RMLEAK read-back; twin heartbeat; offline replay; RMLEAK applied before the valve command on every path (auto-close, override_enable, override_cancel, expiry, reconnect replay); feedback_auto_close_write_order; council:C2D-written rules events publish at the next pass top (rmleak_cleared / water_access_override_enabled / auto_close_reenabled after cmd_ack); App-team flows B1/B2/C1/D1/D4/D5/D6/D8/E1/E2/E3/F2 | — |
| T5-07 | override_cancel with no leak (D4), with no window (D5), and leak_reset cancelling a window | S24 (plan) regression: leak -> auto_close -> leak_reset; override enable/cancel/expiry; RMLEAK read-back; twin heartbeat; offline replay; leak_reset refused while wet (exact detail text); accepted when dry; no-op reset; council:C2D-written rules events publish at the next pass top (rmleak_cleared / water_access_override_enabled / auto_close_reenabled after cmd_ack); App-team flows B1/B2/C1/D1/D4/D5/D6/D8/E1/E2/E3/F2; P0-a wire identity: rules events valve_id is always the provisioned MAC | — |
| T5-08 | Valve long-press (physical) override: live, after a hub restart, and pressed right after the close | S24 (plan) regression: leak -> auto_close -> leak_reset; override enable/cancel/expiry; RMLEAK read-back; twin heartbeat; offline replay; council:genuine valve-button press still starts the window (confirmed-edge gate, SRS 4.4.2); council:press after hub restart mid-incident; council:missed press shortly after the close (fails closed; observe); F-02 (accepted known limitation: false auto-clear after a restart during a leak; observe and record) | run as T3-15, with two added checks |
| T5-09 | Override expiry (bench-only shortened build) | S24 (plan) regression: leak -> auto_close -> leak_reset; override enable/cancel/expiry; RMLEAK read-back; twin heartbeat; offline replay; RMLEAK applied before the valve command on every path (auto-close, override_enable, override_cancel, expiry, reconnect replay); feedback_auto_close_write_order; App-team flows B1/B2/C1/D1/D4/D5/D6/D8/E1/E2/E3/F2 | — |
| T5-10 | RMLEAK write and read-back audit (from the T5-03 … T5-09 UART capture) | S24 (plan) regression: leak -> auto_close -> leak_reset; override enable/cancel/expiry; RMLEAK read-back; twin heartbeat; offline replay; F-01 (the hub's own RMLEAK clear never read as a valve-button override, online paths); RMLEAK applied before the valve command on every path (auto-close, override_enable, override_cancel, expiry, reconnect replay); feedback_auto_close_write_order; RMLEAK read-back after every hub RMLEAK write (cache freshness; 2.1.1 field defect) | — |
| T5-11 | Heartbeat interval change through the twin desired property | S24 (plan) regression: leak -> auto_close -> leak_reset; override enable/cancel/expiry; RMLEAK read-back; twin heartbeat; offline replay; Twin snapshot_interval_s change, re-aim, reject echo, persistence and twin GET on connect (2.0.2 regression) | — |
| T5-12 | Offline event buffering and reconnect replay order | S24 (plan) regression: leak -> auto_close -> leak_reset; override enable/cancel/expiry; RMLEAK read-back; twin heartbeat; offline replay; User decision / 75a4a59: RMLEAK auto-clear 10 s ('All sensors clear — auto-clear timer started (10s)', 'AUTO-CLEAR: all sensors clear for 10s', rmleak_auto_cleared clear_after_seconds:10 about 10-12 s after last dry; valve stays closed; LED RED->YELLOW->GREEN); F-08 (tick rules event published before the pass's leak evaluation; rmleak_auto_cleared precedes the re-wet's leak_detected/auto_close; MQTT-reconnect-pass exception); council:F-08 offline order (rmleak_auto_cleared buffered before re-wet events; replay in order before lifecycle); Offline buffer (mutex, capacity, drain before lifecycle, non-decreasing ts) | — |
| T5-13 | LoRa leak path end to end (only if LoRa sensors and the SX1262 are fitted) | User decision / 75a4a59: RMLEAK auto-clear 10 s ('All sensors clear — auto-clear timer started (10s)', 'AUTO-CLEAR: all sensors clear for 10s', rmleak_auto_cleared clear_after_seconds:10 about 10-12 s after last dry; valve stays closed; LED RED->YELLOW->GREEN); N4 / LoRa leak path end to end (provisioned LoRa sensor evaluated; auto-clear from a LoRa source) | — |
| T5-14 | Contract validator over the full section capture: 0 FAIL | Validator run on the full capture: 0 FAIL, no ordering or valve-less auto_close violations | section 5 instance of VAL-15 |
| T5-15 | override_enable with an unreachable valve while a leak starts: leak handling delay of 10 s or less | RMLEAK applied before the valve command on every path (auto-close, override_enable, override_cancel, expiry, reconnect replay); feedback_auto_close_write_order; council:iothub_task stall behind blocking C2D (override_enable with valve off; eleak->AUTO-CLOSE <=10 s) | run as T6-18 |
| T5-16 | Flapping sensor soak with the 10 s auto-clear (15 min) | User decision / 75a4a59: RMLEAK auto-clear 10 s ('All sensors clear — auto-clear timer started (10s)', 'AUTO-CLEAR: all sensors clear for 10s', rmleak_auto_cleared clear_after_seconds:10 about 10-12 s after last dry; valve stays closed; LED RED->YELLOW->GREEN); F-01 (the hub's own RMLEAK clear never read as a valve-button override, online paths); council:10 s auto-clear flap write rate / owed-clear re-queue (NVS, heap, valve still commandable) | run as T6-15 |
| T6-01 | Power cut during a single-device decommission | S25 power loss mid-decommission: provisioning is the old set or the new set, never a mix; BUG-2 survivors unchanged after a real removal; council:power cut mid-decommission-all; F4.NB3 items 2/3/4 | — |
| T6-02 | Power cut during decommission-all | S25 power loss mid-decommission: provisioning is the old set or the new set, never a mix; BUG-3 / BUG-6 empty-hub boot snapshot shape after a decommission-all power cut; S11 power-cut half; council:power cut mid-decommission-all; F4.NB3 items 2/3/4; Known limitation: DPS registration blocks iothub_task up to 60 s (record duration) | also the DEC-18 power-cut variant |
| T6-03 | Upgrade 2.1.3 → 2.1.4 without erase | S25 upgrade 2.1.3 -> 2.1.4 keeps provisioning ('Hub is PROVISIONED', counts unchanged); N1 BLE starts before Wi-Fi at boot (upgrade boot order); E-03 override window restored after a power-on that lost the clock (upgrade path); council:upgrade/rollback without erase; council:power cut mid-decommission-all; F4.NB3 items 2/3/4 | also VAL-02 runs A and B |
| T6-04 | Rollback 2.1.4 → 2.1.3 without erase | S25 rollback 2.1.4 -> 2.1.3 keeps provisioning; E-22 (2.1.3 reads every offline entry); council:upgrade/rollback without erase | also runs VAL-03 |
| T6-05 | Repeated command IDs | S12 repeated id decom-b-002, unknown MAC, valve decommission with no valve; command-id de-dup deferred; cmd_ack contract (id echoed, id truncated at 63, no ack on parse failure, unknown command); BUG-2 survivors unchanged after a real removal; council:provision adding no device does not re-arm the pulse | — |
| T6-06 | Unknown device IDs, unknown targets and commands, malformed messages | S12 repeated id decom-b-002, unknown MAC, valve decommission with no valve; command-id de-dup deferred; cmd_ack contract (id echoed, id truncated at 63, no ack on parse failure, unknown command) | — |
| T6-07 | Heap like-for-like: valve + 4 BLE hub rebooted with Wi-Fi (2.1.3 vs 2.1.4) | S26 like-for-like heap MONITOR free/min_ever/largest_blk; pass = 2.1.3 minus ~0.3 KB; E-20 heap budget (static RAM +~181 B, ~50 B permanent heap); council:heap after G4b unmeasured | VAL-13 case (a) |
| T6-08 | Heap like-for-like: commissioning from empty (the field scenario) | S26 like-for-like heap MONITOR free/min_ever/largest_blk; pass = 2.1.3 minus ~0.3 KB; E-20 heap budget (static RAM +~181 B, ~50 B permanent heap); N1 BLE starts before Wi-Fi at boot (upgrade boot order); council:heap after G4b unmeasured; Known limitation: DPS registration blocks iothub_task up to 60 s (record duration) | VAL-13 case (b) |
| T6-09 | Heap on a sensors-only BLE hub (reported separately) | S26 sensors-only BLE hub heap reported separately (P0-b: now runs NimBLE); council:sensors-only NimBLE, SAS renewal and Wi-Fi reconnect | — |
| T6-10 | Large hubs: 20 and 33 devices, and 16 BLE sensors with 31-character labels (E-01, known limitation) | E-01 snapshot heap on 20+ / 33-device hubs and 16 BLE with 31-char labels (known limitation, observe); council:E-01 large hub | — |
| T6-11 | Task stack high-water marks (bench-only debug image, never shipped) | council:stack headroom / no HWM instrumentation; council:nimble_port_init retry cannot recover an NPL alloc failure (observe) | — |
| T6-12 | Router off/on × 5 and a ≥ 19 h soak with a SAS renewal (sensors-only BLE hub) | council:sensors-only NimBLE, SAS renewal and Wi-Fi reconnect; CHANGELOG 'Not changed': sensors-only leak sends rmleak_auto_cleared without valve_id, no auto_close; P0-b | — |
| T6-13 | Wi-Fi reset and captive portal beside BLE: heap, protection, credentials | E-21 / S21 captive portal beside BLE: heap, protection, credentials persist; council:BLE at boot beside SoftAP portal | run as T4-10 |
| T6-14 | NVS pressure: a full offline buffer, then a Wi-Fi reset and a power cycle | council:nvs partition pressure and F-08 offline double publish; Known limitation: offline buffer 16 entries, oldest overwritten | also runs T4-10 Part C |
| T6-15 | A sensor flapping every 15–20 s for 15 min (the 10 s auto-clear write rate) | council:10 s auto-clear write rate + relinks with a clear owed | also runs T5-16 |
| T6-16 | Sustained ATT stall on a live valve link, bounded by the 30 s GATT timeout | B1 sustained ATT stall bounded by the 30 s GATT timeout; RMLEAK before CLOSE at relink; council:sustained ATT stall / busy GATT pool; mbuf ENOMEM, relink cap, busy command task | — |
| T6-17 | Valve command queue saturation | council:command-queue saturation; live-path hold without queue guard | — |
| T6-18 | `iothub_task` stalled behind a blocking C2D (`override_enable` with the valve off, then a leak within 2 s) | council:iothub stall behind blocking C2D, <=10 s | also runs T5-15 |
| T6-19 | A leak sensor unheard during a hanging valve connect (≤ 30 s, known limitation) | council:sensors unheard during pending valve connect, deferred | — |
| T6-20 | BLE start claimed twice at once (boot apply and C2D `provision`) | council:atomic BLE start claim | — |
| VAL-01 | Build checkpoint 3 gate (d9fa9c8) (P0) | CP3 build gate (d9fa9c8): warnings, .bss 36,280-36,288, .data 21,572, IRAM unchanged, version; council final vote 'pending a clean CP3'; E-20 static RAM budget | — |
| VAL-02 | Upgrade 2.1.3 → 2.1.4 in place keeps provisioning, rules, the incident latch and the override (P0) | S25 upgrade 2.1.3 -> 2.1.4 without erasing flash (no OTA client in 2.1.4; app-only flash); council:upgrade/rollback keeps provisioning, rules opt-out, latched incident, override (F5); F-01 (owed-clear flag is RAM only; owed line must not appear after a boot); user decision: RMLEAK auto-clear 10 s (lands 10-12 s) | runs A and B are T6-03; run C stays in VAL-02 |
| VAL-03 | Rollback 2.1.4 → 2.1.3 keeps provisioning, and 2.1.3 reads the 2.1.4 offline buffer | S25 rollback 2.1.4 -> 2.1.3; council:upgrade/rollback keeps provisioning, rules opt-out, latched incident, override (F5); E-22 (stamped offline entries readable by 2.1.3) | run as T6-04 |
| VAL-04 | The app shows the right devices after a provision, with the syncing state (P0) | BUG-2 (UI-sync kept, survivors untouched); S1; council:provision that adds no device no longer re-arms pulse/commission (F3); P0-a (no neighbour valve; valve B never appears) | — |
| VAL-05 | The app shows the survivors unchanged after one device is removed (P0) | BUG-2 (UI-sync kept, survivors untouched); S2 | — |
| VAL-06 | The app's empty state: last device removed, heartbeats, reboot (P0) | BUG-3; BUG-5 (valve {}); BUG-6 (empty hub publishes heartbeats, boot, lifecycle); S8; S9 | — |
| VAL-07 | The app shows "no valve" for a sensors-only hub (valve `{}`), and the no-valve command refusal (P0) | BUG-5 (valve {}); P0-a (no neighbour valve; valve B never appears); P0-b (sensors-only hub starts BLE); P0-c (valve commands with no valve refused); S18/S19 UI half | — |
| VAL-08 | The app shows valve battery unknown, Low and Critical, and the battery refusal (P0) | BUG-1 (valve-only battery bands, unknown battery null never 0); S14; S17 (valve_open refused at <=10 %) | — |
| VAL-09 | The app during a leak: locked, the refused open, the 10 s auto-clear, and the re-wet order (P0) | E-06 / user decision: valve_open refused while incident latched, valve disconnected; user decision: RMLEAK auto-clear 10 s (lands 10-12 s); F-08 (rmleak_auto_cleared before re-wet leak_detected/auto_close); b245d94 re-wet at the clear never read as button override; council:app renders release then re-lock (F3); council:rmleak_auto_cleared may be missing in an MQTT-reconnect pass (Known-limit) | — |
| VAL-10 | The app's override state | S24 override enable/blocked/cancel (app half) | — |
| VAL-11 | The app after a device goes offline and comes back, with a connected valve's age at 0 | E-16 (valve last_seen_age_s 0 while linked, offline_duration_s from drop); device_offline reachability-only wire change; S24 offline/recover | — |
| VAL-12 | The app reads every new 2.1.4 wire shape without an error (app parser sweep) | BUG-5 (valve {}); BUG-1 (valve-only battery bands, unknown battery null never 0); CHANGELOG 'cloud and app parsers must accept' list; E-19; no auto_close on a valveless hub (user decision); provision that empties the hub (E-05 path) | — |
| VAL-13 | Heap like-for-like, 2.1.4 against 2.1.3 (P0) | E-20 static RAM budget; S26 heap like-for-like >= 2.1.3 (median of 3 runs; <=300 B shortfall needs written waiver); E-01 snapshot heap / largest block; council:heap after G4b (BLE at boot) unmeasured; sensors-only hubs now run NimBLE (F2) | measured with T6-07 and T6-08; the pass rule stays in VAL-13 |
| VAL-14 | Soak, no reboot, SAS renewal (P0) | E-01 snapshot heap / largest block; council:heap after G4b (BLE at boot) unmeasured; sensors-only hubs now run NimBLE (F2); exit criterion: no reboot in the soak (>=19 h, covers SAS renewal ~18 h); council:SAS renewal / TLS with less heap (F2); council:stack margin carry-over, no canary panic (F1/F4) | — |
| VAL-15 | Validator on the full capture (P0) | F-08 (rmleak_auto_cleared before re-wet leak_detected/auto_close); validator 0 FAIL on the full capture; all wire changes; N1 ts >= 1704067200 | — |

### M.8 Untraced items (required or listed, but no test in this plan)

1. F-09 (deferred to 2.1.5): a valve that went offline with its flood probe wet and returns dry after more than 180 s sends a late device_offline, then device_recovered 60-90 s later. No test (E2F c[2]#5, c[3]#6, c[2] NB3; Final v[2]#9). It could be observed by wetting the valve flood probe, powering the valve off, drying the probe and powering the valve on more than 180 s later.
2. Final v[3]#7: a clear from override_enable that pends because the link drops re-latches through Priority 0 on the stale RMLEAK 1 (fails closed). No test.
3. override_enable on a battery-critical valve still starts the window while the valve refuses to open (plan: deferred, noted). No test.
4. E-12: the valve re-check after a device-set change can raise but never clear a valve leak (a disconnect race). No deterministic bench trigger.
5. E-13 / E-14: cJSON add failures (freed sub-objects, no partial snapshot counted as sent). Needs an allocation fault; only indirect evidence from T6-10.
6. G4d / N13: duplicate sensor ids in a provision payload are ignored. Now tested by DEC-19 (kept here for numbering).
7. G4d: a corrupt sensor count in NVS is clamped to 16 (`... count %u in NVS exceeds %d - clamped`). Needs corrupted NVS; T6-01 only checks that the line never prints.
8. G4c: a disconnect the controller refuses (BLE_GAP_EVENT_TERM_FAILURE, `[DISCONNECT] terminate failed status=%d - link stays up`) no longer blocks valve commands. Cannot be induced on the bench.
9. G4d: sensor metadata copied out under its lock. Internal, nothing observable.
10. 2.1.5 known limitation: a leak re-assert decided in evaluate_leak is queued after the rules lock is released, so a leak_reset landing in that gap is overridden (fails closed). No deterministic test.
11. F-04 (2.1.5 known limitation): a leak evaluated while provisioning is busy for more than 1 s is dropped until the sensor reports again. No deterministic test (only the validator false positive of 0.18 points at it; M.3 row F-04).
12. Deferred items with no test by design: the legacy keyword scan of C2D payloads (never exercised: it can wipe the hub), more than 16 simultaneous leak sources, LoRa driver hardening.

## S. Smoke subset (about 30 minutes)

Run this on every new build, before the full campaign, and again after any fix. It covers the most important changes of 2.1.4 in one chained run. It does not replace the full campaign.

**Start state.** VAL-01 already passed (its build, steps 2-4, is not counted in the 30 min). `SS-V4` provisioned with labels (`ss-v4m`, 0.8): valve A + `<BLE1>` "Sink", `<BLE2>` "Washer", `<BLE3>` "Ensuite", `<BLE4>` "Main", all heard, dry, valve open, battery Good (valve on the bench PSU at 6.00 V if you will run step 7), no incident, no override, LED GREEN. Snapshot interval 60 s (0.9). Valve B powered within 2 m (for step 8). One UART capture (`SMOKE_uart.txt`) and one IoT Hub capture (`SMOKE_iothub.txt`, Azure CLI, 0.7 option B) running for the whole smoke run.

In the tests below, A (section 4, section 5) and `<S1>` (section 3) are `<BLE1>`; `<S2>` is `<BLE2>`; `<VALVE>`, `<V>` and `<VALVE_A>` are valve A.

| # | Run | Time | What it proves | State after |
|---|---|---|---|---|
| 1 | **VAL-01** steps 1, 5 and 6: the image is the build under test (`cc66d72` until the CP4 re-baseline, header notes), `.bin` newer than the commit, boot shows `HUB_IDENT: Firmware version: v2.1.4`. | 2 min | the build under test | `SS-V4` |
| 2 | **T4-02** rows 1-4: router off, power-cycle the hub, wait for the valve and sensors, wet A, then dry A. | 6 min | N1: protection with no Wi-Fi and no clock; leak → `auto_close` with `[CMD] Writing RMLEAK=1` before `[CMD] Writing Valve=0`; the 10 s auto-clear (`AUTO-CLEAR` 10-12 s after the dry report); LED RED → YELLOW → GREEN; every event `holding event for replay` | valve closed, RMLEAK clear, 5 events held |
| 3 | **T4-03** rows 1-3: router on. Then `valve_open` (T4-03 Pass line). | 3 min | pre-sync events stamped at the clock sync and replayed in order before the lifecycle, with real `ts` (none below 1704067200) | `SS-V4` |
| 4 | **T5-04**, one run with Δ ≥ 0 (redo it if the re-wet lands before the clear). Then dry, wait for the clear, send `valve_open`. | 3 min | re-wet at the clear: `rmleak_auto_cleared` → `leak_detected` → `auto_close`; the valve ends CLOSED with `[DATA] RMLEAK=1 (ACTIVE)`; no `RMLEAK cleared externally` (F-08, `b245d94`) | `SS-V4` |
| 5 | **T3-11**, one run (shield the valve, do not power it off). | 4 min | F-01: the hub's own clear, pended while the valve is out of range, is applied at the relink and never read as a button press; no `water_access_override_enabled` | `SS-V4` |
| 6 | **DEC-03** steps 1, 2, 3 and 5; shorten step 4 to 60 s. | 3 min | BUG-2: the snapshot after the removal equals the previous one minus `<BLE4>`; survivors keep `battery`, `rssi`, `fw_version`, `rating`; no `Boot sync: timeout`, no pulse | valve + `<BLE1>`…`<BLE3>` |
| 7 | **T2-01** steps 1, 3, 4 and 5 (the valve power-cycle fast path; skip the wait of step 2). **Only if the bench PSU is ready.** Without a PSU, record `Blocked (no PSU)` here and run T2-01 in the full campaign. | 5 min | BUG-1/S17: valve battery ≤ 10 % → valve and hub `critical`, "Valve battery critical", RED, no health event; `valve_open` refused with "Valve battery critical (≤10 %): the valve will not open. Replace the batteries."; recovery to GREEN | valve open, PSU at GOOD |
| 8 | **T3-01** steps 1-4 and 9 (step 2: wait 1 min). | 4 min | P0-a: after the valve decommission the hub never scans for, links to or commands the powered neighbour; a leak publishes `leak_detected` and **no** `auto_close`; `valve_open` refused "No valve is set up for this hub."; BUG-5 `"valve":{}`; `rmleak_auto_cleared` without `valve_id` | `<BLE1>`…`<BLE3>`, no valve |
| 9 | **DEC-11**, short form: send `dec-11-s1` (trigger mask 3); remove `<BLE1>` and `<BLE2>` (ids `smoke-9a`, `smoke-9b`); wet `<BLE3>`; remove `<BLE3>` (id `smoke-9c`) while it is wet; watch one heartbeat. Expected as DEC-11 steps 1-6 with `<BLE3>` in place of `<BLE4>` (the reason reads "Leak detected: Ensuite, Leak interlock latched"). | 4 min | BUG-3/BUG-6: one `event` snapshot of the empty shape (R-EMPTY) with `"rules":{"auto_close_enabled":true,"trigger_mask":7}` and no "Leak interlock latched"; LED `rating=unprovisioned color=WHITE`; a `heartbeat` 60 s later (−1/+3 s) | empty |
| 10 | **T4-10** A2-A4 and A8, short form (added 2026-09-29, revised for the follow-up). First re-provision `SS-V4` with `ss-v4m` (0.8): the step 9 hub is empty, and an empty hub starts no BLE. Wait until the valve and `<BLE1>`…`<BLE4>` are heard. Then hold the Wi-Fi reset button 10 s, join `WiFi-Hub-<SHORT>` from the phone and open the portal, then submit the site credentials and keep the phone on the page. | 10 min | the portal fix, on a valve hub: `APP_WIFI: WiFi Disconnected. Reason: 8` and `RESET_BTN: Wi-Fi credentials erased from NVS` before `RESET_BTN: Rebooting into AP mode...`; after the reboot `APP_WIFI: portal priority ON (no Wi-Fi credentials) - BLE scanning paused`, `portal priority: wifi_manager task prio 5 -> 8 (httpd, dns_server not raised)`, `BLE_LEAK: Scan paused - Wi-Fi setup portal has the radio` and `BLE_VALVE: [SCAN] Valve scan held - Wi-Fi setup portal has the radio`; while the window is open **no** `Extended passive scan started`, **no** `[SCAN] Starting scan for provisioned valve`, **no** `Roll-up grace expired`, and the fleet LED stays WHITE (the valve is held, never RED); the phone's `APP_WIFI: SoftAP: station … joined, AID=%u` followed by `esp_netif_lwip: DHCP server assigned IP to a client` within 5 s, and the page loads; after the submit `Connected! IP` and `portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops (about 60 s)`, and the phone shows the portal's "Connected!" page; 60 s (±5 s) after `Connected! IP`, `HEALTH_ENGINE: BLE scanning resumed - BLE sensor timeouts restart now (600 s)`, `portal priority OFF (AP stopped) - BLE scanning resumed`, `BLE_LEAK: Scan resumed - Wi-Fi setup portal closed`, `[PORTAL] Valve hunt resumed - Wi-Fi setup portal closed`, `[SCAN] Starting scan for provisioned valve`, `SETUP COMPLETE - READY FOR GATT`; each `<BLE1>`…`<BLE4>` `eleak … — leak=0` again within about 100 s of `portal priority OFF` (the hunt forwards their reports until the valve links); **no** `device_offline` | `SS-V4`, online |
| 11 | **VAL-15** step 1 on `SMOKE_iothub.txt`. | 1 min | the wire contract of every smoke message: 0 FAIL, no `--- ORDERING` block, no `--- AUTO_CLOSE FROM A HUB WITH NO VALVE` block | — |

**Total:** about 45 min with step 7, about 40 min without it.

The step 9 removal commands (one line each):

```json
{"schema":"eflostop.cmd","ver":1,"id":"smoke-9a","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE1>"}}
{"schema":"eflostop.cmd","ver":1,"id":"smoke-9b","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE2>"}}
{"schema":"eflostop.cmd","ver":1,"id":"smoke-9c","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE3>"}}
```

**Smoke pass rule.** Every step passes (step 7 may be `Blocked (no PSU)` only). Any `Guru Meditation`, `abort()`, `stack overflow`, watchdog, unplanned reboot, `RMLEAK cleared externally`, `write attempt … (rc=6)` or `Snapshot not built - out of memory` anywhere in `SMOKE_uart.txt` fails the smoke run (Appendix B.2 has the grep).

**After the smoke run:** the hub is back on `SS-V4` (step 10). Open the valve if it is closed.

| Step | Result | Notes |
|---|---|---|
| 1 VAL-01 | | |
| 2 T4-02 | | |
| 3 T4-03 | | |
| 4 T5-04 | | |
| 5 T3-11 | | |
| 6 DEC-03 | | |
| 7 T2-01 | | |
| 8 T3-01 | | |
| 9 DEC-11 | | |
| 10 T4-10 portal | | |
| 11 VAL-15 | | |

## 1. Decommission, UI sync and the empty hub (BUG-2, BUG-3, BUG-5, BUG-6; plan S1–S13)

This section checks four things:
- A removal leaves every other device's state alone (BUG-2).
- The post-provision UI sync ("Syncing - waiting for N devices", the snapshot pulse, the boot/commission snapshot) still works, and it applies only to devices not yet heard.
- The valve block is `{}` when no valve is provisioned (BUG-5).
- A hub with no devices publishes correct snapshots: once on the transition, at every heartbeat, and once per boot or MQTT connect (BUG-3, BUG-6).

It also covers the Phase E findings on this area (E-02, E-05, E-09, E-10, E-17, E-18), the error paths of `decommission` (S12), `decommission` `all` (S11), and the council's residual risks on the empty hub and decommission-all.

Every UART line and JSON shape below was checked against the firmware at `d9fa9c8`. The main sources are:
- `app_iothub.c`: `handle_c2d_command`, `apply_device_set_change`, `on_hub_emptied`, `reset_rules_state_hub_emptied`, `sync_valve_detectors`, `purge_telemetry_caches`, the snapshot scheduler and the flush block;
- `provisioning_manager.c`: `provisioning_remove_*`, `mark_unprovisioned_if_empty`, `provisioning_handle_azure_payload_json`, `provisioning_decommission`;
- `health_engine.c`: `health_engine_reconcile_devices`, `check_boot_sync_locked`;
- `telemetry_v2.c`: `telemetry_v2_publish_snapshot`, `build_system_health_reason`, `telemetry_v2_publish_lifecycle`;
- `rules_engine.c`: `rules_engine_reset_all`, `rules_engine_forget_unprovisioned`, `rules_engine_on_valve_replaced`;
- `app_ble_leak.c` and `app_ble_valve.c` (`ble_valve_set_target_mac`, `report_valve_cmd_flush`);
- `fleet_led.c`.

Where a document disagrees with the code, the code wins, and the test says so.

### 1.0 Common setup for this section

**Start states, identities and commands.** Use the standard start states `SS-EMPTY`, `SS-V4`, `SS-V4L` and `SS-S2` (0.16), the placeholders `<VALVE_MAC>`, `<BLE1>`…`<BLE4>`, `<LORA1>` and `<GW>` (0.5), and the C2D rules in 0.8. The `id` of each command is `<testid>-s<step>`, except where a test gives an exact id (`decom-b-002` in DEC-10).

**Labels.** This section provisions the BLE sensors with the labels of the `ss-v4m` payload (0.8), so `system_health.reason` names them:

| Sensor | `location.code` | `location.label` |
|---|---|---|
| `<BLE1>` | `kitchen` | `Sink` |
| `<BLE2>` | `laundry` | `Washer` |
| `<BLE3>` | `bathroom` | `Ensuite` |
| `<BLE4>` | `bathroom` | `Main` |

**Phantom sensors.** Some tests need a device that is provisioned but never heard. Use MACs that no nearby device has: `<PH1>` = `00:80:E1:00:00:01`, `<PH2>` = `00:80:E1:00:00:02` and `<PH3>` = `00:80:E1:00:00:03`. The hub accepts them (any valid `XX:XX:XX:XX:XX:XX`), and they stay "not heard" for as long as they are provisioned. Their snapshot entry is `{"sensor_id":"00:80:E1:00:00:01","connected":false,"rating":"critical","last_seen_age_s":null,"battery":null,"rssi":null,"leak_state":false,"fw_version":null,"location":{"code":"unknown","label":""}}`. The per-device `rating` of an unheard device is `critical`, but the device is left out of `system_health` until its grace ends.

**Heartbeat interval.** Every test uses 60 s (0.9) unless it says otherwise. Every confirmed snapshot of any reason re-arms the heartbeat, so a heartbeat comes 60 s after the **last** snapshot. Heartbeat snapshots (`SNAP trigger=heartbeat`, `data.reason` `"heartbeat"`) are always allowed and are not counted as "extra" snapshots in this section.

**Which task prints what.** A C2D command is handled on the esp-mqtt task. That task prints the `C2D_CMD`, `PROVISIONING`, `SENSOR_META` and `RULES_ENGINE` reset lines, `IOTHUB: !!! DECOMMISSION_… !!!` and the `cmd_ack` (`Pub event:`). The device-set change is then applied by `iothub_task` at the top of its next loop pass. That pass prints `HEALTH_ENGINE: Device table loaded…`, the cache purge, the valve detectors, `Twin reported`, `SNAP trigger=…` and `Pub snapshot:`. Lines from the two tasks, and from the BLE tasks, can interleave. Order is guaranteed only within one task, and the `cmd_ack` always goes out before the snapshot that follows it.

**Section-wide fail conditions.** Any of these, at any time in this section, is a Fail for the running test unless that test expects it:
- `TELEMETRY_V2: Snapshot not built - out of memory`, `TELEMETRY_V2: Message not built (%s) - out of memory` (format), `TELEMETRY_V2: Snapshot deferred - health table busy`;
- `HEALTH_ENGINE: Device table full (%d): %u provisioned device(s) not tracked` (format);
- `IOTHUB: Hub empty: rules-engine RAM reset failed - retry owed`;
- `IOTHUB: Device-set change: reconcile deferred, retrying` or `IOTHUB: Telemetry-cache purge: provisioning busy, retrying` more than twice in a row (once, followed by a successful `Device table loaded` within about 2 s, is allowed; record it: E-17);
- any reboot the test did not ask for (a `Guru Meditation`, `abort()`, a stack-overflow message, or a lifecycle `reset_reason` other than `power_on` / `software`);
- a snapshot published after a command's `cmd_ack` that still shows the device that command removed (E-10).

**Observe and record everywhere in this section (known limitations, CHANGELOG 2.1.4):**
- Twin reported (and the lifecycle `provisioned` flag) can read `provisioned:false`, `valve_id` null and counts 0 when provisioning is busy for more than 1 s. The next device-set change republishes it. If you see a decommissioned-looking twin on a hub that still has devices, record it as `Known-limit`, with the time.
- The hub does not de-duplicate command ids. The same `id` twice runs twice.

**Heap.** Read the `MONITOR: heap: free=%lu min_ever=%lu largest_blk=%lu uptime=%lus` (format) line at the start and the end of each test that says so. The hard floor is the 2.1.3 field minimum, `min_ever` ≥ 2,972 B. The CP1 bench figure for a valve + 4 BLE hub was 19,524 B, so a value more than about 1 KB below that needs a note. The heap verdict itself belongs to VAL-13 (measured with T6-07 and T6-08); 0.12 explains the line.

**Suggested run order.** The tests chain, so each one ends in the next one's start state. The whole section takes about 2 h 45 min.

| Order | Test | Starts from | Ends in | Time |
|---|---|---|---|---|
| 1 | DEC-01 | `SS-EMPTY` | V + BLE1–4, all heard | 12 min |
| 2 | DEC-02 | DEC-01 end | same | 3 min |
| 3 | DEC-03 | same | V + BLE1–3 | 5 min |
| 4 | DEC-04 | DEC-03 end | V + BLE2–4 | 13 min |
| 5 | DEC-05 | DEC-04 end | V + BLE1–4 | 6 min |
| 6 | DEC-06 | `SS-V4` (labels) | V + BLE1–3; BLE4 removed and still wet; valve open | 15 min |
| 7 | DEC-07 | DEC-06 end | `SS-V4` (all dry, valve open) | 10 min |
| 8 | DEC-08 | `SS-V4` + `<LORA1>` HW | V + BLE1–4 | 10 min |
| 9 | DEC-09 | `SS-V4` | BLE1–4, no valve | 7 min |
| 10 | DEC-10 | DEC-09 end | BLE1, BLE4 | 6 min |
| 11 | DEC-11 | DEC-10 end | empty | 8 min |
| 12 | DEC-12 | DEC-11 end | empty | 8 min |
| 13 | DEC-13 | DEC-12 end | V + BLE1–4, pulse open | 17 min |
| 14 | DEC-14 | DEC-13 end, inside its pulse window | V + BLE1–4 | 6 min |
| 15 | DEC-15 | `SS-V4` | `SS-V4` (restored by its last step) | 8 min |
| 16 | DEC-16 | `SS-V4` | empty, rules restored | 12 min |
| 17 | DEC-17 | DEC-16 end | empty (`provisioned:true`) | 8 min |
| 18 | DEC-18 | `SS-V4` + an override | empty, after a reboot | 10 min |

### 1.1 Reference snapshots

`<...>` marks a value that changes from run to run. Everything else must match exactly: keys, key order, types and fixed values. `gateway.name` is present only while a hub name is set, and it sits between `short_id` and `fw`. `ts` is at least 1704067200.

**R-SYNC: the first snapshot after a `provision` on an empty hub** (`SNAP trigger=event:provision`), before the valve links and before any sensor is heard:

```json
{
  "schema": "eflostop.v2",
  "ts": <epoch>,
  "gateway": { "id": "<GW>", "short_id": "<last 4 of GW>", "fw": "2.1.4", "uptime_s": <n> },
  "type": "snapshot",
  "data": {
    "reason": "event",
    "system_health": { "rating": "excellent", "reason": "Syncing - waiting for 5 devices" },
    "valve": { "valve_id": "<VALVE_MAC>", "state": "disconnected", "connected": false, "rating": "critical", "last_seen_age_s": null },
    "lora_sensors": [],
    "ble_leak_sensors": [
      { "sensor_id": "<BLE1>", "connected": false, "rating": "critical", "last_seen_age_s": null, "battery": null, "rssi": null, "leak_state": false, "fw_version": null, "location": { "code": "kitchen", "label": "Sink" } },
      { "sensor_id": "<BLE2>", "connected": false, "rating": "critical", "last_seen_age_s": null, "battery": null, "rssi": null, "leak_state": false, "fw_version": null, "location": { "code": "laundry", "label": "Washer" } },
      { "sensor_id": "<BLE3>", "connected": false, "rating": "critical", "last_seen_age_s": null, "battery": null, "rssi": null, "leak_state": false, "fw_version": null, "location": { "code": "bathroom", "label": "Ensuite" } },
      { "sensor_id": "<BLE4>", "connected": false, "rating": "critical", "last_seen_age_s": null, "battery": null, "rssi": null, "leak_state": false, "fw_version": null, "location": { "code": "bathroom", "label": "Main" } }
    ],
    "rules": { "auto_close_enabled": true, "trigger_mask": 7 },
    "override_active": false
  }
}
```

If the valve has already linked when this snapshot is built, the count reads 4, and the valve reads `{"valve_id":"<VALVE_MAC>","state":"unknown","battery":null,"leak_state":false,"rmleak":false,"connected":true,"fw_version":null,"rating":"excellent","last_seen_age_s":0}` while its setup runs, or the R-FULL valve block after `SETUP COMPLETE`. Both are a Pass.

**R-FULL: valve + 4 BLE, everything heard, dry, healthy** (for example the `boot` snapshot at the end of the sync):

```json
{
  "schema": "eflostop.v2",
  "ts": <epoch>,
  "gateway": { "id": "<GW>", "short_id": "<last 4 of GW>", "fw": "2.1.4", "uptime_s": <n> },
  "type": "snapshot",
  "data": {
    "reason": "boot",
    "system_health": { "rating": "excellent", "reason": "All devices healthy" },
    "valve": { "valve_id": "<VALVE_MAC>", "state": "open", "battery": <21..100>, "leak_state": false, "rmleak": false, "connected": true, "fw_version": "2.2.0", "rating": "excellent", "last_seen_age_s": 0 },
    "lora_sensors": [],
    "ble_leak_sensors": [
      { "sensor_id": "<BLE1>", "connected": true, "rating": "excellent", "last_seen_age_s": <0..110>, "battery": <n>, "rssi": <n>, "leak_state": false, "fw_version": "<eleak fw>", "location": { "code": "kitchen", "label": "Sink" } },
      { "sensor_id": "<BLE2>", "connected": true, "rating": "excellent", "last_seen_age_s": <0..110>, "battery": <n>, "rssi": <n>, "leak_state": false, "fw_version": "<eleak fw>", "location": { "code": "laundry", "label": "Washer" } },
      { "sensor_id": "<BLE3>", "connected": true, "rating": "excellent", "last_seen_age_s": <0..110>, "battery": <n>, "rssi": <n>, "leak_state": false, "fw_version": "<eleak fw>", "location": { "code": "bathroom", "label": "Ensuite" } },
      { "sensor_id": "<BLE4>", "connected": true, "rating": "excellent", "last_seen_age_s": <0..110>, "battery": <n>, "rssi": <n>, "leak_state": false, "fw_version": "<eleak fw>", "location": { "code": "bathroom", "label": "Main" } }
    ],
    "rules": { "auto_close_enabled": true, "trigger_mask": 7 },
    "override_active": false
  }
}
```

A sensor with a battery of 21–35 % or an RSSI of −89…−80 dBm rates `good`, and the hub rating is then `good` with a reason such as "1 sensor battery low" or "1 sensor signal weak". That is correct behaviour, but for this section place the sensors so that all rate `excellent`. The sensors' `sensor_id` and the `valve_id` are always upper-case, whatever case the `provision` used.

**R-NOVALVE: BLE1–4 with no valve provisioned** (BUG-5). This is R-FULL with `"valve": {}` and the `data.reason` of the trigger. `system_health` is unchanged ("All devices healthy").

**R-EMPTY: a hub with no device** (BUG-3, BUG-6). `data.reason` is `"event"` for the transition (the removal or `provision` that emptied the hub), `"heartbeat"` at the interval, `"boot"` once per boot or MQTT (re)connect, and `"decommission"` for the final snapshot of `decommission` `all`:

```json
{
  "schema": "eflostop.v2",
  "ts": <epoch>,
  "gateway": { "id": "<GW>", "short_id": "<last 4 of GW>", "fw": "2.1.4", "uptime_s": <n> },
  "type": "snapshot",
  "data": {
    "reason": "event",
    "system_health": { "rating": "excellent", "reason": "No devices provisioned" },
    "valve": {},
    "lora_sensors": [],
    "ble_leak_sensors": [],
    "rules": { "auto_close_enabled": true, "trigger_mask": 7 },
    "override_active": false
  }
}
```

There is no `override_remaining_s` and no `expires_ts`. `rules` shows the stored rules config. It is `true`/`7` after a removal empties the hub. After a `rules_config` sent to an empty hub (DEC-16), or a `provision` whose own rules keys apply on top of the defaults (DEC-17), it shows that value instead.

**R-LIFE-EMPTY: lifecycle from a hub emptied by removals or by `decommission` `all`:**

```json
{"schema":"eflostop.v2","ts":<epoch>,"gateway":{"id":"<GW>","short_id":"<last 4>","fw":"2.1.4","uptime_s":<n>},"type":"lifecycle","data":{"event":"online","reset_reason":"<power_on|software>","provisioned":false,"lora_sensor_count":0,"ble_leak_sensor_count":0,"rules":{"auto_close_enabled":true,"trigger_mask":7}}}
```

There is no `valve_id` key at all when no valve is provisioned. A hub emptied by a `provision` (DEC-17) reads `"provisioned":true` here, with the counts at 0.

**R-TWIN-EMPTY: the twin reported patch on UART** (`IOTHUB: Twin reported (%d): %s`, format):

```json
{"fw_version":"2.1.4","gateway_id":"<GW>","short_id":"<last 4>","hub_name":"<name, or empty>","provisioned":false,"valve_mac":null,"valve_device_id":null,"valve_id":null,"lora_sensor_count":0,"ble_leak_sensor_count":0,"auto_close_enabled":true,"trigger_mask":7,"uptime_s":<n>,"snapshot_interval_s":60,"free_heap":<n>}
```

In the VS Code twin view a reported key set to `null` is deleted, so `valve_id` simply disappears from `properties.reported`. The counts, `provisioned`, `auto_close_enabled` and `trigger_mask` are visible.

---

### DEC-01 — Provision a valve and 4 BLE sensors on an empty hub: UI sync, "Syncing" countdown, pulse, boot snapshot (S1) — P0

**Purpose.** Verify that the 2.1.3 UI-sync behaviour survives the BUG-2 rework. After a `provision`, devices not yet heard show as "Syncing - waiting for N devices" instead of offline. N drops as each device is heard. Every sensor packet and every 30 s produce a snapshot for 5 min (the pulse). One complete `boot` snapshot is published once every device is heard (or at 150 s). This is the baseline the removal tests compare against.

**Preconditions / start state.**
- `SS-EMPTY`, reached by a power cycle so that BLE is not running yet (UART at boot: `IOTHUB: Boot: hub is empty - clearing any persisted rules-engine state`, and no `IOTHUB: Starting BLE`).
- Valve on the PSU at 6.0 V (Good), open, powered and in range. `<BLE1>`…`<BLE4>` dry, with batteries fitted, within 3 m.
- Interval 60 s. UART log and IoT Hub capture running. The app is open on the hub's device screen.

**Command (step 2), on one line:**

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-01-s2","cmd":"provision","payload":{"valve_id":"<VALVE_MAC>","ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE1>","location_code":"kitchen","label":"Sink"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE2>","location_code":"laundry","label":"Washer"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE3>","location_code":"bathroom","label":"Ensuite"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE4>","location_code":"bathroom","label":"Main"}],"auto_close_enabled":true}}
```

**Steps**

| # | Action | Expected (details below) |
|---|---|---|
| 1 | Record the `MONITOR` line. Check that the last snapshot is R-EMPTY and the LED is WHITE. | `FLEET_LED: rating=unprovisioned color=WHITE effect=SOLID` was the last LED line. |
| 2 | Send the command. Call the time of its `cmd_ack` T0. | `cmd_ack` `ok` within 5 s; block A on UART. |
| 3 | Watch the first snapshot. | `SNAP trigger=event:provision` and R-SYNC, at T0 + ≤ 7 s. LED stays WHITE (no new LED line, since WHITE → WHITE is not a change). |
| 4 | Watch the valve link. | Block B. A `SNAP trigger=event:valve_linked` snapshot with the count one lower. |
| 5 | Watch each sensor being heard, up to T0 + 150 s. | Block C. Every `prov_pkt` / `prov_pulse` snapshot shows the count falling (5 → 4 → 3 → 2 → 1). A sensor entry turns `connected:true` with its battery, RSSI and `fw_version` once heard. |
| 6 | When the last device is heard: | `HEALTH_ENGINE: Boot sync: all devices seen`, then `IOTHUB: SNAP trigger=boot` and R-FULL (`"reason":"boot"`). `FLEET_LED: rating=excellent color=GREEN effect=SOLID`. |
| 7 | Keep watching to T0 + 310 s. | Pulse snapshots continue: `SNAP trigger=event:prov_pulse` every 30 s, plus `SNAP trigger=event:prov_pkt` after sensor packets. At T0 + 300 s: `IOTHUB: PROV pulse window closed (%u snapshot(s) requested)` (format), with a count of at most 40. Heartbeats every 60 s after that. |
| 8 | Keep watching to T0 + 610 s. | No `Roll-up grace expired`, no `Boot sync: timeout`, no `SNAP trigger=commission` (everything was heard in the window), no `SNAP trigger=fast`. |
| 9 | Record the `MONITOR` line. Check twin reported. | `min_ever` ≥ 2,972 B; note it against 19,524 B. Twin: `"provisioned":true`, `"valve_id":"<VALVE_MAC>"`, `"lora_sensor_count":0`, `"ble_leak_sensor_count":4`, `"auto_close_enabled":true`, `"trigger_mask":7`. |
| 10 | UI: watch the app during steps 3–7. Open the installer placement screen for one sensor and move that sensor about 3 m. | The app shows the hub as syncing (not offline) at step 3, fills each sensor in as it is heard, and ends with every device healthy. On the placement screen, the RSSI and battery of the moved sensor refresh within about 30 s of its next packet (the pulse). |

**Block A: UART after the command** (esp-mqtt task, in this order):
- `C2D_CMD: Envelope cmd='provision' ver=1 id='dec-01-s2' payload=%s` (format)
- `IOTHUB: C2D cmd='provision' ver=1 id='dec-01-s2'`
- `IOTHUB: Provisioning JSON detected`
- `PROVISIONING: Handling provisioning JSON (%d bytes)` (format)
- `PROVISIONING: Auto-close opt-in: ENABLED (triggers=0x07)`
- `PROVISIONING: Config saved to NVS successfully`
- `PROVISIONING: Provisioning completed successfully!`
- `PROVISIONING: BLE leak sensors: 4`
- `PROVISIONING: Auto-close: enabled triggers=0x07`
- `IOTHUB: Applying provisioned valve MAC: <VALVE_MAC>`
- `BLE_VALVE: [API] Target MAC set to: <VALVE_MAC>`
- `BLE_VALVE: [CMD] No valve commands to flush (valve target set)`
- `IOTHUB: Starting BLE (valve=<VALVE_MAC>, BLE sensors=4)`
- `IOTHUB: Provision: applied 4 inline sensor_meta entry(ies)`
- `TELEMETRY_V2: Pub event: {...,"data":{"event":"cmd_ack","id":"dec-01-s2","cmd":"provision","status":"ok"}}` (format)

Then, on `iothub_task`, within about 1 s of the ack:
- `HEALTH_ENGINE: Device table loaded: 5 device(s) (+5 added, -0 removed)`
- `BLE_LEAK: Sensor tracking reset`
- `IOTHUB: Commission: fast snapshot armed (all-devices-seen, else <=150s; refreshes on late devices)`
- `IOTHUB: Valve detectors reset for <VALVE_MAC>`
- `IOTHUB: Twin reported (%d): %s` (format)
- `IOTHUB: PROV pulse armed: every 30 s for 300 s, plus on every sensor packet`
- `IOTHUB: SNAP trigger=event:provision`, then `TELEMETRY_V2: Pub snapshot: {...}` (R-SYNC), then `IOTHUB: SNAP heartbeat=reset interval_ms=60000`

Also, once per boot: `BLE_VALVE: [INIT] Signal received. Starting BLE stack...` and `BLE_VALVE: [HOST] NimBLE host task started`, then `BLE_LEAK: NimBLE ready, initializing scanner`. They are printed here only because BLE was not running. Within 10 s: `BLE_LEAK: Whitelist reloaded: 4 sensor(s)`.

**Block B: valve link.**
- `BLE_VALVE: [SCAN] Starting scan for provisioned valve <VALVE_MAC>...`
- `BLE_VALVE: [SCAN] Target MAC matched - connecting to provisioned valve: <VALVE_MAC>`
- the `GAP CONNECT EVENT` banner, then `SETUP COMPLETE - READY FOR GATT`
- `BLE_VALVE: [READY] Valve=%u, Flood=%u, RMLEAK=%u, Batt=%u, DIS=%u` (format)
- `IOTHUB: SNAP trigger=event:valve_linked`

Nominal is T0 + 5–30 s.

**Block C: each sensor heard** (a dry sensor bursts about every 100 s, so each is heard by T0 + 110 s):
- `BLE_LEAK: eleak <BLEn> — leak=0 batt=%d%% rssi=%d fw=%s` (format; printed once per sensor, as the tracking reset makes its first packet a change)
- `IOTHUB: Event: BLE Leak <BLEn> leak=0 batt=%d` (format)
- `IOTHUB: SNAP trigger=event:prov_pkt`

No `leak_detected` and no `leak_cleared` event for a dry sensor. A `SNAP clamped by min-interval: +%lld ms` (format) line is normal when two snapshots fall within 5 s.

**LED:** WHITE (`unprovisioned`) → stays WHITE while syncing → GREEN (`rating=excellent color=GREEN effect=SOLID`) at step 6.

**Timing.**
- The `cmd_ack` comes ≤ 5 s after the send.
- The first snapshot comes ≤ 7 s after the ack.
- Every device is heard by T0 + 110 s. If a sensor is not heard by T0 + 150 s, `HEALTH_ENGINE: Boot sync: timeout (150 s) — snapshot gate open; unheard devices still excused for a further %lld s` (format) is printed, and the `boot` snapshot goes out then, still "Syncing - waiting for 1 device". This is not a Fail of the firmware: record it and check that sensor's placement.
- `boot` comes ≤ 2 s after the last device is heard.
- `prov_pulse` comes every 30 s ± 2 s.
- The pulse window closes at T0 + 300 s (+0/+3 s).

**Note (doc vs code).** `C2D_COMMANDS.md` says the snapshot after an adding `provision` is a `commission` snapshot. In the code, that command's own snapshot is `event`, the complete one is `boot`, and `commission` is published only for a device first heard **after** the `boot` snapshot, within 6 min. This plan follows the code.

| Result | Notes |
|---|---|
| | |

---

### DEC-02 — A `provision` that adds no device does not restart the pulse or the commission snapshot (council residual) — P0

**Purpose.** The council flagged a UI-sync change from 2.1.3. A `provision` that adds nothing (an identical re-send, or one with rules only) no longer re-arms the commission snapshot or the 5 min pulse. It still gets its own `event` snapshot and a twin refresh. This test confirms that behaviour and that it does no harm: the valve link and the survivors are untouched.

**Preconditions.** DEC-01 end state (V + BLE1–4, all heard, pulse window closed), or `SS-V4` with the labels.

**Commands:** first the identical re-send (step 1), then a rules-only provision (step 3).

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-02-s1","cmd":"provision","payload":{"valve_id":"<VALVE_MAC>","ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"]}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-02-s3","cmd":"provision","payload":{"rules":{"auto_close_enabled":true,"trigger_mask":7}}}
```

**Steps**

| # | Action | Expected |
|---|---|---|
| 1 | Send `dec-02-s1`. | `cmd_ack` `ok`. `HEALTH_ENGINE: Device table loaded: 5 device(s) (+0 added, -0 removed)`, `IOTHUB: Twin reported (%d): %s` (format), `IOTHUB: SNAP trigger=event:provision`, and one snapshot equal to the previous one (R-FULL content, `"reason":"event"`). |
| 2 | Check what must **not** appear for 60 s after the ack. | No `IOTHUB: Commission: fast snapshot armed…`, no `IOTHUB: PROV pulse armed…`, no `BLE_LEAK: Sensor tracking reset`, no `SNAP trigger=event:prov_pulse` / `prov_pkt`, no `SNAP trigger=boot` / `commission`. The valve stays linked: no `GAP DISCONNECT`. |
| 3 | Send `dec-02-s3`. | Same as steps 1 and 2. `PROVISIONING: Rules: auto_close=enabled triggers=0x07`. |
| 4 | Check the valve lines from step 1. | `BLE_VALVE: [API] Target MAC set to: <VALVE_MAC>` with **no** flush line after it (same valve, so its queue is kept). `IOTHUB: Starting BLE (valve=<VALVE_MAC>, BLE sensors=4)` is printed, but there is **no** second `[INIT] Signal received`. `BLE_VALVE: [TASK] CMD: CONNECT` then `BLE_VALVE: [SCAN] Already connected` (warning) is expected (CHANGELOG, "same text, new triggers"). |

**LED:** GREEN throughout, with no LED line.

**Timing:** the snapshot comes ≤ 7 s after each ack.

**Extended watch (T5-02 is merged here).** In the full run, keep watching for **6 min** after step 1, without touching the sensors. None of these may appear in that time: `BLE_LEAK: Sensor tracking reset`, `IOTHUB: Commission: fast snapshot armed`, `IOTHUB: PROV pulse armed`, `SNAP trigger=event:prov_pkt`, `SNAP trigger=event:prov_pulse`, `HEALTH_ENGINE: Boot sync:`, `IOTHUB: Valve detectors reset`. After the step 1 `event` snapshot only `heartbeat` snapshots come, at the configured interval counted from it (±2 s), and their devices equal the previous heartbeat's (only the ages differ). The LED shows no WHITE. Optional UI check: with the app's installer placement screen open, an **adding** `provision` (one new sensor) still starts the pulse and shows "Syncing - waiting for 1 device", and the screen refreshes RSSI and battery live.

**Pass/Fail.** Fail if the pulse or the commission snapshot re-arms, if the survivors' `battery` / `rssi` / `last_seen_age_s` go `null`, or if the valve relinks. The adding case (a pulse after a device is added) is covered by DEC-04, DEC-07 and DEC-13.

| Result | Notes |
|---|---|
| | |

---

### DEC-03 — Remove one heard BLE sensor after the sync window: the survivors keep their data (S2, BUG-2) — P0

**Purpose.** This is the main BUG-2 check. On 2.1.3, removing one sensor reset every other device to `connected:false`, `rating:critical`, `battery/rssi/fw_version:null` and "Syncing - waiting for N devices". It also printed `Boot sync: timeout` 150 s later and sent extra `boot` / `commission` snapshots. On 2.1.4, the next snapshot must equal the previous one minus the removed sensor, and nothing re-syncs.

**Preconditions.**
- V + BLE1–4 all heard, and the pulse window closed. This is the DEC-01 end state, or `SS-V4` at least 6 min after its `provision`.
- The last snapshot is R-FULL content, with "All devices healthy". LED GREEN.
- Keep the IoT Hub monitor open so that the "before" snapshot is on screen.

**Command:**

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-03-s2","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE4>"}}
```

**Steps**

| # | Action | Expected |
|---|---|---|
| 1 | Copy the last snapshot (the "before" snapshot). Note each survivor's `battery`, `rssi`, `fw_version`, `rating`, `connected` and `last_seen_age_s`, and the snapshot `ts`. | — |
| 2 | Send the command. Call the time of the `cmd_ack` T0. | `cmd_ack` `ok`. UART block A. |
| 3 | Take the first snapshot after the ack (the "after" snapshot). | `SNAP trigger=event:decommission` ≤ 7 s after T0. The snapshot has the 3 survivors and no `<BLE4>`. See the check below. |
| 4 | Watch until T0 + 200 s. | Only heartbeat snapshots (every 60 s), unless a sensor sends a changed value. None of the forbidden lines (below). LED stays GREEN. |
| 5 | Check twin reported. | `"ble_leak_sensor_count":3`, `"provisioned":true`, `"valve_id":"<VALVE_MAC>"`. |
| 6 | UI: look at the app. | `<BLE4>` is gone. The other three still show their battery and signal, with no "syncing" or "offline" state and no flicker. |

**Block A: UART.** On the esp-mqtt task:
- `IOTHUB: C2D cmd='decommission' ver=1 id='dec-03-s2'`
- `IOTHUB: !!! DECOMMISSION_BLE: <BLE4> !!!`
- `PROVISIONING: === REMOVING BLE LEAK SENSOR <BLE4> ===`
- `PROVISIONING: Config saved to NVS successfully`
- `PROVISIONING: BLE leak sensor <BLE4> removed successfully`
- `PROVISIONING: Remaining BLE sensors: 3`
- `PROVISIONING: State: PROVISIONED`
- `SENSOR_META: Removed metadata for <BLE4> (type=0), %d entries remain` (format)
- the `cmd_ack` `Pub event:`

Then on `iothub_task`:
- `HEALTH_ENGINE: Device table loaded: 4 device(s) (+0 added, -1 removed)`
- `IOTHUB: Telemetry caches purged: 0 LoRa, 1 BLE (no longer provisioned)`
- `IOTHUB: Twin reported (%d): %s` (format)
- `IOTHUB: SNAP trigger=event:decommission`
- `TELEMETRY_V2: Pub snapshot: {...}`
- `IOTHUB: SNAP heartbeat=reset interval_ms=60000`

Within 10 s: `BLE_LEAK: Whitelist reloaded: 3 sensor(s)`.

**The "after" snapshot check.** It must equal the "before" snapshot with `<BLE4>`'s element removed, except for these fields:
- `ts` and `gateway.uptime_s` advance;
- `data.reason` is `"event"`;
- each survivor's `last_seen_age_s` is a number, not `null`, and at least its "before" value minus 5 s. It grows with the time elapsed, unless that sensor was heard again in between;
- a survivor's `rssi` or `battery` may differ **only** if that sensor sent a packet in between. Check the UART for its `eleak` line.

Everything else is identical:
- `connected:true` and the `rating` of every survivor;
- `fw_version`;
- `system_health`: `{"rating":"excellent","reason":"All devices healthy"}`, never "Syncing…";
- the valve block (`last_seen_age_s` stays 0);
- `rules`, `override_active`.

**Must NOT appear** (from T0 to T0 + 200 s):
- `IOTHUB: Commission: fast snapshot armed…`
- `IOTHUB: PROV pulse armed…`
- `BLE_LEAK: Sensor tracking reset`
- `HEALTH_ENGINE: Boot sync: timeout` (on 2.1.3 this came at about T0 + 150 s)
- `HEALTH_ENGINE: Roll-up grace expired`
- `IOTHUB: SNAP trigger=boot`, `IOTHUB: SNAP trigger=commission`, `IOTHUB: SNAP trigger=fast`
- a second `SNAP trigger=event:decommission`
- `IOTHUB: Valve detectors reset for` (the valve did not change)
- any snapshot containing `<BLE4>` after the ack

**LED:** GREEN throughout, with no LED line.

**Timing:**
- `cmd_ack` ≤ 5 s after the send;
- the snapshot ≤ 7 s after the ack;
- `Whitelist reloaded` ≤ 10 s after the ack;
- the watch lasts 200 s (the 150 s window, plus margin).

| Result | Notes |
|---|---|
| | |

---

### DEC-04 — Remove sensors during the sync window, heard and unheard: no reset, no new window, original deadlines (S3, BUG-2, E-18, N10) — P0

**Purpose.** Verify that a removal inside the post-provision window changes nothing for the others:
- the unheard devices stay "Syncing" with their **original** deadline (600 s from when they were added, not from the removal);
- heard devices keep their data;
- the pulse carries on and ends on its original schedule;
- the only snapshot the removal itself adds is its own `event:decommission`.

It also checks that the rating-change snapshot fires when an unheard device's grace ends (N10).

**Preconditions.** DEC-03 end state: V + BLE1–3 heard, `<BLE4>` removed but powered and dry. `<PH1>` and `<PH2>` are not near any real device.

**Commands**

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-04-s1","cmd":"provision","payload":{"ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>","00:80:E1:00:00:01","00:80:E1:00:00:02"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE4>","location_code":"bathroom","label":"Main"}]}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-04-s3","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE1>"}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-04-s4","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"00:80:E1:00:00:01"}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-04-s7","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"00:80:E1:00:00:02"}}
```

**Steps.** T0 is the `cmd_ack` of step 1. Write down the log time of every step.

| # | Action | Expected |
|---|---|---|
| 1 | Send `dec-04-s1`. It adds `<BLE4>`, `<PH1>` and `<PH2>` (the valve is not in the payload, so it is kept). | `cmd_ack` `ok`. `HEALTH_ENGINE: Device table loaded: 7 device(s) (+3 added, -0 removed)`, `IOTHUB: Commission: fast snapshot armed…`, `IOTHUB: PROV pulse armed: every 30 s for 300 s, plus on every sensor packet`, `BLE_LEAK: Sensor tracking reset`. Snapshot `SNAP trigger=event:provision`: `system_health` `{"rating":"excellent","reason":"Syncing - waiting for 3 devices"}`. BLE1–3 and the valve keep their values. LED: `FLEET_LED: rating=syncing color=WHITE effect=SOLID`. |
| 2 | Wait until `<BLE4>` is heard (≤ T0 + 110 s). | A `prov_pkt` snapshot with `<BLE4>` `connected:true` and the reason "Syncing - waiting for 2 devices". The LED stays WHITE (2 unheard). |
| 3 | Between T0 + 30 s and T0 + 120 s, remove a **heard** survivor: send `dec-04-s3` (`<BLE1>`). | `HEALTH_ENGINE: Device table loaded: 6 device(s) (+0 added, -1 removed)`. One `SNAP trigger=event:decommission`, with `<BLE1>` gone and the reason unchanged ("Syncing - waiting for 2 devices", or 3 if `<BLE4>` is not heard yet). BLE2, BLE3, BLE4 and the valve keep their values. No `Commission: fast snapshot armed`, no `PROV pulse armed`. |
| 4 | Within 20 s, remove an **unheard** device: send `dec-04-s4` (`<PH1>`). | `HEALTH_ENGINE: Device table loaded: 5 device(s) (+0 added, -1 removed)`. One `SNAP trigger=event:decommission`, with the reason "Syncing - waiting for 1 device". LED still WHITE. |
| 5 | Watch the pulse and the gate to T0 + 310 s. | `prov_pulse` snapshots continue every 30 s. At T0 + 150 s (+0/+5 s): `HEALTH_ENGINE: Boot sync: timeout (150 s) — snapshot gate open; unheard devices still excused for a further %lld s` (format), with the value about 450, then **one** `SNAP trigger=boot` (`"reason":"boot"`, still "Syncing - waiting for 1 device"). At T0 + 300 s (+0/+3 s): `IOTHUB: PROV pulse window closed (%u snapshot(s) requested)` (format). These times are measured from **T0**, not from the removals. |
| 6 | Watch to T0 + 610 s. | At T0 + 600 s (+0/+5 s): `HEALTH_ENGINE: Roll-up grace expired (600 s) — 1 unheard device(s) now count`. Within 7 s: `SNAP trigger=event:health`, with `system_health` `{"rating":"critical","reason":"1 sensor offline"}`, and `FLEET_LED: rating=critical color=RED effect=SOLID`. **No** `device_offline` health event for `<PH2>` (a device never heard raises none). |
| 7 | Send `dec-04-s7` (`<PH2>`). | `HEALTH_ENGINE: Device table loaded: 4 device(s) (+0 added, -1 removed)`. Snapshot `event:decommission`, with `{"rating":"excellent","reason":"All devices healthy"}`. `FLEET_LED: rating=excellent color=GREEN effect=SOLID`. |
| 8 | E-18, observe: from step 3 to step 3 + 15 s, count the `SNAP trigger=event:prov_pkt` lines. | Each one must follow a surviving sensor's burst. A removed sensor's packets, still heard for up to 10 s until `BLE_LEAK: Whitelist reloaded: 5 sensor(s)`, must not drive a `prov_pkt`. For BLE this is hard to attribute, because a dry sensor's unchanged packets print no UART line. DEC-08 checks E-18 exactly with a LoRa sensor. Record the count here. |

**Must NOT appear** (T0 → step 7):
- a second `Boot sync: timeout`;
- a `Roll-up grace expired` at any time other than T0 + 600 s;
- `Commission: fast snapshot armed`, `PROV pulse armed` or `Sensor tracking reset` after step 1;
- any survivor going back to `battery:null`, `rssi:null` or `last_seen_age_s:null` after it was heard.

**LED:** WHITE (syncing) from step 1 → RED at step 6 → GREEN at step 7.

**Timing:**
- snapshots ≤ 7 s after each ack;
- `Boot sync: timeout` at T0 + 150–155 s;
- the pulse closes at T0 + 300–303 s;
- `Roll-up grace expired` at T0 + 600–605 s;
- the `event:health` snapshot ≤ 7 s after that.

CP1 reference: the grace expired at 824.6 s for a provision at 224.9 s, and a removal at 815 s did not move it.

| Result | Notes |
|---|---|
| | |

---

### DEC-05 — Remove the last unheard device during the window: one `boot` snapshot, no second (S4, BUG-2) — P0

**Purpose.** When the only device still unheard is removed, the sync completes at once. There is one `boot` snapshot, and no `Boot sync: timeout` later.

**Preconditions.**
- DEC-04 end state: V + BLE2–4, all heard, LED GREEN.
- `<BLE1>` is powered, dry and in range, but not provisioned.
- No pulse window open: at least 5 min since the last adding `provision`.

**Commands**

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-05-s1","cmd":"provision","payload":{"ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>","00:80:E1:00:00:03"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE1>","location_code":"kitchen","label":"Sink"}]}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-05-s3","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"00:80:E1:00:00:03"}}
```

**Steps.** T0 is the `cmd_ack` of step 1.

| # | Action | Expected |
|---|---|---|
| 1 | Send `dec-05-s1` (adds `<BLE1>` and `<PH3>`). | `Device table loaded: 6 device(s) (+2 added, -0 removed)`, `Commission: fast snapshot armed…`, `PROV pulse armed…`. Snapshot "Syncing - waiting for 2 devices". LED `rating=syncing color=WHITE`. |
| 2 | Wait until `<BLE1>` is heard: a `prov_pkt` snapshot shows the reason "Syncing - waiting for 1 device". This must happen **before T0 + 140 s**. If it has not, wait for `Boot sync: timeout`, remove `<PH3>`, and repeat the test from step 1 (record the repeat). | — |
| 3 | Before T0 + 145 s, send `dec-05-s3` (the last unheard device). | `cmd_ack` `ok`. `HEALTH_ENGINE: Device table loaded: 5 device(s) (+0 added, -1 removed)`, `HEALTH_ENGINE: Boot sync: all devices seen`, `IOTHUB: SNAP trigger=boot`. One snapshot with `"reason":"boot"`, `{"rating":"excellent","reason":"All devices healthy"}`, all 4 sensors and the valve, and no `<PH3>`. `FLEET_LED: rating=excellent color=GREEN effect=SOLID`. |
| 4 | Watch until T0 + 310 s. | No `Boot sync: timeout`. No second `SNAP trigger=boot`. No `SNAP trigger=commission`. `prov_pulse` / `prov_pkt` event snapshots carry on until `PROV pulse window closed` at T0 + 300 s. They are expected, and they all show "All devices healthy". |

**Accepted variant (a narrow race).** The removal is published as a separate `SNAP trigger=event:decommission` 5 s after the `boot` snapshot. This can happen if `iothub_task` applied the change a few milliseconds before the command's snapshot request was raised. Both snapshots must be without `<PH3>`. Record it, but it is still a Pass. Two `boot` snapshots, or a `Boot sync: timeout`, is a Fail.

**LED:** WHITE → GREEN at step 3.

**Timing:** the `boot` snapshot comes ≤ 3 s after the removal ack.

| Result | Notes |
|---|---|
| | |

---
### DEC-06 — Remove a sensor while it is wet: the interlock releases, `leak_reset` is accepted, an override cancel does not re-close (S5, BUG-2 rules purge) — P0

**Purpose.** On 2.1.3, a sensor removed while wet stayed in the rules engine's active-leak list forever. Three things followed:
- the interlock never auto-cleared;
- `leak_reset` was refused ("A leak is still active…");
- an `override_cancel` or an override expiry re-closed the valve for a leak that no longer existed.

On 2.1.4 the removal forgets that sensor's leak source (`Rules: forgot removed leak source`). This test covers the three outcomes in three sub-cases, A, B and C.

**Preconditions.**
- `SS-V4` with the labels (DEC-05 end state): valve open, all dry, no incident, no override.
- No pulse window open.
- A wet paper towel ready for `<BLE4>` (0.11).

**Setup command used before sub-cases B and C** (re-adds `<BLE4>`; DEC-07 verifies the re-add itself in detail):

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-06-sN","cmd":"provision","payload":{"ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE4>","location_code":"bathroom","label":"Main"}]}}
```

Replace `N` with the step number.

**The removal command** (use the step number in the id each time):

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-06-sN","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE4>"}}
```

**Sub-case A: removal, then the 10 s auto-clear**

| # | Action | Expected |
|---|---|---|
| 1 | Wet `<BLE4>`. | Within 20 s: `BLE_LEAK: eleak <BLE4> — leak=1 batt=%d%% rssi=%d fw=%s` (format), `IOTHUB: Event: BLE Leak <BLE4> leak=1 batt=%d` (format), `RULES_ENGINE: LEAK INCIDENT latched by ble_leak_sensor sensor <BLE4>`, `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <BLE4>`. Events in this order: `leak_detected` (`"source_type":"ble_leak_sensor","sensor_id":"<BLE4>","leak_state":true`), `auto_close` (`"sensor_id":"<BLE4>","rmleak_asserted":true`), `valve_state_changed` (`"valve_state":"closed"`). Snapshot: `{"rating":"critical","reason":"Leak detected: Main, Leak interlock latched"}`, valve `"state":"closed","rmleak":true`. LED `rating=critical color=RED`. |
| 2 | Keep `<BLE4>` wet. Send the removal (`dec-06-s2`). Call its `cmd_ack` T0. | `cmd_ack` `ok`. esp-mqtt lines as in DEC-03 block A. On `iothub_task`: `HEALTH_ENGINE: Device table loaded: 4 device(s) (+0 added, -1 removed)`, `IOTHUB: Telemetry caches purged: 0 LoRa, 1 BLE (no longer provisioned)`, `RULES_ENGINE: Rules: forgot removed leak source <BLE4>`, `RULES_ENGINE: All sensors clear — auto-clear timer started (10s)`. `RULES_ENGINE: All leaks resolved — pending auto-close cancelled` may also print; it is allowed. Snapshot `SNAP trigger=event:decommission`: `<BLE4>` gone, `{"rating":"warning","reason":"Leak interlock latched"}`, valve `closed`, `rmleak:true`. LED `rating=warning color=YELLOW`. |
| 3 | Wait. | 10–12 s after `All sensors clear`: `RULES_ENGINE: AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`, `HEALTH_ENGINE: Interlock released — system rating floor removed`, then the event `{"event":"rmleak_auto_cleared","valve_id":"<VALVE_MAC>","clear_after_seconds":10}`. Snapshot `SNAP trigger=event:rules`: `{"rating":"excellent","reason":"All devices healthy"}`, valve `"state":"closed","rmleak":false`. LED `rating=excellent color=GREEN`. The valve stays **closed** (check it physically). |
| 4 | Keep `<BLE4>` wet, and watch for 60 s. | `BLE_LEAK: Whitelist reloaded: 3 sensor(s)` ≤ 10 s after T0. From T0 on: no `leak_detected` / `leak_cleared` for `<BLE4>`, no `LEAK INCIDENT latched`, no `auto_close`, LED stays GREEN. A single `IOTHUB: BLE leak event from unprovisioned <BLE4> dropped` in the first 10 s is allowed. |
| 5 | Send `valve_open` (`{"schema":"eflostop.cmd","ver":1,"id":"dec-06-s5","cmd":"valve_open"}`). | `cmd_ack` `ok`, and the valve opens (`valve_state_changed` `"open"`). Nothing is latched any more. |

**Sub-case B: `leak_reset` right after the removal** (2.1.3 refused it)

| # | Action | Expected |
|---|---|---|
| 6 | Keep `<BLE4>` wet. Send the re-add (`dec-06-s6`). | Within about 25 s: `leak_detected` for `<BLE4>`, `LEAK INCIDENT latched…`, `AUTO-CLOSE + RMLEAK triggered…`, `auto_close`, and the valve closes (DEC-07 A lists the lines). LED RED. |
| 7 | Prepare the `leak_reset` command in VS Code: `{"schema":"eflostop.cmd","ver":1,"id":"dec-06-s8","cmd":"leak_reset"}`. Send the removal (`dec-06-s7`). | `cmd_ack` `ok`, then `Rules: forgot removed leak source <BLE4>` and `All sensors clear — auto-clear timer started (10s)`. LED YELLOW. |
| 8 | **2–8 s after** the removal's `cmd_ack`, send `leak_reset`. | `cmd_ack` `ok` (**not** `error` "A leak is still active. Fix the leak first, or use override to open the valve during a leak."). `IOTHUB: Command: LEAK_RESET`, `RULES_ENGINE: LEAK_RESET: clearing incident (hub_latch=1, valve_rmleak=%d, override=0)` (format; `valve_rmleak` is normally 1), `IOTHUB: Leak incident cleared, RMLEAK reset`. Event `{"event":"rmleak_cleared","valve_id":"<VALVE_MAC>"}`. Snapshot excellent, "All devices healthy", `rmleak:false`, valve closed. LED GREEN. |
| 9 | Watch for 30 s. | **No** `AUTO-CLEAR` line and **no** `rmleak_auto_cleared` (the reset stopped the timer). No `LEAK_RESET refused — %u leak source(s) still active (use override to open during a leak)`. |

If the reset landed after the auto-clear (you see `AUTO-CLEAR…` before `Command: LEAK_RESET`, and then `RULES_ENGINE: LEAK_RESET: no active incident` or a `clearing incident` with `hub_latch=0`), the timing was missed. Repeat steps 6–9. This is not a Fail.

**Sub-case C: an override in force, the wet sensor removed, then `override_cancel`**

| # | Action | Expected |
|---|---|---|
| 10 | Keep `<BLE4>` wet. Send the re-add (`dec-06-s10`), and wait for `auto_close`. | As step 6, except that the valve is still closed from sub-case B, so there is no `valve_state_changed`. `auto_close` (`"rmleak_asserted":true`) re-asserts RMLEAK. Snapshot valve `closed` with `rmleak:true`. LED RED. |
| 11 | Send `override_enable` (`{"schema":"eflostop.cmd","ver":1,"id":"dec-06-s11","cmd":"override_enable"}`). | `cmd_ack` `ok` (≤ 12 s). `IOTHUB: Command: OVERRIDE_ENABLE`, `RULES_ENGINE: OVERRIDE WINDOW STARTED: auto-close blocked for 24h (expiry=%ld)` (format), `RULES_ENGINE: override_enable: 24h override started remotely — RMLEAK cleared, valve opening`. Event `water_access_override_enabled` with `"trigger":"c2d_command"` and `"remaining_s":86400`. The valve opens. `override_enable` also releases the incident latch (`HEALTH_ENGINE: Interlock released — system rating floor removed`). Snapshot: `"override_active":true`, `"override_remaining_s":<≈86400>`, valve `open` with `rmleak:false`, `{"rating":"critical","reason":"Leak detected: Main"}` (`<BLE4>` is still wet; there is no interlock part). LED RED. |
| 12 | Keep `<BLE4>` wet. **Within 4 min of step 10**, send the removal (`dec-06-s12`). | `Rules: forgot removed leak source <BLE4>`, and **no** other rules line: no `All sensors clear…`, no `AUTO-CLEAR…`, no `rmleak_auto_cleared`, because the latch was already released at step 11. Snapshot `event:decommission`: `<BLE4>` gone, `{"rating":"excellent","reason":"All devices healthy"}`, `"override_active":true`, and the valve **open**. LED GREEN. |
| 13 | Send `override_cancel` (`{"schema":"eflostop.cmd","ver":1,"id":"dec-06-s13","cmd":"override_cancel"}`). | `cmd_ack` `ok`. `IOTHUB: Command: OVERRIDE_CANCEL`, `RULES_ENGINE: OVERRIDE WINDOW CANCELLED (remaining_s=%ld)` (format). Event `{"event":"auto_close_reenabled","previous_remaining_s":<n>,"reason":"c2d_command"}`. **Not**: `Override cancelled with %d active leak(s) — executing auto-close`, `auto_close`, or a valve close. On 2.1.3 the removed wet sensor still counted as an active leak here and the valve re-closed. The valve stays **open**. Snapshot `"override_active":false`, with no `override_remaining_s`. LED GREEN. |

Step 12 is timed because the BLE scanner forwards an unchanged wet sensor again every 5 min (its telemetry heartbeat). If `RULES_ENGINE: Override active — auto-close BLOCKED for ble_leak_sensor sensor <BLE4> (remaining=%lds)` (format) appears before step 12, the latch is back. The removal then also prints `All sensors clear — auto-clear timer started (10s)`, followed 10–12 s later by `AUTO-CLEAR…` and `rmleak_auto_cleared`. That is correct: record it and carry on. Step 13's expectations are the same either way.

**End state for DEC-07:** `<BLE4>` removed and still wet; the valve open; no incident, no override.

**LED summary:**
- A and B: RED (wet) → YELLOW (removed, interlock latched; 10–12 s in A, until the `leak_reset` in B) → GREEN.
- C: RED (wet, override on) → GREEN at the removal.

**Timing:**
- the removal snapshot ≤ 7 s after the ack;
- `rmleak_auto_cleared` 10–12 s after `All sensors clear` (≤ 14 s end to end in the IoT Hub capture);
- `leak_reset` must be sent within the 10 s auto-clear window.

| Result | Notes |
|---|---|
| | |

---

### DEC-07 — Re-add a sensor that is still wet: a fresh `leak_detected`, and a re-close (S6, E-02, E-17) — P0

**Purpose.** Verify two fixes.
- **L9 / E-17.** The telemetry-cache purge on removal means a sensor re-added while still wet produces a fresh `leak_detected`. A skipped purge is retried.
- **E-02.** The scanner forgets its per-sensor state when devices are added. On 2.1.3, a sensor removed and re-added within the scanner's 10 s whitelist reload kept its old "wet" state. Its unchanged packets then produced no event at all: no `leak_detected`, no rules evaluation, and the interlock auto-cleared while it was wet, for up to 5 min.

The test is run slow (A) and quick (B).

**Preconditions.**
- DEC-06 end state: `<BLE4>` **not provisioned and wet**. The valve open. No incident. At least 20 s since `<BLE4>` was removed, so `BLE_LEAK: Whitelist reloaded: 3 sensor(s)` has been printed.
- Interval 60 s.

**Commands:** the re-add and the removal from DEC-06, with ids `dec-07-sN`.

**A. Slow re-add** (more than 10 s after the removal, the plan's S6)

| # | Action | Expected |
|---|---|---|
| 1 | Send the re-add (`dec-07-s1`). Call its `cmd_ack` T0. | `HEALTH_ENGINE: Device table loaded: 5 device(s) (+1 added, -0 removed)`, `BLE_LEAK: Sensor tracking reset`, `IOTHUB: Commission: fast snapshot armed…`, `IOTHUB: PROV pulse armed…`. Snapshot `event:provision` with "Syncing - waiting for 1 device" and `<BLE4>` unheard. ≤ 10 s: `BLE_LEAK: Whitelist reloaded: 4 sensor(s)`. |
| 2 | Wait for `<BLE4>`'s next wet burst (≤ T0 + 25 s). | `BLE_LEAK: eleak <BLE4> — leak=1 …` (format), `IOTHUB: Event: BLE Leak <BLE4> leak=1 batt=%d` (format). Event `leak_detected` for `<BLE4>` (`"leak_state":true`), then `RULES_ENGINE: LEAK INCIDENT latched by ble_leak_sensor sensor <BLE4>`, `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <BLE4>`, event `auto_close` (`"rmleak_asserted":true`), `valve_state_changed` `"closed"`. Snapshot critical, "Leak detected: Main, Leak interlock latched". LED RED. |
| 3 | Check the survivors. | BLE1–3 each print one `eleak … leak=0` line after the tracking reset. There is **no** `leak_cleared` or `leak_detected` for them. |

**B. Quick re-add** (within the scanner's 10 s reload: E-02)

| # | Action | Expected |
|---|---|---|
| 4 | Keep `<BLE4>` wet. The incident is latched, and the valve is closed with RMLEAK set (end of A). Prepare the re-add (`dec-07-s5`) in VS Code. Send the removal (`dec-07-s4`). | `cmd_ack` `ok`. `Rules: forgot removed leak source <BLE4>`, `All sensors clear — auto-clear timer started (10s)`. LED YELLOW. |
| 5 | **As soon as** the removal's `cmd_ack` arrives (aim for ≤ 3 s), send the re-add. Call its `cmd_ack` T1. | `Device table loaded: 5 device(s) (+1 added, -0 removed)`, `BLE_LEAK: Sensor tracking reset`. Check the UART: there must be **no** `BLE_LEAK: Whitelist reloaded: 3 sensor(s)` between the removal's `Device table loaded` and the re-add's. If there is one, this run was a slow re-add: repeat steps 4–5. |
| 6 | Wait for `<BLE4>`'s next wet burst (≤ T1 + 20 s). | A fresh `leak_detected` for `<BLE4>`, with the UART `Event: BLE Leak <BLE4> leak=1`. Then **one** of two outcomes, depending on whether that burst landed before or after the 10 s auto-clear: **(i)** before the auto-clear: no `rmleak_auto_cleared`, no new `LEAK INCIDENT latched` (the incident is still latched), and **no** `auto_close` event (the valve is already closed with RMLEAK set, so the rules engine has nothing to do); **(ii)** after the auto-clear: `rmleak_auto_cleared`, then `leak_detected`, `LEAK INCIDENT latched…`, `AUTO-CLOSE + RMLEAK triggered…` and `auto_close`. Either way, the next snapshot is critical with "Leak detected: Main", the valve is `closed` with `rmleak:true`, and the LED is RED. Record which outcome you got. |
| 7 | Send `valve_open` (`dec-07-s7`). | `cmd_ack` `error`, with `"detail":"Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."` UART: `IOTHUB: VALVE_OPEN refused — Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.` |
| 8 | Restore: dry `<BLE4>`. Wait for `rmleak_auto_cleared` (10–12 s after its `leak_cleared`). Then send `valve_open` (`dec-07-s8`). | `leak_cleared`, `All sensors clear — auto-clear timer started (10s)`, `AUTO-CLEAR…`, `rmleak_auto_cleared`. `valve_open` `ok`; the valve opens. LED GREEN. The state is `SS-V4`. |

**Fail conditions:**
- no `leak_detected` for `<BLE4>` within 25 s of T0 (A) or of T1 (B);
- `rmleak_auto_cleared` while `<BLE4>` is wet **with no** `auto_close` within 25 s after it;
- `valve_open` accepted at step 7;
- the valve not closed at the end of step 2 or step 6.

These are exactly the E-02 symptoms (no event for up to 5 min, an auto-clear while wet).

**Note (HANDOFF §7).** §7 says "leak_detected and auto_close arrive at once" for the quick re-add. Per the code at `d9fa9c8`, `auto_close` is published only in outcome (ii). In outcome (i) the rules engine returns early because the valve is closed with RMLEAK set (`rules_engine_evaluate_leak`, the "already closed" check). Outcome (i) is correct and safe.

**E-17 (observe).** If `IOTHUB: Telemetry-cache purge: provisioning busy, retrying` appears after a removal, check two things. Within about 2 s a new `Device table loaded` must follow, with `Telemetry caches purged: 0 LoRa, 1 BLE (no longer provisioned)`. The re-add must still give a fresh `leak_detected`. Record it.

**Timing:**
- `leak_detected` ≤ 25 s after the re-add ack (the ≤ 10 s whitelist reload, plus one 15 s wet burst);
- `auto_close` in the same pass as `leak_detected`;
- the snapshot ≤ 7 s after.

| Result | Notes |
|---|---|
| | |

---

### DEC-08 — Remove a LoRa sensor: survivors unchanged, its later packets ignored, no pulse from them (LoRa decommission, N4, E-18)

**Purpose.**
- LoRa removal works like a BLE removal.
- A removed LoRa sensor's later packets are not evaluated by the rules engine: no leak event, no auto-close (N4).
- Those packets do not drive post-provision `prov_pkt` snapshots (E-18). E-18 is exactly checkable here because the hub logs every LoRa packet it receives.

**Preconditions.**
- `SS-V4` (labels), and a LoRa sensor `<LORA1>` powered, dry and in range.
- Record its report cadence (0.11). If no LoRa hardware is available (the current production PCBA has no SX1262, T5-13), run **variant B** below instead of steps 1-6 and record variant A as `N/A (no LoRa HW)`. Variant B needs no radio.

**Commands**

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-08-s1","cmd":"provision","payload":{"lora_sensors":["<LORA1>"],"sensor_meta":[{"sensor_type":"lora","sensor_id":"<LORA1>","location_code":"utility","label":"Heater"}]}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-08-s3","cmd":"decommission","payload":{"target":"lora","sensor_id":"<LORA1>"}}
```

**Steps.** T0 is the `cmd_ack` of step 1.

| # | Action | Expected |
|---|---|---|
| 1 | Send `dec-08-s1`. | `PROVISIONING: LoRa Sensor[0]: <LORA1>`, `HEALTH_ENGINE: Device table loaded: 6 device(s) (+1 added, -0 removed)`, `PROV pulse armed…`. Snapshot "Syncing - waiting for 1 device", with `"lora_sensors":[{"sensor_id":"<LORA1>","connected":false,"rating":"critical","last_seen_age_s":null,"battery":null,"rssi":null,"leak_state":false,"snr":null,"location":{"code":"utility","label":"Heater"}}]`. |
| 2 | Wait for `<LORA1>`'s first packet. | `IOTHUB: Event: LoRa Packet from <LORA1>`, then a `SNAP trigger=event:prov_pkt` snapshot with `<LORA1>` `"connected":true`, a numeric `battery` and `rssi`, `"snr":<number>` and "All devices healthy". LED GREEN. |
| 3 | If still inside T0 + 300 s (the pulse window), send `dec-08-s3` now. Otherwise send it anyway, and mark step 5 "not checkable". | esp-mqtt: `IOTHUB: !!! DECOMMISSION_LORA: <LORA1> !!!`, `PROVISIONING: === REMOVING LORA SENSOR <LORA1> ===`, `PROVISIONING: Config saved to NVS successfully`, `PROVISIONING: LoRa sensor <LORA1> removed successfully`, `PROVISIONING: Remaining LoRa sensors: 0`, `PROVISIONING: State: PROVISIONED`, `SENSOR_META: Removed metadata for <LORA1> (type=1), %d entries remain` (format), `cmd_ack` `ok`. `iothub_task`: `HEALTH_ENGINE: Device table loaded: 5 device(s) (+0 added, -1 removed)`, `IOTHUB: Telemetry caches purged: 1 LoRa, 0 BLE (no longer provisioned)`, `Twin reported` (`"lora_sensor_count":0`), `SNAP trigger=event:decommission`. The snapshot has `"lora_sensors":[]`; the valve and BLE1–4 are identical to the previous snapshot, as in the DEC-03 check. |
| 4 | Wet `<LORA1>`. Wait for 2 of its packets, then dry it and wait for 1 more. | For every packet: `IOTHUB: Event: LoRa Packet from <LORA1>`, then `IOTHUB: Sensor <LORA1> not provisioned, skipping`. **No** `leak_detected` / `leak_cleared` for `<LORA1>`, **no** `RULES_ENGINE: LEAK INCIDENT latched`, **no** `AUTO-CLOSE + RMLEAK triggered`, **no** `auto_close`. The valve stays open, and the LED stays GREEN. |
| 5 | E-18: for each `Sensor <LORA1> not provisioned, skipping` line inside the pulse window: | no `IOTHUB: SNAP trigger=event:prov_pkt` within 5 s after it. `SNAP trigger=event:prov_pulse` at its own 30 s cadence is fine; so is `prov_pkt` right after a surviving BLE sensor's `eleak` line. |
| 6 | Watch until T0 + 610 s. | No `Roll-up grace expired` and no `device_offline` for `<LORA1>`: it is gone from the table. |

**LED:** GREEN throughout, except WHITE (syncing) between steps 1 and 2.

**Timing:**
- the removal snapshot ≤ 7 s after the ack;
- the LoRa packet lines follow the sensor's own cadence (one period + 10 s).

| Result | Notes |
|---|---|
| | |

**Variant B — LoRa provision and decommission with no radio (a fake LoRa id).** This checks the LoRa provisioning and removal path, the survivors and the twin, with no SX1262 and no LoRa sensor. It does not check packets (N4 step 4, E-18 step 5); those stay with variant A, T3-08 and T4-13, or are waived under EC-2 (9.4). Use the fake id `0x1A2B3C4D` (no real sensor may have it).

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-08-b1","cmd":"provision","payload":{"lora_sensors":["0x1A2B3C4D"],"sensor_meta":[{"sensor_type":"lora","sensor_id":"0x1A2B3C4D","location_code":"utility","label":"Heater"}]}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-08-b3","cmd":"decommission","payload":{"target":"lora","sensor_id":"0x1A2B3C4D"}}
```

| # | Action | Expected | Result | Notes |
|---|---|---|---|---|
| B1 | From `SS-V4` (labels), send `dec-08-b1`. | `cmd_ack` `ok`. `PROVISIONING: LoRa Sensor[0]: 0x1A2B3C4D` (format `LoRa Sensor[%d]: 0x%08lX`), `HEALTH_ENGINE: Device table loaded: 6 device(s) (+1 added, -0 removed)`, `IOTHUB: PROV pulse armed…`, `IOTHUB: Twin reported (%d): %s` (format) with `"lora_sensor_count":1`. Snapshot: `system_health.reason` "Syncing - waiting for 1 device", and `"lora_sensors":[{"sensor_id":"0x1A2B3C4D","connected":false,"rating":"critical","last_seen_age_s":null,"battery":null,"rssi":null,"leak_state":false,"snr":null,"location":{"code":"utility","label":"Heater"}}]`. The valve and `<BLE1>`…`<BLE4>` entries equal the previous snapshot (only the ages differ). LED `rating=syncing color=WHITE effect=SOLID`. | | |
| B2 | Within 60 s, check nothing else changed. | No `GAP DISCONNECT` for the valve, no `BLE_LEAK: Sensor tracking reset` beyond the one an adding provision is allowed, no `null` in any survivor's `battery` / `rssi`. | | |
| B3 | Send `dec-08-b3` (before T0 + 600 s, so no roll-up of the fake id). | `IOTHUB: !!! DECOMMISSION_LORA: 0x1A2B3C4D !!!` (format `!!! DECOMMISSION_LORA: 0x%08lX !!!`), `PROVISIONING: === REMOVING LORA SENSOR 0x1A2B3C4D ===`, `PROVISIONING: LoRa sensor 0x1A2B3C4D removed successfully`, `PROVISIONING: Remaining LoRa sensors: 0`, `SENSOR_META: Removed metadata for 0x1A2B3C4D (type=1), %d entries remain` (format), `cmd_ack` `ok`. Then `HEALTH_ENGINE: Device table loaded: 5 device(s) (+0 added, -1 removed)`, `Twin reported` with `"lora_sensor_count":0`, `SNAP trigger=event:decommission`. **No** `IOTHUB: Telemetry caches purged` line: it prints only when a cache entry existed, and a sensor that was never heard has none. | | |
| B4 | Check the removal snapshot and the twin. | `"lora_sensors":[]`; the valve and `<BLE1>`…`<BLE4>` identical to the snapshot before B1 (only the ages differ); `system_health` back to "All devices healthy"; LED `rating=excellent color=GREEN effect=SOLID` (or `good`). Twin reported: `lora_sensor_count` 0, `ble_leak_sensor_count` 4, `valve_id` `<VALVE_MAC>` (the twin has counts only; the id list is in the snapshot's `lora_sensors`, which is `[]`). | | |
| B5 | Watch until the B1 ack + 610 s. | No `Roll-up grace expired` and no `device_offline` for `0x1A2B3C4D`. | | |

---

### DEC-09 — Remove only the valve while its link is up: `"valve": {}`, no `valve_unlinked`, no rescan (S7, BUG-5, P0-c) — P0

**Purpose.** BUG-5: on 2.1.3 a hub whose valve was removed reported `{"state":"disconnected","connected":false}`, which looks like a real valve that dropped off. It also published a second snapshot on the link teardown (`valve_unlinked`). On 2.1.4 the valve block is `{}` from the removal snapshot on. There is no second snapshot, the twin's `valve_id` is null, and the hub never scans for a valve again. The removed valve is never reported offline.

**Preconditions.**
- `SS-V4` (labels), with the valve linked and ready (`SETUP COMPLETE` seen), open and Good.
- No pulse window. Interval 60 s.
- If available, valve B (neighbour) powered within 2 m.

**Command:**

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-09-s2","cmd":"decommission","payload":{"target":"valve"}}
```

**Steps.** T0 is the `cmd_ack` of step 2.

| # | Action | Expected |
|---|---|---|
| 1 | Record the "before" snapshot (R-FULL content). | — |
| 2 | Send `dec-09-s2`. | See block A. `cmd_ack` `ok`. |
| 3 | First snapshot after the ack. | `SNAP trigger=event:decommission` ≤ 7 s after T0: R-NOVALVE, with `"valve": {}`, BLE1–4 identical to the "before" snapshot as in the DEC-03 check, `{"rating":"excellent","reason":"All devices healthy"}`. |
| 4 | Watch until T0 + 240 s (past the 180 s valve grace). | Only heartbeat snapshots, all with `"valve": {}`. See "must not appear". LED GREEN throughout, never YELLOW "Valve disconnected" nor RED "Valve offline". |
| 5 | Check twin reported. | UART `Twin reported (%d): %s` (format) contains `"valve_id":null`, `"valve_mac":null`, `"valve_device_id":null`, `"provisioned":true`, `"ble_leak_sensor_count":4`. In the VS Code twin view, `valve_id` is gone. |
| 6 | Send `valve_open` (`{"schema":"eflostop.cmd","ver":1,"id":"dec-09-s6","cmd":"valve_open"}`). | `cmd_ack` `error`, with `"detail":"No valve is set up for this hub."` UART: `IOTHUB: Command: VALVE_OPEN`, `IOTHUB: VALVE_OPEN refused — No valve is set up for this hub.` No snapshot follows, and nothing is sent to any valve. |
| 7 | Cross-check (the SAF tests do this in depth): wet `<BLE1>` for about 30 s, then dry it. | Wet: `leak_detected` for `<BLE1>`, `LEAK INCIDENT latched by ble_leak_sensor sensor <BLE1>`, `AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <BLE1>`, `RULES_ENGINE: AUTO-CLOSE: no provisioned valve - auto_close event not published`, `RULES_ENGINE: AUTO-CLOSE: no provisioned valve - nothing to close`. **No** `auto_close` event. Snapshot critical, "Leak detected: Sink, Leak interlock latched", `"valve": {}`. LED RED. Dry: `leak_cleared`, `All sensors clear — auto-clear timer started (10s)`. 10–12 s later: `AUTO-CLEAR…`, `RULES_ENGINE: No valve identity available — event emitted without valve_id`, event `{"event":"rmleak_auto_cleared","clear_after_seconds":10}` with **no** `valve_id`. LED YELLOW → GREEN. |
| 8 | UI. | The app shows no valve (no "valve offline" card), and the 4 sensors are healthy. |

**Block A: UART.** On the esp-mqtt task:
- `IOTHUB: C2D cmd='decommission' ver=1 id='dec-09-s2'`
- `IOTHUB: !!! DECOMMISSION_VALVE !!!`
- `PROVISIONING: === REMOVING VALVE ===`
- `PROVISIONING: Config saved to NVS successfully`
- `PROVISIONING: Valve removed successfully`
- `PROVISIONING: State: PROVISIONED`
- `BLE_VALVE: [API] Target MAC cleared`
- `BLE_VALVE: [CMD] No valve commands to flush (valve decommissioned)`. If a command was queued or pending, you see instead `BLE_VALVE: [CMD] Flushed queued/pending valve commands (valve decommissioned): queued=%u, in flight=%d, pending valve=%d rmleak=%d` (format). Either way, nothing is replayed later (P0-c).
- the `cmd_ack` `Pub event:`

Valve task:
- `BLE_VALVE: [TASK] CMD: DISCONNECT`
- the `GAP DISCONNECT EVENT` banner
- `BLE_VALVE: [DISCONNECT] reason=0x%02x` (format; normally `0x16`, terminated locally)
- `BLE_VALVE: [DISCONNECT] Link was not the provisioned valve - hub not notified`

`iothub_task`:
- `HEALTH_ENGINE: Device table loaded: 4 device(s) (+0 added, -1 removed)`
- `RULES_ENGINE: Valve replaced: old valve leak source not tracked, 0 source(s) still wet, incident not latched`
- `IOTHUB: Valve detectors reset for no valve`
- `IOTHUB: Twin reported (%d): %s` (format)
- `IOTHUB: SNAP trigger=event:decommission`, then `Pub snapshot`

**Must NOT appear** (T0 → T0 + 240 s):
- `PROVISIONING: Hub empty: rules config reset to defaults` and `RULES_ENGINE: Rules-engine state reset (hub empty / decommission)` (sensors remain);
- `IOTHUB: SNAP trigger=event:valve_unlinked`;
- `BLE_VALVE: [SCAN] Starting scan for provisioned valve`, `BLE_VALVE: [SCAN] Target MAC matched`, any new `GAP CONNECT EVENT`, `[PASSKEY]` (no connection to this valve or to valve B);
- `HEALTH_ENGINE: ALERT: valve …`, or any `device_offline` event for the valve;
- any snapshot after the ack with a non-empty `valve` object, or with `"state":"disconnected"`.

**Physical check.** The valve is left as it was (open). It is not commanded to close.

**Timing:**
- `[DISCONNECT]` ≤ 5 s after the ack (about 0.5 s nominal);
- the snapshot ≤ 7 s after the ack;
- the watch lasts 240 s (the 180 s grace + 30 s tick + margin).

**Known limitation (observe and record).** Between the target change and the old link's `DISCONNECT` (about 0.5 s, up to 5 s), a C2D check can read the old valve's cached state. Here `valve_open` is refused by the target check first, so nothing can be seen. Record only if step 6 is accepted (that would be a Fail).

| Result | Notes |
|---|---|
| | |

---

### DEC-10 — Decommission error paths: repeated id, unknown and invalid ids, no valve, bad targets (S12)

**Purpose.**
- Every failed `decommission` returns an `error` ack with the exact `detail` from the code.
- A failed one changes nothing: no reconcile, no twin, no snapshot.
- A repeated command id is not de-duplicated. That is documented, and de-duplication is deferred to 2.1.5. The 2.1.3 field backend reused `decom-b-002` for different sensors.
- `decommission` of the valve on a hub without one now fails (it acked `ok` on 2.1.3).

**Preconditions.** DEC-09 end state: `<BLE1>`…`<BLE4>`, no valve, all dry, no incident, LED GREEN. Interval 60 s.

**The error ack shape** (`detail` changes per step):

```json
{"schema":"eflostop.v2","ts":<epoch>,"gateway":{"id":"<GW>","short_id":"<last 4>","fw":"2.1.4","uptime_s":<n>},"type":"event","data":{"event":"cmd_ack","id":"<id>","cmd":"decommission","status":"error","error":{"code":"decommission","detail":"<detail>"}}}
```

**After every error step, check these for 10 s after the ack:**
- no `HEALTH_ENGINE: Device table loaded`;
- no `IOTHUB: Twin reported`;
- no `SNAP trigger=event:decommission`;
- no `SENSOR_META: Removed metadata`;
- the next heartbeat snapshot equals the previous one (apart from `ts`, `uptime_s`, `last_seen_age_s`, and `data.reason`).

**Steps**

| # | Command (one line each) | Expected ack | Expected UART |
|---|---|---|---|
| 1 | `{"schema":"eflostop.cmd","ver":1,"id":"decom-b-002","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE3>"}}` | `ok` | The normal removal (as in DEC-03), with `Device table loaded: 3 device(s) (+0 added, -1 removed)` and one `event:decommission` snapshot without `<BLE3>`. |
| 2 | The **same message again**, with the same id `decom-b-002` and the same sensor. | `error`, `"detail":"ble sensor decommission failed"` | `IOTHUB: C2D cmd='decommission' ver=1 id='decom-b-002'`, `IOTHUB: !!! DECOMMISSION_BLE: <BLE3> !!!`, `PROVISIONING: === REMOVING BLE LEAK SENSOR <BLE3> ===`, `PROVISIONING: BLE sensor <BLE3> not found in provisioned list` |
| 3 | The same id `decom-b-002` with a **different** sensor: `…"sensor_id":"<BLE2>"}}` | `ok` (**observe and record**: the hub does not de-duplicate ids, a known limitation) | The normal removal of `<BLE2>`: `Device table loaded: 2 device(s) (+0 added, -1 removed)`, one snapshot without `<BLE2>`. |
| 4 | `{"schema":"eflostop.cmd","ver":1,"id":"dec-10-s4","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"00:80:E1:00:00:99"}}` (unknown MAC) | `error`, `"detail":"ble sensor decommission failed"` | `!!! DECOMMISSION_BLE: 00:80:E1:00:00:99 !!!`, `PROVISIONING: BLE sensor 00:80:E1:00:00:99 not found in provisioned list` |
| 5 | `{"schema":"eflostop.cmd","ver":1,"id":"dec-10-s5","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"00:80:E1:2A"}}` (malformed MAC) | `error`, `"detail":"ble sensor decommission failed"` | `!!! DECOMMISSION_BLE: 00:80:E1:2A !!!`, `PROVISIONING: Invalid MAC format: 00:80:E1:2A` |
| 6 | `{"schema":"eflostop.cmd","ver":1,"id":"dec-10-s6","cmd":"decommission","payload":{"target":"ble_leak_sensor"}}` (no `sensor_id`) | `error`, `"detail":"ble sensor decommission failed"` | `IOTHUB: !!! DECOMMISSION_BLE: ? !!!`, and no `PROVISIONING` line |
| 7 | `{"schema":"eflostop.cmd","ver":1,"id":"dec-10-s7","cmd":"decommission","payload":{"target":"lora","sensor_id":"0x12345678"}}` (unknown LoRa) | `error`, `"detail":"lora sensor decommission failed"` | `IOTHUB: !!! DECOMMISSION_LORA: 0x12345678 !!!`, `PROVISIONING: === REMOVING LORA SENSOR 0x12345678 ===`, `PROVISIONING: Sensor 0x12345678 not found in provisioned list` |
| 8 | `{"schema":"eflostop.cmd","ver":1,"id":"dec-10-s8","cmd":"decommission","payload":{"target":"valve"}}` (no valve provisioned) | `error`, `"detail":"valve decommission failed"` (2.1.3 acked `ok`) | `IOTHUB: !!! DECOMMISSION_VALVE !!!`, `PROVISIONING: === REMOVING VALVE ===`, `PROVISIONING: No valve provisioned - nothing to remove`. No `BLE_VALVE: [API] Target MAC cleared`. |
| 9 | `{"schema":"eflostop.cmd","ver":1,"id":"dec-10-s9","cmd":"decommission","payload":{}}` | `error`, `"detail":"missing decommission target"` | `IOTHUB: C2D cmd='decommission' ver=1 id='dec-10-s9'` and nothing else from the handler |
| 10 | `{"schema":"eflostop.cmd","ver":1,"id":"dec-10-s10","cmd":"decommission","payload":{"target":"sensor"}}` | `error`, `"detail":"unknown decommission target"` | as step 9 |

**End state:** `<BLE1>` and `<BLE4>`, no valve. This is DEC-11's start state.

**LED:** GREEN throughout.

**Timing:** each ack ≤ 5 s after the send.

| Result | Notes |
|---|---|
| | |

---
### DEC-11 — Remove the last device: one `event` snapshot of the empty shape, LED WHITE, rules reset, then heartbeats (S8, BUG-3, BUG-6, E-05) — P0

**Purpose.** This is the main empty-hub check. On 2.1.3, removing the last device did two wrong things. It published one snapshot with **stale** valve data and "All devices healthy". Then it published nothing at all until the next re-provision.

On 2.1.4:
- The transition publishes one `event` snapshot with the empty shape (R-EMPTY).
- The LED is WHITE.
- The rules config goes back to `true` / `7`, and the rules-engine state (leak latch, override) is cleared. Both happen inside the removal command, before its ack (E-05).
- A heartbeat snapshot follows at every interval.

This test empties a **sensors-only** hub while a leak is latched, so the state reset is visible. DEC-16 empties a **valve-only** hub.

**Preconditions.**
- DEC-10 end state: `<BLE1>` and `<BLE4>`, no valve, both dry, LED GREEN.
- Interval 60 s (twin reported `"snapshot_interval_s":60`).
- A wet towel ready.

**Commands**

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-11-s1","cmd":"rules_config","payload":{"auto_close_enabled":true,"trigger_mask":3}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-11-s2","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE1>"}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-11-s4","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE4>"}}
```

**Steps.** T0 is the `cmd_ack` of step 4.

| # | Action | Expected |
|---|---|---|
| 1 | Send `dec-11-s1`, so that a reset back to 7 is visible. | `cmd_ack` `ok`. `IOTHUB: Command: RULES_CONFIG`, `PROVISIONING: Setting rules config: auto_close=enabled triggers=0x03`, `PROVISIONING: Rules config saved to NVS`, `RULES_ENGINE: Config updated: auto_close=enabled triggers=0x03`, `IOTHUB: Twin reported (%d): %s` (format) with `"trigger_mask":3`. Snapshot `event:rules_config` with `"rules":{"auto_close_enabled":true,"trigger_mask":3}`. |
| 2 | Send `dec-11-s2`. | Normal removal: `Device table loaded: 1 device(s) (+0 added, -1 removed)`. Snapshot with only `<BLE4>`. |
| 3 | Wet `<BLE4>`. | ≤ 20 s: `leak_detected` for `<BLE4>`, `RULES_ENGINE: LEAK INCIDENT latched by ble_leak_sensor sensor <BLE4>`, `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <BLE4>`, `RULES_ENGINE: AUTO-CLOSE: no provisioned valve - auto_close event not published`, `RULES_ENGINE: AUTO-CLOSE: no provisioned valve - nothing to close`, `HEALTH_ENGINE: Interlock HELD (hub holding valve closed) — system rating floor WARNING`. **No** `auto_close` event. Snapshot `{"rating":"critical","reason":"Leak detected: Main, Leak interlock latched"}`. LED `rating=critical color=RED`. |
| 4 | Keep `<BLE4>` wet. Send `dec-11-s4`, which removes the **last** device. | `cmd_ack` `ok`. Block A. |
| 5 | The first snapshot after the ack. | `SNAP trigger=event:decommission` (or `SNAP trigger=event:hub_emptied`; both are correct) ≤ 7 s after T0. R-EMPTY with `"reason":"event"` and **`"rules":{"auto_close_enabled":true,"trigger_mask":7}`**, `"override_active":false`, and no "Leak interlock latched". `FLEET_LED: rating=unprovisioned color=WHITE effect=SOLID`. |
| 6 | Watch until T0 + 250 s (at least 3 intervals). | `IOTHUB: SNAP trigger=heartbeat` at about T0 + 60 s, + 120 s and + 180 s (each 60 s −1/+3 s after the previous snapshot). Each one is R-EMPTY with `"reason":"heartbeat"`, followed by `IOTHUB: SNAP heartbeat=reset interval_ms=60000`. The LED stays WHITE, with no LED line. |
| 7 | Check twin reported (the UART line after step 4, and the VS Code twin). | R-TWIN-EMPTY: `"provisioned":false`, `"valve_id":null`, `"lora_sensor_count":0`, `"ble_leak_sensor_count":0`, `"auto_close_enabled":true`, `"trigger_mask":7`, `"snapshot_interval_s":60`. |
| 8 | UI. | The app shows the hub with no devices: an empty or "set up" state, not "offline", and no stale valve or sensor tiles. |
| 9 | Dry `<BLE4>` (it is no longer provisioned). | Nothing on the wire. `BLE_LEAK: Whitelist reloaded: 0 sensor(s)` was printed ≤ 10 s after T0. |

**Block A: UART after step 4.** On the esp-mqtt task, in this order:
- `IOTHUB: C2D cmd='decommission' ver=1 id='dec-11-s4'`
- `IOTHUB: !!! DECOMMISSION_BLE: <BLE4> !!!`
- `PROVISIONING: === REMOVING BLE LEAK SENSOR <BLE4> ===`
- `PROVISIONING: No devices remain - state changed to UNPROVISIONED`
- `PROVISIONING: Config saved to NVS successfully`
- `PROVISIONING: BLE leak sensor <BLE4> removed successfully`
- `PROVISIONING: Remaining BLE sensors: 0`
- `PROVISIONING: State: UNPROVISIONED`
- `PROVISIONING: Hub empty: rules config reset to defaults`
- `SENSOR_META: Removed metadata for <BLE4> (type=0), 0 entries remain`, or with another count if other metadata is stored
- `RULES_ENGINE: NVS: rules-engine persistent state cleared`
- `HEALTH_ENGINE: Interlock released — system rating floor removed`
- `RULES_ENGINE: Rules-engine state reset (hub empty / decommission)`
- `IOTHUB: Device is now UNPROVISIONED`
- the `cmd_ack` `Pub event:` (`"status":"ok"`)

Then on `iothub_task`:
- `HEALTH_ENGINE: Device table loaded: 0 device(s) (+0 added, -1 removed)`
- `IOTHUB: Telemetry caches purged: 0 LoRa, 1 BLE (no longer provisioned)`
- `IOTHUB: Hub is now EMPTY - no devices provisioned; heartbeat-only snapshots`
- `IOTHUB: Twin reported (%d): %s` (format)
- `IOTHUB: SNAP trigger=event:decommission`, then `TELEMETRY_V2: Pub snapshot: {...}`, then `IOTHUB: SNAP heartbeat=reset interval_ms=60000`

**Must NOT appear** (T0 → T0 + 250 s):
- `PROVISIONING: Setting rules config: auto_close=enabled triggers=0x07` and `PROVISIONING: Rules config saved to NVS`. These were printed by 2.1.4 development builds; the reset is now part of the removal's own save.
- `IOTHUB: Hub empty: rules-engine RAM reset failed - retry owed` and `RULES_ENGINE: Rules reset: mutex unavailable — erasing NVS state only`.
- `RULES_ENGINE: Rules: forgot removed leak source` (the state was already reset), `RULES_ENGINE: All sensors clear…`, `RULES_ENGINE: AUTO-CLEAR…`, and any `rmleak_auto_cleared` or `rmleak_cleared` event.
- `IOTHUB: SNAP trigger=boot`, `IOTHUB: SNAP trigger=fast`, `IOTHUB: SNAP trigger=commission`, `IOTHUB: PROV pulse armed`, `IOTHUB: Commission: fast snapshot armed`.
- any snapshot with `"valve"` other than `{}`, with a device in an array, or with a rating other than `excellent`.
- a gap longer than 63 s between two snapshots.

**LED:** RED (step 3) → WHITE (`rating=unprovisioned`) at step 4, then WHITE throughout.

**Timing:**
- the event snapshot ≤ 7 s after T0;
- heartbeats every 60 s (−1/+3 s) measured from the previous snapshot, at least 3 of them.

| Result | Notes |
|---|---|
| | |

---

### DEC-12 — Empty hub after a reboot, and across MQTT drops: exactly one `boot` snapshot per connect (S9, BUG-6) — P0

**Purpose.** A hub with no devices must still publish its lifecycle, twin and snapshots:
- one `boot` snapshot after every boot or MQTT (re)connect, once the clock and MQTT are up;
- then heartbeats;
- no `boot` or `fast` snapshot storm.

The council flagged this path as never run on a bench since `on_hub_emptied` changed.

**Preconditions.** DEC-11 end state (empty, interval 60 s). Router WAN cable accessible.

**Steps**

| # | Action | Expected |
|---|---|---|
| 1 | Power-cycle the hub (remove power for 5 s). | Block A (boot). |
| 2 | Wait for the MQTT connect, then 200 s. | `IOTHUB: Connected to Azure IoT Hub!`, `TELEMETRY_V2: MQTT connected = true`, then `TELEMETRY_V2: Pub lifecycle: {...}` (R-LIFE-EMPTY with `"reset_reason":"power_on"`), `IOTHUB: Twin reported (%d): %s` (R-TWIN-EMPTY), **one** `IOTHUB: SNAP trigger=boot` (R-EMPTY with `"reason":"boot"`) ≤ 7 s after `Connected`. After that, only `SNAP trigger=heartbeat` every 60 s. No `SNAP trigger=fast`, even after 150 s. |
| 3 | Unplug the router's **WAN** cable (Wi-Fi stays up). Leave it out for **at least 3 min**, and at least 60 s after the drop is logged. | `IOTHUB: Disconnected.` and `TELEMETRY_V2: MQTT connected = false` within **≤ 120 s** of the unplug (MQTT keepalive 60 s; esp-mqtt can take up to about 2× keepalive to notice a WAN-only outage, 0.15). A heartbeat published before that line is normal. **After** `MQTT connected = false`: no `Pub snapshot` while offline (a heartbeat falling due now is dropped, not buffered), and no `Offline — buffering` (nothing is queued). |
| 4 | Plug the WAN back in. After the reconnect's `boot` snapshot, watch for **at least 185 s**. | `IOTHUB: Connected to Azure IoT Hub!`, lifecycle (R-LIFE-EMPTY, `"reset_reason"` still `"power_on"`), twin, **one** `SNAP trigger=boot` (R-EMPTY `"reason":"boot"`) ≤ 7 s after `Connected`. Then **3** `SNAP trigger=heartbeat`, each 60 s (−1/+3 s) after the previous snapshot (the first timed from the `boot` snapshot), all R-EMPTY with `"reason":"heartbeat"`. Record the three `ts` differences. |
| 5 | Repeat steps 3–4 twice more. | Each reconnect: exactly one lifecycle and exactly one `boot` snapshot, then 3 heartbeats 60 s (−1/+3 s) apart, all R-EMPTY. |
| 6 | Power the router off for 2 min (Wi-Fi drops too), then on. After the reconnect's `boot` snapshot, watch for **at least 185 s**. | After the Wi-Fi and MQTT reconnect: one lifecycle and one `boot`, as step 4, then **3** heartbeats 60 s (−1/+3 s) apart, all R-EMPTY. |
| 7 | Check the IoT Hub capture for the whole test. | Count the `boot` snapshots: power-up + 3 WAN reconnects + 1 router reboot = **5**. Every snapshot is R-EMPTY (`boot` or `heartbeat`). Run the validator (0.18): 0 FAIL. |

**Block A: boot UART** (empty hub). In boot order:
- `PROVISIONING: Loaded existing config from NVS`
- `PROVISIONING: State: UNPROVISIONED`
- `PROVISIONING: Rules: auto_close=enabled triggers=0x07`
- `HEALTH_ENGINE: Device table loaded: 0 device(s) (+0 added, -0 removed)`
- `IOTHUB: Boot: hub is empty - clearing any persisted rules-engine state`
- `RULES_ENGINE: NVS: rules-engine persistent state cleared`
- `RULES_ENGINE: Rules-engine state reset (hub empty / decommission)`
- `IOTHUB: Hub is UNPROVISIONED - waiting for provisioning JSON from Azure`
- `TELEMETRY_V2: Snapshot interval 60s restored from NVS`
- `IOTHUB: QueueSet Initialized. Event loop starting...`

There is **no** `IOTHUB: Starting BLE` and no `BLE_VALVE: [INIT] Signal received`: an empty hub does not start BLE. LED: `FLEET_LED: rating=startup color=OFF effect=SOLID`, then `FLEET_LED: rating=unprovisioned color=WHITE effect=SOLID`.

**Accepted race (record it).** At a reconnect, one `heartbeat` snapshot may go out just **before** the lifecycle message. That happens when a heartbeat was overdue and the loop pass began before the connect. The `boot` snapshot must still follow, exactly once. Two `boot` snapshots in one connect, or any `fast`, is a Fail.

**LED:** WHITE throughout (OFF for the first ~3 s of boot).

**Timing:**
- `boot` ≤ 7 s after `Connected to Azure IoT Hub!`;
- heartbeats 60 s (−1/+3 s) after the previous snapshot.

| Result | Notes |
|---|---|
| | |

---

### DEC-13 — Re-provision an empty hub after 1 and after 10 heartbeats (S10, BUG-6, E-10/E-17, council: BLE start claim) — P0

**Purpose.**
- A `provision` on a hub that has been empty for a while runs the full UI sync, as in DEC-01.
- The first snapshot after its ack already shows the new devices. That is, no stale "No devices provisioned" `event` snapshot slips out after the `ok` (E-10, E-17).
- BLE starts exactly once when the first device arrives on a hub that booted empty (council: atomic BLE start claim).

**Preconditions.**
- DEC-12 end state: empty, BLE **not** started this boot (no `[INIT] Signal received` since the last power-up), interval 60 s.
- Valve and BLE1–4 powered, dry, in range.

**Commands**

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-13-s1","cmd":"provision","payload":{"ble_leak_sensors":["<BLE1>"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE1>","location_code":"kitchen","label":"Sink"}]}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-13-s3","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE1>"}}
```

The step 5 command (the DEC-01 `provision` payload with a new id):

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-13-s5","cmd":"provision","payload":{"valve_id":"<VALVE_MAC>","ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE1>","location_code":"kitchen","label":"Sink"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE2>","location_code":"laundry","label":"Washer"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE3>","location_code":"bathroom","label":"Ensuite"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE4>","location_code":"bathroom","label":"Main"}],"auto_close_enabled":true}}
```

**Steps**

| # | Action | Expected |
|---|---|---|
| 1 | After exactly **one** `SNAP trigger=heartbeat` has followed the last `boot` snapshot, send `dec-13-s1`. Call its ack T0. | esp-mqtt: `BLE_VALVE: [API] Target MAC cleared` (the apply re-states "no valve"; no flush line follows), `IOTHUB: Starting BLE (valve=none, BLE sensors=1)`, then `BLE_VALVE: [INIT] Signal received. Starting BLE stack...` (**exactly once** this boot), `BLE_VALVE: [HOST] NimBLE host task started`, `BLE_LEAK: NimBLE ready, initializing scanner`, `cmd_ack` `ok`. `iothub_task`: `HEALTH_ENGINE: Device table loaded: 1 device(s) (+1 added, -0 removed)`, `IOTHUB: Commission: fast snapshot armed…`, `IOTHUB: PROV pulse armed…`, `Twin reported` (`"provisioned":true`, `"ble_leak_sensor_count":1`). |
| 2 | The **first** snapshot after the ack. | `SNAP trigger=event:provision`, with `{"rating":"excellent","reason":"Syncing - waiting for 1 device"}`, `"valve":{}`, and `<BLE1>` unheard (the R-SYNC entry form). **Fail** if any snapshot after this ack says "No devices provisioned". Then, ≤ T0 + 110 s: `<BLE1>` heard, `HEALTH_ENGINE: Boot sync: all devices seen`, `SNAP trigger=boot` with "All devices healthy", `FLEET_LED: rating=excellent color=GREEN effect=SOLID`. |
| 3 | Send `dec-13-s3`. | As DEC-11 block A, but for `<BLE1>`, with no latched incident (so no `Interlock released` line). R-EMPTY `event`. LED WHITE. |
| 4 | Count 10 heartbeats (about 10 min). | Exactly 10 `SNAP trigger=heartbeat`, 60 s (−1/+3 s) apart, all R-EMPTY. Nothing else. |
| 5 | Send `dec-13-s5` (valve + BLE1–4, with labels). Call its ack T1. | As DEC-01 blocks A–C, with two differences. First, BLE is already running, so `IOTHUB: Starting BLE (valve=<VALVE_MAC>, BLE sensors=4)` is printed but **no** second `[INIT] Signal received`. Second, `HEALTH_ENGINE: Device table loaded: 5 device(s) (+5 added, -0 removed)`. First snapshot after the ack: `event:provision`, with "Syncing - waiting for 5 devices" (or 4 if the valve has already linked). Never "No devices provisioned". |
| 6 | Follow the DEC-01 checks to the `boot` snapshot. | R-FULL `boot`, LED GREEN. **Start DEC-14 now**, while this `provision`'s pulse window is open. |

**Must NOT appear:**
- a second `BLE_VALVE: [INIT] Signal received. Starting BLE stack...` in this boot;
- `nimble_port_init` errors;
- a panic;
- any snapshot showing "No devices provisioned" after the step 1 or step 5 ack.

**LED:**
- WHITE (empty);
- WHITE (syncing, no LED line) → GREEN at step 2;
- WHITE (`unprovisioned`) at step 3;
- WHITE (syncing) → GREEN at step 6.

**Timing:**
- first snapshot ≤ 7 s after each ack;
- BLE up ≤ 3 s after the step 1 ack;
- each dry sensor heard ≤ 110 s.

| Result | Notes |
|---|---|
| | |

---

### DEC-14 — A snapshot that follows a removal or an add never shows the old device set (E-10)

**Purpose.** E-10: a snapshot that fell due in the same `iothub_task` pass as a C2D `provision` or `decommission` used to be built from the **unreconciled** table. It was published after the `ok` ack and still showed the removed device, for example a removed valve as linked and open. The flush now waits one pass for the change. This test raises the chance of that collision: it runs removals and adds inside an open pulse window, where snapshots are due every few seconds.

**Preconditions.**
- DEC-13 end state: V + BLE1–4, all heard, with the step 5 `provision`'s pulse window **open** (≤ 4 min since its ack).
- Each re-add below re-arms the window, so it stays open.

**Commands** (repeat the cycle 3 times; replace `N` with the cycle number):

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-14-cN-a","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE2>"}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-14-cN-b","cmd":"provision","payload":{"ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE2>","location_code":"laundry","label":"Washer"}]}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-14-cN-c","cmd":"decommission","payload":{"target":"valve"}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-14-cN-d","cmd":"provision","payload":{"valve_id":"<VALVE_MAC>"}}
```

**Steps (one cycle).** Send each command about 20 s after the previous one.

| # | Action | Expected: the first `Pub snapshot` on UART after that command's `cmd_ack` `Pub event:` |
|---|---|---|
| a | Remove `<BLE2>`. | No `<BLE2>` in `ble_leak_sensors`. The other devices keep their values. |
| b | Re-add `<BLE2>`. | `<BLE2>` present. It is unheard (`connected:false`, nulls) with "Syncing - waiting for 1 device", or already heard if its packet arrived first. `PROV pulse armed…` printed. |
| c | Remove the valve. | `"valve": {}`. Never a `valve_id`, and never `"state":"open"` / `"connected":true`. `[DISCONNECT] Link was not the provisioned valve - hub not notified` follows. |
| d | Re-add the valve. | A valve block with `"valve_id":"<VALVE_MAC>"`. It shows `"state":"disconnected","connected":false` until the valve relinks, then the R-FULL valve block. Within 30 s: `[SCAN] Target MAC matched - connecting to provisioned valve: <VALVE_MAC>` and `SETUP COMPLETE` (E-09: the valve relinks every cycle). |

**Pass.** In all 3 cycles, 12 commands in total:
- every first snapshot after an ack reflects that command;
- no snapshot after an ack shows the pre-change set;
- the valve relinks after every re-add.

Match the snapshots to the acks by UART order (`Pub event: …cmd_ack…` then `Pub snapshot:`), or by `ts` in the IoT Hub capture.

**Fail.** Any snapshot after an `ok` ack still containing the removed device, or lacking the added one. Also a Fail: the valve not relinking within 60 s of a re-add (compare with DEC-16 step 5, a known limitation for a same-pass re-add).

**LED:**
- GREEN, or WHITE while `<BLE2>` is unheard after its re-add;
- after the valve re-add, WHITE until the valve is heard again.

A valve removed and re-added is a new table entry, so it is unheard until it relinks, which WHITE (syncing) shows.

**Timing:** each snapshot ≤ 7 s after its ack; the valve relinks ≤ 30 s after the re-add.

| Result | Notes |
|---|---|
| | |

---

### DEC-15 — Decommission while MQTT is offline (S13)

**Purpose.** Check that a removal is applied correctly when MQTT is down around it:
- the reconcile runs whatever the MQTT state;
- the ack is either published live or buffered offline and replayed;
- the snapshot is regenerated at the reconnect and shows the new set.

A C2D message can only reach the hub while it is connected. So case A uses IoT Hub's own C2D queue, which delivers a message sent while the hub was offline once it reconnects. Case B tries to drop the link while the command is being handled.

**Preconditions.** `SS-V4` (labels), interval 60 s. Router WAN cable accessible.

**Commands**

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-15-s2","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE3>"}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-15-s6","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE4>"}}
```

**A. Command queued in IoT Hub while the hub is offline**

| # | Action | Expected |
|---|---|---|
| 1 | Unplug the WAN. Wait for `IOTHUB: Disconnected.` | `TELEMETRY_V2: MQTT connected = false`. |
| 2 | Send `dec-15-s2` from VS Code. | The extension reports the message sent (IoT Hub queues it). Nothing on the hub's UART. |
| 3 | Wait 60 s, then plug the WAN back in. | `IOTHUB: Connected to Azure IoT Hub!` Then, in either order: the reconnect's lifecycle, twin and **one** `SNAP trigger=boot` (or `SNAP trigger=fast`); and the queued command, with `IOTHUB: C2D cmd='decommission' ver=1 id='dec-15-s2'`, `!!! DECOMMISSION_BLE: <BLE3> !!!`, `cmd_ack` `ok` (live `Pub event:`), `Device table loaded: 4 device(s) (+0 added, -1 removed)`, `Twin reported` with `"ble_leak_sensor_count":3`, and an `event:decommission` snapshot (it may coalesce with the `boot`). |
| 4 | Check. | The **last** snapshot after the ack has no `<BLE3>`, and the survivors keep their values (the DEC-03 check). The lifecycle `ble_leak_sensor_count` is 4 or 3, depending on whether it went out before the command was delivered: record which. There is exactly one `boot` / `fast` for this reconnect. |

If the queued command is not delivered within 2 min of the reconnect, record A as `Blocked` ("C2D not delivered after reconnect": this depends on the IoT Hub C2D TTL and the MQTT session). Re-send the command online to get to the next start state, and run B.

**B. Link drop while the command is handled** (opportunistic)

| # | Action | Expected |
|---|---|---|
| 5 | Keep the WAN cable in your hand. | — |
| 6 | Send `dec-15-s6`. The moment the UART shows `IOTHUB: C2D cmd='decommission' ver=1 id='dec-15-s6'`, unplug the WAN. | The removal is applied whatever the MQTT state. `PROVISIONING: BLE leak sensor <BLE4> removed successfully` and `HEALTH_ENGINE: Device table loaded: 3 device(s) (+0 added, -1 removed)` are both printed, whether or not `IOTHUB: Disconnected.` has appeared yet: the reconcile never waits for MQTT. `IOTHUB: Disconnected.` follows within ≤ 120 s of the unplug (MQTT keepalive 60 s, up to about 2× for a WAN-only outage, 0.15). |
| 7 | Note where the ack went. | Either the `cmd_ack` `Pub event:` came before `Disconnected.` (live, the usual case), or `TELEMETRY_V2: Offline — buffering event event` and `OFFLINE_BUF: Stored event [%s] (%u bytes), %d buffered` (format). No `Pub snapshot` while offline. |
| 8 | After 60 s, plug the WAN back in. | If the ack was buffered: `TELEMETRY_V2: Draining %d offline event(s) before lifecycle...` (format), `OFFLINE_BUF: Replayed [%s] (%u bytes)` (format), `TELEMETRY_V2: Offline drain complete: %d event(s) replayed` (format), **then** the lifecycle message. The IoT Hub capture then shows the `cmd_ack` (id `dec-15-s6`) before the lifecycle. Then one `boot` (or `fast`) snapshot with no `<BLE4>`, and a twin with `"ble_leak_sensor_count":2`. |

Case B passes if:
- `<BLE4>` was removed exactly once;
- the snapshot after the reconnect excludes it;
- the ack arrives exactly once (live or replayed);
- the hub does not reboot.

Record whether the ack was buffered. Neither outcome is a Fail.

**Restore.** Send the `ss-v4m` payload (0.8) with id `dec-15-s9` to get back to `SS-V4`.

**LED:** GREEN throughout (MQTT does not affect the fleet LED).

**Timing:**
- `boot` ≤ 7 s after `Connected`;
- the queued C2D ≤ 2 min after the reconnect (IoT Hub side).

| Result | Notes |
|---|---|
| | |

---

### DEC-16 — Back to back: empty the hub, then `provision` or `rules_config` at once; the new config survives (E-05, E-09, F-06/F-07) — P0

**Purpose.**
- **E-05.** The empty-hub rules reset used to run later on `iothub_task`, unordered against the next C2D command. So a `provision` or `rules_config` sent right after the last removal could be overwritten back to `true` / `7` after its `ok` ack. Or the reset was skipped, and the old install's latch and override carried over. The reset now happens inside the removal, before its ack, and esp-mqtt handles C2D one at a time.
- **E-09.** A valve re-provisioned right after its decommission must always relink, and must rescan after its next drop.

**Preconditions.** `SS-V4` (labels), interval 60 s. Both commands of each pair prepared in VS Code, so that you can send them within 1 s of each other.

**Commands**

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-16-s1","cmd":"provision","payload":{"ble_leak_sensors":[]}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-16-s2","cmd":"decommission","payload":{"target":"valve"}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-16-s3","cmd":"provision","payload":{"valve_id":"<VALVE_MAC>","auto_close_enabled":false}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-16-s7","cmd":"decommission","payload":{"target":"valve"}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-16-s8","cmd":"rules_config","payload":{"auto_close_enabled":false,"trigger_mask":3}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-16-s11","cmd":"rules_config","payload":{"auto_close_enabled":true,"trigger_mask":7}}
```

**Steps**

| # | Action | Expected |
|---|---|---|
| 1 | Send `dec-16-s1`. It leaves the valve only, and is a `provision` that removes 4 sensors. | `HEALTH_ENGINE: Device table loaded: 1 device(s) (+0 added, -4 removed)`, `IOTHUB: Telemetry caches purged: 0 LoRa, 4 BLE (no longer provisioned)`. No `PROV pulse armed` (nothing was added). Snapshot `event:provision` with the valve only and `"ble_leak_sensors":[]`. No `Hub empty` line (the valve remains). |
| 2 | Send `dec-16-s2` then, **within 1 s**, `dec-16-s3`. | Block A. Both acks `ok`. |
| 3 | Check the rules. | Every snapshot after the `dec-16-s3` ack: `"rules":{"auto_close_enabled":false,"trigger_mask":7}`. Twin reported: `"auto_close_enabled":false`, `"trigger_mask":7`. **Fail** if any snapshot or twin after that ack shows `true`. |
| 4 | Check the valve link (E-09). | After `BLE_VALVE: [TASK] CMD: DISCONNECT` and the `GAP DISCONNECT EVENT`: `BLE_VALVE: [TASK] CMD: CONNECT`, `BLE_VALVE: [SCAN] Starting scan for provisioned valve <VALVE_MAC>...`, `BLE_VALVE: [SCAN] Target MAC matched - connecting to provisioned valve: <VALVE_MAC>`, `SETUP COMPLETE - READY FOR GATT` within 30 s. The next snapshot shows the valve `"connected":true`, `"state":"open"`. If the CONNECT ran before the old link had dropped, `BLE_VALVE: [SCAN] Already connected` (warning) appears first; the rescan must still follow the drop. |
| 5 | **Observe and record (known limitation, planned for 2.1.5).** A decommission and re-provision of the same valve in one loop pass can leave health saying connected with no link. If after step 4 there is a `GAP DISCONNECT` with **no** new `GAP CONNECT` within 60 s, while snapshots still say `"connected":true` / `excellent` and the LED is GREEN, record `Known-limit` with the log times. Then power-cycle the valve to recover. | — |
| 6 | E-09: power-cycle the valve (PSU off 10 s, then on). | The hub rescans and relinks: `[SCAN] Starting scan for provisioned valve <VALVE_MAC>...` and `SETUP COMPLETE` ≤ 30 s after power-on. Snapshot `connected:true`. |
| 7 | Send `dec-16-s7` then, **within 1 s**, `dec-16-s8`. | Block B. Both acks `ok`. The hub is now empty. |
| 8 | Check the rules. | Every snapshot after the `dec-16-s8` ack is R-EMPTY with **`"rules":{"auto_close_enabled":false,"trigger_mask":3}`**, including at least 2 heartbeats. Twin: `"provisioned":false`, `"auto_close_enabled":false`, `"trigger_mask":3`. **Fail** if `true` / `7` appears after that ack. |
| 9 | Power-cycle the hub. | Boot: `PROVISIONING: Rules: auto_close=disabled triggers=0x03`, `IOTHUB: Boot: hub is empty - clearing any persisted rules-engine state` (state only; the config is kept). Lifecycle `"rules":{"auto_close_enabled":false,"trigger_mask":3}`. One `boot` snapshot, R-EMPTY with the same rules. |
| 10 | F-06/F-07, observe throughout. | `IOTHUB: Hub empty: rules-engine RAM reset failed - retry owed` and `RULES_ENGINE: Rules reset: mutex unavailable — erasing NVS state only` should never appear. If one does, record `Known-limit` (a failed empty-hub reset is not retried when a `provision` follows). |
| 11 | Restore: send `dec-16-s11`. | `cmd_ack` `ok`. The snapshot rules are back to `true` / `7`. |

**Block A: UART for steps 2+3.** esp-mqtt, strictly in this order (C2D is sequential):
- `dec-16-s2` (decommission):
  - `IOTHUB: !!! DECOMMISSION_VALVE !!!`
  - `PROVISIONING: === REMOVING VALVE ===`
  - `PROVISIONING: No devices remain - state changed to UNPROVISIONED`
  - `PROVISIONING: Config saved to NVS successfully`
  - `PROVISIONING: Valve removed successfully`
  - `PROVISIONING: State: UNPROVISIONED`
  - `PROVISIONING: Hub empty: rules config reset to defaults`
  - `BLE_VALVE: [API] Target MAC cleared`
  - `BLE_VALVE: [CMD] No valve commands to flush (valve decommissioned)`, or the `Flushed…` warning
  - `RULES_ENGINE: NVS: rules-engine persistent state cleared`
  - `RULES_ENGINE: Rules-engine state reset (hub empty / decommission)`
  - `IOTHUB: Device is now UNPROVISIONED`
  - `cmd_ack` `ok`
- then `dec-16-s3` (provision):
  - `IOTHUB: Provisioning JSON detected`
  - `PROVISIONING: Auto-close opt-in: disabled (triggers=0x07)`
  - `PROVISIONING: Provisioning completed successfully!`
  - `PROVISIONING: Auto-close: disabled triggers=0x07`
  - `IOTHUB: Applying provisioned valve MAC: <VALVE_MAC>`
  - `BLE_VALVE: [API] Target MAC set to: <VALVE_MAC>`
  - `BLE_VALVE: [CMD] No valve commands to flush (valve target set)`
  - `IOTHUB: Starting BLE (valve=<VALVE_MAC>, BLE sensors=0)`
  - `cmd_ack` `ok`

Every reset line of the decommission comes **before** `Provisioning JSON detected`.

On `iothub_task` there are two possible outcomes. Record which one you get.
- **Two changes.** `Device table loaded: 0 device(s) (+0 added, -1 removed)` and `Hub is now EMPTY - no devices provisioned; heartbeat-only snapshots`, `RULES_ENGINE: Valve replaced: old valve leak source not tracked, 0 source(s) still wet, incident not latched`, `Valve detectors reset for no valve`. Then `Device table loaded: 1 device(s) (+1 added, -0 removed)`, `Commission: fast snapshot armed…`, `Valve detectors reset for <VALVE_MAC>`, `PROV pulse armed…`.
- **One combined change.** `Device table loaded: 1 device(s) (+0 added, -0 removed)`, and no `Hub is now EMPTY` and no `Valve replaced`.

In both, no snapshot after the `dec-16-s3` ack shows rules `true`.

**Block B: UART for steps 7+8.**
- The `dec-16-s7` lines, as in block A.
- Then:
  - `IOTHUB: Command: RULES_CONFIG`
  - `PROVISIONING: Setting rules config: auto_close=disabled triggers=0x03`
  - `PROVISIONING: Rules config saved to NVS`
  - `RULES_ENGINE: Config updated: auto_close=disabled triggers=0x03`
  - `IOTHUB: Twin reported (%d): %s` (format)
  - `cmd_ack` `ok`
- `iothub_task`:
  - `Device table loaded: 0 device(s) (+0 added, -1 removed)`
  - `Hub is now EMPTY - no devices provisioned; heartbeat-only snapshots`
  - `Valve replaced: old valve leak source not tracked, 0 source(s) still wet, incident not latched`
  - `Valve detectors reset for no valve`
  - `Twin reported`
  - `SNAP trigger=event:decommission` (or `event:rules_config`, or `event:hub_emptied`: one snapshot, because the two requests coalesce)

**LED:**
- GREEN;
- after step 2: WHITE (unprovisioned, then syncing), then GREEN once the valve relinks;
- after step 7: WHITE.

**Timing:**
- both acks ≤ 5 s;
- the valve relinks ≤ 30 s after the `provision` ack;
- snapshots ≤ 7 s after the acks.

| Result | Notes |
|---|---|
| | |

---

### DEC-17 — A `provision` whose sensor arrays leave a sensors-only hub empty (E-05 follow-up, BUG-6) — P0

**Purpose.** A `provision` with empty sensor arrays, sent to a hub with sensors and no valve, empties the hub. That is a fourth way to empty a hub (it cannot remove a valve). On that edge the hub must:
- reset the rules config to `true` / `7` first, then apply any rules keys in the same payload on top;
- reset the rules-engine state before the ack;
- publish the empty shape.

The hub stays marked provisioned, so lifecycle and twin read `provisioned:true` with no devices. That is a documented wire change: parsers must treat it as an empty hub.

**Preconditions.** DEC-16 end state: empty, rules `true` / `7`, interval 60 s. `<BLE1>` and `<BLE2>` powered and dry.

**Commands**

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-17-s1","cmd":"rules_config","payload":{"auto_close_enabled":false}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-17-s2","cmd":"provision","payload":{"ble_leak_sensors":["<BLE1>","<BLE2>"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE1>","location_code":"kitchen","label":"Sink"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE2>","location_code":"laundry","label":"Washer"}]}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-17-s4","cmd":"provision","payload":{"ble_leak_sensors":[],"lora_sensors":[],"rules":{"trigger_mask":3}}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-17-s8","cmd":"rules_config","payload":{"auto_close_enabled":true,"trigger_mask":7}}
```

**Steps**

| # | Action | Expected |
|---|---|---|
| 1 | Send `dec-17-s1`. | `ok`. Snapshot rules `{"auto_close_enabled":false,"trigger_mask":7}`. |
| 2 | Send `dec-17-s2`. | `IOTHUB: Starting BLE (valve=none, BLE sensors=2)`, `Device table loaded: 2 device(s) (+2 added, -0 removed)`, `PROV pulse armed…`. Rules are still `false` / `7` (this payload has no rules keys). |
| 3 | Wait until both sensors are heard. | "All devices healthy", LED GREEN (≤ 110 s). |
| 4 | Send `dec-17-s4`. Call its ack T0. | Block A. `cmd_ack` `ok`. |
| 5 | The first snapshot after the ack. | `SNAP trigger=event:provision` (or `event:hub_emptied`), R-EMPTY with **`"rules":{"auto_close_enabled":true,"trigger_mask":3}`**: `auto_close_enabled` went from `false` back to `true` (the reset), then the payload's mask 3 applied on top. `FLEET_LED: rating=unprovisioned color=WHITE effect=SOLID`. |
| 6 | Check twin reported. | `"provisioned":true`, `"valve_id":null`, `"lora_sensor_count":0`, `"ble_leak_sensor_count":0`, `"auto_close_enabled":true`, `"trigger_mask":3`. |
| 7 | Watch 2 heartbeats. Then unplug and replug the WAN, and after the reconnect power-cycle the hub. | Heartbeats: R-EMPTY, `"reason":"heartbeat"`, rules `true` / `3`. At the reconnect: lifecycle `{"event":"online",…,"provisioned":true,"lora_sensor_count":0,"ble_leak_sensor_count":0,"rules":{"auto_close_enabled":true,"trigger_mask":3}}` with no `valve_id`, and one `boot` R-EMPTY. After the power cycle: `PROVISIONING: State: PROVISIONED`, `IOTHUB: Boot: hub is empty - clearing any persisted rules-engine state`, `IOTHUB: Hub is PROVISIONED`, no `IOTHUB: Starting BLE`. The lifecycle is as above with `"reset_reason":"power_on"`, and there is one `boot` R-EMPTY. LED WHITE. |
| 8 | Restore: send `dec-17-s8`. | Rules `true` / `7`. |

**Block A: UART for step 4.** esp-mqtt:
- `IOTHUB: Provisioning JSON detected`
- `PROVISIONING: Rules: auto_close=enabled triggers=0x03`
- `PROVISIONING: Config saved to NVS successfully`
- `PROVISIONING: Provisioning completed successfully!`
- `PROVISIONING: State: PROVISIONED`
- `PROVISIONING: LoRa sensors: 0`
- `PROVISIONING: BLE leak sensors: 0`
- `PROVISIONING: Auto-close: enabled triggers=0x03`
- `PROVISIONING: Hub empty: rules config reset to defaults`
- `BLE_VALVE: [API] Target MAC cleared`
- `RULES_ENGINE: NVS: rules-engine persistent state cleared`
- `RULES_ENGINE: Rules-engine state reset (hub empty / decommission)`
- `cmd_ack` `ok`

There is **no** `IOTHUB: Device is now UNPROVISIONED` (the `provision` path keeps the hub marked provisioned) and no `IOTHUB: Starting BLE`.

`iothub_task`:
- `HEALTH_ENGINE: Device table loaded: 0 device(s) (+0 added, -2 removed)`
- `IOTHUB: Telemetry caches purged: 0 LoRa, 2 BLE (no longer provisioned)`
- `IOTHUB: Hub is now EMPTY - no devices provisioned; heartbeat-only snapshots`
- `IOTHUB: Twin reported (%d): %s` (format)
- the `SNAP trigger=event:…` line

**Also check (a variant, optional):** repeat steps 2–5 with the payload `{"ble_leak_sensors":[]}` and no rules keys. The rules must read `true` / `7` afterwards.

**LED:** GREEN → WHITE at step 4, then WHITE.

**Timing:** the snapshot ≤ 7 s after T0; heartbeats every 60 s (−1/+3 s).

| Result | Notes |
|---|---|
| | |

---

### DEC-18 — `decommission` `all`: final snapshot of the empty shape, override cleared, offline buffer cleared, reboot, empty `boot`, heartbeats (S11) — P0

**Purpose.** `decommission` `all` has its own path. It:
- wipes provisioning, the sensor metadata, the hub name, the DPS cache, the rules state and the heartbeat setting;
- acks;
- publishes one unscheduled `decommission` snapshot, which must now have the empty shape;
- clears the offline buffer, so that the old deployment's buffered events are not replayed under the next one (L17);
- restarts.

After the restart the hub must behave like DEC-12. The council flagged this whole path as never run on a bench. This test runs it with a hub name set and an override window active, so the clears are visible.

**Preconditions.**
- `SS-V4` (labels), valve linked and open, all dry.
- Interval 60 s. Note whether the twin **desired** section holds `snapshot_interval_s` or `hub_name`: after the reboot the twin GET re-applies them.

**Commands**

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-18-s1","cmd":"set_hub_name","payload":{"name":"Bench T1"}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-18-s3","cmd":"override_enable"}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-18-s6","cmd":"decommission","payload":{"target":"all"}}
```

**Steps.** T0 is the `cmd_ack` of step 6.

| # | Action | Expected |
|---|---|---|
| 1 | Send `dec-18-s1`. | `ok`. `IOTHUB: Hub name set to: 'Bench T1'`. The next snapshot's `gateway` has `"name":"Bench T1"`. |
| 2 | Wet `<BLE1>`. | `leak_detected`, `auto_close`, the valve closes. LED RED. |
| 3 | Send `dec-18-s3`. | `ok`. `water_access_override_enabled` (`"trigger":"c2d_command"`), and the valve opens. |
| 4 | Dry `<BLE1>`. | `leak_cleared`. No `rmleak_auto_cleared` (the override already released the latch). Snapshot: excellent, "All devices healthy", `"override_active":true`, `"override_remaining_s":<≈86400>`. LED GREEN. |
| 5 | Record the `MONITOR` line. Check the IoT Hub capture is running. | — |
| 6 | Send `dec-18-s6`. | Block A, in order. `cmd_ack` `ok`. |
| 7 | The final snapshot. | `TELEMETRY_V2: Pub snapshot: {...}`, with **no** `SNAP trigger` line before it (this publish is direct), ≤ 2 s after T0: R-EMPTY with `"reason":"decommission"`, **no** `gateway.name`, `"override_active":false`, no `override_remaining_s` / `expires_ts`, and `"rules":{"auto_close_enabled":true,"trigger_mask":7}`. LED `rating=unprovisioned color=WHITE`. |
| 8 | The reboot. | `OFFLINE_BUF: Buffer cleared`, `IOTHUB: Decommissioned — restarting in 3s...`, and the hub restarts 3–5 s later. |
| 9 | The next boot, up to the first connect. | Block B. |
| 10 | After the connect, watch for 200 s. | `TELEMETRY_V2: Pub lifecycle: {...}`: R-LIFE-EMPTY with `"reset_reason":"software"`. `Twin reported`: R-TWIN-EMPTY with `"hub_name":""` and `"snapshot_interval_s":300`, **unless** the twin desired section re-applies them (below). **One** `SNAP trigger=boot` (R-EMPTY, `"reason":"boot"`, no `gateway.name`). Then heartbeats: every 300 s (the default restored by the decommission), or every 60 s if the twin desired `snapshot_interval_s` is 60 (you then see `IOTHUB: Twin: snapshot_interval_s = 60` and `TELEMETRY_V2: Snapshot interval set to 60s (persisted)` after the connect). Both are correct: record which. |
| 11 | Physical and UI. | The valve is left as it was (open). The hub does not scan for it (no `[SCAN]` lines, and no BLE start at all). The app shows the hub as not set up / empty. |

**Block A: UART for step 6.** esp-mqtt:
- `IOTHUB: C2D cmd='decommission' ver=1 id='dec-18-s6'`
- `IOTHUB: !!! DECOMMISSION_ALL !!!`
- `PROVISIONING: === DECOMMISSIONING DEVICE ===`
- `PROVISIONING: Decommissioning successful!`
- `PROVISIONING: Device state: UNPROVISIONED`
- `PROVISIONING: All provisioning data erased from NVS`
- `BLE_VALVE: [API] Target MAC cleared`
- `BLE_VALVE: [CMD] No valve commands to flush (valve decommissioned)`, or the `Flushed…` warning
- `SENSOR_META: All sensor metadata cleared`
- `HUB_IDENT: Hub name cleared`
- `DPS: DPS cache cleared`
- `RULES_ENGINE: NVS: rules-engine persistent state cleared`
- `RULES_ENGINE: Rules-engine state reset (hub empty / decommission)`
- `TELEMETRY_V2: Telemetry settings cleared — snapshot interval back to 300s`
- the `cmd_ack` `Pub event:` (`"id":"dec-18-s6","cmd":"decommission","status":"ok"`)

Valve task: `BLE_VALVE: [TASK] CMD: DISCONNECT`, `GAP DISCONNECT EVENT`, `BLE_VALVE: [DISCONNECT] Link was not the provisioned valve - hub not notified`.

`iothub_task`, after waiting up to 1 s for the valve link to drop:
- `HEALTH_ENGINE: Device table loaded: 0 device(s) (+0 added, -5 removed)`
- `IOTHUB: Telemetry caches purged: 0 LoRa, 4 BLE (no longer provisioned)`
- `IOTHUB: Hub is now EMPTY - no devices provisioned; heartbeat-only snapshots`
- `RULES_ENGINE: Valve replaced: old valve leak source not tracked, 0 source(s) still wet, incident not latched`
- `IOTHUB: Valve detectors reset for no valve`
- `IOTHUB: Twin reported (%d): %s` (format; `"provisioned":false`, `"hub_name":""`, `"snapshot_interval_s":300`)
- `TELEMETRY_V2: Pub snapshot: {...}` (`"reason":"decommission"`)
- `OFFLINE_BUF: Buffer cleared`
- `IOTHUB: Decommissioned — restarting in 3s...`

**Block B: boot after the restart.**
- `PROVISIONING: No existing config found, starting UNPROVISIONED`
- `HEALTH_ENGINE: Device table loaded: 0 device(s) (+0 added, -0 removed)`
- `IOTHUB: Boot: hub is empty - clearing any persisted rules-engine state`
- `IOTHUB: Hub is UNPROVISIONED - waiting for provisioning JSON from Azure`
- **no** `OFFLINE_BUF: Init: %d buffered event(s) pending from before reboot`
- `TELEMETRY_V2: No stored telemetry settings — snapshot interval 300s (default)`, or `TELEMETRY_V2: Snapshot interval 300s (default, none stored)`
- no `IOTHUB: Starting BLE`
- a DPS registration, because the cache was cleared. It can block `iothub_task` for up to 60 s per attempt, a known limitation. It ends with `IOTHUB: DPS: hub=%s device=%s` (format).
- `IOTHUB: Connected to Azure IoT Hub!`

**Must NOT appear:**
- after T0, any snapshot with a device, a `valve` other than `{}`, `"override_active":true` or `gateway.name`;
- after the reboot, any replayed event (`OFFLINE_BUF: Replayed`, `Draining %d offline event(s)`) or a second `boot` snapshot for one connect.

**Power-cut variant (council residual: a power cut mid-decommission-all).** Run T6-02 (variants A and B). It has the expected boot lines, and it records whether the old deployment's buffered events replay (the known pre-existing gap between the ack and `offline_buffer_clear()`).

**LED:** WHITE from step 7. OFF → WHITE across the reboot.

**Timing:**
- the final snapshot ≤ 2 s after T0;
- the restart 3–5 s after `Decommissioned — restarting in 3s...`;
- `Connected` after the reboot typically ≤ 90 s (DPS registration included); record it;
- `boot` ≤ 7 s after `Connected`.

| Result | Notes |
|---|---|
| | |

---

### DEC-19 — Duplicate ids in a `provision` payload are ignored, in any case (N13)

**Purpose.** A repeated id used to take two health slots; the copy that is never heard held the hub RED for good. 2.1.4 (plan N13) keeps the first copy and ignores the rest: BLE MACs compared case-insensitively, LoRa ids compared as parsed values. No radio is needed (the LoRa id is fake, as in DEC-08 variant B).

**Preconditions.** `SS-V4` (labels), all heard, "All devices healthy", LED GREEN. `<ble1>` below means `<BLE1>` written in **lower case** (for example `aa:bb:cc:dd:ee:01`).

**Commands**

```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-19-s1","cmd":"provision","payload":{"valve_id":"<VALVE_MAC>","ble_leak_sensors":["<BLE1>","<BLE2>","<BLE1>","<BLE3>","<BLE4>","<ble1>"],"lora_sensors":["0x1A2B3C4D","0x1a2b3c4d"]}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"dec-19-s4","cmd":"decommission","payload":{"target":"lora","sensor_id":"0x1A2B3C4D"}}
```

| # | Action | Expected | Result | Notes |
|---|---|---|---|---|
| 1 | Send `dec-19-s1`. | `cmd_ack` `ok`. In order: `PROVISIONING: LoRa Sensor[0]: 0x1A2B3C4D`; `PROVISIONING: LoRa sensor 0x1A2B3C4D duplicate in payload - ignored` (W; format `LoRa sensor 0x%08lX duplicate in payload - ignored`); `PROVISIONING: BLE Leak Sensor[0]: <BLE1>`; `BLE Leak Sensor[1]: <BLE2>`; `PROVISIONING: BLE leak sensor <BLE1> duplicate in payload - ignored` (W; format `BLE leak sensor %s duplicate in payload - ignored`); `BLE Leak Sensor[2]: <BLE3>`; `BLE Leak Sensor[3]: <BLE4>`; `PROVISIONING: BLE leak sensor <ble1> duplicate in payload - ignored`. Then `HEALTH_ENGINE: Device table loaded: 6 device(s) (+1 added, -0 removed)` (**6**, not 8). | | |
| 2 | The first snapshot after the ack, and the twin. | `SNAP trigger=event:provision`. `ble_leak_sensors` has **4** entries (`<BLE1>` once, upper case, with its data unchanged); `lora_sensors` has **1** entry `{"sensor_id":"0x1A2B3C4D","connected":false,...}`; `system_health.reason` "Syncing - waiting for 1 device". Twin reported: `"ble_leak_sensor_count":4`, `"lora_sensor_count":1`. LED WHITE (syncing). | | |
| 3 | Check the survivors. | No `null` in `<BLE1>`…`<BLE4>`'s `battery` / `rssi`; the valve stays linked (no `GAP DISCONNECT`). | | |
| 4 | Send `dec-19-s4` to remove the fake id. | As DEC-08 variant B step B3: `!!! DECOMMISSION_LORA: 0x1A2B3C4D !!!`, `Device table loaded: 5 device(s) (+0 added, -1 removed)`, `"lora_sensors":[]`, "All devices healthy", LED GREEN. Only **one** removal is needed: there was never a second copy. | | |

**Fail** if the table holds 7 or 8 devices, if a snapshot lists `<BLE1>` twice or the LoRa id twice, or if "Syncing - waiting for 2 devices" (or more) appears.

---

### 1.2 Section coverage

| Test | Plan scenario | Verifies |
|---|---|---|
| DEC-01 | S1 | UI sync preserved under BUG-2: Syncing countdown, pulse, `boot` snapshot; heap reading |
| DEC-02 | — | Council: a `provision` that adds no device no longer re-arms the pulse or commission snapshot (a UI-sync change); T5-02 is merged here (6 min watch) |
| DEC-03 | S2 | BUG-2: survivors keep their state; no re-sync; no `Boot sync: timeout`; E-10 |
| DEC-04 | S3 | BUG-2 inside the window; per-device deadlines kept; the pulse continues; N10 rating-change snapshot; E-18 (observe) |
| DEC-05 | S4 | BUG-2: removing the last unheard device completes the sync with one `boot` |
| DEC-06 | S5 | BUG-2 in the rules engine: a removed wet sensor is forgotten; auto-clear at 10 s; `leak_reset` accepted; no re-close on `override_cancel` |
| DEC-07 | S6 | L9 cache purge; E-02 scanner reset on add; E-17 purge retry (observe); council: scanner delta state |
| DEC-08 | (LoRa decommission) | LoRa removal; N4: packets from unprovisioned LoRa sensors are ignored; E-18 exact |
| DEC-09 | S7 | BUG-5 `valve {}`; no `valve_unlinked`; twin `valve_id` null; no rescan (P0-a); flush (P0-c); no valve alert; `valve_open` refused with no valve; no `auto_close` with no valve |
| DEC-10 | S12 | Error acks and exact `detail`s; no side effects on error; repeated id (known limitation); valve decommission with no valve |
| DEC-11 | S8 | BUG-3 and BUG-6 transition snapshot; LED WHITE; rules config and state reset in the removal (E-05); heartbeats ≥ 3 |
| DEC-12 | S9 | BUG-6: one `boot` per boot and per MQTT connect; lifecycle `provisioned:false`; no `fast` storm |
| DEC-13 | S10 | Re-provision after 1 and 10 heartbeats; no stale "No devices provisioned" after the ack (E-10/E-17); BLE starts exactly once (council) |
| DEC-14 | — | E-10: a snapshot after a C2D change never shows the old set; E-09 relink under churn |
| DEC-15 | S13 | Removal with MQTT offline: reconcile, ack live or buffered and replayed, snapshot regenerated |
| DEC-16 | — | E-05: back-to-back empty then `provision` / `rules_config`; E-09 same-valve re-provision relinks and rescans; F-06/F-07 and same-pass re-provision (observe) |
| DEC-17 | — | E-05 follow-up: a `provision` that empties the hub; defaults then payload rules; `provisioned:true` with no devices |
| DEC-18 | S11 | `decommission` `all`: final `decommission` snapshot of the empty shape, override cleared, hub name cleared, offline buffer cleared (L17), empty `boot` and heartbeats after the reboot; council: never run on a bench; optional power-cut variant; the power-cut variant is T6-02 |
| DEC-08 variant B | (LoRa decommission, no radio) | LoRa provision and removal with a fake id; survivors unchanged; no `Telemetry caches purged` for a never-heard id |
| DEC-19 | — | N13: duplicate BLE (any case) and LoRa ids in one `provision` are ignored |

## 2. Valve battery (BUG-1; plan S14-S17)

This section checks the valve battery rating with the valve on a bench PSU. The valve is rated critical at 10 % or less, warning at 11-20 % and excellent above 20 %. It also checks the `null` battery for an unknown reading, the refusal of `valve_open` at a critical battery, and the valve's link age.

Every expected line and JSON shape below was checked against the firmware at `d9fa9c8`. The main sources are `health_engine.c` (`compute_valve_rating`, `apply_rating`, `evaluate_timeouts`, `health_get_device_status_all`, `health_alert_to_json`), `telemetry_v2.c` (the snapshot valve block and `build_system_health_reason`), `app_ble_valve.c` (`on_notify`, the setup chain, `reset_link_cache`), `app_iothub.c` (`valve_open_reject_reason`, the snapshot scheduler) and `fleet_led.c`. Where a document disagrees with the code, the code wins, and the test says so.

### 2.0 Common setup for this section

**Hardware**

- Hub on 2.1.4 (`gateway.fw` is `"2.1.4"` in every message).
- eFloStop II valve on FW 2.2.0. Its battery pack is replaced by the bench PSU on the battery terminals. Keep the current limit already used on the valve bench, which must be high enough for a motor run.
- At least one BLE leak sensor, dry, placed so that its rating is `excellent` (RSSI above -80 dBm, battery above 35 %). A LoRa sensor is optional.
- UART: the ESP-IDF serial monitor, logging to a file. IoT Hub: the VS Code Azure IoT Hub extension, with "Start Monitoring Built-in Event Endpoint" on the hub's device and the output saved to a file. The extension's "Send C2D Message to Device" sends the commands.

**PSU voltages (bench facts, valve FW 2.2.0)**

| Name | PSU setting | Valve reading | Valve re-read cadence at this level |
|---|---|---|---|
| GOOD | 6.0 V (or any voltage that reads above 20 %) | more than 20 % | every 10 min |
| LOW | about 5.45 V | 11-20 % | every 60 s (with a beep) |
| CRIT | about 5.35 V | 10 % or less | re-notifies about every 20 s (beeps, closes itself, refuses to open) |

The valve notifies the percentage (0x2A19) on every reading and has no hysteresis. **After you change the voltage, the hub learns the new level only at the valve's next reading.** Allow up to 10 min from GOOD, up to about 70 s from LOW and up to about 30 s from CRIT. Record the actual percentage at each step. The percentages in the plan (65, 10, 11, 9) are examples: the pass criteria are the bands.

**Fast path to a new level (optional).** Set the new voltage, then power-cycle the valve: PSU output off for 10 s, then on. The link is back in 10-20 s and the setup reads the battery. This is a disconnect shorter than the 180 s grace, so it raises no health event. If the link-up reading does not yet reflect the new voltage, wait for the valve's cadence and note it.

**Start state for every test, unless the test says otherwise**

- The hub is provisioned with the valve and the sensor(s), and has been up for more than 10 min. UART has shown `Boot sync: all devices seen` (`HEALTH_ENGINE`).
- MQTT is connected. No leak incident and no override: the last snapshot has `override_active:false`, and `system_health` is `{"rating":"excellent","reason":"All devices healthy"}`.
- The fleet LED is solid GREEN.
- The valve is linked (the snapshot has `valve.connected:true`) with `rmleak:false`.
- The PSU is at GOOD, and the valve is open unless the test says otherwise.

If the baseline reason is anything other than "All devices healthy" (for example a sensor at "signal weak"), fix the placement first. The reason string lists only the devices whose rating equals the system rating, so a warning sensor adds its own part next to "Valve battery low".

**Timing constants (from the code) used for tolerances**

| Constant | Value | Source | Effect on this section |
|---|---|---|---|
| `HEALTH_VALVE_DISC_TIMEOUT_MS` | 180 s | health_engine.h | the disconnect grace before a valve is offline |
| `HEALTH_TICK_INTERVAL_MS` | 30 s | health_engine.h | the grace expiry is seen at the next tick, so `device_offline` comes **180-215 s** after the GAP DISCONNECT |
| `HEALTH_ALERT_DEBOUNCE_MS` | 60 s | health_engine.h | a recovery less than 60 s after `device_offline` is sent at the first tick after the 60 s, so **60-90 s** after the `device_offline` |
| `SNAP_HIGH_WINDOW_MS` / `SNAP_MIN_INTERVAL_MS` | 300 ms / 5 s | app_iothub.c | a rating-change snapshot goes out **within about 5.5 s** of the reading; this plan allows **6 s** from the `[DATA] Battery=` line |
| `FLEET_POLL_MS` | 250 ms | fleet_led.c | the LED follows a rating change within **0.5 s** |
| `SNAPSHOT_INTERVAL_MS` | 300 s (default; the twin can change it) | telemetry_v2.h | heartbeat snapshots |
| `DISCOVERY_TIMEOUT_MS` | 30 s | app_ble_valve.c | an upper bound on valve GATT setup; normally 10-20 s |

**How the firmware behaves (read this before judging a result)**

1. **Bands (valve only):** 10 % or less is `critical` with the reason "Valve battery critical"; 11-20 % is `warning` with "Valve battery low"; above 20 % is `excellent`. There is no `good` band for the valve. An unknown battery (no reading) is never rated.
2. **No health event for the battery.** `device_offline` / `device_recovered` report reachability only. A battery change publishes an `event` snapshot instead (`data.reason:"event"`; UART `SNAP trigger=event:health`, or another label when it coalesces with a valve event in the same window).
3. **A battery change inside a band publishes nothing.** For example 10 → 9, or 65 → 60: the new value appears in the next snapshot for another reason (a heartbeat, an event, a C2D command).
4. **Snapshot `valve.battery`** is the live link's value: a number only when the link is up **and** GATT setup is complete. It is `null` while the link is up but setup is not complete, and the key is **absent** while the valve is disconnected. The **rating** comes from the health engine, which keeps the last real reading across a disconnect.
5. **Cause precedence at critical:** a leak at the valve, then the link past the 180 s grace ("Valve offline"), then the battery ("Valve battery critical"). Inside the grace a critical battery still reads "Valve battery critical", so a critical valve that drops never dips to warning.
6. **`valve.last_seen_age_s`** is `0` while the health engine has the valve linked. After a drop it is the seconds since the drop. It is `null` if the valve was never linked in this uptime.
7. **`device_offline.offline_duration_s`** for the valve is measured from the link drop, so it is normally 180-215.

**Snapshot forcer (used by several tests).** A successful command is always followed by an `event` snapshot. `sensor_meta` that re-sends a sensor's current location is harmless. Copy `code` and `label` from the sensor's `location` object in the last snapshot:

```json
{"schema":"eflostop.cmd","ver":1,"id":"t2-snap-001","cmd":"sensor_meta","payload":{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE1_MAC>","location_code":"<BLE1_CODE>","label":"<BLE1_LABEL>"}}
```

Change the `id` on every send (`t2-snap-002`, ...). Expected: `cmd_ack` `ok`, then UART `SNAP trigger=event:sensor_meta` (`IOTHUB`) within about 5 s.

**Log lines used in this section (verbatim from `d9fa9c8`; "(format)" = has printf fields)**

| Tag | Line | Meaning |
|---|---|---|
| `BLE_VALVE` | `[DATA] Battery=%s` (format), e.g. `[DATA] Battery=10%` | every valve battery read or notify; prints `unknown` for no reading |
| `BLE_VALVE` | `[READY] Battery=%s, Leak=%s, Valve=%s, RMLEAK=%s` (format) | end of GATT setup |
| `BLE_VALVE` | `║            SETUP COMPLETE - READY FOR GATT                   ║` | end of GATT setup (boxed banner) |
| `BLE_VALVE` | `[READY] Announcing flood probe state (dry) for reconciliation` | right after SETUP COMPLETE, dry valve |
| `BLE_VALVE` | `[CONNECT] MAC=%s, handle=%u` (format) | GAP link up |
| `BLE_VALVE` | `[DISCONNECT] reason=0x%02x` (format) | GAP link down (after the `GAP DISCONNECT EVENT` banner) |
| `BLE_VALVE` | `[SCAN] Target MAC matched - connecting to provisioned valve: %s` (format) | rescan found the valve |
| `BLE_VALVE` | `[DATA] Valve State=%d (%s)` (format), e.g. `[DATA] Valve State=0 (CLOSED)` | valve position notify |
| `BLE_VALVE` | `[CMD] Writing %s=%u` (format), e.g. `[CMD] Writing Valve=1` | a valve command written to the valve |
| `IOTHUB` | `Event: BLE Update type=%d` (format) | 1 battery changed, 3 position, 5 CONNECTED, 6 DISCONNECTED |
| `IOTHUB` | `SNAP trigger=event:%s` (format) | an event snapshot is being published |
| `IOTHUB` | `SNAP clamped by min-interval: +%lld ms` (format) | the 5 s spacing delayed it (normal) |
| `TELEMETRY_V2` | `Pub snapshot: %s` / `Pub event: %s` (format) | the JSON as published |
| `HEALTH_ENGINE` | `ALERT: %s %s %s -> %s (%s)` (format), e.g. `ALERT: valve <VALVE_MAC> critical -> critical (device_offline)` | a health event was queued |
| `FLEET_LED` | `rating=%s color=%s effect=SOLID` (format), e.g. `rating=critical color=RED effect=SOLID` | LED colour transition (printed only when the colour changes) |
| `RULES_ENGINE` | `Reconnected: no active incident, valve clear` | valve reconnect with no incident and RMLEAK clear |
| `IOTHUB` | `C2D cmd='%s' ver=%d id='%s'` (format) | C2D received |

**Must never appear in this section:**

- `HEALTH_ENGINE`: `Valve event from %s dropped - the table's valve is %s`. Only one valve is involved.
- `RULES_ENGINE`: `Reconnected: valve RMLEAK active, hub incident clear — re-latching incident`.
- `ALERT: ...` for a battery change on a linked valve.

If the valve reports `rmleak:true` on its own at a critical battery, **stop and raise it**. The `valve_open` refusal would then carry the RMLEAK text, because that check runs first, and a reconnect would latch an incident. This plan expects the valve's battery shut-off to leave RMLEAK clear.

**Reference snapshot, valve critical and linked (T2-01, T2-02, T2-03).** Values in angle brackets and all counters vary. Every other value must match. Shown with one BLE sensor and no LoRa sensor:

```json
{
  "schema": "eflostop.v2",
  "ts": 1790000000,
  "gateway": {"id": "<GW_ID>", "short_id": "<SHORT_ID>", "name": "<HUB_NAME, omitted if none>", "fw": "2.1.4", "uptime_s": 1234},
  "type": "snapshot",
  "data": {
    "reason": "event",
    "system_health": {"rating": "critical", "reason": "Valve battery critical"},
    "valve": {
      "valve_id": "<VALVE_MAC>",
      "state": "closed",
      "battery": 10,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "fw_version": "2.2.0",
      "rating": "critical",
      "last_seen_age_s": 0
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "<BLE1_MAC>",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 40,
        "battery": 90,
        "rssi": -62,
        "leak_state": false,
        "fw_version": "<SENSOR_FW>",
        "location": {"code": "<BLE1_CODE>", "label": "<BLE1_LABEL>"}
      }
    ],
    "rules": {"auto_close_enabled": true, "trigger_mask": 7},
    "override_active": false
  }
}
```

- **Valve LOW:** `system_health` is `{"rating":"warning","reason":"Valve battery low"}`; `valve.battery` is 11-20 and `valve.rating` is `"warning"`. `valve.state` is whatever the valve reports (`"open"` if it had not yet reached critical).
- **Valve GOOD:** `system_health` is `{"rating":"excellent","reason":"All devices healthy"}`; `valve.battery` is above 20 and `valve.rating` is `"excellent"`.
- **Valve disconnected:** the block is exactly `{"valve_id":"<VALVE_MAC>","state":"disconnected","connected":false,"rating":"<rating>","last_seen_age_s":<seconds since the drop>}`, with no `battery`, `leak_state`, `rmleak` or `fw_version` key.

`docs/telemetry/validate_capture.py "<IoT Hub capture file>"` must report 0 FAIL on every capture from this section.

---

### T2-01 — Quick battery-critical check (about 10 min)

**Purpose:** a fast end-to-end check of BUG-1 and S17. A critical valve battery is rated critical with its own reason and RED, raises no health event, and makes `valve_open` fail with the exact detail. It recovers when the battery reads above 20 %.

**Preconditions:** the common start state. The valve is open.

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Set the PSU to CRIT (5.35 V). Power-cycle the valve: output off, wait 10 s, output on. | `[DISCONNECT] reason=0x%02x`; `Event: BLE Update type=6`; `SNAP trigger=event:%s` (`valve_unlinked` or `health`: the two requests coalesce). Then `[CONNECT] MAC=<VALVE_MAC>, handle=%u`, `[DATA] Battery=NN%` with NN ≤ 10, the SETUP COMPLETE banner, `[READY] Battery=NN%, Leak=OK, Valve=%s, RMLEAK=CLEAR`, `Event: BLE Update type=5`, `Reconnected: no active incident, valve clear`, `SNAP trigger=event:%s` (`valve_linked` or `health`). **No `ALERT:` line.** | `valve_unlinked` snapshot: `system_health {"warning","Valve disconnected"}`, disconnected valve block with `rating:"warning"`. Then the reconnect snapshot: the reference critical snapshot, with `battery` = NN, `last_seen_age_s:0`. **No** `category:"health"` event. | GREEN → YELLOW (`rating=warning color=YELLOW effect=SOLID`) during the drop → RED (`rating=critical color=RED effect=SOLID`) at the setup battery read | the snapshot ≤ 6 s after `Event: BLE Update type=5`; the LED ≤ 0.5 s after the `[DATA] Battery=` line | | If NN > 10 (the valve had not yet re-read), wait for its next reading (≤ 10 min) and record the time. |
| 2 | Wait for the valve to close itself (within about 1 min of the critical reading). | `[DATA] Valve State=0 (CLOSED)`; `Event: BLE Update type=3`; `SNAP trigger=event:valve_state_changed` | `valve_state_changed` event: `{"event":"valve_state_changed","source_type":"valve","valve_id":"<VALVE_MAC>","valve_state":"closed","battery":NN,"leak_state":false,"rmleak":false,"fw_version":"2.2.0"}` inside the event envelope | RED | valve behaviour, not a hub timing | | Record whether the valve closed and when. If it was already closed, nothing is sent (delta-gated). |
| 3 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t2-01-open","cmd":"valve_open"}` | `C2D cmd='valve_open' ver=1 id='t2-01-open'`; `Command: VALVE_OPEN`; `VALVE_OPEN refused — Valve battery critical (≤10 %): the valve will not open. Replace the batteries.` **None of** `[TASK] CMD: CONNECT`, `[TASK] CMD: OPEN_VALVE`, `[CMD] Writing Valve=1`, `SNAP trigger=event:valve_open`. | `{"event":"cmd_ack","id":"t2-01-open","cmd":"valve_open","status":"error","error":{"code":"valve_open","detail":"Valve battery critical (≤10 %): the valve will not open. Replace the batteries."}}` in the `data` of an `event` | RED | ack ≤ 2 s | | The detail must match character for character (`≤`, one space before `%`). |
| 4 | Set the PSU to GOOD and power-cycle the valve (10 s off). | as step 1 up to `Reconnected: no active incident, valve clear`, with `[DATA] Battery=NN%` NN > 20 | the `valve_unlinked` snapshot is still critical / "Valve battery critical". The reconnect snapshot: `system_health {"excellent","All devices healthy"}`, `valve.rating:"excellent"`, `battery` NN > 20. No health event. | RED through the drop → a brief YELLOW (`rating=warning color=YELLOW effect=SOLID`) from the setup battery read to CONNECTED → GREEN (`rating=excellent color=GREEN effect=SOLID`) | as step 1 | | During the drop the health engine keeps the last real reading (≤ 10), so the valve stays critical and RED. The setup read (> 20) arrives while the drop is still stamped, which rates the grace (warning, cause LINK) for the 1-5 s until CONNECTED. |
| 5 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t2-01-open2","cmd":"valve_open"}` | `Command: VALVE_OPEN`; `[TASK] CMD: OPEN_VALVE`; `[CMD] Writing Valve=1`; then `[DATA] Valve State=1 (OPEN)` | `cmd_ack` `"status":"ok"`; `valve_state_changed` with `"valve_state":"open"`; an `event` snapshot with `valve.state:"open"` | GREEN | valve open within about 10 s | | |

**Pass:** every row passes, no `ALERT:` line, and no `device_offline` / `device_recovered` in IoT Hub.

---

### T2-02 — Full descent GOOD → LOW → CRIT and back up, with the valve's read cadence (about 35 min)

**Purpose:** BUG-1 at each band, using the valve's real read cadence rather than power cycles. At each step: the snapshot valve block, `system_health`, no health event, the LED colour, and a rating-change snapshot within about 5 s. It also checks that in-band changes and steady re-notifies send nothing extra. This covers the council's open risk that "the valve battery-critical wire contract has not been run on a bench".

**Preconditions:** the common start state. The valve is open and on GOOD for at least 10 min. Note the time of the last `[DATA] Battery=` line (the valve's 10 min clock).

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Baseline: send the snapshot forcer. | `SNAP trigger=event:sensor_meta` | snapshot: `system_health {"excellent","All devices healthy"}`; valve `{"state":"open","battery":>20,"leak_state":false,"rmleak":false,"connected":true,"fw_version":"2.2.0","rating":"excellent","last_seen_age_s":0}` | GREEN | ≤ 6 s | | Record the battery %. |
| 2 | Set the PSU to LOW (5.45 V). Wait for the valve's next reading. | `[DATA] Battery=NN%` with 11 ≤ NN ≤ 20; `Event: BLE Update type=1`; `rating=warning color=YELLOW effect=SOLID`; `SNAP trigger=event:health`. **No `ALERT:`.** | `event` snapshot: `system_health {"warning","Valve battery low"}`; valve `"battery":NN`, `"rating":"warning"`, `"state":"open"`, `"last_seen_age_s":0`. **No** health event. | YELLOW | reading ≤ 10 min after the previous one; snapshot ≤ 6 s after it; LED ≤ 0.5 s | | Record the time from the PSU change to the reading. |
| 3 | Stay on LOW for 3 min. | `[DATA] Battery=` about every 60 s. `Event: BLE Update type=1` only when the value changes. **No** `SNAP trigger=event:health` while the value stays 11-20. | Only heartbeats (if one falls due), still warning / "Valve battery low". | YELLOW | 60 s ± 5 s between readings | | An in-band change (for example 18 → 17) sends no snapshot. |
| 4 | Set the PSU to CRIT (5.35 V). | `[DATA] Battery=NN%` with NN ≤ 10; `rating=critical color=RED effect=SOLID`; `SNAP trigger=event:health`. Then the valve closes itself: `[DATA] Valve State=0 (CLOSED)`, `Event: BLE Update type=3`. **No `ALERT:`.** | `event` snapshot = the reference critical snapshot (`battery` NN, `state` `"open"` or `"closed"` depending on timing). `valve_state_changed` `"valve_state":"closed"`, `"battery":NN`, then a snapshot with `"state":"closed"`. **No** health event. | RED | reading ≤ 70 s after the PSU change; snapshot ≤ 6 s; LED ≤ 0.5 s | | |
| 5 | Hold CRIT for 2 min. | `[DATA] Battery=` about every 20 s. **No** `SNAP trigger=event:health` and no `Event: BLE Update type=1` while the value is unchanged. | nothing new except heartbeats (critical / "Valve battery critical") | RED | 20 s ± 5 s between readings | | Checks that a steady critical reading is rated but causes no snapshot chatter. |
| 6 | Send the snapshot forcer. | `SNAP trigger=event:sensor_meta` | snapshot still critical / "Valve battery critical", valve `"rating":"critical"`, `"last_seen_age_s":0` | RED | ≤ 6 s | | Confirms that `last_seen_age_s` stays 0 on a steady linked valve (E-16). |
| 7 | Set the PSU to LOW (5.45 V). | `[DATA] Battery=NN%` 11-20; `rating=warning color=YELLOW effect=SOLID`; `SNAP trigger=event:health` | snapshot warning / "Valve battery low", valve `"rating":"warning"`, `"state":"closed"` | YELLOW | reading ≤ 30 s; snapshot ≤ 6 s | | The valve does not reopen by itself. |
| 8 | Set the PSU to GOOD. | `[DATA] Battery=NN%` NN > 20; `rating=excellent color=GREEN effect=SOLID`; `SNAP trigger=event:health` | snapshot excellent / "All devices healthy", valve `"rating":"excellent"`, `"state":"closed"` | GREEN | reading ≤ 70 s; snapshot ≤ 6 s | | |
| 9 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t2-02-open","cmd":"valve_open"}` | `Command: VALVE_OPEN`; `[CMD] Writing Valve=1`; `[DATA] Valve State=1 (OPEN)` | `cmd_ack` ok; `valve_state_changed` open | GREEN | | | |
| 10 | Run `validate_capture.py` on the IoT Hub capture. | | 0 FAIL | | | | |

**Pass:** every row passes. Across the whole run there are exactly four rating-change snapshots (steps 2, 4, 7 and 8), each within 6 s, plus the forcer, the valve-state and the heartbeat snapshots. There is no `ALERT:` line and no `category:"health"` event.

---

### T2-03 — Plan S14 sequence: unknown → GOOD → 10 → 11 → 10 → disconnect → reconnect (unknown) → ≤10 (about 35 min)

**Purpose:** the approved S14 walk, corrected by E-19. It checks the unknown battery at link-up (`null`), each band edge, a critical valve that drops and is held critical through the grace, `device_offline` with cause LINK after the grace, and `device_recovered` at the reconnect. The recovered event carries the **fresh link-up reading** (E-19), not the stale 10. It also checks `last_seen_age_s` and `offline_duration_s` from the drop (E-16).

**Preconditions:** the common start state, on GOOD. Have the snapshot forcer ready to paste.

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | **Unknown.** Power-cycle the valve (10 s off). As soon as `[CONNECT] MAC=<VALVE_MAC>, handle=%u` prints, send the forcer. | `[CONNECT] MAC=...`; `SNAP trigger=event:sensor_meta` **before** the SETUP COMPLETE banner | snapshot valve block during setup: `{"valve_id":"<VALVE_MAC>","state":"unknown","battery":null,"leak_state":false,"rmleak":false,"connected":true,"fw_version":null,"rating":"warning","last_seen_age_s":<seconds since the drop>}`; `system_health {"warning","Valve disconnected"}` | YELLOW | setup takes 10-20 s; the forcer snapshot goes out ≤ 5 s after the send | | Best effort: if the snapshot lands after SETUP COMPLETE, repeat (at most 3 tries). **Observe and record:** `connected:true` next to a non-zero `last_seen_age_s` and "Valve disconnected" is what the code produces until setup completes (the health engine un-stamps the drop at CONNECTED). The schema text says 0 while the link is up; a non-zero value here is a doc nit, not a failure. |
| 2 | **GOOD.** Wait for SETUP COMPLETE. | `[READY] Battery=NN%, ...` NN > 20; `Event: BLE Update type=5`; `Reconnected: no active incident, valve clear`; `SNAP trigger=event:%s`; `rating=excellent color=GREEN effect=SOLID` | snapshot excellent / "All devices healthy", valve `"battery":NN`, `"rating":"excellent"`, `"last_seen_age_s":0` | GREEN | snapshot ≤ 6 s after type=5 | | |
| 3 | **→ 10.** Set CRIT (5.35 V). Wait for the valve's next reading. | `[DATA] Battery=NN%` NN ≤ 10; `rating=critical color=RED effect=SOLID`; `SNAP trigger=event:health`; no `ALERT:` | reference critical snapshot | RED | reading ≤ 10 min; snapshot ≤ 6 s | | The valve closes itself (`valve_state_changed` closed): record it. |
| 4 | **→ 11.** Set LOW (5.45 V). | `[DATA] Battery=NN%` 11-20; `rating=warning color=YELLOW effect=SOLID`; `SNAP trigger=event:health` | warning / "Valve battery low", valve `"rating":"warning"` | YELLOW | reading ≤ 30 s; snapshot ≤ 6 s | | |
| 5 | **→ 10.** Set CRIT (5.35 V). | `[DATA] Battery=NN%` ≤ 10; `rating=critical color=RED effect=SOLID`; `SNAP trigger=event:health` | critical / "Valve battery critical" | RED | reading ≤ 70 s; snapshot ≤ 6 s | | Record NN (this is the "last real reading"). |
| 6 | **Disconnect.** Turn the PSU output OFF. Note the time T0 of `[DISCONNECT] reason=...`. | `[DISCONNECT] reason=0x%02x`; `Event: BLE Update type=6`; `SNAP trigger=event:valve_unlinked`. **No** FLEET_LED line (it stays RED). | `valve_unlinked` snapshot: `system_health {"critical","Valve battery critical"}`; valve `{"valve_id":"<VALVE_MAC>","state":"disconnected","connected":false,"rating":"critical","last_seen_age_s":0}` (0-2) | RED | snapshot ≤ 6 s after T0 | | Held critical in the grace: no dip to warning, so no spurious `device_recovered` (plan F2/N12). |
| 7 | At T0 + 90 s, send the forcer. | `SNAP trigger=event:sensor_meta` | still critical / "Valve battery critical"; the disconnected block with `"last_seen_age_s"` 88-96 | RED | | | E-16: the age counts from the drop. |
| 8 | Wait until T0 + 215 s. | `ALERT: valve <VALVE_MAC> critical -> critical (device_offline)`; `SNAP trigger=event:health` | `device_offline` event, `data` exactly: `{"category":"health","event":"device_offline","source_type":"valve","valve_id":"<VALVE_MAC>","rating":"critical","prev_rating":"critical","battery":NN,"offline_duration_s":DD}` with NN = step 5's reading and 180 ≤ DD ≤ 215. No `rssi` key. Then a snapshot: `system_health {"critical","Valve offline"}`, valve `"rating":"critical"`, `"last_seen_age_s"` 180-215. | RED | the event 180-215 s after T0 | | The cause is now LINK; `prev_rating` equals `rating` because the valve was already critical (from the battery). |
| 9 | **Reconnect (unknown).** Set the PSU to about 5.30 V (still critical; a lower value makes the link-up reading distinct, for example 9). Turn the output ON at least 60 s after the step 8 event. As soon as `[CONNECT] MAC=` prints, send the forcer. | `[SCAN] Target MAC matched - connecting to provisioned valve: <VALVE_MAC>`; `[CONNECT] MAC=...`; `SNAP trigger=event:sensor_meta` before SETUP COMPLETE | valve block during setup: `"state":"unknown","battery":null,"fw_version":null,"connected":true,"rating":"critical"`, `last_seen_age_s` = seconds since T0; `system_health {"critical","Valve offline"}` | RED | | | Best effort, as in step 1. Battery `null` after the reconnect is the "unknown again" point of S14. |
| 10 | Wait for SETUP COMPLETE. | `[DATA] Battery=MM%` (the link-up read; MM ≤ 10); the SETUP COMPLETE banner; `[READY] Battery=MM%, ...`; `Event: BLE Update type=5`; `ALERT: valve <VALVE_MAC> critical -> critical (device_recovered)`; `Reconnected: no active incident, valve clear`; `SNAP trigger=event:%s` | `device_recovered`, `data` exactly: `{"category":"health","event":"device_recovered","source_type":"valve","valve_id":"<VALVE_MAC>","rating":"critical","prev_rating":"critical","battery":MM}` — **MM is the link-up reading (E-19)**, no `offline_duration_s`, no `rssi`. Snapshot: critical / "Valve battery critical", valve `"battery":MM`, `"rating":"critical"`, `"last_seen_age_s":0`. | RED | recovered at CONNECTED (≥ 60 s since the offline); snapshot ≤ 6 s | | The plan's text expected battery 10; E-19 corrected it. Pass if MM equals the last `[DATA] Battery=` value before the ALERT line. |
| 11 | **→ 9.** Hold at 5.30 V for 1 min, then send the forcer. | `[DATA] Battery=` every ~20 s; no `SNAP trigger=event:health` (in-band) | snapshot critical / "Valve battery critical", `"battery"` equal to the latest reading (≤ 10) | RED | | | If MM was already the lower value, this step just confirms it is steady. |
| 12 | Restore GOOD; wait for the reading. | `[DATA] Battery=` > 20; `rating=excellent color=GREEN effect=SOLID` | excellent / "All devices healthy" | GREEN | ≤ 30 s (critical cadence) plus ≤ 70 s | | |

**Pass:** every row passes. The IoT Hub capture holds exactly one `device_offline` and one `device_recovered` for the valve, and no health event at any battery step.

---

### T2-04 — Disconnect while battery-critical: inside the grace, and a quick return after `device_offline` (about 12 min)

**Purpose:** a critical valve that drops for less than 180 s raises no health event and stays "Valve battery critical" (no warning dip, so no false `device_recovered`). A valve that returns less than 60 s after its `device_offline` gets its `device_recovered` late, from the debounce, with `prev_rating` equal to `rating`. It also checks `offline_duration_s` from the drop.

**Preconditions:** the common start state, but the valve is critical: PSU on CRIT, the latest snapshot critical / "Valve battery critical", LED RED.

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| A1 | PSU output OFF (time T0). | `[DISCONNECT] reason=0x%02x`; `Event: BLE Update type=6`; `SNAP trigger=event:valve_unlinked`; **no** `ALERT:` | `valve_unlinked` snapshot: critical / "Valve battery critical"; the disconnected valve block, `rating:"critical"` | RED, no FLEET_LED line | ≤ 6 s | | |
| A2 | At T0 + 100 s, output ON (still CRIT). | `[CONNECT] MAC=...`; `[DATA] Battery=NN%` ≤ 10; SETUP COMPLETE; `Event: BLE Update type=5`; `Reconnected: no active incident, valve clear`; **no** `ALERT:` | `valve_linked` snapshot: critical / "Valve battery critical", `"battery":NN`, `"last_seen_age_s":0`. **No** `device_offline` / `device_recovered`. | RED throughout | | | If the link-up reading is 11 (ADC jitter), a warning / "Valve battery low" snapshot is correct: record it. |
| B1 | PSU output OFF (time T1). Wait for the `device_offline`. | `ALERT: valve <VALVE_MAC> critical -> critical (device_offline)` at T1 + 180-215 s | `device_offline` with `rating:"critical"`, `prev_rating:"critical"`, `battery`: the last reading, `offline_duration_s` 180-215. Snapshot "Valve offline". | RED | 180-215 s | | |
| B2 | 10 s after the `device_offline` event (time T2), output ON. | the reconnect lines; SETUP COMPLETE by about T2 + 20 s; **no** `ALERT:` at CONNECTED (it is debounced); then at the first tick 60-90 s after the `device_offline`: `ALERT: valve <VALVE_MAC> critical -> critical (device_recovered)` | reconnect snapshot: critical / "Valve battery critical". Then `device_recovered` `{"category":"health","event":"device_recovered","source_type":"valve","valve_id":"<VALVE_MAC>","rating":"critical","prev_rating":"critical","battery":NN}` 60-90 s after the `device_offline`, and a `health` snapshot. | RED | recovered 60-90 s after the `device_offline` | | Trailing edge of a debounced alert: sent late, not dropped (2.1.3 dropped it). |
| B3 | Restore GOOD. | `[DATA] Battery=` > 20; GREEN line | excellent | GREEN | ≤ 30 s + ≤ 70 s | | |

**Pass:** no health event in part A; exactly one `device_offline` and one late `device_recovered` in part B, both `critical`/`critical`.

---

### T2-05 — Plan S17: `valve_open` refused at ≤ 10 %, linked and disconnected, and the refusal order (about 15 min)

**Purpose:** `valve_open` and `valve_set_state` open are refused with the exact battery detail while the last real reading is ≤ 10 %, even with the valve disconnected. No command reaches the valve and no snapshot follows the refusal. `valve_close` is still accepted. The open is accepted again once the battery reads above 10 %. Optionally, it checks that the RMLEAK refusal outranks the battery one.

**Preconditions:** the common start state, but the valve is critical and linked, with `rmleak:false` and no "Leak interlock latched" in the reason.

C2D messages:

```json
{"schema":"eflostop.cmd","ver":1,"id":"t2-05-open-1","cmd":"valve_open"}
{"schema":"eflostop.cmd","ver":1,"id":"t2-05-set-1","cmd":"valve_set_state","payload":{"state":"open"}}
{"schema":"eflostop.cmd","ver":1,"id":"t2-05-close-1","cmd":"valve_close"}
{"schema":"eflostop.cmd","ver":1,"id":"t2-05-open-2","cmd":"valve_open"}
{"schema":"eflostop.cmd","ver":1,"id":"t2-05-set-2","cmd":"valve_set_state","payload":{"state":"open"}}
{"schema":"eflostop.cmd","ver":1,"id":"t2-05-open-3","cmd":"valve_open"}
{"schema":"eflostop.cmd","ver":1,"id":"t2-05-open-4","cmd":"valve_open"}
{"schema":"eflostop.cmd","ver":1,"id":"t2-05-open-5","cmd":"valve_open"}
```

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Send `t2-05-open-1`. | `Command: VALVE_OPEN`; `VALVE_OPEN refused — Valve battery critical (≤10 %): the valve will not open. Replace the batteries.`; **none of** `[TASK] CMD: CONNECT`, `[TASK] CMD: OPEN_VALVE`, `[CMD] Writing Valve=1`, `SNAP trigger=event:valve_open` | `cmd_ack` `{"event":"cmd_ack","id":"t2-05-open-1","cmd":"valve_open","status":"error","error":{"code":"valve_open","detail":"Valve battery critical (≤10 %): the valve will not open. Replace the batteries."}}`; no snapshot follows | RED | ack ≤ 2 s | | |
| 2 | Send `t2-05-set-1`. | `Command: VALVE_SET_STATE -> open`; `VALVE_SET_STATE open refused — Valve battery critical (≤10 %): the valve will not open. Replace the batteries.` | `cmd_ack` `"cmd":"valve_set_state","status":"error","error":{"code":"valve_set_state","detail":"Valve battery critical (≤10 %): the valve will not open. Replace the batteries."}` | RED | | | |
| 3 | Send `t2-05-close-1`. | `Command: VALVE_CLOSE`; `[TASK] CMD: CLOSE_VALVE`; `[CMD] Writing Valve=0` | `cmd_ack` `"status":"ok"`; `SNAP trigger=event:valve_close` snapshot | RED | | | A close is never refused for the battery. With the valve already closed, no `valve_state_changed` follows. |
| 4 | PSU output OFF (time T0, the `[DISCONNECT]` line). Within the grace (before T0 + 180 s), send `t2-05-open-2`. | `VALVE_OPEN refused — Valve battery critical (≤10 %): the valve will not open. Replace the batteries.` | `cmd_ack` error with the battery detail | RED | | | The last real reading is kept across the disconnect. |
| 5 | After `device_offline` (T0 + 180-215 s), send `t2-05-set-2`. | `VALVE_SET_STATE open refused — Valve battery critical (≤10 %): the valve will not open. Replace the batteries.` | `cmd_ack` error with the battery detail, although the reason now reads "Valve offline" | RED | | | User decision: refused even when the valve is disconnected. |
| 6 | Set GOOD, output ON. **Before** SETUP COMPLETE, send `t2-05-open-3`; after `[READY] Battery=NN%` (NN > 20), send `t2-05-open-4`. | `open-3`: the battery refusal (the reconnect gap still holds the old ≤ 10 reading). `open-4`: `Command: VALVE_OPEN`, `[CMD] Writing Valve=1`, `[DATA] Valve State=1 (OPEN)` | `open-3`: error, battery detail. At CONNECTED: `device_recovered` `"rating":"excellent","prev_rating":"critical"` (sent at once if ≥ 60 s after the `device_offline`, else 60-90 s after it). `open-4`: `cmd_ack` ok, then `valve_state_changed` open. | RED ("Valve offline" past the grace) → GREEN at CONNECTED | | | If `open-3` arrives after the setup battery read, `ok` is also correct: record which. |
| 7 (optional, refusal order) | Return to CRIT and wait for the critical reading. Wet one BLE sensor. | `AUTO-CLOSE + RMLEAK triggered by %s sensor %s` (format) | `leak_detected`, `auto_close`; snapshot with `"Leak detected: <label>"` | RED | | | The valve is critical **and** locked. |
| 8 (optional) | Send `t2-05-open-5`. | `VALVE_OPEN refused — Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.` | `cmd_ack` error with the RMLEAK detail (checked before the battery) | RED | | | Refusal order: no valve → RMLEAK/incident → battery → queue. |
| 9 (optional) | Dry the sensor; wait for `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK` (10-12 s after the dry report). Send `{"schema":"eflostop.cmd","ver":1,"id":"t2-05-open-6","cmd":"valve_open"}`. | the battery refusal again | `rmleak_auto_cleared`; the ack error with the battery detail | RED | auto-clear 10-12 s after the dry report | | Restore GOOD afterwards. |

**Pass:** rows 1-6 pass; rows 7-9 if run. No `[CMD] Writing Valve=1` appears while the battery is critical.

---

### T2-06 — Plan S15: an unknown valve battery is `null`, never 0 (about 5 min, plus part B if hardware allows)

**Purpose:** BUG-1's trap. A valve battery with no real reading is published as `null` (linked but not ready) or left out (disconnected), is never rated, and never blocks `valve_open`. F-10 is observed here too: `rmleak` and `leak_state` read `false` while `state` is `"unknown"`.

**Part A — during GATT setup (every bench).** Preconditions: the common start state, on GOOD.

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| A1 | PSU output OFF for 30 s. | `[DISCONNECT] ...`; `rating=warning color=YELLOW effect=SOLID`; `SNAP trigger=event:%s` (`valve_unlinked` or `health`) | disconnected valve block, **no `battery` key**; `system_health {"warning","Valve disconnected"}` | YELLOW | | | |
| A2 | Output ON; at `[CONNECT] MAC=` send the snapshot forcer. | forcer snapshot before the SETUP COMPLETE banner | `"state":"unknown","battery":null,"leak_state":false,"rmleak":false,"connected":true,"fw_version":null`. **`battery` must not be `0`.** | YELLOW | | | Best effort; up to 3 tries. |
| A3 | Wait for SETUP COMPLETE. | `[READY] Battery=NN%, ...` | the reconnect snapshot with `"battery":NN` (a number) and `"state":"open"` or `"closed"` | GREEN | ≤ 6 s | | |

**Part B — missing battery characteristic (only with a valve build without the Battery Service or the 0x2A19 characteristic).** Valve FW 2.2.0 always has it, so on a production valve record **N/A**, with a pointer to the Phase E battery review (the S15 path was checked in code: `h_batt_char=0`, battery stays 0xFF, rated by link only).

| # | Action | Expected UART | Expected IoT Hub | LED | P/F | Notes |
|---|---|---|---|---|---|---|
| B1 | Link the modified valve (provision it, or power-cycle it). | `[DISC] Battery svc not found` or `[DISC] Battery char not found`; `[READY] Battery=unknown, Leak=%s, Valve=%s, RMLEAK=%s` (format) | every snapshot has `"battery":null` while connected; `valve.rating:"excellent"` (link only); "All devices healthy" | GREEN | | |
| B2 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t2-06-b2","cmd":"valve_open"}`. | no battery refusal | `cmd_ack` ok | GREEN | | An unknown battery does not block the open. |

**Pass:** A1-A3 pass (B1-B2 or N/A). `"battery":0` must never appear for the valve anywhere in the capture.

---

### T2-07 — Plan S16: flapping 10 / 11 (about 10 min)

**Purpose:** a battery hovering at the critical threshold. Expect at most one rating-change snapshot per flip, a final state that matches the final reading, and no health events.

**Preconditions:** the common start state, with the valve already critical (PSU on CRIT, last snapshot critical).

**Procedure.** Make the reading alternate 10 ↔ 11:

- Right after each `[DATA] Battery=` line at ≤ 10, set LOW (5.45 V). The next reading comes within about 20 s.
- Right after each reading at 11-20, set CRIT (5.35 V). The next reading comes within about 60 s.

Do 6 full cycles (12 flips, about 8 min). Alternatively, if the ADC alternates by itself at a voltage between the two, hold that voltage and count the flips. Log each flip time from the `[DATA] Battery=` lines.

| # | Check | Expected | Tolerance | P/F | Notes |
|---|---|---|---|---|---|
| 1 | For every flip, UART | one FLEET_LED line (`rating=warning color=YELLOW effect=SOLID` or `rating=critical color=RED effect=SOLID`), and one `SNAP trigger=event:health` (or a coalesced label) | the snapshot ≤ 6 s after the `[DATA] Battery=` line | | |
| 2 | Count | the number of `event` snapshots caused by the battery (excluding heartbeats and forcers) is ≤ the number of flips | | | With flips at least 20 s apart and the 5 s spacing, expect exactly one per flip. |
| 3 | Each flip snapshot | `system_health` / `valve.rating` / `valve.battery` match that reading: ≤ 10 → critical / "Valve battery critical"; 11-20 → warning / "Valve battery low" | | | |
| 4 | Health events | **none**: no `ALERT:` line, no `category:"health"` event | | | |
| 5 | Stop on a reading (either band), wait 30 s, then send the snapshot forcer | the forcer snapshot matches that final reading and band; the LED matches (RED or YELLOW) | | | |
| 6 | Restore GOOD | excellent, GREEN | | | |

**Pass:** checks 1-5 pass on all 12 flips.

---

### T2-08 — E-16: valve link age while linked, and `offline_duration_s` from the drop (GOOD battery, about 20 min)

**Purpose:** user decision E-16. `valve.last_seen_age_s` is 0 for as long as the valve is linked; 2.1.3 let it grow (547 s, then 847 s in the field log). After a drop it counts from the drop, and `device_offline.offline_duration_s` is measured from the drop. With a GOOD battery this also shows the normal valve offline path (`prev_rating:"warning"` from the grace).

**Preconditions:** the common start state, on GOOD, with the valve linked and the valve's values unchanged for 10 min or more.

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Watch for 12 min (at least 2 heartbeats), and send the forcer once. | `SNAP trigger=heartbeat` at the heartbeat interval | every snapshot: `valve.last_seen_age_s:0`, `"connected":true` | GREEN | heartbeat 300 s ± 5 s (default) | | |
| 2 | PSU output OFF (time T0). | `[DISCONNECT] ...`; `Event: BLE Update type=6`; `rating=warning color=YELLOW effect=SOLID`; `SNAP trigger=event:%s` | snapshot: `system_health {"warning","Valve disconnected"}`, the disconnected block with `"rating":"warning","last_seen_age_s":0` (0-2) | YELLOW | ≤ 6 s | | |
| 3 | At T0 + 120 s, send the forcer. | | `"last_seen_age_s"` 118-126 | YELLOW | | | |
| 4 | Wait for the grace expiry. | `ALERT: valve <VALVE_MAC> warning -> critical (device_offline)`; `rating=critical color=RED effect=SOLID` | `{"category":"health","event":"device_offline","source_type":"valve","valve_id":"<VALVE_MAC>","rating":"critical","prev_rating":"warning","battery":NN,"offline_duration_s":DD}` with NN = the last reading and 180 ≤ DD ≤ 215; then a snapshot `{"critical","Valve offline"}` | RED | 180-215 s after T0 | | 2.1.3 measured DD from the last changed value: hours. |
| 5 | At least 60 s after the step 4 event, output ON. | SETUP COMPLETE; `ALERT: valve <VALVE_MAC> critical -> excellent (device_recovered)`; `rating=excellent color=GREEN effect=SOLID` | `device_recovered` `{"category":"health","event":"device_recovered","source_type":"valve","valve_id":"<VALVE_MAC>","rating":"excellent","prev_rating":"critical","battery":MM}` (MM = the link-up reading; no `offline_duration_s`); snapshot excellent with `"last_seen_age_s":0` | GREEN | at CONNECTED | | |

**Pass:** all rows pass; `last_seen_age_s` is never above 0 on a linked, set-up valve.

---

### T2-09 — Sensor battery bands never reach critical (optional; needs a low sensor battery)

**Purpose:** the valve bands are valve-only. A sensor's low battery rates `warning` at ≤ 20 % and `good` at 21-35 %, and never `critical`. An excellent ↔ good change sends no immediate snapshot (E-15).

**Preconditions:** the common start state, plus a BLE (or LoRa) sensor whose battery can be made to report ≤ 20 % (a depleted cell, or the sensor on a second adjustable supply). If none is available, record **N/A**; the bands were checked in code (`compute_sensor_rating`: `HEALTH_BATTERY_WARN_PCT` 20, `HEALTH_BATTERY_GOOD_PCT` 35).

| # | Action | Expected UART | Expected IoT Hub | LED | P/F | Notes |
|---|---|---|---|---|---|---|
| 1 | Bring the sensor to 21-35 %. | `eleak %s — leak=%d batt=%d%% rssi=%d fw=%s` (format; `BLE_LEAK`, printed when the scanner reports) | **no** immediate `health` snapshot; the next heartbeat shows the sensor `"rating":"good"` and `system_health {"good","1 sensor battery low"}` | GREEN | | E-15: an excellent ↔ good change waits for the heartbeat. |
| 2 | Bring it to ≤ 20 %. | `rating=warning color=YELLOW effect=SOLID`; `SNAP trigger=event:health` | the sensor `"rating":"warning"`; `system_health {"warning","1 sensor battery low"}`; no health event | YELLOW | | |
| 3 | Bring it to the lowest level the sensor still advertises (for example 0-5 %). | no `rating=critical` line | the sensor stays `"rating":"warning"`; never `critical` | YELLOW | | |
| 4 | With the valve on GOOD, send `{"schema":"eflostop.cmd","ver":1,"id":"t2-09-s4","cmd":"valve_open"}`. | no refusal | `cmd_ack` ok | YELLOW | | A sensor battery never blocks the valve. |

**Pass:** the sensor never rates critical.

---

### 2.x Coverage of this section

| Test | Covers |
|---|---|
| T2-01 | BUG-1, S17 (subset), user: valve bands and the exact open detail, council F3 "valve battery-critical wire contract not bench-run" |
| T2-02 | BUG-1 (every band edge, the read cadence), CHANGELOG "every valve battery edge publishes an event snapshot", no battery health event (Phase B wire change), E-16 (linked age 0), council F3 wire contract |
| T2-03 | S14, E-19, E-16, BUG-1 unknown → `null`, the "device_offline reachability only / device_recovered critical / prev_rating = rating" wire changes, plan F2/N12 (held critical through the grace), council F3 wire contract |
| T2-04 | plan F2/N12 (no false recovered inside the grace), the debounced trailing-edge recovery (Health reliability fix), E-16 `offline_duration_s` |
| T2-05 | S17, user: refused even disconnected and at ≤ 10 %, the refusal order (RMLEAK before battery), `valve_close` still allowed |
| T2-06 | S15, BUG-1 (unknown never 0), F-10 (observe `rmleak`/`leak_state` false while `state` is `"unknown"`) |
| T2-07 | S16 |
| T2-08 | E-16, user: valve `last_seen_age_s` 0 while linked and counting from the drop |
| T2-09 | user: sensor bands unchanged, never critical; E-15 |

Not covered here, deliberately: `override_enable` on a battery-critical valve still starts the window while the valve refuses to open (deferred in the plan). It is not tested in this plan (see M.8, untraced items).

## 3. P0 safety and valve lifecycle

This section verifies that the hub only ever links to and commands its **provisioned** valve (P0-a/P0-c), that a
sensors-only hub protects and reports (P0-b), that a valve change or removal carries nothing over to the next
valve (S22, E-04, E-09, E-11), that a latched leak can never be undone by an `OPEN` (E-06), that the hub's own
RMLEAK clear is never read as a valve-button press across a relink (council F-01), and that the busy GATT pool no
longer forces relinks (council B1). Plan scenarios S18, S19, S22, S23. Firmware under test: `d9fa9c8`
(`gateway.fw` = `"2.1.4"`).

### 3.0 Conventions for this section

**Placeholders.** Replace these in every JSON and log line before you send or search.

| Placeholder | Meaning |
|---|---|
| `<GW>` | Hub device id in IoT Hub (`GW-` + 12 hex), also `gateway.id` |
| `<VALVE_A>` | MAC of the hub's own valve (eFloStop II, FW 2.2.0), uppercase `XX:XX:XX:XX:XX:XX`, exactly as UART prints it in `[CONNECT] MAC=` |
| `<VALVE_B>` | MAC of a second eFloStop II valve. It is the "neighbour" in T3-01 and the replacement valve in T3-05/T3-06 |
| `<S1>`, `<S2>` | MACs of two provisioned BLE leak sensors (uppercase, as in `eleak` lines) |
| `<L1>` | Provisioned LoRa sensor id `0xXXXXXXXX` (optional) |
| `<L2>` | A spare LoRa sensor with the **same network key**, never provisioned on this hub (T3-08 only) |

**Bench techniques used below.**
- **Shield the valve** ("take it out of range, do not power it off"): put the valve inside a closed metal tin
  (cake or cookie tin, lid on, PSU leads through a small gap) 1-2 m from the hub. Check it works: the hub prints
  `BLE_VALVE: [DISCONNECT] reason=0x%02x` (format) within about 10 s; it prints `reason=0x208` for a supervision timeout (a hub-initiated drop prints `reason=0x216`). **Unshield** =
  open the tin. A shielded valve keeps its own state (position, persisted RMLEAK).
- **Power off the valve**: switch its bench PSU output off. Only where the step says so.
- **Wet a BLE sensor**: bridge its probe with a wet cloth or stand it in a tray with a few mm of tap water. It
  normally reports the edge within a few seconds, then bursts about every 15 s while wet. **Dry** = lift it out and wipe the probe; it
  normally reports the edge within a few seconds, then about every 100 s. Pass windows are those of 0.11: the wet edge
  within 20 s, the dry edge within 110 s; time everything after a dry edge from the hub's `leak=0` line. The scanner only forwards changes (plus a 5 min heartbeat), so
  a steady wet sensor produces one `eleak ... leak=1` line, not one per burst.
- **Wet the valve's flood probe**: lay the probe on a wet cloth.
- **Power-cycle the hub**: remove its supply for 5 s. **EN reset** = press the EN button (software reset).
- **Valve long-press**: hold the valve button for the valve FW 2.2.0 long-press time (valve clears RMLEAK and opens).

**Sending C2D.** VS Code Azure IoT Hub extension, right-click the device `<GW>` > *Send C2D Message to Device*,
paste the JSON as the body. Watch D2C with *Start Monitoring Built-in Event Endpoint*. Every JSON below uses the
canonical envelope and a unique `id`; reuse of an `id` is harmless (no de-duplication) but makes the capture harder
to read. The `cmd_ack` for every command below is an event of this shape (the hub always acks an envelope):

```json
{"schema":"eflostop.v2","ts":<epoch>,"gateway":{"id":"<GW>","short_id":"<XXXX>","fw":"2.1.4","uptime_s":<n>},"type":"event","data":{"event":"cmd_ack","id":"<id you sent>","cmd":"<cmd>","status":"ok"}}
```

and on a refusal `"status":"error","error":{"code":"<cmd>","detail":"<exact text>"}`. `gateway.name` is present
only if a hub name is set. Tests below give only `data` for events unless the whole message matters.

**Reading the UART.** Lines look like `I (205285) BLE_VALVE: [SCAN] ...`; the number is milliseconds since boot, use
it for every timing check. Lines are quoted verbatim from `main/` at `d9fa9c8`, tag first. **(format)** marks a
line with printf fields, shown as in the source (`%s`, `%d`, `%u`, `%ld`, `%#x`). Several lines contain an em dash
`—` and others a spaced hyphen ` - `: search on a distinctive substring (for example `clear owed by the hub`)
rather than retyping the dash. The TELEMETRY_V2 line `Pub event: {...}` / `Pub snapshot: {...}` prints every
message that goes out live, so UART alone can confirm most D2C content; a replayed offline event does **not** print
`Pub event:`, use the IoT Hub capture for those.

**Fleet LED.** Every colour change prints `FLEET_LED: rating=%s color=%s effect=SOLID` (format). Mapping
(`main/rgb/fleet_led.c`): no devices provisioned -> WHITE (`rating=unprovisioned`); rating critical -> RED;
warning -> YELLOW (a latched leak interlock after every source is dry is a warning floor, "Leak interlock
latched"; a valve disconnected less than 180 s is warning, "Valve disconnected"); otherwise, while a provisioned
device has not been heard yet -> WHITE (`rating=syncing`); excellent/good -> GREEN; first ~3 s of boot -> OFF
(`rating=startup`).

**Timing constants (from the code at `d9fa9c8`).**

| Constant | Value | Where |
|---|---|---|
| RMLEAK auto-clear after every source is dry | 10 s; lands 10-12 s after the last dry report (2 s loop poll while pending) | `AUTO_CLEAR_TIMEOUT_MS`, rules_engine.c |
| RMLEAK grace after a SET / relink before Check 2 looks | 5 s | `RMLEAK_GRACE_PERIOD_MS` |
| Auto-close cooldown (repeat close / reconnect event) | 10 s | `AUTO_CLOSE_COOLDOWN_MS` |
| Override window | 86400 s | `OVERRIDE_WINDOW_DURATION_S` |
| `override_enable` bounded reconnect | 10 s | `OVERRIDE_CONNECT_TIMEOUT_MS` |
| Valve disconnect grace (warning -> critical "Valve offline") | 180 s | `HEALTH_VALVE_DISC_TIMEOUT_MS` |
| Commission sync window after a provision / boot sync window | 150 s / 180 s | health_engine.h |
| Valve connect timeout / discovery timeout / post-connect delay | 30 s / 30 s / 1 s | app_ble_valve.c |
| Valve write retries (non-busy) | 3 attempts, 200 ms then 400 ms apart | `CMD_WRITE_ATTEMPTS` |
| GATT busy (rc=6) wait | 250 ms steps, at most 5 s, then a replay token; never a relink | `CMD_BUSY_RETRY_MS`, `CMD_BUSY_MAX_MS` |
| Forced relinks in a row | 3 | `CMD_MAX_FORCED_RELINKS` |
| BLE scanner whitelist reload / D2C heartbeat / scan-alive log | 10 s / 5 min / 60 s | app_ble_leak.c |
| Valve setup after GAP CONNECT (bench) | typically 10-40 s to `SETUP COMPLETE` | CP1 bench log |

**Lines that fail any T3 test** unless that test explicitly expects them:
- `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window` (expected only in T3-15).
- `RULES_ENGINE: Reconnected: hub incident + valve open + RMLEAK clear — inferring physical override, starting 24h window` (expected only as the known limitation in T4-12, which T3-20 is run as).
- `RULES_ENGINE: Reconnected: valve RMLEAK active, hub incident clear — re-latching incident` (expected only in T3-06 part C and T3-14).
- Any `BLE_VALVE: [CMD] %s write attempt %d/%d failed (rc=%d) - retrying in %d ms` (format) with `rc=6`, and any
  `BLE_VALVE: [CMD] valve write failed %d times (rc=%d) - reconnecting to re-apply (%s=%u)` (format) with `rc=6`.
- A panic, `Guru Meditation`, `stack overflow`, or an unexpected reboot (the boot banner reappearing).

**GATT-busy lines that are acceptable** (B1 fix; each should appear a few times at most, never in a tight loop):
- `BLE_VALVE: [CMD] %s write rc=%d (value awaits the valve's own report)` (format) with `rc=6`, **once**, directly followed by
- `BLE_VALVE: [CMD] %s write: GATT busy - waiting` (format), then later the same `write rc=0` line
- `BLE_VALVE: [CMD] %s read-back: GATT busy - waiting` (format)
- `BLE_VALVE: [CMD] %s=%u held behind the pending RMLEAK command` (format)
- `BLE_VALVE: [CMD] %s=%u kept pending - GATT still busy, replaying it` (format)
- `BLE_VALVE: [CMD] Pending valve command=0 kept behind the RMLEAK command (GATT busy)`
- `BLE_VALVE: [CMD] Pending %s write rc=%d (value awaits the valve's own report)` (format) with `rc=6`, followed by
  `BLE_VALVE: [CMD] Pending valve commands not applied - replay queued ahead of newer commands` and `[TASK] CMD: REPLAY_PENDING`
- `BLE_VALVE: [CMD] %s read-back rc=%d - %s unconfirmed` (format) as `valve read-back rc=6 - position unconfirmed`
  at setup completion when both pending slots were replayed (pre-existing; the position then comes from the
  valve's own `[DATA] Valve State=` notify).

**Common start state "baseline"** (used by most tests): hub on 2.1.4, Wi-Fi up, clock synced, MQTT connected;
provisioned with `<VALVE_A>`, `<S1>`, `<S2>` (and `<L1>` if you have one); `data.rules` =
`{"auto_close_enabled":true,"trigger_mask":7}`; valve A linked, OPEN, RMLEAK clear, battery above 20 % (PSU about
6.0 V); every sensor dry; no leak latched; `override_active:false`; LED GREEN. To reach it from any state: dry
everything, send `leak_reset`, then `valve_open`, and wait for a `heartbeat` or `event` snapshot showing it.

```json
{"schema":"eflostop.cmd","ver":1,"id":"t3-base-prov","cmd":"provision","payload":{"valve_id":"<VALVE_A>","ble_leak_sensors":["<S1>","<S2>"],"auto_close_enabled":true}}
```

(add `"lora_sensors":["<L1>"]` if used). Send this provision only when the hub is not already in that state: a
provision that changes the sensor list restarts their sync window.

---

### T3-01 - Sensors-only hub with an unprovisioned valve nearby: a leak never touches it (S18)

**Purpose.** P0-a: with no valve provisioned the hub must not scan for, connect to, pair with or command any
valve, even one it bonded with earlier. User decision: a leak on a hub with no valve publishes `leak_detected`
but no `auto_close`. Every valve command is refused with "No valve is set up for this hub."

**Preconditions.** Baseline. Valve A powered, OPEN, within 2 m of the hub (after step 1 it is the "unprovisioned
neighbour"; it is the strongest case because the NimBLE bond store still holds its bond, which 2.1.4 keeps).
**Valve B (never provisioned on this hub) is mandatory:** powered, OPEN and advertising within 2 m of the hub for
the whole test (Phase G: "a sensors-only hub with a second, unprovisioned valve powered nearby"). Record both MACs.
UART and IoT Hub capture running.

| # | Action | Expected |
|---|---|---|
| 1 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-01-decom-v","cmd":"decommission","payload":{"target":"valve"}}` | Ack `ok`. UART (IOTHUB/PROVISIONING/RULES lines in this order; the BLE_VALVE task lines can interleave with them): `IOTHUB: !!! DECOMMISSION_VALVE !!!`; `PROVISIONING: === REMOVING VALVE ===`; `PROVISIONING: Valve removed successfully`; `BLE_VALVE: [API] Target MAC cleared`; `BLE_VALVE: [CMD] No valve commands to flush (valve decommissioned)` (or the `Flushed queued/pending valve commands (valve decommissioned): ...` line); `BLE_VALVE: [TASK] CMD: DISCONNECT`; the GAP DISCONNECT banner and `BLE_VALVE: [DISCONNECT] Link was not the provisioned valve - hub not notified`; `IOTHUB: Valve detectors reset for no valve`; `RULES_ENGINE: Valve replaced: old valve leak source not tracked, 0 source(s) still wet, incident not latched`. An `event` snapshot with `"valve":{}` follows within about 5 s. |
| 2 | Wait 2 min. Keep valve A powered and advertising. | None of the "must not" lines below. Snapshot/heartbeat keeps `"valve":{}`. LED GREEN. |
| 3 | Wet `<S1>`. | UART (rules lines come before the IOTHUB event line, same pass): `BLE_LEAK: eleak %s — leak=%d batt=%d%% rssi=%d fw=%s` (format; `<S1>`, `leak=1`); `RULES_ENGINE: LEAK INCIDENT latched by %s sensor %s` (format) = `LEAK INCIDENT latched by ble_leak_sensor sensor <S1>`; `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <S1>` (format; still printed, it is the decision point); `RULES_ENGINE: AUTO-CLOSE: no provisioned valve - auto_close event not published`; `RULES_ENGINE: AUTO-CLOSE: no provisioned valve - nothing to close`; `IOTHUB: Event: BLE Leak %s leak=%d batt=%d` (format); `TELEMETRY_V2: Pub event: {...leak_detected...}`; `IOTHUB: SNAP trigger=event:leak_detected`; `FLEET_LED: rating=critical color=RED effect=SOLID`. IoT Hub: `leak_detected` (below), then the snapshot (below). **No `auto_close` event.** |
| 4 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-01-open","cmd":"valve_open"}` | Ack `error`, `detail` "No valve is set up for this hub.". UART: `IOTHUB: C2D cmd='valve_open' ver=1 id='t3-01-open'`; `IOTHUB: Command: VALVE_OPEN`; `IOTHUB: VALVE_OPEN refused — No valve is set up for this hub.` No `[TASK] CMD:` line. |
| 5 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-01-close","cmd":"valve_close"}` | Ack `error`, same detail. UART `IOTHUB: VALVE_CLOSE refused — No valve is set up for this hub.` |
| 6 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-01-set","cmd":"valve_set_state","payload":{"state":"closed"}}` | Ack `error`, same detail. UART `IOTHUB: VALVE_SET_STATE closed refused — No valve is set up for this hub.` |
| 7 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-01-ovr","cmd":"override_enable"}` | Ack `error`, same detail. UART `RULES_ENGINE: override_enable: no valve provisioned`. |
| 8 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-01-decom-v2","cmd":"decommission","payload":{"target":"valve"}}` | Ack `error`, `detail` "valve decommission failed". UART `PROVISIONING: No valve provisioned - nothing to remove`. |
| 9 | Dry `<S1>`. | `eleak <S1> — leak=0 ...`; `RULES_ENGINE: All sensors clear — auto-clear timer started (%ds)` (format, `10s`); IoT `leak_cleared`; LED `rating=warning color=YELLOW effect=SOLID`; the snapshot after `leak_cleared` has `system_health` `{"rating":"warning","reason":"Leak interlock latched"}`. 10-12 s after the dry `eleak` line: `RULES_ENGINE: AUTO-CLEAR: all sensors clear for %ds — clearing RMLEAK` (format, `10s`); `BLE_VALVE: [CMD] RMLEAK CLEAR refused - no provisioned valve`; `RULES_ENGINE: AUTO-CLEAR: RMLEAK clear not sent - no provisioned valve`; `RULES_ENGINE: No valve identity available — event emitted without valve_id` (W; expected on a valve-less hub, printed as `rmleak_auto_cleared` is built); IoT `{"event":"rmleak_auto_cleared","clear_after_seconds":10}` with **no** `valve_id` (unchanged 2.1.3 behaviour on a valve-less hub: record, not a failure); LED `rating=excellent color=GREEN effect=SOLID`. |
| 10 | Physically check valve A and valve B. | Both still OPEN; neither ever clicked or moved during the whole test, including through the step 3 leak. |

**Must not appear anywhere in T3-01 after step 1** (UART): `[SCAN] Starting scan for provisioned valve`,
`[SCAN] Target MAC matched - connecting to provisioned valve`, `GAP CONNECT EVENT`, `[CONNECT] MAC=` (in
particular no `[CONNECT] MAC=<VALVE_A>` and no `[CONNECT] MAC=<VALVE_B>`),
`[PASSKEY]` (any), `ENCRYPTION CHANGE EVENT`, `[TASK] CMD: SET_RMLEAK`, `[TASK] CMD: CLOSE_VALVE`,
`[TASK] CMD: OPEN_VALVE`, `[CMD] Writing`. (Plan S18 names the 2.1.3 line `[SCAN] Connecting`; its 2.1.4
equivalent is `[SCAN] Target MAC matched - connecting to provisioned valve`.) IoT Hub: no `auto_close`, no
`valve_state_changed`, no event carrying `<VALVE_A>` or `<VALVE_B>` after the step 1 ack.

**Expected IoT Hub JSON, step 3.** `leak_detected` (`location` from sensor_meta, else `{"code":"unknown","label":""}`):

```json
{"schema":"eflostop.v2","ts":<epoch>,"gateway":{"id":"<GW>","short_id":"<XXXX>","fw":"2.1.4","uptime_s":<n>},"type":"event","data":{"event":"leak_detected","source_type":"ble_leak_sensor","sensor_id":"<S1>","leak_state":true,"battery":<n>,"location":{"code":"<code>","label":"<label>"},"rssi":<n>}}
```

Full snapshot (key case), `SNAP trigger=event:leak_detected`:

```json
{"schema":"eflostop.v2","ts":<epoch>,"gateway":{"id":"<GW>","short_id":"<XXXX>","fw":"2.1.4","uptime_s":<n>},"type":"snapshot","data":{
  "reason":"event",
  "system_health":{"rating":"critical","reason":"Leak detected: <S1 label or MAC>, Leak interlock latched"},
  "valve":{},
  "lora_sensors":[],
  "ble_leak_sensors":[
    {"sensor_id":"<S1>","connected":true,"rating":"critical","last_seen_age_s":<0-5>,"battery":<n>,"rssi":<n>,"leak_state":true,"fw_version":"<fw>","location":{"code":"<code>","label":"<label>"}},
    {"sensor_id":"<S2>","connected":true,"rating":"excellent","last_seen_age_s":<n>,"battery":<n>,"rssi":<n>,"leak_state":false,"fw_version":"<fw>","location":{"code":"<code>","label":"<label>"}}],
  "rules":{"auto_close_enabled":true,"trigger_mask":7},
  "override_active":false}}
```

(`rating` of S2 may be `good` if its RSSI is weak; `lora_sensors` lists `<L1>` if provisioned.)

**Timing.** `leak_detected` within about 2 s of the wet `eleak` line; auto-clear 10-12 s after the dry one.
**LED.** GREEN -> RED (step 3) -> YELLOW (step 9) -> GREEN about 10-12 s later.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-02 - Reboot a sensors-only hub: BLE starts and the sensors are scanned and reported (S19)

**Purpose.** P0-b: 2.1.3 never started BLE on a hub with sensors and no valve, so its BLE sensors were never
heard. 2.1.4 starts BLE when a valve **or** a BLE sensor is provisioned.

**Preconditions.** End state of T3-01: provisioned `<S1>`, `<S2>` (optionally `<L1>`), no valve, all dry, no latch.

| # | Action | Expected |
|---|---|---|
| 1 | Power-cycle the hub. Capture UART from the boot banner to +5 min. | Boot, in this order: `PROVISIONING: Loaded existing config from NVS`; `PROVISIONING: BLE leak sensors: %d` (format, `2`); `IOTHUB: Hub is PROVISIONED`; `IOTHUB: Starting BLE (valve=%s, BLE sensors=%u)` (format) = `Starting BLE (valve=none, BLE sensors=2)`; `IOTHUB: QueueSet Initialized. Event loop starting...`. Both of the last two come **before** `APP_WIFI: Connected! IP: %s`. Then `BLE_VALVE: [INIT] Signal received. Starting BLE stack...`, `BLE_VALVE: [HOST] NimBLE host task started`, the `NIMBLE STACK SYNCED` banner, `BLE_LEAK: NimBLE ready, initializing scanner`, `BLE_LEAK: Extended passive scan started (1M + Coded PHY)`, `BLE_LEAK: Whitelist reloaded: %d sensor(s)` (format, `2`). |
| 2 | Wait up to 200 s. | For **each** provisioned sensor one `BLE_LEAK: eleak %s — leak=%d batt=%d%% rssi=%d fw=%s` (format, `leak=0`) and one `IOTHUB: Event: BLE Leak %s leak=%d batt=%d` (format). `BLE_LEAK: [HEARTBEAT] Scanner alive, whitelist=2 sensors` every 60 s. LED: `rating=startup color=OFF` -> `rating=syncing color=WHITE` -> `rating=excellent color=GREEN` once both are heard. IoT Hub: `lifecycle`, then one `boot` snapshot with both sensors `"connected":true`, `"valve":{}`, `system_health` `{"rating":"excellent","reason":"All devices healthy"}`. |
| 3 | Wet `<S1>`, wait 20 s, dry it, wait for the hub's `leak=0` line (≤ 110 s, 0.11), then 15 s. | As T3-01 steps 3 and 9 (including `RULES_ENGINE: No valve identity available — event emitted without valve_id` at the auto-clear): `leak_detected`, no `auto_close`, then `leak_cleared` and `rmleak_auto_cleared` 10-12 s after the dry `leak=0` line. |

**Must not:** `[INIT] nimble_port_init failed`, `[SCAN] Starting scan for provisioned valve`, `GAP CONNECT EVENT`,
`Snapshot not built - out of memory`, a reboot. (A `[INIT] nimble_port_init failed (rc=%d), attempt %d/%d -
retrying in %d s` retry that then succeeds is a finding to record, not a fail.) Record the MONITOR heap line
(`free`, `min_ever`, `largest_blk`) at +5 min for the sensors-only heap figure (S26 reports it separately).

**Timing.** All provisioned sensors heard within 200 s (dry burst about 100 s; boot sync window 180 s).
**2.1.3 reference.** On 2.1.3 this hub prints no `Starting BLE`, no `eleak` lines, and the snapshot never shows the
sensors connected.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-03 - Leak right after a valve decommission: no close, no `auto_close`, nothing sent to the removed valve (E-11)

**Purpose.** E-11: after a valve decommission the old link stays up until its GAP DISCONNECT (about 0.5 s, up to
the supervision timeout if the valve does not answer). A leak in that window used to publish `auto_close` with
`rmleak_asserted:true` for a valve that no longer existed. 2.1.4: no provisioned valve means no `auto_close` at
all, and no command reaches the removed valve (P0-c).

**Preconditions.** Baseline. Wet cloth ready next to `<S1>`.

| # | Action | Expected |
|---|---|---|
| 1 | Switch valve A's PSU off, and within 1 s send `{"schema":"eflostop.cmd","ver":1,"id":"t3-03-decom-v","cmd":"decommission","payload":{"target":"valve"}}`. (A dead peer makes the teardown wait for the supervision timeout, which widens the window.) | Ack `ok`; the decommission lines of T3-01 step 1. |
| 2 | Immediately wet `<S1>`. Keep it wet 30 s. | `LEAK INCIDENT latched by ble_leak_sensor sensor <S1>`; `AUTO-CLOSE: no provisioned valve - auto_close event not published`; `AUTO-CLOSE: no provisioned valve - nothing to close`; IoT `leak_detected` only. Record whether `IOTHUB: Event: BLE Leak <S1> leak=1 ...` came before or after `BLE_VALVE: [DISCONNECT] reason=0x%02x` (the E-11 window was hit if before). The `RULES_ENGINE: Valve replaced: ...` line reads `0 source(s) still wet, incident not latched` if the leak came after the device-set change, or `1 source(s) still wet, incident kept` if before; both pass. |
| 3 | Dry `<S1>`; wait 15 s. | Auto-clear as T3-01 step 9 (`rmleak_auto_cleared` without `valve_id`). |
| 4 | Switch valve A's PSU on; watch 2 min. | Valve A is never scanned for or linked (T3-01 "must not" list). |
| 5 | Restore: re-provision A with the baseline provision JSON; wait for `SETUP COMPLETE`. | A links; `Reconnected: no active incident, valve clear`. |

**Pass.** No `auto_close` event anywhere in the capture between the step 1 ack and step 5; no `[TASK] CMD: SET_RMLEAK`,
`[TASK] CMD: CLOSE_VALVE`, `[CMD] Writing` before step 5. **LED.** RED while wet, YELLOW 10-12 s, GREEN.
If the window was never hit in 3 attempts, record "E-11 window not reproduced"; the test still passes on the
no-`auto_close` criterion.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-04 - Valve commands refused with no valve are never replayed on the next valve (P0-c)

**Purpose.** P0-c: 2.1.3 accepted valve commands with no valve and replayed them on whichever valve linked next.

**Preconditions.** A sensors-only hub (end of T3-01 or T3-03 step 3). Valve A powered, OPEN, in range. No latch.

| # | Action | Expected |
|---|---|---|
| 1 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-04-close","cmd":"valve_close"}` then `{"schema":"eflostop.cmd","ver":1,"id":"t3-04-open","cmd":"valve_open"}` | Both ack `error` "No valve is set up for this hub." |
| 2 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-04-prov","cmd":"provision","payload":{"valve_id":"<VALVE_A>"}}` | Ack `ok`. UART: `IOTHUB: Applying provisioned valve MAC: <VALVE_A>`; `BLE_VALVE: [API] Target MAC set to: <VALVE_A>`; `BLE_VALVE: [CMD] No valve commands to flush (valve target set)`; `IOTHUB: Starting BLE (valve=<VALVE_A>, BLE sensors=2)`; `IOTHUB: Not connected, triggering connection to: <VALVE_A>`; `BLE_VALVE: [TASK] CMD: CONNECT`; `BLE_VALVE: [SCAN] Starting scan for provisioned valve <VALVE_A>...`; `BLE_VALVE: [SCAN] Target MAC matched - connecting to provisioned valve: <VALVE_A>`; `BLE_VALVE: [CONNECT] MAC=<VALVE_A>, handle=%u` (format); ... `SETUP COMPLETE - READY FOR GATT`; `BLE_VALVE: [READY] Battery=%s, Leak=%s, Valve=%s, RMLEAK=%s` (format, `Valve=OPEN, RMLEAK=CLEAR`); the `VALVE RECONNECT RECONCILIATION` box; `RULES_ENGINE: Reconnected: no active incident, valve clear`. |
| 3 | Watch 60 s after `SETUP COMPLETE`. | **None of:** `[CMD] Applying pending`, `[TASK] CMD: REPLAY_PENDING`, `[CMD] Replaying pending`, `[TASK] CMD: CLOSE_VALVE`, `[TASK] CMD: OPEN_VALVE`, `[CMD] Writing`. Valve A stays OPEN (no `[DATA] Valve State=0 (CLOSED)`). |

**IoT Hub.** Ack `ok`; twin reported with `valve_id` `<VALVE_A>`; a `commission` snapshot; after the link, valve
`{"valve_id":"<VALVE_A>","state":"open","battery":<n>,"leak_state":false,"rmleak":false,"connected":true,"fw_version":"<valve fw>","rating":"excellent","last_seen_age_s":0}`.
A snapshot taken during setup may show `"state":"unknown","battery":null` (expected, documented F-10).
**Timing.** `SETUP COMPLETE` within 60 s of the ack. **LED.** GREEN -> WHITE (syncing, valve not yet heard) -> GREEN.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-05 - Re-provision a different valve MAC while linked (S22)

**Purpose.** S22 / P0-a / P0-c: a `provision` naming a different valve drops the link to the old one, flushes
every command queued or pending for it (and logs the counts), and nothing issued for the old valve reaches the new
one. Also the CP2 "valve churn" check.

**Preconditions.** Baseline (A linked, OPEN). Valve B powered, OPEN, within 2 m, able to pair with the fixed
passkey (factory-fresh, or previously paired to this hub).

**Part A - swap A -> B while A is linked and idle.**

| # | Action | Expected |
|---|---|---|
| 1 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-05-prov-b","cmd":"provision","payload":{"valve_id":"<VALVE_B>"}}` | Ack `ok`. UART: `IOTHUB: Provisioning JSON detected`; `PROVISIONING: Valve MAC: <VALVE_B>`; `PROVISIONING: Provisioning completed successfully!`; `IOTHUB: Applying provisioned valve MAC: <VALVE_B>`; `BLE_VALVE: [API] Target MAC set to: <VALVE_B>`; either `BLE_VALVE: [CMD] No valve commands to flush (valve target changed)` or `BLE_VALVE: [CMD] Flushed queued/pending valve commands (%s): queued=%u, in flight=%d, pending valve=%d rmleak=%d` (format, reason `valve target changed`); `BLE_VALVE: [API] Linked to a valve that is no longer the target - disconnecting`; `IOTHUB: Starting BLE (valve=<VALVE_B>, BLE sensors=2)`; `IOTHUB: Connected to wrong MAC, will reconnect to: <VALVE_B>`; `BLE_VALVE: [TASK] CMD: DISCONNECT`; `BLE_VALVE: [TASK] CMD: CONNECT` (may be followed by `[SCAN] Already connected` while A's link is still closing); the GAP DISCONNECT banner with `BLE_VALVE: [DISCONNECT] Link was not the provisioned valve - hub not notified`; `IOTHUB: Valve detectors reset for <VALVE_B>`; `RULES_ENGINE: Valve replaced: old valve leak source not tracked, 0 source(s) still wet, incident not latched`. |
| 2 | Wait for B. | `[SCAN] Starting scan for provisioned valve <VALVE_B>...`; `[SCAN] Target MAC matched - connecting to provisioned valve: <VALVE_B>`; `[CONNECT] MAC=<VALVE_B>, handle=%u`; on a first pairing `[PASSKEY] INPUT required. Responding with the fixed passkey` and `[PASSKEY] Passkey injected successfully`; `LINK ENCRYPTED SUCCESSFULLY`; `SETUP COMPLETE - READY FOR GATT`; `Reconnected: no active incident, valve clear`. `HEALTH_ENGINE: Valve event from %s dropped - the table's valve is %s` (format, A then B) may appear: acceptable (E-08). |

**Part B - commands pending for the old valve are flushed, not replayed (target is B now).**

| # | Action | Expected |
|---|---|---|
| 3 | Shield valve B. Wait for `[DISCONNECT] reason=0x%02x`. | `[SCAN] Starting scan for provisioned valve <VALVE_B>...` |
| 4 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-05-close-b","cmd":"valve_close"}` | Ack `ok` (queued). UART `BLE_VALVE: [TASK] CMD: CLOSE_VALVE`; `BLE_VALVE: [CMD] Valve write not ready. Queuing val=0`. |
| 5 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-05-prov-a","cmd":"provision","payload":{"valve_id":"<VALVE_A>"}}` | Ack `ok`. `BLE_VALVE: [API] Target MAC set to: <VALVE_A>`; `BLE_VALVE: [CMD] Flushed queued/pending valve commands (valve target changed): queued=%u, in flight=%d, pending valve=0 rmleak=-1` (format; **`pending valve=0` is the check**); `RULES_ENGINE: Valve replaced: ...`. |
| 6 | Wait for A to link (A is OPEN). | A links as in Part A step 2 (no passkey if bonded). `[SCAN] Starting scan for provisioned valve <VALVE_A>...` may be absent: the scan already running for B continues and now matches A, so `[SCAN] Target MAC matched - connecting to provisioned valve: <VALVE_A>` is the first line; `IOTHUB: Not connected, triggering connection to: <VALVE_A>` replaces the "wrong MAC" line. After `SETUP COMPLETE`: **no** `[CMD] Applying pending valve command=0`, **no** `[CMD] Replaying pending Valve command=0`, **no** `[CMD] Writing Valve=0`; A stays OPEN. |
| 7 | Unshield B; watch 2 min. | B is never scanned for or linked: no `[SCAN] Target MAC matched - connecting to provisioned valve: <VALVE_B>`, no `[CONNECT] MAC=<VALVE_B>`. B stays in its state (its pended close died with the flush). |

**Part C - valve churn (CP2 check).**

| # | Action | Expected |
|---|---|---|
| 8 | As fast as you can send them (a few seconds apart): decommission valve (`{"schema":"eflostop.cmd","ver":1,"id":"t3-05-c1","cmd":"decommission","payload":{"target":"valve"}}`), provision B (`{"schema":"eflostop.cmd","ver":1,"id":"t3-05-c2","cmd":"provision","payload":{"valve_id":"<VALVE_B>"}}`), decommission valve (`id` `t3-05-c3`), provision A (`id` `t3-05-c4`, `valve_id` `<VALVE_A>`). | All acks `ok`. Every `[SCAN] Target MAC matched - connecting to provisioned valve: X` names the MAC of the most recent preceding `[API] Target MAC set to: X`. A connect already in flight to the previous target may complete and be dropped: `BLE_VALVE: [CONNECT] Peer %s is not the provisioned valve (%s) - disconnecting` (format) then `BLE_VALVE: [DISCONNECT] Rejected link closed` - acceptable, and nothing (`[SECURITY]`, `[PASSKEY]`, `[CMD] Writing`) runs on that link. A decommission during a connect may print `BLE_VALVE: [TASK] Connect in flight cancelled (rc=%d)` (format) - acceptable. Ends with A linked, OPEN. |

**Must not, whole test:** after a `[API] Target MAC set to: X`, any `[PASSKEY]`, `[SECURITY]`, or `[CMD] Writing`
line while the live link is to a valve other than X (read `[CONNECT] MAC=`); any `[CONNECT] MAC=` of a valve that
is not the current target followed by setup (only the "Peer ... not the provisioned valve" drop is allowed).

**IoT Hub.** Each provision: ack `ok`, twin reported with the new `valve_id`, a `commission` snapshot whose
`data.valve.valve_id` is the new MAC. Before the new valve links: `{"valve_id":"<new>","state":"disconnected","connected":false,"rating":"<any>","last_seen_age_s":null}`
and `system_health.reason` "Syncing - waiting for 1 device". No `valve_state_changed` for A or B caused by a stale command.
**Timing.** New valve `SETUP COMPLETE` within 60 s of its provision ack; old link's GAP DISCONNECT within 5 s of the ack.
**LED.** GREEN -> WHITE (new valve syncing) -> GREEN on each swap; YELLOW while the current target is shielded.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-06 - Valve swap with the old valve flooded (E-04, plus the removal and wet-sensor variants)

**Purpose.** E-04: the old valve's MAC-less leak source used to survive a swap, so the new, dry valve was
auto-closed on its first link (`auto_close` `cause:"reconnect"`), and `leak_reset` was refused. Now the old
valve's source is dropped and, with nothing else wet, the latch is released at once (no `rmleak_auto_cleared`, no
RMLEAK write). With a sensor still wet the latch stays and the new valve is closed on its first link.
`d37eb6c`: the same release on a valve **removal**.

**Preconditions.** Baseline. Valve B powered, OPEN, paired with this hub (T3-05 done).

**Part A - flooded A replaced by dry B.**

| # | Action | Expected |
|---|---|---|
| 1 | Wet valve A's flood probe. | `BLE_VALVE: [DATA] Leak=1 (LEAK)`; `RULES_ENGINE: LEAK INCIDENT latched by valve sensor valve`; `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by valve sensor valve`; `[TASK] CMD: SET_RMLEAK`, `[CMD] Writing RMLEAK=1`, `[TASK] CMD: CLOSE_VALVE`, `[CMD] Writing Valve=0`; `[DATA] RMLEAK=1 (ACTIVE)`; `[DATA] Valve State=0 (CLOSED)`. IoT: `leak_detected` `{"event":"leak_detected","source_type":"valve","valve_id":"<VALVE_A>","leak_state":true,"battery":<n>,"location":{"code":"unknown","label":""},"valve_state":"<open|closed, as sampled>","rmleak":<false|true, as sampled>,"fw_version":"<valve fw>"}`; `auto_close` `{"event":"auto_close","source_type":"valve","valve_id":"<VALVE_A>","rmleak_asserted":true}`; `valve_state_changed` closed, `rmleak:true`. LED RED. |
| 2 | Keep A's probe wet. Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-06-prov-b","cmd":"provision","payload":{"valve_id":"<VALVE_B>"}}` | Ack `ok`; swap lines as T3-05 step 1, and the key line `RULES_ENGINE: Valve replaced: old valve leak source dropped, 0 source(s) still wet, incident released`. **No** `rmleak_auto_cleared` event, no `[CMD] Writing RMLEAK=0`. |
| 3 | Wait for B to link. | Reconciliation box shows `║ Hub:   incident=0 leaks=0 override=INACTIVE`; `RULES_ENGINE: Reconnected: no active incident, valve clear`. **Must not:** `Valve reconnected with %d active leak(s) — executing auto-close`, an `auto_close` with `"cause":"reconnect"`, `[CMD] Writing Valve=0` or `[CMD] Writing RMLEAK=1` on B, `inferring physical override`. B stays OPEN. |
| 4 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-06-reset","cmd":"leak_reset"}` | Ack `ok` (it used to be refused with "A leak is still active"). UART `RULES_ENGINE: LEAK_RESET: no active incident`. |
| 5 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-06-open-b","cmd":"valve_open"}` | Ack `ok` (B is not locked). |

Full snapshot after step 3 (key case):

```json
{"schema":"eflostop.v2","ts":<epoch>,"gateway":{"id":"<GW>","short_id":"<XXXX>","fw":"2.1.4","uptime_s":<n>},"type":"snapshot","data":{
  "reason":"event",
  "system_health":{"rating":"excellent","reason":"All devices healthy"},
  "valve":{"valve_id":"<VALVE_B>","state":"open","battery":<n>,"leak_state":false,"rmleak":false,"connected":true,"fw_version":"<valve fw>","rating":"excellent","last_seen_age_s":0},
  "lora_sensors":[],
  "ble_leak_sensors":[
    {"sensor_id":"<S1>","connected":true,"rating":"excellent","last_seen_age_s":<n>,"battery":<n>,"rssi":<n>,"leak_state":false,"fw_version":"<fw>","location":{"code":"<code>","label":"<label>"}},
    {"sensor_id":"<S2>","connected":true,"rating":"excellent","last_seen_age_s":<n>,"battery":<n>,"rssi":<n>,"leak_state":false,"fw_version":"<fw>","location":{"code":"<code>","label":"<label>"}}],
  "rules":{"auto_close_enabled":true,"trigger_mask":7},
  "override_active":false}}
```

**Part B - a sensor still wet: the new valve is closed on its first link.** Before this part: dry A's probe;
with A unlinked (it is not provisioned), long-press A's button so A is OPEN with RMLEAK clear.

| # | Action | Expected |
|---|---|---|
| 6 | (Target is B, OPEN.) Wet `<S1>`. | B auto-closes: `AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <S1>`, `auto_close` with `"sensor_id":"<S1>"`, `"rmleak_asserted":true`. Keep `<S1>` wet. |
| 7 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-06-prov-a","cmd":"provision","payload":{"valve_id":"<VALVE_A>"}}` | `RULES_ENGINE: Valve replaced: old valve leak source not tracked, 1 source(s) still wet, incident kept`. |
| 8 | Wait for A (OPEN) to link. | `RULES_ENGINE: Valve reconnected with %d active leak(s) — executing auto-close` (format, `1`); IoT `{"event":"auto_close","cause":"reconnect","sensor_id":"<S1>","rmleak_asserted":true,"active_leak_count":1}`; `[CMD] Writing RMLEAK=1` before `[CMD] Writing Valve=0`; A reports `[DATA] RMLEAK=1 (ACTIVE)` and `[DATA] Valve State=0 (CLOSED)` within about 3 s of `SETUP COMPLETE`. |
| 9 | Dry `<S1>`; wait 15 s; send `valve_open` (`id` `t3-06-open-a`). | Auto-clear 10-12 s after the dry: `rmleak_auto_cleared` `{"event":"rmleak_auto_cleared","valve_id":"<VALVE_A>","clear_after_seconds":10}`; A stays CLOSED until the `valve_open` (ack `ok`), then opens. |

**Part C - flooded A removed (d37eb6c), then re-added dry.**

| # | Action | Expected |
|---|---|---|
| 10 | Wet A's flood probe (A closes, as step 1). Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-06-decom-a","cmd":"decommission","payload":{"target":"valve"}}` | Ack `ok`; `RULES_ENGINE: Valve replaced: old valve leak source dropped, 0 source(s) still wet, incident released`; no `rmleak_auto_cleared`; snapshot `"valve":{}`, `system_health` excellent "All devices healthy"; LED GREEN. `valve_open` now acks "No valve is set up for this hub.". |
| 11 | Dry A's probe (A itself stays CLOSED with its own RMLEAK=1). Re-provision A with the baseline provision JSON (`id` `t3-06-prov-a2`). | A links CLOSED with RMLEAK ACTIVE; `RULES_ENGINE: Reconnected: valve RMLEAK active, hub incident clear — re-latching incident` (expected here: the hub restarted nothing and owes no clear; it fails closed); then `All sensors clear — auto-clear timer started (10s)` (started by `[READY] Announcing flood probe state (dry) for reconciliation`); 10-12 s later `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`, `[CMD] Writing RMLEAK=0`, `[DATA] RMLEAK=0 (CLEAR)`, IoT `rmleak_auto_cleared` with `valve_id` `<VALVE_A>`. No `RMLEAK cleared externally`, no `water_access_override_enabled`. Send `valve_open` to return to baseline. |

**Timing.** Swap: `Valve replaced` line within 2 s of the provision ack. Part B close within about 3 s of `SETUP
COMPLETE`. Auto-clear 10-12 s after the last dry report. **LED.** Part A: RED -> WHITE (B syncing) -> GREEN. Part
B: RED while `<S1>` wet, YELLOW 10-12 s, GREEN. Part C: RED -> GREEN at the removal; YELLOW about 10-12 s after
re-adding A (re-latched interlock), then GREEN.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-07 - Quick decommission and re-provision of the same valve: it still relinks after its next drop (E-09)

**Purpose.** E-09: a `provision` of valve A that lands before the old link's GAP DISCONNECT (0.5-5 s after a
`decommission` of A) left the hub with no connect request, so after A's next drop the hub never rescanned and A
went "Valve offline" until a leak or a valve command. Now a provision naming a valve always requests the link.

**Preconditions.** Baseline. To hit the window reliably, send the two commands with the Azure CLI from Git Bash
(`az` with the `azure-iot` extension, logged in); otherwise send them from VS Code as fast as possible.

| # | Action | Expected |
|---|---|---|
| 1 | In Git Bash, one line: `az iot device c2d-message send --hub-name <IOTHUB_NAME> --device-id <GW> --data '{"schema":"eflostop.cmd","ver":1,"id":"t3-07-decom","cmd":"decommission","payload":{"target":"valve"}}' ; az iot device c2d-message send --hub-name <IOTHUB_NAME> --device-id <GW> --data '{"schema":"eflostop.cmd","ver":1,"id":"t3-07-prov","cmd":"provision","payload":{"valve_id":"<VALVE_A>"}}'` | Both acks `ok`. `BLE_VALVE: [API] Target MAC cleared`, then `BLE_VALVE: [API] Target MAC set to: <VALVE_A>` with `[CMD] No valve commands to flush (valve target set)` or `[CMD] Flushed queued/pending valve commands (valve target set): queued=%u, ...` (format; `queued=1` means the old DISCONNECT was wiped). |
| 2 | Decide whether the race was hit: `[API] Target MAC set to: <VALVE_A>` printed **before** the old link's `[DISCONNECT] reason=0x%02x` (or no DISCONNECT at all). If not, return to baseline and repeat step 1 (up to 5 tries). Record the number of tries. | When hit: `BLE_VALVE: [TASK] CMD: CONNECT` and `BLE_VALVE: [SCAN] Already connected` appear while the old link is still up. If the DISCONNECT then arrives: `[SCAN] Starting scan for provisioned valve <VALVE_A>...` right after it, and A relinks (`SETUP COMPLETE` within 60 s) with no leak and no valve command sent. If the DISCONNECT was wiped (`queued=1`), the link simply stays up. |
| 3 | Force the next drop: switch A's PSU off for 5 s, then on. | `[DISCONNECT] reason=0x%02x`; `BLE_VALVE: [SCAN] Starting scan for provisioned valve <VALVE_A>...`; A relinks: `[SCAN] Target MAC matched - connecting to provisioned valve: <VALVE_A>` ... `SETUP COMPLETE` within 60 s of power-on. |

**Fail.** After any GAP DISCONNECT of A in this test, no `[SCAN] Starting scan for provisioned valve <VALVE_A>...`
line, and the snapshot turns "Valve offline" (critical) at 180 s with no relink.
**Known limitation to observe (CHANGELOG, 2.1.5):** if decommission and re-provision land in the same `iothub_task`
pass and A does not relink, health can show A connected/excellent with no link. Record any snapshot with
`"connected":true` while UART shows no link.
**IoT Hub.** Two acks `ok`; `event`/`commission` snapshots; `valve.valve_id` `<VALVE_A>` throughout the re-provision.
**LED.** GREEN; YELLOW "Valve disconnected" during the step 3 power-off; GREEN after relink.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-08 - Foreign LoRa sensor: packets are not evaluated by the rules engine (S23, N4)

**Purpose.** N4: packets from LoRa sensors that are not provisioned were evaluated by the rules engine and could
latch a leak and close the valve. Now they are checked against provisioning first (E-18: nor do they drive
post-provision snapshots). **N/A** if no spare LoRa sensor with the same network key exists.

**Preconditions.** Baseline (valve A linked, OPEN). `<L2>` powered, dry, not in `data.lora_sensors`.

| # | Action | Expected |
|---|---|---|
| 1 | Wet `<L2>` (or press its test button if it has one). Watch 5 min. | `APP_LORA: Verified: ID=0x%lX, Batt=%d%%, Leak=0x%X, Sent=%u, Ack=%u` (format, ID = `<L2>`); `IOTHUB: Event: LoRa Packet from 0x%08lX` (format); `IOTHUB: Sensor 0x%08lX not provisioned, skipping` (format). |
| 2 | Dry `<L2>`. | Same three lines for the dry packet. |
| 3 | Positive control, only if `<L1>` is provisioned: wet `<L1>`, wait for its packet, then dry it and wait 15 s after its dry packet. | `LEAK INCIDENT latched by lora sensor <L1>`, `auto_close` with `"source_type":"lora","sensor_id":"<L1>"`, valve A closes; then `rmleak_auto_cleared`. Restore with `valve_open`. |

**Must not (steps 1-2):** `LEAK INCIDENT latched by lora sensor <L2>`, any `AUTO-CLOSE` line, a `leak_detected`
with `<L2>`, `SNAP trigger=event:prov_pkt`, a change of valve A's state. LED stays GREEN. If UART shows
`APP_LORA: Crypto verification FAILED` for `<L2>`, the sensor has a different key: the test is not exercised (N/A).
**Timing.** A LoRa packet can take minutes; wait at least 5 min per step.

| Result | Notes |
|---|---|
| Pass / Fail / N/A | |

---

### T3-09 - `valve_open` is refused while a leak is latched, even with the valve powered off (E-06 decision)

**Purpose.** User decision 2026-09-26 (E-06): `valve_open` and `valve_set_state` open are refused with the RMLEAK
message while a leak incident is latched and no override window is active, even when the valve is disconnected
(its RMLEAK cache then reads clear). 2.1.3 accepted the open, held it, and wrote it at the reconnect.

**Preconditions.** Baseline.

| # | Action | Expected |
|---|---|---|
| 1 | Wet `<S1>` and keep it wet until step 8. | Auto-close as T3-11 step 1: valve A CLOSED, `[DATA] RMLEAK=1 (ACTIVE)`. |
| 2 | Switch valve A's PSU off. Wait for `[DISCONNECT] reason=0x%02x`. | `[SCAN] Starting scan for provisioned valve <VALVE_A>...`. Snapshot valve `"state":"disconnected","connected":false`. |
| 3 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-09-open","cmd":"valve_open"}` | Ack `error`, `detail` exactly "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.". UART `IOTHUB: VALVE_OPEN refused — Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.` No `[TASK] CMD: OPEN_VALVE`. |
| 4 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-09-set-open","cmd":"valve_set_state","payload":{"state":"open"}}` | Ack `error`, same detail. UART `IOTHUB: VALVE_SET_STATE open refused — Valve is locked after a leak (RMLEAK). ...` |
| 5 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-09-close","cmd":"valve_close"}` | Ack `ok` (a close is never refused). `[TASK] CMD: CLOSE_VALVE`; `[CMD] Valve write not ready. Queuing val=0`. |
| 6 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-09-reset","cmd":"leak_reset"}` | Ack `error`, `detail` "A leak is still active. Fix the leak first, or use override to open the valve during a leak.". UART `RULES_ENGINE: LEAK_RESET refused — %u leak source(s) still active (use override to open during a leak)` (format, `1`). |
| 7 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-09-ovr","cmd":"override_enable"}` | After about 10 s: ack `error`, `detail` "The valve isn't responding. Check its power and connection, then try again.". UART `RULES_ENGINE: override_enable: valve not ready — reconnecting (<=%dms)` (format, `10000`); `RULES_ENGINE: override_enable: valve unreachable after reconnect window`. |
| 8 | Dry `<S1>`. | `[CMD] Pending valve CLOSE cancelled (leak resolved)` (the step 5 close); `All leaks resolved — pending auto-close cancelled`; `All sensors clear — auto-clear timer started (10s)`; 10-12 s later `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`, `[TASK] CMD: CLEAR_RMLEAK`, `[CMD] RMLEAK write not ready. Queuing val=0`; IoT `leak_cleared`, then `{"event":"rmleak_auto_cleared","valve_id":"<VALVE_A>","clear_after_seconds":10}`. |
| 9 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-09-open2","cmd":"valve_open"}` | Ack `ok` (the incident is released). `[CMD] Valve write not ready. Queuing val=1`. |
| 10 | Switch valve A's PSU on. | At setup completion: `[CMD] Applying pending RMLEAK command=0` **before** `[CMD] Applying pending valve command=1` (or the `[CMD] Replaying pending ...` pair in the same order); then either `Reconnected: valve RMLEAK active, hub incident clear - RMLEAK clear owed by the hub, not re-latching` or `Reconnected: no active incident, valve clear` (the valve may or may not have kept RMLEAK across its power-off); A opens: `[DATA] Valve State=1 (OPEN)`, IoT `valve_state_changed` `"valve_state":"open","rmleak":false`. None of the section's fail lines; no `water_access_override_enabled`. |

**Timing.** Step 7 ack 10-11 s after the send. Step 8 auto-clear 10-12 s after the dry `eleak`. Step 10 relink
within 60 s of power-on. **LED.** RED (steps 1-7; "Valve disconnected" adds to the reason) -> YELLOW after the
dry -> YELLOW ("Valve disconnected") until A relinks -> GREEN. If A stays off more than 180 s the valve reads
"Valve offline" (RED) and a `device_offline` event is sent; that is expected, not a failure.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-10 - An OPEN pended while the valve is away is overwritten by a leak's close (E-06)

**Purpose.** E-06: an OPEN pended while the valve was out of range was written at the reconnect **before** the close
the leak was owed. Now the unreachable branch pends RMLEAK=1 and CLOSE, which overwrite the pended OPEN, and they
are applied RMLEAK first.

**Preconditions.** Baseline.

| # | Action | Expected |
|---|---|---|
| 1 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-10-close","cmd":"valve_close"}` | Ack `ok`; A closes (`[DATA] Valve State=0 (CLOSED)`); RMLEAK stays CLEAR (a manual close does not lock). |
| 2 | Shield A; wait for `[DISCONNECT] reason=0x%02x`. | Scan restarts. |
| 3 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-10-open","cmd":"valve_open"}` | Ack `ok` (nothing latched). `[TASK] CMD: OPEN_VALVE`; `[CMD] Valve write not ready. Queuing val=1`. |
| 4 | Wet `<S1>`; keep it wet. | `LEAK INCIDENT latched by ble_leak_sensor sensor <S1>`; `AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <S1>`; `RULES_ENGINE: AUTO-CLOSE: valve not connected — scanning; close deferred to reconnect reconciliation`; `[TASK] CMD: SET_RMLEAK`; `[CMD] RMLEAK write not ready. Queuing val=1`; `[TASK] CMD: CLOSE_VALVE`; `[CMD] Valve write not ready. Queuing val=0`; `[TASK] CMD: CONNECT`. IoT `auto_close` `{"event":"auto_close","source_type":"ble_leak_sensor","sensor_id":"<S1>","rmleak_asserted":false}` (plus `location` if set). |
| 5 | Send `valve_open` again (`id` `t3-10-open2`). | Ack `error`, the RMLEAK detail (T3-09 step 3). |
| 6 | Unshield A. | After `SETUP COMPLETE`: `[CMD] Applying pending RMLEAK command=1` (or `[CMD] Replaying pending RMLEAK command=1`) **before** `[CMD] Applying pending valve command=0` (or `[CMD] Replaying pending Valve command=0`). Then `Valve reconnected with 1 active leak(s) — executing auto-close` (IoT `auto_close` `"cause":"reconnect"`) or, if the RMLEAK read-back landed first, `Reconnected with 1 active leak(s) — valve already closed + RMLEAK asserted, nothing to do`. A ends CLOSED with `[DATA] RMLEAK=1 (ACTIVE)`. |
| 7 | Dry `<S1>`, wait 15 s, send `valve_open` (`id` `t3-10-open3`). | Auto-clear, `rmleak_auto_cleared`; ack `ok`; A opens. |

**Must not (step 6):** `[CMD] Applying pending valve command=1`, `[CMD] Replaying pending Valve command=1`,
`[CMD] Writing Valve=1`, `[DATA] Valve State=1 (OPEN)` before step 7, `RMLEAK cleared externally`,
`inferring physical override`, and the section's rc=6 fail lines.
**Timing.** A CLOSED with RMLEAK ACTIVE within about 3 s of `SETUP COMPLETE` (it is already closed; the check is
that it never opens). **LED.** GREEN -> YELLOW (shielded) -> RED (wet) -> RED -> YELLOW -> GREEN.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-11 - F-01(a): the hub's own auto-clear, pended while the valve is out of range, is not read as a button press

**Purpose.** Council F-01 (the blocking issue): a hub RMLEAK clear pended while the link was down used to
re-latch the incident at the relink, and the clear landing was then read as a valve-button press: a false 24 h
`water_access_override_enabled{trigger:"button"}` that blocked auto-close. Pass criteria per HANDOFF section 7 as
corrected in `49defb4` and the final council's F3 note. **Smoke: yes (one run).**

**Preconditions.** Baseline.

| # | Action | Expected |
|---|---|---|
| 1 | Wet `<S1>`. | UART: `LEAK INCIDENT latched by ble_leak_sensor sensor <S1>`; `AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <S1>`; `[TASK] CMD: SET_RMLEAK`; `[CMD] Writing RMLEAK=1`; `[CMD] RMLEAK write rc=0 (value awaits the valve's own report)`; `[TASK] CMD: CLOSE_VALVE`; `[CMD] Writing Valve=0`; `[DATA] RMLEAK=1 (ACTIVE)`; `[DATA] Valve State=0 (CLOSED)`. IoT: `leak_detected`; `{"event":"auto_close","source_type":"ble_leak_sensor","sensor_id":"<S1>","rmleak_asserted":true}` (+ `location` if set); `{"event":"valve_state_changed","source_type":"valve","valve_id":"<VALVE_A>","valve_state":"closed","battery":<n>,"leak_state":false,"rmleak":true,"fw_version":"<valve fw>"}`. LED RED. |
| 2 | Wait 10 s. Shield A (do **not** power it off). Wait for `[DISCONNECT] reason=0x%02x`. Start a stopwatch: finish step 4 within 150 s so A stays inside its 180 s grace. | `[SCAN] Starting scan for provisioned valve <VALVE_A>...` |
| 3 | Dry `<S1>`. | `eleak <S1> — leak=0 ...`; `All leaks resolved — pending auto-close cancelled`; `All sensors clear — auto-clear timer started (10s)`; IoT `leak_cleared`. 10-12 s later: `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`; `[TASK] CMD: CLEAR_RMLEAK`; `[CMD] RMLEAK write not ready. Queuing val=0`; IoT `{"event":"rmleak_auto_cleared","valve_id":"<VALVE_A>","clear_after_seconds":10}`. LED YELLOW. |
| 4 | Unshield A. | `[SCAN] Target MAC matched - connecting to provisioned valve: <VALVE_A>`; `[CONNECT] MAC=<VALVE_A>, handle=%u`; setup reads `[DATA] RMLEAK=1 (ACTIVE)`; `SETUP COMPLETE - READY FOR GATT`; `[READY] Battery=%s, Leak=OK, Valve=CLOSED, RMLEAK=ACTIVE`. Then **`[CMD] Applying pending RMLEAK command=0`** (with `[CMD] Pending RMLEAK write rc=0 (value awaits the valve's own report)` and later `[DATA] RMLEAK=0 (CLEAR)`), and, before or after it (timing), **either** `RULES_ENGINE: Reconnected: valve RMLEAK active, hub incident clear - RMLEAK clear owed by the hub, not re-latching` (followed by `[TASK] CMD: CLEAR_RMLEAK` and `[CMD] Writing RMLEAK=0`, an idempotent re-send) **or** `RULES_ENGINE: Reconnected: no active incident, valve clear`. |
| 5 | Watch 60 s. Then send `{"schema":"eflostop.cmd","ver":1,"id":"t3-11-open","cmd":"valve_open"}` | No line of the fail list below. The first snapshot after `[DATA] RMLEAK=0 (CLEAR)` is the one below. `valve_open` acks `ok`; A opens (`[DATA] Valve State=1 (OPEN)`, `valve_state_changed` open). |

**Fail if any of:** `Reconnected: valve RMLEAK active, hub incident clear — re-latching incident`;
`RMLEAK cleared externally (valve override) — starting 24h override window`; `OVERRIDE WINDOW STARTED`;
IoT `water_access_override_enabled` or `auto_close` within 60 s of the relink; any snapshot after the RMLEAK=0
read-back with "Leak interlock latched" or `"override_active":true`. **Do not require**
`RMLEAK clear read back - the hub's own clear, not a valve override`: it prints only when an incident is latched
again before the hub's clear is read back. A snapshot at the link-up instant may still show `"rmleak":true`
(before the read-back); that is expected.

Full snapshot after the RMLEAK=0 read-back (key case):

```json
{"schema":"eflostop.v2","ts":<epoch>,"gateway":{"id":"<GW>","short_id":"<XXXX>","fw":"2.1.4","uptime_s":<n>},"type":"snapshot","data":{
  "reason":"event",
  "system_health":{"rating":"excellent","reason":"All devices healthy"},
  "valve":{"valve_id":"<VALVE_A>","state":"closed","battery":<n>,"leak_state":false,"rmleak":false,"connected":true,"fw_version":"<valve fw>","rating":"excellent","last_seen_age_s":0},
  "lora_sensors":[],
  "ble_leak_sensors":[
    {"sensor_id":"<S1>","connected":true,"rating":"excellent","last_seen_age_s":<n>,"battery":<n>,"rssi":<n>,"leak_state":false,"fw_version":"<fw>","location":{"code":"<code>","label":"<label>"}},
    {"sensor_id":"<S2>","connected":true,"rating":"excellent","last_seen_age_s":<n>,"battery":<n>,"rssi":<n>,"leak_state":false,"fw_version":"<fw>","location":{"code":"<code>","label":"<label>"}}],
  "rules":{"auto_close_enabled":true,"trigger_mask":7},
  "override_active":false}}
```

**Timing.** Auto-clear 10-12 s after the dry report; relink 10-40 s after unshielding; RMLEAK=0 read back within
about 3 s of `SETUP COMPLETE`. **LED.** GREEN -> RED -> YELLOW (dry, and valve disconnected) -> GREEN after the
relink. **Repeat** 3 times in the full run.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-12 - F-01(b): `leak_reset` sent while the valve is out of range

**Purpose.** Council F-01 route (c): the same false override through a `leak_reset` whose RMLEAK clear is pended.
Also F-05: `leak_reset` now queues its clear under the rules lock.

**Preconditions.** Baseline. Have the `leak_reset` message ready in the extension before step 3.

| # | Action | Expected |
|---|---|---|
| 1-2 | As T3-11 steps 1-2. | As T3-11. |
| 3 | Dry `<S1>`. As soon as `eleak <S1> — leak=0` prints (you have 10 s), send `{"schema":"eflostop.cmd","ver":1,"id":"t3-12-reset","cmd":"leak_reset"}` | Ack `ok`. UART: `IOTHUB: Command: LEAK_RESET`; `RULES_ENGINE: LEAK_RESET: clearing incident (hub_latch=%d, valve_rmleak=%d, override=%d)` (format, `hub_latch=1, valve_rmleak=0, override=0`: the RMLEAK cache is cleared while unlinked); `IOTHUB: Leak incident cleared, RMLEAK reset`; `[TASK] CMD: CLEAR_RMLEAK`; `[CMD] RMLEAK write not ready. Queuing val=0`. IoT `{"event":"rmleak_cleared","valve_id":"<VALVE_A>"}`. No `AUTO-CLEAR` line afterwards. If `AUTO-CLEAR` printed before the `leak_reset` arrived, this run is a T3-11 run: repeat. |
| 4-5 | As T3-11 steps 4-5. | As T3-11 steps 4-5, same fail list and snapshot. |

**Timing / LED.** As T3-11. **Repeat** 3 times in the full run.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-13 - F-01(c): hub power-cycled with the incident latched and every sensor dry

**Purpose.** Council F-01 route (b) with F-02: after a restart the latch is restored from NVS, and a dry report can
start the 10 s auto-clear while the valve is still in GATT setup. Expect exactly one auto-clear and no override.

**Preconditions.** Baseline. Valve A stays powered and in range throughout.

| # | Action | Expected |
|---|---|---|
| 1 | Wet `<S1>`; wait for `[DATA] RMLEAK=1 (ACTIVE)` and `[DATA] Valve State=0 (CLOSED)`. | Auto-close as T3-11 step 1. |
| 2 | Unplug the hub. Dry `<S1>`. Wait 10 s. Plug the hub in. Capture UART from boot to +120 s. | `RULES_ENGINE: NVS: restored incident latch — pending reconcile with valve`. The first dry input (a sensor's first post-boot report, or A's `[READY] Announcing flood probe state (dry) for reconciliation`) prints `All sensors clear — auto-clear timer started (10s)`; 10-12 s later `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`. **Outcome A** (`[READY]` before `AUTO-CLEAR`): `Reconnected: hub + valve RMLEAK in sync`, then at the auto-clear `[CMD] Writing RMLEAK=0` and `[DATA] RMLEAK=0 (CLEAR)`. **Outcome B** (`AUTO-CLEAR` before `[READY]`): `[CMD] RMLEAK write not ready. Queuing val=0`, then at setup `[CMD] Applying pending RMLEAK command=0` and the owed line `Reconnected: valve RMLEAK active, hub incident clear - RMLEAK clear owed by the hub, not re-latching` (or `Reconnected: no active incident, valve clear`). |
| 3 | Watch to +120 s. | Exactly **one** `AUTO-CLEAR` line. A stays CLOSED and ends `[DATA] RMLEAK=0 (CLEAR)`. IoT: one `rmleak_auto_cleared` (if raised before the clock synced it arrives after the `lifecycle`, with a corrected `ts`; UART then shows `TELEMETRY_V2: Time not synced (ts=%ld) - holding %s for replay; stamped when the clock syncs` (format) and `OFFLINE_BUF: Stamped pre-sync event [%s] at clock sync: ts=%lld (%lld s ago)` (format)). |
| 4 | Send `valve_open` (`id` `t3-13-open`). | Ack `ok`, A opens. |

**Fail if any of:** `RMLEAK cleared externally (valve override)`; `OVERRIDE WINDOW STARTED`;
`re-latching incident`; IoT `water_access_override_enabled` within 60 s of `[READY]`; a second `AUTO-CLEAR`.
**Repeat** 5 times; record the gap between the timer start and `[READY]` each time, and whether outcome A or B
occurred. At least one run must be outcome B: if it never happens, shield A before plugging the hub in and
unshield it 20 s after boot.
**LED.** OFF -> YELLOW (restored latch floor, `rating=warning`) or WHITE while syncing -> GREEN after the auto-clear.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-14 - F-01(d): a clear owed at a hub restart is lost with RAM; the reconnect fails closed and the auto-clear releases it

**Purpose.** Final council F5 residual: the "clear owed" flag is RAM only. After a restart the reconnect re-latches
(as 2.1.3, fail closed) and the 10 s auto-clear must then release it with no override window.

**Preconditions.** Baseline.

| # | Action | Expected |
|---|---|---|
| 1-3 | T3-11 steps 1-3 (latch, shield A, dry `<S1>`, wait for `AUTO-CLEAR` and `[CMD] RMLEAK write not ready. Queuing val=0`). | As T3-11. |
| 4 | Keep A shielded. Power-cycle the hub. Wait 30 s after boot. | No valve link. `NVS: restored incident latch` does **not** print (the latch was already released and saved). |
| 5 | Unshield A. | At setup A reads RMLEAK=1: `RULES_ENGINE: Reconnected: valve RMLEAK active, hub incident clear — re-latching incident` (expected here); `[READY] Announcing flood probe state (dry) for reconciliation` starts `All sensors clear — auto-clear timer started (10s)`; 10-12 s later `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`, `[CMD] Writing RMLEAK=0`, `[DATA] RMLEAK=0 (CLEAR)`; IoT `rmleak_auto_cleared` with `valve_id` `<VALVE_A>`. A stays CLOSED. |
| 6 | Repeat steps 1-5 with an EN reset instead of the power-cycle in step 4. | Same result. |

**Fail if any of:** `RMLEAK cleared externally (valve override)`, `OVERRIDE WINDOW STARTED`,
`water_access_override_enabled`, the valve opening by itself, or the latch staying set more than 15 s after
`[READY]` (snapshot still "Leak interlock latched").
**LED.** YELLOW (valve disconnected) -> YELLOW (re-latched floor, about 10-12 s) -> GREEN.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-15 - A genuine valve long-press still starts the 24 h window (SRS 4.4.2), including after a hub restart

**Purpose.** The F-01 fix gates Check 2 on a confirmed lock (the valve was seen with RMLEAK=1 during this incident)
and on no hub clear being owed. A real press must still be detected. Final council F3/F4/F5 residual.

**Preconditions.** Baseline.

| # | Action | Expected |
|---|---|---|
| 1 | Wet `<S1>`; keep it wet. Wait until `[DATA] RMLEAK=1 (ACTIVE)` and then at least 10 more seconds (past the 5 s grace). | Auto-close as T3-11 step 1. |
| 2 | Long-press valve A's button. | Valve side: `[DATA] RMLEAK=0 (CLEAR)`, `[DATA] Valve State=1 (OPEN)`. Within 30 s (normally within 2 s): `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window`; `RULES_ENGINE: OVERRIDE WINDOW STARTED: auto-close blocked for 24h (expiry=%ld)` (format). IoT `{"event":"water_access_override_enabled","trigger":"button","expires_ts":<now+86400>,"remaining_s":86400}`, then a snapshot with `"override_active":true,"override_remaining_s":<about 86400>,"expires_ts":<epoch>`. LED stays RED (`<S1>` is wet). |
| 3 | Dry `<S1>`, wait 20 s, wet it again. | IoT `leak_cleared`, `leak_detected`, then `{"event":"auto_close_blocked_override","source_type":"ble_leak_sensor","sensor_id":"<S1>","override_remaining_s":<n>}`; UART `Override active — auto-close BLOCKED for %s sensor %s (remaining=%lds)` (format). A stays OPEN. |
| 4 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t3-15-cancel","cmd":"override_cancel"}` (`<S1>` still wet). | Ack `ok`. `IOTHUB: Command: OVERRIDE_CANCEL`; `RULES_ENGINE: OVERRIDE WINDOW CANCELLED (remaining_s=%ld)` (format); `RULES_ENGINE: Override cancelled with %d active leak(s) — executing auto-close` (format, `1`); `[CMD] Writing RMLEAK=1` then `[CMD] Writing Valve=0`. IoT `{"event":"auto_close_reenabled","previous_remaining_s":<n>,"reason":"c2d_command"}`; `valve_state_changed` closed, `rmleak:true`. |
| 5 | **After a hub restart:** keep `<S1>` wet (A closed, RMLEAK=1). Power-cycle the hub. Wait for `[READY]` and `Reconnected with 1 active leak(s) — valve already closed + RMLEAK asserted, nothing to do` or `Reconnected: hub + valve RMLEAK in sync`. Wait 10 s. Long-press A's button. | Same as step 2 (line, event and snapshot). Then repeat step 4 to cancel. |
| 6 | **Observe only:** repeat step 1, but long-press within about 1 s of `[CMD] Writing Valve=0`. | Record whether the window starts. A missed press (no window, valve re-locked) is the documented fail-closed residual, not a failure. |
| 7 | Dry `<S1>`; wait for the hub's `leak=0` line (≤ 110 s, 0.11), then 15 s; send `{"schema":"eflostop.cmd","ver":1,"id":"t3-15-open","cmd":"valve_open"}`. | Auto-clear, ack `ok`, back to baseline. |

**Timing.** Window start within 30 s of the press (steps 2 and 5). **LED.** RED throughout the wet periods; GREEN at the end.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-16 - B1(a): hub power-cycled with a sensor wet and the valve open - the close is not delayed by a busy GATT pool

**Purpose.** Council B1 (blocking, fixed in `c2da6be`): at setup completion the replayed RMLEAK and CLOSE (a write
plus a read-back each) fill NimBLE's 4 GATT procedures, and the rules reconcile queues a second SET+CLOSE. 2.1.4
before the fix counted `BLE_HS_ENOMEM` (rc=6) as a failure and forced a relink, delaying the leak close 15-40 s per
cycle. Now rc=6 is waited out and never forces a relink.

**Preconditions.** Baseline.

| # | Action | Expected |
|---|---|---|
| 1 | Unplug the hub. Wet `<S1>` and keep it wet. Plug the hub in. Capture boot to +120 s. | `<S1>`'s first post-boot report latches: `LEAK INCIDENT latched by ble_leak_sensor sensor <S1>`, `AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <S1>`. Record which path: **pended** (report before `SETUP COMPLETE`: `AUTO-CLOSE: valve not connected — scanning; close deferred to reconnect reconciliation` or `[CMD] RMLEAK write not ready. Queuing val=1` + `[CMD] Valve write not ready. Queuing val=0`) or **live** (report after `SETUP COMPLETE`: `[CMD] Writing RMLEAK=1` then `[CMD] Writing Valve=0`). |
| 2 | Pended path: at setup completion. | `[CMD] Applying pending RMLEAK command=1` then `[CMD] Applying pending valve command=0`; then `Valve reconnected with 1 active leak(s) — executing auto-close` (its SET+CLOSE may print the acceptable GATT-busy lines); `[DATA] Valve State=0 (CLOSED)`; `[DATA] RMLEAK=1 (ACTIVE)`. |
| 3 | Measure. | `[DATA] Valve State=0 (CLOSED)` timestamp minus the `SETUP COMPLETE - READY FOR GATT` timestamp. |

**Pass.** Closed within about 3 s of `SETUP COMPLETE` (3-5 s: pass, note it; over 5 s: fail). If the valve motor
itself is slow, also record the time of the valve command's `write rc=0` line (must be within 3 s). None of:
`write attempt N/3 failed (rc=6)`, `valve write failed 3 times (rc=6)`, `reconnecting to re-apply`, a second
`GAP CONNECT EVENT` or any `GAP DISCONNECT EVENT` in the 60 s after `SETUP COMPLETE`. Only the acceptable
GATT-busy lines of section 3.0. Events raised before the clock synced arrive after the `lifecycle` with a corrected
`ts` (N1).
**IoT Hub.** `leak_detected`, `auto_close` (from evaluate_leak, `rmleak_asserted` true or false depending on the
path), possibly a second `auto_close` with `"cause":"reconnect"`, `valve_state_changed` closed `rmleak:true`.
**Repeat** 5 times; the pended path must occur at least twice (if `<S1>` is always heard after `SETUP COMPLETE`,
shield A at plug-in and unshield it 20 s later). **LED.** RED.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-17 - B1(b): leak while the valve is out of range - RMLEAK before CLOSE at the relink, no forced relink

**Purpose.** Council B1 case (b) and the RMLEAK-before-CLOSE ordering across a relink (F4 residual, `dcf0e07`,
`d3b9ef8`): both pended commands must be applied RMLEAK first, the valve must close promptly, and a busy pool must
not force a relink. Part B covers the setup-straddle case. **Smoke: yes (Part A, one run).**

**Preconditions.** Baseline.

**Part A.**

| # | Action | Expected |
|---|---|---|
| 1 | Shield A; wait for `[DISCONNECT] reason=0x%02x`. | Scan restarts. |
| 2 | Wet `<S1>`; keep it wet. | `LEAK INCIDENT latched by ble_leak_sensor sensor <S1>`; `AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <S1>`; `AUTO-CLOSE: valve not connected — scanning; close deferred to reconnect reconciliation`; `[TASK] CMD: SET_RMLEAK`; `[CMD] RMLEAK write not ready. Queuing val=1`; `[TASK] CMD: CLOSE_VALVE`; `[CMD] Valve write not ready. Queuing val=0`; `[TASK] CMD: CONNECT`. IoT `leak_detected`, `auto_close` with `"rmleak_asserted":false`. LED RED. |
| 3 | Wait 20 s; unshield A. | `SETUP COMPLETE - READY FOR GATT`; `[READY] ... Valve=OPEN, RMLEAK=CLEAR`; **`[CMD] Applying pending RMLEAK command=1` before `[CMD] Applying pending valve command=0`** (or `[CMD] Replaying pending RMLEAK command=1` before `[CMD] Replaying pending Valve command=0`); `Valve reconnected with 1 active leak(s) — executing auto-close`; IoT `{"event":"auto_close","cause":"reconnect","sensor_id":"<S1>","rmleak_asserted":true,"active_leak_count":1}`; `[DATA] RMLEAK=1 (ACTIVE)`; `[DATA] Valve State=0 (CLOSED)`. |
| 4 | Measure and check. | CLOSED within about 3 s of `SETUP COMPLETE` (3-5 s pass with note; over 5 s fail). No section fail line; no `reconnecting to re-apply`; exactly one `GAP CONNECT EVENT` for this relink and no `GAP DISCONNECT EVENT` in the next 60 s; no `RMLEAK cleared externally`. |
| 5 | Dry `<S1>`, wait 15 s, `valve_open` (`id` `t3-17-open`). | Auto-clear, `rmleak_auto_cleared`, ack `ok`, A opens (baseline). |

Full snapshot after step 3 (key case):

```json
{"schema":"eflostop.v2","ts":<epoch>,"gateway":{"id":"<GW>","short_id":"<XXXX>","fw":"2.1.4","uptime_s":<n>},"type":"snapshot","data":{
  "reason":"event",
  "system_health":{"rating":"critical","reason":"Leak detected: <S1 label or MAC>, Leak interlock latched"},
  "valve":{"valve_id":"<VALVE_A>","state":"closed","battery":<n>,"leak_state":false,"rmleak":true,"connected":true,"fw_version":"<valve fw>","rating":"excellent","last_seen_age_s":0},
  "lora_sensors":[],
  "ble_leak_sensors":[
    {"sensor_id":"<S1>","connected":true,"rating":"critical","last_seen_age_s":<n>,"battery":<n>,"rssi":<n>,"leak_state":true,"fw_version":"<fw>","location":{"code":"<code>","label":"<label>"}},
    {"sensor_id":"<S2>","connected":true,"rating":"excellent","last_seen_age_s":<n>,"battery":<n>,"rssi":<n>,"leak_state":false,"fw_version":"<fw>","location":{"code":"<code>","label":"<label>"}}],
  "rules":{"auto_close_enabled":true,"trigger_mask":7},
  "override_active":false}}
```

**Part B - setup straddle.**

| # | Action | Expected |
|---|---|---|
| 6 | Shield A; wait for the DISCONNECT; unshield A. As soon as `GAP CONNECT EVENT` prints (10-40 s before `SETUP COMPLETE`), wet `<S1>`. | Any of `[CMD] RMLEAK write not ready. Queuing val=1`, `[CMD] Valve write not ready. Queuing val=0`, `[CMD] %s val=%u pended as the link became ready - replaying it` (format), `[CMD] Valve=0 held behind the pending RMLEAK command`. The first RMLEAK=1 write (`Applying pending`, `Replaying pending` or `Writing RMLEAK=1`) has an earlier timestamp than the first valve=0 write. A ends CLOSED with `[DATA] RMLEAK=1 (ACTIVE)`; no `RMLEAK cleared externally`. |
| 7 | Restore as step 5. | Baseline. |

**Repeat.** Part A 10 times and Part B 3 times in the full run (council F4). Record the close delay per run.
**LED.** YELLOW (shielded) -> RED (wet) -> RED -> YELLOW -> GREEN.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-18 - B1(c): re-wet at the moment of the auto-clear (F-08 order, stale-confirm residual)

**Purpose.** Council B1 case (c), F-08 (`44d3d43`: the tick's `rmleak_auto_cleared` is published before the same
pass's leak) and `b245d94` (a new latch always re-locks). Also the final council's stale-confirm residual, observed
on a slow link.

**Main runs (merged): run T5-04** (section 5), which is this procedure (wet, dry, re-wet within 0-2 s of the `AUTO-CLEAR` line, 5 runs) with a per-run table classified by Δ = time(`eleak ... leak=1`) − time(`AUTO-CLEAR`). Its pass criteria are this test's: every Δ ≥ 0 run publishes `rmleak_auto_cleared` before the re-wet's `leak_detected` and `auto_close` (the MQTT-reconnect pass may miss `rmleak_auto_cleared`), the BLE task writes `RMLEAK=0`, then `RMLEAK=1`, then `Valve=0`, and the valve ends CLOSED with `[DATA] RMLEAK=1 (ACTIVE)` and `"override_active":false`. No `RMLEAK cleared externally`, no `water_access_override_enabled`, and none of the fail lines of 3.0. `RMLEAK clear read back - the hub's own clear, not a valve override` may appear.

The observe-only part below stays in this test.

**Observe only (stale confirm, 2.1.5):** place A at the RF edge (partly shielded; `[CMD] Writing RMLEAK=1` to
`[DATA] RMLEAK=1 (ACTIVE)` gap above 1 s) and run the T5-04 procedure ten times. Record the read-back gap and any
`water_access_override_enabled{trigger:"button"}`. A false window here is the documented known limitation (valve
still ends closed with RMLEAK set), not a failure; record it with the timings.
**LED.** RED -> YELLOW (brief) -> RED -> ... -> GREEN.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-19 - Valve identity: a bonded valve reconnects normally (identity address = provisioned MAC)

**Purpose.** P0-a matches the peer by its **identity** address (`desc.peer_id_addr`), so a bonded valve must keep
reconnecting. Plan risk "P0 BLE changes can affect reconnects of a bonded valve".

**Preconditions.** Baseline; A bonded to this hub (it has linked before).

| # | Action | Expected |
|---|---|---|
| 1 | Switch A's PSU off 5 s, then on. | `[DISCONNECT] reason=0x%02x`; `[SCAN] Starting scan for provisioned valve <VALVE_A>...`; `[SCAN] Target MAC matched - connecting to provisioned valve: <VALVE_A>`; `[CONNECT] MAC=<VALVE_A>, handle=%u` with **exactly** the provisioned MAC; `LINK ENCRYPTED SUCCESSFULLY`; `[ENC_CHANGE] Device is bonded (keys stored)`; `SETUP COMPLETE - READY FOR GATT`; `Reconnected: no active incident, valve clear`. |
| 2 | Power-cycle the hub. | Same relink sequence at boot. |
| 3 | Repeat steps 1 and 2 five times each. | Every cycle relinks. |

**Fail.** `[CONNECT] Peer %s is not the provisioned valve (%s) - disconnecting` for valve A; a relink that does not
reach `SETUP COMPLETE` within 60 s; `[DISCONNECT] Rejected link closed` for A. `[PASSKEY]` lines on a bonded
reconnect or `[ENC_CHANGE] Peer no longer holds our bond — deleting stale LTK`: record (a re-pair), fail only if the
link then does not come up. **LED.** YELLOW while A is off, GREEN after each relink.

| Result | Notes |
|---|---|
| Pass / Fail | |

---

### T3-20 - Known limitation F-03 (deferred to 2.1.5): leak latched while the valve was away, then a hub restart - observe and record

**Purpose.** Record the documented F-03 behaviour: the reconnect finds the valve open with RMLEAK clear and a latched incident, and infers a button press made while the hub was offline (SRS 4.4.2, unchanged from 2.1.3), starting a 24 h window that blocks auto-close. **This is not a failure.**

**Run as T4-12 (merged).** T4-12 variant 2 is this procedure (the leak latched while the valve was away, then a hub restart with the sensor still wet; its branch (ii) is the F-03 inference), variant 1 covers the sensor drying while the hub is off, and its row 3 checks the E-06 refusal on the way. One precaution from this test applies to T4-12 variant 2: power off every LoRa sensor that is not the wet source, and take the battery out of every other BLE sensor, so that no dry report restarts the auto-clear after the restart (that path is F-02, T4-11).

**Fail only if:** the valve opens by itself outside the reconnect inference, or `override_cancel` does not close it (T4-12 variant 2, recovery step).

| Result | Notes |
|---|---|
| Observed / Fail (as T4-12) | |

---

### T3-21 - Known limitation F-02 (accepted): restart during a live leak can release RMLEAK for one wet burst - observe and record

**Purpose.** Record the documented, accepted F-02 behaviour: after a restart the list of wet sources is empty, so the first dry report (another sensor, or the valve's dry link-up) can start the 10 s auto-clear before the still-wet sensor is heard again. RMLEAK can then be released for up to one wet burst (about 15 s for BLE). The valve stays closed and re-locks when the wet sensor is heard. **Not a failure** within those bounds.

**Run as T4-11 Part A (merged).** T4-11 Part A is this procedure (a wet sensor across a hub power-cycle, 3-5 runs, optionally with a LoRa sensor), records the branch and the release gap per run, and also tries a `valve_open` in the gap.

**Fail only if:** the valve reports `[DATA] Valve State=1 (OPEN)` without an accepted `valve_open`; an override window starts; or a release is not followed by a re-lock within one wet burst of the sensor (T4-11 Part A tolerance).

| Result | Notes |
|---|---|
| Observed / Fail (as T4-11 Part A) | |

---

### 3.x Section 3 traceability

| Test | Covers |
|---|---|
| T3-01 | S18, P0-a, user decision "no `auto_close` on a hub with no valve" (`502178c`), valve B (never provisioned) powered nearby and never touched; N9 refusals ("No valve is set up for this hub."), `valve decommission failed` on a valve-less hub |
| T3-02 | S19, P0-b, N2/N3 boot order |
| T3-03 | E-11, P0-c, `502178c` |
| T3-04 | P0-c |
| T3-05 | S22, P0-a, P0-c, E-08, CP2 valve churn, council F4 "valve identity and target change while a link or connect is in flight" |
| T3-06 | E-04 (`e2b8fb7`, `f3b65b0`), `d37eb6c` (removal), Priority 1 on a new valve |
| T3-07 | E-09 (`3f87132`), known limitation "decommission + re-provision in one pass" |
| T3-08 | S23, N4, E-18 |
| T3-09 | E-06 user decision (`2487ff5`, `c4b86e6`), override_enable unreachable |
| T3-10 | E-06 (`fefe0da`), RMLEAK before CLOSE |
| T3-11 | F-01 route (a), F-08, council F1/F4/F5 "F-01 false override" |
| T3-12 | F-01 route (c), F-05 |
| T3-13 | F-01 route (b) with F-02, council F5 "power-cycled during an incident"; T4-11 Part B is run as this test |
| T3-14 | council F5 "clear owed lost at restart, fail closed" |
| T3-15 | SRS 4.4.2 button override, council F3/F4/F5 "genuine press not suppressed", "press right after close fails closed"; T5-08 is run as this test, with two added checks |
| T3-16 | B1 case (a), council F1/F2 "B1 timing" |
| T3-17 | B1 case (b), RMLEAK before CLOSE across a relink, setup straddle (`d3b9ef8`, `dcf0e07`) |
| T3-18 | B1 case (c), F-08 (`44d3d43`), `b245d94` re-wet, council "stale confirm" (observe); main runs are T5-04, the stale-confirm observe part is here |
| T3-19 | P0-a identity address, plan risk "bonded valve reconnects" |
| T3-20 | F-03 known limitation (observe); run as T4-12 |
| T3-21 | F-02 known limitation (observe); run as T4-11 Part A |

**Smoke subset from this section (about 20 min):** T3-01 (steps 1-4, 9), T3-11 (one run), T3-17 Part A (one run).

## 4. Protection before Wi-Fi, clock and offline (N1-N4; plan S20, S21)

This section checks that leak protection no longer waits for the network. In 2.1.3 `iothub_task` blocked on the first Wi-Fi IP, then up to 120 s of SNTP, then the DPS retries, before provisioning, the rules and health engines and BLE were started (N1). 2.1.4 starts all of them at boot, runs SNTP and DPS from the event loop, completes the event QueueSet before BLE starts (N2/N3), and evaluates LoRa packets only from provisioned sensors (N4). Events raised before the first clock sync are now held and time-stamped instead of being discarded. An override window started before the clock synced is timed on the hub uptime. NimBLE now runs beside the captive portal, and while no Wi-Fi credentials are saved BLE scanning pauses so a phone can join it (S21, T4-10).

Every expected line and JSON shape below was checked against the firmware at `d9fa9c8`. The main sources are `main/main.c` (start order), `app_iothub.c` (`iothub_task` prologue, `qset_add_member`, `net_maintain`, `dps_maintain`, `cloud_bringup`, the loop's `cloud_pending` poll, `valve_open_reject_reason`), `telemetry_v2.c` (`build_envelope`, `publish_json`, `telemetry_v2_drain_offline`), `offline_buffer.c` (`offline_buffer_stamp_presync`, `ob_prepare_replay_locked`, `drain_locked`), `rules_engine.c` (`start_override_window`, `override_load_from_nvs`, `rules_engine_tick`, `rules_engine_on_valve_connected`, `rules_engine_evaluate_leak`), `dps_client.c`, `reset_button.c`, `monitoring.c` and `fleet_led.c`. Where a document disagrees with the code, the code wins, and the test says so.

### 4.0 Common setup for this section

**Hardware and tools**

- Hub on 2.1.4 (`gateway.fw` is `"2.1.4"`). The UART log from the ESP-IDF serial monitor, saved to one file per test. The IoT Hub monitor (VS Code Azure IoT Hub extension, "Start Monitoring Built-in Event Endpoint"), saved to one file per test. C2D messages are sent with the extension's "Send C2D Message to Device".
- eFloStop II valve on FW 2.2.0, on the bench PSU at the GOOD level (6.0 V), in RF range.
- Four BLE leak sensors, called A, B, C and D below, provisioned, each with a `sensor_meta` label. A is the one you wet. A cup of water, or the method already used on the bench, to wet and dry a probe.
- Optional: LoRa sensor L, provisioned to this hub. Optional for T4-13: LoRa sensor N, **not** provisioned to this hub but using the same LoRa key (for example, one provisioned to another hub), and BLE sensor X, not provisioned to this hub.
- A router whose 2.4 GHz network you can switch off, and on which you can block one host name or one UDP port (for T4-05 and T4-06). A phone for the captive portal (T4-10).

**Placeholders.** `<VALVE>` is the valve MAC as the snapshot prints it (upper case, colons). `<A>` … `<D>` are the BLE sensor MACs. `<A_LABEL>` is A's `sensor_meta` label. `<GW>` is the gateway ID (`GW-…`) and `<SHORT>` its short ID. `NN` is any number.

**Terms used in the steps**

| Term | Meaning |
|---|---|
| Router off | Switch off the site Wi-Fi (power off the AP, or disable its 2.4 GHz radio). The hub keeps its stored credentials. |
| Power-cycle the hub | Unplug the hub's supply for at least 5 s, then plug it back in. This is the only way this section uses to lose the wall clock: a software restart (`esp_restart`, the Wi-Fi reset button, `decommission all`) keeps the RTC time, so an earlier synced clock survives it. |
| EN reset | The hub's EN/RST button, or Ctrl+T then Ctrl+R in the ESP-IDF monitor. Use it only where a test says so. Its lifecycle `reset_reason` is recorded, not judged. |
| Wet A / dry A | Put A's probe in water / take it out and dry it. A BLE sensor bursts about every 15 s while wet and about every 100 s while dry, so the hub hears a wet edge within about 15 s. |
| The capture checks | `python docs/telemetry/validate_capture.py "<IoT Hub capture>"` gives 0 FAIL, and no message in the capture has `ts` below `1704067200`. |

**Reading times.** The ESP-IDF prefix `I (12345) TAG:` is milliseconds since boot. `gateway.uptime_s` in a message is the hub uptime in seconds when the message was built. To read an epoch `ts` as a date, run in PowerShell: `[DateTimeOffset]::FromUnixTimeSeconds(1785398400).UtcDateTime`. Keep a note of the PC wall-clock time (UTC) of every wet, dry, button press and power action: several tests compare `ts` with it.

**Replayed events print nothing on UART per message.** A buffered event goes out through `offline_buffer_drain()`, which logs `Replayed [ob_NN] (NNN bytes)` (`OFFLINE_BUF`), never `Pub event: …`. Judge the content and the `ts` of replayed events from the IoT Hub capture only.

**Log-line notation.** Each expected line is given as `TAG: text`, quoted verbatim from the firmware. **(format)** marks a line with printf fields, shown as in the source (`%s`, `%d`, …). Several lines contain the em dash `—` (U+2014) exactly as the firmware prints it; others use a plain `-`. Search with the text as written.

**Timing constants used for the tolerances**

| Constant | Value | Source | Effect in this section |
|---|---|---|---|
| Loop poll while cloud bring-up, an owed BLE apply, a device-set retry or an RMLEAK auto-clear is pending | 2 s | `app_iothub.c` (`base`) | the first clock sync, the DPS boot backoff and the 10 s auto-clear are acted on within about 2 s |
| Loop idle cap otherwise | 30 s | `app_iothub.c` | the rules tick (override expiry, override re-base) runs at least every 30 s |
| `SNTP_INITIAL_SYNC_MS` | 120 s | `app_iothub.c` | `SNTP initial sync failed …` 120-122 s after `Initializing SNTP...` |
| SNTP retry timer | 60 s | `app_iothub.c` (the `sntp_retry` timer) | one `SNTP still not synced …` every 60 s after the fallback |
| `SNTP_EPOCH_VALID` | 1704067200 | `app_iothub.h` | a clock below this is "unsynced"; no message may leave with a lower `ts` |
| DPS boot backoff | 5, 10, 15, 20, 25 s after attempts 1-5 | `dps_maintain()` (`attempts * 5`, capped 30) | measured from the END of the failed attempt |
| `DPS_BOOT_ATTEMPTS` / `DPS_RETRY_INTERVAL_MS` | 6 / 5 min | `app_iothub.c` | after attempt 6 fails, one try every 5 min (plus up to 30 s idle) |
| `DPS_TIMEOUT_MS` | 60 s | `dps_client.c` | a live registration can block `iothub_task` for up to 60 s (known limitation) |
| `AUTO_CLEAR_TIMEOUT_MS` | 10 s | `rules_engine.c` | `AUTO-CLEAR` 10-12 s after the last source reads dry |
| `RMLEAK_GRACE_PERIOD_MS` | 5 s | `rules_engine.c` | a valve-button press is read only 5 s or more after the hub's last RMLEAK write |
| `OVERRIDE_WINDOW_DURATION_S` | 86400 s | `rules_engine.c` (compile-time override `-D OVERRIDE_WINDOW_DURATION_S=<s>` for a bench-only build) | the 24 h window |
| `OVERRIDE_BLOCKED_COOLDOWN_MS` | 60 s | `rules_engine.c` | at most one `auto_close_blocked_override` per minute |
| `OFFLINE_BUF_MAX_ENTRIES` / `OFFLINE_BUF_MAX_JSON_LEN` | 16 / 512 B | `offline_buffer.h` | the 17th buffered event overwrites the oldest |
| `MONITORING_INTERVAL_MS` / `HEAP_LOW_WATERMARK` | 10 s / 8192 B | `monitoring.h` | one `MONITOR` heap line every 10 s; `LOW HEAP WARNING` below 8192 B free |
| `HOLD_TIME_MS` | 10 s | `reset_button.c` | the Wi-Fi reset button must be held 10 s |
| `BLE_LEAK_HEARTBEAT_MS` | 5 min | `app_ble_leak.c` | an unchanged wet sensor is re-reported to the rules engine only every 5 min |
| `HEALTH_BOOT_SYNC_TIMEOUT_MS` | 180 s | `health_engine.h` | boot sync window |

**Fleet LED (GPIO 48, `fleet_led.c`).** One line per transition, `FLEET_LED: rating=%s color=%s effect=SOLID` **(format)**. The colour is OFF (`rating=startup`) until the health engine is up, WHITE for a hub with no devices (`rating=unprovisioned`) or while provisioned devices are still unheard (`rating=syncing`), RED for `critical` (a leak anywhere, or a device offline), YELLOW for `warning` (including the "Leak interlock latched" floor while the valve is held closed after the water is gone), and GREEN for `excellent` or `good`. The network LED (GPIO 38) is separate: ramp red with no internet, beat blue while Wi-Fi is up and MQTT is not, ramp blue when MQTT is connected (`NET_STATUS: wifi=%d mqtt=%d` **(format)** on each change).

**Start state for every test, unless the test says otherwise**

- The hub is provisioned with the valve and A-D (and L if used), has been up for more than 5 min, and MQTT is connected.
- The last snapshot has `system_health` `{"rating":"excellent","reason":"All devices healthy"}`, `override_active:false`, and `valve` with `"state":"open"`, `"rmleak":false`, `"connected":true`.
- The offline buffer is empty: the last boot printed `OFFLINE_BUF: Init: buffer empty`, or the last connect printed `OFFLINE_BUF: Drain complete: %d event(s) published, 0 remaining`.
- The fleet LED is GREEN.

**Restoring the bench after a test.** If a test leaves the valve closed: dry every sensor, wait for `rmleak_auto_cleared` (or send `leak_reset`, below), then send `valve_open`. If it leaves an override window open, send `override_cancel`.

```json
{"schema":"eflostop.cmd","ver":1,"id":"t4-reset-1","cmd":"leak_reset"}
{"schema":"eflostop.cmd","ver":1,"id":"t4-open-1","cmd":"valve_open"}
{"schema":"eflostop.cmd","ver":1,"id":"t4-ovc-1","cmd":"override_cancel"}
```

Send each one as a separate C2D message. Use a new `id` for every send.

---

### T4-01 — Boot order: protection, QueueSet and BLE before Wi-Fi (about 5 min)

**Purpose:** N1 and N2/N3. Provisioning, the rules and health engines, the offline buffer, the complete event QueueSet and the BLE start all come at boot, before the Wi-Fi IP and without waiting for it. Every QueueSet member is added (no `could not be added`). The first valve target at boot logs as INFO, not as a flush WARN. HANDOFF §7 "Boot order".

**Preconditions:** the common start state, router on.

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Start the UART capture. Power-cycle the hub. | In this order (other lines interleave): `APP_WIFI: AP SSID: %s` **(format)**; `IOTHUB: Starting IOT Hub Task...`; `RULES_ENGINE: Initialized: auto_close=%s triggers=0x%02X` **(format)** (bench default `auto_close=enabled triggers=0x07`); `IOTHUB: Hub is PROVISIONED`; `OFFLINE_BUF: Init: buffer empty`; `BLE_LEAK: Sensor tracking reset`; `BLE_VALVE: [API] Target MAC set to: %s` **(format)** = `<VALVE>`; `BLE_VALVE: [CMD] No valve commands to flush (%s)` **(format)** = `(valve target set)`, at level **I**; `IOTHUB: Starting BLE (valve=%s, BLE sensors=%u)` **(format)** = `valve=<VALVE>, BLE sensors=4`; `IOTHUB: Not connected, triggering connection to: %s` **(format)**; `IOTHUB: QueueSet Initialized. Event loop starting...`. On the NimBLE side: `BLE_VALVE: [INIT] Signal received. Starting BLE stack...`, `BLE_VALVE: [SM] Fixed Passkey: configured (not logged)`, `BLE_VALVE: [HOST] NimBLE host task started` (these may interleave with the `IOTHUB` lines). Only then, or later: `APP_WIFI: Connected! IP: %s` **(format)**, `IOTHUB: Initializing SNTP...`, `IOTHUB: Time synced: %s` **(format)**, `DPS: Loaded cached assignment from NVS`, `IOTHUB: DPS: hub=%s device=%s` **(format)**, `IOTHUB: Connected to Azure IoT Hub!` | — | `FLEET_LED: rating=startup color=OFF effect=SOLID` → `rating=syncing color=WHITE effect=SOLID` | `Starting BLE …` and `QueueSet Initialized …` at ≤ 3000 ms uptime (plan target about 1 s; record both values) | | If `Connected! IP` prints before `Starting BLE`, record it: that is a very fast association, not a wait. The pass test is that `Starting BLE` and `QueueSet Initialized` come within 3 s of boot whatever Wi-Fi does; T4-02 proves the same with no Wi-Fi at all. |
| 2 | Search the whole capture for the failure strings. | **None of:** `QueueSet: %s queue could not be added%s`; `QueueSet: %s queue missing - its events will not be handled`; `QueueSet: creation failed (out of memory) - rebooting`; `[CMD] Flushed queued/pending valve commands (valve target set)` (a **W** line); `Boot: provisioning unavailable - empty-hub state unknown, retrying in the loop`; `Boot: hub is empty - clearing any persisted rules-engine state`. Acceptable: `IOTHUB: QueueSet: %s queue held %d item(s) at boot - %d discarded` **(format)** for `LoRa` with `0 discarded` (a LoRa packet already waiting). | — | — | — | | Any `N discarded` with N > 0 is a FAIL (a packet was lost). |
| 3 | Wait for the valve link and the sensors. | `BLE_VALVE: [SCAN] Target MAC matched - connecting to provisioned valve: %s` **(format)** = `<VALVE>`; the `GAP CONNECT EVENT` banner; the `SETUP COMPLETE - READY FOR GATT` banner; `BLE_VALVE: [READY] Announcing flood probe state (dry) for reconciliation`; `RULES_ENGINE: Reconnected: no active incident, valve clear`. `BLE_LEAK: Extended passive scan started (1M + Coded PHY)`; one `BLE_LEAK: eleak %s — leak=%d batt=%d%% rssi=%d fw=%s` **(format)** with `leak=0` for each of A-D. | `lifecycle` (below), then a `snapshot` with `"reason":"fast"` or `"boot"`, `system_health` excellent / "All devices healthy" (or "Syncing - waiting for N devices" until the last sensor is heard) | `rating=excellent color=GREEN effect=SOLID` once every device is heard | SETUP COMPLETE ≤ 60 s after boot; each sensor ≤ 100 s (its dry cadence) | | Record the uptime of SETUP COMPLETE. |
| 4 | Check the lifecycle message. | — | `lifecycle` as below, `"reset_reason":"power_on"` | — | — | | |

Expected lifecycle (values vary; the keys, their order and `provisioned:true` must match):

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {"id": "<GW>", "short_id": "<SHORT>", "name": "Bench Hub", "fw": "2.1.4", "uptime_s": 9},
  "type": "lifecycle",
  "data": {
    "event": "online",
    "reset_reason": "power_on",
    "provisioned": true,
    "valve_id": "<VALVE>",
    "lora_sensor_count": 0,
    "ble_leak_sensor_count": 4,
    "rules": {"auto_close_enabled": true, "trigger_mask": 7}
  }
}
```

`gateway.name` appears only when a hub name is set. `lora_sensor_count` is 1 if L is provisioned.

**Optional variant (sensors-only hub, about 3 min).** On a hub provisioned with BLE sensors and no valve, step 1 shows `IOTHUB: Starting BLE (valve=none, BLE sensors=%u)` **(format)**, no `[API] Target MAC set to`, no `Not connected, triggering connection to`, and later `BLE_VALVE: [SCAN] No provisioned valve - not scanning for valves`. On an empty hub, step 1 shows `Boot: hub is empty - clearing any persisted rules-engine state` and no `Starting BLE` at all.

**Pass:** rows 1-4 pass.

---

### T4-02 — Plan S20: boot with the router off, then a leak closes the valve with no Wi-Fi (about 10 min)

**Purpose:** N1 end to end. With no Wi-Fi and no clock, the hub links the valve, hears the sensors and closes the valve on a leak. The events raised before the clock sync are held for replay, not discarded. T4-03 continues from the end of this test, without a reset in between.

**Preconditions:** the common start state. Note the PC time (UTC).

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Router off. Power-cycle the hub. | As T4-01 row 1 up to `IOTHUB: QueueSet Initialized. Event loop starting...`, and the NimBLE lines. **No** `Connected! IP`, **no** `Initializing SNTP...`, **no** `Time synced`. `APP_WIFI: WiFi Disconnected. Reason: %d` **(format)** may repeat while the hub retries. | nothing (no network) | `rating=startup color=OFF` → `rating=syncing color=WHITE`. Network LED ramp red. | `Starting BLE …` ≤ 3000 ms uptime | | The Wi-Fi manager may start its own SoftAP after its retries; that is expected and does not stop protection. That fallback SoftAP logs `APP_WIFI: SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`; a `portal priority ON` line here is a Fail (T4-10 D1). |
| 2 | Wait for the valve link and for A-D to be heard. | T4-01 row 3 lines: `SETUP COMPLETE - READY FOR GATT`, `Reconnected: no active incident, valve clear`, one `eleak … leak=0` per sensor. | nothing | `rating=excellent color=GREEN effect=SOLID` | SETUP COMPLETE ≤ 60 s after boot | | Record the uptime of SETUP COMPLETE. |
| 3 | Wet A. Note the PC time. | `BLE_LEAK: eleak %s — leak=%d batt=%d%% rssi=%d fw=%s` **(format)** with `<A>`, `leak=1`; `RULES_ENGINE: LEAK INCIDENT latched by %s sensor %s` **(format)** = `by ble_leak_sensor sensor <A>`; `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s` **(format)**; `IOTHUB: Event: BLE Leak %s leak=%d batt=%d` **(format)**; then **for each event** (leak_detected, then auto_close): `TELEMETRY_V2: Time not synced (ts=%ld) - holding %s for replay; stamped when the clock syncs` **(format)** with `holding event`, followed by `OFFLINE_BUF: Stored event [%s] (%u bytes), %d buffered` **(format)**. On the valve side, in this order: `BLE_VALVE: [CMD] Writing RMLEAK=1`, `[DATA] RMLEAK=1 (ACTIVE)`, `[CMD] Writing Valve=0`, `[DATA] Valve State=0 (CLOSED)`. Then one more held event (valve_state_changed) with its `Time not synced …` and `Stored event …` lines. | nothing | `rating=critical color=RED effect=SOLID` | `eleak … leak=1` ≤ 15 s after wetting; `AUTO-CLOSE + RMLEAK triggered` ≤ 1 s after it; `[DATA] Valve State=0 (CLOSED)` ≤ 5 s after it | | Record: the uptime of `LEAK INCIDENT latched`, each `ts=` in the `Time not synced` lines (about the uptime in seconds), each `[ob_NN]` key. `Writing RMLEAK=1` must come before `Writing Valve=0`. |
| 4 | Dry A. Note the PC time. | `eleak … leak=0` for `<A>`; `RULES_ENGINE: All leaks resolved — pending auto-close cancelled` may print; `RULES_ENGINE: All sensors clear — auto-clear timer started (%ds)` **(format)** = `(10s)`; one held `leak_cleared` (`Time not synced … holding event …`, `Stored event …`). About 10 s later: `RULES_ENGINE: AUTO-CLEAR: all sensors clear for %ds — clearing RMLEAK` **(format)** = `10s`; one held `rmleak_auto_cleared`; `BLE_VALVE: [CMD] Writing RMLEAK=0`; `[DATA] RMLEAK=0 (CLEAR)`. **No** `[CMD] Writing Valve=1`. | nothing | RED → `rating=warning color=YELLOW effect=SOLID` (interlock floor) → `rating=excellent color=GREEN effect=SOLID` at the auto-clear | `AUTO-CLEAR` 10-12 s after the `eleak … leak=0` line | | The valve stays CLOSED. Five events are now held (plus any health events, which are recorded, not failed). |
| 5 | Wait 2 min with the router still off. | Nothing new except `MONITOR: heap: free=%lu min_ever=%lu largest_blk=%lu uptime=%lus` **(format)** every 10 s and the BLE scanner heartbeat. | nothing | GREEN | — | | Record one MONITOR line. Continue with T4-03 now, without resetting the hub. |

**Pass:** every row passes. The valve closed within 5 s of the auto-close line with no Wi-Fi and no clock. Every event printed `holding event for replay`, never `Offline — buffering`.

---

### T4-03 — Pre-sync events: stamped at the clock sync and replayed in order after the connect (about 5 min)

**Purpose:** the N1 pre-sync hold. When the clock first syncs, each event held from before the sync gets its real `ts` from the hub uptime, written back to flash. The events go out after the first connect, before the lifecycle, in their original order, with `ts` close to the real event time. Council risk "pre-sync event stamping". E-22 (a stamped entry is never written back over 512 B, so it stays readable by 2.1.3).

**Preconditions:** the end state of T4-02 (router off, five pre-sync events held, valve closed, RMLEAK clear, all sensors dry).

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Router on. Note the PC time. | `APP_WIFI: Connected! IP: %s` **(format)**; `IOTHUB: Initializing SNTP...`; `IOTHUB: Time synced: %s` **(format)** (asctime of the synced UTC time); then **one per held event**, oldest first: `OFFLINE_BUF: Stamped pre-sync event [%s] at clock sync: ts=%lld (%lld s ago)` **(format)**. Then `DPS: Loaded cached assignment from NVS`, `IOTHUB: DPS: hub=%s device=%s` **(format)**, `IOTHUB: Connected to Azure IoT Hub!`, `TELEMETRY_V2: MQTT connected = true`, `TELEMETRY_V2: Draining %d offline event(s) before lifecycle...` **(format)**, `OFFLINE_BUF: Draining %d buffered event(s)...` **(format)**, one `OFFLINE_BUF: Replayed [%s] (%u bytes)` **(format)** per event, `OFFLINE_BUF: Drain complete: %d event(s) published, %d remaining` **(format)** = `5 event(s) published, 0 remaining` (or the number held), `TELEMETRY_V2: Offline drain complete: %d event(s) replayed` **(format)**. | The replayed events (below), then `lifecycle` (`"reset_reason":"power_on"`), then snapshots. | network LED ramp red → beat blue → ramp blue. Fleet LED GREEN. | `Initializing SNTP...` ≤ 2 s after `Connected! IP`; `Time synced` normally ≤ 10 s after it; the stamping lines in the same second as `Time synced` | | Record every `ts=` and `(N s ago)` from the stamping lines. **Must not appear:** `Stamped pre-sync event [%s]: ts=%lld (%lld s before this replay)`, `Dropped a buffered pre-sync event [%s] - …`, `Clock not synced - pre-sync event [%s] and %d after it kept for the next drain`, `Dropped a buffered event from an earlier power cycle that was never time-stamped [%s]`. |
| 2 | In the IoT Hub capture, list the messages from this connect. | — | In this order, **before** the `lifecycle`: `leak_detected` (A), `auto_close`, `valve_state_changed` (`"valve_state":"closed"`), `leak_cleared` (A), `rmleak_auto_cleared`. `ts` values never decrease. Each `ts` equals the `ts=` of its stamping line. | — | — | | Any health event held in T4-02 also replays here in its place; record it. |
| 3 | Check the times. | — | For each replayed event: (a) `gateway.uptime_s` equals the UART uptime of the matching T4-02 line (±1 s); (b) `ts - gateway.uptime_s` is the same number for all five (±1 s): the boot instant; (c) the `leak_detected` `ts` = the wall time printed by `Time synced` minus (the uptime of `Time synced` - the uptime of `LEAK INCIDENT latched`), ±2 s, and it is within 20 s after the PC time you noted when you wetted A (A's burst takes up to 15 s); (d) `rmleak_auto_cleared` `ts` - `leak_cleared` `ts` is 9-13 s (the 10-12 s auto-clear, ±1 s of rounding). | — | ±2 s | | (b) is the key check: the whole pre-sync batch is anchored to one boot instant computed at the sync. |
| 4 | Check the first snapshot after the connect. | `IOTHUB: SNAP trigger=%s` or `SNAP trigger=event:%s` lines **(format)** | A snapshot as below (`reason` `"fast"`, `"boot"` or `"event"`). | GREEN | ≤ 30 s after `Connected to Azure IoT Hub!` | | |
| 5 | Run the capture checks. | — | 0 FAIL; no `ts` < 1704067200 anywhere | — | — | | |

Expected replayed events (the `data` objects; each sits in an envelope `{"schema":"eflostop.v2","ts":…,"gateway":{…,"fw":"2.1.4","uptime_s":…},"type":"event","data":{…}}`; battery, rssi and location are A's real values):

```json
{"event":"leak_detected","source_type":"ble_leak_sensor","sensor_id":"<A>","leak_state":true,"battery":87,"location":{"code":"kitchen","label":"<A_LABEL>"},"rssi":-64}
{"event":"auto_close","source_type":"ble_leak_sensor","sensor_id":"<A>","rmleak_asserted":true,"location":{"code":"kitchen","label":"<A_LABEL>"}}
{"event":"valve_state_changed","source_type":"valve","valve_id":"<VALVE>","valve_state":"closed","battery":92,"leak_state":false,"rmleak":true,"fw_version":"2.2.0"}
{"event":"leak_cleared","source_type":"ble_leak_sensor","sensor_id":"<A>","leak_state":false,"battery":87,"location":{"code":"kitchen","label":"<A_LABEL>"},"rssi":-66}
{"event":"rmleak_auto_cleared","valve_id":"<VALVE>","clear_after_seconds":10}
```

`valve_state_changed.rmleak` is the valve's cached RMLEAK when the close was reported. `true` is expected because RMLEAK is written first; record `false` as a note, not a failure.

Expected snapshot after the connect (all sensors dry, valve closed, interlock released):

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398700,
  "gateway": {"id": "<GW>", "short_id": "<SHORT>", "name": "Bench Hub", "fw": "2.1.4", "uptime_s": 310},
  "type": "snapshot",
  "data": {
    "reason": "fast",
    "system_health": {"rating": "excellent", "reason": "All devices healthy"},
    "valve": {
      "valve_id": "<VALVE>",
      "state": "closed",
      "battery": 92,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "fw_version": "2.2.0",
      "rating": "excellent",
      "last_seen_age_s": 0
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {"sensor_id": "<A>", "connected": true, "rating": "excellent", "last_seen_age_s": 40, "battery": 87, "rssi": -64, "leak_state": false, "fw_version": "1.1.0", "location": {"code": "kitchen", "label": "<A_LABEL>"}},
      {"sensor_id": "<B>", "connected": true, "rating": "excellent", "last_seen_age_s": 71, "battery": 64, "rssi": -70, "leak_state": false, "fw_version": "1.1.0", "location": {"code": "laundry", "label": "<B_LABEL>"}}
    ],
    "rules": {"auto_close_enabled": true, "trigger_mask": 7},
    "override_active": false
  }
}
```

`ble_leak_sensors` has one entry per provisioned sensor (four on this bench; two are shown). The fields to judge are `valve.state:"closed"`, `valve.rmleak:false`, `valve.last_seen_age_s:0`, `system_health`, `override_active:false` and `rules`.

**Pass:** every row passes. Finally restore the valve: send `valve_open` (`{"schema":"eflostop.cmd","ver":1,"id":"t4-03-open","cmd":"valve_open"}`); expect `cmd_ack` `"status":"ok"` and `valve_state_changed` `"open"`.

---

### T4-04 — A restart before the clock syncs drops that boot's pre-sync events (about 10 min)

**Purpose:** council risk "pre-sync events and a restart before the clock syncs". Which offline-buffer slots hold this boot's pre-sync events is tracked in RAM only, so after a restart before the sync those events cannot be time-stamped. They must be dropped with a logged line, never sent with a 1970 `ts`. This is accepted behaviour (CHANGELOG: "A pre-sync event still unstamped when the hub restarts … is dropped at replay").

**Preconditions:** the common start state.

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Router off. Power-cycle the hub. Wait for SETUP COMPLETE and GREEN. | as T4-02 rows 1-2 | nothing | GREEN | as T4-02 | | |
| 2 | Wet A, wait for the valve to close, then dry A and wait for the auto-clear. | as T4-02 rows 3-4: every event logs `Time not synced (ts=%ld) - holding event for replay; stamped when the clock syncs` and `Stored event [%s] (%u bytes), %d buffered` **(format)** | nothing | RED → YELLOW → GREEN | as T4-02 | | Record the `[ob_NN]` keys and the final buffered count (normally 5). |
| 3 | With the router still off, do an EN reset. | Boot lines as T4-02 row 1, with `OFFLINE_BUF: Init: %d buffered event(s) pending from before reboot` **(format)** (the count from step 2). The valve links: `RULES_ENGINE: Reconnected: no active incident, valve clear`. | nothing | OFF → WHITE → GREEN | — | | Record the lifecycle `reset_reason` later (step 5); it is not judged. |
| 4 | Router on. | `Connected! IP`, `Initializing SNTP...`, `Time synced: %s` **(format)**. **No** `Stamped pre-sync event [%s] at clock sync …` for the old slots. `Connected to Azure IoT Hub!`, `Draining %d offline event(s) before lifecycle...` **(format)**, `OFFLINE_BUF: Draining %d buffered event(s)...` **(format)**, then **one per old event**: `OFFLINE_BUF: Dropped a buffered event from an earlier power cycle that was never time-stamped [%s]` **(format)**, then `Drain complete: %d event(s) published, %d remaining` **(format)** with `0 remaining`. | **None** of the five events from step 2. `lifecycle`, then snapshots. The valve is still `"state":"closed"`, `"rmleak":false`. | GREEN | — | | If an event was raised after the EN reset and before the sync (for example a health event), it is stamped (`Stamped pre-sync event … at clock sync`) and replayed normally; record it. |
| 5 | Run the capture checks. Note the lifecycle `reset_reason`. | — | 0 FAIL; no `ts` < 1704067200; no `leak_detected` / `auto_close` from before the EN reset | — | — | | |
| 6 | Control run (optional, 10 min): repeat steps 1-2, skip the EN reset (step 3), then do step 4. | `Stamped pre-sync event [%s] at clock sync: ts=%lld (%lld s ago)` **(format)** for every held event, and `Replayed [%s]` for each | the five events, in order, before the lifecycle, with real `ts` (as T4-03) | GREEN | — | | This is T4-03 again; run it only if T4-03 was not run on this build. |

**Pass:** rows 1-5 pass. No crash, no message with a 1970 `ts`, and one `Dropped a buffered event from an earlier power cycle …` line per held event. Restore the valve with `valve_open`.

---

### T4-05 — NTP blocked: the SNTP fallback, and leak protection without a clock (about 15 min)

**Purpose:** N1 with a Wi-Fi link but no time. SNTP no longer blocks the loop. The initial sync window is 120 s, then a 60 s re-poll timer takes over and the loop returns to its 30 s cadence. DPS and MQTT wait for the clock. A leak still closes the valve, and its events are held and then stamped when NTP comes back. HANDOFF §7 "NTP blocked".

**Preconditions:** the common start state. On the router, prepare a rule that blocks the hub's NTP: drop outbound UDP port 123 from the hub's IP, or block DNS for `pool.ntp.org`. Do not block anything else.

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Enable the NTP block. Power-cycle the hub (the clock must be lost). | T4-01 row 1 lines, then `APP_WIFI: Connected! IP: %s` **(format)**, `IOTHUB: Initializing SNTP...`. **No** `Time synced`. **No** `DPS` lines at all (DPS waits for a valid clock). | nothing | OFF → WHITE → GREEN; network LED beat blue (Wi-Fi up, no MQTT) | `Initializing SNTP...` ≤ 2 s after `Connected! IP` | | Record the uptime of `Initializing SNTP...` (T0). |
| 2 | Wait. | `IOTHUB: SNTP initial sync failed — starting 60s retry timer` | nothing | GREEN | at T0 + 120 s, +0 to +2 s | | |
| 3 | Wait 3 min more. | `IOTHUB: SNTP still not synced — restarting NTP poll` once per minute. Still no `DPS` lines, no `Time synced`. | nothing | GREEN | 60 s ± 1 s apart | | The loop's return from the 2 s poll to the 30 s idle has no log line of its own on a release build. It is covered by code review (`cloud_pending` in `app_iothub.c` drops its SNTP clause once `s_sntp_fallback` is set, and its DPS clause needs a valid clock). Record "not observable" here. |
| 4 | Wet A. Wait for the close, dry A, wait for the auto-clear. | As T4-02 rows 3-4: `LEAK INCIDENT latched …`, `AUTO-CLOSE + RMLEAK triggered …`, the valve closes, and every event logs `Time not synced (ts=%ld) - holding event for replay; stamped when the clock syncs` **(format)**; then `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`. | nothing | RED → YELLOW → GREEN | as T4-02 | | Protection works with Wi-Fi up and no clock. |
| 5 | Disable the NTP block. | Within about 90 s: `IOTHUB: Time synced: %s` **(format)** and, from the timer, `IOTHUB: SNTP now synced (ts=%ld) — stopping retry timer` **(format)** (either order). The stamping lines `Stamped pre-sync event [%s] at clock sync: ts=%lld (%lld s ago)` **(format)** right after `Time synced`. Then `DPS: Loaded cached assignment from NVS`, `Connected to Azure IoT Hub!`, the drain with `Replayed [%s]` per event and `Drain complete: … 0 remaining`. | The held events in order before the `lifecycle`, with real `ts` (T4-03 row 3 checks (a)-(d)). | GREEN; network LED ramp blue | `Time synced` ≤ 90 s after the unblock (≤ 60 s to the next re-poll, plus up to 30 s of loop idle) | | |
| 6 | Run the capture checks. | — | 0 FAIL; no `ts` < 1704067200 | — | — | | Restore the valve with `valve_open`. |

**Pass:** every row passes.

---

### T4-06 — DPS unreachable: the in-loop backoff, the 5 min retry and the recovery (DESTRUCTIVE, about 45 min)

**Purpose:** N1 cloud bring-up. A failed DPS registration no longer holds the boot: it is retried from the loop with the old backoff (5-25 s), then every 5 min. The attempts count only while Wi-Fi is up and the clock is valid. Known limitation, observed and recorded: a live registration blocks `iothub_task` for up to 60 s per attempt (CHANGELOG "Known limitations"). It happens only with no DPS cache (first boot, after `decommission all`, after a provisioning-epoch change), which is an empty hub, so no protection is lost.

**This test erases the hub's provisioning.** Run it last in the campaign, or on a freshly erased hub. Afterwards re-provision the hub with the bench `provision` payload (C2D_COMMANDS.md §4.9).

**Preconditions:** the hub is online. On the router, prepare a rule that blocks **only** `global.azure-devices-provisioning.net` (DNS block, or drop TCP 8883 to it). The IoT Hub host (`<name>.azure-devices.net`) must stay reachable. Alternative start: a hub just erased and flashed with 2.1.4 whose Wi-Fi has been set through the portal; then start at step 3.

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Enable the DPS block. | nothing (the hub has its DPS cache) | nothing changes | GREEN | — | | |
| 2 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t4-06-decom-all","cmd":"decommission","payload":{"target":"all"}}` | `IOTHUB: C2D cmd='decommission' ver=1 id='t4-06-decom-all'`; `IOTHUB: !!! DECOMMISSION_ALL !!!`; `DPS: DPS cache cleared`; `OFFLINE_BUF: Buffer cleared`; `IOTHUB: Decommissioned — restarting in 3s...`; then a reboot | `cmd_ack` `"status":"ok"`; one snapshot with `"reason":"decommission"` and the empty shape (`"valve":{}`, empty sensor arrays, `system_health` `{"rating":"excellent","reason":"No devices provisioned"}`) | — | reboot about 3 s after the log line | | The decommission content itself is DEC-18; here it only clears the DPS cache. |
| 3 | Watch the boot. | `IOTHUB: Boot: hub is empty - clearing any persisted rules-engine state`; `IOTHUB: Hub is UNPROVISIONED - waiting for provisioning JSON from Azure`; **no** `Starting BLE`; `IOTHUB: Time synced: %s` **(format)** within 30 s of boot, before or after `Connected! IP` (the software restart kept the RTC time); `APP_WIFI: Connected! IP: %s` **(format)**. Then attempt 1: `DPS: No cached assignment, performing DPS registration...`, `DPS: Derived device key for '%s'` **(format)**, `DPS: Connecting to %s...` **(format)** = `global.azure-devices-provisioning.net`, then the failure (`DPS: MQTT error during DPS registration` and/or `DPS: Disconnected from DPS endpoint`, then `DPS: DPS registration failed (state=%d, timeout=%s)` **(format)**), then `IOTHUB: DPS failed (attempt %d/%d), retry in %ds` **(format)** = `attempt 1/6), retry in 5s`. | nothing | `FLEET_LED: rating=unprovisioned color=WHITE effect=SOLID`; network LED beat blue | attempt 1 within 2 s of both Wi-Fi up and a valid clock | | Record the time from `Connecting to …` to `DPS registration failed …` for each attempt (DNS block: a few seconds; a dropped TCP connect: about 10 s; the bound is 60 s). The DPS-internal failure lines depend on how the block is made; the `IOTHUB` lines do not. |
| 4 | Wait for attempts 2-6. | `DPS failed (attempt 2/6), retry in 10s`, `(attempt 3/6), retry in 15s`, `(attempt 4/6), retry in 20s`, `(attempt 5/6), retry in 25s`, each followed by the next attempt's `DPS: Connecting to …`. After attempt 6: `IOTHUB: DPS unavailable after %d attempts — continuing without cloud. Leak detection and valve auto-close run normally; DPS retried every %d min.` **(format)** = `6 attempts`, `every 5 min`. | nothing | WHITE | each attempt starts at the stated backoff after the previous `DPS registration failed`, +0 to +2 s | | `MONITOR: heap: …` lines keep coming every 10 s throughout (the monitor task is independent); a gap in them longer than 12 s during an attempt is recorded. |
| 5 | Wait 11 min. | Twice, about 5 min apart: `IOTHUB: DPS: retrying registration...`, then the DPS attempt lines and its failure. No further `DPS failed (attempt …)` line (it prints only for attempts 1-5). | nothing | WHITE | 300 s after the previous failure, +0 to +30 s | | |
| 6 | Disable the DPS block. | At the next 5 min retry: `IOTHUB: DPS: retrying registration...`, `DPS: No cached assignment, performing DPS registration...`, `DPS: Connecting to %s...` **(format)**, `DPS: Connected to DPS endpoint`, `DPS: Submitting registration for '%s'...` **(format)** = `'<GW>'`, `DPS: DPS response status=%d` **(format)** (one or more), `DPS: Assigned hub=%s device=%s` **(format)**, `DPS: Cached assignment in NVS (provisioning epoch %d)` **(format)**, `IOTHUB: DPS: hub=%s device=%s` **(format)**, `IOTHUB: DPS: registration recovered — cloud path up`, `IOTHUB: Connected to Azure IoT Hub!` | `lifecycle` with `"provisioned":false` and no `valve_id`, then one `snapshot` with `"reason":"boot"` and the empty shape, then heartbeats | WHITE; network LED ramp blue | ≤ 5 min 30 s after the unblock | | If you unblock during attempts 1-6 instead, the success prints only the DPS lines and `DPS: hub=…`; `registration recovered` is printed only after the boot phase. |
| 7 | Re-provision the hub (bench `provision` payload). | the normal provision lines; `Starting BLE (valve=<VALVE>, BLE sensors=4)` | `cmd_ack` ok; provision snapshots | back to GREEN once all devices are heard | — | | Needed for the rest of the campaign. |

**Pass:** rows 1-6 pass. Attempts 1-5 back off 5, 10, 15, 20, 25 s; attempt 6 logs the "DPS unavailable after 6 attempts" error; later attempts are 5 min apart; the recovery connects without a reboot. Record the longest attempt duration (the known 60 s limitation).

---

### T4-07 — Override started before the clock synced: timed on uptime, re-based at the sync (about 20 min)

**Purpose:** the pre-sync override fix. A valve-button override started while the router is down used to get an expiry in 1970: it ended the moment the clock synced, and with no internet it never ended. Now it is timed on the hub uptime, its `water_access_override_enabled` omits `expires_ts`, and within about 30 s of the first sync it is re-based to the real clock and saved with a real epoch.

**Preconditions:** the common start state. The valve is linked and open.

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Router off. Power-cycle the hub. Wait for SETUP COMPLETE and GREEN. | as T4-02 rows 1-2 | nothing | GREEN | as T4-02 | | |
| 2 | Wet A. Keep it wet. | as T4-02 row 3 (auto-close, valve CLOSED, `[DATA] RMLEAK=1 (ACTIVE)`, every event `holding event for replay`) | nothing | RED | as T4-02 | | Note the uptime of `[DATA] RMLEAK=1 (ACTIVE)`. |
| 3 | At least 6 s after `[DATA] RMLEAK=1 (ACTIVE)`, long-press the valve button (the SRS §4.4.2 override press). Note the PC time and the uptime. | `BLE_VALVE: [DATA] RMLEAK=0 (CLEAR)` and `[DATA] Valve State=1 (OPEN)` (valve side, either order); `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window`; `RULES_ENGINE: NVS: override state=%d expiry=%ld saved` **(format)** = `state=1`, expiry about uptime + 86400; `RULES_ENGINE: OVERRIDE WINDOW STARTED: auto-close blocked for 24h (expiry=%ld)` **(format)** (same expiry); `RULES_ENGINE: Override window stamped before clock sync - timed on uptime (start=%lus) until the clock syncs` **(format)**, `start` = the uptime in seconds; then `Time not synced (ts=%ld) - holding event for replay; stamped when the clock syncs` **(format)** and `Stored event …` for the override event, and again for `valve_state_changed` (open). | nothing | RED stays (A is still wet) | the override lines ≤ 30 s after the press (normally ≤ 1 s: the RMLEAK notify wakes the loop) | | Record `start=`. The expiry printed here is a small number (seconds since power-on + 86400), not an epoch; that is expected. |
| 4 | Dry A. Wait for `eleak … leak=0`. Then wet A again. | Dry: `eleak … leak=0`, a held `leak_cleared`. **No** `auto-clear timer started` (the override start cleared the incident). Re-wet: `eleak … leak=1`; `RULES_ENGINE: LEAK INCIDENT latched by ble_leak_sensor sensor <A>`; `RULES_ENGINE: Override active — auto-close BLOCKED for %s sensor %s (remaining=%lds)` **(format)** with `remaining` = 86400 - (uptime now - `start`), ±1 s; held `leak_detected` and `auto_close_blocked_override`. **No** `AUTO-CLOSE + RMLEAK triggered`, **no** `[CMD] Writing Valve=0`. | nothing | GREEN while dry → RED | ±1 s on `remaining` | | The valve stays OPEN: the window blocks auto-close. |
| 5 | Wait 2 min, router still off. | only `MONITOR` and scanner heartbeat lines | nothing | RED | — | | |
| 6 | Router on. Note the PC time. | `Connected! IP: %s` **(format)**; `Initializing SNTP...`; `Time synced: %s` **(format)**; one `Stamped pre-sync event [%s] at clock sync: ts=%lld (%lld s ago)` **(format)** per held event; `RULES_ENGINE: Override window re-based to the synced clock (expiry=%ld, remaining=%lds)` **(format)**; `RULES_ENGINE: NVS: override state=%d expiry=%ld saved` **(format)** with the same epoch expiry; then `Connected to Azure IoT Hub!` and the drain (`Replayed [%s]` per event, `Drain complete: … 0 remaining`). | the replayed events (below), then `lifecycle`, then snapshots | RED | the re-base line ≤ 30 s after `Time synced` (normally ≤ 2 s) | | Check: re-base `remaining` = 86400 - (uptime at the re-base - `start`), ±2 s; re-base `expiry` = the epoch at the re-base + `remaining`, ±2 s. **Must not appear:** `Override window ran its full duration before the clock synced …`, `Override window was stamped before a clock sync in an earlier boot …`, `OVERRIDE WINDOW EXPIRED`. |
| 7 | Check the replay. | — | In order, before the `lifecycle`: `leak_detected` (A), `auto_close`, `valve_state_changed` (closed); then `water_access_override_enabled` and `valve_state_changed` (open) in either order; then `leak_cleared` (A), `leak_detected` (A), `auto_close_blocked_override`. The override event has **no** `expires_ts`; its `ts` is within ±2 s of the press time; its `gateway.uptime_s` = `start` ±1. | — | ±2 s | | |
| 8 | Check the first snapshot after the connect, and the next one. | `SNAP trigger=…` | Snapshot as below: `override_active:true`, `override_remaining_s` = the re-base `remaining` minus the seconds since the re-base (±2), `expires_ts` = the re-base expiry exactly. A snapshot **without** `expires_ts` is acceptable only if its `ts` is within 30 s of the `Time synced` line. | RED | — | | |
| 9 | Clean up: dry A, wait for `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`, then send `{"schema":"eflostop.cmd","ver":1,"id":"t4-07-ovc","cmd":"override_cancel"}` | `RULES_ENGINE: AUTO-CLEAR: …`; `IOTHUB: Command: OVERRIDE_CANCEL`; `RULES_ENGINE: OVERRIDE WINDOW CANCELLED (remaining_s=%ld)` **(format)** | `rmleak_auto_cleared`; `cmd_ack` ok; `{"event":"auto_close_reenabled","previous_remaining_s":NNNNN,"reason":"c2d_command"}`; `override_active:false` in the next snapshot | RED → YELLOW → GREEN | AUTO-CLEAR 10-12 s after the dry report | | The valve stays open. |
| 10 | Run the capture checks. | — | 0 FAIL | — | — | | |

Expected replayed override and blocked events (the `data` objects):

```json
{"event":"water_access_override_enabled","trigger":"button","remaining_s":86400}
{"event":"auto_close_blocked_override","source_type":"ble_leak_sensor","sensor_id":"<A>","override_remaining_s":86310}
```

`override_remaining_s` in the blocked event is the uptime-based countdown logged in row 4.

Expected snapshot after the re-base (A wet, window open, valve open):

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398900,
  "gateway": {"id": "<GW>", "short_id": "<SHORT>", "name": "Bench Hub", "fw": "2.1.4", "uptime_s": 420},
  "type": "snapshot",
  "data": {
    "reason": "fast",
    "system_health": {"rating": "critical", "reason": "Leak detected: <A_LABEL>, Leak interlock latched"},
    "valve": {
      "valve_id": "<VALVE>",
      "state": "open",
      "battery": 92,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "fw_version": "2.2.0",
      "rating": "excellent",
      "last_seen_age_s": 0
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {"sensor_id": "<A>", "connected": true, "rating": "critical", "last_seen_age_s": 9, "battery": 87, "rssi": -64, "leak_state": true, "fw_version": "1.1.0", "location": {"code": "kitchen", "label": "<A_LABEL>"}},
      {"sensor_id": "<B>", "connected": true, "rating": "excellent", "last_seen_age_s": 71, "battery": 64, "rssi": -70, "leak_state": false, "fw_version": "1.1.0", "location": {"code": "laundry", "label": "<B_LABEL>"}}
    ],
    "rules": {"auto_close_enabled": true, "trigger_mask": 7},
    "override_active": true,
    "override_remaining_s": 86022,
    "expires_ts": 1785484922
  }
}
```

"Leak interlock latched" is there because the re-wet in row 4 latched a new incident (an incident is always latched, even during a window; only the close is blocked). `reason` may also be `"boot"` or `"event"`.

**Pass:** every row passes. The window survived the sync (no expiry at the sync) and carries a real `expires_ts` from the re-base on.

---

### T4-08 — Override started before the clock synced expires after 24 h of uptime with no internet (optional; 24 h, or about 15 min on a bench-only build)

**Purpose:** the second half of the pre-sync override fix. With the router down for good, a window started before any sync used to never expire. Now it expires on the hub uptime through the normal expiry path: RMLEAK before CLOSE if a leak is still active, and a `water_access_override_expired` event (held and stamped later).

**Choose one:**

- **Production image:** run the steps as written and wait 24 h at step 4. The router must stay off the whole time.
- **Bench-only image (the engineer's choice; NEVER ship it, never OTA it to a field hub):** in `main/CMakeLists.txt`, after the `idf_component_register(...)` call, add the line `target_compile_definitions(${COMPONENT_LIB} PRIVATE OVERRIDE_WINDOW_DURATION_S=600)`. Build and flash. The window then lasts 600 s. The log text still says "24h" (it is a fixed string); `remaining_s` in the event reads `600`. **After the test,** remove the line (`git diff main/CMakeLists.txt` shows nothing), rebuild, reflash the release image, and confirm on the next override that `remaining_s` is `86400`. Record the image used in the notes.

Below, D is 86400 s (production) or 600 s (bench-only).

**Preconditions:** the common start state.

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Router off. Power-cycle the hub. Wait for SETUP COMPLETE. | as T4-02 rows 1-2 | nothing | GREEN | | | |
| 2 | Wet A and **keep it wet for the whole test**. Wait for the auto-close. | as T4-02 row 3 | nothing | RED | as T4-02 | | |
| 3 | At least 6 s after `[DATA] RMLEAK=1 (ACTIVE)`, long-press the valve button. | as T4-07 row 3, with `Override window stamped before clock sync - timed on uptime (start=%lus) until the clock syncs` **(format)** | nothing | RED | as T4-07 | | Record `start=` (S). |
| 4 | Wait D. | Every 5 min, A's unchanged wet state is re-reported (the scanner heartbeat): the first time `LEAK INCIDENT latched by ble_leak_sensor sensor <A>` (the override start cleared the incident), and each time `Override active — auto-close BLOCKED for ble_leak_sensor sensor <A> (remaining=%lds)` **(format)**, at most once a minute, with `remaining` = D - (uptime - S). Then: `RULES_ENGINE: OVERRIDE WINDOW EXPIRED: auto-close re-enabled`; `RULES_ENGINE: Override expired with %d active leak(s) — executing auto-close` **(format)** = `1 active leak(s)`; then on the valve, in this order: `[CMD] Writing RMLEAK=1`, `[DATA] RMLEAK=1 (ACTIVE)`, `[CMD] Writing Valve=0`, `[DATA] Valve State=0 (CLOSED)`; held events for `water_access_override_expired` and `valve_state_changed` (closed). | nothing | RED | the expiry at uptime S + D, +0 to +30 s (the 30 s loop idle: with no Wi-Fi there is no 2 s poll) | | **Must not appear:** `Override window restored after a power-on has run its full duration with no clock sync - expiring it now` (that line is for a window restored from NVS, T4-09). |
| 5 | Router on. | `Time synced: %s` **(format)**; `Stamped pre-sync event [%s] at clock sync: …` **(format)** per held event; `Connected to Azure IoT Hub!`; the drain. No `Override window re-based …` (the window has already ended). | replayed in order: …, `water_access_override_enabled` (no `expires_ts`), …, `water_access_override_expired`, `valve_state_changed` (closed); then `lifecycle`; the snapshot has `override_active:false`, `valve.state:"closed"`, `valve.rmleak:true` | RED | — | | With D = 24 h, record any `Buffer full, oldest event overwritten` (16 entries). |
| 6 | Clean up: dry A, wait for `AUTO-CLEAR`, send `valve_open`. If you used the bench-only image, restore the release image now. | `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK` | `rmleak_auto_cleared`; `cmd_ack` ok | GREEN | | | |

Expected expiry event (`data`):

```json
{"event":"water_access_override_expired","auto_close_resumed":true,"active_leak_count":1}
```

**Pass:** rows 1-5 pass. The window expired at S + D (+30 s) with no clock, and the valve was re-locked (RMLEAK before CLOSE).

---

### T4-09 — Override windows restored after a restart with no internet (E-03; about 30 min)

**Purpose:** E-03 and the council's "override restored across a power-on with no internet" risk. Part A: a window with a real expiry, restored after a power cut that lost the clock, used to block auto-close for as long as the site stayed offline. It is now timed from that power-on until the clock syncs, and from its real expiry after that. Part B is a **known limitation, observe and record**: a window started before the clock synced and restored after a restart cannot be re-based, so it ends at the first sync (it fails toward auto-close).

**Preconditions:** the common start state, router on, clock synced.

**Part A — real-epoch window, power cut, no internet**

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| A1 | Wet A. Wait for the close. At least 6 s after `[DATA] RMLEAK=1 (ACTIVE)`, long-press the valve button. | `RMLEAK cleared externally (valve override) — starting 24h override window`; `NVS: override state=1 expiry=%ld saved` **(format)**; `OVERRIDE WINDOW STARTED: auto-close blocked for 24h (expiry=%ld)` **(format)** with an epoch (E). **No** `stamped before clock sync` line. | `{"event":"water_access_override_enabled","trigger":"button","expires_ts":<E>,"remaining_s":86400}` | RED | as T4-07 row 3 | | Record E. |
| A2 | Dry A. Router off. Power-cycle the hub, with the power off for about 60 s. | Boot: `RULES_ENGINE: NVS: restored override window (expiry=%lu, remaining=%lds)` **(format)** with `expiry` = E and `remaining` = 86400 minus a few seconds (timed from this power-on, **not** E - now). At the valve link: `║ Hub:   incident=%d leaks=%d override=%s` **(format)** with `override=ACTIVE`, `║ Override window: remaining=%lds` **(format)** (about 86400 - uptime), `RULES_ENGINE: Reconnected: override window active — skipping auto-close`. | nothing | GREEN | — | | This `remaining` is longer than the real one: known limitation (the hub cannot know how long the power was off). |
| A3 | Wet A. | `LEAK INCIDENT latched by ble_leak_sensor sensor <A>`; `Override active — auto-close BLOCKED for ble_leak_sensor sensor <A> (remaining=%lds)` **(format)** with remaining about 86400 - uptime (not -1); held `leak_detected` and `auto_close_blocked_override`. The valve stays OPEN. | nothing | RED | | | Up to 2.1.3 the blocked event had no `override_remaining_s` in this state. |
| A4 | Dry A. Wait for the auto-clear. | `All sensors clear — auto-clear timer started (10s)`; `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`; held `leak_cleared` and `rmleak_auto_cleared`. The window stays open. | nothing | RED → YELLOW → GREEN | 10-12 s | | |
| A5 | Router on. | `Time synced: %s` **(format)**; the stamping lines; `Connected to Azure IoT Hub!`; the drain. **No** `Override window re-based …` (the expiry is already real), **no** `OVERRIDE WINDOW EXPIRED`. | Replayed: `leak_detected`, `auto_close_blocked_override` with `override_remaining_s` ≈ 86400 - the uptime at the event, `leak_cleared`, `rmleak_auto_cleared`. The snapshot: `override_active:true`, `override_remaining_s` = E - `ts` (±2 s), `expires_ts` = E. | GREEN | ±2 s | | Once the clock syncs, the real expiry E decides again. |
| A6 | (Optional, bench-only image with D = 600 s, see T4-08.) Repeat A1-A2 and keep the router off for more than D after the power-on. | `RULES_ENGINE: Override window restored after a power-on has run its full duration with no clock sync - expiring it now`; `OVERRIDE WINDOW EXPIRED: auto-close re-enabled`; a held `water_access_override_expired` | after the router returns: `water_access_override_expired` with a real `ts` about D after the power-on | — | at power-on + D, +0 to +30 s | | |
| A7 | Clean up: `override_cancel`. | `OVERRIDE WINDOW CANCELLED (remaining_s=%ld)` **(format)** | `cmd_ack` ok, `auto_close_reenabled`, snapshot `override_active:false` | GREEN | | | |

**Part B — window started before the sync, then a restart (known limitation)**

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| B1 | Router off. Power-cycle the hub. Wet A, wait for the close, long-press the valve button (≥ 6 s after RMLEAK=1), then dry A. | as T4-07 rows 1-3, with `Override window stamped before clock sync - timed on uptime (start=%lus) …` **(format)** | nothing | RED → GREEN | | | |
| B2 | EN reset, router still off. | `OFFLINE_BUF: Init: %d buffered event(s) pending from before reboot` **(format)**; `RULES_ENGINE: NVS: restored override window (expiry=%lu, remaining=%lds)` **(format)** with an `expiry` below 1704067200; at the link, `Reconnected: override window active — skipping auto-close` | nothing | GREEN | | | |
| B3 | Router on. | `Time synced: %s` **(format)**; within 30 s: `RULES_ENGINE: Override window was stamped before a clock sync in an earlier boot - elapsed time unknown, expiring it now`, then `OVERRIDE WINDOW EXPIRED: auto-close re-enabled`. At the drain: one `Dropped a buffered event from an earlier power cycle that was never time-stamped [%s]` **(format)** per event of the B1 boot. | `{"event":"water_access_override_expired","auto_close_resumed":false,"active_leak_count":0}` (after the lifecycle if it was buffered offline). **Not** the B1 `water_access_override_enabled`: it was a pre-sync event of an earlier boot and is dropped. | GREEN | expiry ≤ 30 s after `Time synced` | | Observe and record. The cloud sees an expiry with no matching start: note it for the app team. Snapshot `override_active:false`. |

**Pass:** Part A rows A1-A5 and A7 pass. Part B is recorded; it passes if it behaves as listed (the window ends at the sync, no crash, no 1970 `ts`).

---

### T4-10 — Plan S21: captive portal with BLE scanning paused (about 115 min)

**Purpose:** S21, E-21, the council's portal risks, and the 2026-09-29 portal fix and its follow-up. On `d9fa9c8` a phone could not join the SoftAP after a Wi-Fi reset: BLE scanning, which 2.1.4 starts at boot, took the radio from the SoftAP and no DHCP lease was ever given. Since the portal priority window (header note), while the portal is up and **no Wi-Fi credentials are saved**, the leak scanner and the valve hunt pause. NimBLE stays up, a valve already linked stays linked, and a leak close pended for an unlinked valve still hunts it (leak protection outranks the portal). There is no time cap before setup. After the save the window stays open until the setup SoftAP stops, about 60 s after the STA gets its IP, so the phone can load the portal's "Connected!" page; it closes at once if that Wi-Fi is lost first, and a safety net stops the SoftAP about 75 s after the IP. The BLE sensors' and the valve's health timeouts and the snapshot gate are held and restart when scanning resumes. The router-outage fallback portal (credentials still saved) keeps BLE scanning, and the 10 s reset erases the credentials in every Wi-Fi state, so a reset during a router outage brings the hub back in the setup portal with the window. The portal must load and save with no reboot, a leak during the portal must still close the valve, and the credentials must persist. Heap (`free`, `min_ever`, `largest_blk`) is recorded throughout. The council's NVS-pressure risk (a full offline ring next to the credential save) is T6-14 (Part C below points to it).

**Preconditions:** the common start state, a hub with the valve and A-D, and **LoRa sensor L provisioned to it** (Parts A, B, G and H wet L: a BLE sensor is not heard while scanning is paused). Without L, record A6, A7, B2-B6, G2 and H1 as `Blocked`. A phone; note its model and OS, and "forget" `WiFi-Hub-<SHORT>` on it first. For Part F, a hub with BLE sensors and no valve (the 2026-09-29 unit `GW-7C4FADAE69C8` if available). A router whose 2.4 GHz network you can switch off (Parts D and H). Start a fresh UART capture for every part.

**Lines of the portal priority window and the reset** (all **(format)** where they have fields):

| Line | When |
|---|---|
| `HEALTH_ENGINE: BLE scanning paused - BLE sensor timeouts held` | window opens (first). The valve's timeouts are held too, although the text names only the sensors |
| `APP_WIFI: portal priority ON (no Wi-Fi credentials) - BLE scanning paused` (W) | window opens |
| `APP_WIFI: portal priority: wifi_manager task prio %u -> %u (httpd, dns_server not raised)` = `5 -> 8` | window opens |
| `APP_WIFI: SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on` | fallback AP; the window does NOT open |
| `APP_WIFI: SoftAP: station %02X:…:%02X joined, AID=%u` / `… left, AID=%u, reason=%u` | a phone joins / leaves the SoftAP (from about 1 s after boot) |
| `BLE_LEAK: Scan paused - Wi-Fi setup portal has the radio` / `BLE_LEAK: Scan resumed - Wi-Fi setup portal closed` | the leak scanner, within 0.5 s of the window edge |
| `BLE_VALVE: [SCAN] Valve scan held - Wi-Fi setup portal has the radio` | each valve hunt the window holds |
| `BLE_VALVE: [PORTAL] Valve hunt paused - Wi-Fi setup portal has the radio` (+ ` (valve link kept)`) / `[PORTAL] Valve hunt resumed - Wi-Fi setup portal closed` | the valve task, within 1 s of the window edge; at boot the "paused" line prints only if the valve target was already set, so rely on `Valve scan held` there |
| `BLE_VALVE: [PORTAL] Leak response pending - valve hunt runs despite the Wi-Fi setup portal` (W) | a leak close is pended for an unlinked valve: the hunt runs anyway |
| `BLE_VALVE: [PORTAL] Valve hunt stopped - Wi-Fi setup portal has the radio` | that hunt stops because the pended close was withdrawn |
| `APP_WIFI: portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops (about %d s)` = `60` | right after `APP_WIFI: Connected! IP: %s` when Wi-Fi is set up in the window; BLE stays paused |
| `HEALTH_ENGINE: BLE scanning resumed - BLE sensor timeouts restart now (600 s)` | window closes (first). The valve's 180 s restarts too |
| `APP_WIFI: portal priority OFF (%s) - BLE scanning resumed` = `AP stopped` | window closes when the setup SoftAP stops, 60 s after `Connected! IP` |
| `APP_WIFI: portal priority OFF (%s) - BLE scanning resumed` = `Wi-Fi lost after setup` | the STA lost the Wi-Fi it was set up with before the SoftAP stopped (Part H) |
| `APP_WIFI: portal priority: setup AP still up %u s after Wi-Fi connected - stopping it` (W) | the safety net, 75-80 s after `Connected! IP`; should never appear: record it with its time |
| `BLE_VALVE: [PORTAL] Valve hunt not paused - a leak response is pending` (W) | the window opens while a leak close is pended for an unlinked valve |
| `BLE_VALVE: [PORTAL] Valve scan cancelled - Wi-Fi setup portal opened`; `[PORTAL] Valve connect cancelled - Wi-Fi setup portal opened (rc=%d)`; `[PORTAL] Valve connect in flight cancelled (rc=%d)` | the window opened (or a pended leak close was written or withdrawn) as a hunt or a connect was starting or running: race lines at a window edge, record them |
| `BLE_LEAK: Scan cancel failed: %d, will retry` (W) | should never appear; record it |
| `RESET_BTN: Wi-Fi credentials erased from NVS` | the 10 s reset, about 2 s after `Erasing WiFi credentials …` and just before `Rebooting into AP mode...`, in every Wi-Fi state (A2, D5) |
| `RESET_BTN: No Wi-Fi credentials saved - nothing to erase` | the same point, on a hub that never saved Wi-Fi credentials; not expected in this test |
| `RESET_BTN: Wi-Fi NVS lock busy for 3 s - erasing without it` (W); `RESET_BTN: Wi-Fi credential erase failed (%s) - rebooting anyway` (E) | should never appear; record them |

**Part A — valve present (the normal install flow)**

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| A1 | Record three `MONITOR` lines (baseline, online). | `MONITOR: heap: free=%lu min_ever=%lu largest_blk=%lu uptime=%lus` **(format)** | — | GREEN | every 10 s | | Baseline row of the heap table. |
| A2 | Hold the Wi-Fi reset button (GPIO 40) for more than 10 s. | `RESET_BTN: Button pressed — starting %d ms hold timer` **(format)** = `10000 ms`; `RESET_BTN: %d-second hold confirmed — executing WiFi reset` **(format)** = `10-second`; `RESET_BTN: === LONG PRESS CONFIRMED — CLEARING WIFI CREDENTIALS ===`; `RESET_BTN: Erasing WiFi credentials, then rebooting into AP (commissioning preserved in nvs_prov)...`; `IOTHUB: WiFi down — stopping MQTT client (free TLS heap for AP/captive portal)` and `APP_WIFI: WiFi Disconnected. Reason: %d` **(format)** = `8` (the STA was connected); `RESET_BTN: Wi-Fi credentials erased from NVS`; `RESET_BTN: Rebooting into AP mode...`; then one reboot | the MQTT connection drops | — | reboot about 2 s after `Erasing WiFi credentials …` | | This one reboot is intended. From here on, any further boot banner is a FAIL. `Wi-Fi credentials erased from NVS` is the proof of the erase; `No Wi-Fi credentials saved - nothing to erase` here is a FAIL (the hub was online, so its credentials were saved). In the 2 s before the reboot the window can already open (the Wi-Fi manager's own erase sends the SoftAP start): the window-open lines, `BLE_LEAK: Scan paused …` and `[PORTAL] Valve hunt paused - Wi-Fi setup portal has the radio (valve link kept)` may print there; they are expected. |
| A3 | Watch the boot. | `APP_WIFI: AP SSID: WiFi-Hub-<SHORT>`; `esp_netif_lwip: DHCP server started on interface WIFI_AP_DEF with IP: 10.10.0.1` under 1 s after boot, with **no** `APP_WIFI: WiFi Disconnected. Reason: 2NN` line (no credentials left); the three window-open lines above; `IOTHUB: Starting BLE (valve=<VALVE>, BLE sensors=4)`; `BLE_VALVE: [HOST] NimBLE host task started`; `BLE_VALVE: [SCAN] Valve scan held - Wi-Fi setup portal has the radio` at least once; at about 3 s `BLE_LEAK: Scan paused - Wi-Fi setup portal has the radio`; `IOTHUB: Time synced: %s` **(format)** within 30 s although there is no Wi-Fi (the software restart kept the RTC time). **Not** present: `BLE_LEAK: Extended passive scan started (1M + Coded PHY)`, `BLE_VALVE: [SCAN] Starting scan for provisioned valve`, `SETUP COMPLETE - READY FOR GATT`, any `eleak` line, and `Roll-up grace expired` (for the valve too). | nothing | OFF → WHITE (syncing) for the whole window; network LED ramp red | window lines ≤ 1 s after boot | | The valve does not link in this window: its hunt is held from boot. Its 180 s sync excuse is held with the hunt (lead decision C, 2026-09-29 follow-up), so it stays "syncing": a `Roll-up grace expired (180 s) — 1 unheard device(s) now count` line or a RED fleet LED in the window (without a leak) is a FAIL. If `Time synced` does not print, expect `holding event for replay` instead of `Offline — buffering` in A6. |
| A4 | With the phone, join the SoftAP `WiFi-Hub-<SHORT>`. Open the portal (it should pop up; otherwise browse to `http://10.10.0.1`). | `APP_WIFI: SoftAP: station <phone MAC> joined, AID=1`; `esp_netif_lwip: DHCP server assigned IP to a client, IP is: 10.10.0.2` (or `.3` …); `dns_server: Replying to DNS request for %s from %s` **(format)**; `MONITOR` lines continue | nothing | WHITE (A3) | lease ≤ 5 s after the join line | | Heap row "phone joined". **FAIL** if the phone reports "Unable to join", no join line prints, or no lease follows the join (the 2026-09-29 defect). The phone MAC may be a private (random) address. |
| A5 | Refresh the AP list in the portal until the site SSID is listed. | `MONITOR` lines | nothing | — | — | | Heap row "AP list shown". Record whether the list loads first time (the page's own Wi-Fi scans run every 3.8 s: RCA cause 4, managed component, not changed). |
| A6 | Before submitting, wet L. Wait for the close. | `RULES_ENGINE: LEAK INCIDENT latched by lora sensor <L>`; `AUTO-CLOSE + RMLEAK triggered by lora sensor <L>`; `AUTO-CLOSE: valve not connected — scanning; close deferred to reconnect reconciliation`; `BLE_VALVE: [CMD] RMLEAK write not ready. Queuing val=1`; `BLE_VALVE: [PORTAL] Leak response pending - valve hunt runs despite the Wi-Fi setup portal`; `[SCAN] Starting scan for provisioned valve <VALVE>...`; `[CMD] Valve write not ready. Queuing val=0`; `[SCAN] Target MAC matched - connecting to provisioned valve: <VALVE>`; `SETUP COMPLETE - READY FOR GATT`; `[CMD] Applying pending RMLEAK command=1` (or `[CMD] Replaying pending RMLEAK command=1`) **before** `[CMD] Applying pending valve command=0` (or its `Replaying` line); `[DATA] Valve State=0 (CLOSED)`; `Valve reconnected with %d active leak(s) — executing auto-close` **(format)** or `Reconnected with %d active leak(s) — valve already closed + RMLEAK asserted, nothing to do` **(format)**; for each event `TELEMETRY_V2: Offline — buffering %s event` **(format)**. After that, **no** new `[SCAN] Starting scan` while the window stays open. | nothing | RED | CLOSED ≤ 60 s after the wet | | Heap row "leak during portal". The valve stays linked from here on (the window keeps a link that is up). The portal page must still respond (refresh it); record how it behaved while the valve was being hunted. |
| A7 | Dry L and wait for the auto-clear. | `All sensors clear — auto-clear timer started (10s)`; `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`; `BLE_VALVE: [CMD] Writing RMLEAK=0` on the linked valve; **no** `GAP DISCONNECT EVENT` | nothing | RED → YELLOW → WHITE (A-D still syncing) | as T4-02 rows 3-4 | | |
| A8 | Select the site SSID, enter the password, submit. Keep the phone on the portal page. | `APP_WIFI: Connected! IP: %s` **(format)**, then `APP_WIFI: portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops (about %d s)` **(format)** = `60`; `IOTHUB: Initializing SNTP...`; `DPS: Loaded cached assignment from NVS`; `IOTHUB: Connected to Azure IoT Hub!`; the drain: `Replayed [%s]` per event, `Drain complete: %d event(s) published, 0 remaining` **(format)**. Until the SoftAP stops, still **no** `Extended passive scan started` and **no** `eleak` line. About 60 s after `Connected! IP`: `HEALTH_ENGINE: BLE scanning resumed - BLE sensor timeouts restart now (600 s)`; `APP_WIFI: portal priority OFF (AP stopped) - BLE scanning resumed`; within 1 s `BLE_VALVE: [PORTAL] Valve hunt resumed - Wi-Fi setup portal closed` (no scan: the valve is linked); within 0.5 s `BLE_LEAK: Scan resumed - Wi-Fi setup portal closed` and `BLE_LEAK: Extended passive scan started (1M + Coded PHY)`; `eleak <A>` … `<D>` `— leak=0 …` each within about 100 s of `portal priority OFF`; `HEALTH_ENGINE: Boot sync: all devices seen` once A-D are heard (the snapshot gate waited for them). **No** `setup AP still up` warning. **No** Wi-Fi password anywhere (T4-14). | replayed in order, each with a real `ts`: `leak_detected` (L), `auto_close` (`"rmleak_asserted":false`: the valve was not linked when it fired), `valve_state_changed` (closed), `leak_cleared`, `rmleak_auto_cleared`; then `lifecycle` with `"reset_reason":"software"`; then, only after `portal priority OFF` and `Boot sync: all devices seen`, the boot snapshot with A-D `connected:true` (none "syncing"), `valve.state:"closed"`, `valve.rmleak:false`. **No** `device_offline` for A-D. | network LED beat blue → ramp blue; fleet WHITE until A-D are heard, then GREEN | `Connected! IP` ≤ 30 s after the submit; `portal priority OFF (AP stopped)` 60 s (±5 s) after `Connected! IP` | | Heap rows "submitted" and "MQTT up". **The phone must show the portal's success page** ("Connected!", "Your WiFiHub is now on your home network") while the SoftAP is still up: record what it showed and when. The SoftAP stopping may print `SoftAP: station … left …` for the phone. The boot snapshot comes only after the resume (CHANGELOG known limitation). |
| A9 | Watch 2 more minutes. Then send `valve_open`. | `MONITOR` lines; no reboot; **no** second `portal priority OFF`, **no** `portal priority ON`, **no** `setup AP still up` | `cmd_ack` ok; `valve_state_changed` open | GREEN | — | | Heap row "2 min after". |
| A10 | Search the capture from A2 on. | Exactly one boot banner (the intended one in A2). Between `portal priority ON` and `portal priority OFF`: **no** `Extended passive scan started`, and **no** `[SCAN] Starting scan for provisioned valve` except the leak-response hunt of A6 (after `[PORTAL] Leak response pending`). **No** `GAP DISCONNECT EVENT` from A6's `SETUP COMPLETE` to the end. **No** `LOW HEAP WARNING: %lu bytes free (watermark=%d)` **(format)**. | no `device_offline` for A-D anywhere in the capture | — | — | | Record every `Heap dropped %ld bytes since last check` **(format)** line with its time. |
| A11 | Power-cycle the hub. | `Connected! IP: %s` **(format)** without the portal; **no** `portal priority ON` | `lifecycle` `"reset_reason":"power_on"` | GREEN | ≤ 30 s | | The credentials persisted. |

Heap table (fill in from the `MONITOR` lines):

| Point | free | min_ever | largest_blk | uptime |
|---|---|---|---|---|
| A1 baseline (online, before reset) | | | | |
| A3 portal up, scanning paused | | | | |
| A4 phone joined | | | | |
| A5 AP list shown | | | | |
| A6 leak during portal, valve linked | | | | |
| A8 submitted / MQTT up | | | | |
| A9 2 min after | | | | |
| F2 sensors-only hub, page loading | | | | |

Reference figures: 2.1.4 CP1 bench (valve + 4 BLE, normal boot) `min_ever` 19,524 B; 2.1.3 field minimum 2,972 B during commissioning; the 2026-09-29 capture (sensors-only, portal up, scanning) about 50 KB free, `largest_blk` 31,744 B. `min_ever` is a since-boot figure, so the A9 value is the minimum of the whole portal session.

**Part B — valve powered off (in `d9fa9c8` the valve hunt ran continuously here: RCA cause 2)**

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| B1 | Power the valve off at the PSU. Repeat A2-A5. | as A3-A4: the hunt is held, so nothing differs from Part A while the valve is off; **no** `Roll-up grace expired` for the valve | — | WHITE (syncing) for the whole window | lease ≤ 5 s after the join line | | Heap rows as in Part A. |
| B2 | Wet L (the valve is still off). | `LEAK INCIDENT latched by lora sensor <L>`; `AUTO-CLOSE + RMLEAK triggered by lora sensor <L>`; `RULES_ENGINE: AUTO-CLOSE: valve not connected — scanning; close deferred to reconnect reconciliation`; `BLE_VALVE: [CMD] RMLEAK write not ready. Queuing val=1`; `BLE_VALVE: [PORTAL] Leak response pending - valve hunt runs despite the Wi-Fi setup portal`; `[SCAN] Starting scan for provisioned valve <VALVE>...`; `BLE_VALVE: [CMD] Valve write not ready. Queuing val=0`; no `Target MAC matched` (the valve is off) | nothing | RED | hunt ≤ 5 s after the leak lines | | By design the hunt now takes the radio back from the portal: record whether the portal page still responds (observe). |
| B3 | Dry L (the valve is still off). | `All leaks resolved — pending auto-close cancelled`; `BLE_VALVE: [CMD] Pending valve CLOSE cancelled (leak resolved)`; `BLE_VALVE: [CMD] Pending RMLEAK SET cancelled (leak resolved)`; within 1 s `BLE_VALVE: [PORTAL] Valve hunt stopped - Wi-Fi setup portal has the radio`; later the auto-clear, whose RMLEAK=0 pends with `[SCAN] Valve scan held …`. **No** `[SCAN] Starting scan` after the `Valve hunt stopped` line while the window stays open. **No** `Roll-up grace expired` for the valve. | nothing | RED → YELLOW → WHITE (the valve, which the hunt could not reach, and A-D still syncing) | hunt stopped ≤ 2 s after the cancel lines | | The portal page responds again (refresh it). The valve stays held after the leak-response hunt (accepted edge, CHANGELOG known limitation): RED after the auto-clear is a FAIL. |
| B4 | Wet L again, and while it is wet power the valve on. | as B2, then `[SCAN] Target MAC matched - connecting to provisioned valve: <VALVE>`; `SETUP COMPLETE - READY FOR GATT`; `[CMD] Applying pending RMLEAK command=1` (or `[CMD] Replaying pending RMLEAK command=1`) **before** `[CMD] Applying pending valve command=0` (or its `Replaying` line); `[DATA] Valve State=0 (CLOSED)`; after it **no** new `[SCAN] Starting scan` while the window stays open | nothing | RED | CLOSED ≤ 3 s after `SETUP COMPLETE - READY FOR GATT` | | `[CMD] … write: GATT busy - waiting` lines are acceptable. The link stays up for the rest of the window. |
| B5 | Keep L wet. Submit the site credentials (as A8). | as A8 (the valve is already linked) | `leak_detected`, `auto_close` with `"rmleak_asserted":false`, `valve_state_changed` closed with `"rmleak":true` | RED | — | | |
| B6 | Clean up: dry L, wait for `AUTO-CLEAR`, send `valve_open`. | | | GREEN | | | |

**Part C — full offline ring next to the credential save (council NVS-pressure risk): run as T6-14 (merged).** T6-14 fills the offline ring to overflow with the WAN unplugged (including two offline re-wets at the clear), does the Wi-Fi reset and the portal save with the ring full, power-cycles, then checks the replay of all 16 events, the persisted credentials and the valve bond. Record its result here as well.

**Part D — router outage: the fallback portal keeps BLE scanning (the window must NOT open), and the 10 s reset recovers from it**

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| D1 | With the hub online (credentials saved), switch the router off, then EN reset. | `APP_WIFI: WiFi Disconnected. Reason: %d` **(format)** 4 times, about 5 s apart (1 attempt + 3 retries); then `DHCP server started on interface WIFI_AP_DEF`; `APP_WIFI: SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`. **No** `portal priority ON`. `BLE_LEAK: Extended passive scan started (1M + Coded PHY)`, the valve links (`SETUP COMPLETE - READY FOR GATT`), and `eleak` and `[HEARTBEAT]` lines continue throughout. | nothing | GREEN; network LED ramp red | AP about 20-25 s after boot | | A `portal priority ON` line here is a FAIL: it would switch off BLE leak protection during a router outage. |
| D2 | Wet A. Then dry A. | `eleak <A> — leak=1 …` within about 15 s; `LEAK INCIDENT latched by ble_leak_sensor sensor <A>`; `[CMD] Writing RMLEAK=1` before `[CMD] Writing Valve=0`; `[DATA] Valve State=0 (CLOSED)`; after the dry, `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK` | nothing (buffered) | GREEN → RED → YELLOW → GREEN | as T4-02 rows 3-4 | | BLE protection is intact on the fallback AP. |
| D3 | Observe: try to join the fallback SoftAP with the phone and open the portal. | the `SoftAP: station … joined` line, a lease, DNS replies, if the join works | — | — | — | | **Known-limit (observe and record).** BLE keeps scanning here by user decision, so the join may fail as on `d9fa9c8`. Record join / lease / page load. If the page loads, its disconnect button erases nothing here (the Wi-Fi is idle; CHANGELOG known limitation): do not rely on it; D5 is the way out. |
| D4 | Switch the router on. | `APP_WIFI: Connected! IP: %s` **(format)** within about 20 s; **no** `portal priority` line (the window never opened) | the buffered D2 events replay | GREEN | — | | |
| D5 | Switch the router off again and EN reset; wait for `SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on` (as D1). With the STA idle on the fallback AP, hold the reset button 10 s. | the `RESET_BTN` lines of A2 up to `Erasing WiFi credentials, then rebooting into AP (commissioning preserved in nvs_prov)...`; normally **no** `APP_WIFI: WiFi Disconnected. Reason: 8` (the STA is idle); about 2 s later `RESET_BTN: Wi-Fi credentials erased from NVS`, then `RESET_BTN: Rebooting into AP mode...`. After the reboot: `esp_netif_lwip: DHCP server started on interface WIFI_AP_DEF` under 1 s after boot, **no** `APP_WIFI: WiFi Disconnected. Reason: 2NN`, **no** `SoftAP up with saved Wi-Fi credentials`, and the three window-open lines (as A3) | nothing | WHITE (syncing) | window lines ≤ 1 s after boot | | **The reconfiguration lockout, now fixed (user decision 2026-09-29: the 10 s reset always erases).** Before the follow-up the reset kept the credentials here, because an idle STA gets no disconnect event, so the hub came back on the fallback portal. `No Wi-Fi credentials saved - nothing to erase`, or the D1 lines again after the reboot, is a FAIL. |
| D6 | Join the phone to `WiFi-Hub-<SHORT>` and open the portal (as A4). Switch the router on, then submit the site credentials. | as A4 (join, lease, DNS replies); after the submit as A8: `Connected! IP: %s` **(format)**, the `stays paused until the setup AP stops` line, and about 60 s later `portal priority OFF (AP stopped) - BLE scanning resumed`; the valve hunt resumes and the valve links (`[SCAN] Starting scan for provisioned valve`, `SETUP COMPLETE - READY FOR GATT`); `eleak <A>` … `<D>` within about 100 s of `portal priority OFF` | `lifecycle`; the boot snapshot after the resume; **no** `device_offline` | WHITE → GREEN | lease ≤ 5 s after the join line | | The recovery path for a router whose SSID or password changed. The phone shows the success page as in A8. |

**Part E — health hold over a long window (no false offline, fresh timeouts after the resume)**

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| E1 | Take D's battery out (it stays provisioned). Hold the reset button 10 s (as A2). Note the PC time of `portal priority ON`. | as A2-A3 | the MQTT connection drops | as A3 | — | | |
| E2 | Leave the portal up with no credentials for 12 min (longer than the 600 s timeouts). Keep A-C dry. | `MONITOR` lines and `BLE_LEAK: [HEARTBEAT] Scanner alive, whitelist=%d sensors` **(format)** only; no `eleak` line. **No** `Roll-up grace expired` at all (the valve's excuse is held too), **no** `Boot sync: timeout` and **no** `Boot sync: all devices seen` (the snapshot gate waits while BLE sensors and the valve are unheard and not listened to). | nothing | WHITE (syncing), on a valve hub too | — | | A RED fleet LED here is a FAIL (lead decision C). |
| E3 | At about 11 min, wet A and keep it wet. | nothing: A is not heard while scanning is paused | nothing | unchanged | — | | By design a BLE leak during the window is reported in A's first burst after the resume. |
| E4 | Submit the site credentials. Note the times of `Connected! IP` and `portal priority OFF`. | `Connected! IP: %s` **(format)** and the `stays paused until the setup AP stops` line; still no `eleak` line until, about 60 s later, the window-close lines of A8 (`portal priority OFF (AP stopped) - BLE scanning resumed`); `BLE_LEAK: Scan resumed - Wi-Fi setup portal closed`; `eleak <A> — leak=1 …` within about 15 s of `portal priority OFF`, then `LEAK INCIDENT latched by ble_leak_sensor sensor <A>` and the close (RMLEAK first); `eleak <B>`, `<C>` `— leak=0 …` within about 100 s. About 180 s after `portal priority OFF`: `Boot sync: timeout (180 s) — snapshot gate open; unheard devices still excused for a further %lld s` **(format)** = about `420` (D). About 600 s after it: `Roll-up grace expired (600 s) — 1 unheard device(s) now count`, **never earlier**. | event snapshots for A's leak; the boot snapshot at the gate lists A-C `connected:true` and D `connected:false`; **no** `device_offline` for A-C or the valve at any time, live or replayed | RED (leak) | `portal priority OFF` 60 s (±5 s) after `Connected! IP`; gate 180 s (±30 s), grace 600 s (±30 s), both from `portal priority OFF` | | Every timing here counts from `portal priority OFF`, not from `Connected! IP`. On a valve hub whose valve was not linked, the valve hunt runs first and forwards the sensors' reports; the leak scan starts once the valve links. The phone shows the success page before the SoftAP stops (valve hub, valve not linked): record it. |
| E5 | Clean up: put D's battery back, dry A, wait for `AUTO-CLEAR`, send `valve_open`. | `eleak <D>`; `AUTO-CLEAR …` | `valve_state_changed` open | GREEN | | | |

**Part F — sensors-only hub (the unit of the 2026-09-29 capture)**

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| F1 | On a hub with BLE sensors and no valve (`GW-7C4FADAE69C8`: 2 BLE sensors), hold the reset button 10 s. | after the reboot: the window-open lines; `IOTHUB: Starting BLE (valve=none, BLE sensors=2)`; `BLE_LEAK: Scan paused - Wi-Fi setup portal has the radio`; **no** `Extended passive scan started`; no `BLE_VALVE: [PORTAL]` line (no valve) | the MQTT connection drops | WHITE (syncing) | window lines ≤ 1 s after boot | | |
| F2 | Join with the phone and open the portal. | `APP_WIFI: SoftAP: station <phone MAC> joined, AID=1`; `esp_netif_lwip: DHCP server assigned IP to a client, IP is: 10.10.0.2`; `dns_server: Replying to DNS request for …`; the portal page loads | nothing | WHITE | lease ≤ 5 s after the join line | | Heap row "F2". **FAIL** if there is no lease: this step failed on the 2026-09-29 capture. |
| F3 | Submit the site SSID with a wrong password. | `APP_WIFI: WiFi Disconnected. Reason: %d` **(format)**; the portal page reports the failure; **no** `portal priority OFF`; still **no** `Extended passive scan started` | nothing | WHITE | — | | A failed connect must not close the window: BLE stays paused while no Wi-Fi works. |
| F4 | Submit the site credentials. | as A8 without the valve lines: `Connected! IP`, the `stays paused until the setup AP stops` line, about 60 s later `portal priority OFF (AP stopped) - BLE scanning resumed`; each sensor's `eleak … — leak=0 …` within about 100 s of it; `Boot sync: all devices seen` | the boot snapshot lists both sensors `connected:true`; no `device_offline` | GREEN | `portal priority OFF (AP stopped)` 60 s (±5 s) after `Connected! IP` | | The phone must show the success page before the SoftAP stops (this unit could not even join on `d9fa9c8`): record it. |

**Part G (optional) — the portal's disconnect inside the window, then a window opened at runtime with the valve linked**

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| G1 | Within about 30 s of A8's `Connected! IP` (the window is still open and the SoftAP still up), press the portal's disconnect for the connected network. | `APP_WIFI: WiFi Disconnected. Reason: 8`; **no** `portal priority OFF` and no second `portal priority ON` (the window stays open: the credentials are gone again); the leak scan stays paused; **no** `GAP DISCONNECT EVENT` (the valve link is kept); later **no** `setup AP still up` and no `portal priority OFF (AP stopped)` while no credentials are entered | the MQTT connection drops | WHITE (A-D not heard since the reboot) | — | | If the portal page is gone by then, mark G1-G3 `Blocked`. |
| G2 | Wet L. Then dry L and wait for the auto-clear. | `AUTO-CLOSE + RMLEAK triggered by lora sensor <L>`; `[CMD] Writing RMLEAK=1` before `[CMD] Writing Valve=0` on the kept link; `[DATA] Valve State=0 (CLOSED)`; no `[SCAN] Starting scan`; then `AUTO-CLEAR …` | nothing | RED → YELLOW → WHITE | CLOSED ≤ 3 s after the leak line | | |
| G3 | Submit the site credentials again. | `Connected! IP: %s` **(format)** and the `stays paused until the setup AP stops` line; about 60 s later `portal priority OFF (AP stopped) - BLE scanning resumed`; the leak scan resumes; `eleak <A>` … `<D>` within about 100 s | the G2 events replay | WHITE → GREEN | `portal priority OFF` 60 s (±5 s) after `Connected! IP` | | |
| G4 | After G3's `portal priority OFF`, join the phone to the site Wi-Fi, browse to `http://<the IP of G3's Connected! IP>` and press the portal's disconnect. | `APP_WIFI: WiFi Disconnected. Reason: 8`; the window-open lines; `BLE_VALVE: [PORTAL] Valve hunt paused - Wi-Fi setup portal has the radio (valve link kept)`; `BLE_LEAK: Scan paused …`; **no** `GAP DISCONNECT EVENT` | the MQTT connection drops | GREEN (A-D stay online: their timeouts are held) | ≤ 1 s | | A window opened at runtime. If the page cannot be reached on the STA address, mark G4-G5 `Blocked`. |
| G5 | Join `WiFi-Hub-<SHORT>` again, submit the site credentials; afterwards send `valve_open`. | as G3 | `valve_state_changed` open | GREEN | | | |

**Part H — Wi-Fi lost before the setup AP stops, and a valve that drops in the window (valve hub, about 15 min)**

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| H1 | Hold the reset button 10 s (as A2). After the reboot, wet L and wait for the close (as A6: the leak hunt links the valve); then dry L and wait for the auto-clear (as A7). | as A2-A3, A6 and A7 | nothing | WHITE → RED → YELLOW → WHITE | as A6, A7 | | The valve is now linked inside the window. |
| H2 | Power the valve off at the PSU. Wait 4 min. | `GAP DISCONNECT EVENT`; `BLE_VALVE: [SCAN] Valve scan held - Wi-Fi setup portal has the radio`; **no** `[SCAN] Starting scan`; **no** `Roll-up grace expired` | nothing | YELLOW ("Valve disconnected") for the whole 4 min, never RED | — | | Without the valve hold its 180 s grace would expire here and the hub would read critical (RED). RED is a FAIL (lead decision C). |
| H3 | Power the valve on (it stays unlinked: the hunt is held). Submit the site credentials; within about 30 s of `Connected! IP`, switch the router's 2.4 GHz network off. | `Connected! IP: %s` **(format)** and the `stays paused until the setup AP stops` line; then `APP_WIFI: WiFi Disconnected. Reason: %d` **(format)**; `HEALTH_ENGINE: BLE scanning resumed - BLE sensor timeouts restart now (600 s)`; `APP_WIFI: portal priority OFF (Wi-Fi lost after setup) - BLE scanning resumed`; within 1 s `[PORTAL] Valve hunt resumed - Wi-Fi setup portal closed`, then `[SCAN] Starting scan for provisioned valve`, `SETUP COMPLETE - READY FOR GATT`; `BLE_LEAK: Scan resumed - Wi-Fi setup portal closed`, and `Extended passive scan started (1M + Coded PHY)` once the valve has linked; `eleak <A>` … `<D>` within about 100 s. `WiFi Disconnected` repeats every few seconds while the router is off (the STA retries every 5 s). **No** `portal priority ON` and **no** `SoftAP up with saved Wi-Fi credentials` (the setup SoftAP stays up as the fallback portal). | the MQTT connection drops | YELLOW → GREEN once the valve and A-D are heard | `portal priority OFF` ≤ 1 s after the first `WiFi Disconnected` | | If `portal priority OFF (AP stopped)` prints first, the router dropped too late: repeat H1-H3 faster. |
| H4 | Switch the router on. | `APP_WIFI: Connected! IP: %s` **(format)** within about 20 s, with **no** `portal priority` line (the window is closed); no `portal priority OFF` when the SoftAP stops about 60 s later | `lifecycle`; snapshots; **no** `device_offline` for the valve or A-D | GREEN | — | | |

**Pass:** Parts A, B, E, F and H pass, and D1, D2, D4, D5 and D6 pass. D3 is observe-and-record (`Known-limit`). Part G is optional. The FAIL conditions are: any reboot other than the intended ones; a phone that cannot join and get a lease within 5 s in a no-credentials portal (A4, B1, D6, F2); a portal that does not load or does not save; a phone that does not show the success page while the SoftAP is still up after a save (A8, F4); an `Extended passive scan started` or a `[SCAN] Starting scan` inside the window other than the leak-response hunt (the window includes the minute between `Connected! IP` and `portal priority OFF`); a `portal priority OFF` at `Connected! IP`, or a window that stays open more than about 80 s after it with Wi-Fi up; a failed connect that closes the window (F3); a `portal priority ON` on the router-fallback AP (D1, H3); a 10 s reset that does not print `Wi-Fi credentials erased from NVS`, or that brings the hub back on the fallback portal (A2, D5); the valve not closing for a leak during the window while it is powered (A6, B4); a RED fleet LED or a `Roll-up grace expired` for the valve during the window without a leak (A3, B1, B3, E2, H2); a `device_offline` for a BLE sensor within 600 s, or for the valve within 180 s, after `portal priority OFF`; the STA not connecting; or the credentials not persisting. The heap figures are recorded. A `LOW HEAP WARNING`, a `min_ever` below 8,192 B or a `largest_blk` below 7,168 B at any point is a finding to raise with the heap results, not an automatic fail of this test. Part C is T6-14.

---

### T4-11 — Restart during a leak (F-02 accepted; F-01 route b) (about 40 min)

**Purpose:** Part A records the accepted F-02 behaviour. A hub restart empties the list of wet sources, so the first dry report after the restart starts the 10 s auto-clear before a still-wet sensor is heard again, and RMLEAK can be released for up to one wet burst (about 15 s for BLE, minutes for LoRa). The valve must stay closed unless someone opens it, and the interlock must re-latch when the wet sensor is heard. This is **observe and record**, with hard safety criteria. Part B (run as T3-13) checks the council's F-01 route (b): a hub power-cycled during an incident whose sensors dried while it was off must release the latch exactly once, with no false button override.

**Preconditions:** the common start state, router on.

**Part A — sensor still wet across the restart (F-02)**

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| A1 | Wet A. Wait for the close. | `LEAK INCIDENT latched …`; `AUTO-CLOSE + RMLEAK triggered …`; `[DATA] RMLEAK=1 (ACTIVE)`; `[DATA] Valve State=0 (CLOSED)` | `leak_detected`, `auto_close` (`rmleak_asserted:true`), `valve_state_changed` closed | RED | as T4-02 row 3 | | |
| A2 | Keep A wet. Power-cycle the hub. Watch UART from boot to +90 s and IoT Hub for 2 min. | Boot: `RULES_ENGINE: NVS: restored incident latch — pending reconcile with valve`. Then **one of** (record which): **(a)** A is heard before any auto-clear: `eleak <A> — leak=1 …`; if the valve is not ready yet, `AUTO-CLOSE + RMLEAK triggered …` and `AUTO-CLOSE: valve not connected — scanning; close deferred to reconnect reconciliation`, then at the link `Reconnected with %d active leak(s) — valve already closed + RMLEAK asserted, nothing to do` **(format)**; if the valve is ready first, `Reconnected: hub + valve RMLEAK in sync`, possibly `All sensors clear — auto-clear timer started (10s)` (from the valve's dry flood-probe announcement), and nothing more when A is heard within those 10 s (no `AUTO-CLEAR`). **(b)** The auto-clear fires first: `All sensors clear — auto-clear timer started (10s)` (from another sensor's first dry report or after the valve's `[READY] Announcing flood probe state (dry) for reconciliation`), then `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`, `[CMD] Writing RMLEAK=0` (or pended), `[DATA] RMLEAK=0 (CLEAR)`; the valve stays CLOSED; then when A is heard: `LEAK INCIDENT latched by ble_leak_sensor sensor <A>`, `AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <A>`, `[CMD] Writing RMLEAK=1`, `[DATA] RMLEAK=1 (ACTIVE)`. | (a): `leak_detected` (A), possibly `auto_close` (`rmleak_asserted:false`). (b): `rmleak_auto_cleared`, then `leak_detected` (A), then `auto_close` (`rmleak_asserted:true`). In both, **no** `water_access_override_enabled`. | YELLOW (restored latch) → RED when A is heard; in (b) YELLOW or GREEN in the gap, then RED | (b): gap from `AUTO-CLEAR` to `LEAK INCIDENT latched` ≤ 15 s (one BLE wet burst) | | Record the branch and the gap. Events built before the clock sync are held and stamped, which is fine. |
| A3 | As soon as MQTT is connected, send `{"schema":"eflostop.cmd","ver":1,"id":"t4-11-open-1","cmd":"valve_open"}`. Note whether an `AUTO-CLEAR` line has printed since boot and whether A has re-latched. | `IOTHUB: C2D cmd='valve_open' ver=1 id='t4-11-open-1'`; `IOTHUB: Command: VALVE_OPEN`; while the incident is latched: `IOTHUB: VALVE_OPEN refused — Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.` | while latched: `cmd_ack` error, below. In the (b) gap only: `cmd_ack` ok (accepted, F-02); the valve opens, and it is closed again at the re-latch. | — | ack ≤ 2 s | | An `ok` outside the gap is a FAIL. Record the result and the time. |
| A4 | Repeat A1-A3 three times in all (five recommended). If L is available, repeat once with L wet instead of A (its gap can be minutes). | as above | as above | | | | Table: run / branch / gap / valve_open result. |
| A5 | Clean up: dry A, wait for `AUTO-CLEAR`, send `valve_open`. | | | GREEN | | | |

`cmd_ack` for the refusal (`data`):

```json
{"event":"cmd_ack","id":"t4-11-open-1","cmd":"valve_open","status":"error","error":{"code":"valve_open","detail":"Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."}}
```

Expected snapshot once A has re-latched (A wet, valve locked):

```json
{
  "schema": "eflostop.v2",
  "ts": 1785399200,
  "gateway": {"id": "<GW>", "short_id": "<SHORT>", "name": "Bench Hub", "fw": "2.1.4", "uptime_s": 75},
  "type": "snapshot",
  "data": {
    "reason": "event",
    "system_health": {"rating": "critical", "reason": "Leak detected: <A_LABEL>, Leak interlock latched"},
    "valve": {
      "valve_id": "<VALVE>",
      "state": "closed",
      "battery": 92,
      "leak_state": false,
      "rmleak": true,
      "connected": true,
      "fw_version": "2.2.0",
      "rating": "excellent",
      "last_seen_age_s": 0
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {"sensor_id": "<A>", "connected": true, "rating": "critical", "last_seen_age_s": 4, "battery": 87, "rssi": -64, "leak_state": true, "fw_version": "1.1.0", "location": {"code": "kitchen", "label": "<A_LABEL>"}},
      {"sensor_id": "<B>", "connected": true, "rating": "excellent", "last_seen_age_s": 30, "battery": 64, "rssi": -70, "leak_state": false, "fw_version": "1.1.0", "location": {"code": "laundry", "label": "<B_LABEL>"}}
    ],
    "rules": {"auto_close_enabled": true, "trigger_mask": 7},
    "override_active": false
  }
}
```

A sensor not yet heard since the restart shows `null` fields and adds a ", syncing N device(s)" part to the reason while its window is open; record it.

**Part B — sensors dried while the hub was off (F-01 route b with F-02): run as T3-13 (merged).** T3-13 is the same procedure: latch, unplug the hub, dry the sensor, plug the hub in, exactly one `AUTO-CLEAR`, no `RMLEAK cleared externally`, no `OVERRIDE WINDOW STARTED`, no `re-latching incident`, 5 runs with at least one outcome B (`AUTO-CLEAR` before the valve's `[READY]`, forced by shielding the valve for the first 20-60 s).

**Pass:** Part A: in every run the valve reports OPEN only after an accepted `valve_open` in a (b) gap, the valve ends CLOSED with RMLEAK set while A is wet, no override window starts, and the branches and gaps are recorded. Part B: see T3-13.

---

### T4-12 — F-03 known limitation: a leak latched while the valve was unreachable, then a hub restart (observe and record; about 30 min)

**Purpose:** document the deferred F-03 behaviour (CHANGELOG, Known limitations: "A leak latched while the valve was out of reach can still be read as a button press at the reconnect"; the SRS §4.4.2 cross-reboot inference, unchanged from 2.1.3). Record whether a `trigger:"button"` window starts. It also checks the E-06 user decision: `valve_open` is refused while the incident is latched, even with the valve disconnected.

**Preconditions:** the common start state, router on. Valve open, RMLEAK clear, no incident. There are two ways to make the valve unreachable: PSU output off, or an RF shield. The valve never receives the lock in this test, so either works; record which you used.

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Make the valve unreachable. Wait for the `GAP DISCONNECT EVENT` banner. | `GAP DISCONNECT EVENT` | a `valve_unlinked` snapshot | YELLOW (valve disconnected) | | | |
| 2 | Wet A (or L). | `LEAK INCIDENT latched by %s sensor %s` **(format)**; `AUTO-CLOSE + RMLEAK triggered by %s sensor %s` **(format)**; `RULES_ENGINE: AUTO-CLOSE: valve not connected — scanning; close deferred to reconnect reconciliation`; `BLE_VALVE: [CMD] RMLEAK write not ready. Queuing val=1`; `BLE_VALVE: [CMD] Valve write not ready. Queuing val=0` | `leak_detected`; `auto_close` with `"rmleak_asserted":false` | RED | | | |
| 3 | Send `{"schema":"eflostop.cmd","ver":1,"id":"t4-12-open-1","cmd":"valve_open"}` | `VALVE_OPEN refused — Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.` | `cmd_ack` error with that detail (as in T4-11) | RED | ≤ 2 s | | E-06: refused although the valve is disconnected and its RMLEAK cannot be read. |
| 4 | **Variant 1 — dried while the hub is off.** Unplug the hub. Dry A. Make the valve reachable again (PSU on, or remove the shield). Plug the hub in. | `NVS: restored incident latch — pending reconcile with valve`; at the link, the reconciliation box with `║ Valve: state=OPEN rmleak=CLEAR flood=OK` and `║ Hub:   incident=%d leaks=%d override=%s` **(format)** (`incident=1` in (i), `incident=0` in (ii), `leaks=0`, `override=INACTIVE`), then **one of** (record which): **(i)** `RULES_ENGINE: Reconnected: hub incident + valve open + RMLEAK clear — inferring physical override, starting 24h window`, `NVS: override state=1 expiry=%ld saved` **(format)**, `OVERRIDE WINDOW STARTED: auto-close blocked for 24h (expiry=%ld)` **(format)**: the F-03 limitation; **(ii)** an earlier `All sensors clear — auto-clear timer started (10s)` and `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK` (a dry report came 10 s or more before the link), then `Reconnected: no active incident, valve clear`. | (i): `water_access_override_enabled` with `"trigger":"button"` and `"remaining_s":86400`; `expires_ts` is present if the clock had synced before the link, and absent if not (then `Override window re-based to the synced clock …` follows the sync, as in T4-07). (ii): `rmleak_auto_cleared`. | (i): GREEN (window, no leak). (ii): YELLOW → GREEN | | | Record the branch. Clean up (i) with `override_cancel`. |
| 5 | **Variant 2 — still wet across the restart** (use L if available: its next packet is minutes away, which makes branch (ii) below likely). Repeat steps 1-3. Unplug the hub, keep the sensor wet, make the valve reachable, plug the hub in. | **One of** (record which): **(i)** the sensor is heard first: `AUTO-CLOSE + RMLEAK triggered …`, `AUTO-CLOSE: valve not connected — scanning; …`, then at the link `Valve reconnected with %d active leak(s) — executing auto-close` **(format)**, and the valve closes (protected). **(ii)** The valve links first: `Reconnected: hub incident + valve open + RMLEAK clear — inferring physical override, starting 24h window`, then when the sensor is heard: `LEAK INCIDENT latched …` and `Override active — auto-close BLOCKED for %s sensor %s (remaining=%lds)` **(format)**; the valve stays OPEN during the leak: the F-03 limitation. | (i): `leak_detected`, `auto_close`, `valve_state_changed` closed. (ii): `water_access_override_enabled` (`trigger:"button"`), `leak_detected`, `auto_close_blocked_override`. | RED | | | In (ii), recover at once with `{"schema":"eflostop.cmd","ver":1,"id":"t4-12-ovc","cmd":"override_cancel"}`: expect `Override cancelled with %d active leak(s) — executing auto-close` **(format)**, `OVERRIDE WINDOW CANCELLED (remaining_s=%ld)` **(format)**, `auto_close_reenabled`, and the valve closing. |
| 6 | Repeat variants 1 and 2 three times each. Clean up: dry the sensor, wait for `AUTO-CLEAR` (or send `leak_reset`), then `valve_open`. | | | GREEN | | | Table: run / variant / branch / time from boot to the link / time from boot to the first sensor report. |

**Pass:** row 3 passes (E-06). Rows 4-6 are a known limitation: they pass when every run matches one of the listed branches, with no crash and no other state (for example the valve opening by itself, or a window with no `water_access_override_enabled`). The branch counts go to the release notes and to the 2.1.5 F-03 work.

---

### T4-13 — N4: packets from sensors that are not provisioned do not reach the rules engine (optional; needs a neighbour sensor; about 10 min)

**Purpose:** N4. The LoRa key is shared, so a crypto-valid packet from a neighbour's LoRa sensor used to reach `rules_engine_evaluate_leak()` before the provisioning check, and could close this hub's valve. It is now checked against provisioning first. A neighbour BLE sensor is filtered by the scanner's whitelist.

**Preconditions:** the common start state. Neighbour LoRa sensor N (same key, not provisioned here) and/or neighbour BLE sensor X (not provisioned here), both dry and in range. For the control row, provisioned LoRa sensor L.

| # | Action | Expected UART | Expected IoT Hub | LED | Tolerance | P/F | Notes |
|---|---|---|---|---|---|---|---|
| 1 | Wet N (or trigger its leak packet). | `APP_LORA: Verified: ID=0x%lX, Batt=%d%%, Leak=0x%X, Sent=%u, Ack=%u` **(format)** with N's ID; `IOTHUB: Event: LoRa Packet from 0x%08lX` **(format)**; `IOTHUB: Sensor 0x%08lX not provisioned, skipping` **(format)**. **No** `LEAK INCIDENT latched`, **no** `AUTO-CLOSE`. | nothing about N; no `leak_detected`, no `auto_close`; snapshots unchanged | GREEN | | | The valve stays OPEN. |
| 2 | Wet X. | **No** `eleak …` line with X's MAC; nothing else | nothing | GREEN | 1 min | | |
| 3 | Control: wet L. | `Event: LoRa Packet from 0x%08lX` **(format)** with L's ID; `LEAK INCIDENT latched by lora sensor 0x%08lX`; `AUTO-CLOSE + RMLEAK triggered by lora sensor 0x%08lX`; the valve closes | `leak_detected` (`"source_type":"lora"`), `auto_close` | RED | | | Proves the path works for a provisioned sensor. |
| 4 | Clean up: dry N, X and L. When L reports dry, wait for `AUTO-CLEAR`, then `valve_open`. | | | GREEN | L's next packet may take minutes | | |

**Pass:** rows 1-3 pass (row 1 or row 2 alone if only one neighbour sensor is available; record which).

---

### T4-14 — Serial-log hygiene: no Wi-Fi password and no valve passkey on UART (about 5 min, after T4-01 and T4-10)

**Purpose:** the CHANGELOG fix "The UART log no longer prints the site Wi-Fi password … nor the valve's fixed BLE passkey". The Wi-Fi manager component logged the password at INFO on every boot and on the portal save; `main.c` now caps its `wifi_manager` and `http_server` tags at WARN.

**Preconditions:** the UART captures of T4-01 (a boot with stored credentials), T4-10 (a portal save) and any valve pairing run in the campaign. The site Wi-Fi password. The passkey value is the macro `BLE_VALVE_FIXED_PASSKEY` in `main/ble_valve/app_ble_valve.c`; do not copy it into the report.

| # | Action | Expected | P/F | Notes |
|---|---|---|---|---|
| 1 | `Select-String -Path .\uart_T4-*.txt -SimpleMatch -Pattern '<site Wi-Fi password>'` | 0 matches | | |
| 2 | `Select-String -Path .\uart_T4-*.txt -Pattern 'password'` | 0 matches from the `wifi_manager` or `http_server` tags. Any other match is inspected; a match containing the password is a FAIL. | | |
| 3 | `Select-String -Path .\uart_T4-*.txt -SimpleMatch -Pattern '<passkey digits>'` | 0 matches | | |
| 4 | `Select-String -Path .\uart_T4-*.txt -Pattern 'Fixed Passkey'` | `BLE_VALVE: [SM] Fixed Passkey: configured (not logged)` once per BLE start. A pairing prints `[PASSKEY] INPUT required. Responding with the fixed passkey` or `[PASSKEY] DISPLAY action. Responding with the fixed passkey`, with no digits. | | |

**Pass:** rows 1-4 pass.

---

### 4.x Coverage of this section

| Test | Covers |
|---|---|
| T4-01 | N1 (start order), N2/N3 (QueueSet complete and checked before BLE), HANDOFF §7 "Boot order", the INFO first-target flush line, P0-b (optional sensors-only variant) |
| T4-02 | S20, N1 (protection with no Wi-Fi and no clock), the pre-sync hold |
| T4-03 | N1 pre-sync stamping and replay order, E-22, council: pre-sync event stamping (`ts` correct, in order, never below 1704067200) |
| T4-04 | N1 accepted drop, council: pre-sync events and a restart before the sync |
| T4-05 | N1 (SNTP out of the boot path, 120 s fallback, 60 s re-poll), HANDOFF §7 "NTP blocked" |
| T4-06 | N1 (DPS in-loop backoff and 5 min retry), known limitation: a live DPS registration blocks the loop for up to 60 s |
| T4-07 | the pre-sync override fix (uptime timing, no `expires_ts`, re-base), the R10 wire shape |
| T4-08 | pre-sync override expiry after 24 h of uptime with no internet (RMLEAK before CLOSE) |
| T4-09 | E-03 (window restored after a power-on with no internet), council: override restored across a power-on; known limitation E-03 case B (an unsynced window restored after a reset ends at the sync) |
| T4-10 | S21, E-21, council: BLE beside the captive portal (heap, coexistence); the 2026-09-29 portal priority window (Parts A, B, D-H) and its follow-up (the close at the setup AP stop, the valve hold, the 10 s reset during a router outage: A8, D5-D6, F3-F4, H); council: NVS pressure with a full offline ring (Part C); Part C is run as T6-14; T6-13 is run as this test |
| T4-11 | F-02 (accepted, observe and record), council: F-02 on the wire; council: F-01 route (b) with F-02 (Part B); Part A is also T3-21; Part B is run as T3-13 |
| T4-12 | F-03 (deferred, observe and record), E-06 (`valve_open` refused while latched and disconnected); T3-20 is run as this test |
| T4-13 | N4 |
| T4-14 | the CHANGELOG log-hygiene fix (Wi-Fi password, BLE passkey) |

**Smoke subset from this section:** T4-01, T4-02 and T4-03 (about 20 min together; T4-03 runs straight on from T4-02).

## 5. Regression of 2.1.3 behaviour and new rules behaviour (plan S24)

This section checks that the 2.1.3 cloud and UI behaviour still works on 2.1.4. It also covers the rules behaviour that 2.1.4 changed:

- the 10 s RMLEAK auto-clear;
- the tick event published before the pass's own leak handling (F-08);
- a re-wet at the moment of the clear re-locking the valve;
- the hub's own clear no longer being read as a button press;
- `valve_open` refused while an incident is latched.

Every expected log line and JSON shape below was checked against the firmware at `d9fa9c8` (`main/`).

### 5.0 Conventions for this section

**Placeholders.** Replace them before you send anything.

| Placeholder | Meaning |
|---|---|
| `<V>` | The provisioned valve's MAC, in upper case, for example `00:80:E1:27:F7:BB` |
| `<A>` `<B>` `<C>` `<D>` | The four BLE leak sensor MACs, in upper case. The hub prints every outbound id in upper case. |
| `<L1>` | A LoRa sensor id, in the form `0x1A2B3C4D` (8 upper-case hex digits) |
| `<GW>` | The gateway id, `GW-XXXXXXXXXXXX` |

Sensor labels are set by T5-01's `provision`: `<A>` "Sink" (kitchen), `<B>` "Ensuite" (bathroom), `<C>` "Washer" (laundry), `<D>` "Water heater" (utility). If a test runs without them, the reason strings show the MAC in place of the label.

**Sending C2D.** In VS Code, open the Azure IoT Hub view and right-click the hub device. Choose **Send C2D Message to Device** and paste the JSON exactly as shown. Use a new `id` for every send: the hub does not de-duplicate ids, and a new id keeps the acks readable. A well-formed envelope is safe; the legacy keyword scan runs only on a message that fails to parse (0.8). Never send a malformed or wrong-schema message that contains a legacy keyword, and never put `DECOMMISSION_ALL` (or any other legacy keyword, 0.8) in an `id`, label or name field (known hazard).

**Watching D2C.** Start **Start Monitoring Built-in Event Endpoint** before each test. Keep it running for the whole section, then save the whole OUTPUT panel to one file for T5-14.

**UART lines.** Each line is quoted as `<level> <TAG>: <text>`. The ESP-IDF timestamp `(ms)` is left out. The level is `I`, `W` or `E`.

- A line marked **(format)** has printf fields. Match the fixed text, and read the field values from the capture.
- Lines from different tags can interleave: `BLE_VALVE` runs on its own task at the same priority as `IOTHUB`. The order this section requires is stated explicitly wherever it matters.
- Every event the hub publishes also prints `I TELEMETRY_V2: Pub event: {...}`, with the full JSON. Snapshots print `Pub snapshot:` and lifecycle messages print `Pub lifecycle:`.
- Events replayed from the offline buffer do **not** print `Pub event:`; they print `I OFFLINE_BUF: Replayed [ob_NN] (N bytes)`.

**Fleet LED (GPIO 48).** This comes from `main/rgb/fleet_led.c`. Every change prints `I FLEET_LED: rating=<r> color=<C> effect=SOLID` **(format)**.

| Hub state | `<r>` | Colour |
|---|---|---|
| Nothing provisioned | `unprovisioned` | WHITE |
| Rating critical (a leak anywhere, or a device offline) | `critical` | RED |
| Rating warning (for example the hub holding the valve after a leak: "Leak interlock latched") | `warning` | YELLOW (it looks amber) |
| Provisioned devices not yet heard, rating better than warning | `syncing` | WHITE |
| Rating excellent or good | `excellent` or `good` | GREEN |

**Timing constants these tolerances come from** (`d9fa9c8`):

| Constant | Value |
|---|---|
| `AUTO_CLEAR_TIMEOUT_MS` | 10 s |
| Loop poll while an auto-clear is pending | 2 s, otherwise up to 30 s idle |
| `RMLEAK_GRACE_PERIOD_MS` | 5 s |
| `AUTO_CLOSE_COOLDOWN_MS` | 10 s |
| `OVERRIDE_BLOCKED_COOLDOWN_MS` | 60 s |
| `OVERRIDE_CONNECT_TIMEOUT_MS` | 10 s |
| `OVERRIDE_WINDOW_DURATION_S` | 86400 s; a bench build can override it with `-D` |
| `SNAP_HIGH_WINDOW_MS` / `SNAP_LOW_WINDOW_MS` | 300 ms / 2 s |
| `SNAP_MIN_INTERVAL_MS` | 5 s between event and heartbeat snapshots |
| `PROV_PULSE_PERIOD_MS` / `PROV_PULSE_WINDOW_MS` / `PROV_PULSE_MAX_SNAPS` | 30 s / 300 s / 40 |
| `HEALTH_COMMISSION_SYNC_TIMEOUT_MS` | 150 s |
| `COMMISSION_REFRESH_GRACE_MS` | 6 min |
| Unheard sensor excuse (`HEALTH_ROLLUP_UNHEARD_MS`) | 600 s |
| BLE scanner whitelist reload | 10 s |
| Scanner telemetry heartbeat | 5 min |
| Snapshot interval | 60–3600 s, default 300 s |

Other timings:

- BLE leak sensors burst about every 15 s while wet and about every 100 s while dry.
- A wet or dry change is normally reported within a few seconds. The pass windows are those of 0.11: a wet edge within 20 s, a dry edge within 110 s (one dry burst + 10 s). Time everything after a dry edge (auto-clear, LED, events) from the hub's `leak=0` line.
- On the bench a valve ATT round trip takes about 0.45–0.75 s.

**Common preconditions**, unless a test says otherwise:

- The hub runs 2.1.4 (`d9fa9c8`). The boot banner and `gateway.fw` read `2.1.4`.
- It is online with the clock synced.
- It is provisioned with `<V>` and `<A>`..`<D>`, with `auto_close_enabled:true` and `trigger_mask:7`.
- Every device has been heard, every sensor is dry, and the valve is linked and OPEN with RMLEAK clear. There is no override window.
- The last snapshot reads `"rating":"excellent","reason":"All devices healthy"` (or `good`), and the LED is GREEN.
- The valve's bench PSU is at the nominal pack voltage, well above 5.45 V, so the valve battery reads above 20 % and the valve rates `excellent`.

**Result recording.** Every test ends with a Pass/Fail table. A known limitation that the test asks you to observe is recorded, not failed.

---

### T5-01 — Provision: per-advertisement snapshots, and "Syncing - waiting for N devices" clearing as each device is heard

**Smoke:** no (about 10 min). **Covers:** S1 (UI sync preserved), BUG-2 (survivors keep their data), E-10, twin refresh on device-set change.

**Purpose.** A `provision` that adds devices must still give the 2.1.3 installer experience. The command snapshot names the new devices as syncing. Every sensor advertisement triggers a snapshot for 5 minutes. The reason counts down as each new device is first heard. Devices that were already heard keep all their data.

**Start state.** Common preconditions. No `provision` in the last 6 min. Sensor labels do not need to be set yet.

**Steps**

1. Remove `<C>` and `<D>`, one at a time, about 10 s apart:
   ```json
   {"schema":"eflostop.cmd","ver":1,"id":"t5-01-dec-c","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<C>"}}
   ```
   ```json
   {"schema":"eflostop.cmd","ver":1,"id":"t5-01-dec-d","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<D>"}}
   ```
   - UART, for each removal:
     - `W IOTHUB: !!! DECOMMISSION_BLE: <C> !!!` **(format)**
     - `I HEALTH_ENGINE: Device table loaded: 4 device(s) (+0 added, -1 removed)`; after the second removal, `3 device(s) (+0 added, -1 removed)`
     - `I IOTHUB: Telemetry caches purged: 0 LoRa, 1 BLE (no longer provisioned)` **(format)**. This prints only when that sensor had a cache entry.
     - `I IOTHUB: Twin reported (N): {...}`, with `"ble_leak_sensor_count":3` and then `2`
     - `I IOTHUB: SNAP trigger=event:decommission`
   - Must **not** appear: `PROV pulse armed`, `Commission: fast snapshot armed`, and, 150 s later, `Boot sync: timeout`.
   - IoT Hub: `cmd_ack` `ok` for each, then one `event` snapshot per removal. The survivors `<A>` and `<B>` keep their `battery`, `rssi`, `fw_version` and `last_seen_age_s`. None of them turns null.
   - LED stays GREEN.
2. Wait 2 min. Keep `<C>` and `<D>` powered, so they keep advertising.
3. Send the full provision, with labels. Leave `auto_close_enabled` out, so the trigger mask is not re-armed:
   ```json
   {
     "schema": "eflostop.cmd", "ver": 1, "id": "t5-01-prov", "cmd": "provision",
     "payload": {
       "valve_id": "<V>",
       "ble_leak_sensors": ["<A>", "<B>", "<C>", "<D>"],
       "sensor_meta": [
         {"sensor_type": "ble_leak_sensor", "sensor_id": "<A>", "location_code": "kitchen",  "label": "Sink"},
         {"sensor_type": "ble_leak_sensor", "sensor_id": "<B>", "location_code": "bathroom", "label": "Ensuite"},
         {"sensor_type": "ble_leak_sensor", "sensor_id": "<C>", "location_code": "laundry",  "label": "Washer"},
         {"sensor_type": "ble_leak_sensor", "sensor_id": "<D>", "location_code": "utility",  "label": "Water heater"}
       ]
     }
   }
   ```
4. Watch the UART, the IoT Hub monitor and the LED until both `<C>` and `<D>` have been heard, then for the rest of the 5 min pulse window.

**Expected UART** after step 3, in this order for the lines of one tag:

- `I IOTHUB: C2D cmd='provision' ver=1 id='t5-01-prov'`
- `I IOTHUB: Provisioning JSON detected`
- `I PROVISIONING: BLE Leak Sensor[2]: <C>` and `BLE Leak Sensor[3]: <D>` **(format)**
- `I PROVISIONING: Provisioning completed successfully!`
- `I IOTHUB: Applying provisioned valve MAC: <V>` **(format)**, then `I BLE_VALVE: [API] Target MAC set to: <V>` **(format)**. There must be **no** `[CMD] Flushed queued/pending valve commands`, because the target is unchanged.
- `I IOTHUB: Starting BLE (valve=<V>, BLE sensors=4)` **(format)**
- `I IOTHUB: Provision: applied 4 inline sensor_meta entry(ies)` **(format)**
- `I BLE_VALVE: [TASK] CMD: CONNECT`, then `W BLE_VALVE: [SCAN] Already connected` (E-09: a provision always requests the link)
- `I HEALTH_ENGINE: Device table loaded: 5 device(s) (+2 added, -0 removed)`
- `I BLE_LEAK: Sensor tracking reset`
- `I IOTHUB: Commission: fast snapshot armed (all-devices-seen, else <=150s; refreshes on late devices)`
- `I IOTHUB: Twin reported (N): {...}` **(format)**, with `"ble_leak_sensor_count":4`
- `I IOTHUB: PROV pulse armed: every 30 s for 300 s, plus on every sensor packet`
- `I IOTHUB: SNAP trigger=event:provision`

Then, over the next minutes:

- `I BLE_LEAK: Whitelist reloaded: 4 sensor(s)`, within 10 s of the provision.
- Because of the tracking reset, each sensor re-emits once: `I BLE_LEAK: eleak <X> — leak=0 batt=NN% rssi=-NN fw=...` **(format)** and `I IOTHUB: Event: BLE Leak <X> leak=0 batt=NN` **(format)**. These are expected. They produce no `leak_*` event, because the cache is unchanged.
- `I IOTHUB: SNAP trigger=event:prov_pkt` after sensor check-ins, and `I IOTHUB: SNAP trigger=event:prov_pulse` about every 30 s. `SNAP clamped by min-interval: +N ms` **(format)** is normal.
- When the last new device is first heard: `I HEALTH_ENGINE: Boot sync: all devices seen`, then `I IOTHUB: SNAP trigger=boot`.
  - If one of them has not been heard 150 s after the provision, you get `W HEALTH_ENGINE: Boot sync: timeout (150 s) — snapshot gate open; unheard devices still excused for a further N s` **(format)** and `SNAP trigger=boot` instead.
  - The late device then gives `I IOTHUB: SNAP trigger=commission` when it is heard.
- At about 300 s: `I IOTHUB: PROV pulse window closed (N snapshot(s) requested)` **(format)**, with N ≤ 40. `W IOTHUB: PROV pulse capped at 40 snapshots — suppressing the rest of the window` is allowed only if more than 40 were requested.

**Expected IoT Hub**

1. `cmd_ack` `{"event":"cmd_ack","id":"t5-01-prov","cmd":"provision","status":"ok"}`.
2. The command snapshot, within about 5.5 s of the ack. It is built from the reconciled table (E-10), so it already lists `<C>` and `<D>`. Full shape:
   ```json
   {
     "schema": "eflostop.v2",
     "ts": 1790000000,
     "gateway": {"id": "<GW>", "short_id": "<last 4>", "fw": "2.1.4", "uptime_s": 5000},
     "type": "snapshot",
     "data": {
       "reason": "event",
       "system_health": {"rating": "excellent", "reason": "Syncing - waiting for 2 devices"},
       "valve": {"valve_id": "<V>", "state": "open", "battery": 92, "leak_state": false, "rmleak": false,
                 "connected": true, "fw_version": "<valve fw>", "rating": "excellent", "last_seen_age_s": 0},
       "lora_sensors": [],
       "ble_leak_sensors": [
         {"sensor_id": "<A>", "connected": true, "rating": "excellent", "last_seen_age_s": 40, "battery": 87, "rssi": -64,
          "leak_state": false, "fw_version": "<fw>", "location": {"code": "kitchen", "label": "Sink"}},
         {"sensor_id": "<B>", "connected": true, "rating": "excellent", "last_seen_age_s": 71, "battery": 80, "rssi": -70,
          "leak_state": false, "fw_version": "<fw>", "location": {"code": "bathroom", "label": "Ensuite"}},
         {"sensor_id": "<C>", "connected": false, "rating": "critical", "last_seen_age_s": null, "battery": null, "rssi": null,
          "leak_state": false, "fw_version": null, "location": {"code": "laundry", "label": "Washer"}},
         {"sensor_id": "<D>", "connected": false, "rating": "critical", "last_seen_age_s": null, "battery": null, "rssi": null,
          "leak_state": false, "fw_version": null, "location": {"code": "utility", "label": "Water heater"}}
       ],
       "rules": {"auto_close_enabled": true, "trigger_mask": 7},
       "override_active": false
     }
   }
   ```
   - `gateway.name` is present only when a hub name is set. Numbers are examples.
   - An unheard device is `rating:"critical"` in its own entry, but it is excluded from `system_health` while it is syncing.
   - `<A>` and `<B>` must have non-null `battery`, `rssi` and `fw_version`, and a `last_seen_age_s` that keeps counting from before the provision (BUG-2).
3. After the first `eleak <C> ...` line (or `<D>`, whichever is heard first): an `event` snapshot within about 7 s (2 s low-tier window plus the 5 s clamp). It shows that sensor with `connected:true`, real `battery`, `rssi` and `fw_version`, `rating` `excellent` or `good`, and `"reason":"Syncing - waiting for 1 device"`.
4. After the second: a snapshot with `"reason":"boot"` (or `"commission"` in the timeout case above), `"system_health":{"rating":"excellent","reason":"All devices healthy"}`, and all four sensors populated.
5. Between these, further `event` snapshots from the pulse, spaced at least 5 s apart.

**Expected LED**

| When | Colour |
|---|---|
| Step 1 | GREEN |
| Within 1 s of the provision | WHITE (`rating=syncing color=WHITE effect=SOLID`) |
| Within 1 s of the last new device first being heard | GREEN (`rating=excellent color=GREEN effect=SOLID`) |

If a new device stays unheard for 600 s, it counts: RED, with "1 sensor offline". That is a failure of the test setup, not of the firmware.

**Timing tolerances**

- Command snapshot: 5.5 s or less after the ack.
- Each reason change: in the first snapshot after the device's first `eleak` line, within 7.5 s.
- Pulse window: closes 300 s (±2 s) after `PROV pulse armed`.

| Result | Pass / Fail | Notes (time C heard, time D heard, number of pulse snapshots) |
|---|---|---|
| T5-01 | | |

---

### T5-02 — A provision that adds no device (identical re-send): one `event` snapshot, no pulse

**Merged into DEC-02 (section 1).** DEC-02 is the same check: an identical re-send of the `provision` (and a rules-only one) gets `cmd_ack` `ok`, one `event` snapshot and a twin refresh, and nothing else: no `PROV pulse armed`, no `Commission: fast snapshot armed`, no `Sensor tracking reset`, a `BLE_VALVE: [TASK] CMD: CONNECT` then `[SCAN] Already connected` (E-09), no flush line. DEC-02's "Extended watch" paragraph carries this test's 6 min watch and its list of lines that must not appear.

**Covers:** council: a provision that adds no device (a UI-sync change from 2.1.3, documented in the CHANGELOG known limitations), BUG-2 (no re-arm on a no-op change), E-09.

| Result | Pass / Fail | Notes |
|---|---|---|
| T5-02 (= DEC-02) | | |

---

### T5-03 — Leak → auto_close (RMLEAK before CLOSE) → dry → 10 s auto-clear → valve_open

**Smoke:** **yes** (about 8 min). **Covers:**

- S24 (leak → auto_close);
- the user decision on the 10 s auto-clear (`75a4a59`) and its log lines;
- the RMLEAK-before-CLOSE order (feedback_auto_close_write_order);
- F-08 (the tick event is published first);
- F-01 (the hub's own clear is not read as a button press);
- E-15 (a change of the rating into or out of warning publishes promptly);
- E-16 (the valve's `last_seen_age_s` is 0 while it is linked);
- P0-a wire identity (`valve_id` is the provisioned MAC).

**Purpose.** This is the whole protected leak cycle on the happy path, with the new 10 s release timing, the LED sequence and the exact wire order.

**Start state.** Common preconditions, with labels from T5-01.

**Steps**

1. Dip sensor `<A>`'s probe in water and keep it wet.
2. Wait for the valve to close, then wait 30 s.
3. Take `<A>` out and dry the contacts completely.
4. Wait 30 s after the release.
5. Send:
   ```json
   {"schema":"eflostop.cmd","ver":1,"id":"t5-03-open","cmd":"valve_open"}
   ```

**Expected UART, step 1 (wet).** Within a tag, the order is as listed.

- `I BLE_LEAK: eleak <A> — leak=1 batt=NN% rssi=-NN fw=...` **(format)**
- `W RULES_ENGINE: LEAK INCIDENT latched by ble_leak_sensor sensor <A>`
- `I HEALTH_ENGINE: Interlock HELD (hub holding valve closed) — system rating floor WARNING`
- `W RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <A>`
- `I IOTHUB: Event: BLE Leak <A> leak=1 batt=NN` **(format)**
- `I TELEMETRY_V2: Pub event: {...leak_detected...}`, then `Pub event: {...auto_close...}`, in that order
- `BLE_VALVE`, in this order:
  - `[TASK] CMD: SET_RMLEAK`
  - `[CMD] Writing RMLEAK=1`
  - `[CMD] RMLEAK write rc=0 (value awaits the valve's own report)`
  - `[TASK] CMD: CLOSE_VALVE`
  - `[CMD] Writing Valve=0`
  - `[CMD] Valve write rc=0 (value awaits the valve's own report)`
- The read-backs, which can interleave with the lines above: `I BLE_VALVE: [READ] Read success: handle=N` **(format)**, `I BLE_VALVE: [DATA] RMLEAK=1 (ACTIVE)` and `I BLE_VALVE: [DATA] Valve State=0 (CLOSED)`
- `I IOTHUB: SNAP deferred — valve command settling` (zero or more times), then `I IOTHUB: SNAP trigger=event:<label>` **(format)**. The label is whichever request came first in the burst: usually `health` or `leak_detected`, sometimes `rules` or `valve_state_changed`. They coalesce into one snapshot.
- `I FLEET_LED: rating=critical color=RED effect=SOLID`
- **Fail if** `[CMD] Writing Valve=0` appears before `[CMD] Writing RMLEAK=1`. **Fail on** any `write attempt N/3 failed`, `ENQUEUE FAILED` or `RMLEAK cleared externally`.

**Expected IoT Hub, step 1.** Three events: `leak_detected` and `auto_close` in that order, then `valve_state_changed`. The snapshot comes before or after `valve_state_changed`.

```json
{"event":"leak_detected","source_type":"ble_leak_sensor","sensor_id":"<A>","leak_state":true,"battery":87,"location":{"code":"kitchen","label":"Sink"},"rssi":-64}
```
```json
{"event":"auto_close","source_type":"ble_leak_sensor","sensor_id":"<A>","rmleak_asserted":true,"location":{"code":"kitchen","label":"Sink"}}
```
```json
{"event":"valve_state_changed","source_type":"valve","valve_id":"<V>","valve_state":"closed","battery":92,"leak_state":false,"rmleak":true,"fw_version":"<valve fw>"}
```

These are the `data` objects. Each sits in the envelope `{"schema":"eflostop.v2","ts":...,"gateway":{...,"fw":"2.1.4",...},"type":"event","data":{...}}`.

The first snapshot that shows the valve closed must arrive within 7 s of `auto_close`. It must read (full):

```json
{
  "schema": "eflostop.v2", "ts": 1790000100,
  "gateway": {"id": "<GW>", "short_id": "<last 4>", "fw": "2.1.4", "uptime_s": 5600},
  "type": "snapshot",
  "data": {
    "reason": "event",
    "system_health": {"rating": "critical", "reason": "Leak detected: Sink, Leak interlock latched"},
    "valve": {"valve_id": "<V>", "state": "closed", "battery": 92, "leak_state": false, "rmleak": true,
              "connected": true, "fw_version": "<valve fw>", "rating": "excellent", "last_seen_age_s": 0},
    "lora_sensors": [],
    "ble_leak_sensors": [
      {"sensor_id": "<A>", "connected": true, "rating": "critical", "last_seen_age_s": 2, "battery": 87, "rssi": -64,
       "leak_state": true, "fw_version": "<fw>", "location": {"code": "kitchen", "label": "Sink"}},
      {"sensor_id": "<B>", "connected": true, "rating": "excellent", "last_seen_age_s": 51, "battery": 80, "rssi": -70,
       "leak_state": false, "fw_version": "<fw>", "location": {"code": "bathroom", "label": "Ensuite"}},
      {"sensor_id": "<C>", "connected": true, "rating": "excellent", "last_seen_age_s": 12, "battery": 90, "rssi": -58,
       "leak_state": false, "fw_version": "<fw>", "location": {"code": "laundry", "label": "Washer"}},
      {"sensor_id": "<D>", "connected": true, "rating": "excellent", "last_seen_age_s": 77, "battery": 76, "rssi": -72,
       "leak_state": false, "fw_version": "<fw>", "location": {"code": "utility", "label": "Water heater"}}
    ],
    "rules": {"auto_close_enabled": true, "trigger_mask": 7},
    "override_active": false
  }
}
```

An earlier snapshot in the burst may still show `"state":"open"` or `"rmleak":false`. Record it, but it is not a fail if the next snapshot, within 5 s, is correct. `valve.last_seen_age_s` must be `0` in every snapshot while the valve is linked (E-16).

**Expected UART, step 3 (dry).**

- `I BLE_LEAK: eleak <A> — leak=0 ...` **(format)**
- `I RULES_ENGINE: All leaks resolved — pending auto-close cancelled`. It can be followed by `I BLE_VALVE: [CMD] Pending valve CLOSE cancelled (leak resolved)` only if a close was still pending; normally nothing is pending.
- `I RULES_ENGINE: All sensors clear — auto-clear timer started (10s)`
- `I IOTHUB: Event: BLE Leak <A> leak=0 batt=NN` **(format)**, then `Pub event: {...leak_cleared...}`
- `I FLEET_LED: rating=warning color=YELLOW effect=SOLID`
- `I IOTHUB: SNAP trigger=event:leak_cleared` (or `event:health`)
- **10–12 s after "auto-clear timer started":**
  - `W RULES_ENGINE: AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`
  - `I HEALTH_ENGINE: Interlock released — system rating floor removed`
  - `I TELEMETRY_V2: Pub event: {...rmleak_auto_cleared...}`
  - `I BLE_VALVE: [TASK] CMD: CLEAR_RMLEAK`
  - `I BLE_VALVE: [CMD] Writing RMLEAK=0`
  - `I BLE_VALVE: [CMD] RMLEAK write rc=0 (value awaits the valve's own report)`
  - `I BLE_VALVE: [DATA] RMLEAK=0 (CLEAR)`
  - `I FLEET_LED: rating=excellent color=GREEN effect=SOLID` (or `rating=good`)
- **Must not appear:**
  - `[TASK] CMD: OPEN_VALVE` (the hub never reopens the valve itself)
  - `RMLEAK cleared externally (valve override) — starting 24h override window`
  - `water_access_override_enabled`
  - a second `AUTO-CLEAR`

**Expected IoT Hub, step 3.**

- `leak_cleared`:
  ```json
  {"event":"leak_cleared","source_type":"ble_leak_sensor","sensor_id":"<A>","leak_state":false,"battery":87,"location":{"code":"kitchen","label":"Sink"},"rssi":-63}
  ```
- A snapshot within about 5.5 s, with `"system_health":{"rating":"warning","reason":"Leak interlock latched"}`, the valve `closed` with `rmleak:true`, and `<A>` with `leak_state:false` and rating `excellent` or `good`.
- `rmleak_auto_cleared`, 10–12 s after `leak_cleared`:
  ```json
  {"event":"rmleak_auto_cleared","valve_id":"<V>","clear_after_seconds":10}
  ```
- A snapshot with `"system_health":{"rating":"excellent","reason":"All devices healthy"}`. The valve reads `"state":"closed","rmleak":false`; the valve stays closed. `override_active` is `false`.
- No `valve_state_changed` (the valve did not move).

**Expected, step 5 (`valve_open` after the release).**

- UART:
  - `I IOTHUB: Command: VALVE_OPEN`
  - `I BLE_VALVE: [TASK] CMD: CONNECT` and `W BLE_VALVE: [SCAN] Already connected`
  - `I BLE_VALVE: [TASK] CMD: OPEN_VALVE`
  - `I BLE_VALVE: [CMD] Writing Valve=1`
  - `I BLE_VALVE: [CMD] Valve write rc=0 (value awaits the valve's own report)`
  - `I BLE_VALVE: [DATA] Valve State=1 (OPEN)`
- IoT Hub:
  - `{"event":"cmd_ack","id":"t5-03-open","cmd":"valve_open","status":"ok"}`
  - `valve_state_changed` with `"valve_state":"open","rmleak":false`
  - a snapshot with `"state":"open"`
- LED: GREEN.

**Expected LED over the whole test**

| When | Colour |
|---|---|
| Start | GREEN |
| Within 0.5 s of `eleak ... leak=1` | RED |
| Within 0.5 s of `eleak ... leak=0` | YELLOW |
| Within 0.5 s of `AUTO-CLEAR` (10–12 s after the dry report) | GREEN |

**Timing tolerances**

| Interval | Tolerance |
|---|---|
| `eleak <A> — leak=1` → `AUTO-CLOSE + RMLEAK triggered` | ≤ 1 s |
| → `[DATA] Valve State=0 (CLOSED)` | ≤ 3 s |
| `All sensors clear — auto-clear timer started (10s)` → `AUTO-CLEAR: all sensors clear for 10s` | **10.0–12.5 s** (10 s dwell plus the 2 s poll) |
| `AUTO-CLEAR` → `rmleak_auto_cleared` in IoT Hub | ≤ 1 s |

| Result | Pass / Fail | Notes (measured AUTO-CLEAR delta; any early snapshot still showing the valve open) |
|---|---|---|
| T5-03 | | |

---

### T5-04 — Re-wet at the clear (5 runs): release, then re-lock, never a false button override

**Smoke:** no (about 10 min). **Covers:** F-08, the docs-review fix `b245d94` (a re-wet at the release re-locks), F-01, council: F-08 re-wet order (F1, F3, F4), council: stale-confirm false "button" window (observe the read-back gap).

**Purpose.** A sensor that goes wet again at the moment of the 10 s clear must:

- release (`rmleak_auto_cleared`) and then re-lock (`leak_detected`, `auto_close`);
- publish them in that order;
- leave the valve closed with RMLEAK set;
- never start a false `water_access_override_enabled{trigger:"button"}`.

**Start state.** Common preconditions.

**Steps (repeat 5 times)**

1. Wet `<A>` and wait for the valve to close with `[DATA] RMLEAK=1 (ACTIVE)`. Wait 10 s more.
2. Dry `<A>`. Note the time of `All sensors clear — auto-clear timer started (10s)`.
3. The moment `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK` prints (about 10 s after step 2), dip `<A>` again. The aim is for its `eleak <A> — leak=1` to land 0–2 s after the `AUTO-CLEAR` line.
4. Wait 15 s. Record the result in the table below. Dry `<A>`, and let the cycle end with `rmleak_auto_cleared` before the next run.

**Classify each run** by Δ = time(`eleak <A> — leak=1`) − time(`AUTO-CLEAR`):

- **Δ < 0 (the re-wet arrived before the clear).** No `AUTO-CLEAR` fires: the wet report resets the timer. There is no second `auto_close`, because the valve is already locked. The run is valid but does not exercise the case; redo it.
- **Δ ≥ 0.** This is the case under test. Expected below.

**Expected UART (Δ ≥ 0).** In this order: `RULES_ENGINE` and `TELEMETRY_V2` first, then the `BLE_VALVE` commands in FIFO order.

- `W RULES_ENGINE: AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`
- `I TELEMETRY_V2: Pub event: {...rmleak_auto_cleared...}`. It is published straight after the tick, **before** the next two lines (F-08).
- `W RULES_ENGINE: LEAK INCIDENT latched by ble_leak_sensor sensor <A>`
- `W RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <A>`
- `I IOTHUB: Event: BLE Leak <A> leak=1 batt=NN` **(format)**, then `Pub event: {...leak_detected...}` and `Pub event: {...auto_close...}`
- `BLE_VALVE`: `[TASK] CMD: CLEAR_RMLEAK` → `[TASK] CMD: SET_RMLEAK` → `[TASK] CMD: CLOSE_VALVE`. Each is followed by its `[CMD] Writing ...` line, in that order: `RMLEAK=0`, then `RMLEAK=1`, then `Valve=0`.
- The final read-back is `I BLE_VALVE: [DATA] RMLEAK=1 (ACTIVE)`.
- **Allowed:**
  - `I RULES_ENGINE: RMLEAK clear read back - the hub's own clear, not a valve override`
  - `W BLE_VALVE: [CMD] RMLEAK write: GATT busy - waiting`
  - `W BLE_VALVE: [CMD] Valve=0 held behind the pending RMLEAK command`
- **Fail if:** `RMLEAK cleared externally (valve override) — starting 24h override window`, `OVERRIDE WINDOW STARTED`, or any `[TASK] CMD: OPEN_VALVE`.

**Expected IoT Hub (Δ ≥ 0).** `rmleak_auto_cleared` `{"valve_id":"<V>","clear_after_seconds":10}`, then `leak_detected` (`<A>`), then `auto_close` (`<A>`, `rmleak_asserted:true`).

There is no `water_access_override_enabled`. The last snapshot of the run shows the valve `"state":"closed","rmleak":true` and `"override_active":false`.

The one accepted exception: the pass in which MQTT (re)connects. There, `rmleak_auto_cleared` may be missing; `leak_detected` and `auto_close` still appear and the valve stays locked. Record it.

**Expected LED.** RED → YELLOW (dry) → GREEN for about 1 s or less at the clear (it may not be visible) → RED on the re-wet.

**Timing.**

- `AUTO-CLEAR` 10.0–12.5 s after the timer line.
- In a healthy-link run, `Pub event` `rmleak_auto_cleared` → `AUTO-CLOSE + RMLEAK triggered`: under 1 s after the re-wet report is dequeued.
- Record the gap between `[CMD] Writing RMLEAK=1` and the next `[DATA] RMLEAK=1 (ACTIVE)`. A gap over 4 s is a note: it is the known stale-confirm route, which needs more than 5 s to misfire.

| Run | Δ (s) | rmleak_auto_cleared before auto_close? (Y/N/reconnect pass) | Valve ends CLOSED + RMLEAK=1 | "cleared externally"? | Write→read-back gap (s) | Pass/Fail |
|---|---|---|---|---|---|---|
| 1 | | | | | | |
| 2 | | | | | | |
| 3 | | | | | | |
| 4 | | | | | | |
| 5 | | | | | | |

**Pass:** every Δ ≥ 0 run ends CLOSED with RMLEAK=1, with no "cleared externally" and no override event. `rmleak_auto_cleared` precedes `auto_close` in every run that is not a reconnect pass.

---

### T5-05 — The leak_reset / valve_open guard matrix

**Smoke:** **yes** (about 6 min). **Covers:**

- S24 (leak_reset);
- the user decision that `valve_open` is refused while RMLEAK is set or an incident is latched, even with the valve disconnected (E-06);
- the exact refusal texts;
- F-05 (leak_reset queues its clear under the rules lock);
- F-01 (the hub's own clear is not an override);
- council: C2D-written rules events publish at the next pass top.

**Purpose.** `leak_reset` must be refused while any source is wet, and accepted once all sources are dry. `valve_open` and `valve_set_state` open must be refused with the RMLEAK text while locked. `valve_open` must work once released.

**Start state.** Common preconditions.

**Steps and expected results**

**(a) Wet and locked: every "open" request is refused.**

Wet `<A>` and wait for `[DATA] RMLEAK=1 (ACTIVE)` and `[DATA] Valve State=0 (CLOSED)`. Then send, about 3 s apart:

```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-05-reset-wet","cmd":"leak_reset"}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-05-open-wet","cmd":"valve_open"}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-05-set-open-wet","cmd":"valve_set_state","payload":{"state":"open"}}
```

- UART:
  - `I IOTHUB: Command: LEAK_RESET`, then `W RULES_ENGINE: LEAK_RESET refused — 1 leak source(s) still active (use override to open during a leak)`
  - `I IOTHUB: Command: VALVE_OPEN`, then `W IOTHUB: VALVE_OPEN refused — Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.`
  - `I IOTHUB: Command: VALVE_SET_STATE -> open`, then `W IOTHUB: VALVE_SET_STATE open refused — Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.`
  - **No** `[TASK] CMD: OPEN_VALVE` and no `[TASK] CMD: CLEAR_RMLEAK`.
- IoT Hub, three error acks:
  ```json
  {"event":"cmd_ack","id":"t5-05-reset-wet","cmd":"leak_reset","status":"error","error":{"code":"leak_reset","detail":"A leak is still active. Fix the leak first, or use override to open the valve during a leak."}}
  ```
  ```json
  {"event":"cmd_ack","id":"t5-05-open-wet","cmd":"valve_open","status":"error","error":{"code":"valve_open","detail":"Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."}}
  ```
  ```json
  {"event":"cmd_ack","id":"t5-05-set-open-wet","cmd":"valve_set_state","status":"error","error":{"code":"valve_set_state","detail":"Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."}}
  ```
  There is no `rmleak_cleared`. A refused command gets no snapshot of its own.
- LED: RED.

**(b) Optional (E-06): locked, with the valve disconnected.**

Keep `<A>` wet and switch the valve's PSU **off**. Wait for `W BLE_VALVE: [DISCONNECT] reason=0x..` **(format)**, then send:

```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-05-open-off","cmd":"valve_open"}
```

- Expect the same RMLEAK error ack. The valve cache is cleared on disconnect, so this refusal comes from the latched incident.
- Switch the PSU back on **within 3 min**, to avoid a `device_offline` event, and wait for `SETUP COMPLETE`. Then expect:
  - `I RULES_ENGINE: Reconnected with 1 active leak(s) — valve already closed + RMLEAK asserted, nothing to do`, or `W RULES_ENGINE: Valve reconnected with 1 active leak(s) — executing auto-close`;
  - the valve ends closed, with `[DATA] RMLEAK=1 (ACTIVE)`.

**(c) Dry, inside the 10 s window: leak_reset is accepted.**

Dry `<A>`. As soon as `I RULES_ENGINE: All sensors clear — auto-clear timer started (10s)` prints, send within 8 s (have it pasted and ready):

```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-05-reset-dry","cmd":"leak_reset"}
```

- UART:
  - `I IOTHUB: Command: LEAK_RESET`
  - `W RULES_ENGINE: LEAK_RESET: clearing incident (hub_latch=1, valve_rmleak=1, override=0)`
  - `I HEALTH_ENGINE: Interlock released — system rating floor removed`
  - `I IOTHUB: Leak incident cleared, RMLEAK reset`
  - `I BLE_VALVE: [TASK] CMD: CLEAR_RMLEAK`, `[CMD] Writing RMLEAK=0`, `[DATA] RMLEAK=0 (CLEAR)`
- **Must not follow:**
  - `AUTO-CLEAR: all sensors clear for 10s` (the reset cleared the timer)
  - `RMLEAK cleared externally`
  - `Reconnected: ...`
- IoT Hub, in this order:
  1. `{"event":"cmd_ack","id":"t5-05-reset-dry","cmd":"leak_reset","status":"ok"}`
  2. `{"event":"rmleak_cleared","valve_id":"<V>"}`, within about 1 s of the ack. It has no `override_cancelled`, and no `rmleak_auto_cleared` follows.
  3. A snapshot with `"system_health":{"rating":"excellent","reason":"All devices healthy"}` and the valve `"state":"closed","rmleak":false`.
- LED: YELLOW → GREEN within 0.5 s of the reset.

If the 10 s window was missed, `rmleak_auto_cleared` arrives instead and the reset is a no-op, as in (d). Redo (a) and (c).

**(d) A no-op leak_reset.**

Wait until `[DATA] RMLEAK=0 (CLEAR)` has printed, then send `leak_reset` again:

```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-05-reset-noop","cmd":"leak_reset"}
```

- UART: `I RULES_ENGINE: LEAK_RESET: no active incident`, then `I IOTHUB: Leak incident cleared, RMLEAK reset`. There is no RMLEAK write.
- IoT Hub: `cmd_ack` `ok`, **no** rules event, then one `event` snapshot (every successful command gets one).

**(e) Released: valve_open is accepted.**

Send:

```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-05-open-ok","cmd":"valve_open"}
```

Expected as in T5-03 step 5: an `ok` ack, `valve_state_changed` `open` with `rmleak:false`, and the LED GREEN.

**Timing.**

- Refusal acks within 1 s of the send.
- In (c), `rmleak_cleared` 1.5 s or less after the ack; it is published at the next loop pass top.

| Result | Pass / Fail | Notes (did (b) run? which reconnect line?) |
|---|---|---|
| T5-05 | | |

---

### T5-06 — override_enable (C2D) during a leak → blocked leak → override_cancel with the leak still active (re-close)

**Smoke:** no (about 8 min). **Covers:** S24 (override enable and cancel), app-team flows B1/B2/C1/D1, RMLEAK-before-valve on the override paths, council: C2D-written rules events publish at the next pass top.

**Purpose.** The remote override must:

- refuse when there is nothing to override;
- open the valve during a live leak, with RMLEAK cleared before the OPEN, and start a 24 h window;
- block auto-close for further leaks, while still reporting them;
- on cancel with a leak still active, re-close with RMLEAK written before CLOSE.

**Start state.** Common preconditions.

**Steps and expected results**

**(1) Nothing to override.** Send:

```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-06-ovr-none","cmd":"override_enable"}
```

- UART: `I IOTHUB: Command: OVERRIDE_ENABLE`, then `W RULES_ENGINE: override_enable: no active incident to override`
- IoT Hub:
  ```json
  {"event":"cmd_ack","id":"t5-06-ovr-none","cmd":"override_enable","status":"error","error":{"code":"override_enable","detail":"No active leak to override. Use the normal Open Valve control."}}
  ```

**(2) Leak.** Wet `<A>`. Expect the T5-03 step 1 sequence (auto_close, the valve closed with RMLEAK=1, LED RED).

**(3) Remote override while `<A>` is still wet.** Send:

```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-06-ovr-on","cmd":"override_enable"}
```

- UART:
  - `I IOTHUB: Command: OVERRIDE_ENABLE`
  - in this order:
    - `I HEALTH_ENGINE: Interlock released — system rating floor removed`
    - `I RULES_ENGINE: NVS: override state=1 expiry=NNNNNNNNNN saved` **(format)**
    - `W RULES_ENGINE: OVERRIDE WINDOW STARTED: auto-close blocked for 24h (expiry=NNNNNNNNNN)` **(format)**
    - `W RULES_ENGINE: override_enable: 24h override started remotely — RMLEAK cleared, valve opening`
  - `BLE_VALVE`, in this order: `[TASK] CMD: CLEAR_RMLEAK`, `[CMD] Writing RMLEAK=0`, `[TASK] CMD: OPEN_VALVE`, `[CMD] Writing Valve=1`. Then the read-backs `[DATA] RMLEAK=0 (CLEAR)` and `[DATA] Valve State=1 (OPEN)`.
  - **Fail if** `Writing Valve=1` precedes `Writing RMLEAK=0`, or if `RMLEAK cleared externally` appears.
- IoT Hub:
  - `{"event":"cmd_ack","id":"t5-06-ovr-on","cmd":"override_enable","status":"ok"}`
  - then, within about 1 s, `{"event":"water_access_override_enabled","trigger":"c2d_command","expires_ts":<ts+86400 ±2>,"remaining_s":86400}`
  - `valve_state_changed` `{"valve_state":"open","rmleak":false,...}`
  - a snapshot with `"override_active":true`, `"override_remaining_s"` between 86390 and 86400, `"expires_ts"` equal to the event's ±2, the valve `"state":"open","rmleak":false`, and `"system_health":{"rating":"critical","reason":"Leak detected: Sink"}` (`<A>` is still wet, and the interlock is released)
- LED: RED, because a sensor is still wet.

**(4) A further leak is reported, not acted on.** Wet `<B>` as well.

- UART:
  - `I BLE_LEAK: eleak <B> — leak=1 ...` **(format)**
  - `W RULES_ENGINE: LEAK INCIDENT latched by ble_leak_sensor sensor <B>`
  - `I HEALTH_ENGINE: Interlock HELD (hub holding valve closed) — system rating floor WARNING`
  - `I RULES_ENGINE: Override active — auto-close BLOCKED for ble_leak_sensor sensor <B> (remaining=NNNNNs)` **(format)**
  - **No** `AUTO-CLOSE + RMLEAK triggered`, and no `[TASK] CMD: CLOSE_VALVE`.
- IoT Hub:
  - `leak_detected` for `<B>`
  - `{"event":"auto_close_blocked_override","source_type":"ble_leak_sensor","sensor_id":"<B>","override_remaining_s":<86400 − elapsed>}`. This is rate-limited to one per 60 s.
  - a snapshot with `"system_health":{"rating":"critical","reason":"2 leaks detected, Leak interlock latched"}`, `override_active:true`, and the valve still open

**(5) An open during the override is accepted.** Send `valve_open` (`"id":"t5-06-open-ovr"`). Expect an `ok` ack and `[TASK] CMD: OPEN_VALVE`. It is not refused, because the override window is active and RMLEAK is clear. The valve stays open.

**(6) Cancel with both leaks still active: the valve re-closes.** Send:

```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-06-ovr-cancel","cmd":"override_cancel"}
```

- UART:
  - `I IOTHUB: Command: OVERRIDE_CANCEL`
  - `W RULES_ENGINE: OVERRIDE WINDOW CANCELLED (remaining_s=NNNNN)` **(format)**
  - `W RULES_ENGINE: Override cancelled with 2 active leak(s) — executing auto-close`
  - `BLE_VALVE`: `[CMD] Writing RMLEAK=1` **before** `[CMD] Writing Valve=0`, then `[DATA] RMLEAK=1 (ACTIVE)` and `[DATA] Valve State=0 (CLOSED)`
- IoT Hub:
  - `cmd_ack` `ok`
  - `{"event":"auto_close_reenabled","previous_remaining_s":<86400 − elapsed>,"reason":"c2d_command"}`
  - `valve_state_changed` `{"valve_state":"closed","rmleak":true,...}`
  - a snapshot with `"override_active":false` (no `override_remaining_s` or `expires_ts`) and `"reason":"2 leaks detected, Leak interlock latched"`
  - There is **no** `auto_close` event on this path (the code does not build one). This is expected.
- LED: RED.

**(7) Clean up.** Dry `<A>` and `<B>`. After the second dry report you get `All leaks resolved — pending auto-close cancelled`, then `All sensors clear — auto-clear timer started (10s)`, then 10–12 s later `AUTO-CLEAR`, `rmleak_auto_cleared` `{"valve_id":"<V>","clear_after_seconds":10}` and LED GREEN. The valve stays closed.

**Timing.**

- Step 3: the ack within 1 s when the valve is linked; the valve OPEN read-back within 3 s.
- Step 6: the valve CLOSED within 3 s of the ack.
- `expires_ts − ts` of the enabled event: 86400 ±2.

| Result | Pass / Fail | Notes |
|---|---|---|
| T5-06 | | |

---

### T5-07 — override_cancel with no leak (D4), with no window (D5), and leak_reset cancelling a window

**Smoke:** no (about 6 min). **Covers:** S24 (override cancel), app-team flows D4/D5/E3, the "residual incident wiped" behaviour.

**Start state.** Common preconditions.

**Steps and expected results**

**(1)** Wet `<A>` (auto-close). Send `override_enable` (`"id":"t5-07-ovr-on"`): the valve opens and the window starts, as in T5-06 step 3.

**(2)** Dry `<A>`.

- Expect `I IOTHUB: Event: BLE Leak <A> leak=0 ...` and `leak_cleared`.
- **No** `All sensors clear — auto-clear timer started` and **no** `AUTO-CLEAR` / `rmleak_auto_cleared`. The override cleared the incident, so there is nothing to auto-clear.
- LED: GREEN, with `override_active:true` in the snapshot.

**(3) Cancel with nothing wet (D4).** Send:

```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-07-cancel-dry","cmd":"override_cancel"}
```

- UART: `W RULES_ENGINE: OVERRIDE WINDOW CANCELLED (remaining_s=NNNNN)` **(format)**. There is no "executing auto-close" and no valve write.
- IoT Hub: `cmd_ack` `ok`, then `{"event":"auto_close_reenabled","previous_remaining_s":<n>,"reason":"c2d_command"}`, then a snapshot with `override_active:false` and the valve still **open**.

**(4) Cancel with no window (D5).** Send `override_cancel` again (`"id":"t5-07-cancel-none"`).

- UART: `I RULES_ENGINE: override_cancel: no active override window`
- IoT Hub: `cmd_ack` `ok`, **no** `auto_close_reenabled`, and one `event` snapshot.

**(5) Auto-close is armed again.** Wet `<A>`: a full `auto_close` as in T5-03 step 1. It shows that the cancel re-enabled protection and that no residual incident state blocks it.

**(6) leak_reset cancels a window (E3).** With `<A>` still wet, send `override_enable` (`"id":"t5-07-ovr-on-2"`): the valve opens and the window starts. Dry `<A>` and wait for `leak_cleared`. Then send:

```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-07-reset-ovr","cmd":"leak_reset"}
```

- UART:
  - `W RULES_ENGINE: OVERRIDE WINDOW CANCELLED (remaining_s=NNNNN)` **(format)**
  - `W RULES_ENGINE: LEAK_RESET: clearing incident (hub_latch=0, valve_rmleak=0, override=1)`
  - There is no RMLEAK write: nothing is set on the valve.
- IoT Hub: `cmd_ack` `ok`, then `{"event":"rmleak_cleared","valve_id":"<V>","override_cancelled":true}`, then a snapshot with `override_active:false`.

**LED.** RED while wet. GREEN when all sensors are dry and no incident is latched, whether or not the window is active.

| Result | Pass / Fail | Notes |
|---|---|---|
| T5-07 | | |

---

### T5-08 — Valve long-press (physical) override: live, after a hub restart, and pressed right after the close

**Merged into T3-15 (section 3).** T3-15 covers the same three cases: (a) a live long-press 10 s or more after `[DATA] RMLEAK=1 (ACTIVE)` (steps 1-4), (b) a press after a hub power-cycle mid-incident (step 5), and (c) a press within about 1 s of the close (step 6, observe: a missed press fails closed). Run T3-15 with these two additions from this test:

1. **During T3-15 step 5 (the hub power-cycle with the sensor wet), observe F-02** (accepted known limitation): the valve's dry link-up report (`BLE_VALVE: [READY] Announcing flood probe state (dry) for reconciliation`) can start the 10 s auto-clear before the wet sensor is heard again. You then get `AUTO-CLEAR`, `rmleak_auto_cleared`, and at the sensor's next wet burst (about 15 s or less) `leak_detected` and `auto_close`. The valve must stay closed throughout. Record it as in T4-11 Part A. Press the button only once the state has settled (the last `[DATA] RMLEAK=1 (ACTIVE)`, and 10 s with no RMLEAK write).
2. **After T3-15 step 2 (the window is open, `<S1>` wet), check the auto-clear during a window:** wet `<S2>` too and expect `auto_close_blocked_override` for `<S2>` and no close. Dry both sensors: 10-12 s after the last dry report expect `RULES_ENGINE: All sensors clear — auto-clear timer started (10s)`, then `RULES_ENGINE: AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK` and `rmleak_auto_cleared`, while the window stays active (snapshot `"override_active":true`). Then wet `<S1>` again and continue with T3-15 step 3.

**Covers:** SRS §4.4.2 / app-team B3 regression; council: the confirmed-edge rule must not suppress a genuine press (F4, F5); council: a press after a hub restart (F5); council: the missed-press residual (observe); F-02 (observe).

| Case | Window started? (Y/N) | Time press→"cleared externally" (s) | F-02 seen? | Pass/Fail | Notes |
|---|---|---|---|---|---|
| (a) = T3-15 step 2 | | | n/a | | |
| (b) = T3-15 step 5 | | | | | |
| (c) = T3-15 step 6 | | | n/a | observe | |

---

### T5-09 — Override expiry (bench-only shortened build)

**Smoke:** no (about 20 min, plus a reflash). **Covers:** S24 (override expiry), app-team flows D6/D8, RMLEAK-before-CLOSE on the expiry path.

**Bench-only build.** It is never committed and never shipped.

1. Add this line at the end of `main/CMakeLists.txt`:
   ```cmake
   target_compile_definitions(${COMPONENT_LIB} PRIVATE OVERRIDE_WINDOW_DURATION_S=300)
   ```
2. Build and flash. Afterwards run `git checkout -- main/CMakeLists.txt`, and reflash the release image before any other test.
3. `PROJECT_VER` is unchanged, so `gateway.fw` still reads `2.1.4`. Note in the capture file name that it came from the bench image.

The log text still says "24h" (`OVERRIDE WINDOW STARTED: auto-close blocked for 24h`). Only the number changes: `remaining_s` is 300.

**Start state.** The bench image, with the common preconditions.

**(a) Expiry with a leak still active (D6).**

1. Wet `<A>` (auto-close), then send `override_enable` (`"id":"t5-09-ovr-a"`). The valve opens, and you get `water_access_override_enabled` with `"remaining_s":300` and `expires_ts` = ts + 300.
2. Keep `<A>` wet for the whole 5 min.
3. Expected 300–332 s after the enable. The rules tick runs at least every 30 s when idle.
   - UART:
     - `W RULES_ENGINE: OVERRIDE WINDOW EXPIRED: auto-close re-enabled`
     - `W RULES_ENGINE: Override expired with 1 active leak(s) — executing auto-close`
     - `I HEALTH_ENGINE: Interlock HELD (hub holding valve closed) — system rating floor WARNING`
     - `BLE_VALVE`: `[CMD] Writing RMLEAK=1` **before** `[CMD] Writing Valve=0`, then `[DATA] RMLEAK=1 (ACTIVE)` and `[DATA] Valve State=0 (CLOSED)`
   - IoT Hub:
     - `{"event":"water_access_override_expired","auto_close_resumed":true,"active_leak_count":1}`
     - `valve_state_changed` `closed` with `rmleak:true`
     - a snapshot with `override_active:false`
     - no `auto_close` event on this path
   - LED: RED.
4. Clean up: dry `<A>`. The auto-clear follows 10–12 s later.

**(b) Expiry with no leak (D8).**

1. Wet `<A>`, send `override_enable` (`"id":"t5-09-ovr-b"`), then dry `<A>` straight away.
2. At 300–332 s:
   - UART: `OVERRIDE WINDOW EXPIRED: auto-close re-enabled`, with no "executing auto-close" and no valve write.
   - IoT Hub: `{"event":"water_access_override_expired","auto_close_resumed":false,"active_leak_count":0}`, and a snapshot with `override_active:false`. The valve stays **open**.
   - LED: GREEN.

**Optional, on the release image.** Start one override and leave the hub online. Confirm the expiry at 24 h, with up to +30 s.

| Result | Pass / Fail | Notes (measured expiry delay) |
|---|---|---|
| T5-09 (a) | | |
| T5-09 (b) | | |

---

### T5-10 — RMLEAK write and read-back audit (from the T5-03 … T5-09 UART capture)

**Smoke:** no (desk check, about 10 min). **Covers:**

- "RMLEAK is applied before the valve command, on every path" (CHANGELOG Reliability);
- feedback_auto_close_write_order;
- the read-back that keeps the RMLEAK cache fresh (the 2.1.1 field defect);
- F-01 (the hub's own clear is never an override);
- council: B1 regression lines absent on a healthy link.

**Purpose.** A mechanical check over the whole UART log: every hub-issued RMLEAK write is read back, and no valve command overtakes its RMLEAK.

**Steps.** Save the serial monitor output of T5-03 to T5-09 as `t5_uart.txt`, then run in PowerShell:

```powershell
Select-String -Path t5_uart.txt -Pattern '\[CMD\] Writing (RMLEAK|Valve)=|\[DATA\] (RMLEAK|Valve State)=|\[TASK\] CMD: (SET_RMLEAK|CLEAR_RMLEAK|OPEN_VALVE|CLOSE_VALVE)' | ForEach-Object Line
Select-String -Path t5_uart.txt -Pattern 'cleared externally|write attempt|failed 3 times|reconnecting to re-apply|ENQUEUE FAILED|read-back rc=' | ForEach-Object Line
```

**Expected**

1. Every `[CMD] Writing Valve=0` issued by auto-close, `override_cancel` or expiry is preceded by a `[CMD] Writing RMLEAK=1` for the same event. Every `[CMD] Writing Valve=1` issued by `override_enable` is preceded by `[CMD] Writing RMLEAK=0`.
2. Every `[CMD] Writing RMLEAK=v` is followed, within about 1.5 s on a healthy bench link, by `[DATA] RMLEAK=v (ACTIVE|CLEAR)` with the same `v`. This is the hub's own read-back: the valve does not notify a hub-written RMLEAK.
3. The snapshot that follows each read-back shows `valve.rmleak` equal to `v`.
4. The second search prints `RMLEAK cleared externally (valve override) — starting 24h override window` **only** during T5-08. It prints none of the other patterns.
   - `W BLE_VALVE: [CMD] ... write: GATT busy - waiting` and `... held behind the pending RMLEAK command` are acceptable.
   - `[CMD] valve read-back rc=6 - position unconfirmed` is a known pre-existing line, but only at a reconnect with both slots pended, which should not occur in this section.

| Result | Pass / Fail | Notes (count of RMLEAK writes and read-backs) |
|---|---|---|
| T5-10 | | |

---

### T5-11 — Heartbeat interval change through the twin desired property

**Smoke:** no (about 12 min). **Covers:** S24 (twin heartbeat change), the 2.0.2 persistence and GET-on-connect behaviour, and the reported echo of a rejected value.

**Start state.** Common preconditions. The hub is quiet: all sensors dry, no pulse window open (more than 6 min since a provision), and no override. The interval is the default 300 s: `"snapshot_interval_s":300` in the last twin reported.

**How to edit the twin.** In VS Code, right-click the device and choose **Edit Device Twin**. Under `"properties"`, set `"desired"` as shown, save the file, then right-click in the editor and choose **Update Device Twin**.

**Steps and expected results**

**(1) Set 60 s.**

```json
"desired": { "snapshot_interval_s": 60 }
```

- UART:
  - `I IOTHUB: Twin desired patch: {"snapshot_interval_s":60,"$version":N}` **(format)**
  - `I IOTHUB: Twin: snapshot_interval_s = 60`
  - `I TELEMETRY_V2: Snapshot interval set to 60s (persisted)`
  - `I IOTHUB: Twin reported (N): {...,"snapshot_interval_s":60,...}` **(format)**
  - within 30 s (the next loop pass): `I IOTHUB: SNAP heartbeat=re-aimed interval_ms=60000 next_in_ms=N` **(format)**. This prints only while the pending snapshot is the heartbeat, which it is on a quiet hub.
- Twin reported, in full:
  ```json
  {"fw_version":"2.1.4","gateway_id":"<GW>","short_id":"<last 4>","hub_name":"<name or empty>","provisioned":true,"valve_mac":null,"valve_device_id":null,"valve_id":"<V>","lora_sensor_count":0,"ble_leak_sensor_count":4,"auto_close_enabled":true,"trigger_mask":7,"uptime_s":<n>,"snapshot_interval_s":60,"free_heap":<n>}
  ```
- IoT Hub:
  - The first `heartbeat` snapshot comes at the later of (last snapshot + 60 s) and the re-aim pass, so no later than 30 s after the twin update if the last snapshot was more than 60 s ago.
  - Then three consecutive `"reason":"heartbeat"` snapshots, **60 s ±2 s apart**. Each is preceded on UART by `SNAP trigger=heartbeat` and followed by `SNAP heartbeat=reset interval_ms=60000`.
  - Any snapshot, whatever its reason, restarts the 60 s count. Do not touch the sensors during the measurement.

**(2) An out-of-range value is rejected.**

```json
"desired": { "snapshot_interval_s": 30 }
```

- UART:
  - `W TELEMETRY_V2: Snapshot interval 30s rejected — outside [60..3600], keeping 60s`
  - `W IOTHUB: Twin: snapshot_interval_s 30 rejected — reported will show 60`
  - `Twin reported` with `"snapshot_interval_s":60`
- Heartbeats stay at 60 s. There is no `cmd_ack`: desired properties are acknowledged only through the reported echo.

**(3) Persistence across a reboot.** Set desired back to a valid value (`60`) first. A rejected desired value is re-applied, and rejected again, at every connect. Then press EN or power-cycle the hub.

- UART at boot: `I TELEMETRY_V2: Snapshot interval 60s restored from NVS`
- After the connect:
  - `I IOTHUB: Twin GET requested (rid=N)` **(format)**
  - `I IOTHUB: Twin GET: applying desired properties from full document`
  - `I IOTHUB: Twin: snapshot_interval_s = 60`
  - No "Snapshot interval set to" line, because the value is unchanged.
- Heartbeats continue at 60 s after the boot or fast snapshot.

**(4) Restore.**

```json
"desired": { "snapshot_interval_s": 300 }
```

- `Snapshot interval set to 300s (persisted)` and `SNAP heartbeat=re-aimed interval_ms=300000 next_in_ms=N` **(format)**
- The reported echo reads 300. The next heartbeat comes 300 s ±2 s after the last snapshot.

**LED.** GREEN throughout.

| Heartbeat gaps measured (s) | Rejected value echoed as 60? | Restored after reboot? | Pass/Fail | Notes |
|---|---|---|---|---|
| | | | | |

---

### T5-12 — Offline event buffering and reconnect replay order

**Smoke:** no (about 15 min). **Covers:**

- S24 (offline/reconnect replay);
- the offline buffer mutex (CHANGELOG Reliability);
- council: F-08 early take while offline (F3, F5): the tick's `rmleak_auto_cleared` must be buffered ahead of the re-wet's events;
- leak protection with no cloud.

**Purpose.** With MQTT down:

- leak protection works unchanged;
- every event is buffered in order;
- at reconnect the buffer drains before the lifecycle message, in the original order, with real non-decreasing `ts`.

**Start state.** Common preconditions. The hub has been online long enough that its clock is synced, so these are ordinary buffered events, not pre-sync ones. The offline buffer is empty: the last connect printed no `Draining` line.

**Steps**

1. Power the Wi-Fi router off.
   - Expect `W IOTHUB: WiFi down — stopping MQTT client (free TLS heap for AP/captive portal)` and `I TELEMETRY_V2: MQTT connected = false`, within about 10 s.
   - Keep the outage under about 10 min. If the Wi-Fi manager brings up its SoftAP meanwhile, that is its normal behaviour: carry on. It logs `APP_WIFI: SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`, and BLE keeps scanning (step 2 needs it); a `portal priority ON` line is a Fail.
2. Wet `<A>`. The valve must close exactly as in T5-03 step 1: RMLEAK before CLOSE, LED RED.
3. Dry `<A>`. Wait for `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`, 10–12 s after the timer line.
4. Within about 1 s of that `AUTO-CLEAR` line, re-wet `<A>` (the T5-04 case, offline).
5. Wait 15 s, dry `<A>` again, and wait for the second `AUTO-CLEAR`.
6. Power the router on. Wait for the reconnect.

**Expected UART while offline, for each event.**

- `W TELEMETRY_V2: Offline — buffering event event`. This is the literal text: the `%s` is the type `event`.
- `I OFFLINE_BUF: Stored event [ob_NN] (NNN bytes), K buffered` **(format)**

The buffered events are:

1. `leak_detected`
2. `auto_close` (`rmleak_asserted:true`: the valve is linked over BLE)
3. `valve_state_changed` (`closed`)
4. `leak_cleared`
5. `rmleak_auto_cleared`
6. `leak_detected`
7. `auto_close`
8. `leak_cleared`
9. `rmleak_auto_cleared`

That makes K = 9. If the step 4 re-wet landed before the first `AUTO-CLEAR` (Δ < 0, see T5-04), events 5 and 7 are absent and K = 7: redo from step 3. K = 9 is fewer than the 16-slot capacity, so there must be no `Buffer full, oldest event overwritten`. Snapshots are dropped while offline, silently.

In step 4, `rmleak_auto_cleared` (event 5) must be buffered, that is its `Stored event [ob_NN]` line printed, **before** the re-wet's `leak_detected` (event 6). Event 5 might be missing if the re-wet coincides with a reconnect pass, but the router is off throughout, so it must not be missing here.

**Expected UART at the reconnect**, in this order:

- `I IOTHUB: WiFi up — restarting MQTT client` (or `W IOTHUB: WiFi up — MQTT held pending SAS token refresh`, followed by a start within 30 s)
- `I IOTHUB: Connected to Azure IoT Hub!`
- `I TELEMETRY_V2: MQTT connected = true`
- `I TELEMETRY_V2: Draining 9 offline event(s) before lifecycle...`
- `I OFFLINE_BUF: Draining 9 buffered event(s)...`
- `I OFFLINE_BUF: Replayed [ob_NN] (NNN bytes)` × 9 **(format)**
- `I OFFLINE_BUF: Drain complete: 9 event(s) published, 0 remaining`
- `I TELEMETRY_V2: Offline drain complete: 9 event(s) replayed`
- `I TELEMETRY_V2: Pub lifecycle: {...}`
- `I IOTHUB: Twin reported (N): {...}` **(format)**
- `I IOTHUB: SNAP trigger=fast` (the valve is ready), or `SNAP trigger=boot`

**Expected IoT Hub.**

- The nine events arrive in exactly the order listed above, then `lifecycle` `{"event":"online","reset_reason":"power_on","provisioned":true,"valve_id":"<V>","lora_sensor_count":0,"ble_leak_sensor_count":4,"rules":{"auto_close_enabled":true,"trigger_mask":7}}`. `reset_reason` is whatever the last boot was: the hub did not reboot. Then the `fast` or `boot` snapshot, showing the current state: `"All devices healthy"` and the valve closed with `rmleak:false`.
- Each replayed event's `ts` matches the UART time of the event it records, within ±2 s, and `ts` never decreases across the nine.
- No message has a `ts` below 1704067200.

**Expected LED.** RED → YELLOW → GREEN → RED → YELLOW → GREEN, exactly as online. The fleet LED does not depend on the cloud.

**Timing.** Every auto-clear is 10.0–12.5 s after its timer line, offline as online. The drain completes before the lifecycle message.

| Result | Pass / Fail | Notes (order as received; any missing event) |
|---|---|---|
| T5-12 | | |

---

### T5-13 — LoRa leak path end to end (only if LoRa sensors and the SX1262 are fitted)

**Smoke:** no. **Covers:** S24 (the LoRa path), N4 (provisioned LoRa packets are evaluated), the 10 s auto-clear from a LoRa source.

**Applicability.** Mark this test **N/A** if the hub PCBA has no SX1262 fitted, as on the current production PCBA. You can confirm that from the boot log: there is no `I APP_LORA: LoRa Task Started. Listening (encrypted mode)...`.

**Start state.** Common preconditions, plus a working LoRa leak sensor `<L1>` in range.

**Steps and expected results**

**(1) Provision the LoRa sensor.** `lora_sensors` replaces only the LoRa list; the valve and the BLE sensors are untouched.

```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-13-prov-lora","cmd":"provision","payload":{"lora_sensors":["<L1>"],"sensor_meta":[{"sensor_type":"lora","sensor_id":"<L1>","location_code":"basement","label":"Sump"}]}}
```

- UART:
  - `I PROVISIONING: LoRa Sensor[0]: <L1>` **(format)**
  - `I HEALTH_ENGINE: Device table loaded: 6 device(s) (+1 added, -0 removed)`
  - `PROV pulse armed: ...`
- The `event` snapshot carries `"lora_sensors":[{"sensor_id":"<L1>","connected":false,"rating":"critical","last_seen_age_s":null,"battery":null,"rssi":null,"leak_state":false,"snr":null,"location":{"code":"basement","label":"Sump"}}]`, and the reason `"Syncing - waiting for 1 device"`. The LED is WHITE until the sensor's first packet.

**(2) Wet `<L1>`.**

- UART:
  - `I APP_LORA: Packet Received. Size: N, RSSI: -NN, SNR: N.N` **(format)**
  - `I APP_LORA: Verified: ID=0x..., Batt=NN%, Leak=0x1, Sent=N, Ack=N` **(format)**. The id here is not zero-padded.
  - `W RULES_ENGINE: LEAK INCIDENT latched by lora sensor <L1>`
  - `W RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by lora sensor <L1>`
  - `I IOTHUB: Event: LoRa Packet from <L1>` **(format)**
  - `BLE_VALVE`: `Writing RMLEAK=1` before `Writing Valve=0`
- IoT Hub:
  ```json
  {"event":"leak_detected","source_type":"lora","sensor_id":"<L1>","leak_state":true,"battery":78,"location":{"code":"basement","label":"Sump"},"rssi":-95}
  ```
  ```json
  {"event":"auto_close","source_type":"lora","sensor_id":"<L1>","rmleak_asserted":true,"location":{"code":"basement","label":"Sump"}}
  ```
  Then `valve_state_changed` `closed`, and a snapshot whose `lora_sensors[0]` shows `connected:true`, `leak_state:true`, `rating:"critical"` and a numeric `snr`, with the reason `"Leak detected: Sump, Leak interlock latched"`.
- LED: RED.

**(3) Dry `<L1>`, and wait for its dry packet** (the LoRa sensor's own reporting interval).

- Expect `leak_cleared` (`source_type:"lora"`), `All sensors clear — auto-clear timer started (10s)`, and then, **10–12.5 s after the dry packet**, `AUTO-CLEAR` and `{"event":"rmleak_auto_cleared","valve_id":"<V>","clear_after_seconds":10}`. The LED goes YELLOW, then GREEN. The valve stays closed.

**(4) Clean up.** Remove the sensor, unless it stays in the fleet:

```json
{"schema":"eflostop.cmd","ver":1,"id":"t5-13-dec-lora","cmd":"decommission","payload":{"target":"lora","sensor_id":"<L1>"}}
```

(The unprovisioned "foreign LoRa sensor" check, S23, is T3-08, with T4-13.)

| Result | Pass / Fail / N/A | Notes |
|---|---|---|
| T5-13 | | |

---

### T5-14 — Contract validator over the full section capture: 0 FAIL

**Smoke:** **yes** (run it on the smoke capture too; about 2 min). **Covers:** the wire contract of every message produced in this section, the cause-before-consequence ordering, and no `auto_close` from a hub with no valve.

This is the section 5 instance of VAL-15 (section 9). The smoke run uses VAL-15 on the smoke capture.

**Steps**

1. Stop the IoT Hub monitor. Copy the whole OUTPUT panel (the Azure IoT Hub channel) from the start of T5-01 to the end of the last test run, and save it as `t5_iothub_capture.txt`. The validator pulls every `eflostop.v2` JSON object out of any surrounding text.
2. From the repo root run:
   ```powershell
   python docs/telemetry/validate_capture.py "C:\path\to\t5_iothub_capture.txt"; "exit=$LASTEXITCODE"
   ```
3. If T5-09 ran on the bench image, validate its capture too. It must also pass: `remaining_s:300` is a valid duration.

**Expected output**

- Final summary: `N messages checked, N pass, 0 fail`, with no `, K ordering violation(s)` or `, K auto_close with no valve` suffix.
- No `--- message ...` blocks with `FAIL` lines, no `--- ORDERING (cause before consequence) ---` section, and no `--- AUTO_CLOSE FROM A HUB WITH NO VALVE ---` section.
- `exit=0`.
- The `message mix` list includes, at least: `snapshot`, `online`, `leak_detected`, `leak_cleared`, `auto_close`, `valve_state_changed`, `cmd_ack`, `rmleak_auto_cleared`, `rmleak_cleared`, `water_access_override_enabled`, `auto_close_blocked_override` and `auto_close_reenabled`, plus `water_access_override_expired` if T5-09 ran.
- A `not exercised by this capture` list that names only `device_offline` is informational, not a failure.

| Messages checked | Fails | Exit code | Pass/Fail | Notes |
|---|---|---|---|---|
| | | | | |

---

### T5-15 — override_enable with an unreachable valve while a leak starts: leak handling delay of 10 s or less

**Merged into T6-18 (section 6).** T6-18 is this procedure (valve PSU off, `override_enable`, a sensor wetted within 2 s of the `override_enable: valve not ready — reconnecting (<=10000ms)` line) run three times, with the four timestamps Ts, Te, Ta and Tp recorded each time. Run T6-18 and copy its first repetition here.

The IoT Hub result this test also checks, in the same run:
- `{"event":"cmd_ack","id":"t6-18-ovr-<n>","cmd":"override_enable","status":"error","error":{"code":"override_enable","detail":"The valve isn't responding. Check its power and connection, then try again."}}`;
- `leak_detected`, then `auto_close` with `"rmleak_asserted":false` (the valve was unreachable);
- no `water_access_override_enabled`.

**Pass criterion (the same in both):** time(`AUTO-CLOSE + RMLEAK triggered`) − time(`eleak <A> — leak=1`) ≤ 10 s; more than 10 s is a FAIL. After the valve is powered, RMLEAK is applied before the valve command and the valve ends CLOSED with RMLEAK set.

**Covers:** council: an iothub_task stall behind a blocking C2D handler (F1, rounds 1 and final).

| eleak → AUTO-CLOSE delay (s) | Valve closed after relink, RMLEAK first? | Pass/Fail | Notes |
|---|---|---|---|
| (from T6-18 rep 1) | | | |

---

### T5-16 — Flapping sensor soak with the 10 s auto-clear (15 min)

**Merged into T6-15 (section 6).** T6-15 is this soak: a sensor flapping every 15-20 s for 15 min with the 10 s auto-clear, three valve relinks from a shield (the valve stays powered), then `valve_open` and `valve_close` to prove the valve is still commandable. It counts the `AUTO-CLOSE`, `AUTO-CLEAR` and `[CMD] Writing RMLEAK=` lines, and its pass criteria are this test's: no `NVS: incident save failed`, no `NVS open failed`, no `ESP_ERR_NVS`, no `RMLEAK cleared externally`, no `water_access_override_enabled`, `free` back within 2 KB of the idle reference and `min_ever` no more than 1 KB below it, and no reboot. Ask the valve firmware owner about wear on the valve's RMLEAK persistence, as T6-15 says.

**Covers:** council: the write rate from the 10 s auto-clear and the owed-clear re-queue (F2, both rounds), the stability of the new timing, and F-01 under repetition.

| Cycles | min_ever baseline → end | False override? | NVS errors? | Pass/Fail | Notes |
|---|---|---|---|---|---|
| (from T6-15) | | | | | |

---

### 5.x Section summary: IDs, coverage and smoke subset

| Test | Title | Smoke |
|---|---|---|
| T5-01 | Provision: per-advertisement snapshots, and the Syncing reason clearing | |
| T5-02 | An identical re-send: one event snapshot, no pulse (merged: run DEC-02) | |
| T5-03 | Leak → auto_close → 10 s auto-clear → valve_open | **yes** |
| T5-04 | Re-wet at the clear (5 runs) (also the main runs of T3-18) | |
| T5-05 | The leak_reset / valve_open guard matrix | **yes** |
| T5-06 | override_enable → blocked leak → override_cancel re-close | |
| T5-07 | override_cancel with no leak and with no window; leak_reset cancelling a window | |
| T5-08 | Valve long-press override (live, after a restart, a quick press) (merged: run T3-15 with two added checks) | |
| T5-09 | Override expiry (bench-only 300 s build) | |
| T5-10 | RMLEAK write and read-back audit | |
| T5-11 | Twin heartbeat interval change | |
| T5-12 | Offline buffering and replay order | |
| T5-13 | LoRa leak path (if fitted) | |
| T5-14 | Validator: 0 FAIL (section 5 instance of VAL-15) | **yes** |
| T5-15 | override_enable with an unreachable valve during a leak (merged: run T6-18) | |
| T5-16 | Flapping sensor soak (merged: run T6-15) | |

## 6. Robustness, upgrade and heap (plan S25, S26; council residual risks)

This section checks that 2.1.4 keeps its configuration through power cuts, reflashes and rollbacks, that repeated and unknown commands are handled safely, and that heap and stack headroom are no worse than 2.1.3. It also covers the council's residual risks on load, the network, NVS and timing. Every quoted log line and constant was checked against the firmware at `d9fa9c8`. The working tree `main/` is identical to it.

Some entries in the CHANGELOG *Known limitations* list are exercised here: E-01 snapshot heap, the DPS block, sensors unheard during a valve connect, the ATT-buffer busy case, the 16-entry offline ring and the pre-existing mixed-key power cut. Treat them as **observe and record**, never as a failure of 2.1.4. The pass criteria below say which outcome is a fail.

### 6.0 Conventions for this section

**Firmware.**
- 2.1.4 is `d9fa9c8` (`PROJECT_VER "2.1.4"`, so `gateway.fw` is `"2.1.4"`).
- The 2.1.3 baseline is `ae4d59a` (`gateway.fw` is `"2.1.3"`).
- **T6-11 only:** a bench-only debug image, described there. It is never shipped.

**Placeholders.**

| Placeholder | Meaning |
|---|---|
| `<GW>` | The gateway ID, e.g. `GW-7C4FADAE69C8`. |
| `<SHORT>` | The short ID. |
| `<DEVICE_ID>` | The IoT Hub device ID, from UART `DPS: hub=%s device=%s`. |
| `<VALVE>` | The provisioned valve MAC, upper case with colons. |
| `<VALVE_B>` | A second valve that has never paired with this hub. |
| `<BLE1>`..`<BLE4>` | The real BLE leak sensors. |
| `<LORA1>` | A real LoRa sensor, e.g. `0x754A6237`. Leave out every LoRa key when none is available. |

**Log notation.**
- Lines are written as `TAG: text`. ESP-IDF adds `I (ms)`, `W (ms)` or `E (ms)` in front.
- **(format)** marks a line printed with printf fields. The fields are shown as in the code (`%d`, `%s` and so on) or filled with the expected value.
- `—` in a quoted line is a real em dash in the firmware string. If the terminal mangles it, search on a substring.
- Run the serial monitor with wall-clock timestamps (`idf.py -p COMx monitor --timestamps`, or the VS Code ESP-IDF monitor with timestamps on). UART times can then be matched to IoT Hub `ts`.
- **Every "measure the gap" step needs these timestamps.**

**Heap line.**
- `MONITOR: heap: free=%lu min_ever=%lu largest_blk=%lu uptime=%lus` (format) prints every 10 s.
- `min_ever` counts from boot, so every heap comparison starts from a power-on.

**Flashing without erase.**
- Build, then run `idf.py -p COMx app-flash`. This writes the app partition only; NVS is not touched.
- Never use `erase-flash`, or "Erase flash" in VS Code, in this section unless a step says so.
- 2.1.3 and 2.1.4 have the same `partitions.csv`: `nvs` 16 KB, `nvs_prov` 16 KB, `otadata`, `phy_init`, `factory`, `ota_0`, `ota_1`.
- The firmware has no OTA code path (Azure OTA is not built). "OTA from 2.1.3" is therefore exercised as an app-only reflash, which leaves NVS and the partitions exactly as an OTA would.

**2.1.3 image.**
1. Run `git worktree add ..\hub_2_1_3 ae4d59a`, then `Copy-Item sdkconfig ..\hub_2_1_3\sdkconfig` (`sdkconfig` is git-ignored, so the worktree has none; without the copy the build gets a default config with NimBLE off), then build in that folder. Before use, confirm with `idf.py size` and the `.bin` size that it is the real 2.1.3 image: `.bss` 36,120 B, `.data` 21,556 B, `.bin` 1,502,240 B (0.4).
2. Flash it from there with `app-flash`.
3. Confirm `gateway.fw` is `"2.1.3"` in the first lifecycle message.

**Sending C2D messages.**
- Use VS Code Azure IoT Hub → the device → "Send C2D Message to Device". Paste each JSON exactly.
- Change twin desired properties with "Edit Device Twin". Add or edit the key under `properties.desired` and save.

> **CAUTION: legacy keyword scan (open hazard, deferred to 2.1.5).** A C2D body that is not a valid `eflostop.cmd` envelope is searched for keywords **anywhere in its text**:
> - `DECOMMISSION_ALL` (a full wipe) and `DECOMMISSION` on its own;
> - `DECOMMISSION_VALVE`, `DECOMMISSION_BLE:` and `DECOMMISSION_LORA:`;
> - `VALVE_OPEN` and `VALVE_CLOSE`;
> - `LEAK_RESET` and `OVERRIDE_CANCEL`;
> - `RULES_CONFIG:` and `SENSOR_META:`.
>
> Never put these words, in any case, into a malformed-JSON or unknown-schema test message.

**Timing constants** (from the code at `d9fa9c8`):

| What | Value | Source |
|---|---|---|
| MONITOR line interval | 10 s | `MONITORING_INTERVAL_MS` |
| Low-heap warning | free < 8192 B | `HEAP_LOW_WATERMARK` |
| RMLEAK auto-clear | 10 s after every source is dry. With the 2 s poll it lands 10–12 s after the last dry report. | `AUTO_CLEAR_TIMEOUT_MS` |
| Auto-close cooldown | 10 s | `AUTO_CLOSE_COOLDOWN_MS` |
| RMLEAK grace (tick Check 2) | 5 s | `RMLEAK_GRACE_PERIOD_MS` |
| `override_enable` reconnect wait | 10 s, polled every 100 ms | `OVERRIDE_CONNECT_TIMEOUT_MS` |
| Valve connect timeout | 30 s | `ble_gap_connect(..., 30000, ...)` |
| GATT-busy wait | 250 ms steps, up to 5 s | `CMD_BUSY_RETRY_MS`, `CMD_BUSY_MAX_MS` |
| GATT procedure pool | 4 | `CONFIG_BT_NIMBLE_GATT_MAX_PROCS` |
| NimBLE GATT procedure timeout | 30 s | NimBLE `ble_gattc` |
| Valve command queue | 10 deep | `ble_cmd_queue` |
| Snapshot spacing | at least 5 s; retry floor 5 s after a failed publish | `SNAP_MIN_INTERVAL_MS`, `SNAP_RETRY_FLOOR_MS` |
| Heartbeat | 300 s by default; twin range 60–3600 s | `SNAPSHOT_INTERVAL_MS` |
| Fast (valve-ready) snapshot ceiling | 150 s after boot or reconnect | `SNAP_FAST_CEILING_MS` |
| Valve disconnect grace | 180 s | `HEALTH_VALVE_DISC_TIMEOUT_MS` |
| Sensor offline, and roll-up grace for unheard devices | 600 s | `HEALTH_BLE_LEAK_TIMEOUT_MS`, `HEALTH_ROLLUP_UNHEARD_MS` |
| BLE scanner telemetry heartbeat | 300 s. An `eleak` line prints only on a change or on this heartbeat. | `BLE_LEAK_HEARTBEAT_MS` |
| BLE sensor bursts (sensor side) | about every 15 s while wet, about every 100 s while dry | sensor FW |
| Whitelist reload | 10 s | `WHITELIST_RELOAD_MS` |
| Offline buffer | 16 entries of up to 512 B each, in the default `nvs` partition, namespace `offline_buf` | `offline_buffer.h/.c` |
| SAS token | Lives 24 h and is re-minted within 6 h of expiry, so about 18 h after the mint. The loop wakes at least every 30 s. | `SAS_TTL_SEC`, `SAS_RENEW_MARGIN_SEC` |
| Wi-Fi reset button hold | 10 s | `HOLD_TIME_MS` |
| Wi-Fi manager | Retries every 5 s. After 3 failed retries it starts the SoftAP; a local patch stops the retry timer while the AP is up. The AP shuts 60 s after STA gets an IP. The AP IP is 10.10.0.1; the AP password is in `sdkconfig` `CONFIG_DEFAULT_AP_PASSWORD`. | `sdkconfig`, `wifi_manager.c` |
| C2D size limit | 8192 B | `MQTT_RX_MAX_MESSAGE` |
| Task watchdog | 5 s | `CONFIG_ESP_TASK_WDT_TIMEOUT_S` |
| Decommission-all reboot | 3 s after the final snapshot | `app_iothub.c` |

**Fleet LED** (GPIO 48, `fleet_led.c`), in precedence order:

| Hub state | Colour |
|---|---|
| No devices provisioned | WHITE |
| Rating CRITICAL (a leak anywhere, or a device offline) | RED |
| Rating WARNING (low battery, weak signal, or "Leak interlock latched") | YELLOW. The CHANGELOG calls this "amber". |
| Provisioned but not yet heard (syncing) | WHITE |
| EXCELLENT or GOOD | GREEN |
| The first ~3 s of boot | OFF |

- Every change prints one `FLEET_LED: rating=%s color=%s effect=SOLID` (format) line. Examples: `FLEET_LED: rating=unprovisioned color=WHITE effect=SOLID`, `FLEET_LED: rating=syncing color=WHITE effect=SOLID`, `FLEET_LED: rating=excellent color=GREEN effect=SOLID`, `FLEET_LED: rating=critical color=RED effect=SOLID`.
- **Network LED** (GPIO 38): `NET_STATUS: wifi=%d mqtt=%d` (format). The colour is ramp red for `wifi=0`, beat blue for `wifi=1 mqtt=0`, and ramp blue for `wifi=1 mqtt=1`.

**Task stacks** (for T6-11):

| Task | Stack (B) |
|---|---|
| `nimble_host` | 4096 |
| `ble_valve` | 4096 |
| `iothub_task` | 10240 |
| `mqtt_task` | 6144 |
| `ble_leak_scan` | 3072 |
| `health_engine` | 3072 |
| `Tmr Svc` | 4096 |
| `fleet_led_task` | 2560 |
| `ble_starter` | 3072 (the task exits after the BLE start) |
| `monitor` | 3072 |

---

### T6-01 — Power cut during a single-device decommission

| Field | Value |
|---|---|
| Purpose | A power loss while a decommission is being saved must leave the hub with either the old device set or the new one, never a mix. The lifecycle, twin and snapshot must agree with what the hub loaded. |
| Covers | S25; provisioning transactional save (CHANGELOG Reliability, Provisioning); BUG-2 (survivors keep their state after a real removal); council F5 non-blocking note F4.NB3 item 3 (mixed keys after a power cut mid-save: pre-existing, observe) |
| Start state | 2.1.4. Valve + `<BLE1>`..`<BLE4>` (+ `<LORA1>`) provisioned and all heard; fleet LED GREEN; MQTT up. Power the hub from a supply you can cut in under 0.5 s (a switched USB hub or the bench PSU output button). |
| Duration | about 45 min (8 cuts) |

**C2D messages.** Replace `<n>` with the run number so every id is unique.

J1, remove `<BLE4>`:
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-01-decom-b4-<n>","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE4>"}}
```
J2, re-add `<BLE4>` (the full BLE list replaces the BLE set):
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-01-readd-b4-<n>","cmd":"provision","payload":{"ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"]}}
```
J3, remove the valve:
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-01-decom-v-<n>","cmd":"decommission","payload":{"target":"valve"}}
```
J4, re-add the valve:
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-01-readd-v-<n>","cmd":"provision","payload":{"valve_id":"<VALVE>"}}
```

**Cut points.**

| Cut | When to cut the power |
|---|---|
| A | As soon as `IOTHUB: Received C2D Message! (%d bytes)` (format) appears. |
| B | As soon as `IOTHUB: !!! DECOMMISSION_BLE: <BLE4> !!!` (or `IOTHUB: !!! DECOMMISSION_VALVE !!!`) appears. |
| C | As soon as the `cmd_ack` appears in the IoT Hub monitor. |
| D | 2 s after the `cmd_ack`, while the reconcile, twin and snapshot are in flight. |

A cut inside the NVS write (a few ms) is a matter of chance. Most runs land before or after it; that is expected.

**Steps**

1. **Runs 1–4: the sensor, cut points A, B, C, D.** Send J1 and cut the power at the run's point.
   - For reference, an uncut run prints, in this order:
     - `C2D_CMD: Envelope cmd='decommission' ver=1 id='t6-01-decom-b4-<n>' payload={"target":"ble_leak_sensor","sensor_id":"<BLE4>"}` (format)
     - `IOTHUB: !!! DECOMMISSION_BLE: <BLE4> !!!`
     - `PROVISIONING: === REMOVING BLE LEAK SENSOR <BLE4> ===`
     - `PROVISIONING: Config saved to NVS successfully`
     - `PROVISIONING: BLE leak sensor <BLE4> removed successfully`
     - `PROVISIONING: Remaining BLE sensors: 3`
     - `HEALTH_ENGINE: Device table loaded: 5 device(s) (+0 added, -1 removed)` (format; 4 devices when no LoRa sensor is provisioned)
2. **Restore power after 5 s.** Expect at boot, within 5 s:
   - `PROVISIONING: Loaded existing config from NVS`
   - `PROVISIONING: State: PROVISIONED`
   - `PROVISIONING: Valve MAC: <VALVE>`
   - `PROVISIONING: LoRa sensors: %d` (format; unchanged)
   - `PROVISIONING: BLE leak sensors: 4` (the old set) **or** `PROVISIONING: BLE leak sensors: 3` (the new set)
   - `PROVISIONING: Rules: auto_close=enabled triggers=0x07`
   - `IOTHUB: Hub is PROVISIONED`
   - `IOTHUB: Starting BLE (valve=<VALVE>, BLE sensors=4)` or `(… BLE sensors=3)`, matching the count above
   - These must not appear: `PROVISIONING: BLE leak sensor count %u in NVS exceeds %d - clamped`, `PROVISIONING: Failed to load BLE leak sensor MACs`, `PROVISIONING: No existing config found, starting UNPROVISIONED`, `IOTHUB: Boot: hub is empty - clearing any persisted rules-engine state`.
   - Record if seen: `OFFLINE_BUF: Corrupt metadata (h=%u t=%u c=%u), resetting` (format).
3. **After MQTT connects:**
   - The lifecycle has `data.provisioned:true`, `valve_id:"<VALVE>"`, and a `ble_leak_sensor_count` equal to the boot count.
   - The twin reported shows the same counts.
   - The first `fast` or `boot` snapshot lists exactly `<BLE1>`..`<BLE4>` (old set) or `<BLE1>`..`<BLE3>` (new set) in `data.ble_leak_sensors[].sensor_id`.
   - **LED:** WHITE (syncing) at boot, then GREEN once every sensor is heard.
   - **Timing:** sensors are heard within about 120 s (they burst about every 100 s while dry). The boot window is 180 s.
4. **Put the old or new set back.**
   - If the old set loaded, send J1 again with a new `<n>`. Expect `cmd_ack` `status:"ok"`.
   - If the new set loaded, send J2. Expect `HEALTH_ENGINE: Device table loaded: 5 device(s) (+1 added, -0 removed)` (format), `IOTHUB: PROV pulse armed: every 30 s for 300 s, plus on every sensor packet` and `IOTHUB: Commission: fast snapshot armed (all-devices-seen, else <=%ds; refreshes on late devices)` (format).
5. **Runs 5–8: the valve.** Repeat steps 1–4 with J3 (cut points A–D) and J4.
   - At boot, expect `PROVISIONING: Valve MAC: <VALVE>` (old) or `PROVISIONING: Valve MAC: ` with nothing after it (new).
   - **Old set:** the valve relinks: `BLE_VALVE: [SCAN] Target MAC matched - connecting to provisioned valve: <VALVE>`, then `SETUP COMPLETE - READY FOR GATT` within 60 s. The snapshot has `data.valve.valve_id:"<VALVE>"` and `last_seen_age_s:0`.
   - **New set:** the snapshot has `"valve":{}`, the twin has `valve_id:null`, and no `[SCAN] Target MAC matched` appears for `<VALVE>`. J4 then relinks the valve within about 30 s.

**Pass criteria**
- In every run, the set after the reboot equals the old set or the new set exactly.
- The lifecycle, twin and first snapshot agree with the boot-log counts.
- The next decommission or provision works.
- There is no reboot loop, no panic, and no reset reason other than `power_on`.

**Observe and record, not a 2.1.4 fail**
- **A mixed set:** for example the boot count reads 3 but `<BLE4>` is still listed and a survivor is missing, or `Failed to load BLE leak sensor MACs` appears. This is the known pre-existing mixed-key case (F4.NB3 item 3), planned for later. Keep the UART and escalate.

| ID | Result | Notes (cut point per run, old or new set, anomalies) |
|---|---|---|
| T6-01 | [ ] Pass [ ] Fail | |

---

### T6-02 — Power cut during decommission-all

| Field | Value |
|---|---|
| Purpose | A power cut between the decommission-all `cmd_ack` and the reboot must leave an empty, consistent hub: no restored incident latch or override, and a single empty `boot` snapshot. |
| Covers | S25, S11 (the power-cut half), BUG-3/BUG-6 (the empty shape), council:power cut mid-decommission-all (E2F council[4] risk 5, final votes[4] risk 7), F4.NB3 item 2 (old buffered events replay: pre-existing, observe), CHANGELOG known limitation (a DPS registration blocks up to 60 s per attempt) |
| Start state | 2.1.4 with valve + 4 BLE, all heard, MQTT up. An override window is active: wet `<BLE1>`, wait for `auto_close`, send `{"schema":"eflostop.cmd","ver":1,"id":"t6-02-ovr","cmd":"override_enable"}`, see `water_access_override_enabled`, then dry `<BLE1>`. A hub name is set. |
| Duration | about 30 min (3 variants, each followed by a re-commission) |

The council asked for events to be in the offline buffer at the decommission. That cannot be arranged here: the C2D needs MQTT up, and every connect drains the buffer first. So step 3 only records whether an old event replays.

**C2D message**
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-02-decom-all-<n>","cmd":"decommission","payload":{"target":"all"}}
```

**Steps**

1. Send the message.
   - Expect on UART:
     - `IOTHUB: !!! DECOMMISSION_ALL !!!`
     - `PROVISIONING: === DECOMMISSIONING DEVICE ===`
     - `PROVISIONING: Decommissioning successful!`
     - `PROVISIONING: Device state: UNPROVISIONED`
     - `PROVISIONING: All provisioning data erased from NVS`
     - `SENSOR_META: All sensor metadata cleared`
     - `HUB_IDENT: Hub name cleared`
     - `RULES_ENGINE: NVS: rules-engine persistent state cleared`
     - `RULES_ENGINE: Rules-engine state reset (hub empty / decommission)`
     - `TELEMETRY_V2: Telemetry settings cleared — snapshot interval back to 300s`
     - then, from `iothub_task`, `TELEMETRY_V2: Pub snapshot: {...}` with `"reason":"decommission"`
     - `OFFLINE_BUF: Buffer cleared`
     - `IOTHUB: Decommissioned — restarting in 3s...`
   - Cut the power at:
     - **Variant A:** as soon as `!!! DECOMMISSION_ALL !!!` appears;
     - **Variant B:** as soon as the `cmd_ack` shows in the IoT Hub monitor, before `Decommissioned — restarting in 3s...`;
     - **Variant C:** no cut, as the reference: the hub restarts by itself 3 s later.
2. Restore power after 5 s (variants A and B). Expect at boot:
   - `PROVISIONING: No existing config found, starting UNPROVISIONED`
   - `IOTHUB: Boot: hub is empty - clearing any persisted rules-engine state`
   - `RULES_ENGINE: NVS: rules-engine persistent state cleared`
   - `IOTHUB: Hub is UNPROVISIONED - waiting for provisioning JSON from Azure`
   - `HUB_IDENT: Hub name   : (not set)`
   - no `IOTHUB: Starting BLE (...)`: an empty hub does not start BLE
   - These must not appear: `RULES_ENGINE: NVS: restored incident latch — pending reconcile with valve`, `RULES_ENGINE: NVS: restored override window (expiry=%lu, remaining=%lds)`.
   - **DPS:** `DPS: No cached assignment, performing DPS registration...`. Record how long it takes until `IOTHUB: Connected to Azure IoT Hub!`. Up to 60 s per attempt is the known limitation.
   - **Variant A only:** if the cut landed before the erase, the hub boots with `Loaded existing config from NVS` and its old devices. Record it and send the message again.
3. Record which of these appears:
   - `OFFLINE_BUF: Init: buffer empty` is expected.
   - `OFFLINE_BUF: Init: %d buffered event(s) pending from before reboot` (format), followed by a replay after connect, is F4.NB3 item 2 (pre-existing): record it.
4. **IoT Hub after connect**, for every variant:
   - Lifecycle: `{"event":"online","reset_reason":"power_on","provisioned":false,"lora_sensor_count":0,"ble_leak_sensor_count":0,"rules":{"auto_close_enabled":true,"trigger_mask":7}}`. There is no `valve_id` key, and `reset_reason` is `"software"` for variant C.
   - Twin reported: `provisioned:false`, `valve_id:null`, both counts 0, `hub_name:""`, and `snapshot_interval_s:300` (the default restored by the decommission) **unless** the twin desired section holds `snapshot_interval_s` (0.16 sets it to 60 before every test): then the twin GET after the connect re-applies it, you see `IOTHUB: Twin: snapshot_interval_s = 60` and `TELEMETRY_V2: Snapshot interval set to 60s (persisted)`, and the reported value is 60. Both are correct: record which (as DEC-18 step 10).
   - Then exactly **one** `boot` snapshot. Full expected JSON (no `gateway.name`, because the name was cleared):

```json
{"schema":"eflostop.v2","ts":<epoch>,"gateway":{"id":"<GW>","short_id":"<SHORT>","fw":"2.1.4","uptime_s":<n>},"type":"snapshot","data":{"reason":"boot","system_health":{"rating":"excellent","reason":"No devices provisioned"},"valve":{},"lora_sensors":[],"ble_leak_sensors":[],"rules":{"auto_close_enabled":true,"trigger_mask":7},"override_active":false}}
```

   - **Variant C also:** before the reboot, the final snapshot has the same `data` with `"reason":"decommission"` and `"override_active":false`.
   - After that, `heartbeat` snapshots with the same `data` arrive at the interval in force: every 300 s (−1/+3 s), or every 60 s (−1/+3 s) when the twin desired re-applied 60. Watch at least 2.
5. **LED:** `FLEET_LED: rating=unprovisioned color=WHITE effect=SOLID`.
6. **Clean-up:** re-provision the valve + 4 BLE for the next test.

**Timing:** the boot snapshot arrives within about 60 s of the lifecycle message.

**Pass criteria**
- The hub is empty and consistent after every variant.
- No incident latch or override window is restored.
- Exactly one `boot` snapshot per connect, of the shape above.
- Heartbeats continue.

**Observe and record:** old-event replay (step 3) and the DPS time.

| ID | Result | Notes (variant, DPS time, replayed events) |
|---|---|---|
| T6-02 | [ ] Pass [ ] Fail | |

---

### T6-03 — Upgrade 2.1.3 → 2.1.4 without erase

| Field | Value |
|---|---|
| Purpose | An upgrade keeps every stored setting: devices, sensor metadata, the rules opt-out, the hub name, the snapshot interval, the DPS cache, an active override window (run A) and a latched incident (run B). "Hub is PROVISIONED" with unchanged counts. |
| Covers | S25; CHANGELOG *Upgrade notes*; council:upgrade/rollback (E2F council[4] risk 4, final votes[4] risk 6); E-03 (an override restored after a power-on that lost the clock); N1 (boot order); F4.NB3 item 4 (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, but `main/` never calls `esp_ota_mark_app_valid_cancel_rollback()`: no effect on a serial-flashed factory app; note it for the OTA work) |
| Start state | 2.1.3 image built (see 6.0). Valve on the PSU in the Good band (above about 5.6 V). 4 BLE sensors, a LoRa sensor if available. |
| Duration | about 60 min (two runs) |

**Run A: override window and rules opt-out**

1. **Flash 2.1.3** (`app-flash`). If its state is unknown, first send `{"schema":"eflostop.cmd","ver":1,"id":"t6-03-clean","cmd":"decommission","payload":{"target":"all"}}` and let it reboot.
2. **Provision on 2.1.3.** Leave out `lora_sensors` if there is no LoRa sensor.
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-03-prov","cmd":"provision","payload":{"valve_id":"<VALVE>","ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"],"lora_sensors":["<LORA1>"],"auto_close_enabled":true,"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE1>","location_code":"kitchen","label":"T6 Sink"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE2>","location_code":"laundry","label":"T6 Washer"}]}}
```
3. **Name the hub:**
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-03-name","cmd":"set_hub_name","payload":{"name":"T6 Upgrade Hub"}}
```
4. **Set the interval.** In the twin, set `properties.desired.snapshot_interval_s` to `120`. Expect twin reported `snapshot_interval_s:120`.
5. **Start an override window.** Wait until every device is heard (GREEN).
   - Wet `<BLE1>`: expect `leak_detected` and `auto_close`, and the valve closes.
   - Send `{"schema":"eflostop.cmd","ver":1,"id":"t6-03-ovr","cmd":"override_enable"}`. Expect a `cmd_ack` ok, `water_access_override_enabled` with `trigger:"c2d_command"`, and the valve opens.
   - Dry `<BLE1>`.
   - Write down `E`, the override's `expires_ts` from the event or a snapshot.
6. **Opt out of auto-close:**
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-03-rc-off","cmd":"rules_config","payload":{"auto_close_enabled":false}}
```
7. **Save the 2.1.3 reference:** copy the latest 2.1.3 snapshot and twin reported into the notes.
8. **Flash 2.1.4** (`app-flash`, no erase). The flasher resets the chip, so the clock is lost.
9. **Check the 2.1.4 boot UART:**
   - `HUB_IDENT: Firmware version: v2.1.4`
   - `HUB_IDENT: Hub name   : T6 Upgrade Hub`
   - `PROVISIONING: Loaded existing config from NVS`
   - `PROVISIONING: State: PROVISIONED`
   - `PROVISIONING: Valve MAC: <VALVE>`
   - `PROVISIONING: LoRa sensors: 1` (or 0)
   - `PROVISIONING: BLE leak sensors: 4`
   - `PROVISIONING: Rules: auto_close=disabled triggers=0x07`
   - `SENSOR_META: Loaded 2 sensor metadata entries from NVS`
   - `TELEMETRY_V2: Snapshot interval 120s restored from NVS`
   - `RULES_ENGINE: NVS: restored override window (expiry=<E>, remaining=%lds)` (format). With the clock lost, `remaining` counts from this power-on, so it reads about 86400 minus the uptime. It is not yet the real remaining time.
   - `RULES_ENGINE: Initialized: auto_close=disabled triggers=0x07`
   - `IOTHUB: Hub is PROVISIONED`
   - `IOTHUB: Starting BLE (valve=<VALVE>, BLE sensors=4)`, which prints **before** `APP_WIFI: Connected! IP: %s` (N1)
   - `DPS: Loaded cached assignment from NVS`, with no re-registration
   - These must not appear: `IOTHUB: Boot: hub is empty - clearing any persisted rules-engine state`, `PROVISIONING: No existing config found, starting UNPROVISIONED`.
10. **At the valve link:**
    - `BLE_VALVE: [SCAN] Target MAC matched - connecting to provisioned valve: <VALVE>`
    - `SETUP COMPLETE - READY FOR GATT`
    - `RULES_ENGINE: Reconnected: override window active — skipping auto-close`
    - Must not appear: `Reconnected: valve RMLEAK active, hub incident clear - RMLEAK clear owed by the hub, not re-latching`. The owed flag is always false after a boot.
    - **Timing:** the link is up within 60 s of boot.
11. **IoT Hub after `Time synced: ...` and the connect:**
    - **Lifecycle:** `gateway.fw:"2.1.4"`, `gateway.name:"T6 Upgrade Hub"`, and `data` = `{"event":"online","reset_reason":"power_on","provisioned":true,"valve_id":"<VALVE>","lora_sensor_count":1,"ble_leak_sensor_count":4,"rules":{"auto_close_enabled":false,"trigger_mask":7}}`.
    - **Twin reported:** `fw_version:"2.1.4"`, `hub_name:"T6 Upgrade Hub"`, `provisioned:true`, `valve_id:"<VALVE>"`, the same counts, `auto_close_enabled:false`, `trigger_mask:7`, `snapshot_interval_s:120`.
    - **First snapshot** (`fast` at valve-ready, else `boot`). Full expected JSON:

```json
{"schema":"eflostop.v2","ts":<epoch>,"gateway":{"id":"<GW>","short_id":"<SHORT>","name":"T6 Upgrade Hub","fw":"2.1.4","uptime_s":<n>},"type":"snapshot","data":{"reason":"fast","system_health":{"rating":"excellent","reason":"All devices healthy"},"valve":{"valve_id":"<VALVE>","state":"open","battery":<21..100>,"leak_state":false,"rmleak":false,"connected":true,"fw_version":"<valve fw>","rating":"excellent","last_seen_age_s":0},"lora_sensors":[{"sensor_id":"<LORA1>","connected":true,"rating":"excellent","last_seen_age_s":<n>,"battery":<n>,"rssi":<n>,"leak_state":false,"snr":<n|null>,"location":{"code":"unknown","label":""}}],"ble_leak_sensors":[{"sensor_id":"<BLE1>","connected":true,"rating":"excellent","last_seen_age_s":<n>,"battery":<n>,"rssi":<n>,"leak_state":false,"fw_version":"<fw>","location":{"code":"kitchen","label":"T6 Sink"}},{"sensor_id":"<BLE2>","connected":true,"rating":"excellent","last_seen_age_s":<n>,"battery":<n>,"rssi":<n>,"leak_state":false,"fw_version":"<fw>","location":{"code":"laundry","label":"T6 Washer"}},<BLE3 entry>,<BLE4 entry>],"rules":{"auto_close_enabled":false,"trigger_mask":7},"override_active":true,"override_remaining_s":<E - ts>,"expires_ts":<E>}}
```

   Notes on this snapshot:
   - `<BLE3 entry>` and `<BLE4 entry>` have the same shape as the `<BLE1>` entry, with `"location":{"code":"unknown","label":""}` (no metadata was set for them). With no LoRa sensor, `lora_sensors` is `[]` and the lifecycle count is 0.
   - `system_health` may read `good` with "1 sensor signal weak" on a weak link. `rating` and `reason` must agree.
   - A sensor not yet heard shows `connected:false` with null fields, and the reason reads "Syncing - waiting for N device(s)".
   - `override_remaining_s` must equal `E − ts` within ±30 s. `expires_ts` appears only once the clock has synced, so it is present in any snapshot after the MQTT connect.
   - Compare every field with the 2.1.3 reference from step 7. Only `gateway.fw`, `uptime_s`, the live figures and the shapes changed in 2.1.4 (`battery:null` while unknown) may differ.
12. **Heartbeats** arrive every 120 s ±5 s. Watch 3.
13. **Opt-out still in force:** wet `<BLE2>`. Expect `IOTHUB: Event: BLE Leak <BLE2> leak=1 batt=%d` (format) and `leak_detected`, but **no** `RULES_ENGINE: LEAK INCIDENT latched by ...` and no `auto_close`. Dry `<BLE2>`.
14. **LED:** WHITE (syncing) at boot, then GREEN; RED while `<BLE2>` is wet (the leak is in the rating even with auto-close off); GREEN again after it dries.
15. **Clean-up:**
    - Send `{"schema":"eflostop.cmd","ver":1,"id":"t6-03-ovr-cancel","cmd":"override_cancel"}`. Expect `RULES_ENGINE: OVERRIDE WINDOW CANCELLED (remaining_s=%ld)` (format).
    - Send `{"schema":"eflostop.cmd","ver":1,"id":"t6-03-rc-on","cmd":"rules_config","payload":{"auto_close_enabled":true}}`. Expect `RULES_ENGINE: Config updated: auto_close=enabled triggers=0x07`.

**Run B: incident latched across the upgrade**

1. **Flash 2.1.3** (`app-flash`); the devices from run A stay. Send rules_config `auto_close_enabled:true`, as in step 15.
2. **Latch an incident:** wet `<BLE1>` and keep it wet. Expect `auto_close`; the valve ends closed with RMLEAK set.
3. **With `<BLE1>` still wet, flash 2.1.4** (`app-flash`).
4. **Boot UART:**
   - `RULES_ENGINE: NVS: restored incident latch — pending reconcile with valve`
   - the same provisioning lines as in run A, with `auto_close=enabled`
5. **LED:** YELLOW from the first evaluation (the restored latch raises the WARNING floor), then RED once `<BLE1>` is heard wet.
6. **At the valve link, one of these:**
   - `RULES_ENGINE: Reconnected: hub + valve RMLEAK in sync`, when `<BLE1>` has not yet been heard since boot;
   - `RULES_ENGINE: Reconnected with 1 active leak(s) — valve already closed + RMLEAK asserted, nothing to do`.

   These must never appear:
   - `Reconnected: valve RMLEAK active, hub incident clear - RMLEAK clear owed by the hub, not re-latching`
   - `RMLEAK cleared externally (valve override) — starting 24h override window`
   - `Reconnected: hub incident + valve open + RMLEAK clear — inferring physical override, starting 24h window`
7. **Check the lock:**
   - Send `{"schema":"eflostop.cmd","ver":1,"id":"t6-03-open-locked","cmd":"valve_open"}`. Expect a `cmd_ack` `status:"error"` with `error.detail` = `Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.`
   - The snapshot has `valve.state:"closed"`, `valve.rmleak:true`, and a `system_health.reason` that contains `Leak detected: T6 Sink` and `Leak interlock latched`, with rating `critical`.
8. **Dry `<BLE1>`.**
   - Expect `RULES_ENGINE: All sensors clear — auto-clear timer started (10s)`, then 10–12 s later `RULES_ENGINE: AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`, and `rmleak_auto_cleared` with `clear_after_seconds:10`.
   - The valve stays closed.
   - **LED:** RED → YELLOW for about 10–12 s → GREEN.

**Pass criteria**
- Every stored setting listed in *Purpose* is present after the upgrade.
- The counts are unchanged and `Hub is PROVISIONED` prints.
- The override window carries over and, after the sync, `override_remaining_s` matches its real expiry.
- The incident latch carries over. There is no false override and no owed-clear line.
- The DPS cache is reused.

| ID | Result | Notes |
|---|---|---|
| T6-03 run A | [ ] Pass [ ] Fail | |
| T6-03 run B | [ ] Pass [ ] Fail | |

---

### T6-04 — Rollback 2.1.4 → 2.1.3 without erase

| Field | Value |
|---|---|
| Purpose | A rollback keeps provisioning. 2.1.3 must read every offline-buffer entry that 2.1.4 wrote. |
| Covers | S25; CHANGELOG *Upgrade notes* (rollback); E-22 (2.1.4 never writes back a stamped entry longer than 512 B); council:upgrade/rollback (E2F council[4] risk 4 step 3, final votes[4] risk 6 step 3) |
| Start state | 2.1.4 after T6-03 run B clean-up: valve + 4 BLE, meta, name, interval 120 s, auto-close on, clock synced, MQTT up, all dry. **Power off any neighbour valve:** 2.1.3 brings P0-a back. |
| Duration | about 25 min |

**Steps**

1. **Take MQTT down** by unplugging the router's WAN cable; keep Wi-Fi up.
   - Wait for `IOTHUB: Disconnected.` or `TELEMETRY_V2: MQTT connected = false`. This takes up to about 2 min (≤ 120 s: MQTT keepalive 60 s, up to about 2× for a WAN-only outage, 0.15). Do not start step 2 before it.
2. **Buffer some events.** Wet `<BLE2>` for about 20 s, then dry it, wait for the hub's `leak=0` line (≤ 110 s, 0.11), then 15 s.
   - Expect, per event: `TELEMETRY_V2: Offline — buffering event event` and `OFFLINE_BUF: Stored event [ob_%02u] (%u bytes), %d buffered` (format).
   - Expected events: `leak_detected`, `auto_close`, `leak_cleared`, `rmleak_auto_cleared`, and possibly `valve_state_changed` or health events. Write down N, the final buffered count.
3. **With the WAN still unplugged, flash 2.1.3** (`app-flash`).
4. **2.1.3 boot UART:**
   - `PROVISIONING: Loaded existing config from NVS`
   - `PROVISIONING: BLE leak sensors: 4`
   - `PROVISIONING: Rules: auto_close=enabled triggers=0x07`
   - `IOTHUB: Hub is PROVISIONED`
   - `HUB_IDENT: Hub name   : T6 Upgrade Hub`
   - `TELEMETRY_V2: Snapshot interval 120s restored from NVS`
   - `OFFLINE_BUF: Init: N buffered event(s) pending from before reboot` (format)
   - On 2.1.3, BLE starts only after the IP address; that is the old behaviour and expected.
5. **Plug the WAN back in.** 2.1.3 prints:
   - `OFFLINE_BUF: Draining N buffered event(s)...` (format)
   - `OFFLINE_BUF: Replayed [ob_NN] (%u bytes)` (format), once per event
   - `OFFLINE_BUF: Drain complete: N event(s) published, 0 remaining` (format)
   - This must not appear: `OFFLINE_BUF: Read '%s' failed: %s, skipping`.
6. **IoT Hub:**
   - The N events arrive in their original order. Their `gateway.fw` is `"2.1.4"`, because 2.1.4 built them, and each `ts` is at least 1704067200 and within ±2 s of step 2's wall-clock time.
   - Then the 2.1.3 lifecycle (`gateway.fw:"2.1.3"`, `provisioned:true`, the same counts), the twin, and a snapshot with the same 4 BLE sensors and labels.
   - Run `python docs/telemetry/validate_capture.py "<capture>"` and note its result. It may flag 2.1.3 shapes; only malformed JSON is a fail here.
7. **Reflash 2.1.4** (`app-flash`). Check the step-4 lines again on 2.1.4: `Loaded existing config from NVS`, `BLE leak sensors: 4`, `Hub is PROVISIONED`. The configuration has now survived three image changes.

**LED:** the same colours as in T6-03 on both images. RED while `<BLE2>` is wet, then YELLOW and GREEN (30–60 s of YELLOW on 2.1.3, 10–12 s on 2.1.4).

**Pass criteria**
- Provisioning, metadata, name and interval survive 2.1.4 → 2.1.3 → 2.1.4.
- 2.1.3 replays every buffered 2.1.4 event with a real `ts` and no read failure.

| ID | Result | Notes (N buffered, N replayed) |
|---|---|---|
| T6-04 | [ ] Pass [ ] Fail | |

---

### T6-05 — Repeated command IDs

| Field | Value |
|---|---|
| Purpose | Repeating a command id executes and acks each copy on its own merits; the hub does not de-duplicate (deferred to 2.1.5). A repeat of a destructive command is harmless: the second copy of a removal returns an error. An identical re-sent `provision` adds nothing and does not restart the placement pulse. |
| Covers | S12 (duplicate id `decom-b-002`); CHANGELOG deferred item (command-id de-duplication); council:provision adding no device leaves the pulse off (E2F council[2] risk 8 and final votes[2] risk 11, both halves here: UART and wire); `cmd_ack` contract (`id` echoed, `error.code` = command name) |
| Start state | 2.1.4 with valve + 4 BLE, all heard, no leak, valve open, MQTT up. |
| Duration | about 10 min (smoke candidate) |

**Steps**

1. **Identical provision twice.** Send this message, then the **same message again** 10 s later. It uses the same id and payload and has no `auto_close_enabled`, so the trigger mask is left alone.
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-05-prov","cmd":"provision","payload":{"valve_id":"<VALVE>","ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"]}}
```
   - **UART, each time:**
     - `IOTHUB: C2D cmd='provision' ver=1 id='t6-05-prov'`
     - `IOTHUB: Provisioning JSON detected`
     - `PROVISIONING: Provisioning completed successfully!`
     - `PROVISIONING: Config saved to NVS successfully`
     - `HEALTH_ENGINE: Device table loaded: 5 device(s) (+0 added, -0 removed)` (format)
     - `BLE_VALVE: [TASK] CMD: CONNECT` and the warning `BLE_VALVE: [SCAN] Already connected`
     - `IOTHUB: SNAP trigger=event:provision` (format)
   - **Must not appear:** `IOTHUB: PROV pulse armed: every 30 s for 300 s, plus on every sensor packet`, `IOTHUB: Commission: fast snapshot armed (...)`.
   - **IoT Hub:** two `cmd_ack` with the same id: `{"event":"cmd_ack","id":"t6-05-prov","cmd":"provision","status":"ok"}`. Two twin reported updates, and two `event` snapshots, since the commands are 10 s apart and the clamp is 5 s. Both are identical apart from `ts` and the live figures, and "All devices healthy".
   - **LED:** GREEN throughout.
2. **Close twice.** Send this message twice, 2 s apart.
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-05-close","cmd":"valve_close"}
```
   - **UART, each time:** `IOTHUB: Command: VALVE_CLOSE`, then `BLE_VALVE: [CMD] Writing Valve=0`.
   - **IoT Hub:** two `cmd_ack` `ok` with `id:"t6-05-close"`, one `valve_state_changed` to closed, and snapshots with `valve.state:"closed"`.
   - **Timing:** closed within about 3 s.
   - Then send `{"schema":"eflostop.cmd","ver":1,"id":"t6-05-open","cmd":"valve_open"}`. Expect ack `ok` and the valve opens.
3. **Decommission twice (S12).** Send this message twice, 10 s apart.
```json
{"schema":"eflostop.cmd","ver":1,"id":"decom-b-002","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE4>"}}
```
   - **First copy:** `PROVISIONING: BLE leak sensor <BLE4> removed successfully` and `HEALTH_ENGINE: Device table loaded: 4 device(s) (+0 added, -1 removed)`. Ack `ok`, then an `event` snapshot with 3 BLE sensors whose values are unchanged (BUG-2).
   - **Second copy:** `PROVISIONING: BLE sensor <BLE4> not found in provisioned list`. Ack `{"event":"cmd_ack","id":"decom-b-002","cmd":"decommission","status":"error","error":{"code":"decommission","detail":"ble sensor decommission failed"}}`.
   - After the second copy there is **no** `SNAP trigger=event:decommission` and **no** `Device table loaded` line.
4. **Re-add the sensor** (the second half of the council risk):
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-05-readd","cmd":"provision","payload":{"ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"]}}
```
   - **UART:** `HEALTH_ENGINE: Device table loaded: 5 device(s) (+1 added, -0 removed)`, `IOTHUB: PROV pulse armed: every 30 s for 300 s, plus on every sensor packet`, and `IOTHUB: Commission: fast snapshot armed (...)`.
   - **Snapshots:** `system_health.reason` reads "Syncing - waiting for 1 device" until `<BLE4>` is heard (up to about 100 s dry, 600 s at most), then "All devices healthy". The pulse snapshots come every 30 s for 5 min, plus one per sensor packet.
   - **LED:** WHITE (syncing) until `<BLE4>` is heard, then GREEN.

**Pass criteria**
- Each copy is executed and acked independently, with the id echoed.
- The second removal is an error with no snapshot.
- The identical provision arms no pulse; the re-add does.
- The cloud side must de-duplicate by `id`. Record that the hub does not.

| ID | Result | Notes |
|---|---|---|
| T6-05 | [ ] Pass [ ] Fail | |

---

### T6-06 — Unknown device IDs, unknown targets and commands, malformed messages

| Field | Value |
|---|---|
| Purpose | Unknown or invalid identifiers and commands are refused with the documented error ack. Nothing is changed, and there is no snapshot or reconcile. Messages that cannot be parsed are dropped with no ack. An over-long id is truncated safely. |
| Covers | S12 (unknown MAC; a valve decommission on a hub with no valve → `error`, per the CHANGELOG *Wire changes*); `C2D_COMMANDS.md` §3.4 and §6.1–6.2; the robustness of the `cmd_ack` id buffer (63 chars) |
| Start state | 2.1.4, valve + 4 BLE provisioned, MQTT up. Step 5 needs a hub **without** a valve (the sensors-only hub of T6-09 or T6-12); skip it on a valve hub, where it would remove the valve. |
| Duration | about 10 min (smoke candidate) |

**Steps.** Send each message and wait about 5 s between them.

1. **A valid-format MAC that is not provisioned:**
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-06-unk-ble","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"00:80:E1:00:00:01"}}
```
   - UART: `PROVISIONING: BLE sensor 00:80:E1:00:00:01 not found in provisioned list`.
   - Ack: `status:"error"`, `error:{"code":"decommission","detail":"ble sensor decommission failed"}`.
2. **An invalid MAC:**
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-06-bad-mac","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"ZZ"}}
```
   - UART: `PROVISIONING: Invalid MAC format: ZZ`.
   - Ack: the same error as step 1.
3. **An unknown LoRa id:**
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-06-unk-lora","cmd":"decommission","payload":{"target":"lora","sensor_id":"0x00000001"}}
```
   - UART: `IOTHUB: !!! DECOMMISSION_LORA: 0x00000001 !!!` and `PROVISIONING: Sensor 0x00000001 not found in provisioned list`.
   - Ack: `error:{"code":"decommission","detail":"lora sensor decommission failed"}`.
4. **An unknown target, then a missing one:**
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-06-unk-target","cmd":"decommission","payload":{"target":"hub"}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-06-no-target","cmd":"decommission","payload":{}}
```
   - Acks: `detail:"unknown decommission target"`, then `detail:"missing decommission target"`.
5. **Valve-less hub only: a valve decommission with no valve.**
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-06-no-valve","cmd":"decommission","payload":{"target":"valve"}}
```
   - UART: `IOTHUB: !!! DECOMMISSION_VALVE !!!` and `PROVISIONING: No valve provisioned - nothing to remove`.
   - Ack: `detail:"valve decommission failed"`. Up to 2.1.3 this was `ok`.
6. **An unknown command:**
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-06-unk-cmd","cmd":"reboot_now"}
```
   - UART: `IOTHUB: Unknown command: reboot_now`.
   - Ack: `{"event":"cmd_ack","id":"t6-06-unk-cmd","cmd":"reboot_now","status":"error","error":{"code":"reboot_now","detail":"unknown command"}}`.
7. **An envelope with no id.** The payload repeats the current setting, so nothing changes.
```json
{"schema":"eflostop.cmd","ver":1,"cmd":"rules_config","payload":{"auto_close_enabled":true}}
```
   - The ack has **no `id` key**: `{"event":"cmd_ack","cmd":"rules_config","status":"ok"}`.
   - Then `RULES_ENGINE: Config updated: auto_close=enabled triggers=0x07` and an `event` snapshot.
8. **An over-long id (71 characters).**
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-06-long-012345678901234567890123456789012345678901234567890123456789","cmd":"rules_config","payload":{"auto_close_enabled":true}}
```
   - The UART line `C2D_CMD: Envelope cmd='rules_config' ver=1 id='t6-06-long-0123456789012345678901234567890123456789012345678901' payload=...` shows the id cut to 63 characters.
   - The ack's `data.id` is the same 63-character string.
   - There is no crash, and the next command works.
9. **Malformed JSON.** Send this exact text, which is not valid JSON and contains no keyword:
```text
{"schema":"eflostop.cmd","ver":1,"id":"t6-06-broken","cmd":"rules_config"
```
   - UART: `C2D_CMD: Malformed C2D JSON — ignoring (not valid JSON)`, then `IOTHUB: Unrecognized C2D payload`.
   - **No ack.**
10. **An unknown schema that has a `cmd`:**
```json
{"schema":"eflostop.cmd.v9","id":"t6-06-schema","cmd":"rules_config"}
```
   - UART: `C2D_CMD: C2D has 'cmd' but unrecognized schema — ignoring`, then `IOTHUB: Unrecognized C2D payload`.
   - **No ack.**

**After every error in steps 1–6:**
- There must be no `IOTHUB: SNAP trigger=event:...` for it, no `HEALTH_ENGINE: Device table loaded: ...` line, and no twin update.
- The device set, the latest snapshot and the twin are unchanged.
- **LED:** GREEN throughout.

**Pass criteria:** every ack's `status`, `code` and `detail` match exactly; nothing changes; unparsable messages get no ack; the id is truncated to 63 characters; there is no reboot.

| ID | Result | Notes |
|---|---|---|
| T6-06 | [ ] Pass [ ] Fail | |

---

### T6-07 — Heap like-for-like: valve + 4 BLE hub rebooted with Wi-Fi (2.1.3 vs 2.1.4)

| Field | Value |
|---|---|
| Purpose | The S26 measurement, case (a), for the VAL-13 release gate: the minimum free heap on 2.1.4 must be no lower than 2.1.3 like-for-like (the VAL-13 rule, section 9.3). This checks the heap after G4b (BLE started at boot, before Wi-Fi and TLS), which has never been measured. |
| Covers | S26; E-20 (static RAM about +181 B and about 50 B of permanent heap); council:heap after G4b unmeasured (E2F council[1] risk 0, final votes[1] risk 0) |
| Start state | Valve + 4 BLE provisioned (the same devices and room for both images), auto-close on, heartbeat 300 s, Wi-Fi and internet up. No neighbour valve powered. |
| Duration | about 3 h (3 runs × 25 min per image, plus the reflashes) |

**Procedure.** Do it on 2.1.3 first, then on 2.1.4 (reflash without erase; provisioning carries over, see T6-03). On each image, do three runs:

1. Power-cycle the hub (unplug for 5 s), and send no C2D.
2. **At uptime 60, 300, 600 and 900 s,** copy the `MONITOR: heap: free=%lu min_ever=%lu largest_blk=%lu uptime=%lus` (format) line nearest to each.
   - **Expected sequence first, per image:**
     - **2.1.3:** boot → `APP_WIFI: Connected! IP: %s` → `IOTHUB: Connected to Azure IoT Hub!` → valve `SETUP COMPLETE - READY FOR GATT` → a `fast` snapshot → all 4 sensors heard.
     - **2.1.4:** BLE and the valve start before Wi-Fi (N1, T4-01): boot → `IOTHUB: Starting BLE (...)` → valve `SETUP COMPLETE - READY FOR GATT` (usually **before** `APP_WIFI: Connected! IP: %s` or `IOTHUB: Connected to Azure IoT Hub!`; either order is fine, and it is not a deviation) → `IOTHUB: Connected to Azure IoT Hub!` → a `fast` or `boot` snapshot → all 4 sensors heard.
   - **LED:** WHITE (syncing), then GREEN.
3. **At about 900 s,** wet `<BLE1>` (auto-close), dry it after 20 s, and wait 2 min. Then copy the MONITOR line at about 1500 s.
4. Check the lifecycle `reset_reason:"power_on"`.

**Results table** (fill in; use the `min_ever` at 900 s and at 1500 s):

| Image | Run | free@900 | min_ever@900 | largest_blk (lowest seen) | min_ever@1500 |
|---|---|---|---|---|---|
| 2.1.3 | 1 | | | | |
| 2.1.3 | 2 | | | | |
| 2.1.3 | 3 | | | | |
| 2.1.4 | 1 | | | | |
| 2.1.4 | 2 | | | | |
| 2.1.4 | 3 | | | | |

References: CP1 bench, 2.1.4-dev with valve + 4 BLE: min_ever 19,524 B. Field 2.1.3 under the commissioning load: 2,972 B, which is T6-08's scenario, not this one.

**Pass criteria**
- **(i) Heap (the VAL-13 rule):** the median 2.1.4 `min_ever` of the three runs is at least the median 2.1.3 `min_ever`, both at 900 s and at 1500 s. A shortfall of 300 B or less is recorded as `Fail` with the note "within the documented static budget" and goes to the release owner for a written waiver; a larger shortfall is a `Fail` with no waiver. If it misses by less than 1 KB, run 2 more times per image before calling a fail. Copy the figures into VAL-13.
- **(ii) Largest block:** 2.1.4 `largest_blk` is at least 7168 B at every reading.
- **(iii) Forbidden lines:** none of `TELEMETRY_V2: Snapshot not built - out of memory`, `TELEMETRY_V2: Message not built (%s) - out of memory`, `IOTHUB: SNAP heartbeat=suppressed (publish-failed)`, `BLE_VALVE: [INIT] nimble_port_init failed (...)` or `MONITOR: LOW HEAP WARNING: ...` on 2.1.4.
- **(iv) Reset reason:** always `power_on`.

| ID | Result | Notes |
|---|---|---|
| T6-07 | [ ] Pass [ ] Fail | |

---

### T6-08 — Heap like-for-like: commissioning from empty (the field scenario)

| Field | Value |
|---|---|
| Purpose | Repeat the 2.1.3 field low point (min_ever 2,972 B during commissioning) on both images: BLE start + valve link + TLS + the provision burst. The VAL-13 rule (section 9.3) applies; this is its case (b). |
| Covers | S26; council:heap after G4b, case (b) (E2F council[1] risk 0, final votes[1] risk 0); P0-b/N1 (BLE starts on the provision, not on Wi-Fi) |
| Start state | As in T6-07. Use the same commissioning method on both images: the app, if that is the field path, **or** the C2D below. |
| Duration | about 2.5 h (3 runs per image) |

**C2D messages**
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-08-decom-all-<n>","cmd":"decommission","payload":{"target":"all"}}
```
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-08-prov-<n>","cmd":"provision","payload":{"valve_id":"<VALVE>","ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"],"auto_close_enabled":true}}
```

**Steps** (per image, 3 runs)

1. **Send decommission-all.** The hub reboots empty, re-registers with DPS and connects.
   - Expect `IOTHUB: Hub is UNPROVISIONED - waiting for provisioning JSON from Azure`.
   - Copy the MONITOR line just before step 2. On 2.1.4 you should also see `IOTHUB: Boot: hub is empty - clearing any persisted rules-engine state`, and no `Starting BLE`.
   - **LED:** WHITE.
2. **Within 2 min of `Connected to Azure IoT Hub!`, send the provision** (or commission from the app).
   - **2.1.4 UART:**
     - `IOTHUB: Starting BLE (valve=<VALVE>, BLE sensors=4)`
     - `BLE_VALVE: [INIT] Signal received. Starting BLE stack...`
     - `BLE_VALVE: [HOST] NimBLE host task started`
     - `BLE_LEAK: NimBLE ready, initializing scanner`
     - then the valve link and `SETUP COMPLETE - READY FOR GATT`
   - Record whether `BLE_VALVE: [SECURITY] Already encrypted (bonded reconnection)` appears. The bond survives decommission, so this is expected on both images.
   - **IoT Hub:** ack `ok`, twin, the commission snapshot and the pulse snapshots.
   - **LED:** WHITE (syncing), then GREEN.
3. **Copy MONITOR lines** at 60 s, 300 s and 600 s after the ack. Use the `min_ever` at 600 s. The bench PSU stays in the Good band.

**Results table**

| Image | Run | min_ever before provision | min_ever @600 s after ack | lowest largest_blk | DPS time |
|---|---|---|---|---|---|
| 2.1.3 | 1–3 | | | | |
| 2.1.4 | 1–3 | | | | |

**Pass criteria:** the same as T6-07 (i)–(iv), applied to `min_ever` at 600 s after the ack. The DPS duration is recorded only (a known limitation).

| ID | Result | Notes |
|---|---|---|
| T6-08 | [ ] Pass [ ] Fail | |

---

### T6-09 — Heap on a sensors-only BLE hub (reported separately)

| Field | Value |
|---|---|
| Purpose | A hub with BLE sensors and no valve now runs NimBLE (about 69 KB) where 2.1.3 did not (the P0-b fix). Record its heap. It has no 2.1.3 like-for-like threshold, because 2.1.3 did not scan and was deaf to these sensors. |
| Covers | S26 (reported separately); P0-b; council:sensors-only hub runs NimBLE (E2F council[1] risk 1, heap half; final votes[1] risk 5, heap half) |
| Start state | 2.1.4, 4 BLE sensors, no valve. Send `{"schema":"eflostop.cmd","ver":1,"id":"t6-09-decom-v","cmd":"decommission","payload":{"target":"valve"}}` if a valve is provisioned, and power the valve off. |
| Duration | about 1 h (3 runs × 15 min; 2.1.3 once, for information) |

**Steps**

1. **Power-cycle.** Expect:
   - `PROVISIONING: Valve MAC: ` (empty)
   - `PROVISIONING: BLE leak sensors: 4`
   - `IOTHUB: Starting BLE (valve=none, BLE sensors=4)`
   - `BLE_VALVE: [HOST] NimBLE host task started`
   - `BLE_LEAK: NimBLE ready, initializing scanner`
   - `BLE_LEAK: Whitelist reloaded: 4 sensor(s)`
   - and never `BLE_VALVE: [SCAN] Target MAC matched ...`
2. **Copy MONITOR lines** at 60, 300, 600 and 900 s.
   - The snapshots have `"valve":{}`, 4 BLE sensors with `connected:true` once heard, and "All devices healthy".
   - **LED:** WHITE (syncing), then GREEN.
3. **Repeat** 3 times.
4. **For information only:** run once on 2.1.3. Its sensors stay unheard (the P0-b bug), and its heap is about 69 KB higher.

**Pass criteria**
- Snapshots and heartbeats publish: no `Snapshot not built - out of memory` and no `SNAP heartbeat=suppressed (publish-failed)`.
- `largest_blk` is at least 7168 B.
- There is no `LOW HEAP WARNING`.
- **Expectation:** `min_ever` is at least the T6-07 2.1.4 figure, since there is no valve link, SMP or GATT. Record the figures. A lower figure is a note for the heap work in 2.1.5, not a fail.

| ID | Result | Notes (min_ever / largest_blk per run) |
|---|---|---|
| T6-09 | [ ] Pass [ ] Fail | |

---

### T6-10 — Large hubs: 20 and 33 devices, and 16 BLE sensors with 31-character labels (E-01, known limitation)

| Field | Value |
|---|---|
| Purpose | E-01: a snapshot needs about twice its printed size in one heap block, and esp-mqtt's QoS 1 outbox also copies it whole. With NimBLE up, a big hub may never publish a snapshot and retries every 5 s. **Observe and record:** do heartbeats land, and how many publish failures are there? |
| Covers | E-01 (CHANGELOG known limitation, heap tuning deferred to 2.1.5); council:E-01 20+ devices / 16 BLE long labels (E2F council[1] risk 4, final votes[1] risk 7) |
| Start state | 2.1.4, valve + real sensors, MQTT up. Heartbeat set to 60 s through the twin (`properties.desired.snapshot_interval_s = 60`), so 30 min gives about 30 heartbeats. |
| Duration | about 2 h (3 runs × 30 min, plus set-up) |

**Payload generator.** The payloads are too long to type. Save this as `t6_10_gen.py`, fill in the real MACs and ids, and run it as `python t6_10_gen.py <n_ble> <n_lora> <with_valve 0|1>`.
- It prints the C2D JSON: provisioning plus inline `sensor_meta` with 31-character labels, which is the maximum.
- Unfilled slots get fake devices that are never heard: BLE `02:00:00:00:00:NN`, LoRa `0x7E0000NN`.
- It prints the byte count on stderr. It must stay under 8192.

```python
import json, sys
n_ble, n_lora, with_valve = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3] == "1"
VALVE     = "<VALVE>"
REAL_BLE  = ["<BLE1>", "<BLE2>", "<BLE3>", "<BLE4>"]
REAL_LORA = []                     # e.g. ["<LORA1>"]
ble  = (REAL_BLE  + ["02:00:00:00:00:%02X" % i for i in range(1, 17)])[:n_ble]
lora = (REAL_LORA + ["0x7E0000%02X" % i for i in range(1, 17)])[:n_lora]
codes = ["kitchen", "laundry", "garage", "basement", "utility", "hallway", "bedroom", "bathroom"]
lab = lambda k, n: ("%s sensor %02d long label ABCDEFGHIJKLMNOP" % (k, n))[:31]
meta  = [{"sensor_type": "ble_leak_sensor", "sensor_id": s, "location_code": codes[i % 8], "label": lab("BLE", i)} for i, s in enumerate(ble)]
meta += [{"sensor_type": "lora", "sensor_id": s, "location_code": codes[i % 8], "label": lab("LoRa", i)} for i, s in enumerate(lora)]
pl = {"ble_leak_sensors": ble, "lora_sensors": lora, "sensor_meta": meta}
if with_valve: pl["valve_id"] = VALVE
out = json.dumps({"schema": "eflostop.cmd", "ver": 1, "id": "t6-10-prov-%d-%d" % (n_ble, n_lora), "cmd": "provision", "payload": pl}, separators=(",", ":"))
print(out); print("bytes=%d (limit 8192)" % len(out), file=sys.stderr)
```

The full 33-device payload (16 + 16 + valve) is about 4.7 KB.

**Runs** (30 min each; start each from an empty hub so no metadata is left behind)

| Run | Devices | Generator arguments |
|---|---|---|
| 1 | 20: valve + 10 BLE + 9 LoRa | `10 9 1` |
| 2 | 33: valve + 16 BLE + 16 LoRa | `16 16 1` |
| 3 | Sensors-only 16 BLE | `16 0 0`. First send `decommission` `{"target":"valve"}` and power the valve off. |

**Steps** (per run)

1. **Start empty.** Send `{"schema":"eflostop.cmd","ver":1,"id":"t6-10-clean-<n>","cmd":"decommission","payload":{"target":"all"}}` and wait for the reconnect. Set the twin interval to 60 s again: decommission-all resets it to 300.
2. **Send the generated payload.** Expect:
   - `PROVISIONING: Provisioning completed successfully!`
   - `PROVISIONING: BLE leak sensors: %d` and `PROVISIONING: LoRa sensors: %d` (format)
   - `IOTHUB: Provision: applied %d inline sensor_meta entry(ies)` (format), with the count = `n_ble + n_lora`
   - `HEALTH_ENGINE: Device table loaded: %d device(s) (+%u added, -0 removed)` (format)
   - Ack `ok`.
3. **For 30 min, record:**
   - the lowest `largest_blk` and the lowest `min_ever`;
   - the count of each of these lines:
     - `TELEMETRY_V2: Snapshot not built - out of memory`
     - `IOTHUB: SNAP heartbeat=suppressed (publish-failed)`. Printing the JSON can fail without its own log line, so count this line too.
     - `TELEMETRY_V2: Pub snapshot failed (msg_id=%d)` (format)
     - `TELEMETRY_V2: Message not built (%s) - out of memory` (format)
   - the number of `heartbeat` snapshots in the IoT Hub monitor (the reference is 30 at 60 s);
   - the printed size of the largest `TELEMETRY_V2: Pub snapshot: {...}` line (count its characters).
4. **Health.**
   - The fake devices are never heard. After the 600 s roll-up grace expect `HEALTH_ENGINE: Roll-up grace expired (600 s) — %d unheard device(s) now count` (format).
   - The rating goes `critical` ("N sensors offline"), and the LED goes WHITE (syncing) → RED.
   - Record any `device_offline` events for never-heard devices.
5. **Safety still works:** at about 20 min, wet a real BLE sensor. Expect `leak_detected`, plus `auto_close` in runs 1–2 (none in run 3, which has no valve). The valve closes in runs 1–2. Dry it.

**Clean-up:** decommission-all, set the twin interval back to 300 s, and re-provision the standard valve + 4 BLE.

**Pass criteria.** A reboot, panic, watchdog reset, an MQTT disconnect loop or a missed leak response (step 5) is a **fail**. Not publishing snapshots is the known E-01 limitation: record it.

| ID | Run | Heartbeats received / 30 | Snapshot not built | publish-failed | lowest largest_blk | largest snapshot size | Result |
|---|---|---|---|---|---|---|---|
| T6-10 | 1 (20) | | | | | | [ ] Pass [ ] Fail |
| T6-10 | 2 (33) | | | | | | [ ] Pass [ ] Fail |
| T6-10 | 3 (16 BLE) | | | | | | [ ] Pass [ ] Fail |

---

### T6-11 — Task stack high-water marks (bench-only debug image, never shipped)

| Field | Value |
|---|---|
| Purpose | Measure the stack headroom of the tasks that run new or changed code. The only guard in production is the FreeRTOS canary (`CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y`). |
| Covers | council:stack headroom (E2F council[0] risk 0, council[1] risk 3, council[3] risk 5; final votes[0] risk 0, votes[1] risk 2, votes[3] risk 10); HANDOFF §10, the NimBLE host stack check (E-08 about +54 B on the notify path); final votes[1] NB4 (a `nimble_port_init` failure cannot recover: observe) |
| Start state | A **separate debug image** (below), then the production 2.1.4 image for the canary check. Valve A = `<VALVE>`, valve B = `<VALVE_B>`, 4 BLE sensors, a LoRa sensor if available. |
| Duration | about 3 h |

**Building the debug image**
- Build it in its own worktree: `git worktree add ..\hub_hwm d9fa9c8`, then `Copy-Item sdkconfig ..\hub_hwm\sdkconfig` **before** the first build (`sdkconfig` is git-ignored; without the copy the image gets a default config and its stack figures mean nothing). Before adding the instrumentation, an `idf.py size` of the unmodified worktree must match CP3 (`.bss` 36,280 B, `.data` 21,572 B). Never commit it, never merge it, and never flash it on a customer unit.
- In that worktree, set `PROJECT_VER` in `CMakeLists.txt` to `"2.1.4-hwm"`, so every capture shows `gateway.fw:"2.1.4-hwm"`.
- In `main/systemservices/monitoring.c`, add `#include <string.h>` and the function below, and call it every 6th loop (every 60 s) inside `monitoring_task()`, after the heap line.
- ESP-IDF 5.5.1 has `INCLUDE_xTaskGetHandle` and `INCLUDE_uxTaskGetStackHighWaterMark` set to 1. The value returned is in **bytes**.

```c
/* BENCH-ONLY stack high-water logging - never commit, never ship */
static void log_stack_hwm(void)
{
    static const char *names[] = { "nimble_host", "ble_valve", "iothub_task", "mqtt_task",
        "ble_leak_scan", "health_engine", "Tmr Svc", "fleet_led_task", "ble_starter", "monitor" };
    char line[256];
    size_t n = 0;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]) && n < sizeof(line); i++) {
        TaskHandle_t h = xTaskGetHandle(names[i]);
        int w = h ? snprintf(line + n, sizeof(line) - n, " %s=%u", names[i],
                             (unsigned)uxTaskGetStackHighWaterMark(h))
                  : snprintf(line + n, sizeof(line) - n, " %s=-", names[i]);
        if (w < 0) break;
        n += (size_t)w;
    }
    ESP_LOGI(TAG, "stack_hwm_free_bytes:%s", line);
}
```

- The debug image prints `MONITOR: stack_hwm_free_bytes: nimble_host=%u ble_valve=%u ...` (format; this line exists only in the debug image).
- `ble_starter=-` after BLE has started is normal: that task exits.
- A `-` for any other task means its name did not resolve. Check the name in the IDF sources: `nimble_port_freertos.c` creates `"nimble_host"` and `mqtt_client.c` creates `"mqtt_task"`.

**Scenarios on the debug image.** Record the **lowest** value per task over the whole run.

1. From an empty hub, provision valve A + 4 BLE (a first link and the provision burst).
2. Swap valve A → B with `{"schema":"eflostop.cmd","ver":1,"id":"t6-11-swap-b","cmd":"provision","payload":{"valve_id":"<VALVE_B>"}}`. B has never paired, so this is a **fresh SMP pairing**. Then swap back to A the same way.
3. Decommission the valve (`{"target":"valve"}`) and re-provision it.
4. Leak → auto-close → dry → auto-clear. Then send `leak_reset`, `override_enable` during a wet leak, and `override_cancel`.
5. The HANDOFF §7 B1 cases:
   - (a) power-cycle the hub with a sensor wet and the valve open;
   - (b) move the valve out of range (shielded box, powered), wet a sensor, bring the valve back;
   - (c) re-wet at the clear.
6. The HANDOFF §7 F-01 cases:
   - (a) the hub's clear pended across a relink;
   - (b) `leak_reset` while the valve is unlinked.
7. The T6-16 ATT-stall procedure, once.
8. The T6-10 run 3 payload (16 BLE with 31-character labels), then the run 2 payload (33 devices), each with at least one snapshot published or attempted.
9. The T6-13 captive-portal session, once.
10. A 1 h soak:
    - at least 2 BLE sensors bursting (one kept wet for 10 min);
    - 10 valve relinks by switching the valve PSU off and on, including 3 with SET + CLOSE pended (B1 (b)) and 1 with the valve flood probe wet.

**Results table**

| Task | Stack | Lowest free (B) | ≥ 512? |
|---|---|---|---|
| nimble_host | 4096 | | |
| ble_valve | 4096 | | |
| iothub_task | 10240 | | |
| mqtt_task | 6144 | | |
| ble_leak_scan | 3072 | | |
| health_engine | 3072 | | |
| Tmr Svc | 4096 | | |
| fleet_led_task | 2560 | | |
| ble_starter | 3072 (until it exits) | | |
| monitor | 3072 (includes this logging) | | |

**Production image, over the whole Phase G run:**
- grep every UART capture for `stack overflow`, `Guru Meditation`, `assert failed` and `[INIT] nimble_port_init failed`;
- in IoT Hub, check every lifecycle `reset_reason` for `panic` or `watchdog`.

**Pass criteria**
- At least 512 B free on every task, at every point of the debug run.
- No `***ERROR*** A stack overflow in task ... has been detected.` panic on either image.
- No production lifecycle with `reset_reason` `panic` or `watchdog`.
- **Record:** any `[INIT] nimble_port_init failed` (final votes[1] NB4: the retry cannot recover an NPL allocation failure; a reboot is the recovery).

| ID | Result | Notes |
|---|---|---|
| T6-11 | [ ] Pass [ ] Fail | |

---

### T6-12 — Router off/on × 5 and a ≥ 19 h soak with a SAS renewal (sensors-only BLE hub)

| Field | Value |
|---|---|
| Purpose | A sensors-only BLE hub now runs NimBLE, so each Wi-Fi reconnect and the daily SAS renewal (MQTT stop, `set_config`, start, TLS) runs with about 69 KB less heap. MQTT must reconnect every time with no reboot, and a renewal whose `set_config` fails reboots the hub. |
| Covers | council:sensors-only SAS/Wi-Fi reconnect (E2F council[1] risk 1, final votes[1] risk 5); P0-b; the offline buffer and replay on reconnect; CHANGELOG "Not changed" (a sensors-only leak sends `rmleak_auto_cleared` without `valve_id`) |
| Start state | 2.1.4, 4 BLE sensors, **no valve** (as in T6-09). Heartbeat 300 s. The hub has just been power-cycled, and uptime is at least 10 min, so the first SAS mint is recorded. The router can be switched off (the whole router, or its Wi-Fi radio). |
| Duration | about 1 h of toggles, then a soak of at least 19 h |

**Steps**

1. **Baseline.** Find `IOTHUB: SAS: clock valid (ts=%ld) — minting first token, starting MQTT` (format) and write down `ts` = M. The renewal is due at M + 64,800 s (18 h), within 30 s.
   - Copy the MONITOR line at uptime 600 s (the baseline).
   - **LED:** fleet GREEN; network ramp blue.
2. **Cycles k = 1..5.** Switch the router off for 60 s (k = 1–4) or 5 min (k = 5), then on. Wait for the MQTT reconnect and 2 min more before the next cycle.
   - **When off, expect:**
     - `APP_WIFI: WiFi Disconnected. Reason: %d` (format)
     - `IOTHUB: WiFi down — stopping MQTT client (free TLS heap for AP/captive portal)`
     - `TELEMETRY_V2: MQTT connected = false`
     - `NET_STATUS: wifi=0 mqtt=0`, and the network LED ramps red
     - After about 3 failed retries (15–40 s) the Wi-Fi manager may log `wifi_manager: MESSAGE: ORDER_START_AP`. Record it.
   - **When on, expect:**
     - `APP_WIFI: Connected! IP: %s` (format)
     - `IOTHUB: WiFi up — restarting MQTT client`
     - `IOTHUB: Connected to Azure IoT Hub!`
     - `TELEMETRY_V2: MQTT connected = true`
     - `NET_STATUS: wifi=1 mqtt=1`, and the network LED ramps blue
   - **IoT Hub, per reconnect:** a lifecycle with `reset_reason:"power_on"` and an increasing `gateway.uptime_s` (no reboot), a twin, and exactly one `boot` snapshot with `"valve":{}` and the 4 sensors.
   - **Fleet LED:** unchanged GREEN. It does not show the network.
   - **Timing:** MQTT is up within 60 s of the router's Wi-Fi coming back (the router's own boot time is excluded).
   - Copy the MONITOR line 2 min after each reconnect.
   - **During k = 2**, wet `<BLE1>` while the router is off and dry it 20 s later. Expect:
     - `RULES_ENGINE: LEAK INCIDENT latched by ble_leak_sensor sensor <BLE1>`
     - `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <BLE1>`
     - `RULES_ENGINE: AUTO-CLOSE: no provisioned valve - auto_close event not published`
     - `RULES_ENGINE: AUTO-CLOSE: no provisioned valve - nothing to close`
     - `TELEMETRY_V2: Offline — buffering event event`, once per event
     - after drying, `RULES_ENGINE: AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`, 10–12 s later
     - at the reconnect, `TELEMETRY_V2: Draining %d offline event(s) before lifecycle...` (format)
     - IoT Hub then gets `leak_detected`, `leak_cleared` and `rmleak_auto_cleared` (**no `valve_id`**, no `auto_close`) before the lifecycle, with real `ts` values.
     - **LED** during k = 2: RED while wet, YELLOW for 10–12 s, then GREEN.
3. **If the hub does not rejoin** within 5 min of the router being back:
   - This happens after `ORDER_START_AP`: the local wifi_manager patch stops the STA retry timer while the AP is up.
   - Record it and power-cycle the hub to recover.
   - Repeat the same outage length once on 2.1.3 to classify it. The patch predates 2.1.4, so if 2.1.3 behaves the same it is a pre-existing finding, not a 2.1.4 fail.
4. **Soak** for at least 19 h with no intervention. Copy a MONITOR line every hour (or keep the full capture).
   - At about M + 18 h, expect in this order:
     - `IOTHUB: SAS: within 6 h of expiry — renewing`
     - `TELEMETRY_V2: MQTT connected = false`
     - `IOTHUB: SAS: token renewed (valid 24 h, expires ts=%ld)` (format; `ts` ≈ renewal time + 86400)
     - `IOTHUB: Connected to Azure IoT Hub!` within about 15 s
     - then a lifecycle (`reset_reason:"power_on"`, `uptime_s` ≈ 64,800 or more), a twin, and a `boot` snapshot
   - **Must not appear:** `IOTHUB: SAS: esp_mqtt_set_config failed (%s) — client destroyed, rebooting`, `IOTHUB: SAS: mint failed (out of memory)`, `MONITOR: LOW HEAP WARNING: ...`.
5. **Heartbeat count** over the soak: about 12 per hour at 300 s ±5 s.

**Pass criteria**
- There is no reboot at any point: `uptime` in MONITOR never resets, and `reset_reason` stays `power_on`.
- MQTT reconnects after every toggle.
- The SAS renewal succeeds.
- At the end of the soak, `free` is within 2 KB of the 600 s baseline, so there is no leak trend.
- At least 95 % of the expected heartbeats arrive.
- The buffered leak events replay.
- **Record:** `min_ever` after each reconnect and after the renewal, and any AP stranding (step 3).

| ID | Result | Notes (min_ever after reconnects / renewal, AP stranding) |
|---|---|---|
| T6-12 | [ ] Pass [ ] Fail | |

---

### T6-13 — Wi-Fi reset and captive portal beside BLE: heap, protection, credentials

| Field | Value |
|---|---|
| Purpose | NimBLE (valve link, leak scanner) starts at boot beside the SoftAP captive portal, with less heap than 2.1.3's portal. While no Wi-Fi credentials are saved, the portal priority window pauses BLE scanning so a phone can join (the 2026-09-29 fix; on `d9fa9c8` no phone got a lease). The portal must stay usable with no reboot, leak protection must keep working (a leak close pended for an unlinked valve still hunts it), and the credentials must persist. |
| Covers | S21 (heap record); E-21; CHANGELOG known limitation (the portal has less free heap); council:portal beside BLE (E2F council[0] risk 4, council[1] risk 2, council[3] risk 3; final votes[0] risk 9, votes[1] risk 6, votes[3] risk 9) |
| How to run | **Merged: run T4-10** (section 4). T4-10 Part A is this test's run A (valve present: the `RESET_BTN` lines, the window opening with `Starting BLE`, the phone's join and lease, a LoRa leak during the portal that hunts and links the valve, the credential save and the resume, the drain, a power cycle to prove the credentials persisted) and T4-10 Part B is its run B (valve powered off: the hunt held, the leak-response hunt and its stop, the close replayed RMLEAK first). T4-10 Parts D-F and H cover the router-fallback portal and the 10 s reset during a router outage, the health hold (BLE sensors and the valve), a sensors-only hub, and Wi-Fi lost before the setup AP stops. T4-10 keeps the heap table. |

| ID | Run | lowest free / min_ever / largest_blk during portal | Result |
|---|---|---|---|
| T6-13 | A (= T4-10 Part A) | | [ ] Pass [ ] Fail |
| T6-13 | B (= T4-10 Part B) | | [ ] Pass [ ] Fail |
| T6-13 | F (= T4-10 Part F, sensors-only hub) | | [ ] Pass [ ] Fail |

---

### T6-14 — NVS pressure: a full offline buffer, then a Wi-Fi reset and a power cycle

| Field | Value |
|---|---|
| Purpose | The offline ring (16 × up to 512 B) shares the 16 KB default `nvs` partition with the Wi-Fi credentials, PHY calibration and the NimBLE bonds. 2.1.4 also buffers pre-sync events. A full ring must not stop the portal from saving credentials or a bond from being written, and every buffered event must replay. |
| Covers | council:NVS partition pressure (E2F council[1] risk 5, final votes[1] risk 8); council:F-08 double publish, offline half (final votes[1] risk 3: offline re-wet cycles, no NVS error); CHANGELOG known limitation (16 events; the oldest is overwritten) |
| Start state | 2.1.4, valve + 4 BLE, clock synced, MQTT up, all dry. Router WAN cable within reach. |
| Duration | about 40 min |

**Steps**

1. **Take MQTT down:** unplug the router WAN, keeping Wi-Fi up. Wait for `IOTHUB: Disconnected.` or `TELEMETRY_V2: MQTT connected = false` (up to about 2 min).
2. **Fill the ring.** Cycle `<BLE1>`: wet for 20 s, dry for 20 s. Include 2 cycles in which you re-wet `<BLE1>` within about 2 s of `RULES_ENGINE: AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK` (the F-08 offline case). Continue until `OFFLINE_BUF: Buffer full, oldest event overwritten` has appeared at least 3 times; that is about 5 cycles.
   - **Per event:** `TELEMETRY_V2: Offline — buffering event event` and `OFFLINE_BUF: Stored event [ob_%02u] (%u bytes), %d buffered` (format). The count caps at 16.
   - **Must not appear:** `OFFLINE_BUF: NVS write '%s' failed: %s` (format), any `ESP_ERR_NVS_NOT_ENOUGH_SPACE`, `OFFLINE_BUF: Event too large (%u bytes, max %d) - not buffered` (format).
   - **LED:** RED while wet, YELLOW for 10–12 s after each dry, then GREEN.
   - Leave `<BLE1>` dry.
3. **With the WAN still unplugged, do a Wi-Fi reset:** hold the button 10 s. Expect the `RESET_BTN` lines of T4-10 A2, with `APP_WIFI: WiFi Disconnected. Reason: 8` (Wi-Fi is still up) and `RESET_BTN: Wi-Fi credentials erased from NVS`. The hub reboots into AP mode with the window-open lines of T4-10 A3 (`APP_WIFI: portal priority ON (no Wi-Fi credentials) - BLE scanning paused`): BLE scanning stays paused until the setup SoftAP stops, about 60 s after `Connected! IP`.
   - Expect `OFFLINE_BUF: Init: 16 buffered event(s) pending from before reboot`.
   - Complete the portal from the phone as in T4-10 A4 and A8 (the join and the lease, then the site SSID and password). Expect `APP_WIFI: Connected! IP: %s` and `APP_WIFI: portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops (about 60 s)`, then about 60 s later `APP_WIFI: portal priority OFF (AP stopped) - BLE scanning resumed`. Wait for that line before step 4. MQTT cannot connect yet: there is no WAN.
4. **Power-cycle the hub** (unplug for 5 s), with the WAN still unplugged. Expect:
   - `APP_WIFI: Connected! IP: %s` with no portal, so the credentials persisted;
   - `OFFLINE_BUF: Init: 16 buffered event(s) pending from before reboot`;
   - the valve relinks with its bond (`SETUP COMPLETE - READY FOR GATT`).
5. **Plug the WAN in.** Expect:
   - `IOTHUB: Time synced: %s` (format)
   - `IOTHUB: Connected to Azure IoT Hub!`
   - `TELEMETRY_V2: Draining 16 offline event(s) before lifecycle...`
   - `OFFLINE_BUF: Draining 16 buffered event(s)...`
   - `OFFLINE_BUF: Replayed [ob_%02u] (%u bytes)` (format) × 16
   - `OFFLINE_BUF: Drain complete: 16 event(s) published, 0 remaining`
   - `TELEMETRY_V2: Offline drain complete: 16 event(s) replayed`
   - then the lifecycle.
   - If a pre-sync event was buffered between the Wi-Fi reset and here (none should be, since the sensors were dry), `OFFLINE_BUF: Dropped a buffered event from an earlier power cycle that was never time-stamped [%s]` is accepted.
6. **IoT Hub:**
   - The 16 newest events arrive before the lifecycle, in their original order, with non-decreasing `ts` and every `ts` at least 1704067200.
   - The re-wet cycles show `rmleak_auto_cleared`, then `leak_detected` and `auto_close`.
   - Run `python docs/telemetry/validate_capture.py "<capture>"`. Expect 0 FAIL, apart from the known out-of-window gaps from the overwritten oldest entries.
7. **Check the bond and the portal NVS:**
   - Power-cycle once more: Wi-Fi rejoins and the valve relinks bonded (`[SECURITY] Already encrypted (bonded reconnection)` or a clean pairing, with no repeated pairing failures).
   - Grep the whole capture for `ESP_ERR_NVS`.

**Pass criteria**
- No NVS error at any point.
- The credentials are saved while the ring is full, and survive a power cycle.
- All 16 buffered events replay, in order, with real `ts` values.
- The valve bond still works.
- There is no reboot other than the ones the test commands.
- **Record:** the number of `Buffer full` lines.

| ID | Result | Notes |
|---|---|---|
| T6-14 | [ ] Pass [ ] Fail | |

---

### T6-15 — A sensor flapping every 15–20 s for 15 min (the 10 s auto-clear write rate)

| Field | Value |
|---|---|
| Purpose | The 10 s auto-clear (a user decision) makes a sensor flapping at its threshold run a full clear and re-latch cycle every period. Each cycle writes RMLEAK 0 then 1 to the valve, and 2 small entries to `nvs_prov`. Relinks with a hub clear owed re-send the clear. Check there are no NVS errors, the heap stays flat, there is no false override, and the valve still commands correctly afterwards. |
| Covers | council:10 s auto-clear write rate (E2F council[1] risk 6, final votes[1] risk 9 including 3 relinks); CHANGELOG *Changed* (10 s auto-clear); the F-01 owed clear on relink (observed, not the main case) |
| Start state | 2.1.4, valve + `<BLE1>` (the other sensors dry), auto-close on, the valve open, all dry. Copy the idle MONITOR line (the idle reference). A shielded box (metal tin) for the valve, which stays powered. |
| Duration | about 25 min |

**Steps**

1. **Flap for 15 min:** wet `<BLE1>` for about 8 s, dry it for about 8 s, and repeat, for a 15–20 s period. That is about 45–60 wet/dry transitions.
   - **Per cycle, typically:**
     - `RULES_ENGINE: LEAK INCIDENT latched by ble_leak_sensor sensor <BLE1>`
     - `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <BLE1>`
     - `BLE_VALVE: [CMD] Writing RMLEAK=1`, then `BLE_VALVE: [CMD] Writing Valve=0`
     - `RULES_ENGINE: All sensors clear — auto-clear timer started (10s)`
     - 10–12 s after the dry report, if it stays dry, `RULES_ENGINE: AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`, then `BLE_VALVE: [CMD] Writing RMLEAK=0`
   - A re-wet before the clear keeps the latch. A re-wet at the clear gives `rmleak_auto_cleared`, then `leak_detected` and `auto_close`.
   - The valve stays closed throughout. It never reopens by itself.
   - **LED:** RED while wet, YELLOW in the dry interlock window; GREEN only after a full clear.
2. **At about 4, 8 and 12 min,** put the valve in the shielded box for about 60 s (until `GAP DISCONNECT EVENT`), then take it out. At each relink one of these is allowed:
   - `RULES_ENGINE: Reconnected: valve RMLEAK active, hub incident clear - RMLEAK clear owed by the hub, not re-latching` with `BLE_VALVE: [CMD] Applying pending RMLEAK command=0`;
   - `RULES_ENGINE: Reconnected with 1 active leak(s) — valve already closed + RMLEAK asserted, nothing to do`;
   - `RULES_ENGINE: Valve reconnected with 1 active leak(s) — executing auto-close`;
   - `RULES_ENGINE: Reconnected: hub + valve RMLEAK in sync`;
   - `RULES_ENGINE: Reconnected: no active incident, valve clear`.

   **Timing:** relinked within 60 s.
3. **Stop flapping with `<BLE1>` dry.** Wait 15 s: the valve is closed and the snapshot shows `rmleak:false`.
   - Copy the MONITOR line.
   - Send `{"schema":"eflostop.cmd","ver":1,"id":"t6-15-open","cmd":"valve_open"}`. Expect ack `ok`, `valve_state_changed` to open, and the valve opens.
   - Send `{"schema":"eflostop.cmd","ver":1,"id":"t6-15-close","cmd":"valve_close"}`. Expect ack `ok`, and the valve closes.
   - Then open it again.
4. **Count, from the capture:**
   - `AUTO-CLOSE + RMLEAK triggered` lines;
   - `AUTO-CLEAR:` lines;
   - `[CMD] Writing RMLEAK=` lines (the valve's RMLEAK write count);
   - `rmleak_auto_cleared` and `auto_close` events in IoT Hub.

**Pass criteria**
- There are none of these: `RULES_ENGINE: NVS: incident save failed (set=%s commit=%s) — will retry` (format), `NVS open failed`, `ESP_ERR_NVS`, `RMLEAK cleared externally (valve override) — starting 24h override window`, or a `water_access_override_enabled` event.
- `free` at the end is within 2 KB of the idle reference, and `min_ever` is no more than 1 KB below it.
- The valve opens and closes on command afterwards.
- There is no reboot.
- **Follow-up:** send the RMLEAK write count to the valve firmware owner. `rules_engine.c` notes that the valve keeps RMLEAK in the backup register BKP0R; ask them to confirm that there is no flash wear.

| ID | Result | Notes (counts) |
|---|---|---|
| T6-15 | [ ] Pass [ ] Fail | |

---

### T6-16 — Sustained ATT stall on a live valve link, bounded by the 30 s GATT timeout

| Field | Value |
|---|---|
| Purpose | Since the B1 fix, "GATT busy" (`BLE_HS_ENOMEM`) is waited out and replayed, and never forces a relink. If the valve stops answering ATT while the link stays up, the replay cycles every 5–10 s until NimBLE's 30 s GATT procedure timeout drops the link. A leak close can therefore be about 30 s plus a reconnect late. Check it is bounded, there is no tight loop, the logs are bounded, and RMLEAK precedes CLOSE at the relink. |
| Covers | B1; council:sustained ATT stall (final votes[0] risk 2, votes[1] risk 1, votes[3] risk 5); final votes[0] NB4 and votes[1] NB2 (the command task busy for about 10 s per command: observe); votes[3] NB1 (an ENOMEM from ATT-buffer exhaustion is not bounded by the 30 s timeout: CHANGELOG known limitation, observe); votes[0] NB3 and votes[3] NB2 (the relink cap with no replay token: observe) |
| Start state | 2.1.4, valve linked + `<BLE1>`, auto-close on, the valve open, all dry. |
| Duration | about 30 min (3 attempts) |

**How to stall the valve**

| Method | How |
|---|---|
| A (RF edge) | Move the valve away, or partly shield it (foil, or a metal box with the lid ajar), until the link holds but writes answer slowly. You are there when `[CMD] Writing ...` and its read-back are more than about 1 s apart, or `GATT busy - waiting` appears. |
| B (halt the valve application core) | Halt the valve's application core (CPU1) with the STM32CubeIDE debugger through an ST-Link. Use this only if the link stays up and the writes go unanswered: the STM32WB radio core may still answer them, in which case use method A. |

**Steps**

1. **Stall the link** with method A or B, with the link still up.
2. **Wet `<BLE1>`.** Expect `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <BLE1>`, then one or more of:
   - `BLE_VALVE: [CMD] RMLEAK write: GATT busy - waiting`
   - `BLE_VALVE: [CMD] RMLEAK read-back: GATT busy - waiting`
   - `BLE_VALVE: [CMD] RMLEAK=1 kept pending - GATT still busy, replaying it`
   - `BLE_VALVE: [CMD] Pending valve command=0 kept behind the RMLEAK command (GATT busy)`
   - `BLE_VALVE: [CMD] Valve=0 held behind the pending RMLEAK command`

   Write down T0, the time of the `AUTO-CLOSE` line.
3. **Watch without touching anything.** Expect `GAP DISCONNECT EVENT` and `BLE_VALVE: [DISCONNECT] reason=0x%02x` (format) within about 30–35 s of the first unanswered procedure.
   - Record the reason. It is typically a local-terminate code after the GATT timeout, or a supervision timeout when the RF drops.
   - Then restore the valve (bring it back, or resume the core).
4. **At the relink (`SETUP COMPLETE - READY FOR GATT`):**
   - `BLE_VALVE: [CMD] Applying pending RMLEAK command=1` (or `[CMD] Replaying pending RMLEAK command=1`) comes **before** `BLE_VALVE: [CMD] Applying pending valve command=0` (or `[CMD] Replaying pending Valve command=0`).
   - The valve reads CLOSED with RMLEAK ACTIVE: `valve_state_changed` to closed, and a snapshot with `valve.state:"closed"` and `rmleak:true`.
   - `[CMD] valve read-back rc=6 - position unconfirmed` is acceptable, as in HANDOFF §7.
   - Write down T1, when CLOSED is reported.
5. **Throughout:**
   - The busy and replay lines run at about 8 per 5 s or fewer, and the whole episode is under about 60 lines.
   - No `task_wdt` message and no `Task watchdog got triggered`.
   - MONITOR `free` stays flat (±2 KB).
6. **Dry `<BLE1>`** and let the auto-clear run (10–12 s). The valve stays closed. Send `valve_open` to restore.

**Pass criteria**
- The valve ends CLOSED with RMLEAK ACTIVE.
- RMLEAK is written before CLOSE at the relink.
- There is no `[CMD] valve write failed %d times (rc=%d) - reconnecting to re-apply (%s=%u)` with `rc=6`, and no `write attempt N/3 failed (rc=6)`.
- There is no reboot and no watchdog.
- The logs are bounded.
- **Record:** T1 − T0. About 30 s plus the reconnect is expected.

**Observe and record, not a fail**
- **The busy cycle lasts more than 60 s while the link stays up.** This is the documented ATT-buffer ENOMEM case (votes[3] NB1). It passes if the valve closes once the stall is removed.
- `BLE_VALVE: [CMD] valve writes keep failing - no more forced reconnects until a write succeeds` (the relink cap).

| ID | Result | Notes (method, T1 − T0, disconnect reason, lines per 5 s) |
|---|---|---|
| T6-16 | [ ] Pass [ ] Fail | |

---

### T6-17 — Valve command queue saturation

| Field | Value |
|---|---|
| Purpose | The 10-deep valve command queue must not drop the leak's RMLEAK and CLOSE while the command task waits on a busy GATT pool or a link in setup. A C2D burst may be refused with the documented error, but the valve must still close. |
| Covers | council:queue saturation (final votes[0] risk 7: B1 case (b) with 4 BLE + 1 LoRa wet; votes[1] risk 4: 12 C2D `valve_close` during setup); votes[1] NB0 and votes[3] NB3 (the live-path hold has no queue-full guard: a 2.1.5 finding if seen); `C2D_COMMANDS.md` §6.1 "The valve command could not be queued. Try again." |
| Start state | 2.1.4, valve + 4 BLE (+ `<LORA1>`), auto-close on, the valve open, all dry. A shielded box. For part B, Python with `pip install azure-iot-hub` and the IoT Hub service connection string (the `iothubowner` or `service` policy) in `IOTHUB_SERVICE_CS`. |
| Duration | about 30 min |

**Part A: many sources during B1 case (b)**

1. **Take the valve out of range:** put it in the box, still powered, and wait for `GAP DISCONNECT EVENT`.
2. **Wet all 4 BLE sensors and `<LORA1>` within about 5 s.** Expect:
   - one `RULES_ENGINE: LEAK INCIDENT latched by ...`;
   - one `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ...`, as later sources fall in the 10 s cooldown;
   - `RULES_ENGINE: AUTO-CLOSE: valve not connected — scanning; close deferred to reconnect reconciliation`;
   - `BLE_VALVE: [CMD] RMLEAK write not ready. Queuing val=1`, then `BLE_VALVE: [CMD] Valve write not ready. Queuing val=0`;
   - a `leak_detected` for each source.
3. **Take the valve out of the box.** At setup, RMLEAK=1 is applied or replayed before valve=0, and the valve is CLOSED with RMLEAK ACTIVE within about 3–5 s of `SETUP COMPLETE - READY FOR GATT`.
   - **Must not appear:**
     - `[CMD] RMLEAK ENQUEUE FAILED — command queue full, valve NOT commanded`
     - `[CMD] CLOSE ENQUEUE FAILED — command queue full, valve NOT commanded`
     - `[CMD] Pending valve commands not applied, command queue full - kept for the next link`
     - any `RULES_ENGINE: ... enqueue FAILED — ...`
4. Dry every sensor. The auto-clear releases RMLEAK in 10–12 s, and the valve stays closed.

**Part B: a burst of 12 C2D `valve_close` during setup**

1. **Latch a leak while the valve is out of range** (as in part A steps 1–2, with `<BLE1>` only), and keep `<BLE1>` wet.
2. **Take the valve out of the box.** As soon as `GAP CONNECT EVENT` appears (before `SETUP COMPLETE`), run the script below. It sends 12 closes in about 1 s.

```python
# t6_17_burst.py  -  pip install azure-iot-hub ; set IOTHUB_SERVICE_CS first
import os, json, time
from azure.iot.hub import IoTHubRegistryManager
rm = IoTHubRegistryManager.from_connection_string(os.environ["IOTHUB_SERVICE_CS"])
dev = "<DEVICE_ID>"
t0 = time.time()
for i in range(1, 13):
    rm.send_c2d_message(dev, json.dumps({"schema": "eflostop.cmd", "ver": 1,
                                         "id": "t6-17-close-%02d" % i, "cmd": "valve_close"}))
print("12 sent in %.2f s" % (time.time() - t0))
```

3. **Expect:**
   - 12 × `IOTHUB: Command: VALVE_CLOSE`. Each one queues a CONNECT and a CLOSE, so some may be refused:
     - `IOTHUB: VALVE_CLOSE refused — The valve command could not be queued. Try again.`
     - `BLE_VALVE: [CMD] CLOSE ENQUEUE FAILED — command queue full, valve NOT commanded`
     - and the ack `{"event":"cmd_ack","id":"t6-17-close-NN","cmd":"valve_close","status":"error","error":{"code":"valve_close","detail":"The valve command could not be queued. Try again."}}`

     **Acceptable** under this burst. Count them.
   - The rest ack `ok`.
   - The valve reads CLOSED with RMLEAK ACTIVE within about 5 s of `SETUP COMPLETE - READY FOR GATT`, with RMLEAK written before the valve CLOSE.
   - `[CMD] Valve=0 held behind the pending RMLEAK command` may appear.
4. Dry `<BLE1>`, wait for the auto-clear, and send `valve_open`.

**Pass criteria**
- **Part A:** none of the queue-full lines, and the valve closed within about 5 s of setup.
- **Part B:** the valve closed with RMLEAK within about 5 s of setup, RMLEAK first, no panic, and no `RMLEAK cleared externally`.
- **A finding for 2.1.5, not a 2.1.4 fail:** `[CMD] Pending valve commands not applied, command queue full - kept for the next link` followed by the valve still open at the next wet report.

| ID | Result | Notes (refused count in part B, close latency) |
|---|---|---|
| T6-17 | [ ] Pass [ ] Fail | |

---

### T6-18 — `iothub_task` stalled behind a blocking C2D (`override_enable` with the valve off, then a leak within 2 s)

| Field | Value |
|---|---|
| Purpose | esp-mqtt holds its API lock for the whole of a C2D handler. `override_enable` with an unreachable valve waits up to 10 s in that handler, so `iothub_task` blocks at its next publish, which delays the leak handling and the `leak_detected` publish. Measure the delay. **Up to 10 s is the accepted pre-existing behaviour.** |
| Covers | council:iothub stall behind a blocking C2D (E2F council[0] risk 3, final votes[0] risk 6 (1), and the F-08 early publish ahead of Phase 2, final votes[0] NB2 and votes[1] NB3) |
| Start state | 2.1.4, valve + `<BLE1>`, auto-close on, no incident, all dry, MQTT up. UART timestamps on. |
| Duration | about 15 min (3 repetitions; smoke candidate: 1 repetition) |

**Steps**

1. **Switch the valve PSU off.** Wait for `GAP DISCONNECT EVENT` and 20 s more. The snapshot reason shows "Valve disconnected", and the LED is YELLOW.
2. **Send:**
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-18-ovr-<n>","cmd":"override_enable"}
```
   - UART: `IOTHUB: Command: OVERRIDE_ENABLE`, then `RULES_ENGINE: override_enable: valve not ready — reconnecting (<=10000ms)` at Tc.
3. **Within 2 s of Tc, wet `<BLE1>`.** Record:
   - Ts, the time of `BLE_LEAK: eleak <BLE1> — leak=1 batt=%d%% rssi=%d fw=%s` (format). The scanner task is not blocked.
   - Te, the time of `IOTHUB: Event: BLE Leak <BLE1> leak=1 batt=%d` (format).
   - Ta, the time of `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by ble_leak_sensor sensor <BLE1>`. It is followed by `RULES_ENGINE: AUTO-CLOSE: valve not connected — scanning; close deferred to reconnect reconciliation` and the two `Queuing` lines.
   - Tp, the time `leak_detected` goes out: `TELEMETRY_V2: Pub event: {...leak_detected...}`.
4. **At about Tc + 10 s:**
   - `RULES_ENGINE: override_enable: valve unreachable after reconnect window`.
   - Ack `{"event":"cmd_ack","id":"t6-18-ovr-<n>","cmd":"override_enable","status":"error","error":{"code":"override_enable","detail":"The valve isn't responding. Check its power and connection, then try again."}}`.
   - No `water_access_override_enabled`.
   - **LED:** RED from the leak.
5. **Switch the valve PSU on.** At setup, expect `[CMD] Applying pending RMLEAK command=1` (or `Replaying pending RMLEAK command=1`) before the valve command. The valve closes with RMLEAK set.
6. **Dry `<BLE1>`.** The auto-clear follows in 10–12 s. Send `valve_open` to restore. Repeat steps 1–6 two more times.
7. **Optional (final votes[0] risk 6 (2)):** on a healthy link, re-wet `<BLE1>` at the auto-clear. The gap from `TELEMETRY_V2: Pub event: {...rmleak_auto_cleared...}` to `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered ...` should be under 1 s.

**Pass criteria**
- Ta − Ts ≤ 10 s, and Tp − Ts ≤ about 11 s, in every repetition.
- The valve closes once it is powered.
- The ack is the exact error above.
- More than 10 s is a **fail**.
- **Record:** all four timestamps for each repetition.

| ID | Rep | Ta − Ts | Tp − Ts | Result |
|---|---|---|---|---|
| T6-18 | 1 | | | [ ] Pass [ ] Fail |
| T6-18 | 2 | | | [ ] Pass [ ] Fail |
| T6-18 | 3 | | | [ ] Pass [ ] Fail |

---

### T6-19 — A leak sensor unheard during a hanging valve connect (≤ 30 s, known limitation)

| Field | Value |
|---|---|
| Purpose | While a valve connect is pending, the host cannot run the leak scanner's discovery (`ble_gap_ext_disc` returns busy), and the connect times out after 30 s. A leak during that window is heard late. Measure the gap; about 30 s is the expected bound. |
| Covers | CHANGELOG deferred item (sensors unheard during a valve connect attempt); council:sensors unheard during connect (E2F council[3] risk 4, final votes[3] risk 11) |
| Start state | 2.1.4, valve + `<BLE1>`, the valve disconnected and the hub scanning for it, all dry. UART timestamps on. |
| Duration | about 20 min (aim for 3 captured hangs) |

**How to make the connect hang**
- **Method A (preferred):** place the valve at the RF edge, or partly shielded, so that `BLE_VALVE: [SCAN] Target MAC matched - connecting to provisioned valve: <VALVE>` appears but `GAP CONNECT EVENT` does not follow within 5 s. The connect is then pending until its 30 s timeout.
- **Method B:** switch the valve PSU off within 1 s of the `Target MAC matched` line. This works only when the connect has not already completed.

**Steps**

1. Get a hang: `[SCAN] Target MAC matched ...` at Tm, with no `GAP CONNECT EVENT`.
2. **Within 5 s of Tm, wet `<BLE1>`.** Note the wall-clock wet time Tw.
3. **Expect:**
   - about Tm + 30 s: `GAP CONNECT EVENT` with `BLE_VALVE: [CONNECT] status=13`, then `BLE_VALVE: [CONNECT] Failed status=13` (the connect timeout; record the actual status);
   - around the window, possibly `BLE_LEAK: Failed to start ext scan: %d, will retry` (format) or `BLE_LEAK: Scan not active (external cancel?), restarting`;
   - then `BLE_LEAK: eleak <BLE1> — leak=1 batt=%d%% rssi=%d fw=%s` (format) at Th;
   - then `RULES_ENGINE: LEAK INCIDENT latched by ble_leak_sensor sensor <BLE1>` and `leak_detected`.
   - **LED:** RED from Th.
4. Dry `<BLE1>`. Restore the valve to good range; it links and closes, or clears, according to the rules. Repeat until 3 hangs are captured.

**Pass criteria**
- Th − Tm ≤ about 30 s plus one wet burst (about 15 s), so ≤ 45 s.
- The leak is always handled after the window.
- There is no reboot.
- **Record:** Th − Tw and Th − Tm for each hang.

| ID | Hang | Th − Tm | Th − Tw | connect status | Result |
|---|---|---|---|---|---|
| T6-19 | 1–3 | | | | [ ] Pass [ ] Fail |

---

### T6-20 — BLE start claimed twice at once (boot apply and C2D `provision`)

| Field | Value |
|---|---|
| Purpose | BLE start can now be requested from `iothub_task` (the boot or owed apply) and from esp-mqtt (a `provision`) at the same time. The start signal is claimed atomically, so the stack must start exactly once. |
| Covers | CHANGELOG Reliability (the BLE start is claimed atomically; `38439d2`); council:BLE start claim (final votes[0] risk 8, votes[3] risk 8) |
| Start state | 2.1.4, valve + 4 BLE provisioned, MQTT up. |
| Duration | about 30 min (5 repetitions) |

The exact race (an owed apply retry overlapping the C2D) cannot be forced from outside. This test drives the closest reachable overlap and checks the invariant.

**Steps** (repeat 5 times)

1. **Empty the hub without a reboot:** send `{"schema":"eflostop.cmd","ver":1,"id":"t6-20-dv-<n>","cmd":"decommission","payload":{"target":"valve"}}`, then `{"schema":"eflostop.cmd","ver":1,"id":"t6-20-db-<n>","cmd":"provision","payload":{"ble_leak_sensors":[]}}`.
   - The hub is now empty: `PROVISIONING: Hub empty: rules config reset to defaults`.
2. **Power-cycle the hub.** Expect `IOTHUB: Boot: hub is empty - clearing any persisted rules-engine state` and **no** `Starting BLE`.
3. **Send the provision** as soon as `IOTHUB: Connected to Azure IoT Hub!` appears. Have the message ready in the VS Code box.
```json
{"schema":"eflostop.cmd","ver":1,"id":"t6-20-prov-<n>","cmd":"provision","payload":{"valve_id":"<VALVE>","ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"]}}
```
4. **Expect, per boot:**
   - exactly one `BLE_VALVE: [INIT] Signal received. Starting BLE stack...`;
   - exactly one `BLE_VALVE: [HOST] NimBLE host task started`;
   - `IOTHUB: Starting BLE (valve=<VALVE>, BLE sensors=4)`, one or more times; a second request is harmless;
   - the valve links within 60 s (`SETUP COMPLETE - READY FOR GATT`), and the sensors are heard.
   - **LED:** WHITE (empty) → WHITE (syncing) → GREEN.

**Pass criteria**
- In all 5 repetitions, one BLE start and one host task start per boot.
- No `assert failed`, `Guru Meditation` or reboot.
- The valve links.

| ID | Result | Notes |
|---|---|---|
| T6-20 | [ ] Pass [ ] Fail | |

---

### 6.x Residual-risk disposition

**Numbering.**
- "E2F c[i]#j" is `phaseE2F.json` `council[i].r.residual_risks[j]`.
- "Final v[i]#j" is `council_final.json` `votes[i].r.residual_risks[j]`.
- "NBk" is that vote's `non_blocking_issues[k]`.

**The first table lists every risk placed in this section.**

| Origin | Risk (short) | Test(s) here |
|---|---|---|
| E2F c[0]#0 (F1 RTOS) | Stack headroom, no high-water instrumentation | T6-11 |
| E2F c[0]#3 | `iothub_task` stalls behind a blocking C2D (≤ 10 s) | T6-18 |
| E2F c[0]#4 | BLE at boot beside the SoftAP portal | T6-13 (= T4-10) |
| E2F c[1]#0 (F2 memory) | Heap after G4b unmeasured | T6-07, T6-08 |
| E2F c[1]#1 | Sensors-only hub runs NimBLE; SAS renewal and Wi-Fi reconnect with less heap | T6-09 (heap), T6-12 |
| E2F c[1]#2 | Captive portal with BLE running (E-21) | T6-13 (= T4-10) |
| E2F c[1]#3 | Task stack headroom unmeasured | T6-11 |
| E2F c[1]#4 | E-01: 20+ devices or 16 BLE with long labels | T6-10 |
| E2F c[1]#5 | Default `nvs` partition pressure | T6-14 |
| E2F c[1]#6 | 10 s auto-clear write rate for a flapping sensor | T6-15 |
| E2F c[2]#8 | A provision that adds no device no longer re-arms the pulse (UART and wire halves; the app placement screen is VAL-04 step 3 and DEC-01 step 10) | T6-05 steps 1 and 4 |
| E2F c[3]#3 (F4 BLE) | Coexistence and heap during the portal, valve off (worst case) | T6-13 run B (= T4-10 Part B) |
| E2F c[3]#4 | Leak sensors unheard while a valve connect is pending | T6-19 |
| E2F c[3]#5 | NimBLE host stack margin | T6-11 |
| E2F c[4]#4 (F5 persistence) | Upgrade and rollback: provisioning, opt-out, incident and override survive | T6-03, T6-04 |
| E2F c[4]#5 | Power cut mid-decommission-all | T6-02 |
| Final v[0]#0 | Stack headroom on changed-code tasks | T6-11 |
| Final v[0]#2 | Sustained ATT stall on a link that stays up | T6-16 |
| Final v[0]#6 | `iothub_task` stall behind a blocking C2D; re-wet publish-to-close < 1 s | T6-18 (steps 1–7) |
| Final v[0]#7 | Command-queue saturation during B1 case (b) | T6-17 part A |
| Final v[0]#8 | BLE start claim from two tasks | T6-20 |
| Final v[0]#9 | BLE at boot beside the portal | T6-13 (= T4-10) |
| Final v[1]#0 | Heap after G4b, like-for-like (a) and (b) | T6-07, T6-08 |
| Final v[1]#1 | Sustained ATT stall | T6-16 |
| Final v[1]#2 | Task stack headroom after the fix round | T6-11 |
| Final v[1]#3 | F-08 double publish: heap and offline ring (offline and NVS half) | T6-14 step 2 (the online order is T5-04 and T3-18) |
| Final v[1]#4 | Queue saturation, 12 `valve_close` during setup | T6-17 part B |
| Final v[1]#5 | Sensors-only SAS renewal and Wi-Fi reconnect | T6-12 |
| Final v[1]#6 | Portal with BLE (E-21) | T6-13 (= T4-10) |
| Final v[1]#7 | E-01 large hub | T6-10 |
| Final v[1]#8 | `nvs` partition pressure | T6-14 |
| Final v[1]#9 | Auto-clear write rate plus relinks with a clear owed | T6-15 |
| Final v[2]#11 | A provision adding no device (same as E2F c[2]#8) | T6-05 |
| Final v[3]#5 | Sustained busy GATT pool ends at the 30 s GATT timeout | T6-16 |
| Final v[3]#8 | Atomic BLE start claim | T6-20 |
| Final v[3]#9 | Portal coexistence and heap (carried) | T6-13 (= T4-10) |
| Final v[3]#10 | Stack margin of the host and `ble_valve` (1 h soak, 10 relinks) | T6-11 step 10 |
| Final v[3]#11 | Sensors unheard during a pending connect (carried) | T6-19 |
| Final v[4]#6 | Upgrade and rollback without erase | T6-03, T6-04 |
| Final v[4]#7 | Power cut mid-decommission-all | T6-02 |
| Final v[0] NB2, v[1] NB3 | The F-08 early publish runs before Phase 2 | T6-18 |
| Final v[0] NB3, v[3] NB2 | Relink cap engaged with no replay token | T6-16 (observe `valve writes keep failing`) |
| Final v[0] NB4, v[1] NB2 | The command task busy for about 10 s per command under a busy pool | T6-16, T6-17 |
| Final v[1] NB0, v[3] NB3 | The live-path hold has no queue-full guard | T6-17 part B |
| Final v[1] NB4 | The `nimble_port_init` retry cannot recover an NPL allocation failure | T6-11 (observe) |
| Final v[3] NB1 | An ENOMEM from mbuf exhaustion is not bounded by the 30 s GATT timeout | T6-16 (observe) |
| Final v[4] NB3 items 2, 3, 4 | Old events replay after a power cut on decommission-all; mixed keys after a power cut mid-save; rollback enabled with no mark-valid | T6-02, T6-01, T6-03 |

**The second table lists risks whose tests are in other sections.** Section M.5 gives the test IDs for every one of them.

| Topic (for the owning section) | Risks |
|---|---|
| B1 busy GATT pool; RMLEAK before CLOSE across a relink; the rc=6 valve read-back at setup | E2F c[0]#1, c[3]#1; Final v[0]#1, v[0]#4, v[2]#10, v[3]#2, v[3]#3, v[3]#6 |
| F-01: the hub's own clear across a relink or restart; the genuine button press still works; the override-path re-latch | E2F c[0]#2, c[2]#6, c[3]#0, c[4]#0, c[4]#1; Final v[0]#5, v[2]#3, v[2]#4, v[3]#0, v[3]#1, v[3]#7, v[4]#0, v[4]#1, v[4]#2, v[4]#3, v[4]#4 |
| F-08: re-wet at the clear, online and offline order; C2D rules events at the next pass | E2F c[2]#0, c[3]#8; Final v[1]#3 (online half), v[2]#0, v[2]#1, v[2]#2, v[4]#5 |
| Stale-confirm false "button" window at the RF edge | Final v[0]#3, v[3]#4 |
| F-02, F-03 known limitations (observe) | E2F c[2]#7, c[4]#2, c[4]#3; Final v[2]#8 |
| F-09, F-10 (observe) | E2F c[2]#4, c[2]#5, c[3]#6, c[3]#7; Final v[2]#7, v[2]#9 |
| Empty-hub scheduling and shapes; decommission-all final snapshot (S8, S9, S11) | E2F c[2]#1, c[2]#2; Final v[2]#6 |
| Pre-sync event stamping; software reset before the sync; override restored across a power-on with no internet | E2F c[2]#3, c[4]#6, c[4]#7; Final v[2]#5, v[4]#8, v[4]#9 |
| Valve battery-critical wire contract (S14, S17) | E2F c[2]#9; Final v[2]#12 |
| Valve identity and target change while linked (P0-a, P0-c) | E2F c[3]#2 |
| Scanner delta on a remove and re-add within 10 s (E-02) | E2F c[3]#9 |

## 9. Validation and exit criteria

Section 9 checks the release end to end: that the build is the one the review approved, that a provisioned 2.1.3 hub upgrades (and rolls back) without losing anything, that the app shows each new 2.1.4 wire shape correctly, and that the hub holds up under a soak. It ends with the release exit criteria (9.4).

The app checks (VAL-04 to VAL-12) test the **app's rendering** of the firmware's messages. For each one, record both halves: the firmware half (the expected JSON appeared) and the app half (the screen shows it correctly). A firmware half that fails is a firmware defect. An app half that fails, with the firmware half passing, is an app defect: log it against the app. It still blocks **field rollout** of 2.1.4 (9.4, EC-8), because the CHANGELOG lists these shapes as changes that "cloud and app parsers must accept".

### 9.1 Build and upgrade gates

#### VAL-01: Build checkpoint 3 gate (d9fa9c8) (P0)

**Purpose:** prove that the image under test is `d9fa9c8`, built clean, with no new warnings and the expected sizes. Every other result depends on this.
**Preconditions:** the `fix/2.1.4` working tree; ESP-IDF 5.5.1 environment open in PowerShell in the project folder.
**Covers:** CLAUDE_CODE_PROMPT Phase G item 1–2 (CP3), HANDOFF §7 step 1a, council final vote condition "pending a clean CP3".

| Step | Action | Expected | Result | Notes |
|---|---|---|---|---|
| 1 | Run command block 1 below. | HEAD is `d9fa9c8` or a later docs-only commit; the diff prints nothing. Record the `sdkconfig` SHA256 on the results sheet; it must equal the hash of the `sdkconfig` used for CP3 and of the copy in the 2.1.3 worktree (0.2, 0.4). (`sdkconfig` is git-ignored, so no `git diff` can show a change to it.) | | |
| 2 | Run command block 2 below (full rebuild, only to capture the warning lines the CP3 paste omitted). | `exit=0` | | |
| 3 | Run command block 3 below. | **Only the four warnings that are also on `master`:** `app_ble_valve.c:105` `BLE_HS_ATT_ERR` redefined; `app_lora.cpp:185` two missing `uart_config_t` initialisers (two warnings); `app_lora.cpp:160` unused `switch_sync_word`. No `error:`. Any other warning is a Fail. If the command prints **nothing at all**, check the log is not empty: a clean ESP-IDF build of this tree does print those four. | | |
| 4 | `idf.py size` | The exact CP3 figures for `d9fa9c8` (HANDOFF §4b): flash `.text` **1,016,642 B**, `.rodata` **360,188 B**; DIRAM `.bss` **36,280 B**, `.data` **21,572 B**; IRAM 16,384 / 16,384 (100 %, unchanged). A change of more than about ±40 B in any of these needs a look before you go on. | | |
| 5 | `(Get-Item build\eFloStop_WiFiHub_idf1.bin).LastWriteTime` and `.Length` | Later than 2026-09-27 00:35:07 +1000 (the `d9fa9c8` commit time). App `.bin` **1,528,576 B** (0x175300), the CP3 size (±40 B needs a look), with ≥ 27 % of the 2 MB partition free. | | |
| 6 | Flash (0.3) and check the boot lines (0.14) | `HUB_IDENT: Firmware version: v2.1.4`; the ESP-IDF banner shows `App version: 2.1.4`. | | |

Command block 1 (step 1):

```powershell
git log --oneline -1
git diff --stat d9fa9c8 HEAD -- main CMakeLists.txt partitions.csv sdkconfig.defaults managed_components
Get-FileHash sdkconfig
```

Command block 2 (step 2):

```powershell
idf.py fullclean
idf.py build *> "$env:TEMP\build_cp3.log" ; "exit=$LASTEXITCODE"
```

Command block 3 (step 3). Copy it from here, not from a table: the pattern is a .NET regex, and `|` inside it must not be escaped.

```powershell
Select-String -Path "$env:TEMP\build_cp3.log" -Pattern 'warning:|error:' | ForEach-Object Line
```

**Timing:** none. **LED:** not applicable.

#### VAL-02: Upgrade 2.1.3 → 2.1.4 in place keeps provisioning, rules, the incident latch and the override (P0)

**Purpose:** the field upgrade path. A provisioned 2.1.3 hub updated to 2.1.4 without erasing flash must keep its devices, rules config, a latched incident and an active override window. There is no OTA client (0.4), so the update is an app-only flash.
**Procedure (merged):** run **T6-03** (section 6). Its run A carries a provisioned 2.1.3 hub (valve, 4 BLE sensors, sensor metadata, hub name, a 120 s interval, the auto-close opt-out and an active `c2d_command` override) across the app-only flash; its run B carries a latched incident with the sensor still wet. Then run **run C** below on the same bench: it carries a **button** override with a real expiry (the SRS 4.4.2 window) across the same upgrade. VAL-02 passes when T6-03 runs A and B and run C all pass.
**Preconditions (run C):** `SS-213-V4` (hub on 2.1.3 with valve A and `<BLE1>`…`<BLE4>`), clock synced, MQTT connected. The 2.1.4 image from VAL-01. UART and IoT Hub captures running.
**Covers:** S25 (upgrade half), council F5 "Upgrade and rollback …", CHANGELOG Upgrade notes ("no change to any NVS namespace, key or layout"), F-01 flag reset after boot (T6-03 step 10 and run B step 6).

**Run C: an active override survives.**

| Step | Action | Expected | Result | Notes |
|---|---|---|---|---|
| C1 | Flash 2.1.3, latch an incident (wet `<BLE1>` and wait for 2.1.3's `auto_close`; valve linked, `[DATA] RMLEAK=1` seen). **While `<BLE1>` is still wet** (as in T3-15), or at the latest within 20 s of the hub's dry `leak=0` line, **long-press the valve button** to start a button override. (2.1.3 auto-clears 30 s after the dry report, `AUTO_CLEAR_TIMEOUT_MS` 30 s at `ae4d59a`, plus up to 30 s idle; a press after the auto-clear finds no latched incident, starts no override, and run C then has nothing to carry.) Note the 2.1.3 `water_access_override_enabled` `ts` and `expires_ts`. | 2.1.3 UART: `RULES_ENGINE: RMLEAK cleared externally (valve override) — starting 24h override window`. IoT: `water_access_override_enabled{"trigger":"button","remaining_s":86400,...}`. **Both are required before C2**; without them, repeat C1. Then dry `<BLE1>`. | | |
| C2 | Wait about 2 min, then flash 2.1.4 app only. | `RULES_ENGINE: NVS: restored override window (expiry=%lu, remaining=%lds)` (format) with `expiry` equal to the 2.1.3 `expires_ts` and `remaining` = 86400 − (time since C1), ±5 s. | | |
| C3 | Wait for the next snapshot. | `"override_active":true`, `"override_remaining_s"` consistent with C2 (±5 s), `"expires_ts"` equal to the 2.1.3 value. | | |
| C4 | End the override. | `{"schema":"eflostop.cmd","ver":1,"id":"val-02-c4","cmd":"override_cancel"}` → `cmd_ack` `ok`, `auto_close_reenabled` with `"reason":"c2d_command"`, `RULES_ENGINE: OVERRIDE WINDOW CANCELLED (remaining_s=%ld)` (format). | | |

**Timing:** the first 2.1.4 lifecycle within about 10 s of the MQTT connect; full sync (LED GREEN) within 600 s of boot (each dry sensor bursts about every 100 s). **Pass/Fail:** T6-03 run A is P0. T6-03 run B and run C must pass for release (they are the council F5 risk).

| ID | Result | Notes |
|---|---|---|
| VAL-02 (T6-03 A, T6-03 B, run C) | | |

#### VAL-03: Rollback 2.1.4 → 2.1.3 keeps provisioning, and 2.1.3 reads the 2.1.4 offline buffer

**Purpose:** a field unit rolled back to 2.1.3 must keep its devices, and 2.1.3 must drain whatever 2.1.4 left in the offline buffer.
**Procedure (merged):** run **T6-04** (section 6). It is this test with the full expected lines: MQTT taken down with the router WAN unplugged, a leak cycle buffered by 2.1.4, an app-only flash of 2.1.3 with the WAN still down, the 2.1.3 drain (no `OFFLINE_BUF: Read '%s' failed: %s, skipping`, every `ts` real, `gateway.fw` `"2.1.4"` on the replayed events), then a reflash of 2.1.4. VAL-03 passes when T6-04 passes.
**Covers:** S25 (rollback half), E-22 (a stamped entry never written back past 512 B), council F5 rollback step, CHANGELOG Upgrade notes (rollback).

| ID | Result | Notes |
|---|---|---|
| VAL-03 (= T6-04) | | |

### 9.2 End-to-end app / UI validation

Run these with the phone app watching the hub. The IoT Hub capture is the firmware half; the app screen is the app half. Take a screenshot for every "App shows" cell.

#### VAL-04: The app shows the right devices after a provision, with the syncing state (P0)

**Purpose:** after a provision, the app lists exactly the provisioned devices, shows "Syncing - waiting for N devices" (not "offline") while devices are unheard, and fills each device in as it is heard (the post-provision pulse).
**Preconditions:** `SS-EMPTY` (2.1.4). `<BLE1>`…`<BLE4>` powered and dry, valve A powered, valve B powered nearby.
**Covers:** BUG-2 (the intended UI-sync behaviour kept), S1, council F3 "a provision that adds no device no longer restarts the pulse".

| Step | Action | C2D JSON | Expected UART | Expected IoT Hub | LED | App shows | Result | Notes |
|---|---|---|---|---|---|---|---|---|
| 1 | Provision valve A + 4 sensors with labels. | `ss-v4m` (0.8) | `HEALTH_ENGINE: Device table loaded: 5 device(s) (+5 added, -0 removed)`; `IOTHUB: PROV pulse armed: every %d s for %d s, plus on every sensor packet` (format; prints 30 and 300); `IOTHUB: Commission: fast snapshot armed (all-devices-seen, else <=%ds; refreshes on late devices)` (format); `IOTHUB: Starting BLE (valve=<VALVE_MAC>, BLE sensors=4)` | `cmd_ack` `ok`; within 7 s a snapshot with `"system_health":{"rating":"excellent","reason":"Syncing - waiting for 5 devices"}` (the count may already be lower if a device was heard in between) | WHITE (`FLEET_LED: rating=syncing color=WHITE effect=SOLID`) | Exactly 1 valve and 4 sensors, with their labels; a syncing / "waiting" state, **not** offline or red. No valve B. | | |
| 2 | Watch for up to 300 s. | — | Each sensor's burst logs `BLE_LEAK: eleak %s — leak=%d batt=%d%% rssi=%d fw=%s` (format) and is followed by a snapshot (the pulse). | The reason counts down: "Syncing - waiting for 4 devices" … "for 1 device" (singular) … then "All devices healthy". A snapshot at least every 30 s during the 300 s pulse. `valve.valve_id` is always `<VALVE_MAC>`; never `<VALVE_B_MAC>` anywhere. | WHITE → GREEN when the last device is heard | Each device turns from syncing to live (battery, RSSI, fw) as it is heard, within about 30 s of its snapshot. | | |
| 3 | Re-send the same `ss-v4m` payload with id `val-04-s3`. | `{"schema":"eflostop.cmd","ver":1,"id":"val-04-s3","cmd":"provision","payload":{"valve_id":"<VALVE_MAC>","ble_leak_sensors":["<BLE1>","<BLE2>","<BLE3>","<BLE4>"],"sensor_meta":[{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE1>","location_code":"kitchen","label":"Sink"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE2>","location_code":"laundry","label":"Washer"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE3>","location_code":"bathroom","label":"Ensuite"},{"sensor_type":"ble_leak_sensor","sensor_id":"<BLE4>","location_code":"bathroom","label":"Main"}],"auto_close_enabled":true}}` | `HEALTH_ENGINE: Device table loaded: 5 device(s) (+0 added, -0 removed)`. **Not printed:** `PROV pulse armed`, `Commission: fast snapshot armed`. | `cmd_ack` `ok`; one `event` snapshot; a twin reported refresh. No syncing reason. | GREEN | No change; no device goes back to syncing. | | |
| 4 | Add `<LORA1>` if available (a `lora_sensors`-only provision leaves the valve and BLE list as they are). Without LoRa hardware, record `N/A (no LoRa HW)`. | `{"schema":"eflostop.cmd","ver":1,"id":"val-04-s4","cmd":"provision","payload":{"lora_sensors":["<LORA1>"],"sensor_meta":[{"sensor_type":"lora","sensor_id":"<LORA1>","location_code":"utility","label":"Heater"}]}}` | `Device table loaded: 6 device(s) (+1 added, -0 removed)`; `PROV pulse armed ...` | Reason "Syncing - waiting for 1 device" until `<LORA1>` is heard; heard devices keep their data. | WHITE until heard, then GREEN | The new sensor appears as syncing; the others stay live. | | |

**Timing:** snapshot ≤ 7 s after the ack; the pulse lasts 300 s; an unheard device is counted (RED, "1 sensor offline") only 600–605 s after the provision (`HEALTH_ENGINE: Roll-up grace expired (%lu s) — %d unheard device(s) now count`, format).

#### VAL-05: The app shows the survivors unchanged after one device is removed (P0)

**Purpose:** removing one sensor does not reset the others in the app: no device goes back to syncing, offline, or loses its battery, RSSI or firmware.
**Preconditions:** `SS-V4` with labels (VAL-04 end state), everything heard, at least 5 min after the provision.
**Covers:** BUG-2, S2.

| Step | Action | C2D JSON | Expected UART | Expected IoT Hub | LED | App shows | Result | Notes |
|---|---|---|---|---|---|---|---|---|
| 1 | Save the last snapshot (the "before" snapshot). | — | — | — | GREEN | Screenshot. | | |
| 2 | Remove `<BLE1>`. | `{"schema":"eflostop.cmd","ver":1,"id":"val-05-s2","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE1>"}}` | `IOTHUB: !!! DECOMMISSION_BLE: <BLE1> !!!`; `HEALTH_ENGINE: Device table loaded: 4 device(s) (+0 added, -1 removed)` | `cmd_ack` `ok`; one `event` snapshot equal to the "before" snapshot minus `<BLE1>`: survivors keep `connected:true`, rating, `battery`, `rssi`, `fw_version`, `leak_state`; `last_seen_age_s` has only grown. Reason "All devices healthy". | GREEN | `<BLE1>` gone; the other 3 sensors and the valve unchanged: no syncing, no offline, no blank battery. | | |
| 3 | Wait 180 s. | — | **Not printed:** `HEALTH_ENGINE: Boot sync: timeout ...`. | **No** `boot` or `commission` snapshot. | GREEN | No change. | | |

**Timing:** the snapshot ≤ 7 s after the ack.

#### VAL-06: The app's empty state: last device removed, heartbeats, reboot (P0)

**Purpose:** a hub with no devices shows an empty state in the app, keeps publishing (so the app does not show the hub as offline), and shows it again after a reboot.
**Preconditions:** `SS-V4`, snapshot interval 60 s (0.9).
**Covers:** BUG-3, BUG-5, BUG-6, S8, S9.

| Step | Action | C2D JSON | Expected UART | Expected IoT Hub | LED | App shows | Result | Notes |
|---|---|---|---|---|---|---|---|---|
| 1 | Remove `<BLE1>`…`<BLE4>`, one at a time, ≥ 10 s apart. | One line each, in order: `{"schema":"eflostop.cmd","ver":1,"id":"val-06-s1a","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE1>"}}` then `{"schema":"eflostop.cmd","ver":1,"id":"val-06-s1b","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE2>"}}` then `{"schema":"eflostop.cmd","ver":1,"id":"val-06-s1c","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE3>"}}` then `{"schema":"eflostop.cmd","ver":1,"id":"val-06-s1d","cmd":"decommission","payload":{"target":"ble_leak_sensor","sensor_id":"<BLE4>"}}` | `Device table loaded: N device(s) (+0 added, -1 removed)` each time | One `event` snapshot per removal. | GREEN | Each sensor disappears; the valve stays. | | |
| 2 | Remove the valve (the last device). | `{"schema":"eflostop.cmd","ver":1,"id":"val-06-s2","cmd":"decommission","payload":{"target":"valve"}}` | `IOTHUB: !!! DECOMMISSION_VALVE !!!`; `PROVISIONING: Valve removed successfully`; `PROVISIONING: Hub empty: rules config reset to defaults`; `IOTHUB: Device is now UNPROVISIONED`; `IOTHUB: Hub is now EMPTY - no devices provisioned; heartbeat-only snapshots`; `FLEET_LED: rating=unprovisioned color=WHITE effect=SOLID` | `cmd_ack` `ok`, then the empty snapshot below (reason `"event"`); twin reported `"provisioned":false`, `"valve_id":null`, `"lora_sensor_count":0`, `"ble_leak_sensor_count":0`, `"auto_close_enabled":true`, `"trigger_mask":7`. | WHITE | Empty state ("no devices" / "add a device"). No valve tile, no "Valve offline", no stale "open" valve. | | |
| 3 | Wait 3 heartbeat intervals (≥ 185 s). | — | `TELEMETRY_V2: Pub snapshot: ...` once per interval | Three `heartbeat` snapshots with the same empty shape, 60 s apart (−1/+3 s as `ts` differences, counted from the step 2 snapshot). | WHITE | The hub stays "online" in the app, and the empty state stays. | | |
| 4 | Power-cycle the hub. | — | `PROVISIONING: No existing config found, starting UNPROVISIONED`, or `Loaded existing config` with `State: UNPROVISIONED`; `IOTHUB: Boot: hub is empty - clearing any persisted rules-engine state`; **no** `IOTHUB: Starting BLE ...` | lifecycle `{"event":"online","reset_reason":"power_on","provisioned":false,"lora_sensor_count":0,"ble_leak_sensor_count":0,"rules":{"auto_close_enabled":true,"trigger_mask":7}}` (no `valve_id`); exactly **one** `boot` snapshot with the empty shape; then heartbeats. | OFF → WHITE | The same empty state after the reboot. | | |

The empty snapshot (full; `name` appears in `gateway` only if a hub name is set):

```json
{
  "schema": "eflostop.v2",
  "ts": <epoch>,
  "gateway": {"id": "<GW>", "short_id": "<last 4>", "fw": "2.1.4", "uptime_s": <n>},
  "type": "snapshot",
  "data": {
    "reason": "event",
    "system_health": {"rating": "excellent", "reason": "No devices provisioned"},
    "valve": {},
    "lora_sensors": [],
    "ble_leak_sensors": [],
    "rules": {"auto_close_enabled": true, "trigger_mask": 7},
    "override_active": false
  }
}
```

The heartbeats are identical except `"reason": "heartbeat"`; after a reboot or MQTT reconnect, `"reason": "boot"`.

#### VAL-07: The app shows "no valve" for a sensors-only hub (valve `{}`), and the no-valve command refusal (P0)

**Purpose:** a hub with sensors and no valve shows no valve at all in the app, not an offline valve; its sensors are live (P0-b); and a valve command gets the "No valve is set up" message.
**Preconditions:** `SS-S2` (valve B powered within 2 m, never provisioned).
**Covers:** BUG-5, P0-a, P0-b, P0-c (UI half), S9 sensors-only, C5.

| Step | Action | C2D JSON | Expected UART | Expected IoT Hub | LED | App shows | Result | Notes |
|---|---|---|---|---|---|---|---|---|
| 1 | Power-cycle the hub and wait for both sensors to be heard. | — | `IOTHUB: Starting BLE (valve=none, BLE sensors=2)`; `BLE_VALVE: [HOST] NimBLE host task started`; eleak packets for `<BLE1>` and `<BLE2>` (`BLE_LEAK: eleak %s — leak=%d batt=%d%% rssi=%d fw=%s`, format). No `[SCAN] Target MAC matched` and no `BLE_VALVE: [CONNECT] MAC=` line. | Snapshots with `"valve":{}` and both sensors `connected:true` with `battery`, `rssi`, `fw_version`. Reason "All devices healthy" once both are heard. | WHITE → GREEN | 2 sensors, live. **No valve tile**, or an explicit "no valve installed"; never "Valve offline" or "Valve disconnected". | | |
| 2 | Send `valve_open`. | `{"schema":"eflostop.cmd","ver":1,"id":"val-07-s2","cmd":"valve_open"}` | `IOTHUB: Command: VALVE_OPEN`; `IOTHUB: VALVE_OPEN refused — No valve is set up for this hub.` | `cmd_ack` `{"event":"cmd_ack","id":"val-07-s2","cmd":"valve_open","status":"error","error":{"code":"valve_open","detail":"No valve is set up for this hub."}}` | GREEN | The app shows the refusal with that text (or the app's own wording for it), and no "valve opening" spinner stuck forever. Valve B does not move. | | |
| 3 | Send `valve_close`. | `{"schema":"eflostop.cmd","ver":1,"id":"val-07-s3","cmd":"valve_close"}` | `IOTHUB: VALVE_CLOSE refused — No valve is set up for this hub.` | `cmd_ack` error, same `detail`. | GREEN | As step 2. Valve B does not move. | | |

**Timing:** ack ≤ 5 s.

#### VAL-08: The app shows valve battery unknown, Low and Critical, and the battery refusal (P0)

**Purpose:** the app never shows 0 % for an unknown battery, shows the valve's Low and Critical states with their reasons, and shows the battery-critical refusal of `valve_open`.
**Preconditions:** `SS-V4` with valve A on the bench PSU at 6.00 V (0.10), valve open, battery Good.
**Covers:** BUG-1, S14, S17, C6, user decision on the valve battery bands.

| Step | Action | Expected IoT Hub | LED | App shows | Result | Notes |
|---|---|---|---|---|---|---|
| 1 | Switch the PSU off for 5 s, then on. Watch the snapshots in the first 5 s after the valve's `BLE_VALVE: [CONNECT] MAC=%s, handle=%u` (format) line, before `SETUP COMPLETE - READY FOR GATT` (the valve's readings are not in yet). | While disconnected: `"valve":{"valve_id":"<VALVE_MAC>","state":"disconnected","connected":false,"rating":"warning","last_seen_age_s":<s since drop>}` (no `battery` key). If a snapshot lands in the setup window: `"state":"unknown","battery":null,"fw_version":null,"connected":true`. Then the real battery. **Never** `"battery":0`. | YELLOW while disconnected (grace), GREEN after | Battery shown as unknown ("—"), never 0 %. If no snapshot landed in the setup window, record "not exercised" and rely on the validator (EC-5). | | |
| 2 | Set the PSU to the Low point (about 5.45 V, 0.10). Wait for the reading (up to 10 min 30 s). | Within 7 s of the reading: an `event` snapshot `"system_health":{"rating":"warning","reason":"Valve battery low"}`, valve `"rating":"warning"`, `battery` 11–20. **No** `device_offline`. | YELLOW | "Valve battery low" (warning), with the percentage. | | |
| 3 | Set the PSU to the Critical point (about 5.35 V). Wait for the reading (≤ 70 s). | `event` snapshot `"system_health":{"rating":"critical","reason":"Valve battery critical"}`, valve `"rating":"critical"`, `battery` ≤ 10. **No** `device_offline` (a battery critical raises no health event). The valve closes itself (valve FW) → `valve_state_changed` `closed`. | RED (`FLEET_LED: rating=critical color=RED effect=SOLID`) | "Valve battery critical" (critical), distinct from "offline"; valve closed. | | |
| 4 | Send `valve_open`. C2D: `{"schema":"eflostop.cmd","ver":1,"id":"val-08-s4","cmd":"valve_open"}` | UART `IOTHUB: VALVE_OPEN refused — Valve battery critical (≤10 %): the valve will not open. Replace the batteries.`; `cmd_ack` `"status":"error"`, `"error":{"code":"valve_open","detail":"Valve battery critical (≤10 %): the valve will not open. Replace the batteries."}`. No `[CMD] Writing` for the valve. | RED | The refusal text. The valve stays closed. | | |
| 5 | Set the PSU back to 6.00 V. Wait for the reading (≤ 30 s at the Critical cadence). | `event` snapshot back to `excellent` / "All devices healthy", `battery` > 20. | GREEN | Normal battery; open is allowed again. Send `valve_open` (id `val-08-s5`): `cmd_ack` `ok`, then `valve_state_changed` `open`. | | |

**Timing:** snapshot ≤ 7 s after each battery reading; first reading after a PSU change ≤ 10 min 30 s.

#### VAL-09: The app during a leak: locked, the refused open, the 10 s auto-clear, and the re-wet order (P0)

**Purpose:** the app shows the leak, the locked valve, the refused `valve_open` (also with the valve powered off), the release about 10–12 s after drying, and the release-then-relock sequence of a re-wet at the clear.
**Preconditions:** `SS-V4`, valve linked, auto-close enabled.
**Covers:** E-06 / user decision "valve_open refused while an incident is latched, even with the valve disconnected", the 10 s auto-clear (user decision), F-08 ("rmleak_auto_cleared" before the re-wet's `leak_detected` and `auto_close`), `b245d94` re-wet at the clear, council F3 "the app must render release then re-lock".

| Step | Action | C2D JSON | Expected UART | Expected IoT Hub | LED | App shows | Result | Notes |
|---|---|---|---|---|---|---|---|---|
| 1 | Wet `<BLE1>`. | — | `RULES_ENGINE: LEAK INCIDENT latched by %s sensor %s` (format); `RULES_ENGINE: AUTO-CLOSE + RMLEAK triggered by %s sensor %s` (format) | `leak_detected`, `auto_close`, `valve_state_changed` `closed` with `rmleak:true`; snapshot reason "Leak detected: <label>" and `critical`. | RED | Leak alert on `<BLE1>`'s label; valve closed and locked. | | |
| 2 | Switch valve A's power off. Wait for the valve's link to drop. Send `valve_open`. | `{"schema":"eflostop.cmd","ver":1,"id":"val-09-s2","cmd":"valve_open"}` | `IOTHUB: VALVE_OPEN refused — Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.` | `cmd_ack` `"status":"error"`, `"detail":"Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."` | RED | The refusal text. | | |
| 3 | Power valve A on again; wait for it to link. Dry `<BLE1>`. | — | `RULES_ENGINE: All sensors clear — auto-clear timer started (10s)`; 10–12 s later `RULES_ENGINE: AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK` | `leak_cleared`; then `rmleak_auto_cleared` `{"event":"rmleak_auto_cleared","valve_id":"<VALVE_MAC>","clear_after_seconds":10}` 10–12 s after the dry report (`ts` difference ≤ 14 s). The valve **stays closed**. | RED → YELLOW ("Leak interlock latched", about 10–12 s) → GREEN | Leak cleared; the lock released; valve still closed with an "open" control available. | | |
| 4 | Re-wet `<BLE1>` about 10–12 s after drying it, timed to land at the auto-clear. Repeat 5 times (dry, wait, re-wet). | — | Each time: `AUTO-CLEAR: all sensors clear for 10s — clearing RMLEAK`, then its `Pub event` for `rmleak_auto_cleared`, then the re-wet's `LEAK INCIDENT latched ...` and `AUTO-CLOSE + RMLEAK triggered ...`. **Forbidden:** `RMLEAK cleared externally (valve override) — starting 24h override window`. | `rmleak_auto_cleared` → `leak_detected` → `auto_close`, with non-decreasing `ts`. The last snapshot: valve `"state":"closed"`, `"rmleak":true`, `"override_active":false`. Exception (Known-limit): in a pass that is also an MQTT reconnect, `rmleak_auto_cleared` may be missing. | RED | Release, then re-lock, shown in order; ends locked. No "override active". | | |
| 5 | Dry `<BLE1>`, wait for the auto-clear, then open the valve. | `{"schema":"eflostop.cmd","ver":1,"id":"val-09-s5","cmd":"valve_open"}` | `IOTHUB: Command: VALVE_OPEN` and no `refused` | `cmd_ack` `ok`; `valve_state_changed` `open`, `rmleak:false` | GREEN | Valve open, no lock. | | |

Run `validate_capture.py` on this capture: 0 FAIL, 0 ordering violations.

#### VAL-10: The app's override state

**Purpose:** the app shows an active water-access override (started from the app during a leak), with its countdown, and its end.
**Preconditions:** `SS-V4`, valve linked. Wet `<BLE1>` and wait for the valve to close (as VAL-09 step 1). Keep `<BLE1>` wet.
**Covers:** S24 (override), the app half of `water_access_override_enabled` / `auto_close_blocked_override` / `auto_close_reenabled`.

| Step | Action | C2D JSON | Expected UART | Expected IoT Hub | LED | App shows | Result | Notes |
|---|---|---|---|---|---|---|---|---|
| 1 | Start the override. | `{"schema":"eflostop.cmd","ver":1,"id":"val-10-s1","cmd":"override_enable"}` | `IOTHUB: Command: OVERRIDE_ENABLE`; `RULES_ENGINE: OVERRIDE WINDOW STARTED: auto-close blocked for 24h (expiry=%ld)` (format) | `cmd_ack` `ok`; `water_access_override_enabled` `{"trigger":"c2d_command","expires_ts":<ts+86400>,"remaining_s":86400}`; `valve_state_changed` `open`, `rmleak:false`; snapshot `"override_active":true`, `"override_remaining_s"` ≈ 86400, `"expires_ts"` | RED (the sensor is still wet) | "Override active", with about 24 h remaining; valve open; the leak still shown. | | |
| 2 | Dry `<BLE1>`, then wet it again. | — | `RULES_ENGINE: Override active — auto-close BLOCKED for %s sensor %s (remaining=%lds)` (format) | `leak_detected`; `auto_close_blocked_override` (at most one per minute) carrying `override_remaining_s`; the valve stays open. | RED while wet | The leak, and "auto-close paused by override". | | |
| 3 | Cancel the override (sensor still wet). | `{"schema":"eflostop.cmd","ver":1,"id":"val-10-s3","cmd":"override_cancel"}` | `IOTHUB: Command: OVERRIDE_CANCEL`; `RULES_ENGINE: OVERRIDE WINDOW CANCELLED (remaining_s=%ld)` (format) | `cmd_ack` `ok`; `auto_close_reenabled` `{"previous_remaining_s":<n>,"reason":"c2d_command"}`; the valve closes: `valve_state_changed` `closed`, `rmleak:true`; snapshot `"override_active":false` | RED | Override ended; valve closed and locked. | | |
| 4 | Dry `<BLE1>` and let it auto-clear. | — | as VAL-09 step 3 | as VAL-09 step 3 | → GREEN | Normal. | | |

**Timing:** ack ≤ 12 s (bounded reconnect); `override_remaining_s` = 86400 − elapsed, ±2 s.

#### VAL-11: The app after a device goes offline and comes back, with a connected valve's age at 0

**Purpose:** the app shows an offline sensor after its 600 s grace and its recovery; a connected valve's "last seen" stays at 0.
**Preconditions:** `SS-V4`.
**Covers:** E-16 (`last_seen_age_s` 0 while linked; counts from the drop), S24 (offline/recover), the `device_offline` wire change (reachability only).

| Step | Action | Expected IoT Hub | LED | App shows | Result | Notes |
|---|---|---|---|---|---|---|
| 1 | Watch 3 heartbeats. | Valve `"connected":true`, `"last_seen_age_s":0` in every one. | GREEN | Valve "online / just now". | | |
| 2 | Take `<BLE2>`'s battery out. Note the time of its last `BLE_LEAK: eleak <BLE2> — leak=...` line in UART. | 600–635 s later: `device_offline` for `<BLE2>`; snapshot `critical` "1 sensor offline". | GREEN → RED | `<BLE2>` offline. | | |
| 3 | Put the battery back. | `device_recovered` for `<BLE2>` at its first burst; reason back to "All devices healthy". | RED → GREEN | `<BLE2>` online. | | |
| 4 | Put valve A in the RF shield for 4 min, then take it out. | While shielded: `last_seen_age_s` counts up from the drop; 180–215 s after the drop `device_offline` for the valve with `offline_duration_s` about 180, snapshot "Valve offline". After the relink `device_recovered`, `last_seen_age_s` back to 0. | YELLOW (grace) → RED → GREEN | Valve disconnected → offline → online. | | |

#### VAL-12: The app reads every new 2.1.4 wire shape without an error (app parser sweep)

**Purpose:** a final check that the app handles each shape in the CHANGELOG's "Cloud and app parsers must accept" list. Most of these were seen in VAL-04 to VAL-11; tick them off here, and create the ones that were not seen.
**Preconditions:** the captures of VAL-04 to VAL-11.
**Covers:** the CHANGELOG 2.1.4 Upgrade notes (app parsers), council F3.

| Shape (CHANGELOG) | Where you saw it | App handles it (no error, no crash, no misleading value) | Result | Notes |
|---|---|---|---|---|
| `data.valve` equal to `{}` | VAL-06, VAL-07 | | | |
| `battery: null` on the snapshot valve, on `valve_state_changed`, on leak events | VAL-08 step 1 (if exercised) | | | |
| Snapshots and lifecycle from a hub with no devices | VAL-06 | | | |
| `device_recovered` with `rating:"critical"` (a sensor coming back wet). Create it: take a **dry** sensor's battery out and wait for its `device_offline` (600-635 s after it was last heard). Wet its probe, then put the battery back. Its first wet burst gives `device_recovered` with `"rating":"critical"` and `"prev_rating":"critical"`, plus `leak_detected`. (Do not start from a wet sensor: a wet sensor is rated critical for the leak before its staleness is tested, so it never enters the offline state, sends no `device_offline`, and so no `device_recovered`.) | create it | | | |
| `prev_rating` equal to `rating` | any capture | | | |
| The new valve-command error acks, and the RMLEAK refusal during a latched incident | VAL-07, VAL-08, VAL-09 | | | |
| `data.valve.last_seen_age_s` of `0` for a connected valve | VAL-11 | | | |
| No `auto_close` from a hub with no valve: wet `<BLE1>` on `SS-S2` → `leak_detected` only, UART `RULES_ENGINE: AUTO-CLOSE: no provisioned valve - auto_close event not published` | create it | | | |
| `water_access_override_enabled` without `expires_ts` (an override started before the clock synced) | T4-07, T4-08 | | | |
| `data.valve.rmleak` / `leak_state` `false` while `state` is `"unknown"` | VAL-08 step 1 (if exercised) | | | |
| lifecycle and twin `provisioned:true` with no devices (a `provision` whose sensor arrays empty the hub) | create it: from `SS-S2`, send `{"schema":"eflostop.cmd","ver":1,"id":"val-12-prov-empty","cmd":"provision","payload":{"ble_leak_sensors":[]}}` → `PROVISIONING: Hub empty: rules config reset to defaults`; snapshot `valve:{}`, `[]`, "No devices provisioned"; twin reported `"provisioned":true` | | | |
| Events arriving late with a `ts` older than the connect's lifecycle (pre-sync events) | T4-03, T4-05 | | | |

### 9.3 Release gates

#### VAL-13: Heap like-for-like, 2.1.4 against 2.1.3 (P0)

**Purpose:** the release criterion "no lower minimum free heap than 2.1.3", measured like-for-like.
**Preconditions:** the **same hub**, the same devices (valve A + `<BLE1>`…`<BLE4>`), the same router and room, the default 300 s snapshot interval, every run from a power-on, no neighbour valve powered. 2.1.3 (`ae4d59a`) first, then 2.1.4 (`d9fa9c8`, app-only flash so provisioning carries over).
**Covers:** S26, E-01, E-20 (heap budget), council F2 "heap profile after G4b (BLE at boot) unmeasured", CLAUDE_CODE_PROMPT robustness heap threshold.
**Procedure (merged):** the readings are taken with two section 6 procedures, three runs per image:
- case (a), a valve + 4 BLE hub rebooted with Wi-Fi, then one leak cycle: **T6-07** (readings at 900 s and at 1500 s of uptime);
- case (b), commissioning from empty, which is the 2.1.3 field low point: **T6-08** (reading 600 s after the `provision` ack).

Copy the `min_ever` values here and take the median of the three runs per image:

| Case | Image | Run 1 | Run 2 | Run 3 | Median | Lowest `largest_blk` |
|---|---|---|---|---|---|---|
| (a) at 900 s | 2.1.3 | | | | | |
| (a) at 900 s | 2.1.4 | | | | | |
| (a) at 1500 s | 2.1.3 | | | | | |
| (a) at 1500 s | 2.1.4 | | | | | |
| (b) 600 s after the ack | 2.1.3 | | | | | |
| (b) 600 s after the ack | 2.1.4 | | | | | |

**Pass:** the 2.1.4 median `min_ever` ≥ the 2.1.3 median, in both (a) and (b); no `MONITOR: LOW HEAP WARNING`; no `TELEMETRY_V2: Snapshot not built - out of memory`, no `TELEMETRY_V2: Message not built (%s) - out of memory` (format), no `BLE_VALVE: [INIT] nimble_port_init failed ...`; lifecycle `reset_reason` only `power_on` or `software` (after a flash).
**Deviation rule:** the CHANGELOG budgets about 181 B of extra static RAM plus about 50 B of permanent heap in 2.1.4. If the 2.1.4 median is below 2.1.3 by **300 B or less**, record `Fail` with the note "within the documented static budget" and take it to the release owner for a written waiver. A shortfall above 300 B is a `Fail` with no waiver.
**Record also (not pass/fail):** the sensors-only hub figures of T6-09 (it now runs NimBLE, which 2.1.3 did not; there is no 2.1.3 like-for-like for it), and the captive-portal figures of T4-10.

| ID | Result | Notes |
|---|---|---|
| VAL-13 | | |

#### VAL-14: Soak, no reboot, SAS renewal (P0)

**Purpose:** 2.1.4 runs for at least 19 h with a realistic device set and periodic activity, with no reboot, through one SAS token renewal (at about 18 h of uptime).
**Preconditions:** `SS-V4` (`SS-V4L` if a LoRa sensor is available), 2.1.4, snapshot interval 300 s. UART to a file (0.6 option A). IoT Hub via `az` (0.7 option B).
**Run beside it:** T6-12 is the same ≥ 19 h soak on a **sensors-only** hub, with five router cycles first. Run it on a second hub in parallel if you have one; both count for EC-6.
**Covers:** the exit criterion "no reboot in the soak", council F2 "sensors-only hubs … SAS renewal", council F1/F4 stack margin carry-over, E-01 (largest block), the `SAS: esp_mqtt_set_config failed ... rebooting` path.

| Step | Action | Expected | Result | Notes |
|---|---|---|---|---|
| 1 | Start the soak; note the uptime of the first MONITOR line. | — | | |
| 2 | Every 2 h for the first 8 h: wet and dry one sensor (auto-close, auto-clear), send `leak_reset` and `valve_open` (each id `val-14-hHH-<n>`). Twice during the soak, unplug the WAN for 2 min, then plug it back in. Once, shield the valve for 4 min. | Each activity behaves as in VAL-09 / VAL-11. After each WAN return: `IOTHUB: Connected to Azure IoT Hub!`, one lifecycle `online` (`reset_reason` unchanged from the start), one `boot` snapshot, and the offline buffer drained. | | |
| 3 | Leave it running to ≥ 19 h uptime. | `IOTHUB: SAS: token renewed (valid 24 h, expires ts=%ld)` (format) at about 18 h; no reconnect storm after it. | | |
| 4 | Stop and check. | MONITOR `uptime` increases monotonically through the whole file (no reset to a small number). **Zero** of: `Guru Meditation`, `abort()`, `Backtrace`, `task_wdt`, `stack overflow`, `SAS: esp_mqtt_set_config failed`, `Snapshot not built - out of memory`, `LOW HEAP WARNING`. In the IoT Hub capture, lifecycle `online` appears only at the start and after each WAN return, always with the same `reset_reason` as at the start. Heartbeats are present for the whole soak (300 s, −1/+3 s, or an event snapshot in between). Record the final `min_ever` and the lowest `largest_blk`. | | |
| 5 | Run the validator on the soak capture. | 0 FAIL. | | |

#### VAL-15: Validator on the full capture (P0)

**Purpose:** machine-check every message the hub sent during the whole test campaign against the 2.1.4 contract.
**Preconditions:** all IoT Hub captures of the run (full or smoke). Remove any 2.1.3 messages first (0.18).
**Includes:** the per-section validator runs (T5-14 on the section 5 capture, and the capture checks of sections 2, 4 and 6). A FAIL found there is a FAIL here.
**Covers:** every wire change in the CHANGELOG (the validator's assertions), the F-08 ordering (the ORDERING check), "no auto_close with no valve" (user decision), `ts` ≥ 1704067200 (N1 pre-sync stamping).

| Step | Action | Expected | Result | Notes |
|---|---|---|---|---|
| 1 | For each capture file: `python docs\telemetry\validate_capture.py "<file>"` | `N messages checked, N pass, 0 fail`; no `--- ORDERING` block; no `--- AUTO_CLOSE FROM A HUB WITH NO VALVE` block; exit code 0. | | |
| 2 | Check the `message mix`, summed over all files. | Every one of `snapshot`, `lifecycle`, `leak_detected`, `leak_cleared`, `valve_state_changed`, `device_offline`, `auto_close`, `cmd_ack` was seen at least once in the full run. | | |
| 3 | For any FAIL: find the message in the UART log at the same time and classify it. | Each FAIL is a firmware defect (`Fail`), or the known provisioning-busy false positive (0.18), annotated with its evidence. | | |

### 9.4 Release exit criteria for 2.1.4

2.1.4 may be released (tagged, merged and rolled out) only when **all** of these hold. Record each one as Met / Not met, with its evidence.

| # | Criterion | Evidence |
|---|---|---|
| EC-1 | **Build.** CP3 of `d9fa9c8` passed VAL-01: only the four known warnings, IRAM unchanged, `.bss` 36,280–36,288 B, `.data` 21,572 B, version 2.1.4. The image on the bench is that build (its `.bin` is newer than the `d9fa9c8` commit). | VAL-01 |
| EC-2 | **Every P0 and fix test passes.** Every test marked P0, and every test that the traceability matrix (section M) maps to BUG-1, BUG-2, BUG-3, BUG-5, BUG-6, P0-a, P0-b, P0-c, N1–N4, the E-02…E-22 fixes, F-01, F-08 or B1, has the result `Pass`. None is `Fail` or `Blocked`. **LoRa exception (N4, E-18):** when the bench has no LoRa hardware (the current production PCBA has no SX1262), DEC-08 variant B must `Pass`, and DEC-08 variant A, T3-08, T4-13 and T5-13 are recorded `N/A (no LoRa HW)` with a **written waiver** from the release owner that names N4 and E-18 (the packet path) as not bench-verified. Without that waiver they count as `Blocked`. | The results sheet |
| EC-3 | **No new warnings and no new error lines at runtime.** No UART `E (…)` line whose tag and text do not also appear in the 2.1.3 run of VAL-13, apart from the lines the CHANGELOG lists as new. | VAL-13 and VAL-14 UART |
| EC-4 | **Heap like-for-like ≥ 2.1.3.** VAL-13 passes, or its shortfall is 300 B or less and the release owner waived it in writing. No `LOW HEAP WARNING`, and no `Snapshot not built - out of memory` in any run with 5 or fewer devices. | VAL-13, VAL-14 |
| EC-5 | **Validator.** 0 FAIL, 0 ordering violations and 0 `auto_close` with no valve, on the full capture (VAL-15). Any FAIL left is annotated as the known false positive, with its evidence. | VAL-15 |
| EC-6 | **No reboot in the soak.** VAL-14 ran for ≥ 19 h with zero unplanned reboots, zero panics or watchdogs, and one SAS renewal. | VAL-14 |
| EC-7 | **Upgrade and rollback.** VAL-02 (runs A, B and C) and VAL-03 pass: provisioning, the rules config, a latched incident and an override survive 2.1.3 → 2.1.4 → 2.1.3 without erasing flash. | VAL-02, VAL-03 |
| EC-8 | **App.** VAL-04 to VAL-12 pass on both halves. An app-half failure is logged against the app, and **field rollout** waits until the app build that fixes it is released. The firmware may still be tagged. | VAL-04…VAL-12 |
| EC-9 | **Known limitations observed and recorded.** Every "observe and record" step has the result `Known-limit`, with evidence, and none behaved worse than documented. At a minimum: F-02 (a restart during a leak can release RMLEAK for one wet burst; the valve stays closed and re-locks on the next wet report); F-03 (a leak latched while the valve was unreachable, then a hub restart or a relink less than 10 s after drying, can be read as a button press: a false 24 h `trigger:"button"` window); the slow-RMLEAK-read-back ("stale confirm") false button window; DPS blocking `iothub_task` for up to 60 s per attempt; E-01 snapshot heap on a hub with about 20 or more devices; the `rmleak_auto_cleared` that may be missing in a pass that is also an MQTT reconnect. | T3-18, T4-06, T4-09, T4-11, T4-12, T5-04, T6-10 and the other tests listed in M.4 |
| EC-10 | **UART hygiene and log contract.** No line contains the site Wi-Fi password or the word `password` with a value; `BLE_VALVE: [SM] Fixed Passkey: configured (not logged)` is present and no passkey digits are printed; the production-tool lines are present byte for byte: `Firmware version: v2.1.4`, `Gateway ID :`, `WiFi STA MAC:`, `APP_LORA: Initializing LoRa Driver...`, `[HOST] NimBLE host task started`; and the bench anchors `Device table loaded:`, `Roll-up grace expired`, `PROV pulse armed`, `Commission: fast snapshot armed` are unchanged. | Any boot log; T4-14; the DEC- and T5- logs |
| EC-11 | **No open Fail, and every Blocked is resolved or waived in writing** by the release owner (a Blocked P0 test cannot be waived). | The results sheet |
| EC-12 | **Before any push.** Commit `6b84ae3` on `fix/2.1.4` contains the site Wi-Fi password in a doc. It must be redacted from history before the branch is pushed. Nothing is pushed and no PR is opened until the user asks. | `git log -p 6b84ae3` checked |

### 9.5 Coverage of the section 0 and section 9 tests

| Test | Covers |
|---|---|
| VAL-01 | CP3 build gate: warnings, sizes and version of `d9fa9c8`; the council final vote's "pending a clean CP3"; E-20 static budget (the `.bss` range) |
| VAL-02 | S25 upgrade; council F5 "Upgrade and rollback"; F-01 (owed flag false after a boot: the owed line must not appear); CHANGELOG Upgrade notes; run as T6-03 (runs A and B) plus run C |
| VAL-03 | S25 rollback; E-22; council F5 rollback; offline buffer compatibility; run as T6-04 |
| VAL-04 | BUG-2 UI-sync kept; S1; council F3 "a provision that adds no device no longer restarts the pulse"; P0-a (valve B never shows up) |
| VAL-05 | BUG-2; S2 |
| VAL-06 | BUG-3; BUG-5; BUG-6; S8; S9 (reboot) |
| VAL-07 | BUG-5; P0-a; P0-b; P0-c (UI half); S18/S19 (UI half); C5 |
| VAL-08 | BUG-1; S14; S17; C6; the user decision on the valve-only battery bands; unknown battery = null |
| VAL-09 | E-06 (valve_open refused while latched, valve powered off); the 10 s auto-clear; F-08; `b245d94` re-wet at the clear; council F3 "release then re-lock"; council "rmleak_auto_cleared may be missing in an MQTT-reconnect pass" (Known-limit) |
| VAL-10 | S24 override; the override events in the app |
| VAL-11 | E-16 (valve `last_seen_age_s` 0 while linked, `offline_duration_s` from the drop); the `device_offline` reachability-only wire change; S24 offline/recover |
| VAL-12 | Every CHANGELOG "parsers must accept" item; E-19 (a recovered device's fresh battery); the no-valve `auto_close` user decision; a provision that empties the hub (E-05 path) |
| VAL-13 | S26; E-01; E-20; council F2 "heap after G4b unmeasured"; council F2 "sensors-only hubs now run NimBLE" (recorded); measured with T6-07 and T6-08 |
| VAL-14 | "No reboot in the soak"; council F2 SAS renewal with less heap; council F1/F4 stack margin carry-over (no canary panic); E-01 (largest block trend); T6-12 is the sensors-only soak beside it |
| VAL-15 | All wire changes (validator assertions); F-08 ordering; the no-valve `auto_close` rule; N1 `ts` stamping (`ts` ≥ 1704067200) |

## Appendix A. Known limitations (CHANGELOG 2.1.4, verbatim)

These are tested as **observe and record** (result `Known-limit`). M.4 gives the test that observes each one. A known limitation fails only if the hub behaves worse than written here.

### A.1 Not changed in 2.1.4, and deferred

- On a hub with sensors and no valve, a leak still latches the leak incident (when `auto_close_enabled` and
  the source's trigger bit are set), so `rmleak_auto_cleared` (10 s after every sensor is dry) and
  `rmleak_cleared` (after `leak_reset`) are still sent, without `valve_id`, although there is no valve
  interlock to clear.
- Deferred to a later release:
  - the legacy keyword scan of C2D payloads;
  - command-id de-duplication;
  - the single-slot rules-event buffer;
  - more than 16 simultaneous leak sources;
  - sensors going unheard during a valve connect attempt;
  - LoRa driver hardening;
  - removing the valve bond on decommission.

### A.2 Known limitations

- A live DPS registration still blocks `iothub_task` for up to 60 s per attempt. While it runs, leak
  evaluation, auto-close and the rules tick wait for it. It happens only when the hub has no valid DPS cache:
  the first boot, after decommission all, or after a provisioning-epoch change. Moving DPS to its own task is
  future work.
- Between a valve target change and the old link's DISCONNECT, C2D checks can read the old valve's cached
  state (RMLEAK, battery, connected), and during a swap from one valve to another so can the rules engine.
  That window is about 0.5 s on the bench, and up to the 5 s supervision timeout when the valve does not
  answer the terminate. After a decommission, auto-close no longer counts that link as a reachable valve.
- Snapshot heap: cJSON prints a snapshot into a buffer that grows by doubling, so a snapshot needs about
  twice its printed size in one heap block. With NimBLE running the largest free block can be as small as
  7.5 KB (2.1.3 field log), so a hub with about 20 or more devices may fail to publish snapshots, retrying
  every 5 s. NimBLE now also runs on a sensors-only hub with a BLE sensor (P0-b), which 2.1.3 did not. Heap
  tuning is deferred to 2.1.5.
- Heap budget: 2.1.4 uses about 181 B more static RAM than 2.1.3 (`.bss` +160 B, `.data` +16 B at build
  checkpoint 2, plus about 5 B of `.bss` from the council fixes), plus about 50 B of
  permanent heap for the two per-tag log levels set at boot. Both come out of the heap (2.1.3 field
  minimum: 2972 B free). The NimBLE host task also uses about 54 B more of its fixed stack on the valve
  notify path. The captive-portal fix adds about 15 B of `.bss` and about 40 B of permanent heap (the
  SoftAP station-log event handler); build checkpoint 4 confirms the static figure.
- An override started before the clock synced and then restored after a software reset cannot be re-based,
  because its elapsed time is unknown, so it ends at the first clock sync, possibly hours early. That fails
  toward auto-close.
- An override restored after a power cut with no internet ends 24 h after the power-on if the clock has
  not synced by then. That is later than its real expiry, since the hub cannot know how long the power was
  off.
- Captive portal after a Wi-Fi reset: on a hub with a valve or a BLE sensor, NimBLE now starts at boot and
  stays up beside the SoftAP portal, which therefore has less free heap than on 2.1.3.
  - While no Wi-Fi credentials are saved, and for about a minute after Wi-Fi is set up, BLE scanning pauses
    so a phone can join and finish (see *Safety*). During that pause BLE leak sensors are not heard, and a
    lost valve link is not re-found unless a leak close is pended for it. LoRa sensors and an established
    valve link keep working.
  - The pause has no time cap before setup. If setup is abandoned for hours, a BLE sensor that fails
    meanwhile is reported offline only 600 s after scanning resumes, and a valve that fails, 180 s after.
  - On a valve hub the valve is not linked during a portal opened at boot, unless a leak close is pended. It
    reads "syncing" (white fleet LED) until it links, and counts as unheard only if it is still not linked
    180 s after scanning resumes. A valve whose link drops in the window reads "Valve disconnected" (yellow)
    for as long. A valve open, or an RMLEAK clear, pended meanwhile waits for the window to close.
  - The valve stays held even while a pended leak close runs its hunt in the window. The leak rates the hub
    critical while it lasts. Once it clears, a valve that hunt could not reach reads "syncing" or "Valve
    disconnected", not offline, until 180 s after scanning resumes, however long setup takes. A cloud
    `valve_close` pended between the IP and the SoftAP stopping runs the hunt the same way, with no leak.
  - On a hub with BLE sensors or a valve, the first snapshot after setup waits for scanning to resume and
    then for those devices to be heard (at most 180 s after the resume), so it normally comes about 60 s or
    more after the hub gets its IP. The lifecycle message and the buffered events still go out at the
    connect.
  - The router-outage fallback portal (credentials still saved) keeps BLE scanning, so a phone may fail to
    join it, as it did on the development builds. The 10 s reset then brings the hub back in the setup
    portal, with the window (see *Safety*). The portal page's own Wi-Fi scan every 3.8 s (the wifi_manager
    component) is unchanged.
  - The portal page's own disconnect button erases the credentials only while the hub is connected to the
    router, as in 2.1.3 (the Wi-Fi manager component): on the fallback portal, with the connection idle, it
    erases nothing. Use the 10 s reset there.
- Up to 16 events fit in the offline buffer. A long outage before the first clock sync can overwrite the
  oldest held events, as it already could after the sync.
- **A leak latched while the valve was out of reach can still be read as a button press at the reconnect
  (deferred to 2.1.5).** If the hub then restarts, or the valve reconnects less than about 10 s after every
  sensor dried, the reconnect finds the valve open with RMLEAK never applied. It reads that as a press of
  the valve button while the hub was offline (the SRS §4.4.2 cross-reboot inference, unchanged from 2.1.3)
  and starts a 24 h `water_access_override_enabled{trigger:"button"}` window. Auto-close is blocked for that
  window. After a restart the sensor may still be wet, and its next wet report is then blocked too
  (`auto_close_blocked_override`), so the valve stays open during the leak until the window ends, the
  override is cancelled, or someone closes the valve.
- **After a hub restart during a leak, the interlock can be released for one wet report (accepted).** The
  restart empties the hub's list of wet sources, so the first dry report from any device starts the 10 s
  auto-clear before a still-wet sensor is heard again. RMLEAK can then be released (`rmleak_auto_cleared`)
  for up to one wet burst of that sensor: about 15 s for a BLE sensor, minutes for a LoRa sensor. A
  `valve_open` in that gap is accepted. The valve stays closed unless it is opened, and the interlock
  re-latches (`auto_close`, closing the valve again) when the wet sensor is heard.
- **A slow RMLEAK read-back can still be read as a button press (pre-existing, planned for 2.1.5).** When
  the hub re-asserts RMLEAK right after its own clear (a re-wet at the moment of an auto-clear, or at a
  reconnect while a wet source is outside the trigger mask), the hub can take the valve's pre-clear 1 as
  confirmation. If the re-assert's read-back then lands more than 5 s later (about twice the bench round
  trip, marginal RF), the clear's 0 is read as a press: a false `water_access_override_enabled{trigger:
  "button"}` and auto-close blocked for 24 h. The valve itself still ends closed with RMLEAK set.
- Found in the final review and planned for 2.1.5:
  - With the forced-relink cap engaged, a live command that fails on a ready link stays pended until a newer
    command or the next reconnect (2.1.3 dropped it). A live command held behind a pended RMLEAK can wait the
    same way if the 10-deep command queue is full. Under a sustained busy GATT pool one command can hold the
    valve task for about 10 s.
  - A leak re-assert decided in evaluate_leak is queued after the rules lock is released, so a `leak_reset`
    that lands in that gap is overridden (the valve stays locked; fails closed).
  - A leak evaluated while provisioning is busy for more than 1 s (for example during a C2D save) is dropped
    by the rules engine, so the valve is not closed until that sensor reports again (up to 5 minutes for a
    BLE sensor; a LoRa sensor's next packet).
  - The rules reset after a valve replacement is not retried when the rules lock is busy for more than 1 s,
    and a failed rules reset of an emptied hub (lock busy for more than 5 s) is not retried when a
    `provision` arrives first. The old valve's leak source, or a stale latch or override window, can then
    survive into the new setup.
  - A valve that went offline with its flood probe wet and comes back dry after more than 180 s sends a
    late `device_offline` as it reconnects, then `device_recovered` 60–90 s later.
  - After a decommission and re-provision of the same valve within one loop pass, a valve that does not
    relink can stay connected and excellent in health (and green on the LED) with no link.
  - A hub name set for the first time while a snapshot is built fails that snapshot with a misleading
    `Snapshot not built - out of memory`; it is rebuilt at the 5 s retry.
  - Twin reported can read `provisioned:false`, `valve_id` null and device counts of 0 (a
    decommissioned-looking twin) when provisioning is busy for more than 1 s; the next device-set change
    republishes it. The lifecycle `provisioned` flag has the same exposure.
  - A `provision` that adds no device (an identical re-send, a rules-only provision) no longer restarts the
    commission snapshot and the post-provision snapshot pulse, as 2.1.3 did; only newly added devices do.
    The command still gets its own `event` snapshot.
  - When a reconnect replays both a held RMLEAK and a held valve command, the valve command's read-back finds
    the GATT pool full (`[CMD] valve read-back rc=6 - position unconfirmed`); the valve's own state
    notification then reports the position.

## Appendix B. UART log lines: new and changed in 2.1.4

### B.1 The CHANGELOG list (verbatim)

- Unchanged: every line the production tool and the bench scripts match.
- A refused valve command now logs its full reason: `VALVE_OPEN refused — <detail>`, and likewise for
  `VALVE_CLOSE` and `VALVE_SET_STATE open|closed`. It used to print `... — valve RMLEAK is asserted`.
- The fixed BLE passkey is no longer printed. Changed lines (`BLE_VALVE`):
  - `[PASSKEY] INPUT required. Responding with the fixed passkey`
  - `[PASSKEY] DISPLAY action. Responding with the fixed passkey`
  - `[SM] Fixed Passkey: configured (not logged)`
- New, pre-sync events. `TELEMETRY_V2`: `Time not synced (ts=%ld) - holding %s for replay; stamped when the
  clock syncs` (snapshot and lifecycle keep `Time not synced (ts=%ld) — suppressing %s`). `OFFLINE_BUF`:
  - `Stamped pre-sync event [%s] at clock sync: ts=%lld (%lld s ago)`
  - `Stamped pre-sync event [%s]: ts=%lld (%lld s before this replay)` (only when the stamp at the sync
    could not be written)
  - `Dropped a buffered event from an earlier power cycle that was never time-stamped [%s]` (also after a
    software restart before the clock synced)
  - `Dropped a buffered pre-sync event [%s] - cannot be time-stamped from its uptime_s`
  - `Dropped a buffered pre-sync event [%s] - too long once time-stamped`
  - `Clock not synced - pre-sync event [%s] and %d after it kept for the next drain`

  The last three should never appear in a normal run.
- New, override window (`RULES_ENGINE`):
  - `Override window stamped before clock sync - timed on uptime (start=%lus) until the clock syncs`
  - `Override window re-based to the synced clock (expiry=%ld, remaining=%lds)`
  - `Override window ran its full duration before the clock synced (elapsed=%lus) - expiring it now`
  - `Override window was stamped before a clock sync in an earlier boot - elapsed time unknown, expiring it now`
  - `NVS: that window was stamped before a clock sync - elapsed time unknown, not restored`
- New, provisioning (`PROVISIONING`):
  - `LoRa sensor count %u in NVS exceeds %d - clamped`
  - `BLE leak sensor count %u in NVS exceeds %d - clamped`
  - `Failed to take mutex in with_valve_target`
- New, valve command replay and reconnects (`BLE_VALVE`). Each replay is announced by `[CMD] Replaying
  pending ...` and then logs the usual `[CMD] Writing ...`, so a script counting `[CMD] Writing` as live
  commands also counts replays.
  - `[TASK] CMD: REPLAY_PENDING`
  - `[CMD] Replaying pending %s command=%d`
  - `[CMD] Replay of pending valve commands dropped - the valve target changed`
  - `[CMD] Pending %s command=%d cancelled or superseded meanwhile - not applied`
  - `[CMD] Pending %s command=%d cancelled, superseded or flushed meanwhile - not replayed`
  - `[CMD] Pending %s command=%d kept for the next link`
  - `[CMD] Pending valve command=1 kept behind the RMLEAK command`
  - `[CMD] Pending valve commands not applied - left to the replay already queued`
  - `[CMD] Pending valve commands not applied - replay queued ahead of newer commands`
  - `[CMD] Pending valve commands not applied, command queue full - kept for the next link`
  - `[CMD] Valve=%u not written - kept behind the pending RMLEAK command`
  - `[CMD] valve writes keep failing - no more forced reconnects until a write succeeds`
  - `[CMD] valve write failed %d times (rc=%d) - kept for the next link, no forced reconnect (%s=%u)`
  - `[DISCONNECT] terminate failed status=%d - link stays up (handle=%u)`
- New, review fixes:
  - `RULES_ENGINE`: `Override window restored after a power-on has run its full duration with no clock sync - expiring it now`
  - `RULES_ENGINE`: `AUTO-CLOSE: no provisioned valve - auto_close event not published`
  - `RULES_ENGINE`: `Valve replaced: old valve leak source %s, %u source(s) still wet, incident %s`, on every
    valve replacement or removal (`dropped` or `not tracked`; `released`, `kept` or `not latched`)
  - `RULES_ENGINE`: `Failed to take mutex (valve replaced)`
  - `HEALTH_ENGINE`: `Valve event from %s dropped - the table's valve is %s`
  - `BLE_VALVE`: `[CMD] %s val=%u pended as the link became ready - replaying it`
  - `TELEMETRY_V2`: `Message not built (%s) - out of memory`
  - `IOTHUB`: `Telemetry-cache purge: provisioning busy, retrying`
  - `PROVISIONING`: `Hub empty: rules config reset to defaults`, after the removal or `provision` that
    empties the hub
- New, council fixes:
  - `RULES_ENGINE`: `Reconnected: valve RMLEAK active, hub incident clear - RMLEAK clear owed by the hub, not re-latching`
  - `RULES_ENGINE`: `RMLEAK clear read back - the hub's own clear, not a valve override` (only when an
    incident is latched again before the hub's own clear is read back; a plain clear across a relink ends
    silently, or with `Reconnected: no active incident, valve clear`)
  - `RULES_ENGINE`: `RECONNECT: RMLEAK clear not sent - no provisioned valve` (warning) and `RECONNECT:
    RMLEAK clear enqueue FAILED — valve interlock left set` (error)
  - `BLE_VALVE`: `[CMD] %s write: GATT busy - waiting` and `[CMD] %s read-back: GATT busy - waiting`, once
    per wait
  - `BLE_VALVE`: `[CMD] %s=%u kept pending - GATT still busy, replaying it`, after a 5 s wait
  - `BLE_VALVE`: `[CMD] %s=%u held behind the pending RMLEAK command`
  - `BLE_VALVE`: `[CMD] Pending valve command=0 kept behind the RMLEAK command (GATT busy)`

  `GATT busy - waiting` is normal when a reconnect replays commands while new ones are queued. None of
  these should repeat for long.
- New, Wi-Fi setup portal (the portal priority window, see *Safety*):
  - `APP_WIFI`: `portal priority ON (no Wi-Fi credentials) - BLE scanning paused` (warning), then `portal
    priority: wifi_manager task prio %u -> %u (httpd, dns_server not raised)` (`5 -> 8`)
  - `APP_WIFI`: `portal priority: Wi-Fi connected - BLE scanning stays paused until the setup AP stops (about
    %d s)` (`60`), right after `Connected! IP: %s` when Wi-Fi is set up in the window
  - `APP_WIFI`: `portal priority OFF (%s) - BLE scanning resumed`: `AP stopped` (normally about 60 s after
    `Connected! IP`), or `Wi-Fi lost after setup`
  - `APP_WIFI` (warning): `portal priority: setup AP still up %u s after Wi-Fi connected - stopping it` (the
    safety net; should never appear)
  - `APP_WIFI`: `SoftAP up with saved Wi-Fi credentials (router fallback) - BLE scanning stays on`
  - `APP_WIFI`: `SoftAP: station %02X:%02X:%02X:%02X:%02X:%02X joined, AID=%u` and `SoftAP: station
    %02X:%02X:%02X:%02X:%02X:%02X left, AID=%u, reason=%u`, on any SoftAP, window or not
  - `HEALTH_ENGINE`: `BLE scanning paused - BLE sensor timeouts held` and `BLE scanning resumed - BLE sensor
    timeouts restart now (%d s)` (`600 s`). They name only the BLE sensors, but the valve's 180 s is held
    and restarts with them.
  - `BLE_LEAK`: `Scan paused - Wi-Fi setup portal has the radio` and `Scan resumed - Wi-Fi setup portal closed`
  - `BLE_VALVE`: `[SCAN] Valve scan held - Wi-Fi setup portal has the radio`
  - `BLE_VALVE`: `[PORTAL] Valve hunt paused - Wi-Fi setup portal has the radio` (followed by ` (valve link
    kept)` when the valve is linked), `[PORTAL] Valve hunt stopped - Wi-Fi setup portal has the radio` and
    `[PORTAL] Valve hunt resumed - Wi-Fi setup portal closed`
  - `BLE_VALVE` (warnings): `[PORTAL] Leak response pending - valve hunt runs despite the Wi-Fi setup portal`
    and `[PORTAL] Valve hunt not paused - a leak response is pending`
  - `BLE_VALVE`, when the window opens as a hunt or a connect is starting or running: `[PORTAL] Valve scan
    cancelled - Wi-Fi setup portal opened`, `[PORTAL] Valve connect cancelled - Wi-Fi setup portal opened
    (rc=%d)` and `[PORTAL] Valve connect in flight cancelled (rc=%d)`
  - `BLE_LEAK`: `Scan cancel failed: %d, will retry` (warning; should never appear)

  `portal priority ON` must never appear on the router-outage fallback portal.
- New, the 10 s Wi-Fi reset (see *Safety*). `RESET_BTN` prints one outcome line about 2 s after `Erasing
  WiFi credentials, then rebooting into AP (commissioning preserved in nvs_prov)...`, just before
  `Rebooting into AP mode...`:
  - `Wi-Fi credentials erased from NVS`
  - `No Wi-Fi credentials saved - nothing to erase` (nothing was ever saved)
  - `Wi-Fi credential erase failed (%s) - rebooting anyway` (error; should never appear)

  The warning `Wi-Fi NVS lock busy for 3 s - erasing without it` can come before it and should never appear
  either. `APP_WIFI` `WiFi Disconnected. Reason: 8` between `Erasing WiFi credentials …` and the outcome
  line still shows that the hub was connected when the button was pressed. With an idle connection it does
  not print, and the credentials are erased all the same.
- Same text, new conditions (portal window):
  - `HEALTH_ENGINE` `Boot sync: timeout (%lu s) — snapshot gate open; unheard devices still excused for a
    further %lld s`: not while BLE scanning is paused with a BLE sensor or the valve not yet heard, nor
    within 180 s after the resume; the further excuse of such a device then counts from the resume.
  - `HEALTH_ENGINE` `Roll-up grace expired (%lu s) — %d unheard device(s) now count`: for a BLE sensor, never
    earlier than 600 s after scanning resumed; for a valve not linked yet, never earlier than 180 s after it.
  - `BLE_LEAK` `Extended passive scan started (1M + Coded PHY)` and `BLE_VALVE` `[SCAN] Starting scan for
    provisioned valve %s...`: not while the window is open, except the valve hunt for a pended leak close.
- Same text, new conditions (council fixes):
  - `RULES_ENGINE` `RMLEAK cleared externally (valve override) — starting 24h override window`: only when
    the valve was seen with RMLEAK set during the incident and no hub clear is owed.
  - `RULES_ENGINE` `Reconnected with %d active leak(s) — valve already closed + RMLEAK asserted, nothing to
    do`: not while a hub clear is owed; `Valve reconnected with %d active leak(s) — executing auto-close`
    prints instead.
  - `BLE_VALVE` `[CMD] Writing %s=%u` and `[CMD] %s write rc=%d (value awaits the valve's own report)` are
    not printed for a retry that polls a busy GATT pool; after a busy poll only a result other than rc=6
    prints. Every other attempt logs both as before.
  - `BLE_VALVE` `[CMD] %s read-back rc=%d - %s unconfirmed`: also when a read-back's 5 s busy wait runs out
    (rc=6) or its lock wait times out (rc=-1).
  - `BLE_VALVE` `[CMD] valve writes keep failing - no more forced reconnects until a write succeeds`: now
    until a live command write succeeds.
  - A rules event raised by the tick logs its `Pub event: ...` (or offline-buffer) line right after the
    tick, before the pass's leak-report lines.
- Changed number, RMLEAK auto-clear (`RULES_ENGINE`): these print `10s` where they printed `30s`. The
  production tool matches neither; `docs/health_leak_led/TEST_PLAN.md` still quotes the 30 s values.
  - `All sensors clear — auto-clear timer started (%ds)`
  - `AUTO-CLEAR: all sensors clear for %ds — clearing RMLEAK`
- Same text, new triggers:
  - `TELEMETRY_V2` `Snapshot not built - out of memory`: also when a snapshot key could not be added.
  - `IOTHUB` `Hub empty: rules-engine RAM reset failed - retry owed`: also from the C2D command that empties
    the hub.
  - `BLE_LEAK` `Sensor tracking reset`: also after every device-set change that adds devices.
  - `BLE_VALVE` `[TASK] CMD: CONNECT` and the warning `[SCAN] Already connected`: also when a `provision`
    names the valve already linked.
  - `BLE_VALVE` `[CMD] RMLEAK write not ready. Queuing val=1` and `[CMD] Valve write not ready. Queuing
    val=0`: also when a leak, an override cancel or an override expiry needs an unreachable valve closed.
  - `RULES_ENGINE` lines that print an override window's remaining time (`NVS: restored override window`,
    `OVERRIDE WINDOW CANCELLED`, `Override active — auto-close BLOCKED`, `Override window: remaining=`) print
    the countdown from the power-on for a window restored with the clock lost, instead of -1 or 0.
  - `OFFLINE_BUF` `Stamped pre-sync event [%s]: ts=%lld (%lld s before this replay)`: also for an event too
    close to 512 B to be stamped in flash at the sync.
- Gone from the 2.1.4 development builds: `IOTHUB` `Hub empty: rules config reset to defaults failed`. On the
  emptying command, `PROVISIONING` `Setting rules config: auto_close=enabled triggers=0x07` and `Rules config
  saved to NVS` no longer appear; the removal's own save logs `Config saved to NVS successfully`.
- Gone: the requeue lines of the 2.1.4 development builds, `... not applied (rc=%d) - requeued for retry`,
  `... flushed or superseded meanwhile - not requeued`, `... not written - it follows the requeued RMLEAK
  command`, `... kept for the next link, behind the RMLEAK command` and `Pending %s command=%d not applied,
  command queue full - kept for the next link`.
- Gone from the 2.1.4 development builds (portal window): the `Wi-Fi connected` reason of `APP_WIFI` `portal
  priority OFF (%s) - BLE scanning resumed`. The window no longer closes at `Connected! IP`.

### B.2 Lines that must not appear in a normal run (quick grep)

Run this over every UART capture of the campaign (PowerShell, in the capture folder). Any hit needs the test's own rules to excuse it; the "Allowed only in" column lists the tests that expect the line.

```powershell
Select-String -Path .\*_uart.txt -Pattern 'Guru Meditation','abort\(\)','stack overflow','Task watchdog got triggered','assert failed','could not be added','RMLEAK cleared externally','inferring physical override','write attempt .* \(rc=6\)','reconnecting to re-apply','Snapshot not built - out of memory','Message not built','LOW HEAP WARNING','SAS: esp_mqtt_set_config failed','ENQUEUE FAILED','NVS: incident save failed','ESP_ERR_NVS','Dropped a buffered pre-sync event','Buffer full, oldest event overwritten','portal priority ON','Scan cancel failed','setup AP still up','Wi-Fi NVS lock busy','Wi-Fi credential erase failed' | ForEach-Object { "$($_.Filename):$($_.LineNumber): $($_.Line)" }
```

| Line (substring) | Allowed only in |
|---|---|
| `Guru Meditation`, `abort()`, `stack overflow`, `Task watchdog got triggered`, `assert failed` | nowhere |
| `QueueSet: %s queue could not be added%s` | nowhere |
| `RMLEAK cleared externally (valve override) — starting 24h override window` | T3-15 (and T5-08 = T3-15), T4-07, T4-08, T4-09, VAL-02 run C (on 2.1.3), T3-18 stale-confirm observe part |
| `Reconnected: hub incident + valve open + RMLEAK clear — inferring physical override, starting 24h window` | T4-12 (F-03, observe) |
| `write attempt %d/%d failed (rc=6)`, `reconnecting to re-apply` with `rc=6` | nowhere (B1) |
| `Snapshot not built - out of memory`, `Message not built (%s) - out of memory` | T6-10 (observe); the first-time hub-name case (0.17) |
| `LOW HEAP WARNING` | a finding in T4-10 and T6-10; a Fail elsewhere |
| `SAS: esp_mqtt_set_config failed` | nowhere |
| `ENQUEUE FAILED` | T6-17 Part B (`CLOSE ENQUEUE FAILED` under the 12-command burst) |
| `NVS: incident save failed`, `ESP_ERR_NVS` | nowhere |
| `Dropped a buffered pre-sync event [%s] - …` | nowhere (CHANGELOG: never in a normal run) |
| `Dropped a buffered event from an earlier power cycle that was never time-stamped` | T4-04, T4-09 Part B, T6-14 step 5 |
| `Buffer full, oldest event overwritten` | T6-14, T4-08 (24 h run) |
| `portal priority ON (no Wi-Fi credentials) - BLE scanning paused` | T4-10 (Parts A, B, E, F, G, H, D5-D6, and A2 before its reboot), T6-14 step 3, smoke step 10. Anywhere else, above all on a router-fallback SoftAP, it is a Fail: it pauses BLE leak protection |
| `Scan cancel failed: %d, will retry` | nowhere (record it with the test) |
| `portal priority: setup AP still up %u s after Wi-Fi connected - stopping it` | nowhere: the safety net fired, so the setup SoftAP did not stop on its own; record it with its time after `Connected! IP` |
| `Wi-Fi NVS lock busy for 3 s - erasing without it`, `Wi-Fi credential erase failed (%s) - rebooting anyway` | nowhere (record it with the test) |
| The site Wi-Fi password, or the valve passkey digits | nowhere (T4-14) |

**N17 section-wide rule (IoT Hub capture).** In every test, a snapshot whose `valve` is `{}` and whose `ble_leak_sensors` and `lora_sensors` are both `[]` while the twin or UART shows devices provisioned (`Device table loaded: N device(s)` with N > 0) is a **Fail**: 2.1.4 skips the snapshot when the health-table copy fails, and the 5 s retry publishes the real one. The validator does not check this; look for it in the snapshot at each device-set change.
