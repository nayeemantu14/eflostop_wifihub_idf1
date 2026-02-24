# eFloStop WiFi Hub - Application Scope Document

## For App Developers Building Mobile/Web Clients

---

# 1. Product Overview

## 1.1 What is the eFloStop WiFi Hub?

The eFloStop WiFi Hub is an ESP32-S3 based IoT gateway designed for residential and commercial water leak detection and automatic valve control. It acts as a central bridge between field devices (leak sensors, motorized water valve) and the Azure IoT cloud, enabling real-time monitoring and remote control from a companion mobile or web application.

## 1.2 Core Value Proposition

- **Detect water leaks** from multiple sensor types (BLE and LoRa) placed throughout a property.
- **Automatically close the main water valve** when a leak is detected, preventing water damage.
- **Enable remote valve control** from anywhere via the cloud.
- **Provide real-time visibility** into system health, device status, and sensor readings.

## 1.3 System Topology

```
[Mobile/Web App]
       |
       v
[Azure IoT Hub] <--MQTT/TLS--> [eFloStop WiFi Hub (ESP32-S3)]
                                       |          |         |
                                      BLE        LoRa     GPIO
                                       |          |         |
                                  [Valve]   [Sensors]   [LED/Button]
```

The app does NOT communicate directly with the hub. All communication flows through Azure IoT Hub using Cloud-to-Device (C2D) messages for commands and Device-to-Cloud (D2C) telemetry for data.

---

# 2. Device Capabilities and Limits

## 2.1 Supported Peripherals

| Peripheral | Protocol | Max Count | Description |
|---|---|---|---|
| Water Valve | BLE (NimBLE) | 1 | Motorized ball valve with battery, leak sensor, and RMLEAK interlock |
| LoRa Leak Sensors | LoRa (SX1276/RadioLib) | 16 | Battery-powered leak detectors with AES-128-CCM encrypted packets |
| BLE Leak Sensors | BLE (passive scan) | 16 | Battery-powered BLE advertisement-based leak detectors |
| RGB Status LED | WS2812 (GPIO 38) | 1 | On-board visual status indicator |
| WiFi Reset Button | GPIO 40 | 1 | Physical button, hold 5 seconds to erase WiFi credentials |

## 2.2 Connectivity

| Interface | Details |
|---|---|
| WiFi | 2.4 GHz, managed by captive portal for setup |
| Cloud | Azure IoT Hub over MQTT/TLS with SAS token authentication |
| Device Provisioning | Azure DPS (Device Provisioning Service) with group enrollment |
| Time Sync | SNTP (pool.ntp.org), timezone set to AEST-10 |

## 2.3 Flash Partition Layout

| Partition | Size | Purpose |
|---|---|---|
| nvs | 16 KB | Provisioning config, rules, sensor metadata, offline buffer, DPS cache |
| factory | 2 MB | Factory firmware |
| ota_0 | 2 MB | OTA update slot A |
| ota_1 | 2 MB | OTA update slot B |

The hub supports over-the-air (OTA) firmware updates via the dual OTA partition scheme.

---

# 3. Provisioning and Device Lifecycle

## 3.1 First-Time Setup Flow

1. **Power on**: Hub boots unprovisioned, starts WiFi captive portal (AP mode).
2. **WiFi configuration**: User connects to the hub's AP and enters home WiFi credentials via a captive portal web page.
3. **Cloud registration**: Hub performs Azure DPS registration using its gateway ID (derived from WiFi MAC: `GW-XXXXXXXXXXXX`), receives IoT Hub hostname and device key. These are cached in NVS.
4. **Cloud connection**: Hub connects to Azure IoT Hub over MQTT/TLS.
5. **Provisioning command**: The backend sends a `provision` C2D command with valve MAC, LoRa sensor IDs, and BLE leak sensor MACs.
6. **Operational**: Hub begins monitoring sensors and controlling the valve.

