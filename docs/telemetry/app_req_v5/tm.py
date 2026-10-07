# ruff: noqa: E501
"""V5 edits: 4.8 Settings and 4.8.1 Trigger Mask: V4 P172-P173, tables T15-T16.

Addresses V4's original numbering through ctx (see common.py). Every edit: ctx.note(...).

T15 (4.8 requirements): APP-FR-091, 092, 093 (acceptance only) and 094 rewritten; APP-FR-095,
096 and 097 added after APP-FR-094 (id_map.json). T16 and its heading become the 4.8.1 Trigger
Mask section of final/tm_section.md, with the canon.md wording (C2 terms, C3, C7, C20 LoRa row).
Row banding: V4 tables shade odd body rows (F5F5F5) and leave even ones plain, so every new
row copies the row whose shading its position needs."""
from .common import (EditError, add_row, colour_priority, delete_para, delete_row,  # noqa: F401
                     insert_after, insert_lines_after, insert_table_after, replace, replace_in_cell,
                     set_cell, set_text)


# ---------------------------------------------------------------- 4.8 requirements (T15)

FR091_REQ = ('The app shall show an auto-close toggle labelled "Automatic shutoff" (on/off), read from '
             'data.rules.auto_close_enabled in the latest snapshot. Changing it sends rules_config with only '
             'auto_close_enabled.')
FR091_ACC = ('Turning it off sends {"auto_close_enabled":false}. After cmd_ack "ok", a snapshot within a few '
             'seconds shows data.rules.auto_close_enabled=false and trigger_mask unchanged. Outside a pending change, '
             'the toggle follows the latest data.rules: since 2.1.4, removing the hub\'s last device sets the rules '
             'back to the default (on, trigger_mask 7); up to 2.1.3 the hub kept its last rules (after a '
             'decommission, until its next restart; after a provision, also across restarts).')

FR092_REQ = ('The app shall show two leak source checkboxes, "BLE leak sensors" (bit 0) and "Valve flood probe" '
             '(bit 2), read from data.rules.trigger_mask in the latest snapshot. Changing one sends rules_config '
             'with only trigger_ble_leak or trigger_valve_flood (true or false). The app shall never send '
             'trigger_mask or trigger_lora (section 4.8.1).')
FR092_ACC = ('Unchecking "Valve flood probe" on a hub at trigger_mask 7 sends {"trigger_valve_flood":false}. '
             'After cmd_ack "ok", a snapshot within a few seconds shows trigger_mask=3. Bit 1 never changes.')

FR093_ACC = ('Choosing 10 minutes writes desired snapshot_interval_s=600. The hub applies it at once and keeps it '
             'across restarts: twin reported.snapshot_interval_s, the interval in force, shows 600, also after a '
             'hub restart. A value outside 60-3600 s is rejected, and reported keeps the old value.')

FR094_REQ = ('Settings changes shall show a pending state until the hub confirms them. For rules_config: after '
             'cmd_ack "ok", the app shall keep the pending state until a snapshot\'s data.rules shows the value sent '
             '(normally within a few seconds), then show it. A snapshot sent just after the ack can still show the '
             'old value: the app shall not revert the control on it. If no snapshot shows the value within 60 s '
             'of the ack, the app shall end the pending state and show the values in the latest data.rules. On '
             '"error", it shall follow APP-FR-054 and keep '
             'the previous values; with no cmd_ack, it shall follow APP-FR-053. The snapshot interval is a Device '
             'Twin desired property and gets no cmd_ack: it is confirmed when twin reported.snapshot_interval_s '
             'equals the value written. While the hub is offline the change stays pending; the hub applies the '
             'desired value when it reconnects. The app and backend shall not depend on the order of the cmd_ack, '
             'the twin update and the snapshot, and shall wait until a reported twin value equals the value sent '
             'rather than read the twin once when the ack arrives.')
FR094_ACC = ('Pending state until confirmed. A snapshot with the old value just after the ack does not revert the '
             'control. The UI then matches data.rules, or twin reported.snapshot_interval_s for the interval. '
             'Without a confirming snapshot, the pending state ends 60 s after the ack.')

