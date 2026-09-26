# eFloStop II Wi-Fi Hub — Telemetry Message Catalogue

*Every message the hub can send, as a real example — v5.0*

> GENERATED FILE — produced by `docs/telemetry/build_messages.py` from `messages_data.py`.

| | |
|---|---|
| Document version | 5.0 (supersedes v2.0, which documented firmware 1.9.0) |
| Firmware version | 2.1.4 — `CMakeLists.txt:12` |
| Git commit | `a5a07eab6d0ef82481e2b9453cb9f951a7ff221f` |
| Schema | `eflostop.v2` |
| Topic | `devices/<device_id>/messages/events/` (QoS 1) |
| Message count | 48 distinct messages across 8 families |

This document lists every real telemetry message the eFloStop II Wi-Fi Hub can publish to Azure
IoT Hub. Each entry is a complete message exactly as it appears on the wire: the same keys, in the same order,
that the firmware serializer produces for that situation.

Read these as messages you could have captured, not as templates. The example identifiers, names and readings
are realistic and consistent throughout — one hub, one valve and two leak sensors across the whole document —
so an installation can be followed from message to message. The identifiers and readings are examples; the key
sets, the key order and every enum value are exact.

All of these arrive on one topic, `devices/<device_id>/messages/events/`, at QoS 1. Three envelope `type`
values carry everything: `lifecycle`, `snapshot` and `event`. Within `event`, the `data.event` field tells you
which of the shapes below you have received.

Three things to know before you build a parser. **A conditional key is omitted, not sent as null** — except
inside the two sensor arrays, where every key is always present and unknown values are `null`, and for
`battery` on the snapshot valve, `valve_state_changed` and the leak events, which is `null` when unknown.
**`data.valve` can be `{}`**: a hub with no valve provisioned sends an empty object, not a disconnected valve.
And in a snapshot, a sensor that has gone silent keeps its last `battery`, `rssi` and `leak_state`;
`connected` and `last_seen_age_s` tell you they are stale (S3).

**Changes in firmware 2.0.0.** Firmware 1.9.0 made water detection a single event family; 2.0.0 finishes the
job on naming. Two things changed, and there is no compatibility shim on the telemetry plane — every hub runs
the new shape.

1. **The identity key is named for the device type.** A valve reports `valve_id`; a leak sensor reports
   `sensor_id`. This replaces the single `device_id` that 1.9.0 used everywhere. It applies to the snapshot's
   `data.valve` object and both sensor arrays, to `valve_state_changed`, and to the leak family — where the
   key is `sensor_id` on K1–K4 and `valve_id` on K5–K6. The lifecycle message's `data.valve_device_id` and
   the twin's `valve_device_id` are both now `valve_id`. (2.0.0 left `device_id` in place on the hub-generated
   events; 2.1.0 finished the rename — see below.)

   The key always sits in the **same position** — third in an event's `data`, immediately after
   `source_type`; first in a snapshot device object — and always carries the same string form. So a consumer
   switches on `source_type` and reads the matching key; nothing else about the shape moves.

2. **Where a message names a device type, the key is always `data.source_type`.** Health events called it
   `data.dev_type` up to 1.9.0, which made the same concept a third name alongside outbound `source_type` and
   inbound `sensor_type`. The vocabulary is unchanged and still exactly three values: `valve`,
   `ble_leak_sensor`, `lora`, and no value outside that set is ever emitted — the reconnect variant of
   `auto_close` (R3) used to put `"reconnect"` in `source_type`, and now carries `data.cause` instead.

   Not every message names a device: the override-window events (R5, R6, R7) describe a hub-wide policy
   window rather than a device, and carry no `source_type` and no identity. Treat `source_type` as
   "present on any message that concerns a specific device", not as universal.

**Changes in firmware 2.0.1.** A follow-up release fixing three things bench testing exposed. `leak_detected`
now reaches the cloud **before** the `auto_close` it caused (the order was inverted); the `rmleak` value on a
valve leak event is now sampled deterministically at detection rather than racing the interlock write; and
`rmleak_cleared` / `rmleak_auto_cleared` (R8, R9) now carry an identity — previously they arrived with no
identity at all and could not be attributed to any device.

**Changes in firmware 2.1.0 — `device_id` is gone.** Up to 2.0.2, three families kept a generic `device_id`
while everything else had moved to `valve_id` / `sensor_id`: the health alerts (`device_offline`,
`device_recovered`), the `auto_close` family (including `auto_close_blocked_override`) and the RMLEAK interlock
events (`rmleak_cleared`, `rmleak_auto_cleared`). The reasoning was that the hub raises these *about* a device
rather than the device reporting itself. That distinction described how the firmware is built, not anything a
consumer can act on, and it cost you a second lookup rule and a third spelling of one concept.

They now use the same key as everything else: **`valve_id` when `data.source_type` is `valve`, `sensor_id`
otherwise**, in the same position and the same string form. `device_id` no longer appears anywhere on the
telemetry plane. One rule now answers "which device does this message concern?" for every message the hub
sends — read `valve_id` or `sensor_id`, whichever is present.

The two RMLEAK events still deliberately carry NO `source_type`, because the incident they close may have been
latched by a sensor while the interlock itself is always the valve's. That costs nothing now: they carry
`valve_id`, and the key names the device type on its own.

On a hub provisioned with sensors but **no valve**, the two RMLEAK events omit `valve_id` entirely rather than
naming a valve that does not exist — so treat it as optional there. `auto_close` also omits `valve_id` rather than
emit a placeholder when no MAC resolves, and since 2.1.4 a hub with no valve provisioned sends no `auto_close` at
all.

**Changes in firmware 2.1.4 — the empty hub, the missing valve and the unknown battery.** A bug-fix release
for defects found in the field on 2.1.3. Nothing is renamed; several shapes you may not have seen before now
appear, and every one of them replaces a message that stated something false.

1. **A hub with no devices keeps talking.** Up to 2.1.3 a hub whose last device was removed sent one
   snapshot that still showed the removed valve, then went silent. It now publishes lifecycle (L4,
   `provisioned:false`), twin and snapshots (S8): `data.valve` is `{}`, both sensor arrays are `[]`, and
   `system_health` is `excellent` / `"No devices provisioned"`.

2. **`data.valve` is the provisioned valve, or `{}`.** A hub with sensors and no valve sends `{}` (S9). It
   used to send `{"state":"disconnected","connected":false}`, which looked like a real valve that had dropped
   off. When a valve is provisioned, `valve_id` is always its MAC, and live readings come only from a link
   to that exact valve.

3. **An unknown battery is `null`, never 0.** On the snapshot valve, `valve_state_changed` and every leak
   event (S10, V6, K7). During the seconds between the valve's link coming up and its readings arriving,
   the snapshot says `state:"unknown"` and `battery:null` rather than a default that reads as an empty
   battery.

4. **The valve's battery can make it critical.** At or below 10 % the valve is rated `critical` with the
   reason `"Valve battery critical"` (S11); 11–20 % is `warning` / `"Valve battery low"`. This band is the
   valve's only: a sensor's low battery never reaches critical. No health event is sent for it; the
   snapshot that carries it follows within seconds. `valve_open` is refused while it lasts (C6).

