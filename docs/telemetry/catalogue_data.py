# ruff: noqa: E501
"""Single source of truth for the eFloStop II Hub Telemetry Catalogue.

Everything the document says lives here or in `fields.json` (the 91-field master).
`build_catalogue.py` renders this into the .docx, the .md, the JSON Schemas and the CSV,
so those four artefacts can never drift apart.

CITATION RULE: every factual claim carries a repo-relative `path:line`. A claim with no
citation does not belong in this file. Values that are not stated in firmware source are
written literally as "Not specified in source" — never guessed.

TAGS: [INFERRED] = reasoning beyond literal code, with its citation chain.
      [UNVERIFIED] = could not be resolved from this repo; also listed in §18.
      [EXTERNAL] = citation points outside this repo (ESP-IDF / cJSON).
"""

# --------------------------------------------------------------------------------------
# Document metadata
# --------------------------------------------------------------------------------------

META = {
    "title": "eFloStop II Wi-Fi Hub — Azure IoT Hub Telemetry Catalogue",
    "subtitle": "Complete telemetry surface and consolidation review",
    "doc_version": "1.0",
    "date": "2026-07-31",
    "author": "Claude Code",
    "repo": "eFloStop_WiFiHub_idf1",
    "classification": "Internal — for joint firmware/backend consolidation review",
    # git_sha, git_branch, tree_state and fw_version are injected at build time by
    # build_catalogue.py from `git rev-parse` and CMakeLists.txt so they cannot go stale.
    "fw_version_citation": "CMakeLists.txt:12",
    "schema_string": "eflostop.v2",
    "schema_citation": "main/telemetry/telemetry_v2.c:64; constant main/telemetry/telemetry_v2.h:14",
}

REVISION_HISTORY = [
    ("1.0", "2026-07-31", "Claude Code", "First issue. Full surface enumerated from firmware source; consistency audit across seven dimensions."),
]

# --------------------------------------------------------------------------------------
# §3 Architecture
# --------------------------------------------------------------------------------------

ARCHITECTURE_INTRO = """The eFloStop II Wi-Fi Hub is an ESP32-S3 gateway. It has no sensors of its own for
water detection: it listens to battery-powered peers over two radios, keeps a picture of their state in RAM,
and reports that picture to Azure IoT Hub over MQTT. It also drives a motorised valve, which it can close
automatically when a leak is detected.

Three kinds of peer exist. A single STM32WB **valve controller** connects over BLE GATT and both reports its
state (position, battery, its own flood probe, and a leak interlock) and accepts commands. Multiple STM32WBA
**leak sensors** never connect at all — they broadcast BLE advertisements carrying leak status and battery,
and the hub scans passively. **LoRa sensors** are supported in firmware but the LoRa radio is not populated on
the current PCBA, so in practice that array is empty; the fields remain in the contract and are documented
here for completeness.

Everything the hub sends upstream is produced by one serializer function and leaves through one publish call,
which is why the message format is uniform. Understanding that single path is most of what a cloud consumer
needs."""

PLANES = [
    {
        "plane": "D2C telemetry",
        "direction": "Device → cloud",
        "topic": "devices/<device_id>/messages/events/",
        "envelope": "eflostop.v2 (schema, ts, gateway, type, data)",
        "publish_site": "main/telemetry/telemetry_v2.c:111 (live); main/offline_buffer/offline_buffer.c:148 (replay)",
        "note": "The subject of this document. Three envelope `type` values carry seven payload families.",
    },
    {
        "plane": "Device twin — reported",
        "direction": "Device → cloud",
        "topic": "$iothub/twin/PATCH/properties/reported/?$rid=N",
        "envelope": "None — 12 flat keys, no envelope, and NOT suppressed before clock sync",
        "publish_site": "main/iothub/app_iothub.c:994",
        "note": "A durable state document, not a message stream. 11 of its 12 keys also exist on the D2C plane.",
    },
    {
        "plane": "Device twin — desired",
        "direction": "Cloud → device",
        "topic": "$iothub/twin/PATCH/properties/desired/#  (subscribe)",
        "envelope": "None — 2 flat keys",
        "publish_site": "n/a (inbound)",
        "note": "The hub never publishes $iothub/twin/GET, so a desired change written while the hub is offline is never fetched (verified: no such string in main/).",
    },
    {
        "plane": "C2D commands",
        "direction": "Cloud → device",
        "topic": "devices/<device_id>/messages/devicebound/#  (subscribe)",
        "envelope": "eflostop.cmd envelope, or two legacy forms",
        "publish_site": "n/a (inbound)",
        "note": "11 commands. There is no direct-method subscription anywhere, so the only acknowledgement channel is a D2C telemetry event (cmd_ack).",
    },
    {
        "plane": "DPS provisioning",
        "direction": "Device → cloud, pre-hub",
        "topic": "$dps/registrations/PUT/iotdps-register/?$rid=1 (register) and $dps/registrations/GET/iotdps-get-operationstatus/?$rid=N&operationId=<operation id> (poll)",
        "envelope": "None — a single registrationId key and nothing else",
        "publish_site": "main/dps_client/dps_client.c:288, :312",
        "note": "A different broker and namespace, used once to discover which IoT Hub to talk to. Included so the publish-site count is complete.",
    },
]

