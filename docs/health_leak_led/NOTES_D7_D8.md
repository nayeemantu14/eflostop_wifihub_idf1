# D7 / D8 — investigate-only notes (no 2.1.2 code change)

Both were observed in the 2.1.1 field captures and deliberately left alone in 2.1.2. Recorded so the
data is not lost and so a future change starts from measurements rather than a re-investigation.

---

## D7 — heap headroom is thin

### Observation

From the UART log (`MONITOR` line, 10 s cadence), with WiFi + TLS + MQTT + NimBLE (valve link and leak
scanner) all up:

```
I (882)    MONITOR: heap: free=128688 min_ever=128688 largest_blk=86016 uptime=0s
I (20892)  MONITOR: heap: free=57956  min_ever=54616  largest_blk=31744 uptime=20s
W (20902)  MONITOR: Heap dropped 75240 bytes since last check
I (30912)  MONITOR: heap: free=34156  min_ever=24516  largest_blk=17408 uptime=30s
W (30912)  MONITOR: Heap dropped 23800 bytes since last check
I (690912) MONITOR: heap: free=34172  min_ever=14668  largest_blk=19456 uptime=690s
```

Steady state settles at **~34 KB free**, **largest free block ~17–19 KB**, and an all-time low of
**14.6 KB**. The two big steps are the BLE stack coming up (~75 KB at 20 s) and the TLS handshake plus
MQTT session (~24 KB at 30 s).

### Assessment

Not failing, and no allocation failure appears anywhere in the capture. But a 17 KB largest block is
thin for a TLS renegotiation or an OTA download, and `min_ever` dropping to 14.6 KB after boot means
something transiently takes another ~20 KB during normal running.

2.1.2 should *help* slightly: the storm was allocating and freeing a cJSON document plus a printed
string per cycle at 1–2 Hz, which is exactly the churn that fragments a heap and depresses the largest
free block.

### What to do next (not in 2.1.2)

1. Re-measure on 2.1.2 and compare `min_ever` and `largest_blk` against the numbers above — the storm
   fix alone may account for much of the gap.
2. Identify the transient consumer between 30 s and 690 s that pulls `min_ever` to 14.6 KB. Candidates:
   the snapshot builder (a full snapshot with 4 sensors is a ~1.5 KB JSON string plus the cJSON tree —
   comfortably the largest single allocation in normal operation), and the TLS record buffers.
3. Before enabling Azure OTA (see `dps_ota_plan.md`), establish the peak requirement — OTA plus TLS on
   top of this baseline is the realistic worst case and it is not currently characterised.
4. Consider building the snapshot JSON incrementally rather than materialising the whole cJSON tree, if
   step 2 confirms it as the peak.

**Do not** raise task stacks or add buffers speculatively — measure first.

---

## D8 — sub-second leak flap

### Observation

UART, during the first leak test:

```
I (736592) BLE_LEAK: eleak 00:80:E1:2A:29:FC — leak=0 batt=100% rssi=-38 fw=1.1.0
I (736882) BLE_LEAK: eleak 00:80:E1:2A:29:FC — leak=1 batt=100% rssi=-42 fw=1.1.0
```

**290 ms apart.** The hub faithfully published `leak_cleared`, then `leak_detected`, then a second
`auto_close`, and `track_leak_source()` ran the full all-clear → re-latch cycle in between (visible as
`All leaks resolved — pending auto-close cancelled` / `All sensors clear — auto-clear timer started
(30s)` immediately followed by a fresh `AUTO-CLOSE + RMLEAK triggered`).

### Assessment

This is almost certainly a **sensor-side** artifact, not a hub bug: a marginal water bridge on the
probe during a manual wet test, reported by the STM32WBA's 50 ms debounce. The hub has **no leak
debounce of its own** — by design, since latency to shut off water is the thing that matters most.

The cost of the flap is not safety (the valve was already closed and RMLEAK re-asserted) but noise:
three extra D2C events and a needless all-clear/re-latch cycle, including an NVS write on 2.1.1. The
2.1.2 write-skip cache already removes the NVS component.

### Why no fix in 2.1.2

Any hub-side debounce trades shut-off latency for tidiness, and shut-off latency is the wrong thing to
trade. It also risks masking genuine intermittent leaks (a dripping joint that wets and dries).

### Options if it proves to be a real field nuisance

1. **Asymmetric debounce (preferred if anything).** Act on `leak=1` immediately, as now; require a dry
   report to persist for N seconds (e.g. 10 s) before treating the source as clear. Protects the
   all-clear/re-latch cycle and the 30 s auto-clear timer without adding a microsecond to shut-off
   latency. Would live in `track_leak_source()` in `rules_engine.c`.
2. **Suppress only the telemetry**, not the state machine: coalesce a `leak_cleared` immediately
   followed by a `leak_detected` for the same source into no event at all.
3. **Fix it at the sensor**: lengthen the WBA dry-confirmation debounce. Cheapest if the flap only ever
   appears during manual finger tests and not with real standing water.

### Before choosing

Quantify it. Count flaps (a `leak_cleared` followed by a `leak_detected` for the same `sensor_id`
within 5 s) over a real soak with actual standing water, not finger tests. If the rate is
indistinguishable from zero outside manual testing, do nothing — option 3 by default.
