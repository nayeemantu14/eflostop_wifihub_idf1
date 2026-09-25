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

This is a bug-fix and safety release on top of 2.1.3. It fixes the field defects found on 2.1.3 and a group of
valve-safety defects found while analysing them. The root-cause analysis is in `docs/field_logs/2.1.3/ROOT_CAUSE.md`.

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
    rules config (`auto_close_enabled` true, `trigger_mask` 7).

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
    discarded. A link still up to the old valve is disconnected, and a connect in progress is cancelled.
- **N1: leak protection waited for Wi-Fi.**
  - Provisioning, the rules and health engines, and BLE used to start only after the first IP address, then
    up to 120 s of SNTP and the DPS retries.
  - They now start at boot, without Wi-Fi. SNTP and DPS run from the main loop without blocking it.
- **N2/N3: the valve's first link-up could be lost.** The queue set is now complete before BLE starts, and every
  queue-set add is checked.
- **N4: packets from LoRa sensors that are not provisioned were evaluated by the rules engine.**
  - They are now checked against provisioning first.
  - A busy provisioning read counts as "provisioned", so a real leak is never dropped.
- **A hub that boots empty clears any persisted leak latch and override.** Without this, a valve provisioned
  later could be blocked from auto-closing for 24 h by an inferred override.

### Reliability

- **Valve writes.**
  - A valve or RMLEAK write that the BLE stack refuses is retried up to 3 times, 200 ms and then 400 ms apart.
  - After that, the link is dropped and the command is re-applied when the valve reconnects. It used to be
    lost.
  - RMLEAK is always applied before the valve command.
  - BLE start-up (`nimble_port_init`) is retried up to 5 times.
- **Health.**
  - A debounced alert is sent once the debounce has passed, instead of being dropped.
  - A rating change with no device event (for example the end of the sync window, or a valve battery going
    critical) publishes an `event` snapshot within seconds, instead of waiting for the next heartbeat.
- **Snapshot.** A snapshot is not published when the health table is busy or memory runs out. It used to publish
  empty device arrays, or a partial document.
- **Provisioning.**
  - Changes are transactional: a failed NVS save restores the previous configuration instead of leaving RAM and
    flash disagreeing.
  - Duplicate sensor ids in a `provision` payload are ignored.
  - Removing a valve from a hub that has none now reports an error.
- **Sensor metadata** is copied out under its lock. Callers used to hold a pointer into the table after the lock
  was released.
- **Offline buffer.**
  - It is protected by a mutex.
  - An event too large for it is refused, instead of being cut into invalid JSON.
  - It is cleared after a decommission-all.
- **Twin reported** is refreshed after every device-set change. A decommission used to leave the twin naming the
  removed device.
- **The UART log no longer prints the site Wi-Fi password** (it came from the Wi-Fi provisioning component's
  INFO logging).

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

Commands (`C2D_COMMANDS.md` §4.1–4.3 and §6.1). The `detail` strings are exact:

| Command | Up to 2.1.3 | 2.1.4 |
|---|---|---|
| `valve_open`, `valve_close`, `valve_set_state` with no valve provisioned | `ok`, and the hub then drove any nearby valve | `error`, "No valve is set up for this hub." |
| `valve_open`, `valve_set_state` open, with the last real valve battery ≤ 10 % | `ok` (the valve stayed shut) | `error`, "Valve battery critical (≤10 %): the valve will not open. Replace the batteries." |
| valve command when the valve command queue is full | `ok` | `error`, "The valve command could not be queued. Try again." |
| `decommission` `{"target":"valve"}` on a hub with no valve | `ok` | `error`, "valve decommission failed" |

The RMLEAK refusal of `valve_open` is unchanged. An `ok` from a valve command still means "queued"; the
valve's own report (`valve_state_changed`, the next snapshot) confirms it.

### Upgrade notes

- **OTA from 2.1.3 keeps provisioning.** There is no change to any NVS namespace, key or layout, nor to the
  partition table. The valve, sensors, sensor metadata, rules, hub name, DPS cache and snapshot interval all
  carry over.
- **Rolling back to 2.1.3 keeps provisioning too,** for the same reason. The 2.1.3 behaviour returns with it,
  including P0-a.
- **A hub that boots with no devices** clears any persisted leak latch and override window on that boot.
- **Cloud and app parsers must accept:**
  - `data.valve` equal to `{}`;
  - `battery: null` on the snapshot valve, `valve_state_changed` and leak events;
  - snapshots and lifecycle from a hub with no devices;
  - `device_recovered` with `rating:"critical"`;
  - `prev_rating` equal to `rating`;
  - the new valve-command error acks.
- **Serial log.**
  - Unchanged: every line the production tool and the bench scripts match.
  - A refused valve command now logs its full reason: `VALVE_OPEN refused — <detail>`, and likewise for
    `VALVE_CLOSE` and `VALVE_SET_STATE open|closed`. It used to print `... — valve RMLEAK is asserted`.
- **The NimBLE bond store** may still hold a bond to a neighbour's valve made under 2.1.3's name match. It is no
  longer used, and 2.1.4 does not delete it.
- **Not changed in 2.1.4:**
  - On a hub with sensors and no valve, a leak still publishes `auto_close` (with `rmleak_asserted:false`), as in
    2.1.3, although there is no valve to close.
  - Deferred to a later release:
    - the legacy keyword scan of C2D payloads;
    - command-id de-duplication;
    - the single-slot rules-event buffer;
    - more than 16 simultaneous leak sources;
    - sensors going unheard during a valve connect attempt;
    - LoRa driver hardening;
    - removing the valve bond on decommission.
