# eFloStop II Wi-Fi Hub — Cloud-to-Device Command Catalogue

*Every command the cloud can send, as a real example — v3.0*

> GENERATED FILE — produced by `docs/telemetry/build_commands.py` from `commands_data.py`.

| | |
|---|---|
| Document version | 3.0 (supersedes v1.0, which documented firmware 1.9.0) |
| Firmware version | 2.1.4 — `CMakeLists.txt:12` |
| Git commit | `ade686b615bf449a8c5dc93bc9c76b1af821ce01` |
| Schema | `eflostop.cmd` |
| Direction | cloud → hub (C2D) |
| Transport | devices/<device_id>/messages/devicebound/#  (MQTT, QoS 1) |
| Command count | 11 commands across 4 groups |

This document lists every command the cloud can send to the eFloStop II Wi-Fi Hub. Each entry is a
complete message exactly as it goes on the wire, followed by the acknowledgement you get back and every rejection
the hub can answer with. The error text is quoted exactly as the firmware emits it.

Commands arrive as ordinary Azure IoT Hub cloud-to-device messages. There are no direct methods and no
device-twin commands; the twin carries configuration only. The hub subscribes once on connect and dispatches
every message it receives on that topic.

This revision, v3.0, documents firmware 2.1.4. v2.0 (firmware 2.1.0) is the interim revision between v1.0
and this one; anything marked 2.1.4 is new since v2.0, chiefly the new refusals of the valve commands.

**Read the Traps section at the end before you write the client.** Several behaviours are surprising enough to
cost a day if you meet them in the field instead of here — in particular, a command with the wrong `schema` can
still execute while sending you no acknowledgement at all.

## What changed since v1.0

Document **v1.0** described firmware **1.9.0**; this is **v3.0**, describing firmware **2.1.4**. Unlike the telemetry plane, the command plane keeps a compatibility path: the one renamed key still accepts its old spelling.

| | v1.0 — firmware 1.9.0 | v3.0 — firmware 2.1.4 |
|---|---|---|
| Maximum command size | `~1 KB shared with the topic; anything larger was dropped silently, with no ack — a provision with inline sensor_meta broke at about six sensors` | `4 KB buffer plus fragment reassembly, so all 16 sensors fit in one provision. Over 8 KB is rejected WITH a cmd_ack error correlated to your id  (2.1.0)` |
| provision — valve identifier | `payload.valve_mac` | `payload.valve_id  (valve_mac still accepted, deprecated, logs a warning)` |
| provision — auto-close opt-in | `only payload.rules.auto_close_enabled (+ trigger_mask), nested` | `top-level payload.auto_close_enabled added: true also arms trigger_mask=7 (BLE + LoRa + valve probe), false flips the master flag only. The nested rules object still works and wins when both are sent.` |
| provision — a malformed valve key | `aborts the whole command` | `the other spelling is tried first; aborts only if NEITHER yields a valid MAC` |
| decommission — target matching | `the BLE family case-insensitive; "valve" / "lora" / "all" case-SENSITIVE` | `every target case-insensitive` |
| sensor_meta — sensor_type, sensor_id | `unchanged` | `unchanged — inbound metadata keys were already correct` |
| rules_config — trigger_valve_flood | `unchanged` | `unchanged — a config bit name, not a device type` |
| Twin reported — valve identifier | `valve_device_id` | `valve_id, always present, null when no valve; valve_mac and valve_device_id are explicitly nulled to delete them from the twin` |
| provision — twin reported | `refreshed only on the next MQTT reconnect, so it could read stale for hours` | `republished immediately on a successful provision` |
| Device Twin — documented at all | `absent from this document; snapshot_interval_s was undocumented` | `full Device Twin section: both desired properties, all 15 reported properties` |
| Twin desired — snapshot_interval_s | `RAM only: every reboot silently reverted to 300 s while the twin still advertised your value` | `persisted to NVS, and a twin GET on every connection reconciles a value changed while the hub was offline` |
| Twin desired — a rejected value | `silently ignored, invisible to the cloud` | `previous value kept; reported echoes what is IN FORCE, so desired vs reported disagreeing is the rejection signal` |
| Twin reported — snapshot_interval_s | `not reported at all` | `reported unconditionally` |
| Snapshot after a command | `only sensor_meta, provision and decommission; rules_config produced NOTHING, so the change was invisible until the next heartbeat` | `every command that succeeds is followed by a snapshot, labelled with the command name` |
| rules_config — twin reported | `stale until the next MQTT reconnect, so polling the twin to confirm read the OLD auto_close_enabled / trigger_mask` | `republished immediately after the ack` |
| valve_open / valve_close / valve_set_state — no valve provisioned | `ok, and the hub then connected to any nearby eFloStop valve and drove it` | `error: No valve is set up for this hub.  (2.1.4)` |
| valve_open / valve_set_state open — leak incident latched, no override window, valve disconnected or its RMLEAK clear | `ok; the open was held and written at the reconnect, ahead of the close the leak was owed` | `error: Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.  (2.1.4)` |
| valve_open / valve_set_state open — valve battery at or below 10 % | `ok, for a valve that refused to open` | `error: Valve battery critical (≤10 %): the valve will not open. Replace the batteries.  (2.1.4)` |
| valve_open / valve_close / valve_set_state — command queue full | `ok, and the command was dropped` | `error: The valve command could not be queued. Try again.  (2.1.4)` |
| decommission — target valve on a hub with no valve | `ok, with nothing to remove` | `error: valve decommission failed  (2.1.4)` |
| Valve commands when the valve is changed or removed | `a command queued for the old valve could be applied to the next valve that linked` | `every queued, pending or in-flight valve command is discarded  (2.1.4)` |
| Firmware version | `1.9.0` | `2.1.4` |

