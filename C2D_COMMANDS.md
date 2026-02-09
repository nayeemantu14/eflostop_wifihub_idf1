# eFloStop WiFi Hub — Cloud-to-Device (C2D) Command Reference

## Overview

The eFloStop WiFi Hub receives commands from the cloud via **Azure IoT Hub Cloud-to-Device (C2D) messages**. Commands are delivered as MQTT messages on the standard Azure C2D topic and processed by the hub's IoT task event loop.

### How It Works

1. **Cloud sends** a C2D message to the hub's Azure IoT Hub device identity.
2. **Hub receives** the message via MQTT, parses it, and dispatches the command.
3. **Hub executes** the command (valve control, provisioning, config update, etc.).
4. **Hub publishes** a `cmd_ack` telemetry event (for envelope commands) so the cloud knows the result.

### Message Formats (Priority Order)

The parser tries these formats in order:

| Priority | Format | Detection |
|----------|--------|-----------|
| 1 | **Canonical envelope** | JSON with `"schema": "eflostop.cmd"` |
| 2 | **Legacy envelope** | JSON with `"schema": "eflostop.cmd.v1"` |
| 3 | **Legacy text** | Plain text keywords (`VALVE_OPEN`, `DECOMMISSION_ALL`, etc.) |

**Recommendation:** Always use the canonical envelope format for new integrations.

---

## Command Envelope

### Canonical Format

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "<correlation-id>",
  "cmd": "<command_name>",
  "payload": { ... }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `schema` | string | **yes** | Must be `"eflostop.cmd"` |
| `ver` | integer | no | Envelope version. Defaults to `1` if omitted. |
| `id` | string | no | Correlation ID. If present, the hub sends a `cmd_ack` response. Use a unique value (UUID, counter, timestamp) to correlate requests with responses. |
| `cmd` | string | **yes** | Normalized command name (see command list below). |
| `payload` | object | varies | Command-specific payload. Some commands require it, others don't. |

### Why No Version in the Schema String

The schema identifier `"eflostop.cmd"` is intentionally stable and version-free. Versioning is handled by the separate `ver` field, which:
- Avoids string-matching overhead for version checks on the ESP32.
- Allows the cloud to explicitly request a specific protocol version.
- Keeps the schema string constant across protocol revisions (no `v1`/`v2`/`v3` string proliferation).

### Legacy Envelope (Backward Compatible)

The hub also accepts the older format where the version was embedded in the schema string:

```json
{
  "schema": "eflostop.cmd.v1",
  "id": "<correlation-id>",
  "cmd": "<command_name>",
  "payload": { ... }
}
```

This is treated identically to the canonical format with `ver: 1`. Both formats produce the same internal result.

---

## Command Acknowledgment (cmd_ack)

When a command includes an `id` field (or uses the envelope format), the hub publishes a telemetry event confirming the result.

### Success Response

```json
{
  "schema": "eflostop.v2",
  "ts": 1770589401,
  "gateway": { "id": "GW-34B7DA6AAD54", "fw": "1.3.0", "uptime_s": 971 },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "prov-002",
    "cmd": "provision",
    "status": "ok"
  }
}
```

### Error Response

```json
{
  "schema": "eflostop.v2",
  "ts": 1770589401,
  "gateway": { "id": "GW-34B7DA6AAD54", "fw": "1.3.0", "uptime_s": 971 },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "prov-002",
    "cmd": "provision",
    "status": "error",
    "error": {
      "code": "provision",
      "detail": "provisioning failed"
    }
  }
}
```

### Ack Fields

| Field | Type | Description |
|-------|------|-------------|
| `event` | string | Always `"cmd_ack"` |
| `id` | string | Same correlation ID from the request (omitted if request had no `id`) |
| `cmd` | string | The command name that was executed |
| `status` | string | `"ok"` or `"error"` |
| `error` | object | Present only on error. Contains `code` (command name) and `detail` (human-readable message). |

### Correlation ID Behavior