NEW_ROWS = [  # (values, (V4 row whose shading the position needs, its expect))
    (["APP-FR-095", "P1",
      'While data.rules.auto_close_enabled is false, the app should show the leak source checkboxes as inactive, '
      'with the note "Automatic shutoff is off. Leaks are still reported." Their values are kept.',
      'Checkboxes inactive while auto-close is off. Values unchanged after auto-close is turned back on.'],
     (3, "APP-FR-093")),
    (["APP-FR-096", "P1",
      'The app should not let the user clear both leak source checkboxes while auto-close is on. When the user '
      'clears the last checked one, the app should offer to turn "Automatic shutoff" off instead.',
      'Clearing the last checkbox shows the offer. Accepting sends {"auto_close_enabled":false}.'],
     (4, "APP-FR-094")),
    (["APP-FR-097", "P0",
      'The backend shall send top-level auto_close_enabled in a provision payload only in the first provision of '
      'a hub, and only when the user chose a setting. The first provision after decommission target "all", or '
      'after the hub\'s last device was removed, counts as a first provision. It shall not send '
      'auto_close_enabled or a rules object in any later provision that adds or removes devices. The MVP app '
      'asks the user no such question at setup (sections 3.8 and 6.6): unless the backend\'s own setup flow '
      'records a choice, the backend leaves auto_close_enabled out, the hub starts with auto-close on and '
      'trigger_mask 7, and the user changes it later in Settings.',
      'A provision that adds a sensor after setup leaves data.rules.auto_close_enabled and trigger_mask '
      'unchanged.'],
     (3, "APP-FR-093")),
]


def _settings_table(ctx):
    ctx.T(15, "Req ID | Priority | Requirement | Acceptance Criteria")

    set_cell(ctx.cell(15, 1, 2, "auto-close toggle (on/off)"), FR091_REQ)
    set_cell(ctx.cell(15, 1, 3, "Toggle sends command."), FR091_ACC)
    ctx.note('s2-39, tm-12: APP-FR-091 - toggle labelled "Automatic shutoff", read from data.rules, sends only '
             'auto_close_enabled; acceptance names the confirming field and the 2.1.4 empty-hub rules reset')

    set_cell(ctx.cell(15, 2, 2, "trigger mask checkboxes"), FR092_REQ)
    set_cell(ctx.cell(15, 2, 3, "Checkboxes map to bitmask."), FR092_ACC)
    ctx.note('tm-13: APP-FR-092 - per-source booleans trigger_ble_leak / trigger_valve_flood; never trigger_mask '
             'or trigger_lora (V4 "map to bitmask" sent 5 and cleared the LoRa bit)')

    set_cell(ctx.cell(15, 3, 3, "Slider sends twin update."), FR093_ACC)
    ctx.note('s2-40: APP-FR-093 acceptance - applied at once, persisted, confirmed by twin '
             'reported.snapshot_interval_s; values outside 60-3600 s rejected')

    set_cell(ctx.cell(15, 4, 2, "twin acknowledgment"), FR094_REQ)
    set_cell(ctx.cell(15, 4, 3, "pending state until confirmed"), FR094_ACC)
    ctx.note('s2-41, tm-14: APP-FR-094 - no "twin acknowledgment": rules_config confirmed by cmd_ack then a '
             'snapshot whose data.rules shows the value sent (a snapshot just after the ack can still show the old '
             'value: no revert), no cmd_ack per APP-FR-053; snapshot interval by twin reported (no cmd_ack, pending '
             'while the hub is offline); no dependence on ack/twin/snapshot order')

    t15 = ctx.T(15, "Req ID | Priority | Requirement | Acceptance Criteria")
    after = ctx.row(15, 4, "APP-FR-094")
    for values, (like, like_exp) in NEW_ROWS:
        after = add_row(t15, values, like_row=ctx.row(15, like, like_exp), after_row=after)
        colour_priority(after)
    ctx.note("tm-15: new APP-FR-095 (P1, checkboxes inactive while auto-close is off), APP-FR-096 (P1, no empty "
             "leak source selection while auto-close is on), APP-FR-097 (P0, backend sends auto_close_enabled only "
             "in a hub's first provision; the first after decommission \"all\" or after the last device was removed "
             "counts) after APP-FR-094")
    ctx.note("global fix INT-05: APP-FR-095..097 priority text coloured by V4's house style (P0 red, P1 orange); "
             "the copied rows carried their template row's colour")
    ctx.note("global fix XC-09, XC-16: APP-FR-094 ends a rules_config pending state 60 s after the ack without a "
             "confirming snapshot, and waits for a reported twin value equal to the value sent (2.1.3 writes the "
             "twin before the ack, so a change from the value read at the ack never comes)")
    ctx.note("global fix XC-13: APP-FR-097 says the MVP app asks no setup question, so without a backend-recorded "
             "choice the backend leaves auto_close_enabled out; 4.8.1 'Default and persistence' points to the first "
             "provision")


