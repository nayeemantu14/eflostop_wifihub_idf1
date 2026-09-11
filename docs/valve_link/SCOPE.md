# Scope of Work — Valve link loss: fast detection, damped notification

**Target release:** hub FW 2.2.0
**Status:** scope agreed · implementation not started
**Owner:** hub firmware
**Revision:** 2 — amended for D7 (no new events) and D8 (rating drives the app tile colour)
**Blocked on:** one confirmation from the cloud team (§4.1) before Phase 3 ships

---

## 1. Problem

When the BLE link to the valve drops, the cloud does not find out for **3–5 minutes**, and the
first thing it hears is already `critical`. The app therefore renders a stale "healthy" valve
for minutes and then jumps straight to red, with no intermediate state and no predictable
timing. Reported by the app team as "UI elements are not in sync".

Two independent suppressions cause it, both verified at FW 2.1.0:

1. **The disconnect edge produces no cloud message at all.** The valve-event publish block is
   gated behind `if (vlk_mac_ok)` (`app_iothub.c:2343`), and `vlk_mac_ok` derives from
   `ble_valve_get_mac()` (`:2177`). `app_ble_valve.c:1328` zeroes `g_valve_mac` *immediately
   before* `notify_hub_update(BLE_UPD_DISCONNECTED)` at `:1329`, so that gate is structurally
   always false on the disconnect edge.
2. **No health alert fires on the WARNING edge.** `maybe_enqueue_alert()`
   (`health_engine.c:199-212`) returns early unless the transition crosses CRITICAL.

First signal is therefore the CRITICAL promotion: 180 s grace + ≤30 s health tick + ≤30 s for
`iothub_task` (the alert queue is **not** in the QueueSet, `app_iothub.c:2001-2010`) + ≤5 s
`SNAP_MIN_INTERVAL_MS` ≈ **245 s**, or the 300 s heartbeat, whichever lands first.

### 1.1 Root cause behind the root cause

`HEALTH_VALVE_DISC_TIMEOUT_MS` (180 s) is simultaneously **the "is it really gone?" filter** and
**the "should we interrupt the user?" policy**. Tightening it for responsiveness necessarily
spams; loosening it for calm necessarily blinds. This scope deletes that coupling.

### 1.2 The detection floor is 5.0 s, not 2.56 s

`ble_gap_connect(..., NULL, ...)` (`app_ble_valve.c:1207`) passes NULL connection parameters, so
NimBLE substitutes `ble_gap_conn_params_dflt` — supervision `BLE_GAP_INITIAL_SUPERVISION_TIMEOUT`
= 0x0100 = 2560 ms. That governs only the first few tens of ms. The valve then requests its own
parameters, **once per connection**:

| where | value |
|---|---|
| `DK-Servo_Motor/STM32_WPAN/App/app_ble.c:1192-1193` | `CONN_P(250)` → 250 ms interval |
| `DK-Servo_Motor/Core/Inc/app_conf.h:181` | `L2CAP_TIMEOUT_MULTIPLIER 0x1F4` = 500 × 10 ms = **5000 ms** |

and the hub accepts it blind — `case BLE_GAP_EVENT_L2CAP_UPDATE_REQ: return 0;`
(`app_ble_valve.c:1351-1353`). **The steady-state supervision timeout is 5000 ms and nobody on
this product chose it.** It went unnoticed because `BLE_GAP_EVENT_CONN_UPDATE` (`:1347-1349`)
logs only `status`: the firmware has never once printed its own connection parameters.

### 1.3 A second, larger defect found while scoping — the hub reports its own intent as fact

`write_valve_command()` (`app_ble_valve.c:1583-1589`):

```c
int rc = ble_gattc_write_flat(valve_conn_handle, h_valve_char, &val, 1, NULL, NULL);
if (rc == 0)
{
    g_val_state = val;              // <-- the hub's INTENT, not the valve's state
    notify_hub_update(BLE_UPD_STATE);
}
```

`rc == 0` is **local NimBLE host acceptance**, not valve confirmation. The cache is then written
from the hub's own intent, and the valve's real confirming value is subsequently **swallowed** by
the delta gate at `:522-526` (`old_state == g_val_state` ⇒ no event). Both wire legs —
`valve_state_changed.valve_state` (`telemetry_v2.c:784`) and snapshot `data.valve.state`
(`:581-583`) — read that same variable. Same defect on `apply_pending_valve_cmd_if_any()`
(`:725-729`) and on the C2D path.

