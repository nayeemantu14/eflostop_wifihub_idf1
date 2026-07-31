# ruff: noqa: E501
"""Every real telemetry message the hub can publish, as concrete examples.

DIFFERENT FROM APPENDIX B OF THE CATALOGUE. Appendix B prints *maximal* payloads — every key
that could ever appear on a message type, shown together, including keys that are mutually
exclusive. Those are deliberately NOT real messages.

Everything here IS a real message: the exact key set, in the exact emission order the
serializer produces, for one concrete situation. Any of these could be copied off the wire.

Example values are realistic and internally consistent across the whole document — the same
hub, the same valve, the same two sensors throughout — so a reader can follow one installation
across every message type. They are examples, not firmware literals; enum values, constants and
key sets are exact.
"""

# One installation, used consistently everywhere.
GW_ID = "GW-A0B7651C2D3E"          # from Wi-Fi STA MAC A0:B7:65:1C:2D:3E
GW_SHORT = "2D3E"                  # last two MAC bytes
HUB_NAME = "Main House"
FW = "1.8.0"
VALVE_MAC = "C4:19:D1:88:2A:7F"
VALVE_FW = "2.2.0"
SENSOR_A = "00:80:E1:27:9A:E6"     # BLE leak sensor, under the kitchen sink
SENSOR_B = "00:80:E1:27:A1:04"     # BLE leak sensor, laundry
SENSOR_FW = "1.1.0"
LORA_ID = "0x1A2B3C4D"
TS = 1785398400                    # 2026-07-31T12:00:00Z


def env(ts, uptime, type_, data, name=True):
    """Envelope in emission order: schema, ts, gateway, type, data."""
    gw = {"id": GW_ID, "short_id": GW_SHORT}
    if name:
        gw["name"] = HUB_NAME
    gw["fw"] = FW
    gw["uptime_s"] = uptime
    return {"schema": "eflostop.v2", "ts": ts, "gateway": gw, "type": type_, "data": data}


def ble_sensor(mac, label, code, connected=True, battery=87, rssi=-64, leak=False,
               age=12, fw=SENSOR_FW):
    """Snapshot element, BLE leak sensor. Emission order is fixed."""
    s = {"sensor_id": mac, "connected": connected, "rating": "excellent" if connected else "critical",
         "last_seen_age_s": age}
    if connected:
        s["battery"] = battery
        s["leak_state"] = leak
        s["rssi"] = rssi
        s["fw_version"] = fw
    else:
        # Cache merge skipped: three nulls and a hard-coded false. See F-01.
        s["battery"] = None
        s["leak_state"] = False
        s["rssi"] = None
        s["fw_version"] = None
    s["location"] = {"code": code, "label": label}
    return s


VALVE_OK = {"mac": VALVE_MAC, "state": "open", "battery": 92, "leak_state": False,
            "rmleak": False, "connected": True, "fw_version": VALVE_FW,
            "rating": "excellent", "last_seen_age_s": 4}
VALVE_CLOSED = dict(VALVE_OK, state="closed", rmleak=True)
VALVE_GONE = {"mac": VALVE_MAC, "state": "disconnected", "connected": False,
              "rating": "critical", "last_seen_age_s": 245}
HEALTH_OK = {"rating": "excellent", "reason": "All devices healthy"}
RULES = {"auto_close_enabled": True, "trigger_mask": 7}

S_A = ble_sensor(SENSOR_A, "Under sink", "kitchen")
S_B = ble_sensor(SENSOR_B, "Behind machine", "laundry", battery=64, rssi=-78, age=31)


def snap(reason, valve, sensors, override=None, rules=True, health=None):
    d = {"reason": reason, "system_health": health or HEALTH_OK, "valve": valve,
         "lora_sensors": [], "ble_leak_sensors": sensors}
    if rules:
        d["rules"] = RULES
    d["override_active"] = bool(override)
    if override:
        d["override_remaining_s"] = override[0]
        d["expires_ts"] = override[1]
    return d


# --------------------------------------------------------------------------------------
# The messages.  `group` orders the document; `when` says what causes this exact message.
# --------------------------------------------------------------------------------------

