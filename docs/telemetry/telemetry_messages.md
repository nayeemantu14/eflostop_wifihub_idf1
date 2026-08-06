# eFloStop II Wi-Fi Hub — Telemetry Message Catalogue

*Every message the hub can send, as a real example — v3.0*

> GENERATED FILE — produced by `docs/telemetry/build_messages.py` from `messages_data.py`.

| | |
|---|---|
| Document version | 3.0 (supersedes v2.0, which documented firmware 1.9.0) |
| Firmware version | 2.0.2 — `CMakeLists.txt:12` |
| Git commit | `f852ddf7b4209db1fbd2539babcb791898eed94a` |
| Schema | `eflostop.v2` |
| Topic | `devices/<device_id>/messages/events/` (QoS 1) |
| Message count | 37 distinct messages across 8 families |

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

Two things to know before you build a parser. **A conditional key is omitted, not sent as null** — except
inside the two sensor arrays, where every key is always present and unknown values are `null`. And in a
snapshot, a sensor that has gone silent reports `"leak_state": false`, which is indistinguishable from a
genuine dry reading; see message S3.

**Changes in firmware 2.0.0.** Firmware 1.9.0 made water detection a single event family; 2.0.0 finishes the
job on naming. Two things changed, and there is no compatibility shim on the telemetry plane — every hub runs
the new shape.

1. **The identity key is named for the device type.** A valve reports `valve_id`; a leak sensor reports
   `sensor_id`. This replaces the single `device_id` that 1.9.0 used everywhere. It applies to the snapshot's
   `data.valve` object and both sensor arrays, to `valve_state_changed`, and to the leak family — where the
   key is `sensor_id` on K1–K4 and `valve_id` on K5–K6. The lifecycle message's `data.valve_device_id` and
   the twin's `valve_device_id` are both now `valve_id`.

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
`rmleak_cleared` / `rmleak_auto_cleared` (R8, R9) now carry `source_type` and `device_id` — previously they
arrived with no identity at all and could not be attributed to any device.

**Hub-generated events keep the generic `device_id`, on purpose.** That is the health alerts (`device_offline`,
`device_recovered`), the `auto_close` family (including `auto_close_blocked_override`) and the RMLEAK interlock
events (`rmleak_cleared`, `rmleak_auto_cleared`). The hub raises all of these *about* a device rather than the
device reporting itself, and one builder serves valves and sensors alike. On the health and auto_close families
`data.source_type` still tells you which kind of device it is; the two RMLEAK events deliberately carry NO
`source_type` at all, because the incident they close may have been latched by a sensor while the interlock
itself is always the valve’s. If you are writing a single "which device does this message concern?" helper:
read `valve_id` or `sensor_id` on device-reported messages, `device_id` on these.

On a hub provisioned with sensors but **no valve**, the two RMLEAK events omit `device_id` entirely rather than
naming a valve that does not exist — so treat it as optional there. The same applies to `auto_close`: rather than
emit a placeholder, the hub omits the key when no MAC resolves.

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

Document **v2.0** described firmware **1.9.0**; this is **v3.0**, describing firmware **2.0.2**. v2.0 of the `.docx` is kept unchanged so the two can be read side by side. Every row below is a breaking change — there is no compatibility shim on the telemetry plane.

