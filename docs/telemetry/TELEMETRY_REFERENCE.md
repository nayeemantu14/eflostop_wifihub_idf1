> **Superseded for firmware 2.1.4.** This document describes firmware 1.8.0. For 2.1.4 use `telemetry_messages.md` (v5.0) and the JSON schemas in `docs/telemetry/schemas/`.

# eFloStop II WiFi Hub — Telemetry Reference (`eflostop.v2`)
### Every message the hub sends to the cloud

**Hub firmware:** 1.8.0 · **Schema:** `eflostop.v2` (additive-only) · **Audience:** backend + app
engineers, no firmware knowledge assumed.

This document is generated from, and cited against, the firmware source. Every claim carries a
`file:line`. It was cross-checked by a completeness pass (every `"event"` literal and every publish
call site in `main/` mechanically verified present) and an accuracy pass (every conditional field
checked for **omitted vs `null`**, every enum checked closed).

> **The single most important rule in this document:** a conditional field is either **omitted** or
> emitted as **`null`**, and which one depends on the field. They are not interchangeable, and at least
> one field (`valve.fw_version`) uses *both* depending on why the value is unknown. Treat missing and
> `null` as equivalent in your parser unless a section says otherwise.

---

## 1. Everything at a glance

There are **three** envelope `type` values and **one** telemetry topic. Nothing else is telemetry.

| `type` | Purpose | Cadence |
|---|---|---|
| `lifecycle` | "I am online" — sent once per **MQTT connect**, not per boot | On every connect (incl. scheduled reconnects — see §9.1) |
| `snapshot` | Full current state of hub + valve + all sensors | 5 min heartbeat + on state change |
| `event` | Something happened, right now | Event-driven, debounced |

### Complete event index (`type: "event"`)

Every possible value of `data.event`. There are no others.

| `data.event` | Family | Meaning |
|---|---|---|
| `leak_detected` | leak | A sensor went wet |
| `leak_cleared` | leak | A sensor went dry |
| `valve_flood_detected` | leak | The valve's own probe detected water |
| `valve_flood_cleared` | leak | The valve's own probe is dry |
| `valve_state_changed` | valve | Valve opened or closed (delta-gated) |
| `auto_close` | rules | The hub closed the valve in response to a leak |
| `auto_close_blocked_override` | rules | A leak occurred but auto-close was suppressed by the 24 h override |
| `auto_close_reenabled` | rules | The override window ended via `override_cancel` |
| `rmleak_cleared` | rules | The leak interlock was cleared |
| `rmleak_auto_cleared` | rules | The interlock cleared itself on the auto-clear timeout |
| `water_access_override_enabled` | rules | 24 h override window started |
| `water_access_override_expired` | rules | 24 h override window elapsed |
| `device_offline` | health | A device crossed **into** `critical` |
| `device_recovered` | health | A device crossed **out of** `critical` |
| `cmd_ack` | ack | Result of a C2D command |
| `rules_engine` | *fallback* | Defensive; should never appear (see §5 and §6) |
| `health_engine` | *fallback* | Defensive; should never appear (see §5 and §6) |

Health events are the only ones carrying `data.category: "health"` — that field is how you recognise
them. Nothing is ever named `health_alert`; that is an internal C type, not a wire value.

### What the hub does NOT send

Verified by exhausting every `esp_mqtt_client_publish` site in `main/` (five in total: DPS ×2,
twin ×1, offline-buffer replay ×1, telemetry ×1):

- **No direct methods** — the hub does not implement them. Use C2D messages.
- **No `$iothub/twin/GET`** — the hub never requests the full twin, so a desired property set while it
  is offline is **never seen**. Desired properties only arrive as live PATCH notifications.
- **No file upload, no MQTT last-will.**

---

## 2. Common envelope + type:"lifecycle"

### What this family covers

Every device-to-cloud telemetry message the hub sends is built by **one** function, `build_envelope()` in `main/telemetry/telemetry_v2.c:59-92`, and published by **one** function, `publish_json()` at `main/telemetry/telemetry_v2.c:99-126`. There is no second telemetry path. If you can parse the envelope, you can parse every `eflostop.v2` message.

This document covers:
1. **The common envelope** — the outer wrapper shared by all three `type` values (`lifecycle`, `snapshot`, `event`).
2. **`type:"lifecycle"`** — the hub's "I am online" birth message (`telemetry_v2.c:334-368`).

#### Transport

| Property | Value | Source |
|---|---|---|
| MQTT topic | `devices/<device_id>/messages/events/` | `telemetry_v2.c:254-255`, `:280-281` |
| QoS | 1 (at-least-once) | `telemetry_v2.c:111` (`qos=1`) |
| Retain | 0 | `telemetry_v2.c:111` |
| Payload | UTF-8 JSON, unformatted / no whitespace | `cJSON_PrintUnformatted`, `telemetry_v2.c:103` |
| `<device_id>` | The DPS-assigned Azure device id (equals the Gateway ID in current enrollment) | `app_iothub.c:1368`, `:1410` |

`<device_id>` is seeded to the Gateway ID at init (`app_iothub.c:1484-1486`) and corrected by `telemetry_v2_attach_client()` once DPS assigns one. No publish can happen before that correction, because `publish_json()` requires a non-NULL MQTT client (`telemetry_v2.c:108`).

#### Envelope shape

