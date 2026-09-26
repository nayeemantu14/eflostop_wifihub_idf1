# Changelog — eFloStop II Wi-Fi Hub firmware

The firmware version lives only in `PROJECT_VER` (`CMakeLists.txt`). It is reported as `gateway.fw` on every
telemetry message and as `fw_version` in twin reported.

Wire-level detail for the entries below is in:
- `docs/telemetry/telemetry_messages.md`, which has a real example of every message, v5.0;
- the JSON schemas in `docs/telemetry/schemas/`;
- `C2D_COMMANDS.md`.

`docs/telemetry/validate_capture.py` checks an IoT Hub capture against this contract.

---

## 2.1.4 — 2026-09-25

This is a bug-fix and safety release on top of 2.1.3. It fixes the field defects found on 2.1.3, a group of
valve-safety defects found while analysing them, and the defects found in the release review. The root-cause
analysis is in `docs/field_logs/2.1.3/ROOT_CAUSE.md`.

The telemetry schema is still `eflostop.v2`. No key is renamed or removed, and no NVS data changes. Some values
and shapes are new, and parsers must accept them (see *Wire changes*).

### Fixed (2.1.3 field defects)

- **BUG-1: a valve with a flat battery was never rated critical, and an unknown battery read as 0 %.**
  - The valve now has its own battery bands:
    - ≤ 10 % is **critical**, with the reason "Valve battery critical";
    - 11–20 % is **warning**, with "Valve battery low";
    - above 20 % is excellent.

    A valve at 21–35 % used to read "good". These bands apply to the valve only: sensor battery bands are
    unchanged and never reach critical.
  - An unknown valve battery is `null` everywhere, never 0.
  - The valve state is `"unknown"` until the valve's characteristics have been read after a connect.
  - The battery is passed to the health engine on every read, not only when it changes, so a steady low
    reading is always rated.
- **BUG-2: removing one device wiped the health state of every other device.**
  - Removing a device used to reset the survivors' last-seen time, battery, RSSI and rating.
  - It also restarted the boot sync windows. As a result, "Boot sync: timeout" appeared 150 s after every
    removal, extra `boot`/`commission` snapshots were sent, and a genuinely offline sensor could be hidden for
    up to 10 minutes.
  - Now the device table is reconciled: survivors keep their state, only newly added devices get the sync
    window, and a removal arms no extra snapshot.
  - Removed devices are also cleared from:
    - the telemetry caches;
    - the rules engine's active-leak list. A removed wet sensor used to block `leak_reset` and make an
      override cancel or expiry re-close the valve.
- **BUG-3: a hub whose last device was removed published a stale snapshot.** That snapshot still showed the
  removed valve as open and connected, with "All devices healthy". The snapshot now shows only provisioned
  devices.
- **BUG-5: the valve block with no valve provisioned.** It was `{"state":"disconnected","connected":false}`,
  which looked like a real valve that had dropped off. It is now `{}`.
- **BUG-6: a hub with no devices went silent.**
  - Lifecycle, twin, alerts, the offline-buffer drain and every snapshot used to stop.
  - An empty hub now publishes like any other hub:
    - an `event` snapshot on the transition to empty;
    - `heartbeat` snapshots at the interval;
    - one `boot` snapshot per boot or MQTT (re)connect.
  - When the hub becomes empty, the rules engine's state (leak latch, override window) is reset, and so is the
    rules config (`auto_close_enabled` true, `trigger_mask` 7). Both are reset by the command that empties the
    hub, before its ack: the removal's own save writes the default config. A `provision` or `rules_config`
    sent straight after the removal is therefore never overwritten back to true / 7. A `provision` that itself
    leaves no device resets them too; rules keys in that payload apply on top of the defaults.

### Safety

- **P0-a: the hub linked to any nearby eFloStop valve.**
  - With no valve provisioned (or a different one), the hub matched valves by their advertised name and paired
    with the fixed passkey. Auto-close, `valve_open` / `valve_close` and the override paths could then close
    or open a neighbour's valve.
  - Valves are now matched by the provisioned MAC only. A connection to any other valve is dropped at connect,
    before pairing, discovery or any command.
  - With no valve provisioned, every valve command is refused.
  - The valve identity on the wire (rules events included) is always the provisioned MAC.
