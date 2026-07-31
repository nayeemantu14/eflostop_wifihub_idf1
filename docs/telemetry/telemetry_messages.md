# eFloStop II Wi-Fi Hub — Telemetry Message Catalogue

*Every message the hub can send, as a real example*

> GENERATED FILE — produced by `docs/telemetry/build_messages.py` from `messages_data.py`.

| | |
|---|---|
| Firmware version | 1.8.0 — `CMakeLists.txt:12` |
| Git commit | `688e242439f5afe9c17f30edc12cebe1b8502635` |
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

## Index of messages

| ID | `type` | `data.event` | Message |
|---|---|---|---|
| L1 | `lifecycle` | `online` | Hub connected, fully commissioned |
| L2 | `lifecycle` | `online` | Hub connected, nothing commissioned yet |
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
| V3 | `event` | `valve_flood_detected` | The valve's own flood probe went wet |
| V4 | `event` | `valve_flood_cleared` | The valve's own flood probe went dry |
| V5 | `event` | `valve_state_changed` | Valve opened, firmware revision unknown |
| K1 | `event` | `leak_detected` | A leak sensor went wet |
| K2 | `event` | `leak_cleared` | A leak sensor went dry |
| K3 | `event` | `leak_detected` | A leak sensor with no location assigned |
| K4 | `event` | `leak_detected` | A LoRa sensor went wet |
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

Sent on every MQTT connect once the hub is provisioned — including reconnects after a network drop, so this is not once per boot.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 42
  },
  "type": "lifecycle",
  "data": {
    "event": "online",
    "reset_reason": "power_on",
    "provisioned": true,
    "valve_mac": "C4:19:D1:88:2A:7F",
    "lora_sensor_count": 0,
    "ble_leak_sensor_count": 2,
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    }
  }
}
```

*`main/telemetry/telemetry_v2.c:334-368; trigger main/iothub/app_iothub.c:1694`*

### L2 — Hub connected, nothing commissioned yet

Same trigger, but before any provisioning. The hub name is unset so gateway.name is omitted; no valve is stored so data.valve_mac is omitted; the rules object is omitted with both its children.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "fw": "1.8.0",
    "uptime_s": 18
  },
  "type": "lifecycle",
  "data": {
    "event": "online",
    "reset_reason": "power_on",
    "provisioned": true,
    "lora_sensor_count": 0,
    "ble_leak_sensor_count": 0
  }
}
```

*`omissions main/telemetry/telemetry_v2.c:82-83, :344-346, :359-364`*

### L3 — Hub reconnected after a crash

Identical shape to L1; only data.reset_reason differs. The seven possible values are power_on, software, panic, watchdog, brownout, deep_sleep and unknown.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 7
  },
  "type": "lifecycle",
  "data": {
    "event": "online",
    "reset_reason": "panic",
    "provisioned": true,
    "valve_mac": "C4:19:D1:88:2A:7F",
    "lora_sensor_count": 0,
    "ble_leak_sensor_count": 2,
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    }
  }
}
```

*`mapping main/telemetry/telemetry_v2.c:128-141`*

## Snapshot

### S1 — Routine heartbeat, everything healthy

The 300 s heartbeat. This is the most common message the hub sends.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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
      "mac": "C4:19:D1:88:2A:7F",
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

*`main/telemetry/telemetry_v2.c:372-604; interval main/telemetry/telemetry_v2.h:15`*

### S2 — Heartbeat with the valve disconnected

The BLE link to the valve is down. The valve object collapses: battery, leak_state, rmleak and fw_version are all omitted, and state carries "disconnected" instead of a position.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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
      "mac": "C4:19:D1:88:2A:7F",
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

*`disconnected branch main/telemetry/telemetry_v2.c:442-455`*

### S3 — Heartbeat with a sensor gone silent — read this one carefully

Sensor B has not been heard for over 10 minutes, so the health engine marks it disconnected and the cache merge is skipped. battery, rssi and fw_version go null, but leak_state is written as a hard-coded false. If that sensor was wet when it went silent, this message says it is dry and the earlier leak_detected event is never retracted. last_seen_age_s keeps its real value and is the only field that contradicts the false.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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
      "mac": "C4:19:D1:88:2A:7F",
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

*`placeholder main/telemetry/telemetry_v2.c:562; merge gate :541-552; F-01`*

### S4 — Snapshot while a water-access override is running

A 24-hour override window is open, so three extra keys appear. override_remaining_s and expires_ts are both omitted when no window is active.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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
      "mac": "C4:19:D1:88:2A:7F",
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

*`main/telemetry/telemetry_v2.c:594-599; window constant main/rules_engine/rules_engine.c:29`*

### S5 — First snapshot after boot

Identical shape to S1; only data.reason differs. The seven values are heartbeat, event, commission, boot, fast, decommission and — on the auto_close_reenabled rules event only — c2d_command.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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
      "mac": "C4:19:D1:88:2A:7F",
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

*`snap_reason_str main/iothub/app_iothub.c:581-591; decommission literal :1570`*

### S6 — Early snapshot as soon as the valve is ready

reason "fast". Sent before the picture is complete by design, so sensors may not have checked in yet and the arrays can be short.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 25
  },
  "type": "snapshot",
  "data": {
    "reason": "fast",
    "system_health": {
      "rating": "excellent",
      "reason": "All devices healthy"
    },
    "valve": {
      "mac": "C4:19:D1:88:2A:7F",
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
    "ble_leak_sensors": [],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": false
  }
}
```