Keys are emitted in a deterministic order — `schema`, `ts`, `gateway`, `type`, `data` — but treat that as incidental, not contractual.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785400312,
  "gateway": { "id": "...", "short_id": "...", "name": "...", "fw": "...", "uptime_s": 0 },
  "type": "lifecycle | snapshot | event",
  "data": { }
}
```

`data` is always an object and is always added by the caller immediately after `build_envelope()` returns; there is no code path that emits an envelope without `data`.

#### The pre-SNTP suppression rule (critical)

`build_envelope()` reads the wall clock with `time()` and, if it is below `EPOCH_VALID_THRESHOLD_TELEM` = `1704067200` (2024-01-01 00:00:00 UTC), it **destroys the whole message and returns NULL** (`telemetry_v2.c:66-74`):

```c
if (now < EPOCH_VALID_THRESHOLD_TELEM) {
    ESP_LOGW(TELEM_TAG, "Time not synced (ts=%ld) — suppressing %s", (long)now, type);
    cJSON_Delete(root);
    return NULL;
}
```

Consequences the backend must understand:
- The message is **never sent and never queued**. Every publisher returns early on NULL, so the message does not reach MQTT *and does not reach the NVS offline buffer either* — even if it is a safety-critical leak `event`. It is lost permanently.
- There is therefore **no such thing as an `eflostop.v2` message with `ts` < 1704067200**. `ts` is always a trustworthy epoch.
- In practice a *connected* hub always has a valid clock: the SAS token cannot be minted below the same threshold (`app_iothub.c:1335`, `:1385`), so MQTT is held down until SNTP lands. The suppression window is the boot gap between Wi-Fi association and first NTP reply (SNTP init blocks up to ~120 s, then retries every 60 s — `app_iothub.c:1203-1235`).

#### How the envelope reaches the wire (or does not)

`publish_json()` (`telemetry_v2.c:99-126`) has exactly three outcomes:

| Condition | Behaviour | Code |
|---|---|---|
| Client attached **and** MQTT connected | Publish immediately, QoS 1 | `:108-114` |
| Offline **and** `type == "event"` | Serialize to the NVS ring buffer for replay | `:115-118` |
| Offline **and** `type` is `lifecycle` or `snapshot` | **Dropped silently** (regenerated on reconnect) | `:119-122` |

The offline buffer (`main/offline_buffer/offline_buffer.c`) is a 16-slot NVS ring, 512 bytes per slot (`offline_buffer.h:12-13`). On reconnect it is replayed FIFO to the same topic at QoS 1 (`offline_buffer.c:148`) *before* the lifecycle message.

#### When lifecycle is published, and its exact ordering

The trigger is **MQTT connect**, not boot. `mqtt_event_handler` sets a flag on `MQTT_EVENT_CONNECTED` and wakes the event loop (`app_iothub.c:1109-1110`); `iothub_task` acts on it once, inside the loop:

```c
// app_iothub.c:1689-1697
if (g_needs_lifecycle) {
    g_needs_lifecycle = false;
    telemetry_v2_drain_offline();   // Replay buffered events before lifecycle
    telemetry_v2_publish_lifecycle();
    publish_twin_reported();        // Device Twin reported properties
    g_boot_snapshot_sent = false;   // Wait for boot sync before first snapshot
    g_fast_snapshot_sent = false;
    g_fast_arm_ms = snap_now_ms();
}
```

So the guaranteed post-connect sequence on the events topic is:

1. **0..16 replayed `type:"event"` messages** (oldest first) — each carrying its *original* `ts` and *original* `gateway.uptime_s` from when the event occurred, possibly from before the last reboot.
2. **one `type:"lifecycle"`** — `data.event:"online"`.
3. a Twin reported PATCH on a different topic (`$iothub/twin/PATCH/properties/reported/?$rid=N`, `app_iothub.c:986-992`).
4. later, the first `type:"snapshot"` — the "fast" snapshot at valve-ready or at the 150 s ceiling, else the boot snapshot when health boot-sync completes (`app_iothub.c:1828-1850`).

The lifecycle publish is gated on provisioning: the loop hits `if (!provisioned) { ... continue; }` at `app_iothub.c:1672-1686` *before* the lifecycle block. If the hub connects while unprovisioned, `g_needs_lifecycle` stays latched and the lifecycle fires on the first loop pass after provisioning completes.


#### `Common envelope (wrapper for every eflostop.v2 message)`

| | |
|---|---|
| **Envelope `type`** | `shared wrapper — used identically by lifecycle, snapshot and event` |
| **Source** | `main/telemetry/telemetry_v2.c:59-92` |

**Trigger.** Not a standalone message. build_envelope(type) is called by all seven publishers: telemetry_v2_publish_lifecycle (type "lifecycle", telemetry_v2.c:336), telemetry_v2_publish_snapshot (type "snapshot", :374) and the five event publishers (type "event", :610, :637, :659, :679, :700). Hard gate: if the wall clock is < 1704067200 the envelope is deleted and NULL returned (:70-74), the caller returns early, and the message is neither published nor written to the NVS offline buffer — it is lost. Envelope construction is otherwise unconditional and never rate-limited; the rate limiting lives in the callers.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785400312,
  "gateway": {
    "id": "GW-A0B7651228C0",
    "short_id": "28C0",
    "fw": "1.8.0",
    "uptime_s": 359
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "cmd": "valve_close",
    "status": "ok"
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `schema` | string | always | Literal constant "eflostop.v2" — the macro TELEMETRY_SCHEMA (telemetry_v2.h:14). Never varies in this firmware. Use it as the discriminator against the never-called legacy v1 "gatewayID"/devices[] shape. |
| `ts` | number (integer, Unix epoch SECONDS, UTC) | always | Wall clock at the moment the message was BUILT, from time() (telemetry_v2.c:66-67, :76). TZ is forced to UTC0 (app_iothub.c:1208), so no local-time offset. Guaranteed >= 1704067200 (2024-01-01) because lower values suppress the message. Second resolution — two messages built in the same second share a ts. For a replayed offline event this is the ORIGINAL event time, not the replay time. |
| `gateway` | object | always | Hub identity + liveness block. Same content in every message type. |
| `gateway.id` | string | always | Immutable Gateway ID, format "GW-XXXXXXXXXXXX" (12 uppercase hex, no separators), derived from the Wi-Fi STA MAC in eFuse (hub_identity.c:33-35). Snapshotted into s_gateway_id at telemetry_v2_init (telemetry_v2.c:253) from hub_identity_get_gateway_id(). This is the ONLY stable hub identifier — key all backend records on it. |
| `gateway.short_id` | string | always | Last 4 uppercase hex digits of the Gateway ID, i.e. MAC bytes [4] and [5] (hub_identity.c:37). Exactly 4 characters. Also the suffix of the hub's setup AP SSID ("WiFi-Hub-28C0"), so the app can match a physical unit during onboarding. NOT globally unique — 65536 values. |
| `gateway.name` | string | conditional-OMITTED (key absent when unset — never null, never "") | User-assigned friendly hub name, added only when the stored name is non-empty: `if (hub_name[0]) cJSON_AddStringToObject(gw, "name", hub_name);` (telemetry_v2.c:81-83). Max 31 characters; longer names are REJECTED, not truncated (hub_identity.c:99-103). Set via Twin desired `hub_name` (app_iothub.c:1029-1036) or C2D `set_hub_name` (:918), persisted in NVS namespace "hub_ident", key "hub_name", in the nvs_prov partition. Can appear, change or vanish between two messages of the same session — display only, never an identifier. |
| `gateway.fw` | string | always | Hub firmware version, e.g. "1.8.0". Read at runtime from the ESP-IDF app descriptor, which is populated from PROJECT_VER in the top-level CMakeLists.txt (currently 1.8.0) — telemetry_v2_fw_version(), telemetry_v2.c:50-54. Literal "0.0.0" if the descriptor is unreadable. Same string as the OTA image header and the boot banner, so it cannot drift. |
| `gateway.uptime_s` | number (integer seconds) | always | esp_timer_get_time() / 1000000 — MONOTONIC seconds since power-on/reset (telemetry_v2.c:85-86). Not wall clock, unaffected by SNTP steps, and NOT reset by an MQTT reconnect. Use a decrease across consecutive messages to detect a hub reboot; the first message after a real reboot has a small uptime_s (tens of seconds), whereas a network-only reconnect carries a large one. |
| `type` | string | always | Message class discriminator. Exactly three values are ever emitted: "lifecycle", "snapshot", "event" (telemetry_v2.c:336, :374, and :610/:637/:659/:679/:700). Route on this first, then on data.event. |
| `data` | object | always | Type-specific payload, attached by the calling publisher right after build_envelope() returns. Its shape depends entirely on `type`. For type "event" produced by telemetry_v2_publish_rules_event / _publish_health_event, `data` is the rules/health engine's own parsed JSON object spliced in wholesale (telemetry_v2.c:665, :684), so those payloads are the most variable. |


#### `lifecycle / online`

| | |
|---|---|
| **Envelope `type`** | `lifecycle` |
| **Source** | `main/telemetry/telemetry_v2.c:334-368` |

**Trigger.** One message per successful MQTT CONNECT to IoT Hub — NOT once per boot. MQTT_EVENT_CONNECTED sets g_needs_lifecycle = true and wakes the loop (app_iothub.c:1107-1110); iothub_task consumes the flag exactly once at app_iothub.c:1689-1697, immediately AFTER telemetry_v2_drain_offline() replays the NVS ring buffer and immediately BEFORE publish_twin_reported(). Publishing is gated on provisioning: the loop returns early at app_iothub.c:1672-1686 while unprovisioned, so the flag stays latched and the lifecycle fires on the first loop pass after provisioning. No debounce, no coalescing, no rate limit — a flapping link produces one lifecycle per successful CONNECT, each repeating the SAME boot-latched reset_reason with a growing uptime_s. If the hub is offline when this runs it cannot be (publish requires connectivity), but note that publish_json() would DROP a lifecycle rather than buffer it (telemetry_v2.c:119-122): lifecycle is never replayed.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785400241,
  "gateway": {
    "id": "GW-A0B7651228C0",
    "short_id": "28C0",
    "name": "Basement Hub",
    "fw": "1.8.0",
    "uptime_s": 47
  },
  "type": "lifecycle",
  "data": {
    "event": "online",
    "reset_reason": "power_on",
    "provisioned": true,
    "valve_mac": "00:80:E1:27:9A:E6",
    "lora_sensor_count": 2,
    "ble_leak_sensor_count": 3,
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    }
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `data.event` | string | always | Hardcoded literal "online" (telemetry_v2.c:340). It is the only data.event value a lifecycle message can carry — there is no "offline" counterpart from the device (use IoT Hub connection-state events or the snapshot heartbeat gap for that). |
| `data.reset_reason` | string (closed set of 7) | always | Why the hub last booted. Latched at boot from esp_reset_reason() and mapped by reset_reason_str() (telemetry_v2.c:128-141). Exactly these 7 values: "power_on" (ESP_RST_POWERON), "software" (ESP_RST_SW — any esp_restart(): 10 s-hold Wi-Fi reset button reset_button.c:100, C2D decommission_all app_iothub.c:1571, MQTT set-config self-heal reboot app_iothub.c:1307). NOTE: there is no OTA in this firmware, so an update can never be the cause., "panic" (ESP_RST_PANIC — crash/exception), "watchdog" (COLLAPSES ESP_RST_INT_WDT, ESP_RST_TASK_WDT and ESP_RST_WDT into one value), "brownout" (ESP_RST_BROWNOUT — supply sag), "deep_sleep" (ESP_RST_DEEPSLEEP — not used by this product), "unknown" (the default branch, which swallows ESP_RST_UNKNOWN, ESP_RST_EXT external reset pin, ESP_RST_SDIO, ESP_RST_USB, ESP_RST_JTAG, ESP_RST_EFUSE, ESP_RST_PWR_GLITCH and ESP_RST_CPU_LOCKUP). Constant for the life of the boot and REPEATED on every reconnect's lifecycle. |
| `data.provisioned` | boolean | always | provisioning_is_provisioned() — true when the NVS provisioning state is PROV_STATE_PROVISIONED (provisioning_manager.c:81-96). In practice ALWAYS true in a lifecycle message, because the event loop returns early while unprovisioned (app_iothub.c:1672) and can never reach the lifecycle block. Do not use it to detect an unprovisioned hub. |
| `data.valve_mac` | string | conditional-OMITTED (key absent when no valve is provisioned — never null) | Provisioned valve BLE MAC in uppercase colon form "XX:XX:XX:XX:XX:XX". Added only when provisioning_get_valve_mac() returns true, which requires state == PROVISIONED AND a non-empty stored MAC (provisioning_manager.c:583-601). This is the CONFIGURED valve, not a live-connection indicator — a provisioned but disconnected valve still appears here. Live valve connectivity lives in the snapshot's valve object. |
| `data.lora_sensor_count` | number (integer, 0-16) | always | Number of provisioned LoRa leak sensors. Always emitted, including as 0 (telemetry_v2.c:348-351). The count comes from provisioning_get_lora_sensors(), which writes 0 when the hub is unprovisioned or the list is empty and is 0 if the getter fails. On a mutex timeout the getter explicitly writes 0; only the argument/uninitialised early return leaves the caller value untouched (which is also 0). Either way the wire value is 0 (provisioning_manager.c:644-647, :670-673). Ceiling is MAX_LORA_SENSORS = 16. |
| `data.ble_leak_sensor_count` | number (integer, 0-16) | always | Number of provisioned BLE leak sensors, same semantics and same always-emitted behaviour as lora_sensor_count (telemetry_v2.c:353-356; provisioning_manager.c:652-676). Ceiling is MAX_BLE_LEAK_SENSORS = 16. Note this counts BLE leak sensors only — the valve is separate and is not included in either count. |
| `data.rules` | object | conditional-OMITTED (present unless provisioning_get_rules_config() fails — in practice always present) | Global (hub-wide) auto-close configuration, guarded by `if (provisioning_get_rules_config(&rules))` (telemetry_v2.c:358-364). That call only fails if the provisioning manager is uninitialised or its mutex times out after 1 s (provisioning_manager.c:977-991), so the object is effectively always emitted — but the key can be absent and the backend must tolerate that rather than crash. Byte-for-byte the SAME shape as the snapshot's `rules` object (telemetry_v2.c:577-583), deliberately, so one parser serves both. |
| `data.rules.auto_close_enabled` | boolean | always when data.rules is present | Master switch for automatic valve closure on a leak. Default true (provisioning_manager.c:58, :200). Settable via the provisioning payload (provisioning_manager.c:480-482). |
| `data.rules.trigger_mask` | number (integer bitmask, 0-7) | always when data.rules is present | Bitmask of leak SOURCES allowed to trigger auto-close. Bit 0 = 0x01 RULES_TRIGGER_BLE_LEAK, bit 1 = 0x02 RULES_TRIGGER_LORA, bit 2 = 0x04 RULES_TRIGGER_VALVE_FLOOD (provisioning_manager.h:22-25). Default RULES_TRIGGER_ALL = 7. Emitted as a NUMBER, not an array or a string. |


**Integration notes for this family**

- OMITTED vs null is inconsistent BY DESIGN across the codebase, and this family uses OMISSION exclusively. In the envelope and in lifecycle, an unavailable value means the KEY IS ABSENT: `gateway.name` (telemetry_v2.c:82-83), `data.valve_mac` (:345-346) and `data.rules` (:359-364) are the three omittable keys, and none of them is ever emitted as null. By contrast the snapshot builder DOES emit explicit nulls via cJSON_AddNullToObject (e.g. valve.fw_version telemetry_v2.c:440, sensor battery/rssi/snr :505-508). So a backend parser must handle both conventions and must never assume "key present, value null" for a missing envelope/lifecycle field.
- A message built before SNTP sync is destroyed, not deferred — and that includes safety-critical events. build_envelope() returns NULL (telemetry_v2.c:70-74) before the caller ever reaches publish_json(), so the NVS offline buffer is bypassed entirely. A leak detected between Wi-Fi association and the first NTP reply is NEVER reported to the cloud (the hub still closes the valve locally via the rules engine, which runs independently of telemetry). Look for the log line "Time not synced (ts=…) — suppressing <type>".
- `ts` is build time, not delivery time, and replayed offline events keep their original `ts` AND their original `gateway.uptime_s`. The envelope is serialized once, stored as an opaque blob in NVS, and republished verbatim on reconnect (offline_buffer.c:148). A replayed event can therefore be minutes or hours older than its IoT Hub enqueuedTime, and its uptime_s can be from a previous boot. Order and dedupe on `ts` (plus data.event identity), never on arrival order.
- Lifecycle is per-CONNECT, not per-BOOT, and reset_reason is repeated every time. A hub with a flaky uplink emits many lifecycle messages per boot, each with the identical boot-latched reset_reason and an ever-growing uptime_s. Counting lifecycle messages will massively over-report reboots. To detect a genuine reboot, watch for gateway.uptime_s DECREASING between consecutive messages (any type), or for a lifecycle with a small uptime_s.
- Lifecycle and snapshot are DROPPED when offline; only type:"event" is buffered. publish_json() branches on `strcmp(type_hint, "event") == 0` (telemetry_v2.c:115). So there is no guarantee of a lifecycle for every session boundary, and a hub that boots, buffers events, and only later gets a link will replay events first. Never treat lifecycle as the session-start marker for reconciliation — use the snapshot stream as state-of-truth.
- reset_reason lossily collapses ESP32 reset causes. All three watchdog flavours (interrupt WDT, task WDT, other WDT) become the single value "watchdog", so you cannot distinguish them from telemetry. More surprisingly, an EXTERNAL RESET PIN event (ESP_RST_EXT) falls into the default branch and reports as "unknown", not as a distinct value — as do USB/JTAG/eFuse/power-glitch/CPU-lockup resets. "unknown" is therefore a grab-bag, not just an error case. Only "panic" and "brownout" are reliable fault signals.
- `data.provisioned` is always true in a lifecycle message. The event loop `continue`s at app_iothub.c:1672 while unprovisioned, so the lifecycle block at :1689 is unreachable in that state. The field is a constant in practice — do not build unprovisioned-hub detection on it.
- The same three config values appear on TWO paths with DIFFERENT shapes and DIFFERENT presence rules. Lifecycle nests them as `data.rules.{auto_close_enabled,trigger_mask}` (telemetry_v2.c:360-363); the Device Twin reported PATCH FLATTENS them to top-level `auto_close_enabled` and `trigger_mask` (app_iothub.c:973-974). Likewise the hub name: lifecycle/snapshot OMIT `gateway.name` when unset, while the twin ALWAYS emits `hub_name` and uses an EMPTY STRING for unset (app_iothub.c:954, unconditional). Two code paths, two conventions, one logical field.
- `trigger_mask` is a numeric bitmask (0-7), not an array or CSV: 0x01 = BLE leak sensors, 0x02 = LoRa sensors, 0x04 = valve on-board flood sensor. Default 7 (all). A value of 0 with auto_close_enabled:true means "enabled but nothing can trigger it" — a valid and easily-misread configuration.
- Offline-buffer limits will corrupt oversized events. The ring holds 16 entries max and each slot is capped at OFFLINE_BUF_MAX_JSON_LEN = 512 bytes. On overflow the OLDEST entry is silently overwritten (offline_buffer.c:103-105), so a long outage loses the earliest events. Worse, a JSON payload longer than 512 bytes is TRUNCATED at store time (offline_buffer.c:77-81) and later replayed as syntactically invalid JSON — the backend must tolerate an unparseable message on the events topic. Envelope overhead alone is ~130-170 bytes, leaving ~340-380 bytes for `data`.
- QoS 1 means at-least-once: duplicates are expected. There is no message-id, sequence number or idempotency key anywhere in the envelope — `ts` has only second resolution and `gateway.uptime_s` is the closest thing to a monotonic counter. Also, a drain that is interrupted by a rejected publish stops early and leaves the remaining entries in NVS for the NEXT reconnect (offline_buffer.c:152-155), which can produce a second replay attempt of the tail. Make all event handling idempotent on (gateway.id, ts, data.event, sensor id).
- Nothing in FW 1.8.0 sends the legacy v1 "gatewayID"/`devices[]` payload. build_valve_delta_json(), build_lora_delta_json() and build_ble_leak_delta_json() still exist in app_iothub.c:399/442/484 but are marked `__attribute__((unused))` and are never called. The ONLY publish sites on the events topic are telemetry_v2.c:111 (live) and offline_buffer.c:148 (replay). If a `gatewayID`-shaped message ever arrives, it is not from this firmware version.
- Numeric fields print as plain integers even though cJSON stores them as doubles. `ts`, `gateway.uptime_s`, both counts and `trigger_mask` go through cJSON_AddNumberToObject and are printed with %1.15g, which for these magnitudes yields no decimal point and no exponent (e.g. `1785400241`, not `1.785400241e9`). They are safe to parse as 64-bit integers, but a strict schema should still accept `number`, not `integer`, in case a future field carries a fraction.
- Message ordering after reconnect is fixed and worth relying on for reconciliation: N replayed events (each older than now) -> exactly one lifecycle -> one Twin reported PATCH on `$iothub/twin/PATCH/properties/reported/?$rid=N` -> the first snapshot (a deliberately-early "fast" snapshot at valve-ready, or by a 150 s ceiling). See app_iothub.c:1689-1697 and :1828-1850. The lifecycle is the marker that all buffered history for that outage has already been sent.


---

## 3. type:"snapshot" (full-state report)

### What a snapshot is

The **snapshot** is the hub's complete, self-contained picture of the whole installation: the hub itself, the valve, every provisioned LoRa leak sensor, every provisioned BLE leak sensor, the global auto-close rule config, and the 24 h water-access override window. It is the message the app should drive its UI from. Events (`type:"event"`) tell you *something just happened*; the snapshot tells you *what the world looks like right now*.

There is exactly **one** snapshot builder, `telemetry_v2_publish_snapshot()` at `main/telemetry/telemetry_v2.c:372-604`, and exactly **two** call sites:

| Call site | Purpose |
|---|---|
| `main/iothub/app_iothub.c:1882` | the single flush block of the scheduler — produces reasons `heartbeat`, `event`, `commission`, `boot`, `fast` |
| `main/iothub/app_iothub.c:1568` | the one-off final snapshot after `decommission` `target:"all"`, published just before reboot — reason `decommission` |

Transport is identical to all other `eflostop.v2` telemetry: MQTT PUBLISH to `devices/<device_id>/messages/events/`, **QoS 1**, retain 0 (`telemetry_v2.c:111`).

#### Snapshots are NEVER buffered offline

`publish_json()` (`telemetry_v2.c:99-126`) buffers only messages whose `type_hint` is `"event"`. A snapshot generated while MQTT is down is **silently dropped** with the comment *"regenerated on reconnect"* (`telemetry_v2.c:119-122`). Consequences for the backend:

* You will never receive a back-dated snapshot. Every snapshot's `ts` is close to its arrival time.
* Snapshot delivery has gaps across outages. **Do not** infer "nothing changed" from an absent snapshot — reconstruct the outage window from the replayed `event` messages, which *are* buffered.
* Everything before the wall clock is valid (`ts < 1704067200`) is suppressed too (`telemetry_v2.c:70-74`), so no snapshot is ever emitted in the first few seconds after boot, before SNTP.

#### The `reason` field is coarse — the fine-grained cause is log-only

`data.reason` can only ever be one of **six** strings: `heartbeat`, `event`, `commission`, `boot`, `fast`, `decommission`. The first five come from `snap_reason_str()` (`app_iothub.c:581-591`); `decommission` is a hard-coded literal at `app_iothub.c:1568`.

The firmware *does* track a finer sub-label (`s_snap_evt`: `"leak_detected"`, `"leak_cleared"`, `"valve_state_changed"`, `"valve_flood_detected"`, `"valve_flood_cleared"`, `"rules"`, `"health"`, `"sensor_meta"`, `"commission-refresh"`, `"fast"`) but it is **only written to the UART log** (`app_iothub.c:1878`) and is **not on the wire**. If you need to know *which* event caused a `reason:"event"` snapshot, correlate by timestamp with the `type:"event"` message that immediately precedes it (the event is always published *before* the coupled snapshot — see the ordering invariant in `app_iothub.c:1852-1857`).

#### The scheduler (what governs snapshot volume)

All scheduling state lives in `app_iothub.c:139-164` and is owned by `iothub_task` alone (lock-free). The deadline is a **monotonic** `esp_timer` value, immune to SNTP wall-clock steps.

| Constant | Value | Meaning |
|---|---|---|
| `SNAPSHOT_INTERVAL_MS` | 300 000 (5 min) | default heartbeat period (`telemetry_v2.h:15`) |
| Twin `snapshot_interval_s` | clamped to **60…3600 s** | runtime heartbeat override (`app_iothub.c:1017-1026`) |
| `SNAP_HIGH_WINDOW_MS` | 300 ms | coalescing window for safety-critical events |
| `SNAP_LOW_WINDOW_MS` | 2 000 ms | coalescing window for low-priority events |
| `SNAP_MIN_INTERVAL_MS` | 5 000 ms | hard floor between EVENT/HEARTBEAT snapshots → **≤ 12 snapshots/min** |
| `SNAP_RETRY_FLOOR_MS` | 5 000 ms | backoff when connected but the publish failed (outbox full) |
| `SNAP_OFFLINE_FLOOR_MS` | 30 000 ms | loop idle cap while offline (prevents spin; not a publish) |
| `SNAP_FAST_CEILING_MS` | 150 000 ms | fire the `fast` snapshot by 150 s even if the valve never connects |
| `COMMISSION_REFRESH_GRACE_MS` | 360 000 (6 min) | window in which late sensors trigger extra `commission` snapshots |
| `HEALTH_BOOT_SYNC_TIMEOUT_MS` | 120 000 (2 min) | boot "wait for all devices to be heard" window |
| `HEALTH_COMMISSION_SYNC_TIMEOUT_MS` | 150 000 (2.5 min) | same window, re-armed after a provision/decommission |

**Pull-in-only coalescing** (`snap_request`, `app_iothub.c:610-641`): a request may only move the deadline *earlier*, or upgrade LOW→HIGH tier inside an existing EVENT burst. A cascade of events (e.g. leak → auto-close → valve state change → health alert, all within ~300 ms) therefore collapses into **one** snapshot, not four.

**Heartbeat re-arm is publish-confirmed** (`snap_rearm_heartbeat`, `app_iothub.c:596-603`): the next heartbeat is `last_confirmed_publish + interval`. `telemetry_v2_publish_snapshot()` returns `true` only when the message actually reached esp-mqtt with `msg_id >= 0`. An offline drop, a pre-SNTP suppression, or a saturated QoS-1 outbox never counts as "sent", so the heartbeat cannot be silently starved. Net effect for the backend: after a reconnect you get a snapshot promptly, and the heartbeat cadence resumes from that moment (heartbeats are **not** on a wall-clock grid — do not expect them at :00/:05/:10).

**Value-completeness gate** (`app_iothub.c:1858-1874`): while the boot/commission sync window is still open, `heartbeat`/`boot`/`commission` snapshots are *deferred* in 2 s steps so an incomplete device picture is never published. `event` and `fast` always pass this gate — `fast` is deliberately early and incomplete.

#### Boot / reconnect sequence you should expect

1. MQTT connects → buffered `event`s replayed → `lifecycle` → twin reported patch (`app_iothub.c:1689-1697`).
2. **`reason:"fast"`** as soon as the valve GATT setup completes (`ble_valve_is_ready()`, typically 20-30 s), or unconditionally at 150 s. One-shot per connection. Carries real valve data but sensors that have not yet beaconed appear offline/null. This snapshot also *opens* the 6-minute incremental-refresh window.
3. **`reason:"boot"`** if the sync window (all devices heard, else 120 s) closes before the fast snapshot fired. In practice `fast` usually wins and marks `boot` as satisfied, so you often see `fast` and never `boot`.
4. **`reason:"commission"`** repeatedly for up to 6 minutes, once for each additional device heard for the first time (dry WBA leak sensors beacon on a ~100 s cadence, so a 3-sensor install typically yields 1-3 refresh snapshots).
5. **`reason:"heartbeat"`** every 5 min thereafter, plus **`reason:"event"`** whenever something changes.

A `provision` command, or `decommission` of `valve`/`lora`/`ble` (i.e. anything that is not `target:"all"`), calls `arm_commission_snapshot()` (`app_iothub.c:566-575`) and restarts steps 3-4 — but **not** step 2 (`g_fast_snapshot_sent` is only reset on the lifecycle path). So after a re-provision you get `commission` snapshots, not a `fast` one.

#### Presence semantics — the part that breaks naive parsers

This builder uses **three** distinct ways of saying "I don't know", and they are different on the wire:

1. **Key omitted entirely** — e.g. `data.valve.battery` simply does not exist when the valve is disconnected.
2. **Key present with JSON `null`** — e.g. `lora_sensors[].battery` is explicitly `null` for an offline sensor (`cJSON_AddNullToObject`).
3. **Key present with a placeholder** — e.g. `lora_sensors[].leak_state` is hard-coded `false` (not `null`) when no cached data exists, and `location.label` is `""` when no metadata is set.

Inside the sensor arrays the shape is **fixed**: every element always has the same keys, with `null` for unknown values. Inside `data.valve` the shape is **variable**: the connected branch and the disconnected branch emit different key sets. Parse them accordingly.


#### `snapshot`

| | |
|---|---|
| **Envelope `type`** | `snapshot` |
| **Source** | `main/telemetry/telemetry_v2.c:372` |

**Trigger.** Six distinct reasons, all producing this identical schema.

(1) reason="heartbeat" — periodic. Deadline = last CONFIRMED publish + interval. Interval defaults to 300 s (SNAPSHOT_INTERVAL_MS, telemetry_v2.h:15) and is overridable via Device Twin desired `snapshot_interval_s`, clamped to 60..3600 s (app_iothub.c:1017-1026). Re-armed only on a confirmed publish (app_iothub.c:1912), so an outage does not accumulate a backlog. A separate fixed 5-min FreeRTOS timer (telemetry_v2.c:263-267) only WAKES the event loop; it does not itself set the cadence.

(2) reason="event" — coupled to a state change, coalesced. Requested by snap_request(SNAP_EVENT, tier, label) from 7 sites: rules-engine telemetry (app_iothub.c:1707), health alert burst (:1728), LoRa leak change (:1749), valve flood detected/cleared (:1779), valve state change, delta-gated (:1788), BLE leak change (:1810), sensor_meta rename (:1580). Coalescing window: 300 ms for HIGH tier (all safety-critical), 2000 ms for LOW tier (only `auto_close_blocked_override`, app_iothub.c:1706). Hard floor of SNAP_MIN_INTERVAL_MS = 5000 ms since the last confirmed publish (app_iothub.c:628) → at most ~12 snapshots/min. Pull-in-only: a burst of N events collapses into ONE snapshot (app_iothub.c:635).

(3) reason="boot" — the complete post-sync snapshot, fired when health_is_boot_sync_complete() turns true: all provisioned devices heard at least once, else the window times out (120 s at boot / 150 s after a provision) (app_iothub.c:1842-1843). Urgent: no min-interval clamp.

(4) reason="fast" — the EARLY boot/reconnect snapshot, fired as soon as ble_valve_is_ready() (CONNECTED|ENCRYPTED|DISCOVERY_DONE, ~20-30 s), or unconditionally 150 s after the (re)connect (SNAP_FAST_CEILING_MS) (app_iothub.c:1828-1832). One-shot per MQTT connection; re-armed only in the lifecycle block (:1695). Incomplete-by-design: sensors that have not beaconed yet appear connected:false with null values. A successful fast publish sets g_boot_snapshot_sent (suppressing the later `boot` snapshot) and OPENS the 6-min incremental-refresh window (:1893-1910).

(5) reason="commission" — incremental refresh. For COMMISSION_REFRESH_GRACE_MS = 6 min after a commission event, each time health_get_sync_counts() reports MORE devices heard than the last published snapshot reflected, another snapshot fires (app_iothub.c:1844-1849). Urgent, no min-interval clamp. Armed by arm_commission_snapshot() (:566-575) from: `provision` (:896), `decommission target:"valve"` (:728), `target:"lora"` (:744), `target:"ble"` (:763). The window closes early once seen >= total (:1891).

(6) reason="decommission" — exactly one snapshot after `decommission` with target:"all", published from iothub_task after waiting up to ~1 s for the valve BLE link to drop, then esp_restart() 3 s later (app_iothub.c:1561-1572). Renders the CLEARED state: empty device arrays, rating "excellent".

GLOBAL SUPPRESSION: no snapshot of any reason is emitted while the wall clock is invalid (ts < 1704067200) (telemetry_v2.c:70-74), nor while unprovisioned (app_iothub.c:1672-1686), nor while MQTT is down — offline snapshots are DROPPED, never buffered (telemetry_v2.c:119-122).

PUBLISH-FAIL BACKOFF: connected but esp_mqtt_client_publish returned a negative msg_id (e.g. QoS-1 outbox saturated) → retry no sooner than +5000 ms, honoured by every subsequent snap_request (app_iothub.c:1919-1920).

```json
{
  "schema": "eflostop.v2",
  "ts": 1785364820,
  "gateway": {
    "id": "GW-F412FA2C28C0",
    "short_id": "28C0",
    "name": "Basement Hub",
    "fw": "1.8.0",
    "uptime_s": 86412
  },
  "type": "snapshot",
  "data": {
    "reason": "heartbeat",
    "system_health": {
      "rating": "critical",
      "reason": "1 sensor offline"
    },
    "valve": {
      "mac": "00:80:E1:27:9A:E6",
      "state": "open",
      "battery": 78,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "fw_version": "2.2.0",
      "rating": "excellent",
      "last_seen_age_s": 3
    },
    "lora_sensors": [
      {
        "sensor_id": "0x754A6237",
        "connected": true,
        "rating": "excellent",
        "last_seen_age_s": 128,
        "battery": 92,
        "leak_state": false,
        "rssi": -72,
        "snr": 9.5,
        "location": {
          "code": "kitchen",
          "label": "Kitchen sink"
        }
      },
      {
        "sensor_id": "0x754A6238",
        "connected": false,
        "rating": "critical",
        "last_seen_age_s": null,
        "battery": null,
        "leak_state": false,
        "rssi": null,
        "snr": null,
        "location": {
          "code": "garage",
          "label": ""
        }
      }
    ],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:11:22:33",
        "connected": true,
        "rating": "warning",
        "last_seen_age_s": 47,
        "battery": 18,
        "leak_state": false,
        "rssi": -81,
        "fw_version": "1.1.0",
        "location": {
          "code": "laundry",
          "label": "Washer"
        }
      }
    ],
    "rules": {
      "auto_close_enabled": true,
      "trigger_mask": 7
    },
    "override_active": true,
    "override_remaining_s": 73840,
    "expires_ts": 1785438660
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `schema` | string | always | Constant "eflostop.v2". telemetry_v2.c:64. Reject anything else. |
| `ts` | number (integer-valued) | always | Hub wall-clock UNIX epoch SECONDS at build time (telemetry_v2.c:66-76). Guaranteed >= 1704067200 (2024-01-01) because build_envelope refuses to build below that. This is the hub's SNTP-synced clock, not the broker's; treat it as authoritative for ordering snapshots from one hub but do not assume sub-second accuracy or strict monotonicity across an SNTP step. |
| `gateway.id` | string | always | Gateway ID, format "GW-XXXXXXXXXXXX" (12 uppercase hex = WiFi STA MAC from eFuse). Immutable per unit; also the Azure DPS registration ID. hub_identity.c:33-35, copied into telemetry at telemetry_v2.c:79. |
| `gateway.short_id` | string | always | Last 4 uppercase hex digits of the MAC, e.g. "28C0" (hub_identity.c:37). Used in the setup AP SSID "WiFi-Hub-28C0" — this is the string that lets a user match a physical hub. telemetry_v2.c:80. |
| `gateway.name` | string | conditional-OMITTED | User-assigned hub name, max 31 chars. Key is entirely ABSENT when no name is set — never null, never "" (telemetry_v2.c:81-83 guards on hub_name[0]). Set via Twin desired `hub_name` or C2D `set_hub_name`; cleared by decommission_all, so the final decommission snapshot has no name key even if one was set. |
| `gateway.fw` | string | always | Hub firmware version, e.g. "1.8.0". Single source of truth is PROJECT_VER in CMakeLists.txt, read from the ESP-IDF app descriptor at runtime, so it can never drift from the OTA image header. Falls back to the literal "0.0.0" if the descriptor is unreadable (telemetry_v2.c:50-54). |
| `gateway.uptime_s` | number (integer-valued) | always | Seconds since boot, from esp_timer (telemetry_v2.c:85-86). Monotonic; resets to ~0 on any reboot. A snapshot with a small uptime_s plus reason "fast"/"boot" is your reboot signal. |
| `type` | string | always | Constant "snapshot" for this family. Note the key order: `type` comes AFTER `gateway` in the wire bytes (telemetry_v2.c:89). |
| `data.reason` | string enum | conditional-OMITTED (always present in current firmware) | Why this snapshot was sent. Exactly six possible values: "heartbeat" \| "event" \| "commission" \| "boot" \| "fast" \| "decommission". Omitted only if the trigger argument were NULL/empty (telemetry_v2.c:382-383); both call sites always pass a non-empty literal, so treat it as always present but code defensively. The finer event label is log-only and NOT on the wire. |
| `data.system_health.rating` | string enum | always | Worst-of roll-up across every provisioned device: "excellent" \| "good" \| "warning" \| "critical" (health_engine.c:57-66, 166-178). Lock-free volatile read, so it is present even when the per-device table could not be read. "unknown" exists in the enum-to-string switch default but is unreachable for a valid rating. NOTE: an empty install (nothing provisioned) rolls up to "excellent", not "unknown". |
| `data.system_health.reason` | string | always | Human-readable English explanation, max 127 chars, safe to show in the UI verbatim (telemetry_v2.c:157-231, 395-401). Built as a comma-joined list of up to 5 clauses drawn from a fixed set, counting only devices AT the system rating level: "Valve offline" \| "Valve disconnected" \| "Valve battery low" \| "N sensor(s) offline" \| "N sensor(s) battery low" \| "N sensor(s) signal weak". Special values: "All devices healthy" when rating==excellent; "Degraded" if the rating is non-excellent but no clause matched; "Health data unavailable" when the health mutex timed out (see gotchas). Pluralisation is inline ("1 sensor offline" vs "2 sensors offline"). |
| `data.valve` | object | always | The valve block. The OBJECT is always present, but its key set is VARIABLE — see the connected/disconnected branches below (telemetry_v2.c:405-458). It can legitimately be as small as {"state":"disconnected","connected":false} when no valve is provisioned and none is connected. |
| `data.valve.mac` | string | conditional-OMITTED | Valve BLE MAC, colon-separated "XX:XX:XX:XX:XX:XX". Uppercase ONLY on the connected branch (built with %02X from the NimBLE peer address, app_ble_valve.c:1216-1218); when disconnected the value is the provisioning string verbatim, and lowercase hex is accepted at provisioning time — so COMPARE CASE-INSENSITIVELY. Prefers the LIVE connected MAC; falls back to the PROVISIONED MAC from the health table when disconnected (telemetry_v2.c:421-425). OMITTED entirely when the valve is both disconnected AND not present in the health table (i.e. not provisioned, or the health read failed). |
| `data.valve.state` | string enum | always | "open" \| "closed" \| "unknown" \| "disconnected". Mapped from the raw GATT byte: 1 -> "open", 0 -> "closed", anything else (including the -1 not-yet-read sentinel and any byte > 1) -> "unknown" (telemetry_v2.c:429-430). The literal "disconnected" is emitted INSTEAD when the BLE link is down (telemetry_v2.c:442) — so "disconnected" means unknown position, NOT closed. Never render it as a valve position. |
| `data.valve.battery` | number (0-100) | conditional-OMITTED (connected only) | Valve battery percent. Present ONLY in the connected branch (telemetry_v2.c:431); the key is absent when connected:false — it is NOT emitted as null. Beware: the underlying g_val_battery initialises to 0 and is reset to 0 on every BLE disconnect (app_ble_valve.c:134, 1209, 1264), so a valve that is connected but whose battery characteristic has not been read yet reports 0, not null. |
| `data.valve.leak_state` | boolean | conditional-OMITTED (connected only) | true = the valve's own on-board flood sensor is wet. Present ONLY when connected (telemetry_v2.c:432). Resets to false on BLE disconnect. |
| `data.valve.rmleak` | boolean | conditional-OMITTED (connected only) | The valve's remote-leak interlock latch. true = the valve is LOCKED after an auto-close and will refuse/undo an open command until cleared (via C2D leak_reset, override_enable, or a physical single long press). Present ONLY when connected (telemetry_v2.c:433). Use this to grey out the app's Open control. |
| `data.valve.connected` | boolean | always | true = a BLE GATT link to the valve exists right now, derived from ble_valve_get_mac() succeeding (telemetry_v2.c:407, 434, 443). This is the field to branch on when parsing the valve object. |
| `data.valve.fw_version` | string OR null | conditional (present-or-null when connected; OMITTED when disconnected) | Valve firmware revision from the BLE Device Information Service, e.g. "2.2.0". Three-state: a string when read, JSON null when connected but the DIS read has not completed (telemetry_v2.c:437-440), key absent entirely when connected:false. This is the only field in the snapshot with all three presence states. |
| `data.valve.rating` | string enum | conditional-OMITTED | Per-valve health: "excellent" \| "good" \| "warning" \| "critical". OMITTED when the valve has no health-table entry — i.e. not provisioned, or the health read failed (telemetry_v2.c:447-449). Semantics (health_engine.c:141-164): disconnected < 3 min -> "warning" (grace), disconnected >= 3 min -> "critical", never connected this uptime -> "critical", connected with battery <= 20% -> "warning", <= 35% -> "good", else "excellent". |
| `data.valve.last_seen_age_s` | number OR null | conditional (present-or-null when a health entry exists; OMITTED otherwise) | Seconds since the valve was last heard from. JSON null when the valve has never been seen this uptime (internal UINT32_MAX sentinel, telemetry_v2.c:450-455). Key absent entirely when there is no health entry. For a connected valve this is refreshed by every BLE notify, so it is typically a few seconds. |
| `data.lora_sensors` | array of object | always | One element per PROVISIONED LoRa leak sensor (max 16), built by iterating the health table and merging the live telemetry cache (telemetry_v2.c:461-515). Order follows the provisioning order and is stable across snapshots unless the device list changes. Empty array = no LoRa sensors provisioned OR the health read failed — these are indistinguishable on the wire (see gotchas). Element key set is FIXED: all 9 keys are always present, with null for unknown values. |
| `data.lora_sensors[].sensor_id` | string | always | The PROVISIONED sensor ID as "0x" + 8 UPPERCASE hex digits, e.g. "0x754A6237" (health_engine.c:466-467). This exact string is also used as sensor_id in leak events and as the key for sensor_meta. Match case-insensitively; the firmware itself uses strcasecmp. |
| `data.lora_sensors[].connected` | boolean | always | true = heard at least once this uptime AND within the 10-min timeout (HEALTH_LORA_TIMEOUT_MS; health_engine.c:637-644). This is the gate for whether the value fields below are real or null. |
| `data.lora_sensors[].rating` | string enum | always | "excellent" \| "good" \| "warning" \| "critical". Computed by compute_sensor_rating (health_engine.c:111-139): never seen or silent > 10 min -> "critical"; battery <= 20% or RSSI <= -90 dBm -> "warning"; battery <= 35% or RSSI in (-90,-80] -> "good"; else "excellent". Battery 0xFF and RSSI 0 are treated as unknown and skipped by the rating logic. |
| `data.lora_sensors[].last_seen_age_s` | number OR null | always present; NULL when never seen | Seconds since the last LoRa packet. Explicit JSON null (cJSON_AddNullToObject) when the sensor has never checked in this uptime (telemetry_v2.c:473-478). Note this is uptime-scoped: a reboot or any provisioning change resets it to null until the sensor next beacons. |
| `data.lora_sensors[].battery` | number OR null | always present; NULL when not connected | Last reported battery percent. Explicitly null whenever connected==false, because the cache merge is gated on health[i].connected (telemetry_v2.c:484-509) — deliberate, so a just-reloaded device cannot emit stale values alongside connected:false. Emitted RAW with no 0xFF-to-null mapping in this builder, unlike health_alert_to_json. |
| `data.lora_sensors[].leak_state` | boolean | always present; FALSE (not null) when not connected | true = this sensor currently reports wet. Derived as cached->leak_status == 1 (telemetry_v2.c:500-501) — note the strict ==1, so any other non-zero raw status would read as false. CRITICAL ASYMMETRY: when there is no cached data this is hard-coded false (telemetry_v2.c:506), NOT null, while battery/rssi/snr beside it are null. Never read leak_state:false on an offline sensor as "confirmed dry" — always check connected first. |
| `data.lora_sensors[].rssi` | number OR null | always present; NULL when not connected | Signal strength in dBm as received by the hub's SX1262, signed, typically -40 to -120. Explicit null when not connected (telemetry_v2.c:502, 507). |
| `data.lora_sensors[].snr` | number (may be fractional) OR null | always present; NULL when not connected | LoRa signal-to-noise ratio in dB. Stored as a C float, so it serialises as a possibly-fractional JSON number (e.g. 9.5, -3.25) — do NOT parse as an integer. Explicit null when not connected (telemetry_v2.c:503, 508). This field exists ONLY on LoRa elements, never on BLE ones. |
| `data.lora_sensors[].location.code` | string enum | always | Machine-readable room code, one of exactly 13 values: "unknown" \| "bathroom" \| "kitchen" \| "laundry" \| "garage" \| "garden" \| "basement" \| "utility" \| "hallway" \| "bedroom" \| "living_room" \| "attic" \| "outdoor" (sensor_meta.c:23-27, 411-417). Falls back to "unknown" when no metadata is stored for this sensor. The object is always emitted (telemetry_v2.c:143-153). |
| `data.lora_sensors[].location.label` | string | always present; EMPTY STRING when unknown | User-assigned free-text label, max 31 chars, e.g. "Kitchen sink". Emitted as "" (empty string, NOT null, NOT omitted) when no metadata exists for this sensor (telemetry_v2.c:151). Set via C2D `sensor_meta` or inline in a `provision` payload; a rename triggers a reason:"event" snapshot. |
| `data.ble_leak_sensors` | array of object | always | One element per PROVISIONED BLE leak sensor (max 16), same construction pattern as lora_sensors (telemetry_v2.c:518-571). Element key set is FIXED at 9 keys: identical to a LoRa element EXCEPT there is no `snr` and there IS an `fw_version`. Empty array = none provisioned OR health read failed. |
| `data.ble_leak_sensors[].sensor_id` | string | always | The PROVISIONED BLE MAC, colon-separated, e.g. "00:80:E1:11:22:33" (health_engine.c:482). Case is whatever provisioning stored; the firmware matches it case-insensitively, and so should you. |
| `data.ble_leak_sensors[].connected` | boolean | always | true = an advertisement was received at least once this uptime AND within the 10-min timeout (HEALTH_BLE_LEAK_TIMEOUT_MS). Note these are passive-scan advertisers, not GATT connections — "connected" here means "recently heard". |
| `data.ble_leak_sensors[].rating` | string enum | always | "excellent" \| "good" \| "warning" \| "critical", same compute_sensor_rating thresholds as LoRa (10-min silence, 20/35% battery, -90/-80 dBm RSSI). |
| `data.ble_leak_sensors[].last_seen_age_s` | number OR null | always present; NULL when never seen | Seconds since the last advertisement. Explicit null when never heard this uptime (telemetry_v2.c:530-535). Expect values up to ~100 s even for a perfectly healthy dry WBA sensor — that is its normal burst cadence, not a fault. |
| `data.ble_leak_sensors[].battery` | number OR null | always present; NULL when not connected | Last advertised battery percent; explicit null when connected==false (telemetry_v2.c:553, 561). Emitted raw, no 0xFF-to-null mapping. |
| `data.ble_leak_sensors[].leak_state` | boolean | always present; FALSE (not null) when not connected | true = wet. Taken directly from the cached boolean (telemetry_v2.c:554). Same asymmetry as LoRa: hard-coded false, NOT null, when there is no cached data (telemetry_v2.c:562). Gate on connected before trusting a false. |
| `data.ble_leak_sensors[].rssi` | number OR null | always present; NULL when not connected | Advertisement RSSI in dBm as seen by the hub's BLE scanner; explicit null when not connected (telemetry_v2.c:555, 563). |
| `data.ble_leak_sensors[].fw_version` | string OR null | always present; NULL when unknown or not connected | Leak-sensor firmware version, "M.m.p" (max 11 chars), e.g. "1.1.0". Explicit JSON null both when the sensor is not connected and when it is connected but advertised no version string (telemetry_v2.c:556-559, 564). Never omitted. This field exists ONLY on BLE elements, never on LoRa ones. |
| `data.ble_leak_sensors[].location.code` | string enum | always | Same 13-value room enum as the LoRa location, "unknown" when no metadata (telemetry_v2.c:567). |
| `data.ble_leak_sensors[].location.label` | string | always present; EMPTY STRING when unknown | Same semantics as the LoRa label: free text, or "" (never null) when no metadata exists. |
| `data.rules` | object | conditional-OMITTED | Global auto-close rule config. The whole object is OMITTED (not null) if provisioning_get_rules_config() fails — only possible when the provisioning manager is uninitialised or its 1 s mutex times out (provisioning_manager.c:977-991). Deliberately byte-identical in shape to the lifecycle message's `rules` object so one parser serves both (telemetry_v2.c:577-583 vs :358-364). It is a GLOBAL setting, disjoint from any future per-sensor flag. |
| `data.rules.auto_close_enabled` | boolean | always (when data.rules present) | Master enable for leak-triggered automatic valve closure. Defaults to true, including after a full decommission (provisioning_manager.c:546). |
| `data.rules.trigger_mask` | number (bitmask 0-7) | always (when data.rules present) | Which sensor classes may trigger auto-close: bit0 (1) = BLE leak sensors, bit1 (2) = LoRa sensors, bit2 (4) = the valve's own flood sensor. 7 = all (the default, RULES_TRIGGER_ALL). provisioning_manager.h:22-25. |
| `data.override_active` | boolean | always | true = the 24 h water-access override window is running: leaks still publish events, but automatic valve closure is BLOCKED. Read atomically together with the two fields below in one mutex hold to avoid TOCTOU (rules_engine.c:1175-1207, telemetry_v2.c:590-594). Reads false if the rules engine is uninitialised or its 1 s mutex times out — a false negative is possible, so also watch for the `water_access_override_enabled` / `water_access_override_expired` events. |
| `data.override_remaining_s` | number | conditional-OMITTED (present iff override_active is true) | Seconds left in the override window, counting down from 86400 (OVERRIDE_WINDOW_DURATION_S, rules_engine.c:29). Emitted only inside the `if (ovr_active)` block and only when the value is >= 0 (telemetry_v2.c:595-597); since every active-window code path yields >= 0, in practice: present exactly when override_active is true, omitted exactly when it is false. Can legitimately be 0 — window expired but the rules tick has not processed it yet. |
| `data.expires_ts` | number (integer-valued) | conditional-OMITTED | ABSOLUTE UNIX epoch seconds at which the override window ends — same field name and units as the corresponding override event, so the two can be reconciled directly. Emitted only when override_active is true AND the expiry is known and strictly in the future (telemetry_v2.c:598-599). Therefore ABSENT while active-but-already-expired (the remaining_s==0 case). Prefer this over override_remaining_s for UI countdowns; it is immune to message latency. Note the flat placement: it is data.expires_ts, NOT data.override_expires_ts, and it sits alongside override_active rather than nested. |


