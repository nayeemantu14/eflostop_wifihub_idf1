# FW 2.1.3 — manual bench test plan

Firmware under test: **2.1.3** (`PROJECT_VER`, `CMakeLists.txt:12`).
Design + rationale: `PLAN.md` in this folder.

---

## 0. Setup, capture, and handoff

### Build + flash
```
idf.py build
idf.py -p <COM> flash monitor
```

### Hardware
- 1 hub (ESP32-S3), fleet LED on **GPIO 48** (solid colours), network LED on GPIO 38 (animated)
- 1 valve (STM32WB, FW ≥ 2.2.0)
- **4 BLE leak sensors** (STM32WBA "eleak"). At least one placed **far** (weak RSSI) so it is a
  genuinely late first-contact — several cases depend on that
- Water: a damp cloth or a shorting wire across the sensor probe

### Capture — every case
1. `UART logs.txt` ← the full `idf.py monitor` output, from the **reset banner** onward
2. `IoT hub monitor.txt` ← VS Code Azure IoT Hub **Start Monitoring Built-in Event Endpoint**

Both files are **overwritten each run**, so note in your message which case each capture is for.
Default paths:
```
C:\Users\antun\Desktop\UART logs.txt
C:\Users\antun\Desktop\IoT hub monitor.txt
```

### What I need in your message
- which case(s) the capture covers
- anything you did out of order, or did not wait for
- the LED colours **you actually saw**, with rough timings — the UART logs transitions, but your
  eyes are the check on the LED itself

### Reading the new log lines

| Line | Meaning |
|---|---|
| `PROV pulse armed: every 30 s for 300 s, plus on every sensor packet` | a `provision` opened the window |
| `SNAP trigger=event:prov_pulse` | a periodic (30 s) pulse published |
| `SNAP trigger=event:prov_pkt` | a sensor packet triggered a publish |
| `SNAP clamped by min-interval: +N ms` | two requests inside 5 s — expected, coalescing working |
| `PROV pulse window closed (N snapshot(s) requested)` | window expired; N counts **requests**, not messages |
| `PROV pulse capped at 40 snapshots …` | the safety valve bit — should NOT appear |
| `Boot sync: timeout (180 s) — snapshot gate open; unheard devices still excused for 600 s` | deadline 1 |
| `Roll-up grace expired (600 s) — unheard devices now count` | deadline 2 |
| `FLEET_LED: rating=<r> color=<c> effect=SOLID` | one line per LED transition |

---

## Part A — provision pulse

### P1 — the pulse runs (headline case)
**Setup** Provisioned hub, valve linked, all 4 sensors in range and dry, LED GREEN and settled.
**Steps**
1. Start both captures. Note the wall-clock.
2. Send a `provision` C2D command (the normal app/tool commissioning payload, same device set).
3. **Wait a full 6 minutes** without touching anything. Do not wet a sensor.

**Pass**
- `PROV pulse armed: every 30 s for 300 s, plus on every sensor packet` appears within ~2 s of the
  `C2D_CMD: Envelope cmd='provision'` line
- `SNAP trigger=event:prov_pulse` appears roughly every 30 s (**±5 s**, the min-interval clamp may
  nudge one later) for 5 minutes → expect **8–10** of them
- `PROV pulse window closed (N snapshot(s) requested)` at **~300 s** after arming
- IoT Hub shows a matching run of snapshots across the window, then the cadence drops back to the
  5-minute heartbeat
- **No** `PROV pulse capped` line

**Fail signatures** no `PROV pulse armed` (arming broken); pulses stop early (window collapsing);
pulses continue past ~305 s (expiry broken); gaps > 40 s between pulses.

---

### P2 — packet-triggered refresh (the second headline)
**Setup** Same run as P1 — read it from the same capture.
**Steps** none; this is analysis of the P1 capture.

**Pass**
- At least one `SNAP trigger=event:prov_pkt` inside the window — ideally several
- Distinct `prov_pkt` and `prov_pulse` lines both present: the two arms are independent

**Read this criterion carefully — the obvious version of it is wrong.**