Health events now report reachability only: `device_offline` is always a lost link, a debounced alert is
sent late instead of dropped, and `device_recovered` can carry `rating:"critical"` when the device came back
wet (H4). On the command plane, `valve_open` / `valve_close` / `valve_set_state` now ack `error` when there
is no valve (C5), at a critical battery (C6), or when the command could not be queued — see `C2D_COMMANDS.md`.
`valve_open` is also refused with the RMLEAK detail while the hub has a leak incident latched and no override
window, even with the valve disconnected (C3).

A hub with no valve provisioned no longer sends `auto_close`: a leak there sends `leak_detected` only (R1). The
valve's `last_seen_age_s` is `0` while its link is up and counts from the drop once it is disconnected (S1, S2),
and a valve `device_offline` measures `offline_duration_s` from the drop (H3). The RMLEAK interlock now
auto-clears 10 s after every source is dry (it was 30 s), about 10-12 s after the last dry report, so
`rmleak_auto_cleared` carries `clear_after_seconds:10` (R9).

**Events raised before the hub's clock syncs now arrive, late.** Leak protection runs from power-up, before
Wi-Fi. An event raised before the first clock sync (say a leak while the router is still coming back after a
power cut) used to be thrown away. It is now held in the hub's offline buffer, given its real `ts` (worked out from
the hub uptime) as soon as the clock syncs, and sent after the first connect, in order. A held event left over
from a restart before the clock synced is dropped, because its time can no longer be known. Snapshots and lifecycle are still not
sent before the sync; they are rebuilt after connect. `ts` is therefore never earlier than 2024 on any message.
An override window started before the sync sends `water_access_override_enabled` without `expires_ts` (R10).

**Identifiers are always UPPERCASE.** A BLE MAC is rendered `AA:BB:CC:DD:EE:FF` — colon-separated, upper
case — and a LoRa id `0x1A2B3C4D`, on every message and every plane, regardless of the case the device was
commissioned in. Before 1.9.0 a sensor commissioned in lower case appeared lower case in snapshot arrays and
upper case in events: the same physical device under two values of the key you join on. The hub normalises on
the way out, so a single exact-match join is safe. Matching case-insensitively anyway costs nothing and is
still the more defensive choice.

**One inbound key changed too.** The `provision` command's `valve_mac` is now `valve_id`, matching what the
hub reports back. `valve_mac` is still accepted as a deprecated fallback so existing app and production-tool
builds keep commissioning; it logs a warning and will be removed. Every other inbound key is unchanged — the
C2D `sensor_meta` payload still uses `sensor_type` and `sensor_id`.

## What changed since v2.0

Document **v2.0** described firmware **1.9.0**; this is **v5.0**, describing firmware **2.1.4**. v2.0 of the `.docx` is kept unchanged so the two can be read side by side. Every row below is a breaking change — there is no compatibility shim on the telemetry plane.

The table is cumulative, so a reader holding any earlier revision can use it. Rows are annotated with the firmware release that introduced them; v3.0 (firmware 2.0.0) and v4.0 (firmware 2.1.0) are the interim revisions between v2.0 and this one; anything marked 2.1.4 is new since v4.0.

| | v2.0 — firmware 1.9.0 | v5.0 — firmware 2.1.4 |
|---|---|---|
| Identity key, snapshot valve object | `data.valve.device_id` | `data.valve.valve_id` |
| Identity key, snapshot sensor arrays | `data.lora_sensors[].device_id, data.ble_leak_sensors[].device_id` | `data.lora_sensors[].sensor_id, data.ble_leak_sensors[].sensor_id` |
| Identity key, leak events — sensors (K1-K4) | `data.device_id` | `data.sensor_id` |
| Identity key, leak events — valve (K5, K6) | `data.device_id` | `data.valve_id` |
| Identity key, valve_state_changed | `data.device_id` | `data.valve_id` |
| Identity key, health events (H1-H3) | `data.device_id` | `data.valve_id or data.sensor_id, per data.source_type  (2.1.0)` |
| Identity key, auto_close / rules events (R1-R4, R8, R9) | `data.device_id` | `data.valve_id or data.sensor_id, per data.source_type  (2.1.0)` |
| Identity key, lifecycle | `data.valve_device_id` | `data.valve_id` |
| Identity key, twin reported | `valve_device_id` | `valve_id` |
| Device-type key name | `data.source_type on leak/rules events, but data.dev_type on health events` | `data.source_type on every outbound message; dev_type is gone` |
| Reconnect variant of auto_close (R3) | `data.source_type "reconnect" — a value outside the device-type vocabulary` | `data.cause "reconnect"; the event carries no source_type at all` |
| Inbound provision, valve identifier | `payload.valve_mac` | `payload.valve_id  (valve_mac still accepted as a deprecated fallback)` |
| Inbound decommission, target matching | `"ble" case-insensitive, but "valve" / "lora" / "all" case-SENSITIVE` | `all targets case-insensitive` |
| Order of leak_detected vs auto_close | `auto_close published ~40 ms BEFORE the leak_detected that caused it` | `leak_detected first, then auto_close, then the snapshot  (2.0.1)` |
| rmleak on a valve leak event | `raced the interlock write — false or true depending on GATT timing` | `sampled once at detection; always the pre-interlock value  (2.0.1)` |
| Heartbeat cadence | `300 s, fixed in practice: the twin could set it but the value was lost on every reboot, so hubs effectively always ran 300 s` | `300 s default, tunable 60-3600 s per hub and PERSISTED across reboots. Read reported.snapshot_interval_s; do not assume 300  (2.0.2)` |
| rmleak_cleared / rmleak_auto_cleared | `no source_type, no identity — unattributable` | `valve_id names the valve; still no source_type  (2.0.1/2.0.2, key renamed 2.1.0)` |
| auto_close rmleak_asserted | `hardcoded true, even when the valve was unreachable and nothing was sent` | `reflects whether the RMLEAK+close writes were actually issued  (2.0.2)` |
| Placeholder device ids | `auto_close could ship the identity "valve" — a phantom device matching nothing` | `the key is omitted when no MAC resolves  (2.0.2)` |
| Generic device_id, anywhere on the wire | `the only identity key — every message used it` | `GONE. Every outbound message names its device with valve_id or sensor_id  (2.1.0)` |
| Snapshot valve object, no valve provisioned | `{"state":"disconnected","connected":false} — looked like a real valve that dropped off` | `{} (empty object)  (2.1.4)` |
| Snapshot valve object, which valve | `whatever valve the hub was linked to, even one being removed or a neighbour's` | `the PROVISIONED valve only: valve_id is its MAC; live data only from a link to that MAC  (2.1.4)` |
| Valve battery when unknown (snapshot, valve_state_changed, leak events) | `0 — indistinguishable from an empty battery` | `null  (2.1.4)` |
| Snapshot valve before its readings are in | `the link's defaults: battery 0 and whatever state byte was cached` | `state "unknown", battery null, fw_version null  (2.1.4)` |
| Valve battery rating | `shared sensor bands: <= 20 % warning, no critical band at all` | `<= 10 % critical ("Valve battery critical"), 11-20 % warning — valve only  (2.1.4)` |
| Hub with no devices | `one stale snapshot, then silence: no lifecycle, twin, snapshot or events` | `lifecycle (provisioned:false), twin, and snapshots with valve {}, [] arrays and "No devices provisioned"  (2.1.4)` |
| Health alerts (device_offline / device_recovered) | `any non-leak critical, battery included; a debounced alert was dropped` | `reachability only; a debounced alert is sent late (prev_rating may equal rating); device_recovered may be critical  (2.1.4)` |
| valve_open / valve_close / valve_set_state acks | `ok unless RMLEAK — even with no valve, at a critical battery, or when nothing was queued` | `error: "No valve is set up for this hub.", battery critical (open), "The valve command could not be queued. Try again."; the RMLEAK refusal (open) also while a leak incident is latched, valve disconnected or not  (2.1.4)` |
| auto_close on a hub with no valve provisioned | `sent, with rmleak_asserted false, for a valve that does not exist` | `not sent; leak_detected still is  (2.1.4)` |
| Snapshot valve last_seen_age_s | `seconds since the valve's last changed value or connect: hundreds of seconds on a steady, connected valve` | `0 while the link is up; seconds since the drop once disconnected  (2.1.4)` |
| Valve device_offline offline_duration_s | `from the valve's last changed value, plus the 180 s grace: hours on a steady valve` | `from the link drop, so normally about 180  (2.1.4)` |
| Events raised before the hub's first clock sync | `destroyed: a leak_detected or auto_close raised while the router was still down never reached the cloud` | `held in the offline buffer, ts worked out from the hub uptime when the clock first syncs, sent after the first connect; one left over from a restart before the sync is dropped  (2.1.4)` |
| rmleak_auto_cleared clear_after_seconds (R9) | `30: RMLEAK lifted 30-60 s after the last source read dry` | `10: RMLEAK lifted about 10-12 s after the last source reads dry; the valve still never reopens by itself  (2.1.4)` |
| water_access_override_enabled, override started before the clock synced (R10) | `expires_ts an instant in 1970, and the window ended the moment the clock synced` | `expires_ts omitted; the window runs on the hub uptime until the clock syncs, then carries on  (2.1.4)` |
| Firmware version | `1.9.0` | `2.1.4 — breaking changes on both the telemetry and command planes` |