## 3.2 Provisioning States

| State | Description |
|---|---|
| `UNPROVISIONED` | No devices assigned. Hub is connected to cloud but idle. Waiting for provisioning payload. |
| `PROVISIONED` | At least one device (valve, LoRa sensor, or BLE leak sensor) is assigned. Hub is operational. |

## 3.3 Gateway Identity

The gateway ID is deterministic and derived from the ESP32 WiFi station MAC address:

```
Format: GW-{MAC[0]}{MAC[1]}{MAC[2]}{MAC[3]}{MAC[4]}{MAC[5]}
Example: GW-34B7DA6AAD54
```

This ID is used as the Azure DPS registration ID and appears in all telemetry messages.

---

# 4. Cloud Communication Architecture

## 4.1 Protocol Stack

```
Application     : JSON telemetry / JSON commands
Transport       : MQTT v3.1.1 (QoS 1)
Security        : TLS 1.2 with Azure certificate bundle
Authentication  : SAS token (HMAC-SHA256, 1-year expiry)
Provisioning    : Azure DPS with group SAS key enrollment
```

## 4.2 MQTT Topics

| Direction | Topic Pattern | Purpose |
|---|---|---|
| D2C (telemetry) | `devices/{device_id}/messages/events/` | All telemetry from hub to cloud |
| C2D (commands) | `devices/{device_id}/messages/devicebound/#` | Commands from cloud to hub |
| Twin reported | `$iothub/twin/PATCH/properties/reported/?$rid={n}` | Hub reports its state |
| Twin desired | `$iothub/twin/PATCH/properties/desired/#` | Cloud pushes config changes |

## 4.3 Connection Lifecycle

1. WiFi connects -> IoT Hub task is notified
2. DPS registration (or NVS cache hit)
3. MQTT connect to assigned IoT Hub
4. On MQTT_CONNECTED:
   - Subscribe to C2D and Device Twin topics
   - Drain any offline-buffered events
   - Publish lifecycle telemetry
   - Publish Device Twin reported properties
   - Wait for boot sync (all sensors check in or 2-minute timeout)
   - Publish initial snapshot
5. Enter event loop (process sensor data, publish telemetry, handle commands)

---

# 5. Telemetry Schema (D2C)

All telemetry uses the `eflostop.v2` schema. Every message shares a common envelope.

## 5.1 Common Envelope

```json
{
  "schema": "eflostop.v2",
  "ts": 1770589401,
  "gateway": {
    "id": "GW-34B7DA6AAD54",
    "fw": "1.3.0",
    "uptime_s": 971
  },
  "type": "<message_type>",
  "data": { ... }
}
```

| Field | Type | Description |
|---|---|---|
| `schema` | string | Always `"eflostop.v2"` |
| `ts` | number | Unix timestamp (seconds since epoch) |
| `gateway.id` | string | Gateway identifier |
| `gateway.fw` | string | Firmware version (currently `"1.3.0"`) |
| `gateway.uptime_s` | number | Seconds since last boot |
| `type` | string | One of: `"lifecycle"`, `"snapshot"`, `"event"` |
| `data` | object | Type-specific payload |

## 5.2 Message Type: lifecycle

Published on every MQTT connect/reconnect. Provides the hub's boot state.

```json
{
  "type": "lifecycle",
  "data": {
    "event": "online",
    "reset_reason": "power_on",
    "provisioned": true,
    "valve_mac": "00:80:E1:27:F7:BB",
    "lora_sensor_count": 1,
    "ble_leak_sensor_count": 5,
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    }
  }
}
```

**Reset reasons**: `power_on`, `software`, `panic`, `watchdog`, `brownout`, `deep_sleep`, `unknown`

## 5.3 Message Type: snapshot

Published periodically (default every 5 minutes, configurable 60-3600s via Device Twin). Contains the full state of all devices.

