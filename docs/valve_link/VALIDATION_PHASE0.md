# Manual Validation Plan — Phase 0 (hub FW 2.1.1)

**Build under test:** branch `feat/valve-link-detection`, commit `bfbf304`, `PROJECT_VER 2.1.1`
**Scope:** the eight correctness prerequisites in [SCOPE.md](SCOPE.md) §5 Phase 0
**Status of the code:** compiles clean against the real toolchain; **never run on hardware**

> Two of these changes (P0-6, P0-7) touch behaviour that only shows up when something is
> already going wrong. Please do not skip V6 and V7 — they are the only tests that exercise the
> two defects with genuine safety consequences.

---

## 0. Before you start

### 0.1 Confirm you flashed the right binary

The single most common way to waste a bench session is validating the old image.

```
idf.py build flash monitor
```

In the boot banner, confirm **`2.1.1`**. If it still says 2.1.0 the build did not take.
Cross-check the ELF hash against the banner if in doubt:

```
sha256sum build/eFloStop_WiFiHub_idf1.elf     # first 9 hex chars match the banner
```

### 0.2 Rig

| Item | Requirement |
|---|---|
| Hub | UART at 115200, `idf.py monitor` capturing to a file |
| Valve | Powered, provisioned, reachable. Physical access to the gate for V7. |
| Leak sensor | **At least one**, provisioned, and you must be able to wet/dry it |
| Cloud | `az iot hub monitor-events -n <hub> -d GW-xxxxxxxxxxxx --props all -t 0`, running **before** each test |
| Optional | A second leak sensor makes V6 far more convincing |

### 0.3 Capture both streams for every test

UART alone cannot show publish timing; the cloud alone cannot show *why*. Several pass criteria
below depend on comparing the two.

### 0.4 Baseline — establish a known-good state

Wait for a `reason: "heartbeat"` snapshot showing `valve.connected: true`, `valve.rating`
`excellent`, and every sensor `connected: true`. Do not start until you have seen it.

---

## 1. Test cases

Each test lists the change it exercises, the stimulus, what to watch, and the **pass criterion**.
Where a test can only *partially* confirm a change, that is stated — please do not record a pass
you did not actually observe.

---

### V1 — The disconnect edge reaches the cloud at all  · P0-1 · **CRITICAL**

*This is the headline fix. Before it, a valve disconnect produced no cloud message whatsoever.*

**Stimulus.** With the hub idle and online, **power the valve off** (pull the cell).

**Watch UART for:**
```
[DISCONNECT] reason=0x08
SNAP trigger=event:valve_unlinked
Pub snapshot:
```

**PASS if all of:**
- `SNAP trigger=event:valve_unlinked` appears within **~10 s** of the disconnect
- A snapshot reaches the cloud carrying `valve.connected: false` and `valve.state: "disconnected"`
- The snapshot's `data.reason` is `"event"`

**FAIL if** no snapshot appears until the next heartbeat, or until `device_offline` fires ~4 min later. That is the old behaviour.

**Record:** seconds from `[DISCONNECT]` to the snapshot arriving in the cloud: ______

---

### V2 — The reconnect edge also publishes, and only once  · P0-1

**Stimulus.** Power the valve back on. Wait for full reconnect.

**PASS if:**
- `SNAP trigger=event:valve_linked` appears **exactly once**
- The following snapshot shows `valve.connected: true`
- You do **not** see repeated `valve_linked` / `valve_unlinked` requests while the link is stable

**This is the delta gate.** Repeats mean `s_valve_pub_linked` is not holding.

---

### V3 — A flapping valve does not flood the cloud  · P0-1 · P0-6

**Stimulus.** Put the valve at the edge of range (foil, or a long corridor) so it connects and
drops repeatedly for **~10 minutes**.

**PASS if:**
- Snapshots appear on **transitions only**, not continuously
- No two snapshots arrive closer together than **5 s** (the `SNAP_MIN_INTERVAL_MS` cap)
- The hub does not reboot, and the log shows no heap warnings

**Record:** snapshots in 10 min: ______   ·  disconnect/reconnect cycles in 10 min: ______

---

### V4 — A fast-flapping valve now eventually reports critical  · P0-2 · **CRITICAL**

*Previously impossible. A valve flapping faster than the 180 s grace restarted the clock forever
and never reached CRITICAL — the most degraded valve in the fleet stayed silent.*