## Index of messages

| ID | `type` | `data.event` | Message |
|---|---|---|---|
| L1 | `lifecycle` | `online` | Hub connected, fully commissioned |
| L2 | `lifecycle` | `online` | Hub commissioned, but with no devices |
| L3 | `lifecycle` | `online` | Hub reconnected after a crash |
| L4 | `lifecycle` | `online` | Hub with no devices (never provisioned, or every device removed) |
| S1 | `snapshot` | `—` | Routine heartbeat, everything healthy |
| S2 | `snapshot` | `—` | Heartbeat with the valve disconnected |
| S3 | `snapshot` | `—` | Heartbeat with a sensor gone silent |
| S4 | `snapshot` | `—` | Snapshot while a water-access override is running |
| S5 | `snapshot` | `—` | First snapshot after boot |
| S6 | `snapshot` | `—` | Early snapshot as soon as the valve is ready |
| S7 | `snapshot` | `—` | Final snapshot before decommissioning |
| S8 | `snapshot` | `—` | Hub with no devices |
| S9 | `snapshot` | `—` | Hub with sensors and no valve |
| S10 | `snapshot` | `—` | Valve linked, but its readings not in yet |
| S11 | `snapshot` | `—` | Valve battery critical |
| V1 | `event` | `valve_state_changed` | Valve opened |
| V2 | `event` | `valve_state_changed` | Valve closed and locked after a leak |
| V5 | `event` | `valve_state_changed` | Valve opened, firmware revision unknown |
| V6 | `event` | `valve_state_changed` | Valve moved before its battery was read |
| K1 | `event` | `leak_detected` | A leak sensor went wet |
| K2 | `event` | `leak_cleared` | A leak sensor went dry |
| K3 | `event` | `leak_detected` | A leak sensor with no location assigned |
| K4 | `event` | `leak_detected` | A LoRa sensor went wet |
| K5 | `event` | `leak_detected` | The valve's own flood probe went wet |
| K6 | `event` | `leak_cleared` | The valve's own flood probe went dry |
| K7 | `event` | `leak_detected` | The valve's flood probe went wet, battery not read |
| R1 | `event` | `auto_close` | Hub closed the valve because a sensor reported a leak |
| R2 | `event` | `auto_close` | Hub closed the valve because its own flood probe went wet |
| R3 | `event` | `auto_close` | Hub closed the valve on reconnect, finding a leak still active |
| R4 | `event` | `auto_close_blocked_override` | A leak occurred but auto-close was suppressed |
| R5 | `event` | `water_access_override_enabled` | A 24-hour water-access override started |
| R6 | `event` | `water_access_override_expired` | The override window elapsed |
| R7 | `event` | `auto_close_reenabled` | The override was cancelled by command |
| R8 | `event` | `rmleak_cleared` | The leak interlock was cleared |
| R9 | `event` | `rmleak_auto_cleared` | The leak interlock cleared itself |
| R10 | `event` | `water_access_override_enabled` | An override started before the hub's clock synced |
| H1 | `event` | `device_offline` | A device stopped responding |
| H2 | `event` | `device_recovered` | A device came back |
| H3 | `event` | `device_offline` | The valve stopped responding |
| H4 | `event` | `device_recovered` | A sensor came back wet |
| C1 | `event` | `cmd_ack` | A command succeeded |
| C2 | `event` | `cmd_ack` | A command succeeded, no correlation id supplied |
| C3 | `event` | `cmd_ack` | A command was refused |
| C4 | `event` | `cmd_ack` | An unrecognised command |
| C5 | `event` | `cmd_ack` | A valve command on a hub with no valve |
| C6 | `event` | `cmd_ack` | valve_open refused: valve battery critical |
| F1 | `event` | `rules_engine` | A rules payload could not be re-parsed |
| F2 | `event` | `health_engine` | A health payload could not be re-parsed |

## Lifecycle

### L1 — Hub connected, fully commissioned

Every MQTT connect, including reconnects — so not once per boot. Since 2.1.4 it goes out whether or not the hub is provisioned; a hub with no devices sends L4.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 42
  },
  "type": "lifecycle",
  "data": {
    "event": "online",
    "reset_reason": "power_on",
    "provisioned": true,
    "valve_id": "C4:19:D1:88:2A:7F",
    "lora_sensor_count": 0,
    "ble_leak_sensor_count": 2,
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    }
  }
}
```

*`telemetry_v2_publish_lifecycle() main/telemetry/telemetry_v2.c:482-516; trigger iothub_task() main/iothub/app_iothub.c:2000-2004`*

### L2 — Hub commissioned, but with no devices

A provision that carried only rules (no valve, no sensors) leaves the hub provisioned with zero devices. Rare; treat it exactly like L4.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "fw": "2.1.4",
    "uptime_s": 18
  },
  "type": "lifecycle",
  "data": {
    "event": "online",
    "reset_reason": "power_on",
    "provisioned": true,
    "lora_sensor_count": 0,
    "ble_leak_sensor_count": 0,
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    }
  }
}
```

*`omissions build_envelope() and telemetry_v2_publish_lifecycle() main/telemetry/telemetry_v2.c`*

### L3 — Hub reconnected after a crash