## The command envelope

Every command is a JSON object with the same four top-level keys. `payload` is present only
for commands that take arguments.

Keys are read in the order shown. The parser is strict about `schema` and `cmd` and permissive about everything
else — `ver` in particular is read and then never checked.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "cmd": "valve_close",
  "id": "req-8f23a1"
}
```

| Key | Type | Required | Notes |
|---|---|---|---|
| `schema` | string | yes | Exactly "eflostop.cmd" (or the accepted legacy "eflostop.cmd.v1"). Compared case-sensitively. Any other value does NOT produce an error — see Trap 1. |
| `ver` | number | no | Defaults to 1 when absent. Read, logged, and then ignored: there is no range check and nothing branches on it. Any number is accepted. |
| `cmd` | string | yes | The command name, matched case-sensitively. Truncated to 31 characters without warning; a longer name therefore matches nothing and answers "unknown command". |
| `id` | string | no | Your correlation id, echoed back on the acknowledgement. Truncated to 63 characters without warning. Omit it and you still get an ack for an enveloped command, but the ack carries no id key at all — it is absent, not empty. |
| `payload` | object | conditional | Required by the six commands that take arguments; must be absent or ignored for the other five. |

## Acknowledgements

The hub answers with a `cmd_ack`. This is not a reply channel — it is an ordinary telemetry event
on the normal device-to-cloud topic, so you correlate it by `data.id`.

**An ack is sent when the command arrived as a JSON envelope, or when it carried a non-empty `id`.** A legacy
plain-text command satisfies neither, so it executes silently. `data.error.code` is the command name rather than
a machine-readable code, so match on `data.error.detail` if you need to distinguish causes.

One caution on correlation: when you omit `id`, the ack omits `data.id` entirely rather than sending an empty
string, so a consumer that requires the key will fail to parse.

**Every command that succeeds is followed by a snapshot** (2.0.2). It carries `data.reason` `"event"` and arrives
within a second or two of the ack, so you never have to infer the resulting state from the acknowledgement
alone — read it from the snapshot. A failed command produces the ack only.

Bursts coalesce: several commands in quick succession yield one snapshot, and no more than one snapshot is
published every 5 seconds, so state may be batched but is never skipped.

**Success:**

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
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-8f23a1",
    "cmd": "valve_close",
    "status": "ok"
  }
}
```

**Failure:**

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 3612
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-9c02b7",
    "cmd": "valve_open",
    "status": "error",
    "error": {
      "code": "valve_open",
      "detail": "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."
    }
  }
}
```

## Index of commands

| ID | `cmd` | Payload | What it does |
|---|---|---|---|
| V1 | `valve_open` | none | Open the valve |
| V2 | `valve_close` | none | Close the valve |
| V3 | `valve_set_state` | yes | Set the valve to a given position |
| L1 | `leak_reset` | none | Clear the leak latch after the leak is fixed |
| L2 | `override_enable` | none | Start a 24-hour water-access override |
| L3 | `override_cancel` | none | Cancel the override window early |
| P1 | `provision` | yes | Commission the hub |
| P2 | `decommission` | yes | Remove one device, or wipe the hub |
| C1 | `sensor_meta` | yes | Name a sensor and give it a location |
| C2 | `rules_config` | yes | Configure auto-close |
| C3 | `set_hub_name` | yes | Name the hub |

## Valve control

> Three commands, two code paths. `valve_set_state` routes to exactly the same handlers as `valve_open` and `valve_close`, so prefer it for new work: one command, one shape, and the state you want is explicit rather than implied by the command name. All three queue a BLE write and return immediately — the ack means *accepted*, not *the valve moved*. Watch for the `valve_state_changed` event to know it actually moved. Since 2.1.4 all three are refused when no valve is provisioned or the valve command queue is full, and an open also while the valve's battery is critical; up to 2.1.3 every one of those cases acked `ok`. An open is also refused while the hub has a leak incident latched and no override window, even with the valve disconnected (2.1.4).

### V1 — `valve_open`

Queues a BLE write to the provisioned valve. Checked in this order before anything is sent, and the first failure is the answer: a valve is provisioned; the valve is not locked after a leak (its RMLEAK interlock is clear and, since 2.1.4, the hub has no leak incident latched without an override window); the valve's battery is not critical; the command fits in the queue.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "cmd": "valve_open",
  "id": "req-open-001"
}
```

The ack means the write was **queued**, not that the valve moved. Opening an already-open valve emits no event, because `valve_state_changed` is delta-gated.