#### `snapshot (reason="fast") — early, deliberately incomplete`

| | |
|---|---|
| **Envelope `type`** | `snapshot` |
| **Source** | `main/iothub/app_iothub.c:1831` |

**Trigger.** Identical schema to the main snapshot; called out separately because its CONTENT semantics differ and a naive consumer will misread it as "sensors are broken".

Fired once per MQTT connection, as early as ble_valve_is_ready() (valve GATT setup complete: CONNECTED|ENCRYPTED|DISCOVERY_DONE, typically 20-30 s after boot/reconnect), or unconditionally at SNAP_FAST_CEILING_MS = 150 s if the valve never becomes ready (app_iothub.c:1828-1832). It exists so the app has something to render fast, instead of waiting the full 120 s boot-sync window for one slow or absent sensor.

It BYPASSES the value-completeness gate (app_iothub.c:1868) on purpose. Sensors that have not yet beaconed appear connected:false with null battery/rssi/snr/fw_version and leak_state:false — even when they are perfectly healthy. A dry WBA leak sensor beacons roughly every 100 s, so at 25 s uptime most of them will legitimately not have been heard.

On a successful publish it marks g_boot_snapshot_sent (so the later reason:"boot" snapshot is skipped) and OPENS the 6-minute incremental-refresh window, provided some devices are still unheard (app_iothub.c:1893-1910). The gaps are then filled by a series of reason:"commission" snapshots.

ARMED ONLY on the boot/reconnect path — g_fast_snapshot_sent is reset in the lifecycle block (app_iothub.c:1695) AND in the unprovisioned branch of the event loop (:1680-1684) — so provisioning a previously-unprovisioned or decommissioned hub also emits a fresh `fast` snapshot with no MQTT reconnect. It is never reset by arm_commission_snapshot(). So a `provision` or partial `decommission` produces reason:"commission" snapshots but never a new reason:"fast".