L1 with a different reset_reason: power_on, software, panic, watchdog, brownout, deep_sleep or unknown.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 7
  },
  "type": "lifecycle",
  "data": {
    "event": "online",
    "reset_reason": "panic",
    "provisioned": true,
    "valve_id": "C4:19:D1:88:2A:7F",
    "lora_sensor_count": 0,
    "ble_leak_sensor_count": 2,
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    }
  }
}
```

*`reset_reason_str() main/telemetry/telemetry_v2.c:138-151`*

### L4 — Hub with no devices (never provisioned, or every device removed)

New in 2.1.4. A hub with nothing provisioned used to publish nothing at all (BUG-6). It now publishes lifecycle, twin and snapshots (S8) like any other hub, with provisioned false and no valve_id. rules is back at the defaults: a hub that becomes empty resets it to true / 7.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 40
  },
  "type": "lifecycle",
  "data": {
    "event": "online",
    "reset_reason": "software",
    "provisioned": false,
    "lora_sensor_count": 0,
    "ble_leak_sensor_count": 0,
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    }
  }
}
```

*`telemetry_v2_publish_lifecycle() main/telemetry/telemetry_v2.c; no provisioned gate on Phase 3 of iothub_task() and on_hub_emptied() main/iothub/app_iothub.c`*

## Snapshot

### S1 — Routine heartbeat, everything healthy

The heartbeat. Cadence defaults to 300 s but is tunable 60-3600 s per hub through the Device Twin and persists across reboots (2.0.2) — read reported.snapshot_interval_s rather than assuming 300. The valve's last_seen_age_s is 0 while its link is up (2.1.4): link supervision drops a link that stops answering, so a linked valve is being heard now. Up to 2.1.3 it counted from the valve's last changed value, and a steady valve read hundreds of seconds while connected.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 3600
  },
  "type": "snapshot",
  "data": {
    "reason": "heartbeat",
    "system_health": {
      "rating": "excellent",
      "reason": "All devices healthy"
    },
    "valve": {
      "valve_id": "C4:19:D1:88:2A:7F",
      "state": "open",
      "battery": 92,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "fw_version": "2.2.0",
      "rating": "excellent",
      "last_seen_age_s": 0
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 12,
        "battery": 87,
        "rssi": -64,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "kitchen",
          "label": "Under sink"
        }
      },
      {
        "sensor_id": "00:80:E1:27:A1:04",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 31,
        "battery": 64,
        "rssi": -78,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "laundry",
          "label": "Behind machine"
        }
      }
    ],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": false
  }
}
```

*`telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c:520-757; default interval main/telemetry/telemetry_v2.h:16, tunable range :22-23`*

### S2 — Heartbeat with the valve disconnected

The BLE link to the valve is down: battery, leak_state, rmleak and fw_version are omitted and state carries "disconnected". last_seen_age_s counts from the moment the link dropped (2.1.4). Here that is 245 s, past the valve's 180 s grace, so the valve is critical.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 5400
  },
  "type": "snapshot",
  "data": {
    "reason": "heartbeat",
    "system_health": {
      "rating": "critical",
      "reason": "Valve offline"
    },
    "valve": {
      "valve_id": "C4:19:D1:88:2A:7F",
      "state": "disconnected",
      "connected": false,
      "rating": "critical",
      "last_seen_age_s": 245
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 12,
        "battery": 87,
        "rssi": -64,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "kitchen",
          "label": "Under sink"
        }
      },
      {
        "sensor_id": "00:80:E1:27:A1:04",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 31,
        "battery": 64,
        "rssi": -78,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "laundry",
          "label": "Behind machine"
        }
      }
    ],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": false
  }
}
```

*`valve-disconnected branch of telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c:520-757`*

### S3 — Heartbeat with a sensor gone silent

Sensor unheard for 10 minutes. connected goes false and the rating critical. battery, rssi and leak_state keep the sensor's LAST reported values, and last_seen_age_s says how old they are. A sensor that was wet when it went silent therefore still reads leak_state true, and system_health names the leak. fw_version goes null, because it is merged only while connected.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 9000
  },
  "type": "snapshot",
  "data": {
    "reason": "heartbeat",
    "system_health": {
      "rating": "critical",
      "reason": "1 sensor offline"
    },
    "valve": {
      "valve_id": "C4:19:D1:88:2A:7F",
      "state": "open",
      "battery": 92,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "fw_version": "2.2.0",
      "rating": "excellent",
      "last_seen_age_s": 0
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 12,
        "battery": 87,
        "rssi": -64,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "kitchen",
          "label": "Under sink"
        }
      },
      {
        "sensor_id": "00:80:E1:27:A1:04",
        "connected": false,
        "rating": "critical",
        "last_seen_age_s": 734,
        "battery": 87,
        "rssi": -64,
        "leak_state": false,
        "fw_version": null,
        "location": {
          "code": "laundry",
          "label": "Behind machine"
        }
      }
    ],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": false
  }
}
```

*`sensor-array builder in telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c (battery / rssi / leak_state from the health table)`*

### S4 — Snapshot while a water-access override is running

A 24-hour override is open. override_active is on EVERY snapshot; override_remaining_s and expires_ts are the conditional pair. For a window started before the hub's clock synced (R10), expires_ts is missing for up to about 30 s after the sync, until the hub re-bases the window to the real clock.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 12000
  },
  "type": "snapshot",
  "data": {
    "reason": "event",
    "system_health": {
      "rating": "excellent",
      "reason": "All devices healthy"
    },
    "valve": {
      "valve_id": "C4:19:D1:88:2A:7F",
      "state": "open",
      "battery": 92,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "fw_version": "2.2.0",
      "rating": "excellent",
      "last_seen_age_s": 0
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 12,
        "battery": 87,
        "rssi": -64,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "kitchen",
          "label": "Under sink"
        }
      },
      {
        "sensor_id": "00:80:E1:27:A1:04",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 31,
        "battery": 64,
        "rssi": -78,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "laundry",
          "label": "Behind machine"
        }
      }
    ],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": true,
    "override_remaining_s": 82740,
    "expires_ts": 1785481140
  }
}
```

*`telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c:520-757; window constant OVERRIDE_WINDOW_DURATION_S main/rules_engine/rules_engine.c:29`*

### S5 — First snapshot after boot

S1 with a different reason: heartbeat, event, commission, boot, fast or decommission.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 12
  },
  "type": "snapshot",
  "data": {
    "reason": "boot",
    "system_health": {
      "rating": "excellent",
      "reason": "All devices healthy"
    },
    "valve": {
      "valve_id": "C4:19:D1:88:2A:7F",
      "state": "open",
      "battery": 92,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "fw_version": "2.2.0",
      "rating": "excellent",
      "last_seen_age_s": 0
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 12,
        "battery": 87,
        "rssi": -64,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "kitchen",
          "label": "Under sink"
        }
      },
      {
        "sensor_id": "00:80:E1:27:A1:04",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 31,
        "battery": 64,
        "rssi": -78,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "laundry",
          "label": "Behind machine"
        }
      }
    ],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": false
  }
}
```

*`snap_reason_str() main/iothub/app_iothub.c:597-607; decommission reboot path iothub_task() main/iothub/app_iothub.c:1778`*

### S6 — Early snapshot as soon as the valve is ready

Fires the moment the valve's BLE setup completes, before the sensors have beaconed. Array membership comes from the provisioned list, not from who has checked in, so an unheard sensor still appears, with nulls. A device not yet heard since it was added is kept out of the roll-up during its sync window, so the hub reads excellent and says what it is waiting for rather than "2 sensors offline".

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 25
  },
  "type": "snapshot",
  "data": {
    "reason": "fast",
    "system_health": {
      "rating": "excellent",
      "reason": "Syncing - waiting for 2 devices"
    },
    "valve": {
      "valve_id": "C4:19:D1:88:2A:7F",
      "state": "open",
      "battery": 92,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "fw_version": "2.2.0",
      "rating": "excellent",
      "last_seen_age_s": 0
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": false,
        "rating": "critical",
        "last_seen_age_s": null,
        "battery": null,
        "rssi": null,
        "leak_state": false,
        "fw_version": null,
        "location": {
          "code": "kitchen",
          "label": "Under sink"
        }
      },
      {
        "sensor_id": "00:80:E1:27:A1:04",
        "connected": false,
        "rating": "critical",
        "last_seen_age_s": null,
        "battery": null,
        "rssi": null,
        "leak_state": false,
        "fw_version": null,
        "location": {
          "code": "laundry",
          "label": "Behind machine"
        }
      }
    ],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": false
  }
}
```