PUBLISH_SITES = [
    ("main/dps_client/dps_client.c:288", "DPS", "$dps/registrations/PUT/iotdps-register/?$rid=1", "1", "0", "MQTT_EVENT_SUBSCRIBED on the DPS client while state == SUBSCRIBING (:272-275). Live registration only — a cache hit skips it entirely."),
    ("main/dps_client/dps_client.c:312", "DPS", "$dps/registrations/GET/iotdps-get-operationstatus/?$rid=N&operationId=<operation id> (:308-311)", "1", "0", "Registration response parsed as still-pending. NULL payload; 3 s inter-poll delay (:304)."),
    ("main/iothub/app_iothub.c:994", "Twin reported", "$iothub/twin/PATCH/properties/reported/?$rid=N (:990-991)", "1", "0", "Three callers only: set_hub_name success (:922), end of every parseable desired patch (:1045), and the provisioned MQTT (re)connect block (:1695)."),
    ("main/telemetry/telemetry_v2.c:111", "D2C telemetry", "devices/<device_id>/messages/events/ (:254-255)", "1", "0", "The only live D2C site. Fed by seven publisher wrappers. Online branch only — offline, events are buffered and other types dropped (:115-122)."),
    ("main/offline_buffer/offline_buffer.c:148", "D2C telemetry (replay)", "same topic, passed in from main/telemetry/telemetry_v2.c:734", "1", "0", "Once per MQTT connect, before lifecycle (main/iothub/app_iothub.c:1693). Replays stored bytes verbatim, so ts and gateway.uptime_s are the ORIGINAL values, possibly from a previous boot."),
]

# --------------------------------------------------------------------------------------
# §10 Message types.  `id` is the controlled vocabulary used everywhere else.
# --------------------------------------------------------------------------------------