- If the same `id` is sent twice, the command executes again and a new ack is published. Commands are **not idempotent** by default — sending `valve_close` twice will attempt to close the valve twice.
- The hub does not deduplicate based on `id`. The correlation ID is purely for request-response matching.
- If `id` is omitted, no `cmd_ack` is published (fire-and-forget mode).

---

## Command Reference

### valve_open

Opens the water valve.

| Field | Value |
|-------|-------|
| `cmd` | `"valve_open"` |
| `payload` | none |

**Example:**
```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "open-001",
  "cmd": "valve_open"
}
```

**Side effects:** Initiates BLE connection to the valve (if not connected), then sends the open command. The valve state change is published as a `valve_state_changed` telemetry event.

**Legacy equivalent:** `VALVE_OPEN`

---

### valve_close

Closes the water valve.

| Field | Value |
|-------|-------|
| `cmd` | `"valve_close"` |
| `payload` | none |

**Example:**
```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "close-001",
  "cmd": "valve_close"
}
```

**Side effects:** Same as `valve_open` but closes. Published as `valve_state_changed` event.

**Legacy equivalent:** `VALVE_CLOSE`

---

### valve_set_state

Unified valve control — opens or closes based on the `state` field in the payload.

| Field | Value |
|-------|-------|
| `cmd` | `"valve_set_state"` |
| `payload.state` | `"open"` or `"closed"` (required) |

**Example:**
```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "valve-001",
  "cmd": "valve_set_state",
  "payload": { "state": "open" }
}
```

**Error conditions:**
| Detail | Cause |
|--------|-------|
| `missing 'state' field (expected "open" or "closed")` | Payload missing or no `state` key |
| `invalid state value (expected "open" or "closed")` | `state` is not `"open"` or `"closed"` |

**No legacy equivalent** — this is an envelope-only command.

---

### leak_reset

Resets the leak incident latch and clears the RMLEAK interlock on the valve. Does **not** open the valve — that requires a separate `valve_open` or `valve_set_state` command.

| Field | Value |
|-------|-------|
| `cmd` | `"leak_reset"` |
| `payload` | none |

**Example:**
```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "reset-001",
  "cmd": "leak_reset"
}
```

**Side effects:** Clears the rules engine leak incident. Sends RMLEAK=0 to the valve over BLE. Published as `rmleak_cleared` telemetry event by the rules engine.

**Legacy equivalent:** `LEAK_RESET`

---

### provision

Provisions the hub with device identities (valve MAC, sensor IDs). This is the primary onboarding command.

| Field | Value |
|-------|-------|
| `cmd` | `"provision"` |
| `payload` | Provisioning object (see below) |

**Payload schema:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `valve_mac` | string | no | BLE MAC of the valve, e.g. `"00:80:E1:27:F7:BB"` |
| `lora_sensors` | string[] | no | Array of LoRa sensor hex IDs, e.g. `["0x754A6237"]` |
| `ble_leak_sensors` | string[] | no | Array of BLE leak sensor MACs, e.g. `["00:80:E1:27:99:E7"]` |

At least one field must be present. Fields use **replace** semantics for each category present — e.g. sending `ble_leak_sensors` replaces all BLE leak sensors, but does not affect `valve_mac` or `lora_sensors` if those fields are absent.

**Example:**
```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "prov-002",
  "cmd": "provision",
  "payload": {
    "valve_mac": "00:80:E1:27:F7:BB",
    "ble_leak_sensors": ["00:80:E1:27:99:E7"],
    "lora_sensors": ["0x754A6237"]
  }
}
```

**Limits:**
- Max 16 LoRa sensors
- Max 16 BLE leak sensors
- 1 valve

**Side effects:** Stores config in NVS. Triggers BLE valve connection if valve MAC is set. Publishes lifecycle + snapshot after provisioning completes.

**Legacy equivalent:** Any JSON object starting with `{` that doesn't match the envelope schema is treated as a provisioning payload.

---

### decommission

Removes provisioned devices. The `target` field in the payload controls what is removed.

| Field | Value |
|-------|-------|
| `cmd` | `"decommission"` |
| `payload.target` | `"valve"`, `"lora"`, `"ble"`, or `"all"` (required) |