```json
{
  "type": "snapshot",
  "data": {
    "system_health": {
      "rating": "excellent",
      "reason": "All devices healthy"
    },
    "valve": {
      "mac": "00:80:E1:27:F7:BB",
      "state": "open",
      "battery": 85,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "rating": "excellent",
      "last_seen_age_s": 12
    },
    "lora_sensors": [
      {
        "sensor_id": "0x754A6237",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 45,
        "battery": 92,
        "leak_state": false,
        "rssi": -67,
        "snr": 8.5,
        "location": {
          "code": "basement",
          "label": "Water heater area"
        }
      }
    ],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:27:99:E7",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 30,
        "battery": 78,
        "leak_state": false,
        "rssi": -55,
        "location": {
          "code": "laundry",
          "label": "Downstairs laundry"
        }
      }
    ],
    "override_active": false
  }
}
```

### Snapshot Data Field Reference

**system_health**:

| Field | Type | Values |
|---|---|---|
| `rating` | string | `"excellent"`, `"good"`, `"warning"`, `"critical"` |
| `reason` | string | Human-readable explanation (e.g., "Valve offline", "2 sensors battery low") |

**valve**:

| Field | Type | Description |
|---|---|---|
| `mac` | string | BLE MAC address |
| `state` | string | `"open"`, `"closed"`, `"unknown"`, `"disconnected"` |
| `battery` | number | 0-100 percent |
| `leak_state` | bool | true if valve's built-in flood sensor detects water |
| `rmleak` | bool | true if RMLEAK interlock is asserted (valve locked closed) |
| `connected` | bool | true if BLE connection is active |
| `rating` | string | Health rating for this device |
| `last_seen_age_s` | number/null | Seconds since last BLE communication, null if never seen |

**lora_sensors[] / ble_leak_sensors[]**:

| Field | Type | Description |
|---|---|---|
| `sensor_id` | string | `"0xHEXID"` for LoRa, `"XX:XX:XX:XX:XX:XX"` for BLE |
| `connected` | bool | true if sensor has checked in and is not timed out |
| `rating` | string | Health rating |
| `last_seen_age_s` | number/null | Seconds since last data received |
| `battery` | number/null | 0-100 percent, null if unknown |
| `leak_state` | bool | true if leak is detected |
| `rssi` | number/null | Signal strength in dBm |
| `snr` | number/null | Signal-to-noise ratio (LoRa only) |
| `location.code` | string | Location category code |
| `location.label` | string | User-defined label (max 31 chars) |

## 5.4 Message Type: event

Published in real time when a state change occurs. The `data.event` field identifies the specific event.

### 5.4.1 Valve Events

| Event Name | Trigger |
|---|---|
| `valve_state_changed` | Valve opened or closed |
| `valve_flood_detected` | Valve's built-in flood sensor activated |
| `valve_flood_cleared` | Valve's built-in flood sensor cleared |

Payload:
```json
{
  "event": "valve_state_changed",
  "valve_state": "closed",
  "battery": 85,
  "leak_state": false,
  "rmleak": true
}
```

### 5.4.2 Leak Events

| Event Name | Trigger |
|---|---|
| `leak_detected` | A sensor reports a new leak |
| `leak_cleared` | A previously leaking sensor reports clear |

Payload:
```json
{
  "event": "leak_detected",
  "source_type": "ble_leak_sensor",
  "sensor_id": "00:80:E1:27:99:E7",
  "leak_state": true,
  "battery": 78,
  "rssi": -55,
  "location": {
    "code": "laundry",
    "label": "Downstairs laundry"
  }
}
```

`source_type` values: `"lora"`, `"ble_leak_sensor"`

### 5.4.3 Rules Engine Events

Published by the automatic valve control system.

| Event Name | Trigger |
|---|---|
| `auto_close` | Rules engine automatically closed the valve due to a leak |
| `rmleak_cleared` | RMLEAK interlock was cleared via `leak_reset` command |
| `auto_close_reenabled` | 24h override window was cancelled (includes `remaining_s`) |

