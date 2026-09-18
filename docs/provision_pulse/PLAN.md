# FW 2.1.3 — post-provision snapshot pulse + boot-sync window decoupling

Status: implemented, awaiting bench validation (`TEST_PLAN.md`).
Branch: `feat/valve-link-detection`. Version: `PROJECT_VER` 2.1.2 → **2.1.3** (`CMakeLists.txt:12`).

---

## 1. Problems

### A — commissioning feedback goes quiet exactly when an installer is watching

After a C2D `provision` the hub published:

1. one snapshot when the health sync window closed (`app_iothub.c`, boot/commission arming), then
2. a refresh **only on a device's very first contact** — gated on `seen > g_commission_pub_seen`.

Two consequences:

- **2nd and later packets from an already-heard sensor produced nothing.** A dry STM32WBA leak
  sensor beacons about every 100 s and each burst carries fresh battery + RSSI — the two numbers an
  installer uses to judge placement. None of it reached the cloud until the 5-minute heartbeat.
- **The grace window closed early by design.** `if (seen >= total) g_commission_until_ms = 0;` —
  once every device had been heard once, refreshes stopped, even seconds into commissioning.

Behind both sat a plumbing gap: a repeat packet from an unchanged sensor **never reaches
`iothub_task` at all**. The scanner's telemetry delta gate (`app_ble_leak.c`, `!data_changed &&
!heartbeat_due → return`) drops it before the queue send. Only the health engine sees every burst,
because 2.1.2 hoisted `health_post_ble_leak_checkin()` above that gate.

### B — the 180 s boot-sync window was undersized (reproduced on the bench)

Capture of 2026-09-17, four healthy sensors, first-contact times **56.0 / 89.8 / 199.0 / 265.6 s**:

```
  4.6s  FLEET_LED: rating=syncing  color=WHITE
