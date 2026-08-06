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
FW = "2.0.2"
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


VALVE_OK = {"valve_id": VALVE_MAC, "state": "open", "battery": 92, "leak_state": False,
            "rmleak": False, "connected": True, "fw_version": VALVE_FW,
            "rating": "excellent", "last_seen_age_s": 4}
VALVE_CLOSED = dict(VALVE_OK, state="closed", rmleak=True)
VALVE_GONE = {"valve_id": VALVE_MAC, "state": "disconnected", "connected": False,
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
         when="Every MQTT connect once provisioned, including reconnects — so not once per boot.",
         cite="telemetry_v2_publish_lifecycle() main/telemetry/telemetry_v2.c:483-519; trigger iothub_task() main/iothub/app_iothub.c:1957-1961",
         msg=env(TS, 42, "lifecycle", {
             "event": "online", "reset_reason": "power_on", "provisioned": True,
             "valve_id": VALVE_MAC, "lora_sensor_count": 0, "ble_leak_sensor_count": 2,
             "rules": RULES})),
    dict(group="Lifecycle", id="L2", title="Hub commissioned, but with no devices",
         when="Provisioned but with no valve and no sensors. A hub that was never provisioned publishes nothing at all, so data.provisioned is true on every lifecycle you receive.",
         cite="omissions build_envelope() main/telemetry/telemetry_v2.c:69-107 and telemetry_v2_publish_lifecycle() :483-519; unprovisioned/offline snapshot gate iothub_task() main/iothub/app_iothub.c:2164-2180",
         msg=env(TS, 18, "lifecycle", {
             "event": "online", "reset_reason": "power_on", "provisioned": True,
             "lora_sensor_count": 0, "ble_leak_sensor_count": 0,
             "rules": RULES}, name=False)),
    dict(group="Lifecycle", id="L3", title="Hub reconnected after a crash",
         when="L1 with a different reset_reason: power_on, software, panic, watchdog, brownout, deep_sleep or unknown.",
         cite="reset_reason_str() main/telemetry/telemetry_v2.c:138-151",
         msg=env(TS, 7, "lifecycle", {
             "event": "online", "reset_reason": "panic", "provisioned": True,
             "valve_id": VALVE_MAC, "lora_sensor_count": 0, "ble_leak_sensor_count": 2,
             "rules": RULES})),

    # ---------------- snapshot ----------------
    dict(group="Snapshot", id="S1", title="Routine heartbeat, everything healthy",
         when="The heartbeat. Cadence defaults to 300 s but is tunable 60-3600 s per hub through the Device Twin and persists across reboots (2.0.2) — read reported.snapshot_interval_s rather than assuming 300.",
         cite="telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c:521-761; default interval main/telemetry/telemetry_v2.h:16, tunable range :21-23",
         msg=env(TS, 3600, "snapshot", snap("heartbeat", VALVE_OK, [S_A, S_B]))),
    dict(group="Snapshot", id="S2", title="Heartbeat with the valve disconnected",
         when="The BLE link to the valve is down: battery, leak_state, rmleak and fw_version are omitted and state carries \"disconnected\".",
         cite="valve-disconnected branch of telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c:521-761",
         msg=env(TS, 5400, "snapshot",
                 snap("heartbeat", VALVE_GONE, [S_A, S_B],
                      health={"rating": "critical", "reason": "Valve offline"}))),
    dict(group="Snapshot", id="S3", title="Heartbeat with a sensor gone silent — read this one carefully",
         when="Sensor unheard for 10 minutes. battery, rssi and fw_version go null but leak_state is hard-coded false — a sensor that was wet when it went silent reads dry here, and the earlier leak_detected is never retracted. last_seen_age_s is the only field that contradicts it.",
         cite="sensor-array builder in telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c:521-761; F-01",
         msg=env(TS, 9000, "snapshot",
                 snap("heartbeat", VALVE_OK,
                      [S_A, ble_sensor(SENSOR_B, "Behind machine", "laundry",
                                       connected=False, age=734)],
                      health={"rating": "critical", "reason": "1 sensor offline"}))),
    dict(group="Snapshot", id="S4", title="Snapshot while a water-access override is running",
         when="A 24-hour override is open. override_active is on EVERY snapshot; override_remaining_s and expires_ts are the conditional pair.",
         cite="telemetry_v2_publish_snapshot() main/telemetry/telemetry_v2.c:521-761; window constant OVERRIDE_WINDOW_DURATION_S main/rules_engine/rules_engine.c:29",
         msg=env(TS, 12000, "snapshot",
                 snap("event", VALVE_OK, [S_A, S_B], override=(82740, TS + 82740)))),
    dict(group="Snapshot", id="S5", title="First snapshot after boot",
         when="S1 with a different reason: heartbeat, event, commission, boot, fast or decommission.",
         cite="snap_reason_str() main/iothub/app_iothub.c:588-601; decommission reboot path iothub_task() main/iothub/app_iothub.c:1728",
         msg=env(TS, 12, "snapshot", snap("boot", VALVE_OK, [S_A, S_B]))),
    dict(group="Snapshot", id="S6", title="Early snapshot as soon as the valve is ready",
         when="Fires the moment the valve's BLE setup completes, before the sensors have beaconed. Array membership comes from the provisioned list, not from who has checked in, so an unheard sensor still appears with nulls.",
         cite="snap_reason_str() main/iothub/app_iothub.c:588-601; membership health_engine_reload_devices() main/health_engine/health_engine.c:450-523",
         msg=env(TS, 25, "snapshot",
                 snap("fast", VALVE_OK,
                      [ble_sensor(SENSOR_A, "Under sink", "kitchen",
                                  connected=False, age=None),
                       ble_sensor(SENSOR_B, "Behind machine", "laundry",
                                  connected=False, age=None)],
                      health={"rating": "critical", "reason": "2 sensors offline"}))),
    dict(group="Snapshot", id="S7", title="Final snapshot before decommissioning",
         when="The last message from this identity. It pictures the CLEARED hub, not the installation — config, name, valve link and health table are already wiped when it is built.",
         cite="decommission branch of handle_c2d_command() main/iothub/app_iothub.c:662-975; final publish iothub_task() main/iothub/app_iothub.c:1590-2254",
         msg=env(TS, 60000, "snapshot",
                 snap("decommission",
                      {"state": "disconnected", "connected": False},
                      []),
                 name=False)),

    # ---------------- valve events ----------------
    dict(group="Valve events", id="V1", title="Valve opened",
         when="A GATT notification whose value changed. Delta-gated: an unchanged notification produces nothing.",
         cite="telemetry_v2_publish_valve_event() main/telemetry/telemetry_v2.c:763-797; ble_update dequeue iothub_task() main/iothub/app_iothub.c:1826",
         msg=env(TS, 4000, "event", {"event": "valve_state_changed", "source_type": "valve",
                                     "valve_id": VALVE_MAC, "valve_state": "open",
                                     "battery": 92, "leak_state": False, "rmleak": False,
                                     "fw_version": VALVE_FW})),
    dict(group="Valve events", id="V2", title="Valve closed and locked after a leak",
         when="V1 with rmleak true — the valve is latched and will refuse to open until the interlock is cleared.",
         cite="telemetry_v2_publish_valve_event() main/telemetry/telemetry_v2.c:763-797",
         msg=env(TS, 4020, "event", {"event": "valve_state_changed", "source_type": "valve",
                                     "valve_id": VALVE_MAC, "valve_state": "closed",
                                     "battery": 92, "leak_state": False, "rmleak": True,
                                     "fw_version": VALVE_FW})),
    # V3/V4 moved to the Leak events group in 1.9.0 — see K5/K6.
    dict(group="Valve events", id="V5", title="Valve opened, firmware revision unknown",
         when="V1 with fw_version absent after a failed DIS read. There is no null variant; the key is simply gone.",
         cite="fw_version omission in telemetry_v2_publish_valve_event() main/telemetry/telemetry_v2.c:763-797",
         msg=env(TS, 4700, "event", {"event": "valve_state_changed", "source_type": "valve",
                                     "valve_id": VALVE_MAC, "valve_state": "open",
                                     "battery": 92, "leak_state": False, "rmleak": False})),

    # ---------------- leak events ----------------
    # ONE family for "water was detected somewhere", from every source. Since 1.9.0 the
    # valve's own flood probe reports here too (K5/K6) instead of through the separate
    # valve_flood_detected / valve_flood_cleared events it used up to 1.8.0.
    #
    # Required core, IDENTICAL keys, order and types for every source:
    #     event, source_type, device_id, leak_state, battery, location
    # Source extras, present only where they apply:
    #     ble_leak_sensor / lora -> rssi
    #     valve                  -> valve_state, rmleak, fw_version
    dict(group="Leak events", id="K1", title="A leak sensor went wet",
         when="A BLE advertisement reporting a leak state different from the cached one. The sensor's fields are FLAT on data here, where a snapshot nests them in ble_leak_sensors[].",
         cite="telemetry_v2_publish_leak_event() main/telemetry/telemetry_v2.c:799-831; BLE leak dequeue iothub_task() main/iothub/app_iothub.c:1824-1900",
         msg=env(TS, 8000, "event", {"event": "leak_detected", "source_type": "ble_leak_sensor",
                                     "sensor_id": SENSOR_A, "leak_state": True, "battery": 87,
                                     "location": {"code": "kitchen", "label": "Under sink"},
                                     "rssi": -64})),
    dict(group="Leak events", id="K2", title="A leak sensor went dry",
         when="leak_state is guaranteed false whenever event is leak_cleared — one value selects both.",
         cite="BLE leak dequeue iothub_task() main/iothub/app_iothub.c:1824-1900",
         msg=env(TS, 8600, "event", {"event": "leak_cleared", "source_type": "ble_leak_sensor",
                                     "sensor_id": SENSOR_A, "leak_state": False, "battery": 87,
                                     "location": {"code": "kitchen", "label": "Under sink"},
                                     "rssi": -66})),
    dict(group="Leak events", id="K3", title="A leak sensor with no location assigned",
         when="K1 with the location fallback. The key is always present: code becomes \"unknown\" and label an empty string.",
         cite="add_location_obj() main/telemetry/telemetry_v2.c:153-168; add_location_for_source() :170-202",
         msg=env(TS, 8100, "event", {"event": "leak_detected", "source_type": "ble_leak_sensor",
                                     "sensor_id": SENSOR_B, "leak_state": True, "battery": 64,
                                     "location": {"code": "unknown", "label": ""},
                                     "rssi": -78})),
    dict(group="Leak events", id="K4", title="A LoRa sensor went wet",
         when="K1 with source_type \"lora\" and a hex id. NOT SEEN IN PRACTICE — the LoRa radio is unpopulated on the current PCBA.",
         cite="LoRa dequeue iothub_task() main/iothub/app_iothub.c:1824",
         msg=env(TS, 8200, "event", {"event": "leak_detected", "source_type": "lora",
                                     "sensor_id": LORA_ID, "leak_state": True, "battery": 78,
                                     "location": {"code": "basement", "label": "Sump"},
                                     "rssi": -95})),
    dict(group="Leak events", id="K5", title="The valve's own flood probe went wet",
         when="The valve's own probe, discriminated by source_type \"valve\". No rssi (a GATT link, not an advertisement); location is always the unknown fallback because sensor_meta cannot address a valve yet; adds valve_state, rmleak and fw_version.",
         cite="BLE_UPD_LEAK branch iothub_task() main/iothub/app_iothub.c:1887; builder telemetry_v2_publish_leak_event() main/telemetry/telemetry_v2.c:799-831",
         msg=env(TS, 4100, "event", {"event": "leak_detected", "source_type": "valve",
                                     "valve_id": VALVE_MAC, "leak_state": True, "battery": 92,
                                     "location": {"code": "unknown", "label": ""},
                                     "valve_state": "open", "rmleak": False,
                                     "fw_version": VALVE_FW})),
    dict(group="Leak events", id="K6", title="The valve's own flood probe went dry",
         when="K5's counterpart. valve_state and rmleak reflect the auto-close K5 triggered: closed and still latched.",
         cite="BLE_UPD_LEAK branch iothub_task() main/iothub/app_iothub.c:1887",
         msg=env(TS, 4600, "event", {"event": "leak_cleared", "source_type": "valve",
                                     "valve_id": VALVE_MAC, "leak_state": False, "battery": 92,
                                     "location": {"code": "unknown", "label": ""},
                                     "valve_state": "closed", "rmleak": True,
                                     "fw_version": VALVE_FW})),

    # ---------------- rules events ----------------
    dict(group="Rules events", id="R1", title="Hub closed the valve because a sensor reported a leak",
         when="The automatic shut-off. rmleak_asserted says whether the interlock writes were ISSUED, not that they landed — false when the valve was unreachable (hardcoded true up to 2.0.1). location is present only when the source has a sensor_meta entry and OMITTED otherwise, the opposite of K1-K6.",
         cite="build_auto_close_telemetry() main/rules_engine/rules_engine.c:409-442",
         msg=env(TS, 8005, "event", {"event": "auto_close", "source_type": "ble_leak_sensor",
                                     "device_id": SENSOR_A, "rmleak_asserted": True,
                                     "location": {"code": "kitchen", "label": "Under sink"}})),
    dict(group="Rules events", id="R2", title="Hub closed the valve because its own flood probe went wet",
         when="R1 with source_type \"valve\" and no location. device_id is the valve's MAC, so this joins to the same device record as K5 and the snapshot.",
         cite="build_auto_close_telemetry() main/rules_engine/rules_engine.c:409-442; identity resolution wire_device_id() main/rules_engine/rules_engine.c:299-309 and add_device_id() :311-335",
         msg=env(TS, 4105, "event", {"event": "auto_close", "source_type": "valve",
                                     "device_id": VALVE_MAC, "rmleak_asserted": True})),
    dict(group="Rules events", id="R3", title="Hub closed the valve on reconnect, finding a leak still active",
         when="A structurally different auto_close: carries active_leak_count and data.cause \"reconnect\", and no source_type at all. Branch on data.cause to tell it from R1/R2.",
         cite="rules_engine_on_valve_connected() main/rules_engine/rules_engine.c:978-1137",
         msg=env(TS, 400, "event", {"event": "auto_close", "cause": "reconnect",
                                    "device_id": SENSOR_A, "rmleak_asserted": True,
                                    "active_leak_count": 1})),
    dict(group="Rules events", id="R4", title="A leak occurred but auto-close was suppressed",
         when="An override window is open, so the hub deliberately did not close. override_remaining_s is omitted when the remaining time is negative.",
         cite="rules_engine_evaluate_leak() main/rules_engine/rules_engine.c:474-632",
         msg=env(TS, 20000, "event", {"event": "auto_close_blocked_override",
                                      "source_type": "ble_leak_sensor", "device_id": SENSOR_A,
                                      "override_remaining_s": 61200})),
    dict(group="Rules events", id="R5", title="A 24-hour water-access override started",
         when="Started by a valve long-press (\"button\") or the override_enable command (\"c2d_command\"). remaining_s is always the full 86400.",
         cite="start_override_window() main/rules_engine/rules_engine.c:193-225; OVERRIDE_WINDOW_DURATION_S main/rules_engine/rules_engine.c:29",
         msg=env(TS, 19000, "event", {"event": "water_access_override_enabled",
                                      "trigger": "button", "expires_ts": TS + 86400,
                                      "remaining_s": 86400})),
    dict(group="Rules events", id="R6", title="The override window elapsed",
         when="auto_close_resumed is exactly active_leak_count > 0 sampled BEFORE any close is attempted — not a report of what happened. With auto-close disabled it still reads true and nothing closes. This message is the only trace of an expiry-driven closure.",
         cite="rules_engine_tick() main/rules_engine/rules_engine.c:1139-1280 (expiry + re-close)",
         msg=env(TS + 86400, 105400, "event", {"event": "water_access_override_expired",
                                               "auto_close_resumed": True,
                                               "active_leak_count": 1})),
    dict(group="Rules events", id="R7", title="The override was cancelled by command",
         when="Response to override_cancel. reason is always \"c2d_command\" — a different vocabulary from data.reason on a snapshot.",
         cite="cancel_override_window() main/rules_engine/rules_engine.c:227-247; rules_engine_cancel_override() :813-895",
         msg=env(TS, 25000, "event", {"event": "auto_close_reenabled",
                                      "previous_remaining_s": 61200,
                                      "reason": "c2d_command"})),
    dict(group="Rules events", id="R8", title="The leak interlock was cleared",
         when="Response to leak_reset. override_cancelled appears only if a window was open. Deliberately NO source_type: the incident may have been latched by a sensor. device_id is omitted on a hub with sensors and no valve.",
         cite="rules_engine_reset_leak_incident() main/rules_engine/rules_engine.c:731-811",
         msg=env(TS, 26000, "event", {"event": "rmleak_cleared", "device_id": VALVE_MAC,
                                      "override_cancelled": True})),
    dict(group="Rules events", id="R9", title="The leak interlock cleared itself",
         when="Every source dry for 30 seconds, so the hub released the latch itself. Does NOT re-open the valve. Same identity rule as R8.",
         cite="rules_engine_tick() main/rules_engine/rules_engine.c:1139-1280; AUTO_CLOSE_COOLDOWN_MS main/rules_engine/rules_engine.c:17",
         msg=env(TS, 8700, "event", {"event": "rmleak_auto_cleared", "device_id": VALVE_MAC,
                                     "clear_after_seconds": 30})),

    # ---------------- health events ----------------
    dict(group="Health events", id="H1", title="A device stopped responding",
         when="A peer crossed into the critical rating. data.category \"health\" is how you recognise these. Critical is reached for connectivity only; a low battery gives warning.",
         cite="health_alert_to_json() main/health_engine/health_engine.c:585-616; compute_sensor_rating() main/health_engine/health_engine.c:123-151",
         msg=env(TS, 9100, "event", {"category": "health", "event": "device_offline",
                                     "source_type": "ble_leak_sensor", "device_id": SENSOR_B,
                                     "rating": "critical", "prev_rating": "excellent",
                                     "battery": 64, "rssi": -78, "offline_duration_s": 600})),
    dict(group="Health events", id="H2", title="A device came back",
         when="The recovery counterpart of H1.",
         cite="health_alert_to_json() main/health_engine/health_engine.c:585-616",
         msg=env(TS, 9700, "event", {"category": "health", "event": "device_recovered",
                                     "source_type": "ble_leak_sensor", "device_id": SENSOR_B,
                                     "rating": "excellent", "prev_rating": "critical",
                                     "battery": 64, "rssi": -75})),
    dict(group="Health events", id="H3", title="The valve stopped responding",
         when="H1 for the valve. rssi is ALWAYS absent, offline_duration_s is omitted when it would be zero, and prev_rating is ALWAYS \"warning\" — a valve gets a 3-minute grace before promotion to critical. Only sensors jump straight there.",
         cite="rssi writers handle_lora_checkin() main/health_engine/health_engine.c:246-265 and handle_ble_leak_checkin() main/health_engine/health_engine.c:267-283; omit-on-zero health_alert_to_json() main/health_engine/health_engine.c:585-616; compute_valve_rating() main/health_engine/health_engine.c:153-176; promotion :338-346",
         msg=env(TS, 5500, "event", {"category": "health", "event": "device_offline",
                                     "source_type": "valve", "device_id": VALVE_MAC,
                                     "rating": "critical", "prev_rating": "warning",
                                     "battery": 92, "offline_duration_s": 180})),

    # ---------------- command acks ----------------
    dict(group="Command acknowledgements", id="C1", title="A command succeeded",
         when="One per enveloped command, or any command carrying a correlation id. data.id echoes what you sent.",
         cite="telemetry_v2_publish_cmd_ack() main/telemetry/telemetry_v2.c:872-897; ack gate handle_c2d_command() main/iothub/app_iothub.c:662-975",
         msg=env(TS, 30000, "event", {"event": "cmd_ack", "id": "req-8f21",
                                      "cmd": "valve_close", "status": "ok"})),
    dict(group="Command acknowledgements", id="C2", title="A command succeeded, no correlation id supplied",
         when="No inbound id, so data.id is OMITTED rather than empty. Match on cmd and timing.",
         cite="id omission in telemetry_v2_publish_cmd_ack() main/telemetry/telemetry_v2.c:872-897",
         msg=env(TS, 30100, "event", {"event": "cmd_ack", "cmd": "valve_open", "status": "ok"})),
    dict(group="Command acknowledgements", id="C3", title="A command was refused",
         when="error.code is not a code — it is the command name repeated. The free-text detail is the only real discriminator.",
         cite="telemetry_v2_publish_cmd_ack() main/telemetry/telemetry_v2.c:872-897; message valve_open_reject_reason() main/iothub/app_iothub.c:543-557",
         msg=env(TS, 30200, "event", {
             "event": "cmd_ack", "id": "req-8f22", "cmd": "valve_open", "status": "error",
             "error": {"code": "valve_open",
                       "detail": "Valve is locked after a leak (RMLEAK). Clear it with leak_reset first, or use override to open the valve during a leak."}})),
    dict(group="Command acknowledgements", id="C4", title="An unrecognised command",
         when="An unknown command name. The detail is a fixed string.",
         cite="unknown-command branch of handle_c2d_command() main/iothub/app_iothub.c:662-975",
         msg=env(TS, 30300, "event", {
             "event": "cmd_ack", "id": "req-8f23", "cmd": "reboot_hub", "status": "error",
             "error": {"code": "reboot_hub", "detail": "unknown command"}})),

    # ---------------- fallback ----------------
    dict(group="Fallback messages", id="F1", title="A rules payload could not be re-parsed",
         when="Defensive path: the rules engine's own JSON failed to re-parse, so the raw string is sent under a fallback event name. You should never see this.",
         cite="telemetry_v2_publish_rules_event() main/telemetry/telemetry_v2.c:833-851 (fallback branch included)",
         msg=env(TS, 31000, "event", {"event": "rules_engine",
                                      "raw": "{\"event\":\"auto_close\",\"source_type\":\"ble_lea"})),
    dict(group="Fallback messages", id="F2", title="A health payload could not be re-parsed",
         when="The health-engine counterpart of F1.",
         cite="telemetry_v2_publish_health_event() main/telemetry/telemetry_v2.c:853-870 (fallback branch included)",
         msg=env(TS, 31100, "event", {"event": "health_engine",
                                      "raw": "{\"category\":\"health\",\"event\":\"device_off"})),
]

