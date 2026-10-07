# ruff: noqa: E501
"""V5 edits: Revision history (except the V5 row, which is front's), 1 Purpose and Scope, 2 System Context, 3 MVP App Screens: V4 P0-P130 and tables T0-T5 (T0 rows only if a finding says so).

Addresses V4's original numbering through ctx (see common.py). Every edit: ctx.note(...)."""
from .common import (EditError, add_row, delete_para, delete_row, insert_after,  # noqa: F401
                     insert_lines_after, insert_table_after, replace, replace_in_cell,
                     set_cell, set_text, zebra)


def _delete_orig_row(ctx, k, r, expect=None, empty=False):
    """Delete V4's original row r of table k, wherever it sits now."""
    row = ctx.row(k, r, expect)
    if empty and any(ctx.orig_cell(k, r, c).strip() for c in range(len(ctx._cells[k][r]))):
        raise EditError(f"T{k} R{r}: expected an empty row")
    t = ctx.T(k)
    idx = [x._tr for x in t.rows].index(row._tr)
    delete_row(t, idx)


# ---------------------------------------------------------------- Revision history

def _revision_history(ctx):
    ctx.T(0, "Change Description")
    set_cell(ctx.cell(0, 4, 2, "snapshot freshness demoted to a secondary gate"),
             "hub online/offline now determined primarily by Azure IoT Hub device-connection state; "
             "snapshot freshness demoted to a secondary gate; the app now BLOCKS commands while the hub is offline. "
             "Also documented the leak_reset wet-leak interlock and the hub-side valve_open/valve_set_state RMLEAK "
             "reject (4.4.1, APP-FR-057, 5.3, new TC-N08) - synced from firmware 1.4.1 (TC-N10 bench-verified).")
    _delete_orig_row(ctx, 0, 7, "Also documented the leak_reset wet-leak interlock")
    _delete_orig_row(ctx, 0, 6, empty=True)
    ctx.note("s1-02: revision history: the stray change text of T0 R7 moved into the 24/06/2026 row (R4); empty R6 and the stray R7 deleted")


# ---------------------------------------------------------------- 1 Purpose and Scope

def _scope(ctx):
    set_text(ctx.P(23, "Real-time alerts and push notifications"),
             "Real-time alerts and push notifications for leaks, auto-close, devices going offline, and a valve "
             "battery at 10 % or less, too low to open the valve (sections 4.7 and 4.10). The hub sends no event "
             "for a battery level: the valve battery push is driven by valve.battery in the snapshot (APP-FR-117). "
             "Since 2.1.4 the snapshot also rates such a valve \"critical\" (reason \"Valve battery critical\"); "
             "2.1.3 rates it \"warning\", so the push reads valve.battery, not the rating.")
    ctx.note("s1-04: 1.3 alert scope names what is pushed; the valve battery push comes from the snapshot, not from an event")

    replace(ctx.P(24, "Valve control: open, close, leak reset, and override cancel."),
            "open, close, leak reset, and override cancel.",
            "open, close, leak reset, override cancel, and the remote 24-hour water-access override "
            "(override_enable, see 4.4.3).")
    ctx.note("s1-05: 1.3 valve control scope adds the remote override (override_enable)")

    set_text(ctx.P(26, "Basic settings: auto-close enabled, trigger mask, and snapshot interval."),
             "Basic settings: auto-close on/off (\"Automatic shutoff\", auto_close_enabled), the leak sources that "
             "can trigger it (trigger mask, section 4.8.1), and the snapshot interval (P1, APP-FR-093).")
    ctx.note("s1-06, tm-04: 1.3 settings scope: auto-close is the master switch, the trigger mask selects leak sources, snapshot interval is P1")

    set_text(ctx.P(28, "Sensor metadata editing name and room/location assignment."),
             "Sensor metadata editing: name (label) and room/location assignment.")
    ctx.note("s1-07: 1.3 sensor metadata bullet: colon added, name is label on the wire")

    p31 = ctx.P(31, "Full OTA firmware update UI and workflows.")
    set_text(p31,
             "Full OTA firmware update UI and workflows. Hub firmware 2.1.3 and 2.1.4 have no over-the-air update "
             "function. The app may show the version the hub runs (gateway.fw in telemetry, fw_version in twin "
             "reported).")
    ctx.note("s1-08: 1.4 OTA bullet: the hub has no OTA function; where the running version is read")

    insert_after(p31,
                 "Hub Wi-Fi setup inside the app. The app never talks to the hub, so the user sets up the hub's "
                 "Wi-Fi from the phone: when the hub has no Wi-Fi saved, or cannot reach its saved router, it opens "
                 "an open setup network named WiFi-Hub-<short_id> (for example WiFi-Hub-AD54), with its setup page "
                 "at http://10.10.0.1. The app may show these steps as help text. Holding the hub's reset button for "
                 "10 seconds erases the saved Wi-Fi, restarts the hub and reopens the setup network; the hub keeps "
                 "its devices and settings.",
                 like=p31)
    ctx.note("s1-09: 1.4 new out-of-scope bullet: hub Wi-Fi setup happens outside the app (setup network, 10.10.0.1, 10 s reset)")


# ---------------------------------------------------------------- 1.5 Glossary (T1)

