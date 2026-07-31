# ruff: noqa: E501
"""Appendix B — maximal and minimal payloads for every message type.

NO VALUE HERE IS INVENTED. Each is one of:
  * a string literal from firmware source (e.g. "eflostop.v2");
  * an enum member from a switch in firmware source;
  * a named constant's value (e.g. 86400 from OVERRIDE_WINDOW_DURATION_S);
  * a documented range bound (e.g. 100 for a 0-100 percent battery);
  * the printf FORMAT STRING that generates the value, used verbatim where firmware
    defines no literal at all (e.g. gateway.id shows "GW-%02X%02X%02X%02X%02X%02X").

That last convention is the important one. Rather than fabricating a plausible-looking
gateway id or MAC address, the payload prints the generating format string, cited. These
are not example values and must not be read as sample data — they show what produces the
value. This is stated in the appendix preamble in the rendered document.

MAXIMAL = every key that can possibly appear, present simultaneously, including keys that
are mutually exclusive in practice. Mutually exclusive keys are annotated in
`exclusive_notes` with the condition that governs them.
MINIMAL = only the keys that are always present for that message type.
"""

# Constants and formats used below, each with the citation that justifies it.
SOURCE_VALUES = [
    ("\"eflostop.v2\"", "The telemetry schema literal", "main/telemetry/telemetry_v2.h:14"),
    ("1704067200", "EPOCH_VALID_THRESHOLD_TELEM — the lowest ts the firmware will ever emit; anything below it is suppressed", "main/telemetry/telemetry_v2.c:57"),
    ("\"GW-%02X%02X%02X%02X%02X%02X\"", "The gateway id format string", "main/hub_identity/hub_identity.c:33-34"),
    ("\"%02X%02X\"", "The short id format string (last two MAC bytes)", "main/hub_identity/hub_identity.c:37"),
    ("\"%02X:%02X:%02X:%02X:%02X:%02X\"", "The BLE MAC rendering format", "main/ble_leak_scanner/app_ble_leak.c:88"),
    ("\"0x%08lX\"", "The LoRa sensor id format", "main/health_engine/health_engine.c:241"),
    ("0", "gateway.uptime_s at boot; also last_seen_age_s for a device just heard", "main/telemetry/telemetry_v2.c:85-86"),
    ("100", "Upper bound of the documented 0-100 percent battery range", "main/ble_leak_scanner/app_ble_leak.h:21"),
    ("-128", "Lower bound of int8_t, the rssi type", "main/telemetry/telemetry_v2.h:33, :42"),
    ("-32", "Lower bound of the LoRa snr range, -32.00 to +31.75 in quarter steps", "main/app_lora/lora.cpp:417-422"),
    ("7", "trigger_mask with all three defined bits set: bit0 BLE, bit1 LoRa, bit2 valve flood", "main/provisioning_manager/provisioning_manager.h:22-25"),
    ("16", "MAX_LORA_SENSORS and MAX_BLE_LEAK_SENSORS", "main/provisioning_manager/provisioning_manager.h:13-14"),
    ("86400", "OVERRIDE_WINDOW_DURATION_S — 24 hours", "main/rules_engine/rules_engine.c:29"),
    ("30", "AUTO_CLEAR_TIMEOUT_MS / 1000", "main/rules_engine/rules_engine.c:18"),
    ("\"All devices healthy\"", "The system_health.reason string for the healthy case", "main/telemetry/telemetry_v2.c:162"),
]

PREAMBLE = """Two payloads are given for every message type. The **maximal** payload shows every key that can
possibly appear, present at the same time — including keys that are mutually exclusive in practice, which are
listed with their governing condition beneath each example. The **minimal** payload shows only the keys that are
always present.

No value in these payloads is invented. Each is a string literal from firmware source, an enum member, a named
constant's value, or a documented range bound. Where firmware defines no literal value at all — a gateway id, a
MAC address — the payload prints **the printf format string that generates it**, taken verbatim from source.
So `"GW-%02X%02X%02X%02X%02X%02X"` is not a sample gateway id; it is the format at
`main/hub_identity/hub_identity.c:33-34` that produces every real one. Read these as generating rules, not as
captured traffic.

`ts` is shown as `1704067200` throughout because that is the exact threshold constant below which the firmware
refuses to emit anything (`main/telemetry/telemetry_v2.c:57`) — it is the lowest timestamp that can ever appear
on the wire."""