**Consequence: if the valve does not actually close, the hub tells the cloud it did, and nothing
corrects it until the next reconnect** (setup step 5, `:818-822`, re-reads the characteristic).
On a water shut-off product that is false assurance at the worst possible moment. Addressed by
**P0-7**.

---

## 2. Decisions taken

| # | Decision | Rationale |
|---|---|---|
| D1 | **No new JSON keys.** | Backend/app just absorbed a breaking rename in 2.1.0. |
| D2 | **Snapshot `rating` goes `critical` immediately on link loss.** No WARNING stage for link loss. | One state, no intermediate for the UI to misread. |
| D3 | **A snapshot is published on the disconnect edge (~3.2 s).** | The UI cannot be in sync with a message that is never sent. |
| D4 | **`device_offline` stays damped and decoupled from the rating.** | Satisfies "do not spam" without slowing the state channel. |
| D5 | **Hub firmware only. No valve firmware change.** | The hub clamps connection parameters as central; §5 Phase 1. |
| D6 | **`last_seen_age_s` is made honest.** | Currently means "since last *changed* data"; has no relation to the rating. |
| **D7** | **No new event names.** Kills `auto_close_unconfirmed` and `valve_link_unstable`. | With D1, the wire surface is **frozen**. |
| **D8** | **`rating` drives the app tile colour.** | Resolves what the app binds to. Makes §3 the entire app contract. |

### 2.1 The two channels

| | STATE | ALERT |
|---|---|---|
| carrier | snapshot (`type:"snapshot"`) | `device_offline` / `device_recovered` |
| latency | **~3.2 s** (~1.4 s armed) | armed ~1.4 s · idle 10 min · flapping never |
| app binds | **`valve.rating` → tile colour (D8)** | — |
| may raise a push? | **NO** | **YES — the only channel that may** |

> **D8 supersedes revision 1, which said the app binds the tile to `connected`.** It does not.
> This has a consequence that must be stated plainly: **`rating` has no valve-position input**,
> so the tile is structurally blind to whether the valve is open or closed. The hub's own LED is
> *more honest* — `fleet_led.c:149-156` already overrides to RED on
> `ble_valve_is_connected() && (leak || rmleak)`. That override is the existing precedent for
> feeding incident state into a user-facing colour.

---

## 3. The valve rating contract (D8)

After P2-2, `compute_valve_rating()` reads exactly three fields: `disconnect_ms`,
`last_seen_ms`, `last_battery`. **This table is now the app's entire contract.**

### 3.1 Intended rows

| # | Condition | `rating` | `connected` | `state` | User must be told |
|---|---|---|---|---|---|
| 1 | `disconnect_ms > 0` — link down, any duration | `critical` | `false` | `"disconnected"` | "Valve unreachable" |
| 2 | `last_seen_ms == 0` — never reached this uptime (boot, post-reboot, post-reprovision) | `critical` | `false` | `"disconnected"` | "Valve not yet reached" — **not** "valve failed" |
| 3 | connected, `battery <= 20` | `warning` | `true` | `"open"`/`"closed"` | "Valve battery low" |
| 4 | connected, `21..35` | `good` | `true` | — | Healthy |
| 5 | connected, `> 35` | `excellent` | `true` | — | Healthy |
| 6 | connected, battery `0xFF` (unknown) | `excellent` | `true` | — | Healthy — **unknown renders as healthy** |

**Row 1 is the entire link-loss vocabulary.** After P2-2 there is no link-derived WARNING left:
**valve `warning` means battery, and only battery, forever.**

Two things the app team must be told explicitly:

- **The valve and the sensors no longer speak the same rating language.** `compute_sensor_rating()`
  is unchanged and keeps three causes including RSSI. The valve path never populates `last_rssi`
  at all, so **a valve tile cannot express "link marginal"**, and D7 removed the alternative
  carrier. Do not imply signal quality from a valve rating.
- **`good` and `excellent` are not distinguishable to the user** — the hub collapses both to
  green. Either the app declares its own 4→3 collapse in writing, or two of the four enum values
  are dead weight.

### 3.2 Pathological rows — also part of the contract, all four currently undefined

