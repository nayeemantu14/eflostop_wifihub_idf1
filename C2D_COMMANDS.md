# eFloStop WiFi Hub - Cloud-to-Device (C2D) Command Reference

# 1 Overview

The hub gets commands from the cloud through Azure IoT Hub C2D messages. They come in over MQTT and the hub's IoT task picks them up.

## 1.1 How it works

1. Cloud sends a C2D message to the hub's device identity on Azure.
2. Hub receives it over MQTT, parses it, figures out what command it is.
3. Hub runs the command (valve control, provisioning, config change, etc).
4. Hub sends back a `cmd_ack` telemetry event so the cloud knows what happened.

## 1.2 Message formats

The parser tries these formats in order:

| Priority | Format | How it's detected |
|----------|--------|-------------------|
| 1 | Canonical envelope | JSON with `"schema": "eflostop.cmd"` |
| 2 | Legacy envelope | JSON with `"schema": "eflostop.cmd.v1"` |
| 3 | Legacy text | Plain text keywords like `VALVE_OPEN`, `DECOMMISSION_ALL`, etc. |

For anything new, use the canonical envelope format.

---

# 2 Command Envelope

## 2.1 Canonical format

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "<correlation-id>",
  "cmd": "<command_name>",
  "payload": { ... }
}
```

| Field | Type | Required | What it does |
|-------|------|----------|--------------|
| `schema` | string | yes | Has to be `"eflostop.cmd"` |
| `ver` | integer | no | Envelope version. Defaults to `1` if you leave it out. |
| `id` | string | no | Correlation ID. If you include this, the hub sends back a `cmd_ack`. Use something unique (UUID, counter, timestamp) so you can match requests to responses. |
| `cmd` | string | yes | The command name (see list below). |
| `payload` | object | depends | Some commands need it, some don't. |

## 2.2 Why the schema string doesn't have a version in it

We keep `"eflostop.cmd"` as a fixed string on purpose. The version goes in the `ver` field instead. This way:
- We don't waste time doing string comparisons for version checks on the ESP32.
- The cloud can ask for a specific protocol version if needed.
- We don't end up with `v1`/`v2`/`v3` strings everywhere.

## 2.3 Legacy envelope (still works)

The hub also accepts the older format where the version was baked into the schema string:

```json
{
  "schema": "eflostop.cmd.v1",
  "id": "<correlation-id>",
  "cmd": "<command_name>",
  "payload": { ... }
}
```

This gets treated the same as the canonical format with `ver: 1`. No difference internally.

---

# 3 Command Acknowledgment (cmd_ack)

When a command has an `id` field, the hub sends back a telemetry event to confirm what happened.

## 3.1 Success

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

## 3.2 Error

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

## 3.3 Ack fields

| Field | Type | What it is |
|-------|------|------------|
| `event` | string | Always `"cmd_ack"` |
| `id` | string | Same correlation ID you sent in the request. Left out if the request didn't have one. |
| `cmd` | string | Which command ran |
| `status` | string | `"ok"` or `"error"` |
| `error` | object | Only shows up on errors. Has `code` (the command name) and `detail` (a readable message). |

## 3.4 How correlation IDs behave

- If you send the same `id` twice, the command runs again and you get another ack. Commands are not idempotent. Sending `valve_close` twice tries to close the valve twice.
- The hub doesn't deduplicate by `id`. It's just for matching requests to responses.
- If you leave out `id`, no `cmd_ack` gets published. Fire and forget.

---

# 4 Command Reference

## 4.1 valve_open

Opens the water valve.

| Field | Value |
|-------|-------|
| `cmd` | `"valve_open"` |
| `payload` | none |

Example:
```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "open-001",
  "cmd": "valve_open"
}
```

What happens: the hub connects to the valve over BLE (if not already connected) and sends the open command. A `valve_state_changed` telemetry event gets published.

Legacy text: `VALVE_OPEN`

---

## 4.2 valve_close

Closes the water valve.

| Field | Value |
|-------|-------|
| `cmd` | `"valve_close"` |
| `payload` | none |

Example:
```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "close-001",
  "cmd": "valve_close"
}
```

Same as `valve_open` but closes. Published as a `valve_state_changed` event.

Legacy text: `VALVE_CLOSE`

---

## 4.3 valve_set_state

Single command that can open or close the valve depending on the `state` you pass.

| Field | Value |
|-------|-------|
| `cmd` | `"valve_set_state"` |
| `payload.state` | `"open"` or `"closed"` (required) |

Example:
```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "valve-001",
  "cmd": "valve_set_state",
  "payload": { "state": "open" }
}
```

Errors:
| Detail | Why |
|--------|-----|
| `missing 'state' field (expected "open" or "closed")` | Payload is missing or has no `state` key |
| `invalid state value (expected "open" or "closed")` | `state` is something other than `"open"` or `"closed"` |

No legacy equivalent. This only works with the envelope format.

---

## 4.4 leak_reset

Resets the leak incident latch and clears the RMLEAK interlock on the valve. This does not open the valve. You need to send a separate `valve_open` or `valve_set_state` for that.

| Field | Value |
|-------|-------|
| `cmd` | `"leak_reset"` |
| `payload` | none |

Example:
```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "reset-001",
  "cmd": "leak_reset"
}
```

What happens: clears the rules engine leak incident, sends RMLEAK=0 to the valve over BLE. The rules engine publishes an `rmleak_cleared` telemetry event.

Legacy text: `LEAK_RESET`

---

## 4.5 provision

Tells the hub which devices it should talk to (valve MAC, sensor IDs). This is the main onboarding command.

| Field | Value |
|-------|-------|
| `cmd` | `"provision"` |
| `payload` | Provisioning object (see below) |

Payload fields:

| Field | Type | Required | What it is |
|-------|------|----------|------------|
| `valve_mac` | string | no | BLE MAC of the valve, e.g. `"00:80:E1:27:F7:BB"` |
| `lora_sensors` | string[] | no | Array of LoRa sensor hex IDs, e.g. `["0x754A6237"]` |
| `ble_leak_sensors` | string[] | no | Array of BLE leak sensor MACs, e.g. `["00:80:E1:27:99:E7"]` |

You need at least one field. Each field replaces that whole category. So if you send `ble_leak_sensors`, it replaces all the BLE leak sensors. But it won't touch `valve_mac` or `lora_sensors` if you didn't include those.

Example:
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

Limits:
- Up to 16 LoRa sensors
- Up to 16 BLE leak sensors
- 1 valve

What happens: config gets saved to NVS. If a valve MAC was set, the hub starts connecting to it over BLE. A lifecycle + snapshot telemetry gets published after provisioning finishes.

Legacy: any bare JSON object (starts with `{`) that doesn't match the envelope schema gets treated as a provisioning payload.

---

## 4.6 decommission

Removes devices from the hub. The `target` field says what to remove.

| Field | Value |
|-------|-------|
| `cmd` | `"decommission"` |
| `payload.target` | `"valve"`, `"lora"`, `"ble"`, or `"all"` (required) |

### 4.6.1 target: "valve"

Removes the valve and disconnects BLE.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "decom-v-001",
  "cmd": "decommission",
  "payload": { "target": "valve" }
}
```

Legacy text: `DECOMMISSION_VALVE`

### 4.6.2 target: "lora"

Removes one LoRa sensor. You have to pass the `sensor_id`.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "decom-l-001",
  "cmd": "decommission",
  "payload": { "target": "lora", "sensor_id": "0x754A6237" }
}
```

Legacy text: `DECOMMISSION_LORA:0x754A6237`

### 4.6.3 target: "ble"

Removes one BLE leak sensor. You have to pass the `sensor_id`.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "decom-b-001",
  "cmd": "decommission",
  "payload": { "target": "ble", "sensor_id": "00:80:E1:27:99:E7" }
}
```