PAYLOADS = {
    "lifecycle": {
        "enum_overrides": {"data.event": ['online']},
        "maximal": {
            "schema": "eflostop.v2",
            "ts": 1704067200,
            "gateway": {
                "id": "GW-%02X%02X%02X%02X%02X%02X",
                "short_id": "%02X%02X",
                "name": "<hub name, up to 31 chars>",
                "fw": "1.8.0",
                "uptime_s": 0,
            },
            "type": "lifecycle",
            "data": {
                "event": "online",
                "reset_reason": "power_on",
                "provisioned": True,
                "valve_mac": "%02X:%02X:%02X:%02X:%02X:%02X",
                "lora_sensor_count": 16,
                "ble_leak_sensor_count": 16,
                "rules": {"auto_close_enabled": True, "trigger_mask": 7},
            },
        },
        "minimal": {
            "schema": "eflostop.v2",
            "ts": 1704067200,
            "gateway": {"id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X", "fw": "1.8.0", "uptime_s": 0},
            "type": "lifecycle",
            "data": {"event": "online", "reset_reason": "power_on", "provisioned": True,
                     "lora_sensor_count": 16, "ble_leak_sensor_count": 16},
        },
        "exclusive_notes": [
            ("gateway.name", "Omitted when the stored hub name is empty. main/telemetry/telemetry_v2.c:82-83"),
            ("data.valve_mac", "Omitted when no valve MAC is provisioned. main/telemetry/telemetry_v2.c:344-346"),
            ("data.rules", "Omitted, with both children, when provisioning_get_rules_config() returns false. main/telemetry/telemetry_v2.c:359-364"),
            ("data.reset_reason", "Shown as power_on; any of the 7 members in the reset_reason enum may appear. main/telemetry/telemetry_v2.c:128-141"),
        ],
    },
    "snapshot": {
        "maximal": {
            "schema": "eflostop.v2",
            "ts": 1704067200,
            "gateway": {"id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X",
                        "name": "<hub name, up to 31 chars>", "fw": "1.8.0", "uptime_s": 0},
            "type": "snapshot",
            "data": {
                "reason": "heartbeat",
                "system_health": {"rating": "excellent", "reason": "All devices healthy"},
                "valve": {
                    "mac": "%02X:%02X:%02X:%02X:%02X:%02X",
                    "state": "open",
                    "battery": 100,
                    "leak_state": False,
                    "rmleak": False,
                    "connected": True,
                    "fw_version": "<valve DIS firmware revision string, up to 31 chars>",
                    "rating": "excellent",
                    "last_seen_age_s": 0,
                },
                "lora_sensors": [{
                    "sensor_id": "0x%08lX", "connected": True, "rating": "excellent",
                    "last_seen_age_s": 0, "battery": 100, "leak_state": False,
                    "rssi": -128, "snr": -32,
                    "location": {"code": "kitchen", "label": "<label, up to 31 chars>"},
                }],
                "ble_leak_sensors": [{
                    "sensor_id": "%02X:%02X:%02X:%02X:%02X:%02X", "connected": True, "rating": "excellent",
                    "last_seen_age_s": 0, "battery": 100, "leak_state": False, "rssi": -128,
                    "fw_version": "<sensor firmware revision string, up to 11 chars>",
                    "location": {"code": "kitchen", "label": "<label, up to 31 chars>"},
                }],
                "rules": {"auto_close_enabled": True, "trigger_mask": 7},
                "override_active": True,
                "override_remaining_s": 86400,
                "expires_ts": 1704067200,
            },
        },
        "minimal": {
            "schema": "eflostop.v2",
            "ts": 1704067200,
            "gateway": {"id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X", "fw": "1.8.0", "uptime_s": 0},
            "type": "snapshot",
            "data": {
                "reason": "heartbeat",
                "system_health": {"rating": "excellent", "reason": "All devices healthy"},
                "valve": {"state": "disconnected", "connected": False},
                "lora_sensors": [],
                "ble_leak_sensors": [],
                "override_active": False,
            },
        },
        # Only the six snapshot values of data.reason are reachable here; c2d_command belongs
        # to the rules-event vocabulary that shares this key name.
        "enum_overrides": {"data.reason": ["heartbeat", "event", "commission", "boot", "fast", "decommission"]},
        "exclusive_notes": [
            ("valve.mac / battery / leak_state / rmleak / fw_version", "Present only on the connected branch. When the valve is disconnected the object collapses to state:\"disconnected\" and connected:false. main/telemetry/telemetry_v2.c:427-443"),
            ("valve.state", "Carries \"disconnected\" only in the disconnected branch; the maximal example shows the connected value \"open\". These two are mutually exclusive. main/telemetry/telemetry_v2.c:429-430, :442"),
            ("valve.fw_version", "Three-state: a string when the DIS read succeeded, explicit null when it failed, and absent entirely when the valve is disconnected. main/telemetry/telemetry_v2.c:436-444"),
            ("valve.rating / last_seen_age_s", "Present only when a valve health record exists. last_seen_age_s is null when the UINT32_MAX sentinel applies. main/telemetry/telemetry_v2.c:447-455"),
            ("lora_sensors[] and ble_leak_sensors[]", "Always present as arrays. Each element always carries every key shown; unknown values are explicit null EXCEPT leak_state, which is a hard-coded false — see F-01. main/telemetry/telemetry_v2.c:499-508, :553-564"),
            ("lora_sensors[]", "Empty in practice: the LoRa radio is not populated on the current PCBA. The key is still emitted unconditionally. main/telemetry/telemetry_v2.c:515"),
            ("data.rules", "Omitted with both children when the rules config cannot be read. main/telemetry/telemetry_v2.c:578-583"),
            ("override_remaining_s", "Present only when override_active is true AND the value is >= 0. main/telemetry/telemetry_v2.c:595-597"),
            ("expires_ts", "Present only when override_active is true AND the stored expiry is non-zero. Both this and override_remaining_s are omitted when the clock is unsynced. main/telemetry/telemetry_v2.c:598-599; main/rules_engine/rules_engine.c:1195-1198"),
            ("data.reason", "Shown as heartbeat. Only SIX of the key's seven members are reachable in a snapshot — heartbeat, event, commission, boot, fast and decommission. The seventh, c2d_command, occurs only on the auto_close_reenabled rules event, because this key carries two disjoint vocabularies. main/iothub/app_iothub.c:581-591, :1570"),
        ],
    },
    "valve_event": {
        "enum_overrides": {"data.event": ['valve_state_changed', 'valve_flood_detected', 'valve_flood_cleared']},
        "maximal": {
            "schema": "eflostop.v2", "ts": 1704067200,
            "gateway": {"id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X",
                        "name": "<hub name, up to 31 chars>", "fw": "1.8.0", "uptime_s": 0},
            "type": "event",
            "data": {"event": "valve_state_changed", "valve_state": "open", "battery": 100,
                     "leak_state": False, "rmleak": False, "fw_version": "<valve DIS firmware revision string, up to 31 chars>"},
        },
        "minimal": {
            "schema": "eflostop.v2", "ts": 1704067200,
            "gateway": {"id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X", "fw": "1.8.0", "uptime_s": 0},
            "type": "event",
            "data": {"event": "valve_state_changed", "valve_state": "open", "battery": 100,
                     "leak_state": False, "rmleak": False},
        },
        "exclusive_notes": [
            ("data.event", "One of valve_state_changed, valve_flood_detected or valve_flood_cleared. main/iothub/app_iothub.c:1777-1787"),
            ("data.valve_state", "Three members only — open, closed, unknown. Unlike the snapshot's valve.state it can never be \"disconnected\", because a valve event cannot be produced while the valve is disconnected (F-04). main/telemetry/telemetry_v2.c:617-618"),
            ("data.fw_version", "Omitted when the DIS read failed. This path has no null variant, unlike the snapshot's valve.fw_version. main/telemetry/telemetry_v2.c:623-625"),
            ("No valve identifier", "A valve event carries no MAC or id at all; it is implicitly about the single valve this hub owns. main/telemetry/telemetry_v2.c:608-629"),
        ],
    },
    "leak_event": {
        "enum_overrides": {"data.event": ['leak_detected', 'leak_cleared']},
        "maximal": {
            "schema": "eflostop.v2", "ts": 1704067200,
            "gateway": {"id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X",
                        "name": "<hub name, up to 31 chars>", "fw": "1.8.0", "uptime_s": 0},
            "type": "event",
            "data": {"event": "leak_detected", "source_type": "ble_leak_sensor",
                     "sensor_id": "%02X:%02X:%02X:%02X:%02X:%02X", "leak_state": True,
                     "battery": 100, "rssi": -128,
                     "location": {"code": "kitchen", "label": "<label, up to 31 chars>"}},
        },
        "minimal": {
            "schema": "eflostop.v2", "ts": 1704067200,
            "gateway": {"id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X", "fw": "1.8.0", "uptime_s": 0},
            "type": "event",
            "data": {"event": "leak_detected", "source_type": "ble_leak_sensor",
                     "sensor_id": "%02X:%02X:%02X:%02X:%02X:%02X", "leak_state": True,
                     "battery": 100, "rssi": -128,
                     "location": {"code": "unknown", "label": ""}},
        },
        "exclusive_notes": [
            ("data.event", "leak_detected or leak_cleared. main/iothub/app_iothub.c:1748, :1807"),
            ("data.source_type", "lora or ble_leak_sensor on this path. Passed as a string literal, not via source_to_str. main/iothub/app_iothub.c:1749, :1809"),
            ("data.sensor_id", "Format depends on source: the BLE MAC rendering shown here, or \"0x%08lX\" for a LoRa sensor. main/ble_leak_scanner/app_ble_leak.c:88; main/iothub/app_iothub.c:1745"),
            ("data.location", "Always present on this path, falling back to code:\"unknown\" and an empty label when no metadata is stored — hence the minimal example. main/telemetry/telemetry_v2.c:143-153"),
            ("No snr", "The leak-event shape has no snr key even for a LoRa source, unlike the snapshot's lora_sensors[] element. main/telemetry/telemetry_v2.c:640-651"),
        ],
    },
    "rules_event": {
        "maximal": {
            "schema": "eflostop.v2", "ts": 1704067200,
            "gateway": {"id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X",
                        "name": "<hub name, up to 31 chars>", "fw": "1.8.0", "uptime_s": 0},
            "type": "event",
            "data": {
                "event": "auto_close", "source_type": "ble_leak_sensor",
                "sensor_id": "%02X:%02X:%02X:%02X:%02X:%02X", "rmleak_asserted": True,
                "location": {"code": "kitchen", "label": "<label, up to 31 chars>"},
                "active_leak_count": 1, "override_remaining_s": 86400, "trigger": "button",
                "expires_ts": 1704067200, "remaining_s": 86400, "override_cancelled": True,
                "previous_remaining_s": 86400, "reason": "c2d_command",
                "auto_close_resumed": True, "clear_after_seconds": 30,
                "raw": "<the unparsed engine payload, verbatim>",
            },
        },
        "minimal": {
            "schema": "eflostop.v2", "ts": 1704067200,
            "gateway": {"id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X", "fw": "1.8.0", "uptime_s": 0},
            "type": "event",
            "data": {"event": "rmleak_auto_cleared", "clear_after_seconds": 30},
        },
        # This family is a union of seven shapes, so "always present" is the INTERSECTION across
        # all seven, not the key set of the smallest one. Only data.event is common to all; a
        # water_access_override_enabled message carries no clear_after_seconds and must still
        # validate. Without this the generated schema would reject six of the seven shapes.
        "data_required": ["event"],
        # data.reason carries two disjoint vocabularies on one key. Only the rules meaning is
        # reachable here; the six snapshot values are not.
        "enum_overrides": {"data.reason": ["c2d_command"], "data.event": ['auto_close', 'auto_close_blocked_override', 'auto_close_reenabled', 'rmleak_cleared', 'rmleak_auto_cleared', 'water_access_override_enabled', 'water_access_override_expired', 'rules_engine']},
        "exclusive_notes": [
            ("This maximal payload is a union across seven event shapes", "No single rules event carries all of these keys. The union is shown because the requirement is to display every key that can appear on this message type. The seven shapes are listed below."),
            ("auto_close (sensor-triggered)", "event, source_type, sensor_id, rmleak_asserted, and location only when metadata exists and the source is not valve_flood. main/rules_engine/rules_engine.c:328-357"),
            ("auto_close (valve-reconnect)", "event, source_type:\"reconnect\", sensor_id, rmleak_asserted, active_leak_count. Never carries location. main/rules_engine/rules_engine.c:932-943"),
            ("auto_close_blocked_override", "event, source_type, sensor_id, and override_remaining_s only when it is >= 0. main/rules_engine/rules_engine.c:467-478"),
            ("water_access_override_enabled", "event, trigger, expires_ts, remaining_s. main/rules_engine/rules_engine.c:213-222"),
            ("water_access_override_expired", "event, auto_close_resumed, active_leak_count. main/rules_engine/rules_engine.c:1023-1032"),
            ("rmleak_cleared", "event, and override_cancelled only when a window was open. main/rules_engine/rules_engine.c:676-685"),
            ("auto_close_reenabled", "event, previous_remaining_s, reason. main/rules_engine/rules_engine.c:718-726"),
            ("rmleak_auto_cleared", "event, clear_after_seconds. This is the smallest rules event and is used as the minimal example. main/rules_engine/rules_engine.c:1080-1087"),
            ("data.rmleak_asserted", "Hard-coded true at both emit sites; false is unreachable. main/rules_engine/rules_engine.c:336, :938"),
            ("data.remaining_s", "Always the OVERRIDE_WINDOW_DURATION_S constant, 86400. main/rules_engine/rules_engine.c:218, :29"),
            ("data.raw", "MUTUALLY EXCLUSIVE with every other data key. It appears only when the rules engine's own JSON could not be re-parsed, in which case data.event is the fallback literal \"rules_engine\" and the real payload is carried as an opaque string instead. main/telemetry/telemetry_v2.c:667-670"),
        ],
    },
    "health_event": {
        "enum_overrides": {"data.event": ['device_offline', 'device_recovered', 'health_engine']},
        "maximal": {
            "schema": "eflostop.v2", "ts": 1704067200,
            "gateway": {"id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X",
                        "name": "<hub name, up to 31 chars>", "fw": "1.8.0", "uptime_s": 0},
            "type": "event",
            "data": {"category": "health", "event": "device_offline", "dev_type": "ble_leak_sensor",
                     "sensor_id": "%02X:%02X:%02X:%02X:%02X:%02X", "rating": "critical",
                     "prev_rating": "excellent", "battery": 100, "rssi": -128,
                     "offline_duration_s": 600,
                     "raw": "<the unparsed engine payload, verbatim>"},
        },
        "minimal": {
            "schema": "eflostop.v2", "ts": 1704067200,
            "gateway": {"id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X", "fw": "1.8.0", "uptime_s": 0},
            "type": "event",
            "data": {"category": "health", "event": "device_offline", "dev_type": "valve",
                     "sensor_id": "%02X:%02X:%02X:%02X:%02X:%02X", "rating": "critical",
                     "prev_rating": "excellent"},
        },
        "exclusive_notes": [
            ("data.category", "Always the literal \"health\". This key is how a consumer recognises a health event. main/health_engine/health_engine.c:584"),
            ("data.battery", "Omitted when the cached value is the 0xFF sentinel. main/health_engine/health_engine.c:595-597"),
            ("data.rssi", "Omitted when the cached value is 0. The valve never records an RSSI at all, so this key is always absent for a valve health event. main/health_engine/health_engine.c:598-600; writers only at :249, :267"),
            ("data.offline_duration_s", "Omitted when the value is 0. Shown here as 600, the 10-minute sensor timeout after which a device is judged offline. main/health_engine/health_engine.c:601-603; timeout main/health_engine/health_engine.h:16-17"),
            ("data.dev_type", "Was \"ble_leak\" at git HEAD and is \"ble_leak_sensor\" in the working tree. Both spellings exist in the fleet. main/health_engine/health_engine.c:77"),
            ("data.raw", "MUTUALLY EXCLUSIVE with every other data key. It appears only when the health engine's own JSON could not be re-parsed, in which case data.event is the fallback literal \"health_engine\" and the real payload is carried as an opaque string instead. Shown in the maximal payload because the requirement is to display every key that can appear. main/telemetry/telemetry_v2.c:686-689"),
        ],
    },
    "cmd_ack": {
        "enum_overrides": {"data.event": ['cmd_ack']},
        "maximal": {
            "schema": "eflostop.v2", "ts": 1704067200,
            "gateway": {"id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X",
                        "name": "<hub name, up to 31 chars>", "fw": "1.8.0", "uptime_s": 0},
            "type": "event",
            "data": {"event": "cmd_ack", "id": "<correlation id, up to 63 chars>",
                     "cmd": "valve_open", "status": "error",
                     "error": {"code": "valve_open",
                               "detail": "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."}},
        },
        "minimal": {
            "schema": "eflostop.v2", "ts": 1704067200,
            "gateway": {"id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X", "fw": "1.8.0", "uptime_s": 0},
            "type": "event",
            "data": {"event": "cmd_ack", "cmd": "valve_open", "status": "ok"},
        },
        "exclusive_notes": [
            ("data.id", "OMITTED, not an empty string, when the inbound command carried no correlation id. main/telemetry/telemetry_v2.c:705-706"),
            ("data.error", "Present with both children only when status is \"error\" and an error message was supplied. status:\"ok\" and data.error are therefore mutually exclusive — the maximal example shows the error case. main/telemetry/telemetry_v2.c:709-713"),
            ("data.error.code", "Byte-identical to data.cmd — the same C variable is written to both. It is not a machine-readable failure class. See F-06. main/telemetry/telemetry_v2.c:707, :711"),
            ("data.cmd", "Any of the 11 command names, or an unrecognised inbound string. main/commands/c2d_commands.h:35-45"),
        ],
    },
    "twin_reported": {
        "plane_note": "DEVICE TWIN — NOT D2C TELEMETRY. Flat keys, no envelope, no ts. Not subject to the pre-clock-sync suppression that applies to telemetry.",
        "maximal": {
            "fw_version": "1.8.0", "gateway_id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X",
            "hub_name": "<hub name, up to 31 chars>", "provisioned": True,
            "valve_mac": "%02X:%02X:%02X:%02X:%02X:%02X",
            "lora_sensor_count": 16, "ble_leak_sensor_count": 16,
            "auto_close_enabled": True, "trigger_mask": 7, "uptime_s": 0, "free_heap": 0,
        },
        "minimal": {
            "fw_version": "1.8.0", "gateway_id": "GW-%02X%02X%02X%02X%02X%02X", "short_id": "%02X%02X",
            "hub_name": "", "provisioned": True,
            "lora_sensor_count": 16, "ble_leak_sensor_count": 16, "uptime_s": 0, "free_heap": 0,
        },
        "exclusive_notes": [
            ("hub_name", "ALWAYS emitted, as an empty string when unset — unlike telemetry's gateway.name, which is omitted. main/iothub/app_iothub.c:956"),
            ("valve_mac", "Omitted when no valve is provisioned. main/iothub/app_iothub.c:961"),
            ("auto_close_enabled and trigger_mask", "Both omitted when the rules config cannot be read. main/iothub/app_iothub.c:975-976"),
            ("free_heap", "Shown as 0 because no source literal exists; the real value is esp_get_free_heap_size() in bytes. This is the only hub diagnostic that exists on the twin and nowhere in telemetry. main/iothub/app_iothub.c:981"),
        ],
    },
    "twin_desired": {
        "plane_note": "DEVICE TWIN DESIRED — INBOUND, cloud to device. Shown for completeness of the cloud interface.",
        "maximal": {"hub_name": "<hub name, up to 31 chars>", "snapshot_interval_s": 3600},
        "minimal": {},
        "exclusive_notes": [
            ("hub_name", "Max 31 characters. Also settable via the set_hub_name command — see §13.3. main/hub_identity/hub_identity.h:10"),
            ("snapshot_interval_s", "Clamped to 60..3600; 3600 shown is the documented upper bound. RAM only, so it reverts to 300 on reboot, and it is never echoed into twin reported. main/iothub/app_iothub.c:1022"),
            ("minimal is empty", "Both keys are optional; a desired patch may contain either, both or neither."),
        ],
    },
    "c2d_command": {
        "plane_note": "C2D COMMAND — INBOUND, cloud to device. Shown for completeness of the cloud interface.",
        "maximal": {
            "schema": "eflostop.cmd", "ver": 1, "id": "<correlation id, up to 63 chars>",
            "cmd": "decommission", "payload": {"target": "lora", "sensor_id": "0x%08lX"},
        },
        "minimal": {"cmd": "valve_open"},
        "exclusive_notes": [
            ("schema", "The canonical envelope literal. A legacy envelope uses \"eflostop.cmd.v1\", and a legacy plain-text form is also accepted. main/commands/c2d_commands.h:13-17"),
            ("payload", "Shape depends entirely on cmd. The decommission shape is shown; see §14 for the payload of each of the 11 commands. main/iothub/app_iothub.c:657-935"),
            ("id", "Optional. If absent, the resulting cmd_ack omits data.id entirely. main/telemetry/telemetry_v2.c:705-706"),
        ],
    },
    "dps_registration": {
        "plane_note": "DPS PROVISIONING — a different broker and topic namespace, used once before the hub connection exists. Not part of your telemetry ingest.",
        "maximal": {"registrationId": "GW-%02X%02X%02X%02X%02X%02X"},
        "minimal": {"registrationId": "GW-%02X%02X%02X%02X%02X%02X"},
        "exclusive_notes": [
            ("registrationId", "The only key. Hand-built with snprintf rather than cJSON, and the only non-cJSON JSON that leaves the device. main/dps_client/dps_client.c:283-284"),
            ("No tags or model id", "The payload carries nothing else, which means DPS enrolment tags cannot originate in firmware. main/dps_client/dps_client.c:283-284"),
        ],
    },
}