| # | Condition | `rating` | User sees | Status |
|---|---|---|---|---|
| 7 | No valve health entry (`valve_hs == NULL`: unprovisioned, mid-decommission) | **KEY ABSENT** — `telemetry_v2.c:600` wraps `rating` *and* `last_seen_age_s` in `if (valve_hs)` | Undefined | **App must specify.** Recommend: absent ⇒ neutral/grey, **never green** |
| 8 | `HEALTH_EVT_VALVE_DISCONNECTED` dropped from the input queue | `excellent` **permanently** | **Green tile, valve unreachable, never self-heals** | **New defect — P0-8** |
| 9 | Publish-ordering race on the disconnect edge | `excellent` **with** `connected:false` in the same payload | **Green at the instant of link loss** | Was bench-gate G4 (cosmetic). **Under D8 it is a safety defect — P2-4** |
| 10 | Valve reachable, battery fine, **close failed / gate jammed** | `excellent` | **Green while water is on the floor** | Valve position is not an input to the rating. Mitigated by **P0-7**; see §9 risk 2 |

> **Row 8 deserves a second read.** `health_post_valve_event()` (`health_engine.h:212-218`)
> **discards** the return of `health_post_event()`, which is `xQueueSend(..., 0)` on a **depth-16**
> queue drained by the **lowest-priority task in the firmware** (prio 2 vs `iothub_task` prio 5).
> If the DISCONNECTED event is dropped, `disconnect_ms` is never stamped — and `evaluate_timeouts()`
> gates the valve re-rate on `if (dev->disconnect_ms > 0)`, so **no 30 s tick can ever recover it**.
> The failure is asymmetric in the unsafe direction: a dropped CONNECTED self-heals on the next
> data update; a dropped DISCONNECTED never does. **P0-3 raises the *alert* queue — this is the
> *input* queue, a different queue.**

---

## 4. Open questions

### 4.1 BLOCKING Phase 3 — what does the backend trigger a push on?

**D8 raises the pressure on this rather than relieving it.** D8 answers what the *app* binds to;
it does not answer what the *backend pushes on*, and now that `rating` is the tile there is more
temptation to push from it. If notifications are driven from snapshot `rating` or
`system_health.rating`, D2 (critical at ~3 s) **spams maximally** — a 3-second RF blip buzzes the
phone.

**Required from Watts Digital:** push is emitted on the `device_offline` **event only**, never on
a snapshot field. If they push on rating today, they change the trigger *first*, then we ship.

Phases 0–2 are safe to build regardless.

### 4.2 New events — CLOSED by D7

Both `auto_close_unconfirmed` and `valve_link_unstable` are out.

**Record this accurately:** the hazard `auto_close_unconfirmed` targeted is **real**, is **not**
solved by Phase 2, and is now addressed hub-side by **P0-7** instead. Do not record it as
"technically void" — that converts an accepted risk into a closed question and guarantees nobody
revisits it. The lost advisory (`valve_link_unstable`) was nice-to-have and is simply gone.

### 4.3 What the app binds to — CLOSED by D8

`rating`. See §3.

---

## 5. Work breakdown

**S** ≈ under half a day · **M** ≈ 1–2 days · **L** ≈ 3+ days. Bench time excluded (§8).

### Phase 0 — Correctness prerequisites

No design decisions, no cloud dependency. **Every later phase is invisible or actively wrong
without them.** Ship as 2.1.1.