- **P0-b: a hub with sensors and no valve never started BLE,** so it never heard its BLE leak sensors. BLE now
  starts when a valve **or** at least one BLE sensor is provisioned.
- **P0-c: commands issued with no valve were replayed on the next valve that linked.**
  - They are now refused.
  - When the provisioned valve changes or is removed, every queued, pending or in-flight valve command is
    discarded, and a link still up to the old valve is disconnected.
  - When the valve is removed, a connect in progress is also cancelled. When it is replaced by another valve,
    a connect already in flight to the old one is not cancelled: it completes, and the hub then drops it at
    GAP CONNECT, before pairing, discovery or any command (as in P0-a).
- **N1: leak protection waited for Wi-Fi.**
  - Provisioning, the rules and health engines, and BLE used to start only after the first IP address, then
    up to 120 s of SNTP and the DPS retries.
  - They now start at boot, without Wi-Fi. SNTP runs from the main loop without blocking it. DPS also runs
    from the main loop, but a live registration still blocks it for up to 60 s per attempt (see Known
    limitations). With NTP blocked, the loop polls every 2 s only during the initial 120 s sync window, then
    returns to its normal 30 s cadence.
  - Leak protection now runs before the clock is set, so it can raise events before the first clock sync:
    a `leak_detected`, the `auto_close` that follows it, a health alert. The hub used to discard every such
    event, because its `ts` would have been seconds since power-on. It is now kept in the offline buffer. As
    soon as the clock first syncs, its `ts` is corrected from the hub uptime (the synced clock minus the uptime
    elapsed since the event's `gateway.uptime_s`) and rewritten in flash; it goes out after the first connect,
    in order. Once stamped, it survives a restart like any other buffered event.
  - A pre-sync event still unstamped when the hub restarts (a restart before the clock synced, a software
    reset included) is dropped at replay, because its time cannot be known: which slots hold this boot's
    pre-sync events is tracked in RAM only.
  - Snapshots and lifecycle are still not sent before the first sync; they are regenerated after connect.
- **The override window before the first clock sync.**
  - A window started before the clock synced (a valve long-press while the router is down) was stamped with
    an expiry in 1970. It ended the moment the clock synced, and with no internet it never expired at all.
  - It is now measured on the hub uptime until the clock syncs, then re-based to the synced clock and carried
    on (the rules tick does this within about 30 s of the sync). With no internet it expires after 24 h of
    uptime, through the normal expiry path (auto-close if a leak is still active, RMLEAK before CLOSE).
  - A window restored after a restart from an earlier unsynced power cycle cannot be re-based, because its
    elapsed time is unknown. Until the first sync it is timed from this boot's power-on, never beyond its
    stored expiry. At the first sync it ends, as in 2.1.3, which fails toward auto-close.
  - A window with a real expiry, restored after a power cut that lost the clock, used to block auto-close for
    as long as the site stayed without internet: its expiry could not be compared with anything. It is now
    timed from that power-on. It ends at its real expiry once the clock syncs, or 24 h after the power-on if
    the clock has still not synced by then, through the normal expiry path. A software reset in between does
    not restart that count. Meanwhile `override_remaining_s` and `previous_remaining_s` count down from the
    power-on, and `auto_close_blocked_override` carries `override_remaining_s` (it was omitted).
  - `water_access_override_enabled` omits `expires_ts` while the clock is unsynced (it used to carry the 1970
    instant). `override_remaining_s` (snapshot, `auto_close_blocked_override`) and `previous_remaining_s`
    (`auto_close_reenabled`) are measured on the uptime meanwhile. NVS keeps its keys and their meaning: the
    uptime basis is RAM only, and after the re-base `ovr_expiry` holds a real epoch.
- **N2/N3: the valve's first link-up could be lost.** The queue set is now complete before BLE starts, and every
  queue-set add is checked.
- **N4: packets from LoRa sensors that are not provisioned were evaluated by the rules engine.**
  - They are now checked against provisioning first.
  - A busy provisioning read counts as "provisioned", so a real leak is never dropped.
- **A hub that boots empty clears any persisted leak latch and override.** Without this, a valve provisioned
  later could be blocked from auto-closing for 24 h by an inferred override.
- **A leak on a hub with no provisioned valve no longer publishes `auto_close`.** There is no valve to close,
  and the event reported a shut-off that never happened. `leak_detected` still goes out and the incident still
  latches (when `auto_close_enabled` and the source's trigger bit are set). If provisioning is busy at that
  moment, the hub assumes a valve is present and publishes it. After a valve decommission, auto-close also no
  longer counts the old valve's link, still up until its DISCONNECT, as a reachable valve.
- **`valve_open` and `valve_set_state` open are refused while a leak incident is latched** and no override
  window is active, with the RMLEAK refusal, even when the valve is disconnected or its RMLEAK reads clear. An
  open sent while the valve was out of range used to be accepted, held, and written at the reconnect ahead of
  the close the leak was owed.
- **A close owed to an unreachable valve is held for its reconnect.** When a leak, an `override_cancel` or an
  override expiry with a leak still active needs the valve closed while it is out of range, the hub now queues
  RMLEAK and then CLOSE for the reconnect, replacing any open held for it. It used to start a scan only. If
  every source is dry before the reconnect, both are cancelled.
- **Replacing or removing the valve drops the old valve's leak reading.** A flood seen by the old valve used to
  survive the change: the new, dry valve was closed on its first link (a false `auto_close` with
  `cause:"reconnect"`), or the latched incident was read as a physical override and blocked auto-close for
  24 h. Now the old valve's leak source is forgotten, and when nothing else is wet the incident latch is
  released at once, with no `rmleak_auto_cleared` event and no RMLEAK write. With a sensor still wet the latch
  stays, and the new valve is closed on its first link.
- **A valve re-provisioned right after its decommission is always linked.** A `provision` naming a valve now
  always requests the valve link. One sent before the old link's DISCONNECT used to leave the hub not
  rescanning after the valve's next drop, so the valve went "Valve offline" until a leak or a valve command.
- **A leak that re-latches as the interlock is being released always re-locks the valve.** The rules tick runs
  before the report that woke `iothub_task` is handled, and the hub's RMLEAK cache reads clear only once the
  valve's read-back of the clear lands. A sensor reporting wet again in that moment (just as the auto-clear
  fired, or just after a `leak_reset`) latched a new incident but found the valve "closed with RMLEAK set" and
  did nothing. When the clear then landed, the hub read its own clear as a valve-button override
  (`RMLEAK cleared externally (valve override) — starting 24h override window`) and blocked auto-close for
  24 h while the sensor was wet. A newly latched incident now always re-asserts RMLEAK and CLOSE, queued
  behind the clear so the valve ends locked, and publishes `auto_close`. The race predates 2.1.4.
- **The hub's own RMLEAK clear is no longer read as a valve-button override.**
  - When the hub releases the interlock itself (the 10 s auto-clear, `leak_reset` or `override_enable`) while
    the valve is out of range or still setting up its link, the clear is held for the reconnect. At the
    reconnect the valve still reports its old RMLEAK for a moment, until the held clear is written and read
    back. The hub read that as "valve locked, no incident" and re-latched the incident. When its own clear
    then landed, it read it as a press of the valve button and started a false 24 h
    `water_access_override_enabled{trigger:"button"}` window that blocked auto-close.
  - The hub now remembers, in RAM, a clear it owes the valve until it reads RMLEAK clear back. At the
    reconnect it sends the clear again instead of re-latching, and the clear landing starts nothing. After a
    hub restart that memory is gone and the reconnect re-latches, as in 2.1.3 (fail closed); the 10 s
    auto-clear then releases it once every source is dry.
  - A live button override is now read only when the valve was seen with RMLEAK set during this incident and
    it then goes clear. A lock that never reached the valve (a close that never landed) no longer produces
    `trigger:"button"`.
  - `leak_reset` and the auto-clear queue their clear while they hold the rules lock. A re-latch on another
    task can therefore no longer queue its RMLEAK and close ahead of the clear, which then landed last and
    read as a button press.
  - The inference a reconnect makes from "valve open, RMLEAK clear" (SRS §4.4.2, a press while the hub was
    offline) is unchanged; see Known limitations.

### Changed

- **The RMLEAK interlock auto-clears 10 s after every leak source is dry (it was 30 s).**
  - `AUTO_CLEAR_TIMEOUT_MS` is 10 s. While an auto-clear is pending, `iothub_task` polls every 2 s instead of
    idling for up to 30 s, so the interlock is released about 10–12 s after the last leak source reports dry.
    It used to take 30–60 s: the 30 s dwell plus up to one 30 s idle wait.
  - Only RMLEAK is lifted. The valve never reopens by itself; `valve_open` or the valve button opens it.
  - `rmleak_auto_cleared` carries `clear_after_seconds:10`.
  - The amber "Leak interlock latched" floor after a leak dries now lasts about 10–12 s (it was 30–60 s), on
    the hub rating and on the fleet LED, which follows it.
  - A sensor that goes wet again 10–30 s after drying (up to about 60 s, counting the old idle wait) now
    produces a full clear and re-latch cycle (`rmleak_auto_cleared`, then `auto_close` again), where the old
    dwell used to hold the interlock. The valve stays closed throughout, including for a re-wet at the very
    moment of the clear (see Safety). That re-wet also sends `rmleak_auto_cleared` and then `auto_close`
    (see *Rules events* under Reliability).

### Reliability

- **Valve writes.**
  - A valve or RMLEAK write that the BLE stack refuses is retried up to 3 times, 200 ms and then 400 ms apart.
  - "GATT busy" is not a failure. NimBLE has 4 GATT procedures, and a command write that finds them all in use
    (`BLE_HS_ENOMEM`, rc=6) is retried every 250 ms for up to 5 s without using up one of the 3 attempts. Its
    read-back waits the same way, so the hub's RMLEAK cache is not left stale (a stale clear could read as a
    button override). If the pool is still full after 5 s, the command stays pending and is replayed on the
    same link; it never forces a reconnect. Before this, the RMLEAK and close queued right after a
    reconnect's own replays could find the pool full and force a reconnect, so the valve closed a leak one or
    more 15–40 s reconnect cycles late. A valve that stops answering is still dropped by NimBLE's 30 s GATT
    timeout.
  - After that, the link is dropped and the command is re-applied when the valve reconnects. It used to be
    lost.
  - Forced reconnects are capped at 3 in a row. After that a failed write stays pending for the next natural
    reconnect, and the cap is lifted once a live command write succeeds with nothing left pending (or the
    valve target changes). A command replayed at a reconnect no longer lifts it, so a valve that accepts the
    replays but refuses live writes still reaches the cap.
  - A re-applied command that fails again at reconnect is replayed ahead of any newer command, never after
    it, so it can no longer undo a newer command. A command cancelled or superseded meanwhile is not
    replayed. A newer command written during the reconnect is never overwritten by an older pending one.
  - RMLEAK is applied before the valve command, on every path. One exception, once the reconnect cap is
    reached (or the link could not be dropped): if the RMLEAK write itself keeps failing on a live link, an
    open waits behind it, but a close is still written, because holding a close back during a leak is worse.
  - That order also holds across the end of the link setup. A valve command queued while the link was
    finishing its setup could be written ahead of an RMLEAK command held for that link; it now waits behind
    it. While a held RMLEAK write waits for a busy GATT pool, a held close waits behind it too.
  - A disconnect the controller refuses (`BLE_GAP_EVENT_TERM_FAILURE`) no longer leaves valve commands
    blocked for the rest of the link.
  - A command held for the next link just as the current link finished its setup is replayed on that link at
    once. It could sit until the next reconnect.
  - BLE start-up (`nimble_port_init`) is retried up to 5 times, and each failed attempt now releases the
    NimBLE porting-layer memory it allocated.
  - BLE start requested by two tasks at once (a `provision` and the boot-time valve apply) starts the stack
    once: the start signal is claimed atomically, and the start-up task clears its handle before it exits.
