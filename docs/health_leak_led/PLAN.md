# FW 2.1.2 — auto_close storm, leak-aware system health, fleet LED semantics

Status: **implemented, awaiting bench validation** (see `TEST_PLAN.md`).
Version: `CMakeLists.txt` `PROJECT_VER` 2.1.1 → **2.1.2**.

---

## Why

Two field captures from hub `GW-7C4FADAE69C8` running 2.1.1 — an Azure IoT Hub message log and the
matching UART log — exposed one runaway plus a cluster of health/indicator defects.

The headline: **a single wet sensor put the hub into an infinite auto-close loop** that published an
`auto_close` event to Azure every 1–2 s indefinitely, committed NVS at the same rate, and re-wrote
the valve over GATT each pass. Separately, **system health had no notion of leak at all**: the hub
reported `system_health: {"rating":"excellent","reason":"All devices healthy"}` while a sensor
reported `leak_state:true` and the valve sat closed, and the fleet LED stayed GREEN throughout.

---

## Defects

### D1 — `auto_close cause:reconnect` storm (critical, runaway)

A self-sustaining feedback cycle:

```
rules_engine_on_valve_connected()   [Priority 1: active_leak_count > 0]
  ├─ publish auto_close {"cause":"reconnect", active_leak_count:1}
  ├─ ble_valve_set_rmleak(true)
  └─ ble_valve_close()
        → write_valve_command(0) → ble_gattc_read(..., on_read_cb)
              → on_read_cb → setup_next_step() → setup_step 10→11→12…
                    → default: → notify_hub_update(BLE_UPD_CONNECTED)
                          → iothub dequeues type=5 → back to the top, forever
```

`on_read_cb()` called `setup_next_step()` unconditionally. The post-write read-backs added in
`752621d` / `5accc8a` reused that callback, so every command write bumped the file-static
`setup_step` past its terminal value of 10 into the `default:` arm, which re-ran the
"SETUP COMPLETE" block and re-posted `BLE_UPD_CONNECTED`.

Evidence — UART log: `[SETUP] Step 11` (line 631), `Step 12` (line 676), each immediately followed by
a re-printed `SETUP COMPLETE` / `[READY]` banner and a fresh `VALVE RECONNECT RECONCILIATION`. 35+
`auto_close cause:reconnect` events between t=731 s and t=970 s **with no intervening
`BLE_UPD_DISCONNECTED`** — the decisive tell, since a genuine reconnect must be preceded by a
disconnect.

Aggravating factor: `rules_engine_on_valve_connected()` Priority 1 had **neither** guard that the
equivalent action in `rules_engine_evaluate_leak()` has — no `valve_state==0 && rmleak_already`
idempotence check, no cooldown, and it never wrote `g_last_auto_close_tick`. It also called
`incident_save_to_nvs()` every pass: ~40 commits/minute on the `rules_eng` namespace.

> HEAD (`5accc8a`) made this **worse**, not better: a second read-back (RMLEAK) took the loop gain
> from 1 to 2 `CONNECTED` per cycle, which would eventually saturate the 16-deep `ble_update_queue`.

### D2 — `valve.rmleak` never read true

The valve does not echo `REMOTE_LEAK` from its own write handler, and 2.1.1 had no read-back, so
`g_val_rmleak` stayed at the value `BLE_GAP_EVENT_CONNECT` zeroed it to. Every snapshot reported
`"rmleak": false` while the paired event reported `"rmleak_asserted": true`. The cache half is fixed
by `5accc8a`'s read-back; `rmleak_asserted` remains "the write was issued" — see the decision record.

### D3 — `system_health` ignored leak entirely

`health_engine.c` had zero leak inputs; `compute_sensor_rating()` keyed only off staleness, battery
and RSSI. Field evidence (IoT log 3:30:19 → 3:31:02): sensor `00:80:E1:2A:B6:8E` `leak_state:true`,
valve `closed`, `system_health` = `excellent` / `"All devices healthy"` for 45 s. The leaking sensor's
own `rating` also stayed `excellent`. Previously acknowledged as a known gap in
`docs/telemetry/telemetry_catalogue.md:1145`.

### D4 — fleet LED stayed GREEN through an active leak

`fleet_led.c` escalated to RED only on a **valve** condition
(`ble_valve_get_leak() || ble_valve_get_rmleak_state()`). A wet BLE sensor with a dry valve probe
never turned it red — and because of D2, `g_val_rmleak` was stuck false, so even the valve path could
not fire. UART: last transition was `rating=good color=GREEN` at 867 s; the leak ran 928–1003 s with
no transition at all.

