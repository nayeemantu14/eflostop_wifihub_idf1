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
| `id` | string | Same correlation ID you sent. **Omitted** (not `""`) if the request didn't have one. |
| `cmd` | string | Which command ran |
| `status` | string | `"ok"` or `"error"` |
| `error` | object | Only on errors. Has `code` (the command name) and `detail` (a readable message). |

## 3.4 When you get an ack — and when you don't

The hub publishes a `cmd_ack` only when **`cmd.is_envelope || cmd.id[0]`** is true. In practice:

- **Envelope commands always ack** (canonical or legacy envelope), even without an `id` — the ack then has no `data.id` key at all.
- **Legacy text commands never ack** (`VALVE_OPEN`, `DECOMMISSION_ALL`, etc.) — they aren't envelopes and carry no `id`. Fire-and-forget.
- The hub does **not** deduplicate by `id`. Send the same `id` twice and the command runs twice (commands are not idempotent — `valve_close` twice tries to close twice). Use a new `id` per retry.

⚠️ **Before the clock syncs.** The hub only connects to IoT Hub once its clock is SNTP-synced, so no command can reach it earlier. No `eflostop.v2` message goes out with a `ts` before epoch `1704067200` (2024-01-01 UTC). Since 2.1.4 an event built before the first sync, `cmd_ack` included, is held in the offline buffer and sent after the first connect, its `ts` corrected from the hub uptime once the clock syncs; one left over from a restart before the sync is dropped. Snapshots and lifecycle are not sent before the sync. Up to 2.1.3 every pre-sync event was discarded. An ack can still arrive late or not at all (a restart, a full offline buffer), so confirm real state from the next snapshot and never assume failure from a missing ack. (Twin reported PATCHes are *not* clock-gated — see §8.)

## 3.5 Duplicates, order and dedupe (2.1.4) — for the cloud team

This applies to every `eflostop.v2` message on `devices/<device_id>/messages/events/` (events, `cmd_ack`, snapshots, lifecycle), not only to acks. Background: HANDOFF §15n (WP2c) and §15o (WP2d) in `docs/field_logs/2.1.4/`.