| | v2.0 — firmware 1.9.0 | v3.0 — firmware 2.0.2 |
|---|---|---|
| Identity key, snapshot valve object | `data.valve.device_id` | `data.valve.valve_id` |
| Identity key, snapshot sensor arrays | `data.lora_sensors[].device_id, data.ble_leak_sensors[].device_id` | `data.lora_sensors[].sensor_id, data.ble_leak_sensors[].sensor_id` |
| Identity key, leak events — sensors (K1-K4) | `data.device_id` | `data.sensor_id` |
| Identity key, leak events — valve (K5, K6) | `data.device_id` | `data.valve_id` |
| Identity key, valve_state_changed | `data.device_id` | `data.valve_id` |
| Identity key, health events | `data.device_id` | `data.device_id  (UNCHANGED — hub-generated, deliberately generic)` |
| Identity key, auto_close / rules events | `data.device_id` | `data.device_id  (UNCHANGED — same reason as health)` |
| Identity key, lifecycle | `data.valve_device_id` | `data.valve_id` |
| Identity key, twin reported | `valve_device_id` | `valve_id` |
| Device-type key name | `data.source_type on leak/rules events, but data.dev_type on health events` | `data.source_type on every outbound message; dev_type is gone` |
| Reconnect variant of auto_close (R3) | `data.source_type "reconnect" — a value outside the device-type vocabulary` | `data.cause "reconnect"; the event carries no source_type at all` |
| Inbound provision, valve identifier | `payload.valve_mac` | `payload.valve_id  (valve_mac still accepted as a deprecated fallback)` |
| Inbound decommission, target matching | `"ble" case-insensitive, but "valve" / "lora" / "all" case-SENSITIVE` | `all targets case-insensitive` |
| Order of leak_detected vs auto_close | `auto_close published ~40 ms BEFORE the leak_detected that caused it` | `leak_detected first, then auto_close, then the snapshot  (2.0.1)` |
| rmleak on a valve leak event | `raced the interlock write — false or true depending on GATT timing` | `sampled once at detection; always the pre-interlock value  (2.0.1)` |
| Heartbeat cadence | `300 s, fixed in practice: the twin could set it but the value was lost on every reboot, so hubs effectively always ran 300 s` | `300 s default, tunable 60-3600 s per hub and PERSISTED across reboots. Read reported.snapshot_interval_s; do not assume 300  (2.0.2)` |
| rmleak_cleared / rmleak_auto_cleared | `no source_type, no identity — unattributable` | `device_id names the valve; no source_type  (2.0.1/2.0.2)` |
| auto_close rmleak_asserted | `hardcoded true, even when the valve was unreachable and nothing was sent` | `reflects whether the RMLEAK+close writes were actually issued  (2.0.2)` |
| rmleak_cleared / rmleak_auto_cleared | `no identity at all` | `device_id names the valve; NO source_type (it would mean a different thing here)  (2.0.2)` |
| Placeholder device ids | `auto_close could ship device_id "valve" — a phantom device matching nothing` | `the key is omitted when no MAC resolves  (2.0.2)` |
| Firmware version | `1.9.0` | `2.0.2 — major bump: breaking change on both the telemetry and command planes` |

## Index of messages

| ID | `type` | `data.event` | Message |
|---|---|---|---|
| L1 | `lifecycle` | `online` | Hub connected, fully commissioned |
| L2 | `lifecycle` | `online` | Hub commissioned, but with no devices |
| L3 | `lifecycle` | `online` | Hub reconnected after a crash |
| S1 | `snapshot` | `—` | Routine heartbeat, everything healthy |
| S2 | `snapshot` | `—` | Heartbeat with the valve disconnected |
| S3 | `snapshot` | `—` | Heartbeat with a sensor gone silent — read this one carefully |
| S4 | `snapshot` | `—` | Snapshot while a water-access override is running |
| S5 | `snapshot` | `—` | First snapshot after boot |
| S6 | `snapshot` | `—` | Early snapshot as soon as the valve is ready |
| S7 | `snapshot` | `—` | Final snapshot before decommissioning |
| V1 | `event` | `valve_state_changed` | Valve opened |
| V2 | `event` | `valve_state_changed` | Valve closed and locked after a leak |
| V5 | `event` | `valve_state_changed` | Valve opened, firmware revision unknown |
| K1 | `event` | `leak_detected` | A leak sensor went wet |
| K2 | `event` | `leak_cleared` | A leak sensor went dry |
| K3 | `event` | `leak_detected` | A leak sensor with no location assigned |
| K4 | `event` | `leak_detected` | A LoRa sensor went wet |
| K5 | `event` | `leak_detected` | The valve's own flood probe went wet |
| K6 | `event` | `leak_cleared` | The valve's own flood probe went dry |
| R1 | `event` | `auto_close` | Hub closed the valve because a sensor reported a leak |
| R2 | `event` | `auto_close` | Hub closed the valve because its own flood probe went wet |
| R3 | `event` | `auto_close` | Hub closed the valve on reconnect, finding a leak still active |
| R4 | `event` | `auto_close_blocked_override` | A leak occurred but auto-close was suppressed |
| R5 | `event` | `water_access_override_enabled` | A 24-hour water-access override started |
| R6 | `event` | `water_access_override_expired` | The override window elapsed |
| R7 | `event` | `auto_close_reenabled` | The override was cancelled by command |
| R8 | `event` | `rmleak_cleared` | The leak interlock was cleared |
| R9 | `event` | `rmleak_auto_cleared` | The leak interlock cleared itself |
| H1 | `event` | `device_offline` | A device stopped responding |
| H2 | `event` | `device_recovered` | A device came back |
| H3 | `event` | `device_offline` | The valve stopped responding |
| C1 | `event` | `cmd_ack` | A command succeeded |
| C2 | `event` | `cmd_ack` | A command succeeded, no correlation id supplied |
| C3 | `event` | `cmd_ack` | A command was refused |
| C4 | `event` | `cmd_ack` | An unrecognised command |
| F1 | `event` | `rules_engine` | A rules payload could not be re-parsed |
| F2 | `event` | `health_engine` | A health payload could not be re-parsed |

