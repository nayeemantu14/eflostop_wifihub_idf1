# ruff: noqa: E501
"""V5 edits: The V5 revision-history row (T0) and the 'Changes in V5 that need app or backend work' list after T0. Written last, by the integrator.

Addresses V4's original numbering through ctx (see common.py). Every edit: ctx.note(...).

Placement. In V4, T0 is followed by P13 (a paragraph holding only a page break), then the table of
contents (a content control, not a body paragraph, so it has no P number), P14 (empty) and the Heading 1
"1. Purpose and Scope". The change list goes directly after T0, before P13's page break, so it stays on
the Revision History pages, ahead of the table of contents.

The cached table of contents is not edited here: after every module has run, the builder rebuilds the
cached entries from V5's headings (_sync_cached_toc), so the new Heading 2 gets a TOC2 entry under
"Revision History". Page numbers there are approximate until the field is updated in Word."""
from .common import (EditError, add_row, delete_para, delete_row, insert_after,  # noqa: F401
                     insert_lines_after, insert_table_after, replace, replace_in_cell,
                     set_cell, set_text)

V5_DESCRIPTION = (
    "Documents hub firmware 2.1.4, the release the app ships against, with the 2.1.3 behaviour wherever "
    "the cloud can see a difference. Trigger mask rewritten (4.8.1): bit 1 is LoRa leak sensors, the "
    "default is 7, and the app changes it only with trigger_ble_leak and trigger_valve_flood. Identity "
    "keys corrected to valve_id and sensor_id. Delivery, cmd_ack, online/offline, empty-hub, battery, "
    "RMLEAK, QR label and hub-name rules corrected; new Appendix A.5. New APP-FR-086, 095-097, 117, 118; "
    "APP-NF-013, 014, 017, 018; TC-013 to TC-022, TC-N09, TC-N11 to TC-N18. \"Changes in V5\" below lists "
    "the work for the app and backend."
)

CHANGES_INTRO = (
    "An app or backend built to V4 needs the changes below. The trigger mask comes first, then the "
    "safety and data-loss items. Each item names the sections that hold the full rule. Items headed "
    "\"Backend\" are mainly backend work; the others are mainly app work, with any backend step marked "
    "\"Backend:\". Figures 1 to 6 and 8 to 11 predate V5 and are to be redrawn: where a figure and the "
    "text differ, the text is correct."
)