*`snap_reason_str() main/iothub/app_iothub.c; membership health_engine_reconcile_devices(), roll-up excuse rollup_unheard_locked() main/health_engine/health_engine.c`*

### S7 — Final snapshot before decommissioning

The last message from this identity before the decommission-all reboot. It pictures the CLEARED hub, not the installation: config, name, valve link and health table are already wiped when it is built. Since 2.1.4 that means valve {} and "No devices provisioned" (it used to report a disconnected valve).

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "fw": "2.1.4",
    "uptime_s": 60000
  },
  "type": "snapshot",
  "data": {
    "reason": "decommission",
    "system_health": {
      "rating": "excellent",
      "reason": "No devices provisioned"
    },
    "valve": {},
    "lora_sensors": [],
    "ble_leak_sensors": [],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": false
  }
}
```

*`decommission branch of handle_c2d_command(); final publish in the g_decommission_reboot block of iothub_task() main/iothub/app_iothub.c`*

### S8 — Hub with no devices

New in 2.1.4 (BUG-3/5/6). The snapshot that follows the removal of the last device: reason "event", valve {}, both arrays [], and system_health excellent / "No devices provisioned". The hub then keeps publishing: "heartbeat" at the interval, and one "boot" after every boot or MQTT (re)connect. Up to 2.1.3 an emptied hub sent one stale snapshot (the removed valve, still open) and then went silent.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 70000
  },
  "type": "snapshot",
  "data": {
    "reason": "event",
    "system_health": {
      "rating": "excellent",
      "reason": "No devices provisioned"
    },
    "valve": {},
    "lora_sensors": [],
    "ble_leak_sensors": [],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": false
  }
}
```

*`on_hub_emptied() and apply_device_set_change() main/iothub/app_iothub.c; empty-table reason in telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c`*

### S9 — Hub with sensors and no valve

New in 2.1.4 (BUG-5). A sensors-only hub reports valve {} — no valve_id, no state. Up to 2.1.3 it reported {"state":"disconnected","connected":false}, indistinguishable from a real valve that had dropped off.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 3600
  },
  "type": "snapshot",
  "data": {
    "reason": "heartbeat",
    "system_health": {
      "rating": "excellent",
      "reason": "All devices healthy"
    },
    "valve": {},
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 12,
        "battery": 87,
        "rssi": -64,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "kitchen",
          "label": "Under sink"
        }
      },
      {
        "sensor_id": "00:80:E1:27:A1:04",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 31,
        "battery": 64,
        "rssi": -78,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "laundry",
          "label": "Behind machine"
        }
      }
    ],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": false
  }
}
```

*`valve block of telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c`*

### S10 — Valve linked, but its readings not in yet

New in 2.1.4 (BUG-1). The BLE link to the provisioned valve is up but its characteristics have not been read yet — a few seconds after every connect. state is "unknown", battery and fw_version are null, and leak_state / rmleak are the defaults. Up to 2.1.3 this window published battery 0, which read as an empty battery. Here the valve has not been heard since boot, so it is still in its sync window.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 20
  },
  "type": "snapshot",
  "data": {
    "reason": "event",
    "system_health": {
      "rating": "excellent",
      "reason": "Syncing - waiting for 1 device"
    },
    "valve": {
      "valve_id": "C4:19:D1:88:2A:7F",
      "state": "unknown",
      "battery": null,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "fw_version": null,
      "rating": "critical",
      "last_seen_age_s": null
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 12,
        "battery": 87,
        "rssi": -64,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "kitchen",
          "label": "Under sink"
        }
      },
      {
        "sensor_id": "00:80:E1:27:A1:04",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 31,
        "battery": 64,
        "rssi": -78,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "laundry",
          "label": "Behind machine"
        }
      }
    ],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": false
  }
}
```

*`ready / live branch of the valve block in telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c`*

### S11 — Valve battery critical

New in 2.1.4 (BUG-1). The valve's battery is at or below 10 %, so the valve is rated critical and the hub RED, reason "Valve battery critical". At 11-20 % it is warning / "Valve battery low". This is a valve-only band: a sensor's low battery never reaches critical. No health alert is sent for it; this snapshot follows within seconds. valve_open is refused while it lasts (C6).

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 90000
  },
  "type": "snapshot",
  "data": {
    "reason": "event",
    "system_health": {
      "rating": "critical",
      "reason": "Valve battery critical"
    },
    "valve": {
      "valve_id": "C4:19:D1:88:2A:7F",
      "state": "open",
      "battery": 8,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "fw_version": "2.2.0",
      "rating": "critical",
      "last_seen_age_s": 0
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 12,
        "battery": 87,
        "rssi": -64,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "kitchen",
          "label": "Under sink"
        }
      },
      {
        "sensor_id": "00:80:E1:27:A1:04",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 31,
        "battery": 64,
        "rssi": -78,
        "leak_state": false,
        "fw_version": "1.1.0",
        "location": {
          "code": "laundry",
          "label": "Behind machine"
        }
      }
    ],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": false
  }
}
```

*`compute_valve_rating() main/health_engine/health_engine.c; build_system_health_reason() main/telemetry/telemetry_v2.c`*

## Valve events

> **The numbering jumps from V2 to V5 on purpose.** V3 and V4 were the valve's own flood-probe events (`valve_flood_detected` / `valve_flood_cleared`). In firmware 1.9.0 they became ordinary leak events and moved to **K5** and **K6** under *Leak events*. The surviving messages keep their original identifiers so this document can be read side by side with v1.0 and v2.0 — V1 here is the same message as V1 there.

### V1 — Valve opened

A GATT notification whose value changed. Delta-gated: an unchanged notification produces nothing.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 4000
  },
  "type": "event",
  "data": {
    "event": "valve_state_changed",
    "source_type": "valve",
    "valve_id": "C4:19:D1:88:2A:7F",
    "valve_state": "open",
    "battery": 92,
    "leak_state": false,
    "rmleak": false,
    "fw_version": "2.2.0"
  }
}
```

*`telemetry_v2_publish_valve_event() main/telemetry/telemetry_v2.c:763-797; ble_update dequeue iothub_task() main/iothub/app_iothub.c:2106-2116`*

### V2 — Valve closed and locked after a leak