### 5.4.4 Health Alert Events

Published when a device's health rating changes (e.g., sensor goes offline, battery drops critically low).

```json
{
  "event": "health_alert",
  "dev_type": "lora",
  "dev_id": "0x754A6237",
  "new_rating": "critical",
  "old_rating": "excellent",
  "battery": 92,
  "rssi": -67,
  "offline_duration_s": 650
}
```

### 5.4.5 Command Acknowledgment Events

Published when a C2D command with a correlation ID completes.

Success:
```json
{
  "event": "cmd_ack",
  "id": "open-001",
  "cmd": "valve_open",
  "status": "ok"
}
```

Error:
```json
{
  "event": "cmd_ack",
  "id": "prov-002",
  "cmd": "provision",
  "status": "error",
  "error": {
    "code": "provision",
    "detail": "provisioning failed"
  }
}
```

---

# 6. Commands (C2D) - App to Hub

## 6.1 Command Envelope Format

All new commands should use the canonical envelope:

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "<correlation-id>",
  "cmd": "<command_name>",
  "payload": { ... }
}
```

Always include an `id` field (UUID or unique string) to receive a `cmd_ack` response confirming execution.

## 6.2 Command Reference

### 6.2.1 valve_open

Opens the water valve. If BLE is disconnected, the hub will attempt to reconnect first.

```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "open-001", "cmd": "valve_open" }
```

### 6.2.2 valve_close

Closes the water valve.

```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "close-001", "cmd": "valve_close" }
```

### 6.2.3 valve_set_state

Combined open/close command with explicit state parameter.

```json
{
  "schema": "eflostop.cmd", "ver": 1, "id": "vs-001",
  "cmd": "valve_set_state",
  "payload": { "state": "open" }
}
```

`state` must be `"open"` or `"closed"`.

### 6.2.4 leak_reset

Clears the leak incident latch and removes the RMLEAK interlock on the valve. Does NOT open the valve; a separate `valve_open` or `valve_set_state` command is required after reset.

```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "reset-001", "cmd": "leak_reset" }
```

### 6.2.5 override_cancel

Cancels the 24-hour Water Access Override window. Re-enables automatic valve closure immediately. If leaks are actively detected, the valve closes right away.

```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "ovr-001", "cmd": "override_cancel" }
```

### 6.2.6 provision

Assigns devices to the hub. Each field replaces that entire category. Omitted categories are left unchanged.

```json
{
  "schema": "eflostop.cmd", "ver": 1, "id": "prov-001",
  "cmd": "provision",
  "payload": {
    "valve_mac": "00:80:E1:27:F7:BB",
    "lora_sensors": ["0x754A6237"],
    "ble_leak_sensors": [
      "00:80:E1:27:99:E7",
      "00:80:E1:2A:AD:6D"
    ]
  }
}
```

Limits: 1 valve, up to 16 LoRa sensors, up to 16 BLE leak sensors.

### 6.2.7 decommission

Removes devices. The `target` field specifies what to remove.

| Target | Description | Requires |
|---|---|---|
| `"valve"` | Remove valve, disconnect BLE | nothing |
| `"lora"` | Remove one LoRa sensor | `sensor_id` (hex string) |
| `"ble"` | Remove one BLE leak sensor | `sensor_id` (MAC string) |
| `"all"` | **Factory reset** - wipes everything, restarts device | nothing |

```json
{
  "schema": "eflostop.cmd", "ver": 1, "id": "decom-001",
  "cmd": "decommission",
  "payload": { "target": "all" }
}
```

**WARNING**: `target: "all"` erases all NVS data and restarts the device. It will boot into captive portal mode.

### 6.2.8 rules_config

Updates the automatic valve closure rules. Uses merge semantics (only included fields change).

```json
{
  "schema": "eflostop.cmd", "ver": 1, "id": "rules-001",
  "cmd": "rules_config",
  "payload": {
    "auto_close_enabled": true,
    "trigger_mask": 7
  }
}
```

**trigger_mask** bitmask:

| Bit | Value | Source |
|---|---|---|
| 0 | 1 | BLE leak sensors |
| 1 | 2 | LoRa leak sensors |
| 2 | 4 | Valve built-in flood sensor |
| all | 7 | All sources |

### 6.2.9 sensor_meta

Assigns location metadata to a sensor for display purposes.

```json
{
  "schema": "eflostop.cmd", "ver": 1, "id": "meta-001",
  "cmd": "sensor_meta",
  "payload": {
    "sensor_type": "ble",
    "sensor_id": "00:80:E1:27:99:E7",
    "location_code": "laundry",
    "label": "Downstairs laundry"
  }
}
```

**sensor_type**: `"ble"` or `"lora"`

**location_code** values: `"bathroom"`, `"kitchen"`, `"laundry"`, `"garage"`, `"garden"`, `"basement"`, `"utility"`, `"hallway"`, `"bedroom"`, `"living_room"`, `"attic"`, `"outdoor"`

**label**: Free text, max 31 characters.

---

# 7. Device Twin

The hub uses Azure Device Twin for configuration and state reporting.

## 7.1 Reported Properties (Hub -> Cloud)

Updated on every MQTT connect and after provisioning changes.

```json
{
  "fw_version": "1.3.0",
  "gateway_id": "GW-34B7DA6AAD54",
  "provisioned": true,
  "valve_mac": "00:80:E1:27:F7:BB",
  "lora_sensor_count": 1,
  "ble_leak_sensor_count": 5,
  "auto_close_enabled": true,
  "trigger_mask": 7,
  "uptime_s": 12345,
  "free_heap": 125000
}
```

## 7.2 Desired Properties (Cloud -> Hub)

| Property | Type | Range | Description |
|---|---|---|---|
| `snapshot_interval_s` | integer | 60 - 3600 | Telemetry snapshot publish interval in seconds (default: 300) |

---

# 8. Rules Engine (Automatic Valve Control)

## 8.1 How Auto-Close Works

1. A leak event arrives from any sensor (BLE, LoRa, or valve flood sensor).
2. The rules engine checks if `auto_close_enabled` is true and the sensor type's bit is set in `trigger_mask`.
3. If conditions are met and no override window is active: the valve is closed and RMLEAK interlock is asserted.
4. An `auto_close` telemetry event is published.

## 8.2 The 24-Hour Override Window

When a user physically presses the valve button to override an auto-close:

1. RMLEAK is cleared on the valve hardware side.
2. The hub detects this and starts a 24-hour override window.
3. During the window: leaks are still detected and reported, but auto-close is suppressed.
4. After 24 hours, auto-close rules resume automatically.
5. The app can cancel the window early via the `override_cancel` command.

## 8.3 Leak Incident Lifecycle

```
NORMAL --> leak_detected --> auto_close (valve closed + RMLEAK set)
  ^                              |
  |                              v
  +--- leak_reset cmd ---- INCIDENT ACTIVE (valve locked)
       (clears latch)       User must then send valve_open to resume water