**Acknowledgement on success:**

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
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-open-001",
    "cmd": "valve_open",
    "status": "ok"
  }
}
```

**Rejections.** Each is a `cmd_ack` with `status:"error"`; the `error.detail` text below is exact.

| `error.detail` | When |
|---|---|
| `No valve is set up for this hub.` | No valve is provisioned on this hub (2.1.4). Up to 2.1.3 this acked `ok`, and the hub then connected to any nearby eFloStop valve and opened it. |
| `Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.` | The valve's RMLEAK interlock is asserted, i.e. it latched after a leak. Since 2.1.4 also while the hub has a leak incident latched and no override window, even with the valve disconnected: an open sent while the valve was out of range used to be accepted, held, and written at the reconnect ahead of the close the leak was owed. Clear it with leak_reset once dry, or use override_enable to get water during a live leak. |
| `Valve battery critical (≤10 %): the valve will not open. Replace the batteries.` | The valve's last real battery reading is at or below 10 % (2.1.4). The valve refuses to open at that level, so up to 2.1.3 the hub acked `ok` for a valve that stayed shut. |
| `The valve command could not be queued. Try again.` | The hub's valve command queue was full, so nothing was queued (2.1.4; it used to ack `ok` and drop the command). |

*`c2d_valve_command() and valve_open_reject_reason() main/iothub/app_iothub.c`*

### V2 — `valve_close`

Queues a BLE write to the provisioned valve. Never refused for RMLEAK or battery; refused only when there is no valve to send it to or it cannot be queued. Up to 2.1.3 it always acked `ok`.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "cmd": "valve_close",
  "id": "req-close-001"
}
```

Does **not** touch RMLEAK, the incident latch or an active override window.

**Acknowledgement on success:**

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
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-close-001",
    "cmd": "valve_close",
    "status": "ok"
  }
}
```

**Rejections.** Each is a `cmd_ack` with `status:"error"`; the `error.detail` text below is exact.

| `error.detail` | When |
|---|---|
| `No valve is set up for this hub.` | No valve is provisioned on this hub (2.1.4). |
| `The valve command could not be queued. Try again.` | The hub's valve command queue was full, so nothing was queued (2.1.4). |

*`c2d_valve_command() main/iothub/app_iothub.c`*

### V3 — `valve_set_state`

The unified form, and the one to prefer. "open" runs exactly the V1 checks and "closed" the V2 ones.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "cmd": "valve_set_state",
  "id": "req-state-001",
  "payload": {
    "state": "closed"
  }
}
```

| Key | Type | Required | Rules |
|---|---|---|---|
| `state` | string | yes | Exactly "open" or "closed", case-sensitive. No aliases, no trimming. "OPEN", "Open", "close" and "shut" are all rejected. |

No legacy plain-text form — which is a safety property: a wrong `schema` here is rejected outright, where the same mistake on V1/V2 executes silently (Trap 1).

**Acknowledgement on success:**

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
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-state-001",
    "cmd": "valve_set_state",
    "status": "ok"
  }
}
```

**Rejections.** Each is a `cmd_ack` with `status:"error"`; the `error.detail` text below is exact.

| `error.detail` | When |
|---|---|
| `missing 'state' field (expected "open" or "closed")` | No payload, payload is not valid JSON, `state` is absent, or `state` is not a string. Checked before the value is looked at. |
| `No valve is set up for this hub.` | Either state, no valve provisioned on this hub (2.1.4). |
| `Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak.` | state is "open" and the RMLEAK interlock is set, or (2.1.4) a leak incident is latched with no override window, even with the valve disconnected. Same refusal as V1, but error.code here is "valve_set_state". |
| `Valve battery critical (≤10 %): the valve will not open. Replace the batteries.` | state is "open" and the last real valve battery reading is at or below 10 % (2.1.4). |
| `The valve command could not be queued. Try again.` | Either state, the hub's valve command queue was full (2.1.4). |
| `invalid state value (expected "open" or "closed")` | `state` is a string but is neither exactly "open" nor exactly "closed". Checked last. |

*`valve_set_state branch of handle_c2d_command(); c2d_valve_command() main/iothub/app_iothub.c`*

## Leak and override

> These manage the post-leak interlock. `leak_reset` is the normal path once the leak is fixed; `override_enable` is the sanctioned way to get water while a leak is still live. Note that `override_enable` can block for up to 10 seconds while it reconnects to the valve.

### L1 — `leak_reset`

Clears the incident latch and the valve's RMLEAK, cancels any override window and restores auto-close. Deliberately does NOT open the valve.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "cmd": "leak_reset",
  "id": "req-reset-001"
}
```

With nothing to clear it still acks `ok` and emits nothing. Treat a following `rmleak_cleared` event as the evidence that something actually changed.

**Acknowledgement on success:**

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
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-reset-001",
    "cmd": "leak_reset",
    "status": "ok"
  }
}
```

**Rejections.** Each is a `cmd_ack` with `status:"error"`; the `error.detail` text below is exact.

| `error.detail` | When |
|---|---|
| `A leak is still active. Fix the leak first, or use override to open the valve during a leak.` | THREE different causes collapse into this one string: a sensor is still reporting wet (the intended case); the rules engine is not initialised; or an internal 1-second lock timed out. The latter two report a leak when no leak is involved. |

*`main/iothub/app_iothub.c:714-726; main/rules_engine/rules_engine.c:731-811`*

### L2 — `override_enable`

The sanctioned way to get water during a live leak: clears the incident, blocks auto-close for 24 h, clears RMLEAK and opens the valve. Preconditions are checked first, so the window starts on execution, not on receipt.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "cmd": "override_enable",
  "id": "req-ovr-001"
}
```

**Can block for up to 10 seconds** while it reconnects to the valve — size the app's ack timeout accordingly. Calling it while a window is open refreshes the expiry to a fresh 24 h rather than erroring.