V1 with rmleak true — the valve is latched and will refuse to open until the interlock is cleared.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 4020
  },
  "type": "event",
  "data": {
    "event": "valve_state_changed",
    "source_type": "valve",
    "valve_id": "C4:19:D1:88:2A:7F",
    "valve_state": "closed",
    "battery": 92,
    "leak_state": false,
    "rmleak": true,
    "fw_version": "2.2.0"
  }
}
```

*`telemetry_v2_publish_valve_event() main/telemetry/telemetry_v2.c:763-797`*

### V5 — Valve opened, firmware revision unknown

V1 with fw_version absent after a failed DIS read. There is no null variant; the key is simply gone.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 4700
  },
  "type": "event",
  "data": {
    "event": "valve_state_changed",
    "source_type": "valve",
    "valve_id": "C4:19:D1:88:2A:7F",
    "valve_state": "open",
    "battery": 92,
    "leak_state": false,
    "rmleak": false
  }
}
```

*`fw_version omission in telemetry_v2_publish_valve_event() main/telemetry/telemetry_v2.c:763-797`*

### V6 — Valve moved before its battery was read

New in 2.1.4 (BUG-1). V1 with battery null: there is no real battery reading on this link yet (the characteristic is missing, its read failed, or setup has not finished). Up to 2.1.3 this was 0, which reads as an empty battery.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 4800
  },
  "type": "event",
  "data": {
    "event": "valve_state_changed",
    "source_type": "valve",
    "valve_id": "C4:19:D1:88:2A:7F",
    "valve_state": "closed",
    "battery": null,
    "leak_state": false,
    "rmleak": false,
    "fw_version": "2.2.0"
  }
}
```

*`0xFF -> null in telemetry_v2_publish_valve_event() main/telemetry/telemetry_v2.c`*

## Leak events

> **The identity key on this family is not one key.** It is named for the device type: `sensor_id` on K1–K4, `valve_id` on K5–K6. Both sit in the same position — third, immediately after `source_type` — and carry the same string form, so switch on `source_type` and read the corresponding key. K5 and K6 are the valve's own flood probe, which up to v1.0 appeared separately as V3 and V4 under *Valve events* with a different payload shape.

### K1 — A leak sensor went wet

A BLE advertisement reporting a leak state different from the cached one. The sensor's fields are FLAT on data here, where a snapshot nests them in ble_leak_sensors[].

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 8000
  },
  "type": "event",
  "data": {
    "event": "leak_detected",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:9A:E6",
    "leak_state": true,
    "battery": 87,
    "location": {
      "code": "kitchen",
      "label": "Under sink"
    },
    "rssi": -64
  }
}
```

*`telemetry_v2_publish_leak_event() main/telemetry/telemetry_v2.c:799-831; BLE leak dequeue iothub_task() main/iothub/app_iothub.c:2130-2143`*

### K2 — A leak sensor went dry

leak_state is guaranteed false whenever event is leak_cleared — one value selects both.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 8600
  },
  "type": "event",
  "data": {
    "event": "leak_cleared",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:9A:E6",
    "leak_state": false,
    "battery": 87,
    "location": {
      "code": "kitchen",
      "label": "Under sink"
    },
    "rssi": -66
  }
}
```

*`BLE leak dequeue iothub_task() main/iothub/app_iothub.c:2130-2143`*

### K3 — A leak sensor with no location assigned

K1 with the location fallback. The key is always present: code becomes "unknown" and label an empty string.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 8100
  },
  "type": "event",
  "data": {
    "event": "leak_detected",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:A1:04",
    "leak_state": true,
    "battery": 64,
    "location": {
      "code": "unknown",
      "label": ""
    },
    "rssi": -78
  }
}
```

*`add_location_obj() main/telemetry/telemetry_v2.c:153-163; add_location_for_source() :170-189`*

### K4 — A LoRa sensor went wet

K1 with source_type "lora" and a hex id. NOT SEEN IN PRACTICE — the LoRa radio is unpopulated on the current PCBA.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 8200
  },
  "type": "event",
  "data": {
    "event": "leak_detected",
    "source_type": "lora",
    "sensor_id": "0x1A2B3C4D",
    "leak_state": true,
    "battery": 78,
    "location": {
      "code": "basement",
      "label": "Sump"
    },
    "rssi": -95
  }
}
```

*`LoRa dequeue iothub_task() main/iothub/app_iothub.c:2053`*

### K5 — The valve's own flood probe went wet

The valve's own probe, discriminated by source_type "valve". No rssi (a GATT link, not an advertisement); location is always the unknown fallback because sensor_meta cannot address a valve yet; adds valve_state, rmleak and fw_version.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 4100
  },
  "type": "event",
  "data": {
    "event": "leak_detected",
    "source_type": "valve",
    "valve_id": "C4:19:D1:88:2A:7F",
    "leak_state": true,
    "battery": 92,
    "location": {
      "code": "unknown",
      "label": ""
    },
    "valve_state": "open",
    "rmleak": false,
    "fw_version": "2.2.0"
  }
}
```

*`BLE_UPD_LEAK branch iothub_task() main/iothub/app_iothub.c:2077; builder telemetry_v2_publish_leak_event() main/telemetry/telemetry_v2.c:799-831`*

### K6 — The valve's own flood probe went dry

K5's counterpart. valve_state and rmleak reflect the auto-close K5 triggered: closed and still latched.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 4600
  },
  "type": "event",
  "data": {
    "event": "leak_cleared",
    "source_type": "valve",
    "valve_id": "C4:19:D1:88:2A:7F",
    "leak_state": false,
    "battery": 92,
    "location": {
      "code": "unknown",
      "label": ""
    },
    "valve_state": "closed",
    "rmleak": true,
    "fw_version": "2.2.0"
  }
}
```

*`BLE_UPD_LEAK branch iothub_task() main/iothub/app_iothub.c:2077`*

### K7 — The valve's flood probe went wet, battery not read

New in 2.1.4. K5 with battery null: the valve has no real battery reading on this link. battery is null, never 0, for an unknown reading from ANY source. It stays in the required core, so the key is always present.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 4150
  },
  "type": "event",
  "data": {
    "event": "leak_detected",
    "source_type": "valve",
    "valve_id": "C4:19:D1:88:2A:7F",
    "leak_state": true,
    "battery": null,
    "location": {
      "code": "unknown",
      "label": ""
    },
    "valve_state": "open",
    "rmleak": false,
    "fw_version": "2.2.0"
  }
}
```

*`0xFF -> null in telemetry_v2_publish_leak_event() main/telemetry/telemetry_v2.c`*

## Rules events

> **Cause arrives before consequence.** When water triggers an automatic shut-off you receive `leak_detected` **first**, then `auto_close`, then the snapshot. Up to firmware 2.0.0 the order was inverted — `auto_close` reached the cloud about 40 ms ahead of the leak event that caused it, so a consumer reading in order saw a valve close for no stated reason. Fixed in 2.0.1.

Note the `rmleak` values across that sequence are not contradictory even though they differ: `leak_detected` reports the valve **at detection**, before the interlock (`rmleak:false`); `auto_close` reports the interlock being applied (`rmleak_asserted:true`); the snapshot that follows reports the settled state (`rmleak:true`).

These name their device the same way every other message does: `valve_id` when `source_type` is `valve`, `sensor_id` otherwise. Up to firmware 2.0.2 they used a generic `device_id` instead, on the grounds that the hub raises them *about* a device rather than the device reporting itself — a distinction that mattered to the firmware and not to you. Removed in 2.1.0.

### R1 — Hub closed the valve because a sensor reported a leak

