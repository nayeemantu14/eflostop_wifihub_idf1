# eFloStop WiFi Hub - Cloud-to-Device (C2D) Command Reference

# 1 Overview

The hub gets commands from the cloud through Azure IoT Hub C2D messages. They come in over MQTT and the hub's IoT task picks them up.

> Internal firmware reference — documents the protocol exactly as the hub implements it.

## 1.1 How it works

1. Cloud sends a C2D message to the hub's device identity on Azure.
2. Hub receives it over MQTT, parses it, figures out what command it is.
3. Hub runs the command (valve control, provisioning, config change, etc).
4. Hub sends back a `cmd_ack` telemetry event so the cloud knows what happened (when eligible — see §3.4).

## 1.2 Message formats

The parser tries these formats in order:

| Priority | Format | How it's detected |
|----------|--------|-------------------|
| 1 | Canonical envelope | JSON with `"schema": "eflostop.cmd"` |
| 2 | Legacy envelope | JSON with `"schema": "eflostop.cmd.v1"` |
| 3 | Legacy text | Plain text keywords like `VALVE_OPEN`, `DECOMMISSION_ALL`, etc. |

For anything new, use the canonical envelope format. Several newer commands (`valve_set_state`, `override_enable`, `set_hub_name`) are **envelope-only** and have no legacy text form.

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
| `id` | string | no | Correlation ID. Include it to match a request to its `cmd_ack`. Use something unique (UUID, counter, timestamp). |
| `cmd` | string | yes | The command name (see list below). |
| `payload` | object | depends | Some commands need it, some don't. |

Note: with the envelope format you get a `cmd_ack` **even if you omit `id`** (the ack just has no `id` to match on). See §3.4 for the exact rule.

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

When a command is eligible (see §3.4), the hub sends back a telemetry event to confirm what happened.

## 3.1 Success