### D5 — false "N sensors offline" → CRITICAL/RED for minutes after boot

Two compounding causes.

**(a) Health liveness was gated by the telemetry delta filter.** `health_post_ble_leak_checkin()` sat
*below* the delta/heartbeat gate in `app_ble_leak.c`, gated by `BLE_LEAK_HEARTBEAT_MS = 5 min` — even
though the scanner *hears* a dry sensor roughly every 100 s
(`docs/commission_snapshot/FEATURE_TRACKER.md:75`). Against `HEALTH_BLE_LEAK_TIMEOUT_MS = 600 s` that
is ~1.5–2× margin, so **one missed 5-minute post window marked a healthy sensor offline.** Measured
posts in the field log were 300–400 s apart (sensor `29:FC` at 57.9 / 357.9 / 658.8 s).

**(b) Never-heard devices were rated CRITICAL and counted in the roll-up.**
`health_engine_reload_devices()` seeds every device `HEALTH_CRITICAL`, and `recalc_system_rating()`
was a plain `max()` with no regard for the boot-sync window. `HEALTH_BOOT_SYNC_TIMEOUT_MS = 120 s` was
*shorter* than the 300 s post gate, so `Boot sync: timeout (120 s)` was guaranteed (UART:458). Field
result: RED from 17.5 s to 867 s — **14 minutes on a healthy hub.**

---

## Target behaviour

| Condition (highest precedence first) | Rating | LED |
|---|---|---|
| Nothing provisioned | — | WHITE |
| **Any** leak — sensor or valve probe — *regardless of the 24 h override window* | CRITICAL | RED |
| A device that has been heard and is offline | CRITICAL | RED |
| Hub still holding the valve closed (latched incident) | WARNING | YELLOW |
| Low battery / weak signal | WARNING | YELLOW |
| Provisioned but not yet heard, boot window open | (excellent) | WHITE "syncing" |
| All good | EXCELLENT / GOOD | GREEN |

A leak incident therefore reads **RED while wet → YELLOW while dry-but-interlocked → GREEN once
resolved** (30 s all-dry auto-clear, `LEAK_RESET`, or a physical override).

---

## Implementation

### 1. Break the read-back → setup-state-machine coupling (D1) — three independent cuts

`main/ble_valve/app_ble_valve.c`
- `on_read_cb()` split into `handle_read_result()` (folds the value into the cache via `on_notify()`,
  handles the auth-error retry, returns whether the caller may advance) plus two thin callbacks:
  - `on_read_cb()` — **setup chain only** (steps 5–8): advances `setup_next_step()`.
  - `on_cmd_read_cb()` — **command read-backs**: updates the cache and stops.
  The four command read-back sites (`apply_pending_valve_cmd_if_any`,
  `apply_pending_rmleak_cmd_if_any`, `write_valve_command`, `write_rmleak_command`) now use
  `on_cmd_read_cb`. **This alone kills the loop.**
- `setup_next_step()`'s `default:` arm returns early when `BLE_STATE_BIT_DISCOVERY_DONE` is already
  set, so no future path can re-announce a live link. `clear_all_state_bits()` runs on every
  `BLE_GAP_EVENT_CONNECT`, so a genuine reconnect is unaffected.

`main/rules_engine/rules_engine.c`
- Priority 1 gained the idempotence guard: `valve_state == 0 && valve_rmleak` → latch, stamp, return.
- Priority 1 rate-limits **the event only, never the writes** — via a dedicated
  `g_last_reconnect_close_tick`. See the decision record for why both of those matter.

### 2. Leak as a first-class health input (D3, D4)

- `health_device_t` and `health_device_status_t` gained `bool leaking`.
- `compute_sensor_rating()` and `compute_valve_rating()` check `dev->leaking` **first**, above the
  staleness test, so a sensor that reported wet and then went quiet stays CRITICAL-because-leaking
  rather than being reclassified as merely offline.
- New `HEALTH_EVT_VALVE_LEAK` + `health_post_valve_leak()` for the valve's flood characteristic
  (it notifies independently of battery and connect, so it has no check-in to ride along on).
- `health_post_lora_checkin()` / `health_post_ble_leak_checkin()` each gained a trailing `bool
  leaking` — the wet/dry bit arrives in the same packet/advertisement, so this costs nothing and
  avoids doubling the queue traffic.
