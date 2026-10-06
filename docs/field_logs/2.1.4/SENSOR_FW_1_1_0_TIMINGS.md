# The BLE leak sensor timings that hub firmware 2.1.4 depends on (sensor FW 1.1.0)

**What this is.** Hub firmware 2.1.4 shares one radio between BLE scanning and Wi-Fi (the radio policy,
`main/radio_policy/`). It guarantees leak detection only because it knows how the eFloStop BLE leak sensor
advertises. Those timings belong to the sensor firmware (FW 1.1.0, the STM32WBA leak sensor), not to the hub. This
page lists each one, the hub constant that encodes it, what in the hub depends on it, and what happens if a sensor
release changes it. It is plan item WP10's "the FW 1.1.0 timing assumptions the hub's asserts depend on"
(`RADIO_PORTAL_PLAN.md` §4.8, §5, §11, §14; `HANDOFF.md` 15u, 15w).

**The rule for the sensor team.** A sensor release that changes any row below must be checked against this page
before it ships, and the hub's constants and its leak model re-run with the new values. A changed constant makes the
hub's compile-time checks name every scan row that no longer covers a burst; **an unchanged constant with a changed
sensor does not**: the hub then builds and runs, and silently misses part of its guarantee.

**Status.** Every value below is the plan's assumption from the sensor's design. **None is measured yet:** bench
gate G0 measures the advert spacing (Ta) and the loss rate (p_loss) per sensor, and the model is re-run with them
(`HANDOFF.md` 15u, residual 1). Until then the hub's constants are marked PROVISIONAL in `radio_policy.h`.

## 1. The timings

| # | Sensor timing (FW 1.1.0) | Hub constant (file) | What depends on it in the hub | If a sensor release changes it |
|---|---|---|---|---|
| T1 | **Advert spacing within a burst (Ta):** 312.5-437.5 ms, plus the BLE stack's random advDelay of 0-10 ms, so at most **448 ms** between two adverts | `RP_ADV_SMAX_MS` 448 (`radio_policy.h`); derived `RP_L_MS` = 448 + `RP_JITTER_MS` 100 = **548 ms**, `RP_W_MIN_MS` = 548 + 50 = **598 ms** (the shortest Coded window that always holds an advert); `L_TICKS` (`app_ble_leak.c`: 548 ms rounded up to whole ticks plus one, 56 ticks at 100 Hz) | **I1** (every Coded window at least 598 ms, `_Static_assert` per row and the boot self-test); I2's recovery (≥ 2 × 548 ms); a pulse granted into a "young" Coded scan only once it covered 548 ms, or with 1.5 s of budget (`RP_UNALIGNED_MIN_MS`); the I2 monitor's "covering window" | Longer: windows of 0.6 s no longer always hold an advert; every 0.6 s Coded row (AP_IDLE, SERVE, AP_K1M, LR_AP) loses its burst guarantee. Raise `RP_ADV_SMAX_MS` and the asserts name the rows to lengthen. Shorter: harmless |
| T2 | **Heartbeat burst length:** at least **2.5 s** of adverts per burst | `RP_BURST_MIN_MS` 2500; derived `RP_GAP_MAX_MS` = 2500 − 2 × 548 = **1,404 ms** (the longest Coded gap, jitter included) | **I1** (every row's Coded gap plus jitter ≤ 1.404 s: per-row `_Static_assert`); so every 2.5 s burst outside a pulse contains a whole Coded window | Shorter than about 2.4 s: the rows whose Coded (or 1M) gap plus jitter is 1.3 s (AP_K1M, LR_AP, LR_AP_30, SERVE-B and its discovery row) stop guaranteeing every burst first, then N_MIXED and NORMAL_LR (1.2 s with their dither); the asserts name them once the constant is updated |
| T3 | **Leak-edge burst:** at least **4.0 s** of adverts when a sensor turns wet | `RP_BURST_EDGE_MS` 4000 | **I2:** `RP_BLIND_MAX_MS` (2.8 s) + `RP_RECOVERY_MS` (1.2 s) ≤ 4.0 s (`_Static_assert`, `radio_policy.c:137`): any pulse plus its recovery fits inside one edge burst, so a wetting during a pulse is still heard in its first burst | Shorter: a wetting that starts at a pulse is heard only at the next wet heartbeat (about 15 s later), not in its edge burst |
| T4 | **Wet heartbeat cadence:** a burst every **15 s** while wet | none directly; the **period rule** `RP_PERIOD_OK()` (`radio_policy.c:78`): no row or row sequence lasts a whole divisor of 8, 15 or 100 s; the leak model's cadence; `BLE_LEAK_HEARTBEAT_MS` (5 min, the hub's own re-report of an unchanged sensor) | The period rule keeps a row from phase-locking onto the heartbeat; every leak-latency figure in plan §5 and `HANDOFF.md` 15u/15w (p99 / p99.9 at cadence 15 s); **M4's pacing window `RP_REPEAT_MS` 45 s** (a stranger timing Connects to one sensor's heartbeats covers 1 burst in 3 at 45 s, 1 in 2 at 30 s, which is past G2: a multiple of the cadence is the worst choice) | Another cadence: re-check the period rule's divisors (add the new cadence to `RP_PERIOD_OK()`), re-run the leak model, and re-choose `RP_REPEAT_MS` so it is not a multiple of the cadence |
| T5 | **Dry cadence:** a burst about every **100 s** while dry | `HEALTH_BLE_LEAK_TIMEOUT_MS` 600 s (`health_engine.h`; offline after 10 min unheard: about 6 bursts of margin); `STARVE_MS` 250 s (`app_ble_leak.c`: the sensor-starvation guard); the period rule's 100 s | No false `device_offline` while a sensor is dry and in range; the starvation guard (a sensor unheard ≥ 250 s gets a denser profile for 110 s, at most once per 600 s) | Slower: the 600 s offline margin and the 250 s guard must be re-derived; a cadence near 120 s or more leaves fewer than 5 bursts per offline window |
| T6 | **PHY:** LE Coded (S=8) by default; **1M when the sensor's LR_BUT is set** (some field sensors are on 1M: user answer D1, 2026-10-01); the PHY changes only by that setting | the per-sensor PHY table (`app_ble_leak.c`: learned from `ext_disc.prim_phy`, changed after `PHY_FLIP_ADVERTS` 4 adverts in a row on the other PHY, saved in `nvs_prov` namespace `ble_phy`, at most 112 B); `RP_UNKNOWN_PHY_MS` 600 s (B3) | Which rows run: N_CODED (1M 20 %, Coded 80 %) when every sensor is known Coded; N_MIXED / AP_K1M (I1 also for 1M) while a sensor is known on 1M or not known yet; the 1M discovery slots | A sensor that switches PHY at run time without a setting change, or advertises on both PHYs: the table flips and logs `PHY learned` lines; coverage stays (both PHYs scanned), but the one-PHY rows lose their 1M or Coded guarantee for that sensor until it is learned again |
| T7 | **Advertising type and content:** extended advertising, passive scan is enough (no scan response needed), the leak state, battery and firmware in the eleak advertisement (parsed by `process_leak_adv()` in `app_ble_leak.c`) | the scanner's passive extended scans (`filter_duplicates` off) | Every Coded and 1M window | A connectable or scan-response-only format, or a legacy-only advertisement on Coded: the hub would hear nothing; the hub's parser must change with it |

