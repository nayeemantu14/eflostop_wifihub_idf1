# ruff: noqa: E501
"""V5 edits: 6 State Machines and Workflows, 7 External Interfaces, 8 Non-Functional Requirements: V4 P480-P517, tables T30-T36.

Addresses V4's original numbering through ctx (see common.py). Every edit: ctx.note(...)."""
from .common import (EditError, add_row, delete_para, delete_row, insert_after,  # noqa: F401
                     insert_lines_after, insert_table_after, replace, replace_in_cell,
                     set_cell, set_text)


def _six(ctx):
    """6.1 - 6.6: valve state machine, leak lifecycle, override, onboarding, valve command, commissioning."""

    # ---- 6.1 Figure 11 caption + explanation (s6-01, tm-v06) ----
    cap = ctx.P(483, "Figure 11. Valve state machine as seen by the app")
    set_text(cap, "Figure 11. Valve state machine as seen by the app (valve.state in the snapshot). "
                  "States: OPEN, CLOSED, DISCONNECTED, UNKNOWN. A provisioned valve starts in DISCONNECTED; "
                  "UNKNOWN is the short state after the hub links to it. The RMLEAK interlock keeps the valve "
                  "closed after an auto-close until the automatic RMLEAK clear, leak_reset or the 24-hour "
                  "override clears it.")
    p = insert_after(cap, (
        "valve.state reads \"disconnected\" (connected false) for a provisioned valve until the hub links to it. "
        "When the link comes up the state is \"unknown\" (connected true, readings not in yet, usually for less "
        "than 30 s), then \"open\" or \"closed\". Losing the link from any state returns it to \"disconnected\". "
        "\"unknown\" is this short linking state, not an error: while it lasts, ignore valve.rmleak and "
        "valve.leak_state. Since 2.1.4 a hub with no valve set up sends an empty valve object (\"valve\":{}); "
        "a 2.1.3 hub sends {\"state\":\"disconnected\",\"connected\":false} with no valve_id."),
        like=ctx.P(485, "See Figure in section 4.4 for the full leak incident flow diagram"))
    p = insert_after(p, (
        "After an auto-close the RMLEAK interlock keeps the valve closed until it is cleared in one of three "
        "ways: automatically, about 10 to 12 s after every leak source reads dry (rmleak_auto_cleared; 30 to "
        "60 s up to 2.1.3); by leak_reset once every leak source is dry; or by the 24-hour override "
        "(override_enable, or the button on the valve), which also opens the valve. The automatic clear and "
        "leak_reset do not open the valve: the user then taps Open Valve. When the valve links while "
        "auto_close_enabled is true, no override window is active and any provisioned leak source is wet, the "
        "hub closes it and sets RMLEAK whatever the trigger mask says, and sends auto_close with cause "
        "\"reconnect\" (section 4.8.1)."))
    ctx.note("s6-01, tm-v06: Figure 11 caption corrected (DISCONNECTED first, UNKNOWN is the linking state; three ways RMLEAK clears); "
             "new paragraphs after it give the states, the empty valve object (2.1.3 shape) and the reconnect close that ignores the trigger mask")

    # ---- 6.2 Leak incident lifecycle (s6-02) ----
    set_text(ctx.P(485, "See Figure in section 4.4 for the full leak incident flow diagram"), (
        "See Figure 8 in section 4.4 for the full leak incident flow. The key point: clearing the RMLEAK "
        "interlock and opening the valve are separate steps, and the app must keep that order. The interlock "
        "(valve.rmleak true) is cleared in one of three ways: automatically, about 10 to 12 s after every leak "
        "source reads dry, including any whose trigger-mask bit is clear (rmleak_auto_cleared); by leak_reset, "
        "which the hub refuses while any leak source is still wet; or by the 24-hour override (override_enable, "
        "or the button on the valve), which also opens the valve. The automatic clear and leak_reset never "
        "open the valve: the user then taps Open Valve. In most incidents the automatic clear comes first, so "
        "the app must not wait for a leak_reset that the user never sends. The hub refuses valve_open and "
        "valve_set_state \"open\" while the valve reports RMLEAK set and, since 2.1.4, also while a leak "
        "incident is latched with no override window open (APP-FR-057). Up to 2.1.3 the automatic clear came 30 to 60 s after the last dry report, "
        "and the hub checked only the valve's own RMLEAK."))
    ctx.note("s6-02: 6.2 names Figure 8 and gives the three ways RMLEAK clears (automatic clear first in most incidents), with the 2.1.3 timing and refusal")

    # ---- 6.3 Override window (s6-03) ----
    set_text(ctx.P(487, "See Figure in section 4.4.2 for the override window flow"), (
        "See Figure 9 in section 4.4.2 for the override window flow. The hub keeps the override window across "
        "reboots. Drive the countdown from override_remaining_s in the snapshot, or remaining_s in "
        "water_access_override_enabled. expires_ts (Unix epoch seconds, UTC) is also sent, but treat it as "
        "optional. Since 2.1.4 it is left out when the window started before the hub's clock was set (after a "
        "power cut, until the hub reaches the internet again): water_access_override_enabled then has none, "
        "and snapshots leave it out until up to about 30 s after the clock sync. A 2.1.3 hub sends a time in "
        "1970 in that event instead, so ignore any expires_ts below 1704067200. Since 2.1.4 a window started before the "
        "clock was set keeps its full 24 hours, unless the hub restarts before the clock is set. A 2.1.3 hub, "
        "and a 2.1.4 hub that restarted, ends it at the first clock sync and sends "
        "water_access_override_expired. The window can also end without water_access_override_expired or "
        "auto_close_reenabled: an accepted leak_reset cancels it (its rmleak_cleared event carries "
        "override_cancelled: true), and decommission target \"all\" and, since 2.1.4, removing the hub's last "
        "device clear it with no event. The next snapshot then shows override_active false."))
    ctx.note("global fix INT-15: 6.3 says when expires_ts is missing in the wording of 4.4.2 and APP-FR-041 (since 2.1.4, a window started before the clock was set; snapshots carry it from up to about 30 s after the sync; rules_engine.c:2066-2075)")
    ctx.note("s6-03: 6.3 drives the countdown from the remaining-seconds fields, makes expires_ts optional (ignore values below 1704067200 from 2.1.3), "
             "and lists the ways a window ends with no expiry event")

    # ---- 6.4 Onboarding sequence T30 (s6-04 .. s6-08) ----
    ctx.T(30, "Actor | Action")
    set_cell(ctx.cell(30, 3, 1, "Scans QR code on hub label"), (
        "Scans the QR code on the hub label. It holds a short query string such as "
        "id=GW-34B7DA6AAD54&type=hub&hw=esp32s3-C&sw=v2.1.4, sometimes after a brand web address. The Gateway "
        "ID is the id value (see section 7.3)."))
    set_cell(ctx.cell(30, 4, 1, "Sends Gateway ID to Watts backend"),
             "Checks that type is hub, takes the id value, converts it to upper case and sends it to the Watts backend.")
    set_cell(ctx.cell(30, 5, 1, "Looks up device in Azure IoT Hub registry"), (
        "Looks up the device in the Azure IoT Hub registry by that ID (IoT Hub device IDs are case-sensitive; "
        "hubs register in upper case). A hub is in the registry only after its first registration with Azure "
        "DPS, so it must have been set up on Wi-Fi and reached the internet at least once. Right after Wi-Fi "
        "setup this can take about two minutes: since 2.1.4 the hub first closes its Wi-Fi setup network (15 "
        "to 60 s after it joins the router, at most 75 s), and a first DPS registration can take up to a "
        "minute more. A 2.1.3 hub registers soon after it joins the router, once its clock is set. If the hub is not found, the app "
        "asks the user to wait two minutes and try again (APP-FR-007)."))
    set_cell(ctx.cell(30, 7, 1, "Shows dashboard in loading state"), (
        "Shows the dashboard. If the backend already holds a snapshot for this hub, shows it at once; "
        "otherwise shows a loading state."))
    set_cell(ctx.cell(30, 8, 1, "Sends next snapshot (within 5 minutes or sooner)"), (
        "Sends a snapshot at least every snapshot_interval_s (default 300 s; 60 to 3600 s), a few seconds "
        "after any change it reports or any command it accepts, and soon after every connect (after a restart within "
        "about 150 s, sooner once the valve has linked)."))
    set_cell(ctx.cell(30, 10, 1, "Displays live data from first snapshot"), (
        "Displays live data from the first snapshot. Since 2.1.4 a hub with no devices yet sends "
        "\"valve\":{}, empty ble_leak_sensors and lora_sensors arrays and system_health.reason \"No devices "
        "provisioned\" (rating \"excellent\"): show \"No devices set up yet\", not \"All Clear\". A new 2.1.3 hub "
        "is not provisioned and sends no snapshot and no lifecycle, only twin reported: while IoT Hub reports it "
        "connected and twin reported has no valve_id (or valve_id null) and both sensor counts are 0, show \"No "
        "devices set up yet\" instead of the loading state or \"Hub Offline\" (APP-FR-008)."))
    ctx.note("s6-04, s6-05, s6-06: T30 R3-R5 read the label query string, upper-case the Gateway ID, and say when a just-set-up hub appears in the registry (since 2.1.4 after its setup network closes)")
    ctx.note("s6-07: T30 R8 gives the real snapshot timing; the stored-snapshot shortcut moved to the app's loading step (T30 R7)")
    ctx.note("global fix XC-19, XC-11: T30 R10 decides a 2.1.3 empty hub from twin reported's device lists, not the provisioned flag (APP-FR-008, canon C8)")
    ctx.note("s6-08: T30 R10 adds the hub with no devices (\"No devices set up yet\", canon C8) and the 2.1.3 hub that sends only twin reported")

    # ---- 6.5 Valve command flow T31 (s6-09 .. s6-13) ----
    ctx.T(31, "Step | Actor | Action")
    set_cell(ctx.cell(31, 4, 2, "Generates a unique correlation ID"), (
        "Generates a unique correlation ID (for example a UUID; at most 63 characters, because the hub cuts a "
        "longer id and its ack would not match). Shows spinner on valve card. Disables buttons."))
    set_cell(ctx.cell(31, 6, 2, "Sends C2D message to Azure IoT Hub"),
             "Sends the C2D message to Azure IoT Hub with the command JSON and a 30-second expiry (section 7.1).")
    set_cell(ctx.cell(31, 7, 2, "Receives command. Sends close command to valve."), (
        "Receives the command. Since 2.1.4 it refuses it with a cmd_ack error when no valve is set up (\"No "
        "valve is set up for this hub.\") or its valve command queue is full (\"The valve command could not be "
        "queued. Try again.\"); a 2.1.3 hub acks every valve_close \"ok\". Otherwise it queues the close and "
        "starts a link to the valve if it is not connected. A close is never refused for RMLEAK or battery. "
        "If the valve is out of range, the close waits on the hub, with no time limit, and is carried out when "
        "the valve next links; a newer valve command replaces it, and a hub restart drops it."))
    set_cell(ctx.cell(31, 8, 2, "Publishes cmd_ack event"), (
        "Publishes the cmd_ack event (type=\"event\", data.event=\"cmd_ack\", data.status=\"ok\") as soon as "
        "the close is queued. \"ok\" means accepted and queued, not that the valve has moved."))
    set_cell(ctx.cell(31, 10, 2, "Receives ack. Removes spinner."), (
        "Receives the ack. Shows \"Closing...\" on the valve card (\"Waiting for valve\" while "
        "valve.connected is false) and keeps the buttons disabled until step 11 confirms the new state. If "
        "nothing confirms it within 60 s of the ack, shows the state from the latest snapshot with the "
        "message \"The valve has not confirmed yet. Check that it has power and is in range.\" and keeps "
        "reading snapshots. The buttons are enabled again at that point."))
    set_cell(ctx.cell(31, 11, 2, "Publishes next snapshot confirming valve.state"), (
        "When the valve reports that it has closed, publishes valve_state_changed with valve_state "
        "\"closed\", then a snapshot with valve.state \"closed\" (snapshots are at least 5 s apart). The app "
        "then shows \"Closed\". The snapshot that follows the ack is often sent before the valve has moved and "
        "can still show \"open\": do not revert the card on it. A valve that relinks without moving sends no "
        "valve_state_changed, and a position change made while it was not linked shows only in the next "
        "snapshot (section 5.2.3). Treat valve_state_changed as the current state, not as proof of a "
        "movement."))
    ctx.note("s6-09: T31 R4 limits the correlation id to 63 characters")
    ctx.note("s6-19: T31 R6 says the backend sends the valve command with the 30-second expiry (section 7.1)")
    ctx.note("s6-10, s6-11: T31 R7-R8 say the hub queues the close (refusals since 2.1.4, held while the valve is out of range) and that ok means queued")
    ctx.note("global fix completeness-02: T31 R11: a relink without movement sends no valve_state_changed (app_ble_valve.c:800-805 posts a state change only outside setup; link-up posts CONNECTED and LEAK only; same on 2.1.3), as 5.2.3 says")
    ctx.note("s6-12, s6-13: T31 R10-R11 add the Closing.../Waiting for valve state and the 60 s confirmation rule (canon C23); confirmation comes from valve_state_changed or a snapshot")

    # ---- 6.5 Timeout scenario (s6-14) ----
    p493 = ctx.P(493, "If no cmd_ack arrives within 20 seconds")
    set_text(p493, ctx.orig(493) + (
        " A cmd_ack can still arrive after the timeout: the hub keeps an ack it could not send and sends it "
        "after it reconnects. The app does not change the UI on a late ack; the backend records it against "
        "its command (APP-NF-018), and the next snapshot shows the state. Because the backend sets a "
        "30-second expiry on valve_open, valve_close, valve_set_state and override_enable (section 7.1), IoT "
        "Hub can deliver such a command for at most about 10 s after the app's timeout. A valve command the "
        "hub had already received is carried out as in step 7, even when its ack arrives late or never: a "
        "close for a valve out of range still runs when the valve next links."))
    replace(p493, 'the app shows: "Command timed out. The hub may be offline. Your command may still execute when the hub reconnects."',
            'the app shows the APP-FR-053 message "Command timed out. The hub may be offline."')
    ctx.note("global fix completeness-07: 6.5 timeout scenario quotes the APP-FR-053 text (the rest of the paragraph says when the command can still run)")
    replace(p493, "this post-send timeout applies only when the hub drops after a command has already been sent.",
            "this post-send timeout applies when the hub drops after a command has been sent, or dropped shortly "
            "before the send while IoT Hub still showed it connected.")
    ctx.note("s6-14: 6.5 timeout scenario adds the late cmd_ack (APP-NF-018), the 30 s expiry bound on IoT Hub delivery, "
             "and that a command the hub already received still runs; the timeout also covers a hub that dropped just before the send (APP-NF-011)")

    # ---- 6.6 Commissioning and decommissioning (s6-15) ----
    p495 = ctx.P(495, "For MVP, sensors are provisioned by sending a provision C2D command")
    set_text(p495, (
        "For MVP, the backend sets up the hub's devices with the provision C2D command (typically during "
        "setup). The app does not drive the provisioning flow in MVP. Each device list in a provision payload "
        "(ble_leak_sensors, lora_sensors) replaces the hub's whole list of that type: always send every "
        "sensor that should stay, never only the new one. A list left out of the payload keeps its current "
        "contents. valve_id adds or replaces the valve (valve_mac is still accepted but deprecated: send "
        "valve_id); a provision cannot remove it. A provision that only adds or removes devices carries no "
        "rules keys (APP-FR-097). Individual devices are removed with decommission C2D commands (see section "
        "4.11)."))
    insert_lines_after(p495, [
        "After a provision or decommission the cloud gets the cmd_ack, then a twin reported update and a "
        "snapshot with the new device set. A 2.1.3 hub sends the twin report before the cmd_ack for a "
        "provision, and none after a decommission that removes one device. New devices show connected false "
        "and null readings until the hub first hears them (\"Not heard yet\", APP-FR-086). When devices were "
        "added, the hub sends a further snapshot once every new device has been heard (at most about 150 s "
        "later), one each time a new device is heard for the first time, and for 5 minutes a snapshot about "
        "every 30 s and on every sensor report, while the installer places the sensors.",
        "Since 2.1.4, adding or removing a device leaves the other devices' state unchanged. Up to 2.1.3, every "
        "provision or decommission returned the remaining devices to \"not heard yet\" until each was heard "
        "again: show that as syncing, not as the devices going offline.",
        "Since 2.1.4, when a decommission or a provision leaves the hub with no device, the hub clears any "
        "leak latch and override window and puts the rules back to their defaults (auto_close_enabled true, "
        "trigger_mask 7, see section 4.8.1), with no event. Its snapshots then show \"valve\":{}, empty sensor "
        "arrays and system_health.reason \"No devices provisioned\". Detect an empty hub from the device "
        "lists, not from the provisioned flag: a hub emptied by a provision still reports provisioned true. "
        "Up to 2.1.3 an emptied hub kept its last rules: until its next restart when a decommission emptied "
        "it, and after restarts too when a provision emptied it. A 2.1.3 hub emptied by a decommission falls "
        "silent: no lifecycle and no snapshots, only twin reported, until it is provisioned again.",
        "To reuse a 2.1.3 hub for a new installation or a new owner, the backend sends decommission target "
        "\"all\", not single-device removals: a 2.1.3 hub emptied by single-device removals also keeps its leak "
        "latch and any override window across restarts, and a later provision starts with them. A kept "
        "override window pauses automatic shutoff for up to 24 hours. Since 2.1.4 the hub clears both when it "
        "is emptied, and at a restart with no devices.",
    ])
    ctx.note("global fix XC-12: 6.6 adds the 2.1.3 reuse rule: decommission \"all\", because single removals leave the leak latch and override window in NVS (master app_iothub.c:925-931 clears them only for \"all\"; master rules_engine.c:534, :539 reload them at boot; HEAD app_iothub.c:3718-3750 clears them at an empty boot)")
    ctx.note("s6-15: 6.6 states that each sensor list in a provision replaces the whole list, what the cloud sees after a device-set change "
             "(2.1.3 order and reset), and the empty-hub reset since 2.1.4")