- Producers: `app_ble_leak.c`, `app_lora.cpp` (`packet.leakStatus != 0`), `app_ble_valve.c`
  `on_notify()` flood branch.
- `fleet_led.c`: the valve-only leak special case is **deleted**. Leak is now an input to the rating
  for every device type, restoring the module to the pure-reader contract its own header promises.

Nothing in the health engine consults the override window — which is exactly how "a leak is critical
even under an active override" falls out for free.

### 3. YELLOW floor while the hub holds the valve closed

- `health_engine.c`: `static volatile bool s_interlock_held` + `health_set_interlock_held()` /
  `health_is_interlock_held()`. `recalc_system_rating()` raises `worst` to `HEALTH_WARNING` when set.
  The setter posts `HEALTH_EVT_TICK` so the roll-up refreshes immediately rather than up to 30 s later.
- `rules_engine.c`: the mirror is called from **inside `incident_save_to_nvs()`**. Verified that the
  12 `incident_save_to_nvs()` call sites are exactly the 12 `g_leak_incident_active = …` mutation
  sites, so one insertion point cannot miss a transition. Plus one explicit push in
  `rules_engine_init()` after `incident_load_from_nvs()`, so a latch that survived a reboot shows
  amber from the first LED evaluation.
- `incident_save_to_nvs()` also gained a write-skip cache (`g_incident_persisted`), reset in
  `rules_engine_clear_persistent_state()`.

### 4. Honest liveness + quiet boot (D5)

- `app_ble_leak.c`: `health_post_ble_leak_checkin()` hoisted **above** the delta gate, with a new
  per-sensor `HEALTH_CHECKIN_MIN_MS` (5 s) so a multi-packet burst posts once. Health now sees every
  burst (~100 s) → ~6× margin against the 600 s timeout. **Telemetry gating is untouched — no change
  to what goes on the wire.**
- `recalc_system_rating()` skips `in_use && !ever_seen` while `!s_boot_sync_done`. The per-device
  rating stays CRITICAL (honest; the snapshot still reports `connected:false`), and once the window
  closes the device counts again, so a genuinely absent device is still escalated.
- `check_boot_sync_locked()` calls `recalc_system_rating()` on the false→true edge — **required**,
  because the flag now changes the roll-up's inputs and that function is reached from the fleet LED /
  iothub poll, which does not otherwise recalc.
- `HEALTH_BOOT_SYNC_TIMEOUT_MS` 120 s → **180 s** (one burst cycle + margin). The first snapshot does
  not depend on this window — the `SNAP_FAST` escape hatch (valve-ready or a 150 s ceiling) fired at
  36 s in the field log.
- `HEALTH_BLE_LEAK_TIMEOUT_MS` deliberately left at 600 s: with the gate removed the margin is now
  correct, and genuine dead-sensor detection stays at 10 minutes.
- `fleet_led.c` precedence, ordered so a real CRITICAL can never be masked by "syncing":
  `total == 0` → `rating >= WARNING` → `seen < total && !sync_complete` → GREEN.
- `telemetry_v2.c` `build_system_health_reason()`: leak bucket **first**, then an interlock cause part,
  then the existing connectivity/battery/signal parts, then a trailing informational
  `"syncing N devices"`. Takes `sync_done` as a parameter, sampled **before** the rating.

### 5. Minor cleanups
- `fleet_led.h` documented `WARNING -> ORANGE` where the code renders YELLOW — corrected, and the
  whole precedence table rewritten.
- `app_lora.cpp` now uses `LED_CMD_LORA_PULSE` instead of a bare `'G'` literal (the constant existed
  but was never referenced by the producer — a silent coupling).
- `reset_button.c`: the `gpio_install_isr_service` handling was **already correct**; the
  `E (862) gpio: ... already installed` line is emitted by the IDF driver itself before it returns
  the error, and cannot be suppressed from our side. Comment added so nobody re-investigates.

---

## Decision record

**Post-leak, water dry, valve still held closed → YELLOW** (user decision). Considered and rejected:
straight back to GREEN (a homeowner seeing green with no water is being told the wrong thing), and
staying RED (indistinguishable from an active leak). Amber is the honest middle state.