| # | File · function | Change | Size | Risk |
|---|---|---|---|---|
| P0-1 | `app_ble_valve.c:1328-1329` | Capture `g_valve_mac` to a local before the `memset`, or move the `memset` after `notify_hub_update()`. Unblocks every valve link message. | S | Low |
| P0-2 | `health_engine.c:295-300` | Guard the disconnect stamp: `else if (dev->disconnect_ms == 0)`. Today a valve flapping faster than the grace restarts it forever and **never** reaches CRITICAL. Also fixes a spurious `device_recovered` for a valve that never came back. | S | Low |
| P0-3 | `health_engine.c:236` + `:538` | Do not commit `dev->rating` when the **alert** `xQueueSend` fails; raise alert queue 4 → 8. | S | Low |
| P0-4 | `app_iothub.c:2001-2010` | Add the health alert queue to the QueueSet. Removes ≤30 s latency per alert. | S | Low |
| P0-5 | `app_ble_valve.c` · `BLE_GAP_EVENT_ENC_CHANGE` failure branch | `ble_store_util_delete_peer()` before terminating. A lost valve-side bond makes the hub re-offer a dead LTK forever in a ~5 s loop, whose cloud signature is **identical** to a marginal-RF flapper — so Phase 3 suppression would hide a permanent failure indefinitely. | M | Med |
| P0-6 | `app_ble_valve.c:1537,1547` + re-entrancy guard in `handle_valve_disc` | `filter_duplicates` 1 → 0, plus a `g_connecting` guard. **Safety, not latency** — see below. | M | **High** |
| **P0-7** | `app_ble_valve.c:1583-1589`, `:725-729` | **Valve position must be sourced from the valve, never from hub intent.** Stop writing `g_val_state` from the local ATT-write result. After the write, issue `ble_gattc_read(valve_conn_handle, h_valve_char, on_read_cb, NULL)` — the **identical call setup step 5 already makes on every connection** (`:818-822`), landing in `on_notify()` via `on_read_cb` (`:660`). The cache still holds `1`; a real close returns `0`, trips the delta gate, and emits `valve_state_changed("closed")` **sourced from the valve**. A failed close returns `1` → no event, and the coupled snapshot honestly ships `state:"open"`. | M | **High** |
| **P0-8** | `health_engine.h:212-218` + `health_engine.c:532` | **A dropped DISCONNECTED is unrecoverable.** Check `health_post_event()`'s return; on failure for a valve DISCONNECTED, retry or set a deferred flag the health task drains. Consider raising the **input** queue depth 16 → 32. Row 8 of §3.2. | M | **High** |

**P0-6 rationale.** The valve reconnect scan runs `filter_duplicates=1` while the leak scanner
runs `=0` (`app_ble_leak.c:290`). The controller dedups on **address only** and never refreshes
(`CONFIG_BT_CTRL_SCAN_DUPL_TYPE=0`, `CONFIG_BT_CTRL_DUPL_SCAN_CACHE_REFRESH_PERIOD=0`), and the
leak scanner's self-heal is gated on `!ble_gap_disc_active()` (`app_ble_leak.c:363`), false while
the valve scan owns the radio. A dry→LEAK data change does not clear an address-keyed entry.
**The exact condition in which we cannot close the valve is the condition in which we are least
likely to learn there is water on the floor.**

**P0-7 note.** The read-back formulation is deliberate. The alternative — "make NOTIFY the sole
writer of `g_val_state`" — is fragile: if the valve does not notify on a write-driven transition
the cache sticks at `open` for a physically closed valve, converting a silent-but-optimistic
signal into a **permanent false-alarm generator**. The read-back cannot get stuck, and it
depends only on the valve answering a GATT read, which it demonstrably does on every connection
in production today. **D1 ✓ D5 ✓ D7 ✓ D8 ✓** — existing event names, existing JSON keys, no valve
reflash.

### Phase 1 — Detection

| # | File · function | Change | Size | Risk |
|---|---|---|---|---|
| P1-1 | `app_ble_valve.c:1347-1353` | Accept the valve's L2CAP request unchanged. Then from `CONN_UPDATE`, `ble_gap_conn_find()`; if supervision exceeds the clamp or latency ≠ 0, issue a **master-initiated** `ble_gap_update_params()` `{itvl 200/200, latency 0, supervision 300}`. **One re-issue per connection (latch).** | M | Med |
| P1-2 | GAP event switch | Explicit `case BLE_GAP_EVENT_CONN_UPDATE_REQ: return 0;`. Today it falls through to `default:` and is silently accepted verbatim. | S | Low |
| P1-3 | `app_ble_valve.c:1347-1349` | Log `conn_itvl`, `conn_latency`, `supervision_timeout` on every connect and update. **Mandatory interlock** — the absence of this line is why a 2× error survived into production. | S | Low |
| P1-4 | `app_ble_valve.c:1308` | Branch on `event->disconnect.reason`. A peer-initiated terminate (0x13/0x16) is immediate and definitive — not eligible for blip grace. | S | Low |
| P1-5 | `app_ble_valve.c` · new `last_valve_adv_ms`; expose `ble_valve_adv_silence_ms()` | Advertisement-silence discriminator, used by Phase 3 for **alert eligibility only**. | M | Low |
| P1-6 | `app_ble_valve.c:1524-1525` | Scan `itvl` 160 → 176, `window` 80 → 88. 100 ms against a 500 ms advertising interval is a 5:1 harmonic lock. Identical duty cycle. | S | Low |
| P1-7 | new `ble_valve_set_armed_link(bool)` from `rules_engine` | **Armed Link Mode**: `{itvl 80/80, latency 0, supervision 120}`; revert on clear. | M | Med |
| P1-8 | `app_ble_leak.c:276-283` | Same de-harmonising. Leak sensors advertise in the same band. | S | Low |
| P1-9 | `app_ble_valve.c:1526` | `passive` 0 → 1. The valve sets no scan-response data, so every SCAN_REQ provokes a pointless coin-cell transmission. | S | Low |