def _seven(ctx):
    """7.1 Azure IoT Hub, 7.2 Push Notifications, 7.3 QR Code Scanning."""

    set_text(ctx.P(499, "Protocol: MQTT over TLS (port 8883)"), (
        "Protocol: MQTT over TLS (port 8883). The hub registers itself through Azure DPS (symmetric-key "
        "enrollment group, endpoint global.azure-devices-provisioning.net) with its Gateway ID as the "
        "registration ID, and connects to IoT Hub as the device ID that DPS assigns (normally the Gateway ID). "
        "It keeps that assignment; a decommission with target \"all\" makes it register again. The hub needs "
        "outbound TCP 8883 to DPS and IoT Hub, and NTP (UDP 123) to pool.ntp.org, because it does not connect "
        "until its clock is set (see Authentication). A network that blocks any of these leaves the hub on "
        "Wi-Fi but never in the cloud: the app shows \"Hub not found\" or \"Hub Offline\"."))
    ctx.note("s6-16: 7.1 Protocol gives the DPS registration ID, the device ID and the network needs (8883, NTP)")

    set_text(ctx.P(500, "Authentication: Per-device SAS token"), (
        "Authentication: per-device SAS token. The hub derives its device key from the DPS enrollment group "
        "key and its Gateway ID. Each token is valid for 24 hours. The hub makes a new token at every boot and "
        "about every 18 hours while connected. To apply it, the hub closes its MQTT connection and connects "
        "again at once, so IoT Hub reports a disconnect of a few seconds and a new lifecycle message follows. "
        "The hub connects only after its clock is set by SNTP, because the token's expiry comes from the "
        "clock."))
    ctx.note("s6-17: 7.1 Authentication corrected: 24-hour tokens renewed about every 18 hours with a short reconnect (V4 said one year)")

    set_text(ctx.P(501, "D2C telemetry: Published to devices/{device_id}/messages/events/"), (
        "D2C telemetry: published to devices/{device_id}/messages/events/ with QoS 1. The body is UTF-8 JSON. "
        "The hub sets no message properties, not even content type or content encoding, so IoT Hub message "
        "routing queries on $body do not work: route the whole stream and parse the body in the backend. A "
        "snapshot from a hub with all 16 BLE and 16 LoRa leak sensors is about 8 KB, and can reach about 10 KB "
        "with long labels."))
    ctx.note("integrator: 7.1 D2C snapshot size matches section 9 'Large sensor count': about 8 KB, up to about 10 KB with long labels")
    ctx.note("s6-18: 7.1 D2C says the hub sets no message properties ($body routing does not work) and gives the largest snapshot size")

    set_text(ctx.P(502, "C2D commands: Received on devices/{device_id}/messages/devicebound/#"), (
        "C2D commands: received on the devices/{device_id}/messages/devicebound/# subscription (QoS 1). The "
        "hub accepts a message of up to 8,192 bytes; a larger one is refused with a cmd_ack error (section "
        "5.2.7). The hub does not check a command's age and does not de-duplicate commands by id: it runs "
        "every C2D message it receives, in arrival order, when it receives it. A message still queued in IoT "
        "Hub runs when the hub reconnects, and a command delivered twice runs twice. So the backend sets a "
        "30-second expiry (the message's absolute expiry time, ExpiryTimeUtc in the IoT Hub service SDK) on "
        "valve_open, valve_close, valve_set_state and override_enable: the app's 20 s ack timeout "
        "(APP-FR-053) plus 10 s for the backend and IoT Hub. Every other command keeps the IoT Hub default "
        "time-to-live (1 hour unless the IoT Hub's cloud-to-device settings change it). Every send, retries "
        "included, uses a new correlation id."))
    ctx.note("s6-19: 7.1 C2D adds the 8,192-byte limit, no age check and no id dedupe, and the 30 s expiry on the four valve-actuating commands (canon C15)")

    set_text(ctx.P(503, "Device Twin: Desired properties delivered via"), (
        "Device Twin: the hub reads the whole twin (GET) at every connect and applies its desired properties "
        "then, and again on every desired PATCH ($iothub/twin/PATCH/properties/desired/#). It writes reported "
        "properties at every connect, after every provision, decommission, rules_config and set_hub_name, and "
        "after every desired change. Since 2.1.4 a command's cmd_ack goes out before the twin report it "
        "causes; a 2.1.3 hub sends the twin report first for provision, rules_config and set_hub_name, and "
        "sends none after a decommission that removes one device. The report normally follows within a "
        "second, sometimes a few seconds later. On a slow or reconnecting link an older report can land after "
        "a newer one; the hub's next report corrects it. Wait until a reported value equals the value the "
        "command set rather than reading the twin once when the ack arrives."))
    ctx.note("global fix XC-16: 7.1 Device Twin compares reported with the value sent (2.1.3 writes the twin before the ack)")
    ctx.note("s6-20: 7.1 Device Twin says when the hub reads and writes the twin, the ack-before-report order since 2.1.4 (2.1.3 order), and to wait for the reported value")

    set_text(ctx.P(505, "Connection state: The backend shall use Azure IoT Hub's native device-connection state"), (
        ctx.orig(505) + " The hub uses a 60 s MQTT keep-alive, so after a power cut or a silent network loss "
        "IoT Hub reports it disconnected only after about 90 s. Every hub also closes and reopens its "
        "connection about every 18 hours to renew its token (see Authentication above) and sends a lifecycle "
        "message after it: the backend ignores a disconnect followed by a reconnect within 60 s and raises "
        "no alert for it."))
    ctx.note("s6-21: 7.1 Connection state adds the 90 s silent-drop delay and the 60 s debounce for the token-renewal reconnect (canon C14)")

    # ---- 7.2 push rules (s6-22) ----
    insert_after(ctx.P(507, "The Watts backend decides when to send push notifications"), (
        "Push rules (section 4.10) use these hub messages: leak_detected (APP-FR-111), auto_close "
        "(APP-FR-112), device_offline with data.category \"health\" (APP-FR-113), "
        "water_access_override_enabled (APP-FR-116) and water_access_override_expired with "
        "auto_close_resumed true (APP-FR-118). device_offline and device_recovered report a device's link "
        "only. The hub sends no health event for a leak, for a battery at any level, for a weak signal, or for "
        "a device not heard since the hub started or since it was added. A rule for those reads the snapshot "
        "instead: a device's rating, or a part of system_health.reason such as \"Valve battery critical\" or "
        "\"1 sensor offline\". The valve battery push (APP-FR-117) works this way. The backend sends one push "
        "per message, never one for a byte-identical copy (APP-NF-013). It pushes for leak_detected and "
        "auto_close even when they arrive late, for example replayed after an outage; when an event's ts is "
        "more than 5 minutes older than its arrival, the body adds \"at [local time of ts]\"."))
    ctx.note("s6-22: 7.2 new paragraph lists the hub messages behind each push, the snapshot-driven rules (no health event for a leak, battery or never-heard device), one push per message, and late pushes (canon C6, C18)")

    # ---- 7.3 QR code (s6-23) ----
    p509 = ctx.P(509, "The QR code on the hub label contains the Gateway ID string")
    set_text(p509, (
        "The QR code on the hub label holds a short query string, not a bare Gateway ID, for example "
        "id=GW-34B7DA6AAD54&type=hub&hw=esp32s3-C&sw=v2.1.4. Some production lines print the same string after "
        "a brand web address, for example "
        "https://www.wattsau.com.au/?id=GW-34B7DA6AAD54&type=hub&hw=esp32s3-C&sw=v2.1.4. The app accepts both, "
        "and also a code or typed text that holds only a Gateway ID. Use the platform camera API (AVFoundation "
        "on iOS, CameraX on Android) or a library like ZXing."))
    insert_after(p509, (
        "To read a label: if the text starts with http:// or https://, take everything after the first \"?\"; "
        "split the rest on \"&\"; split each part on the first \"=\" only; URL-decode each key and value. Keys "
        "are lower case; ignore unknown keys and do not rely on key order. Accept the code only when type is "
        "hub, and take id as the Gateway ID. Check the Gateway ID against GW-[0-9A-Fa-f]{12} and convert it to "
        "upper case before sending it to the backend: the hub registers in upper case, and IoT Hub device IDs "
        "are case-sensitive. Valve labels (type=valve, id=VV-...) and leak sensor labels (type=sensor, "
        "id=LK-...) use the same format; the Add Hub screen rejects them with \"This is not a hub QR code.\" "
        "sw is the firmware at production time; the version the hub runs is gateway.fw."))
    ctx.note("s6-23: 7.3 rewritten: the label QR is a query string (plain or after a brand web address); parsing steps, type check, upper-casing, rejecting valve and sensor labels (canon C17)")