**Boot, devices not yet heard → WHITE "syncing"** (user decision). Considered and rejected: optimistic
GREEN (hides a dead sensor for the whole window) and keeping RED (the defect being fixed).
`system_health.rating` keeps the existing four-value vocabulary — "syncing" is carried by the reason
string, because adding a rating value would break cloud consumers.

**The interlock floor keys off the hub's `g_leak_incident_active`, not the valve's RMLEAK.** The hub
latch is the thing that is persisted and reconciled; if the two diverge,
`rules_engine_on_valve_connected()` Priority 2 re-latches and they converge.

**Priority 1 rate-limits the event, never the writes.** `evaluate_leak()` may skip the close on
cooldown because it only runs on a fresh leak report. Priority 1 must not: `evaluate_leak()` stamps
`g_last_auto_close_tick` *before* it checks connectivity, so a leak found while the valve was offline
arrives at Priority 1 already inside the cooldown. Gating the close there would mean a wet sensor and
an **open valve** — strictly worse than the storm. Both writes are idempotent at the valve, so running
them unconditionally is free.

**The reconnect cooldown uses its own stamp** (`g_last_reconnect_close_tick`), not
`g_last_auto_close_tick`. Sharing it would suppress the event on the *first* reconnect after an
offline leak — the one reconnect event that actually carries new information.

**`rmleak_asserted` still means "the write was issued"**, matching `build_auto_close_telemetry()`,
which is handed the same once-sampled link state. It deliberately does *not* report
`ble_valve_get_rmleak_state()`: that is the pre-write cache at that point (the write happens after the
mutex is released), so it would read `false` on exactly the pass that asserts the interlock. Whether
the interlock took is carried by `valve.rmleak` in the next snapshot, fed by the read-back. Changing
the semantics would also desynchronise the two `auto_close` variants.

**`s_interlock_held` is a lock-free `volatile bool`, not mutex-protected.** The writer is already
holding the rules-engine mutex; taking `s_mutex` there would create the one lock ordering the health
engine has otherwise avoided. Verified `rules_engine.c` contains no `health_*` call, so no cycle
exists in either direction.

---

## Bugs found and fixed during self-review

Recorded because each was introduced by this change and would have shipped:

1. **Stale roll-up when the boot window closes.** `check_boot_sync_locked()` set `s_boot_sync_done`
   without re-rolling, and the new exclusion rule made the roll-up depend on that flag. A window that
   timed out with a genuinely absent sensor left `s_system_rating` at its stale EXCELLENT until the
   next 30 s tick: callers saw "sync complete" and "everything excellent" simultaneously → **GREEN
   for up to 30 s on a hub missing a device.** Fixed by recalculating on the transition edge.
2. **Shared cooldown stamp.** The reconnect event initially reused `g_last_auto_close_tick`, which
   `evaluate_leak()` stamps at leak detection — suppressing the first (most informative) reconnect
   event. Fixed with a dedicated stamp.
3. **Rating/reason generation skew.** `build_system_health_reason()` called
   `health_is_boot_sync_complete()`, which can close the window and re-roll *after* the rating had
   been read — publishing `rating:excellent` next to `reason:"All devices healthy"` for a hub that had
   just become critical. Fixed by sampling `sync_done` before the rating and passing it in.
4. **Interlock cause masked by the syncing part.** With the interlock held during the boot window the
   reason read `"syncing 5 devices"` instead of naming the interlock. Fixed by adding
   `health_is_interlock_held()` and emitting the interlock as an explicit part rather than inferring
   it from the absence of any other cause.

Also folded in: while syncing, never-heard devices are skipped from the cause buckets so the same
device is not described twice in contradictory terms ("1 sensor offline" *and* "syncing 1 device");
and the new wire strings are ASCII-only, matching the rest of the `reason` vocabulary.

## Bugs found by the adversarial review council (second round)

Six independent reviewers plus a two-lens refutation pass over the diff. 32 findings, heavily
convergent — five lenses independently found (1) below and four found (2). All of the following were
verified against the code and fixed:

1. **The 5 s health check-in throttle swallowed the wet/dry edge** (high). `HEALTH_CHECKIN_MIN_MS` was
   introduced as a *liveness* decimator, but because `leaking` now rides on the same helper it also
   gated the dry→wet transition: a sensor bursting dry at t0 and wetted at t0+1.5 s emits its entire
   wet burst inside the window, so the health engine kept `leaking == false` and the roll-up kept
   reporting excellent/GREEN **for up to a full burst cycle (~100 s)** — reintroducing the exact
   false-green this release exists to remove. Now the edge bypasses the throttle
   (`leak != s->last_leak`, which still holds the previous value at that point).