MESSAGE_TYPES = [
    {
        "id": "lifecycle", "title": "Lifecycle (\"online\")", "plane": "D2C telemetry",
        "type_value": "lifecycle", "event_value": "online",
        "purpose": "Announces that the hub has an MQTT session and states how it is commissioned. It is the cloud's cue that a hub is reachable and what it believes it is responsible for.",
        "trigger": "Every MQTT connect, once the hub is provisioned — not once per boot. A reconnect after a network drop produces another one.",
        "citation_trigger": "main/iothub/app_iothub.c:1694; ordering :1693-1695",
        "cadence": "Per MQTT connect (including scheduled reconnects).",
        "builder": "main/telemetry/telemetry_v2.c:334-368",
        "retry": "Not retried. If the publish fails, the message is lost; the next connect produces a fresh one.",
        "notes": "Emitted AFTER the offline buffer is drained and BEFORE the twin reported patch (main/iothub/app_iothub.c:1693-1695). It is never buffered when offline, because publish_json only buffers type==\"event\" (main/telemetry/telemetry_v2.c:115-122).",
    },
    {
        "id": "snapshot", "title": "Snapshot (full state roll-up)", "plane": "D2C telemetry",
        "type_value": "snapshot", "event_value": None,
        "purpose": "The complete current state of the hub, the valve and every commissioned sensor in one message. It is the broadest view of the system, but note that it is NOT authoritative for leak state: F-01 shows that a sensor which has gone silent reports leak_state:false in a snapshot while the leak_detected event already published for it is never retracted. For leak state the event stream is authoritative and the snapshot is not.",
        "trigger": "A 300 s heartbeat, plus a coupled snapshot after most events, plus commissioning changes, plus one at boot.",
        "citation_trigger": "interval default main/telemetry/telemetry_v2.c:42; sole flush main/iothub/app_iothub.c:1860-1926; out-of-band decommission publish :1570",
        "cadence": "300 s default. The interval is settable via the twin desired property snapshot_interval_s (clamped 60..3600, main/iothub/app_iothub.c:1022) but is held in RAM only, so it reverts to 300 s on every reboot.",
        "builder": "main/telemetry/telemetry_v2.c:372-604",
        "retry": "The heartbeat is re-armed only when the publish actually reached esp-mqtt with msg_id >= 0 (success branch main/iothub/app_iothub.c:1884-1916). If the publish fails while connected — for example a full QoS-1 outbox — the next attempt is floored 5 s out rather than retried tightly (failure branch :1917-1923; SNAP_RETRY_FLOOR_MS defined at :153).",
        "notes": "data.reason names the trigger. Seven values are reachable: five from snap_reason_str (main/iothub/app_iothub.c:581-591), plus \"decommission\" passed as a literal (:1570), plus \"c2d_command\" which appears on the same key inside the auto_close_reenabled rules event (main/rules_engine/rules_engine.c:722).",
    },
    {
        "id": "valve_event", "title": "Valve event", "plane": "D2C telemetry",
        "type_value": "event", "event_value": "valve_state_changed | valve_flood_detected | valve_flood_cleared",
        "purpose": "Reports that the valve moved, or that the valve's own on-board flood probe went wet or dry.",
        "trigger": "A BLE GATT notification from the valve whose value differs from the last one seen (delta-gated).",
        "citation_trigger": "main/iothub/app_iothub.c:1777-1791; delta gate reset to -2 on reconnect :1658",
        "cadence": "Event-driven, delta-gated. No fixed rate.",
        "builder": "main/telemetry/telemetry_v2.c:608-629",
        "retry": "Buffered for replay if the hub is offline (main/telemetry/telemetry_v2.c:115-118).",
        "notes": "There is NO valve_disconnected event. g_valve_mac is zeroed at main/ble_valve/app_ble_valve.c:1271 BEFORE notify_hub_update(BLE_UPD_DISCONNECTED) at :1272, so the MAC lookup fails, mac_ok is false, and the whole valve-event block is skipped (main/iothub/app_iothub.c:1761-1796). Valve loss is visible only via the next snapshot's connected:false and a health device_offline event.",
    },
    {
        "id": "leak_event", "title": "Leak event", "plane": "D2C telemetry",
        "type_value": "event", "event_value": "leak_detected | leak_cleared",
        "purpose": "Reports that a specific leak sensor went wet or dry. This is the safety-critical message.",
        "trigger": "A LoRa packet or BLE advertisement whose leak state differs from the cached value.",
        "citation_trigger": "main/iothub/app_iothub.c:1747-1750 (LoRa), :1807-1811 (BLE)",
        "cadence": "Event-driven, delta-gated. The first sighting of a sensor emits only if it is currently leaking (main/iothub/app_iothub.c:289, :300, :336, :350).",
        "builder": "main/telemetry/telemetry_v2.c:631-654",
        "retry": "Buffered for replay if offline. Destroyed, not buffered, if the clock has not synced (see §4.3).",
        "notes": "Carries the sensor's identity flat at data.sensor_id, whereas a snapshot carries the same sensor nested inside an array element. The two representations differ — see §4.6.",
    },
    {
        "id": "rules_event", "title": "Rules-engine event", "plane": "D2C telemetry",
        "type_value": "event", "event_value": "auto_close | auto_close_blocked_override | auto_close_reenabled | rmleak_cleared | rmleak_auto_cleared | water_access_override_enabled | water_access_override_expired",
        "purpose": "Reports a decision the hub made: it closed the valve because of a leak, it declined to close because a user override was active, or an override window started, was cancelled, or expired.",
        "trigger": "The rules engine, on leak evaluation, on a C2D override command, on valve reconnect, and on its periodic tick.",
        "citation_trigger": "main/rules_engine/rules_engine.c:213-222, :328-357, :467-478, :676-685, :718-726, :932-943, :1023-1032, :1080-1087",
        "cadence": "Event-driven.",
        "builder": "main/telemetry/telemetry_v2.c:656-674 (envelope wrapper); payloads built in main/rules_engine/rules_engine.c",
        "retry": "Buffered for replay if offline.",
        "notes": "auto_close has TWO structurally different shapes: the sensor-triggered form carries an optional location object (main/rules_engine/rules_engine.c:328-357), while the valve-reconnect form instead carries active_leak_count and uses source_type:\"reconnect\", which is not a member of the leak_source_t enum (:932-943). Nothing in source reconciles them.",
    },
    {
        "id": "health_event", "title": "Health event", "plane": "D2C telemetry",
        "type_value": "event", "event_value": "device_offline | device_recovered",
        "purpose": "Reports that a peer device crossed into, or out of, the 'critical' health rating.",
        "trigger": "The health engine's rating recomputation, on a 30 s tick.",
        "citation_trigger": "main/health_engine/health_engine.c:577-608; drained at main/iothub/app_iothub.c:1720",
        "cadence": "Event-driven off a 30 s evaluation tick (main/health_engine/health_engine.h:18).",
        "builder": "main/health_engine/health_engine.c:577-608 (payload); envelope main/telemetry/telemetry_v2.c:676-693",
        "retry": "Buffered for replay if offline.",
        "notes": "These are the only events carrying data.category:\"health\" — that key is how you recognise them. The name is derived from a rating comparison rather than from a distinct event type: is_offline = (new_rating == HEALTH_CRITICAL) (main/health_engine/health_engine.c:586). In practice critical is only ever reached for connectivity reasons - a sensor never seen or past its 600 s timeout (:122-123), or a valve past its 3-minute disconnect grace (:148-150), or a device not yet heard from since boot (:457, :472, :487). A low battery yields warning, never critical (:126-127), so the name does match the cause.",
    },
    {
        "id": "cmd_ack", "title": "Command acknowledgement", "plane": "D2C telemetry (acknowledging a C2D command)",
        "type_value": "event", "event_value": "cmd_ack",
        "purpose": "Reports the outcome of an inbound cloud-to-device command.",
        "trigger": "Completion of C2D command handling, success or failure.",
        "citation_trigger": "main/iothub/app_iothub.c:932-935, :793-795",
        "cadence": "One per inbound command.",
        "builder": "main/telemetry/telemetry_v2.c:695-718",
        "retry": "Buffered for replay if offline.",
        "notes": "This message straddles two planes: it is the response to a C2D command but is delivered as D2C telemetry, because there is no direct-method subscription anywhere in the firmware. It is also published from the esp-mqtt event task rather than the main IoT Hub task, so it bypasses the provisioned gate and the snapshot scheduler and can interleave at any point in the stream.",
    },
    {
        "id": "twin_reported", "title": "Device twin — reported properties", "plane": "Device twin (NOT D2C telemetry)",
        "type_value": None, "event_value": None,
        "purpose": "A durable statement of the hub's identity and commissioning state, held by IoT Hub and readable at any time without waiting for a message.",
        "trigger": "Three callers: a successful set_hub_name, the end of every parseable desired-property patch, and the provisioned MQTT (re)connect block.",
        "citation_trigger": "main/iothub/app_iothub.c:922, :1045, :1695",
        "cadence": "Not periodic. Only on the three triggers above.",
        "builder": "main/iothub/app_iothub.c:944-996",
        "retry": "None.",
        "notes": "Flat keys, no envelope, no ts. Critically, it is NOT subject to the pre-clock-sync suppression that destroys D2C telemetry, so a twin patch can arrive from a hub whose D2C stream is still being discarded.",
    },
    {
        "id": "c2d_command", "title": "C2D command (inbound)", "plane": "C2D commands (INBOUND — not telemetry)",
        "type_value": None, "event_value": None,
        "purpose": "The cloud's control surface: open/close the valve, clear a leak interlock, commission or decommission devices, configure rules, rename the hub.",
        "trigger": "Cloud-initiated.",
        "citation_trigger": "C2D subscribe main/iothub/app_iothub.c:1116-1118; dispatch :643-938",
        "cadence": "On demand.",
        "builder": "Parsed by main/commands/c2d_commands.c; dispatched in main/iothub/app_iothub.c:657-935",
        "retry": "n/a (inbound). Every command produces exactly one cmd_ack telemetry event.",
        "notes": "Three inbound formats are accepted: the canonical eflostop.cmd envelope, a legacy envelope (eflostop.cmd.v1), and legacy plain text. The legacy text forms are normalised in-process into a synthetic canonical payload object so the dispatcher sees one shape.",
    },
    {
        "id": "dps_registration", "title": "DPS registration (pre-hub)", "plane": "DPS provisioning (a separate broker)",
        "type_value": None, "event_value": None,
        "purpose": "First-boot discovery of which IoT Hub this device belongs to, using a symmetric key derived from a group key.",
        "trigger": "First boot, or any boot where the NVS cache is absent or its provisioning epoch does not match.",
        "citation_trigger": "main/dps_client/dps_client.c:272-288",
        "cadence": "Once per device lifetime in the normal case; the result is cached in NVS.",
        "builder": "main/dps_client/dps_client.c:283-284",
        "retry": "Polls for operation status every 3 s (main/dps_client/dps_client.c:304).",
        "notes": "The payload is the only hand-built (non-cJSON) JSON that leaves the device. It carries a registration id and nothing else — no tags, no model id — which means enrolment tags cannot originate in firmware.",
    },
]