**Stimulus.** Force the valve to disconnect and reconnect roughly **every 60–120 s** (well inside
the 180 s grace) and hold that for **at least 12 minutes**.

**PASS if:**
- `valve.rating` reaches **`critical`** and `system_health.reason` becomes `"Valve offline"` at some point
- UART shows `ALERT: valve ... -> critical`

**FAIL if** the rating oscillates between `excellent` and `warning` indefinitely and never reaches `critical`.

**Also check (P0-2's second bug):** you must **not** see a `device_recovered` event for the valve
at any point when the valve did not actually come back. Search the cloud capture:
```
grep -c '"event": "device_recovered"' capture.txt
```
Each one must correspond to a real reconnection you observed.

---

### V5 — Alert latency  · P0-4

**Stimulus.** Leave the valve powered off (from V1) and wait for the CRITICAL promotion.

**PASS if** the `device_offline` event reaches the cloud within **~10 s** of the UART line
`ALERT: valve ... -> critical`.

**FAIL if** the gap is tens of seconds — that means the wake is not working and the alert is
waiting for the loop's idle timeout.

**Record:** UART `ALERT:` timestamp ______   ·  cloud `device_offline` timestamp ______

---

### V6 — Leak sensors stay audible while the valve is missing  · P0-6 · **SAFETY — DO NOT SKIP**

*This is the most important test in the plan. Before this fix, while the hub was hunting for a
lost valve it could be deaf to a leak sensor going wet — the exact condition in which it cannot
close the valve is the one in which it is least likely to learn there is water.*

**Setup.** Leak sensor provisioned, dry, and confirmed `connected: true` in a snapshot.

**Stimulus, in this order:**
1. **Power the valve off** and leave it off. Confirm UART shows the hub scanning for it
   (`[SCAN] Starting scan for 'eFloStopV2'`).
2. Wait **2 minutes** with the valve still absent, so the scan is well established.
3. **Now wet the leak sensor.**

**PASS if:**
- A `leak_detected` event reaches the cloud within **~30 s** of wetting
- UART shows the BLE leak event being received

**FAIL if** the leak is not detected, or is only detected after you restore the valve. **A failure
here is a blocker — do not ship.**

**Then dry the sensor** and confirm `leak_cleared` also arrives. Repeat the wet/dry cycle **three
times** without restoring the valve; all three must be detected.

**Record:** wet→`leak_detected` latency, each of 3 cycles: ______ / ______ / ______

---

### V7 — A close that does not happen is not reported as closed  · P0-7 · **SAFETY — DO NOT SKIP**

*Before this fix the hub cached its own intent the moment the write was locally queued, so a
valve that never received or never acted on a close was still reported as closed.*

**Stimulus A — happy path (regression check first):**
1. Valve connected, open, everything healthy.
2. Send C2D `valve_close`.
3. **PASS if** `valve_state_changed` with `valve_state: "closed"` arrives, and the following
   snapshot shows `valve.state: "closed"`.
   *This confirms the valve's own notify is now driving the report. If this fails, P0-7 has
   broken the normal path and must be reverted.*

**Stimulus B — the actual test:**
1. Re-open the valve.
2. **Physically prevent the gate from moving** (hold it, or block it — whatever your rig allows
   without damaging the motor).
3. Send C2D `valve_close`.

**PASS if** the snapshot after the command reports `valve.state: "open"`, or omits the state —
**anything except a confident `"closed"`.**

**FAIL if** the hub reports `"closed"`. That is the defect P0-7 exists to remove.

> **Honest limitation, please note when recording the result:** the valve has no position sensor
> and reports its *commanded* state. If the valve's firmware believes it closed, it will say
> closed. This test proves the hub stopped inventing the answer; it does not prove physical
> position sensing, which this product does not have.

**Stimulus C — link lost mid-command:**
1. Valve connected and open.
2. Send C2D `valve_close` and **power the valve off within ~1 second**.
3. **PASS if** no `valve_state_changed("closed")` is published, and the snapshot shows
   `connected: false` rather than a closed valve.

---

### V8 — A reflashed valve re-pairs instead of looping  · P0-5

**Stimulus.** Erase the valve's bond (reflash it, or factory-reset it) while it stays provisioned
in the hub.

**Watch UART for:**
```
[ENC_CHANGE] Encryption failed: status=...
[ENC_CHANGE] Peer no longer holds our bond — deleting stale LTK
```

**PASS if** the hub deletes the bond and **successfully re-pairs** within a few attempts.

**FAIL if** it loops indefinitely re-offering the dead key (`[ENC_CHANGE] Encryption failed`
repeating every ~5 s with no recovery).

**If you cannot easily erase the valve bond, mark this NOT TESTED rather than guessing.**

---

### V9 — Host load with the duplicate filter off  · P0-6

**Stimulus.** In the densest RF environment you can arrange (office Wi-Fi, several BLE devices,
phone hotspots), leave the hub scanning for an **absent** valve for **30 minutes** with all leak
sensors present.

**PASS if all of:**
- No reboot, no watchdog, no `E (` errors in the UART log
- Free heap stays stable — check `min_ever` does not trend downward
- Leak sensors continue to be reported throughout

**Record:** starting free heap ______  ·  ending free heap ______  ·  `min_ever` ______

---

### V10 — Regressions

Run the normal flows and confirm nothing broke:

| # | Flow | Pass criterion |
|---|---|---|
| V10.1 | Boot → provision → commission snapshot | `reason: "commission"` snapshot arrives, all devices listed |
| V10.2 | Leak → auto-close | `leak_detected` → `auto_close` → `valve_state_changed("closed")` → snapshot, **in that order** |
| V10.3 | Heartbeat cadence | Snapshots every 300 s (or your twin-configured interval), no drift |
| V10.4 | C2D `valve_open` / `valve_close` | Both work, `cmd_ack` returned, snapshot follows |
| V10.5 | RMLEAK auto-clear | After 30 s dry, `rmleak_auto_cleared` fires |
| V10.6 | Contract validator | `python docs/telemetry/validate_capture.py <capture>` passes 100 % |

**V10.2 deserves attention** — it is the flow P0-7 most directly touches.

---

## 2. Sign-off

| # | Test | Result | Notes |
|---|---|---|---|
| V1 | Disconnect edge published | ☐ PASS ☐ FAIL ☐ N/T | |
| V2 | Reconnect edge, once only | ☐ PASS ☐ FAIL ☐ N/T | |
| V3 | Flapping does not flood | ☐ PASS ☐ FAIL ☐ N/T | |
| V4 | Fast flap reaches critical | ☐ PASS ☐ FAIL ☐ N/T | |
| V5 | Alert latency < ~10 s | ☐ PASS ☐ FAIL ☐ N/T | |
| **V6** | **Leaks heard while valve missing** | ☐ PASS ☐ FAIL ☐ N/T | **blocker if FAIL** |
| **V7** | **Unachieved close not reported closed** | ☐ PASS ☐ FAIL ☐ N/T | **blocker if FAIL** |
| V8 | Reflashed valve re-pairs | ☐ PASS ☐ FAIL ☐ N/T | |
| V9 | Host load stable, 30 min | ☐ PASS ☐ FAIL ☐ N/T | |
| V10 | Regressions (6 sub-cases) | ☐ PASS ☐ FAIL ☐ N/T | |

**Ship criteria:** V6 and V7 must PASS. V1, V2, V4 and V10 must PASS. V3, V5, V8, V9 may be
deferred with a written note, but V9 must complete before any fleet rollout.

---

## 3. What this plan does NOT cover

Stated so nobody assumes more coverage than exists.

- **P0-3 and P0-8** (the two queue-drop fixes) are **not directly testable** on the bench — they
  require the health queues to actually overflow, which needs fault injection rather than a
  stimulus. They are covered by code review and the compiler only. If you want them proven,
  that is a separate instrumented build with an artificially shrunk queue.
- **Phase 1–3 behaviour** is not here. There is no supervision-timeout clamp in this build, so
  valve disconnect detection is still ~5 s at the radio, and `rating` still walks
  `warning → critical` over 180 s rather than going straight to `critical`.
- **Notification policy** is unchanged. `device_offline` still fires on the same rule; the
  anti-spam work is Phase 3.
- **Long-term soak.** V9 is 30 minutes. A 72-hour coexistence soak (scope gate G2) belongs with
  Phase 1, when the supervision timeout actually changes.

---

## 4. If something fails

Capture and keep **both** streams plus the exact time of the stimulus. The single most useful
extra datum is the UART line immediately preceding the failure — most of these changes are about
ordering, and the ordering is only visible there.

For V6 or V7 failures, stop and report before continuing; both are safety-relevant and the rest
of the plan is not meaningful if either is broken.