```json
{
  "schema": "eflostop.v2",
  "ts": 1770589401,
  "gateway": { "id": "GW-50787D0E28CC", "short_id": "28CC", "fw": "1.4.1", "uptime_s": 971 },
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
  "gateway": { "id": "GW-50787D0E28CC", "short_id": "28CC", "fw": "1.4.1", "uptime_s": 971 },
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

The `gateway` object also carries `name` when a hub name is set. `fw` is the running firmware version (`1.4.1`), read at runtime from the build's `PROJECT_VER` — it always matches the boot banner and OTA image.

## 3.3 Ack fields

| Field | Type | What it is |
|-------|------|------------|
| `event` | string | Always `"cmd_ack"` |
| `id` | string | Same correlation ID you sent. Empty string if the request didn't have one. |
| `cmd` | string | Which command ran |
| `status` | string | `"ok"` or `"error"` |
| `error` | object | Only on errors. Has `code` (the command name) and `detail` (a readable message). |

## 3.4 When you get an ack — and when you don't

The hub publishes a `cmd_ack` only when **`cmd.is_envelope || cmd.id[0]`** is true. In practice:

- **Envelope commands always ack** (canonical or legacy envelope), even without an `id` — the ack just comes back with an empty `id`.
- **Legacy text commands never ack** (`VALVE_OPEN`, `DECOMMISSION_ALL`, etc.) — they aren't envelopes and carry no `id`. Fire-and-forget.
- The hub does **not** deduplicate by `id`. Send the same `id` twice and the command runs twice (commands are not idempotent — `valve_close` twice tries to close twice). Use a new `id` per retry.

⚠️ **Pre-clock suppression.** All `eflostop.v2` telemetry — including `cmd_ack` — is **suppressed until the hub's clock is SNTP-synced** (the envelope builder returns nothing before epoch `1704067200` / 2024-01-01 UTC). So a command received right after a cold boot may **execute but produce no ack**. Confirm real state from the next snapshot, never assume failure from a missing ack. (Twin reported PATCHes are *not* clock-gated — see §8.)

---

# 4 Command Reference

## 4.1 valve_open

Opens the water valve.

| Field | Value |
|-------|-------|
| `cmd` | `"valve_open"` |
| `payload` | none |

```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "open-001", "cmd": "valve_open" }
```

What happens: if the valve's RMLEAK latch is asserted (valve locked after an auto-close), the hub **rejects the command up front** with a `cmd_ack` error and sends nothing to the valve — this prevents the sub-second water-on transient you'd otherwise get from letting the valve briefly honor the open before its own RMLEAK interlock re-closes it. Otherwise the hub calls `ble_valve_connect()` then `ble_valve_open()`; on that (allowed) path there is **no transport/BLE success check**, so a forwarded open `cmd_ack`s **`ok`** regardless of whether the valve is reachable. The actual open is confirmed asynchronously by a `valve_state_changed` event (emitted by the BLE-notify path when the valve reports its new state) and by the next snapshot — **not** by this command. To open during an active leak, use `override_enable` (it clears RMLEAK as part of the guarded 24h window, so it bypasses this check by design).

Errors:
| Detail | Why |
|--------|-----|
| `Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.` | Valve RMLEAK latch is asserted — clear it via `leak_reset`, or open during a leak via `override_enable` |

Legacy text: `VALVE_OPEN`

---

## 4.2 valve_close

Closes the water valve.

| Field | Value |
|-------|-------|
| `cmd` | `"valve_close"` |
| `payload` | none |

```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "close-001", "cmd": "valve_close" }
```

Same shape as `valve_open` but closes. Always acks `ok` (no transport check); the real state arrives later as a `valve_state_changed` event. Note a manual close does **not** assert RMLEAK or latch a leak incident — that only happens on auto-close.

Legacy text: `VALVE_CLOSE`

---

## 4.3 valve_set_state

Single command that opens or closes depending on the `state` you pass. Preferred for new code.

| Field | Value |
|-------|-------|
| `cmd` | `"valve_set_state"` |
| `payload.state` | `"open"` or `"closed"` (required) |

```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "valve-001", "cmd": "valve_set_state", "payload": { "state": "open" } }
```

Errors:
| Detail | Why |
|--------|-----|
| `missing 'state' field (expected "open" or "closed")` | Payload missing or no `state` key |
| `invalid state value (expected "open" or "closed")` | `state` is something other than `"open"`/`"closed"` |
| `Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.` | `state:"open"` while the valve RMLEAK latch is asserted (same guard as `valve_open`) |

Envelope-only. No legacy text form.

---

## 4.4 leak_reset

Clears the leak incident latch and the RMLEAK interlock on the valve. **Does not open the valve** — send a separate `valve_open`/`valve_set_state` after.

| Field | Value |
|-------|-------|
| `cmd` | `"leak_reset"` |
| `payload` | none |

```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "reset-001", "cmd": "leak_reset" }
```

What happens: if it passes the guard, the hub clears the incident latch (+NVS), cancels any active override window, zeroes the active-leak count, and sends RMLEAK=0 to the valve over BLE. A `rmleak_cleared` event is published **only when there was something to clear** (an incident was active, the valve had RMLEAK set, or an override was cancelled — adds `override_cancelled:true` in that case); a no-op reset acks `ok` with no rules event.

**Guard (interlock):** while any leak source is still wet (`active-leak count > 0`), `leak_reset` is **refused** and nothing is cleared — so a follow-up `valve_open` can't restore water during a live leak. Use `override_enable` to open during an active leak instead.

| Error detail | Why |
|--------------|-----|
| `A leak is still active. Fix the leak first, or use override to open the valve during a leak.` | A leak source is still active (one string covers all refusal causes, incl. mutex timeout) |

Legacy text: `LEAK_RESET`

---

## 4.5 override_enable

**Remote equivalent of the physical valve button.** Opens the valve **during an active leak** and starts the guarded **24-hour Water Access Override window** (auto-close paused; leaks still detected and reported). This is the redundant, app-reachable version of the valve-button override.

| Field | Value |
|-------|-------|
| `cmd` | `"override_enable"` |
| `payload` | none |

```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "ovr-en-001", "cmd": "override_enable" }
```

**Preconditions** (checked in this exact order, before any state change):
1. Valve **provisioned** with a target MAC — else `No valve is set up for this hub.`
2. Valve **reachable** — if not ready, the hub attempts a bounded reconnect (up to ~10 s); still not ready → `The valve isn't responding...`
3. **Something to override** — an active window, a latched incident, or RMLEAK set on the valve. Pre-emptive use with no leak → `No active leak to override...`
4. Valve's **own flood probe dry** — if the valve is standing in water → `Water detected at the valve...` (absolute safety floor; no remote open while the valve is wet).