2. **The `DISCOVERY_DONE` idempotence guard could strand a valve close** (high). `start_discovery_chain()`
   resets `setup_step` but never cleared `DISCOVERY_DONE`, so a re-entered discovery on an already
   set-up link would run the whole chain and then be silently abandoned at the `default:` arm — leaving
   `g_setup_in_progress` stuck true (which suppresses *every* `notify_hub_update`, so the hub goes deaf
   to valve state) and stranding any pending CLOSE/RMLEAK. Reachable **because of this change**: routing
   command read-backs through `handle_read_result()` lets an auth-error on a post-setup read re-enter
   `initiate_security()`. Fixed at the root — `start_discovery_chain()` now clears the bit, which also
   stops `is_ready_for_gatt()` claiming a usable session while the handles are zeroed.
3. **A valve dropping while wet left health permanently "leaking"** (medium). The disconnect handler
   zeroed `g_val_leak` locally but never told the health engine, so `compute_valve_rating()` — which
   checks `leaking` first — reported CRITICAL-because-leaking instead of CRITICAL-because-offline:
   an unclearable "Leak detected: valve", and no `device_offline` alert (no rating transition to fire
   on). Now posts `health_post_valve_leak(false)` on disconnect.
4. **A table reload forgot a wet valve probe** (medium). `health_engine_reload_devices()` zeroes
   `leaking`, and the valve only re-notifies the flood characteristic on a *change*, so a probe already
   wet was forgotten until it dried and re-wetted. `reseed_valve_health_if_connected()` now re-feeds it
   alongside the battery.
5. **A wet valve probe could read excellent/WHITE during the boot window** (medium). The `!ever_seen`
   roll-up exclusion had no leak exemption, and `handle_valve_leak()` deliberately does not touch
   `ever_seen`. A leaking device is now never excluded.
6. **A connected valve was never re-rated** (high). `evaluate_timeouts()` skipped it unless
   `disconnect_ms > 0`, so a dropped `leaking`/battery queue post — or a rating the alert queue refused
   to commit — hid a wet flood probe for the whole episode, with no self-healing path. Now re-rated
   every tick like every other device.
7. **A leak published `device_offline` for a demonstrably online device** (medium). `health_alert_to_json()`
   names the event purely from the rating, which was sound while CRITICAL could only mean staleness.
   The alert is now suppressed for leak-driven CRITICAL (the leak has its own event, snapshot, rating and
   reason), with `crit_is_leak` remembering why, so the matching `device_recovered` stays silent too.
   Suppressing rather than adding a `device_leak` value keeps the wire vocabulary unchanged.
8. **Decommission left the interlock floor raised** (medium). `rules_engine_clear_persistent_state()`
   erased the NVS key but left `g_leak_incident_active` true, so a zero-device hub still reported
   warning with a leak-interlock reason. Now clears the RAM latch and the floor.
9. **The interlock reason claimed the valve was closed when it might be open** (medium). The latch is
   also set when an active override *blocked* the close. The string is now "Leak interlock latched".
10. **A wet *and* unreachable valve stopped reporting that the hub cannot close it** (low). The leak
    bucket's `continue` suppressed the valve's connectivity part. Now falls through for a valve that is
    also disconnected — but not for a connected one, which the branch below would have mislabelled
    "Valve battery low".
11. **A cloud-supplied sensor label could forge an extra cause** (medium). `reason` is a comma-joined
    list and the label comes from provisioning, so `"kitchen, Valve offline"` would read as a real
    cause. `leak_label_for()` now copies into a caller buffer (also removing the
    dereference-after-lookup lifetime question) and maps commas to semicolons.
12. **The NVS write-skip cache advanced on a failed write** (low), which would have permanently stopped
    retrying and silently lost the latch across a reboot. Return codes are now checked.
13. **`fleet_led` sampled the rating before the sync flag** (low) — the inverse of the order
    `telemetry_v2` deliberately adopted, giving one wrong frame and a spurious GREEN transition.
14. **`reason[128]` truncated the last cause mid-word** at six parts (low) → 192.
15. **`health_get_system_rating()`'s documented contract was stale** (low): it is no longer
    `max(per-device rating)` in either direction. Documented.

