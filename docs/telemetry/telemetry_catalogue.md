# eFloStop II Wi-Fi Hub — Azure IoT Hub Telemetry Catalogue

*Complete telemetry surface and consolidation review*

> GENERATED FILE — do not edit by hand. Produced by `docs/telemetry/build_catalogue.py`
> from `fields.json` and the `catalogue_*.py` modules. Edit those and re-run the build.

| | |
|---|---|
| Document version | 1.0 |
| Date | 2026-07-31 |
| Git commit | `b62b25e1106387238f8c0b9c06b7db8eda5aacce` (branch `docs/telemetry-catalogue`) |
| Working tree | Working tree has uncommitted changes at build time; this document describes the working tree, not the committed HEAD. |
| Firmware version | 1.8.0 — `CMakeLists.txt:12` |
| Telemetry schema | `eflostop.v2` — `main/telemetry/telemetry_v2.c:64; constant main/telemetry/telemetry_v2.h:14` |

## 1. Executive summary — cloud interface census

| Plane | Mechanism | Named things | Direction | Section |
|---|---|---|---|---|
| Telemetry plane | D2C messages on devices/<device_id>/messages/events/ | 91 | Device → cloud | §10, §11, §12 |
| Twin plane (reported) | $iothub/twin/PATCH/properties/reported/ | 12 | Device → cloud | §13.1 |
| Twin plane (desired) | $iothub/twin/PATCH/properties/desired/ | 2 | Cloud → device | §13.2 |
| Command plane | devices/<device_id>/messages/devicebound/ | 11 | Cloud → device | §14 |
| Provisioning plane | $dps/registrations/ (a different broker) | 1 | Device → cloud, pre-hub | §15 |

116 named things across four planes (91 + 12 + 2 + 11), plus one provisioning payload key. Eleven leaf names collide across planes — see the Plane Collision Index in §4.8.

## 2. How many fields must your ingest really handle?

A backend engineer's first question is "how many distinct fields must my ingest actually handle?"
The answer is not simply 91. The 91 breaks down as follows. Paths belonging to the previous wire shape are NOT
part of the 91 — they exist only inside three deprecated serializers that have no caller and can never reach
the wire, so they are excluded by construction. They are enumerated in full in §19.1 so that exclusion can be
checked rather than taken on trust.

| Class | Count | What it means for you |
|---|---|---|
| Live and variable | 71 | Values that change in normal operation. These are what your ingest must genuinely handle. |
| Constant on the wire | 4 | Emitted on every applicable message but the value never varies: data.category (always "health"), data.rmleak_asserted (always true), data.remaining_s (a compile-time constant), and data.provisioned (false is unreachable because the publish path is gated on being provisioned). |
| LoRa, hardware not populated | 12 | The 12 data.lora_sensors[] paths. The key data.lora_sensors is emitted unconditionally, so you WILL see it on every snapshot — as an empty array, because the LoRa radio is do-not-populate on the current PCBA. |
| Fallback-only | 1 | data.raw, emitted only when an internally generated payload could not be re-parsed. |
| Diagnostic containers | 3 | The three container objects whose only role is to hold others: data.error, data.rules, data.location on the auto_close path — each present only under its own condition. |

## 2.1 Ground rules

**Ground rules — constraints that apply to every option in this document.**

There is no over-the-air update client in this firmware image. A repo-wide search for `esp_https_ota`,
`esp_ota_begin`, `esp_ota_write` and `esp_ota_set_boot_partition` across `main/` returns nothing, even though
the partition table reserves `ota_0`, `ota_1` and `otadata` (`partitions.csv:4-8`). The consequence chain is:
no OTA client in the image → a firmware change reaches only units flashed in production, plus any unit
physically reflashed → **any change to the wire format leaves already-fielded hubs emitting the old format
indefinitely**.

This makes cloud-side handling the only option available for the existing fleet, and it is why every option
set in §7 opens with "no change; the cloud absorbs it". How much this matters depends on how many units are
fielded — a number this repository cannot tell us, and the first question in §9.3.

## 3.2 The four planes

| Plane | Direction | Topic | Envelope | Note |
|---|---|---|---|---|
| D2C telemetry | Device → cloud | `devices/<device_id>/messages/events/` | eflostop.v2 (schema, ts, gateway, type, data) | The subject of this document. Three envelope `type` values carry seven payload families. |
| Device twin — reported | Device → cloud | `$iothub/twin/PATCH/properties/reported/?$rid=N` | None — 12 flat keys, no envelope, and NOT suppressed before clock sync | A durable state document, not a message stream. 11 of its 12 keys also exist on the D2C plane. |
| Device twin — desired | Cloud → device | `$iothub/twin/PATCH/properties/desired/#  (subscribe)` | None — 2 flat keys | The hub never publishes $iothub/twin/GET, so a desired change written while the hub is offline is never fetched (verified: no such string in main/). |
| C2D commands | Cloud → device | `devices/<device_id>/messages/devicebound/#  (subscribe)` | eflostop.cmd envelope, or two legacy forms | 11 commands. There is no direct-method subscription anywhere, so the only acknowledgement channel is a D2C telemetry event (cmd_ack). |
| DPS provisioning | Device → cloud, pre-hub | `$dps/registrations/PUT/iotdps-register/?$rid=1 (register) and $dps/registrations/GET/iotdps-get-operationstatus/?$rid=N&operationId=<operation id> (poll)` | None — a single registrationId key and nothing else | A different broker and namespace, used once to discover which IoT Hub to talk to. Included so the publish-site count is complete. |

## 3.4 Publish call sites

| Call site | Plane | Topic | QoS | Retain | Trigger |
|---|---|---|---|---|---|
| `main/dps_client/dps_client.c:288` | DPS | `$dps/registrations/PUT/iotdps-register/?$rid=1` | 1 | 0 | MQTT_EVENT_SUBSCRIBED on the DPS client while state == SUBSCRIBING (:272-275). Live registration only — a cache hit skips it entirely. |
| `main/dps_client/dps_client.c:312` | DPS | `$dps/registrations/GET/iotdps-get-operationstatus/?$rid=N&operationId=<operation id> (:308-311)` | 1 | 0 | Registration response parsed as still-pending. NULL payload; 3 s inter-poll delay (:304). |
| `main/iothub/app_iothub.c:994` | Twin reported | `$iothub/twin/PATCH/properties/reported/?$rid=N (:990-991)` | 1 | 0 | Three callers only: set_hub_name success (:922), end of every parseable desired patch (:1045), and the provisioned MQTT (re)connect block (:1695). |
| `main/telemetry/telemetry_v2.c:111` | D2C telemetry | `devices/<device_id>/messages/events/ (:254-255)` | 1 | 0 | The only live D2C site. Fed by seven publisher wrappers. Online branch only — offline, events are buffered and other types dropped (:115-122). |
| `main/offline_buffer/offline_buffer.c:148` | D2C telemetry (replay) | `same topic, passed in from main/telemetry/telemetry_v2.c:734` | 1 | 0 | Once per MQTT connect, before lifecycle (main/iothub/app_iothub.c:1693). Replays stored bytes verbatim, so ts and gateway.uptime_s are the ORIGINAL values, possibly from a previous boot. |

## 4. Current-state consistency audit

### 4.1 Naming conventions

**Severity:** No action proposed · **Fixable where:** n/a · **Meeting time:** 0 minutes — recorded so it is not reopened

This is a null finding, stated explicitly so the meeting does not spend time on it. Naming is
already uniform. Every key on every live wire — all 61 distinct D2C leaf names, all 12 twin-reported keys, both
twin-desired keys, all 11 C2D command names and all 21 C2D payload keys — is lowercase with underscore as the
only separator. There is no camelCase, no PascalCase, no kebab-case and no uppercase character in any key
anywhere.

What does vary is not casing but *vocabulary*: the same concept is spelled differently depending on where it
appears, and several abbreviations are not self-evident to a reader who has not seen the firmware.