```json
{
  "schema": "eflostop.v2",
  "ts": 1785360031,
  "gateway": {
    "id": "GW-F412FA2C28C0",
    "short_id": "28C0",
    "fw": "1.8.0",
    "uptime_s": 27
  },
  "type": "snapshot",
  "data": {
    "reason": "fast",
    "system_health": {
      "rating": "critical",
      "reason": "2 sensors offline"
    },
    "valve": {
      "mac": "00:80:E1:27:9A:E6",
      "state": "open",
      "battery": 78,
      "leak_state": false,
      "rmleak": false,
      "connected": true,
      "fw_version": "2.2.0",
      "rating": "excellent",
      "last_seen_age_s": 1
    },
    "lora_sensors": [
      {
        "sensor_id": "0x754A6237",
        "connected": false,
        "rating": "critical",
        "last_seen_age_s": null,
        "battery": null,
        "leak_state": false,
        "rssi": null,
        "snr": null,
        "location": {
          "code": "kitchen",
          "label": "Kitchen sink"
        }
      }
    ],
    "ble_leak_sensors": [
      {
        "sensor_id": "00:80:E1:11:22:33",
        "connected": false,
        "rating": "critical",
        "last_seen_age_s": null,
        "battery": null,
        "leak_state": false,
        "rssi": null,
        "fw_version": null,
        "location": {
          "code": "laundry",
          "label": "Washer"
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

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `data.reason` | string | always | Literal "fast". Treat this snapshot as PROVISIONAL: trust the valve block, but do not raise "sensor offline" alerts from it. Wait for the following reason:"commission" or reason:"boot"/"heartbeat" snapshot before deciding a sensor is genuinely missing. |
| `data.system_health.rating` | string enum | always | Frequently "critical" in a fast snapshot purely because unheard sensors are rated critical until their first check-in (health_engine.c:468, 483 seed every device as CRITICAL on load). Suppress health notifications derived from reason:"fast". |
| `data.lora_sensors[].last_seen_age_s` | null | always present, typically NULL here | null means "not heard yet this uptime", which at 25 s uptime is the normal case, not a fault. |
| `data.ble_leak_sensors[].fw_version` | null | always present, typically NULL here | null until the sensor's first advertisement carrying a version string is received. |


#### `snapshot (reason="decommission") — final cleared-state snapshot before reboot`

| | |
|---|---|
| **Envelope `type`** | `snapshot` |
| **Source** | `main/iothub/app_iothub.c:1568` |

**Trigger.** Exactly one snapshot, emitted after a C2D `decommission` command with payload target:"all" succeeds. This is the ONLY snapshot not produced by the scheduler's flush block.

Sequence (app_iothub.c:772-804, 1561-1572): the C2D handler (esp-mqtt event task) wipes provisioning NVS, sensor_meta, hub identity name, the DPS cache and the rules-engine override/incident NVS; clears the valve target MAC and requests a BLE disconnect; reloads the health table so it is EMPTY; publishes the cmd_ack; then sets g_decommission_reboot and wakes iothub_task. iothub_task waits up to ~1 s (20 x 50 ms) for the valve BLE link to actually drop so the snapshot shows the valve gone rather than lingering as connected, publishes this snapshot directly via telemetry_v2_publish_snapshot("decommission"), waits 3 s to let esp-mqtt flush, then esp_restart().

Because the reboot follows 3 s later, this snapshot is best-effort: it is a plain QoS-1 publish with no delivery confirmation, and it is DROPPED (not buffered) if MQTT happens to be down. After the reboot the hub is unprovisioned, so it re-registers with DPS and publishes NO further snapshots until it is provisioned again.

NOTE: partial decommissions (target "valve", "lora", "ble") do NOT use this path — they call arm_commission_snapshot() and surface as reason:"commission".

```json
{
  "schema": "eflostop.v2",
  "ts": 1785361200,
  "gateway": {
    "id": "GW-F412FA2C28C0",
    "short_id": "28C0",
    "fw": "1.8.0",
    "uptime_s": 4021
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

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `data.reason` | string | always | Literal "decommission". Your signal that this hub has been factory-reset and will reboot within ~3 s. Expect no further telemetry until it is re-provisioned. |
| `gateway.name` | (absent) | conditional-OMITTED — always omitted here | hub_identity_clear() runs before this snapshot is built (app_iothub.c:776), so the name key is gone even if the hub had one. gateway.id and gateway.short_id are unaffected because they are derived from the eFuse MAC, not NVS. |
| `data.system_health.rating` | string | always | Always "excellent" here: the health table is empty, and recalc_system_rating() over zero in-use devices yields HEALTH_EXCELLENT (health_engine.c:166-178). Do NOT read this as "the installation is healthy". |
| `data.system_health.reason` | string | always | Always the literal "All devices healthy", for the same reason — build_system_health_reason short-circuits on an excellent rating (telemetry_v2.c:161-164). |
| `data.valve` | object | always | Minimal 2-key form {"state":"disconnected","connected":false}. `mac` is omitted because the BLE link was dropped AND the valve no longer exists in the health table; `rating` and `last_seen_age_s` are omitted for the same reason. This is the smallest valve object the firmware can emit. |
| `data.lora_sensors` | array (empty) | always | Always []. The health table was reloaded from the now-empty provisioning config before this snapshot was built. |
| `data.ble_leak_sensors` | array (empty) | always | Always []. |
| `data.rules` | object | always present here | Present with the post-decommission DEFAULTS {"auto_close_enabled":true,"trigger_mask":7} — provisioning_decommission() explicitly re-seeds these in RAM after memset (provisioning_manager.c:546-547), so this block does not disappear. |
| `data.override_active` | boolean | always | Always false: rules_engine_clear_persistent_state() wipes the override window and incident latch from NVS before the snapshot (app_iothub.c:778). |


**Integration notes for this family**

- EMPTY SENSOR ARRAY IS AMBIGUOUS. `lora_sensors: []` and `ble_leak_sensors: []` mean EITHER "none provisioned" OR "the health engine's 1-second mutex timed out". Both array loops are wrapped in `if (have_health)` (telemetry_v2.c:462, 519) and health_get_device_status_all() returns false on mutex timeout (health_engine.c:611-613). Disambiguate via data.system_health.reason == "Health data unavailable", which is emitted from exactly the same failure (telemetry_v2.c:396-400). In that same failure the valve block also silently loses its `rating` and `last_seen_age_s` keys and may lose `mac`. Never delete a device from your model because a single snapshot omitted it — require two consecutive snapshots, or check for the "Health data unavailable" marker.
- leak_state IS NEVER null AND DEFAULTS TO false. For every sensor element, `battery`, `rssi`, `snr` and `fw_version` become JSON null when there is no live data, but `leak_state` is hard-coded `false` (telemetry_v2.c:506, 562). An offline or never-heard sensor therefore reports `"connected":false, "leak_state":false` — which is NOT evidence that the area is dry. ALWAYS gate leak_state on connected==true. This is the single most dangerous misread available in this payload.
- THE VALVE OBJECT CHANGES SHAPE, IT DOES NOT NULL OUT. When connected:false, the keys `battery`, `leak_state`, `rmleak` and `fw_version` are ABSENT — not null (telemetry_v2.c:427-444). Code that reads `valve.rmleak` without an existence check will see undefined/None and may coerce it to false, silently enabling an Open control on a valve that is actually latched. Branch on `valve.connected` first. Separately, `valve.fw_version` is the only field in the entire snapshot with three presence states (string / null / absent).
- valve.state:"disconnected" IS NOT A POSITION. It replaces open/closed/unknown when the BLE link is down (telemetry_v2.c:442) and means "position unknown". Rendering it as closed will tell users their water is off when it may be on. Likewise `"unknown"` (raw GATT byte neither 0 nor 1, including the -1 not-yet-read sentinel) is not a position.
- data.reason IS ONLY SIX VALUES AND DOES NOT SAY WHICH EVENT FIRED IT. The finer sub-label ("leak_detected", "valve_state_changed", "rules", "health", "sensor_meta", "commission-refresh", ...) lives in s_snap_evt and is written ONLY to the UART log (app_iothub.c:1878). On the wire you get bare "event". To attribute a snapshot, correlate with the type:"event" message published immediately before it — the firmware guarantees that ordering (the flush block runs after all event publishers, app_iothub.c:1852-1857).
- expires_ts IS FLAT AND NOT PREFIXED. The absolute override expiry is `data.expires_ts`, sitting as a sibling of `data.override_active` — NOT `data.override.expires_ts` and NOT `data.override_expires_ts` (telemetry_v2.c:599). It is omitted when the window is active but already past its expiry (the override_remaining_s==0 case). Prefer expires_ts over override_remaining_s for countdowns: remaining_s is computed at build time and is wrong by however long the message took to arrive.
- SNAPSHOTS ARE DROPPED OFFLINE, EVENTS ARE BUFFERED. publish_json() buffers to the NVS ring only when type_hint == "event" (telemetry_v2.c:115-118); snapshots and lifecycle messages hit the else branch and are discarded (telemetry_v2.c:119-122). You will therefore NEVER receive a stale/back-dated snapshot, but you WILL have snapshot gaps across every outage. Reconstruct what happened during a gap from the replayed events, then let the first post-reconnect snapshot (reason "fast" or "boot") re-baseline your state.
- reason:"fast" LOOKS LIKE A MASS OUTAGE AND ISN'T. It is published 20-30 s after boot/reconnect and deliberately bypasses the value-completeness gate (app_iothub.c:1868). Sensors that have not beaconed yet show connected:false, rating:"critical", nulls everywhere, and system_health.rating is often "critical" — because health_engine seeds every provisioned device as CRITICAL until its first check-in (health_engine.c:468, 483). Suppress user-visible offline/health alerts derived from reason:"fast", and expect a burst of reason:"commission" snapshots over the next 6 minutes that progressively fill the gaps.
- HEARTBEATS ARE NOT ON A WALL-CLOCK GRID. The deadline is last CONFIRMED publish + interval, on a monotonic esp_timer base (app_iothub.c:596-603). Any event-coupled, boot, fast or commission snapshot resets it, so the observed gap between consecutive snapshots ranges from 5 s (SNAP_MIN_INTERVAL_MS floor) to the full interval. Do not build a "missed heartbeat" alarm on exact 300 s spacing; allow at least 2x the interval plus slack, and remember the interval itself is Twin-tunable to anywhere in 60..3600 s.
- UPTIME-SCOPED FIELDS RESET ON EVERY REBOOT *AND* EVERY PROVISIONING CHANGE. `last_seen_age_s`, `connected`, `rating` and `ever_seen` all derive from health-engine state that health_engine_reload_devices() wipes wholesale (health_engine.c:444, 490-497). A `provision` or partial `decommission` therefore makes every device briefly look offline/critical with last_seen_age_s:null, even though nothing physically changed. The firmware compensates for the valve only (reseed_valve_health_if_connected, app_iothub.c:552-560); sensors genuinely have to be re-heard. Treat any snapshot with reason "commission"/"boot"/"fast" as a re-baseline, not as a fleet of new faults.
- BATTERY IS EMITTED RAW — 0xFF IS NOT MAPPED TO null HERE. The snapshot writes cached->battery straight out (telemetry_v2.c:499, 553). The health engine internally treats 0xFF as "unknown" and skips it when rating, and health_alert_to_json() omits the key when it is 0xFF (health_engine.c:591-593) — but this builder does neither, so a sensor reporting 0xFF would appear as battery:255. Clamp/validate to 0..100 on ingest. Separately, valve.battery can read 0 on a freshly-connected valve: g_val_battery initialises to 0 and is reset to 0 on every disconnect (app_ble_valve.c:134, 1209, 1264), so 0 means "not read yet" as often as it means "flat".
- snr IS A FLOAT, rssi IS SIGNED. lora_sensors[].snr comes from a C float and serialises with a decimal part (e.g. 9.5, -3.25) — an integer-only parser will throw or truncate. rssi on both sensor types is a signed int8 (typically -40..-120). And note the array asymmetry: `snr` exists ONLY on LoRa elements, `fw_version` exists ONLY on BLE elements. Do not write one shared strict sensor schema for both arrays.
- location.label IS "" AND location.code IS "unknown" — NEVER null, NEVER ABSENT. add_location_obj always emits both keys (telemetry_v2.c:143-153). Falling back on `label ?? code` will silently pick "" over a perfectly good code, because "" is not null. Test for emptiness, not for null. location.code is a closed 13-value enum (sensor_meta.c:23-27); unrecognised codes cannot occur, and out-of-range values coerce to "unknown".
- override_active CAN FALSE-NEGATIVE. rules_engine_get_override_status() returns active=false if the rules engine is uninitialised or its 1-second mutex times out (rules_engine.c:1187) — indistinguishable on the wire from a genuinely inactive window. Cross-check against the water_access_override_enabled / water_access_override_expired / auto_close_reenabled events rather than treating a single snapshot's override_active:false as authoritative. Corollary: override_remaining_s is present exactly when override_active is true and omitted exactly when it is false, so its presence is not independent information.
- data.rules CAN VANISH ENTIRELY. The whole object is omitted (not nulled) when provisioning_get_rules_config() fails on a mutex timeout (telemetry_v2.c:578). Cache the last-known rules rather than resetting your UI to defaults when the block is missing. Its shape is intentionally byte-identical to the lifecycle message's `rules` object, so reuse one parser for both. trigger_mask is a bitmask (bit0=BLE, bit1=LoRa, bit2=valve flood; 7=all), not an enum.
- NOTHING IS EMITTED BEFORE SNTP, AND NOTHING WHILE UNPROVISIONED. build_envelope() returns NULL below ts 1704067200 (telemetry_v2.c:70-74), and the whole publish phase is skipped when unprovisioned (app_iothub.c:1672-1686). A hub that boots without internet does its leak detection and valve auto-close normally and reports NOTHING — silence is not evidence of a healthy or an unhealthy site.
- reason:"decommission" IS FIRE-AND-FORGET AND MAY NEVER ARRIVE. It is a plain QoS-1 publish followed by esp_restart() 3 s later (app_iothub.c:1568-1571), with no delivery check and no offline buffering. Do not make it the sole trigger for de-registering a hub in your backend; also handle "hub stopped reporting and then re-appeared as unprovisioned". Its content is also deceptive in isolation: system_health.rating is "excellent" and reason is "All devices healthy" purely because the device table is empty.
- MESSAGE VOLUME CEILING. The min-interval clamp is 5 s for EVENT/HEARTBEAT (SNAP_MIN_INTERVAL_MS), so <= 12 snapshots/min from those paths — but COMMISSION, BOOT and FAST bypass the clamp entirely (app_iothub.c:614-622) and are only bounded by the 6-minute refresh window and the number of devices. Worst realistic case is a burst of a few snapshots per minute for ~6 min after a commission. Size ingest accordingly; a full 16+16-sensor snapshot is several kilobytes of JSON.
- TWIN REPORTED PROPERTIES ARE A DIFFERENT PATH AND A DIFFERENT SHAPE. publish_twin_reported() (app_iothub.c:942-993) sends to $iothub/twin/PATCH/properties/reported/?$rid=N, NOT to the events topic, and carries a FLAT object (fw_version, gateway_id, short_id, hub_name, provisioned, valve_mac?, lora_sensor_count, ble_leak_sensor_count, auto_close_enabled?, trigger_mask?, uptime_s, free_heap) with no schema/ts/type envelope. Note hub_name there is always present and may be the empty string, whereas gateway.name in the snapshot is omitted when unset. Never merge the two shapes.


---

## 4. Valve and leak events (type="event")

### Scope

This family covers the **five** device-to-cloud event names produced by the two parameterised event builders:

| Builder | `file:line` | Event names it produces |
|---|---|---|
| `telemetry_v2_publish_valve_event(event_name)` | `main/telemetry/telemetry_v2.c:608-629` | `valve_state_changed`, `valve_flood_detected`, `valve_flood_cleared` |
| `telemetry_v2_publish_leak_event(event_name, source_type, sensor_id, leak_state, battery, rssi)` | `main/telemetry/telemetry_v2.c:631-654` | `leak_detected`, `leak_cleared` |

**There are exactly four call sites in the entire repository** (verified by `grep -rn "publish_valve_event\|publish_leak_event" --include=*.c --include=*.h`), all inside the single `iothub_task` event loop in `main/iothub/app_iothub.c`:

| `file:line` | Builder | Event name(s) |
|---|---|---|
| `app_iothub.c:1745-1748` | leak | `leak_detected` / `leak_cleared`, `source_type="lora"` |
| `app_iothub.c:1776-1778` | valve | `valve_flood_detected` / `valve_flood_cleared` |
| `app_iothub.c:1787` | valve | `valve_state_changed` (literal) |
| `app_iothub.c:1805-1809` | leak | `leak_detected` / `leak_cleared`, `source_type="ble_leak_sensor"` |

No other module (rules engine, health engine, C2D handler) calls either builder. The rules engine and health engine have their own builders and are a different family.

### Transport

All five go to `devices/<device_id>/messages/events/`, **QoS 1**, retain 0 (`telemetry_v2.c:111`). Envelope `schema` is `"eflostop.v2"`, `type` is `"event"`.

### Envelope (identical for all five)

Built by `build_envelope("event")` at `telemetry_v2.c:59-92`:

```
schema, ts, gateway{id, short_id, name?, fw, uptime_s}, type, data{...}
```

`gateway.name` is the only conditional envelope field — **omitted entirely** (not null) when the user has never assigned a hub name (`telemetry_v2.c:82-83`).

### Two distinct data shapes

**Valve events** carry `valve_state` / `battery` / `leak_state` / `rmleak` / `fw_version?`. They carry **no** `location` object and **no** `sensor_id` (the valve is identified implicitly — there is exactly one per hub; its MAC appears only in snapshots).

**Leak events** carry `source_type` / `sensor_id` / `leak_state` / `battery` / `rssi` / `location{code,label}`. They carry **no** `fw_version` and **no** `snr`, even though the hub holds both in its sensor caches — those surface only in `type="snapshot"`.

### `source_type` — exact enum

The parameter is a free-form `const char *`, but only two literals are ever passed:

- `"lora"` — `app_iothub.c:1747`
- `"ble_leak_sensor"` — `app_iothub.c:1807`

Inside the builder, `source_type` is also used as a discriminator for metadata lookup: `strcmp(source_type, "lora") == 0 ? SENSOR_TYPE_LORA : SENSOR_TYPE_BLE_LEAK` (`telemetry_v2.c:648-649`). Anything that is not exactly `"lora"` is treated as a BLE sensor.

### `location{code,label}` — always present on leak events

`add_location_obj()` (`telemetry_v2.c:143-153`) **unconditionally** creates the `location` object with **both** keys as strings. It is never omitted and never null.

When `sensor_meta_find()` returns NULL, the fallback is `{"code":"unknown","label":""}` — `code` from `sensor_meta_location_code_to_str(LOC_UNKNOWN)`, `label` from the literal `""`.

`code` is one of exactly 13 strings (`sensor_meta.c:23-27`, asserted against `LOC_COUNT`):
`unknown`, `bathroom`, `kitchen`, `laundry`, `garage`, `garden`, `basement`, `utility`, `hallway`, `bedroom`, `living_room`, `attic`, `outdoor`

`label` is the user-assigned free-text label, max 31 chars + NUL (`SENSOR_META_LABEL_MAX 32`).

### Delta-gating summary

Nothing in this family is time-rate-limited; each event publishes immediately. Volume is bounded purely by **change detection**, in two stages for leak sensors and two different mechanisms for the valve. See each message's `trigger` field and the gotchas.


#### `valve_state_changed`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/iothub/app_iothub.c:1787` |

**Trigger.** Emitted when the hub's cached valve position changes. Chain: valve GATT NOTIFY on the valve-state characteristic -> app_ble_valve.c:463-469 on_notify() compares old vs new and enqueues BLE_UPD_STATE onto ble_update_queue ONLY if (old_state != g_val_state && !g_setup_in_progress); iothub_task dequeues it (app_iothub.c:1620-1621) and applies a SECOND delta-gate s_valve_pub_state (app_iothub.c:1785-1789, declared :168, sentinel -2 = nothing published yet). ALSO fired optimistically by the hub's OWN GATT write: write_valve_command() sets g_val_state = the value written and calls notify_hub_update(BLE_UPD_STATE) as soon as ble_gattc_write_flat returns rc==0 (app_ble_valve.c:1523-1527), and likewise on the deferred/pending-command path (app_ble_valve.c:668-672) - so a cloud open/close, or a rules-engine auto-close, produces this event before the valve has confirmed anything. GATE: requires the connected valve MAC to equal the provisioned MAC (app_iothub.c:1763-1772) or nothing is published. The s_valve_pub_state gate is reset to -2 on BLE_UPD_CONNECTED (app_iothub.c:1652-1656) so the first real state change after a (re)connect always emits. NO debounce, NO rate limit. Also arms a coupled snapshot: snap_request(SNAP_EVENT, SNAP_TIER_HIGH, "valve_state_changed") (app_iothub.c:1788).

```json
{
  "schema": "eflostop.v2",
  "ts": 1785312045,
  "gateway": {
    "id": "GW-3C8427A128C0",
    "short_id": "28C0",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 48213
  },
  "type": "event",
  "data": {
    "event": "valve_state_changed",
    "valve_state": "closed",
    "battery": 87,
    "leak_state": false,
    "rmleak": true,
    "fw_version": "2.2.0"
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `schema` | string | always | Constant "eflostop.v2" (TELEMETRY_SCHEMA, telemetry_v2.h:14). Added at telemetry_v2.c:64. |
| `ts` | number (integer seconds) | always | Unix epoch SECONDS (not ms) UTC from time(), captured when the message is BUILT (telemetry_v2.c:66-76). For events replayed from the offline NVS ring this is the original occurrence time, which can be hours before the broker receives it. |
| `gateway.id` | string | always | Immutable hub identity "GW-" + 12 uppercase hex of the WiFi STA MAC (eFuse). Also the Azure DPS registration ID. |
| `gateway.short_id` | string | always | 4 uppercase hex chars = last two MAC bytes (hub_identity.c:37, snprintf "%02X%02X", mac[4], mac[5]). Same value shown in the setup AP SSID "WiFi-Hub-XXXX". |
| `gateway.name` | string | conditional-OMITTED | User-assigned hub name (Twin desired hub_name or C2D set_hub_name, max 31 chars). The KEY IS ABSENT when no name has been set - it is never emitted as null (telemetry_v2.c:81-83). |
| `gateway.fw` | string | always | HUB firmware version from the ESP-IDF app descriptor (PROJECT_VER in CMakeLists.txt, currently "1.8.0"). Do not confuse with data.fw_version, which is the VALVE's firmware. |
| `gateway.uptime_s` | number (integer seconds) | always | Seconds since hub boot (esp_timer_get_time()/1000000). Monotonic; resets to ~0 on reboot. |
| `type` | string | always | Constant "event" for this whole family. |
| `data.event` | string | always | Literal "valve_state_changed" (the only hardcoded event name at a valve call site). |
| `data.valve_state` | string | always | One of exactly "open" \| "closed" \| "unknown", from ble_valve_get_state() mapped st==1?"open":st==0?"closed":"unknown" (telemetry_v2.c:616-618). NOTE the key is valve_state here, but valve.state in snapshots - and snapshots add a 4th value "disconnected" that events NEVER carry. |
| `data.battery` | number (integer 0-100) | always | Valve battery percent from ble_valve_get_battery(). Emitted as 0 (NOT null) when the battery characteristic has not been read yet - g_val_battery is initialised/reset to 0 (app_ble_valve.c:134, 1209, 1264). |
| `data.leak_state` | boolean | always | The VALVE's OWN onboard flood sensor (the wet-contact under the valve), not a remote leak sensor. From ble_valve_get_leak(). |
| `data.rmleak` | boolean | always | Remote-leak interlock latch on the valve. true = the hub has asserted RMLEAK so the valve refuses to reopen. Cached value last read/notified from the valve (ble_valve_get_rmleak_state()). |
| `data.fw_version` | string | conditional-OMITTED | VALVE firmware revision read from BLE DIS 0x180A / char 0x2A26, e.g. "2.2.0". The KEY IS ABSENT when the DIS read has not succeeded (telemetry_v2.c:623-625 only adds it if ble_valve_get_firmware_rev() returns true, which is false when g_firmware_rev is empty - app_ble_valve.c:1947-1951). CONTRAST: snapshots emit valve.fw_version as explicit NULL in the same situation (telemetry_v2.c:440). |


#### `valve_flood_detected`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/iothub/app_iothub.c:1776` |

**Trigger.** The valve's OWN onboard flood sensor transitioned dry -> wet. Chain: valve NOTIFY on the flood characteristic -> app_ble_valve.c:471-478 enqueues BLE_UPD_LEAK only if (old_leak != g_val_leak && !g_setup_in_progress) - i.e. the delta-gate lives entirely in the BLE layer; there is NO second hub-side gate for flood (unlike valve_state_changed). iothub_task picks BLE_UPD_LEAK and chooses the name by re-reading the cache: ble_valve_get_leak() ? "valve_flood_detected" : "valve_flood_cleared" (app_iothub.c:1775-1779). GATE: requires connected valve MAC == provisioned MAC (app_iothub.c:1763-1772). The same BLE_UPD_LEAK also drives rules_engine_evaluate_leak(LEAK_SOURCE_VALVE_FLOOD, ...) earlier in the same loop iteration (app_iothub.c:1646-1649), so an auto-close will produce a rules-engine event AND a valve_state_changed in the same burst. NO debounce, NO rate limit. Arms a coupled snapshot at SNAP_TIER_HIGH (app_iothub.c:1779).

```json
{
  "schema": "eflostop.v2",
  "ts": 1785312101,
  "gateway": {
    "id": "GW-3C8427A128C0",
    "short_id": "28C0",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 48269
  },
  "type": "event",
  "data": {
    "event": "valve_flood_detected",
    "valve_state": "open",
    "battery": 87,
    "leak_state": true,
    "rmleak": false,
    "fw_version": "2.2.0"
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `schema` | string | always | Constant "eflostop.v2". |
| `ts` | number (integer seconds) | always | Unix epoch seconds UTC at build time. |
| `gateway.id` | string | always | "GW-" + 12 uppercase hex of the WiFi STA MAC. |
| `gateway.short_id` | string | always | 4 uppercase hex chars (last two MAC bytes). |
| `gateway.name` | string | conditional-OMITTED | User hub name; key absent (never null) when unset. |
| `gateway.fw` | string | always | Hub firmware version, e.g. "1.8.0". |
| `gateway.uptime_s` | number (integer seconds) | always | Seconds since hub boot. |
| `type` | string | always | Constant "event". |
| `data.event` | string | always | Literal "valve_flood_detected". Chosen at app_iothub.c:1776 by ble_valve_get_leak() == true. |
| `data.valve_state` | string | always | "open" \| "closed" \| "unknown". Note this is the position AT THE MOMENT THE FLOOD WAS REPORTED - it will usually still be "open", because the auto-close write happens separately and reports its own valve_state_changed event. |
| `data.battery` | number (integer 0-100) | always | Valve battery percent; 0 (not null) if never read. |
| `data.leak_state` | boolean | always | Always true for this event name - fully redundant with data.event. |
| `data.rmleak` | boolean | always | Remote-leak interlock latch as cached at build time. Typically still false here; the hub asserts RMLEAK a moment later as part of the auto-close sequence. |
| `data.fw_version` | string | conditional-OMITTED | Valve DIS firmware revision; KEY ABSENT (not null) when the DIS read has not succeeded. |


#### `valve_flood_cleared`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/iothub/app_iothub.c:1777` |

**Trigger.** The valve's OWN onboard flood sensor transitioned wet -> dry. Identical mechanism and identical call site as valve_flood_detected - the two names are the two arms of one ternary at app_iothub.c:1776-1777. BLE-layer delta-gate only (app_ble_valve.c:476-477). GATE: connected valve MAC == provisioned MAC. NO debounce, NO rate limit. Arms a coupled snapshot at SNAP_TIER_HIGH (app_iothub.c:1779). This event does NOT by itself mean water access was restored - it only means the valve's local wet contact dried. Reopening is governed by the rules engine / RMLEAK and reported by its own events.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785315790,
  "gateway": {
    "id": "GW-3C8427A128C0",
    "short_id": "28C0",
    "fw": "1.8.0",
    "uptime_s": 51958
  },
  "type": "event",
  "data": {
    "event": "valve_flood_cleared",
    "valve_state": "closed",
    "battery": 86,
    "leak_state": false,
    "rmleak": true
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `schema` | string | always | Constant "eflostop.v2". |
| `ts` | number (integer seconds) | always | Unix epoch seconds UTC at build time. |
| `gateway.id` | string | always | "GW-" + 12 uppercase hex of the WiFi STA MAC. |
| `gateway.short_id` | string | always | 4 uppercase hex chars (last two MAC bytes). |
| `gateway.name` | string | conditional-OMITTED | User hub name. ABSENT in this example because no name was set - that is the normal wire form, not an error. |
| `gateway.fw` | string | always | Hub firmware version, e.g. "1.8.0". |
| `gateway.uptime_s` | number (integer seconds) | always | Seconds since hub boot. |
| `type` | string | always | Constant "event". |
| `data.event` | string | always | Literal "valve_flood_cleared". Chosen at app_iothub.c:1777 by ble_valve_get_leak() == false. |
| `data.valve_state` | string | always | "open" \| "closed" \| "unknown" at build time. Usually "closed" here, because the earlier flood already auto-closed the valve. |
| `data.battery` | number (integer 0-100) | always | Valve battery percent; 0 (not null) if never read. |
| `data.leak_state` | boolean | always | Always false for this event name - fully redundant with data.event. |
| `data.rmleak` | boolean | always | Remote-leak interlock latch. Often still true here: the valve stays interlocked until the rules engine or a user action clears it. |
| `data.fw_version` | string | conditional-OMITTED | Valve DIS firmware revision; KEY ABSENT (not null) when the DIS read has not succeeded. Absent in this example. |


#### `leak_detected`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/iothub/app_iothub.c:1746` |

**Trigger.** A provisioned leak sensor transitioned dry -> wet. TWO SOURCES, TWO CALL SITES, IDENTICAL FIELD SET.

(A) LoRa - app_iothub.c:1745-1748, source_type="lora". A LoRa frame is received, decrypted and CRC/MAC-verified (app_lora.cpp:287 decode_frame) then queued on lora_rx_queue (depth 10, app_lora.cpp:231/292). iothub_task requires provisioning_is_lora_sensor_provisioned(pkt.sensorId) (app_iothub.c:1736) then applies the delta-gate update_lora_cache_check_leak() (app_iothub.c:264-301), which publishes ONLY when the cached leakStatus differs from the new one. There is NO scanner-side gate for LoRa - every uplink reaches the cache gate.

(B) BLE ("eleak" STM32WBA/WB sensor) - app_iothub.c:1805-1809, source_type="ble_leak_sensor". TWO STAGES: (1) scanner-side gate app_ble_leak.c:187-195 enqueues only when leak, battery or fw_version changed, OR a 5-minute heartbeat is due (BLE_LEAK_HEARTBEAT_MS, app_ble_leak.c:37) - the heartbeat deliberately re-sends UNCHANGED data for the health engine; (2) hub-side gate update_ble_leak_cache_check_leak() (app_iothub.c:307-351) publishes only when leak_state actually changed, so heartbeats and battery-only changes produce NO leak event. The sensor must also be on the commissioned whitelist (app_ble_leak.c:158-162) and advertise device name "eleak" with ST company ID 0x0030.

FIRST-SIGHTING RULE (both sources): when a sensor occupies a fresh cache slot, the gate returns "leaking?" - so a first sighting in the WET state emits leak_detected, and a first sighting in the DRY state emits NOTHING (app_iothub.c:289, 336).

NO debounce, NO rate limit, NO coalescing on the event itself. Arms a coupled snapshot at SNAP_TIER_HIGH (app_iothub.c:1749-1750 / 1810-1811).

```json
{
  "schema": "eflostop.v2",
  "ts": 1785312088,
  "gateway": {
    "id": "GW-3C8427A128C0",
    "short_id": "28C0",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 48256
  },
  "type": "event",
  "data": {
    "event": "leak_detected",
    "source_type": "lora",
    "sensor_id": "0x754A6237",
    "leak_state": true,
    "battery": 92,
    "rssi": -104,
    "location": {
      "code": "basement",
      "label": "Water heater"
    }
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `schema` | string | always | Constant "eflostop.v2". |
| `ts` | number (integer seconds) | always | Unix epoch seconds UTC at build time. For a leak that occurred while the hub was offline, this is the ORIGINAL time - the message is replayed later from the NVS ring. |
| `gateway.id` | string | always | "GW-" + 12 uppercase hex of the WiFi STA MAC. |
| `gateway.short_id` | string | always | 4 uppercase hex chars (last two MAC bytes). |
| `gateway.name` | string | conditional-OMITTED | User hub name; key absent (never null) when unset. |
| `gateway.fw` | string | always | Hub firmware version, e.g. "1.8.0". |
| `gateway.uptime_s` | number (integer seconds) | always | Seconds since hub boot. |
| `type` | string | always | Constant "event". |
| `data.event` | string | always | Literal "leak_detected" (app_iothub.c:1746 for LoRa, :1806 for BLE). |
| `data.source_type` | string | always | Exactly "lora" (app_iothub.c:1747) or "ble_leak_sensor" (app_iothub.c:1807). No other value is ever produced. Also used internally as the metadata-table discriminator: anything != "lora" is treated as BLE (telemetry_v2.c:648-649). |
| `data.sensor_id` | string | always | Format DEPENDS on source_type. lora: "0x%08lX" = lowercase "0x" + 8 UPPERCASE hex digits, e.g. "0x754A6237" (app_iothub.c:1743-1744). ble_leak_sensor: UPPERCASE colon-separated MAC "XX:XX:XX:XX:XX:XX" (app_ble_leak.c:86-90). The hub compares these case-insensitively internally (strcasecmp), so the backend should too. This is the join key to the lora_sensors[].sensor_id / ble_leak_sensors[].sensor_id arrays in snapshots. |
| `data.leak_state` | boolean | always | Always true for this event name - 100% redundant with data.event (LoRa: (pkt.leakStatus != 0); BLE: evt.leak_detected). Both the name and this flag derive from the same value in the same statement. |
| `data.battery` | number (integer) | always | Raw battery percentage from the sensor payload, normally 0-100. LoRa: pkt.batteryPercentage (uint8_t). BLE: manufacturer-data byte [3] (uint8_t). Emitted as a plain number, never null - a sensor that has not reported a battery simply never generates this event. |
| `data.rssi` | number (integer, signed) | always | Received signal strength in dBm as measured BY THE HUB, int8_t so normally negative (e.g. -104). LoRa: SX1262 packet RSSI (app_lora.cpp:274). BLE: advertisement RSSI (app_ble_leak.c:210). Never null. NOTE: snr is NOT included in leak events even for LoRa - it appears only in snapshots. |
| `data.location` | object | always | ALWAYS present on leak events (never omitted, never null) - add_location_obj() unconditionally creates it (telemetry_v2.c:143-153). NEVER present on valve events. |
| `data.location.code` | string | always | One of exactly 13 slugs: unknown \| bathroom \| kitchen \| laundry \| garage \| garden \| basement \| utility \| hallway \| bedroom \| living_room \| attic \| outdoor (sensor_meta.c:23-27). Falls back to "unknown" when no metadata row exists for this (type, sensor_id). |
| `data.location.label` | string | always | User-assigned free text, max 31 chars. Present as an EMPTY STRING "" (not null, not omitted) when no metadata row exists (telemetry_v2.c:151). |


#### `leak_cleared`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/iothub/app_iothub.c:1806` |

**Trigger.** A provisioned leak sensor transitioned wet -> dry. Exactly the same two call sites and the same two-stage gating as leak_detected - the two names are the two arms of one ternary (app_iothub.c:1746 for LoRa, :1806 for BLE). Because the gate is a pure leak_state delta, this fires once per wet->dry transition and never repeats while the sensor stays dry.

A first sighting in the DRY state emits NOTHING (the cache-insert paths return the leaking flag, app_iothub.c:289 / 336) - so the backend must NOT wait for leak_cleared to learn that a newly commissioned sensor is dry; that comes from the snapshot.

leak_cleared is only the SENSOR drying out. It does not imply the valve reopened or the interlock cleared - those are separate rules-engine events. NO debounce, NO rate limit. Arms a coupled snapshot at SNAP_TIER_HIGH.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785316402,
  "gateway": {
    "id": "GW-3C8427A128C0",
    "short_id": "28C0",
    "name": "Main House",
    "fw": "1.8.0",
    "uptime_s": 52570
  },
  "type": "event",
  "data": {
    "event": "leak_cleared",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:9A:E6",
    "leak_state": false,
    "battery": 88,
    "rssi": -67,
    "location": {
      "code": "unknown",
      "label": ""
    }
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `schema` | string | always | Constant "eflostop.v2". |
| `ts` | number (integer seconds) | always | Unix epoch seconds UTC at build time. |
| `gateway.id` | string | always | "GW-" + 12 uppercase hex of the WiFi STA MAC. |
| `gateway.short_id` | string | always | 4 uppercase hex chars (last two MAC bytes). |
| `gateway.name` | string | conditional-OMITTED | User hub name; key absent (never null) when unset. |
| `gateway.fw` | string | always | Hub firmware version, e.g. "1.8.0". |
| `gateway.uptime_s` | number (integer seconds) | always | Seconds since hub boot. |
| `type` | string | always | Constant "event". |
| `data.event` | string | always | Literal "leak_cleared" (app_iothub.c:1746 for LoRa, :1806 for BLE). |
| `data.source_type` | string | always | Exactly "lora" or "ble_leak_sensor" - the example above shows the BLE variant. |
| `data.sensor_id` | string | always | "0x" + 8 uppercase hex for lora; uppercase colon MAC "XX:XX:XX:XX:XX:XX" for ble_leak_sensor. Compare case-insensitively. |
| `data.leak_state` | boolean | always | Always false for this event name - 100% redundant with data.event. |
| `data.battery` | number (integer) | always | Raw battery percentage from the sensor payload, normally 0-100. Never null. |
| `data.rssi` | number (integer, signed) | always | Hub-measured signal strength in dBm, int8_t, normally negative. Never null. No snr field. |
| `data.location` | object | always | Always present with both keys; never omitted, never null. |
| `data.location.code` | string | always | One of the 13 location slugs; "unknown" when no metadata row exists for this sensor - as in the example above. |
| `data.location.label` | string | always | Empty string "" (NOT null, NOT omitted) when no metadata row exists - as in the example above. |


**Integration notes for this family**

- KEY NAME DIVERGENCE between events and snapshots. Valve events use data.valve_state; snapshots use data.valve.state. Same three values plus a FOURTH value "disconnected" that ONLY snapshots emit (telemetry_v2.c:442). A parser written against snapshots will not find valve_state, and vice versa.
- PRESENCE SEMANTICS DIVERGE for the valve firmware version. In valve EVENTS, data.fw_version is OMITTED when unknown (telemetry_v2.c:623-625 only adds the key when ble_valve_get_firmware_rev() returns true). In SNAPSHOTS the same unknown state is an explicit JSON null (cJSON_AddNullToObject, telemetry_v2.c:440). The backend must treat "key absent" and "key null" as the same meaning in this schema, because the firmware is inconsistent about which it uses.
- Leak events carry NO fw_version and NO snr, even though the hub caches both. The BLE sensor's fw_version (telem_ble_leak_cache_t.fw_version, parsed from mfg-data bytes [4..6]) and the LoRa snr surface ONLY in type="snapshot". Do not expect to learn sensor firmware from an event.
- data.leak_state is 100% redundant with data.event on all four leak/flood events - both derive from the same expression in the same statement. It is not an independent signal and can never disagree with the event name.
- data.location is present on EVERY leak event and on NO valve event. Both location.code and location.label are ALWAYS strings - the object is built unconditionally (telemetry_v2.c:143-153). "Unknown location" is encoded as {"code":"unknown","label":""}, never as a missing object and never as null.
- location.code == "unknown" does NOT prove the sensor is unlabeled. sensor_meta_find() also returns NULL when the metadata module has not been initialised or when its 1000 ms mutex wait times out (sensor_meta.c:157-177), producing the identical unknown/"" fallback. Treat a single "unknown" as low-confidence; trust the snapshot's steady-state value.
- valve_state_changed is OPTIMISTIC on hub-initiated writes. write_valve_command() sets the cached state to the value it wrote and emits the event as soon as ble_gattc_write_flat() returns rc==0 (app_ble_valve.c:1523-1527; also the deferred path at :668-672) - BEFORE any confirmation from the valve. The valve firmware can refuse to open while its own flood contact is wet, in which case there is no NOTIFY and the hub keeps reporting the wrong position until the next read/notify. Always reconcile against the following snapshot, do not treat this event as ground truth for actuation success.
- There is NO valve_state_changed at boot or reconnect. The delta-gate is reset to -2 on BLE_UPD_CONNECTED (app_iothub.c:1652-1656), but BLE_UPD_STATE is only enqueued on an actual change, and the initial GATT reads during setup are suppressed by g_setup_in_progress (app_ble_valve.c:468, and reads are routed through on_notify at :603). The INITIAL valve position is learned only from type="snapshot".
- A first sighting of a DRY sensor emits nothing. The cache-insert paths return the current leaking flag (app_iothub.c:289 for LoRa, :336 for BLE), so a newly commissioned or newly rebooted-hub sensor that is dry produces NO leak_cleared. Baseline state must come from the snapshot; events are transitions only.
- The BLE scanner's 5-minute heartbeat does NOT produce leak events. app_ble_leak.c:187-195 re-enqueues unchanged advertisements every BLE_LEAK_HEARTBEAT_MS purely to keep the health engine's last_seen fresh; the second gate (update_ble_leak_cache_check_leak) drops them because leak_state did not change. Likewise a battery-only change never produces a leak event - battery moves are visible only in snapshots.
- PRE-SNTP EVENTS ARE SILENTLY LOST, not buffered. build_envelope() returns NULL when the wall clock is below 1704067200 (telemetry_v2.c:69-74), so publish_json() is never reached and the offline ring is never touched. A leak occurring in the window between boot and the first successful SNTP sync produces NO cloud message at all - only a hub-side log line.
- OFFLINE RING CAN CORRUPT LARGE EVENTS. When MQTT is down, events (and only events - lifecycle/snapshot are dropped, telemetry_v2.c:115-122) go to a 16-entry NVS ring with a 512-byte cap. offline_buffer_store() TRUNCATES anything longer to exactly 512 bytes and stores it anyway (offline_buffer.c:77-81), so a long location.label plus a long hub name can produce an event that replays as INVALID JSON. The backend must tolerate an unparseable body on this topic. A 17th offline event silently overwrites the oldest (offline_buffer.c:103-105).
- Replayed offline events carry their ORIGINAL ts, which can be far behind broker-receive time. They are replayed FIFO immediately BEFORE the lifecycle message on reconnect (app_iothub.c:1691-1692, offline_buffer.c:137-163), so relative order among buffered events is preserved - but do not derive occurrence time from message-enqueued-time or arrival order across a reconnect boundary.
- NO IDEMPOTENCY KEY ANYWHERE IN THIS FAMILY. There is no sequence number, no message id, no correlation id on valve/leak events. Combined with QoS 1 (at-least-once) and the ring replay, the backend must de-duplicate on (gateway.id, data.event, data.sensor_id, ts) or accept duplicates. Note ts has 1-second resolution, so two genuine transitions within the same second are indistinguishable.
- SOURCE QUEUES DROP ON OVERFLOW, losing events permanently. ble_update_queue is only 5 deep (app_ble_valve.c:1790), lora_rx_queue and ble_leak_rx_queue are 10 (app_lora.cpp:231, app_ble_leak.c:391), and every producer uses xQueueSend(..., 0) with no retry. The iothub loop dequeues exactly ONE item per iteration (app_iothub.c:1616-1629). A burst of valve notifications or simultaneous sensor traffic can silently discard a transition - another reason the snapshot, not the event stream, is the authority on current state.
- MORE THAN 16 SENSORS OF ONE KIND CORRUPTS THE DELTA BASELINE. When the telemetry cache is full, both update helpers overwrite SLOT 0 unconditionally (app_iothub.c:293-300, 340-350). Two sensors then share one baseline, which can emit spurious leak_detected/leak_cleared pairs. TELEM_MAX_LORA_CACHE and TELEM_MAX_BLE_LEAK_CACHE are both 16 (telemetry_v2.h:26-27).
- VALVE EVENTS ARE FULLY SUPPRESSED ON MAC MISMATCH. If the currently connected valve MAC does not equal the provisioned MAC, or either lookup fails, mac_ok stays false and neither valve_state_changed nor valve_flood_* is published (app_iothub.c:1759-1774) - with only a hub log warning. Leak events have analogous gates: LoRa requires provisioning_is_lora_sensor_provisioned() (app_iothub.c:1736), BLE requires the MAC to be on the commissioned whitelist (app_ble_leak.c:158-162).
- EVERY event in this family also ARMS A SNAPSHOT (snap_request(SNAP_EVENT, SNAP_TIER_HIGH, ...) at app_iothub.c:1749, 1779, 1788, 1810). The snapshot is coalesced (300 ms HIGH window) and floor-clamped to last_publish + 5000 ms (app_iothub.c:149-151, 624-640), so a burst of leak+flood+state changes collapses into ONE snapshot. THE EVENTS THEMSELVES ARE NOT COALESCED OR RATE-LIMITED - each publishes immediately, so a rapidly toggling wet contact produces one message per toggle.
- ONE PHYSICAL INCIDENT PRODUCES SEVERAL MESSAGES ACROSS FAMILIES - do not double-count. A single sensor leak typically yields: leak_detected (this family) + auto_close (rules family) + valve_state_changed (this family, from the hub's own close write) + one coalesced snapshot. The valve's own wet contact yields valve_flood_detected + auto_close + valve_state_changed. Correlate on gateway.id and a short time window; there is no shared incident id on the wire.
- ts is in SECONDS, not milliseconds, and cJSON serialises it from a double - expect a bare integer like 1785312045, but a strict parser should accept a JSON number rather than assume an integer token.


---

## 5. Rules-engine events (device→cloud telemetry, `type: "event"`)

### What this family is

The rules engine is the hub's local safety brain. It decides when to shut the water off automatically, when *not* to (the 24-hour water-access override), and when to release the valve's remote-leak interlock (RMLEAK). Every one of those decisions emits exactly one telemetry event in this family.

There are **8 distinct payload shapes across 7 event-name strings** (`auto_close` is produced by two different builders with different fields), plus **1 defensive fallback shape** that should never appear on the wire.

#### Transport

All of these travel the same path as every other `eflostop.v2` telemetry message:

- MQTT topic `devices/<device_id>/messages/events/`, **QoS 1** — `publish_json`, `main/telemetry/telemetry_v2.c:111`
- Envelope `type` is always the literal `"event"` — `build_envelope("event")`, `main/telemetry/telemetry_v2.c:659`

#### The unusual bit: the payload is spliced, not built

Unlike the valve/leak/health/cmd_ack publishers, this family does **not** build its `data` object in the telemetry layer. `rules_engine.c` serialises a small JSON object into a heap string (`g_pending_telemetry`), and `telemetry_v2_publish_rules_event()` **parses that string and grafts it in as `data` verbatim** (`main/telemetry/telemetry_v2.c:662-665`).

Consequence for you: `data` contains **only** the keys the rules engine wrote. There are no injected common fields — no `sensor_id` on override events, no `location` on most of them, no `valve_state` anywhere. Treat each event name as its own record type.

#### Shared envelope (identical for all 9 shapes)

Built by `build_envelope()`, `main/telemetry/telemetry_v2.c:59-92`. Key order on the wire is insertion order: `schema`, `ts`, `gateway`, `type`, `data`.

| Path | Type | Presence | Meaning |
|---|---|---|---|
| `schema` | string | always | Frozen literal `"eflostop.v2"` (`telemetry_v2.h:14`) |
| `ts` | number | always | Unix epoch **seconds**, hub wall clock at build time (`telemetry_v2.c:76`) |
| `gateway.id` | string | always | `GW-<12 hex>`, derived from the WiFi STA MAC. Immutable, = the Azure DPS registration ID |
| `gateway.short_id` | string | always | Last 4 hex of `gateway.id`, e.g. `"28C0"` |
| `gateway.name` | string | **conditional — OMITTED** | User-assigned hub name. Key is absent entirely when unset (`telemetry_v2.c:82-83`) |
| `gateway.fw` | string | always | Hub firmware version from the ESP-IDF app descriptor; currently `1.8.0` (`CMakeLists.txt:12`) |
| `gateway.uptime_s` | number | always | Seconds since hub boot |
| `type` | string | always | `"event"` for this whole family |
| `data` | object | always | The rules-engine blob, documented per message below |

#### Hard prerequisite: valid wall clock

`build_envelope()` returns NULL and the message is **destroyed, not queued**, if `ts < 1704067200` (2024-01-01) — `telemetry_v2.c:66-74`. A rules decision taken before SNTP sync produces **no telemetry at all**, even though the state change (override window started, RMLEAK cleared, valve closed) has already happened and been persisted to NVS. The state is only recoverable from the next `snapshot`.

#### Offline behaviour

When MQTT is down but the hub is provisioned, `publish_json` sees `type_hint == "event"` and pushes the fully-formed JSON string into an NVS ring buffer (`telemetry_v2.c:115-118` → `offline_buffer_store`). Ring = **16 entries, 512 bytes each**, oldest silently overwritten when full (`offline_buffer.h:12-13`, `offline_buffer.c:103-105`). On reconnect the ring is replayed **FIFO, before the `lifecycle` "online" event** (`app_iothub.c:1691-1692`).

Replayed events keep their **original `ts` and original `gateway.uptime_s`**. Order your event log by `ts`, never by arrival time or IoT Hub enqueue time.

#### Cadence of the producers

Three contexts produce rules events:

1. **`rules_engine_tick()`** — `app_iothub.c:1611`, once per `iothub_task` event-loop iteration. The loop's blocking wait is `min(time-to-next-snapshot-deadline, 30000 ms)` (2000 ms while a boot/commission snapshot is pending) and it also wakes immediately on any LoRa packet, valve BLE update, BLE leak advertisement or snapshot trigger — so the **worst-case tick period is ~30 s**. All time-based events (auto-clear, window expiry, physical-override detection) inherit this latency.
2. **`rules_engine_evaluate_leak()`** — `app_iothub.c:1638` (LoRa), `:1642` (BLE leak), `:1647` (valve flood probe). Runs on the sensor event itself.
3. **`rules_engine_on_valve_connected()`** — `app_iothub.c:1653`, once when the valve's BLE/GATT setup completes.

Plus the **C2D command handler**, which runs on the *esp-mqtt event task* (a different thread) and can produce `rmleak_cleared`, `auto_close_reenabled` and `water_access_override_enabled(trigger:"c2d_command")` at any instant.

#### Coupled snapshot

Immediately after publishing a rules event, `app_iothub.c:1706-1707` requests an event-coupled `type:"snapshot"`. Every rules event gets `SNAP_TIER_HIGH` (300 ms coalescing window) **except** `auto_close_blocked_override`, which gets `SNAP_TIER_LOW` (2000 ms) — selected by a literal `strstr(json, "auto_close_blocked_override")`. Snapshots are floor-clamped to one per 5 s (`SNAP_MIN_INTERVAL_MS`, `app_iothub.c:151`), and the snapshot always publishes **after** the event in the same loop iteration. The snapshot arrives with `data.reason == "event"` (the string `"rules"` is a log label only — `snap_reason_str`, `app_iothub.c:584`).

So the normal wire sequence is: `event` → (≤5 s) → `snapshot` carrying the post-decision `override_active` / `valve.state` / `valve.rmleak`.

#### The one-slot pending buffer (read this before you build alerting)

`g_pending_telemetry` is a **single `char *`**. Every builder does `if (g_pending_telemetry) free(g_pending_telemetry);` before storing its own string (`rules_engine.c:219, 352, 475, 682, 723, 940, 1029, 1084`), and the consumer takes it exactly **once per event-loop iteration** (`rules_engine_take_pending_telemetry()`, `app_iothub.c:1660`).

**At most one rules event is ever published per loop iteration, and an earlier one built in the same iteration is silently discarded.** Within an iteration the producer order is tick (`:1611`) → evaluate_leak (`:1638-1647`) → on_valve_connected (`:1653`) → take (`:1660`), so *later wins*. Concretely: a `water_access_override_expired` produced by the tick can be overwritten by an `auto_close` from a LoRa packet processed in the same iteration and never reach the cloud. The asynchronous C2D thread can clobber, or be clobbered by, either.

Design your backend so that **`snapshot` is the source of truth for state** and these events are best-effort annotations explaining *why* the state changed.


#### `auto_close — variant A (sensor-triggered)`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/rules_engine/rules_engine.c:333` |

**Trigger.** A leak source reported wet and the hub decided to shut the water off. Produced by rules_engine_evaluate_leak() (main/rules_engine/rules_engine.c:391) only after EVERY one of these gates passes, in order: (1) engine initialised; (2) leak_active == true (a clearing report returns at :404 and never builds an event); (3) provisioning_is_provisioned() (:410); (4) a rules config is readable (:417); (5) rules.auto_close_enabled (:423); (6) the source's bit is set in rules.trigger_mask — BLE_LEAK=0x01, LORA=0x02, VALVE_FLOOD=0x04 (:432, provisioning_manager.h:22-24); (7) the 24 h override window is INACTIVE (an active window diverts to auto_close_blocked_override, :452); (8) NOT already (valve closed AND valve RMLEAK asserted) (:487) — a redundant re-trigger is silent; (9) the auto-close cooldown has elapsed. COOLDOWN: AUTO_CLOSE_COOLDOWN_MS = 10 000 ms (rules_engine.c:17), a single GLOBAL timer (g_last_auto_close_tick), and it only applies once g_auto_close_triggered is already true (:495). So the FIRST close of an incident is immediate; further closes within 10 s of the last one — from any sensor — are suppressed with no event. Upstream feed rates: LoRa packet arrival (app_iothub.c:1638), BLE leak advertisement (:1642, sensor advertises every 312-437 ms), valve flood-probe GATT notify (:1647). Note there is NO delta-gate on this path: rules_engine_evaluate_leak is called on every raw sensor report, wet or dry, so a sensor re-asserting 'wet' every 400 ms is throttled purely by gate 8 + the 10 s cooldown. Side effects after the event is queued: ble_valve_set_rmleak(true) then ble_valve_close() (:527-528, order is load-bearing so the resulting valve_state_changed reports rmleak:true); if the valve is disconnected it only calls ble_valve_connect() (:530) and the actual close happens later via variant B.

```json
{
  "schema": "eflostop.v2",
  "ts": 1769812345,
  "gateway": {
    "id": "GW-A0B7651828C0",
    "short_id": "28C0",
    "name": "Kitchen Hub",
    "fw": "1.8.0",
    "uptime_s": 48213
  },
  "type": "event",
  "data": {
    "event": "auto_close",
    "source_type": "lora",
    "sensor_id": "0x0A1B2C3D",
    "rmleak_asserted": true,
    "location": {
      "code": "basement",
      "label": "Water heater"
    }
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `data.event` | string | always | Frozen literal "auto_close" (rules_engine.c:333). Identical to variant B — you MUST branch on source_type / presence of active_leak_count to tell them apart. |
| `data.source_type` | string | always | Which kind of sensor triggered the shutoff. From source_to_str() (rules_engine.c:259-267): "ble_leak_sensor" \| "lora" \| "valve_flood" \| "unknown". "valve_flood" = the valve's own onboard water probe. "unknown" is unreachable given the current leak_source_t enum but the default branch exists. Same vocabulary as data.source_type on leak_detected/leak_cleared events, so one lookup table serves both. |
| `data.sensor_id` | string | always | Identifier of the triggering sensor: uppercase colon-separated MAC "XX:XX:XX:XX:XX:XX" for BLE leak sensors (app_ble_leak.c:88), "0x%08lX" uppercase-hex e.g. "0x0A1B2C3D" for LoRa sensors (app_iothub.c:1636), or the literal "valve" for the valve flood probe (app_iothub.c:1648). Falls back to the literal "unknown" if the caller passed NULL (rules_engine.c:335) — not reachable from any current call site. |
| `data.rmleak_asserted` | boolean | always | HARDCODED true (rules_engine.c:336). It records the hub's INTENT to set the valve's remote-leak interlock, not a confirmed valve response. If the valve is offline the BLE write never happens (:529-531) yet this still reads true. Do not treat it as valve acknowledgement — read valve.rmleak from the coupled snapshot for that. |
| `data.location` | object | conditional — OMITTED | User-assigned room metadata for the triggering sensor. The whole object is absent when source_id is NULL, OR source == LEAK_SOURCE_VALVE_FLOOD (the valve has no metadata entry), OR sensor_meta_find() has no entry for that sensor (rules_engine.c:339-349). NOTE the asymmetry: the leak_detected/leak_cleared events use add_location_obj() which ALWAYS emits location with code:"unknown", label:"" — this builder omits the key entirely instead. Handle both. |
| `data.location.code` | string | always (when data.location present) | Machine-readable room slug, one of the 13 values in s_location_strings (sensor_meta.c:23-27): "unknown", "bathroom", "kitchen", "laundry", "garage", "garden", "basement", "utility", "hallway", "bedroom", "living_room", "attic", "outdoor". |
| `data.location.label` | string | always (when data.location present) | Free-text user label, max 31 chars + NUL (SENSOR_META_LABEL_MAX = 32, sensor_meta.h:11). May be the empty string "" if the user set a location but no label. |


#### `auto_close — variant B (valve-reconnect catch-up)`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/rules_engine/rules_engine.c:934` |

**Trigger.** The valve came back on BLE while leaks were already known to be active, so the hub closes it now. Produced by rules_engine_on_valve_connected() (main/rules_engine/rules_engine.c:865), called once from app_iothub.c:1653 when a BLE_UPD_CONNECTED update arrives (GATT setup complete). Fires at 'Priority 1' (:919) only when: the 24 h override window is INACTIVE (an active window returns at :913 with no event), AND g_active_leak_count > 0, AND rules.auto_close_enabled. WHY A SECOND SITE: rules_engine_evaluate_leak() refuses to queue valve BLE writes while the valve is disconnected (:526-531) — it only kicks off a reconnect scan. The reconnect handler is where the deferred close actually executes, and it emits its own event with its own shape. In practice you will see variant A (from the leak that arrived while the valve was down, if the leak arrived while the hub was cloud-connected) and then variant B minutes later when the valve returns. RATE LIMITING: none. This path does NOT consult g_last_auto_close_tick / AUTO_CLOSE_COOLDOWN_MS, so a valve that flaps its BLE link will emit one variant-B auto_close per reconnect as long as any sensor is still wet. Side effects: ble_valve_set_rmleak(true) then ble_valve_close() unconditionally (:948-949).

```json
{
  "schema": "eflostop.v2",
  "ts": 1769812702,
  "gateway": {
    "id": "GW-A0B7651828C0",
    "short_id": "28C0",
    "name": "Kitchen Hub",
    "fw": "1.8.0",
    "uptime_s": 48570
  },
  "type": "event",
  "data": {
    "event": "auto_close",
    "source_type": "reconnect",
    "sensor_id": "00:80:E1:27:9A:E6",
    "rmleak_asserted": true,
    "active_leak_count": 2
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `data.event` | string | always | Frozen literal "auto_close" (rules_engine.c:934) — the SAME string as variant A. |
| `data.source_type` | string | always | HARDCODED to the literal "reconnect" (rules_engine.c:935). This is NOT a sensor type and is NOT produced by source_to_str(). It is the only reliable discriminator between the two auto_close shapes. Your source_type enum must therefore be OPEN: {"ble_leak_sensor", "lora", "valve_flood", "unknown", "reconnect"}. |
| `data.sensor_id` | string | always | g_active_leak_ids[0] — the FIRST entry in the hub's active-leak tracking table, i.e. whichever source went wet earliest and is still wet (rules_engine.c:936-937). Format is whatever that source's ID is: MAC string, "0xHEXID", or "valve". If several sensors are wet this names only one of them; use active_leak_count to know there are more, and the coupled snapshot's per-sensor leak_state array to know which. The ternary's "unknown" fallback is dead code — the enclosing if already requires g_active_leak_count > 0. |
| `data.rmleak_asserted` | boolean | always | HARDCODED true (rules_engine.c:938). Same intent-not-confirmation caveat as variant A, though on this path the valve IS connected by definition, so the write does go out. |
| `data.active_leak_count` | number | always | How many distinct leak sources are currently tracked as wet, 1..16 (MAX_ACTIVE_LEAK_SOURCES = 16, rules_engine.c:75). PRESENT ONLY on variant B — variant A never carries this key. Its presence is the second reliable discriminator between the two shapes. |


#### `auto_close_blocked_override`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/rules_engine/rules_engine.c:469` |

**Trigger.** A leak was detected but the 24 h water-access override window is active, so the hub deliberately did NOT close the valve. Produced by rules_engine_evaluate_leak() (main/rules_engine/rules_engine.c:452-482). Gate order is identical to auto_close variant A through gate 6 (provisioned, auto_close_enabled, source in trigger_mask) — so a sensor excluded from trigger_mask produces nothing at all here either. The leak incident latch is set FIRST and unconditionally (:442-447, persisted to NVS) so protection resumes the instant the window ends; then the override check at :452 diverts to this event and returns. RATE LIMIT — the strictest in this family: OVERRIDE_BLOCKED_COOLDOWN_MS = 60 000 ms (rules_engine.c:31), enforced by a SINGLE GLOBAL tick stamp g_last_blocked_event_tick (:454). At most ONE of these per minute for the ENTIRE HUB, regardless of how many different sensors are wet — a second sensor going wet 5 s after the first is invisible. There is a first-run bypass (|| g_last_blocked_event_tick == 0) so the very first blocked leak after boot always emits. Because of the 60 s bucket you cannot count leak episodes from this event; use the leak_detected events (which are individually delta-gated per sensor) for that. This is the only rules event classified low-priority downstream: it gets SNAP_TIER_LOW (2 s snapshot coalescing) instead of HIGH (app_iothub.c:1706-1707).

```json
{
  "schema": "eflostop.v2",
  "ts": 1769815100,
  "gateway": {
    "id": "GW-A0B7651828C0",
    "short_id": "28C0",
    "name": "Kitchen Hub",
    "fw": "1.8.0",
    "uptime_s": 50968
  },
  "type": "event",
  "data": {
    "event": "auto_close_blocked_override",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:9A:E6",
    "override_remaining_s": 71234
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `data.event` | string | always | Frozen literal "auto_close_blocked_override" (rules_engine.c:469). Semantics: 'a real leak is happening, we are reporting it, and we are intentionally leaving the water on because the user asked for it.' This should surface in the app as an active warning, not as a resolved condition. |
| `data.source_type` | string | always | Same source_to_str() vocabulary as auto_close variant A: "ble_leak_sensor" \| "lora" \| "valve_flood" \| "unknown" (rules_engine.c:470). |
| `data.sensor_id` | string | always | MAC string, "0xHEXID" or "valve"; literal "unknown" if the caller passed NULL (rules_engine.c:471). Because of the 60 s global rate limit this names only the sensor that happened to win the bucket. |
| `data.override_remaining_s` | number | conditional — OMITTED | Seconds left in the override window at the moment of the block. Added ONLY when the computed value is >= 0 (rules_engine.c:472-474); the key is ABSENT (never null) otherwise. Absent means the hub could not compute a sane remaining time: either the wall clock was below the 2024-01-01 validity threshold, or g_override_window_expiry <= now (window already expired but rules_engine_tick has not processed the expiry yet, up to ~30 s). Treat an absent value as 'unknown, window expiring imminently', not as zero. Note the field name differs from remaining_s on water_access_override_enabled. |


#### `water_access_override_enabled`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/rules_engine/rules_engine.c:215` |

**Trigger.** The 24 h water-access override window was started or refreshed: automatic valve closures are now BLOCKED, while leaks continue to be reported. Single builder start_override_window() (main/rules_engine/rules_engine.c:193-223) reached from exactly THREE call sites, distinguished by data.trigger: (1) rules_engine.c:1128 — rules_engine_tick() 'Check 2' physical-override detection, trigger="button". Conditions: override state INACTIVE, ble_valve_is_ready() true (full GATT, not just a GAP link), leak incident latched, the 5 s RMLEAK_GRACE_PERIOD_MS since the hub's own last RMLEAK write has elapsed (:1104-1108), the valve was already 'ready' on the previous tick (:1111-1117), and ble_valve_get_rmleak_state() now reads false — i.e. the user pressed the physical valve button and the valve cleared its own interlock. Detection latency = the tick period, worst case ~30 s. (2) rules_engine.c:978 — rules_engine_on_valve_connected() Priority 2 case (a), trigger="button". Conditions: hub incident latched AND valve reports RMLEAK clear AND valve_state == open. This infers an override that happened while the hub was rebooting or the valve was disconnected; the window is started RETROACTIVELY FROM NOW, so expires_ts is later than the true 24 h deadline by however long the hub was blind. (3) rules_engine.c:846 — rules_engine_enable_override_remote(), trigger="c2d_command", from C2D command "override_enable" (app_iothub.c:823-825). Four preconditions must all pass first, each mapping to a distinct cmd_ack error when it fails: valve provisioned (:789), valve reachable within a bounded 10 s BLE reconnect (OVERRIDE_CONNECT_TIMEOUT_MS, :797-809), something to override — active incident OR valve RMLEAK asserted OR window already active (:818), and the valve's own flood probe DRY (:826). On failure NOTHING is emitted, only a cmd_ack error. The window starts at EXECUTION, never at receipt. RATE LIMITING: none on the event itself; the underlying transitions are inherently rare. IDEMPOTENT REFRESH: calling override_enable while a window is already active resets the window to a full duration and RE-EMITS this event with a new expires_ts — the log-vs-state distinction matters, prev_state is only used for log wording (:204-210), not for the payload.

```json
{
  "schema": "eflostop.v2",
  "ts": 1769812500,
  "gateway": {
    "id": "GW-A0B7651828C0",
    "short_id": "28C0",
    "name": "Kitchen Hub",
    "fw": "1.8.0",
    "uptime_s": 48368
  },
  "type": "event",
  "data": {
    "event": "water_access_override_enabled",
    "trigger": "button",
    "expires_ts": 1769898900,
    "remaining_s": 86400
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `data.event` | string | always | Frozen literal "water_access_override_enabled" (rules_engine.c:215). |
| `data.trigger` | string | always | How the override was initiated. Exactly two values reach the wire: "button" — the user physically pressed the valve's button (either detected live by the tick, or inferred at valve reconnect); "c2d_command" — the app sent the override_enable C2D command. Code is `trigger ? trigger : "button"` (rules_engine.c:216), so "button" is also the NULL fallback, but no call site passes NULL. IMPORTANT: this key is named `trigger`, while the semantically-equivalent field on auto_close_reenabled is named `reason`. Two different key names for the same concept; map both. |
| `data.expires_ts` | number | always | ABSOLUTE Unix epoch seconds when the window ends = now + OVERRIDE_WINDOW_DURATION_S (rules_engine.c:200, 217). THIS is the field to persist and to drive countdowns from — it is the same field name and same absolute semantics as data.expires_ts in the snapshot message, so the two agree. CAVEAT: time(&now) here is NOT epoch-validity-checked, so on a hub with an unsynced clock this would be a nonsense small number; in practice build_envelope suppresses the whole publish in that case, so you never see the bad value — but the hub HAS persisted it to NVS and will act on it. |
| `data.remaining_s` | number | always | The window's FULL configured duration as a constant, NOT a live countdown: cJSON_AddNumberToObject(root, "remaining_s", (double)OVERRIDE_WINDOW_DURATION_S) (rules_engine.c:218). Default 86400 (24 h, rules_engine.c:29). It is 86400 even when this event is a REFRESH of an already-running window, because the window is genuinely reset to full duration. Do not hardcode 86400 on your side — bench/debug firmware can be compiled with -D OVERRIDE_WINDOW_DURATION_S=<seconds> (rules_engine.c:26-30), so short values are legitimate. Prefer expires_ts. |


#### `water_access_override_expired`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/rules_engine/rules_engine.c:1025` |

**Trigger.** The 24 h override window reached its expiry time on its own; automatic closure protection is restored. Produced by rules_engine_tick() (main/rules_engine/rules_engine.c:1012-1032). Conditions: override state == ACTIVE, the wall clock is valid (now >= 1704067200), and now >= g_override_window_expiry (:1016). NVS override keys are erased before the event is built (:1020). LATENCY: bounded by the tick period only — up to ~30 s past the true expiry (2 s while a commission/boot snapshot is pending, and immediately if any sensor/valve/snapshot queue event wakes the loop sooner). If the wall clock is invalid the expiry check is SKIPPED entirely (:1016), so a hub that loses SNTP keeps the window open indefinitely. If the hub was powered off across the expiry, the window is instead cleared silently at boot by override_load_from_nvs() (:167-173) and NO event is ever emitted — you will only see override_active flip to false in the next snapshot. FOLLOW-ON: if leaks are still active and auto_close_enabled, the engine immediately issues ble_valve_set_rmleak(true) + ble_valve_close() (:1042-1056) and RETURNS EARLY — deliberately leaving this event in the one-slot pending buffer. So the immediate re-close produces NO auto_close event; it is visible only as a valve_state_changed event plus the coupled snapshot. RATE LIMITING: none needed — a window can expire at most once.

```json
{
  "schema": "eflostop.v2",
  "ts": 1769898912,
  "gateway": {
    "id": "GW-A0B7651828C0",
    "short_id": "28C0",
    "name": "Kitchen Hub",
    "fw": "1.8.0",
    "uptime_s": 134780
  },
  "type": "event",
  "data": {
    "event": "water_access_override_expired",
    "auto_close_resumed": true,
    "active_leak_count": 1
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `data.event` | string | always | Frozen literal "water_access_override_expired" (rules_engine.c:1025). |
| `data.auto_close_resumed` | boolean | always | MISLEADING NAME — read carefully. It is literally (g_active_leak_count > 0) (rules_engine.c:1026-1027), i.e. 'leaks were STILL wet at expiry, so the hub is closing the valve right now'. Auto-close capability is restored either way; false does NOT mean protection stayed off, it means there was nothing to close for. Note also that true only means the hub INTENDED to close: the actual close additionally requires provisioning_get_rules_config() to succeed and rules.auto_close_enabled to be true (:1038), which is not re-checked when building this field. |
| `data.active_leak_count` | number | always | Number of leak sources still tracked as wet at the instant of expiry, 0..16 (rules_engine.c:1028). Redundant with auto_close_resumed (which is just count > 0) but gives the magnitude. Same field name and semantics as data.active_leak_count on auto_close variant B. |


#### `auto_close_reenabled`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/rules_engine/rules_engine.c:720` |

**Trigger.** The user cancelled an active 24 h override window from the app, restoring automatic-closure protection immediately. Produced by rules_engine_cancel_override() (main/rules_engine/rules_engine.c:700-782), reached ONLY from the C2D command "override_cancel" (app_iothub.c:813-818; command literal at c2d_commands.h:43). CRITICAL EMISSION GATE: the function returns true EARLY at :709-713 when no window is active — so override_cancel against an inactive window yields a SUCCESSFUL cmd_ack and ZERO telemetry. Never infer 'window cancelled' from the cmd_ack alone; wait for this event or for override_active:false in the snapshot. NOT the only cancel path: the C2D "leak_reset" command ALSO cancels the window (via the same cancel_override_window() helper, rules_engine.c:665) but emits rmleak_cleared with override_cancelled:true INSTEAD of this event. Two different event names for 'the window ended by user action' — handle both. FOLLOW-ON: if leaks are still wet at cancel time and auto_close_enabled, the engine immediately issues ble_valve_set_rmleak(true) + ble_valve_close() (:749-752) and RETURNS, deliberately leaving this event in the pending slot — so there is NO accompanying auto_close event, only valve_state_changed plus the coupled snapshot. If no leaks are active it instead wipes residual incident state (:771-778) so the next leak starts a clean cycle. RATE LIMITING: none; bounded by user action and by the fact that the second cancel finds no window and emits nothing.

```json
{
  "schema": "eflostop.v2",
  "ts": 1769830100,
  "gateway": {
    "id": "GW-A0B7651828C0",
    "short_id": "28C0",
    "name": "Kitchen Hub",
    "fw": "1.8.0",
    "uptime_s": 65968
  },
  "type": "event",
  "data": {
    "event": "auto_close_reenabled",
    "previous_remaining_s": 68800,
    "reason": "c2d_command"
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `data.event` | string | always | Frozen literal "auto_close_reenabled" (rules_engine.c:720). Semantics: the override window was cancelled early by the user and normal auto-close is live again. |
| `data.previous_remaining_s` | number | always | Seconds that WOULD have remained in the window had it not been cancelled, captured by cancel_override_window() (rules_engine.c:227-244). It is initialised to 0 and only filled in when the wall clock is valid AND g_override_window_expiry > now (:233-235) — so a 0 here is AMBIGUOUS: it can mean 'expired at that instant' or 'clock unsynced, could not compute'. It is never negative and never null/absent. Useful for analytics ('users cancel their override after a median of N hours'); do not use it to reconstruct the window's original length. |
| `data.reason` | string | always | HARDCODED to the literal "c2d_command" (rules_engine.c:722) — there is exactly one producer of this event, so no other value is possible today. Note the key is `reason` here, whereas the equivalent field on water_access_override_enabled is `trigger`, and `reason` is ALSO the key used on snapshot messages for the snapshot trigger ("heartbeat"/"event"/"boot"/"commission"/"fast"/"decommission") — three unrelated uses of similar names across the schema. Key off the envelope type and data.event before interpreting `reason`. |


#### `rmleak_cleared`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/rules_engine/rules_engine.c:678` |

**Trigger.** The user explicitly reset the leak incident from the app, releasing the valve's remote-leak interlock (RMLEAK). Produced by rules_engine_reset_leak_incident() (main/rules_engine/rules_engine.c:631-698), reached ONLY from the C2D command "leak_reset" (app_iothub.c:701-711; command literal at c2d_commands.h:38). TWO EMISSION GATES, both silent when they fail: (1) HARD REFUSAL — if g_active_leak_count > 0 the function returns false at :645-650 having changed NOTHING; the dispatcher then returns a cmd_ack ERROR with detail "A leak is still active. Fix the leak first, or use override to open the valve during a leak." and no telemetry is emitted. (Design intent: clearing the interlock while water is still present would let a follow-on valve_open restore water with no protection and no override window; the sanctioned during-leak path is override_enable.) (2) NOTHING-TO-DO — if the reset succeeds but was_active, valve_rmleak and had_override are ALL false (:671), state is cleared, cmd_ack is ok, and NO event is emitted. So a successful leak_reset does not guarantee this event. SIDE EFFECTS when it does fire: hub incident latch cleared and persisted, g_auto_close_triggered / g_all_clear_since / g_active_leak_count / g_rmleak_assert_tick zeroed (:653-659), any active override window cancelled via cancel_override_window() (:665), and ble_valve_set_rmleak(false) written to the valve (:694-696). It deliberately does NOT open the valve — opening requires a separate valve_set_state command. RATE LIMITING: none; bounded by user action, and a repeat reset with nothing latched emits nothing (gate 2).

```json
{
  "schema": "eflostop.v2",
  "ts": 1769833400,
  "gateway": {
    "id": "GW-A0B7651828C0",
    "short_id": "28C0",
    "name": "Kitchen Hub",
    "fw": "1.8.0",
    "uptime_s": 69268
  },
  "type": "event",
  "data": {
    "event": "rmleak_cleared",
    "override_cancelled": true
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `data.event` | string | always | Frozen literal "rmleak_cleared" (rules_engine.c:678). Semantics: user-initiated incident reset. Distinct from rmleak_auto_cleared, which is the hub's own 30 s all-clear timer. |
| `data.override_cancelled` | boolean | conditional — OMITTED | Added ONLY when a 24 h override window was active and this reset cancelled it (rules_engine.c:679-681), and when present its value is ALWAYS the literal true — it is never emitted as false and never as null. ABSENCE means 'no override window was running', so your parser must default it to false, not to null/unknown. This is the ONLY optional field on this event: a bare {"event":"rmleak_cleared"} is the normal single-key payload. |


#### `rmleak_auto_cleared`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/rules_engine/rules_engine.c:1082` |

**Trigger.** Fully automatic, no user or cloud involvement: every leak source has been dry for 30 continuous seconds, so the hub releases the valve's RMLEAK interlock by itself. Produced by rules_engine_tick() 'Check 1' (main/rules_engine/rules_engine.c:1071-1092). Sequence: track_leak_source() removes each source from the active table as it reports dry; when the table hits zero AND an incident is latched it stamps g_all_clear_since (:305-323); the tick then waits for AUTO_CLEAR_TIMEOUT_MS = 30 000 ms (rules_engine.c:18) to elapse. Note the tick also requires g_leak_incident_active to still be true (:1063). Any source reporting wet again resets g_all_clear_since to 0 (:293), restarting the 30 s from scratch — so a flapping sensor can hold the interlock indefinitely. EFFECTIVE LATENCY: 30 s plus up to one tick period, i.e. roughly 30-60 s after the last sensor goes dry. SIDE EFFECTS: incident latch cleared and persisted to NVS, g_auto_close_triggered and g_all_clear_since zeroed (:1075-1078), then ble_valve_set_rmleak(false) is written to the valve (:1090). It EXPLICITLY DOES NOT OPEN THE VALVE — the valve stays closed and the user must issue valve_set_state open. This event therefore means 'you are now allowed to reopen the water', not 'the water is back on'. RATE LIMITING: none needed; the incident latch is cleared as part of the same operation, so it cannot re-fire until a new leak latches a new incident.

```json
{
  "schema": "eflostop.v2",
  "ts": 1769813600,
  "gateway": {
    "id": "GW-A0B7651828C0",
    "short_id": "28C0",
    "name": "Kitchen Hub",
    "fw": "1.8.0",
    "uptime_s": 49468
  },
  "type": "event",
  "data": {
    "event": "rmleak_auto_cleared",
    "clear_after_seconds": 30
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `data.event` | string | always | Frozen literal "rmleak_auto_cleared" (rules_engine.c:1082). Semantics: hub-initiated interlock release after a sustained all-clear. Do NOT surface this as 'water restored' — the valve remains closed. |
| `data.clear_after_seconds` | number | always | COMPILE-TIME CONSTANT, not a measurement: AUTO_CLEAR_TIMEOUT_MS / 1000, currently always 30 (rules_engine.c:18, 1083). It documents the dry-hold threshold that was satisfied, not the actual elapsed time (which is >= 30 s and can be up to ~60 s because of the tick granularity). Treat it as firmware configuration echoed back, not telemetry. |


#### `rules_engine (defensive fallback — should never appear)`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/telemetry/telemetry_v2.c:668` |

**Trigger.** Emitted by telemetry_v2_publish_rules_event() when cJSON_Parse() fails on the string handed over by the rules engine (main/telemetry/telemetry_v2.c:662, 666-671). In the current firmware this is UNREACHABLE: every producer builds a cJSON object and serialises it with cJSON_PrintUnformatted (rules_engine.c:220, 355, 476, 683, 941, 1030, 1085), and if that allocation fails g_pending_telemetry stays NULL, which rules_engine_take_pending_telemetry() reports as 'nothing pending' and app_iothub.c:1660/1700 never calls the publisher for. So a print→parse round-trip cannot fail. It is documented here because it is a real branch in shipping code and would change the payload SHAPE — not just add a field — if a future producer ever hands over a hand-built or truncated string. Backends should log-and-alert on this rather than crash. Downstream treatment is identical to any other rules event, except that the SNAP_TIER selection at app_iothub.c:1706 does a raw substring match for "auto_close_blocked_override" on the string, so a fallback whose raw text happened to contain that substring would get the LOW tier.

```json
{
  "schema": "eflostop.v2",
  "ts": 1769813900,
  "gateway": {
    "id": "GW-A0B7651828C0",
    "short_id": "28C0",
    "name": "Kitchen Hub",
    "fw": "1.8.0",
    "uptime_s": 49768
  },
  "type": "event",
  "data": {
    "event": "rules_engine",
    "raw": "{\"event\":\"auto_close\",\"source_type\":\"lo"
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `data.event` | string | always | Frozen literal "rules_engine" (telemetry_v2.c:668). Its ONLY meaning is 'the hub could not parse its own rules payload'. It is not a real rules decision and carries no state — do not let it drive UI or auto-close reasoning. |
| `data.raw` | string | always | The unparseable rules-engine string, embedded verbatim as a JSON STRING (telemetry_v2.c:669), so all inner quotes arrive escaped. Length is unbounded by this code path, but the offline buffer would truncate the whole envelope at 512 bytes if it were captured while MQTT was down (offline_buffer.c:77-81). Diagnostic only. |


**Integration notes for this family**

- ONE rules event per event-loop iteration, maximum — earlier ones are silently destroyed. g_pending_telemetry is a single char* and every builder frees the previous value before storing its own (rules_engine.c:219, 352, 475, 682, 723, 940, 1029, 1084); the consumer drains it exactly once per iteration (app_iothub.c:1660). Within an iteration the producer order is rules_engine_tick() (:1611) → rules_engine_evaluate_leak() (:1638-1647) → rules_engine_on_valve_connected() (:1653), so LATER WINS. A water_access_override_expired built by the tick can be overwritten by an auto_close from a LoRa packet in the same iteration and never reach the cloud. Worse, the C2D handler runs on a DIFFERENT thread (the esp-mqtt event task) and can clobber, or be clobbered by, any of them at any instant. Never treat this event stream as a complete audit log; reconcile against the type:"snapshot" message, which is the state authority.
- `auto_close` is TWO different message shapes under one event name. Discriminate on data.source_type == "reconnect" (rules_engine.c:935) or on the presence of data.active_leak_count (:939) — variant A (rules_engine.c:333) has neither and may carry data.location; variant B has both and never carries location. If you build a strict schema per event name, variant B will fail validation. Your source_type enum must be OPEN: {"ble_leak_sensor", "lora", "valve_flood", "unknown", "reconnect"}.
- Several important state changes emit NO event at all, by design. (a) When an override window is CANCELLED or EXPIRES with leaks still wet, the hub immediately closes the valve but RETURNS EARLY (rules_engine.c:755, :1057) without building an auto_close — you see auto_close_reenabled / water_access_override_expired, then a valve_state_changed, and nothing linking them. (b) C2D override_cancel against an inactive window returns success with zero telemetry (:709-713). (c) C2D leak_reset with nothing latched returns success with zero telemetry (:671, :686-688). (d) C2D leak_reset while a sensor is still wet is REFUSED — cmd_ack error, no telemetry, no state change (:645-650). (e) C2D override_enable that fails any of its four preconditions emits only a cmd_ack error (:789, :805, :820, :828). Never infer state from a successful cmd_ack; wait for the event or the coupled snapshot.
- Nothing in this family ever emits JSON null. Every optional field is either present with a real value or the key is ABSENT. The three optional fields are data.location (auto_close variant A, rules_engine.c:339-349), data.override_remaining_s (auto_close_blocked_override, :472-474) and data.override_cancelled (rmleak_cleared, :679-681). override_cancelled is only ever emitted as `true` — absence must be defaulted to false. Contrast this with the snapshot message, which DOES use cJSON_AddNullToObject for battery/rssi/snr/fw_version/last_seen_age_s — so your JSON layer must handle both conventions depending on message type.
- Two different key names for 'why the override changed', and a third unrelated use of the same word. water_access_override_enabled uses data.trigger ("button" | "c2d_command", rules_engine.c:216); auto_close_reenabled uses data.reason (always "c2d_command", :722); and snapshot messages use data.reason for the SNAPSHOT trigger ("heartbeat"/"event"/"boot"/"commission"/"fast"/"decommission", telemetry_v2.c:383). Always key off envelope type + data.event before interpreting either name.
- remaining_s on water_access_override_enabled is a CONSTANT, not a countdown. It is always OVERRIDE_WINDOW_DURATION_S — 86400 by default (rules_engine.c:218), including when the event represents a REFRESH of an already-running window. Use data.expires_ts (absolute epoch seconds, :217) for all countdown maths; it is the same field name and semantics as data.expires_ts in the snapshot, so the two are directly comparable. And do not hardcode 86400: bench firmware can be built with -D OVERRIDE_WINDOW_DURATION_S=<seconds> (rules_engine.c:26-30).
- auto_close_blocked_override is rate-limited to ONE PER MINUTE FOR THE WHOLE HUB, not per sensor. OVERRIDE_BLOCKED_COOLDOWN_MS = 60000 with a single global g_last_blocked_event_tick (rules_engine.c:31, 454). If three sensors go wet during an override window you may see exactly one of these, naming only one sensor. You cannot count leak episodes or enumerate wet sensors from this event — use the per-sensor delta-gated leak_detected events, and the snapshot's lora_sensors[]/ble_leak_sensors[] leak_state array.
- rmleak_asserted:true is HARDCODED on both auto_close variants (rules_engine.c:336, :938) and records the hub's INTENT, not the valve's acknowledgement. On variant A the valve may be disconnected, in which case no BLE write happens at all — the code only kicks off a reconnect scan (:529-531) and the actual close arrives later as variant B. For confirmed valve state read valve.state / valve.rmleak / valve.connected from the coupled snapshot.
- Timer-driven events (rmleak_auto_cleared, water_access_override_expired, and the trigger:"button" flavour of water_access_override_enabled) fire from rules_engine_tick(), which runs once per iothub_task loop iteration with a blocking wait of min(next-snapshot-deadline, 30 s) — 2 s while a boot/commission snapshot is pending. So add UP TO ~30 s of jitter to every threshold: rmleak_auto_cleared lands ~30-60 s after the last sensor goes dry despite advertising clear_after_seconds:30, and water_access_override_expired can arrive up to ~30 s after expires_ts. Physical-button detection additionally waits a 5 s RMLEAK_GRACE_PERIOD_MS after any hub-issued RMLEAK write, and requires the valve to have been fully GATT-ready on the previous tick too (rules_engine.c:1101-1117).
- Pre-SNTP rules decisions are LOST, not buffered. build_envelope() destroys the message when ts < 1704067200 (telemetry_v2.c:66-74) and returns before publish_json, so nothing is written to the offline ring either. Meanwhile the state change has already happened and been persisted to NVS (override window, incident latch). Symptom for you: override_active flips to true in a snapshot with no preceding water_access_override_enabled event. Separately, if the hub is UNPROVISIONED the rules event is taken from the pending slot and then free()d without publishing or buffering (app_iothub.c:1660, 1673).
- Replayed offline events carry STALE timestamps. Rules events produced while MQTT is down go to a 16-entry / 512-byte-per-entry NVS ring (offline_buffer.h:12-13) and are replayed FIFO on reconnect, BEFORE the lifecycle "online" message (app_iothub.c:1691-1692). Each keeps its ORIGINAL ts and gateway.uptime_s. Order your event log by data ts, not by arrival or IoT Hub enqueue time. When the ring overflows the OLDEST entry is silently overwritten with only a local log line (offline_buffer.c:103-105) — you get no gap indication. Entries over 512 bytes are TRUNCATED before storage (:77-81), which would yield invalid JSON on replay; current rules payloads are ~250-400 bytes with the envelope, so this is headroom, not a live bug.
- The 24 h override window can end in three ways with three DIFFERENT telemetry outcomes, and you must handle all three to keep override state correct: (1) natural expiry → water_access_override_expired (rules_engine.c:1025); (2) C2D override_cancel → auto_close_reenabled (:720); (3) C2D leak_reset → rmleak_cleared WITH override_cancelled:true (:678-681), NOT auto_close_reenabled. A fourth path emits nothing at all: if the hub is powered off across the expiry, override_load_from_nvs() clears the window silently at boot (:167-173) and only the next snapshot's override_active:false reveals it.
- Sensor IDs are format-heterogeneous within the same data.sensor_id key. BLE leak sensors: uppercase colon-separated MAC "XX:XX:XX:XX:XX:XX" (app_ble_leak.c:88). LoRa sensors: "0x%08lX" uppercase hex, e.g. "0x0A1B2C3D" (app_iothub.c:1636). The valve's own flood probe: the bare literal "valve" (app_iothub.c:1648). Do not assume MAC-shaped strings. Also note auto_close variant B reports only g_active_leak_ids[0] — the earliest-still-wet source — even when active_leak_count > 1 (rules_engine.c:936-937).
- data.location appears on auto_close variant A but is OMITTED ENTIRELY when the sensor has no sensor_meta entry (rules_engine.c:339-349), whereas the leak_detected / leak_cleared events use add_location_obj() (telemetry_v2.c:143-153) which ALWAYS emits location with code:"unknown", label:"". Same conceptual object, two different absence conventions. location.code is one of 13 fixed slugs (sensor_meta.c:23-27: unknown, bathroom, kitchen, laundry, garage, garden, basement, utility, hallway, bedroom, living_room, attic, outdoor) and location.label is free text, max 31 chars, possibly "" (sensor_meta.h:11). Neither auto_close variant B, nor auto_close_blocked_override, nor any override/rmleak event carries location at all.
- auto_close_resumed on water_access_override_expired does NOT mean 'auto-close is enabled again' — it is literally (active_leak_count > 0) (rules_engine.c:1026-1027), i.e. 'leaks were still wet at expiry so we are closing now'. Protection resumes either way. And even true only records intent: the actual close additionally requires rules.auto_close_enabled, which is checked separately at :1038 and not reflected in the field.
- previous_remaining_s == 0 on auto_close_reenabled is ambiguous. cancel_override_window() initialises it to 0 and only computes a real value when the wall clock is valid AND expiry > now (rules_engine.c:227-235), so 0 means either 'the window was expiring at that instant' or 'the clock was unsynced'. It is never negative, never absent and never null. Fine for analytics, unusable for reconstructing the window's original length — use the expires_ts from the matching water_access_override_enabled for that.
- Every gate in rules_engine_evaluate_leak() short-circuits BEFORE any telemetry is built, so a mis-set config makes the whole family go quiet with no cloud-visible signal. If rules.auto_close_enabled is false (rules_engine.c:423) or the source's bit is missing from rules.trigger_mask (:432 — BLE_LEAK 0x01, LORA 0x02, VALVE_FLOOD 0x04, provisioning_manager.h:22-24), you get NEITHER auto_close NOR auto_close_blocked_override for that source. The current config is echoed in data.rules.auto_close_enabled / data.rules.trigger_mask on both lifecycle (telemetry_v2.c:358-364) and snapshot (:577-583) messages — read it there before concluding the hub is failing to protect.
- Gate 8 in the auto-close path silently suppresses re-triggers: if the valve is already closed AND already has RMLEAK asserted, rules_engine_evaluate_leak returns with no event (rules_engine.c:487-491). Combined with the 10 s global AUTO_CLOSE_COOLDOWN_MS (:17, :495), a single leak episode produces ONE auto_close event no matter how many sensors are wet or how often they re-advertise. Do not size dashboards or alerting on auto_close event counts as a proxy for leak severity.
- The trigger:"button" override started at valve reconnect (rules_engine.c:978, from rules_engine_on_valve_connected) is INFERRED, not observed, and its 24 h clock starts FROM NOW rather than from the moment the user actually pressed the button. Its expires_ts is therefore later than the true deadline by however long the hub was rebooting or the valve was disconnected. The inference fires on hub incident latched + valve RMLEAK clear + valve_state == open; the sibling case (valve CLOSED) instead silently re-asserts RMLEAK with no telemetry (:981-984).


---

## 6. Health events and command acknowledgements (cmd_ack)

### What this family is

Two unrelated producers that share the `eflostop.v2` **event** envelope:

1. **Health events** — the hub's device-supervision engine (`main/health_engine/health_engine.c`) watches the valve and every provisioned leak sensor and emits an event **only** when a device crosses the *Critical* boundary: `device_offline` (went Critical) or `device_recovered` (left Critical). Built by `health_alert_to_json()` (`health_engine.c:573-604`), wrapped by `telemetry_v2_publish_health_event()` (`telemetry_v2.c:676-693`).
2. **cmd_ack** — the per-command acknowledgement for cloud-to-device commands, built by `telemetry_v2_publish_cmd_ack()` (`telemetry_v2.c:695-718`), called from the C2D dispatcher `handle_c2d_command()` (`app_iothub.c:643-936`).

Both are published to `devices/<device_id>/messages/events/` at **QoS 1** with `"type":"event"`, via `publish_json()` (`telemetry_v2.c:99-126`).

### Transport / envelope facts that apply to both

- Envelope built by `build_envelope("event")` (`telemetry_v2.c:59-92`). Every message carries `schema`, `ts`, `gateway{...}`, `type`, `data`.
- **Suppressed before clock sync.** If `time(NULL) < 1704067200` (2024-01-01Z) `build_envelope` returns NULL and the message is **discarded, not buffered** (`telemetry_v2.c:70-74`). For health alerts this means the JSON string is built, freed and lost.
- **Offline behaviour.** Because `type_hint == "event"`, a message produced while MQTT is down goes to the NVS ring buffer (16 entries, 512-byte cap, oldest overwritten) and is replayed **verbatim** on reconnect *before* the `lifecycle`/`online` message (`telemetry_v2.c:115-118`, `offline_buffer.c:73-120`, `app_iothub.c:1691-1692`). Replayed messages keep their **original** `ts` and `gateway.uptime_s`, so the backend must order by `ts`, not by arrival.
- **This family emits no JSON nulls.** Every optional field is *omitted* (`cJSON_AddNumberToObject`/`AddStringToObject` guarded by an `if`). There is no `cJSON_AddNullToObject` anywhere in `health_alert_to_json` or `telemetry_v2_publish_cmd_ack`. (This is the opposite of the `snapshot` message, which deliberately emits explicit nulls.) A backend parser for *this* family only needs "key missing" handling.

### Health: the alert-generation rule (`health_engine.c:184-228`)

`maybe_enqueue_alert()` is the single gate. In order:

1. **Critical transitions only** (`:189-194`). `into_critical = new==CRITICAL && old!=CRITICAL`; `out_of_critical = new!=CRITICAL && old==CRITICAL`. Anything else `return`s with no event.
2. **Boot / reload suppression** (`:197-199`). `if (out_of_critical && !dev->ever_seen) return;` — every device starts at `rating = HEALTH_CRITICAL, ever_seen = false` when the device table is (re)loaded (`health_engine_reload_devices`, `:453-486`), so a device's **first ever check-in this uptime never produces `device_recovered`**. The suppression is per-device and per-reload, not time-windowed.
3. **60 s per-device debounce** (`:202-205`, `HEALTH_ALERT_DEBOUNCE_MS`, `health_engine.h:19`). `last_alert_ms` is stamped **only when the alert is actually enqueued** (`:222`). A transition suppressed by the debounce is **lost forever** — the caller still commits `dev->rating = new_rating` (`:249-250`, `:268-269`, `:289-290`, `:313-314`, `:331-332`, `:345-346`), so nothing re-fires it later.
4. **Bounded queue** (`:221`). The alert queue is 4 deep (`:523`). On a full queue `xQueueSend` fails, the alert is dropped **and the debounce timestamp is not stamped**.

Evaluation cadence: a 30 s tick timer (`HEALTH_TICK_INTERVAL_MS`, `health_engine.h:18`) runs `evaluate_timeouts()` (`:317-351`); check-in events are handled immediately on arrival.

Thresholds (`health_engine.h:16-25`): sensor offline after **10 min** with no check-in; valve **3 min** grace after BLE disconnect (rated `warning`, no event) before Critical; battery warn ≤20 %, good ≤35 %; RSSI warn ≤-90 dBm, good ≤-80 dBm.

### What does NOT generate a health event

- **Battery falling to warning level.** `EXCELLENT → WARNING` (battery ≤20 %) is not a Critical transition, so **no event at all**. Same for `→ GOOD` (≤35 %) and for any RSSI degradation.
- **Valve BLE disconnect itself.** The disconnect only sets `rating = warning` (3-min grace, `compute_valve_rating` `:144-148`). No event unless the grace expires.
- **A device that is offline from boot (or from the moment of a `provision`).** It is already `CRITICAL` after the table load, so there is no *transition into* Critical — **`device_offline` is never emitted for it**. `evaluate_timeouts` also explicitly skips sensors with `last_seen_ms == 0` (`:339`).
- **Un-provisioned devices.** `handle_lora_checkin`/`handle_ble_leak_checkin` return early when `find_device()` misses (`:240`, `:258`).

All of the above are visible **only in the `snapshot` message**: per-device `rating` / `connected` / `last_seen_age_s` in `data.valve`, `data.lora_sensors[]`, `data.ble_leak_sensors[]`, plus the roll-up `data.system_health.rating` and the human string `data.system_health.reason` (built by `build_system_health_reason`, `telemetry_v2.c:157-231`, e.g. `"2 sensors battery low"`, `"Valve battery low"`, `"Valve disconnected"`, `"Valve offline"`, `"N sensors signal weak"`). The snapshot is the authoritative reconciliation source; health events are notification hints only.

### The unreachable `health_engine` fallback

`telemetry_v2_publish_health_event` (`telemetry_v2.c:682-690`) re-parses the JSON string it was handed; if `cJSON_Parse` fails it emits `data:{"event":"health_engine","raw":"<original string>"}`. The **only** call site (`app_iothub.c:1716-1718`) passes the output of `health_alert_to_json()`, i.e. a string produced by `cJSON_PrintUnformatted` moments earlier — so it always re-parses. This shape is effectively unreachable (only a heap-exhaustion failure inside `cJSON_Parse` could produce it). Documented below for completeness; treat it as a defensive path, and note it carries **no** `category` field.

### cmd_ack: when it is (and is not) sent

`handle_c2d_command` sends an ack only when `cmd.is_envelope || cmd.id[0]` (`app_iothub.c:931-933`). Consequences:

- **Envelope commands always get exactly one ack** — `{"schema":"eflostop.cmd"|"eflostop.cmd.v1", "cmd":"...", "id":"...", "payload":{...}}` (`c2d_commands.c:68-128`).
- **Legacy plain-text commands get no ack at all** (`VALVE_OPEN`, `DECOMMISSION_LORA:0x…`, `RULES_CONFIG:{…}`, …). `parse_legacy` sets `is_envelope = false` and leaves `id` empty (`c2d_commands.c:144-295`).
- **A bare provisioning JSON blob** (no `schema`, no `cmd`) is normalised to `cmd = "provision"` through the legacy path → **no ack** (`c2d_commands.c:273-289`).
- **Unparseable payloads get no ack and no error** — `handle_c2d_command` returns at `:646-649`.
- `decommission` with `target:"all"` acks **early** (`app_iothub.c:791-793`, always `status:"ok"`), then the hub publishes a final `snapshot` with `data.reason:"decommission"` and reboots ~3 s later to re-register with DPS (`app_iothub.c:1561-1571`).


#### `health / device_offline`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/health_engine/health_engine.c:582` |

**Trigger.** A supervised device crosses INTO health rating CRITICAL. Sensors (dev_type lora|ble_leak_sensor): no check-in for 10 min (HEALTH_LORA_TIMEOUT_MS / HEALTH_BLE_LEAK_TIMEOUT_MS = 600000, health_engine.h:16-17), detected by the 30 s tick in evaluate_timeouts (health_engine.c:317-351) -> wall-clock latency 10:00-10:30 after the last check-in. Valve: the BLE link drops (handle_valve_event(false), health_engine.c:283 -> rating WARNING, NO event), then when (now - disconnect_ms) >= 180000 (HEALTH_VALVE_DISC_TIMEOUT_MS) the 30 s tick promotes it to CRITICAL -> latency 3:00-3:30 after the disconnect. GATES: (a) only Critical transitions fire (health_engine.c:189-194); (b) 60 s per-device debounce keyed on the last successfully-enqueued alert (health_engine.c:202-205) - a debounced transition is silently and permanently lost because dev->rating is committed anyway; (c) 4-deep alert queue (health_engine.c:523) - on overflow the alert is dropped and the debounce is not stamped; (d) a device that was never heard this uptime is already CRITICAL, so it never transitions in and NEVER produces this event (health_engine.c:339 + :189). DELIVERY: drained by iothub_task once per loop iteration (app_iothub.c:1715-1722), only while the hub is provisioned (app_iothub.c:1672-1686); each non-empty drain also schedules one coupled HIGH-tier snapshot (app_iothub.c:1727-1728). Dropped outright if the wall clock is not yet valid; buffered to the NVS ring and replayed on reconnect if MQTT is down.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785412345,
  "gateway": {
    "id": "GW-A0B7651028C0",
    "short_id": "28C0",
    "name": "Basement Hub",
    "fw": "1.8.0",
    "uptime_s": 41522
  },
  "type": "event",
  "data": {
    "category": "health",
    "event": "device_offline",
    "dev_type": "lora",
    "sensor_id": "0x0000A1B2",
    "rating": "critical",
    "prev_rating": "good",
    "battery": 78,
    "rssi": -83,
    "offline_duration_s": 612
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `schema` | string | always | Constant "eflostop.v2" (TELEMETRY_SCHEMA). telemetry_v2.c:64. |
| `ts` | number (integer, unix epoch seconds, UTC) | always | Hub wall clock at build time. Guaranteed >= 1704067200 because the message is suppressed otherwise. On an offline-buffered replay this is the ORIGINAL event time, not the delivery time. telemetry_v2.c:76. |
| `gateway.id` | string | always | Gateway ID, form "GW-XXXXXXXXXXXX" (WiFi STA MAC, no colons). Also the Azure DPS registration ID. telemetry_v2.c:79. |
| `gateway.short_id` | string | always | Last 4 hex of the Gateway ID (e.g. "28C0"); matches the setup AP SSID "WiFi-Hub-28C0". telemetry_v2.c:80. |
| `gateway.name` | string | conditional-omitted | User-assigned hub name. OMITTED (key absent) when no name has been set - hub_identity_get_name() returns an empty string and the key is skipped. telemetry_v2.c:81-83. |
| `gateway.fw` | string | always | Hub firmware version from the ESP-IDF app descriptor (PROJECT_VER), e.g. "1.8.0". telemetry_v2.c:84, telemetry_v2.c:50-54. |
| `gateway.uptime_s` | number (integer seconds) | always | Seconds since hub boot (esp_timer). On a buffered replay this is the value captured when the event occurred. telemetry_v2.c:85-86. |
| `type` | string | always | Constant "event" for this family. telemetry_v2.c:89. |
| `data.category` | string | always | Constant "health". This is the ONLY message family in the whole firmware that emits a `category` key - use it to route health events. health_engine.c:580. |
| `data.event` | string | always | Constant "device_offline" here. Chosen by `bool is_offline = (alert->new_rating == HEALTH_CRITICAL)` -> "device_offline" : "device_recovered". health_engine.c:582-584. |
| `data.dev_type` | string enum | always | Which kind of device. Exactly one of "valve" \| "lora" \| "ble_leak_sensor" (dev_type_to_str, health_engine.c:68-76). The "unknown" default is unreachable - health_dev_type_t has only those three members (health_engine.h:47-51). |
| `data.sensor_id` | string | always | Device identity. For dev_type "lora": "0x%08lX" uppercase 8-digit hex, e.g. "0x0000A1B2" (health_engine.c:466-467). For "ble_leak_sensor" and "valve": the MAC string exactly as it was stored at provisioning time (provisioning_manager.c:406, :462-464 - stored VERBATIM, no case normalisation). Note the key is `sensor_id` even when dev_type is "valve". |
| `data.rating` | string enum | always | NEW rating. For device_offline this is always "critical" (that is the definition of the event). Enum: "excellent" \| "good" \| "warning" \| "critical" (health_rating_to_str, health_engine.c:57-66); "unknown" is unreachable. health_engine.c:588. |
| `data.prev_rating` | string enum | always | Rating immediately before the transition. For device_offline it is one of "excellent" \| "good" \| "warning" - never "critical" (that would not be a transition). "warning" occurs for the valve (3-min disconnect grace) and for a low-battery/weak-signal sensor that then times out. health_engine.c:589. |
| `data.battery` | number (integer percent 0-100) | conditional-omitted | LAST KNOWN battery percent, captured at alert build time from dev->last_battery - NOT sampled now, so it can be 10+ minutes stale (much older for the valve). OMITTED (key absent) if and only if the stored value is 0xFF / 255, the "unknown" sentinel: no check-in has ever carried a battery this uptime, or the device table was just reloaded (health_engine.c:470/485 set 0xFF). Caveat: a sensor legitimately reporting 255 is indistinguishable from unknown. health_engine.c:591-593. |
| `data.rssi` | number (integer dBm, negative) | conditional-omitted | LAST KNOWN RSSI in dBm at the hub, from dev->last_rssi. OMITTED (key absent) if and only if the stored value is exactly 0, which is the "unknown" sentinel. Consequence: this field is ALWAYS OMITTED for dev_type "valve" - no code path ever writes last_rssi for a valve entry (handle_valve_event health_engine.c:273-295 and handle_valve_battery :302-315 do not set it, and reload zeroes it). Also omitted for a sensor whose only feed reported 0. health_engine.c:594-596. |
| `data.offline_duration_s` | number (integer seconds) | conditional-omitted | Seconds between the device's last recorded check-in and the moment the alert was raised: (now - dev->last_seen_ms)/1000. Set only when `into_critical && dev->last_seen_ms > 0` (health_engine.c:217-219) and then emitted only when > 0 (:597-599). OMITTED when the device was never heard this uptime, or when the computed value truncates to 0 sub-second. For dev_type "valve" this measures time since the valve's last BLE NOTIFY, NOT time since the link dropped - see gotchas. |


#### `health / device_recovered`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/health_engine/health_engine.c:584` |

**Trigger.** A supervised device crosses OUT of health rating CRITICAL. Sensors: a check-in arrives (handle_lora_checkin health_engine.c:234-253 on every decoded LoRa packet - app_lora.cpp:297; handle_ble_leak_checkin :255-271 on a BLE advertisement that passed the scanner's delta gate or its 5-min heartbeat - app_ble_leak.c:189-222). Valve: any data-bearing BLE NOTIFY or a (re)connect posts VALVE_CONNECTED (app_ble_valve.c:436-445), clearing disconnect_ms. GATES (same as device_offline plus one more): (a) only Critical transitions fire; (b) the FIRST check-in after every boot or every device-table reload is suppressed by `if (out_of_critical && !dev->ever_seen) return;` (health_engine.c:197-199) - so a fresh boot, a `provision`, or any `decommission` never produces a burst of device_recovered; (c) 60 s per-device debounce - a device that flaps offline->online inside 60 s of its device_offline gets NO recovery event, leaving the backend believing it is still offline until a snapshot corrects it; (d) 4-deep queue. Delivery, clock suppression and offline buffering are identical to device_offline.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785413001,
  "gateway": {
    "id": "GW-A0B7651028C0",
    "short_id": "28C0",
    "name": "Basement Hub",
    "fw": "1.8.0",
    "uptime_s": 42178
  },
  "type": "event",
  "data": {
    "category": "health",
    "event": "device_recovered",
    "dev_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:9A:E6",
    "rating": "warning",
    "prev_rating": "critical",
    "battery": 18,
    "rssi": -67
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `schema` | string | always | Constant "eflostop.v2". telemetry_v2.c:64. |
| `ts` | number (integer, unix epoch seconds, UTC) | always | Hub wall clock at build time; original event time on a buffered replay. telemetry_v2.c:76. |
| `gateway.id` | string | always | Gateway ID "GW-XXXXXXXXXXXX". telemetry_v2.c:79. |
| `gateway.short_id` | string | always | Last 4 hex of the Gateway ID. telemetry_v2.c:80. |
| `gateway.name` | string | conditional-omitted | User-assigned hub name; key OMITTED when unset. telemetry_v2.c:81-83. |
| `gateway.fw` | string | always | Hub firmware version. telemetry_v2.c:84. |
| `gateway.uptime_s` | number (integer seconds) | always | Seconds since hub boot. telemetry_v2.c:85-86. |
| `type` | string | always | Constant "event". telemetry_v2.c:89. |
| `data.category` | string | always | Constant "health". health_engine.c:580. |
| `data.event` | string | always | Constant "device_recovered" here - emitted whenever the new rating is anything other than HEALTH_CRITICAL. health_engine.c:582-584. |
| `data.dev_type` | string enum | always | "valve" \| "lora" \| "ble_leak_sensor". health_engine.c:586, :68-76. |
| `data.sensor_id` | string | always | LoRa: "0x%08lX" uppercase. BLE leak / valve: provisioned MAC string verbatim. health_engine.c:587. |
| `data.rating` | string enum | always | NEW rating - one of "excellent" \| "good" \| "warning". IMPORTANT: device_recovered does NOT mean healthy. A sensor that returns with battery <= 20 % or RSSI <= -90 dBm recovers straight to "warning" (compute_sensor_rating, health_engine.c:111-139; compute_valve_rating :141-164). health_engine.c:588. |
| `data.prev_rating` | string enum | always | Always "critical" for this event, by construction. health_engine.c:589. |
| `data.battery` | number (integer percent 0-100) | conditional-omitted | Battery percent from the check-in that triggered the recovery. OMITTED if and only if the stored value is 0xFF/255 (unknown). health_engine.c:591-593. |
| `data.rssi` | number (integer dBm) | conditional-omitted | RSSI of the triggering check-in. OMITTED if and only if the stored value is 0, and therefore ALWAYS omitted for dev_type "valve". health_engine.c:594-596. |
| `data.offline_duration_s` | number (integer seconds) | conditional-omitted | ALWAYS OMITTED on device_recovered. It is only populated when `into_critical` is true (health_engine.c:217-219); the alert struct is memset to zero (:209), so it stays 0 and the `> 0` guard at :597 skips the key. Do not expect a recovery event to tell you how long the device was down. |


#### `cmd_ack (status "ok")`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/telemetry/telemetry_v2.c:708` |

**Trigger.** Sent once per accepted cloud-to-device command, immediately after the dispatcher finishes handling it, from the esp-mqtt event task (handle_c2d_command, app_iothub.c:931-933). Sent ONLY when the message arrived as a recognised JSON envelope (schema "eflostop.cmd" or legacy "eflostop.cmd.v1") OR carried a non-empty correlation `id`; legacy plain-text commands and bare provisioning JSON get NO ack (see overview). No debounce, no rate limit, no coalescing - exactly one ack per dispatched envelope. Commands that can ack: valve_open, valve_close, valve_set_state, leak_reset, decommission, override_cancel, override_enable, rules_config, sensor_meta, provision, set_hub_name (c2d_commands.h:35-45), plus any unrecognised `cmd` value (which acks with status "error"). `decommission` with target "all" acks EARLY and unconditionally ok (app_iothub.c:791-793) before the hub publishes its final snapshot and reboots. Note also that a valve command acks "ok" as soon as the BLE request is QUEUED - ble_valve_open()/ble_valve_close() only xQueueSend to the BLE task (app_ble_valve.c:1813-1823) and the dispatcher discards their return value.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785414222,
  "gateway": {
    "id": "GW-A0B7651028C0",
    "short_id": "28C0",
    "name": "Basement Hub",
    "fw": "1.8.0",
    "uptime_s": 43399
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "a3f9c1e2-7b44-4d1a-9f0e-55c2b1d8e6a1",
    "cmd": "valve_close",
    "status": "ok"
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `schema` | string | always | Constant "eflostop.v2". telemetry_v2.c:64. |
| `ts` | number (integer, unix epoch seconds, UTC) | always | Hub wall clock when the ack was built. telemetry_v2.c:76. |
| `gateway.id` | string | always | Gateway ID "GW-XXXXXXXXXXXX". telemetry_v2.c:79. |
| `gateway.short_id` | string | always | Last 4 hex of the Gateway ID. telemetry_v2.c:80. |
| `gateway.name` | string | conditional-omitted | User-assigned hub name; key OMITTED when unset. telemetry_v2.c:81-83. |
| `gateway.fw` | string | always | Hub firmware version. telemetry_v2.c:84. |
| `gateway.uptime_s` | number (integer seconds) | always | Seconds since hub boot. telemetry_v2.c:85-86. |
| `type` | string | always | Constant "event". telemetry_v2.c:89. |
| `data.event` | string | always | Constant "cmd_ack". telemetry_v2.c:704. Note: unlike health events there is NO `category` key here. |
| `data.id` | string (<= 63 chars) | conditional-omitted | Correlation ID echoed verbatim from the command envelope's `id`. OMITTED (key absent) when the envelope carried no `id` or an empty one - `if (correlation_id && correlation_id[0])` (telemetry_v2.c:705-706). Truncated to 63 characters by the parser (char id[64], c2d_commands.h:24, strncpy at c2d_commands.c:109), so a longer ID will not round-trip. |
| `data.cmd` | string (<= 31 chars) | always | The command name being acked, echoed from the envelope's `cmd` field. Truncated to 31 characters (char cmd[32], c2d_commands.h:25, strncpy at c2d_commands.c:104) - a longer name is silently shortened here AND in error.code. telemetry_v2.c:707. |
| `data.status` | string enum | always | "ok" or "error" - exactly two values, `success ? "ok" : "error"`. telemetry_v2.c:708. |
| `data.error` | object | conditional-omitted | ALWAYS OMITTED when status is "ok" - the object is only added under `if (!success && error_msg)` (telemetry_v2.c:709-714). |


#### `cmd_ack (status "error")`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/telemetry/telemetry_v2.c:709` |

**Trigger.** Same trigger and same publisher as the ok form; the dispatcher set `success = false` and supplied an `error_msg` string. In the current code every `success = false` branch in handle_c2d_command also sets error_msg, so in practice status "error" always carries the `error` object - but the builder guard is `if (!success && error_msg)`, so the backend MUST treat `error` as optional and not dereference it blindly. Failure causes are validation rejects (missing/invalid payload fields, unknown command, unknown decommission target), safety interlocks (valve locked by RMLEAK, leak still active, valve flood probe wet, valve unreachable) and internal failures (NVS/mutex). One ack per command; no retry, no rate limit.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785414290,
  "gateway": {
    "id": "GW-A0B7651028C0",
    "short_id": "28C0",
    "name": "Basement Hub",
    "fw": "1.8.0",
    "uptime_s": 43467
  },
  "type": "event",
  "data": {
    "event": "cmd_ack",
    "id": "b7d20c55-1a83-45f6-8e11-9c4477a0de32",
    "cmd": "valve_open",
    "status": "error",
    "error": {
      "code": "valve_open",
      "detail": "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."
    }
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `schema` | string | always | Constant "eflostop.v2". telemetry_v2.c:64. |
| `ts` | number (integer, unix epoch seconds, UTC) | always | Hub wall clock when the ack was built. telemetry_v2.c:76. |
| `gateway.id` | string | always | Gateway ID "GW-XXXXXXXXXXXX". telemetry_v2.c:79. |
| `gateway.short_id` | string | always | Last 4 hex of the Gateway ID. telemetry_v2.c:80. |
| `gateway.name` | string | conditional-omitted | User-assigned hub name; key OMITTED when unset. telemetry_v2.c:81-83. |
| `gateway.fw` | string | always | Hub firmware version. telemetry_v2.c:84. |
| `gateway.uptime_s` | number (integer seconds) | always | Seconds since hub boot. telemetry_v2.c:85-86. |
| `type` | string | always | Constant "event". telemetry_v2.c:89. |
| `data.event` | string | always | Constant "cmd_ack". telemetry_v2.c:704. |
| `data.id` | string (<= 63 chars) | conditional-omitted | Correlation ID from the command envelope; OMITTED when absent or empty. telemetry_v2.c:705-706. |
| `data.cmd` | string (<= 31 chars) | always | Command name, truncated to 31 chars. For an unrecognised command this is whatever the cloud sent (truncated), acked with detail "unknown command". telemetry_v2.c:707. |
| `data.status` | string enum | always | "error" in this form. telemetry_v2.c:708. |
| `data.error` | object | conditional-omitted | Present when `!success && error_msg != NULL`. Every current failure path sets error_msg, so today it is present on every status "error"; still code defensively - the builder permits status "error" with no `error` object. telemetry_v2.c:709-714. |
| `data.error.code` | string | always (when data.error is present) | NOT a machine error enum - it is `cmd_name`, i.e. an exact duplicate of data.cmd (telemetry_v2.c:711). Do not build error handling on this field; branch on data.error.detail or on your own request context instead. |
| `data.error.detail` | string | always (when data.error is present) | Human-readable failure reason, a frozen C string literal from the dispatcher. Full enumeration (all in main/iothub/app_iothub.c): (1) "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak." (:539-540, used by valve_open and valve_set_state->open); (2) "missing 'state' field (expected \"open\" or \"closed\")" (:679); (3) "invalid state value (expected \"open\" or \"closed\")" (:696); (4) "A leak is still active. Fix the leak first, or use override to open the valve during a leak." (:708, leak_reset); (5) "missing decommission target" (:720); (6) "valve decommission failed" (:733); (7) "lora sensor decommission failed" (:753); (8) "ble sensor decommission failed" (:769); (9) "full decommission failed" (:802); (10) "unknown decommission target" (:807); (11) "override cancel failed" (:817); (12) "No active leak to override. Use the normal Open Valve control." (:830); (13) "Water detected at the valve. It can't be opened remotely until the valve area is dry." (:833); (14) "The valve isn't responding. Check its power and connection, then try again." (:836); (15) "No valve is set up for this hub." (:839); (16) "Something went wrong applying the override. Your water state is unchanged. Try again." (:842, OVERRIDE_ENABLE_ERR_INTERNAL); (17) "rules config update failed" (:853); (18) "sensor metadata update failed" (:862); (19) "provisioning failed" (:902); (20) "missing 'name' field" (:913, set_hub_name); (21) "name too long (max 31 chars)" (:916); (22) "unknown command" (:927). Items 2 and 3 contain literal double quotes, JSON-escaped as \" on the wire. |


#### `health fallback ("health_engine") - effectively unreachable`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/telemetry/telemetry_v2.c:687` |

**Trigger.** Would be emitted if telemetry_v2_publish_health_event() failed to re-parse the health JSON string handed to it (telemetry_v2.c:682-690). The only caller (app_iothub.c:1716-1718) passes the output of health_alert_to_json(), which is itself produced by cJSON_PrintUnformatted, so the re-parse always succeeds. Reachable only under heap exhaustion inside cJSON_Parse. Documented so a strict backend parser does not choke on it; do not build features on it.

```json
{
  "schema": "eflostop.v2",
  "ts": 1785414400,
  "gateway": {
    "id": "GW-A0B7651028C0",
    "short_id": "28C0",
    "fw": "1.8.0",
    "uptime_s": 43577
  },
  "type": "event",
  "data": {
    "event": "health_engine",
    "raw": "{\"category\":\"health\",\"event\":\"device_offline\",\"dev_type\":\"lora\",\"sensor_id\":\"0x0000A1B2\",\"rating\":\"critical\",\"prev_rating\":\"good\"}"
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `schema` | string | always | Constant "eflostop.v2". |
| `ts` | number (integer, unix epoch seconds, UTC) | always | Hub wall clock. |
| `gateway.id` | string | always | Gateway ID. |
| `gateway.short_id` | string | always | Last 4 hex of the Gateway ID. |
| `gateway.name` | string | conditional-omitted | Hub name; OMITTED when unset. |
| `gateway.fw` | string | always | Hub firmware version. |
| `gateway.uptime_s` | number (integer seconds) | always | Seconds since hub boot. |
| `type` | string | always | Constant "event". |
| `data.event` | string | always | Constant "health_engine". telemetry_v2.c:687. Note there is NO `category` key on this shape, so `category == "health"` routing would miss it. |
| `data.raw` | string | always | The unparsed health-alert JSON as an escaped string. telemetry_v2.c:688. |


**Integration notes for this family**

- `category` is health-only. `"category":"health"` (health_engine.c:580) is the ONLY occurrence of a `category` key in the entire firmware - no other event family (valve, leak, rules, cmd_ack) emits it. It is a reliable health-event discriminator, but do not expect it on other messages.
- Health events are hints, not state. Only Critical transitions are reported. A battery falling to warning (<= 20 %), a weak RSSI (<= -90 dBm), or a valve BLE disconnect inside its 3-minute grace produce NO event whatsoever. The `snapshot` message is the only place those are visible (per-device `rating`/`connected`/`last_seen_age_s` plus `system_health.rating` and the `system_health.reason` prose). Never derive device health purely from the event stream.
- A device that is offline from boot NEVER produces `device_offline`. Every device is initialised to `rating = CRITICAL, ever_seen = false` by health_engine_reload_devices (health_engine.c:453-486), so there is no transition INTO Critical; evaluate_timeouts also skips sensors with `last_seen_ms == 0` (:339). The same is true after every `provision` or `decommission` command, which reload the table. Such devices appear only in the snapshot as `connected:false, rating:"critical", last_seen_age_s:null`.
- The first recovery after every boot/reload is deliberately suppressed (health_engine.c:197-199), so you will not see a burst of `device_recovered` when a hub comes up or is re-provisioned. Corollary: after a reboot the backend must re-derive per-device state from the first snapshot, not from missing events.
- The 60-second per-device debounce silently DROPS transitions - it does not delay them. `last_alert_ms` is only stamped on a successful enqueue (health_engine.c:202-205, :222) while `dev->rating` is committed regardless, so a device that flaps offline->online within 60 s emits `device_offline` and then no `device_recovered` at all. The backend will believe it is offline until the next snapshot. The coupled snapshot fires only when at least one alert actually popped (app_iothub.c:1727-1728), so a fully debounced-away transition triggers no snapshot either - reconciliation then waits for the periodic heartbeat.
- `offline_duration_s` for `dev_type:"valve"` is NOT time since the link dropped. `last_seen_ms` for the valve is refreshed by any data-bearing BLE NOTIFY (app_ble_valve.c:436-445) and all valve notifies are delta-gated on value change (app_ble_valve.c:468, :476, :484, :492). A valve that sat quietly connected for six hours and then dropped reports roughly 6 h + 180 s. Treat it as "seconds since the valve last said anything", and use `ts` minus your own last-seen bookkeeping if you need true downtime.
- `battery` and `rssi` are last-known, not now. They are copied from the stored per-device state when the alert is built (health_engine.c:214-215). On a timeout-driven `device_offline` they are at least 10 minutes stale for sensors, arbitrarily stale for the valve.
- `rssi` is always absent for the valve. No code path ever populates `last_rssi` for a valve health record, and 0 is the omit sentinel - so `dev_type:"valve"` health events never carry `rssi`. Do not render a blank/zero signal bar from its absence.
- 0xFF/255 battery is indistinguishable from "unknown". `battery` is omitted exactly when the stored byte is 255 (health_engine.c:591). A sensor that genuinely reported 255 % is dropped as unknown. Likewise `rssi` is omitted exactly when the stored value is 0 dBm.
- `device_recovered` does not mean healthy. `rating` on a recovery can be "warning" or "good" (e.g. a sensor that comes back with a nearly dead battery). Read `rating`, not the event name.
- This family emits no nulls, but the snapshot does. In health events and cmd_ack every optional field is OMITTED (no key). In the `snapshot` message the same conceptual fields are emitted as explicit `null` (e.g. `battery`, `rssi`, `last_seen_age_s`, `fw_version`). A single shared parser must handle both "key missing" and "key present with null".
- Health alerts can be lost silently in three ways: (a) the alert queue is only 4 deep (health_engine.c:523) and a failed enqueue drops the alert without stamping the debounce; (b) the drain loop lives inside the provisioned-only branch of iothub_task (app_iothub.c:1672-1686, :1715-1722), so alerts raised while the hub is unprovisioned pile up and overflow; (c) anything built before the wall clock is valid (< 1704067200) is discarded by build_envelope and NOT offline-buffered (telemetry_v2.c:70-74).
- Offline-buffered health events replay out of order relative to lifecycle. On reconnect the NVS ring drains BEFORE the `lifecycle`/`online` message (app_iothub.c:1691-1692), and each replayed message keeps its ORIGINAL `ts` and `gateway.uptime_s`. The ring holds 16 entries and silently overwrites the oldest (offline_buffer.c:103-105); a payload over 512 bytes is truncated mid-string, producing invalid JSON (offline_buffer.c:77-81) - health/cmd_ack envelopes are ~250-400 bytes so this is unlikely but not impossible with a long `gateway.name` plus a long error detail.
- cmd_ack `error.code` is not an error code. It is literally `cmd_name`, a byte-for-byte duplicate of `data.cmd` (telemetry_v2.c:711). Branch on `error.detail` (or on your own request state), and be aware the detail strings are user-facing English prose that may be reworded in a future firmware - pin behaviour to the command plus HTTP-style intent, not to string equality, where you can.
- `status:"ok"` on a valve command means "queued", not "done". `ble_valve_open()`/`ble_valve_close()` only xQueueSend into the BLE task and the dispatcher discards their boolean result (app_iothub.c:664-671, app_ble_valve.c:1813-1823). Actual actuation arrives later as a valve event plus a snapshot. Never treat the ack as confirmation of physical state.
- Some commands are never acked. Legacy plain-text commands (`VALVE_OPEN`, `DECOMMISSION_LORA:0x…`, `RULES_CONFIG:{…}`) and bare provisioning JSON produce `is_envelope = false` with an empty `id`, so the `if (cmd.is_envelope || cmd.id[0])` gate at app_iothub.c:931 skips the ack entirely. Unparseable payloads also return silently (app_iothub.c:646-649). Always send commands as an `eflostop.cmd` envelope with an `id` if you want an ack.
- `decommission` target "all" acks "ok" BEFORE the work finishes. The early ack at app_iothub.c:791-793 is unconditional; the hub then waits up to ~1 s for the valve BLE link to drop, publishes a final `snapshot` with `data.reason:"decommission"`, waits 3 s and reboots to re-register with DPS (app_iothub.c:1561-1571). Expect the MQTT connection to disappear immediately after that snapshot.
- Field-length truncation is silent. `data.id` is capped at 63 characters and `data.cmd` (and therefore `error.code`) at 31 characters by the C2D parser (c2d_commands.h:24-25, c2d_commands.c:104/109). Correlation IDs longer than 63 characters will not round-trip - keep them to <= 63 (a UUID at 36 chars is safe).
- `sensor_id` is the key name even for the valve, and its formatting differs by device class: LoRa is always regenerated as uppercase `0x%08lX` (health_engine.c:466-467), while valve and BLE-leak MACs are echoed verbatim from the provisioning payload with no case normalisation (provisioning_manager.c:406, :462-464). Internal matching uses strcasecmp, so the hub tolerates any case - your join keys should too.
- Cadence expectations for false-offline tuning: sensor offline is declared 10:00-10:30 after the last check-in (10-min timeout, 30-s tick). LoRa posts a check-in on every decoded packet, but the BLE-leak scanner delta-gates advertisements and only forces a check-in every 5 minutes when data is unchanged (app_ble_leak.c:37, :189-195) - a 2x margin against the 10-minute timeout, so a couple of missed bursts will not trip it, but there is not much more headroom than that. Valve offline is declared 3:00-3:30 after the BLE link drops.
- Health events and cmd_ack are published from DIFFERENT tasks - health from iothub_task, cmd_ack from the esp-mqtt event task (handle_c2d_command). There is no ordering guarantee between a cmd_ack and the snapshot or health/rules event it caused, even though all use QoS 1 on the same topic.


---

## 7. Device twin reported properties + telemetry delivery semantics

### Two separate device→cloud channels

The hub sends data to Azure IoT Hub over **two structurally different MQTT paths**. They share one TLS/MQTT connection (`mqtts://<hub>.azure-devices.net`, MQTT 3.1.1, keepalive 60 s, SAS-token auth) but nothing else — different topics, different Azure-side plumbing, different retrieval APIs.

| | Telemetry (`eflostop.v2`) | Device Twin reported |
|---|---|---|
| Topic | `devices/<device_id>/messages/events/` | `$iothub/twin/PATCH/properties/reported/?$rid=<n>` |
| QoS / retain | 1 / 0 | 1 / 0 |
| Payload | `eflostop.v2` envelope (`schema`/`ts`/`gateway`/`type`/`data`) | **flat** JSON object, no envelope, no `schema`, no `ts` |
| Backend retrieval | Built-in Event Hub endpoint, or a custom route | Twin REST API / service SDK `getTwin()`, or Event Grid `Microsoft.Devices.DeviceTwinChanged` |
| Affected by IoT Hub message routing? | **Yes** | **No** |
| Publisher | `publish_json()` — `main/telemetry/telemetry_v2.c:99-126` | `publish_twin_reported()` — `main/iothub/app_iothub.c:942-994` |

> **Routing warning.** A custom IoT Hub route with condition `true` on the *Device Telemetry Messages* source diverts telemetry away from the built-in `messages/events` endpoint. Twin reported properties are **completely unaffected** by that — they never traverse the routing pipeline. If your consumer suddenly sees no telemetry after someone adds a route, the twin will still be updating normally, and vice-versa: watching the twin tells you nothing about whether telemetry is flowing.

`<device_id>` is whatever DPS returns in `registrationState.deviceId` (`main/dps_client/dps_client.c:205-215`). With the current symmetric-key **group** enrollment and no custom allocation policy that is the registration ID, i.e. the Gateway ID `GW-XXXXXXXXXXXX`. The topic is (re)built in `telemetry_v2.c:254-255` and again in `telemetry_v2_attach_client()` (`telemetry_v2.c:280-281`) once DPS has answered.

---

### A. Device Twin reported properties

`publish_twin_reported()` builds a **flat** cJSON object — no nesting at all. Note this differs from telemetry, where the same rules data is nested under `data.rules`. The topic carries an incrementing request id (`g_twin_rid`, `app_iothub.c:171`, `:987`); IoT Hub answers on `$iothub/twin/res/<status>/?$rid=<n>`, which the firmware subscribes to (`:1118`) and **only logs** (`:1144`) — there is no application-level verification that the patch was accepted, and the `esp_mqtt_client_publish()` return value at `:992` is discarded, so a failed patch is never retried.

**When it is sent — exactly three call sites, no periodic patch:**

1. **`app_iothub.c:1693`** — every MQTT (re)connect. `MQTT_EVENT_CONNECTED` sets `g_needs_lifecycle` (`:1109`); `iothub_task` consumes it and runs, in this order: `telemetry_v2_drain_offline()` → `telemetry_v2_publish_lifecycle()` → `publish_twin_reported()`. **Gated on `provisioned`** — the `if (!provisioned) … continue;` at `:1672` sits *before* this block, so an unprovisioned hub sends no twin patch and no lifecycle.
2. **`app_iothub.c:1043`** — end of `handle_twin_desired()`, after *any* desired-property PATCH notification that parses as JSON, whether or not a recognised key was present and whether or not the value was in range. Invalid JSON returns early at `:1011-1014` with no patch.
3. **`app_iothub.c:920`** — after a successful `set_hub_name` C2D command.

Call sites 2 and 3 run in the **esp-mqtt event task**; call site 1 runs in `iothub_task`. The function guards only on `mqtt_client == NULL` (`:946`), not on connectivity — but all three sites happen while the connection is up.

**Desired properties the hub understands** (`handle_twin_desired`, `app_iothub.c:1000-1044`): `snapshot_interval_s` (number, accepted only in `[60..3600]`, out-of-range silently logged and ignored) and `hub_name` (string, ≤ 31 chars).

---

### B. Telemetry delivery semantics

**Topic:** `devices/<device_id>/messages/events/` — note the **trailing slash and no property bag**. Every telemetry message of every `type` goes to this one topic.

**QoS 1, retain 0** — `esp_mqtt_client_publish(s_mqtt, s_topic, json_str, 0, 1, 0)` (`telemetry_v2.c:111`). Length argument `0` means "use `strlen`". At-least-once: duplicates are possible after a reconnect and the consumer must be idempotent. `CONFIG_MQTT_SKIP_PUBLISH_IF_DISCONNECTED` is not set and `CONFIG_MQTT_CUSTOM_OUTBOX` is not set (default heap outbox, default 30 s expiry).

**No application properties, no content-type, no content-encoding, no message-id, no correlation-id.** The firmware never appends a property bag (`$.ct` / `$.ce` / custom `key=value`) to the topic — grepping the whole `main/` tree finds only the two bare `devices/%s/messages/events/` `snprintf`s. Consequences:
- Azure treats the body as opaque bytes (`application/octet-stream`). **Body-based routing queries such as `$body.type = 'event'` can never match.** Only `true` or system/`$connectionDeviceId`-style conditions work.
- There is nothing on the message envelope to dispatch on. The consumer must parse the JSON and switch on the payload `type` field (`"lifecycle"` / `"snapshot"` / `"event"`) and then on `data.event`.
- There is no sequence number or message id anywhere in the payload. Dedupe on `(gateway.id, ts, type, data.event)`.

**Ordering.** Every `lifecycle`, `snapshot`, `event` (valve/leak/rules/health) publish is issued from the single `iothub_task` loop in a fixed within-iteration order, so wire order for those is deterministic. The **exception is `cmd_ack`**, published from `handle_c2d_command()` in the esp-mqtt event task (`app_iothub.c:792`, `:932`) — an ack is emitted the moment the command is processed, *before* the state change it acknowledges has been observed over BLE and *before* the coupled snapshot is flushed. `ack status:"ok"` means "accepted", never "applied". Beyond that, MQTT gives per-topic ordering only best-effort under reconnects — do not build logic that requires it.

**Wall-clock gate.** `build_envelope()` (`telemetry_v2.c:59-92`) checks `now < 1704067200` (2024-01-01 UTC) and returns `NULL`. Nothing is published *and nothing is buffered* — the message is never built, so pre-SNTP events are lost outright, not delayed. `ts` is Unix epoch **seconds, UTC**, stamped at build time, and cJSON prints it as a bare integer.

**Offline buffer** (`main/offline_buffer/offline_buffer.c`, NVS ring buffer):

- **Capacity 16 entries**, **512 bytes per entry** (`offline_buffer.h:12-13`). Metadata (`head`/`tail`/`count`) and the slots (`ob_00` … `ob_15`) live in namespace `offline_buf` of the **default `nvs` partition** (16 KB, `partitions.csv`) — *not* the dedicated `nvs_prov` partition used for commissioning. Survives reboot and power loss.
- **Only `type:"event"` is buffered.** `publish_json()` branches on `strcmp(type_hint, "event")` (`telemetry_v2.c:115-118`); `lifecycle` and `snapshot` are dropped when offline (`:119-122`) because they are regenerated on reconnect.
- **Overflow = drop-oldest.** At 16 entries the write proceeds and `tail` advances, silently overwriting the oldest event (`offline_buffer.c:100-108`). There is **no gap marker** in the telemetry — the backend cannot tell events were lost.
- **Oversize = TRUNCATE, not drop.** `len > 512` is clamped to 512 (`offline_buffer.c:77-81`), so a too-large event is replayed as **invalid, unparseable JSON**. Worst realistic cases land around 400–470 bytes (a `cmd_ack` error with a 63-char correlation id and a long `detail`, or a leak event with a 31-char hub name plus a 31-char label), so this is close to the limit. Parse defensively.
- **Drain timing.** `telemetry_v2_drain_offline()` (`telemetry_v2.c:728-736`) has exactly one caller: `app_iothub.c:1691`, inside the reconnect lifecycle block — so the drain runs **before** the `online` lifecycle and before the twin patch, and only once the hub is provisioned. Drain is strictly FIFO (oldest first), re-publishing the stored bytes verbatim at QoS 1 to the current topic (`offline_buffer.c:148`). On a publish failure it stops and leaves the remainder for the next reconnect (`:152-155`); on an NVS read failure it skips *and erases* that slot (`:143-145`, `:159`) — that event is gone.
- **Replayed events keep their original `ts`.** The whole envelope was serialised at event time, so `ts`, `gateway.uptime_s`, `gateway.name` and `gateway.fw` are all as-of-the-event, not as-of-the-replay. IoT Hub's `EnqueuedTimeUtc` will be the replay time.
- `offline_buffer_init()` runs at `app_iothub.c:1478` and resets the ring if the metadata is out of range (`offline_buffer.c:54-61`). `offline_buffer_clear()` **has no callers anywhere in the firmware** — not even `decommission all`.


#### `Device Twin reported-properties patch`

| | |
|---|---|
| **Envelope `type`** | `twin-reported (NOT a telemetry envelope — no schema/ts/gateway/data wrapper)` |
| **Source** | `main/iothub/app_iothub.c:942` |

**Trigger.** Three call sites only; there is NO periodic twin patch and NO retry. (1) app_iothub.c:1693 — on every MQTT (re)connect, immediately after the offline drain and the 'online' lifecycle message; gated on the hub being provisioned (the `if (!provisioned) continue;` at :1672 precedes it), so an unprovisioned hub never patches the twin. (2) app_iothub.c:1043 — at the end of handle_twin_desired(), after ANY desired-property PATCH notification that parses as JSON, even if no recognised key was present or the value was rejected as out of range; a patch that is invalid JSON returns early at :1011-1014 and produces no reported patch. (3) app_iothub.c:920 — after a successful `set_hub_name` C2D command. No debounce, no rate limit, no coalescing. The publish result is ignored (:992), so a lost patch is never retried — reported properties then stay stale until the next reconnect, desired patch, or set_hub_name.

```json
{
  "fw_version": "1.8.0",
  "gateway_id": "GW-A0764E9F28C0",
  "short_id": "28C0",
  "hub_name": "Kitchen Hub",
  "provisioned": true,
  "valve_mac": "00:80:E1:27:9A:E6",
  "lora_sensor_count": 2,
  "ble_leak_sensor_count": 3,
  "auto_close_enabled": true,
  "trigger_mask": 7,
  "uptime_s": 864,
  "free_heap": 142312
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `fw_version` | string | always | Hub firmware version, read at runtime from the ESP-IDF app descriptor (PROJECT_VER in CMakeLists.txt:12, currently "1.8.0"). Byte-identical to telemetry gateway.fw and to the boot-banner/OTA-header version. Built via telemetry_v2_fw_version() (telemetry_v2.c:50-54), which falls back to "0.0.0" if the descriptor is unreadable. app_iothub.c:951 |
| `gateway_id` | string | always | "GW-" + the 6-byte WiFi STA MAC as uppercase hex, no separators (15 chars, e.g. "GW-A0764E9F28C0"). Derived from eFuse, immutable for the life of the unit, and used as the DPS registration ID. Same value as telemetry gateway.id. hub_identity.c:33-35 / app_iothub.c:952 |
| `short_id` | string | always | Last two MAC bytes as 4 uppercase hex chars (e.g. "28C0"). Same value as telemetry gateway.short_id and the suffix of the setup AP SSID "WiFi-Hub-28C0". hub_identity.c:37 / app_iothub.c:953 |
| `hub_name` | string | always — but EMPTY STRING "" when no name has been assigned (never omitted, never null) | User-assigned friendly name, max 31 chars, persisted in NVS namespace "hub_ident" of the nvs_prov partition. Set via twin desired `hub_name` or C2D `set_hub_name`. NOTE the asymmetry with telemetry: gateway.name is OMITTED when unset, this field is present-but-empty. app_iothub.c:954 |
| `provisioned` | boolean | always | true only when the provisioning state machine is in PROV_STATE_PROVISIONED. Returns false if the provisioning manager failed to init or its 1 s mutex timed out (provisioning_manager.c:81-95). app_iothub.c:955 |
| `valve_mac` | string | conditional-OMITTED (key absent; never emitted as null) | Commissioned valve BLE MAC, uppercase colon-separated "AA:BB:CC:DD:EE:FF". Present only when provisioning_get_valve_mac() succeeds — i.e. state == PROVISIONED and a non-empty MAC is stored (provisioning_manager.c:583-601). Because this is a twin PATCH, omitting the key LEAVES THE PREVIOUS TWIN VALUE INTACT — after a valve decommission the twin keeps reporting the old MAC forever. app_iothub.c:957-959 |
| `lora_sensor_count` | number (integer, 0..16) | always | Count of commissioned LoRa leak sensors. provisioning_get_lora_sensors() explicitly writes 0 when unprovisioned, when none are commissioned, or on mutex timeout (provisioning_manager.c:627-650), so the key is always emitted. app_iothub.c:961-964 |
| `ble_leak_sensor_count` | number (integer, 0..16) | always | Count of commissioned BLE leak sensors; same 0-on-failure behaviour as lora_sensor_count (provisioning_manager.c:652-676). app_iothub.c:966-969 |
| `auto_close_enabled` | boolean | conditional-OMITTED (this key and trigger_mask are added or omitted together; never null) | Master enable for automatic valve close on leak. Emitted whenever provisioning_get_rules_config() returns true, which is whenever the provisioning manager is initialised and its 1 s mutex is acquired — note this does NOT require the hub to be provisioned, so in practice the key is effectively always present. Omitted only if provisioning_init() failed or the mutex timed out (provisioning_manager.c:977-991). Default true. Same value as telemetry data.rules.auto_close_enabled, but FLAT here vs nested there. app_iothub.c:971-975 |
| `trigger_mask` | number (integer, uint8 bitmask 0..7) | conditional-OMITTED (same guard as auto_close_enabled) | Which leak sources may trigger auto-close: bit0 (1) = BLE leak sensor, bit1 (2) = LoRa sensor, bit2 (4) = valve-integrated flood sensor. Default 7 (all). provisioning_manager.h:22-25. Same value as telemetry data.rules.trigger_mask. app_iothub.c:974 |
| `uptime_s` | number (integer) | always | Seconds since boot, esp_timer_get_time()/1000000 (integer-truncated). Independent of wall clock, so it is valid even before SNTP. app_iothub.c:977-978 |
| `free_heap` | number (integer, bytes) | always | esp_get_free_heap_size() at patch time. Diagnostic only; not present anywhere in telemetry. app_iothub.c:979-980 |


#### `Offline event replay (re-publish of an NVS-buffered event)`

| | |
|---|---|
| **Envelope `type`** | `event` |
| **Source** | `main/offline_buffer/offline_buffer.c:122` |

**Trigger.** On MQTT (re)connect, once the hub is provisioned, telemetry_v2_drain_offline() (telemetry_v2.c:728-736, sole caller app_iothub.c:1691) publishes every buffered event FIFO (oldest first) BEFORE the 'online' lifecycle message and before the twin patch. Each stored blob is re-published verbatim to devices/<device_id>/messages/events/ at QoS 1 (offline_buffer.c:148). Events are stored only while offline and only for type:"event" (telemetry_v2.c:115-118); the buffer holds 16 entries max, drop-oldest on overflow, 512 bytes max per entry (truncated, not dropped). Drain stops on the first publish failure and resumes next reconnect; an NVS read failure erases and loses that slot. There is no coalescing or de-duplication — if the same leak toggled several times offline, every transition replays.

```json
{
  "schema": "eflostop.v2",
  "ts": 1769712345,
  "gateway": {
    "id": "GW-A0764E9F28C0",
    "short_id": "28C0",
    "name": "Kitchen Hub",
    "fw": "1.8.0",
    "uptime_s": 3612
  },
  "type": "event",
  "data": {
    "event": "leak_detected",
    "source_type": "ble_leak_sensor",
    "sensor_id": "00:80:E1:27:9A:E7",
    "leak_state": true,
    "battery": 92,
    "rssi": -71,
    "location": {
      "code": "kitchen",
      "label": "Under sink"
    }
  }
}
```

| Field | Type | Presence | Meaning |
|---|---|---|---|
| `ts` | number (integer, Unix epoch seconds UTC) | always | The time the event OCCURRED, not the time it was replayed — build_envelope() stamped it before the envelope was serialised into NVS (telemetry_v2.c:66-76). Can be hours older than IoT Hub's EnqueuedTimeUtc. Order and de-duplicate on this, never on enqueued time. |
| `gateway.uptime_s` | number (integer) | always | Hub uptime at the ORIGINAL event time. Because buffered events survive reboots, a replayed event can report a LARGER uptime_s than a live message published seconds later in the new boot session. |
| `gateway.name` | string | conditional-OMITTED (absent when no hub name was set at the time the event was built) | Hub name as it was when the event occurred. A rename (or a `decommission all`, which clears the name but never clears the buffer) while events were pending means replayed events carry the OLD name while live ones carry the new one. |
| `gateway.fw` | string | always | Hub firmware version as of the original event. An OTA while events were buffered means replayed events report the pre-upgrade version. |
| `data` | object | always | Byte-identical to what the original publisher built — see the valve/leak/rules/health/cmd_ack event families for the per-event field contracts. The replay path adds nothing and rewrites nothing: there is no replay flag, no retry counter, and no gap marker for events lost to overflow. |


**Integration notes for this family**

- Twin reported properties never traverse IoT Hub message routing. A custom route with condition `true` on Device Telemetry Messages diverts telemetry away from the built-in endpoint and the twin keeps updating exactly as before — so a healthy twin is NOT evidence that telemetry is arriving, and a broken telemetry consumer is not evidence the hub is offline. Read the twin with the service SDK getTwin() / Twin REST API, or subscribe to Event Grid Microsoft.Devices.DeviceTwinChanged.
- Reported properties are a PATCH, not a replace, and the firmware never sends a key as null. `valve_mac`, `auto_close_enabled` and `trigger_mask` are the three conditionally-omitted keys — when omitted, the previous twin value SURVIVES. After a valve decommission the twin will keep reporting the old `valve_mac` indefinitely. Treat twin `valve_mac` as untrustworthy; use telemetry `data.valve.mac` plus `provisioned`/`lora_sensor_count`/`ble_leak_sensor_count` (which are always sent) for real state.
- Two different absence encodings for the same value: twin `hub_name` is ALWAYS present and is `""` when unset, while telemetry `gateway.name` is OMITTED when unset. Do not treat `""` and missing as different states.
- No telemetry message carries a content-type, content-encoding, or any application property — the publish topic is the bare `devices/<device_id>/messages/events/` with no property bag. Azure therefore treats every body as opaque bytes, so routing/enrichment queries on the JSON body (`$body.type = 'event'`, `$body.data.event = 'leak_detected'`) can NEVER match. Only `true` or system-property conditions work. All three message types share one topic; you must parse the JSON and dispatch on the payload `type` field yourself.
- The twin never echoes `snapshot_interval_s`. If you write a desired heartbeat interval you cannot confirm from the twin that it was accepted — values outside [60..3600] are silently ignored with only a device-side log (app_iothub.c:1018-1025), and the accepted value is not reported anywhere. The only observable confirmation is the actual snapshot cadence.
- Desired-property writes are lost if the hub is offline. The firmware never issues a twin GET (there is no `$iothub/twin/GET` publish anywhere in the codebase) and MQTT clean-session is enabled, so it only ever sees live `$iothub/twin/PATCH/properties/desired` notifications — a desired patch written while the hub is disconnected is never applied, not even on reconnect. Either re-write desired properties after you observe the hub's reconnect reported-patch, or use the `set_hub_name` C2D command instead (IoT Hub queues C2D messages independently of the MQTT session).
- An unprovisioned hub is almost completely silent. The event loop's `if (!provisioned) continue;` (app_iothub.c:1672) sits before the lifecycle block, so a factory-fresh or freshly-decommissioned hub connects and subscribes but sends no lifecycle, no snapshot, no offline drain and no twin patch — only `cmd_ack` (published from the C2D handler, which is not gated). Do not treat twin/telemetry silence as 'hub offline'.
- Twin patches are fire-and-forget: the publish return code is discarded (app_iothub.c:992) and the `$iothub/twin/res/<status>` reply is only logged (:1144). A rejected or dropped patch leaves the twin stale until the next reconnect, desired patch, or `set_hub_name` — there is no retry and no periodic refresh.
- All eflostop.v2 telemetry is suppressed until the wall clock passes 2024-01-01 (build_envelope, telemetry_v2.c:66-74). Because the envelope is never even built, those events are NOT offline-buffered either — they are lost outright, not delayed. Expect a silent window after every cold boot until SNTP lands.
- Replayed offline events keep their ORIGINAL `ts`, `gateway.uptime_s`, `gateway.name` and `gateway.fw`. Always order and de-duplicate on payload `ts`, never on IoT Hub EnqueuedTimeUtc. Because the drain runs BEFORE the `online` lifecycle (app_iothub.c:1691-1692), the lifecycle for the current session arrives AFTER older events, and a replayed event can report a larger `uptime_s` than a live message sent moments later in a newer boot session.
- The offline buffer holds at most 16 events; the 17th silently overwrites the oldest (offline_buffer.c:100-108). Nothing in the telemetry signals the loss — no gap marker, no sequence number, no drop counter. A long outage with a chattering sensor will silently discard the earliest transitions, so never reconstruct leak state purely by replaying the event stream; reconcile against the next `snapshot`.
- Per-entry cap is 512 bytes and an oversized event is TRUNCATED rather than dropped (offline_buffer.c:77-81), so it is replayed as invalid, unparseable JSON. Worst realistic payloads reach roughly 400-470 bytes (a `cmd_ack` error carrying a 63-char correlation id plus a long `detail`, or a leak event with a 31-char hub name and a 31-char sensor label), i.e. uncomfortably close to the limit. Wrap the JSON parse in a try/catch and dead-letter rather than crash.
- Only `type:"event"` is buffered offline. `lifecycle` and `snapshot` are dropped while disconnected (telemetry_v2.c:119-122) on the assumption they are regenerated on reconnect — so there is no historical snapshot series across an outage, only a fresh one afterwards.
- Events are NOT buffered when the hub is connected but the publish call itself fails. `publish_json()` only takes the offline branch when `!(s_mqtt && s_connected)`; a negative `msg_id` (e.g. saturated QoS-1 outbox) on the online branch is logged and the event is silently dropped (telemetry_v2.c:108-118). Snapshots detect this and back off 5 s (app_iothub.c:1915-1921), but events have no such recovery.
- The offline buffer lives in namespace `offline_buf` of the DEFAULT 16 KB `nvs` partition — the only module still there; commissioning, identity, DPS cache, sensor metadata and the override window all moved to the dedicated `nvs_prov` partition. The default partition is shared with the WiFi driver and captive-portal credentials, so 16 x 512-byte blobs are real pressure: if NVS is full, `offline_buffer_store()` returns false and the event vanishes with only a log, and if the partition ever runs out of free pages at boot, app_main erases the whole default partition (main.c:45-50), taking pending events with it.
- `offline_buffer_clear()` has no callers anywhere in the firmware — `decommission all` (app_iothub.c:772-799) clears provisioning, sensor metadata, hub identity, the DPS cache and the rules state, but not the event buffer. A decommissioned hub can therefore replay pre-decommission events, still stamped with the OLD `gateway.name`, on its next connection under a re-registered identity.
- QoS 1 is at-least-once and there is no sequence number or message id in any payload. Duplicates across reconnects are expected; make the consumer idempotent and de-duplicate on (`gateway.id`, `ts`, `type`, `data.event`).
- `cmd_ack` is published from the C2D handler in the esp-mqtt event task (app_iothub.c:792, :932), i.e. the moment the command is parsed and dispatched — before the resulting BLE state change is observed and before the coupled snapshot is flushed by `iothub_task`. `status:"ok"` means 'command accepted', never 'valve actually moved'. Confirm the effect from the following `valve_state_changed` event or `snapshot`, not from the ack.

---

## 8. DPS registration — the one non-telemetry device→cloud path

Before the hub can reach IoT Hub at all it must obtain its assignment from the Azure Device Provisioning
Service. This is a **separate endpoint and a separate topic namespace** — not IoT Hub telemetry — but it
is device→cloud traffic, it is the first traffic in a hub's life, and it is the origin of the
`<device_id>` embedded in every topic above. It is included here so first-boot traffic is explainable.

| Property | Value |
|---|---|
| Endpoint | `global.azure-devices-provisioning.net` (not the IoT Hub host) |
| ID scope | Compiled in (`AZURE_DPS_ID_SCOPE`, [app_iothub.h](../../main/iothub/app_iothub.h)) |
| Registration ID | The Gateway ID, `GW-XXXXXXXXXXXX` |
| Auth | SAS derived as `HMAC-SHA256(enrollment_group_key, registration_id)` |

**Two publishes:**

| # | Topic | Payload | QoS |
|---|---|---|---|
| 1 | `$dps/registrations/PUT/iotdps-register/?$rid=1` | `{"registrationId":"GW-XXXXXXXXXXXX"}` | 1 |
| 2 | `$dps/registrations/GET/iotdps-get-operationstatus/?$rid=<n>&operationId=<id>` | **empty / zero-length** | 1 |

Publish 2 repeats on an interval while the registration is still assigning
([dps_client.c:288](../../main/dps_client/dps_client.c#L288), [:312](../../main/dps_client/dps_client.c#L312)).
**An empty body on the poll is normal**, not a malformed message.

**When it happens.** Only on first boot, after `dps_clear_cache()`, after a `decommission all` reboot, or
after a **provisioning-epoch bump** (a firmware change that repoints the fleet at new infrastructure).
The assignment is otherwise cached in NVS and DPS is never contacted again.

**Why you care:** seeing these again for a hub that was already registered is a meaningful fleet event —
it means the hub re-registered. It is also the only observable signal for a hub in the state this document
otherwise describes as silent (unprovisioned, pre-lifecycle).

**One consequence for event ordering.** Telemetry is initialised *before* DPS with no client attached, so
events occurring pre-registration are written to the NVS ring and replayed once a device id exists
([app_iothub.c:1480-1486](../../main/iothub/app_iothub.c#L1480-L1486)). You can therefore legitimately
receive an event whose `ts` **predates the device's own registration record** in IoT Hub.

---

## 9. Behaviours that will surprise you

These are not payload shapes — they are emission patterns that a backend built purely from the message
schemas above will misinterpret. Each was found by an audit pass against the source.

### 9.1 A scheduled reconnect every ~18 hours produces a benign `lifecycle` + `fast` snapshot ⚠️

The hub renews its Azure SAS token when less than 6 h remains on a 24 h token — i.e. roughly **every
18 hours on a perfectly healthy network** ([app_iothub.c:64-65](../../main/iothub/app_iothub.c#L64-L65),
[:1325-1349](../../main/iothub/app_iothub.c#L1325-L1349)). Renewal is a real MQTT
stop → reconfigure → start ([:1281-1315](../../main/iothub/app_iothub.c#L1281-L1315)), so it fires
`MQTT_EVENT_CONNECTED`, which re-latches the lifecycle block
([:1104-1110](../../main/iothub/app_iothub.c#L1104-L1110), [:1689-1697](../../main/iothub/app_iothub.c#L1689-L1697)).

Consequences, all on a healthy hub:
- an unsolicited `type:"lifecycle"` / `data.event:"online"` (~1.3× per hub per day)
- a Device Twin reported PATCH
- a fresh `reason:"fast"` snapshot
- a brief window (seconds) where events buffer to NVS instead of publishing

**Do not treat a repeat `lifecycle` as evidence of an unstable network or a reboot.** To distinguish a
real restart from a reconnect, use `gateway.uptime_s` (which keeps climbing across a reconnect) or
`data.reset_reason` — not the presence of a lifecycle message. Suppress "hub reconnected" alerting
unless `uptime_s` decreased.

> **Open firmware item:** this noise is an artifact of proactive renewal, not a requirement. It can be
> removed by marking the renewal as an internal reconnect so it does not re-latch the lifecycle block.
> Tracked for a follow-up release.

### 9.2 Twin reported properties go stale after a configuration change ⚠️

`publish_twin_reported()` has exactly **three** call sites: MQTT reconnect, the end of
`handle_twin_desired`, and a successful `set_hub_name`
([app_iothub.c:1693](../../main/iothub/app_iothub.c#L1693), [:1043](../../main/iothub/app_iothub.c#L1043), [:920](../../main/iothub/app_iothub.c#L920)).

The C2D commands `rules_config`, `provision` and partial `decommission` **never** re-patch the twin. And
because a twin patch omits absent keys rather than nulling them, the old values persist. So
`auto_close_enabled`, `trigger_mask`, `provisioned`, `lora_sensor_count` and `ble_leak_sensor_count` can
all be **stale for up to ~18 h** (until the next reconnect) after the user changes them.

**Read configuration state from the `snapshot`, not from the twin.** The twin is reliable for identity
(`gateway_id`, `short_id`, `fw_version`) and little else.

### 9.3 Two valve notifications produce no event at all

`BLE_UPD_BATTERY` and `BLE_UPD_RMLEAK` refresh internal state but emit **no** device→cloud event
([app_iothub.c:1774-1794](../../main/iothub/app_iothub.c#L1774-L1794)). Valve battery changes and
valve-side RMLEAK 0↔1 transitions are visible only in the next `snapshot` (`valve.battery`,
`valve.rmleak`), or indirectly via a rules event.

**Consequence for the app:** if you gate the "Open valve" control on `valve.rmleak` and wait for an
event before refreshing, the control can sit in the wrong state for up to a full heartbeat interval.
Reconcile against the snapshot.

### 9.4 An empty hub name silently clears it — and still acks `ok`

`{"name":""}` via either the twin desired path or the `set_hub_name` C2D command reaches
`hub_identity_set_name()`, which treats an empty string as a **clear**
([hub_identity.c:91-97](../../main/hub_identity/hub_identity.c#L91-L97)). The command returns
`status:"ok"`. Afterwards twin `hub_name` is `""` and **`gateway.name` disappears from every subsequent
envelope**. Do not round-trip a blank form field into this command.

### 9.5 Pre-SNTP messages are lost, not queued

Before the wall clock is valid (`ts < 1704067200`), `build_envelope()` destroys the message and returns
NULL ([telemetry_v2.c:66-74](../../main/telemetry/telemetry_v2.c#L66-L74)). It reaches neither MQTT
**nor the NVS offline buffer** — even a leak event. In practice a *connected* hub always has a valid
clock, because the SAS token cannot be minted below the same threshold, so MQTT is held down until SNTP
lands. But the window exists between Wi-Fi association and the first NTP reply.

There is therefore **no such thing as an `eflostop.v2` message with `ts` below 2024-01-01**.

---

## 10. Known schema inconsistencies

Real inconsistencies in the current contract. Documented as-is; changing them would be a breaking
change, so they are candidates for a future additive fix rather than a silent correction.

| Issue | Detail | Impact |
|---|---|---|
| **~~Two type vocabularies~~ — FIXED in 1.8.0** | `source_type` ∈ `ble_leak_sensor` \| `lora` \| `valve_flood` \| `reconnect` and `dev_type` ∈ `valve` \| `lora` \| `ble_leak_sensor` now agree on `ble_leak_sensor`. **Before 1.8.0 `dev_type` was `ble_leak_sensor`** — accept both if you support older hubs. Inbound `sensor_type` / `decommission target` accept `ble_leak_sensor`, `ble_leak_sensor` and `ble` permanently. | One lookup table now works. `valve` vs `valve_flood` stay distinct on purpose — device vs leak source. |
| **`reconnect` is an undeclared `source_type`** | `source_to_str()` yields `ble_leak_sensor` \| `lora` \| `valve_flood` \| `unknown`, but the valve-reconnect catch-up path writes the literal `"reconnect"` directly ([rules_engine.c:935](../../main/rules_engine/rules_engine.c#L935)). | Accept `reconnect` in `source_type` even though it is not in the enum. |
| **Health events carry no `location`** | `auto_close` and the leak events include `location:{code,label}`; health events do not, despite carrying `sensor_id`. | "Laundry sensor is offline" cannot be rendered from the event alone — join on `sensor_id` against the last snapshot. |
| **MAC case is not guaranteed** | Uppercase only when the valve is connected; the disconnected fallback echoes the provisioning string, and lowercase hex is accepted at provisioning time. LoRa IDs are always uppercase (`0x%08lX`). | **Compare all MAC-shaped identifiers case-insensitively.** |
| **`valve.fw_version` has three states** | A string when known; `null` when the link is up but the DIS read failed; **absent** when the valve is disconnected. | Distinguish absent from null, or you will misreport a disconnected valve. |

---

## 11. Integration checklist

1. **Discriminate on `schema` == `eflostop.v2`**, then on `type`, then on `data.event`.
2. **Ignore unrecognised `data.event` values** rather than rejecting the message — the schema is
   additive-only and new events will appear.
3. **Treat missing and `null` as equivalent** unless §10 says otherwise.
4. **Reconcile against `snapshot`, not events.** Events are notification hints; the snapshot is the
   authoritative state. This is doubly true for config (§9.2) and valve RMLEAK (§9.3).
5. **Do not derive countdowns from `expires_ts` against the phone clock** — use `remaining_s` /
   `override_remaining_s`, which are clock-skew-proof.
6. **Never treat a `cmd_ack` timeout as failure** — reconcile against the next snapshot first. A
   timeout means "possibly offline", not "did not happen".
7. **Gate features on `gateway.fw`**, not on error responses.
8. **Expect replayed events with old timestamps.** After a reconnect, up to 16 buffered events replay
   FIFO carrying their **original** `ts` and `uptime_s` — possibly from before the last reboot, and
   possibly predating the device's own registration record. Order by `ts`, not arrival.
9. **Message routing note.** Telemetry goes to the built-in endpoint via the fallback route. Twin
   reported does **not** — it is unaffected by message routing. A custom route with condition `true`
   diverts all telemetry away from the built-in endpoint while twin updates keep flowing.