GROUP_ORDER = ["Lifecycle", "Snapshot", "Valve events", "Leak events", "Rules events",
               "Health events", "Command acknowledgements", "Fallback messages"]

# Rendered under the group heading, before its messages.
GROUP_NOTES = {
    "Valve events":
        "**The numbering jumps from V2 to V5 on purpose.** V3 and V4 were the valve's own "
        "flood-probe events (`valve_flood_detected` / `valve_flood_cleared`). In firmware 1.9.0 "
        "they became ordinary leak events and moved to **K5** and **K6** under *Leak events*. "
        "The surviving messages keep their original identifiers so this document can be read "
        "side by side with v1.0 and v2.0 — V1 here is the same message as V1 there.",
    "Leak events":
        "**The identity key on this family is not one key.** It is named for the device type: "
        "`sensor_id` on K1–K4, `valve_id` on K5–K6. Both sit in the same position — third, "
        "immediately after `source_type` — and carry the same string form, so switch on "
        "`source_type` and read the corresponding key. K5 and K6 are the valve's own flood "
        "probe, which up to v1.0 appeared separately as V3 and V4 under *Valve events* with a "
        "different payload shape.",
    "Rules events":
        "**Cause arrives before consequence.** When water triggers an automatic shut-off you "
        "receive `leak_detected` **first**, then `auto_close`, then the snapshot. Up to firmware "
        "2.0.0 the order was inverted — `auto_close` reached the cloud about 40 ms ahead of the "
        "leak event that caused it, so a consumer reading in order saw a valve close for no "
        "stated reason. Fixed in 2.0.1.\n\n"
        "Note the `rmleak` values across that sequence are not contradictory even though they "
        "differ: `leak_detected` reports the valve **at detection**, before the interlock "
        "(`rmleak:false`); `auto_close` reports the interlock being applied "
        "(`rmleak_asserted:true`); the snapshot that follows reports the settled state "
        "(`rmleak:true`).\n\n"
        "These events carry `device_id`, not `valve_id`/`sensor_id` — the hub raises them "
        "*about* a device rather than the device reporting itself.",
    "Health events":
        "**These carry `device_id`, not `sensor_id` or `valve_id`** — and they are the only "
        "device-identifying events that do. A health alert is raised by the hub *about* a "
        "device rather than reported *by* one, and the same builder serves valves and sensors "
        "alike, so the key is deliberately generic. `data.source_type` still tells you which "
        "kind of device it is. Note this key was `dev_type` up to firmware 1.9.0.",
}