| Variant | Where it occurs | Citation |
|---|---|---|
| Same concept, different key name: firmware version | gateway.fw (envelope, hub's own) · data.fw_version (valve events) · data.valve.fw_version (snapshot) · data.ble_leak_sensors[].fw_version (snapshot) · fw_version (twin reported) | main/telemetry/telemetry_v2.c:84, :625, :438, :557; main/iothub/app_iothub.c:953 |
| Same concept, flat vs nested: valve MAC | data.valve_mac (lifecycle, flat) · data.valve.mac (snapshot, nested) · valve_mac (twin reported, flat) | main/telemetry/telemetry_v2.c:346, :422; main/iothub/app_iothub.c:961 |
| Same concept, flat vs nested: valve position | data.valve_state (valve events) · data.valve.state (snapshot). Also differ in member set — see §4.5 | main/telemetry/telemetry_v2.c:617, :429 |
| Duration naming: five different shapes | remaining_s · override_remaining_s · previous_remaining_s · last_seen_age_s · offline_duration_s · clear_after_seconds (the only _seconds spelling) | main/rules_engine/rules_engine.c:218, :473, :721, :1083; main/telemetry/telemetry_v2.c:451; main/health_engine/health_engine.c:602 |
| Identity naming across planes | gateway.id (D2C, nested in gateway) · gateway_id (twin, flat) · short_id on both | main/telemetry/telemetry_v2.c:79; main/iothub/app_iothub.c:954 |

### 4.2 Identity representation

**Severity:** Costs backend engineering · **Fixable where:** Cloud-ingest, or both · **Meeting time:** 10 minutes

Twenty-four distinct identity renderings exist across the four planes. Most are benign. Two
facts matter for anyone writing a cloud-side join.

First, a BLE leak sensor's identifier reaches the cloud through two different renderers depending on the
message type: in a snapshot it is the string stored at provisioning time, and in a leak event it is rendered
fresh from the radio address. Both are uppercase colon-separated MACs by convention, but the firmware compares
them case-insensitively (`strcasecmp`), so uppercase is a convention the firmware does not enforce on itself.
A cloud-side join by exact string equality is therefore relying on something the device does not guarantee.

Second, the valve is identified inconsistently: a snapshot carries `data.valve.mac`, a lifecycle message
carries `data.valve_mac`, and a valve **event** carries no valve identifier at all — the event is implicitly
about "the one valve this hub owns".

| Variant | Where it occurs | Citation |
|---|---|---|
| Hub | GW- + 12 uppercase hex, derived from the eFuse MAC. Also the DPS registrationId and the IoT Hub deviceId. | main/hub_identity/hub_identity.c:31-37 |
| Hub short id | Last 4 hex characters of the gateway id — the last two MAC bytes. Also used in the setup AP SSID. | main/hub_identity/hub_identity.c:31-37 |
| Valve, snapshot | data.valve.mac — colon-separated uppercase, from the live BLE link or the health record | main/telemetry/telemetry_v2.c:420-425 |
| Valve, lifecycle | data.valve_mac — the provisioned string | main/telemetry/telemetry_v2.c:344-346 |
| Valve, events | No identifier is emitted at all | main/telemetry/telemetry_v2.c:608-629 |
| BLE sensor, snapshot | data.ble_leak_sensors[].sensor_id — the provisioned string | main/telemetry/telemetry_v2.c:525 |
| BLE sensor, event | data.sensor_id — rendered fresh from the radio address with the format %02X:%02X:%02X:%02X:%02X:%02X (uppercase, colon-separated) | main/ble_leak_scanner/app_ble_leak.c:88; passed main/iothub/app_iothub.c:1809 |
| BLE sensor, C2D | sensor_id copied verbatim into mac_clean for the decommission payload — the copy loop stops at the first whitespace character, so colons are PRESERVED and the rendering is unchanged | main/commands/c2d_commands.c:186-190 (copy loop), :192-193 (payload) |
| LoRa sensor | 0x + 8 uppercase hex from a uint32, on both D2C and C2D | main/health_engine/health_engine.c:241; main/commands/c2d_commands.c:173 |
| Pseudo-identifiers in data.sensor_id | The literal "valve" for the valve's own flood probe, and "unknown" for an unresolved source | main/iothub/app_iothub.c:1650; main/rules_engine/rules_engine.c:335, :471 |

### 4.3 Time representation

**Severity:** Breaks ingest correctness · **Fixable where:** Nowhere for fielded units · **Meeting time:** 15 minutes

One unit is used everywhere: seconds. There are no milliseconds, no ISO-8601 strings and no
timezone offsets anywhere in the contract. Twelve time-valued fields exist, split between two absolute-epoch
fields and eight durations or ages, fed by three different clocks.

Two behaviours materially affect a cloud consumer.

**Telemetry produced before the clock syncs is destroyed, not delayed.** `build_envelope()` reads the wall
clock and, if it is below 1704067200 (2024-01-01), deletes the object it was building and returns NULL. Because
`publish_json()` returns immediately on a NULL root, the message never reaches the branch that would have
buffered it for replay. This applies to every message type including `leak_detected` and including `cmd_ack`.
The twin plane is not affected — it has no such gate — so a hub can be patching its twin while its telemetry
stream is being silently discarded.

**Replayed events carry their original timestamp.** The offline buffer republishes stored bytes verbatim, so a
replayed event's `ts` and `gateway.uptime_s` are the values from when it was created — possibly from a previous
boot. Message arrival order at the cloud therefore does not imply `ts` order, and a consumer that dedupes or
orders on enqueue time will get it wrong.

```c
// build_envelope() — main/telemetry/telemetry_v2.c:66-76
time_t now;
time(&now);

/* Suppress telemetry if SNTP has not synced yet */
if (now < EPOCH_VALID_THRESHOLD_TELEM) {          // 1704067200, :57
    ESP_LOGW(TELEM_TAG, "Time not synced (ts=%ld) - suppressing %s", (long)now, type);
    cJSON_Delete(root);
    return NULL;                                   // <- message destroyed here
}
cJSON_AddNumberToObject(root, "ts", (double)now);

// publish_json() — main/telemetry/telemetry_v2.c:99-122
if (!root) return false;                           // <- returns BEFORE the buffer branch below

char *json_str = cJSON_PrintUnformatted(root);     // :103
cJSON_Delete(root);
if (!json_str) return false;

bool sent = false;
if (s_mqtt && s_connected) {                       // :108  online branch
    int msg_id = esp_mqtt_client_publish(s_mqtt, s_topic, json_str, 0, 1, 0);
    sent = (msg_id >= 0);
} else if (strcmp(type_hint, "event") == 0) {      // :115  offline branch
    offline_buffer_store(json_str, strlen(json_str));   // never reached for a NULL root
} else {
    ESP_LOGD(TELEM_TAG, "Offline - dropping %s (regenerated)", type_hint);   // :121
}
```

| Variant | Where it occurs | Citation |
|---|---|---|
| Absolute epoch, seconds, UTC | ts (every message) and data.expires_ts (override window) | main/telemetry/telemetry_v2.c:76, :599 |
| Monotonic uptime, seconds | gateway.uptime_s — esp_timer microseconds divided by 1e6; unaffected by clock sync; resets to 0 on reboot | main/telemetry/telemetry_v2.c:85-86 |
| Age-since, seconds, with a null sentinel | last_seen_age_s on the valve and both sensor arrays; UINT32_MAX becomes JSON null | main/telemetry/telemetry_v2.c:450-455, :473-478, :530-535 |
| Countdown durations, seconds | override_remaining_s, previous_remaining_s, remaining_s, clear_after_seconds | main/rules_engine/rules_engine.c:473, :721, :218, :1083 |
| Elapsed duration, seconds, omitted when zero | offline_duration_s on health events | main/health_engine/health_engine.c:601-603 |
| No timestamp of their own | Every message relies solely on the envelope ts. No payload carries a second, event-specific time, so for a replayed event there is no way to distinguish 'when it happened' from 'when it was built'. | main/telemetry/telemetry_v2.c:59-92 |

### 4.4 Units & scaling

**Severity:** Costs backend engineering · **Fixable where:** Cloud-ingest, or both · **Meeting time:** 10 minutes

Thirty-two numeric keys exist across all planes, 23 of them on the telemetry plane. Six
conventions express units, and the most common one is "no suffix at all": 11 of the 32 numeric keys carry their
unit only in the semantics of the name — `battery`, `rssi`, `snr` and `free_heap` among them.

Exactly one numeric value on any plane is validated: the twin-desired `snapshot_interval_s` is range-checked
against 60..3600, and a value outside that range is discarded with a warning rather than clamped, so the
previous interval stays in force. Nothing else is validated at all, which has two visible consequences. `battery` is documented as
a percentage but no code path clamps it to 0..100, so a peer reporting 255 would be forwarded verbatim. And
`trigger_mask` is stored and echoed as a raw byte with no masking, so its wire range is 0..255 even though only
bits 0-2 are ever interpreted.

One field alone can print a decimal point. LoRa `snr` is quantised to quarter-dB steps, and the JSON serializer
prints a number without a decimal point whenever the value is integral. The same field therefore alternates
between `9` and `9.25` between messages — a consumer with a strict integer-or-float expectation will see both.

| Variant | Where it occurs | Citation |
|---|---|---|
| _s suffix means seconds | 7 D2C paths plus twin uptime_s | main/telemetry/telemetry_v2.c:85, :451; main/iothub/app_iothub.c:979 |
| _seconds suffix, the only one | data.clear_after_seconds | main/rules_engine/rules_engine.c:1083 |
| ts / _ts means Unix epoch seconds | ts, data.expires_ts | main/telemetry/telemetry_v2.c:76, :599 |
| _count suffix means a dimensionless count | data.lora_sensor_count, data.ble_leak_sensor_count, data.active_leak_count | main/telemetry/telemetry_v2.c:351, :356; main/rules_engine/rules_engine.c:939 |
| _mask suffix means a bitmask | data.rules.trigger_mask; bit0=BLE, bit1=LoRa, bit2=valve flood | main/provisioning_manager/provisioning_manager.h:22-25; emitted main/telemetry/telemetry_v2.c:362, :581 |
| No suffix — unit implied by name only (11 of 32 keys) | battery (percent), rssi (dBm), snr (dB), free_heap (bytes) | main/telemetry/telemetry_v2.c:431, :502, :503; main/iothub/app_iothub.c:981 |
| Only validated numeric on any plane | twin desired snapshot_interval_s, range-checked 60..3600; out-of-range values are discarded, not clamped | main/iothub/app_iothub.c:1022 |
| Signed values | rssi is int8_t on three paths. A LoRa RSSI below -128 dBm wraps into the positive range, so a positive LoRa rssi is a wrapped value, not a strong signal. | main/app_lora/app_lora.cpp:274 |

### 4.5 Type encoding & absence semantics

**Severity:** Breaks ingest correctness — safety-relevant · **Fixable where:** Cloud-ingest now; both, long term · **Meeting time:** 25 minutes — the largest single item

Two things in this dimension are already fully consistent and need no discussion. Every one of
the 15 boolean fields is a real JSON `true`/`false` — there are no 0/1 integers and no quoted booleans anywhere.
And every one of the 19 enum-valued fields is a string; there are no integer-coded enums. Full member lists for
all 19 are in §12 and Appendix D.

The contentious part is how the contract says "I don't know". Four different conventions coexist, and one of
them cannot express the concept at all.

Twenty-six fields are **omitted** when unknown. Eight fields emit an explicit **null** — and these are almost
entirely inside the two sensor arrays, which were built to a fixed-shape rule where every key is always present.
Two fields are **three-state**, using value, null *and* absence to mean three different things. And two fields
use a **hard-coded placeholder**: when a sensor has no live cache entry, `leak_state` is written as `false`.

That last case is the single highest-value item in this document. In the same object, three keys say "no data"
by being null while one key makes a positive assertion of "dry". They are describing the same absence. A sensor
that was **wet** and then goes silent flips to `leak_state:false` on the next snapshot, because the cache merge
is gated on the health engine's `connected` flag, which is itself a freshness test with a 600 s timeout — so
once a sensor has been silent for ten minutes its cached leak state is dropped and the field reads dry, while
the `leak_detected` event already published for it is never retracted. A consumer treating snapshots as authoritative will silently clear a live leak.

One detail matters for anyone writing the consuming rule. `last_seen_age_s` is emitted *before and outside*
the cache lookup, so on a cache miss it keeps its real numeric value rather than going null — it is null only
on the health engine's never-seen sentinel. That makes it the one field in the object that still contradicts
`leak_state:false`: a large `last_seen_age_s` alongside `leak_state:false` is the signature of this case.

```c
// The SAME sensor object, rendered two ways. main/telemetry/telemetry_v2.c:521-564
// (a) sensor is connected and has a live cache entry:
{"sensor_id":"AA:BB:CC:DD:EE:FF","connected":true,"rating":"good",
 "last_seen_age_s":12,"battery":87,"leak_state":true,"rssi":-64,
 "fw_version":"1.1.0","location":{"code":"kitchen","label":"Under sink"}}

// (b) same sensor, connected==false so the cache merge at :541-552 is skipped:
{"sensor_id":"AA:BB:CC:DD:EE:FF","connected":false,"rating":"critical",
 "last_seen_age_s":734,"battery":null,"leak_state":false,"rssi":null,
 "fw_version":null,"location":{"code":"kitchen","label":"Under sink"}}
//                  ^^^ still NUMERIC: emitted at :530-535, before the cache lookup
//                            ^^^^ three keys say "unknown" (battery, rssi, fw_version)
//   "leak_state":false says "dry" - byte-identical to a genuine dry reading.
```

| Variant | Where it occurs | Citation |
|---|---|---|
| Class 1 — key omitted when unknown (26 fields) | gateway.name, data.valve_mac, data.rules and its two children, data.valve.mac/battery/leak_state/rmleak/rating, data.fw_version, data.battery and data.rssi on health alerts, data.offline_duration_s, data.location and its children on auto_close, data.override_remaining_s, data.expires_ts, data.override_cancelled, data.id, data.error and its children, data.reason, data.raw | main/telemetry/telemetry_v2.c:82-83, :344-346, :360-364, :420-433, :447-449, :623-625, :705-706, :709-713; main/health_engine/health_engine.c:595-603; main/rules_engine/rules_engine.c:338-349, :472-474, :679-681 |
| Class 2 — explicit null when unknown (8 fields) | Entirely inside the two sensor arrays: last_seen_age_s, battery, rssi, snr (LoRa) and last_seen_age_s, battery, rssi, fw_version (BLE) | main/telemetry/telemetry_v2.c:477, :505, :507, :508, :534, :561, :563, :564 |
| Class 3 — hard-coded placeholder false (2 fields) | data.lora_sensors[].leak_state and data.ble_leak_sensors[].leak_state, written false when the cache merge is skipped. The sibling keys battery, rssi and fw_version go null in the same branch; last_seen_age_s does NOT, because it is emitted before the cache lookup | main/telemetry/telemetry_v2.c:506, :562; merge gate :485-496, :541-552; last_seen_age_s emitted earlier at :473-478, :530-535 |
| Class 4 — three-state: value, null AND absent (2 fields) | data.valve.fw_version (string when read, null when the DIS read failed, absent when the valve is disconnected) and data.valve.last_seen_age_s | main/telemetry/telemetry_v2.c:436-444, :447-455 |
| A fifth pattern a consumer must handle identically — in-band string sentinel | 20 fields can emit the literal "unknown" or an empty string as a value rather than being omitted or nulled, e.g. location.code out-of-range and every enum's default: branch | main/sensor_meta/sensor_meta.c:414-420; main/health_engine/health_engine.c:64 |
| Same value, two different derivations (a firmware inconsistency, see F-02) | LoRa leak: the event path tests leak_status != 0, the snapshot path tests leak_status == 1 | main/iothub/app_iothub.c:1750 versus main/telemetry/telemetry_v2.c:501 |

### 4.6 Structure & envelope

**Severity:** Costs backend engineering · **Fixable where:** Cloud-ingest, or both · **Meeting time:** 15 minutes

The envelope is completely uniform, and that is worth stating plainly because it is the part
most likely to be assumed broken. One function builds it, it contains no type-conditional branch, and all seven
telemetry payload families call it. Lifecycle, snapshot and event messages differ in the value of exactly one
key: `type`.

The variation is entirely below the envelope, and it takes two forms.

**Three representations of "a device".** The valve, a LoRa sensor and a BLE leak sensor are described by three
differently-shaped objects. The valve alone has `mac` rather than `sensor_id`, and alone can carry the state
`"disconnected"`. Only LoRa carries `snr`. Only BLE and the valve carry `fw_version`. The valve is a single
object; the other two are array elements.

**The same sensor is nested in one message and flat in another.** In a snapshot a sensor is an object inside
`data.ble_leak_sensors[]` with its identity under `sensor_id`. In a leak event the same sensor's fields are
hoisted to the top of `data` — `data.sensor_id`, `data.battery`, `data.rssi`, `data.leak_state`. A consumer
therefore needs two different extraction paths for one physical device.

```c
// The SAME BLE sensor, in a snapshot and in an event.
// snapshot - nested inside an array element (main/telemetry/telemetry_v2.c:521-571)
{"type":"snapshot","data":{"ble_leak_sensors":[
    {"sensor_id":"AA:BB:CC:DD:EE:FF","battery":87,"rssi":-64,"leak_state":true,
     "location":{"code":"kitchen","label":"Under sink"}}]}}

// event - the same fields hoisted flat onto data (main/telemetry/telemetry_v2.c:640-651)
{"type":"event","data":{"event":"leak_detected","source_type":"ble_leak_sensor",
    "sensor_id":"AA:BB:CC:DD:EE:FF","leak_state":true,"battery":87,"rssi":-64,
    "location":{"code":"kitchen","label":"Under sink"}}}
```

| Variant | Where it occurs | Citation |
|---|---|---|
| Envelope is 100% uniform | One builder, no type-conditional branches, called by all 7 telemetry families | main/telemetry/telemetry_v2.c:59-92; call sites :336, :374, :610, :637, :659, :679, :700 |
| Valve has mac; sensors have sensor_id | data.valve.mac versus data.<array>[].sensor_id | main/telemetry/telemetry_v2.c:422, :468, :525 |
| Only the valve can be "disconnected" | data.valve.state has 4 members; data.valve_state on events has 3 and never carries disconnected | main/telemetry/telemetry_v2.c:429-430, :442 versus :617-618 |
| Only LoRa carries snr | data.lora_sensors[].snr has no counterpart in the BLE array | main/telemetry/telemetry_v2.c:503 |
| fw_version present on BLE and valve, absent on LoRa | data.ble_leak_sensors[].fw_version, data.valve.fw_version; no LoRa equivalent | main/telemetry/telemetry_v2.c:557, :438 |
| auto_close has two incompatible shapes | Sensor-triggered carries an optional location and no active_leak_count; valve-reconnect carries active_leak_count, no location, and source_type "reconnect" which is not a leak_source_t member | main/rules_engine/rules_engine.c:328-357 versus :932-943; enum main/rules_engine/rules_engine.c:259-267 |
| data.raw is a structural outlier | A string containing an unparsed payload, emitted only when the engine JSON could not be re-parsed | main/telemetry/telemetry_v2.c:669, :688 |

### 4.7 Schema version handling across 1.7.0 and 1.8.0

**Severity:** Breaks ingest correctness during rollout · **Fixable where:** Cloud-ingest (no OTA path) · **Meeting time:** 10 minutes

One schema identifier travels on the telemetry plane: the constant string `eflostop.v2`. It is
emitted on every message and has not changed across the versions examined. It identifies the *envelope shape*,
not the field set — fields have been added and one enum value renamed without it moving, so a consumer cannot
use it to detect capability.

The wire has been additive in practice, with one exception now in flight. In the working tree, the health
event's `dev_type` value for a BLE leak sensor changed from `ble_leak` to `ble_leak_sensor`, deliberately, to
match the spelling used by `source_type` on leak events. The in-source comment states that intent.

This is a breaking string change on the telemetry plane shipping without an OTA path. A backend matching on
`ble_leak` stops matching on 1.8.0 hubs, while every hub still on 1.7.0 keeps sending the old spelling until it
is physically reflashed. Both spellings will exist in stored history. The full 1.7.0 versus 1.8.0 delta is in
§18; this subsection covers only the versioning question itself.

| Variant | Where it occurs | Citation |
|---|---|---|
| Telemetry schema identifier | eflostop.v2 — constant, emitted on every message, identifies the envelope shape only | main/telemetry/telemetry_v2.c:64; main/telemetry/telemetry_v2.h:14 |
| Command schema identifiers | eflostop.cmd (canonical, with a numeric ver) and eflostop.cmd.v1 (legacy) | main/commands/c2d_commands.h:13-17 |
| Hub firmware version on the wire | gateway.fw on every telemetry message and fw_version on the twin, both from the build-time PROJECT_VER | main/telemetry/telemetry_v2.c:84; main/iothub/app_iothub.c:953; CMakeLists.txt:12 |
| No version field distinguishes field-set capability | Fields have been added and an enum value renamed with no change to the schema string, so it cannot be used for capability detection | main/telemetry/telemetry_v2.c:64 |
| The one breaking change in flight | dev_type: ble_leak at HEAD b62b25e, ble_leak_sensor in the working tree | main/health_engine/health_engine.c:77; intent comment :68-71 |

### 4.8 Plane collision index

Eleven of the twelve twin-reported keys carry the same information as a telemetry field. Only `free_heap`
is unique to the twin. Nine of the eleven share the leaf name exactly; two do not - the twin's `gateway_id`
and `hub_name` correspond to telemetry leaves named `id` and `name` inside the `gateway` object, so a
name-based search finds one and not the other. These are *name* collisions, not path collisions — the telemetry field is usually nested
while the twin key is always flat — but a reader searching for a name will find two answers with different
delivery semantics, so each is listed here with the question it is authoritative for.

The general rule: the twin is a durable statement of *configuration and identity*, updated only on three
triggers; telemetry is a stream of *observations*, updated continuously. Where the two can disagree, the twin
is the staler of the pair, because it is not rewritten on a timer.

| Leaf name | Telemetry path | Twin key | Can they diverge? | Authoritative for | Citation |
|---|---|---|---|---|---|
| fw_version | gateway.fw | fw_version | No — same source function | Either. Identical by construction. | main/telemetry/telemetry_v2.c:84 vs main/iothub/app_iothub.c:953 |
| gateway_id / id | gateway.id | gateway_id | No — same source function | Either. Note the key name differs between planes. | main/telemetry/telemetry_v2.c:79 vs main/iothub/app_iothub.c:954 |
| short_id | gateway.short_id | short_id | No — same source function | Either. | main/telemetry/telemetry_v2.c:80 vs main/iothub/app_iothub.c:955 |
| hub_name / name | gateway.name | hub_name | YES — the telemetry key is OMITTED when the name is empty, the twin key is always emitted (as an empty string) | The twin. It always states the value; telemetry only states it when set. | main/telemetry/telemetry_v2.c:82-83 vs main/iothub/app_iothub.c:956 |
| provisioned | data.provisioned | provisioned | No in practice — both read the same predicate | Either. | main/telemetry/telemetry_v2.c:342 vs main/iothub/app_iothub.c:957 |
| valve_mac | data.valve_mac | valve_mac | No — both read the provisioned value, both omitted when unset | Either. | main/telemetry/telemetry_v2.c:344-346 vs main/iothub/app_iothub.c:961 |
| lora_sensor_count | data.lora_sensor_count | lora_sensor_count | No — same source | Either. | main/telemetry/telemetry_v2.c:351 vs main/iothub/app_iothub.c:966 |
| ble_leak_sensor_count | data.ble_leak_sensor_count | ble_leak_sensor_count | No — same source | Either. | main/telemetry/telemetry_v2.c:356 vs main/iothub/app_iothub.c:971 |
| auto_close_enabled | data.rules.auto_close_enabled | auto_close_enabled | No — both conditional on the same getter | Either. Note telemetry nests it under data.rules; the twin is flat. | main/telemetry/telemetry_v2.c:361, :580 vs main/iothub/app_iothub.c:975 |
| trigger_mask | data.rules.trigger_mask | trigger_mask | No — both echo the same stored byte | Either. | main/telemetry/telemetry_v2.c:362, :581 vs main/iothub/app_iothub.c:976 |
| uptime_s | gateway.uptime_s | uptime_s | YES — both read the same clock but at different moments, and the twin is written on only three triggers, so it can be arbitrarily stale | Telemetry, which is current as of its ts. | main/telemetry/telemetry_v2.c:85-86 vs main/iothub/app_iothub.c:979 |
| free_heap | — (not on the telemetry plane) | free_heap | n/a — twin-only | The twin. This is the only hub diagnostic not available in telemetry at all. | main/iothub/app_iothub.c:981 |

## 5. Overlap and derivation relationships

Overlap is presented here as a relationship, not as waste. A field carried in two places
sometimes costs bytes for no benefit, and sometimes buys something real: the twin can be queried without
replaying a message stream, a duplicated value survives a dropped message, and two independent decode paths
fail independently. Each item below states the relationship and what, if anything, the duplication buys.

The byte figures are the actual serialized `"key":value` length plus its separating comma, matching the
compact output of `cJSON_PrintUnformatted` (`main/telemetry/telemetry_v2.c:103`).

### O-1 — data.valve.state == "disconnected" is equivalent to data.valve.connected == false

Both are written from the same boolean. The valve position enum absorbs link state, and link state is then re-emitted as its own boolean.

**Byte cost:** 17 B per snapshot · **What the overlap buys:** Buys nothing structurally, but connected is the field a consumer should actually gate on, because state conflates position with link.

*`main/telemetry/telemetry_v2.c:427-434, :442-443`*

### O-2 — Per-sensor connected is derivable from last_seen_age_s

connected == (last_seen_age_s is not null AND last_seen_age_s <= 600). The timeout is 600 s for both sensor classes.

**Byte cost:** 17-18 B per sensor per snapshot · **What the overlap buys:** Buys a consumer not having to know the 600 s constant, which is a firmware-side decision that could change.

*`main/health_engine/health_engine.c:640-648, :651-656; timeout main/health_engine/health_engine.h:16-17`*

### O-2a — That derivation does NOT hold for the valve — a trap worth 60 seconds

The three valve keys come from three different sources in the same object. connected reads the live BLE link; last_seen_age_s and rating read the health record; and the health engine's own valve connected predicate is different again and is never emitted. There is no timeout constant relating valve connected to valve last_seen_age_s, so a cloud rule written for the sensor arrays will misjudge the valve. Valve liveness is authoritative from connected only.

**Byte cost:** n/a · **What the overlap buys:** This is a correctness note rather than an overlap saving.

*`main/telemetry/telemetry_v2.c:407, :434, :443, :447-455; health predicate main/health_engine/health_engine.c:637-638`*

### O-3 — Per-sensor rating == "critical" is equivalent to connected == false

compute_sensor_rating returns critical on exactly the complement of the connected predicate, using the same constant. rating still adds information above critical, distinguishing excellent/good/warning from battery and RSSI.

**Byte cost:** 21 B per sensor per snapshot · **What the overlap buys:** rating is cached by a 30 s health tick while connected is computed live at snapshot build time, so the two can disagree for up to about 30 seconds. That skew is observable in production data.

*`main/health_engine/health_engine.c:121-124, :126-141; tick main/health_engine/health_engine.h:18`*

### O-4 — data.system_health.rating is the worst rating in the same snapshot

It is a max() over the per-device ratings, every one of which is also emitted in the same message.

**Byte cost:** 21 B per snapshot · **What the overlap buys:** Buys the consumer not having to implement the ordering of the rating enum.

*`main/health_engine/health_engine.c:170-182`*

### O-5 — data.system_health.reason is a prose rendering of the same arrays

The builder reads only rating, dev_type, connected and last_battery — all four already present per device in the same payload. Its only extra input is a battery threshold constant.

**Byte cost:** 31 B healthy, up to about 140 B degraded, capped at 127 characters · **What the overlap buys:** Buys a human-readable summary for a UI without the consumer implementing the phrasing rules.

*`main/telemetry/telemetry_v2.c:157-231, :386; threshold main/health_engine/health_engine.h:21`*

### O-6 — gateway.short_id is the last 4 characters of gateway.id

Both are rendered from the same MAC bytes, and both are emitted on every telemetry message and again on the twin.

**Byte cost:** 18 B per message · **What the overlap buys:** Buys nothing on the wire; it exists because the short id is used in the setup AP SSID.

*`main/hub_identity/hub_identity.c:31-37; emitted main/telemetry/telemetry_v2.c:79-80`*

### O-7 — data.override_remaining_s is usually data.expires_ts minus ts — but not always

Both derive from the same expiry moment. One reachable branch breaks the derivation: when the window has expired but the tick has not yet processed it, remaining is 0 and expires_ts is omitted. A second branch exists in the rules engine for an unsynced clock, but it cannot reach the wire, because build_envelope suppresses the whole message below the same epoch threshold - so a consumer will never observe it.

**Byte cost:** 26 B per snapshot when an override is active · **What the overlap buys:** The exceptions are exactly why the field is not purely redundant. A consumer computing remaining from expires_ts alone will be wrong in both branches.

*`main/rules_engine/rules_engine.c:1191-1198 (expired branch :1197-1198; unreachable unsynced branch :1195-1196); envelope suppression main/telemetry/telemetry_v2.c:69-74; emitted :594-599`*

### O-8 — data.override_active is NOT derivable — listed to close the lead

The obvious derivation "active if expires_ts is present" fails in both O-7 exception branches, where override_active is true with no expires_ts.

**Byte cost:** n/a · **What the overlap buys:** It carries information no other field in the payload does, so it is listed here only to close the lead rather than as a candidate for removal.

*`main/telemetry/telemetry_v2.c:594-599`*

### O-9 — cmd_ack: data.error.code is byte-identical to data.cmd

The same C variable is written to both keys. See F-06.

**Byte cost:** Varies with command name length · **What the overlap buys:** Buys nothing. There is no failure taxonomy on this path.

*`main/telemetry/telemetry_v2.c:707, :711`*

### O-10 — On leak and valve-flood events, data.leak_state is implied by data.event

The event name is selected from the same leak state that is then emitted as the field.

**Byte cost:** 20 B per event · **What the overlap buys:** For valve flood the duplication is also racy: the field is a second, later read of live state, so a fast toggle can in principle produce valve_flood_detected together with leak_state:false. For valve_state_changed the field is independent information and is not redundant.

*`main/iothub/app_iothub.c:1748-1750, :1778-1780; emitted main/telemetry/telemetry_v2.c:644, :620`*

### O-11 — data.auto_close_resumed is (data.active_leak_count > 0)

Both are derived from the same counter in adjacent statements in the same object.

**Byte cost:** 26 B per water_access_override_expired event · **What the overlap buys:** Buys nothing.

*`main/rules_engine/rules_engine.c:1026-1028`*

### O-12 — Eleven of the twelve twin-reported keys duplicate a telemetry field

Only free_heap is unique to the twin. Full row-by-row analysis, including which plane is authoritative and where the two can diverge, is the Plane Collision Index in §4.8.

**Byte cost:** n/a — different plane, not extra bytes per telemetry message · **What the overlap buys:** Buys real queryability: the twin can be read at any time without replaying the telemetry stream, which is exactly what a device-management view needs.

*`main/iothub/app_iothub.c:953-982`*

### O-13 — Four fields are constant on the wire

data.category is always "health"; data.rmleak_asserted is hard-coded true at both emit sites; data.remaining_s is a compile-time constant; and data.provisioned cannot be false in practice because the publish path is gated on being provisioned. Constant does not mean removable: data.category is the key that identifies a health event, and data.rmleak_asserted and data.override_cancelled are presence markers whose information is in whether the key appears at all.

**Byte cost:** About 75 B combined across the messages that carry them · **What the overlap buys:** rmleak_asserted and override_cancelled are presence markers typed as booleans — their information is in whether the key appears, not in its value.

*`main/health_engine/health_engine.c:584; main/rules_engine/rules_engine.c:336, :938, :218, :679-681; main/telemetry/telemetry_v2.c:342; gate main/iothub/app_iothub.c:1674-1688`*

### O-14 — Invariant identity is re-sent on every message

schema, gateway.id, gateway.short_id, gateway.name and gateway.fw change either never or only at a firmware update, yet are present on every telemetry message.

**Byte cost:** About 120 B per message · **What the overlap buys:** Buys self-describing messages: any single message can be interpreted without joining to another source, which matters for an at-least-once stream where a consumer may see messages out of order.

*`main/telemetry/telemetry_v2.c:64, :79-84`*

## 6. Compatibility envelope

### 6.1 There is no over-the-air update path

**Status: FIXED — hard constraint**

A repo-wide search of `main/` for `esp_https_ota`, `esp_ota_begin`, `esp_ota_write` and
`esp_ota_set_boot_partition` returns nothing. The partition table nevertheless reserves `ota_0`, `ota_1` and
`otadata`, so the flash layout anticipates OTA that the application does not implement.

The consequence chain: no OTA client in the image, so a firmware change reaches only units flashed in
production plus any unit physically reflashed, so **a wire-format change leaves already-fielded hubs emitting
the old format indefinitely**. This makes cloud-side handling the only option available for the existing fleet,
and it is why every option set in §7 opens with a no-change option.

How much this costs depends on how many units are fielded and whether the production tool can reflash a
returned unit — neither is determinable from this repository, and both are the first questions in §9.3.

*`partitions.csv:4-8; absence verified across main/`*

### 6.2 The Watts Digital app contract

**Status: UNKNOWN — cannot be established from this repository**

No application source is present in this repository. It is therefore not possible to state which telemetry
fields the app consumes, and no field can be declared safe to rename on the evidence available here.

This is stated as an unknown rather than assumed either way, because assuming a field is unused is the specific
mistake that breaks a shipped app. Resolving it needs the app team, and it is Q-13.

*`[UNVERIFIED] — no app source in this repository`*

### 6.3 Formats already frozen by production tooling

**Status: PARTIALLY FIXED — evidence in-repo**

The production tool parses the hub's UART boot log to read the gateway ID, firmware version and Wi-Fi MAC
during manufacture. That is a serial-console contract rather than a cloud contract, so it does not constrain the
telemetry schema, but it does mean the gateway ID rendering `GW-` + 12 uppercase hex is consumed by tooling
outside this repository.

The identifier format is also part of the command plane, not just telemetry: the LoRa `0x%08lX` rendering is
constructed as a C2D decommission payload, so changing the identifier format on the telemetry plane would
require changing it on the command plane in the same release.

*`main/hub_identity/hub_identity.c:31-37; C2D coupling main/commands/c2d_commands.c:173`*

### 6.4 Historical data already stored cloud-side

**Status: UNKNOWN — but one change is already in flight**

What the backend has already persisted cannot be determined from this repository. One concrete case exists
regardless: the health event `dev_type` value for a BLE leak sensor changed from `ble_leak` to
`ble_leak_sensor` between git HEAD and the working tree. Both spellings will therefore exist in stored history,
and both will exist in the fleet simultaneously for as long as any 1.7.0 hub remains unflashed.

*`main/health_engine/health_engine.c:77; intent comment :68-71`*

### 6.5 Constraints that are weaker than they look

**Status: NOT ACTUALLY FIXED**

Three things are worth explicitly *not* treating as fixed, so the meeting does not rule out options that
are genuinely available.

The telemetry schema string `eflostop.v2` is a constant that identifies the envelope shape, not the field set.
Fields have been added and an enum value renamed without it moving, so nothing in the current design depends on
it staying still — but equally, bumping it would not by itself tell a consumer anything about field-set
capability.

The additive-only pattern is a habit observed in the git history rather than a rule asserted anywhere in code.
No comment in the telemetry path marks any field as frozen.

And the wire is not as locked as the no-OTA constraint suggests for *new* units: anything flashed in production
from now on can carry a different format. The constraint binds the fielded fleet, not the product.

*`main/telemetry/telemetry_v2.c:64; main/telemetry/telemetry_v2.h:14`*

## 7. Options per issue

Every set opens with Option 0 (no change). Nothing here is recommended or chosen.

### F-01 — leak_state cannot express "unknown"

When a sensor has no live cache entry the snapshot emits leak_state:false while emitting null for
every other unknown value in the same object. A sensor that was wet and went silent therefore reads dry, and the
leak_detected event already published for it is never retracted. This is the one item in this document where the
encoding can cause a consumer to under-report a leak.

| ID | Option | Cost | Risk | Blast radius | Owner | FW change on shipped units? | Reversible? |
|---|---|---|---|---|---|---|---|
| O-F01-0 | No change — the cloud gates on connected | Zero device work. The rule is: treat leak_state as meaningful only when connected is true; otherwise treat leak state as unknown and carry forward the last known value. | The rule must be applied in every consumer that touches leak_state, including dashboards and any alerting path. A single consumer that forgets it under-reports a live leak. | Every current and future cloud consumer. | Backend | No | Reversible |
| O-F01-a | Emit null instead of false when there is no cache entry | A one-line change per array in the device, making leak_state consistent with its four sibling keys. | Changes the JSON type of a field from boolean to nullable boolean. Any consumer with a strict boolean parser breaks. Fielded units keep emitting false regardless, so the cloud rule from Option 0 is still required during the transition. | Device firmware plus every consumer's type handling. | Firmware, then backend | Yes — shipped units unaffected | Reversible |
| O-F01-b | Omit the key entirely when there is no cache entry | Aligns leak_state with the class-1 omission convention used elsewhere in the payload. | Same breakage profile as O-F01-a, and additionally changes the object's key set, which affects consumers that assume a fixed shape for array elements. The sensor arrays are currently the one place with a guaranteed-fixed shape. | Device firmware plus consumers assuming fixed array-element shape. | Firmware, then backend | Yes | Reversible |
| O-F01-c | Add a separate freshness field rather than changing leak_state | Leaves the existing field untouched and adds an explicit indicator such as a data-age or validity flag. | Purely additive, so nothing breaks — but it grows the payload and leaves the misleading field in place for any consumer that does not adopt the new one. | Additive only. | Firmware | Yes | Reversible |

### F-03 — Pre-clock-sync telemetry is destroyed rather than buffered

Every message built before SNTP completes is deleted inside build_envelope and never reaches the
offline-buffer branch. Leak events and command acks are included. For an ack this means the cloud sees a timeout
on a command that actually executed.

| ID | Option | Cost | Risk | Blast radius | Owner | FW change on shipped units? | Reversible? |
|---|---|---|---|---|---|---|---|
| O-F03-0 | No change — the cloud tolerates the gap | Zero device work. Consumers treat the interval between a hub's boot and its first message as a known blind spot, and treat a missing ack as indeterminate rather than as failure. | A non-idempotent command issued in that window may be retried against a device that already executed it. The blind spot's length is not observable from the cloud. | Command-issuing services and any consumer reasoning about gaps. | Backend | No | Reversible |
| O-F03-a | Buffer pre-sync messages and stamp them at drain time | Messages survive the window and are published once the clock is known. | The timestamp would then be the drain time rather than the observation time, which changes what ts means for those messages. Requires a way to distinguish stamped-late messages, otherwise event ordering silently degrades. | Device firmware plus any consumer that reasons about ts precisely. | Firmware | Yes | Reversible |
| O-F03-b | Buffer with a monotonic offset and resolve at drain time | Preserves true relative ordering and true intervals by recording uptime at build time and converting once the wall clock is known. | More device-side state and more complexity in the buffer path, which is currently deliberately simple — it stores bytes and replays them verbatim. | Device firmware only; the wire format is unchanged if the resolved ts is written before publishing. | Firmware | Yes | Reversible |
| O-F03-c | Delay the MQTT connection until the clock has synced | Removes the window rather than handling it, since nothing is published before the clock is valid. | Delays every hub's first contact, including its lifecycle message and its twin patch, and makes a hub with no working SNTP invisible rather than partially visible. Trades a data gap for a connectivity gap. | Device firmware; changes observed connect timing for every hub. | Firmware | Yes | One-way in effect — hubs that cannot sync stop reporting at all |

### F-02 — LoRa leak state is derived two different ways

The event path tests leak_status != 0 and the snapshot path tests leak_status == 1, so a sensor
reporting 2 auto-closes the valve while every snapshot reports dry. Note this is latent while the LoRa radio is
not populated on the PCBA.

| ID | Option | Cost | Risk | Blast radius | Owner | FW change on shipped units? | Reversible? |
|---|---|---|---|---|---|---|---|
| O-F02-0 | No change — latent while LoRa hardware is DNP | Zero work. The divergence cannot occur in the field today because no LoRa sensor is fitted. | The inconsistency remains in the code and becomes live the moment LoRa hardware is populated. It is invisible in production data until then, so it will not be rediscovered by observation. | None today; the LoRa product variant later. | Neither — deferred | No | Reversible |
| O-F02-a | Make both paths use the same test | One-line change; removes the divergence at source. | Requires deciding which semantic is correct, which needs the sensor firmware's definition of leak_status — that firmware is not in this repository. | Device firmware. | Firmware | Yes | Reversible |

### 4.6 — Three inconsistent representations of "a device"

The valve, a LoRa sensor and a BLE leak sensor are described by three differently-shaped objects,
and the same sensor appears nested in a snapshot but flat in an event. A consumer needs several extraction paths
for what is conceptually one entity.

| ID | Option | Cost | Risk | Blast radius | Owner | FW change on shipped units? | Reversible? |
|---|---|---|---|---|---|---|---|
| O-46-0 | No change — the cloud normalises at ingest | Zero device work. A mapping layer converts all three shapes plus the flat event form into one internal entity model. | The mapping is permanent and must be maintained as fields are added. Note that it is also the only option in this set that reaches already-fielded units (§6.1) — what follows from that is for the meeting to decide. | Backend ingest layer. | Backend | No | Reversible |
| O-46-a | Converge the device shapes on new firmware, keep both readable | New units emit one consistent device object; the ingest mapping is retained for older units. | Two shapes in flight simultaneously and indefinitely, because fielded units never converge without a reflash. The mapping layer is not removed, only frozen. | Firmware plus backend, with a long dual-support tail. | Both | Yes | Reversible |
| O-46-b | Additive convergence — emit the new shape alongside the old | Nothing breaks, because the existing keys stay. Consumers migrate at their own pace. | Materially increases payload size on a message that is already the largest, and leaves two representations of the same data in one message, which is its own consistency problem. | Payload size; every consumer eventually. | Firmware | Yes | Reversible |

### 4.2 — Identifier rendering is not guaranteed join-safe

A BLE sensor's identifier reaches the cloud from two different renderers depending on message type.
Both are uppercase colon-separated by convention, but firmware compares case-insensitively, so the case is not an
invariant the device enforces on itself.

| ID | Option | Cost | Risk | Blast radius | Owner | FW change on shipped units? | Reversible? |
|---|---|---|---|---|---|---|---|
| O-42-0 | No change — the cloud normalises identifiers at ingest | Zero device work. Case-fold and strip separators on ingest, and join on the normal form. | Must also be applied to historical rows, or joins will straddle two conventions. Whether that has already been done is Q-04. | Backend ingest plus any historical backfill. | Backend | No | Reversible |
| O-42-a | Guarantee a single rendering in firmware | Makes the convention an invariant so a byte-equality join is safe. | Does not help fielded units, and does not by itself fix historical rows. | Firmware. | Firmware | Yes | Reversible |

### O-12 — Eleven twin keys duplicate telemetry fields

Only free_heap is twin-only. A reader searching for a name finds two answers with different
delivery semantics; two of the eleven can genuinely diverge.

| ID | Option | Cost | Risk | Blast radius | Owner | FW change on shipped units? | Reversible? |
|---|---|---|---|---|---|---|---|
| O-12-0 | No change — document which plane is authoritative | Zero engineering. The Plane Collision Index in §4.8 already states, per key, whether the values can diverge and which plane answers which question. | Relies on readers consulting it. The two divergent cases (hub_name presence, uptime_s staleness) will surprise anyone who does not. | Documentation only. | Neither | No | Reversible |
| O-12-a | Reduce the twin to what only the twin can answer | Keeps identity and configuration on the twin and drops values that telemetry already carries live. | Removes the ability to query current state without replaying telemetry, which is the twin's main advantage. Also breaks any consumer reading those twin keys today. | Backend consumers of the twin. | Both | Yes | Reversible |
| O-12-b | Keep both and make the overlap explicit in the contract | Formalises the split rather than changing it: the twin is the queryable current-state document, telemetry is the historical record. | No reduction in bytes or duplication; the benefit is conceptual clarity only. | Documentation and convention. | Both | No | Reversible |

## 8. Migration and versioning

This document describes the working tree, which is FW 1.8.0 and uncommitted. Git HEAD is b62b25e,
which is FW 1.7.0. The two differ on the telemetry plane in exactly two ways. Everything else in the working-tree
diff is internal: no cJSON_Add* call was added or removed anywhere in main/, so no field appeared or disappeared.

| What changed | At HEAD (1.7.0) | In the working tree (1.8.0) | Consequence | Citation |
|---|---|---|---|---|
| data.dev_type value for a BLE leak sensor | ble_leak | ble_leak_sensor | Breaking for any consumer matching the exact string. Deliberate, to match source_type on leak events; the intent is stated in an in-source comment. | main/health_engine/health_engine.c:77; comment :68-71 |
| Accepted sensor_type aliases on the sensor_meta command | "ble" only, case-insensitively | ble_leak_sensor, ble_leak and ble all accepted case-insensitively | Additive and inbound only. Widens what the command plane accepts; changes nothing outbound. | main/sensor_meta/sensor_meta.h:51-56; sensor_meta handler main/iothub/app_iothub.c:859-873; the same helper is also used by the decommission target test at :758 |

Because there is no OTA path, both versions will exist in the fleet simultaneously and
indefinitely. A 1.7.0 hub emits `ble_leak`; a 1.8.0 hub emits `ble_leak_sensor`; both spellings will appear in
stored history. Any consumer matching that value needs to accept both, and the decision on whether that
acceptance is permanent is Q-04.

## 9. Open questions

### For the backend team

- **Q-01** Do you already normalise identifiers at ingest — and if so, what is the normal form, and has it been applied to historical rows?  
  *Unblocks O-42-0 and O-42-a.*
- **Q-02** Which fields does your ingest actually read today, and which are stored but unused? We cannot determine this from the firmware repository, and it decides which of the four constant-on-the-wire fields can simply be dropped.  
  *Unblocks O-13 decisions and the consent agenda.*
- **Q-04** Both spellings of the BLE device class, ble_leak and ble_leak_sensor, will exist in the fleet and in stored history. Do you want the cloud to accept both permanently, or should the firmware revert until a coordinated cutover?  
  *Unblocks the §18 rollout decision.*
- **Q-05** Is a nullable boolean acceptable in your parsers for leak_state, or would a separate freshness field be easier to adopt?  
  *Chooses between O-F01-a, O-F01-b and O-F01-c.*
- **Q-06** Do you treat a missing cmd_ack as failure today? If so, the pre-clock-sync window can make a successful non-idempotent command look failed.  
  *Unblocks O-F03-0.*
- **Q-07** Would you rather receive one converged device shape and do less mapping, or keep the current shapes and keep the mapping you have already built?  
  *Chooses between O-46-0, O-46-a and O-46-b.*

### For the firmware team

- **Q-08** Are the two auto_close payload shapes deliberate? No source comment reconciles them, and the valve-reconnect variant uses a source_type value that is not a member of the underlying enum.  
  *Resolves the open item in §4.6.*
- **Q-09** Should snapshot_interval_s persist across reboots, and should it be echoed into twin reported — or is the current RAM-only behaviour acceptable if it is simply documented?  
  *Resolves F-07 and F-08.*
- **Q-10** Should valve disconnection emit its own event, or is inferring it from the next snapshot's connected:false and the health device_offline event sufficient?  
  *Resolves F-04.*
- **Q-11** Should trigger_mask be constrained to its three defined bits at all — and if so, on the way in, on the way out, on both, or should the device keep echoing the raw byte and let the cloud clamp it?  
  *Resolves F-05.*
- **Q-12** Is a machine-readable failure class wanted on cmd_ack, given data.error.code currently repeats the command name?  
  *Resolves F-06.*

### Product / owner decisions

- **Q-03** How many hubs are fielded, how many are in inventory, and can the production tool reflash a returned unit and at what cost? This single answer determines how much the no-OTA constraint costs and therefore how seriously to take every device-side option in §7.  
  *Gates every option marked "requires firmware change on shipped units".*
- **Q-13** Which fields does the Watts Digital app consume? No app source is in this repository, so no field can currently be declared safe to rename.  
  *Gates every rename option.*
- **Q-14** Is there an agreed path to update fielded hubs at all — OTA added later, service reflash, or replacement? The answer decides whether "change the wire format" is ever available for the existing fleet.  
  *Gates the long-term shape of every option set.*
- **Q-15** This product reports no flow or consumption data at all. Is that a permanent product decision, or a gap the backend is expected to fill later? It determines whether usage analytics and consumption billing are off the table.  
  *Scopes future work.*
- **Q-16** There is no audit surface: cmd_ack records that a command succeeded but carries no actor identity and no authentication result. Is a who-closed-my-valve trail required?  
  *Scopes future work.*


## 10. Telemetry message catalogue

### Lifecycle ("online")  (`type` = `lifecycle`)

**Purpose.** Announces that the hub has an MQTT session and states how it is commissioned. It is the cloud's cue that a hub is reachable and what it believes it is responsible for.

**Trigger.** Every MQTT connect, once the hub is provisioned — not once per boot. A reconnect after a network drop produces another one.  
**Cadence.** Per MQTT connect (including scheduled reconnects).  
**Retry.** Not retried. If the publish fails, the message is lost; the next connect produces a fresh one.  
**Builder.** `main/telemetry/telemetry_v2.c:334-368`

Emitted AFTER the offline buffer is drained and BEFORE the twin reported patch (main/iothub/app_iothub.c:1693-1695). It is never buffered when offline, because publish_json only buffers type=="event" (main/telemetry/telemetry_v2.c:115-122).

Maximal payload — every key that can appear on this message type, shown together. Keys that are
mutually exclusive in practice are included deliberately; this is not a single observed message.

```json
{
  "schema": "eflostop.v2",
  "ts": 1704067200,
  "gateway": {
    "id": "GW-%02X%02X%02X%02X%02X%02X",
    "short_id": "%02X%02X",
    "name": "<hub name, up to 31 chars>",
    "fw": "1.8.0",
    "uptime_s": 0
  },
  "type": "lifecycle",
  "data": {
    "event": "online",
    "reset_reason": "power_on",
    "provisioned": true,
    "valve_mac": "%02X:%02X:%02X:%02X:%02X:%02X",
    "lora_sensor_count": 16,
    "ble_leak_sensor_count": 16,
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    }
  }
}
```

### Snapshot (full state roll-up)  (`type` = `snapshot`)

**Purpose.** The complete current state of the hub, the valve and every commissioned sensor in one message. It is the broadest view of the system, but note that it is NOT authoritative for leak state: F-01 shows that a sensor which has gone silent reports leak_state:false in a snapshot while the leak_detected event already published for it is never retracted. For leak state the event stream is authoritative and the snapshot is not.

**Trigger.** A 300 s heartbeat, plus a coupled snapshot after most events, plus commissioning changes, plus one at boot.  
**Cadence.** 300 s default. The interval is settable via the twin desired property snapshot_interval_s (clamped 60..3600, main/iothub/app_iothub.c:1022) but is held in RAM only, so it reverts to 300 s on every reboot.  
**Retry.** The heartbeat is re-armed only when the publish actually reached esp-mqtt with msg_id >= 0 (success branch main/iothub/app_iothub.c:1884-1916). If the publish fails while connected — for example a full QoS-1 outbox — the next attempt is floored 5 s out rather than retried tightly (failure branch :1917-1923; SNAP_RETRY_FLOOR_MS defined at :153).  
**Builder.** `main/telemetry/telemetry_v2.c:372-604`

data.reason names the trigger. Seven values are reachable: five from snap_reason_str (main/iothub/app_iothub.c:581-591), plus "decommission" passed as a literal (:1570), plus "c2d_command" which appears on the same key inside the auto_close_reenabled rules event (main/rules_engine/rules_engine.c:722).

Maximal payload — every key that can appear on this message type, shown together. Keys that are
mutually exclusive in practice are included deliberately; this is not a single observed message.

```json
{
  "schema": "eflostop.v2",
  "ts": 1704067200,
  "gateway": {
    "id": "GW-%02X%02X%02X%02X%02X%02X",
    "short_id": "%02X%02X",
    "name": "<hub name, up to 31 chars>",
    "fw": "1.8.0",
    "uptime_s": 0
  },
  "type": "snapshot",
  "data": {
    "reason": "heartbeat",
    "system_health": {
      "rating": "excellent",
      "reason": "All devices healthy"
    },
    "valve": {
      "mac": "%02X:%02X:%02X:%02X:%02X:%02X",
      "state": "open",
      "battery": 100,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "fw_version": "<valve DIS firmware revision string, up to 31 chars>",
      "rating": "excellent",
      "last_seen_age_s": 0
    },
    "lora_sensors": [
      {
        "sensor_id": "0x%08lX",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 0,
        "battery": 100,
        "leak_state": false,
        "rssi": -128,
        "snr": -32,
        "location": {
          "code": "kitchen",
          "label": "<label, up to 31 chars>"
        }
      }
    ],
    "ble_leak_sensors": [
      {
        "sensor_id": "%02X:%02X:%02X:%02X:%02X:%02X",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 0,
        "battery": 100,
        "leak_state": false,
        "rssi": -128,
        "fw_version": "<sensor firmware revision string, up to 11 chars>",
        "location": {
          "code": "kitchen",
          "label": "<label, up to 31 chars>"
        }
      }
    ],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": true,
    "override_remaining_s": 86400,
    "expires_ts": 1704067200
  }
}
```

### Valve event  (`type` = `event`)

**Purpose.** Reports that the valve moved, or that the valve's own on-board flood probe went wet or dry.

**Trigger.** A BLE GATT notification from the valve whose value differs from the last one seen (delta-gated).  
**Cadence.** Event-driven, delta-gated. No fixed rate.  
**Retry.** Buffered for replay if the hub is offline (main/telemetry/telemetry_v2.c:115-118).  
**Builder.** `main/telemetry/telemetry_v2.c:608-629`

There is NO valve_disconnected event. g_valve_mac is zeroed at main/ble_valve/app_ble_valve.c:1271 BEFORE notify_hub_update(BLE_UPD_DISCONNECTED) at :1272, so the MAC lookup fails, mac_ok is false, and the whole valve-event block is skipped (main/iothub/app_iothub.c:1761-1796). Valve loss is visible only via the next snapshot's connected:false and a health device_offline event.

Maximal payload — every key that can appear on this message type, shown together. Keys that are
mutually exclusive in practice are included deliberately; this is not a single observed message.

```json
{
  "schema": "eflostop.v2",
  "ts": 1704067200,
  "gateway": {
    "id": "GW-%02X%02X%02X%02X%02X%02X",
    "short_id": "%02X%02X",
    "name": "<hub name, up to 31 chars>",
    "fw": "1.8.0",
    "uptime_s": 0
  },
  "type": "event",
  "data": {
    "event": "valve_state_changed",
    "valve_state": "open",
    "battery": 100,
    "leak_state": false,
    "rmleak": false,
    "fw_version": "<valve DIS firmware revision string, up to 31 chars>"
  }
}
```

### Leak event  (`type` = `event`)

**Purpose.** Reports that a specific leak sensor went wet or dry. This is the safety-critical message.

**Trigger.** A LoRa packet or BLE advertisement whose leak state differs from the cached value.  
**Cadence.** Event-driven, delta-gated. The first sighting of a sensor emits only if it is currently leaking (main/iothub/app_iothub.c:289, :300, :336, :350).  
**Retry.** Buffered for replay if offline. Destroyed, not buffered, if the clock has not synced (see §4.3).  
**Builder.** `main/telemetry/telemetry_v2.c:631-654`

Carries the sensor's identity flat at data.sensor_id, whereas a snapshot carries the same sensor nested inside an array element. The two representations differ — see §4.6.

Maximal payload — every key that can appear on this message type, shown together. Keys that are
mutually exclusive in practice are included deliberately; this is not a single observed message.

```json
{
  "schema": "eflostop.v2",
  "ts": 1704067200,
  "gateway": {
    "id": "GW-%02X%02X%02X%02X%02X%02X",
    "short_id": "%02X%02X",
    "name": "<hub name, up to 31 chars>",
    "fw": "1.8.0",
    "uptime_s": 0
  },
  "type": "event",
  "data": {
    "event": "leak_detected",
    "source_type": "ble_leak_sensor",
    "sensor_id": "%02X:%02X:%02X:%02X:%02X:%02X",
    "leak_state": true,
    "battery": 100,
    "rssi": -128,
    "location": {
      "code": "kitchen",
      "label": "<label, up to 31 chars>"
    }
  }
}
```

### Rules-engine event  (`type` = `event`)

**Purpose.** Reports a decision the hub made: it closed the valve because of a leak, it declined to close because a user override was active, or an override window started, was cancelled, or expired.

**Trigger.** The rules engine, on leak evaluation, on a C2D override command, on valve reconnect, and on its periodic tick.  
**Cadence.** Event-driven.  
**Retry.** Buffered for replay if offline.  
**Builder.** `main/telemetry/telemetry_v2.c:656-674 (envelope wrapper); payloads built in main/rules_engine/rules_engine.c`

auto_close has TWO structurally different shapes: the sensor-triggered form carries an optional location object (main/rules_engine/rules_engine.c:328-357), while the valve-reconnect form instead carries active_leak_count and uses source_type:"reconnect", which is not a member of the leak_source_t enum (:932-943). Nothing in source reconciles them.

Maximal payload — every key that can appear on this message type, shown together. Keys that are
mutually exclusive in practice are included deliberately; this is not a single observed message.

> NOTE: this family is a UNION across several mutually exclusive event shapes. No single
> message carries all of these keys. Only `data.event` is present on every shape.

```json
{
  "schema": "eflostop.v2",
  "ts": 1704067200,
  "gateway": {
    "id": "GW-%02X%02X%02X%02X%02X%02X",
    "short_id": "%02X%02X",
    "name": "<hub name, up to 31 chars>",
    "fw": "1.8.0",
    "uptime_s": 0
  },
  "type": "event",
  "data": {
    "event": "auto_close",
    "source_type": "ble_leak_sensor",
    "sensor_id": "%02X:%02X:%02X:%02X:%02X:%02X",
    "rmleak_asserted": true,
    "location": {
      "code": "kitchen",
      "label": "<label, up to 31 chars>"
    },
    "active_leak_count": 1,
    "override_remaining_s": 86400,
    "trigger": "button",
    "expires_ts": 1704067200,
    "remaining_s": 86400,
    "override_cancelled": true,
    "previous_remaining_s": 86400,
    "reason": "c2d_command",
    "auto_close_resumed": true,
    "clear_after_seconds": 30,
    "raw": "<the unparsed engine payload, verbatim>"
  }
}
```

### Health event  (`type` = `event`)

**Purpose.** Reports that a peer device crossed into, or out of, the 'critical' health rating.

**Trigger.** The health engine's rating recomputation, on a 30 s tick.  
**Cadence.** Event-driven off a 30 s evaluation tick (main/health_engine/health_engine.h:18).  
**Retry.** Buffered for replay if offline.  
**Builder.** `main/health_engine/health_engine.c:577-608 (payload); envelope main/telemetry/telemetry_v2.c:676-693`

These are the only events carrying data.category:"health" — that key is how you recognise them. The name is derived from a rating comparison rather than from a distinct event type: is_offline = (new_rating == HEALTH_CRITICAL) (main/health_engine/health_engine.c:586). In practice critical is only ever reached for connectivity reasons - a sensor never seen or past its 600 s timeout (:122-123), or a valve past its 3-minute disconnect grace (:148-150), or a device not yet heard from since boot (:457, :472, :487). A low battery yields warning, never critical (:126-127), so the name does match the cause.

Maximal payload — every key that can appear on this message type, shown together. Keys that are
mutually exclusive in practice are included deliberately; this is not a single observed message.

```json
{
  "schema": "eflostop.v2",
  "ts": 1704067200,
  "gateway": {
    "id": "GW-%02X%02X%02X%02X%02X%02X",
    "short_id": "%02X%02X",
    "name": "<hub name, up to 31 chars>",
    "fw": "1.8.0",
    "uptime_s": 0
  },
  "type": "event",
  "data": {
    "category": "health",
    "event": "device_offline",
    "dev_type": "ble_leak_sensor",
    "sensor_id": "%02X:%02X:%02X:%02X:%02X:%02X",
    "rating": "critical",
    "prev_rating": "excellent",
    "battery": 100,
    "rssi": -128,
    "offline_duration_s": 600,
    "raw": "<the unparsed engine payload, verbatim>"
  }
}
```

### Command acknowledgement  (`type` = `event`)

**Purpose.** Reports the outcome of an inbound cloud-to-device command.

**Trigger.** Completion of C2D command handling, success or failure.  
**Cadence.** One per inbound command.  
**Retry.** Buffered for replay if offline.  
**Builder.** `main/telemetry/telemetry_v2.c:695-718`

This message straddles two planes: it is the response to a C2D command but is delivered as D2C telemetry, because there is no direct-method subscription anywhere in the firmware. It is also published from the esp-mqtt event task rather than the main IoT Hub task, so it bypasses the provisioned gate and the snapshot scheduler and can interleave at any point in the stream.

Maximal payload — every key that can appear on this message type, shown together. Keys that are
mutually exclusive in practice are included deliberately; this is not a single observed message.

```json
{
  "schema": "eflostop.v2",
  "ts": 1704067200,
  "gateway": {
    "id": "GW-%02X%02X%02X%02X%02X%02X",
    "short_id": "%02X%02X",
    "name": "<hub name, up to 31 chars>",
    "fw": "1.8.0",
    "uptime_s": 0
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "<correlation id, up to 63 chars>",
    "cmd": "valve_open",
    "status": "error",
    "error": {
      "code": "valve_open",
      "detail": "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."
    }
  }
}
```

## 11. Master telemetry data dictionary

| JSON path | Type | Units | Range/enum | Opt? | Gating | Message types | Meaning | Source |
|---|---|---|---|---|---|---|---|---|
| `data` | object | n/a | n/a | No | always in practice; see note | cmd_ack, health_event, leak_event, lifecycle, rules_event, snapshot, valve_event | Type-specific payload container. Created and attached with UNCHECKED cJSON calls in every publisher, so an allocation failure yields a published envelope with NO data key. | `main/telemetry/telemetry_v2.c:339+366 (lifecycle), :377+602 (snapshot), :613+627, :640+652, :665, :670, :684, :689, :703+716` |
| `data.active_leak_count` | number | count | >= 0 | No | always in the valve-reconnect auto_close and in water_access_override_expired | rules_event | How many sensors were simultaneously asserting a leak. Present ONLY in these two shapes, so the normal auto_close lacks it. | `main/rules_engine/rules_engine.c:939, :1028` |
| `data.auto_close_resumed` | bool | n/a | true \| false | No | always (water_access_override_expired only) | rules_event | Whether leaks were still active when the window expired, i.e. whether auto-close fired immediately on resume. | `main/rules_engine/rules_engine.c:1026-1027` |
| `data.battery` | number | percent | 0..255 as emitted; sender intent 0..100 | Yes | always (leak events, valve events); conditional in health alerts (OMITTED when 0xFF) | health_event, leak_event, valve_event | Battery of the peer the event is about. Note the health-alert variant omits the key when unknown while the leak/valve variants always emit it. | `main/telemetry/telemetry_v2.c:645 (leak), :619 (valve), main/health_engine/health_engine.c:595-597 (health, omit-on-0xFF)` |
| `data.ble_leak_sensor_count` | number | count | 0..MAX_BLE_LEAK_SENSORS | No | always (lifecycle only) | lifecycle | How many BLE leak sensors are commissioned. | `main/telemetry/telemetry_v2.c:353-356` |
| `data.ble_leak_sensors` | array | n/a | array of objects | No | always (snapshot only); empty array when none or health is unavailable | snapshot | One entry per COMMISSIONED BLE leak sensor, driven by the health table. | `main/telemetry/telemetry_v2.c:518, :571; iteration :520-570` |
| `data.ble_leak_sensors[].battery` | number\|null | percent | 0..255 as emitted; header intent 0..100 | Yes | always present; explicit null when there is no live cache entry | snapshot | Battery from manufacturer-data byte 3 of the eleak advertisement. | `main/telemetry/telemetry_v2.c:553 (value), :561 (null); parse main/ble_leak_scanner/app_ble_leak.c:177` |
| `data.ble_leak_sensors[].connected` | bool | n/a | true \| false | No | always for each element | snapshot | Heard within the BLE-leak health timeout. Also gates the cache merge below. | `main/telemetry/telemetry_v2.c:526; merge gate :541` |
| `data.ble_leak_sensors[].fw_version` | string\|null | n/a | "M.m.p", max 11 chars | Yes | always present; explicit null when the cache has no version OR there is no cache entry | snapshot | Sensor firmware version, built ONLY when the advertisement carries >= 7 bytes of manufacturer data. A 4-byte (older) payload leaves this permanently null. | `main/telemetry/telemetry_v2.c:556-559 (string or null), :564 (null); parse gate main/ble_leak_scanner/app_ble_leak.c:180-185` |
| `data.ble_leak_sensors[].last_seen_age_s` | number\|null | seconds | >= 0 when a number | Yes | always present; explicit null when UINT32_MAX (never heard) | snapshot | Seconds since this sensor's last advertisement was accepted. | `main/telemetry/telemetry_v2.c:530-535` |
| `data.ble_leak_sensors[].leak_state` | bool | n/a | true \| false | Yes | always present; hard-coded false (NOT null) when there is no live cache entry | snapshot | Leak flag from manufacturer-data byte 2, tested != 0 (consistent with the BLE leak event, unlike the LoRa path). | `main/telemetry/telemetry_v2.c:554 (value), :562 (placeholder false); parse main/ble_leak_scanner/app_ble_leak.c:176-178` |
| `data.ble_leak_sensors[].location` | object | n/a | n/a | Yes | always for each element (never omitted) | snapshot | Installed-location metadata container. | `main/telemetry/telemetry_v2.c:567 -> builder :143-153` |
| `data.ble_leak_sensors[].location.code` | string | n/a | enum, 13 values: unknown \| bathroom \| kitchen \| laundry \| garage \| garden \| basement \| utility \| hallway \| bedroom \| living_room \| attic \| outdoor | No | always for each element | snapshot | Machine-readable room code; "unknown" when no metadata is stored. | `main/telemetry/telemetry_v2.c:148; table main/sensor_meta/sensor_meta.c:23-27` |
| `data.ble_leak_sensors[].location.label` | string | n/a | free text, max 31 chars | No | always for each element; "" when no metadata is stored | snapshot | User-entered free-text location label. | `main/telemetry/telemetry_v2.c:151` |
| `data.ble_leak_sensors[].rating` | string | n/a | enum: excellent \| good \| warning \| critical \| unknown | No | always for each element | snapshot | Health rating from timeout + battery + RSSI thresholds. | `main/telemetry/telemetry_v2.c:527-528; compute main/health_engine/health_engine.c:115-143` |
| `data.ble_leak_sensors[].rssi` | number\|null | dBm | int8_t, typically -100..-20 | Yes | always present; explicit null when there is no live cache entry | snapshot | Advertisement RSSI as reported by NimBLE (no narrowing bug on this path). | `main/telemetry/telemetry_v2.c:555 (value), :563 (null)` |
| `data.ble_leak_sensors[].sensor_id` | string | n/a | colon-separated MAC | No | always for each element | snapshot | BLE leak sensor MAC as rendered by the health engine. | `main/telemetry/telemetry_v2.c:525` |
| `data.category` | string | n/a | Fixed literal "health" | No | always (health alert events only) | health_event | Marks the event as coming from the health engine rather than the rules engine. Constant. | `main/health_engine/health_engine.c:584` |
| `data.clear_after_seconds` | number | seconds | AUTO_CLEAR_TIMEOUT_MS / 1000 | No | always (rmleak_auto_cleared only) | rules_event | The all-clear dwell time that had to elapse before the hub auto-cleared the RMLEAK interlock. | `main/rules_engine/rules_engine.c:1083; constant main/rules_engine/rules_engine.c:18` |
| `data.cmd` | string | n/a | free text, max 31 chars | No | always (cmd_ack only) | cmd_ack | Echo of the command name being acked, taken verbatim from the inbound cmd field (so it may be an unrecognised name). | `main/telemetry/telemetry_v2.c:707; source main/iothub/app_iothub.c:934` |
| `data.dev_type` | string | n/a | enum: valve \| lora \| ble_leak_sensor \| unknown | No | always (health alert events only) | health_event | Which kind of peer the health alert is about. CHANGED IN 1.8.0: was "ble_leak" at HEAD, now "ble_leak_sensor" to match leak-event source_type. | `main/health_engine/health_engine.c:590; mapping :72-80 (git diff shows "ble_leak" at HEAD b62b25e)` |
| `data.error` | object | n/a | n/a | Yes | conditional: OMITTED on success or when no error message was supplied | cmd_ack | Failure detail container. | `main/telemetry/telemetry_v2.c:709-713` |
| `data.error.code` | string | n/a | same value as data.cmd | Yes | present whenever data.error is present | cmd_ack | The failing command's name (NOT a numeric or symbolic error code) — it is literally cmd_name again. | `main/telemetry/telemetry_v2.c:711` |
| `data.error.detail` | string | n/a | free text | Yes | present whenever data.error is present | cmd_ack | Human-readable, end-user-facing failure reason. Longest strings come from the override_enable rejection set. | `main/telemetry/telemetry_v2.c:712; message set main/iothub/app_iothub.c:539-541, :679, :696, :708, :720, :832-845` |
| `data.event` | string | n/a | enum, 18 values: online \| valve_state_changed \| valve_flood_detected \| valve_flood_cleared \| leak_detected \| leak_cleared \| cmd_ack \| auto_close \| auto_close_blocked_override \| auto_close_reenabled \| rmleak_cleared \| rmleak_auto_cleared \| water_access_override_enabled \| water_access_override_expired \| device_offline \| device_recovered \| rules_engine \| health_engine | No | always in lifecycle and in every event shape; ABSENT in snapshot | cmd_ack, health_event, leak_event, lifecycle, rules_event, valve_event | Event discriminator inside data. The single most important routing key for the backend event log. | `main/telemetry/telemetry_v2.c:340 ("online"), :614, :641, :668, :687, :704; main/rules_engine/rules_engine.c:215, :333, :469, :678, :720, :934, :1025, :1082; main/health_engine/health_engine.c:587-588` |
| `data.expires_ts` | number | seconds (Unix epoch, UTC) | >= 1704067200 | Yes | snapshot: OMITTED unless override_active AND the expiry is known/in-future. water_access_override_enabled event: always. | rules_event, snapshot | Absolute epoch at which the override window expires. Same field name and meaning on both message types. | `main/telemetry/telemetry_v2.c:598-599; main/rules_engine/rules_engine.c:217` |
| `data.fw_version` | string | n/a | free text, max 31 chars | Yes | conditional: OMITTED when the valve DIS read failed (NO null variant here) | valve_event | Valve firmware revision inside a valve event. Note the different absence convention from data.valve.fw_version, which uses an explicit null. | `main/telemetry/telemetry_v2.c:623-625 vs main/telemetry/telemetry_v2.c:436-440` |
| `data.id` | string | n/a | free text, max 63 chars | Yes | conditional: OMITTED when the inbound command carried no correlation id (NOT "") | cmd_ack | Echo of the inbound command's correlation id so the app can match the ack to its request. C2D_COMMANDS.md:127 claims an empty string here; the source omits the key. | `main/telemetry/telemetry_v2.c:705-706` |
| `data.leak_state` | bool | n/a | true \| false | No | always (leak events and valve events) | leak_event, valve_event | Leak asserted or cleared at the moment of the event. | `main/telemetry/telemetry_v2.c:644 (leak events), :620 (valve events)` |
| `data.location` | object | n/a | n/a | Yes | leak events: ALWAYS. auto_close: OMITTED when the source is valve_flood or no metadata is stored. | leak_event, rules_event | Installed-location metadata for the sensor that raised the event. TWO DIFFERENT ABSENCE CONVENTIONS on the same path depending on the producer. | `always-emit main/telemetry/telemetry_v2.c:650 -> :143-153; omit-when-unknown main/rules_engine/rules_engine.c:338-349` |
| `data.location.code` | string | n/a | enum, 13 values: unknown \| bathroom \| kitchen \| laundry \| garage \| garden \| basement \| utility \| hallway \| bedroom \| living_room \| attic \| outdoor | Yes | present whenever data.location is present | leak_event, rules_event | Machine-readable room code. | `main/telemetry/telemetry_v2.c:148; main/rules_engine/rules_engine.c:344; table main/sensor_meta/sensor_meta.c:23-27` |
| `data.location.label` | string | n/a | free text, max 31 chars | Yes | present whenever data.location is present | leak_event, rules_event | User-entered free-text location label. | `main/telemetry/telemetry_v2.c:151; main/rules_engine/rules_engine.c:346` |
| `data.lora_sensor_count` | number | count | 0..MAX_LORA_SENSORS | No | always (lifecycle only) | lifecycle | How many LoRa leak sensors are commissioned. | `main/telemetry/telemetry_v2.c:348-351` |
| `data.lora_sensors` | array | n/a | array of objects | No | always (snapshot only); empty array when no LoRa sensors or health is unavailable | snapshot | One entry per COMMISSIONED LoRa leak sensor, driven by the health table (not the radio cache). | `main/telemetry/telemetry_v2.c:461, :515; iteration :463-514` |
| `data.lora_sensors[].battery` | number\|null | percent | 0..255 as emitted; sender intent 0..100 | Yes | always present; explicit null when there is no live cache entry | snapshot | Battery from the decrypted LoRa frame. | `main/telemetry/telemetry_v2.c:499 (value), :505 (null)` |
| `data.lora_sensors[].connected` | bool | n/a | true \| false | No | always for each element | snapshot | Heard within the LoRa health timeout. Also gates whether the cached radio values below are merged at all. | `main/telemetry/telemetry_v2.c:469; merge gate :485` |
| `data.lora_sensors[].last_seen_age_s` | number\|null | seconds | >= 0 when a number | Yes | always present; explicit null when UINT32_MAX (never heard) | snapshot | Seconds since this sensor last checked in. | `main/telemetry/telemetry_v2.c:473-478` |
| `data.lora_sensors[].leak_state` | bool | n/a | true \| false | Yes | always present; hard-coded false (NOT null) when there is no live cache entry | snapshot | Leak flag. DERIVED WITH A DIFFERENT TEST from the leak event: the snapshot uses leak_status == 1 while the event uses leak_status != 0, so a sensor reporting 2 would auto-close the valve yet read leak_state:false in every snapshot. | `main/telemetry/telemetry_v2.c:500-501 (== 1) vs main/iothub/app_iothub.c:1750 (!= 0); no-cache placeholder :506` |
| `data.lora_sensors[].location` | object | n/a | n/a | Yes | always for each element (never omitted) | snapshot | Installed-location metadata container. ALWAYS emitted here, unlike the omit-when-unknown convention used by the rules engine's auto_close event. | `main/telemetry/telemetry_v2.c:511 -> builder :143-153` |
| `data.lora_sensors[].location.code` | string | n/a | enum, 13 values: unknown \| bathroom \| kitchen \| laundry \| garage \| garden \| basement \| utility \| hallway \| bedroom \| living_room \| attic \| outdoor | No | always for each element | snapshot | Machine-readable room code from the sensor_meta table; "unknown" when no metadata is stored. | `main/telemetry/telemetry_v2.c:148; table main/sensor_meta/sensor_meta.c:23-27` |
| `data.lora_sensors[].location.label` | string | n/a | free text, max 31 chars | No | always for each element; "" when no metadata is stored | snapshot | User-entered free-text location label. | `main/telemetry/telemetry_v2.c:151; max main/sensor_meta/sensor_meta.h:12` |
| `data.lora_sensors[].rating` | string | n/a | enum: excellent \| good \| warning \| critical \| unknown | No | always for each element | snapshot | Health rating from timeout + battery + RSSI thresholds. | `main/telemetry/telemetry_v2.c:470-471; compute main/health_engine/health_engine.c:115-143` |
| `data.lora_sensors[].rssi` | number\|null | dBm | int8_t: -128..127 (negative is normal; 67..127 indicates a wrapped value) | Yes | always present; explicit null when there is no live cache entry | snapshot | Received signal strength of the last LoRa frame. Can be POSITIVE (67..127) because the SX127x driver's int result is narrowed to int8_t, wrapping any true RSSI below -128 dBm. | `main/telemetry/telemetry_v2.c:502 (value), :507 (null); narrowing main/app_lora/app_lora.cpp:274; computation main/app_lora/lora.cpp:387-412` |
| `data.lora_sensors[].sensor_id` | string | n/a | "0x" + 8 uppercase hex, e.g. 0x754A6237 | No | always for each element | snapshot | LoRa sensor id as rendered by the health engine. | `main/telemetry/telemetry_v2.c:468` |
| `data.lora_sensors[].snr` | number\|null | dB | -32.00..+31.75 in 0.25 steps | Yes | always present; explicit null when there is no live cache entry | snapshot | LoRa signal-to-noise ratio. The ONLY non-integer number on the whole wire: quarter-dB steps, serialized bare (e.g. 9) when integral and as 9.25 / 9.5 / 9.75 otherwise. | `main/telemetry/telemetry_v2.c:503 (value), :508 (null); source main/app_lora/lora.cpp:417-422; encoding [EXTERNAL] $IDF_PATH/components/json/cJSON/cJSON.c:611-625` |
| `data.offline_duration_s` | number | seconds | >= 1 when present | Yes | conditional: OMITTED when the value is 0 | health_event | How long the device had been unheard. Omitted (not zero) when unknown. | `main/health_engine/health_engine.c:601-603` |
| `data.override_active` | bool | n/a | true \| false | No | always (snapshot only) | snapshot | Is the 24 h water-access override window open (auto-close suppressed). | `main/telemetry/telemetry_v2.c:594` |
| `data.override_cancelled` | bool | n/a | always true when present | Yes | conditional: OMITTED when no override window was open | rules_event | A leak_reset also tore down an open override window. | `main/rules_engine/rules_engine.c:679-681` |
| `data.override_remaining_s` | number | seconds | >= 0 when present | Yes | snapshot: OMITTED unless override_active AND remaining >= 0. auto_close_blocked_override event: OMITTED when remaining < 0. | rules_event, snapshot | Seconds left in the override window. Reports the FULL window duration when the clock is unsynced, and 0 when expired but not yet processed by the rules tick. | `main/telemetry/telemetry_v2.c:595-597; main/rules_engine/rules_engine.c:472-474; semantics main/rules_engine/rules_engine.c:1187-1200` |
| `data.prev_rating` | string | n/a | enum: excellent \| good \| warning \| critical \| unknown | No | always (health alert events only) | health_event | The device's rating immediately before the transition, so the backend can tell degradation from recovery. | `main/health_engine/health_engine.c:593` |
| `data.previous_remaining_s` | number | seconds | >= 0 | No | always (auto_close_reenabled only) | rules_event | How much of the override window was discarded when it was cancelled early. | `main/rules_engine/rules_engine.c:721; value returned by cancel_override_window() at :715` |
| `data.provisioned` | bool | n/a | true \| false | No | always (lifecycle only) | lifecycle | Whether the hub currently has a commissioning record. Note: the whole publish path is gated on provisioned, so in practice only true is ever observed here. | `main/iothub/app_iothub.c:1674-1688 gate; field main/telemetry/telemetry_v2.c:342` |
| `data.rating` | string | n/a | enum: excellent \| good \| warning \| critical \| unknown | No | always (health alert events only) | health_event | The device's NEW health rating that triggered the alert. | `main/health_engine/health_engine.c:592; mapping :57-66` |
| `data.raw` | string | n/a | n/a | Yes | conditional: OMITTED unless cJSON_Parse of the engine JSON fails | health_event, rules_event | Verbatim rules-/health-engine JSON string, emitted only when re-parsing that string fails. The string was itself produced by cJSON_PrintUnformatted, so this is reachable only on allocation failure. | `main/telemetry/telemetry_v2.c:669 (rules), :688 (health); guarded by :662, :682` |
| `data.reason` | string | n/a | enum, 7 values total. snapshot: heartbeat \| event \| commission \| boot \| fast \| decommission. rules event: c2d_command | Yes | snapshot: always in practice; rules event: always | rules_event, snapshot | TWO DISTINCT MEANINGS ON ONE PATH. In a snapshot it is the trigger that caused the snapshot. In the auto_close_reenabled rules event it is the cause of the override cancellation. | `snapshot main/telemetry/telemetry_v2.c:383 with main/iothub/app_iothub.c:581-591 and :1570; rules event main/rules_engine/rules_engine.c:722` |
| `data.remaining_s` | number | seconds | 86400 in a production build (compile-overridable via -D OVERRIDE_WINDOW_DURATION_S) | No | always (water_access_override_enabled only) | rules_event | Full override window duration at the moment the window was started or refreshed. A CONSTANT, not a countdown; the countdown is override_remaining_s. | `main/rules_engine/rules_engine.c:218; constant main/rules_engine/rules_engine.c:28-30` |
| `data.reset_reason` | string | n/a | enum: power_on \| software \| panic \| watchdog \| brownout \| deep_sleep \| unknown | No | always (lifecycle only) | lifecycle | Why the hub last rebooted. Lossy: it collapses the 16-member esp_reset_reason_t onto 7 strings, so 9 causes (including an external-pin reset) all report "unknown". | `main/telemetry/telemetry_v2.c:341; mapping :128-141` |
| `data.rmleak` | bool | n/a | true \| false | No | always (valve events only) | valve_event | Valve remote-leak interlock latch at the moment of a valve event. | `main/telemetry/telemetry_v2.c:621` |
| `data.rmleak_asserted` | bool | n/a | always true when present | No | always (auto_close events only) | rules_event | Confirms the hub asserted the valve's RMLEAK interlock as part of the auto-close. Hardcoded true at both sites. | `main/rules_engine/rules_engine.c:336, :938` |
| `data.rssi` | number | dBm | int8_t: -128..127 (LoRa values may be wrapped positive, see lora_sensors[].rssi) | Yes | always (leak events); conditional in health alerts (OMITTED when 0) | health_event, leak_event | Signal strength of the reporting peer. In a health alert the key is omitted when 0, and the valve NEVER has an RSSI recorded, so a valve health alert never carries this field. | `main/telemetry/telemetry_v2.c:646; main/health_engine/health_engine.c:598-600; valve never sets last_rssi (main/health_engine/health_engine.c:249, :267 are the only writers)` |
| `data.rules` | object | n/a | n/a | Yes | conditional: OMITTED if provisioning_get_rules_config() fails (mutex timeout / not initialised) | lifecycle, snapshot | Global auto-close configuration container. Byte-identical shape in lifecycle and snapshot so the backend reuses one parser. | `main/telemetry/telemetry_v2.c:363 (lifecycle), :582 (snapshot); guard main/provisioning_manager/provisioning_manager.c:977-990` |
| `data.rules.auto_close_enabled` | bool | n/a | true \| false | Yes | present whenever data.rules is present | lifecycle, snapshot | Master switch: does a leak automatically close the valve. | `main/telemetry/telemetry_v2.c:361, :580` |
| `data.rules.trigger_mask` | number | bitmask | 0..255 on the wire (only bits 0-2 are interpreted) | Yes | present whenever data.rules is present | lifecycle, snapshot | Which leak sources may trigger auto-close. bit0=BLE leak, bit1=LoRa, bit2=valve flood. NOTHING masks or validates the inbound value, so the emitted range is 0..255, not 0..7. | `main/telemetry/telemetry_v2.c:362, :581; bits main/provisioning_manager/provisioning_manager.h:22-25; unmasked cast main/rules_engine/rules_engine.c:562 and main/provisioning_manager/provisioning_manager.c:486` |
| `data.sensor_id` | string | n/a | "0x"+8hex \| colon MAC \| "valve" \| "unknown" | No | always in leak events, auto_close, auto_close_blocked_override and health alerts | health_event, leak_event, rules_event | Identifies the sensor. LoRa uses 0x+8hex, BLE uses a colon MAC, the valve's own flood sensor uses the literal "valve", and an unresolved id becomes "unknown". | `main/telemetry/telemetry_v2.c:643; main/rules_engine/rules_engine.c:335, :471, :936; main/health_engine/health_engine.c:591; "valve" literal main/iothub/app_iothub.c:1650` |
| `data.source_type` | string | n/a | enum: lora \| ble_leak_sensor \| valve_flood \| reconnect \| unknown | No | always in leak events and in the auto_close / auto_close_blocked_override rules events | leak_event, rules_event | Which transport/peer class the leak came from. "reconnect" is a pseudo-source used only by the valve-reconnect auto_close. | `main/telemetry/telemetry_v2.c:642 (leak events, "lora" \| "ble_leak_sensor" from main/iothub/app_iothub.c:1749, :1809); main/rules_engine/rules_engine.c:334, :470 via source_to_str main/rules_engine/rules_engine.c:259-267; reconnect variant main/rules_engine/rules_engine.c:935` |
| `data.status` | string | n/a | enum: ok \| error | No | always (cmd_ack only) | cmd_ack | Did the command succeed. | `main/telemetry/telemetry_v2.c:708` |
| `data.system_health` | object | n/a | n/a | No | always (snapshot only) | snapshot | Fleet roll-up container: the single worst device rating plus a human-readable cause. | `main/telemetry/telemetry_v2.c:392, :402` |
| `data.system_health.rating` | string | n/a | enum: excellent \| good \| warning \| critical \| unknown | No | always (snapshot only) | snapshot | Worst rating across all provisioned devices. Does NOT track leak state, so it can read "excellent" while the hub's fleet LED is RED for a valve flood. | `main/telemetry/telemetry_v2.c:391-394; source main/health_engine/health_engine.c:57-66; LED asymmetry main/rgb/fleet_led.c:149-156` |
| `data.system_health.reason` | string | n/a | free text, max 127 chars; "All devices healthy" when rating is excellent; "Degraded" fallback; "Health data unavailable" on a health mutex timeout | No | always (snapshot only) | snapshot | Comma-joined human-readable cause list built from the devices sitting at the system rating. Free text intended for display, not parsing. | `main/telemetry/telemetry_v2.c:395-401; builder :157-231; fallback "Health data unavailable" :399` |
| `data.trigger` | string | n/a | enum: button \| c2d_command | No | always (water_access_override_enabled only) | rules_event | What opened the override window: a physical long-press on the valve, or the override_enable C2D command. | `main/rules_engine/rules_engine.c:216; callers :846 ("c2d_command"), :978 and :1128 ("button")` |
| `data.valve` | object | n/a | n/a | No | always (snapshot only) | snapshot | Live valve state container. Always present even when the valve is disconnected. | `main/telemetry/telemetry_v2.c:405, :458` |
| `data.valve.battery` | number | percent | 0..255 as emitted; header intent is 0..100 | Yes | conditional: OMITTED when the valve is disconnected | snapshot | Valve battery from GATT byte 0. Reads 0 between CONNECTED and GATT-setup completion, indistinguishable from a genuinely flat battery. | `main/telemetry/telemetry_v2.c:431; cache init main/ble_valve/app_ble_valve.c:134, reset :1209` |
| `data.valve.connected` | bool | n/a | true \| false | No | always (snapshot only) | snapshot | Whether the valve GATT link is currently up. The authoritative liveness flag; there is NO valve_disconnected event. | `main/telemetry/telemetry_v2.c:434 (true), :443 (false)` |
| `data.valve.fw_version` | string\|null | n/a | free text, max 31 chars | Yes | THREE-STATE: string when read; explicit null when connected but DIS read failed; key ABSENT when disconnected | snapshot | Valve firmware revision from the BLE Device Information Service, copied verbatim with no format validation. | `main/telemetry/telemetry_v2.c:436-440 (string or null), and absent via the :441 else-branch` |
| `data.valve.last_seen_age_s` | number\|null | seconds | >= 0 when a number | Yes | THREE-STATE: number when known; explicit null when UINT32_MAX (never seen); key ABSENT when the valve has no health record | snapshot | Seconds since the valve was last heard from. | `main/telemetry/telemetry_v2.c:450-455` |
| `data.valve.leak_state` | bool | n/a | true \| false | Yes | conditional: OMITTED when the valve is disconnected | snapshot | Water detected AT THE VALVE body (its own flood sensor), distinct from the remote leak sensors. | `main/telemetry/telemetry_v2.c:432; source main/ble_valve/app_ble_valve.c:474 (data[0] != 0)` |
| `data.valve.mac` | string | n/a | colon-separated MAC | Yes | conditional: OMITTED when neither the live BLE link nor a health record has a MAC | snapshot | Valve BLE MAC. Prefers the live link, falls back to the provisioned health record so a disconnected valve still identifies itself. | `main/telemetry/telemetry_v2.c:420-425` |
| `data.valve.rating` | string | n/a | enum: excellent \| good \| warning \| critical \| unknown | Yes | conditional: OMITTED when the valve has no health record | snapshot | Valve health rating from the health engine (connection + battery), independent of leak state. | `main/telemetry/telemetry_v2.c:446-449` |
| `data.valve.rmleak` | bool | n/a | true \| false | Yes | conditional: OMITTED when the valve is disconnected | snapshot | The valve's remote-leak interlock latch. While true the valve refuses to open, and the hub refuses to forward a valve_open command. | `main/telemetry/telemetry_v2.c:433; source main/ble_valve/app_ble_valve.c:482; open-refusal main/iothub/app_iothub.c:536-543` |
| `data.valve.state` | string | n/a | enum: open \| closed \| unknown \| disconnected (unknown = characteristic byte is neither 0 nor 1) | No | always (snapshot only) | snapshot | Valve position. "disconnected" is emitted in place of a position when the BLE link is down, so this field mixes position and link state. | `main/telemetry/telemetry_v2.c:427-430 (connected), :442 (disconnected)` |
| `data.valve_mac` | string | n/a | colon-separated MAC, e.g. AA:BB:CC:DD:EE:FF | Yes | conditional: OMITTED when no valve MAC is provisioned | lifecycle | Provisioned valve BLE MAC (from NVS, not the live link). | `main/telemetry/telemetry_v2.c:344-346` |
| `data.valve_state` | string | n/a | enum: open \| closed \| unknown | No | always (valve events only) | valve_event | Valve position at the moment of a valve event. DIFFERENT PATH from data.valve.state and it never carries "disconnected". | `main/telemetry/telemetry_v2.c:616-618` |
| `gateway` | object | n/a | n/a | No | always | cmd_ack, health_event, leak_event, lifecycle, rules_event, snapshot, valve_event | Hub identity + liveness container. | `main/telemetry/telemetry_v2.c:78, :87` |
| `gateway.fw` | string | n/a | "1.8.0" in the working tree (CMakeLists.txt:12); "0.0.0" if the descriptor is empty | No | always | cmd_ack, health_event, leak_event, lifecycle, rules_event, snapshot, valve_event | Hub firmware version, read at runtime from the ESP-IDF app descriptor (PROJECT_VER). Never a hardcoded string. | `main/telemetry/telemetry_v2.c:84; main/telemetry/telemetry_v2.c:50-54; source CMakeLists.txt:12` |
| `gateway.id` | string | n/a | "GW-" + 12 uppercase hex | No | always | cmd_ack, health_event, leak_event, lifecycle, rules_event, snapshot, valve_event | Immutable hub id derived from the WiFi STA MAC in eFuse. Same string used as the DPS registration id and the MQTT client id. | `main/telemetry/telemetry_v2.c:79; derivation main/hub_identity/hub_identity.c:29-37` |
| `gateway.name` | string | n/a | free text, max 31 chars (HUB_NAME_MAX_LEN) | Yes | conditional: OMITTED when the stored name is empty | cmd_ack, health_event, leak_event, lifecycle, rules_event, snapshot, valve_event | User-assigned hub name from twin desired hub_name or C2D set_hub_name. NOTE the opposite convention on the twin plane, where hub_name is always emitted as "". | `main/telemetry/telemetry_v2.c:81-83; twin counterpart main/iothub/app_iothub.c:956` |
| `gateway.short_id` | string | n/a | 4 uppercase hex chars | No | always | cmd_ack, health_event, leak_event, lifecycle, rules_event, snapshot, valve_event | Last 2 bytes of the STA MAC as 4 hex chars. Also the SoftAP SSID suffix (WiFi-Hub-XXXX) the installer matches during setup. | `main/telemetry/telemetry_v2.c:80; main/hub_identity/hub_identity.c:33-37` |
| `gateway.uptime_s` | number | seconds | >= 0 | No | always | cmd_ack, health_event, leak_event, lifecycle, rules_event, snapshot, valve_event | Seconds since boot from the monotonic esp_timer. On an offline-buffer replay this is the ORIGINAL build-time value, possibly from a previous boot. | `main/telemetry/telemetry_v2.c:85-86; replay main/offline_buffer/offline_buffer.c:148` |
| `schema` | string | n/a | Fixed literal "eflostop.v2" | No | always | cmd_ack, health_event, leak_event, lifecycle, rules_event, snapshot, valve_event | Wire-contract identifier. Constant; bump signals a breaking payload change. | `main/telemetry/telemetry_v2.c:64; constant main/telemetry/telemetry_v2.h:14` |
| `ts` | number | seconds (Unix epoch, UTC) | >= 1704067200 (2024-01-01T00:00:00Z) | No | always | cmd_ack, health_event, leak_event, lifecycle, rules_event, snapshot, valve_event | Device wall-clock at message BUILD time. The whole message is destroyed (never sent, never buffered) if ts < 1704067200, so a pre-SNTP leak event is lost permanently. | `main/telemetry/telemetry_v2.c:66-76; SNTP gate :70-74; threshold :57` |
| `type` | string | n/a | enum: "lifecycle" \| "snapshot" \| "event" | No | always | cmd_ack, health_event, leak_event, lifecycle, rules_event, snapshot, valve_event | Envelope discriminator. Tells the backend which `data` shape to expect. | `main/telemetry/telemetry_v2.c:89; values passed at :336, :374, :610, :637, :659, :679, :700` |

## 12. Categorised catalogue (index into §11)

### 12.1 Device identity & provisioning (13)

Who this hub is, and what it believes it is responsible for.

- `data`
- `data.ble_leak_sensor_count`
- `data.lora_sensor_count`
- `data.provisioned`
- `data.valve.mac`
- `data.valve_mac`
- `gateway`
- `gateway.id`
- `gateway.name`
- `gateway.short_id`
- `schema`
- `ts`
- `type`

### 12.2 Connectivity & network health (9)

Whether each peer is currently reachable, and how long since it was last heard.

- `data.ble_leak_sensors[].connected`
- `data.ble_leak_sensors[].last_seen_age_s`
- `data.ble_leak_sensors[].rssi`
- `data.lora_sensors[].connected`
- `data.lora_sensors[].last_seen_age_s`
- `data.lora_sensors[].rssi`
- `data.rssi`
- `data.valve.connected`
- `data.valve.last_seen_age_s`

### 12.3 System health & diagnostics (13)

The hub's own liveness and the roll-up health rating of the whole installation.

- `data.ble_leak_sensors[].rating`
- `data.category`
- `data.dev_type`
- `data.lora_sensors[].rating`
- `data.offline_duration_s`
- `data.prev_rating`
- `data.rating`
- `data.reset_reason`
- `data.system_health`
- `data.system_health.rating`
- `data.system_health.reason`
- `data.valve.rating`
- `gateway.uptime_s`

### 12.4 Firmware, OTA & configuration state (7)

Software versions across the hub and its peers, and the rules configuration in force.

- `data.ble_leak_sensors[].fw_version`
- `data.fw_version`
- `data.rules`
- `data.rules.auto_close_enabled`
- `data.rules.trigger_mask`
- `data.valve.fw_version`
- `gateway.fw`

### 12.5 Leak detection & sensor telemetry (19)

The leak sensors themselves: identity, wet/dry state and physical location.

- `data.ble_leak_sensors`
- `data.ble_leak_sensors[].leak_state`
- `data.ble_leak_sensors[].location`
- `data.ble_leak_sensors[].location.code`
- `data.ble_leak_sensors[].location.label`
- `data.ble_leak_sensors[].sensor_id`
- `data.leak_state`
- `data.location`
- `data.location.code`
- `data.location.label`
- `data.lora_sensors`
- `data.lora_sensors[].leak_state`
- `data.lora_sensors[].location`
- `data.lora_sensors[].location.code`
- `data.lora_sensors[].location.label`
- `data.lora_sensors[].sensor_id`
- `data.lora_sensors[].snr`
- `data.sensor_id`
- `data.source_type`

### 12.6 Valve control & actuation telemetry (6)

The valve's position and its leak interlock.

- `data.rmleak`
- `data.valve`
- `data.valve.leak_state`
- `data.valve.rmleak`
- `data.valve.state`
- `data.valve_state`

### 12.7 Flow & water-usage metering (0)

EXPLICITLY ABSENT — this product has no flow sensing. No field in the contract reports volume, flow rate or consumption. Confirmed by exhausting the 91-field census; there is no flow-related code path in main/.

### 12.8 Power, battery & energy (4)

Battery level for every battery-powered device.

- `data.battery`
- `data.ble_leak_sensors[].battery`
- `data.lora_sensors[].battery`
- `data.valve.battery`

### 12.9 Alarms, faults & error codes (7)

The event discriminator and the command-failure surface.

- `data.cmd`
- `data.error`
- `data.error.code`
- `data.error.detail`
- `data.event`
- `data.id`
- `data.status`

### 12.10 Commissioning, lifecycle & user-linkage events (12)

Why a snapshot was sent, and the 24-hour water-access override window.

- `data.active_leak_count`
- `data.auto_close_resumed`
- `data.clear_after_seconds`
- `data.expires_ts`
- `data.override_active`
- `data.override_cancelled`
- `data.override_remaining_s`
- `data.previous_remaining_s`
- `data.reason`
- `data.remaining_s`
- `data.rmleak_asserted`
- `data.trigger`

### 12.11 Security & audit events (0)

EXPLICITLY ABSENT — no D2C telemetry field reports an authentication, authorisation or audit event. Security material (the SAS token and the DPS symmetric key) is used for transport authentication only and never appears in a payload; it is described in §3.3 and §18.

### 12.12 Diagnostic / debug-only telemetry (1)

The fallback escape hatch used when an internally generated payload cannot be re-parsed.

- `data.raw`

## 13. Device twin plane (NOT telemetry)

### 13.1 Twin reported (device to cloud)

| Key | Type | Presence | Meaning | Citation |
|---|---|---|---|---|
| fw_version | string | always | Hub firmware version, from the build-time PROJECT_VER. | main/iothub/app_iothub.c:953 |
| gateway_id | string | always | GW- prefixed hub identity. Same value as telemetry gateway.id, different key name. | main/iothub/app_iothub.c:954 |
| short_id | string | always | Last 4 hex of the gateway id. | main/iothub/app_iothub.c:955 |
| hub_name | string | always — emitted even when empty | User-assigned hub name. NOTE: telemetry omits gateway.name when empty; the twin always emits the key. | main/iothub/app_iothub.c:956 |
| provisioned | boolean | always | Whether the hub has completed commissioning. | main/iothub/app_iothub.c:957 |
| valve_mac | string | omitted when no valve is provisioned | The provisioned valve MAC. | main/iothub/app_iothub.c:961 |
| lora_sensor_count | number | always | How many LoRa sensors are commissioned. | main/iothub/app_iothub.c:966 |
| ble_leak_sensor_count | number | always | How many BLE leak sensors are commissioned. | main/iothub/app_iothub.c:971 |
| auto_close_enabled | boolean | omitted if the rules config cannot be read | Whether the hub will close the valve automatically on a leak. | main/iothub/app_iothub.c:975 |
| trigger_mask | number | omitted if the rules config cannot be read | Bitmask of which leak sources may trigger auto-close. bit0=BLE, bit1=LoRa, bit2=valve flood. | main/iothub/app_iothub.c:976 |
| uptime_s | number | always | Seconds since boot AT THE MOMENT THE TWIN WAS PATCHED. Only three triggers write the twin, so this can be arbitrarily stale — it is not a live value. | main/iothub/app_iothub.c:979 |
| free_heap | number | always | Free heap in bytes. The ONLY hub diagnostic that exists on the twin and nowhere in telemetry. | main/iothub/app_iothub.c:981 |

### 13.2 Twin desired (cloud to device)

| Key | Type | Effect | Citation |
|---|---|---|---|
| hub_name | string | Sets the user-assigned hub name. Written to NVS, then echoed into twin reported and into telemetry gateway.name. Max 31 characters; a longer value is rejected and logged, leaving the stored name unchanged. | main/iothub/app_iothub.c:1030-1040; length limit main/hub_identity/hub_identity.h:10 |
| snapshot_interval_s | number | Sets the snapshot heartbeat period. Range-checked against 60..3600; a value outside that range is DISCARDED with a warning, not clamped, so the previous interval stays in force. This is the only validated numeric on any plane. Held in RAM only, so it reverts to the 300 s default on every reboot, and it is NOT echoed into twin reported, so the cloud gets no acknowledgement that it was applied. | main/iothub/app_iothub.c:1019-1028; range check :1022; default main/telemetry/telemetry_v2.c:42 |

**Two inbound paths reach one stored value.** The hub name can be set either by writing the twin
desired property `hub_name`, or by sending the C2D command `set_hub_name`. Both write the same NVS value, and
that one value then surfaces on two outbound planes under two different key names, with two different presence
rules.

    INBOUND                                    STORED            OUTBOUND
    twin desired  hub_name  ──┐
                              ├──►  NVS "hub_ident"  ──┬──►  twin reported  hub_name   (always emitted)
    C2D  set_hub_name  ───────┘                        └──►  D2C  gateway.name         (OMITTED if empty)

Citations: twin desired path `main/iothub/app_iothub.c:1030-1040`; C2D command path `:908-935`; twin reported
emission `:956`; telemetry emission with its emptiness test `main/telemetry/telemetry_v2.c:82-83`.

Two consequences worth a moment of meeting time. A backend that implements only one inbound path will find the
other silently diverging in the field. And because telemetry omits the key when the name is empty while the twin
always emits it, "no name set" looks different depending on which plane you read.

## 14. Command plane (INBOUND)

| Command | What it does | Payload keys | Notes | Citation |
|---|---|---|---|---|
| valve_open | Open the valve. | none | Refused while the valve's RMLEAK interlock is latched — the hub returns an error rather than forwarding the request. | main/iothub/app_iothub.c:657-667; refusal guard :539-541 |
| valve_close | Close the valve. | none |  | main/iothub/app_iothub.c:668-673 |
| valve_set_state | Set the valve to an explicit state. | state |  | main/iothub/app_iothub.c:674-700 |
| leak_reset | Clear the leak interlock (RMLEAK) on the valve. | none | REFUSED while any leak source is still wet, to prevent restoring water during an active leak. No event is emitted on refusal. | main/iothub/app_iothub.c:701-713; guard main/rules_engine/rules_engine.c:645-650 |
| decommission | Remove a device, or everything, from the hub's commissioning. | target, sensor_id | target is one of valve, lora, ble or all. The legacy plain-text forms are normalised in-process into this payload shape. | main/iothub/app_iothub.c:714-814; normalisation main/commands/c2d_commands.c:161, :172-173, :192-193, :203 |
| override_cancel | Cancel an active 24-hour water-access override window. | none | Produces the auto_close_reenabled event. If leaks are still active the hub immediately re-closes the valve. | main/iothub/app_iothub.c:815-824; main/rules_engine/rules_engine.c:718-726 |
| override_enable | Start a 24-hour water-access override window remotely. | none | Has the largest set of rejection messages of any command. | main/iothub/app_iothub.c:825-849; rejection set :832-845 |
| rules_config | Set auto-close behaviour. | auto_close_enabled, trigger_mask, trigger_ble_leak, trigger_lora, trigger_valve_flood | trigger_mask is stored as a raw byte with no masking or range check, so any value 0..255 is accepted and echoed back on the wire. THREE convenience booleans also exist: trigger_ble_leak, trigger_lora and trigger_valve_flood each set or clear one bit of the mask. They are applied AFTER trigger_mask, so a payload carrying both has the individual booleans win on the bits they name. | main/iothub/app_iothub.c:850-858; raw mask main/rules_engine/rules_engine.c:560-562; convenience booleans :566, :574, :582 |
| sensor_meta | Set a sensor's location code and label. | sensor_type, sensor_id, location_code, label | Accepts three aliases for a BLE sensor: ble_leak_sensor, ble_leak and ble, case-insensitively. | main/iothub/app_iothub.c:859-873; aliases main/sensor_meta/sensor_meta.h:51-56 |
| provision | Commission the hub: valve, sensors and rules in one message. | valve_mac, lora_sensors, ble_leak_sensors, rules, sensor_meta | An optional inline sensor_meta ARRAY may be carried in the same payload, so location metadata can be set during commissioning without a second command. Each element is {sensor_type, sensor_id, location_code, label}. | main/iothub/app_iothub.c:874-907; inline metadata applied :887, parsed main/sensor_meta/sensor_meta.c:374, element fields :277-278, :294, :324, :338 |
| set_hub_name | Set the user-assigned hub name. | name | Max 31 characters. The same value is also settable via the twin desired property hub_name — see §13.3. | main/iothub/app_iothub.c:908-935 |

**An acknowledgement is not guaranteed.** A command produces a `cmd_ack` telemetry event only if
it arrived inside an envelope (`eflostop.cmd` or `eflostop.cmd.v1`) **or** carried a correlation id. The emit is
wrapped in `if (cmd.is_envelope || cmd.id[0])`, so a legacy plain-text command sent without a correlation id is
executed and then **silently produces nothing at all** — no success ack and no failure ack. A caller that treats
a missing ack as failure will be wrong for exactly that case.

The same gate wraps a second emit site: the decommission-all path acknowledges early, before the hub restarts,
rather than going through the normal end-of-dispatch ack.

There is no direct-method subscription anywhere in the firmware — a repo-wide search finds no
`$iothub/methods/POST` subscription. The IoT Hub connection makes exactly three subscriptions -
`main/iothub/app_iothub.c:1118` (C2D), `:1120` (twin responses) and `:1122-1123` (twin desired). A fourth
subscribe exists in the codebase, `main/dps_client/dps_client.c:269`, but it is on the DPS client and a
different broker, so it is not part of the hub connection. The transport therefore supplies no request/response correlation, and the
acknowledgement travels on the telemetry plane rather than the command plane.

Correlation is by the optional `data.id` field, echoed from the inbound command's correlation id. If the inbound
command carried no id, **the key is omitted entirely** — it is not an empty string. Note the interaction with the
gate above: a command with no id can only have been acked because it was enveloped, so an ack with no `data.id`
can only be matched by `data.cmd` and timing.

There is no machine-readable failure class. On failure, `data.error.code` is byte-identical to `data.cmd` — the
same C variable is written to both (`main/telemetry/telemetry_v2.c:707` and `:711`). The only discriminator is
the free-text `data.error.detail`. See F-06.

Two delivery caveats specific to acks, both consequences of riding the telemetry plane:

- An ack is published from the esp-mqtt event task rather than the main IoT Hub task, so it bypasses the
  provisioned gate and the snapshot scheduler and can interleave anywhere in the stream. The gate and emit are
  at `main/iothub/app_iothub.c:932-935`; the decommission-all early ack is at `:793-795`.
- **A command executed before the clock syncs has its ack destroyed** by the pre-epoch gate in `build_envelope`
  (`main/telemetry/telemetry_v2.c:69-74`). The command still ran. The cloud sees a timeout on an operation that
  actually took effect, which matters for any non-idempotent command — see F-03.

## 15. Provisioning plane (DPS)

None of these payloads appear in your telemetry ingest. DPS uses a different broker, a different
credential and a different topic namespace, and completes before the IoT Hub connection exists.

```json
{
  "registrationId": "GW-%02X%02X%02X%02X%02X%02X"
}
```

## 16. Trigger and cadence matrix

| Trigger | Plane | Produces | Fields affected | Citation |
|---|---|---|---|---|
| Timer, 300 s default | Telemetry | snapshot (reason: heartbeat) | The whole snapshot field set | Interval settable 60..3600 via twin desired, RAM only. main/telemetry/telemetry_v2.c:42; main/iothub/app_iothub.c:1022 |
| MQTT connect | Telemetry | offline-buffer drain, then lifecycle | Buffered events replayed verbatim, then the lifecycle field set | main/iothub/app_iothub.c:1693-1694 |
| MQTT connect | Twin reported | twin PATCH | All 12 twin keys | main/iothub/app_iothub.c:1695 |
| Boot | Telemetry | snapshot (reason: boot) | Full snapshot | main/iothub/app_iothub.c:586 |
| Valve ready | Telemetry | snapshot (reason: fast) | Full snapshot, incomplete by design | main/iothub/app_iothub.c:587 |
| Valve GATT notify, value changed | Telemetry | valve event | data.valve_state, battery, leak_state, rmleak, fw_version | Delta-gated; gate reset to -2 on reconnect. main/iothub/app_iothub.c:1782-1791, :1658 |
| BLE advertisement, leak state changed | Telemetry | leak event + coupled snapshot | data.sensor_id, leak_state, battery, rssi, location | main/iothub/app_iothub.c:1807-1811 |
| LoRa packet, leak state changed | Telemetry | leak event + coupled snapshot | Same leak-event field set | main/iothub/app_iothub.c:1747-1750 |
| Rules engine decision | Telemetry | rules event + coupled snapshot | Varies by event; see §10 | main/rules_engine/rules_engine.c, seven emit sites |
| Health tick, 30 s, rating crossed critical | Telemetry | health event + one coupled snapshot per burst | data.category, event, dev_type, sensor_id, rating, prev_rating, battery, rssi, offline_duration_s | main/health_engine/health_engine.c:577-608; drain main/iothub/app_iothub.c:1720-1730 |
| Inbound C2D command | Telemetry | cmd_ack | data.id, cmd, status, error | Published from the esp-mqtt event task. main/iothub/app_iothub.c:932-935 |
| Provisioning change | Telemetry | snapshot (reason: commission) | Full snapshot | main/iothub/app_iothub.c:585 |
| decommission_all command | Telemetry | snapshot (reason: decommission) | Full snapshot, published out of band immediately before restart | main/iothub/app_iothub.c:1570 |
| set_hub_name success | Twin reported | twin PATCH | All 12 twin keys | main/iothub/app_iothub.c:922 |
| Any parseable desired patch | Twin reported | twin PATCH | All 12 twin keys | main/iothub/app_iothub.c:1045 |
| Cloud writes a desired property | Twin desired | inbound only | hub_name, snapshot_interval_s | Delivered only while subscribed; never fetched on connect. main/iothub/app_iothub.c:1122-1123 |
| First boot, or NVS cache miss | Provisioning | DPS register, then poll every 3 s | registrationId | main/dps_client/dps_client.c:272-312 |

## 17. Payload volume

This analysis covers the telemetry plane. The twin plane is negligible by comparison — twelve flat
keys written on only three triggers — and the command plane is inbound. Figures are compact JSON as produced by
cJSON_PrintUnformatted (main/telemetry/telemetry_v2.c:103), excluding MQTT and TLS framing.

```
Snapshots at the 300 s default heartbeat:

    86,400 s/day / 300 s = 288 snapshots/day
    288 x 2,162 B        = 622,656 B/day  (about 608 KiB/day)

Of that, the invariant and derivable content identified in §5 accounts for:

    288 x 902 B          = 259,776 B/day  (about 254 KiB/day, roughly 42%)

Events are additional and depend entirely on installation activity. A quiet installation adds only the
per-connect lifecycle message; an installation with an intermittent sensor can add far more, because each
health transition emits an event and pulls a coupled snapshot with it.
```

## 19. Findings, gaps and risks

### F-01 [High] A sensor that was wet and then goes silent reports leak_state:false in the next snapshot

When the cache merge is skipped the snapshot writes the literal false into leak_state, while writing null into battery, rssi and fw_version in the same object. The merge is gated on the health engine's `connected` flag, which is itself a freshness test: connected is true only while the device has been heard within its 600 s timeout (main/health_engine/health_engine.c:640-648, timeout :16-17 of the header). So a sensor silent for more than ten minutes has its cached leak state dropped, and the field reads dry rather than unknown. A sensor that reported a leak and then went offline therefore flips to leak_state:false, while the leak_detected event already published for it is never retracted. A consumer that treats snapshots as authoritative will silently clear a live leak. Note that last_seen_age_s is emitted before the cache lookup and so keeps its real numeric value — a large last_seen_age_s next to leak_state:false is the signature of this case, and is the only in-payload contradiction available to a consumer.

*Defect — `main/telemetry/telemetry_v2.c:506, :562; merge gate :485-496, :541-552; last_seen_age_s emitted earlier at :473-478, :530-535`*

### F-02 [High] LoRa leak state is derived two different ways, so a value of 2 auto-closes the valve while every snapshot reports dry

The event path tests `pkt.leakStatus != 0`; the snapshot path tests `cached->leak_status == 1`. A LoRa sensor reporting leak_status == 2 triggers leak_detected and an auto-close, yet reads leak_state:false in every subsequent snapshot. The BLE path does not have this mismatch — both sides derive from a bool set with != 0.

*Defect — `main/iothub/app_iothub.c:1750 versus main/telemetry/telemetry_v2.c:501; BLE comparison main/ble_leak_scanner/app_ble_leak.c:176-178`*

### F-03 [High] All telemetry produced before the clock syncs is destroyed rather than buffered, including leak events and command acks

build_envelope() returns NULL when the wall clock is below epoch 1704067200, and publish_json() returns immediately on a NULL root — before reaching the branch that would have buffered the message for replay. Every message type is affected. For a command ack this means the cloud sees a timeout on a command that actually executed, which is a hazard for any non-idempotent command. The twin plane has no such gate, so a hub can be patching its twin while its telemetry is being discarded.

*Defect — `main/telemetry/telemetry_v2.c:69-74, :99-122`*

### F-04 [Medium] Valve disconnection produces no D2C event

g_valve_mac is zeroed before notify_hub_update(BLE_UPD_DISCONNECTED) is called, so the subsequent MAC lookup fails, mac_ok is false, and the entire valve-event block is skipped. Valve loss is observable only through the next snapshot's connected:false and a health device_offline event.

*Defect — `main/ble_valve/app_ble_valve.c:1271-1272; skipped block main/iothub/app_iothub.c:1761-1796`*

### F-05 [Medium] trigger_mask is accepted and echoed as an unvalidated raw byte

The inbound path stores (uint8_t)valueint with no masking and no range check, and the outbound path echoes it verbatim, so the wire range is 0..255 even though only bits 0-2 are ever interpreted. A cloud consumer cannot assume the value is within the documented three-bit domain.

*Defect — `main/provisioning_manager/provisioning_manager.c:484-486; emitted main/telemetry/telemetry_v2.c:362, :581; interpreted main/rules_engine/rules_engine.c:432`*

### F-06 [Medium] data.error.code is not an error code — it is the command name repeated

The same C variable is written to data.cmd and to data.error.code. There is no numeric or symbolic failure class anywhere on this path; the only discriminator is free-text detail. A consumer cannot switch on error.code to classify a failure.

*Observation — `main/telemetry/telemetry_v2.c:707, :711`*

### F-07 [Medium] A desired property written while the hub is offline is never fetched

The hub never publishes $iothub/twin/GET — no such string exists in main/. It only receives desired-property patches pushed while it is subscribed. Combined with snapshot_interval_s being RAM-only, a configured interval is lost on every reboot and is never re-read.

*Defect — `verified absent in main/; desired-property subscribe main/iothub/app_iothub.c:1122-1123; interval default main/telemetry/telemetry_v2.c:42`*

### F-08 [Medium] snapshot_interval_s is accepted but never acknowledged

The value is validated and applied but is not echoed into twin reported, so a cloud caller has no confirmation that the setting took effect, and no way to read back the value currently in force.

*Observation — `main/iothub/app_iothub.c:1019-1028 versus the reported set at :953-982`*

### F-09 [Low] Three watchdog reset causes collapse into one wire value

ESP_RST_INT_WDT, ESP_RST_TASK_WDT and ESP_RST_WDT all map to the string watchdog, so a hung task cannot be distinguished from a blocked interrupt handler. The default: branch additionally absorbs an external pin reset, which then reports as unknown.

*Observation — `main/telemetry/telemetry_v2.c:134-136, :139`*

### F-10 [Low] g_twin_rid is a non-atomic int shared across twin patches

The request id for twin PATCH topics is incremented without synchronisation. Twin patches are published from more than one context, so concurrent patches could in principle collide on a request id.

*Observation — `main/iothub/app_iothub.c:171, :990-991`*

### F-11 [Low] nvs_save_cache does not check individual nvs_set_* return values

The DPS cache write does not test the result of each nvs_set_* call before marking the cache valid, so a partial write could be recorded as complete.

*Observation — `main/dps_client/dps_client.c:116-129 (nvs_save_cache); unchecked writes :122-126`*

### F-12 [Low] Valve battery reads 0 between BLE connect and GATT setup completion

The cached value is initialised to 0 and reset on disconnect, and that 0 is emitted verbatim. It is indistinguishable on the wire from a genuinely flat battery. The health-alert path converts a 0xFF sentinel to an omitted key, but this 0 is not converted.

*Observation — `main/ble_valve/app_ble_valve.c:134, :1209; emitted main/telemetry/telemetry_v2.c:431, :619`*

### F-13 [Low] A positive rssi on a LoRa sensor is a wrapped value, not a strong signal

The LoRa path narrows an int to int8_t, so a true RSSI below -128 dBm wraps into the positive 0..127 range.

*Observation — `main/app_lora/app_lora.cpp:274`*

### F-17 [High] A legacy plain-text command with no correlation id is executed but never acknowledged

Both cmd_ack emit sites are wrapped in `if (cmd.is_envelope || cmd.id[0])`. A command that arrives in the legacy plain-text form AND carries no correlation id is therefore dispatched and acted on, but produces no acknowledgement of any kind — not a success ack and not a failure ack. A cloud caller that treats a missing ack as failure will conclude the command failed when it actually succeeded, and may retry a non-idempotent operation such as decommission.

*Defect — `main/iothub/app_iothub.c:932-935 (end-of-dispatch ack), :793-795 (decommission-all early ack); legacy text parsing main/commands/c2d_commands.c:161, :172-173, :192-193, :203`*

### F-15 [Medium] An offline event larger than 512 bytes is truncated and then replayed as invalid JSON

offline_buffer_store silently truncates any payload above OFFLINE_BUF_MAX_JSON_LEN to exactly 512 bytes, and the drain republishes the stored bytes verbatim. A truncated JSON object cannot parse, so such a replayed message will be rejected by any cloud-side parser rather than arriving in a degraded form. Typical leak and valve events are well under the limit — roughly 320 B and 260 B — so this is reachable only for the largest events, such as a cmd_ack carrying one of the longer override_enable rejection messages, or an event with a full-length location label. Only buffered (offline) messages are affected; an online publish is not truncated.

*Defect — `main/offline_buffer/offline_buffer.c:77-81, :148; limit main/offline_buffer/offline_buffer.h:13`*

### F-16 [Low] The offline buffer holds 16 events; older events are lost in a longer outage

OFFLINE_BUF_MAX_ENTRIES is 16. An outage producing more than 16 events does not preserve them all, so a cloud consumer cannot assume the replayed sequence after a reconnect is complete. Snapshots and lifecycle messages are not buffered at all — only type=="event" is stored.

*Observation — `main/offline_buffer/offline_buffer.h:12; buffering condition main/telemetry/telemetry_v2.c:115-122`*

### F-14 [Low] Three deprecated serializers for a previous wire shape remain in the image

build_valve_delta_json, build_lora_delta_json and build_ble_leak_delta_json build a flat gatewayID + devices[] shape. All three are marked (DEPRECATED) and __attribute__((unused)) and have no callers, so the old shape cannot reach the wire. They are listed because a backend may still hold parsers for that shape.

*Observation — `main/iothub/app_iothub.c:397-399, :441-443, :483-485`*

### 19.1 The previous wire shape — unreachable paths

Three serializers for a previous wire shape remain in the image. All three are marked
`(DEPRECATED)` and `__attribute__((unused))`, and a search for their names finds only their definitions —
no caller anywhere. They cannot reach the wire, so none of the paths below is part of the 91-field census.

They are listed because a backend may still hold parsers for this shape from an earlier firmware
generation. Note how different it is from the current contract: a flat `gatewayID` rather than a nested
`gateway` object, a `devices` array wrapping everything, and no envelope at all — no `schema`, no `ts`,
no `type`.

| Unreachable path | Citation |
|---|---|
| `gatewayID` | `main/iothub/app_iothub.c:402, :447, :489` |
| `devices` | `main/iothub/app_iothub.c:405, :450, :492` |
| `devices[].valve` | `main/iothub/app_iothub.c:411` |
| `devices[].valve.valve_mac` | `main/iothub/app_iothub.c:416, :431 (null variant)` |
| `devices[].valve.battery` | `main/iothub/app_iothub.c:417` |
| `devices[].valve.leak_state` | `main/iothub/app_iothub.c:418` |
| `devices[].valve.rmleak` | `main/iothub/app_iothub.c:419` |
| `devices[].valve.valve_state` | `main/iothub/app_iothub.c:423, :425, :427, :432` |
| `devices[].leak_sensors` | `main/iothub/app_iothub.c:456` |
| `devices[].leak_sensors.<sensor_0xNNNNNNNN>.battery` | `main/iothub/app_iothub.c:464` |
| `devices[].leak_sensors.<sensor_0xNNNNNNNN>.leak_state` | `main/iothub/app_iothub.c:465` |
| `devices[].leak_sensors.<sensor_0xNNNNNNNN>.rssi` | `main/iothub/app_iothub.c:466` |
| `devices[].leak_sensors.<sensor_0xNNNNNNNN>.location` | `main/iothub/app_iothub.c:475` |
| `devices[].leak_sensors.<sensor_0xNNNNNNNN>.location.code` | `main/iothub/app_iothub.c:472` |
| `devices[].leak_sensors.<sensor_0xNNNNNNNN>.location.label` | `main/iothub/app_iothub.c:474` |
| `devices[].ble_leak_sensors` | `main/iothub/app_iothub.c:498` |
| `devices[].ble_leak_sensors.<MAC>.battery` | `main/iothub/app_iothub.c:506` |
| `devices[].ble_leak_sensors.<MAC>.leak_state` | `main/iothub/app_iothub.c:507` |
| `devices[].ble_leak_sensors.<MAC>.rssi` | `main/iothub/app_iothub.c:508` |
| `devices[].ble_leak_sensors.<MAC>.location` | `main/iothub/app_iothub.c:515` |
| `devices[].ble_leak_sensors.<MAC>.location.code` | `main/iothub/app_iothub.c:512` |
| `devices[].ble_leak_sensors.<MAC>.location.label` | `main/iothub/app_iothub.c:514` |

### 19.2 Gaps — what this repository cannot answer

| Gap | Why it matters | Status |
|---|---|---|
| Deployed fleet composition | How many hubs are fielded, how many are in inventory, and whether a physical reflash path exists via the production tool. This determines how much the no-OTA constraint actually costs. Not determinable from this repository. | [UNVERIFIED] |
| Backend storage contents | Which fields are already persisted cloud-side and for how long. Determines what a rename breaks in history. | [UNVERIFIED] |
| Actual app consumption | Which fields the Watts Digital app reads today. No app source is in this repository, so no field can be declared safe to change on the evidence available here. | [UNVERIFIED] |
| auto_close dual shape intent | Whether the two structurally different auto_close payloads are deliberate. No source comment reconciles them and no version marker distinguishes them. | [UNVERIFIED] |
| LoRa NaN reachability | Whether the LoRa driver can produce a NaN SNR, which cJSON would print as the bare token null. snr is the only float on the path. Not traced. | [UNVERIFIED] |
| Production tool source | A separate PyQt6 project. Only its UART boot-log contract is visible from this repository. | [UNVERIFIED] |

### 19.3 Security material

No credential value is reproduced in this document. Two pieces of security material exist as
literals in firmware source and are referenced here by kind and location only:

- `<redacted: DPS ID scope>` — `main/iothub/app_iothub.h:30`, the AZURE_DPS_ID_SCOPE definition.
- `<redacted: DPS group symmetric key>` — `main/iothub/app_iothub.h:31`, the AZURE_DPS_GROUP_KEY definition.

The per-device key is derived at runtime by HMAC-SHA256 over the registration id using the group key
(`main/dps_client/dps_client.c:139-177`) and cached in NVS. A SAS token is constructed from it for MQTT
authentication (`main/iothub/app_iothub.c:219-254`). None of this material appears in any telemetry, twin or
command payload. That a group key is embedded in firmware is noted here as a security observation for the
team's own consideration; it is out of scope for the telemetry consolidation itself.

## Appendix D Glossary

| Term | Meaning |
|---|---|
| adv | Advertisement — a BLE broadcast packet. Leak sensors advertise rather than connecting. |
| BAS | Battery Service — a standard BLE service defining a 0-100 percent battery level characteristic. |
| C2D | Cloud to device — a message sent from IoT Hub to the device. |
| cmd | Command. |
| D2C | Device to cloud — a telemetry message sent from the device to IoT Hub. |
| dev_type | Device type — which class of peer a health event refers to. |
| DIS | Device Information Service — a standard BLE service. The hub reads the valve's firmware revision string from it. |
| DNP | Do not populate — a component present in the design but deliberately not fitted to the board. The LoRa radio is DNP on the current PCBA. |
| DPS | Device Provisioning Service — the Azure service a device contacts once to discover which IoT Hub it belongs to. |
| fw | Firmware version. |
| GATT | Generic Attribute Profile — the BLE mechanism for reading, writing and subscribing to values on a connected peer. Used for the valve. |
| gw | Gateway — this hub. |
| loc | Location. |
| MQTT | The publish/subscribe protocol used for all cloud communication here, over TLS. |
| NVS | Non-volatile storage — the ESP32's key/value store in flash. Survives reboot. |
| ovr | Override — the 24-hour water-access override window. |
| plane | One of the four independent communication channels between the hub and the cloud: telemetry, twin, command and provisioning. Each has its own topic, direction and delivery semantics. |
| prov | Provisioning. |
| RMLEAK | Remote leak interlock — a latch on the valve. While it is set the valve refuses to open, which is how an automatic leak shut-off is prevented from being immediately undone. |
| RSSI | Received signal strength indication, in dBm. Always negative in normal operation. |
| SAS | Shared access signature — the time-limited token used to authenticate the MQTT connection. |
| SNR | Signal-to-noise ratio, in dB. LoRa only. |
| SNTP | Simple Network Time Protocol — how the hub learns the wall-clock time. Until it completes, telemetry is discarded. |
| snapshot | The message type carrying the full current state of the hub and every device it knows about. |
| ts | Timestamp — Unix epoch seconds, UTC. |
| twin | The device twin — a JSON document held by IoT Hub, with a reported half written by the device and a desired half written by the cloud. |

## Appendix D.1 Enumerated values in full

### Envelope discriminator

Applies to: `type`

| Value | Meaning |
|---|---|
| lifecycle | Boot / online announcement. |
| snapshot | Full state roll-up. |
| event | A discrete occurrence. |

This string also has behaviour inside the firmware: publish_json compares it against "event" to decide whether an offline message is buffered or dropped (main/telemetry/telemetry_v2.c:115-122).

### Event discriminator

Applies to: `data.event`

| Value | Meaning |
|---|---|
| online | Hub has an MQTT session. main/telemetry/telemetry_v2.c:340 |
| valve_state_changed | The valve opened or closed. main/iothub/app_iothub.c:1789-1790 |
| valve_flood_detected | The valve's own flood probe went wet. main/iothub/app_iothub.c:1778-1779 |
| valve_flood_cleared | The valve's own flood probe went dry. main/iothub/app_iothub.c:1778-1779 |
| leak_detected | A leak sensor went wet. main/iothub/app_iothub.c:1748, :1808 |
| leak_cleared | A leak sensor went dry. main/iothub/app_iothub.c:1748, :1808 |
| cmd_ack | Result of an inbound C2D command. main/telemetry/telemetry_v2.c:704 |
| water_access_override_enabled | A 24-hour override window started. main/rules_engine/rules_engine.c:215 |
| auto_close | The hub closed the valve in response to a leak. main/rules_engine/rules_engine.c:333, :934 |
| auto_close_blocked_override | A leak occurred but auto-close was suppressed by an active override. main/rules_engine/rules_engine.c:469 |
| rmleak_cleared | The leak interlock was cleared. main/rules_engine/rules_engine.c:678 |
| auto_close_reenabled | An override window was cancelled by command. main/rules_engine/rules_engine.c:720 |
| water_access_override_expired | The override window elapsed. main/rules_engine/rules_engine.c:1025 |
| rmleak_auto_cleared | The interlock cleared itself on timeout. main/rules_engine/rules_engine.c:1082 |
| device_offline | A peer crossed into the critical health rating. main/health_engine/health_engine.c:587-588 |
| device_recovered | A peer crossed out of the critical health rating. main/health_engine/health_engine.c:587-588 |
| rules_engine | Fallback label; accompanies data.raw when a rules payload could not be re-parsed. main/telemetry/telemetry_v2.c:668 |
| health_engine | Fallback label; accompanies data.raw when a health payload could not be re-parsed. main/telemetry/telemetry_v2.c:687 |

Eighteen members. Two of them (rules_engine, health_engine) are fallback labels emitted only if an internally generated payload could not be re-parsed; they accompany data.raw and should never be seen in normal operation.

### Reset cause (lifecycle only) — LOSSY

Applies to: `data.reset_reason`

| Value | Meaning |
|---|---|
| power_on | ESP_RST_POWERON. main/telemetry/telemetry_v2.c:131 |
| software | ESP_RST_SW — a deliberate esp_restart(). main/telemetry/telemetry_v2.c:132 |
| panic | ESP_RST_PANIC — a crash. main/telemetry/telemetry_v2.c:133 |
| watchdog | ESP_RST_INT_WDT, ESP_RST_TASK_WDT and ESP_RST_WDT — three causes, one string. main/telemetry/telemetry_v2.c:134-136 |
| brownout | ESP_RST_BROWNOUT — supply voltage collapsed. main/telemetry/telemetry_v2.c:137 |
| deep_sleep | ESP_RST_DEEPSLEEP. main/telemetry/telemetry_v2.c:138 |
| unknown | The default: branch — every other cause, including an external pin reset. main/telemetry/telemetry_v2.c:139 |

Two distinct losses. The three ESP-IDF watchdog causes collapse into one string, so a hung task cannot be distinguished from a blocked interrupt. And the default: branch absorbs everything else including an operator pressing the reset button, which then looks identical to a genuinely unknown cause. [EXTERNAL] the full esp_reset_reason_t member list is ESP-IDF's, at $IDF_PATH/components/esp_system/include/esp_system.h.

### Why this message was produced — two disjoint vocabularies on one key

Applies to: `data.reason`

| Value | Meaning |
|---|---|
| heartbeat | The periodic timer; also the default: fallback. main/iothub/app_iothub.c:588-589 |
| event | A telemetry event just fired and pulled a snapshot with it. main/iothub/app_iothub.c:584 |
| commission | A provisioning change. main/iothub/app_iothub.c:585 |
| boot | The first snapshot after boot. main/iothub/app_iothub.c:586 |
| fast | An early snapshot taken as soon as the valve is ready; incomplete by design. main/iothub/app_iothub.c:587 |
| decommission | Passed as a literal, not via the enum, immediately before the hub restarts. main/iothub/app_iothub.c:1570 |
| c2d_command | Rules-event meaning only: the override was cancelled by a cloud command. main/rules_engine/rules_engine.c:722 |

The same key carries two unrelated vocabularies. In a snapshot it names the trigger that caused the snapshot. In the auto_close_reenabled rules event it names why the override was cancelled. They are told apart only by type and data.event.

### Health rating — one vocabulary, six field paths

Applies to: `data.system_health.rating`, `data.valve.rating`, `data.lora_sensors[].rating`, `data.ble_leak_sensors[].rating`, `data.rating`, `data.prev_rating`

| Value | Meaning |
|---|---|
| excellent | HEALTH_EXCELLENT. main/health_engine/health_engine.c:60 |
| good | HEALTH_GOOD. main/health_engine/health_engine.c:61 |
| warning | HEALTH_WARNING. main/health_engine/health_engine.c:62 |
| critical | HEALTH_CRITICAL. main/health_engine/health_engine.c:63 |
| unknown | The default: branch. main/health_engine/health_engine.c:64 |

The most consistently reused enum in the contract: six different field names, one vocabulary. data.system_health.rating is the worst rating across all devices in the same snapshot.

### Device class on health events — CHANGED IN 1.8.0

Applies to: `data.dev_type`

| Value | Meaning |
|---|---|
| valve | HEALTH_DEV_VALVE. main/health_engine/health_engine.c:75 |
| lora | HEALTH_DEV_LORA. main/health_engine/health_engine.c:76 |
| ble_leak_sensor | HEALTH_DEV_BLE_LEAK. Was ble_leak at HEAD. main/health_engine/health_engine.c:77 |
| unknown | The default: branch. main/health_engine/health_engine.c:78 |

The ble_leak_sensor value was ble_leak at git HEAD b62b25e and is ble_leak_sensor in the working tree, changed deliberately to match source_type on leak events (intent comment at main/health_engine/health_engine.c:68-71). Both spellings will exist in stored history and in the fleet simultaneously — see §18.

### What kind of thing reported the leak

Applies to: `data.source_type`

| Value | Meaning |
|---|---|
| lora | A LoRa sensor. main/iothub/app_iothub.c:1749 |
| ble_leak_sensor | A BLE leak sensor. main/iothub/app_iothub.c:1809; enum member main/rules_engine/rules_engine.c:262 |
| valve_flood | The valve's own on-board flood probe. main/rules_engine/rules_engine.c:264 |
| reconnect | Pseudo-source used only by the valve-reconnect auto_close variant; not a leak_source_t member. main/rules_engine/rules_engine.c:935 |
| unknown | The default: branch. main/rules_engine/rules_engine.c:265 |

The leak-event path does NOT use source_to_str — it passes string literals directly (main/iothub/app_iothub.c:1749, :1809), so this vocabulary is maintained in two places. The value is then re-parsed inside the firmware (strcmp against "lora") to select a metadata table at main/telemetry/telemetry_v2.c:648-649, so the wire string is load-bearing in the device. Note reconnect is a pseudo-source that is not a member of the underlying C enum.

### Where the sensor is installed

Applies to: `data.location.code`, `data.lora_sensors[].location.code`, `data.ble_leak_sensors[].location.code`

| Value | Meaning |
|---|---|
| unknown | No location assigned, or an out-of-range code. |
| bathroom | Bathroom. |
| kitchen | Kitchen. |
| laundry | Laundry. |
| garage | Garage. |
| garden | Garden. |
| basement | Basement. |
| utility | Utility room. |
| hallway | Hallway. |
| bedroom | Bedroom. |
| living_room | Living room. |
| attic | Attic. |
| outdoor | Outdoor. |

The only enum in the contract with a compile-time guard: a _Static_assert binds the string table length to LOC_COUNT (main/sensor_meta/sensor_meta.c:28-29), so the table and the C enum cannot drift apart. An out-of-range code falls back to unknown.

### Valve position in a snapshot — 4 members

Applies to: `data.valve.state`

| Value | Meaning |
|---|---|
| open | Position byte == 1. main/telemetry/telemetry_v2.c:429-430 |
| closed | Position byte == 0. main/telemetry/telemetry_v2.c:429-430 |
| unknown | Position byte was neither 0 nor 1, including the -1 not-yet-read sentinel. main/telemetry/telemetry_v2.c:430 |
| disconnected | No BLE link to the valve. Snapshot only. main/telemetry/telemetry_v2.c:442 |

This field merges two different facts: the valve's mechanical position AND whether there is a BLE link at all. unknown means the GATT characteristic byte was neither 0 nor 1; disconnected means there is no link. The link state is also carried separately as data.valve.connected, so it appears twice.

### Valve position on a valve event — 3 members

Applies to: `data.valve_state`

| Value | Meaning |
|---|---|
| open | Position byte == 1. main/telemetry/telemetry_v2.c:617 |
| closed | Position byte == 0. main/telemetry/telemetry_v2.c:617 |
| unknown | Neither 0 nor 1. main/telemetry/telemetry_v2.c:618 |

The same ternary as data.valve.state but WITHOUT the disconnected member, because a valve event cannot be produced while the valve is disconnected (see §10, valve event).

### What started the override window

Applies to: `data.trigger`

| Value | Meaning |
|---|---|
| button | A physical long-press on the valve, or the reconnect inference. main/rules_engine/rules_engine.c:978, :1128 |
| c2d_command | A remote override_enable command. main/rules_engine/rules_engine.c:846 |

The ternary's default is button, so any path that does not explicitly say c2d_command reports button.

### Command outcome

Applies to: `data.status`

| Value | Meaning |
|---|---|
| ok | The command succeeded. |
| error | The command failed; data.error is then present. |

A plain success flag rendered as a string. The failure detail is free text in data.error.detail; there is no machine-readable failure class — see F-06.