**Acknowledgement on success:**

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
    "event": "cmd_ack",
    "id": "req-ovr-001",
    "cmd": "override_enable",
    "status": "ok"
  }
}
```

**Rejections.** Each is a `cmd_ack` with `status:"error"`; the `error.detail` text below is exact.

| `error.detail` | When |
|---|---|
| `No valve is set up for this hub.` | The hub is not commissioned, or no valve MAC is configured. Checked first. |
| `The valve isn't responding. Check its power and connection, then try again.` | The valve was not reachable, and stayed unreachable for a bounded 10-second reconnect attempt. |
| `No active leak to override. Use the normal Open Valve control.` | There is nothing to override: no window already running, no incident latched, no RMLEAK asserted. Pre-emptive use is rejected. |
| `Water detected at the valve. It can't be opened remotely until the valve area is dry.` | The valve's own flood probe is wet. This is an absolute floor — no override, remote or physical, opens a valve standing in water. |
| `Something went wrong applying the override. Your water state is unchanged. Try again.` | Internal failure: the rules engine is not initialised, or a lock timed out. |

*`main/iothub/app_iothub.c:842-866; main/rules_engine/rules_engine.c:897-970`*

### L3 — `override_cancel`

Ends the window and re-enables auto-close. If leaks are still active it closes the valve and re-asserts RMLEAK there and then.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "cmd": "override_cancel",
  "id": "req-ovrc-001"
}
```

With no window running it acks `ok` and changes nothing. The window otherwise ends only by expiry, this command or `leak_reset` — never because sensors went dry.

**Acknowledgement on success:**

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
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-ovrc-001",
    "cmd": "override_cancel",
    "status": "ok"
  }
}
```

**Rejections.** Each is a `cmd_ack` with `status:"error"`; the `error.detail` text below is exact.

| `error.detail` | When |
|---|---|
| `override cancel failed` | Internal failure only — the rules engine is not initialised, or a lock timed out. Cancelling when there is no window is NOT an error. |

*`main/iothub/app_iothub.c:832-841; main/rules_engine/rules_engine.c:813-895`*

## Commissioning

> `provision` is additive per key but whole-list-replace per array: sending `ble_leak_sensors` replaces the entire list, and sending it empty wipes it. `decommission` with target `all` is the only command in this document that reboots the hub.

### P1 — `provision`

Sets the valve, the sensor lists, the auto-close choice and optional per-sensor metadata. Does not reboot. At least one of valve_id, lora_sensors, ble_leak_sensors, auto_close_enabled or rules must be present and well-formed.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "cmd": "provision",
  "id": "req-prov-001",
  "payload": {
    "valve_id": "C4:19:D1:88:2A:7F",
    "ble_leak_sensors": [
      "00:80:E1:27:9A:E6",
      "00:80:E1:27:A1:04"
    ],
    "auto_close_enabled": true,
    "sensor_meta": [
      {
        "sensor_type": "ble_leak_sensor",
        "sensor_id": "00:80:E1:27:9A:E6",
        "location_code": "kitchen",
        "label": "Under sink"
      },
      {
        "sensor_type": "ble_leak_sensor",
        "sensor_id": "00:80:E1:27:A1:04",
        "location_code": "laundry",
        "label": "Behind machine"
      }
    ]
  }
}
```

| Key | Type | Required | Rules |
|---|---|---|---|
| `valve_id` | string | no | Exactly 17 characters, XX:XX:XX:XX:XX:XX. Canonical since firmware 2.0.0 — the same key the hub reports back on the snapshot, the events, the lifecycle message and the twin. If NEITHER this nor valve_mac yields a usable MAC, the WHOLE command fails and nothing is saved — unlike a bad sensor MAC, which is merely skipped. |
| `valve_mac` | string | no | DEPRECATED alias of valve_id, still accepted so existing app and production-tool builds keep commissioning. Logs a warning. valve_id wins when both are present and valid; if valve_id is present but malformed, valve_mac is still tried before the command is failed. Will be removed — migrate to valve_id. |
| `lora_sensors` | string[] | no | Whole-list replace. Each entry MUST carry the "0x" prefix or it is silently dropped while the command still acks ok (Trap 4). Max 16; a longer list is truncated, not rejected. An empty array wipes the list. |
| `ble_leak_sensors` | string[] | no | Whole-list replace. Each entry must be a valid 17-character MAC; invalid entries are skipped without failing the command. Max 16, truncated if longer. An empty array wipes the list. |
| `auto_close_enabled` | boolean | no | The setup-flow opt-in, answered once while the devices are being added. true sets the master flag AND arms trigger_mask to 7 — BLE, LoRa and the valve's own flood probe. false flips the master flag only and leaves trigger_mask untouched, because the mask is not consulted while the flag is off. Omitted or JSON null leaves the stored setting alone; emit null, never false, for 'the user was not asked'. Must be a real boolean — 1 and "true" are ignored with a warning. NOTE this is NOT how the identically named key behaves in rules_config, where it never touches the mask. |
| `rules` | object | no | Merged. Accepts auto_close_enabled (boolean) and trigger_mask (number). The three convenience booleans of rules_config are NOT honoured here. Applied AFTER the top-level auto_close_enabled, so specific beats shorthand: {"auto_close_enabled":true,"rules":{"trigger_mask":3}} ends up enabled with the valve probe excluded. |
| `sensor_meta` | object[] | no | Applied after commissioning succeeds. Elements use exactly the same rules as the standalone sensor_meta command. A rejected element is skipped and never fails the provision. IMPORTANT: this key alone does not satisfy the at-least-one requirement (Trap 3). |

A commissioning window follows: a `commission` snapshot once every device has been heard, or at a 150 s deadline, then refreshes for about six minutes as late devices appear. Twin reported is republished immediately on success (2.0.2). Limits: 16 LoRa, 16 BLE leak sensors, 32 metadata entries. **A provision that changes the valve** (2.1.4) discards every valve command queued, pending or in flight for the old one, and drops a link still up to it. A connect already in flight to the old valve is not cancelled: it completes, and the hub then drops it at once, before pairing or any command, because that valve is no longer the provisioned one.

**Acknowledgement on success:**

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 95
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-prov-001",
    "cmd": "provision",
    "status": "ok"
  }
}
```