*`main/iothub/app_iothub.c:587`*

### S7 — Final snapshot before decommissioning

Published out of band immediately before the hub restarts and forgets its commissioning. This is the last message from this identity.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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
      "mac": "C4:19:D1:88:2A:7F",
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

*`main/iothub/app_iothub.c:1570`*

## Valve events

### V1 — Valve opened

A GATT notification whose value differs from the last one seen. Delta-gated, so an unchanged notification produces nothing.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 4000
  },
  "type": "event",
  "data": {
    "event": "valve_state_changed",
    "valve_state": "open",
    "battery": 92,
    "leak_state": false,
    "rmleak": false,
    "fw_version": "2.2.0"
  }
}
```

*`main/telemetry/telemetry_v2.c:608-629; gate main/iothub/app_iothub.c:1782-1791`*

### V2 — Valve closed and locked after a leak

The same shape as V1. rmleak true means the valve is latched and will refuse to open until the interlock is cleared.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 4020
  },
  "type": "event",
  "data": {
    "event": "valve_state_changed",
    "valve_state": "closed",
    "battery": 92,
    "leak_state": false,
    "rmleak": true,
    "fw_version": "2.2.0"
  }
}
```

*`main/telemetry/telemetry_v2.c:608-629`*

### V3 — The valve's own flood probe went wet

The valve has a probe of its own, separate from the leak sensors. Note this event carries no identifier — it is implicitly about the single valve this hub owns.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 4100
  },
  "type": "event",
  "data": {
    "event": "valve_flood_detected",
    "valve_state": "open",
    "battery": 92,
    "leak_state": true,
    "rmleak": false,
    "fw_version": "2.2.0"
  }
}
```

*`main/iothub/app_iothub.c:1778-1779`*

### V4 — The valve's own flood probe went dry

The clearing counterpart of V3.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 4600
  },
  "type": "event",
  "data": {
    "event": "valve_flood_cleared",
    "valve_state": "closed",
    "battery": 92,
    "leak_state": false,
    "rmleak": true,
    "fw_version": "2.2.0"
  }
}
```

*`main/iothub/app_iothub.c:1778-1779`*

### V5 — Valve opened, firmware revision unknown

Identical to V1 except data.fw_version is omitted, which happens when the DIS read failed at setup. There is NO null variant on this path — the key is simply absent.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 4700
  },
  "type": "event",
  "data": {
    "event": "valve_state_changed",
    "valve_state": "open",
    "battery": 92,
    "leak_state": false,
    "rmleak": false
  }
}
```

*`main/telemetry/telemetry_v2.c:623-625`*

## Leak events

### K1 — A leak sensor went wet

The safety-critical message. Emitted when a BLE advertisement reports a leak state different from the cached one. Note the sensor's fields are FLAT on data here, whereas a snapshot nests the same sensor inside ble_leak_sensors[].

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 8000
  },
  "type": "event",
  "data": {
    "event": "leak_detected",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:9A:E6",
    "leak_state": true,
    "battery": 87,
    "rssi": -64,
    "location": {
      "code": "kitchen",
      "label": "Under sink"
    }
  }
}
```