## Lifecycle

### L1 — Hub connected, fully commissioned

Every MQTT connect once provisioned, including reconnects — so not once per boot.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
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

*`telemetry_v2_publish_lifecycle() main/telemetry/telemetry_v2.c:483-519; trigger iothub_task() main/iothub/app_iothub.c:1957-1961`*

### L2 — Hub commissioned, but with no devices

Provisioned but with no valve and no sensors. A hub that was never provisioned publishes nothing at all, so data.provisioned is true on every lifecycle you receive.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "fw": "2.0.2",
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

*`omissions build_envelope() main/telemetry/telemetry_v2.c:69-107 and telemetry_v2_publish_lifecycle() :483-519; unprovisioned/offline snapshot gate iothub_task() main/iothub/app_iothub.c:2164-2180`*

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
    "fw": "2.0.2",
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

## Snapshot

### S1 — Routine heartbeat, everything healthy

The heartbeat. Cadence defaults to 300 s but is tunable 60-3600 s per hub through the Device Twin and persists across reboots (2.0.2) — read reported.snapshot_interval_s rather than assuming 300.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
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
      "last_seen_age_s": 4
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 12,
        "battery": 87,
        "leak_state": false,
        "rssi": -64,
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
        "leak_state": false,
        "rssi": -78,
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

*`telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c:521-761; default interval main/telemetry/telemetry_v2.h:16, tunable range :21-23`*

### S2 — Heartbeat with the valve disconnected

The BLE link to the valve is down: battery, leak_state, rmleak and fw_version are omitted and state carries "disconnected".

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
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
        "leak_state": false,
        "rssi": -64,
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
        "leak_state": false,
        "rssi": -78,
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

*`valve-disconnected branch of telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c:521-761`*

### S3 — Heartbeat with a sensor gone silent — read this one carefully

Sensor unheard for 10 minutes. battery, rssi and fw_version go null but leak_state is hard-coded false — a sensor that was wet when it went silent reads dry here, and the earlier leak_detected is never retracted. last_seen_age_s is the only field that contradicts it.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
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
      "last_seen_age_s": 4
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 12,
        "battery": 87,
        "leak_state": false,
        "rssi": -64,
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
        "battery": null,
        "leak_state": false,
        "rssi": null,
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

*`sensor-array builder in telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c:521-761; F-01`*

### S4 — Snapshot while a water-access override is running

A 24-hour override is open. override_active is on EVERY snapshot; override_remaining_s and expires_ts are the conditional pair.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
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
      "last_seen_age_s": 4
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 12,
        "battery": 87,
        "leak_state": false,
        "rssi": -64,
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
        "leak_state": false,
        "rssi": -78,
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