**Rejections.** Each is a `cmd_ack` with `status:"error"`; the `error.detail` text below is exact.

| `error.detail` | When |
|---|---|
| `provisioning failed` | One string for every cause: no payload; payload not valid JSON; a valve key was offered but no spelling of it yielded a valid MAC; none of valve_id / valve_mac / lora_sensors / ble_leak_sensors / auto_close_enabled / rules present and well-typed; or the storage write failed. |

*`main/iothub/app_iothub.c:891-933; main/provisioning_manager/provisioning_manager.c:359-616`*

### P2 — `decommission`

Removes one commissioned device, or erases everything and reboots. Removing a sensor also drops its stored location metadata.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "cmd": "decommission",
  "id": "req-dec-001",
  "payload": {
    "target": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:A1:04"
  }
}
```

| Key | Type | Required | Rules |
|---|---|---|---|
| `target` | string | yes | One of "valve", "lora", "ble_leak_sensor" or "all". The BLE family also accepts the aliases "ble_leak" and "ble". All targets are matched case-INSENSITIVELY as of firmware 2.0.0; before that only the BLE family was, so "ALL" and "LORA" were rejected. |
| `sensor_id` | string | conditional | Required for "lora" and for the BLE family; ignored for "valve" and "all". A LoRa id is parsed as hexadecimal with the "0x" prefix optional here. A BLE MAC must be the full 17-character form, matched case-insensitively. |

**Target `all` is the only command here that reboots.** Its final snapshot describes the CLEARED hub — `valve` `{}`, empty arrays, no name, "No devices provisioned" — not the site you wiped. **Target `valve`** discards every valve command still queued, pending or in flight, disconnects a link still up to the valve and cancels a connect in progress, so nothing sent for the removed valve reaches it or the next one (2.1.4). It also drops the removed valve's own leak reading, releasing the leak incident at once when no sensor is wet, and from then on a leak publishes no `auto_close` until a valve is provisioned again. **A removal that empties the hub** puts the rules config back to auto_close_enabled true / trigger_mask 7 and clears the leak latch and any override window before the ack; a `provision` or `rules_config` sent after it applies on top (2.1.4).

**Acknowledgement on success:**

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
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-dec-001",
    "cmd": "decommission",
    "status": "ok"
  }
}
```

**Rejections.** Each is a `cmd_ack` with `status:"error"`; the `error.detail` text below is exact.

| `error.detail` | When |
|---|---|
| `missing decommission target` | No payload, payload not parseable, or `target` absent or not a string. |
| `unknown decommission target` | `target` is a string but matches none of the accepted values — including a case-mismatched "ALL", "Valve" or "LORA". |
| `valve decommission failed` | The hub has no valve provisioned (2.1.4; it used to ack `ok`), or an internal failure: manager not initialised, lock timeout, or the storage write failed. |
| `lora sensor decommission failed` | sensor_id absent or unparseable, the id is not in the commissioned list, or an internal failure. |
| `ble sensor decommission failed` | sensor_id absent, malformed, not in the commissioned list, or an internal failure. |
| `full decommission failed` | target "all" and the erase failed. No reboot happens in this case. |

*`main/iothub/app_iothub.c:727-831`*

## Configuration

> None of these move the valve or change commissioning. `sensor_meta` and `set_hub_name` are cosmetic-but-persistent; `rules_config` decides whether a leak closes the valve at all.

### C1 — `sensor_meta`

Sets the location code and label for one already-commissioned sensor. The payload IS the metadata object — no array wrapper on this command.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "cmd": "sensor_meta",
  "id": "req-meta-001",
  "payload": {
    "sensor_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:9A:E6",
    "location_code": "kitchen",
    "label": "Under sink"
  }
}
```

| Key | Type | Required | Rules |
|---|---|---|---|
| `sensor_type` | string | yes | Case-insensitive. "ble_leak_sensor" is canonical; "ble_leak" and "ble" are permanent aliases. "lora" for a LoRa sensor. |
| `sensor_id` | string | yes | The MAC or LoRa id. No format validation is applied here. Truncated to 17 characters. Matched case-insensitively, so either case resolves to the same entry. |
| `location_code` | string | no | Three-state, deliberately: absent keeps the stored value; a recognised name applies it; an UNRECOGNISED name also keeps the stored value and still acks ok (Trap 6). "unknown" is itself a valid value and the only way to clear a location. |
| `label` | string | no | Free text, silently truncated to 31 characters — never rejected. An empty string clears the label. |

Accepted codes: `unknown`, `bathroom`, `kitchen`, `laundry`, `garage`, `garden`, `basement`, `utility`, `hallway`, `bedroom`, `living_room`, `attic`, `outdoor`. A successful update triggers an immediate snapshot.

**Acknowledgement on success:**

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 13000
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-meta-001",
    "cmd": "sensor_meta",
    "status": "ok"
  }
}
```