# Message types that are D2C telemetry, in the order they appear in §10.
D2C_MESSAGE_IDS = ["lifecycle", "snapshot", "valve_event", "leak_event", "rules_event", "health_event", "cmd_ack"]

# --------------------------------------------------------------------------------------
# §12 taxonomy
# --------------------------------------------------------------------------------------

SECTIONS_12 = [
    ("12.1", "Device identity & provisioning", "Who this hub is, and what it believes it is responsible for."),
    ("12.2", "Connectivity & network health", "Whether each peer is currently reachable, and how long since it was last heard."),
    ("12.3", "System health & diagnostics", "The hub's own liveness and the roll-up health rating of the whole installation."),
    ("12.4", "Firmware, OTA & configuration state", "Software versions across the hub and its peers, and the rules configuration in force."),
    ("12.5", "Leak detection & sensor telemetry", "The leak sensors themselves: identity, wet/dry state and physical location."),
    ("12.6", "Valve control & actuation telemetry", "The valve's position and its leak interlock."),
    ("12.7", "Flow & water-usage metering", "EXPLICITLY ABSENT — this product has no flow sensing. No field in the contract reports volume, flow rate or consumption. Confirmed by exhausting the 91-field census; there is no flow-related code path in main/."),
    ("12.8", "Power, battery & energy", "Battery level for every battery-powered device."),
    ("12.9", "Alarms, faults & error codes", "The event discriminator and the command-failure surface."),
    ("12.10", "Commissioning, lifecycle & user-linkage events", "Why a snapshot was sent, and the 24-hour water-access override window."),
    ("12.11", "Security & audit events", "EXPLICITLY ABSENT — no D2C telemetry field reports an authentication, authorisation or audit event. Security material (the SAS token and the DPS symmetric key) is used for transport authentication only and never appears in a payload; it is described in §3.3 and §18."),
    ("12.12", "Diagnostic / debug-only telemetry", "The fallback escape hatch used when an internally generated payload cannot be re-parsed."),
]

BUILD_GATES_NOTE = """No D2C telemetry field is gated by a compile-time flag. Every `cJSON_Add*` call in `main/`
sits outside any preprocessor conditional — verified across all four JSON-building files, whose only `#if`
is a constant definition (main/rules_engine/rules_engine.c:28-30). The build-gated field count is therefore
zero, and there is no debug-only build that emits extra keys.

One setting does change whether a *value* can be obtained rather than whether a *key* is emitted:
CONFIG_BT_NIMBLE_EXT_ADV selects which advertising-report callback the BLE scanner uses, but both branches
call the same process_leak_adv() and build the same event structure (main/ble_leak_scanner/app_ble_leak.c:236-252
versus :308-327), so the wire shape is identical either way."""