**P1-1 is master-initiated deliberately:** as central the hub's `LL_CONNECTION_UPDATE_IND` has no
peripheral veto. The rejected alternative — mutating the valve's L2CAP request in place — is a
*negotiation*; on rejection NimBLE reverts to the 30–50 ms connect defaults **indefinitely**, a
silent 5–8× coin-cell drain. Failure here degrades to today's safe 250 ms / 5000 ms, and P1-3
makes it visible.

**Why no valve change:** the valve requests parameters exactly once per connection, from
`CUSTOM_CONN_HANDLE_EVT` (`custom_app.c:266-269` → `app_main.c:491 processStandy()`). No timer,
no retry, so the clamp is never undone; the sequence replays on every reconnect.

### Phase 2 — State publication

| # | File · function | Change | Size | Risk |
|---|---|---|---|---|
| P2-1 | `app_iothub.c` · valve-events block | Request a snapshot on `BLE_UPD_DISCONNECTED` and `BLE_UPD_CONNECTED`, **outside** the dead `vlk_mac_ok` gate but **inside** a new `s_valve_pub_linked` delta gate modelled on `s_valve_pub_state` (`:179`). Reset to `-1` alongside it on reconnect reconciliation. | M | Med |
| P2-2 | `health_engine.c:156-179` | Link down ⇒ `CRITICAL` immediately. `HEALTH_VALVE_DISC_TIMEOUT_MS` no longer governs the rating. **Risk raised Med → High: under D8 this is the tile.** | S | **High** |
| P2-3 | `health_engine.c` · `handle_valve_event()` | Refresh `last_seen_ms` whenever the link is **confirmed alive**, not only on delta-gated notifies (D6). | M | Med |
| **P2-4** | `app_iothub.c` · snapshot flush | **Close the publish-ordering race.** `valve.connected` comes from the BLE cache (already zeroed); `valve.rating` comes from the health engine on a lower-priority task. The snapshot request must be sequenced behind the health engine's view of the disconnect. Row 9 of §3.2 — **was bench-gate G4, now a design requirement.** | M | **High** |
| **P2-5** | `app_iothub.c` | **State-channel damping.** The `s_valve_pub_linked` delta gate suppresses *repeats*, but every genuine down/up edge still publishes. Under D2+D8 a flapper strobes the tile red/green. Widen the minimum publish interval to 60 s with last-value-wins once ≥3 disconnects/h are seen. Internal only — no new field. | M | Med |

### Phase 3 — Notification decoupling · **gated on §4.1**

| # | File · function | Change | Size | Risk |
|---|---|---|---|---|
| P3-1 | `health_engine.h` · `health_post_valve_event()` | Extend to carry `disc_reason`, `adv_silent`, `hazard_armed`. `hazard_armed` sources from `rules_engine_is_leak_incident_active()` plus override/pending-write state. **This missing argument is the architectural gap.** | M | Med |
| P3-2 | `health_engine.c:199-243` | **Stop keying off the rating transition.** Key off a separate eligibility predicate. See the trap. | L | **High** |
| P3-3 | `health_engine.c` · new | Latch-and-Bucket (§5.1). | L | High |
| P3-4 | `health_engine.c:332-351` | The armed path must be **event-driven from the disconnect edge**, never via the 30 s tick. | M | Med |
| P3-5 | `health_engine.c` + NVS `"health"` | Persist bucket state and `last_alert_ms`. A reboot loop must not refill the bucket. | M | Low |
| P3-6 | `health_engine.h:24` | Retire `HEALTH_VALVE_DISC_TIMEOUT_MS` as a dual-purpose constant → `HEALTH_VALVE_CONFIRM_MS`, `HEALTH_VALVE_OFFLINE_PUSH_MS`. | S | Low |