184.6s  W HEALTH_ENGINE: Boot sync: timeout (180 s)
184.6s  FLEET_LED: rating=critical color=RED          <-- 81 s of RED
up=198  snapshot: critical | "1 sensor offline"       <-- published
265.6s  FLEET_LED: rating=excellent color=GREEN
```

Root cause: **one constant served two conflicting purposes.** `s_boot_sync_timeout_ms` gated both

- the boot **snapshot** — wants to be *short*, so the app gets data promptly; and
- the roll-up **exclusion** of never-heard devices — wants to be *long*, so we don't cry wolf.

Raising it to 240 s (the earlier proposal) would also have failed this boot.

---

## 2. Design

### Part A — the pulse

For `PROV_PULSE_WINDOW_MS` (5 min) after a `provision`, request a snapshot:

- every `PROV_PULSE_PERIOD_MS` (30 s) — the periodic arm; and
- whenever the health engine's check-in counter moves — the packet arm.

State is `iothub_task`-owned and lock-free, matching the existing scheduler discipline.
`arm_commission_snapshot()` gained a `bool pulse`; it runs on the **esp-mqtt event task** so it sets
only the `g_prov_pulse_arm` flag, and `iothub_task` computes every deadline. Same split as
`g_cmd_snap_pending`.

`pulse=true` for `provision` only. The three per-device `decommission` targets pass `false` — a
one-shot device-list edit is already covered by the first-contact refresh plus the generic
on-success command snapshot, and does not warrant ten extra snapshots.

#### Why both arms use `snap_request(SNAP_EVENT, SNAP_TIER_LOW, …)`

Every property is load-bearing:

| Property | Consequence |
|---|---|
| `SNAP_EVENT` is rate-clamped by `SNAP_MIN_INTERVAL_MS` (5 s, ≤12/min) | `SNAP_COMMISSION`/`BOOT`/`FAST` deliberately bypass the clamp; using one would let a multi-sensor burst publish back-to-back |
| `SNAP_EVENT` passes the incomplete-window `gate_ok` | pulses publish **during** the open sync window — the progressive refinement that is the point; the same licence `SNAP_FAST` already takes |
| `SNAP_EVENT` does not set `g_boot_snapshot_sent`, does not touch `g_commission_until_ms` | the guaranteed **complete** boot/commission snapshot still fires when the window closes; no existing guarantee weakened |
| `data.reason` stays `"event"` | **no new schema enum value** — `snapshot.schema.json` unchanged, nothing for a cloud consumer to learn. The distinction lives in the UART trace |
| `SNAP_TIER_LOW` (2 s coalescing) | four sensors bursting together collapse to one snapshot; `TIER_HIGH`'s 300 ms window would emit up to four |

A **separate window variable**, not a reuse of `g_commission_until_ms`, precisely because that one
is zeroed at `seen >= total` — sharing it would end the pulse early and fight the existing
incremental-refresh design.

#### The packet signal

```c
static volatile uint32_t s_checkin_seq;      /* health_engine.c */
uint32_t health_get_checkin_seq(void);
```

Bumped in `health_engine_task()`'s switch on `HEALTH_EVT_LORA_CHECKIN` and
`HEALTH_EVT_BLE_LEAK_CHECKIN` — **not** on valve events, which already couple their own snapshot via
the `valve_linked` delta gate. Single writer (the health task), single reader (`iothub_task`),
32-bit aligned → lock-free. Wrap-safe: compared with `!=`, never `<`.

The health engine is the right home rather than a second signal out of `app_ble_leak.c`: it is the
one module that sees every burst from **both** BLE and LoRa, so one counter covers both sources.
Bursts are already decimated per sensor by `HEALTH_CHECKIN_MIN_MS` (5 s) in the scanner, so a
multi-advertisement burst bumps the counter about once.

#### Supporting changes

- `commission_pending` ORs in `g_prov_pulse_until_ms != 0`. It only chooses the 2 s vs 30 s select
  base, but the packet arm is **polled**, so without it an advertisement could wait up to 30 s for
  its snapshot — and "within a couple of seconds" is the feature's whole user-visible promise.
- The `!provisioned` branch clears the pulse state. The firing block sits *below* that `continue`,
  so a decommission mid-window would otherwise strand the window and pin the loop to a 2 s poll
  forever.

#### A gated heartbeat CAN starve the pulse — and it is a pre-existing scheduler defect (fixed)

I first analysed this and concluded it was unreachable. **That conclusion was wrong**; the council
found the path. Recorded in full because the reasoning error is instructive.

`snap_request()` is **pull-in-only** (`want < s_snap_due_ms`). While the sync window is open, a due
`SNAP_HEARTBEAT` fails `gate_ok` and the flush block re-arms the deadline to `flush_now + 2000` —
*every pass*. That makes a suppressed reason a moving baseline sitting ~2 s ahead forever, so a
`SNAP_TIER_LOW` request (`want = now + SNAP_LOW_WINDOW_MS`, also **2000**) can never be strictly
less, and even `TIER_HIGH` loses on the iterations right after a deferral. Requests are discarded
**in silence**.

My error was assuming the provision's own command snapshot always publishes and rearms the
heartbeat, pushing it ≥60 s out. It does not when the deadline is **already past-due** at the moment
the window opens — and `snap_request()` cannot move a past-due deadline, so the request is a no-op
and the reason *stays* `HEARTBEAT`. That state is routine: the `!provisioned` branch re-arms the
heartbeat from a stale `s_snap_last_pub_ms` on every iteration, so any hub that has been
unprovisioned or offline for longer than one interval enters a `provision` with a past-due deadline.

**This is pre-existing, not new in 2.1.3.** The same mechanism delays a `leak_detected` *snapshot*
by up to the whole boot/commission window. Safety is unaffected — the leak event message is a direct
publish and the valve close runs in Phase 2 — but the snapshot the app renders could lag a leak by
minutes. The 30 s pulse is simply the first feature to request at `TIER_LOW` on that path and so the
first to make it visible.

**Fix:** a reason the gate is *currently suppressing* no longer outranks an `EVENT`, which that gate
always lets through. The suppression rule is now a single helper, `snap_window_suppressing()`, used
by both the flush block's `gate_ok` and the new `yields` term in `snap_request()` — they were
duplicated inline expressions, which is exactly how the scheduler came to disagree with itself.

#### Budget

`PROV_PULSE_MAX_SNAPS` (40) is a safety valve against a pathological case — a sensor flapping every
5 s for 5 minutes — and **logs a warning when it bites**; no silent truncation. The close log always
states the count. Note the counter counts **requests**, not publishes: `snap_request()` coalesces,
so the published total is lower. Realistic figure with four sensors: 10 periodic + ~12 packet,
coalescing to roughly **15–25 messages**, once per commissioning event.

### Part B — two windows, two meanings

`s_boot_sync_timeout_ms` (180 s) keeps its **snapshot-gate** meaning.
`health_is_boot_sync_complete()` is unchanged, and so is every use of it in `app_iothub.c`.

A second, longer threshold governs the **roll-up exclusion**:

```c
#define HEALTH_ROLLUP_UNHEARD_MS  HEALTH_BLE_LEAK_TIMEOUT_MS   /* 600 s */
```

Aligned with the sensor timeout rather than picked: 600 s is already the deadline at which this
engine will declare a *previously-seen* sensor offline, so it is the principled point at which
"haven't heard yet" becomes "offline". A genuinely absent device is still escalated — at the same
deadline steady-state uses, not a shorter guess.

`check_boot_sync_locked()` was restructured to evaluate **both** deadlines. The
`if (s_boot_sync_done) return;` early return is gone, because deadline 2 is longer than deadline 1
and so must stay reachable after the first closes. Each flag keeps its own one-shot guard, and a
recalc fires on **either** edge — deadline 2 needs that for exactly the reason deadline 1 did:
nothing else recalcs when it passes, so without it the roll-up would keep excluding a genuinely
absent device (reporting EXCELLENT/GREEN) until the next 30 s tick.

Both flags are reset in `health_engine_reload_devices()`, so a `provision` restarts both — correct,
since freshly commissioned devices genuinely have not been heard yet.

#### `health_is_rollup_syncing()` — so the LED and the reason string stay honest

With Part B alone the LED would read WHITE → **GREEN at 180 s** (rating excellent, because the
unheard device is excluded) → RED at 600 s. GREEN while a device is still unheard is a mild
false-green and the wrong middle state. So the exclusion predicate is exposed:

```c
static bool rollup_unheard_locked(const health_device_t *dev);   /* shared */
bool health_is_rollup_syncing(void);                             /* public */
```

`recalc_system_rating()` (which *skips* these devices) and `health_is_rollup_syncing()` (which
*reports* them) now share one predicate, so the rating, the fleet LED and the telemetry reason
string cannot disagree about who is still syncing. The predicate takes no time argument: both
deadlines are latched into flags, so it is a pure function of state and cannot straddle a deadline
mid-pass.

Repointed consumers:

- `fleet_led.c` — the `"syncing" → WHITE` arm. The old `seen < total &&` half is removed as
  **redundant, not dropped**: the predicate is true only when some in-use device has `!ever_seen`,
  which is exactly `seen < total`. One predicate instead of two that could drift apart.
  `health_get_sync_counts(NULL, &total)` — the seen-count is no longer needed.
- `telemetry_v2.c` — `build_system_health_reason()`'s `sync_done` parameter became
  `rollup_syncing`; the call site still samples it **before** the rating.

Three consumers, three different correct answers, one predicate each:

| Consumer | Question | Predicate | Clock |
|---|---|---|---|
| snapshot gate (`app_iothub.c`) | may we publish a "complete" snapshot? | `health_is_boot_sync_complete()` | 180 s |
| fleet LED | is the rating complete yet? | `health_is_rollup_syncing()` | 600 s |
| telemetry reason | same | `health_is_rollup_syncing()` | 600 s |

#### Behaviour

- Reproduced capture: WHITE until 265.6 s, then GREEN. **No RED excursion, no published
  `1 sensor offline`.**
- Genuinely dead sensor: WHITE until 600 s, then RED. The T8/T11 deadline moves 180 s → 600 s.
- A **wet** device is never excluded, whatever `ever_seen` says — the `leaking` exemption is
  preserved verbatim from 2.1.2 and is load-bearing.

---

## 3. Files changed

| File | Change |
|---|---|
| `main/health_engine/health_engine.h` | `HEALTH_ROLLUP_UNHEARD_MS`; `health_is_rollup_syncing()`, `health_get_checkin_seq()`; doc note narrowing `health_is_boot_sync_complete()` to the snapshot gate |
| `main/health_engine/health_engine.c` | `s_rollup_grace_done`, `s_checkin_seq`; `rollup_unheard_locked()`; `check_boot_sync_locked()` two-deadline restructure; `recalc_system_rating()` uses the shared predicate; two new getters; reload resets both flags |
| `main/iothub/app_iothub.c` | pulse constants + 5 statics; `arm_commission_snapshot(bool pulse)` + 4 call sites; arm-consume block; pulse/packet firing block; `commission_pending` OR; unprovisioned reset |
| `main/rgb/fleet_led.c` | syncing arm reads `health_is_rollup_syncing()`; `seen` removed |
| `main/telemetry/telemetry_v2.c` | reason builder takes `rollup_syncing` |
| `CMakeLists.txt` | `PROJECT_VER "2.1.3"` |

**No wire-schema change. No new `data.reason` value. No C2D contract change.**

---

## 4. Decision record

| # | Decision | Why |
|---|---|---|
| D1 | Pulse arms use `SNAP_EVENT`, not a new `snap_reason_t` | keeps the 5 s rate clamp, passes the incomplete-window gate, leaves `g_boot_snapshot_sent` alone, and needs no `data.reason` schema change |
| D2 | Separate pulse window, not `g_commission_until_ms` | that one is zeroed at `seen >= total`, which would end the pulse early |
| D3 | Packet signal lives in the health engine | the only module that sees every burst from both BLE **and** LoRa (user's choice: both sources) |
| D4 | `pulse=true` for `provision` only | a per-device decommission does not warrant ten extra snapshots |
| D5 | `HEALTH_ROLLUP_UNHEARD_MS` = `HEALTH_BLE_LEAK_TIMEOUT_MS` | principled rather than tuned: it is already the point at which a seen sensor is called offline. Accepted cost: a dead sensor takes 10 min to go RED instead of 3 |
| D6 | New predicate rather than reusing the snapshot gate for the LED | avoids a GREEN window between 180 s and 600 s while a device is still unheard |
| D7 | `health_is_rollup_syncing()` fails **closed**; `health_is_boot_sync_complete()` fails **open** | both avoid latching the caller into "syncing"; on a mutex hiccup the LED falls through to its rating-based answer, never holding WHITE over a real fault |
| D8 | `PROV_PULSE_MAX_SNAPS` counts requests and logs when it bites | bounds work regardless of coalescing; a capped window must not read as a completed one |
| D9 | Version bumped despite 2.1.2 not shipping | bench captures are attributed by `gateway.fw`; two builds must not share a version string |

## 5. Adversarial review + agent council

22 agents: 5 independent reviewers (one per dimension) → 2 adversarial refutation lenses per finding
(*does-it-actually-happen*, *is-the-code-really-like-that*) → dedup/synthesis. A finding survived
only if **neither** lens could refute it.

**8 raised → 4 survived → 3 confirmed after synthesis.** All 3 verified against the source by hand
before fixing; all 3 were real, and **two were introduced by this change**.

| # | Sev | Finding | Fix |
|---|---|---|---|
| C1 | high | **The 600 s grace was applied to the valve.** `rollup_unheard_locked()` had no device-type branch, so a valve that never connects (`ever_seen` is set only on CONNECTED) was excluded from the roll-up for 600 s instead of the 180 s/150 s window edge it used before 2.1.3. Result: fleet LED WHITE "syncing" and published `rating:"excellent"` for up to 10 minutes on a hub with **no way to shut the water off** — and it re-arms on every provision, i.e. during commissioning. My own justification for the constant (sensor burst cadence, `HEALTH_BLE_LEAK_TIMEOUT_MS`) never applied to the valve: it is on a continuous link and its own offline deadline is `HEALTH_VALVE_DISC_TIMEOUT_MS` = 180 s. | `if (dev->dev_type == HEALTH_DEV_VALVE) return false;` — the valve's exclusion ends at the window edge, as before. Restores the correct asymmetry: never-connected is no longer treated more leniently than connected-then-lost. |
| C2 | high | **Stale `provisioned` sample destroyed the pulse arm — Part A never ran on first commissioning.** `provisioned` is latched *before* the blocking `xQueueSelectFromSet()`; a `provision` lands on the esp-mqtt task, sets the flags, then wakes that select. The task resumes holding `provisioned == false`, falls into the `!provisioned` branch and wipes the flags the provision just set. The pre-existing resets there survive because they self-heal via `SNAP_FAST`; `g_prov_pulse_arm` has exactly one writer, so clearing it killed the feature outright, with no log line. On a factory-fresh hub the race window is up to 30 s — i.e. always hit. **Would have passed every bench run starting from an already-provisioned hub.** | Re-sample at the Phase 3 gate: `if (!provisioned && !(provisioned = provisioning_is_provisioned()))`. Fixes the root cause rather than one symptom, and repairs the same staleness hitting `g_boot_snapshot_sent` / `g_fast_arm_ms` / `g_commission_until_ms`. |
| C3 | med | **Gate-deferred heartbeat is an unbeatable baseline** — see the analysed-interaction section above. **Pre-existing**; also delays leak snapshots. | `snap_window_suppressing()` helper + a `yields` term letting `EVENT` take a deadline from a currently-suppressed reason. One rule, two call sites. |

### Also fixed: one finding the council refuted 1/2

The cap warning could never fire. Both pulse arms can increment in the **same** iteration, so a count
of `MAX-1` becomes `MAX+1` in one pass and stepped straight over the bare `== PROV_PULSE_MAX_SNAPS`
test — the cap would then suppress the rest of the window in exactly the silence that branch exists
to prevent. Trivially verifiable by inspection, so fixed despite the refutation: both equality values
are now matched and the count parks on a sentinel above them.

### Refuted (no action)
- `PROV_PULSE_MAX_SNAPS` trips during a normal 16-sensor commission — the 5 s min-interval clamp bounds request volume well below 40.
- Grace expiry flips EXCELLENT→CRITICAL with no snapshot/alert coupling — `apply_rating()`/`maybe_enqueue_alert()` already cover the per-device transition, and the health-alert branch couples a snapshot.
- `s_checkin_seq++` fires for unprovisioned LoRa sensors — `find_device()` returns NULL for an unknown id, so the handler returns before the bump is reached.

## 6. Out of scope (scoped, not built)

- **C2D ack gaps** found while auditing this session: legacy-text commands and parse failures emit
  no `cmd_ack` at all (even when the message carried a correlation id); `error.code` is the command
  name rather than a code; `valve_close` has no error path; `ok` means *accepted*, not *applied*.
- **D6 lifecycle-on-reconnect** — still deferred by earlier decision.