- **Rules events.** A rules event raised by the rules tick (`rmleak_auto_cleared`,
  `water_access_override_expired`, or a button `water_access_override_enabled`) is now published straight
  after the tick, before the same pass handles a leak report. The rules engine holds one pending event, and a
  wet report handled in that pass replaced it with its `auto_close`, so the release never reached the cloud.
  A sensor that goes wet again just as the interlock auto-clears now gives `rmleak_auto_cleared`, then
  `leak_detected` and `auto_close`. The one exception is the pass in which MQTT (re)connects: its offline
  replay and lifecycle go first, and there the event can still be replaced as before. A multi-slot buffer is
  still deferred.
- **Health.**
  - A debounced alert is sent once the debounce has passed, instead of being dropped.
  - A change of the hub rating to, from or within warning and critical with no device event (for example the
    end of the sync window, or a valve battery going critical) publishes an `event` snapshot within seconds,
    instead of waiting for the next heartbeat. So does every valve battery edge. A change between excellent
    and good (a sensor's signal hovering near the weak threshold) waits for the heartbeat, as in 2.1.3.
  - Valve health events carry the MAC of the link they came from. An event from the old valve still queued
    when the valve is replaced is dropped, instead of rating the new valve (for example "Valve battery
    critical", which refuses `valve_open`, or healthy before it ever linked).
  - The valve re-check after a device-set change can raise the valve's leak in health but never clear it. A
    disconnect racing it could show a wet valve as dry for 180 s.
  - Check-ins from devices not in the health table (a sensor just removed, a neighbour's LoRa sensor) no
    longer trigger post-provision `prov_pkt` snapshots.
- **Snapshot.**
  - A snapshot is not published when the health table is busy or memory runs out. It used to publish empty
    device arrays, or a partial document. A snapshot missing any key because an allocation failed is not
    counted as sent either: it is rebuilt whole at the 5 s retry.
  - A snapshot that falls due in the same pass as a C2D `provision` or `decommission` waits one pass (a few
    milliseconds) for the device-set change, so it no longer shows the old device table after the `ok` ack.
- **Out of memory, events.** An event or lifecycle message whose `gateway` or `data` object cannot be
  allocated is dropped, and logged, instead of going out without it. A sub-object that fails to attach is
  freed instead of leaked.
- **Provisioning.**
  - Changes are transactional: a failed NVS save restores the previous configuration instead of leaving RAM and
    flash disagreeing.
  - Duplicate sensor ids in a `provision` payload are ignored.
  - Removing a valve from a hub that has none now reports an error.
  - Applying the provisioned valve to BLE reads and sets the valve target in one provisioning lock hold. A
    retry of an apply that found provisioning busy can therefore never re-target a valve that a C2D
    `provision` or `decommission` removed or replaced meanwhile.
  - A corrupt sensor count in flash is clamped to 16 at load (and logged) instead of overrunning the device
    lists, and every stored MAC string is terminated. Nothing is erased and no NVS data changes.
  - After a device-set change that adds devices, the BLE sensor scanner forgets what it last reported, so a
    sensor removed and re-added within its 10 s whitelist refresh is evaluated again. Re-added wet that way,
    it used to raise no `leak_detected` and no auto-close for up to 5 minutes, and the latch auto-cleared
    while it was still wet.
  - A telemetry-cache purge skipped because provisioning was busy is retried, and owed device-set work is
    polled every 2 s instead of every 30 s.
- **Sensor metadata** is copied out under its lock. Callers used to hold a pointer into the table after the lock
  was released.
- **Offline buffer.**
  - It is protected by a mutex.
  - An event too large for it is refused, instead of being cut into invalid JSON.
  - It is cleared after a decommission-all.
  - A pre-sync event that would grow past 512 B once time-stamped is not written back to flash stamped. It is
    stamped in RAM when it is sent, so every entry stays readable by 2.1.3 after a rollback. If the hub
    restarts between the clock sync and that send, such an event is dropped.
- **Twin reported** is refreshed after every device-set change. A decommission used to leave the twin naming the
  removed device.
- **The UART log no longer prints the site Wi-Fi password** (it came from the Wi-Fi provisioning component's
  INFO logging), **nor the valve's fixed BLE passkey** (it was printed at every BLE start and on pairing).

### Wire changes

Telemetry (`eflostop.v2`):

| Where | Up to 2.1.3 | 2.1.4 |
|---|---|---|
| snapshot `data.valve`, no valve provisioned | `{"state":"disconnected","connected":false}` | `{}` |
| snapshot `data.valve.valve_id` | the MAC of whichever valve was linked | always the provisioned valve's MAC; live fields only from a link to that MAC |
| snapshot `data.valve` before its readings are in | `battery` 0, cached state | `state` `"unknown"`, `battery` `null`, `fw_version` `null` |
| `battery` when unknown: snapshot valve, `valve_state_changed`, leak events | `0` | `null` |
| `system_health.reason` | "All devices healthy" on the one stale snapshot of an emptied hub; "Health data unavailable" on a busy health table | "No devices provisioned" (rating `excellent`); "Valve battery critical" (rating `critical`); "Health data unavailable" is no longer sent |
| hub with no devices | no lifecycle, twin or snapshots | lifecycle (`provisioned:false`), twin, and snapshots: `event` / `heartbeat` / `boot` |
| `device_offline` / `device_recovered` | sent on every non-leak critical edge, named from the rating alone; a debounced alert was dropped | reachability only: `device_offline` is always a lost link; no event for a battery- or leak-driven critical; `device_recovered` may carry `rating:"critical"` (back into a leak); `prev_rating` may equal `rating` (an alert sent after its debounce) |
| rules events `valve_id` | the linked valve's MAC while one was linked, else the provisioned one | always the provisioned valve's MAC |
| events raised before the first clock sync | discarded, never sent | sent after the first connect, in order, `ts` corrected from the hub uptime when the clock first syncs; one left by a restart before the sync is dropped. `ts` is never below 1704067200 |
| `water_access_override_enabled.expires_ts`, window started before the clock synced | an instant in 1970 | omitted (the expiry is about `ts` + `remaining_s`) |
| `override_remaining_s` / `previous_remaining_s`, window started before the clock synced | full duration, omitted or 0 | measured on the hub uptime; snapshots omit `expires_ts` until the window is re-based, within about 30 s of the sync |
| `override_remaining_s` / `previous_remaining_s`, window with a real expiry restored after a power-on that lost the clock | full duration (snapshot), omitted (`auto_close_blocked_override`) or 0 (`auto_close_reenabled`) | counts down from the power-on until the clock syncs; the snapshot omits `expires_ts` meanwhile |
| `water_access_override_expired`, same window | not sent until the clock synced | also sent about 24 h after the power-on if the clock has not synced by then |
| snapshot `data.valve.last_seen_age_s` | seconds since the valve's last changed value or connect, so it grew for hours on a steady, connected valve | `0` while the valve's link is up; seconds since the link dropped once disconnected; `null` if never seen this uptime |
| `device_offline.offline_duration_s`, valve | from the valve's last changed value, plus the 180 s grace: hours on a steady valve | from the link drop, so normally about 180 |
| `auto_close` on a hub with no provisioned valve | sent, with `rmleak_asserted:false` | not sent; `leak_detected` still is |
| `rmleak_auto_cleared.clear_after_seconds` | `30`; RMLEAK lifted 30–60 s after the last leak source read dry | `10`; RMLEAK lifted about 10–12 s after the last leak source reads dry |

Commands (`C2D_COMMANDS.md` §4.1–4.3 and §6.1). The `detail` strings are exact:

| Command | Up to 2.1.3 | 2.1.4 |
|---|---|---|
| `valve_open`, `valve_close`, `valve_set_state` with no valve provisioned | `ok`, and the hub then drove any nearby valve | `error`, "No valve is set up for this hub." |
| `valve_open`, `valve_set_state` open, with the last real valve battery ≤ 10 % | `ok` (the valve stayed shut) | `error`, "Valve battery critical (≤10 %): the valve will not open. Replace the batteries." |
| valve command when the valve command queue is full | `ok` | `error`, "The valve command could not be queued. Try again." |
| `decommission` `{"target":"valve"}` on a hub with no valve | `ok` | `error`, "valve decommission failed" |
| `valve_open`, `valve_set_state` open, while a leak incident is latched and no override window is active, with the valve disconnected or its RMLEAK clear | `ok`; the open was written (held for the reconnect when the valve was disconnected) | `error`, "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak." |

The RMLEAK refusal's text is unchanged, and it is still sent while the valve's RMLEAK is asserted. The refusal
checks run in this order: no valve, then RMLEAK asserted or an incident latched, then the battery, then the
queue. An `ok` from a valve command still means "queued"; the valve's own report (`valve_state_changed`, the
next snapshot) confirms it.

### Upgrade notes

- **OTA from 2.1.3 keeps provisioning.** There is no change to any NVS namespace, key or layout, nor to the
  partition table. The valve, sensors, sensor metadata, rules, hub name, DPS cache and snapshot interval all
  carry over.
- **Rolling back to 2.1.3 keeps provisioning too,** for the same reason. The 2.1.3 behaviour returns with it,
  including P0-a. A pre-sync event still in the offline buffer at the rollback and not yet time-stamped (the
  clock never synced, the entry was too close to 512 B to be stamped in flash, or its stamp could not be
  written) is replayed by 2.1.3 unchanged, with a `ts` in 1970; one already stamped keeps its real
  `ts`. 2.1.4 never writes a stamped entry back longer than 512 B, so 2.1.3 can read every entry. An override
  window re-based by 2.1.4 is stored with a real epoch, so 2.1.3 restores it with the right remaining time.
- **A hub that boots with no devices** clears any persisted leak latch and override window on that boot.
- **Cloud and app parsers must accept:**
  - `data.valve` equal to `{}`;
  - `battery: null` on the snapshot valve, `valve_state_changed` and leak events;
  - snapshots and lifecycle from a hub with no devices;
  - `device_recovered` with `rating:"critical"`;
  - `prev_rating` equal to `rating`;
  - the new valve-command error acks, and the RMLEAK refusal while a leak incident is latched;
  - `data.valve.last_seen_age_s` of `0` for a connected valve;
  - no `auto_close` from a hub with no valve: a leak there sends `leak_detected` only;
  - `water_access_override_expired` sent before the first clock sync, so it arrives late like any pre-sync
    event;
  - events that arrive late, after the first connect, with a `ts` earlier than that connect's lifecycle
    message (events raised before the first clock sync);
  - `water_access_override_enabled` without `expires_ts`, and a snapshot with `override_active:true` and no
    `expires_ts` for up to about 30 s after the first clock sync;
  - `data.valve.rmleak` and `data.valve.leak_state` reading `false` while `data.valve.state` is `"unknown"`
    (the link is up but the valve's readings are not in yet), even on a valve locked after a leak. Ignore
    both until the state is known;
  - lifecycle and twin reported with `provisioned:true` and no devices, from a hub that a `provision` emptied
    (a hub emptied by removals reports `false`).
- **Serial log.**
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
    - `RULES_ENGINE`: `RMLEAK clear read back - the hub's own clear, not a valve override`
    - `RULES_ENGINE`: `RECONNECT: RMLEAK clear not sent - no provisioned valve` (warning) and `RECONNECT:
      RMLEAK clear enqueue FAILED — valve interlock left set` (error)
    - `BLE_VALVE`: `[CMD] %s write: GATT busy - waiting` and `[CMD] %s read-back: GATT busy - waiting`, once
      per wait
    - `BLE_VALVE`: `[CMD] %s=%u kept pending - GATT still busy, replaying it`, after a 5 s wait
    - `BLE_VALVE`: `[CMD] %s=%u held behind the pending RMLEAK command`
    - `BLE_VALVE`: `[CMD] Pending valve command=0 kept behind the RMLEAK command (GATT busy)`

    `GATT busy - waiting` is normal when a reconnect replays commands while new ones are queued. None of
    these should repeat for long.
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
- **The NimBLE bond store** may still hold a bond to a neighbour's valve made under 2.1.3's name match. It is no
  longer used, and 2.1.4 does not delete it.
- **Not changed in 2.1.4:**
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
- **Known limitations.**
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
    notify path.
  - An override started before the clock synced and then restored after a software reset cannot be re-based,
    because its elapsed time is unknown, so it ends at the first clock sync, possibly hours early. That fails
    toward auto-close.
  - An override restored after a power cut with no internet ends 24 h after the power-on if the clock has
    not synced by then. That is later than its real expiry, since the hub cannot know how long the power was
    off.
  - Captive portal after a Wi-Fi reset: on a hub with a valve or a BLE sensor, BLE now starts at boot and
    runs beside the SoftAP portal, which therefore has less free heap than on 2.1.3.
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
  - Found in the final review and planned for 2.1.5:
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