Legacy text: `DECOMMISSION_BLE:00:80:E1:27:99:E7`

### 4.6.4 target: "all"

CAREFUL: this is a full factory reset. Wipes everything and restarts the hub.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "id": "decom-all-001",
  "cmd": "decommission",
  "payload": { "target": "all" }
}
```

What happens, in order:
1. Erases all NVS provisioning data
2. Clears all sensor metadata
3. Disconnects the valve BLE
4. Sends `cmd_ack` (if `id` was included)
5. Restarts the device after 3 seconds
6. Hub boots up unprovisioned and goes into captive portal mode

Legacy text: `DECOMMISSION_ALL` or `DECOMMISSION`

---

## 4.7 rules_config

Changes the auto-close rules engine settings. Uses merge logic, so only the fields you send get updated. Everything else stays the same.

| Field | Value |
|-------|-------|
| `cmd` | `"rules_config"` |
| `payload` | Rules config object (see below) |

Payload fields:

| Field | Type | What it does |
|-------|------|--------------|
| `auto_close_enabled` | bool | Turn on/off automatic valve close when a leak is detected |
| `trigger_mask` | integer | Bitmask for which leak sources trigger auto-close: bit 0 = BLE leak, bit 1 = LoRa, bit 2 = valve flood. Set to `7` (0x07) for all sources. |

Example:
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

What happens: saved to NVS. Takes effect right away for the next leak event.

Legacy text: `RULES_CONFIG:{"auto_close_enabled":true,"trigger_mask":7}`

---

## 4.8 sensor_meta

Assigns location info to a sensor. This is how you tag sensors with a room name or label so telemetry makes more sense.

| Field | Value |
|-------|-------|
| `cmd` | `"sensor_meta"` |
| `payload` | Sensor metadata object (see below) |

Payload fields:

| Field | Type | Required | What it is |
|-------|------|----------|------------|
| `sensor_type` | string | yes | `"ble"` or `"lora"` |
| `sensor_id` | string | yes | MAC address (BLE) or hex ID (LoRa) |
| `location_code` | string | no | One of: `"bathroom"`, `"kitchen"`, `"laundry"`, `"garage"`, `"garden"`, `"basement"`, `"utility"`, `"hallway"`, `"bedroom"`, `"living_room"`, `"attic"`, `"outdoor"` |
| `label` | string | no | Free text label, max 31 chars. Like `"Downstairs laundry"` |

Example:
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

What happens: saved to NVS. The location data shows up in telemetry events and snapshots for that sensor from then on.

Legacy text: `SENSOR_META:{"sensor_type":"ble","sensor_id":"00:80:E1:27:99:E7","location_code":"laundry","label":"Downstairs laundry"}`

---

# 5 Legacy Text Commands

These are the old plain-text commands. They still work but you won't get a `cmd_ack` back since there's no correlation ID.

| Legacy text | Maps to command | Maps to payload |
|-------------|-----------------|-----------------|
| `VALVE_OPEN` | `valve_open` | (none) |
| `VALVE_CLOSE` | `valve_close` | (none) |
| `LEAK_RESET` | `leak_reset` | (none) |
| `DECOMMISSION_VALVE` | `decommission` | `{"target":"valve"}` |
| `DECOMMISSION_LORA:0x754A6237` | `decommission` | `{"target":"lora","sensor_id":"0x754A6237"}` |
| `DECOMMISSION_BLE:00:80:E1:27:99:E7` | `decommission` | `{"target":"ble","sensor_id":"00:80:E1:27:99:E7"}` |
| `DECOMMISSION_ALL` | `decommission` | `{"target":"all"}` |
| `DECOMMISSION` | `decommission` | `{"target":"all"}` |
| `RULES_CONFIG:{json}` | `rules_config` | (the json after the colon) |
| `SENSOR_META:{json}` | `sensor_meta` | (the json after the colon) |
| `{json}` (bare JSON) | `provision` | (the JSON itself) |

---

# 6 Error Reference

## 6.1 Common errors

| Command | Error detail | What went wrong |
|---------|-------------|-----------------|
| `provision` | `provisioning failed` | Bad MAC format, NVS write failed, or the payload was empty |
| `decommission` | `missing decommission target` | Payload is missing or doesn't have a `target` field |
| `decommission` | `unknown decommission target` | `target` isn't `valve`, `lora`, `ble`, or `all` |
| `decommission` | `valve decommission failed` | Valve wasn't provisioned, or NVS error |
| `decommission` | `lora sensor decommission failed` | Sensor ID not found, or NVS error |
| `decommission` | `ble sensor decommission failed` | Sensor MAC not found, or NVS error |
| `decommission` | `full decommission failed` | NVS erase failed |
| `rules_config` | `rules config update failed` | Bad JSON or NVS write error |
| `sensor_meta` | `sensor metadata update failed` | Missing required fields or NVS error |
| `valve_set_state` | `missing 'state' field ...` | No `state` in the payload |
| `valve_set_state` | `invalid state value ...` | `state` isn't `"open"` or `"closed"` |
| (any) | `unknown command` | `cmd` field isn't recognized |

## 6.2 Parse failures

If the message can't be parsed at all (broken JSON, random text, nothing matches), the hub just logs a warning. No ack gets sent because there's no correlation ID to respond to.

---

# 7 Security notes

## 7.1 Dangerous commands

| Command | Risk level | Notes |
|---------|------------|-------|
| `decommission` (target: `all`) | High | Wipes all config and restarts. Device goes back to unprovisioned. |
| `decommission` (target: `valve`) | Medium | Removes the valve, which means auto-close protection is gone. |
| `provision` | Medium | Replaces device identities. Could point valve control at a different device. |

Right now, commands are authenticated through the Azure IoT Hub device identity (SAS token or X.509 cert). There's no extra command-level auth or confirmation on the device side. The security boundary is the Azure IoT Hub connection.

## 7.2 Things to keep in mind for the app side

- Show a confirmation dialog in the app before sending `decommission`, especially `target: "all"`.
- Always use correlation IDs so you can confirm the command actually ran.
- Don't let end users build raw C2D commands. Validate inputs in the app or backend first.

---

# 8 Quick Reference

```
Command              Payload
-----------------    ----------------------------------------
valve_open           (none)
valve_close          (none)
valve_set_state      { "state": "open"|"closed" }
leak_reset           (none)
provision            { valve_mac, lora_sensors, ble_leak_... }
decommission         { "target": "valve|lora|ble|all", ... }
rules_config         { auto_close_enabled, trigger_mask }
sensor_meta          { sensor_type, sensor_id, location_... }
```