# ---------------------------------------------------------------- 4.8.1 Trigger Mask (P173, T16)

INTRO = ("The trigger mask is a number stored on the hub. It selects which kinds of leak source can make the hub "
         "close the valve on its own (auto-close). Only bits 0, 1 and 2 are used. auto_close_enabled is the master "
         "switch above it: while auto_close_enabled is false, the hub never closes the valve on its own, whatever "
         "the mask says. Every leak is still reported to the cloud, whatever either setting says.")

BITS_HEADER_SOURCE = "Source (source_type)"
V4_T16 = {  # expect guards: V4's original T16 body cells, per row and distinct cell
    1: ["0", "1", "BLE leak sensors", "When checked, BLE sensor leaks"],
    2: ["2", "4", "Valve flood probe", "When checked, the valve built-in"],
    3: ["Both", "5", "All sources", "Default. Both sensor types"],
}
BITS = [
    ["0", "1", 'BLE leak sensors ("ble_leak_sensor")',
     "When set, a wet report from a provisioned BLE leak sensor makes the hub assert RMLEAK and close the valve."],
    ["1", "2", 'LoRa leak sensors ("lora")',
     "The same, for a provisioned LoRa leak sensor. The MVP app shows LoRa leak sensors read-only and never "
     "changes this bit."],
    ["2", "4", 'Valve flood probe ("valve")',
     "The same, when the valve flood probe reports water."],
    ["All", "7", "BLE leak sensors, LoRa leak sensors and the valve flood probe",
     "Default. All three kinds of leak source can trigger auto-close."],
]

AFTER_TABLE = ("A set bit acts only while auto_close_enabled is true and no 24-hour override window is active. "
               "During a window the hub sends auto_close_blocked_override instead and the valve stays open.")

HOW_APP = [
    'Read the settings from data.rules in the latest snapshot. The "BLE leak sensors" checkbox is bit 0 '
    '(trigger_mask AND 1). The "Valve flood probe" checkbox is bit 2 (trigger_mask AND 4). While '
    "data.rules.auto_close_enabled is false, show the checkboxes as inactive (APP-FR-095).",
    "To change a setting, send rules_config with only the key that changed: auto_close_enabled, trigger_ble_leak "
    "(bit 0) or trigger_valve_flood (bit 2). Each is a JSON boolean. For the trigger keys, true sets the bit and "
    "false clears it; every other bit stays as it is. Keys left out are not changed. auto_close_enabled never "
    "changes the mask, and the trigger keys never change auto_close_enabled.",
    "The app shall never send trigger_mask or trigger_lora. trigger_mask replaces all bits at once: a value built "
    "from the two checkboxes (1 + 4 = 5) turns LoRa leak sensors off without the user knowing.",
    "The app shows nothing for bit 1. Backend: if a hub reports bit 1 clear (data.rules.trigger_mask AND 2 is 0), "
    "for example after a build made to V4 sent trigger_mask 5, restore it with one rules_config "
    "{\"trigger_lora\": true} from a support tool. This is the only use of trigger_lora.",
    "To stop automatic closing, send auto_close_enabled false. Clearing the checkboxes does not stop it: the LoRa "
    'bit stays set, and even a mask of 0 does not stop the closes described under "When the hub does not check '
    'the mask" below.',
    "Hub firmware 2.1.3 and 2.1.4 handle all of these keys the same way.",
]