Do **not** expect a `prov_pkt` to follow a `BLE_LEAK: eleak <MAC> …` line. That log statement sits
*below* the scanner's telemetry delta gate, so **an unchanged repeat packet prints no `eleak` line at
all** — only first contacts and genuine value changes do. Pairing `prov_pkt` against `eleak` lines
therefore looks like a failure when the feature is working perfectly.

The correct assertion is the **inverse**: a `prov_pkt` with **no** nearby `eleak` line is the proof.
It means a delta-gated repeat packet — invisible to `iothub_task` before 2.1.3, and still invisible
to the D2C event path — reached the health engine, bumped the check-in counter, and produced a fresh
snapshot. That is precisely the gap this feature closes.

*(Confirmed on the 2.1.3 capture: `prov_pkt` at 405957 / 505737 / 548187 / 556347, none with an
`eleak` line, while the only `eleak` lines in the window were the three first-contacts.)*

Packets landing within 5 s of a previous publish coalesce rather than producing their own line —
that is `SNAP_MIN_INTERVAL_MS` working. Expect fewer `prov_pkt` lines than sensor bursts.

---

### P3 — the refreshed data is actually new
**Setup** Same P1 capture, plus one deliberate action: at about T+90 s, **physically move one
sensor** (3–4 m further away, or behind a wall) to force its RSSI to change.
**Pass** Comparing two consecutive in-window snapshots in the IoT Hub log, that sensor's
`health[i].rssi` (and/or `battery`) **changes value**. Proves the snapshots carry fresh sensor data
rather than re-publishing a cached row.
**Note** RSSI naturally jitters a few dB; look for a clear shift (≥ 5 dB), not noise.

---

### P4 — the window is time-based, not seen-based (regression guard)
**Setup** All 4 sensors **close to the hub** so every one is heard within the first ~60 s.
**Steps** Provision. Watch for the complete `SNAP trigger=commission` or `=boot`. Then keep watching.
**Pass** Pulses **continue** at 30 s all the way to ~300 s, *after* every device has been heard.
**Why** The pre-existing refresh window is zeroed at `seen >= total`. If the pulse had reused it,
pulses would stop the moment the last sensor checked in. This case is the direct guard on D2.

---

### P5 — the guaranteed complete snapshot survives
**Setup** Same captures as P1/P4.
**Pass**
- Exactly **one** `SNAP trigger=boot` or `SNAP trigger=commission` per provision, carrying the full
  device table
- It is **not** replaced or skipped by pulses
- Its `data.reason` in IoT Hub is `"boot"` or `"commission"`; every pulse's is `"event"`
- **No** snapshot anywhere in the capture has a `reason` outside
  `heartbeat | event | commission | boot | fast | decommission`

**Why** Pulses use `SNAP_EVENT` specifically so they cannot set `g_boot_snapshot_sent`. This case
proves that held.

---

### P6 — decommission does NOT pulse
**Steps** On a settled hub, `decommission` a single BLE sensor (target `ble_leak_sensor`).
**Pass** **No** `PROV pulse armed` line. The old behaviour is intact: one command snapshot plus the
first-contact refresh. Then re-provision it back.

---

### P7 — message budget
**Setup** Count from the P1 capture.
**Pass**
- Total IoT Hub messages between `PROV pulse armed` and `PROV pulse window closed` is **≤ 40**;
  expected **15–25**
- `PROV pulse capped` absent
- `N` in the close line ≥ the number of messages actually seen (requests ≥ publishes, because
  `snap_request` coalesces). If N is much larger than the message count, that is the clamp working.

---

### P8 — offline during the window
**Steps**
1. Provision. Wait ~60 s (confirm pulses are running).
2. Kill Wi-Fi (unplug the AP, or move the hub out of range) for ~90 s.
3. Restore Wi-Fi.
4. Keep watching to ~360 s from arming.

**Pass**
- While offline: no snapshots published (nothing to publish to), no crash, no watchdog, no reboot
- On reconnect: **one** snapshot, not a backlog burst of the pulses that were missed
- The window still closes at **~300 s of wall clock** from arming — it is not extended by the outage
- Heap does not fall monotonically across the outage (`free heap` lines)