def _glossary(ctx):
    ctx.T(1, "Term")
    set_cell(ctx.cell(1, 3, 1, "Auto-registers hubs on first boot."),
             "Azure Device Provisioning Service. The hub registers itself the first time it reaches the internet, "
             "with its Gateway ID as the registration ID, and from then on is in the IoT Hub registry under that "
             "ID. It registers again after decommission target \"all\" (factory reset).")
    ctx.note("s1-10: glossary DPS: registration at first internet contact, Gateway ID as registration ID, again after decommission all")

    r3 = ctx.row(1, 3, "DPS")
    add_row(ctx.T(1), ["Auto-close",
                       "The hub closing the valve and asserting RMLEAK on its own when a leak is reported. Turned on "
                       "and off by auto_close_enabled, the master switch. The trigger mask selects which leak sources "
                       "can trigger it. Shown to the user as \"Automatic shutoff\"."],
            like_row=r3, after_row=r3)
    ctx.note("tm-v01: glossary: new Auto-close row (master switch, trigger mask, UI label \"Automatic shutoff\")")

    set_cell(ctx.cell(1, 4, 1, "Must be cleared by a leak_reset command"),
             "Interlock that keeps the valve locked closed after an auto-close. The valve cannot be opened while it "
             "is set. It is cleared by a leak_reset command (refused while any leak source is still wet), by the hub "
             "itself about 10 s after every leak source is dry (since 2.1.4; up to 2.1.3, 30 to 60 s), or by the "
             "24-hour override (the button on the valve, or override_enable). leak_reset and the automatic clear do "
             "not open the valve; the override does.")
    ctx.note("tm-02: glossary RMLEAK: the three ways it clears, auto-clear 10 s since 2.1.4 (30-60 s up to 2.1.3)")

    r4 = ctx.row(1, 4, "RMLEAK")
    add_row(ctx.T(1), ["Leak incident",
                       "The hub's own record that a leak has called for auto-close: a wet report from a leak source "
                       "whose trigger-mask bit is set while auto-close is on, or a close the hub makes, while auto-close "
                       "is on, at a valve reconnect, an override_cancel or the end of an override window because a "
                       "leak source is still wet (section 4.8.1). The hub stores it, so it survives a restart. While it is active and no "
                       "override window is open, the hub keeps RMLEAK set on the valve and refuses valve_open and "
                       "valve_set_state \"open\"; since "
                       "2.1.4 it refuses also while the valve is disconnected (2.1.3 checked only the valve's own "
                       "RMLEAK). system_health.rating stays at least \"warning\" while it is active (reason \"Leak "
                       "interlock latched\"). It ends when RMLEAK is cleared (see RMLEAK), with decommission target "
                       "\"all\" and, since 2.1.4, when the hub's last device is removed."],
            like_row=r4, after_row=r4)
    ctx.note("s1-12: glossary: new Leak incident row (what latches it, RMLEAK and Open refusal, warning floor, how it ends)")

    set_cell(ctx.cell(1, 5, 1, "Full system state report sent periodically by the hub."),
             "Full system state report. The hub sends one soon after each connect, a few seconds after every change "
             "or command that succeeded, and at least once per snapshot interval (snapshot_interval_s, default "
             "300 s) as a heartbeat. data.reason says why it was sent (section 5.2.2); it is not state. The app "
             "rebuilds its view from the latest snapshot. A 2.1.3 hub with no devices sends none.")
    ctx.note("s1-13: glossary Snapshot: when the hub sends one, data.reason, no snapshots from a 2.1.3 hub with no devices")

    set_cell(ctx.cell(1, 7, 1, "Birth message sent every time the hub connects"),
             "Birth message sent every time the hub connects or reconnects to MQTT. Events the hub kept while "
             "offline are sent just before it. Since 2.1.4 a hub with no devices sends it too; a 2.1.3 hub with no "
             "devices sends none.")
    ctx.note("s1-14: glossary Lifecycle: kept events go out just before it; since 2.1.4 also from a hub with no devices")

    set_cell(ctx.cell(1, 9, 1, "Command acknowledgment event."),
             "Command acknowledgment event (data.event \"cmd_ack\"). The hub sends one for every command that "
             "arrives as an eflostop.cmd envelope; data.id is left out when the command had no id, and a message that "
             "is not a valid envelope gets none. status \"ok\" means the hub accepted the command: for valve_open, "
             "valve_close and valve_set_state it means queued, not that the valve moved; valve_state_changed or a "
             "later snapshot confirms the move. error.code is always the command name; match error.detail (section "
             "5.2.7). The cmd_ack comes before the snapshot the command causes and, since 2.1.4, before the twin "
             "report it causes (2.1.3 sent the twin report first for provision, rules_config and set_hub_name). Like "
             "any message, it can arrive twice.")
    ctx.note("s1-15: glossary cmd_ack: which commands get one, ok = accepted (valve: queued), order against twin and snapshot, can arrive twice")

    set_cell(ctx.cell(1, 10, 1, "24-hour grace period after a user physically opens the valve"),
             "24-hour period that starts when the valve is opened during a leak: with the button on the valve after "
             "an auto-close, or remotely with override_enable. Leaks are still detected and reported, but the hub "
             "does not close the valve on its own during the window (it sends auto_close_blocked_override instead). "
             "It ends after 24 hours, or earlier with override_cancel, an accepted leak_reset, decommission target "
             "\"all\" and, since 2.1.4, when the hub's last device is removed. It survives a hub restart. Snapshots "
             "show it in override_active and override_remaining_s. Shown to the user as \"24h override\".")
    ctx.note("s1-16, tm-03: glossary Override Window: remote start, leaks still reported, how it ends, survives a restart")

    set_cell(ctx.cell(1, 11, 1, "Bitmask controlling which sensor types can trigger auto-close."),
             "A number stored on the hub that selects which kinds of leak source can make the hub close the valve on "
             "its own (auto-close). Bit 0 (1) = BLE leak sensors, bit 1 (2) = LoRa leak sensors, bit 2 (4) = the "
             "valve flood probe. Default 7 (all three). auto_close_enabled is the master switch above it. The app "
             "changes it only with trigger_ble_leak and trigger_valve_flood, never with trigger_mask or trigger_lora. "
             "See section 4.8.1.")
    ctx.note("tm-01: glossary Trigger Mask: bit 1 (LoRa) added, default 7, master switch, how the app writes it")

    set_cell(ctx.cell(1, 12, 1, "Per-device health score"),
             "Per-device health score: excellent, good, warning, or critical. A device is critical when it reports "
             "water, is offline past its timeout, or has not been heard yet; since 2.1.4 a valve is also critical at "
             "10 % battery or less. system_health.rating is the worst device rating, except that a device not heard "
             "yet is left out for its first minutes after a restart or after a provision added it (section 2.6, "
             "Starting up), and it is at least warning while a leak incident is latched. system_health.reason names "
             "the cause.")
    ctx.note("s1-17: glossary Health Rating: what critical means, how system_health.rating rolls up")


# ---------------------------------------------------------------- 2 System Context