# Side-by-side map for anyone diffing this against document v2.0 (firmware 1.9.0).
# (what, v2.0 / FW 1.9.0, v3.0 / FW 2.0.0)
CHANGES = [
    ("Identity key, snapshot valve object",
     "data.valve.device_id",
     "data.valve.valve_id"),
    ("Identity key, snapshot sensor arrays",
     "data.lora_sensors[].device_id, data.ble_leak_sensors[].device_id",
     "data.lora_sensors[].sensor_id, data.ble_leak_sensors[].sensor_id"),
    ("Identity key, leak events — sensors (K1-K4)",
     "data.device_id",
     "data.sensor_id"),
    ("Identity key, leak events — valve (K5, K6)",
     "data.device_id",
     "data.valve_id"),
    ("Identity key, valve_state_changed",
     "data.device_id",
     "data.valve_id"),
    ("Identity key, health events",
     "data.device_id",
     "data.device_id  (UNCHANGED — hub-generated, deliberately generic)"),
    ("Identity key, auto_close / rules events",
     "data.device_id",
     "data.device_id  (UNCHANGED — same reason as health)"),
    ("Identity key, lifecycle",
     "data.valve_device_id",
     "data.valve_id"),
    ("Identity key, twin reported",
     "valve_device_id",
     "valve_id"),
    ("Device-type key name",
     "data.source_type on leak/rules events, but data.dev_type on health events",
     "data.source_type on every outbound message; dev_type is gone"),
    ("Reconnect variant of auto_close (R3)",
     "data.source_type \"reconnect\" — a value outside the device-type vocabulary",
     "data.cause \"reconnect\"; the event carries no source_type at all"),
    ("Inbound provision, valve identifier",
     "payload.valve_mac",
     "payload.valve_id  (valve_mac still accepted as a deprecated fallback)"),
    ("Inbound decommission, target matching",
     "\"ble\" case-insensitive, but \"valve\" / \"lora\" / \"all\" case-SENSITIVE",
     "all targets case-insensitive"),
    ("Order of leak_detected vs auto_close",
     "auto_close published ~40 ms BEFORE the leak_detected that caused it",
     "leak_detected first, then auto_close, then the snapshot  (2.0.1)"),
    ("rmleak on a valve leak event",
     "raced the interlock write — false or true depending on GATT timing",
     "sampled once at detection; always the pre-interlock value  (2.0.1)"),
    ("Heartbeat cadence",
     "300 s, fixed in practice: the twin could set it but the value was lost on "
     "every reboot, so hubs effectively always ran 300 s",
     "300 s default, tunable 60-3600 s per hub and PERSISTED across reboots. "
     "Read reported.snapshot_interval_s; do not assume 300  (2.0.2)"),
    ("rmleak_cleared / rmleak_auto_cleared",
     "no source_type, no identity — unattributable",
     "device_id names the valve; no source_type  (2.0.1/2.0.2)"),
    ("auto_close rmleak_asserted",
     "hardcoded true, even when the valve was unreachable and nothing was sent",
     "reflects whether the RMLEAK+close writes were actually issued  (2.0.2)"),
    ("rmleak_cleared / rmleak_auto_cleared",
     "no identity at all",
     "device_id names the valve; NO source_type (it would mean a different thing here)  (2.0.2)"),
    ("Placeholder device ids",
     "auto_close could ship device_id \"valve\" — a phantom device matching nothing",
     "the key is omitted when no MAC resolves  (2.0.2)"),
    ("Firmware version",
     "1.9.0",
     "2.0.2 — major bump: breaking change on both the telemetry and command planes"),
]

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
genuine dry reading; see message S3.