EXAMPLE_LEAD = 'Example: the user unchecks "Valve flood probe".'
EXAMPLE_JSON = ('{"schema":"eflostop.cmd","ver":1,"id":"rc-004","cmd":"rules_config",'
                '"payload":{"trigger_valve_flood":false}}')

SENDS_BACK = [
    'cmd_ack with status "ok" once the new values are saved. The hub sends status "error", error.code '
    '"rules_config" and error.detail "rules config update failed" only when the payload is missing or null, or '
    "when it could not save the change. A payload that changes nothing is not an error. After an error the hub "
    "sends only the cmd_ack.",
    'After "ok", the hub sends a Device Twin reported update (auto_close_enabled, trigger_mask) and a snapshot '
    "with data.rules, normally within a few seconds. Since 2.1.4 the cmd_ack is sent first; up to 2.1.3 the twin "
    "update was sent before the cmd_ack. The app and backend shall not depend on the order. On a slow link the "
    "twin can briefly show an older value. A snapshot sent just after the cmd_ack can still show the old values; "
    "the next one shows the change. The snapshot's data.rules is the reference.",
    "The mask is reported in three places, each next to auto_close_enabled: snapshot data.rules.trigger_mask, "
    "lifecycle data.rules.trigger_mask, and twin reported trigger_mask (top level of the reported properties). If "
    "the hub cannot read its rules within 1 s, it leaves them out of that message: keep the last known values.",
    "Device Twin desired properties cannot set auto_close_enabled or trigger_mask. The hub ignores those keys "
    "there.",
]

HUB_DOES = [
    "**When the mask is checked.** Each time a leak report arrives from a provisioned leak source, the hub checks "
    "auto_close_enabled, the source's bit and the override window. A wet report from a source whose bit is clear "
    "does not latch a leak incident: no auto_close, no auto_close_blocked_override, no RMLEAK.",
    "**When the hub does not check the mask.** Three other moments close the valve without checking the mask: "
    "the valve reconnecting to the hub (for example after a BLE dropout or a hub restart), an override_cancel, and "
    "the end of a 24-hour override window. At each of them, if auto_close_enabled is true and any provisioned leak "
    "source is wet, the hub asserts RMLEAK and closes the valve, even when that source's bit is clear. A "
    "reconnect while an override window is active closes nothing. The "
    'reconnect close sends auto_close with cause "reconnect". The other two send no auto_close (section 4.4.2). '
    "Hub firmware 2.1.3 and 2.1.4 both behave this way.",
    "**A source with its bit clear still counts as wet.** leak_reset is refused while any provisioned leak source "
    "is wet, and the automatic RMLEAK clear waits until every provisioned leak source is dry, whatever the mask.",
    "**What the mask never affects.** leak_detected and leak_cleared events, leak_state in the snapshot, the "
    "device's \"critical\" health rating while wet, and the leak push (APP-FR-111).",
    "**When a change takes effect.** From the next leak report. The hub does not re-check sources that are "
    "already wet when the change arrives. A wet source whose bit is switched on (or a wet source when "
    "auto_close_enabled is switched on) closes the valve at its next wet report: within about 5 minutes for a "
    "BLE leak sensor, at its next packet for a LoRa leak sensor, and at the next probe change or valve reconnect "
    "for the valve flood probe.",
    "**Switching off never opens.** Clearing a bit, or setting auto_close_enabled false, never opens the valve, "
    "never clears RMLEAK and never cancels a close the hub is still waiting to deliver to an unreachable valve. A "
    "locked valve stays locked until leak_reset, the automatic RMLEAK clear (10 to 12 s after every leak source "
    "is dry since 2.1.4; 30 to 60 s up to 2.1.3) or the 24-hour override.",
    "**Default and persistence.** The default is auto_close_enabled true and trigger_mask 7. The hub stores both "
    "values. They survive a restart, a power cut and a Wi-Fi reset. The hub goes back to the default after "
    'decommission target "all", and, since 2.1.4, when a decommission or a provision leaves it with no device '
    "(up to 2.1.3 such a hub kept its last rules: after a decommission, until its next restart; after a provision, "
    "also across restarts). On a hub that is not provisioned "
    "(lifecycle data.provisioned false), a rules_config lasts only until the next restart. Send a setup choice, "
    "when there is one, in the first provision instead (APP-FR-097).",
    "**The valve.** If the valve also acts on its own flood probe, that is valve firmware behaviour. The trigger "
    "mask does not control it, and this document does not specify it.",
    "**No validation.** The hub does not range-check trigger_mask. It keeps the low 8 bits of the integer part "
    "(256 becomes 0, -1 becomes 255) and ignores a value that is not a JSON number. For auto_close_enabled and the "
    'trigger keys, only JSON true and false count: 1, "true" and null are ignored. All of these get cmd_ack "ok". '
    "If a payload holds both trigger_mask and a trigger key, the trigger key wins for its bit.",
]