def _context(ctx):
    replace(ctx.P(36, "It talks to sensors and the valve over BLE"),
            "It talks to sensors and the valve over BLE, and it talks to the cloud",
            "It talks to the valve and the BLE leak sensors over BLE, also receives LoRa leak sensors over LoRa, "
            "and talks to the cloud")
    ctx.note("s1-18: 2.1 context text: the hub also receives LoRa leak sensors")

    ctx.T(2, "Component")
    set_cell(ctx.cell(2, 1, 2, "Scans BLE leak sensors and connects to the valve by BLE."),
             "Connects to Azure IoT Hub over MQTT/TLS. Scans BLE leak sensors, receives LoRa leak sensors, and "
             "connects to the valve by BLE. Runs the rules engine (auto-close) and the health engine locally.")
    ctx.note("s1-v03: 2.2 WiFi Hub row: LoRa receiver added")

    set_cell(ctx.cell(2, 2, 2, "Motorized valve. Reports valve state"),
             "Motorized valve. Reports its position (open/closed), battery, RMLEAK and its own water sensor (the "
             "valve flood probe) to the hub, and takes open, close and RMLEAK commands from the hub over BLE. After "
             "an auto-close, the button on the valve opens it and starts the 24-hour override window. At 10 % "
             "battery or less the valve closes itself and will not open until its batteries are replaced; since "
             "2.1.4 the hub then "
             "refuses Open with an error, while a 2.1.3 hub acks it \"ok\" and the valve stays shut.")
    ctx.note("s1-19: 2.2 valve row: RMLEAK written by the hub, valve button override, no opening at 10 % or less (since 2.1.4 refused by the hub)")
    ctx.note("global fix completeness-04: 2.2 valve row: at 10 % or less the valve also closes itself (valve FW 2.2.0 readBatteryADC: Critical closes an open valve)")

    set_cell(ctx.cell(2, 3, 2, "Advertise leak status and battery in BLE manufacturer data."),
             "Battery-powered. Advertise leak state, battery and firmware version over BLE, about every 100 seconds "
             "while dry and every 15 seconds while wet. The hub reports a sensor offline (connected false, rating "
             "critical) after 10 minutes without hearing it (checked every 30 s). Up to 16 per hub.")
    ctx.note("s1-20: 2.2 BLE leak sensor row: report cadence and the 10-minute offline timeout")

    replace(ctx.P(43, "D2C messages carry snapshots, events, and cmd_ack."),
            "D2C messages carry snapshots, events, and cmd_ack. C2D messages carry commands.",
            "D2C messages carry snapshots, lifecycle messages and events (cmd_ack is an event). C2D messages carry "
            "commands. The Device Twin carries desired settings to the hub (snapshot_interval_s, hub_name) and the "
            "hub's reported properties back (identity, firmware, device set, rules and settings in force; section "
            "5.4).")
    ctx.note("s1-v07: Figure 2 caption: lifecycle messages, cmd_ack is an event, what the twin carries")

    ctx.T(3, "Device")
    set_cell(ctx.cell(3, 1, 3, "Passkey: 222900."),
             "Hub keeps a bonded BLE link to its valve. Since 2.1.4 it links only to the provisioned valve (matched "
             "by MAC), and with no valve provisioned it refuses valve commands (\"No valve is set up for this "
             "hub.\"). A 2.1.3 hub with no valve provisioned can link to any nearby eFloStop valve: send valve "
             "commands only to a hub whose lifecycle data or twin reported carries a valve_id.")
    ctx.note("s1-21: 2.4 valve row: valve passkey removed; since 2.1.4 the hub links only to the provisioned valve; 2.1.3 rule for the backend")

    r2 = ctx.row(3, 2, "BLE leak sensors")
    add_row(ctx.T(3), ["LoRa leak sensors", "LoRa radio", "16",
                       "Supported by the hub firmware; the MVP app shows them read-only and offers no LoRa setup, "
                       "removal or editing. The snapshot always carries data.lora_sensors (an empty array when none), "
                       "and lifecycle and twin reported carry lora_sensor_count. A LoRa leak sensor's sensor_id is "
                       "\"0x\" and 8 upper-case hex digits, for example 0x1A2B3C4D. Its leaks use source_type \"lora\" "
                       "and trigger mask bit 1, which the app never changes (section 4.8.1). The app lists these "
                       "sensors and their leaks in the sensor list, alerts and history, and the backend accepts their "
                       "events and pushes their leaks like any other leak sensor's."],
            like_row=r2, after_row=r2)
    ctx.note("s1-22, tm-05: 2.4 new LoRa leak sensors row (16 per hub, read-only in the MVP app, sensor_id format, bit 1 never changed)")

    ctx.T(4, "Field")
    set_cell(ctx.cell(4, 1, 2, "Derived from the hub WiFi MAC address."),
             "Hub ID. GW- followed by the 12 hex digits of the hub's Wi-Fi MAC address, always upper case. It is "
             "also the hub's device ID in Azure IoT Hub and its DPS registration ID; IoT Hub device IDs are "
             "case-sensitive.")
    ctx.note("s1-23: 2.5 gateway.id: upper case, it is the IoT Hub device ID (case-sensitive)")

    set_cell(ctx.cell(4, 2, 0, "valve.device_id"), "data.valve.valve_id")
    set_cell(ctx.cell(4, 2, 1, "00:80:E1:27:9A:E6"), "00:80:E1:27:9A:E6")
    set_cell(ctx.cell(4, 2, 2, "Was valve.mac before firmware 1.9.0."),
             "BLE MAC address of the provisioned valve, upper case with colons. The same valve_id key and value name "
             "the valve in lifecycle data, twin reported, valve_state_changed, leak events with source_type "
             "\"valve\", rules events and health events. Since 2.1.4 data.valve is {} when no valve is provisioned. "
             "A 2.1.3 hub with no valve provisioned sends {\"state\": \"disconnected\", \"connected\": false}, or, "
             "when it has linked to a nearby valve (section 2.4), that valve's valve_id and state: on 2.1.3 treat "
             "data.valve as a valve only while twin reported valve_id is not null.")
    ctx.note("s1-24: 2.5 valve identity: the key is valve_id (V4's valve.device_id is never sent); {} with no valve since 2.1.4; key history moved to one note after the table")

    set_cell(ctx.cell(4, 3, 0, "ble_leak_sensors[].device_id"), "data.ble_leak_sensors[].sensor_id")
    set_cell(ctx.cell(4, 3, 2, "Was sensor_id before firmware 1.9.0."),
             "BLE MAC address of a BLE leak sensor, upper case with colons. The same sensor_id key names the sensor "
             "in leak events, rules events and health events. LoRa leak sensors use sensor_id too (in "
             "data.lora_sensors[] and in events with source_type \"lora\"), with values like 0x1A2B3C4D.")
    ctx.note("s1-25: 2.5 sensor identity: the key is sensor_id (V4's device_id is never sent); LoRa sensor_id format")

    set_cell(ctx.cell(4, 4, 1, "28C0"), "AD54")
    set_cell(ctx.cell(4, 4, 2, "Last four hex digits of the Gateway ID."),
             "Last four hex digits of the Gateway ID, upper case. Used in the hub's setup Wi-Fi network name "
             "WiFi-Hub-<short_id> (here WiFi-Hub-AD54).")
    ctx.note("s1-26: 2.5 gateway.short_id: example AD54 matches GW-34B7DA6AAD54; upper case")
    ctx.note("global fix INT-18: 2.5 setup network name written WiFi-Hub-<short_id>, as in 1.4 and 2.6 (no XXXX)")

    set_cell(ctx.cell(4, 5, 2, "Optional user-assigned hub name (max 31 chars)."),
             "Optional user-assigned hub name, up to 31 bytes of UTF-8 (31 plain ASCII characters). Telemetry sends "
             "gateway.name only when a name is set; twin reported hub_name is \"\" when unset. The backend sets it "
             "only through the Device Twin desired property hub_name: \"\" clears it, and a longer name is ignored "
             "(reported hub_name keeps the old one). The hub applies it at once and again at every connect. The hub "
             "also has a set_hub_name command, but the app and backend do not use it: a name set only by command "
             "reverts whenever desired.hub_name differs. See section 5.4.")
    ctx.note("s1-27: 2.5 gateway.name: 31 bytes, left out when unset, set only through desired.hub_name (set_hub_name not used)")

    p47 = ctx.P(47, "These values show up in telemetry")
    insert_after(ctx.T(4, "Field"),
                 "Hub firmware before 2.0.2 named the valve valve.mac (snapshot) and valve_mac (lifecycle, twin "
                 "reported), and firmware before 2.1.0 named the device in health, auto_close and RMLEAK events "
                 "device_id. No hub the app meets (2.1.3 or 2.1.4) sends these keys, and twin reported writes "
                 "valve_mac and valve_device_id as null, which removes any old value. Read valve_id and sensor_id.",
                 like=p47)
    ctx.note("s1-24, s1-25: 2.5 the document's one note on the old identity keys (valve.mac, valve_mac, device_id), after the IDs table")

    ctx.T(5, "State")
    set_cell(ctx.cell(5, 2, 1, "no snapshot has arrived within 2x the snapshot interval"),
             "IoT Hub reports the device as disconnected, or the backend has received neither a snapshot nor a "
             "lifecycle message for longer than the larger of 2 x snapshot_interval_s and 5 minutes. "
             "snapshot_interval_s is the value in force in twin reported (300 s when unknown), so the default is 10 "
             "minutes. The backend ignores an IoT Hub disconnect followed by a reconnect within 60 s: every hub "
             "reconnects about every 18 hours to renew its 24-hour SAS token. After a power cut IoT Hub reports the "
             "hub disconnected only after about 90 s. On a 2.1.3 hub with no devices only the IoT Hub connection "
             "state applies (see Unprovisioned). The app blocks new commands while offline (see APP-NF-011). "
             "While offline the hub keeps watching its leak sensors and valve and closes the valve on a leak when "
             "auto-close is on; since 2.1.4 also after a restart while the router is down (a 2.1.3 hub that "
             "restarts while Wi-Fi is down does not act on leaks until it is back on Wi-Fi and its cloud start-up "
             "has run). It keeps up to 16 events and sends them after it reconnects, oldest first, before its "
             "lifecycle message. They keep their original ts, so they can arrive after newer messages, and a copy "
             "can arrive twice (see 2.7 and 5.5).")
    ctx.note("s1-28: 2.6 Offline: gate is the larger of 2 x snapshot_interval_s and 5 minutes (snapshot or lifecycle), 60 s reconnect debounce, local protection, replayed events late and possibly twice")

    set_cell(ctx.cell(5, 3, 1, "No valve or sensors assigned yet."),
             "No devices set up. The hub waits for a provision command. Since 2.1.4 it still sends a lifecycle at "
             "each connect, twin reported, and snapshots (one soon after each connect, then heartbeats) with "
             "\"valve\": {}, empty ble_leak_sensors and lora_sensors arrays and system_health {\"rating\": "
             "\"excellent\", \"reason\": \"No devices provisioned\"}. Show \"No devices set up yet\", not \"All "
             "Clear\". Decide this from the device lists (see Provisioned). A 2.1.3 hub that is not provisioned "
             "(provisioned false), such as a new hub or one emptied by a decommission, sends no lifecycle and no "
             "snapshot, only twin reported: while IoT Hub reports it connected, show \"No devices set up yet\" "
             "and use only the IoT Hub connection state for online/offline. A 2.1.3 hub left with no devices by a "
             "provision stays provisioned and keeps sending lifecycle and snapshots, with \"valve\": {\"state\": "
             "\"disconnected\", \"connected\": false} and empty sensor arrays; the device-list rule identifies it.")
    ctx.note("global fix XC-11: 2.6 Unprovisioned: a 2.1.3 hub goes silent when it is not provisioned, not when it is empty; one emptied by a provision keeps sending (master app_iothub.c:2418, provisioning_manager.c:591)")
    ctx.note("s1-29: 2.6 Unprovisioned: what a 2.1.4 hub with no devices sends; 2.1.3 sends no lifecycle or snapshot (IoT Hub state only); label \"No devices set up yet\"")

    set_cell(ctx.cell(5, 4, 1, "Has at least one device assigned"),
             "Has at least one device (the valve, a BLE leak sensor or a LoRa leak sensor) and is monitoring it. "
             "Decide this from the device lists, not from the provisioned flag: data.valve has a valve_id, or "
             "ble_leak_sensors or lora_sensors has an entry (in lifecycle and twin reported: a valve_id that is not "
             "null, or ble_leak_sensor_count or lora_sensor_count above 0). The provisioned flag can read true with no devices after a provision that set only "
             "rules or emptied the sensor lists. Since 2.1.4, when a decommission or a provision leaves the hub "
             "with no device, it also resets its rules to auto_close_enabled true and trigger_mask 7 and ends any "
             "leak incident and override window, with no event. Up to 2.1.3 such a hub kept its last rules: until "
             "its next restart when a decommission emptied it, and across restarts when a provision emptied it "
             "(it stays provisioned). It also kept any leak incident and override window, across restarts.")
    ctx.note("global fix INT-09/XC-03: 2.6 Provisioned: 2.1.3 kept its rules until restart after an emptying decommission, across restarts after an emptying provision (master provisioning_manager.c:151, :591, :834); latch and window kept across restarts (master rules_engine.c:534, :539)")
    ctx.note("s1-30: 2.6 Provisioned: decide from the device lists, not the provisioned flag; since 2.1.4 losing the last device resets rules, latch and override")

    r4 = ctx.row(5, 4, "Provisioned")
    nr = add_row(ctx.T(5), ["Starting up",
                            "A device the hub has not heard since it started, or since a provision added it, reads "
                            "connected false, last_seen_age_s null and rating \"critical\" (a sensor also battery, "
                            "rssi and fw_version null and leak_state false; the valve state \"disconnected\"). The "
                            "hub leaves it out of system_health.rating for its first 10 minutes (a sensor) or 3 "
                            "minutes (the valve; 2.5 minutes when a provision added it). Meanwhile "
                            "system_health.reason reads \"Syncing - waiting for N device(s)\" when nothing else is "
                            "wrong, or ends with \"syncing N device(s)\". After that window it counts: a sensor as "
                            "\"N sensor(s) offline\", the valve as \"Valve offline\". No device_offline is sent for a "
                            "device that was never heard. The app shows such a device with a grey \"Not heard yet\" "
                            "badge during its window (APP-FR-086). Up to 2.1.3, every provision or decommission also "
                            "returned the remaining devices to this state until each was heard again; since 2.1.4 "
                            "the remaining devices keep their state."],
                 like_row=r4, after_row=r4)
    ctx.note("s1-31: 2.6 new Starting up state: devices not heard yet, how long they are left out of the rating, \"Not heard yet\" badge")

    add_row(ctx.T(5), ["Wi-Fi setup",
                       "The hub's open setup network WiFi-Hub-<short_id> is up: at first setup, after the 10-second "
                       "Wi-Fi reset, or when the hub cannot reach its saved router. While it has not joined the "
                       "router, the hub is offline to the cloud: it sends nothing and runs no commands. Treat it as "
                       "Offline. It keeps its devices and settings. After first setup or the 10-second reset (a join "
                       "from the setup page), it reaches the cloud only after it has joined the router and, since "
                       "2.1.4, after its setup network has closed, 15 to 60 seconds after it joins (at most 75 s). "
                       "After a router outage, since 2.1.4 the hub tries its saved router again about every 35 "
                       "seconds, rejoins it on its own once it is back, and closes its setup network within 20 "
                       "seconds of rejoining (at most 75 s). A 2.1.3 hub does not try again: once its setup network "
                       "has opened during a router outage (one longer than about 30 seconds), it stays offline after "
                       "the router is back until it is unplugged and plugged back in, or joined to the router again "
                       "from its setup page. Once online, the hub sends the events it kept and its lifecycle, and a "
                       "snapshot follows within a few minutes."],
            like_row=r4, after_row=nr)
    ctx.note("s1-32: 2.6 new Wi-Fi setup state: offline to the cloud, keeps its devices; setup-page tail 15-60 s; "
             "since 2.1.4 router retry and automatic rejoin; 2.1.3 can stay offline after a router outage until "
             "power-cycled")