Left alone deliberately: the invalid-JSON `provision` example at `C2D_COMMANDS.md:562` is in a file that
was already modified before this work started and is not part of this change.

## The most serious defect found: a valve probe already wet at link-up (fixed)

Found while preparing the round-2 review, and worse than the storm this release set out to fix.

`rules_engine_evaluate_leak(LEAK_SOURCE_VALVE, …)` is reached from exactly one place —
`app_iothub.c:2251`, guarded by `ble_upd_type == BLE_UPD_LEAK`. And `on_notify()` suppresses
`notify_hub_update(BLE_UPD_LEAK)` while `g_setup_in_progress` is true. Setup step 6 reads the flood
characteristic, so `g_val_leak` is correctly populated during setup — but the event that would make anyone
*act* on it is swallowed, and `rules_engine_on_valve_connected()` Priority 1 keys off
`g_active_leak_count`, which this leak was never entered into.

So a probe that was already wet **before** the BLE link came up produced no dry→wet edge for anything
downstream. The valve only ever closed on an edge observed after setup completed.

Both halves are pre-existing (the suppression and the single evaluation site predate 2.1.2). What is new
is the asymmetry: because 2.1.2 posts `health_post_valve_leak()` unconditionally, the hub now reports
`system_health: critical` and drives the fleet LED **red** for this state — while leaving the valve
**open**. Silence was bad; a red light next to an open valve is worse, because it looks like the system
noticed and acted.

Realistic triggers, both field-plausible:
- the hub reboots into an existing flood at the valve;
- the valve power-cycles or drops and re-links while its probe is wet.

Fix: the `default:` arm of `setup_next_step()` now announces `BLE_UPD_LEAK` when `g_val_leak` is true at
link-up, after `BLE_UPD_CONNECTED` so reconciliation runs first. This routes the catch-up through the
existing, already-tested `BLE_UPD_LEAK` path, which samples the valve state consistently before calling the
rules engine, rather than adding a second evaluation site. The setup-time suppression itself is left alone —
it is correct not to emit events mid-chain; what was missing was the catch-up once the link is up.

Covered by **T7b** in `TEST_PLAN.md`, which is the only test in the plan that exercises a path where the hub
could report a leak and still leave the water running.

## Round-2 adversarial review council

Six lenses over the round-1 output, then a two-lens refutation pass on the de-duplicated set: 26 raw
findings → 14 unique → 6 survived. Convergence was again the signal — **7 lenses independently reported
the same one-line omission**, 4 the `crit_is_leak` edge/state error, 3 the reload wipe. Fixed:

**R2-1 (high, 3 lenses, both refuters confirmed) — a provisioning reload wiped every sensor's `leaking`
bit, making the round-1 leak exemption inert.** `health_engine_reload_devices()` memsets the table; only
the valve was re-seeded. So a `provision` or per-device `decommission` issued while a sensor was standing
in water dropped that sensor out of the roll-up until its next burst (~100 s BLE, longer for LoRa) — and
because the same memset clears `ever_seen`, the new `!ever_seen && !leaking` exclusion then skipped it,
with `leaking` being precisely the bit just zeroed. With `auto_close_enabled:false` there is no interlock
floor either, so the hub published `rating:"excellent"` / `"Syncing - waiting for 4 devices"` with a wet
sensor on the floor: D3/D4 exactly, reintroduced by the fix meant to prevent it. Pre-diff this scenario
showed RED (the old fleet LED had a valve-leak override and the roll-up had no exclusion), so this was a
genuine regression, not a pre-existing gap. Fixed by carrying `leaking`/`crit_is_leak` across the memset
and restoring them onto devices that are still provisioned — self-contained, rather than depending on
another module's cache surviving and being walked.

**R2-2 (medium, 7 lenses) — `build_system_health_reason()` lacked the roll-up's `!leaking` exemption.**
`recalc_system_rating()` exempts leaking devices from the not-yet-heard exclusion, so a wet device can set
the rating to critical while `ever_seen` is still false — the valve especially, since `handle_valve_leak()`
deliberately does not touch `ever_seen`. The reason builder skipped it, so the snapshot could publish
`rating:"critical"` next to `reason:"syncing N devices"`, naming no leak at all. The two predicates are now
identical, and leaking devices are excluded from the `unheard` tally so the same device is not described
twice.

