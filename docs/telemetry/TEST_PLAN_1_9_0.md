# FW 1.9.0 — Test, Verification & Validation Plan

**Change under test:** valve flood events consolidated into the `leak_detected` / `leak_cleared`
family; `sensor_id` → `device_id` on every outbound message; one device vocabulary
(`valve` / `ble_leak_sensor` / `lora`); snapshot settle barrier so a snapshot never reports
pre-transition valve state.

**Scope of proof.** Two things must be shown: (1) the wire format is what the specification says,
for every source, and (2) the snapshot that follows a valve transition reports the post-transition
state. Everything else is regression cover.

---

## 0. Setup

| | |
|---|---|
| Build | `idf.py build` in the ESP-IDF v5.5.1 environment |
| Flash + monitor | `idf.py -p COMx flash monitor` — 115200 baud |
| Hub identity | note the `GW-xxxxxxxxxxxx` printed at boot; you need it for the cloud monitor |
| Cloud | `az iot hub monitor-events -n <hub-name> -d <GW-id> --props all -t 0` |
| Devices | 1 valve (FW 2.2.0+), 2 BLE leak sensors, both provisioned with location metadata |

Run the UART monitor and the cloud monitor **side by side** and timestamp both. Several checks are
about *ordering between the two*, which a single log cannot show.

### UART lines that carry the evidence

| Grep | What it proves | Source |
|---|---|---|
| `Pub event:` | the exact D2C JSON, verbatim | `main/telemetry/telemetry_v2.c:110` |
| `Pub snapshot:` | the exact snapshot JSON | same |
| `[DATA] Leak=` | the valve probe transition was decoded | `main/ble_valve/app_ble_valve.c` `on_notify` |
| `[CMD] Writing valve command=` / `Valve write rc=` | a hub-issued write reached GATT | `write_valve_command` |
| `[CMD] Writing RMLEAK=` / `RMLEAK write rc=` | the RMLEAK write landed | `write_rmleak_command` |
| `SNAP trigger=event:` | which event pulled the snapshot in | `main/iothub/app_iothub.c` flush block |
| `SNAP deferred — valve command settling` | **the new barrier actually fired** | flush block |
| `Offline — buffering event` | the offline path was taken | `telemetry_v2.c:116` |

`SNAP deferred` is `ESP_LOGD`. Set the log level to DEBUG for the `IOTHUB` tag (`idf.py menuconfig`
→ Log output → Default log verbosity → Debug, or `esp_log_level_set("IOTHUB", ESP_LOG_DEBUG)`)
or T3 cannot be positively confirmed — only inferred.

---

## 1. Wire-format tests

### T1 — Valve probe wet

**Do:** bridge the valve's flood probe contacts (or wet it).

**Expect** one `Pub event:` with:

```json
{"event":"leak_detected","source_type":"valve","device_id":"<valve MAC>",
 "leak_state":true,"battery":<0-100>,"location":{"code":"unknown","label":""},
 "valve_state":"open","rmleak":false,"fw_version":"2.2.0"}
```

**Pass:**
- `event` is `leak_detected` — **not** `valve_flood_detected`
- `source_type` is `valve` — **not** `valve_flood`
- `device_id` is the valve's MAC in `AA:BB:CC:DD:EE:FF` form, and equals `data.valve.device_id`
  in the following snapshot
- `leak_state` is `true`
- `location` is **present** with `code:"unknown"`, `label:""`
- `rssi` is **absent**
- no `sensor_id` key anywhere in the message

### T2 — Valve probe dry

**Do:** dry the probe.

**Pass:** `event` is `leak_cleared`, `leak_state` is `false`. Same key set as T1.

### T3 — Sensor wet / dry

**Do:** wet, then dry, a BLE leak sensor.

**Pass:**
- `event` `leak_detected` then `leak_cleared`; `source_type` `ble_leak_sensor`
- `device_id` is the sensor MAC
- `rssi` **present**, negative
- `valve_state`, `rmleak`, `fw_version` **absent**
- `location` carries the provisioned code and label

### T4 — Uniform core

**Do:** take the T1 and T3 payloads and compare `data` key-by-key.

**Pass:** the first six keys are `event`, `source_type`, `device_id`, `leak_state`, `battery`,
`location` — same names, **same order**, same JSON types, in both. This is the property the whole
change exists to deliver; if it fails, nothing else matters.

### T5 — Sensor with no location

**Do:** wet a sensor that has no `sensor_meta` entry.

**Pass:** `location` present, `{"code":"unknown","label":""}` — key never omitted, never `null`.

### T6 — `valve_state_changed`

**Do:** press the valve's button to change position.

**Pass:** `event` is still `valve_state_changed` (it is not a leak). It now also carries
`source_type:"valve"` and `device_id`.

### T7 — LoRa

**Not testable** — the LoRa radio is DNP on the current PCBA. The code path is exercised only by
inspection. Record as untested rather than passed.

---

## 2. The snapshot-staleness fix

### T8 — Auto-close, the reported bug  **(primary)**