# ---------------------------------------------------------------- 2.7 Assumptions

def _assumptions(ctx):
    set_text(ctx.P(53, "Time on the hub is synced via SNTP after WiFi connect."),
             "Time on the hub is synced via SNTP (pool.ntp.org) after Wi-Fi connects. The hub does not connect to "
             "IoT Hub until its clock reads 2024-01-01 00:00:00 UTC or later, so every message reaches the cloud "
             "with a real ts. If the first sync does not finish within 120 seconds, the hub retries every 60 "
             "seconds. Since 2.1.4 an event raised before the first sync (the hub watches for leaks before Wi-Fi "
             "is up) is kept on the hub and sent after the first connect with its ts corrected from the hub's "
             "uptime, so it can arrive after newer messages; 2.1.3 discarded such events. All ts fields in "
             "telemetry messages are Unix epoch seconds in UTC. The backend and app shall store and transmit "
             "timestamps in UTC; timezone localisation is performed only at the display layer.")
    ctx.note("s1-33: 2.7 time sync: no IoT Hub connection before a valid clock, 120 s first window then 60 s retries; since 2.1.4 pre-sync events are kept and sent late")

    p54 = ctx.P(54, "will not change during MVP development")
    set_text(p54,
             "The telemetry schema string stays \"eflostop.v2\" and the command schema \"eflostop.cmd\", but message "
             "content can change under these strings. Since 2.1.4, parsers must accept \"valve\": {} on a hub with "
             "no valve, a null valve battery where 2.1.3 sent 0, and lifecycle and snapshots from a hub with no "
             "devices. New optional fields, event types and commands can also be added. Consumers shall therefore "
             "read gateway.fw, follow the version notes in this document, ignore unknown fields (section 5.2), and "
             "accept null for any value the hub does not know.")
    ctx.note("s1-34: 2.7 schema stability: content changes under the same schema strings; the 2.1.4 shapes parsers must accept")

    insert_after(p54,
                 "Delivery from the hub is at least once. The same message can arrive twice, and events the hub "
                 "kept during an outage arrive late with their original ts and can arrive after newer messages. "
                 "Since 2.1.4 an "
                 "event whose send meets a dropping connection is kept and sent again after the reconnect, and the "
                 "MQTT client can deliver its own copy too; the copies are byte-identical, ts included (2.1.3 lost "
                 "such an event instead). The backend shall drop a message only when its body is byte-identical to "
                 "one it already has (compare the raw body, or its SHA-256), keep the first copy, and order events "
                 "by ts, never by arrival or IoT Hub enqueued time. It shall never de-duplicate on a subset of "
                 "fields such as gateway.id + ts + event + device id: ts has one-second resolution, so two different "
                 "messages can share them. At-least-once is not \"never lost\": the hub keeps at most 16 unsent "
                 "events, and after an outage the first snapshot is the source of truth. Details in 5.5 and 8.3 "
                 "(APP-NF-013, APP-NF-014).",
                 like=p54)
    ctx.note("s1-35: 2.7 new assumption: at-least-once delivery, byte-identical dedupe, order by ts")