AT_SETUP = [
    "Top-level auto_close_enabled true turns auto-close on and sets the mask to 7. false turns auto-close off and "
    "leaves the mask unchanged. Leave the key out, or send null, when the user did not choose. A new hub is "
    "already on, with mask 7.",
    "Send auto_close_enabled only in the first provision of a hub (APP-FR-097). A later provision that carries "
    "auto_close_enabled true sets the mask back to 7 and undoes the user's choices. The first provision after "
    'decommission target "all", or after the hub\'s last device was removed, counts as a first provision.',
    'A rules object ({"auto_close_enabled": true or false, "trigger_mask": number}) is applied after the '
    "top-level key and wins over it. In provision, trigger_mask is read only inside rules, and the trigger keys "
    "(trigger_ble_leak and the others) are not read at all. The backend never sends rules.trigger_mask 5: it "
    "turns LoRa leak sensors off.",
    "Always send rules keys together with devices. A provision that holds only rules keys marks a hub with no "
    "devices as provisioned.",
]

MAC = '"00:80:E1:27:B4:96"'
EXAMPLES_HEADER = ["Mask before", "Command and payload", "Mask after", "Note"]
EXAMPLES = [
    ["7", 'rules_config {"trigger_ble_leak": false}', "6", "LoRa and valve flood probe stay on."],
    ["6", 'rules_config {"trigger_valve_flood": false}', "2", "Only the LoRa bit is left."],
    ["2", 'rules_config {"trigger_ble_leak": true, "trigger_valve_flood": true}', "7",
     "Two keys in one payload also work. The app sends one key per change."],
    ["7", 'rules_config {"auto_close_enabled": false}', "7", "Mask kept. The hub closes nothing on its own."],
    ["7", 'rules_config {"trigger_mask": 5}', "5", "LoRa turned off. The app shall not send this."],
    ["any", 'rules_config {"trigger_mask": 0, "trigger_valve_flood": true}', "4", "The trigger key wins for bit 2."],
    ["7", 'rules_config {"trigger_mask": 256}', "0", 'No validation. cmd_ack "ok".'],
    ["7", 'rules_config with "payload": null', "7", 'cmd_ack "error", error.detail "rules config update failed".'],
    ["1", 'provision {"ble_leak_sensors": [' + MAC + '], "auto_close_enabled": true}', "7",
     "Auto-close on, and the user's mask is lost. This is why the backend sends auto_close_enabled only in a "
     "hub's first provision (APP-FR-097)."],
    ["7", 'provision {"ble_leak_sensors": [' + MAC + '], "auto_close_enabled": true, "rules": {"trigger_mask": 3}}',
     "3", "rules wins. Auto-close on."],
    ["5", 'provision {"ble_leak_sensors": [' + MAC + '], "trigger_ble_leak": false}', "5",
     "Trigger keys are not read in provision."],
]


def _bullets(anchor, lines, rich=False):
    return insert_lines_after(anchor, lines, style="List Bullet", rich=rich)


def _lead(anchor, text):
    """A sub-topic lead-in: a bold Normal paragraph, as V4 P171."""
    return insert_after(anchor, f"**{text}**", style="Normal", rich=True)