#### target: "valve"

Removes the valve from provisioning. Disconnects BLE.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "decom-v-001",
  "cmd": "decommission",
  "payload": { "target": "valve" }
}
```

**Legacy equivalent:** `DECOMMISSION_VALVE`

#### target: "lora"

Removes a specific LoRa sensor. Requires `sensor_id`.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "decom-l-001",
  "cmd": "decommission",
  "payload": { "target": "lora", "sensor_id": "0x754A6237" }
}
```

**Legacy equivalent:** `DECOMMISSION_LORA:0x754A6237`

#### target: "ble"

Removes a specific BLE leak sensor. Requires `sensor_id`.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "decom-b-001",
  "cmd": "decommission",
  "payload": { "target": "ble", "sensor_id": "00:80:E1:27:99:E7" }
}
```

**Legacy equivalent:** `DECOMMISSION_BLE:00:80:E1:27:99:E7`

#### target: "all"

**DANGEROUS:** Full factory decommission. Erases all provisioning data, sensor metadata, and restarts the hub.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "decom-all-001",
  "cmd": "decommission",
  "payload": { "target": "all" }
}
```

**Side effects:**
1. Erases all NVS provisioning data
2. Clears all sensor metadata
3. Disconnects valve BLE
4. Sends `cmd_ack` (if `id` present)
5. **Restarts the device** after 3 seconds
6. Hub boots into unprovisioned state (captive portal mode)

**Legacy equivalent:** `DECOMMISSION_ALL` or `DECOMMISSION`

---

### rules_config

Updates the auto-close rules engine configuration. Uses **merge semantics** — only fields present in the payload are changed; others keep their current values.

| Field | Value |
|-------|-------|
| `cmd` | `"rules_config"` |
| `payload` | Rules config object (see below) |

**Payload schema:**

| Field | Type | Description |
|-------|------|-------------|
| `auto_close_enabled` | bool | Enable/disable automatic valve close on leak detection |
| `trigger_mask` | integer | Bitmask of leak sources that trigger auto-close: bit 0 = BLE leak, bit 1 = LoRa, bit 2 = valve flood. Value `7` (0x07) = all sources. |

**Example:**
```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "rules-001",
  "cmd": "rules_config",
  "payload": {
    "auto_close_enabled": true,
    "trigger_mask": 7
  }
}
```

**Side effects:** Persisted to NVS. Takes effect immediately for subsequent leak events.

**Legacy equivalent:** `RULES_CONFIG:{"auto_close_enabled":true,"trigger_mask":7}`

---

### sensor_meta

Sets location metadata for a sensor. Used to associate human-readable labels and location codes with sensors for telemetry enrichment.

| Field | Value |
|-------|-------|
| `cmd` | `"sensor_meta"` |
| `payload` | Sensor metadata object (see below) |

**Payload schema:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `sensor_type` | string | **yes** | `"ble"` or `"lora"` |
| `sensor_id` | string | **yes** | MAC address (BLE) or hex ID (LoRa) |
| `location_code` | string | no | Location enum: `"bathroom"`, `"kitchen"`, `"laundry"`, `"garage"`, `"garden"`, `"basement"`, `"utility"`, `"hallway"`, `"bedroom"`, `"living_room"`, `"attic"`, `"outdoor"` |
| `label` | string | no | Free-text label (max 31 chars), e.g. `"Downstairs laundry"` |

**Example:**
```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "meta-001",
  "cmd": "sensor_meta",
  "payload": {
    "sensor_type": "ble",
    "sensor_id": "00:80:E1:27:99:E7",
    "location_code": "laundry",
    "label": "Downstairs laundry"
  }
}
```

**Side effects:** Persisted to NVS. Location data appears in subsequent telemetry events and snapshots for this sensor.

**Legacy equivalent:** `SENSOR_META:{"sensor_type":"ble","sensor_id":"00:80:E1:27:99:E7","location_code":"laundry","label":"Downstairs laundry"}`

---

## Legacy Text Commands

These plain-text commands are supported for backward compatibility. They do **not** produce a `cmd_ack` response (no correlation ID).