**Rejections.** Each is a `cmd_ack` with `status:"error"`; the `error.detail` text below is exact.

| `error.detail` | When |
|---|---|
| `sensor metadata update failed` | One string for every cause: no payload; payload not valid JSON or not an object; sensor_type missing or unrecognised; sensor_id missing or empty; the 32-entry table is full; or the storage write failed. An unrecognised location_code does NOT fail the command. |

*`main/iothub/app_iothub.c:876-890; main/sensor_meta/sensor_meta.c:285-359`*

### C2 — `rules_config`

Decides whether a leak closes the valve, and which sensor classes may trigger it. Merged onto the stored config, so an empty object is a valid no-op.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "cmd": "rules_config",
  "id": "req-rules-001",
  "payload": {
    "auto_close_enabled": true,
    "trigger_mask": 7
  }
}
```

| Key | Type | Required | Rules |
|---|---|---|---|
| `auto_close_enabled` | boolean | no | A PURE master switch here: it never touches trigger_mask. That is deliberately different from the identically named key at the top level of a provision payload, where true also arms all three trigger bits — this command edits settings and changes exactly what you send, provision answers a setup question. Must be a real JSON boolean. The number 1 or the string "true" are silently ignored. |
| `trigger_mask` | number | no | Bit 0 = BLE leak sensors, bit 1 = LoRa, bit 2 = the valve's own flood probe; 7 enables all. NO range validation and no clamping anywhere — bits 3 to 7 are stored, echoed back in the twin, and never consulted. 0 is legal and disables every trigger. |
| `trigger_ble_leak` | boolean | no | Convenience field for bit 0. Applied AFTER trigger_mask, so it wins over that bit if both are sent. |
| `trigger_lora` | boolean | no | Convenience field for bit 1, same precedence. |
| `trigger_valve_flood` | boolean | no | Convenience field for bit 2, same precedence. |

Emits no telemetry EVENT, but from 2.0.2 a snapshot and a twin reported publish both follow the ack — read the new values from data.rules or from reported. Before 2.0.2 this command produced neither, so the change stayed invisible until the next heartbeat. The same settings also arrive via `provision`; the convenience booleans are exclusive to this command. **A hub with no valve provisioned publishes no `auto_close`** on a leak, whatever this config says (2.1.4; up to 2.1.3 it sent one with rmleak_asserted false): there is nothing to close, and the leak_detected still goes out.

**Acknowledgement on success:**

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 14000
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-rules-001",
    "cmd": "rules_config",
    "status": "ok"
  }
}
```

**Rejections.** Each is a `cmd_ack` with `status:"error"`; the `error.detail` text below is exact.

| `error.detail` | When |
|---|---|
| `rules config update failed` | No payload, payload not valid JSON, the rules engine is not initialised, or the storage write failed. A nonsensical trigger_mask is NOT an error. |

*`main/iothub/app_iothub.c:867-875; main/rules_engine/rules_engine.c:634-702`*

### C3 — `set_hub_name`

Sets the name shown as gateway.name in telemetry and hub_name in the twin. Survives a Wi-Fi reset; cleared by a full decommission.

```json
{
  "schema": "eflostop.cmd",
  "ver": 1,
  "cmd": "set_hub_name",
  "id": "req-name-001",
  "payload": {
    "name": "Main House"
  }
}
```

| Key | Type | Required | Rules |
|---|---|---|---|
| `name` | string | yes | Maximum 31 characters, counted in BYTES — so a UTF-8 name with accents fits fewer glyphs. 32 or more is REJECTED outright, not truncated, and nothing is written. An empty string is a valid explicit CLEAR. |

Publishes twin reported immediately. The handler ignores the storage result, so a failed write still acks `ok` (Trap 7). Also settable as a twin desired property, which sends no acknowledgement at all.

**Acknowledgement on success:**

```json
{
  "schema": "eflostop.v2",
  "ts": 1785398400,
  "gateway": {
    "id": "GW-A0B7651C2D3E",
    "short_id": "2D3E",
    "name": "Main House",
    "fw": "2.1.4",
    "uptime_s": 15000
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "req-name-001",
    "cmd": "set_hub_name",
    "status": "ok"
  }
}
```

**Rejections.** Each is a `cmd_ack` with `status:"error"`; the `error.detail` text below is exact.

| `error.detail` | When |
|---|---|
| `missing 'name' field` | No payload, payload not parseable, or `name` absent or not a string. |
| `name too long (max 31 chars)` | Over 31 bytes. Nothing is written and the previous name is kept. |

*`main/iothub/app_iothub.c:934-951; main/hub_identity/hub_identity.c:91-137`*

## Device Twin

Two settings are changed through the twin rather than through a command. Write them under
`properties.desired`.

**Desired properties produce no `cmd_ack`.** The acknowledgement is that the hub echoes the value it actually
applied into `properties.reported` — agreement means accepted, disagreement means rejected.

Both persist across a reboot and a Wi-Fi reset, and are cleared only by `decommission` with target `all`.

### Desired properties (cloud writes, hub reads)