*`main/telemetry/telemetry_v2.c:631-654; trigger main/iothub/app_iothub.c:1807-1811`*

### K2 — A leak sensor went dry

The clearing counterpart of K1.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 8600
  },
  "type": "event",
  "data": {
    "event": "leak_cleared",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:9A:E6",
    "leak_state": false,
    "battery": 87,
    "rssi": -66,
    "location": {
      "code": "kitchen",
      "label": "Under sink"
    }
  }
}
```

*`main/iothub/app_iothub.c:1807-1811`*

### K3 — A leak sensor with no location assigned

Identical to K1 except the location object falls back. The key is always present on this path; code becomes "unknown" and label an empty string.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 8100
  },
  "type": "event",
  "data": {
    "event": "leak_detected",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:A1:04",
    "leak_state": true,
    "battery": 64,
    "rssi": -78,
    "location": {
      "code": "unknown",
      "label": ""
    }
  }
}
```

*`fallback main/telemetry/telemetry_v2.c:143-153`*

### K4 — A LoRa sensor went wet

Same shape as K1 with source_type "lora" and a hex sensor id. NOT SEEN IN PRACTICE: the LoRa radio is not populated on the current PCBA, so no LoRa sensor can report. Included because the firmware path exists.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 8200
  },
  "type": "event",
  "data": {
    "event": "leak_detected",
    "source_type": "lora",
    "sensor_id": "0x1A2B3C4D",
    "leak_state": true,
    "battery": 78,
    "rssi": -95,
    "location": {
      "code": "basement",
      "label": "Sump"
    }
  }
}
```

*`main/iothub/app_iothub.c:1745-1750`*

## Rules events

### R1 — Hub closed the valve because a sensor reported a leak

The automatic shut-off. rmleak_asserted is always true here — it is a presence marker, not a variable.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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

*`main/rules_engine/rules_engine.c:328-357`*

### R2 — Hub closed the valve because its own flood probe went wet

Same shape as R1 but with source_type "valve_flood", sensor_id "valve", and NO location object — location is omitted entirely for the valve-flood source.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 4105
  },
  "type": "event",
  "data": {
    "event": "auto_close",
    "source_type": "valve_flood",
    "sensor_id": "valve",
    "rmleak_asserted": true
  }
}
```

*`main/rules_engine/rules_engine.c:338-349; pseudo-id main/iothub/app_iothub.c:1650`*

### R3 — Hub closed the valve on reconnect, finding a leak still active

A structurally DIFFERENT auto_close. It carries active_leak_count and no location, and source_type is "reconnect", which is not a member of the leak-source enum. Nothing in the firmware reconciles the two shapes.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 400
  },
  "type": "event",
  "data": {
    "event": "auto_close",
    "source_type": "reconnect",
    "sensor_id": "00:80:E1:27:9A:E6",
    "rmleak_asserted": true,
    "active_leak_count": 1
  }
}
```

*`main/rules_engine/rules_engine.c:932-943`*

### R4 — A leak occurred but auto-close was suppressed

A user override window is open, so the hub deliberately did not close the valve. override_remaining_s is omitted if the remaining time is negative.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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

*`main/rules_engine/rules_engine.c:467-478`*

### R5 — A 24-hour water-access override started

Started either by a long press on the valve (trigger "button") or by the override_enable command (trigger "c2d_command"). remaining_s is always the full window constant, 86400.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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

*`main/rules_engine/rules_engine.c:213-222; constant :29`*

### R6 — The override window elapsed

auto_close_resumed says whether leaks were still active when the window ended — if so the hub immediately re-closes the valve.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785484800,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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

*`main/rules_engine/rules_engine.c:1023-1032`*

### R7 — The override was cancelled by command

Sent in response to override_cancel. reason is always "c2d_command" here — note this key carries a completely different vocabulary from data.reason on a snapshot.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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

*`main/rules_engine/rules_engine.c:718-726`*

### R8 — The leak interlock was cleared

Sent in response to leak_reset. override_cancelled appears only if an override window was open at the time; otherwise the key is absent and this message is just the event name.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 26000
  },
  "type": "event",
  "data": {
    "event": "rmleak_cleared",
    "override_cancelled": true
  }
}
```

