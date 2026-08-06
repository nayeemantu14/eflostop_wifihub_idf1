# Test plan — `snapshot_interval_s` persistence and twin reconciliation (FW 2.0.2)

Scope: the Device Twin desired property `snapshot_interval_s` only. Nothing in the rules
engine, provisioning or leak path is touched by this change, so the existing matrix is
unaffected — run this alongside it, not instead of it.

## What changed, and therefore what has to be proved

| # | Defect before | Change |
|---|---|---|
| 1 | Interval lived only in RAM — every reboot silently reverted to 300 s while the twin still advertised the operator's value | Persisted to NVS (`nvs_prov` partition, namespace `telemetry`, key `snap_int`) |
| 2 | Hub never fetched the twin. IoT Hub pushes a desired PATCH only on **change**, so nothing ever reconciled a rebooted hub | `$iothub/twin/GET` issued once per connection, on the SUBACK for `$iothub/twin/res/#` |
| 3 | Not echoed in reported — the app could not read back what was applied | `reported.snapshot_interval_s` carries the value **in force**, unconditionally |
| 4 | Out-of-range silently ignored, with no feedback anywhere | Rejected in the setter (one rule for all callers), previous value kept, reported still published so the app sees the mismatch |

Incidental fix found while implementing: the `MQTT_EVENT_DATA` router was gated on
`data_len > 0`, which discarded the **empty-bodied** ack of our own reported PATCH before
any routing ran. That is why `Twin response:` never appeared in any bench log despite the
twin working. The twin/res branch now runs before the length test.

## Setup

```bash
HUB=<your-iot-hub-name>
DEV=GW-50787D0D96DC

# terminal 1 — cloud
az iot hub monitor-events -n $HUB -d $DEV --props all -t 0

# terminal 2 — device
idf.py -p COMx monitor          # 115200
```

Read/write the twin:

```bash
# write
az iot hub device-twin update -n $HUB -d $DEV --desired '{"snapshot_interval_s": 900}'

# read back what the hub actually applied
az iot hub device-twin show -n $HUB -d $DEV \
   --query "{desired:properties.desired.snapshot_interval_s, reported:properties.reported.snapshot_interval_s}"
```

### UART lines that carry the evidence

| Grep | Meaning |
|---|---|
| `Snapshot interval %ds restored from NVS` | persisted value loaded at boot (**test 1**) |
| `Snapshot interval %ds (default, none stored)` | nothing persisted — first boot or post-decommission |
| `Init: schema=eflostop.v2 interval=%ds` | the value the scheduler actually started on |
| `Twin GET requested (rid=%d)` | GET published after SUBACK (**test 2**) |
| `Twin response: $iothub/twin/res/200/... (N bytes)` | Azure answered |
| `Twin GET: applying desired properties from full document` | the reconciliation ran |
| `Snapshot interval set to %ds (persisted)` | accepted **and** written to flash |
| `Snapshot interval %ds rejected — outside [60..3600]` | validation fired (**test 4**) |
| `Twin document truncated: ... NOT applied` | **must never appear** — see S11 |
| `SNAP heartbeat=reset interval_ms=%d` | scheduler latched the new cadence |
| `Telemetry settings cleared` | decommission wiped it |

---

## Tests

### S1 — Default, nothing stored
**Do:** decommission (or flash a blank hub), boot with no `snapshot_interval_s` in desired.
**Pass:** UART `Snapshot interval 300s (default, none stored)` then `Init: ... interval=300s`.
`reported.snapshot_interval_s == 300`. Heartbeat snapshots 300 s apart.

### S2 — Twin GET happens on every connection **(load-bearing)**
**Do:** boot, watch the connect sequence. Then force a reconnect (drop Wi-Fi ~30 s, restore).
**Pass:** on **each** connect, in this order:
`Connected to Azure IoT Hub!` → `Twin GET requested (rid=N)` → `Twin response: $iothub/twin/res/200/?$rid=N (…bytes)` → `Twin GET: applying desired properties from full document`.
Exactly one GET per connection — `rid` must not repeat within a connection.
**Fail if:** no `Twin response:` line. That means the response topic is not live and every
other test in this plan is passing for the wrong reason.