**Preconditions:** auto-close ON, valve trigger bit set in the trigger mask, no override window
open, valve connected and open.

**Do:** wet the valve probe.

**Expect this order.** Capture both logs and line them up:

```
UART   [DATA] Leak=1 (LEAK)
cloud  event  leak_detected      source_type=valve  valve_state=open
cloud  event  auto_close         source_type=valve
UART   [CMD] Writing valve command=0
UART   [CMD] Valve write rc=0
UART   SNAP trigger=event:leak_detected
cloud  snapshot   data.valve.state = "closed"
```

**Pass:**
- the snapshot's `data.valve.state` is **`closed`**
- `Valve write rc=0` appears **before** `SNAP trigger=`
- at least one `SNAP deferred — valve command settling` line appears between the event and the
  snapshot (requires DEBUG logging — see §0)

**Fail:** a snapshot with `data.valve.state:"open"` published after the `auto_close`. That is the
original defect.

**Note on the event itself:** `leak_detected` legitimately reports `valve_state:"open"` — at the
instant water was detected the valve *was* open. The fix is about the **snapshot**, which is what
the UI renders.

### T9 — C2D close

**Do:** send C2D `valve_close`.

**Pass:** `cmd_ack` first, then a snapshot reporting `"closed"`. Never a snapshot reporting `"open"`
after the ack. This path arms the barrier from the esp-mqtt task rather than iothub_task, so it is
a distinct case from T8, not a duplicate.

### T10 — Override paths

**Do:** (a) with an incident active, send `override_enable`; (b) then `override_cancel`.

**Pass:** in both cases the snapshot following the rules event reports the post-command valve state.
(a) should end `open`, (b) should end `closed` if the leak is still active.

### T11 — Barrier timeout, valve unreachable

**Do:** power the valve down, then send C2D `valve_close`.

**Pass:** the snapshot still publishes, within roughly 1.5 s of when it was due. The barrier must
degrade to *late*, never to *blocked*. Watch for a run of `SNAP deferred` lines that never ends —
that would be a starvation bug.

### T12 — RMLEAK wake

**Do:** trigger an auto-close and watch for `[CMD] Writing RMLEAK=1` / `RMLEAK write rc=0`.

**Pass:** a snapshot follows reporting `data.valve.rmleak: true` without waiting for an unrelated
event to trigger it. Before this change that write posted no notification at all.

---

## 3. Regression

### T13 — Offline buffering and replay  **(size risk)**

**Do:** disconnect Wi-Fi. Wet the valve probe, then a sensor. Reconnect.

**Pass:** both events replay and are **valid JSON**.

**Why this needed checking:** `offline_buffer_store()` truncates at `OFFLINE_BUF_MAX_JSON_LEN` = 512
(`main/offline_buffer/offline_buffer.h:13`) and the drain republishes the stored bytes verbatim — a
truncated payload reaches the cloud as malformed JSON. The valve leak event is **larger** than the
`valve_flood_detected` event it replaces: it gains `source_type`, `device_id` and `location`.

**Computed worst case** — every string at its buffer maximum (31-char `gateway.name`, 31-char valve
`fw_version`, longest `location.code` with a 31-char `label`), compact separators as
`cJSON_PrintUnformatted` emits them:

| Message | Bytes | Margin to 512 |
|---|---:|---:|
| 1.8.0 `valve_flood_detected` (the old shape, for reference) | 335 | +177 |
| **1.9.0 valve `leak_detected`** | **423** | **+89** |
| 1.9.0 sensor `leak_detected` | 395 | +117 |
| 1.9.0 `valve_state_changed` | 388 | +124 |
| 1.9.0 `auto_close` | 371 | +141 |
| 1.9.0 `device_offline` | 397 | +115 |

The valve leak event grew by **88 bytes** and is the largest buffered message, but stays 89 bytes
inside the ceiling even at absolute worst case. **No change to `OFFLINE_BUF_MAX_JSON_LEN` is
required.** Typical messages run 250–320 bytes.

Still confirm empirically: after replay, check the received JSON parses. The arithmetic assumes no
future key is added to these events without re-checking.

### T14 — Snapshot cadence

**Do:** leave the hub idle and connected for 15 minutes with no leaks.

**Pass:** heartbeat snapshots every 300 s (or the twin-configured interval), unchanged. No
`SNAP deferred` lines while idle. The extra `BLE_UPD_RMLEAK` wake must not add traffic.

### T15 — Boot and commissioning

**Do:** cold boot a provisioned hub. Separately, run a fresh `provision`.

**Pass:** the fast/boot snapshot still fires at valve-ready (~20–30 s); the commission snapshot and
incremental refresh behave as before. The barrier must not delay or suppress these.

### T16 — Rapid toggle

**Do:** wet and dry the valve probe within about a second, several times.

**Pass:** **no message ever pairs `event:"leak_detected"` with `leak_state:false`**, or
`leak_cleared` with `leak_state:true`. This was possible before the change because the serializer
re-read the live probe after the event name had been chosen.