> **⚠ THE TRAP — P3-2 is the single most important item in this scope.**
> Today the alert is driven directly off the rating transition. If P2-2 lands and
> `maybe_enqueue_alert()` still keys off `EXCELLENT → CRITICAL`, **`device_offline` fires at
> ~3 seconds** and the product spams maximally. **P2-2 and P3-2 must land in the same build, or
> P2-2 waits.**

#### 5.1 Latch-and-Bucket

1. **Evidentiary confirmation gate.** Alert-eligible only when `link_down AND adv_silence ≥ 10 s`.
   A live valve in range advertises every 500–700 ms into a ~50 %-duty scan, so a blip never
   qualifies — and produces no `device_recovered` either, because nothing was ever alarmed.
   **This stage is the entire anti-spam guarantee; D7 did not touch it.**
2. **Hazard-class timer.** Armed = 0 ms, idle = 600 000 ms. Reconnect cancels.
3. **Escalation latch.** Clearing requires 60 s of continuous link. Max two alert messages per
   episode.
4. **Token bucket**, per device: capacity 2, refill 1 / 24 h, persisted. **Armed bypasses the
   bucket, the debounce and the latch entirely.**

**Flap detection is retained internally** (it feeds stage 1 and P2-5) even though D7 removed its
outbound advisory.

**Worked example — valve flaps every 30 s for an hour (120 cycles):** stage 1 blocks every
episode because the valve keeps advertising → **0 pushes, 0 alert messages**, and with P2-5
≤ ~60 coalesced snapshots instead of 240 tile transitions.

### Phase 4 — Availability (schedule separately)

| # | File · function | Change | Size | Risk |
|---|---|---|---|---|
| P4-1 | `app_ble_valve.c:1207` | `ble_gap_connect` duration 30000 → 5000 with retry. Today one stale advertisement costs **30 s of total BLE blindness** — valve *and* leak sensors. | S | Med |
| P4-2 | `app_ble_valve.c:57` | `POST_CONNECT_SECURITY_DELAY_MS` 1000 → 200. **Weakest-evidenced number here** (G6). | S | Med |
| P4-3 | `start_discovery_chain()` | Cache the GATT handle set per bonded peer. Removes ~35–40 ATT round trips per relink. | L | Med |

---

## 6. Constants

| Constant | Value | Justification |
|---|---|---|
| Idle conn interval | **250 ms — unchanged** | The valve's own request. Modelled ~6 µA / ~3.4 yr. |
| Idle supervision timeout | **3000 ms** (from 5000) | **Margin policy: the link tolerates exactly 12 lost connection events.** 250 ms × 12. Spec floor 500 ms. |
| Armed conn interval | **100 ms** | ~12 µA. A 24 h window costs 0.29 mAh of ~180 mAh = **0.16 % of valve life**. |
| Armed supervision timeout | **1200 ms** | 100 ms × 12. |
| Slave latency | **0, clamped hub-side** | `supervisionTimeout > (1+latency) × connInterval × 2` couples latency to the detection floor. |
| `HEALTH_VALVE_CONFIRM_MS` | **10 000 ms** | 14–20 missed advertising opportunities. **If P1-6 slips, raise to 15 000.** |
| `HEALTH_VALVE_OFFLINE_PUSH_MS` | **600 000 ms** | 2× `SNAPSHOT_INTERVAL_MS`, so every cold push is corroborated by a heartbeat already reporting `connected:false`. |
| `RECOVERY_STABLE_MS` | **60 000 ms** | Reuses the existing `HEALTH_ALERT_DEBOUNCE_MS` value — no new constant. |
| Scan itvl / window | **176 / 88** | Breaks the 5:1 harmonic lock at identical duty cycle. |
| State damping (P2-5) | **5 s → 60 s at ≥3 disc/h** | Stops the D2+D8 tile strobe. |
| Push bucket | **capacity 2, refill 1 / 24 h** | Idle/flap only; armed unbudgeted. |
| `ble_gap_connect` duration | **5000 ms** | ≈ 7–10 valve advertising intervals. |