```

## 8.4 Valve Reconnect Reconciliation

If BLE disconnects and reconnects, the rules engine re-evaluates the current state:

1. If override window is active: skip auto-close but sync RMLEAK state.
2. If any sensors are actively leaking and auto-close is enabled: close valve + assert RMLEAK.
3. Otherwise: synchronize hub/valve RMLEAK state for consistency.

---

# 9. Health Monitoring System

## 9.1 Health Ratings

Each provisioned device is assigned a health rating:

| Rating | Meaning |
|---|---|
| `excellent` | All parameters nominal |
| `good` | Minor degradation (e.g., signal slightly weak) |
| `warning` | Battery below 20% or signal below -90 dBm |
| `critical` | Device offline (no data received within timeout) |

## 9.2 System Health Rating

The overall system health is the worst rating among all provisioned devices. The snapshot includes a human-readable `reason` string (e.g., "Valve offline", "2 sensors battery low").

## 9.3 Timeout Thresholds

| Device Type | Timeout | Description |
|---|---|---|
| LoRa sensor | 10 minutes | No packet received |
| BLE leak sensor | 10 minutes | No advertisement received |
| Valve (grace) | 3 minutes | BLE disconnected, rated "warning" |
| Valve (critical) | 3 minutes (after grace) | BLE disconnected, rated "critical" |

## 9.4 Health Alert Events

Alerts are generated on rating transitions (e.g., excellent -> critical). Includes debounce (60 seconds minimum between alerts per device).

## 9.5 Boot Sync

After reboot, the health engine waits up to 2 minutes for all provisioned devices to check in before publishing the first snapshot. This prevents false "offline" alerts at startup.

---

# 10. Offline Resilience

## 10.1 Event Buffering

When MQTT is disconnected, critical events (leak detected, auto-close, etc.) are stored in an NVS ring buffer:

- Maximum 16 events
- Maximum 512 bytes per event JSON
- Oldest events are overwritten when the buffer is full
- FIFO replay on reconnect (oldest first, before lifecycle message)

## 10.2 What Gets Buffered vs. Dropped

| Message Type | Offline Behavior |
|---|---|
| `event` (leak, auto-close, cmd_ack, health) | Buffered to NVS |
| `lifecycle` | Dropped (regenerated on reconnect) |
| `snapshot` | Dropped (regenerated on reconnect) |

## 10.3 Rules Engine Offline Behavior

The rules engine operates fully offline. Leak evaluation and automatic valve closure happen locally on the device regardless of cloud connectivity. The events are buffered for later delivery.

---

# 11. WiFi Management

## 11.1 Captive Portal

On first boot or after WiFi credentials are erased, the hub creates a WiFi access point with a captive portal. The user connects to this AP and configures WiFi credentials through a web interface.

## 11.2 WiFi Watchdog

| Parameter | Value | Description |
|---|---|---|
| Max disconnect time | 60 seconds | Time before forcing AP mode |
| Max watchdog triggers | 3 | Reboots before halting (prevents infinite loops) |
| Boot counter | NVS-persisted | Tracks consecutive watchdog-triggered reboots |

## 11.3 Physical Reset Button

- **GPIO 40** with internal pull-up
- **Hold for 5 seconds** to erase WiFi credentials and restart into captive portal mode

---

# 12. LoRa Communication

## 12.1 Security

All LoRa packets are encrypted with AES-128-CCM:

- Per-sensor keys derived from a master secret using a KDF
- 13-byte nonce constructed from sensor ID, boot random, and frame counter
- 4-byte MIC (authentication tag) for integrity verification
- Replay protection via frame counter tracking

## 12.2 Packet Structure

Each LoRa sensor transmission contains:

| Field | Description |
|---|---|
| `sensorId` | 4-byte unique sensor identifier |
| `batteryPercentage` | 0-100% battery level |
| `leakStatus` | 0 = dry, non-zero = leak detected |
| `frameSent` | Transmission counter (replay protection) |
| `frameAck` | Acknowledged frame counter |
| `rssi` | Received signal strength |
| `snr` | Signal-to-noise ratio |

## 12.3 Radio Hardware

The hub uses an SX1276-compatible LoRa transceiver connected via SPI, managed through the RadioLib library. The UART interface (TX: GPIO 37, RX: GPIO 36) is also configured but secondary to the SPI radio.

---

# 13. BLE Communication

## 13.1 Valve BLE Connection

The hub maintains a persistent BLE connection to the motorized valve using NimBLE:

- **Pairing**: Encrypted and authenticated (MITM protection) with bonding
- **Security state machine**: Connected -> Pairing -> Encrypted -> Authenticated -> Bonded -> Service Discovery -> GATT Ready
- **Auto-reconnect**: Hub automatically reconnects if BLE drops
- **Command queue**: Valve commands (open, close, RMLEAK) are queued and executed over GATT

### Valve BLE Characteristics

| Characteristic | Direction | Description |
|---|---|---|
| Battery | Read/Notify | Battery percentage (0-100) |
| Valve State | Read/Notify | 0 = closed, 1 = open |
| Leak Detect | Read/Notify | Built-in flood sensor |
| RMLEAK | Read/Write/Notify | Interlock: 1 = assert (lock valve closed), 0 = clear |
| Control | Write | Open/close valve commands |

## 13.2 BLE Leak Sensor Scanning

The hub passively scans BLE advertisements for provisioned leak sensor MACs:

- Advertisements are parsed for battery and leak status data
- Only provisioned sensor MACs are processed
- Events are sent to the IoT Hub task via a FreeRTOS queue
- Change detection: only leak state transitions generate telemetry events

---

# 14. App Developer Integration Guide

## 14.1 Sending Commands from the App

The app backend should:

1. Build a JSON command envelope with a unique correlation ID.
2. Send it as a C2D message to the device's Azure IoT Hub identity.
3. Listen for the `cmd_ack` telemetry event with the matching correlation ID.
4. Handle timeout (hub may be offline - the command will be delivered when it reconnects).

### Recommended Correlation ID Strategy

Use UUIDs or structured IDs like `{user_id}-{action}-{timestamp}` to enable request tracing.

## 14.2 Consuming Telemetry in the App

The app backend should:

1. Route D2C messages from Azure IoT Hub (via Event Hub, Azure Functions, etc.).
2. Parse the `eflostop.v2` envelope and route based on `type` and `data.event`.
3. Use snapshots to rebuild full device state (useful after app startup).
4. Use events for real-time notifications (leak detected, valve state change, health alerts).

## 14.3 User-Facing App Screens (Recommended)

Based on the hub's capabilities, the app should expose:

| Screen | Data Source | Actions |
|---|---|---|
| **Dashboard** | Latest snapshot | Valve open/close, system health overview |
| **Sensors** | Snapshot sensor arrays | View battery, leak state, signal, location per sensor |
| **Alerts** | Health alert events + leak events | Push notifications for leaks and device issues |
| **Valve Control** | Snapshot valve object | Open, close, view battery/state/RMLEAK |
| **Leak History** | Event stream (leak_detected/cleared, auto_close) | Timeline of incidents |
| **Settings** | Device Twin + rules_config | Auto-close on/off, trigger mask, snapshot interval |
| **Sensor Setup** | Provision/decommission commands | Add/remove sensors, assign locations |
| **System Info** | Lifecycle + snapshot | Firmware version, uptime, gateway ID, WiFi status |

## 14.4 Key UX Considerations

1. **Valve open after leak reset**: The `leak_reset` command only clears the latch. Show the user a two-step flow: "Reset Leak" then "Open Valve".
2. **Decommission confirmation**: Always show a confirmation dialog before `decommission`, especially for `target: "all"` (factory reset).
3. **Override window display**: When `override_active` is true in snapshots, show the remaining time and offer a "Cancel Override" button.
4. **Offline indicator**: If the hub hasn't sent a snapshot within 2x the snapshot interval, show it as potentially offline.
5. **Health badges**: Map the four health ratings to color-coded badges (green/blue/yellow/red).

## 14.5 Command Validation Rules (App-Side)

The app should validate before sending commands to prevent errors:

| Command | Validation |
|---|---|
| `provision` | Valve MAC format: `XX:XX:XX:XX:XX:XX`. LoRa IDs: `0x` + 8 hex digits. Max 16 of each sensor type. |
| `decommission` (lora/ble) | Require `sensor_id` field. |
| `valve_set_state` | `state` must be `"open"` or `"closed"`. |
| `rules_config` | `trigger_mask` must be 0-7. |
| `sensor_meta` | `sensor_type` must be `"ble"` or `"lora"`. `label` max 31 chars. `location_code` must be from the defined list. |

## 14.6 Error Handling

All command errors are returned in `cmd_ack` events. Common patterns:

| Scenario | App Behavior |
|---|---|
| `cmd_ack` with `status: "ok"` | Show success confirmation |
| `cmd_ack` with `status: "error"` | Show `error.detail` to user |
| No `cmd_ack` within timeout | Show "Command may not have been received, hub could be offline" |
| Hub reboots (decommission all) | Expect no ack (sent before restart). Watch for lifecycle event. |

---

# 15. Security Considerations

## 15.1 Authentication Boundary

The security boundary is the Azure IoT Hub connection. Commands are authenticated through the device's SAS token. There is no additional command-level authentication on the hub.

## 15.2 Dangerous Commands

| Command | Risk | Mitigation |
|---|---|---|
| `decommission` (target: all) | High - factory reset | Require explicit user confirmation in app |
| `decommission` (target: valve) | Medium - removes leak protection | Warn user that auto-close will be disabled |
| `provision` | Medium - replaces device identities | Only allow from authenticated backend, never from raw user input |

## 15.3 LoRa Encryption

LoRa sensor data is encrypted end-to-end (sensor to hub) using AES-128-CCM with per-sensor derived keys. The cloud/app never handles raw LoRa packets.

## 15.4 BLE Security

The valve BLE connection uses encrypted and authenticated pairing with bonding (MITM protection). BLE leak sensors use passive advertisement scanning (no pairing required).

---

# 16. Firmware and Device Metadata

| Property | Value |
|---|---|
| Firmware version | 1.3.0 |
| Telemetry schema | eflostop.v2 |
| Command schema | eflostop.cmd (ver: 1) |
| Target hardware | ESP32-S3 |
| Framework | ESP-IDF (>= 4.1.0) |
| BLE stack | NimBLE |
| LoRa library | RadioLib (jgromes, >= 7.5.0) |
| WiFi manager | esp32-wifi-manager (ankayca, >= 0.0.4) |
| LED driver | led_strip (espressif, >= 3.0.2) |
| mDNS | espressif/mdns (>= 1.9.1) |
| Azure DPS scope | 0ne01136E89 |
| RTOS | FreeRTOS (ESP-IDF integrated) |

---

# 17. Glossary

| Term | Definition |
|---|---|
| **C2D** | Cloud-to-Device: messages sent from the cloud (backend/app) to the hub |
| **D2C** | Device-to-Cloud: telemetry sent from the hub to the cloud |
| **DPS** | Azure Device Provisioning Service: automatic device registration |
| **RMLEAK** | Remove Leak interlock: a flag on the valve that locks it in the closed position until explicitly cleared |
| **Override Window** | A 24-hour period after a physical valve override where auto-close is temporarily disabled |
| **Provisioning** | The process of assigning devices (valve, sensors) to a hub |
| **Decommission** | The process of removing devices from a hub |
| **NVS** | Non-Volatile Storage: ESP32 flash-based key-value store for persistent configuration |
| **SAS Token** | Shared Access Signature: HMAC-based authentication token for Azure IoT Hub |
| **Snapshot** | Periodic full-state telemetry message containing all device data |
| **Lifecycle** | Birth/reconnect announcement telemetry from the hub |
| **Gateway ID** | Unique hub identifier derived from WiFi MAC address (format: GW-XXXXXXXXXXXX) |
| **trigger_mask** | Bitmask controlling which sensor types can trigger auto-close (BLE=1, LoRa=2, Valve=4) |
| **Boot Sync** | Post-reboot window (up to 2 minutes) where the hub waits for all sensors to check in |