def _trigger_mask(ctx):
    head = ctx.P(173, "4.8.1 Trigger Mask Bits")
    set_text(head, "4.8.1 Trigger Mask")
    insert_after(head, INTRO, style="Normal")
    ctx.note("tm-16: 4.8.1 retitled \"Trigger Mask\"; intro paragraph: what the mask selects, auto_close_enabled "
             "as the master switch, every leak still reported")

    t16 = ctx.T(16, "Bit | Decimal Value | Source | Description")
    set_cell(ctx.cell(16, 0, 2, "Source"), BITS_HEADER_SOURCE)
    # V4 R1 (shaded) -> bit 0, R2 (plain) -> bit 1, R3 (shaded) -> bit 2, a copy of R2 (plain) -> All.
    for r, values in ((1, BITS[0]), (2, BITS[1]), (3, BITS[2])):
        for c, v in enumerate(values):
            set_cell(ctx.cell(16, r, c, V4_T16[r][c]), v)
    add_row(t16, BITS[3], like_row=ctx.row(16, 2, "Valve flood probe"), after_row=ctx.row(16, 3, "Both"))
    ctx.note("tm-16: T16 bit table rewritten - bit 1 (2, LoRa leak sensors, read-only in the MVP app, never "
             "changed) added; source_type per bit; default corrected from 5 to 7 (it was always 7)")

    p = insert_after(t16, AFTER_TABLE, style="Normal")
    p = _lead(p, "How the app reads and changes the mask")
    p = _bullets(p, HOW_APP)
    p = insert_after(p, EXAMPLE_LEAD, style="Normal")
    p = ctx.code_after(p, [EXAMPLE_JSON])
    ctx.note("tm-16: 4.8.1 how the app reads (data.rules, AND 1 / AND 4) and writes (one boolean key per change; "
             "never trigger_mask or trigger_lora; auto_close_enabled false is the only way to stop closes), with a "
             "rules_config example")
    ctx.note("global fix XC-18: 4.8.1 says how a hub whose bit 1 is already clear gets it back: one rules_config "
             "{\"trigger_lora\": true} from a backend support tool (rules_engine.c:1173-1179); the app never sends it")

    p = _lead(p, "What the hub sends back")
    p = _bullets(p, SENDS_BACK)
    ctx.note("tm-16: 4.8.1 cmd_ack ok/error, twin report and snapshot after ok (cmd_ack first since 2.1.4, twin "
             "first up to 2.1.3), the three places the mask is reported, desired properties cannot set it")

    p = _lead(p, "What the hub does")
    p = _bullets(p, HUB_DOES, rich=True)
    ctx.note("tm-16: 4.8.1 hub behaviour - when the mask is checked, the three closes that ignore it (valve "
             "reconnect, override_cancel, override expiry), clear-bit sources still count as wet, what the mask never "
             "affects, timing, switching off never opens, default 7 and persistence (2.1.4 empty-hub reset), valve "
             "firmware out of scope, no validation")

    p = _lead(p, "At setup (provision, sent by the backend)")
    p = _bullets(p, AT_SETUP)
    ctx.note("tm-16: 4.8.1 provision opt-in - top-level auto_close_enabled (true sets mask 7), first provision only "
             "(APP-FR-097, including the first after a reset or after the last device was removed), rules object "
             "wins, never rules.trigger_mask 5, trigger keys not read, rules keys always with devices")

    p = _lead(p, "Worked examples")
    ex = insert_table_after(p, t16, EXAMPLES_HEADER, [EXAMPLES[0]])
    # Body template of insert_table_after is the shaded row: alternate with the new table's own rows.
    for i, values in enumerate(EXAMPLES[1:], start=2):
        if i == 2:
            add_row(ex, values, like_row=ctx.row(16, 2, "Valve flood probe"), after_row=None)
        else:
            add_row(ex, values, like_row=ex.rows[1 if i % 2 else 2], after_row=None)
    ctx.note("tm-16: 4.8.1 worked-examples table (11 rows: rules_config and provision, mask before and after)")


def apply(ctx):
    _settings_table(ctx)
    _trigger_mask(ctx)