*`telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c:521-761; window constant OVERRIDE_WINDOW_DURATION_S main/rules_engine/rules_engine.c:29`*

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
    "fw": "2.0.2",
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
      "last_seen_age_s": 4
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 12,
        "battery": 87,
        "leak_state": false,
        "rssi": -64,
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
        "leak_state": false,
        "rssi": -78,
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

*`snap_reason_str() main/iothub/app_iothub.c:588-601; decommission reboot path iothub_task() main/iothub/app_iothub.c:1728`*

### S6 — Early snapshot as soon as the valve is ready

Fires the moment the valve's BLE setup completes, before the sensors have beaconed. Array membership comes from the provisioned list, not from who has checked in, so an unheard sensor still appears with nulls.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
    "uptime_s": 25
  },
  "type": "snapshot",
  "data": {
    "reason": "fast",
    "system_health": {
      "rating": "critical",
      "reason": "2 sensors offline"
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
      "last_seen_age_s": 4
    },
    "lora_sensors": [],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:9A:E6",
        "connected": false,
        "rating": "critical",
        "last_seen_age_s": null,
        "battery": null,
        "leak_state": false,
        "rssi": null,
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
        "leak_state": false,
        "rssi": null,
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

*`snap_reason_str() main/iothub/app_iothub.c:588-601; membership health_engine_reload_devices() main/health_engine/health_engine.c:450-523`*

### S7 — Final snapshot before decommissioning

The last message from this identity. It pictures the CLEARED hub, not the installation — config, name, valve link and health table are already wiped when it is built.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "fw": "2.0.2",
    "uptime_s": 60000
  },
  "type": "snapshot",
  "data": {
    "reason": "decommission",
    "system_health": {
      "rating": "excellent",
      "reason": "All devices healthy"
    },
    "valve": {
      "state": "disconnected",
      "connected": false
    },
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

*`decommission branch of handle_c2d_command() main/iothub/app_iothub.c:662-975; final publish iothub_task() main/iothub/app_iothub.c:1590-2254`*

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
    "fw": "2.0.2",
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

*`telemetry_v2_publish_valve_event() main/telemetry/telemetry_v2.c:763-797; ble_update dequeue iothub_task() main/iothub/app_iothub.c:1826`*

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
    "fw": "2.0.2",
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
    "fw": "2.0.2",
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
    "fw": "2.0.2",
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

*`telemetry_v2_publish_leak_event() main/telemetry/telemetry_v2.c:799-831; BLE leak dequeue iothub_task() main/iothub/app_iothub.c:1824-1900`*

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
    "fw": "2.0.2",
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

*`BLE leak dequeue iothub_task() main/iothub/app_iothub.c:1824-1900`*

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
    "fw": "2.0.2",
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

*`add_location_obj() main/telemetry/telemetry_v2.c:153-168; add_location_for_source() :170-202`*

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
    "fw": "2.0.2",
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

*`LoRa dequeue iothub_task() main/iothub/app_iothub.c:1824`*

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
    "fw": "2.0.2",
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

*`BLE_UPD_LEAK branch iothub_task() main/iothub/app_iothub.c:1887; builder telemetry_v2_publish_leak_event() main/telemetry/telemetry_v2.c:799-831`*

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
    "fw": "2.0.2",
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

*`BLE_UPD_LEAK branch iothub_task() main/iothub/app_iothub.c:1887`*

## Rules events

> **Cause arrives before consequence.** When water triggers an automatic shut-off you receive `leak_detected` **first**, then `auto_close`, then the snapshot. Up to firmware 2.0.0 the order was inverted — `auto_close` reached the cloud about 40 ms ahead of the leak event that caused it, so a consumer reading in order saw a valve close for no stated reason. Fixed in 2.0.1.

Note the `rmleak` values across that sequence are not contradictory even though they differ: `leak_detected` reports the valve **at detection**, before the interlock (`rmleak:false`); `auto_close` reports the interlock being applied (`rmleak_asserted:true`); the snapshot that follows reports the settled state (`rmleak:true`).

These events carry `device_id`, not `valve_id`/`sensor_id` — the hub raises them *about* a device rather than the device reporting itself.

### R1 — Hub closed the valve because a sensor reported a leak

The automatic shut-off. rmleak_asserted says whether the interlock writes were ISSUED, not that they landed — false when the valve was unreachable (hardcoded true up to 2.0.1). location is present only when the source has a sensor_meta entry and OMITTED otherwise, the opposite of K1-K6.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
    "uptime_s": 8005
  },
  "type": "event",
  "data": {
    "event": "auto_close",
    "source_type": "ble_leak_sensor",
    "device_id": "00:80:E1:27:9A:E6",
    "rmleak_asserted": true,
    "location": {
      "code": "kitchen",
      "label": "Under sink"
    }
  }
}
```

*`build_auto_close_telemetry() main/rules_engine/rules_engine.c:409-442`*

### R2 — Hub closed the valve because its own flood probe went wet

R1 with source_type "valve" and no location. device_id is the valve's MAC, so this joins to the same device record as K5 and the snapshot.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
    "uptime_s": 4105
  },
  "type": "event",
  "data": {
    "event": "auto_close",
    "source_type": "valve",
    "device_id": "C4:19:D1:88:2A:7F",
    "rmleak_asserted": true
  }
}
```