| Property | Type | Range | Notes |
|---|---|---|---|
| `snapshot_interval_s` | number | 60 - 3600 | Heartbeat snapshot cadence in seconds; default 300. Applied immediately, without a reboot or reconnect: the pending deadline is re-aimed from the last confirmed publish, so shortening the interval takes effect at once rather than after the old one expires. A value outside the range is REJECTED with the previous value kept in both RAM and flash - watch for the desired/reported disagreement. Must be a JSON number. |
| `hub_name` | string | 0 - 31 chars | User-assigned friendly name. Appears as gateway.name in telemetry and hub_name in reported. An empty string clears it. A longer value is ignored with a warning. The set_hub_name command (C3) does the same thing and is preferable when you want an ack. |

```json
{
  "properties": {
    "desired": {
      "snapshot_interval_s": 900,
      "hub_name": "Beach House"
    }
  }
}
```

### Reported properties (hub writes, cloud reads)

Republished on every MQTT connect, after any desired change, and after a successful `provision` or
`set_hub_name`. Written in the order below.

Device-owned: writing any of these under `desired` does nothing. In particular **`auto_close_enabled` and
`trigger_mask` are read-only here** - change them with `rules_config` (C2) or the `provision` opt-in (P1).

| Property | Type | Notes |
|---|---|---|
| `fw_version` | string | Same runtime value as gateway.fw, from PROJECT_VER. |
| `gateway_id` | string | GW-xxxxxxxxxxxx, derived from the Wi-Fi MAC. Immutable. |
| `short_id` | string | Last four hex of the gateway id; also the AP SSID suffix. |
| `hub_name` | string | Empty string when never set - not absent. |
| `provisioned` | boolean | Whether any device identity is stored. |
| `valve_mac` | null | ALWAYS null. Explicitly nulled to DELETE the pre-1.8.0 spelling from the twin. |
| `valve_device_id` | null | ALWAYS null. Same, for the 1.8.0-1.9.0 spelling. See Trap 12. |
| `valve_id` | string|null | Canonical valve identity. Always present, null when no valve is provisioned. |
| `lora_sensor_count` | number | 0 - 16. |
| `ble_leak_sensor_count` | number | 0 - 16. |
| `auto_close_enabled` | boolean | Read-only mirror of the rules config. Omitted if the config read fails. |
| `trigger_mask` | number | Read-only mirror. Bit 0 BLE, bit 1 LoRa, bit 2 valve probe. |
| `uptime_s` | number | Seconds since boot at the moment the patch was built. |
| `snapshot_interval_s` | number | The cadence IN FORCE - not necessarily what desired asks for. This is how you confirm a write landed, and how you detect a rejected one. Added in 2.0.2. |
| `free_heap` | number | Bytes. Diagnostic only; varies constantly. |

**A value set while the hub was offline is not lost.** IoT Hub pushes a desired PATCH only on *change* and
only to a connected device, so the hub also issues a `$iothub/twin/GET` once per connection and applies the
full document. Changed in 2.0.2 — before that a reboot silently reverted to the 300 s default while the twin
went on advertising your value, with nothing to reconcile the two.

**A rejected write looks like** `desired.snapshot_interval_s: 30` alongside `reported.snapshot_interval_s: 300`.
Nothing is surfaced to the cloud beyond that disagreement.

**Tags are never read by the hub.** `tags.DeviceModel` and anything else there is cloud-side only.

## Traps and sharp edges

These are behaviours that will cost you time if you meet them in the field rather than here. Each was
confirmed by reading the firmware, not inferred from documentation.

**Trap 1 — a wrong `schema` can still execute the command, and you get no acknowledgement.** If `schema` is
anything other than `eflostop.cmd` or `eflostop.cmd.v1`, the envelope parser gives up and the raw text is then
scanned for legacy keywords — case-insensitively, anywhere in the message. So `{"schema":"eflostop.cmd.v2",
"cmd":"valve_open","id":"abc"}` **opens the valve**, and because it took the legacy path it sends no ack at all.
The cloud sees a timeout and cannot tell "unsupported schema" from "dead hub", while the physical action has
already happened. This affects `valve_open`, `valve_close`, `leak_reset`, `override_cancel` and the
`decommission` family. It does NOT affect `valve_set_state`, `override_enable`, `provision`, `sensor_meta`,
`rules_config` or `set_hub_name`, which have no legacy keyword. Always send the exact schema string.

**Trap 2 — the same substring scan reads your whole message.** Because the legacy matcher searches the entire
uppercased text, any command that fails envelope parsing and happens to contain `VALVE_OPEN`, `LEAK_RESET`,
`DECOMMISSION_VALVE`, `RULES_CONFIG:`, `SENSOR_META:` or `OVERRIDE_CANCEL` anywhere — including inside a sensor
label — is routed to that command. Avoid putting command names in free-text fields.

**Trap 3 — a provision carrying only `sensor_meta` is rejected.** The at-least-one-field requirement is
satisfied only by `valve_id` (or its deprecated alias `valve_mac`), `lora_sensors`, `ble_leak_sensors`,
`auto_close_enabled` or `rules`. A payload whose sole key is `sensor_meta` answers `provisioning failed` and
the metadata is never written. Use the standalone `sensor_meta` command to set metadata on its own. The
inverse also bites: `auto_close_enabled` DOES satisfy the requirement, so a payload whose sole key is that
flag marks an unprovisioned hub as commissioned with no devices. Send it with the devices it describes.