---

## Part B — boot-sync decoupling

### P9 — quiet boot (the reproduced defect)
**Setup** Fully provisioned, fully healthy hub, all 4 sensors powered. **At least one sensor placed
far enough away that its first contact is late** — the 2026-09-17 capture had one at 265.6 s, which
is what this case must reproduce and then tolerate.
**Steps** Power-cycle the hub. Capture from the reset banner. Watch for **10 minutes**.

**Pass**
- LED: OFF/dark briefly → **WHITE** (`rating=syncing color=WHITE`) → **GREEN** when the last sensor
  is heard
- **ZERO** `color=RED` transitions
- **ZERO** snapshots with `system_health.rating: "critical"` and a `"N sensor(s) offline"` reason
- **ZERO** `device_offline` health events for a sensor that is in fact healthy
- `Boot sync: timeout (180 s) — snapshot gate open; unheard devices still excused for 600 s` **may**
  appear — that is now benign and expected when a sensor is late
- `Roll-up grace expired (600 s)` must **NOT** appear (every sensor was heard before 600 s)
- While WHITE, the snapshot reason reads `Syncing - waiting for N device(s)`

**Record for me** the UART timestamp of each sensor's **first** `eleak <MAC>` line. That is the
number that decides whether this boot actually exercised the case: if the latest is < 180 s, the
old code would have passed too and the case is inconclusive — move a sensor further away and re-run.

---

### P10 — a genuinely absent sensor is still flagged (proves P9 masks nothing)
**Setup** Same hub, but **remove one sensor's battery** before powering the hub.
**Steps** Power-cycle. Watch for **12 minutes**.

**Pass**
- LED **WHITE** for the first ~600 s — *not* RED at 180 s, and *not* GREEN
- `Roll-up grace expired (600 s) — unheard devices now count` at ~600 s
- LED → **RED** within ~1 s of that line (the edge recalc, not the next 30 s tick)
- A snapshot with `rating: "critical"` and `"1 sensor offline"`
- Exactly **one** `device_offline` health event for that MAC

**Why** This is the cost of P9, made explicit and bounded. If the LED goes RED at 180 s, Part B did
not take effect. If it never goes RED, the grace is latched — a real bug.

---

### P11 — a leak beats the grace
**Setup** Provisioned hub. Power-cycle and **immediately** (within the first 60 s, while the LED is
still WHITE) wet one sensor.
**Pass**
- LED → **RED** promptly (within a few seconds of the `eleak … leak=1` line) — the wet sensor is
  **not** excluded by the syncing grace
- `system_health.rating: "critical"`, reason names the leak and its location
- The valve closes (`auto_close`) if `auto_close_enabled`
- LED does **not** revert to WHITE while the sensor is still wet

**Why** The `leaking` exemption in `rollup_unheard_locked()` is the one part of the exclusion logic
that must never be optimised away. This case is its guard.

---

### P13 — pulse works from a FACTORY-FRESH hub (council finding C2)
**Why this is its own case** P1 starts from an already-provisioned hub, and the bug C2 found was
invisible from there: `iothub_task` latches `provisioned` before its blocking select, so the wake
carrying the *first* `provision` resumed with a stale `false` and wiped the pulse arm. On a
factory-fresh hub that race window is up to 30 s wide — effectively always hit. **P1 passing does not
imply P13 passes.**

**Setup** Hub **decommissioned** (`decommission` target `all`) or factory-fresh. Confirm the LED is
WHITE "unprovisioned" and the boot log says `Device is now UNPROVISIONED`.
**Steps**
1. Start both captures.
2. Let it sit unprovisioned for **at least 90 s** — this is deliberate: it lets the heartbeat
   deadline go past-due, which is also the precondition for finding C3.
3. Send `provision`.
4. Watch for 6 minutes.

**Pass**
- `PROV pulse armed: every 30 s for 300 s, plus on every sensor packet` appears — **this is the
  assertion**; before the fix it was absent