The automatic shut-off. rmleak_asserted says whether the interlock writes were ISSUED, not that they landed — false when the valve was unreachable (hardcoded true up to 2.0.1). location is present only when the source has a sensor_meta entry and OMITTED otherwise, the opposite of K1-K6. A hub with no valve provisioned sends no auto_close at all (2.1.4): a leak there sends leak_detected only. Up to 2.1.3 it also sent auto_close with rmleak_asserted false. When the valve is out of range, rmleak_asserted is false and the RMLEAK and close writes are held for its reconnect, RMLEAK first (2.1.4).

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 8005
  },
  "type": "event",
  "data": {
    "event": "auto_close",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:9A:E6",
    "rmleak_asserted": true,
    "location": {
      "code": "kitchen",
      "label": "Under sink"
    }
  }
}
```

*`build_auto_close_telemetry() main/rules_engine/rules_engine.c:428-459`*

### R2 — Hub closed the valve because its own flood probe went wet

R1 with source_type "valve" and no location. The identity key follows the source: valve_id here, sensor_id on R1 — so this joins to the same device record as K5 and the snapshot.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 4105
  },
  "type": "event",
  "data": {
    "event": "auto_close",
    "source_type": "valve",
    "valve_id": "C4:19:D1:88:2A:7F",
    "rmleak_asserted": true
  }
}
```

*`build_auto_close_telemetry() main/rules_engine/rules_engine.c:428-459; identity resolution wire_device_id() main/rules_engine/rules_engine.c:315-323 and add_device_id() :327-341`*

### R3 — Hub closed the valve on reconnect, finding a leak still active

A structurally different auto_close: carries active_leak_count and data.cause "reconnect", and no source_type at all. Branch on data.cause to tell it from R1/R2. The identity key is still correct — valve-vs-sensor is decidable from the tracking id even though the device TYPE is not.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 400
  },
  "type": "event",
  "data": {
    "event": "auto_close",
    "cause": "reconnect",
    "sensor_id": "00:80:E1:27:9A:E6",
    "rmleak_asserted": true,
    "active_leak_count": 1
  }
}
```

*`rules_engine_on_valve_connected() main/rules_engine/rules_engine.c:997-1162`*

### R4 — A leak occurred but auto-close was suppressed

An override window is open, so the hub deliberately did not close. override_remaining_s is omitted only when the window has just expired and the hub has not processed it yet. Since 2.1.4 a window started before the clock synced is timed on the hub uptime, and a window with a real expiry restored after a power-on that lost the clock counts down from that power-on, so both carry override_remaining_s (the second used to omit it).

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 20000
  },
  "type": "event",
  "data": {
    "event": "auto_close_blocked_override",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:9A:E6",
    "override_remaining_s": 61200
  }
}
```

*`rules_engine_evaluate_leak() main/rules_engine/rules_engine.c:493-651`*

### R5 — A 24-hour water-access override started

Started by a valve long-press ("button") or the override_enable command ("c2d_command"). remaining_s is always the full 86400. expires_ts is omitted when the window started before the hub's clock synced (R10).

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 19000
  },
  "type": "event",
  "data": {
    "event": "water_access_override_enabled",
    "trigger": "button",
    "expires_ts": 1785484800,
    "remaining_s": 86400
  }
}
```

*`start_override_window() main/rules_engine/rules_engine.c:193-223; OVERRIDE_WINDOW_DURATION_S main/rules_engine/rules_engine.c:29`*

### R6 — The override window elapsed

auto_close_resumed is exactly active_leak_count > 0 sampled BEFORE any close is attempted — not a report of what happened. With auto-close disabled it still reads true and nothing closes. This message is the only trace of an expiry-driven closure. Since 2.1.4 it can also come before the hub's clock has synced: a window restored after a power-on that lost the clock ends 24 h after that power-on if the clock has still not synced, and the event then arrives late, like any pre-sync event (R10).

```json
{
  "schema": "eflostop.v2",
  "ts": 1785484800,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 105400
  },
  "type": "event",
  "data": {
    "event": "water_access_override_expired",
    "auto_close_resumed": true,
    "active_leak_count": 1
  }
}
```

*`rules_engine_tick() main/rules_engine/rules_engine.c:1164-1303 (expiry + re-close)`*

### R7 — The override was cancelled by command

Response to override_cancel. reason is always "c2d_command" — a different vocabulary from data.reason on a snapshot.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 25000
  },
  "type": "event",
  "data": {
    "event": "auto_close_reenabled",
    "previous_remaining_s": 61200,
    "reason": "c2d_command"
  }
}
```

*`cancel_override_window() main/rules_engine/rules_engine.c:227-245; rules_engine_cancel_override() :832-914`*

### R8 — The leak interlock was cleared

Response to leak_reset. override_cancelled appears only if a window was open. Deliberately NO source_type: the incident may have been latched by a sensor. The interlock is the valve's, so the key is valve_id — omitted entirely on a hub with sensors and no valve.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 26000
  },
  "type": "event",
  "data": {
    "event": "rmleak_cleared",
    "valve_id": "C4:19:D1:88:2A:7F",
    "override_cancelled": true
  }
}
```

*`rules_engine_reset_leak_incident() main/rules_engine/rules_engine.c:750-830`*

### R9 — The leak interlock cleared itself

Every source dry for 10 seconds, so the hub released the latch itself. Does NOT re-open the valve; valve_open or the valve button does. Same identity rule as R8. Since 2.1.4 the dwell is 10 s (it was 30 s), and the hub polls every 2 s while the clear is pending, so this arrives about 10-12 s after the last source reports dry (it was 30-60 s). A sensor that goes wet again 10-30 s after drying (up to about 60 s, counting the old idle wait) therefore now gets a full clear and re-latch cycle (this event, then auto_close again); the valve stays closed throughout. Known limit: a re-wet that lands within about 2 s of the clear (before the next rules tick, or before the valve's read-back of the clear) can instead be taken for a valve-button override: no second auto_close, then water_access_override_enabled with trigger "button" and a 24 h window, so auto-close is blocked and valve_open is accepted while the sensor is still wet. The race predates 2.1.4, when its window was up to 30 s wide.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 8700
  },
  "type": "event",
  "data": {
    "event": "rmleak_auto_cleared",
    "valve_id": "C4:19:D1:88:2A:7F",
    "clear_after_seconds": 10
  }
}
```

*`rules_engine_tick() main/rules_engine/rules_engine.c; AUTO_CLEAR_TIMEOUT_MS main/rules_engine/rules_engine.c`*

### R10 — An override started before the hub's clock synced

New in 2.1.4. R5 from a hub that had no clock yet: it powered up with the router down, and the valve button was pressed 95 s after boot. expires_ts is omitted rather than naming an instant in 1970; the expiry is about ts + remaining_s. The event was held in the hub's offline buffer and sent after the first connect, with ts rewritten to the moment of the press, worked out from the hub uptime (gateway.uptime_s is the uptime at the press). Every event raised before the first clock sync arrives this way, late but correctly time-stamped; one left over from a restart before the clock synced is dropped. The window runs on the hub uptime until the clock syncs and is then re-based to it, so snapshots carry expires_ts from then on (S4).

```json
{
  "schema": "eflostop.v2",
  "ts": 1785438400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 95
  },
  "type": "event",
  "data": {
    "event": "water_access_override_enabled",
    "trigger": "button",
    "remaining_s": 86400
  }
}
```