**Delivery is at-least-once (QoS 1).** Since 2.1.4 an event whose publish sees the hub's MQTT connection end is also kept in the hub's offline buffer and replayed at the next connect (decision D3); it used to be lost when the MQTT client's queue expired it 30 s later. So:
- **The same event can arrive twice,** and the second copy can arrive **after newer events** (the MQTT client's own resend, or the replay, comes later). Every duplicate the hub produces is the same bytes, `ts` included, with one exception below.
- **Dedupe rule (decision D3, 2026-10-02): the cloud must dedupe on `gateway.id + ts + event + device id`, keeping the first copy.** `event` is `data.event`; the device id is `data.valve_id` or `data.sensor_id`, whichever is present.
- ⚠️ **The key is under review (HANDOFF §15n, TC-2).** `ts` has one-second resolution, so the key also merges two *different* messages: two `cmd_ack` in the same second (they carry no device id and differ only in `data.id` and `data.cmd`), two `valve_state_changed` for one valve in the same second (they differ in `data.valve_state`), and two `auto_close` for one sensor in one second (they differ in `data.cause`). Because every duplicate is byte-identical, deduping on the whole message body, or a hash of it, drops only true duplicates; the firmware reviewers recommend that. Until it is decided, a backend that keeps the key should add `data.id`, `data.cmd`, `data.valve_state` and `data.cause` where present.
- **The one exception:** an event raised before the hub's first clock sync gets its real `ts` when the clock syncs. If writing that `ts` to flash failed, it is worked out again at each replay, so two copies can differ in `ts` by about 1 s.
- **At-least-once is not "never lost".** An event the MQTT client accepted into a connection that is dying silently (the internet down while Wi-Fi stays up), before the broker acknowledged it, can still be lost, as before 2.1.4. The offline buffer holds 16 events and overwrites the oldest. The first snapshot after the reconnect carries the state.

**Order.**
- Order events by `ts` (the time the hub built the message), never by arrival or IoT Hub enqueued time: replayed events keep their original `ts` and `gateway.uptime_s`, and duplicates can arrive late.
- After a reconnect, buffered events come before that connection's `lifecycle`, and new events after it.
- `leak_detected` is sent before the `auto_close` it causes. Since 2.1.4 a leak report that meets a busy lock on the hub is decided a little later, so its `auto_close` can arrive after the same sensor's `leak_cleared`. Match an `auto_close` to its leak by `sensor_id` (or `valve_id`) and `ts`, not by position.
- A `device_offline` or `device_recovered` held on the hub during a stall can arrive after the `decommission` ack that removed that device. Ignore health events for device ids the hub no longer has.
- A `cmd_ack` still comes before the snapshot its command causes; other events can come between them.

**Twin reported (§8.1).** Since 2.1.4 the hub builds its twin reports on one task and sends them from another, while some command handlers still report at once. After a reconnect with buffered events, or a `provision` followed within about a second by `rules_config` or `set_hub_name`, an older reported value can be written after a newer one and stay until the hub's next report (HANDOFF §15n, TW-1, under review). Before acting on a value that was just changed, use the command's `cmd_ack`, or read `reported` again after the next report.

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

What happens: the hub runs these checks in order, **before anything is sent to the valve**. The first one that fails is refused with a `cmd_ack` error, and nothing is queued:

1. **No valve provisioned** → `No valve is set up for this hub.` The hub only ever connects to, and commands, the valve in its provisioning.
2. **The valve is locked after a leak** → the RMLEAK error below. That is either the valve's RMLEAK latch being asserted (valve locked after an auto-close), or, since 2.1.4, the hub having a leak incident latched with no override window active. The incident check applies even while the valve is disconnected, when its RMLEAK cannot be read. Forwarding the open would let the valve briefly honour it before its own RMLEAK interlock re-closes it, a sub-second water-on transient.
3. **The valve battery is at or below 10 %** → `Valve battery critical (≤10 %): the valve will not open. Replace the batteries.` The check uses the last real battery reading, which is kept while the valve is disconnected. An unknown battery (no reading yet) does not block the open. The valve itself refuses to open at that level.
4. Otherwise the hub requests a connect and queues the open. If the valve command queue is full → `The valve command could not be queued. Try again.`

An **`ok` ack means the open was queued, not that the valve opened**. GATT writes have no completion callback. The actual open is confirmed asynchronously by a `valve_state_changed` event (emitted when the valve reports its new state) and by the next snapshot, **not** by this command. To open during an active leak, use `override_enable`. It clears RMLEAK as part of the guarded 24 h window, so it bypasses check 2 by design.

**Changed in 2.1.4.** Up to 2.1.3 checks 1, 3 and 4 did not exist, so `valve_open` acked `ok` with no valve provisioned, at a critical battery, and when the enqueue failed. With no valve provisioned, the hub also connected to *any* nearby eFloStop valve by name and applied the open to it. Check 2 looked only at the valve's RMLEAK, which reads clear while the valve is disconnected, so an open sent while the valve was out of range during a leak was accepted, held, and written at the reconnect ahead of the close the leak was owed.

Errors (the `detail` strings are exact):
| Detail | Why |
|--------|-----|
| `No valve is set up for this hub.` | No valve is provisioned on this hub (2.1.4) |
| `Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.` | Valve RMLEAK latch is asserted, or (2.1.4) a leak incident is latched with no override window, even with the valve disconnected — clear it via `leak_reset` once dry, or open during a leak via `override_enable` |
| `Valve battery critical (≤10 %): the valve will not open. Replace the batteries.` | Last real valve battery reading is ≤ 10 % (2.1.4) |
| `The valve command could not be queued. Try again.` | The hub's valve command queue was full (2.1.4) |

The legacy text form sends no ack, so a refusal is visible only in the serial log: `VALVE_OPEN refused — <detail>`.

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

Same shape as `valve_open` but closes. A close is never refused for RMLEAK or battery. It is refused only when there is no valve to send it to, or it cannot be queued (below). An `ok` ack means the close was **queued**; the real state arrives later as a `valve_state_changed` event. Note a manual close does **not** assert RMLEAK or latch a leak incident — that only happens on auto-close.

**Changed in 2.1.4.** Up to 2.1.3 `valve_close` always acked `ok`.

Errors (the `detail` strings are exact):
| Detail | Why |
|--------|-----|
| `No valve is set up for this hub.` | No valve is provisioned on this hub |
| `The valve command could not be queued. Try again.` | The hub's valve command queue was full |

Serial log on a refusal: `VALVE_CLOSE refused — <detail>`.

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

`state:"open"` runs exactly the `valve_open` checks (§4.1), and `state:"closed"` the `valve_close` ones (§4.2). As there, `ok` means queued.

Errors (the `detail` strings are exact):
| Detail | Why |
|--------|-----|
| `missing 'state' field (expected "open" or "closed")` | Payload missing or no `state` key |
| `invalid state value (expected "open" or "closed")` | `state` is something other than `"open"`/`"closed"` |
| `No valve is set up for this hub.` | Either state, no valve provisioned (2.1.4) |
| `Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.` | `state:"open"` while the valve RMLEAK latch is asserted, or (2.1.4) a leak incident is latched with no override window, even with the valve disconnected (same guard as `valve_open`) |
| `Valve battery critical (≤10 %): the valve will not open. Replace the batteries.` | `state:"open"` with the last real valve battery reading ≤ 10 % (2.1.4) |
| `The valve command could not be queued. Try again.` | Either state, the hub's valve command queue was full (2.1.4) |

Serial log on a refusal: `VALVE_SET_STATE open refused — <detail>` or `VALVE_SET_STATE closed refused — <detail>`.

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

> **A hub with no valve publishes no `auto_close`** (2.1.4). With no valve provisioned there is nothing to close, so a leak sends `leak_detected` only, whatever this config says. Up to 2.1.3 it also sent `auto_close` with `rmleak_asserted:false`. The leak incident still latches (when `auto_close_enabled` and the source's trigger bit are set), so `rmleak_auto_cleared` and `rmleak_cleared` are still sent there, without `valve_id`.

> **Here `auto_close_enabled` is a pure master switch — it never touches `trigger_mask`.** That is the opposite of the same key at the top level of a `provision` payload (§4.9), where `true` also arms all three trigger bits. This command is for *editing settings*, so it changes exactly what you send; `provision` is for *answering a setup question*, so it does the obvious whole-system thing. Use this command for per-source tuning after commissioning.

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
| `sensor_type` | string | yes | `"ble_leak_sensor"` or `"lora"` (case-insensitive). **Legacy aliases `"ble"` and `"ble_leak"` are accepted permanently** — see §4.8.1. |
| `sensor_id` | string | yes | MAC address (BLE) or `0x`-hex ID (LoRa) |
| `location_code` | string | no | One of: `bathroom`, `kitchen`, `laundry`, `garage`, `garden`, `basement`, `utility`, `hallway`, `bedroom`, `living_room`, `attic`, `outdoor` (unknown value → `unknown`; omitted → keep existing) |
| `label` | string | no | Free text, **max 31 chars** (silently truncated, not rejected). Omitted → keep existing. |

```json
{
  "schema": "eflostop.cmd", "ver": 1, "id": "meta-001", "cmd": "sensor_meta",
  "payload": { "sensor_type": "ble_leak_sensor", "sensor_id": "00:80:E1:2A:3B:00", "location_code": "Kitchen", "label": "Sink" }
}
```

What happens: find-or-create the entry by `(type, id)`, persist to NVS (namespace `sen_meta`, up to **32** entries). Location data then appears in telemetry events and snapshots for that sensor.

| Error detail | Why |
|--------------|-----|
| `sensor metadata update failed` | Missing/invalid `sensor_type` or `sensor_id`, table full (32), or NVS error |

Legacy text: `SENSOR_META:{"sensor_type":"ble","sensor_id":"00:80:E1:27:99:E7","location_code":"laundry","label":"Downstairs laundry"}`

### 4.8.1 Device type vocabulary — one name, three accepted spellings

From FW **1.8.0** the BLE leak sensor is called **`ble_leak_sensor`** everywhere in the contract, in
both directions. Before 1.8.0 the same physical device had three different names depending on which
field you were looking at, which meant no single lookup table worked:

| Field | Direction | Before 1.8.0 | From 1.8.0 |
|---|---|---|---|
| `data.source_type` (leak / auto_close events) | outbound | `ble_leak_sensor` | `ble_leak_sensor` (unchanged) |
| health events' device-type field | outbound | `ble_leak` | **`ble_leak_sensor`** |
| `payload.sensor_type` (`sensor_meta`, inline `sensor_meta[]`) | inbound | `ble` | **`ble_leak_sensor`** (aliases kept) |
| `payload.target` (`decommission`) | inbound | `ble` | **`ble_leak_sensor`** (aliases kept) |

**Inbound is fully backward compatible.** `ble_leak_sensor`, `ble_leak` and `ble` are all accepted,
case-insensitively, and **the aliases are permanent** — nothing you send today will stop working. New
integrations should send `ble_leak_sensor`.

**One outbound field changed:** the health events' device-type field now reads `ble_leak_sensor` instead
of `ble_leak`. If you match on that value, accept both — hubs on firmware older than 1.8.0 still emit
`ble_leak`. Note the *key* carrying it also changed in 2.0.0 — see the note below.

`lora` was already consistent and is unchanged.

**MAC letter case (inbound):** send a MAC in whatever case you like — every inbound comparison is
`strcasecmp`, so `00:80:e1:2a:3b:00` and `00:80:E1:2A:3B:00` address the same sensor for
`sensor_meta`, `decommission` and `provision`. **Outbound is always UPPERCASE** from FW **1.9.0**:
the hub normalises the provisioned string on read, so the outbound identifier is a single value you
can join on exactly. Before 1.9.0 a sensor commissioned in lower case appeared lower case in snapshot
arrays and upper case in events — the same device under two keys. The examples in this document use
uppercase to match what comes back.

> **Superseded in 1.9.0.** This section previously stated that `valve` (a device) and `valve_flood` (water
> seen by the valve's own probe) were deliberately different values. That distinction has been removed.
> `data.source_type` now names the **device** in every case, so the valve is `valve` everywhere — in leak
> events, in `auto_close` events and in health events. The value `valve_flood` is no longer emitted on any
> outbound message. The inbound `rules_config` field `trigger_valve_flood` is a *config key*, not a device
> type, and is unchanged.

> **Changed again in 2.0.0 — the outbound key names.** Two things moved on the telemetry plane. Neither
> affects anything you *send*, but both affect what you *parse*.
>
> 1. **The device-type key is `data.source_type` on every outbound message.** Health events carried it as
>    `data.dev_type` up to 1.9.0, which made this one concept a third name alongside outbound `source_type`
>    and inbound `sensor_type`. The three values are unchanged.
> 2. **The identity key is named for the device type.** A valve reports `valve_id`, a leak sensor reports
>    `sensor_id`, replacing the single `device_id` that 1.9.0 used. It keeps the same position and the same
>    string form, so switch on `source_type` and read the matching key.

> **Changed again in 2.1.0 — `device_id` is gone.** 2.0.0 left the generic `device_id` on three families:
> **health alerts** (`device_offline`, `device_recovered`), the **`auto_close`** family (including
> `auto_close_blocked_override`) and the **RMLEAK interlock** events (`rmleak_cleared`,
> `rmleak_auto_cleared`) — on the grounds that the hub raises those *about* a device rather than the device
> reporting itself. That distinction described the firmware's internals, not anything you can act on, so it
> was removed. Those events now use `valve_id` / `sensor_id` like everything else, and `device_id` no longer
> appears anywhere on the telemetry plane. One rule now covers every message: read `valve_id` when
> `data.source_type` is `valve`, `sensor_id` otherwise.
>
> Nothing you *send* changes — this is a telemetry-plane rename only. The C2D command payloads and the
> `cmd_ack` shape are untouched.
>
> Full detail, with a worked example of every message: `docs/telemetry/eFloStop2_Telemetry_Messages_v4.0.docx`.

Unchanged, because they already use the canonical stem: the snapshot/provision array key
`ble_leak_sensors`, and the count field `ble_leak_sensor_count`.

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
| `valve_id` | string | no | BLE MAC of the valve, e.g. `"00:80:E1:27:F7:BB"`. **Canonical from FW 2.0.0.** |
| `valve_mac` | string | no | **Deprecated** alias of `valve_id`, still accepted. See the note below. |
| `lora_sensors` | string[] | no | Array of LoRa sensor hex IDs, e.g. `["0x754A6237"]` |
| `ble_leak_sensors` | string[] | no | Array of BLE leak sensor MACs |
| `auto_close_enabled` | bool | no | **The setup-flow opt-in.** `true` = a leak shuts the water off, and **all three** trigger sources are armed. `false` = master switch off. See below. |
| `rules` | object | no | `{ "auto_close_enabled": bool, "trigger_mask": int }` — the explicit form, for when you want a specific mask. |
| `sensor_meta` | object[] | no | Optional inline per-sensor metadata (label / location). Same element schema as the standalone `sensor_meta` command (§4.8). |

At least one field is required. Each present array does a **full replace** of that whole category (e.g. sending `ble_leak_sensors` replaces all BLE sensors but leaves the valve and `lora_sensors` untouched).

**Changing the valve (2.1.4).** A `provision` that names a different valve discards every valve command queued, pending or in flight for the old one, and drops a link still up to it. A connect already in flight to the old valve is not cancelled: it completes, and the hub then drops it at once, before pairing or any command, because that valve is no longer the provisioned one.

**`auto_close_enabled` — the one-question opt-in (FW 2.0.2).** The app asks the user once, while adding the valve and sensors, whether a leak should close the valve, and sends the answer in the same `provision` payload as the devices it applies to.

- **`true`** sets the master flag **and** arms `trigger_mask` to `7` — BLE leak sensors, LoRa sensors *and* the valve's own flood probe. A user who wants leaks to shut the water off means all of them; the valve standing in water is the least ambiguous leak there is. The user can narrow this afterwards with `rules_config` (§4.7).
- **`false`** flips the master flag only and **leaves `trigger_mask` untouched**. The mask is never consulted while the master flag is off, so clearing it would buy nothing and would throw away a per-source selection the user gets back for free by re-enabling.
- **Omitted, or JSON `null`** leaves the stored setting alone. Fresh hubs default to enabled with all triggers armed. Emit `null` rather than `false` for "the user was not asked" — `false` disarms automatic shutoff.

> **Send it only when the user actually answered.** A later `provision` that adds a sensor and re-sends `auto_close_enabled: true` out of habit will re-arm all three trigger bits, silently undoing any per-source narrowing the user made through `rules_config` in between. Omit the key on incremental provisions.
- Only a **real JSON boolean** counts. `1` and `"true"` are ignored with a warning, leaving the stored value unchanged.

> **This key does not mean quite the same thing in `rules_config`.** There it is a pure master switch and never touches the mask, because that caller is editing settings and says exactly what it wants. Here it is answering a setup question and gets the obvious whole-system behaviour. If you want an explicit mask during commissioning, send `rules` instead of (or as well as) this key.

**Precedence when both are present.** The top-level flag is applied first, then `rules` is merged over it — specific beats shorthand. So `{"auto_close_enabled": true, "rules": {"trigger_mask": 3}}` ends up **enabled with only the two sensor bits armed**, the valve probe excluded.

**A provision that empties the hub (2.1.4).** When the sensor arrays in a `provision` leave the hub with no device, the hub resets `auto_close_enabled` / `trigger_mask` to `true` / `7` and clears the leak latch and any override window, before the ack. Rules keys in the same payload (`auto_close_enabled`, `rules`) apply on top of those defaults. The hub stays marked provisioned: lifecycle and twin reported then read `provisioned:true` with no devices, where a hub emptied by `decommission` reads `false`. Treat both as an empty hub.

`auto_close_enabled` also satisfies the at-least-one-field requirement on its own — which means a payload whose only key is `auto_close_enabled` marks an unprovisioned hub as commissioned with no devices. Send it with the devices it describes, as the setup flow does.

> **Renamed in 2.0.0: `valve_mac` → `valve_id`.** The hub reports the valve's identity as `valve_id` on the
> snapshot, on valve and leak events, on the lifecycle message and in twin reported — so the same spelling now
> works in both directions. `valve_mac` remains accepted so existing app and production-tool builds keep
> commissioning; it logs a deprecation warning and will be removed. **Migrate to `valve_id`.**
>
> If a payload carries both, `valve_id` wins. If `valve_id` is present but malformed, the hub still tries
> `valve_mac` before failing the command — so a transitional backend that always sends `valve_mac` and
> populates `valve_id` only when it knows the value cannot lose a good commissioning to a bad one.

**Validation asymmetry (important):**
- If a valve key is offered and **no spelling of it yields a valid MAC** (`XX:XX:XX:XX:XX:XX` hex, exactly 17 characters), the **entire provision hard-fails** → `cmd_ack error`, and the sensor arrays and rules in the same payload are discarded too.
- Invalid entries inside `lora_sensors` / `ble_leak_sensors` are **silently skipped** (warned, not added) and the command can still ack `ok`. → **Verify the resulting counts** in the snapshot/twin; don't assume `ok` means every sensor was added.

```json
{
  "schema": "eflostop.cmd", "ver": 1, "id": "prov-002", "cmd": "provision",
  "payload": {
    "valve_id": "00:80:E1:27:F7:BB",
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
    "valve_id": "00:80:E1:27:F7:BB",
    "ble_leak_sensors": ["00:80:e1:2a:3b:00", "00:80:e1:2a:3f:59"]
  }
}
```

**Inline sensor metadata (optional, additive):** a `provision` may also carry a `sensor_meta` array so a sensor's label/location is set in the *same* command that provisions it. Each element is identical to the standalone `sensor_meta` payload (`{sensor_type, sensor_id, location_code, label}`, §4.8) and is applied through the same parse/validate/apply path. Fully optional — a bare `provision` with no `sensor_meta` key behaves exactly as before. Malformed individual entries are skipped (warned), never failing the provision. `sensor_id` is matched case-insensitively, so any-case MACs resolve.

```json
{
  "schema": "eflostop.cmd", "ver": 1, "id": "prov-003", "cmd": "provision",
  "payload": {
    "valve_id": "00:80:E1:27:F7:BB",
    "ble_leak_sensors": ["00:80:e1:2a:3b:00", "00:80:e1:2a:3f:59"],
    "sensor_meta": [
      { "sensor_type": "ble_leak_sensor", "sensor_id": "00:80:E1:2A:3B:00", "location_code": "bathroom", "label": "Ensuite" },
      { "sensor_type": "ble_leak_sensor", "sensor_id": "00:80:E1:2A:3F:59", "location_code": "kitchen",  "label": "Sink" }
    ]
  }
}
{
  "schema": "eflostop.cmd", "ver": 1, "id": "prov-003", "cmd": "provision",
  "payload": {
    "valve_id": "00:80:E1:27:F7:BB",
    "ble_leak_sensors": ["00:80:e1:2a:3b:00"],
    "sensor_meta": [
      { "sensor_type": "ble_leak_sensor", "sensor_id": "00:80:E1:2A:3B:00", "location_code": "bathroom", "label": "Ensuite" }
    ]
  }
}
```

**The complete setup-flow payload** — valve, sensors, their labels and the auto-close answer, in one command:

```json
{
  "schema": "eflostop.cmd", "ver": 1, "id": "prov-004", "cmd": "provision",
  "payload": {
    "valve_id": "00:80:E1:27:F7:BB",
    "ble_leak_sensors": ["00:80:e1:2a:3b:00"],
    "sensor_meta": [
      { "sensor_type": "ble_leak_sensor", "sensor_id": "00:80:E1:2A:3B:00", "location_code": "bathroom", "label": "Ensuite" }
    ],
    "auto_close_enabled": true
  }
}

{
  "schema": "eflostop.cmd", "ver": 1, "id": "prov-004", "cmd": "provision",
  "payload": {
    "valve_id": "00:80:E1:27:F7:BB",
    "ble_leak_sensors": ["00:80:e1:2a:2b:a5", "00:80:e1:2a:cb:b6","00:80:e1:2a:b6:8e", "00:80:e1:2a:29:fc"],
    "sensor_meta": [
      { "sensor_type": "ble_leak_sensor", "sensor_id": "00:80:e1:2a:2b:a5", "location_code": "bathroom", "label": "Ensuite" },
      { "sensor_type": "ble_leak_sensor", "sensor_id": "00:80:e1:2a:cb:b6", "location_code": "bathroom", "label": "Main Bathroom" },
      { "sensor_type": "ble_leak_sensor", "sensor_id": "00:80:e1:2a:b6:8e", "location_code": "kitchen", "label": "dishwasher" },
      { "sensor_type": "ble_leak_sensor", "sensor_id": "00:80:e1:2a:29:fc", "location_code": "laundry", "label": "under washer" }
],
    "auto_close_enabled": true
  }
}
```

Confirm the result two ways. Twin reported is republished **immediately** on a successful provision (FW 2.0.2 — before that it only refreshed on the next MQTT reconnect, so the twin could read stale for hours), carrying `auto_close_enabled`, `trigger_mask`, `valve_id` and the device counts. The snapshots that follow carry the same rules values under `data.rules`. The command's own `event` snapshot comes first. When the provision added devices, a `boot` snapshot follows once every new device has been heard (or after the 150 s sync window), and a `commission` snapshot for a device first heard after that, within the post-provision window. Since 2.1.4 a provision that adds no device (an identical re-send, a rules-only provision) does not restart the commission snapshot or the post-provision snapshot pulse.

Limits: 1 valve · up to 16 LoRa sensors · up to 16 BLE leak sensors.

What happens: config saved to NVS, health devices reloaded, and (if a valve MAC was set) the hub starts connecting to it over BLE. The `cmd_ack`, the refreshed twin reported and the command's own `event` snapshot follow (above). No lifecycle message follows a provision: since 2.1.4 the hub sends lifecycle only on an MQTT (re)connect, provisioned or not. (Up to 2.1.3 a hub that connected while unprovisioned held its lifecycle back and sent it after the first provision.)

| Error detail | Why |
|--------------|-----|
| `provisioning failed` | No usable valve identifier (neither `valve_id` nor `valve_mac` valid), no recognizable fields, NVS write failure, or empty payload |

Legacy: a **bare JSON object** (text starting with `{`) that does **not** match the envelope schema is treated as a provisioning payload. Note: a well-formed envelope is consumed by the envelope parser first, so this fallback only fires for non-envelope JSON.

---

## 4.10 decommission

Removes devices from the hub. The `target` field says what to remove.

Since 2.1.4, a removal that leaves the hub with no device also puts the rules config back to `auto_close_enabled` true / `trigger_mask` 7 and clears the leak latch and any override window, before the ack. A `provision` or `rules_config` sent after it applies on top of that.

| Field | Value |
|-------|-------|
| `cmd` | `"decommission"` |
| `payload.target` | `"valve"`, `"lora"`, `"ble_leak_sensor"`, or `"all"` (required). Legacy aliases `"ble"` / `"ble_leak"` accepted permanently — see §4.8.1. |
| `payload.sensor_id` | required for `"lora"` / `"ble_leak_sensor"` |

### 4.10.1 target: "valve"
Removes the valve, clears its target MAC, and disconnects BLE. A connect in progress is cancelled, and any valve command still queued or waiting for a reconnect is discarded, so nothing sent for the removed valve reaches the next one. Since 2.1.4, sending it to a hub that has no valve acks `error` `valve decommission failed` (it used to ack `ok`). The removed valve's own leak reading is dropped with it: if no sensor is wet, the leak incident is released at once, with no `rmleak_auto_cleared` event. From then on a leak publishes no `auto_close` until a valve is provisioned again (§4.7).
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
{ "schema": "eflostop.cmd", "ver": 1, "id": "decom-b-001", "cmd": "decommission", "payload": { "target": "ble_leak_sensor", "sensor_id": "00:80:E1:2A:3F:59" } }

{ "schema": "eflostop.cmd", "ver": 1, "id": "decom-b-002", "cmd": "decommission", "payload": { "target": "ble_leak_sensor", "sensor_id": "00:80:E1:2A:3B:00"} }
Legacy text: `DECOMMISSION_BLE:00:80:E1:27:99:E7`

### 4.10.4 target: "all"
⚠️ **Full factory reset.** Wipes everything and restarts.
```json
{ "schema": "eflostop.cmd", "ver": 1, "id": "decom-all-001", "cmd": "decommission", "payload": { "target": "all" } }
```
What happens, in order:
1. Erases all NVS provisioning data
2. Clears the valve target MAC and disconnects the valve BLE (first, since 2.1.4)
3. Clears all sensor metadata
4. Clears hub identity (`hub_name`), DPS cache, and rules-engine persistent state (override window + incident latch)
5. Sends `cmd_ack` (envelope/`id` permitting)
6. Publishes one last `decommission` snapshot with the empty-hub shape, clears the offline event buffer, and restarts after ~3 seconds
7. Hub boots unprovisioned. Its Wi-Fi credentials are kept (only the hub's Wi-Fi reset button clears them), so it reconnects, re-registers with DPS, and publishes an empty `boot` snapshot followed by heartbeats. It does not open the captive portal.

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
| `valve_open`, `valve_close`, `valve_set_state` | `No valve is set up for this hub.` | No valve provisioned (2.1.4) |
| `valve_open`, `valve_set_state` (open) | `Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.` | Valve RMLEAK latch asserted, or (2.1.4) a leak incident latched with no override window, even with the valve disconnected |
| `valve_open`, `valve_set_state` (open) | `Valve battery critical (≤10 %): the valve will not open. Replace the batteries.` | Last real valve battery reading ≤ 10 % (2.1.4) |
| `valve_open`, `valve_close`, `valve_set_state` | `The valve command could not be queued. Try again.` | Valve command queue full (2.1.4) |
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
| `provision` | `provisioning failed` | No usable `valve_id`/`valve_mac`, empty/unknown payload, or NVS error |
| `decommission` | `missing decommission target` | No `target` |
| `decommission` | `unknown decommission target` | `target` not valve/lora/ble/all |
| `decommission` | `valve decommission failed` | Valve not provisioned / NVS error |
| `decommission` | `lora sensor decommission failed` | Sensor not found / NVS error |
| `decommission` | `ble sensor decommission failed` | Sensor not found / NVS error |
| `decommission` | `full decommission failed` | NVS erase failed |
| `set_hub_name` | `missing 'name' field` | No `name` |
| `set_hub_name` | `name too long (max 31 chars)` | Name > 31 chars |
| (any) | `unknown command` | `cmd` not recognized |

Note: an `ok` from `valve_open` / `valve_close` / `valve_set_state` means the command was **queued**, not that the valve moved. Those commands report no BLE transport failure: the real outcome arrives later as `valve_state_changed` and the next snapshot. They ack `error` only for the refusals listed above. Up to 2.1.3 the only refusal was RMLEAK; every other case acked `ok`.

## 6.2 Parse failures

If a message can't be parsed at all (broken JSON, random text, nothing matches), the hub logs a warning and sends no ack (no correlation ID to respond to).

## 6.3 Acks and the clock sync

No `cmd_ack` is sent with an unsynced `ts`. Since 2.1.4 one built before the first clock sync is held and sent after the first connect with its `ts` corrected, and one left by a restart before the sync is dropped (see §3.4). Reconcile a missing ack via the next snapshot.

---

# 7 Security notes

## 7.1 Dangerous commands

| Command | Risk level | Notes |
|---------|------------|-------|
| `decommission` (target: `all`) | High | Wipes all config + identity + DPS cache and restarts. Back to unprovisioned. |
| `decommission` (target: `valve`) | Medium | Removes the valve → auto-close protection gone. |
| `provision` | Medium | Replaces device identities. Could point valve control at a different device. |
| `rules_config` / `provision` (`auto_close_enabled:false`) | Medium | Disables automatic shutoff for all sensors. Note that an app which serialises optional fields must emit `null`, not `false`, for "not asked". |
| `override_enable` | Medium | Deliberately pauses auto-close for 24 h during a known leak. |

Commands are authenticated through the Azure IoT Hub device identity (SAS token or X.509 cert). There's no extra command-level auth on the device side — the security boundary is the Azure IoT Hub connection.

## 7.2 App-side guidance

- Confirm before sending `decommission` (especially `all`) and `override_enable`.
- Use correlation IDs to match acks — but treat a missing ack as *unknown*, not *failed* (reconcile via snapshot; an ack can be held until the hub reconnects, and C2D delivery can be delayed/queued).
- Don't let end users build raw C2D commands. Validate in the app/backend first.

---

# 8 Device Twin Properties

## 8.1 Reported properties (device → cloud)

Published on connect and after relevant changes (e.g. `set_hub_name`). This PATCH is **not** wrapped in the telemetry envelope and is **not** clock-gated, so it can publish before SNTP sync.

```json
{
  "fw_version": "2.0.0",
  "gateway_id": "GW-50787D0E28CC",
  "short_id": "28CC",
  "hub_name": "Beach House",
  "provisioned": true,
  "valve_mac": null,
  "valve_device_id": null,
  "valve_id": "00:80:E1:27:F7:BB",
  "lora_sensor_count": 1,
  "ble_leak_sensor_count": 3,
  "auto_close_enabled": true,
  "trigger_mask": 7,
  "snapshot_interval_s": 900,
  "uptime_s": 12345,
  "free_heap": 98000
}
```

`fw_version` is the same runtime value as `gateway.fw` (from `PROJECT_VER`).
`auto_close_enabled`/`trigger_mask` appear only when the rules config reads back.
`snapshot_interval_s` (added 2.0.2) is unconditional and reports the cadence **in force** — compare it
against `desired.snapshot_interval_s` to confirm a write was accepted (§8.2).

**`valve_id` is always present, and is `null` when no valve is provisioned.** It is not omitted — a twin
reported PATCH is a *merge*, so an omitted key keeps its previous value forever, and a decommissioned hub
would go on reporting the valve it no longer has.

> **Renamed in 2.0.0 — and read this before you write the parser.** The valve identity property has now had
> three names: `valve_mac` (≤ 1.7.0), `valve_device_id` (1.8.0–1.9.0), and **`valve_id`** from 2.0.0, which
> matches what telemetry and the `provision` command both use (§4.9).
>
> Because twin PATCHes merge, an in-place upgrade would otherwise leave a hub reporting `valve_id` **next to**
> a frozen `valve_device_id`, and a backend written as `reported.valve_device_id ?? reported.valve_id` would
> silently keep reading the stale one — including after a valve swap. So 2.0.0 explicitly writes
> `"valve_mac": null` and `"valve_device_id": null`, which **deletes** those properties from the twin. You may
> see the nulls in a raw twin fetch; treat both keys as gone. They will stop being sent one release after
> every hub has upgraded.
>
> **Read `reported.valve_id` and nothing else.**

## 8.2 Desired properties (cloud → device)

| Property | Type | Range | Description |
|----------|------|-------|-------------|
| `snapshot_interval_s` | int | 60–3600 | Heartbeat snapshot interval. Persisted; takes effect immediately. Default 300. |
| `hub_name` | string | max 31 chars | User-assigned friendly name (persisted; `""` clears) |

```json
{ "snapshot_interval_s": 900, "hub_name": "Beach House" }
```

**Changed in 2.0.2 — you no longer have to re-apply after a reboot.** Before this release the interval lived only in RAM, and the hub never fetched the twin on connect (IoT Hub pushes a desired PATCH only when the document *changes*). A hub that rebooted therefore ran the 300 s default while the twin still advertised your value, and nothing reconciled the two until somebody edited the twin again. Now the value is persisted, and the hub issues a `$iothub/twin/GET` once per connection and applies whatever the twin says.

**Both writable settings are echoed in reported.** `reported.snapshot_interval_s` carries the interval **actually in force**, which is how you confirm a write landed — and how you detect one that didn't. An out-of-range value is rejected and the previous value is kept, in RAM and in flash; the hub still publishes reported afterwards, so `desired.snapshot_interval_s: 30` followed by `reported.snapshot_interval_s: 300` is the signature of a rejected write. Desired properties produce no `cmd_ack`, so this echo is the only acknowledgement you get.

Cleared to the 300 s default by `decommission` with target `all`, alongside the other factory-reset state.

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
provision            { valve_id, lora_sensors, ble_leak_sensors, auto_close_enabled,
                       rules, sensor_meta[] (opt) }
                     auto_close_enabled:true also arms trigger_mask=7; rules{} wins if both sent
decommission         { "target": "valve|lora|ble|all", sensor_id? }
set_hub_name         { "name": "max 31 chars" }   [envelope-only]

Acks:  envelope cmds always ack (id optional); legacy text never acks;
       no ack carries an unsynced ts: one built before the first
       clock sync is held and sent after connect (2.1.4).

Device Twin Desired:  (both persisted; both echoed in reported)
Property             Range
-----------------    ----------------------------------------
snapshot_interval_s  60-3600 (seconds, default 300)
hub_name             max 31 chars (friendly name)

Read reported.snapshot_interval_s for the value ACTUALLY in force —
out-of-range writes are rejected and the previous value is kept.
```