### S3 — Live apply, and the pending deadline is re-aimed
**Do:** with the hub connected and idle, note the uptime of the last `SNAP trigger=heartbeat`.
Then set desired to `60`.
**Pass:**
`Twin desired patch:` → `Twin: snapshot_interval_s = 60` →
`Snapshot interval set to 60s (persisted)` →
`SNAP heartbeat=re-aimed interval_ms=60000 next_in_ms=<N>`.

The re-aim line is the one that matters. The new deadline is measured from the **last
confirmed publish**, not from now — so if the last heartbeat was 200 s ago and you set 60 s,
the next one is immediately due and publishes at once. If it was 20 s ago, it lands ~40 s
later. Either way the *following* heartbeats are 60 s apart.
`reported.snapshot_interval_s == 60` within a few seconds. No reboot required.

**Why this test grew a clause (found on the first bench run):** `snap_rearm_heartbeat()` runs
only after a successful publish, so before this fix a mid-cycle change left the deadline
already pending untouched. Going 300 → 900 was harmless (one more snapshot at the old
spacing), but going 3600 → 60 meant waiting up to an hour before the faster cadence began.
The re-aim only fires when the pending snapshot is the heartbeat — an EVENT deadline pulled
in by a leak is never pushed back to serve a cadence change — and is skipped while a
publish-failure backoff is active.

### S3b — An event deadline is not disturbed by a cadence change
**Do:** wet the valve probe and, within the ~300 ms event window, change the interval.
Easier variant: set the interval repeatedly while leak events are flowing.
**Pass:** no `SNAP heartbeat=re-aimed` line appears while an `event:` snapshot is pending, and
the event snapshot still publishes on its own schedule (`SNAP trigger=event:…`).

### S4 — Survives a reboot **(the headline fix)**
**Do:** leave desired at `60` from S3. Power-cycle the hub.
**Pass:** `Snapshot interval 60s restored from NVS` appears **before** `Connected to Azure
IoT Hub!` — i.e. the value is in force from the first scheduler tick, not corrected later
by the twin GET. `Init: ... interval=60s`. First heartbeat after boot is at 60 s.

### S5 — Survives a reboot with the cloud unreachable **(discriminating)**
**Do:** power off your router (or move the hub out of Wi-Fi range). Power-cycle the hub.
**Pass:** `Snapshot interval 60s restored from NVS` still appears. Restore Wi-Fi afterwards.
**Why this test exists:** S4 alone cannot distinguish "restored from NVS" from "corrected by
the twin GET a second later". This one can — it is the only proof that defect 1 is fixed
independently of defect 2.

### S6 — Twin GET reconciles a change made while the hub was offline **(the case that used to fail)**
**Do:** power the hub **off**. With it off, set desired to `1800`. Power it back on.
**Pass:** boot restores 60 from NVS, then the twin GET applies 1800:
`Twin GET: applying desired properties…` → `Snapshot interval set to 1800s (persisted)` →
`SNAP heartbeat=reset interval_ms=1800000`. `reported.snapshot_interval_s == 1800`.
**Why:** IoT Hub sends no PATCH for a change made while the device was disconnected. Before
this release the hub would have run 300 s (or 60 s) forever while the twin said 1800.

### S7 — Out-of-range rejected, and visibly so
**Do:** set desired to `30`. Then to `7200`. Then back to `900`.
**Pass:** for each bad value, `Snapshot interval 30s rejected — outside [60..3600], keeping 1800s`
and `Twin: snapshot_interval_s 30 rejected — reported will show 1800`.
`reported.snapshot_interval_s` stays at the previous value while `desired` shows the bad one —
that mismatch is the app's only signal, since desired properties produce no `cmd_ack`.
Power-cycle after a rejection and confirm the **old** value is still in NVS (nothing bad was written).

### S8 — Repeated identical values do not burn flash
**Do:** with desired fixed at `900`, force five reconnects (drop/restore Wi-Fi).
**Pass:** each connect logs `Twin GET: applying desired properties…`, but
`Snapshot interval set to 900s (persisted)` appears **zero** times after the first.
**Why:** every twin GET re-delivers the full document. An unconditional write here would cost
one flash erase cycle per reconnect, forever.