*`start_override_window() and rules_engine_tick() main/rules_engine/rules_engine.c; pre-sync hold publish_json() main/telemetry/telemetry_v2.c; stamping ob_prepare_replay_locked() main/offline_buffer/offline_buffer.c`*

## Health events

> These name their device with `valve_id` or `sensor_id`, per `data.source_type` — the same key, in the same position, that the snapshot and the leak events use for that device. The key was `dev_type` up to firmware 1.9.0 and a generic `device_id` from 2.0.0 to 2.0.2; 2.1.0 was the last of those three renames, and the identity key is now uniform across every outbound message.

### H1 — A device stopped responding

A device that had been heard became unreachable. data.category "health" is how you recognise these. Since 2.1.4 these events report REACHABILITY only: device_offline is always critical-because-unreachable. A device that is critical for another reason raises no health event, whether it is wet (see the leak events) or a valve at <= 10 % battery (see S11). A debounced alert is no longer dropped: it is sent once the debounce has passed.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 9100
  },
  "type": "event",
  "data": {
    "category": "health",
    "event": "device_offline",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:A1:04",
    "rating": "critical",
    "prev_rating": "excellent",
    "battery": 64,
    "rssi": -78,
    "offline_duration_s": 600
  }
}
```

*`health_alert_to_json(), is_offline_state(), apply_rating() main/health_engine/health_engine.c`*

### H2 — A device came back

The recovery counterpart of H1, sent only if the device_offline was. Never carries offline_duration_s.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 9700
  },
  "type": "event",
  "data": {
    "category": "health",
    "event": "device_recovered",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:A1:04",
    "rating": "excellent",
    "prev_rating": "critical",
    "battery": 64,
    "rssi": -75
  }
}
```

*`health_alert_to_json() main/health_engine/health_engine.c`*

### H3 — The valve stopped responding

H1 for the valve. rssi is ALWAYS absent and offline_duration_s is omitted when it would be zero. offline_duration_s is measured from the link drop (2.1.4), so it is normally about 180, the grace; up to 2.1.3 it counted from the valve's last changed value and could read hours. prev_rating is normally "warning", because a valve gets a 3-minute grace before promotion to critical; only sensors jump straight there. battery is omitted, not null, when the valve has never reported one.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 5500
  },
  "type": "event",
  "data": {
    "category": "health",
    "event": "device_offline",
    "source_type": "valve",
    "valve_id": "C4:19:D1:88:2A:7F",
    "rating": "critical",
    "prev_rating": "warning",
    "battery": 92,
    "offline_duration_s": 180
  }
}
```

*`rssi writers handle_lora_checkin() / handle_ble_leak_checkin(), omit-on-unknown health_alert_to_json(), compute_valve_rating() main/health_engine/health_engine.c`*

### H4 — A sensor came back wet

New in 2.1.4. H2 where the device returns INTO a leak: it is reachable again, so device_recovered is sent, but it is still critical, now because it is wet. rating and prev_rating are then both "critical". The two can also be equal on a trailing-edge alert (one sent after its debounce), where both carry the current rating. Read the event name, not a rating comparison, to tell offline from recovered.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 9800
  },
  "type": "event",
  "data": {
    "category": "health",
    "event": "device_recovered",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:A1:04",
    "rating": "critical",
    "prev_rating": "critical",
    "battery": 64,
    "rssi": -76
  }
}
```

*`is_offline_state(), apply_rating(), trailing edge in evaluate_timeouts() main/health_engine/health_engine.c`*

## Command acknowledgements

### C1 — A command succeeded

One per enveloped command, or any command carrying a correlation id. data.id echoes what you sent.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 30000
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-8f21",
    "cmd": "valve_close",
    "status": "ok"
  }
}
```

*`telemetry_v2_publish_cmd_ack() main/telemetry/telemetry_v2.c:872-895; ack gate handle_c2d_command() main/iothub/app_iothub.c:671-1011`*

### C2 — A command succeeded, no correlation id supplied

No inbound id, so data.id is OMITTED rather than empty. Match on cmd and timing.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 30100
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "cmd": "valve_open",
    "status": "ok"
  }
}
```

*`id omission in telemetry_v2_publish_cmd_ack() main/telemetry/telemetry_v2.c:872-895`*

### C3 — A command was refused

error.code is not a code — it is the command name repeated. The free-text detail is the only real discriminator. This detail answers valve_open, or valve_set_state open, while the valve's RMLEAK latch is asserted. Since 2.1.4 it is also sent while the hub has a leak incident latched and no override window, even with the valve disconnected; an open sent while the valve was out of range used to be accepted and written at the reconnect.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 30200
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-8f22",
    "cmd": "valve_open",
    "status": "error",
    "error": {
      "code": "valve_open",
      "detail": "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."
    }
  }
}
```

*`telemetry_v2_publish_cmd_ack() main/telemetry/telemetry_v2.c; message valve_open_reject_reason() main/iothub/app_iothub.c`*

### C4 — An unrecognised command

An unknown command name. The detail is a fixed string.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 30300
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-8f23",
    "cmd": "reboot_hub",
    "status": "error",
    "error": {
      "code": "reboot_hub",
      "detail": "unknown command"
    }
  }
}
```

*`unknown-command branch of handle_c2d_command() main/iothub/app_iothub.c:671-1011`*

### C5 — A valve command on a hub with no valve

New in 2.1.4 (P0-a/c). valve_open, valve_close and valve_set_state are refused when no valve is provisioned. Up to 2.1.3 they acked ok, and the hub then connected to any nearby eFloStop valve and drove it. "The valve command could not be queued. Try again." is the sibling refusal when the hub's valve command queue is full.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 30400
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-8f24",
    "cmd": "valve_close",
    "status": "error",
    "error": {
      "code": "valve_close",
      "detail": "No valve is set up for this hub."
    }
  }
}
```

*`c2d_valve_command() main/iothub/app_iothub.c`*

### C6 — valve_open refused: valve battery critical

New in 2.1.4 (BUG-1). valve_open, or valve_set_state open, while the valve's last real battery reading is at or below 10 %. The valve would refuse to open anyway, and up to 2.1.3 the hub acked ok for a valve that stayed shut. Closing is never refused for battery.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 30500
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-8f25",
    "cmd": "valve_open",
    "status": "error",
    "error": {
      "code": "valve_open",
      "detail": "Valve battery critical (≤10 %): the valve will not open. Replace the batteries."
    }
  }
}
```

*`valve_open_reject_reason() main/iothub/app_iothub.c`*

## Fallback messages

### F1 — A rules payload could not be re-parsed

Defensive path: the rules engine's own JSON failed to re-parse, so the raw string is sent under a fallback event name. You should never see this.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 31000
  },
  "type": "event",
  "data": {
    "event": "rules_engine",
    "raw": "{\"event\":\"auto_close\",\"source_type\":\"ble_lea"
  }
}
```

*`telemetry_v2_publish_rules_event() main/telemetry/telemetry_v2.c:833-851 (fallback branch included)`*

### F2 — A health payload could not be re-parsed

The health-engine counterpart of F1.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 31100
  },
  "type": "event",
  "data": {
    "event": "health_engine",
    "raw": "{\"category\":\"health\",\"event\":\"device_off"
  }
}
```

*`telemetry_v2_publish_health_event() main/telemetry/telemetry_v2.c:853-870 (fallback branch included)`*