- Pulses then behave as P1: ~8–10 `prov_pulse` lines, `PROV pulse window closed (N …)` at ~300 s
- **No** long silence between the provision and the first pulse (C3 would have shown as the
  `prov_pulse` lines appearing in the UART with no corresponding IoT Hub snapshots)

**Fail signature** `PROV pulse armed` missing entirely → C2 fix not effective. `PROV pulse` lines
present in UART but no matching snapshots in IoT Hub for the first ~150 s → C3 fix not effective.

---

### P14 — a missing valve is NOT excused for 10 minutes (council finding C1)
**Why** The 600 s roll-up grace is calibrated for ~100 s-cadence advertising sensors. C1 found it was
also being applied to the valve, which sits on a continuous link and has its own 180 s offline
deadline — so a hub that could not reach its valve showed WHITE "syncing" and published
`rating:"excellent"` for 10 minutes. The valve is the one device the entire leak response depends on.

**Setup** Provisioned hub, all 4 sensors present and healthy, **valve powered OFF** (or moved out of
BLE range) before the hub boots.
**Steps** Power-cycle the hub. Capture from the reset banner. Watch for 6 minutes.

**Pass**
- LED goes WHITE, then **RED** at roughly the boot-sync window edge — **~180 s**, *not* 600 s
- A snapshot reports `rating: "critical"` naming the valve (offline / not connected)
- `Roll-up grace expired (600 s)` is **irrelevant** here — the valve must escalate well before it,
  whether or not that line appears for the sensors
- The LED does **not** sit WHITE or GREEN for 10 minutes

**Fail signature** LED stays WHITE past ~200 s, or snapshots keep reporting `excellent` /
`Syncing - waiting for 1 device`, → the valve exemption is not in effect.

**Then** power the valve back on and confirm the hub links and the LED returns to GREEN.

---

### P12 — version
**Pass** Boot log shows `App version: 2.1.3`; telemetry `gateway.fw: "2.1.3"`; twin reported
`fw_version: "2.1.3"`.

---

## Part C — carried over from 2.1.2 (still open)

### T12b — provision while a leak is active (needs a retry)
The 2026-09-17 attempt did not exercise this: the provision landed at 324.6 s, in the gap between
the sensor drying at 322.5 s and re-wetting at 326.0 s, so there was no active leak to carry. The
absence of `Reload: carried active leak` was therefore **correct**, not a failure.

**Steps**
1. Wet a sensor. Wait for the LED to go **RED** and confirm `leak_detected` in IoT Hub.
2. **While the LED is still RED and the sensor is still wet**, send `provision`.
3. Hold the sensor wet for a further 60 s.

**Pass**
- `W HEALTH_ENGINE: Reload: carried active leak for <MAC>` right after
  `Device table loaded: N device(s)`
- LED stays **RED** for the whole period — **no** flicker to WHITE or YELLOW
- Snapshots keep reporting `critical` with the leak named; never `Syncing - waiting for…` as the
  sole reason
- The pulse also arms (this is a `provision`), so expect `prov_pulse` lines alongside

### T7b isolating variant — deferred by your decision
Valve flood probe already wet at link-up, with `active_leak_count == 0` so Priority 1 cannot mask
the path. Still open; run when convenient.

---

## Results

Run 1 = capture of 2026-09-18 (2 boots: boot 1 ended in `decommission all` + reboot; boot 2 provisioned
at t=285.8 s). Sensor first contacts in boot 2, relative to the provision: **+17 / +59 / +170 / +473 s**.