*`build_auto_close_telemetry() main/rules_engine/rules_engine.c:409-442; identity resolution wire_device_id() main/rules_engine/rules_engine.c:299-309 and add_device_id() :311-335`*

### R3 — Hub closed the valve on reconnect, finding a leak still active

A structurally different auto_close: carries active_leak_count and data.cause "reconnect", and no source_type at all. Branch on data.cause to tell it from R1/R2.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
    "uptime_s": 400
  },
  "type": "event",
  "data": {
    "event": "auto_close",
    "cause": "reconnect",
    "device_id": "00:80:E1:27:9A:E6",
    "rmleak_asserted": true,
    "active_leak_count": 1
  }
}
```

*`rules_engine_on_valve_connected() main/rules_engine/rules_engine.c:978-1137`*

### R4 — A leak occurred but auto-close was suppressed

An override window is open, so the hub deliberately did not close. override_remaining_s is omitted when the remaining time is negative.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
    "uptime_s": 20000
  },
  "type": "event",
  "data": {
    "event": "auto_close_blocked_override",
    "source_type": "ble_leak_sensor",
    "device_id": "00:80:E1:27:9A:E6",
    "override_remaining_s": 61200
  }
}
```

*`rules_engine_evaluate_leak() main/rules_engine/rules_engine.c:474-632`*

### R5 — A 24-hour water-access override started

Started by a valve long-press ("button") or the override_enable command ("c2d_command"). remaining_s is always the full 86400.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
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

*`start_override_window() main/rules_engine/rules_engine.c:193-225; OVERRIDE_WINDOW_DURATION_S main/rules_engine/rules_engine.c:29`*

### R6 — The override window elapsed

auto_close_resumed is exactly active_leak_count > 0 sampled BEFORE any close is attempted — not a report of what happened. With auto-close disabled it still reads true and nothing closes. This message is the only trace of an expiry-driven closure.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785484800,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
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

*`rules_engine_tick() main/rules_engine/rules_engine.c:1139-1280 (expiry + re-close)`*

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
    "fw": "2.0.2",
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

*`cancel_override_window() main/rules_engine/rules_engine.c:227-247; rules_engine_cancel_override() :813-895`*

### R8 — The leak interlock was cleared

Response to leak_reset. override_cancelled appears only if a window was open. Deliberately NO source_type: the incident may have been latched by a sensor. device_id is omitted on a hub with sensors and no valve.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
    "uptime_s": 26000
  },
  "type": "event",
  "data": {
    "event": "rmleak_cleared",
    "device_id": "C4:19:D1:88:2A:7F",
    "override_cancelled": true
  }
}
```

*`rules_engine_reset_leak_incident() main/rules_engine/rules_engine.c:731-811`*

### R9 — The leak interlock cleared itself

Every source dry for 30 seconds, so the hub released the latch itself. Does NOT re-open the valve. Same identity rule as R8.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
    "uptime_s": 8700
  },
  "type": "event",
  "data": {
    "event": "rmleak_auto_cleared",
    "device_id": "C4:19:D1:88:2A:7F",
    "clear_after_seconds": 30
  }
}
```