## 2. Where the hub checks them

- **At compile time** (`main/radio_policy/radio_policy.c` lines 86-146): per row, I1 for Coded (one Coded window ≥ `RP_W_MIN_MS`, gap plus jitter ≤ `RP_GAP_MAX_MS`), I1 for 1M on the rows that run with a known-1M sensor, I8 on the AP rows, the period rule on every row and sequence, and I2's `RP_BLIND_MAX_MS + RP_RECOVERY_MS <= RP_BURST_EDGE_MS`, `RP_RECOVERY_MS >= 2 * RP_L_MS`. A violation stops the build and names the row.
- **At boot:** the same checks run again as a self-test: `RADIO: Profile self-test passed: 14 rows hold I1, I2, I8 and the period rule (SERVE rung SERVE-A)`. A failure pins the hub to the safe normal scan (N_CODED, no Wi-Fi pulses) and prints `Profile self-test FAILED (<row>: <rule>) - pinned to NORMAL (N_CODED), no Wi-Fi pulses`.
- **At run time:** the I2 monitor (`RADIO: I2: BLE went N ms with no Coded scan (limit 2800 ms) - send this log`, and `[SUMMARY]`'s `longest Coded gap N ms (k over 2900 ms)`), and the duty watchdog.
- **On the bench (G0):** the scanner's per-burst line `BLE_LEAK: eleak <MAC> burst: n=N in S s, dT a-b ms, phy=<PHY>` (first 4 sensors) gives Ta (its shortest `dT` must stay at or below 437.5 ms), the burst length and the PHY.

## 3. What the hub does not assume

- No wet latch, no longer edge burst and no tighter advert spacing: these sensor changes were considered and left out of 2.1.4 (plan §14, "Sensor firmware changes … out of this release"). If a later sensor release adds them, the hub's guarantees only get better, but its constants should be updated to claim it.
- No particular p_loss: the guarantees are stated at p_loss 0.3 and 0.5 (plan §5); G0 measures it.
- Nothing about LoRa sensors: they are received by the SX1262 path, which the radio policy does not schedule.

## 4. Change log of this page

| Date | Change |
|---|---|
| 2026-10-07 | First version (WP10), for hub firmware 2.1.4 at `b651701` (CP7). |