| # | Case | Result | Notes |
|---|---|---|---|
| P13 | **Pulse from factory-fresh** (C2) | **PASS** | boot 2 provisioned from UNPROVISIONED; `PROV pulse armed` at 285817. Council fix C2 confirmed on the exact path it broke |
| P1 | Pulse runs (30 s × 5 min) | **PASS** (run 2) | run 2: armed 50673, `PROV pulse window closed (17 snapshot(s) requested)` at 351673 = **301.0 s**. 8 `prov_pulse` + 5 `prov_pkt` published (17 requested → 13 published, the rest coalesced). One pulse correctly absorbed by the `boot` snapshot at 202183 |
| P2 | Packet-triggered refresh | **PASS** | 4 `prov_pkt`, none with an `eleak` line → all from delta-gated repeat packets. Criterion corrected (see P2) |
| P3 | Refreshed data is new | **FAIL (run 2) → fixed → PASS (run 3)** | run 2: sensor `…2B:A5` moved mid-window (rssi −53 → −70) but **five consecutive pulse snapshots kept reporting −53**; the true value appeared only at the next heartbeat, 300 s later. Root cause: snapshot `rssi`/`battery` read from the delta-gated `s_ble_cache`, and rssi is not even in the delta test. Now sourced from the health table. **Run 3 confirms the fix: 18 of 31 snapshot rssi values appear in NO delta-logged `eleak` line**, so they can only have come from an unlogged burst via the health table — impossible before the fix |
| P4 | Time-based, not seen-based | **PASS** (run 2) | run 2: `conn=4/4` at up=249 (which zeroes the pre-existing commission window), and pulses continued at up=263/293/307/323/349 — 100 s past the collapse. Direct confirmation that the separate pulse window (D2) holds |
| P5 | Complete snapshot survives | **PASS** | exactly one `fast` (the boot snapshot) + 2 `commission` incremental refreshes; no duplicate, none replaced by pulses |
| P6 | Decommission does not pulse | not run | |
| P7 | Message budget | **PASS** | 17 messages (15 snapshots) in the window; cap 40; no `PROV pulse capped` |
| P8 | Offline during window | not run | |
| P9 | Quiet boot | **PASS (strong)** | boot 2: sensors at +17/+59/+170/+473 s — two past the 150 s window, one past 180 s. **Zero RED**, zero `N sensor offline`; WHITE → GREEN at 758587, 150 ms after the last sensor was heard. Pre-2.1.3 this would have gone RED at 435587 with "2 sensors offline" |
| P10 | Absent sensor still flagged | **PASS** | boot 1: sensor `…2B:A5` never heard → WHITE until `Roll-up grace expired` at 602633 → RED, snapshot `critical / "1 sensor offline"`. Proves P9 masks nothing |
| P11 | Leak beats the grace | not run | |
| P14 | **Missing valve not excused 10 min** (C1) | not run | valve connected throughout |
| P12 | Version | **PASS** | `App version: 2.1.3`; `gateway.fw:"2.1.3"` in all 56 messages |
| T12b | Provision during a live leak | not run | retry from 2.1.2 |
| T7b | Valve wet at link-up (isolating) | not run | deferred |

### Regression guards also confirmed in run 1
- **D1 storm: zero** `cause:reconnect`; `[SETUP] Step` never exceeded 9 (max is 10).
- Wire schema: every `data.reason` inside the six-value enum — the pulse adds no new value, as designed.
- No incoherent `system_health` pairs; `critical | Leak detected: Ensuite, Leak interlock latched` and
  `warning | Leak interlock latched` both render correctly. D3/D4 stay fixed.
- Both C2D commands were envelopes with ids → both got `cmd_ack status:"ok"`.
- Heap plateaued at free≈33500, `min_ever` 19848 over ~43 min. No downward trend.
- The boot-1 reset was the intentional `decommission all` reboot (`esp_restart_noos`); the
  mbedtls/mqtt errors around it are TLS teardown during the 3 s flush wait, not a fault.

### Run 2 (2026-09-18, Run A: P4 + P1 expiry + P3) — the two run-1 log fixes verified
- `Boot sync: timeout (150 s) — snapshot gate open; unheard devices still excused for a further
  **449 s**` at t=200633 — correct remaining time (was reporting the 600 s constant).
- `Roll-up grace expired` **absent** from the capture. Correct: the deadline passed at t≈650633 with
  every device heard, so it now logs at DEBUG instead of shouting a false WARNING.