**Changes in firmware 2.0.0.** Firmware 1.9.0 made water detection a single event family; 2.0.0 finishes the
job on naming. Two things changed, and there is no compatibility shim on the telemetry plane — every hub runs
the new shape.

1. **The identity key is named for the device type.** A valve reports `valve_id`; a leak sensor reports
   `sensor_id`. This replaces the single `device_id` that 1.9.0 used everywhere. It applies to the snapshot's
   `data.valve` object and both sensor arrays, to `valve_state_changed`, and to the leak family — where the
   key is `sensor_id` on K1–K4 and `valve_id` on K5–K6. The lifecycle message's `data.valve_device_id` and
   the twin's `valve_device_id` are both now `valve_id`.

   The key always sits in the **same position** — third in an event's `data`, immediately after
   `source_type`; first in a snapshot device object — and always carries the same string form. So a consumer
   switches on `source_type` and reads the matching key; nothing else about the shape moves.

2. **Where a message names a device type, the key is always `data.source_type`.** Health events called it
   `data.dev_type` up to 1.9.0, which made the same concept a third name alongside outbound `source_type` and
   inbound `sensor_type`. The vocabulary is unchanged and still exactly three values: `valve`,
   `ble_leak_sensor`, `lora`, and no value outside that set is ever emitted — the reconnect variant of
   `auto_close` (R3) used to put `"reconnect"` in `source_type`, and now carries `data.cause` instead.

   Not every message names a device: the override-window events (R5, R6, R7) describe a hub-wide policy
   window rather than a device, and carry no `source_type` and no identity. Treat `source_type` as
   "present on any message that concerns a specific device", not as universal.