# At most 12 items: trigger mask first, then safety and data loss. Built from the section editors'
# app_actions, duplicates merged.
CHANGES = [
    "**Trigger mask.** The Settings screen changes a leak source only with rules_config and one key, "
    "trigger_ble_leak or trigger_valve_flood (true or false), and turns automatic shutoff on or off "
    "with auto_close_enabled alone. Never send trigger_mask or trigger_lora: V4's checkbox mapping sent "
    "5, which turns auto-close for LoRa leak sensors off. Read the toggle and the checkboxes from "
    "data.rules in the latest snapshot (default trigger_mask 7) and keep the last known values when a "
    "message has no rules. Do not let the user clear both checkboxes while automatic shutoff is on "
    "(sections 3.7, 4.8, 4.8.1).",

    "**Backend: provision.** Send top-level auto_close_enabled only in a hub's first provision, and only "
    "when the user chose a setting; never send it or a rules object in a later provision, because true "
    "sets the mask back to 7. Each device list in a provision replaces the hub's whole list, so always "
    "send the complete list. Send valve_id, not valve_mac (sections 4.8 APP-FR-097, 4.8.1, 5.3, 6.6).",

    "**Backend: command envelope.** Build every C2D command with a JSON library as an exact eflostop.cmd "
    "envelope with a string id of at most 63 characters, new for every send, retries included. A "
    "malformed message gets no cmd_ack and can still run an old plain-text command found in its text, "
    "even DECOMMISSION_ALL, which resets the hub (sections 5.3, 9).",

    "**Backend: duplicates and order.** Drop a message only when its body is byte-identical to one "
    "already received, and keep the first copy; never de-duplicate on gateway.id + ts + event. Order "
    "events and history by ts, never by arrival. An event older than the latest snapshot goes into "
    "history only. Push for late leak_detected and auto_close events too, adding their local time when "
    "ts is more than 5 minutes old (sections 5.2, 5.5, 8.3 APP-NF-013 and APP-NF-014, 4.10).",

    "**Identity keys and LoRa.** Read the valve as valve_id and every leak sensor as sensor_id, in "
    "snapshots, events and twin reported: V4's device_id is never sent, and health events carry "
    "source_type, not dev_type. Parse data.lora_sensors, lora_sensor_count and source_type \"lora\", and "
    "show LoRa leak sensors and their leaks read-only. Backend: accept their events and push their leaks "
    "(sections 2.4, 2.5, 5.2.2 to 5.2.6, 5.4.1).",

    "**Hub label QR.** Read the code as a query string (id=GW-...&type=hub&hw=...&sw=...), also when it "
    "follows a brand web address, and still accept a bare Gateway ID. Accept only type hub, check the ID "
    "against GW-[0-9A-Fa-f]{12} and convert it to upper case before the lookup "
    "(sections 3.8, 4.2, 6.4, 7.3).",

    "**Valve commands.** cmd_ack \"ok\" means queued: show \"Opening...\" or \"Closing...\" (\"Waiting for "
    "valve\" while the valve is disconnected) until valve_state_changed or a snapshot confirms it, and "
    "show the \"has not confirmed yet\" message after 60 s. Tell errors apart by error.detail, never by "
    "error.code; show the texts marked Show as sent, including the refusals new in 2.1.4. Backend: "
    "set a 30-second expiry on valve_open, valve_close, valve_set_state and override_enable "
    "(sections 4.5, 5.2.7, 5.5, 6.5).",

    "**Leak handling.** The hub clears RMLEAK by itself 10 to 12 s after every leak source is dry "
    "(rmleak_auto_cleared; 30 to 60 s on 2.1.3): enable Open Valve then, without waiting for a Leak "
    "Reset. Disable Leak Reset while any leak source is wet (APP-FR-038). Disable \"Open with 24h "
    "Override\" only while the valve flood probe is wet (valve.leak_state true) or the valve battery is 10 % "
    "or less (APP-FR-043): the override is how the user opens the valve while a leak sensor is still wet. "
    "A disconnected valve keeps its last known leak state in the banner (APP-FR-036). When an override "
    "ends or is cancelled while a source is wet and automatic shutoff is on, the valve closes with no "
    "auto_close event: take the valve from valve_state_changed and the snapshot (sections 4.4, 6.1, 6.2, "
    "6.3).",

    "**Online and offline.** Use the IoT Hub connection state first, and ignore a disconnect followed by "
    "a reconnect within 60 s (every hub renews its token about every 18 hours). As a second gate, treat "
    "the hub as offline after no snapshot or lifecycle for the larger of 2 x snapshot_interval_s and 5 "
    "minutes. Keep the Leak Detected banner while offline, marked \"Hub offline - last known state\" "
    "(sections 2.6, 3.5, 4.3, 7.1, 8.3).",

    "**No devices, no valve, not heard yet.** Decide \"no devices\" and \"no valve\" from the device lists "
    "(valve_id, the sensor arrays or counts), never from the provisioned flag. When the hub has no "
    "devices, show \"No devices set up yet\". When data.valve has no valve_id (\"valve\": {} since 2.1.4), "
    "show \"No valve set up\" and hide the valve controls; on 2.1.3 do the same while twin reported "
    "valve_id is null, because a 2.1.3 hub with no valve set up can link to and drive a nearby valve. "
    "Show \"Connecting...\" while valve.state is \"unknown\", \"--\" for a null battery (a valve battery of "
    "0 from a 2.1.3 hub means unknown) and a grey \"Not heard yet\" badge for a device not heard yet. "
    "Colour a battery orange at 20 % or less, and the valve battery red at 10 % or less "
    "(sections 2.6, 3.1, 4.2, 4.3, 4.7, 5.2.2).",

    "**Backend: push rules.** There is no health_alert event: push on device_offline, and ignore health "
    "events for a device the hub no longer lists. Add the valve battery push, driven by valve.battery in "
    "the snapshot because the hub sends no battery event, and the push when an override ends during a "
    "leak. Send one auto_close push per closing, and no second leak push for a device that is already "
    "wet (sections 4.7, 4.10, 5.2.4, 5.2.6, 8.3).",

    "**Backend: Device Twin and routing.** Set the hub name only through desired hub_name, never with "
    "set_hub_name. When a hub is reset or changes owner, set desired hub_name and snapshot_interval_s to "
    "null. Confirm a settings change when twin reported equals the value sent, not by reading the twin "
    "once when the cmd_ack arrives. The hub sets no "
    "message properties, so route the whole D2C stream and parse the body (sections 5.1, 5.4, 5.6, 7.1).",
]


def apply(ctx):
    t0 = ctx.T(0, "Change Description")

    # (a) V5 row. s1-02 deleted V4's R6 and R7 (canon C22), so V4's R5 is now the last row.
    v4_row = ctx.row(0, 5, "03/08/2026")
    add_row(t0, ["V5", ctx.DATE, V5_DESCRIPTION, ctx.AUTHOR, ""], like_row=v4_row, after_row=v4_row)
    ctx.note(f"V5 revision-history row after the V4 row (R5; s1 deleted R6 and R7): {ctx.DATE}, hub firmware {ctx.FW} with the {ctx.FW_FIELD} differences, trigger mask first")

    # (b) The change list, after T0 and before P13's page break.
    head = insert_after(t0, "Changes in V5 that need app or backend work", style="Heading 2")
    intro = insert_after(head, CHANGES_INTRO, style="Normal")
    if len(CHANGES) > 12:
        raise EditError("the V5 change list has more than 12 items")
    insert_lines_after(intro, CHANGES, style="List Bullet", rich=True)
    ctx.note(f"new Heading 2 'Changes in V5 that need app or backend work' after T0: an intro and {len(CHANGES)} items, trigger mask first, then safety and data loss, each naming its sections")