**Modelled, not measured:** valve current vs interval — 250 ms ≈ 6 µA / 3.4 yr; 100 ms ≈ 12 µA /
1.7 yr. Anchored on the bench-measured 5–7 µA in Stop2, CR2032 derated to ~180 mAh usable.
**Buying detection speed with connection interval requires a bench measurement first.**

---

## 7. Backend correlation rule (auto-close outcome)

Derivable from the frozen surface **once P0-7 lands**. Correlate on `gateway.id` + `valve_id`. On
ingest of `data.event == "auto_close"`:

```
rmleak_asserted == false
    -> NOT ISSUED AT THIS INSTANT. NOT terminal. The hub is scanning and will
       re-issue on reconnect (expect a second auto_close with cause:"reconnect").
       Render "closing…", never "your valve was not closed".

otherwise inspect the NEXT type:"snapshot" from that gateway.id (INGEST ORDER, not ts):

  connected:true  + state:"closed" + rmleak:true  -> CLOSE CONFIRMED BY THE VALVE
                                                     (post-P0-7 only; before that,
                                                      "issued, not confirmed")
  connected:true  + state:"open"                  -> CLOSE FAILED, valve reachable.
                                                     Highest severity. This row only
                                                     becomes reachable via P0-7.
  connected:false (state "disconnected",
                   post-P2-2 also rating:"critical") -> UNCONFIRMED, LINK LOST (~3.2 s)
  no snapshot arrives                             -> UNKNOWN, not unconfirmed.
                                                     Snapshots are DROPPED while offline
                                                     (telemetry_v2.c:129-132) while events
                                                     are buffered and replayed (:125-128).
```

Window: **10 s** steady state (bounded by `VALVE_CMD_SETTLE_MS` 1500 + `SNAP_MIN_INTERVAL_MS`
5000); **15 s** to cover an in-flight reconnect. **Never a fixed timeout for the reboot/offline
case.** Suppress every timer verdict across a hub reboot — detect via `gateway.uptime_s`
decreasing between snapshots (existing key, D1-safe).

---

## 8. Bench gates

| # | Gate | Accept criteria |
|---|---|---|
| G1 | Conn-update loop (P1-1 is triggered by the event it generates) | Exactly one LL update per connection, over 100 connect cycles |
| G2 | 3000 ms under Wi-Fi coex. ESP32-S3 is single-antenna; losses are bursty and correlated, so per-event PER math understates P(12 consecutive) | 72 h soak, 3 hubs, heavy STA + an OTA mid-run. **< 1 spurious disconnect per valve per 7 days.** On failure go to **4000 ms, not 2500** |
| G3 | `filter_duplicates=0` host load (P0-6) | 6 sensors, dense RF; host CPU stable, no connect starvation. Fallback: periodic scan restart, not disabling the filter |
| G4 | **Publish-ordering race — now a design requirement (P2-4), not an observation** | 20 consecutive disconnects: **no snapshot may ever ship `connected:false` with `rating:"excellent"`** |
| G5 | Armed-path latency | Disconnect edge → MQTT publish with a leak open: **< 2 s** |
| G6 | `POST_CONNECT_SECURITY_DELAY_MS` 200 | 200 reconnect cycles, zero security failures. Back off 500, then 1000 |
| G7 | Physical link loss | (a) pull the cell mid-connection → confirmed-gone ~13 s; (b) foil-wrap → confirmed-gone; (c) out of range and back within 8 s → **no alert of any kind**. **(c) proves the anti-spam design** |
| G8 | Armed Link Mode transitions | Update issued on incident *open* before the RMLEAK/close enqueue; a mid-incident reconnect re-applies armed params |
| G9 | Bucket across reboots (P3-5) | Survives a reboot loop |
| **G11** | **P0-7 close confirmation** | Physically block the valve gate, command a close: the snapshot must report `state:"open"`. Unblock: must report `"closed"`. **This is the test that proves the hub stopped reporting its own intent.** |
| **G12** | **P0-8 dropped-disconnect recovery** | Flood the health input queue, force a disconnect: the valve must still reach `rating:"critical"` |
| G10 | Regression | A capture spanning disconnect/reconnect passes `validate_capture.py` 100 % |

---