**R2-3 (high) — round-1's own Fix 3 was wrong and is reverted.** Posting `health_post_valve_leak(false)` on
disconnect demoted a *known* flood at the valve from CRITICAL to the WARNING disconnect grace for a full
3 minutes. Losing the link does not dry the floor. Both objections that motivated it are already answered
elsewhere: the reason builder now names both causes for a leaking+disconnected valve (R1-10), and setup
step 6 re-reads the probe on every reconnect so it is not unclearable. Accepted cost, documented in the
code: no `device_offline` alert fires for that transition (the rating was already CRITICAL), but
`valve.connected:false` and the "Valve offline" clause still carry it on the wire. Under-reporting an alert
beats under-reporting an active flood.

**R2-4 (high) — a full leak queue silently consumed the dry→wet delta.** `process_leak_adv()` committed
`s->last_leak` *before* `xQueueSend()` and ignored its return, so when `ble_leak_rx_queue` was full the
event was discarded and every later advertisement in the burst compared equal — no retry.
`rules_engine_evaluate_leak()`, the only thing that closes the valve for a sensor leak, would not run until
the 5-minute telemetry heartbeat forced an event through. Meanwhile the health check-in (hoisted above the
gate, and not queued behind this) already had the LED red: the indicator right, the valve open. Tracked
state is now committed only after a successful send.

**R2-5 (medium, 4 lenses) — `crit_is_leak` was edge-driven.** Set only on the into-critical edge, it was
never set when a leak arrived at a device already CRITICAL (seeded after a reload, or genuinely offline),
so the eventual dry-out published a bare `device_recovered` for a `device_offline` that was never sent. Now
maintained from the state in `apply_rating()` on every commit.

**R2-6 (medium) — an offline-but-wet sensor contradicted itself in one snapshot**, emitting
`leak_state:false` in its array entry while `system_health.reason` said `Leak detected: <its label>` — and
the safer of the two values was the one an array reader would take. Both `else` branches now report
`health[i].leaking`. Field stays boolean, schema unchanged.

Also fixed, found while implementing the above (my own T7b fix, twice over): the at-link-up flood
announcement republished `leak_detected` on every reconnect with no delta gate; and gating it naively left
`s_valve_pub_wet` stuck at 1 after a valve reconnected dry, which would have suppressed the *next* genuine
valve leak entirely. Resolved by announcing both wet and dry at link-up and letting one consumer-side delta
gate arbitrate (`s_valve_pub_wet`, initialised to 0 so a routine dry link-up is silent). This also closes a
pre-existing hole: a valve that reconnects dry after a wet episode now publishes `leak_cleared` and tells
the rules engine to drop itself from the active-leak table.

Refuted and correctly left alone (6): claims about the lock-free `s_interlock_held` being unsound for a
bool, the fleet LED's 4 Hz mutex contention, and four others. Not verified (below the refutation cap):
documentation drift in `docs/telemetry/` — the catalogue's O-3/O-4 invariants and `snapshot.schema.json`
still describe the pre-2.1.2 contract. Real but cosmetic; tracked below.

## Known remaining work (not blocking a bench test)

- `docs/telemetry/telemetry_catalogue.md` invariants O-3 ("per-sensor critical implies not connected") and
  O-4 (the `max()` roll-up), plus the "does NOT track leak state" note at :1145 and the generated
  `field_registry.csv` / `schemas/snapshot.schema.json`, all describe pre-2.1.2 semantics. The generator
  inputs are `docs/telemetry/catalogue_options.py` and `fields.json`. Needs a regeneration pass.
- The reason vocabulary gained five new strings (`Leak detected: <label>`, `N leaks detected`,
  `Leak interlock latched`, `Syncing - waiting for N device(s)`, `syncing N devices`). Any cloud consumer
  matching on the previous fixed set needs updating.
- `C2D_COMMANDS.md:562` — the `provision` example is missing three commas and is invalid JSON. Pre-existing,
  in a file already modified before this work.

---

## Out of scope

- **D6 lifecycle-on-reconnect** — the hub re-publishes `lifecycle:online` with
  `reset_reason:"power_on"` on every MQTT reconnect (IoT log 3:24:33, `uptime_s:581`, no reboot), so
  the cloud cannot distinguish a reboot from a reconnect. Deferred by decision.
- **Twin `valve_mac:null` / `valve_device_id:null`** — left in place. These are read by the cloud,
  which cannot be verified from this repo; removing them is a wire-contract change, not a cleanup.
- **D7 / D8** — investigate-only, see `NOTES_D7_D8.md`.