### S9 — Wi-Fi reset does **not** clear it
**Do:** with 900 stored, do the 10 s button hold (Wi-Fi-only reset), re-enter credentials via
the captive portal.
**Pass:** `Snapshot interval 900s restored from NVS`. The setting lives in `nvs_prov`, which the
button does not touch.

### S10 — Decommission **does** clear it
**Do:** `decommission` with target `all`.
**Pass:** `Telemetry settings cleared — snapshot interval back to 300s`. After the automatic
reboot, `Snapshot interval 300s (default, none stored)` and `reported.snapshot_interval_s == 300`.

### S11 — Twin document fits the MQTT receive buffer **(risk)**
**Do:** read the `(N bytes)` figure on the `Twin response:` line at every connect.
**Pass:** `Twin document truncated:` never appears.
**Why:** `CONFIG_MQTT_BUFFER_SIZE` is the 1024-byte default and a twin GET response carries
desired + reported **plus `$metadata` for both**, which is easily 2–3× the properties alone.
If it ever truncates, the desired properties are silently not applied — the exact failure
this change removes. The firmware now logs it as an error rather than failing quietly; if you
see it, raise the buffer and re-run S2/S6.

### S12 — Cadence accuracy regression
**Do:** set `60`, leave idle 10 minutes.
**Pass:** ~10 heartbeat snapshots, intervals within a few hundred ms of 60000 ms.
`SNAP clamped by min-interval` may appear if events coincide — that is the 5 s floor doing
its job, not a failure.

### S13 — Event snapshots still work at a short interval
**Do:** with `60` set, wet the valve probe.
**Pass:** the event snapshot still publishes promptly (`SNAP trigger=event:…`) and the
heartbeat cadence is unaffected. No more than ~12 snapshots in any one minute.

---

## Sign-off

| Test | Result | Notes |
|---|---|---|
| S1 default | PASS | bench 2026-08-05, uptime 108 s |
| S2 twin GET per connection | PASS | **load-bearing** — both boots (rid=1, rid=2); GET 50 ms after connect |
| S3 live apply + re-aim | PASS | new build `62215a64a`. Four re-aims, all arithmetically exact: 300→1800 (next_in 1775809, fired 1828532), 1800→60 (next_in −461260 → immediate), 60→900, 900→60 (next_in 29930, fired 2894892). |
| S3b event deadline undisturbed | NOT EXERCISED | no interval change was made while an event snapshot was pending |
| S4 survives reboot | PASS | **headline fix** — `restored from NVS` at 9672 ms, MQTT connect at 10202 ms |
| S5 survives reboot, cloud down | PASS (by ordering) | the scheduler started on 900 s 530 ms BEFORE MQTT connected, so the value cannot have come from the cloud. Router-off variant not needed. |
| S6 offline change reconciled | PASS | **the case that used to fail** — NVS gave 900 at 6152 ms, twin GET reconciled to 1800 at 6792 ms |
| S7 out-of-range rejected | PASS | twin read 2026-08-05 10:12: `desired: 30` / `reported: 1800` — rejected, previous value kept in RAM and flash |
| S8 no redundant flash writes | PASS | twin GET re-delivered 900; no `(persisted)` line followed — change-gate held |
| S9 Wi-Fi reset preserves | PASS | 10 s hold → `restored from NVS` 1800 s after reboot; `nvs_prov` untouched as designed |
| S10 decommission clears | PASS | `Telemetry settings cleared — back to 300s`, then `300s (default, none stored)` after the reboot |
| S11 twin fits buffer | PASS | 323 → 364 → 362 B of 1024 across three boots; +41 B per desired property |
| S12 cadence accuracy | PASS | 4 × 300010 ms, then 7 × 900000 ms over ~1.8 h. No drift, no missed beat. |
| S13 event snapshots at 60 s | PASS | 24 event snapshots interleaved with 60 s heartbeats; peak 4 per 60 s window (limit ~12); one `clamped by min-interval: +1483 ms` — the 5 s floor working |

**Minimum to call the fix proven: S2, S4, S5, S6.** S5 and S6 are the two that distinguish
the two independent defects; passing S4 alone does not tell you which mechanism did the work.

## Known limitation, not a defect

A value written to desired while the hub is connected arrives as a PATCH and applies within a
second. A value written while it is disconnected applies on the next connection (S6). There is
no queue and no retry — IoT Hub holds the desired document, and the hub reads it on connect.