| Legacy Text | Mapped Command | Mapped Payload |
|-------------|---------------|----------------|
| `VALVE_OPEN` | `valve_open` | _(none)_ |
| `VALVE_CLOSE` | `valve_close` | _(none)_ |
| `LEAK_RESET` | `leak_reset` | _(none)_ |
| `DECOMMISSION_VALVE` | `decommission` | `{"target":"valve"}` |
| `DECOMMISSION_LORA:0x754A6237` | `decommission` | `{"target":"lora","sensor_id":"0x754A6237"}` |
| `DECOMMISSION_BLE:00:80:E1:27:99:E7` | `decommission` | `{"target":"ble","sensor_id":"00:80:E1:27:99:E7"}` |
| `DECOMMISSION_ALL` | `decommission` | `{"target":"all"}` |
| `DECOMMISSION` | `decommission` | `{"target":"all"}` |
| `RULES_CONFIG:{json}` | `rules_config` | _(json after colon)_ |
| `SENSOR_META:{json}` | `sensor_meta` | _(json after colon)_ |
| `{json}` _(bare JSON)_ | `provision` | _(the JSON itself)_ |

---

## Error Reference

### Common Error Details

| Command | Error Detail | Cause |
|---------|-------------|-------|
| `provision` | `provisioning failed` | Invalid MAC format, NVS write failure, or empty payload |
| `decommission` | `missing decommission target` | Payload missing or no `target` field |
| `decommission` | `unknown decommission target` | `target` is not `valve`, `lora`, `ble`, or `all` |
| `decommission` | `valve decommission failed` | Valve not provisioned or NVS error |
| `decommission` | `lora sensor decommission failed` | Sensor ID not found or NVS error |
| `decommission` | `ble sensor decommission failed` | Sensor MAC not found or NVS error |
| `decommission` | `full decommission failed` | NVS erase failed |
| `rules_config` | `rules config update failed` | Invalid JSON or NVS write error |
| `sensor_meta` | `sensor metadata update failed` | Missing required fields or NVS error |
| `valve_set_state` | `missing 'state' field ...` | No `state` in payload |
| `valve_set_state` | `invalid state value ...` | `state` is not `"open"` or `"closed"` |
| _(any)_ | `unknown command` | `cmd` field not recognized |

### Parse Failures

If the message cannot be parsed at all (invalid JSON, unrecognized text, no schema match), the hub logs a warning but does **not** publish any ack — there is no correlation ID to respond to.

---

## Security Notes

### Dangerous Commands

| Command | Risk | Notes |
|---------|------|-------|
| `decommission` (target: `all`) | **High** | Erases all config and restarts. Device becomes unprovisioned. |
| `decommission` (target: `valve`) | Medium | Removes valve — auto-close protection is lost. |
| `provision` | Medium | Replaces device identities. Can redirect valve control to a different device. |

Currently, commands are authenticated via the Azure IoT Hub device identity (SAS token or X.509 certificate). There is no additional command-level authentication or confirmation dialog on the device. The security boundary is the Azure IoT Hub connection itself.

### Recommendations for App Developers

- Require user confirmation in the app UI before sending `decommission` (especially `target: "all"`).
- Use correlation IDs for all commands so you can confirm execution.
- Do not expose raw C2D command construction to end users — validate inputs in the app/backend.

---

## Quick Reference Card

```
┌─────────────────────┬──────────────────────────────────────────┐
│ Command             │ Payload                                  │
├─────────────────────┼──────────────────────────────────────────┤
│ valve_open          │ (none)                                   │
│ valve_close         │ (none)                                   │
│ valve_set_state     │ { "state": "open"|"closed" }             │
│ leak_reset          │ (none)                                   │
│ provision           │ { valve_mac, lora_sensors, ble_leak_... }│
│ decommission        │ { "target": "valve|lora|ble|all", ... }  │
│ rules_config        │ { auto_close_enabled, trigger_mask }     │
│ sensor_meta         │ { sensor_type, sensor_id, location_...}  │
└─────────────────────┴──────────────────────────────────────────┘
```