## 9. Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| Backend pushes on snapshot `rating` | **Catastrophic** — maximal spam, worse than today | §4.1 hard gate on Phase 3 |
| P2-2 ships without P3-2 | **Catastrophic** — `device_offline` at 3 s | Same build, or hold P2-2 |
| **Health input queue pins the tile green permanently** (row 8) | **Catastrophic** — unreachable valve renders healthy, never self-heals. D8 promoted this from cosmetic to safety-critical | **P0-8**, G12 |
| **`rating` has no valve-position input** (row 10) | Tile is blind to the product's primary function; green while water is on the floor | **P0-7** makes the position honest; §7 gives the backend the correlation rule |
| **Publish-order race** (row 9) | Green tile at the instant of link loss | **P2-4**, G4 |
| **`rating` key can be absent** (row 7) | The whole app contract can be a missing field | App must define; recommend neutral, never green |
| **D2+D8 tile strobe on a flapper** | User sees red/green oscillation | **P2-5** |
| 3000 ms too aggressive under coex | Spurious disconnects | G2; fallback 4000 ms |
| P0-6 saturates the NimBLE host | Dropped advertisements | G3; bounded fallback |
| Valve rejects the master update | Reverts to 5000 ms detection | Degrades to today's behaviour; P1-3 makes it visible |
| `last_seen_age_s` semantic change (D6) | Backend dashboards shift | DOC-3; notify Watts Digital before ship |
| **Frozen surface + mutable semantics** | With no new keys or events permitted, every future requirement is met by changing what an existing field *means*. 2.2.0 does this three times — `last_seen_age_s`, `rating`, `valve_state_changed` — each with an identical wire shape and **no version signal**. D1 exists to protect consumers from breaking changes; with D7 it converts every breaking change into a **silent** one | **Procedural and release-blocking: no semantic change ships without a DOC item and a named notice to Watts Digital** |

---

## 10. Documentation

| # | Where | Change |
|---|---|---|
| DOC-1 | `docs/telemetry/messages_data.py` → regenerate | Snapshot published on the valve link edge; `valve.rating` goes `critical` on disconnect with **no WARNING stage** |
| DOC-2 | same | Write **"A state field is not a notification trigger"** into the contract. `device_offline` is the only push-eligible signal |
| DOC-3 | same | Correct `last_seen_age_s` semantics for the valve (D6) |
| DOC-4 | same | **Publish §3 verbatim** — under D8 it is the app's entire contract, including the four pathological rows |
| DOC-5 | same | Record that `valve_state_changed` is now sourced from the valve, not hub intent (P0-7). Silent semantic change — notify Watts Digital |
| DOC-6 | `DK-Servo_Motor/Core/Inc/app_conf.h:62-63` | Comments say `/**< 80ms */` and `/**< 100ms */` for values that are **500 ms and 700 ms**. Off by 6–7×. **Comment-only, no reflash** |

---

## 11. Sequencing

```
Phase 0 (P0-1..P0-8)  ──────►  ship alone as 2.1.1, no cloud dependency
   │
   ├─► Phase 1  ──────────────►  needs G1, G2
   │       │
   │       └─► Phase 2  ───────┐
   │                           ├─► MUST ship together (the P3-2 trap)
   │           Phase 3  ───────┘   gated on §4.1
   │
   └─► Phase 4  ──────────────►  independent
```

- **2.1.1** — Phase 0 only. Eight bug fixes, no cloud coordination. Gets P0-6 (leak deafness),
  P0-7 (false close confirmation) and P0-8 (permanently green tile) fixed immediately.
- **2.2.0** — Phases 1+2+3 together, after §4.1 is answered.
- **2.2.x** — Phase 4.

---

## 12. Expected outcome

| | today | after 2.2.0 |
|---|---|---|
| Cloud knows, idle | ~245 s (or never, per P0-2) | **~3.2 s** |
| Cloud knows, leak active | ~245 s | **~1.4 s** |
| Cloud knows, valve terminated deliberately | ~245 s | **~0.5 s** |
| Tile state | stale-green, then jumps red | red in ~3 s, and correct |
| **Close reported but not achieved** | **reported as closed** | **reported as open (P0-7)** |
| Push on a 3 s blip | possible | **never** |
| Push on a flapping valve, per hour | possible | **zero** |
| Push for a genuinely dead valve, idle | ~4 min | 10 min, evidence-backed |
| Valve battery cost | — | **zero in idle**; 0.16 % per 24 h incident |
| Valve firmware releases required | — | **none** |