On success the hub: clears the valve interlock (`RMLEAK=0`), **opens** the valve, and starts a fresh 24 h window. It is **idempotent** — calling it while a window is already active refreshes it to a fresh 24 h. Success telemetry: `water_access_override_enabled` (`trigger:"c2d_command"`, `expires_ts`, `remaining_s:86400`), plus the eventual `valve_state_changed{open, rmleak:false}` and the `cmd_ack`.

| Error detail (verbatim — render as-is) | When |
|---|---|
| `No active leak to override. Use the normal Open Valve control.` | No window, no incident, RMLEAK clear |
| `Water detected at the valve. It can't be opened remotely until the valve area is dry.` | Valve flood probe wet |
| `The valve isn't responding. Check its power and connection, then try again.` | Valve not ready after the ~10 s reconnect |
| `No valve is set up for this hub.` | Unprovisioned / no valve target MAC |
| `Something went wrong applying the override. Your water state is unchanged. Try again.` | Internal error (mutex timeout / engine not initialized) |

Envelope-only — **no legacy text form** (note the asymmetry: `override_cancel` *has* a legacy form, `override_enable` does not). Requires hub firmware `gateway.fw ≥ 1.4.0`.

---

## 4.6 override_cancel

Cancels the 24-hour Water Access Override window and re-enables automatic valve closure immediately. If leaks are actively being detected (and auto-close is enabled) when this arrives, the valve auto-closes right away.

| Field | Value |
|-------|-------|
| `cmd` | `"override_cancel"` |
| `payload` | none |

```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "ovr-cancel-001", "cmd": "override_cancel" }
```