- Four capture gaps remain (80 s at 410813, plus three smaller) but **all fall after the pulse window
  closed**, so none affect run 2's assertions. Scrollback still needs raising for the longer cases.

### Run 3 (2026-09-18, ~32 min idle soak) — P3 re-test + steady-state soak
Single boot, **no capture gaps** (scrollback fix worked). All four sensors heard within 77 s →
`Boot sync: all devices seen`, LED WHITE → GREEN at 77093 and **unchanged for the next 30 minutes**.

- **P3 PASS** — provenance proof above.
- **No-false-offline PASS** (the old 2.1.2 T9/P12 criterion): **zero events of any kind** in 32 min —
  no `device_offline`, no leak events. Max `last_seen_age_s` across all sensors and all snapshots
  was **172 s** against the 600 s offline threshold (3.5× margin) and under the old <200 s target.
  In 2.1.1 this reached 320 s+.
- **Heap: zero drift.** `free=33816`, `min_ever=21368`, `largest_blk=20480` — all three *identical*
  at uptime 200 / 400 / … / 1800 s. The two `Heap dropped` warnings are WiFi/TLS/MQTT bring-up at
  10.8 s and 50.9 s. No leak.
- **Volume:** 11 messages in 32 min — 1 lifecycle, 1 `fast`, 3 `commission`, 6 `heartbeat` on an exact
  300 s cadence. `Roll-up grace expired` correctly absent (nothing unheard → DEBUG).
- Startup warnings are all known-benign: the `gpio_install_isr_service already installed` **E** line is
  emitted by the IDF driver itself before returning the error we then tolerate (documented at
  `reset_button.c:223`); `Failed to start ext scan: 15, will retry` is the NimBLE single-scan
  constraint, retried successfully.
- **Analysis note:** the IoT Hub capture contains **two boot epochs** — the first 3 messages
  (`ts − uptime = 1789708381`) are the tail of the *previous* boot. Taken together they look like a
  duplicated lifecycle + duplicated `fast`, i.e. a one-shot violation. Separating by epoch shows
  11 messages on the soak boot matching the UART exactly. **Always group IoT Hub messages by
  `ts − uptime_s` before drawing conclusions** — this trap has now produced a false reading twice.

### Found by reading run 2 (fixed, needs a re-flash)
- **Snapshot `rssi`/`battery` were stale during the pulse** — the defect P3 was designed to catch, and
  it defeated the purpose of the whole feature. Both fields came from `s_ble_cache` /
  `s_lora_cache`, which are fed only through the delta-gated queue; and the scanner's delta test
  covers `leak | battery | fw_version` but **not rssi**, so an RSSI-only change could reach the cloud
  *only* via the 5-minute telemetry heartbeat. Meanwhile the health engine had the fresh value all
  along (its check-in is above that gate). Both snapshot branches now read `battery`, `rssi` and
  `leak_state` from the health table; `fw_version` (BLE) and `snr` (LoRa) stay cache-sourced.
  No schema change — same fields, same types, just live values.
- `snr` (LoRa only) is still cache-sourced and therefore still delta-stale. Flagged, not fixed:
  plumbing snr through the health event is wider than this fix warrants and rssi is the placement
  figure.

### Found by reading run 1 (fixed, needs a re-flash)
- `Roll-up grace expired … unheard devices now count` printed at **W** level with **zero** unheard
  devices (boot 2, t=885587, all four sensors healthy). Now only warns when something is genuinely
  unheard, and states the count.
- `Boot sync: timeout` claimed "still excused for 600 s" when only 450 s remained (the deadline is
  measured from the same origin). Now reports the **remaining** time.
- P2's pass criterion was unverifiable as written — see the corrected case.

### Suggested order

**Start with P13.** It covers the factory-fresh path that two of the three council findings live on,
and if it fails the rest of Part A is moot. It also leaves the hub provisioned, ready for the others.

P13 → P1+P2+P3 (one capture) → P4 → P6 → P14 → P9 → P11 → T12b → P10 (slowest, 12 min) → P8 →
P5+P7+P12 read out of any of the above captures.