### T17b — `device_id` is uppercase everywhere  *(added after the first bench capture)*

**Do:** commission a BLE sensor with a **lower-case** MAC, then capture one full cycle — a snapshot, a
`leak_detected`, an `auto_close` and a `device_offline` for that sensor.

**Pass:** the sensor's `device_id` is upper case on **all four**. Extract every `device_id` value from
the capture, upper-case them, and confirm the set of distinct raw values equals the set of distinct
upper-cased values — i.e. no device appears under two spellings.

**Why:** in the 2026-08-03 capture the same sensor appeared as `00:80:e1:2a:3b:00` in 26 snapshot
array elements and `00:80:E1:2A:3B:00` in 11 events. Events render the MAC from the radio (`%02X`);
snapshot arrays echoed the provisioned string verbatim. `provisioning_get_valve_mac()` and
`provisioning_get_ble_leak_sensors()` now normalise on read, which also repairs hubs already
commissioned in lower case without an NVS migration.

### T17a — Valve identity is single-valued  *(added after review)*

**Do:** trigger a valve-probe auto-close and capture the whole burst.

**Pass:** `leak_detected`, `auto_close`, the snapshot's `data.valve.device_id`, `valve_state_changed`
and any valve `device_offline` **all carry the same MAC**. The literal string `"valve"` must not
appear as a `device_id` value on any message. The rules engine still tracks the valve internally
under that pseudo-id — that is deliberate and must not leak onto the wire.

### T17 — Vocabulary consistency

**Do:** in one session, capture a valve-triggered `auto_close`, a sensor-triggered `auto_close`, and
a valve `device_offline` (power the valve down for >10 min).

**Pass:** every one reports the device as `valve`, `ble_leak_sensor` or `lora` — `valve_flood` must
not appear anywhere. Health events use `device_id`.

### T18 — Full-surface sweep for stale keys

**Do:** run for one heartbeat cycle with all message types exercised, capture everything.

**Pass:** `grep -c 'sensor_id' captured.json` returns **0** for outbound messages, and
`grep -c 'valve_flood' captured.json` returns **0**. Inbound C2D messages still use `sensor_id`
deliberately — exclude them from the grep.

---

## 4. Schema validation

Capture real payloads and validate rather than eyeballing:

```bash
az iot hub monitor-events -n <hub> -d <GW-id> --props all -t 0 -o json > captured.json
```

Then check, per message, that:
- the required core is present on every `leak_detected` / `leak_cleared`
- `source_type` ∈ {`valve`, `ble_leak_sensor`, `lora`}
- `rssi` appears **iff** `source_type != "valve"`
- `valve_state`, `rmleak` appear **iff** `source_type == "valve"`
- no outbound message contains `sensor_id`

`jsonschema` is already installed. The schemas under `docs/telemetry/schemas/` describe **1.8.0**
and have not been regenerated — see the open items below — so write the assertions directly rather
than validating against them.

---

## 5. Sign-off

| Test | Result | Notes |
|---|---|---|
| T1 valve wet | | |
| T2 valve dry | | |
| T3 sensor wet/dry | | |
| T4 uniform core | | |
| T5 no-location fallback | | |
| T6 valve_state_changed | | |
| T7 LoRa | N/A | radio DNP on this PCBA |
| **T8 auto-close snapshot** | | **primary** |
| T9 C2D close | | |
| T10 override paths | | |
| T11 barrier timeout | | |
| T12 RMLEAK wake | | |
| T13 offline replay | | worst case 423 B vs 512 B ceiling — computed safe, confirm empirically |
| T14 cadence | | |
| T15 boot/commission | | |
| T16 rapid toggle | | |
| T17b device_id uppercase | | re-test after the normalisation fix |
| T17 vocabulary | | |
| T18 stale-key sweep | | |

**Ship gate:** T4, T8 and T13 must pass. T4 is the contract Watts Digital builds against, T8 is the
defect this release fixes, T13 is the one risk this release introduces (computed safe, but the
margin is the smallest of any message type).

---

## 6. Known open items

- **`docs/telemetry/schemas/*.schema.json`, `field_registry.csv`, `telemetry_catalogue.md` and the
  catalogue `.docx` still describe 1.8.0.** They were deliberately left alone: that document is an
  as-is analysis produced to drive this consolidation decision, and retrofitting the new field names
  into prose that argues about the old inconsistencies would make it self-contradictory. The 1.9.0
  wire spec is `telemetry_messages.md` / `eFloStop2_Telemetry_Messages_v1.0.docx`.
- **`docs/telemetry/TELEMETRY_REFERENCE.md` is untracked and still describes 1.8.0**, including two
  lines that assert a `leak_state` guarantee the old firmware did not actually provide.
- **Valve location is not provisionable.** `sensor_meta` has no valve entry type, so a valve leak
  event always reports `location:{"code":"unknown","label":""}`. The key is present for shape
  uniformity.
- **Inbound C2D still uses `sensor_id`**, deliberately — the app and production tool send it today.
  Outbound is `device_id`. Worth aligning in a later pass.