What happens:
1. The window is cleared (state → inactive, NVS updated), and an `auto_close_reenabled` event is queued (`previous_remaining_s`, `reason:"c2d_command"`).
2. If a leak is still active **and** auto-close is enabled, the valve closes and RMLEAK is asserted right away (you'll also see `valve_state_changed{closed, rmleak:true}`).
3. If no leak is active, residual incident state is wiped so the next leak starts a fresh cycle.

If **no override window is active**, the command succeeds silently (returns `ok`, no event). The only error is an internal failure:

| Error detail | Why |
|--------------|-----|
| `override cancel failed` | Internal error (mutex timeout / engine not initialized) — *not* the no-window case |

Legacy text: `OVERRIDE_CANCEL`

---

## 4.7 rules_config

Changes the auto-close rules engine settings. **This is how you enable/disable auto-close.** Merge logic — only the fields you send change; everything else stays.

| Field | Value |
|-------|-------|
| `cmd` | `"rules_config"` |
| `payload` | Rules config object (see below) |

Payload fields:

| Field | Type | What it does |
|-------|------|--------------|
| `auto_close_enabled` | bool | **Master** on/off for automatic valve close on a leak. `true` = enabled, `false` = disabled. |
| `trigger_mask` | integer | Per-type bitmask: bit 0 = BLE leak (1), bit 1 = LoRa (2), bit 2 = valve flood (4). `7` (0x07) = all sources. |
| `trigger_ble_leak` | bool | Convenience: set/clear **bit 0** of the mask. Applied *after* `trigger_mask`, so a bool wins for its bit. |
| `trigger_lora` | bool | Convenience: set/clear **bit 1**. |
| `trigger_valve_flood` | bool | Convenience: set/clear **bit 2**. |

Enable auto-close:
```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "rc-on", "cmd": "rules_config", "payload": { "auto_close_enabled": true } }
```
Disable auto-close:
```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "rc-off", "cmd": "rules_config", "payload": { "auto_close_enabled": false } }
```
Enable all + all triggers:
```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "rc-001", "cmd": "rules_config", "payload": { "auto_close_enabled": true, "trigger_mask": 7 } }
```

What happens: parsed and merged into the current config (defaults `auto_close_enabled=true`, `trigger_mask=7` if no prior value), persisted to NVS, takes effect on the next leak event. The new state shows up in the next snapshot's `data.rules` and in Twin reported (`auto_close_enabled`, `trigger_mask`). No dedicated rules event is emitted for a config change — only the `cmd_ack`.

| Error detail | Why |
|--------------|-----|
| `rules config update failed` | Missing payload, JSON parse failure, or NVS write error |

Legacy text: `RULES_CONFIG:{"auto_close_enabled":true,"trigger_mask":7}`

---

## 4.8 sensor_meta

Assigns location info to a sensor — a room code and/or a free-text label so telemetry reads clearly.

| Field | Value |
|-------|-------|
| `cmd` | `"sensor_meta"` |
| `payload` | Sensor metadata object (see below) |

Payload fields:

| Field | Type | Required | What it is |
|-------|------|----------|------------|
| `sensor_type` | string | yes | `"ble"` or `"lora"` (case-insensitive) |
| `sensor_id` | string | yes | MAC address (BLE) or `0x`-hex ID (LoRa) |
| `location_code` | string | no | One of: `bathroom`, `kitchen`, `laundry`, `garage`, `garden`, `basement`, `utility`, `hallway`, `bedroom`, `living_room`, `attic`, `outdoor` (unknown value → `unknown`; omitted → keep existing) |
| `label` | string | no | Free text, **max 31 chars** (silently truncated, not rejected). Omitted → keep existing. |

```json
{
  "schema": "eflostop.cmd", "ver": 1, "id": "meta-001", "cmd": "sensor_meta",
  "payload": { "sensor_type": "ble", "sensor_id": "00:80:E1:27:99:E7", "location_code": "laundry", "label": "Downstairs laundry" }
}
```

What happens: find-or-create the entry by `(type, id)`, persist to NVS (namespace `sen_meta`, up to **32** entries). Location data then appears in telemetry events and snapshots for that sensor.

| Error detail | Why |
|--------------|-----|
| `sensor metadata update failed` | Missing/invalid `sensor_type` or `sensor_id`, table full (32), or NVS error |

Legacy text: `SENSOR_META:{"sensor_type":"ble","sensor_id":"00:80:E1:27:99:E7","location_code":"laundry","label":"Downstairs laundry"}`

---

## 4.9 provision

Tells the hub which devices it should talk to (valve MAC, sensor IDs). The main onboarding command.

| Field | Value |
|-------|-------|
| `cmd` | `"provision"` |
| `payload` | Provisioning object (see below) |

Payload fields:

| Field | Type | Required | What it is |
|-------|------|----------|------------|
| `valve_mac` | string | no | BLE MAC of the valve, e.g. `"00:80:E1:27:F7:BB"` |
| `lora_sensors` | string[] | no | Array of LoRa sensor hex IDs, e.g. `["0x754A6237"]` |
| `ble_leak_sensors` | string[] | no | Array of BLE leak sensor MACs |
| `rules` | object | no | `{ "auto_close_enabled": bool, "trigger_mask": int }` |

At least one field is required. Each present array does a **full replace** of that whole category (e.g. sending `ble_leak_sensors` replaces all BLE sensors but leaves `valve_mac`/`lora_sensors` untouched).

**Validation asymmetry (important):**
- An **invalid `valve_mac`** format (not exactly `XX:XX:XX:XX:XX:XX` hex) is a **hard fail of the entire provision** → `cmd_ack error`.
- Invalid entries inside `lora_sensors` / `ble_leak_sensors` are **silently skipped** (warned, not added) and the command can still ack `ok`. → **Verify the resulting counts** in the snapshot/twin; don't assume `ok` means every sensor was added.

```json
{
  "schema": "eflostop.cmd", "ver": 1, "id": "prov-002", "cmd": "provision",
  "payload": {
    "valve_mac": "00:80:E1:27:F7:BB",
    "ble_leak_sensors": ["00:80:E1:27:99:E7", "00:80:E1:2A:AD:6D"],
    "lora_sensors": ["0x754A6237"]
  }
}
{
  "schema": "eflostop.cmd", 
  "ver": 1, 
  "id": "prov-002", 
  "cmd": "provision",
  "payload": {
    "ble_leak_sensors": ["00:80:e1:2a:3b:00", "00:80:e1:2a:3f:59"]
  }
}
```

Limits: 1 valve · up to 16 LoRa sensors · up to 16 BLE leak sensors.

What happens: config saved to NVS, health devices reloaded, and (if a valve MAC was set) the hub starts connecting to it over BLE. A lifecycle + snapshot telemetry follows.

| Error detail | Why |
|--------------|-----|
| `provisioning failed` | Invalid `valve_mac` format, no recognizable fields, NVS write failure, or empty payload |

Legacy: a **bare JSON object** (text starting with `{`) that does **not** match the envelope schema is treated as a provisioning payload. Note: a well-formed envelope is consumed by the envelope parser first, so this fallback only fires for non-envelope JSON.

---

## 4.10 decommission

Removes devices from the hub. The `target` field says what to remove.

| Field | Value |
|-------|-------|
| `cmd` | `"decommission"` |
| `payload.target` | `"valve"`, `"lora"`, `"ble"`, or `"all"` (required) |
| `payload.sensor_id` | required for `"lora"` / `"ble"` |

### 4.10.1 target: "valve"
Removes the valve, clears its target MAC, and disconnects BLE.
```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "decom-v-001", "cmd": "decommission", "payload": { "target": "valve" } }
```
Legacy text: `DECOMMISSION_VALVE`

### 4.10.2 target: "lora"
Removes one LoRa sensor (and its metadata). Requires `sensor_id`.
```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "decom-l-001", "cmd": "decommission", "payload": { "target": "lora", "sensor_id": "0x754A6237" } }
```
Legacy text: `DECOMMISSION_LORA:0x754A6237`

### 4.10.3 target: "ble"
Removes one BLE leak sensor (and its metadata). Requires `sensor_id`.
```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "decom-b-001", "cmd": "decommission", "payload": { "target": "ble", "sensor_id": "00:80:E1:27:99:E7" } }
```
Legacy text: `DECOMMISSION_BLE:00:80:E1:27:99:E7`

### 4.10.4 target: "all"
⚠️ **Full factory reset.** Wipes everything and restarts.
```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "decom-all-001", "cmd": "decommission", "payload": { "target": "all" } }
```
What happens, in order:
1. Erases all NVS provisioning data
2. Clears all sensor metadata
3. Clears hub identity (`hub_name`), DPS cache, and rules-engine persistent state (override window + incident latch)
4. Clears the valve target MAC and disconnects the valve BLE
5. Sends `cmd_ack` (envelope/`id` permitting)
6. Restarts the device after ~3 seconds
7. Hub boots unprovisioned and goes into captive-portal mode

Legacy text: `DECOMMISSION_ALL` or `DECOMMISSION`

| Error detail | Why |
|--------------|-----|
| `missing decommission target` | Payload missing or no `target` |
| `unknown decommission target` | `target` isn't `valve`/`lora`/`ble`/`all` |
| `valve decommission failed` | Valve not provisioned or NVS error |
| `lora sensor decommission failed` | Sensor ID not found or NVS error |
| `ble sensor decommission failed` | Sensor MAC missing/not found or NVS error |
| `full decommission failed` | NVS erase failed |

---

## 4.11 set_hub_name

Sets or clears the user-friendly name for this hub. Same name settable via Device Twin desired property `hub_name` — both paths write NVS and sync Twin reported.

| Field | Value |
|-------|-------|
| `cmd` | `"set_hub_name"` |
| `payload.name` | string, max 31 chars (required). `""` clears the name. |

```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "name-001", "cmd": "set_hub_name", "payload": { "name": "Kitchen Hub" } }
```

What happens:
1. Name saved to NVS (`hub_ident` namespace; persists across reboot, survives WiFi reset, cleared on `decommission all`).
2. A Twin reported PATCH is published immediately (this is **not** clock-gated — it goes out even before SNTP sync).
3. The name appears in telemetry snapshots under `gateway.name`.

| Error detail | Why |
|--------------|-----|
| `missing 'name' field` | Payload missing or no `name` key |
| `name too long (max 31 chars)` | Name exceeds 31 characters |

Envelope-only. No legacy text form.

---

# 5 Legacy Text Commands

Old plain-text commands. They still work but **never** return a `cmd_ack` (not envelopes, no correlation ID).

| Legacy text | Maps to command | Maps to payload |
|-------------|-----------------|-----------------|
| `VALVE_OPEN` | `valve_open` | (none) |
| `VALVE_CLOSE` | `valve_close` | (none) |
| `LEAK_RESET` | `leak_reset` | (none) |
| `OVERRIDE_CANCEL` | `override_cancel` | (none) |
| `DECOMMISSION_VALVE` | `decommission` | `{"target":"valve"}` |
| `DECOMMISSION_LORA:0x754A6237` | `decommission` | `{"target":"lora","sensor_id":"0x754A6237"}` |
| `DECOMMISSION_BLE:00:80:E1:27:99:E7` | `decommission` | `{"target":"ble","sensor_id":"00:80:E1:27:99:E7"}` |
| `DECOMMISSION_ALL` / `DECOMMISSION` | `decommission` | `{"target":"all"}` |
| `RULES_CONFIG:{json}` | `rules_config` | (the json after the colon) |
| `SENSOR_META:{json}` | `sensor_meta` | (the json after the colon) |
| `{json}` (bare, non-envelope) | `provision` | (the JSON itself) |

Keyword detection is case-insensitive; JSON after a `:` keeps its original case. `DECOMMISSION_LORA`/`_BLE` **must** include the `:` or the parse fails. **Envelope-only commands** (`valve_set_state`, `override_enable`, `set_hub_name`) have no legacy form.

---

# 6 Error Reference

## 6.1 Errors by command

| Command | Error detail | What went wrong |
|---------|-------------|-----------------|
| `valve_set_state` | `missing 'state' field ...` | No `state` in payload |
| `valve_set_state` | `invalid state value ...` | `state` not `"open"`/`"closed"` |
| `leak_reset` | `A leak is still active. Fix the leak first, or use override to open the valve during a leak.` | A leak source is still wet (guard) |
| `override_enable` | `No active leak to override. Use the normal Open Valve control.` | No incident/RMLEAK/window |
| `override_enable` | `Water detected at the valve. It can't be opened remotely until the valve area is dry.` | Valve flood probe wet |
| `override_enable` | `The valve isn't responding. Check its power and connection, then try again.` | Valve unreachable after ~10 s |
| `override_enable` | `No valve is set up for this hub.` | Unprovisioned / no valve MAC |
| `override_enable` | `Something went wrong applying the override. Your water state is unchanged. Try again.` | Internal error |
| `override_cancel` | `override cancel failed` | Internal error (mutex/init) — not the no-window case |
| `rules_config` | `rules config update failed` | Bad/missing JSON or NVS error |
| `sensor_meta` | `sensor metadata update failed` | Missing fields, table full (32), or NVS error |
| `provision` | `provisioning failed` | Bad `valve_mac`, empty/unknown payload, or NVS error |
| `decommission` | `missing decommission target` | No `target` |
| `decommission` | `unknown decommission target` | `target` not valve/lora/ble/all |
| `decommission` | `valve decommission failed` | Valve not provisioned / NVS error |
| `decommission` | `lora sensor decommission failed` | Sensor not found / NVS error |
| `decommission` | `ble sensor decommission failed` | Sensor not found / NVS error |
| `decommission` | `full decommission failed` | NVS erase failed |
| `set_hub_name` | `missing 'name' field` | No `name` |
| `set_hub_name` | `name too long (max 31 chars)` | Name > 31 chars |
| (any) | `unknown command` | `cmd` not recognized |

Note: `valve_open` / `valve_close` never report a transport/BLE failure — they always ack `ok` (the real outcome arrives later as `valve_state_changed` / next snapshot).

## 6.2 Parse failures

If a message can't be parsed at all (broken JSON, random text, nothing matches), the hub logs a warning and sends no ack (no correlation ID to respond to).

## 6.3 No ack before clock sync

Even a successful command produces no `cmd_ack` if the hub's clock isn't SNTP-synced yet (see §3.4). Reconcile via the next snapshot.

---

# 7 Security notes

## 7.1 Dangerous commands

| Command | Risk level | Notes |
|---------|------------|-------|
| `decommission` (target: `all`) | High | Wipes all config + identity + DPS cache and restarts. Back to unprovisioned. |
| `decommission` (target: `valve`) | Medium | Removes the valve → auto-close protection gone. |
| `provision` | Medium | Replaces device identities. Could point valve control at a different device. |
| `rules_config` (`auto_close_enabled:false`) | Medium | Disables automatic shutoff for all sensors. |
| `override_enable` | Medium | Deliberately pauses auto-close for 24 h during a known leak. |

Commands are authenticated through the Azure IoT Hub device identity (SAS token or X.509 cert). There's no extra command-level auth on the device side — the security boundary is the Azure IoT Hub connection.

## 7.2 App-side guidance

- Confirm before sending `decommission` (especially `all`) and `override_enable`.
- Use correlation IDs to match acks — but treat a missing ack as *unknown*, not *failed* (reconcile via snapshot; acks are clock-gated and C2D delivery can be delayed/queued).
- Don't let end users build raw C2D commands. Validate in the app/backend first.

---

# 8 Device Twin Properties

## 8.1 Reported properties (device → cloud)

Published on connect and after relevant changes (e.g. `set_hub_name`). This PATCH is **not** wrapped in the telemetry envelope and is **not** clock-gated, so it can publish before SNTP sync.

```json
{
  "fw_version": "1.4.1",
  "gateway_id": "GW-50787D0E28CC",
  "short_id": "28CC",
  "hub_name": "Beach House",
  "provisioned": true,
  "valve_mac": "00:80:E1:27:F7:BB",
  "lora_sensor_count": 1,
  "ble_leak_sensor_count": 3,
  "auto_close_enabled": true,
  "trigger_mask": 7,
  "uptime_s": 12345,
  "free_heap": 98000
}
```

`fw_version` is the same runtime value as `gateway.fw` (from `PROJECT_VER`). `valve_mac` is present only when a valve is provisioned; `auto_close_enabled`/`trigger_mask` only when the rules config reads back.

## 8.2 Desired properties (cloud → device)

| Property | Type | Range | Description |
|----------|------|-------|-------------|
| `snapshot_interval_s` | int | 60–3600 | Telemetry snapshot interval (not persisted across reboot — re-apply after each lifecycle) |
| `hub_name` | string | max 31 chars | User-assigned friendly name (persisted; `""` clears) |

```json
{ "hub_name": "Beach House" }
```

---

# 9 Quick Reference

```
C2D Commands:
Command              Payload
-----------------    ----------------------------------------
valve_open           (none)
valve_close          (none)
valve_set_state      { "state": "open"|"closed" }
leak_reset           (none)
override_enable      (none)              [envelope-only; fw >= 1.4.0]
override_cancel      (none)
rules_config         { auto_close_enabled, trigger_mask, trigger_* }
sensor_meta          { sensor_type, sensor_id, location_code, label }
provision            { valve_mac, lora_sensors, ble_leak_sensors, rules }
decommission         { "target": "valve|lora|ble|all", sensor_id? }
set_hub_name         { "name": "max 31 chars" }   [envelope-only]

Acks:  envelope cmds always ack (id optional); legacy text never acks;
       all acks suppressed until SNTP clock sync.

Device Twin Desired:
Property             Range
-----------------    ----------------------------------------
snapshot_interval_s  60-3600 (seconds)
hub_name             max 31 chars (friendly name)
```