# ---------------------------------------------------------------- 3 MVP App Screens

def _screens(ctx):
    # 3.1 Valve Control Screen
    set_text(ctx.P(62, "Valve state: open, closed, or disconnected."),
             "Valve state: open, closed, disconnected, or unknown. \"unknown\" means the link is up but the valve's "
             "readings are not in yet: show \"Connecting...\" and ignore valve.rmleak and valve.leak_state until the "
             "state is known. If data.valve has no valve_id, the hub has no valve set up: show \"No valve set up\" "
             "and hide the valve controls. 2.1.4 sends \"valve\": {} then; 2.1.3 sends {\"state\": "
             "\"disconnected\", \"connected\": false}, and a 2.1.3 hub with no valve set up can link to a nearby "
             "valve and show it here, so on 2.1.3 also check that twin reported valve_id is not null.")
    ctx.note("s1-36: 3.1 valve state: \"unknown\" added; no-valve shapes for 2.1.4 and 2.1.3")

    set_text(ctx.P(63, "Valve battery percentage with color coding"),
             "Valve battery percentage, coloured by the valve's battery bands: red at 10 % or less (the valve closes "
             "itself and will not open; since 2.1.4 the hub also refuses Open with an error), orange from 11 to "
             "20 %, green above "
             "20 %. Use these colours for every hub version: since 2.1.4 the hub rates the valve the same way, "
             "while 2.1.3 rated a valve at 20 % or less \"warning\" and 21-35 % \"good\". Show \"--\" when battery "
             "is null or missing (valve disconnected, or state \"unknown\"). A 2.1.3 hub sends the valve battery as "
             "0 when it has not read it: treat 0 from a 2.1.3 hub as unknown.")
    ctx.note("s1-37: 3.1 valve battery colours match the valve bands (red <=10, orange 11-20, green >20); null shows \"--\"; 2.1.3 zero is unknown")

    set_text(ctx.P(65, "RMLEAK lock state when a leak incident is active."),
             "RMLEAK lock state (valve.rmleak) when a leak incident is active. valve.rmleak is not sent while the "
             "valve is disconnected: keep the last known lock state and mark it \"last known\". Since 2.1.4 the hub "
             "also refuses Open while a leak incident is latched and no override window is open, even if the valve "
             "is disconnected (2.1.3 checked only the valve's own RMLEAK); show the cmd_ack error.detail as sent.")
    ctx.note("s1-38: 3.1 RMLEAK: not sent while the valve is disconnected; since 2.1.4 Open refused while latched with no override window open")

    set_text(ctx.P(66, "Leak detected banner when any sensor reports a leak."),
             "Leak detected banner when any leak source reports water: valve.leak_state (the valve flood probe), or "
             "leak_state in any entry of ble_leak_sensors or lora_sensors, in the latest snapshot, or a "
             "leak_detected event of any source_type that is newer than that snapshot. A valve entry with no "
             "leak_state (valve disconnected) or with state \"unknown\" keeps the valve's last known leak state "
             "(APP-FR-036).")
    ctx.note("s1-39: 3.1 leak banner: the valve flood probe and LoRa leak sensors count too")
    ctx.note("global fix completeness-03: 3.1 and 3.5 leak banner keep a disconnected valve's last known leak state (app_ble_valve.c:1902-1923 keeps the valve critical as a leak; the disconnected entry has no leak_state)")

    set_text(ctx.P(67, "Control buttons: Open Valve, Close Valve, Leak Reset."),
             "Control buttons: Open Valve, Close Valve, Leak Reset, each with a confirmation dialog. While "
             "valve.rmleak is true, also \"Open with 24h Override\" (override_enable, section 4.4.3, APP-FR-043), "
             "with a double confirmation (APP-FR-044).")
    ctx.note("s1-v01: 3.1 controls: \"Open with 24h Override\" added")

    replace(ctx.P(68, "visible only when override active is true"),
            "override active is true", "data.override_active is true")
    ctx.note("s1-v02: 3.1 Override Cancel: field name data.override_active")

    # 3.2 System Health Screen
    set_text(ctx.P(72, "Figure 4. System health summary"),
             "Figure 4. System health summary (Blynk reference mockup). Shows connection health (2/4 sensors "
             "connected), a battery health line, active sensor count (3/4), and last leak location/time. The "
             "mockup's \"CRITICAL: 22%\" predates the hub's battery bands: the app rates batteries as the hub does "
             "(valve: 10 % or less critical, 11-20 % warning; sensor: 20 % or less warning), so 22 % is not low.")
    ctx.note("s1-v08: Figure 4 caption: the mockup's CRITICAL at 22 % is not a real band")

    set_text(ctx.P(75, "Human-readable health reason from the snapshot"),
             "Human-readable health reason from the snapshot (system_health.reason), shown as sent. It names the "
             "device causes at the overall rating, most urgent first, and also \"Leak interlock latched\" while a "
             "leak incident is latched, for example \"Leak detected: Kitchen, Leak interlock latched\", \"2 sensors "
             "offline\" or \"Valve battery low\". A lesser device issue (such as a low valve battery while a sensor "
             "is offline) is not in it: read each device's rating for those. The full "
             "list of texts is in section 5.2.2. Do not drive app logic from this text.")
    ctx.note("s1-40: 3.2 health reason: V4's example could not occur; it names the device causes at the overall rating, plus \"Leak interlock latched\" while latched")

    set_text(ctx.P(77, "Battery warnings: list any device with battery below 20%."),
             "Battery warnings: list any device with battery at 20 % or less (the hub rates it warning). Show a "
             "valve at 10 % or less as critical on every hub version: it will not open (since 2.1.4 the hub also "
             "rates it critical). A valve battery of 0 from a 2.1.3 hub means unknown.")
    ctx.note("s1-41: 3.2 battery warnings at 20 % or less (not below 20 %); a valve at 10 % or less is critical")

    set_text(ctx.P(78, "Last leak event: location name and timestamp."),
             "Last leak event: where (location.label, else the room for location.code, else the sensor_id; "
             "\"Valve\" for source_type \"valve\") and when (the event's ts).")
    ctx.note("s1-v06: 3.2 last leak event: where the place name comes from, the valve flood probe has no location")

    # 3.3 Sensor Detail Screen
    set_text(ctx.P(81, "The second shows a disconnected sensor with unknown values."),
             "Each sensor needs its own detail view. The first mockup shows a connected sensor. The second shows a "
             "disconnected sensor; on the wire such a sensor keeps its last leak state and battery, and values are "
             "unknown only for a sensor the hub has not heard since it restarted or since the sensor was added.")
    ctx.note("s1-v04: 3.3 intro: a disconnected sensor keeps its last values")

    set_text(ctx.P(83, "Figure 5. Sensor detail view: connected state"),
             "Figure 5. Sensor detail view: connected state (Blynk mockup). Kitchen sensor, connected, no leak, "
             "battery 100%. The app shows the sensor ID read-only, in upper case as the hub sends it (sensor_id, "
             "for example 00:80:E1:27:B4:96).")
    set_text(ctx.P(85, "Figure 6. Sensor detail view: disconnected state"),
             "Figure 6. Sensor detail view: disconnected state (Blynk mockup). Bathroom sensor, disconnected. Dashes "
             "for leak and battery apply only to a sensor the hub has not heard since it restarted or since it was "
             "added; a sensor heard before keeps showing its last leak state and battery.")
    ctx.note("s1-v09: Figures 5 and 6 captions: sensor ID read-only and upper case; dashes only for a sensor not heard yet")

    set_text(ctx.P(87, "Sensor name (user-editable label, max 31 characters)."),
             "Sensor name (user-editable label). The hub stores up to 31 bytes of UTF-8 (31 plain ASCII characters, "
             "fewer with accents or emoji) and silently cuts a longer label, so limit input to 31 bytes.")
    ctx.note("s1-42: 3.3 sensor name: 31 bytes, cut silently")

    set_text(ctx.P(89, "Sensor ID (BLE MAC address string)."),
             "Sensor ID (sensor_id): for a BLE leak sensor the BLE MAC address, upper case with colons, for example "
             "00:80:E1:27:B4:96; for a LoRa leak sensor \"0x\" and 8 upper-case hex digits, for example 0x1A2B3C4D.")
    ctx.note("s1-43: 3.3 sensor ID: field name and format (BLE and LoRa)")

    set_text(ctx.P(90, "Connection status: Connected or Disconnected"),
             "Connection status: Connected (connected true: the hub heard the sensor in about the last 10 minutes) "
             "or Disconnected. Show last_seen_age_s as a human-readable time. null means the hub has not heard the "
             "sensor since it last restarted or since the sensor was added: show \"Not heard yet\" (APP-FR-070, "
             "APP-FR-086).")
    ctx.note("s1-44: 3.3 connection status: what Connected means; null last_seen_age_s is \"Not heard yet\"")

    set_text(ctx.P(91, "Leak state: \"No Leak\" (green) or \"Leak Detected\" (red)."),
             "Leak state: \"No Leak\" (green) or \"Leak Detected\" (red), from leak_state. The hub always sends a "
             "boolean; for a disconnected sensor it is the last state the hub heard. Always show \"Leak Detected\" "
             "when leak_state is true, even if the sensor is disconnected. Show \"--\" only when last_seen_age_s is "
             "null (not heard since the hub restarted or since the sensor was added) and leak_state is false.")
    ctx.note("s1-45: 3.3 leak state: never hide leak_state true behind \"--\"")

    set_text(ctx.P(92, "Battery percentage with color bar. Show \"--\" if unknown."),
             "Battery percentage with a color bar: orange at 20 % or less (the hub rates the sensor warning), green "
             "above. For a disconnected sensor it is the last reading. Show \"--\" when battery is null (not heard "
             "since the hub restarted or since the sensor was added). A sensor battery never makes a sensor "
             "critical.")
    ctx.note("s1-46: 3.3 sensor battery: thresholds and when it is null")

    # 3.4 Leak Alert Notification
    set_text(ctx.P(99, "When a leak event arrives, the app shall show a prominent alert."),
             "When a leak_detected event arrives, the app shall show a prominent alert. If the app is in the "
             "background, the backend shall send a push notification (APP-FR-111). The notification shall name the "
             "place: location.label, else the room for location.code; when the label is empty and the code is "
             "\"unknown\", \"at the valve\" for source_type \"valve\" (the valve flood probe) and \"by a leak "
             "sensor\" otherwise. It should deep-link to the dashboard or valve control screen. The backend never "
             "pushes for a byte-identical copy of a message it already has (see 2.7). An event replayed after an "
             "outage still gets its push; when its ts is more than 5 minutes older than its arrival, the body adds "
             "\"at [local time of ts]\", for example \"Water leak detected in Kitchen at 14:05\". The current state "
             "comes from the first snapshot after the reconnect, not from replayed events.")
    ctx.note("s1-47: 3.4 leak alert: place name when there is no location, one push per message, late events")

    # 3.5 Dashboard
    set_text(ctx.P(102, "Status banner at the top"),
             "Status banner at the top, first match wins (APP-FR-021): \"Leak Detected\" (red) when any device "
             "reports a leak in the latest state (valve.leak_state, a disconnected valve's last known leak state, or "
             "leak_state in any entry of ble_leak_sensors or lora_sensors), also while the hub is offline, then "
             "with \"Hub offline - last known state\"; "
             "otherwise \"Hub Offline\" (grey) when the hub is offline (section 2.6); otherwise \"No devices set up "
             "yet\" when the hub has no devices; otherwise \"Needs Attention\" (orange) when system_health.rating is "
             "\"warning\" or \"critical\"; otherwise \"All Clear\" (green).")
    ctx.note("s1-48: 3.5 banner order: Leak Detected wins also offline, then Hub Offline, No devices set up yet, Needs Attention, All Clear")

    replace(ctx.P(103, "Valve card: state, battery, RMLEAK lock icon if active."),
            "RMLEAK lock icon if active.",
            "RMLEAK lock icon if active. On a hub with no valve (no valve_id in data.valve, or on 2.1.3 a null twin "
            "reported valve_id; see 3.1) show \"No valve set up\" instead.")
    ctx.note("s1-49: 3.5 valve card on a hub with no valve")
    ctx.note("global fix XC-05: 3.5 valve card applies the 2.1.3 guard (null twin reported valve_id), as 3.1 and APP-FR-022 do")

    set_text(ctx.P(106, "Pull-to-refresh to request a fresh snapshot"),
             "Pull-to-refresh re-reads the latest state from the backend. There is no command that asks the hub for "
             "a snapshot: the hub sends one after every command that succeeds, after events, and at least once per "
             "snapshot interval.")
    ctx.note("s1-50: 3.5 pull-to-refresh re-reads the backend; no command requests a snapshot")

    # 3.6 Event History
    set_text(ctx.P(111, "Filterable by event type"),
             "Filterable by event type: leaks (leak_detected, leak_cleared), valve and interlock "
             "(valve_state_changed, auto_close, auto_close_blocked_override, rmleak_cleared, rmleak_auto_cleared, "
             "water_access_override_enabled, water_access_override_expired, auto_close_reenabled), device health "
             "(device_offline, device_recovered), and commands (cmd_ack).")
    ctx.note("s1-52: 3.6 history filters: which events go in which filter")

    insert_after(ctx.P(112, "Tapping an event shows detail"),
                 "History is sorted by each event's ts, not by arrival: events kept on the hub during an outage keep "
                 "their original ts and arrive after newer ones, and the same event can arrive twice. Store each "
                 "event once: drop a message that is byte-identical to one already stored (APP-NF-013, see 2.7).")
    ctx.note("s1-51: 3.6 new bullet: history ordered by ts, byte-identical duplicates stored once")

    # 3.7 Settings
    set_text(ctx.P(115, "Auto-close enabled (on/off toggle)."),
             "Auto-close on/off toggle, labelled \"Automatic shutoff\". Changing it sends rules_config with "
             "auto_close_enabled alone. This is the master switch: while it is off, the hub never closes the valve "
             "on its own. Leaks are still reported.")
    ctx.note("tm-06: 3.7 auto-close toggle: label, master switch, leaks still reported")

    set_text(ctx.P(116, "Trigger mask checkboxes: BLE leak sensors, Valve flood probe."),
             "Leak source checkboxes: \"BLE leak sensors\" and \"Valve flood probe\". They show bits 0 and 2 of "
             "data.rules.trigger_mask in the latest snapshot. A change sends rules_config with only "
             "trigger_ble_leak or trigger_valve_flood, never trigger_mask or trigger_lora, so bit 1 (LoRa leak "
             "sensors) never changes (section 4.8.1). While auto-close is off the checkboxes are inactive "
             "(APP-FR-095), and clearing the last checked one offers to turn \"Automatic shutoff\" off instead "
             "(APP-FR-096).")
    ctx.note("tm-07: 3.7 leak source checkboxes send trigger_ble_leak / trigger_valve_flood, never trigger_mask (V4's mapping cleared the LoRa bit)")

    set_text(ctx.P(117, "Snapshot interval slider (60 to 3600 seconds)."),
             "Snapshot interval (60 to 3600 seconds, default 300), offered as the values in APP-FR-093 (1, 2, 5, 10, "
             "15, 30, 60 minutes). Sets Device Twin desired snapshot_interval_s. The hub saves it, keeps it across "
             "restarts and re-reads desired at every connect. There is no cmd_ack: the change is confirmed when twin "
             "reported snapshot_interval_s equals the value sent. A value outside 60-3600 is ignored and reported "
             "keeps the old value.")
    ctx.note("s1-54: 3.7 snapshot interval: fixed values per APP-FR-093, confirmed by twin reported, out-of-range ignored")

    set_text(ctx.P(118, "Each setting change shall send the appropriate C2D command"),
             "Each setting change shall send the matching update: rules_config for auto-close and the leak sources, "
             "and the Device Twin desired property snapshot_interval_s for the snapshot interval. Auto-close and the "
             "leak sources cannot be set through the Device Twin.")
    ctx.note("tm-08: 3.7 rules go only through rules_config; the twin carries only the snapshot interval")

    set_text(ctx.P(119, "Show confirmation when settings are saved."),
             "Show a change as confirmed only once the hub confirms it (APP-FR-094). For rules_config, that is "
             "cmd_ack \"ok\" followed by a snapshot whose data.rules shows the value sent; until then keep the "
             "control pending, and do not revert it on a snapshot sent just after the ack. For the snapshot "
             "interval, it is twin reported snapshot_interval_s equal to the value sent.")
    ctx.note("tm-v03: 3.7 confirmation comes from the hub, not from the send")
    ctx.note("global fix XC-02: 3.7 confirmation point is the snapshot's data.rules after the ack, as APP-FR-094 and 4.8.1 say (not the ack alone)")

    # 3.8 Onboarding
    set_text(ctx.P(121, "The QR code contains the gateway ID"),
             "When the user first opens the app, they need to add a hub. The cleanest approach is a QR code scan "
             "during onboarding. The QR code on the hub label holds a short query string, not a bare Gateway ID, "
             "for example id=GW-34B7DA6AAD54&type=hub&hw=esp32s3-C&sw=v2.1.4. Some production lines print the same "
             "string after a brand web address, for example "
             "https://www.wattsau.com.au/?id=GW-34B7DA6AAD54&type=hub&hw=esp32s3-C&sw=v2.1.4. The app accepts both "
             "forms, and also a code or typed text that holds only a Gateway ID. For a label it accepts the code "
             "only when type is hub, and takes id as the Gateway ID (section 7.3).")
    ctx.note("s1-56: 3.8 hub label QR is a query string (plain or after a brand web address), not a bare Gateway ID")

    replace(ctx.P(125, "types the Gateway ID (e.g., GW-34B7DA6AAD54)."),
            "(e.g., GW-34B7DA6AAD54).",
            "(e.g., GW-34B7DA6AAD54). The app checks it against GW-[0-9A-Fa-f]{12} and converts the letters to "
            "upper case before sending: IoT Hub device IDs are case-sensitive and the hub's ID is upper case.")
    ctx.note("s1-57: 3.8 step 3: Gateway ID upper-cased before lookup")

    set_text(ctx.P(126, "4. Backend checks the Azure IoT Hub device registry"),
             "4. Backend checks the Azure IoT Hub device registry for a device whose ID is the Gateway ID. The hub "
             "registers itself through DPS the first time it reaches the internet, so a hub that has never been "
             "online is not found yet.")
    ctx.note("s1-58: 3.8 step 4: which ID is looked up, why a new hub may not be found yet")

    set_text(ctx.P(128, "6. App starts showing live data when the next snapshot arrives"),
             "6. The app shows the latest snapshot the backend already holds for this hub, if any; otherwise it "
             "waits for the next one (the hub sends one soon after each connect, then at least once per snapshot "
             "interval: default 5 minutes, up to 60 minutes if changed). A new hub has no devices yet: since 2.1.4 "
             "its snapshot shows \"valve\": {}, empty sensor lists and the reason \"No devices provisioned\"; show "
             "\"No devices set up yet\". A 2.1.3 hub with no devices sends no snapshot: while IoT Hub reports it "
             "connected and twin reported has no valve_id (or valve_id null) and both sensor counts are 0, show "
             "the same message (APP-FR-008). After a provision that adds "
             "devices, the hub sends a snapshot a few seconds after the cmd_ack (new devices show \"Not heard yet\" "
             "until the hub hears them), then about every 30 seconds for 5 minutes.")
    ctx.note("s1-59: 3.8 step 6: first data comes from the backend's latest snapshot; the empty hub on 2.1.4 and 2.1.3; snapshots after a provision")
    ctx.note("integrator: 3.8 step 6 decides a 2.1.3 empty hub from twin reported's device lists, not the provisioned flag, as APP-FR-008 and canon C8 do")

    replace(ctx.P(129, "7. If not found, show an error"),
            "\"Hub not found. Make sure the hub is powered on and connected to WiFi.\"",
            "\"Hub not found. Make sure the hub is powered on and connected to Wi-Fi. A hub that has just joined "
            "Wi-Fi can take up to two minutes to appear: try again then.\"")
    ctx.note("s1-v05: 3.8 step 7: a hub that just joined Wi-Fi can take up to two minutes to appear")


def _band(ctx):
    """V4's zebra banding (odd body rows F5F5F5, even rows plain) across the rows s1 inserted, and in
    V4's own broken T4."""
    for k, exp in ((1, "Term"), (3, "Device"), (4, "Field"), (5, "State")):
        zebra(ctx.T(k, exp), ctx.body_shd)
    ctx.note("global fix INT-06: T1 glossary, T3 devices, T4 IDs and T5 hub states re-banded (odd body rows shaded F5F5F5, even rows plain)")


def apply(ctx):
    _revision_history(ctx)
    _scope(ctx)
    _glossary(ctx)
    _context(ctx)
    _assumptions(ctx)
    _screens(ctx)
    _band(ctx)