MESSAGES = [
    # ---------------- lifecycle ----------------
    dict(group="Lifecycle", id="L1", title="Hub connected, fully commissioned",
         when="Sent on every MQTT connect once the hub is provisioned — including reconnects after a network drop, so this is not once per boot.",
         cite="main/telemetry/telemetry_v2.c:334-368; trigger main/iothub/app_iothub.c:1694",
         msg=env(TS, 42, "lifecycle", {
             "event": "online", "reset_reason": "power_on", "provisioned": True,
             "valve_mac": VALVE_MAC, "lora_sensor_count": 0, "ble_leak_sensor_count": 2,
             "rules": RULES})),
    dict(group="Lifecycle", id="L2", title="Hub connected, nothing commissioned yet",
         when="Same trigger, but before any provisioning. The hub name is unset so gateway.name is omitted; no valve is stored so data.valve_mac is omitted; the rules object is omitted with both its children.",
         cite="omissions main/telemetry/telemetry_v2.c:82-83, :344-346, :359-364",
         msg=env(TS, 18, "lifecycle", {
             "event": "online", "reset_reason": "power_on", "provisioned": True,
             "lora_sensor_count": 0, "ble_leak_sensor_count": 0}, name=False)),
    dict(group="Lifecycle", id="L3", title="Hub reconnected after a crash",
         when="Identical shape to L1; only data.reset_reason differs. The seven possible values are power_on, software, panic, watchdog, brownout, deep_sleep and unknown.",
         cite="mapping main/telemetry/telemetry_v2.c:128-141",
         msg=env(TS, 7, "lifecycle", {
             "event": "online", "reset_reason": "panic", "provisioned": True,
             "valve_mac": VALVE_MAC, "lora_sensor_count": 0, "ble_leak_sensor_count": 2,
             "rules": RULES})),

    # ---------------- snapshot ----------------
    dict(group="Snapshot", id="S1", title="Routine heartbeat, everything healthy",
         when="The 300 s heartbeat. This is the most common message the hub sends.",
         cite="main/telemetry/telemetry_v2.c:372-604; interval main/telemetry/telemetry_v2.h:15",
         msg=env(TS, 3600, "snapshot", snap("heartbeat", VALVE_OK, [S_A, S_B]))),
    dict(group="Snapshot", id="S2", title="Heartbeat with the valve disconnected",
         when="The BLE link to the valve is down. The valve object collapses: battery, leak_state, rmleak and fw_version are all omitted, and state carries \"disconnected\" instead of a position.",
         cite="disconnected branch main/telemetry/telemetry_v2.c:442-455",
         msg=env(TS, 5400, "snapshot",
                 snap("heartbeat", VALVE_GONE, [S_A, S_B],
                      health={"rating": "critical", "reason": "Valve offline"}))),
    dict(group="Snapshot", id="S3", title="Heartbeat with a sensor gone silent — read this one carefully",
         when="Sensor B has not been heard for over 10 minutes, so the health engine marks it disconnected and the cache merge is skipped. battery, rssi and fw_version go null, but leak_state is written as a hard-coded false. If that sensor was wet when it went silent, this message says it is dry and the earlier leak_detected event is never retracted. last_seen_age_s keeps its real value and is the only field that contradicts the false.",
         cite="placeholder main/telemetry/telemetry_v2.c:562; merge gate :541-552; F-01",
         msg=env(TS, 9000, "snapshot",
                 snap("heartbeat", VALVE_OK,
                      [S_A, ble_sensor(SENSOR_B, "Behind machine", "laundry",
                                       connected=False, age=734)],
                      health={"rating": "critical", "reason": "1 sensor offline"}))),
    dict(group="Snapshot", id="S4", title="Snapshot while a water-access override is running",
         when="A 24-hour override window is open, so three extra keys appear. override_remaining_s and expires_ts are both omitted when no window is active.",
         cite="main/telemetry/telemetry_v2.c:594-599; window constant main/rules_engine/rules_engine.c:29",
         msg=env(TS, 12000, "snapshot",
                 snap("event", VALVE_OK, [S_A, S_B], override=(82740, TS + 82740)))),
    dict(group="Snapshot", id="S5", title="First snapshot after boot",
         when="Identical shape to S1; only data.reason differs. The seven values are heartbeat, event, commission, boot, fast, decommission and — on the auto_close_reenabled rules event only — c2d_command.",
         cite="snap_reason_str main/iothub/app_iothub.c:581-591; decommission literal :1570",
         msg=env(TS, 12, "snapshot", snap("boot", VALVE_OK, [S_A, S_B]))),
    dict(group="Snapshot", id="S6", title="Early snapshot as soon as the valve is ready",
         when="reason \"fast\". Sent before the picture is complete by design, so sensors may not have checked in yet and the arrays can be short.",
         cite="main/iothub/app_iothub.c:587",
         msg=env(TS, 25, "snapshot", snap("fast", VALVE_OK, []))),
    dict(group="Snapshot", id="S7", title="Final snapshot before decommissioning",
         when="Published out of band immediately before the hub restarts and forgets its commissioning. This is the last message from this identity.",
         cite="main/iothub/app_iothub.c:1570",
         msg=env(TS, 60000, "snapshot", snap("decommission", VALVE_OK, [S_A, S_B]))),

    # ---------------- valve events ----------------
    dict(group="Valve events", id="V1", title="Valve opened",
         when="A GATT notification whose value differs from the last one seen. Delta-gated, so an unchanged notification produces nothing.",
         cite="main/telemetry/telemetry_v2.c:608-629; gate main/iothub/app_iothub.c:1782-1791",
         msg=env(TS, 4000, "event", {"event": "valve_state_changed", "valve_state": "open",
                                     "battery": 92, "leak_state": False, "rmleak": False,
                                     "fw_version": VALVE_FW})),
    dict(group="Valve events", id="V2", title="Valve closed and locked after a leak",
         when="The same shape as V1. rmleak true means the valve is latched and will refuse to open until the interlock is cleared.",
         cite="main/telemetry/telemetry_v2.c:608-629",
         msg=env(TS, 4020, "event", {"event": "valve_state_changed", "valve_state": "closed",
                                     "battery": 92, "leak_state": False, "rmleak": True,
                                     "fw_version": VALVE_FW})),
    dict(group="Valve events", id="V3", title="The valve's own flood probe went wet",
         when="The valve has a probe of its own, separate from the leak sensors. Note this event carries no identifier — it is implicitly about the single valve this hub owns.",
         cite="main/iothub/app_iothub.c:1778-1779",
         msg=env(TS, 4100, "event", {"event": "valve_flood_detected", "valve_state": "open",
                                     "battery": 92, "leak_state": True, "rmleak": False,
                                     "fw_version": VALVE_FW})),
    dict(group="Valve events", id="V4", title="The valve's own flood probe went dry",
         when="The clearing counterpart of V3.",
         cite="main/iothub/app_iothub.c:1778-1779",
         msg=env(TS, 4600, "event", {"event": "valve_flood_cleared", "valve_state": "closed",
                                     "battery": 92, "leak_state": False, "rmleak": True,
                                     "fw_version": VALVE_FW})),
    dict(group="Valve events", id="V5", title="Valve opened, firmware revision unknown",
         when="Identical to V1 except data.fw_version is omitted, which happens when the DIS read failed at setup. There is NO null variant on this path — the key is simply absent.",
         cite="main/telemetry/telemetry_v2.c:623-625",
         msg=env(TS, 4700, "event", {"event": "valve_state_changed", "valve_state": "open",
                                     "battery": 92, "leak_state": False, "rmleak": False})),

    # ---------------- leak events ----------------
    dict(group="Leak events", id="K1", title="A leak sensor went wet",
         when="The safety-critical message. Emitted when a BLE advertisement reports a leak state different from the cached one. Note the sensor's fields are FLAT on data here, whereas a snapshot nests the same sensor inside ble_leak_sensors[].",
         cite="main/telemetry/telemetry_v2.c:631-654; trigger main/iothub/app_iothub.c:1807-1811",
         msg=env(TS, 8000, "event", {"event": "leak_detected", "source_type": "ble_leak_sensor",
                                     "sensor_id": SENSOR_A, "leak_state": True, "battery": 87,
                                     "rssi": -64,
                                     "location": {"code": "kitchen", "label": "Under sink"}})),
    dict(group="Leak events", id="K2", title="A leak sensor went dry",
         when="The clearing counterpart of K1.",
         cite="main/iothub/app_iothub.c:1807-1811",
         msg=env(TS, 8600, "event", {"event": "leak_cleared", "source_type": "ble_leak_sensor",
                                     "sensor_id": SENSOR_A, "leak_state": False, "battery": 87,
                                     "rssi": -66,
                                     "location": {"code": "kitchen", "label": "Under sink"}})),
    dict(group="Leak events", id="K3", title="A leak sensor with no location assigned",
         when="Identical to K1 except the location object falls back. The key is always present on this path; code becomes \"unknown\" and label an empty string.",
         cite="fallback main/telemetry/telemetry_v2.c:143-153",
         msg=env(TS, 8100, "event", {"event": "leak_detected", "source_type": "ble_leak_sensor",
                                     "sensor_id": SENSOR_B, "leak_state": True, "battery": 64,
                                     "rssi": -78,
                                     "location": {"code": "unknown", "label": ""}})),
    dict(group="Leak events", id="K4", title="A LoRa sensor went wet",
         when="Same shape as K1 with source_type \"lora\" and a hex sensor id. NOT SEEN IN PRACTICE: the LoRa radio is not populated on the current PCBA, so no LoRa sensor can report. Included because the firmware path exists.",
         cite="main/iothub/app_iothub.c:1745-1750",
         msg=env(TS, 8200, "event", {"event": "leak_detected", "source_type": "lora",
                                     "sensor_id": LORA_ID, "leak_state": True, "battery": 78,
                                     "rssi": -95,
                                     "location": {"code": "basement", "label": "Sump"}})),

    # ---------------- rules events ----------------
    dict(group="Rules events", id="R1", title="Hub closed the valve because a sensor reported a leak",
         when="The automatic shut-off. rmleak_asserted is always true here — it is a presence marker, not a variable.",
         cite="main/rules_engine/rules_engine.c:328-357",
         msg=env(TS, 8005, "event", {"event": "auto_close", "source_type": "ble_leak_sensor",
                                     "sensor_id": SENSOR_A, "rmleak_asserted": True,
                                     "location": {"code": "kitchen", "label": "Under sink"}})),
    dict(group="Rules events", id="R2", title="Hub closed the valve because its own flood probe went wet",
         when="Same shape as R1 but with source_type \"valve_flood\", sensor_id \"valve\", and NO location object — location is omitted entirely for the valve-flood source.",
         cite="main/rules_engine/rules_engine.c:338-349; pseudo-id main/iothub/app_iothub.c:1650",
         msg=env(TS, 4105, "event", {"event": "auto_close", "source_type": "valve_flood",
                                     "sensor_id": "valve", "rmleak_asserted": True})),
    dict(group="Rules events", id="R3", title="Hub closed the valve on reconnect, finding a leak still active",
         when="A structurally DIFFERENT auto_close. It carries active_leak_count and no location, and source_type is \"reconnect\", which is not a member of the leak-source enum. Nothing in the firmware reconciles the two shapes.",
         cite="main/rules_engine/rules_engine.c:932-943",
         msg=env(TS, 400, "event", {"event": "auto_close", "source_type": "reconnect",
                                    "sensor_id": SENSOR_A, "rmleak_asserted": True,
                                    "active_leak_count": 1})),
    dict(group="Rules events", id="R4", title="A leak occurred but auto-close was suppressed",
         when="A user override window is open, so the hub deliberately did not close the valve. override_remaining_s is omitted if the remaining time is negative.",
         cite="main/rules_engine/rules_engine.c:467-478",
         msg=env(TS, 20000, "event", {"event": "auto_close_blocked_override",
                                      "source_type": "ble_leak_sensor", "sensor_id": SENSOR_A,
                                      "override_remaining_s": 61200})),
    dict(group="Rules events", id="R5", title="A 24-hour water-access override started",
         when="Started either by a long press on the valve (trigger \"button\") or by the override_enable command (trigger \"c2d_command\"). remaining_s is always the full window constant, 86400.",
         cite="main/rules_engine/rules_engine.c:213-222; constant :29",
         msg=env(TS, 19000, "event", {"event": "water_access_override_enabled",
                                      "trigger": "button", "expires_ts": TS + 86400,
                                      "remaining_s": 86400})),
    dict(group="Rules events", id="R6", title="The override window elapsed",
         when="auto_close_resumed says whether leaks were still active when the window ended — if so the hub immediately re-closes the valve.",
         cite="main/rules_engine/rules_engine.c:1023-1032",
         msg=env(TS + 86400, 105400, "event", {"event": "water_access_override_expired",
                                               "auto_close_resumed": True,
                                               "active_leak_count": 1})),
    dict(group="Rules events", id="R7", title="The override was cancelled by command",
         when="Sent in response to override_cancel. reason is always \"c2d_command\" here — note this key carries a completely different vocabulary from data.reason on a snapshot.",
         cite="main/rules_engine/rules_engine.c:718-726",
         msg=env(TS, 25000, "event", {"event": "auto_close_reenabled",
                                      "previous_remaining_s": 61200,
                                      "reason": "c2d_command"})),
    dict(group="Rules events", id="R8", title="The leak interlock was cleared",
         when="Sent in response to leak_reset. override_cancelled appears only if an override window was open at the time; otherwise the key is absent and this message is just the event name.",
         cite="main/rules_engine/rules_engine.c:676-685",
         msg=env(TS, 26000, "event", {"event": "rmleak_cleared", "override_cancelled": True})),
    dict(group="Rules events", id="R9", title="The leak interlock cleared itself",
         when="Every leak source has been dry for 30 seconds, so the hub released the interlock on its own. This clears the latch but does NOT re-open the valve.",
         cite="main/rules_engine/rules_engine.c:1080-1087; constant :18",
         msg=env(TS, 8700, "event", {"event": "rmleak_auto_cleared", "clear_after_seconds": 30})),

    # ---------------- health events ----------------
    dict(group="Health events", id="H1", title="A device stopped responding",
         when="A peer crossed into the critical health rating. These are the only events carrying data.category \"health\" — that key is how you recognise them. Critical is only ever reached for connectivity reasons; a low battery gives warning, never critical.",
         cite="main/health_engine/health_engine.c:577-608; rating :118-124",
         msg=env(TS, 9100, "event", {"category": "health", "event": "device_offline",
                                     "dev_type": "ble_leak_sensor", "sensor_id": SENSOR_B,
                                     "rating": "critical", "prev_rating": "excellent",
                                     "battery": 64, "rssi": -78, "offline_duration_s": 600})),
    dict(group="Health events", id="H2", title="A device came back",
         when="The recovery counterpart of H1.",
         cite="main/health_engine/health_engine.c:586-588",
         msg=env(TS, 9700, "event", {"category": "health", "event": "device_recovered",
                                     "dev_type": "ble_leak_sensor", "sensor_id": SENSOR_B,
                                     "rating": "excellent", "prev_rating": "critical",
                                     "battery": 64, "rssi": -75})),
    dict(group="Health events", id="H3", title="The valve stopped responding",
         when="Same shape with dev_type \"valve\". data.rssi is ALWAYS absent for the valve — the hub never records an RSSI for it — and offline_duration_s is omitted when it would be zero.",
         cite="rssi writers only at main/health_engine/health_engine.c:249, :267; omit-on-zero :598-603",
         msg=env(TS, 5500, "event", {"category": "health", "event": "device_offline",
                                     "dev_type": "valve", "sensor_id": VALVE_MAC,
                                     "rating": "critical", "prev_rating": "excellent",
                                     "battery": 92, "offline_duration_s": 180})),

    # ---------------- command acks ----------------
    dict(group="Command acknowledgements", id="C1", title="A command succeeded",
         when="Every enveloped command, or any command carrying a correlation id, produces exactly one of these. data.id echoes the id you sent.",
         cite="main/telemetry/telemetry_v2.c:695-718; gate main/iothub/app_iothub.c:932-935",
         msg=env(TS, 30000, "event", {"event": "cmd_ack", "id": "req-8f21",
                                      "cmd": "valve_close", "status": "ok"})),
    dict(group="Command acknowledgements", id="C2", title="A command succeeded, no correlation id supplied",
         when="If the inbound command carried no id, data.id is OMITTED — it is not an empty string. You can then only match the ack by cmd and timing.",
         cite="main/telemetry/telemetry_v2.c:705-706",
         msg=env(TS, 30100, "event", {"event": "cmd_ack", "cmd": "valve_open", "status": "ok"})),
    dict(group="Command acknowledgements", id="C3", title="A command was refused",
         when="On failure an error object appears. Note error.code is NOT an error code — it is the command name repeated. The only real discriminator is the free-text detail.",
         cite="main/telemetry/telemetry_v2.c:707-713; message main/iothub/app_iothub.c:539-540",
         msg=env(TS, 30200, "event", {
             "event": "cmd_ack", "id": "req-8f22", "cmd": "valve_open", "status": "error",
             "error": {"code": "valve_open",
                       "detail": "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."}})),
    dict(group="Command acknowledgements", id="C4", title="An unrecognised command",
         when="Any command name the hub does not know produces this. The detail is a fixed string.",
         cite="main/iothub/app_iothub.c:928-935",
         msg=env(TS, 30300, "event", {
             "event": "cmd_ack", "id": "req-8f23", "cmd": "reboot_hub", "status": "error",
             "error": {"code": "reboot_hub", "detail": "unknown command"}})),

    # ---------------- fallback ----------------
    dict(group="Fallback messages", id="F1", title="A rules payload could not be re-parsed",
         when="A defensive path. If the rules engine's own JSON fails to re-parse, the hub sends the raw string instead under a fallback event name. You should never see this; if you do, something is wrong on the device.",
         cite="main/telemetry/telemetry_v2.c:656-674",
         msg=env(TS, 31000, "event", {"event": "rules_engine",
                                      "raw": "{\"event\":\"auto_close\",\"source_type\":\"ble_lea"})),
    dict(group="Fallback messages", id="F2", title="A health payload could not be re-parsed",
         when="The health-engine counterpart of F1, with the same caveat.",
         cite="main/telemetry/telemetry_v2.c:676-693",
         msg=env(TS, 31100, "event", {"event": "health_engine",
                                      "raw": "{\"category\":\"health\",\"event\":\"device_off"})),
]

GROUP_ORDER = ["Lifecycle", "Snapshot", "Valve events", "Leak events", "Rules events",
               "Health events", "Command acknowledgements", "Fallback messages"]

INTRO = """This document lists every real telemetry message the eFloStop II Wi-Fi Hub can publish to Azure
IoT Hub. Each entry is a complete message exactly as it appears on the wire: the same keys, in the same order,
that the firmware serializer produces for that situation.

Read these as messages you could have captured, not as templates. The example identifiers, names and readings
are realistic and consistent throughout — one hub, one valve and two leak sensors across the whole document —
so an installation can be followed from message to message. The identifiers and readings are examples; the key
sets, the key order and every enum value are exact.

All of these arrive on one topic, `devices/<device_id>/messages/events/`, at QoS 1. Three envelope `type`
values carry everything: `lifecycle`, `snapshot` and `event`. Within `event`, the `data.event` field tells you
which of the shapes below you have received.

Two things to know before you build a parser. **A conditional key is omitted, not sent as null** — except
inside the two sensor arrays, where every key is always present and unknown values are `null`. And in a
snapshot, a sensor that has gone silent reports `"leak_state": false`, which is indistinguishable from a
genuine dry reading; see message S3."""