def _eight(ctx):
    """8.1 - 8.4: T32 - T35 (T36 unchanged)."""

    # ---- 8.1 APP-NF-004 (s6-24) ----
    ctx.T(32, "Req ID | Priority | Requirement | Acceptance Criteria")
    set_cell(ctx.cell(32, 4, 2, "The app shall not display sensitive keys, tokens, or internal IDs"), (
        "The app shall not display secrets (keys, SAS tokens, connection strings, auth tokens) or "
        "backend-internal record IDs in the UI or in debug logs shipped to production. Device identifiers the "
        "user needs, the Gateway ID, valve_id (a BLE MAC address) and sensor_id (a BLE MAC address, or a LoRa "
        "ID such as 0x1A2B3C4D), may be shown."))
    ctx.note("s6-24: APP-NF-004 allows the device identifiers the user needs (Gateway ID, valve_id, sensor_id)")

    # ---- 8.2 latency targets (s6-v01, s6-25, s6-26) ----
    ctx.T(33, "Metric | Target | Notes")
    set_cell(ctx.cell(33, 1, 2, "From hub event to phone push"), (
        "From hub event to phone push, while the hub is connected. Depends on Event Hub + backend + APNs/FCM. "
        "An event raised while the hub is offline, or (since 2.1.4) before its clock is set after a power "
        "cut, is sent after the hub reconnects with its original ts; its push then adds the event's local "
        "time when it is more than 5 minutes old (APP-FR-111). A 2.1.3 hub discards events raised before its "
        "clock is set."))
    set_cell(ctx.cell(33, 2, 0, "Valve command confirmation"), "Valve command acknowledgement")
    set_cell(ctx.cell(33, 2, 2, "From user tap to cmd_ack displayed"), (
        "From user tap to cmd_ack displayed. The hub acks once the command is queued, so the ack does not "
        "wait for the valve's BLE link. The valve's new position is confirmed later by valve_state_changed "
        "and the next snapshot: within a few seconds when the valve is linked; when it is not, only after the "
        "hub has linked to it again, which can take 30 s or more, and not at all while the valve stays out of "
        "range. The app stops waiting for it after 60 s (APP-FR-055)."))
    set_cell(ctx.cell(33, 4, 1, "< 2x snapshot interval"), "< 2x snapshot interval, or 5 min if larger")
    set_cell(ctx.cell(33, 4, 2, "Secondary offline gate"), (
        "Secondary offline gate. Primary signal is the IoT Hub device-connection state. While connected, the "
        "hub sends a snapshot at least every snapshot_interval_s; the value in force is in twin reported "
        "snapshot_interval_s (300 s when unknown). If neither a snapshot nor a lifecycle message has arrived "
        "for longer than the larger of 2x that interval and 5 minutes, also show \"Hub Offline\". The "
        "5-minute floor covers the first snapshot after a restart, which can come up to about 150 s after the "
        "lifecycle. On a 2.1.3 hub with no devices, which sends no snapshot and no lifecycle, only the IoT Hub "
        "connection state applies."))
    ctx.note("s6-v01: 8.2 push latency target applies while the hub is connected; late events carry their original ts")
    ctx.note("s6-25: 8.2 'Valve command confirmation' becomes 'Valve command acknowledgement': the ack does not wait for the BLE link; the move is confirmed separately")
    ctx.note("s6-26: 8.2 snapshot freshness gate per canon C14 (larger of 2x snapshot_interval_s and 5 minutes; a snapshot or lifecycle restarts it)")

    # ---- 8.3 reliability (s6-27, s6-v02, s6-28, new APP-NF-013/014/017/018) ----
    t34 = ctx.T(34, "Req ID | Priority | Requirement | Acceptance Criteria")
    set_cell(ctx.cell(34, 1, 2, "The app shall handle the hub being offline gracefully"), (
        "The app shall handle the hub being offline gracefully. Online/offline status shall be determined "
        "primarily from the Azure IoT Hub device-connection state; a disconnect followed by a reconnect within "
        "60 s is ignored (every hub reconnects about every 18 hours to renew its token, section 7.1). Snapshot "
        "freshness is a secondary gate: the hub also counts as offline when neither a snapshot nor a lifecycle "
        "message has arrived for longer than the larger of 2x snapshot_interval_s and 5 minutes "
        "(snapshot_interval_s as in force in twin reported; 300 s when unknown). On a 2.1.3 hub with no "
        "devices, which sends no snapshot and no lifecycle, only the IoT Hub connection state applies. When "
        "offline, show \"Hub Offline\" status."))
    set_cell(ctx.cell(34, 1, 3, "Offline shown when IoT Hub reports disconnected"), (
        "Offline shown when IoT Hub has reported the hub disconnected for more than 60 s, or after 10 min "
        "(default 2x 300 s) without a snapshot or lifecycle message. A token-renewal reconnect does not show "
        "\"Hub Offline\"."))
    set_cell(ctx.cell(34, 2, 2, "When the hub is offline, the app shall block the user from sending commands"), (
        "When the hub is offline, the app shall block the user from sending commands. The relevant controls "
        "shall be disabled and an informational message shown: \"Hub is offline - controls unavailable until "
        "it reconnects.\" Blocking sends keeps most commands from queuing in IoT Hub and running unexpectedly "
        "when the hub reconnects. It cannot catch a hub that dropped off a short time before (IoT Hub reports "
        "a silent drop only after about 90 s, and a disconnect is ignored for its first 60 s): a command sent "
        "then waits in IoT Hub and runs when the hub reconnects, because the hub does not check a command's "
        "age. The backend's 30-second expiry on valve_open, valve_close, valve_set_state and override_enable "
        "(section 7.1) limits this for valve commands. This is a pre-send gate; the 20s post-send command "
        "timeout in APP-FR-053 still applies if the hub drops after a command has been sent."))
    set_cell(ctx.cell(34, 3, 2, "The app shall not crash on unexpected null values"), (
        "The app shall not crash on null or missing values in snapshot and event fields. battery, rssi, "
        "last_seen_age_s, fw_version and lora_sensors[].snr can be null. A disconnected valve has no battery, "
        "leak_state, rmleak or fw_version keys. Since 2.1.4 a hub with no valve sends an empty valve object "
        "{} (a 2.1.3 hub sends {\"state\":\"disconnected\",\"connected\":false} with no valve_id). Since 2.1.4 "
        "an unknown battery is null, never 0, in snapshots, valve_state_changed and leak events; on 2.1.3 "
        "treat a valve battery of 0 as unknown. While valve.state is \"unknown\", ignore valve.rmleak and "
        "valve.leak_state."))
    set_cell(ctx.cell(34, 3, 3, "Null fields shown as"), (
        "Null or missing fields shown as \"--\" or \"Unknown\". A valve object without valve_id shows \"No "
        "valve set up\". No crash."))
    ctx.note("s6-27: APP-NF-010 offline gate per canon C14, with the 60 s reconnect debounce")
    ctx.note("s6-v02: APP-NF-011 says what the pre-send gate cannot catch and points to the 30 s valve-command expiry")
    ctx.note("s6-28: APP-NF-012 lists every null or missing field (empty valve object, null battery since 2.1.4, 2.1.3 zero battery)")

    # Row templates keep V4's banding (F5F5F5 / no fill, alternating) and priority colour
    # (P0 C62828 from T34, P1 E65100 from T35; the four T34/T35 columns are 2160 dxa each).
    r3 = ctx.row(34, 3, "APP-NF-012")                     # shaded, P0
    r2 = ctx.row(34, 2, "APP-NF-011")                     # unshaded, P0
    p1_shaded = ctx.row(35, 1, "APP-NF-015")              # shaded, P1
    p1_plain = ctx.row(35, 2, "APP-NF-016")               # unshaded, P1
    r = add_row(t34, ["APP-NF-013", "P0", (
        "The backend shall expect every D2C message at least once, and sometimes twice; a duplicate can "
        "arrive after newer messages. It shall drop a message only when its body is byte-identical to one "
        "already received (compare the raw body, or its SHA-256), keep the first copy, and never de-duplicate "
        "on a subset of fields such as gateway.id + ts + event. History rows and push notifications are "
        "created from the first copy only. One rare, harmless exception: an event raised before the hub's "
        "clock was first set can arrive twice with ts about 1 s apart; the backend may treat two messages that "
        "are identical except for a ts at most 1 s apart as one. At-least-once does not mean never lost: the "
        "hub keeps at most 16 unsent events, so after a long outage the next snapshot is the source of "
        "truth."), (
        "A leak_detected delivered twice gives one history row and one push. Two different cmd_acks with the "
        "same ts are both kept.")], like_row=r2, after_row=r3)
    r = add_row(t34, ["APP-NF-014", "P0", (
        "The backend and app shall order events by ts, not by arrival time, and shall not let an event older "
        "than the latest snapshot change the displayed state. After an outage the hub sends its buffered "
        "events (up to 16, oldest first) before that connection's lifecycle message, and a duplicate can "
        "arrive after newer events. A command's cmd_ack comes before the snapshot the command causes and, "
        "since 2.1.4, before its twin report (2.1.3 wrote the twin report first for provision, rules_config "
        "and set_hub_name)."), (
        "A replayed valve_state_changed older than the latest snapshot does not change the valve card. Event "
        "history shows buffered events at their original times.")], like_row=r3, after_row=r)
    r = add_row(t34, ["APP-NF-017", "P1", (
        "The backend shall ignore device_offline and device_recovered events for a device that is no longer "
        "provisioned: one removed by a decommission acked \"ok\", or one the latest snapshot no longer lists. "
        "It shall log them, and shall send no push and change no device list for them. A health event held on "
        "the hub can arrive after the cmd_ack of the decommission that removed its device."), (
        "A device_offline for a sensor removed a moment earlier sends no push, and the sensor does not "
        "reappear in the app.")], like_row=p1_plain, after_row=r)
    add_row(t34, ["APP-NF-018", "P1", (
        "The backend shall record a cmd_ack that arrives after the app's 20-second timeout (APP-FR-053) "
        "against its command, and shall not resolve that command a second time. The app shall not change its "
        "UI on a late ack: the next snapshot shows the state. A missing ack means the result is unknown, not "
        "that the command failed."), (
        "A cmd_ack that arrives 30 s after its command is logged with that command. The app UI changes only "
        "with the next snapshot.")], like_row=p1_shaded, after_row=r)
    ctx.note("s6-29: new APP-NF-013 (P0) at-least-once delivery, byte-identical dedupe")
    ctx.note("s6-30: new APP-NF-014 (P0) order events by ts; an event older than the latest snapshot does not change the displayed state (with the 2.1.3 twin order)")
    ctx.note("decision 25: new APP-NF-017 (P1) ignore health events for a removed device; new APP-NF-018 (P1) record a late cmd_ack, a missing ack means unknown")

    # ---- 8.4 logging (s6-31) ----
    ctx.T(35, "Req ID | Priority | Requirement | Acceptance Criteria")
    set_cell(ctx.cell(35, 2, 2, "The backend should log all events received"), (
        "The backend should log all D2C messages received, including message type, event name, gateway ID, "
        "ts, arrival time and the body hash used for de-duplication (APP-NF-013), and should log the "
        "duplicates it drops."))
    ctx.note("s6-31: APP-NF-016 logs ts and arrival time, the dedupe hash, and the dropped duplicates")


def apply(ctx):
    _six(ctx)
    _seven(ctx)
    _eight(ctx)