**Changes in firmware 2.0.1.** A follow-up release fixing three things bench testing exposed. `leak_detected`
now reaches the cloud **before** the `auto_close` it caused (the order was inverted); the `rmleak` value on a
valve leak event is now sampled deterministically at detection rather than racing the interlock write; and
`rmleak_cleared` / `rmleak_auto_cleared` (R8, R9) now carry `source_type` and `device_id` — previously they
arrived with no identity at all and could not be attributed to any device.

**Hub-generated events keep the generic `device_id`, on purpose.** That is the health alerts (`device_offline`,
`device_recovered`), the `auto_close` family (including `auto_close_blocked_override`) and the RMLEAK interlock
events (`rmleak_cleared`, `rmleak_auto_cleared`). The hub raises all of these *about* a device rather than the
device reporting itself, and one builder serves valves and sensors alike. On the health and auto_close families
`data.source_type` still tells you which kind of device it is; the two RMLEAK events deliberately carry NO
`source_type` at all, because the incident they close may have been latched by a sensor while the interlock
itself is always the valve’s. If you are writing a single "which device does this message concern?" helper:
read `valve_id` or `sensor_id` on device-reported messages, `device_id` on these.

On a hub provisioned with sensors but **no valve**, the two RMLEAK events omit `device_id` entirely rather than
naming a valve that does not exist — so treat it as optional there. The same applies to `auto_close`: rather than
emit a placeholder, the hub omits the key when no MAC resolves.

**Identifiers are always UPPERCASE.** A BLE MAC is rendered `AA:BB:CC:DD:EE:FF` — colon-separated, upper
case — and a LoRa id `0x1A2B3C4D`, on every message and every plane, regardless of the case the device was
commissioned in. Before 1.9.0 a sensor commissioned in lower case appeared lower case in snapshot arrays and
upper case in events: the same physical device under two values of the key you join on. The hub normalises on
the way out, so a single exact-match join is safe. Matching case-insensitively anyway costs nothing and is
still the more defensive choice.

**One inbound key changed too.** The `provision` command's `valve_mac` is now `valve_id`, matching what the
hub reports back. `valve_mac` is still accepted as a deprecated fallback so existing app and production-tool
builds keep commissioning; it logs a warning and will be removed. Every other inbound key is unchanged — the
C2D `sensor_meta` payload still uses `sensor_type` and `sensor_id`."""