*`main/rules_engine/rules_engine.c:676-685`*

### R9 — The leak interlock cleared itself

Every leak source has been dry for 30 seconds, so the hub released the interlock on its own. This clears the latch but does NOT re-open the valve.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 8700
  },
  "type": "event",
  "data": {
    "event": "rmleak_auto_cleared",
    "clear_after_seconds": 30
  }
}
```

*`main/rules_engine/rules_engine.c:1080-1087; constant :18`*

## Health events

### H1 — A device stopped responding

A peer crossed into the critical health rating. These are the only events carrying data.category "health" — that key is how you recognise them. Critical is only ever reached for connectivity reasons; a low battery gives warning, never critical.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 9100
  },
  "type": "event",
  "data": {
    "category": "health",
    "event": "device_offline",
    "dev_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:A1:04",
    "rating": "critical",
    "prev_rating": "excellent",
    "battery": 64,
    "rssi": -78,
    "offline_duration_s": 600
  }
}
```

*`main/health_engine/health_engine.c:577-608; rating :118-124`*

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
    "fw": "1.8.0",
    "uptime_s": 9700
  },
  "type": "event",
  "data": {
    "category": "health",
    "event": "device_recovered",
    "dev_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:A1:04",
    "rating": "excellent",
    "prev_rating": "critical",
    "battery": 64,
    "rssi": -75
  }
}
```

*`main/health_engine/health_engine.c:586-588`*

### H3 — The valve stopped responding

Same shape with dev_type "valve". data.rssi is ALWAYS absent for the valve — the hub never records an RSSI for it — and offline_duration_s is omitted when it would be zero.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 5500
  },
  "type": "event",
  "data": {
    "category": "health",
    "event": "device_offline",
    "dev_type": "valve",
    "sensor_id": "C4:19:D1:88:2A:7F",
    "rating": "critical",
    "prev_rating": "excellent",
    "battery": 92,
    "offline_duration_s": 180
  }
}
```

*`rssi writers only at main/health_engine/health_engine.c:249, :267; omit-on-zero :598-603`*

## Command acknowledgements

### C1 — A command succeeded

Every enveloped command, or any command carrying a correlation id, produces exactly one of these. data.id echoes the id you sent.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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

*`main/telemetry/telemetry_v2.c:695-718; gate main/iothub/app_iothub.c:932-935`*

### C2 — A command succeeded, no correlation id supplied

If the inbound command carried no id, data.id is OMITTED — it is not an empty string. You can then only match the ack by cmd and timing.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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

*`main/telemetry/telemetry_v2.c:705-706`*

### C3 — A command was refused

On failure an error object appears. Note error.code is NOT an error code — it is the command name repeated. The only real discriminator is the free-text detail.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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

*`main/telemetry/telemetry_v2.c:707-713; message main/iothub/app_iothub.c:539-540`*

### C4 — An unrecognised command

Any command name the hub does not know produces this. The detail is a fixed string.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
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

*`main/iothub/app_iothub.c:928-935`*

## Fallback messages

### F1 — A rules payload could not be re-parsed

A defensive path. If the rules engine's own JSON fails to re-parse, the hub sends the raw string instead under a fallback event name. You should never see this; if you do, something is wrong on the device.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 31000
  },
  "type": "event",
  "data": {
    "event": "rules_engine",
    "raw": "{\"event\":\"auto_close\",\"source_type\":\"ble_lea"
  }
}
```

*`main/telemetry/telemetry_v2.c:656-674`*

### F2 — A health payload could not be re-parsed

The health-engine counterpart of F1, with the same caveat.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 31100
  },
  "type": "event",
  "data": {
    "event": "health_engine",
    "raw": "{\"category\":\"health\",\"event\":\"device_off"
  }
}
```

*`main/telemetry/telemetry_v2.c:676-693`*