**Trap 4 — LoRa ids without the `0x` prefix are dropped silently.** In a `provision` payload, `["1A2B3C4D"]` is
skipped element by element while the command still acks `ok` with zero sensors stored. Send `["0x1A2B3C4D"]`.
Inconsistently, `decommission` accepts the prefix as optional.

**Trap 5 — FIXED in 2.0.0, but check which firmware you are talking to.** Up to 1.9.0 the decommission targets
were inconsistently case-sensitive: `ble`, `BLE` and `Ble_Leak_Sensor` all worked, but `ALL`, `Valve` and
`LORA` were rejected with `unknown decommission target` — the destructive `all` target being one of the strict
ones, so a case mismatch silently no-opped. From 2.0.0 every target is matched case-insensitively. Sending
lower case remains the safe habit, and is the only thing that works on both.

**Trap 6 — a misspelt `location_code` keeps the old location and still acks `ok`.** This is deliberate, so a
typo cannot wipe a stored location, but it means you cannot tell from the ack whether the value was applied.
Send `unknown` if you intend to clear it.

**Trap 7 — `set_hub_name` acks `ok` even when the write fails.** The handler ignores the storage result. The
only failures that surface are the two argument checks. Confirm through the twin reported `hub_name` if it
matters.

**Trap 8 — message size: fixed in 2.1.0, but know the ceiling.** Up to 2.0.2 the receive buffer was 1024
bytes and the handler did not reassemble fragments, so anything larger arrived truncated, failed to parse and
was dropped **with no ack** — indistinguishable, from your side, from a message that never arrived. Because
the whole 1024 covered the topic as well as the payload, and inline `sensor_meta` costs roughly 135 bytes per
sensor, a `provision` broke at about the **sixth** sensor.

Since 2.1.0 the buffer is 4096 and oversized messages are reassembled from their fragments, so a `provision`
carrying all 16 sensors with full-length labels arrives intact. The remaining limit is **8192 bytes**, above
which the message is rejected — and rejection is now reported: you get a `cmd_ack` with
`status: "error"` and a `payload too large` detail, correlated against your `id`, which is recovered from the
first fragment. Splitting a large `provision` into a `provision` followed by individual `sensor_meta` calls is
still the lightest option, and remains the only one that works against a hub running 2.0.2 or earlier.

**Trap 9 — `cmd` and `id` are truncated without warning**, at 31 and 63 characters. **GUIDs are unaffected**
— a canonical 36-character GUID, or 38 with braces, fits with room to spare, and the ack echoes whatever was
stored without truncating it again. The longest command name the firmware defines is 15 characters. This only
bites a correlation scheme that exceeds 63 characters, where the truncated `id` comes back unmatched and the
request looks unanswered.

**Trap 10 — one error string can mean several things.** `A leak is still active…` also covers an uninitialised
rules engine and an internal lock timeout, neither of which involves a leak. Treat these strings as
user-facing text, not as machine-readable causes.

**Trap 11 — a provision whose only valve key is JSON `null` is fine; one whose value is a malformed string is
not.** `{"valve_id": null, "ble_leak_sensors":[…]}` means "no valve in this payload" and commissions the
sensors normally. `{"valve_id": "00:80:E1"}` fails the WHOLE command, and the sensors in the same message are
discarded with it. If you serialise optional fields, emit `null` or omit the key — never an empty or partial
string.

**Trap 12 — the device twin keeps deleted properties unless they are explicitly nulled.** A reported PATCH is
a merge, so a property the firmware stops writing freezes at its last value rather than disappearing. The
valve identity has been renamed twice, so 2.0.0 sends `"valve_mac": null` and `"valve_device_id": null` to
delete the old spellings. Read `reported.valve_id`; do not write a `?? fallback` chain across the three
names, or you will keep reading a stale value forever.

**Trap 13 — re-sending `auto_close_enabled: true` on an incremental provision silently re-arms every trigger.**
The key is the setup-flow opt-in, and `true` always sets `trigger_mask` back to 7. A later `provision` that
merely adds a sensor and repeats the flag out of habit will undo any per-source narrowing the user made
through `rules_config` in between, with no warning and an `ok` ack. Send the key only on the commissioning
message where the user actually answered the question; omit it (or send JSON `null`) on every other
provision.

## Legacy plain-text commands

Before the JSON envelope the hub accepted bare text, and it still does. These forms are recognised
case-insensitively as substrings of the whole message:

`VALVE_OPEN`, `VALVE_CLOSE`, `LEAK_RESET`, `OVERRIDE_CANCEL`, `DECOMMISSION_VALVE`, `DECOMMISSION_ALL`,
`DECOMMISSION` (whole message), `DECOMMISSION_LORA:<hex>`, `DECOMMISSION_BLE:<mac>`, `RULES_CONFIG:{json}` and
`SENSOR_META:{json}`.

**None of them produce an acknowledgement**, because the ack requires either an envelope or a non-empty `id`.
`DECOMMISSION_LORA` or `DECOMMISSION_BLE` without the colon fails to parse and runs nothing.

There is no legacy form for `valve_set_state`, `override_enable`, `provision` or `set_hub_name`.

Separately, a bare JSON object with no `cmd` key is treated as a `provision` payload. That is how the very first
commissioning message can be sent without an envelope. If the object does contain a string `cmd` but the schema
is unrecognised, it is dropped silently instead.