*`rules_engine_tick() main/rules_engine/rules_engine.c:1139-1280; AUTO_CLOSE_COOLDOWN_MS main/rules_engine/rules_engine.c:17`*

## Health events

> **These carry `device_id`, not `sensor_id` or `valve_id`** — and they are the only device-identifying events that do. A health alert is raised by the hub *about* a device rather than reported *by* one, and the same builder serves valves and sensors alike, so the key is deliberately generic. `data.source_type` still tells you which kind of device it is. Note this key was `dev_type` up to firmware 1.9.0.

### H1 — A device stopped responding

A peer crossed into the critical rating. data.category "health" is how you recognise these. Critical is reached for connectivity only; a low battery gives warning.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
    "uptime_s": 9100
  },
  "type": "event",
  "data": {
    "category": "health",
    "event": "device_offline",
    "source_type": "ble_leak_sensor",
    "device_id": "00:80:E1:27:A1:04",
    "rating": "critical",
    "prev_rating": "excellent",
    "battery": 64,
    "rssi": -78,
    "offline_duration_s": 600
  }
}
```

*`health_alert_to_json() main/health_engine/health_engine.c:585-616; compute_sensor_rating() main/health_engine/health_engine.c:123-151`*

### H2 — A device came back

The recovery counterpart of H1.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
    "uptime_s": 9700
  },
  "type": "event",
  "data": {
    "category": "health",
    "event": "device_recovered",
    "source_type": "ble_leak_sensor",
    "device_id": "00:80:E1:27:A1:04",
    "rating": "excellent",
    "prev_rating": "critical",
    "battery": 64,
    "rssi": -75
  }
}
```

*`health_alert_to_json() main/health_engine/health_engine.c:585-616`*

### H3 — The valve stopped responding

H1 for the valve. rssi is ALWAYS absent, offline_duration_s is omitted when it would be zero, and prev_rating is ALWAYS "warning" — a valve gets a 3-minute grace before promotion to critical. Only sensors jump straight there.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
    "uptime_s": 5500
  },
  "type": "event",
  "data": {
    "category": "health",
    "event": "device_offline",
    "source_type": "valve",
    "device_id": "C4:19:D1:88:2A:7F",
    "rating": "critical",
    "prev_rating": "warning",
    "battery": 92,
    "offline_duration_s": 180
  }
}
```

*`rssi writers handle_lora_checkin() main/health_engine/health_engine.c:246-265 and handle_ble_leak_checkin() main/health_engine/health_engine.c:267-283; omit-on-zero health_alert_to_json() main/health_engine/health_engine.c:585-616; compute_valve_rating() main/health_engine/health_engine.c:153-176; promotion :338-346`*

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
    "fw": "2.0.2",
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

*`telemetry_v2_publish_cmd_ack() main/telemetry/telemetry_v2.c:872-897; ack gate handle_c2d_command() main/iothub/app_iothub.c:662-975`*

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
    "fw": "2.0.2",
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

*`id omission in telemetry_v2_publish_cmd_ack() main/telemetry/telemetry_v2.c:872-897`*

### C3 — A command was refused

error.code is not a code — it is the command name repeated. The free-text detail is the only real discriminator.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.0.2",
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

*`telemetry_v2_publish_cmd_ack() main/telemetry/telemetry_v2.c:872-897; message valve_open_reject_reason() main/iothub/app_iothub.c:543-557`*

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
    "fw": "2.0.2",
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

*`unknown-command branch of handle_c2d_command() main/iothub/app_iothub.c:662-975`*

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
    "fw": "2.0.2",
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
    "fw": "2.0.2",
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

