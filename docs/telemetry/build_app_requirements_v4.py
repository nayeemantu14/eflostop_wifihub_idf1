#!/usr/bin/env python3
# ruff: noqa: E501
"""Produce V4 of the Watts Digital app-requirements document from V3.

V3 documents the firmware-1.8.0 wire format. Firmware 1.9.0 consolidated the valve's own
flood-probe events into the leak family and renamed the identity key, so §2.5, §5.2.x and
§5.4.1 no longer match what the hub emits.

This EDITS A LOADED COPY of V3 and saves under a new name. It does not regenerate the
document, because V3 carries 11 embedded images, 45 tables and hand-applied styling that a
regeneration would lose. V3 itself is never written to — the script refuses to run if the
output path equals the input path.

Scope of change, deliberately narrow:
  - outbound (D2C + twin) identity key  sensor_id / valve.mac / valve_mac -> device_id
  - valve flood events folded into leak_detected / leak_cleared with source_type "valve"
  - dev_type "ble_leak" -> "ble_leak_sensor" (the 1.8.0 rename V3 predates)
  - device ids stated as always uppercase

INBOUND command payloads keep their KEY names — sensor_id and valve_mac — because that contract
did not change and the app already sends them. Every such site is listed in KEEP_INBOUND below so
the intent is explicit rather than an omission.

Inbound device-type VALUES are canonicalised to "ble_leak_sensor" though: "ble" is a legacy alias
and the document's own guidance already says new integrations should send the canonical spelling,
so the examples were teaching the opposite of the advice. Nothing breaks — "ble" and "ble_leak"
are accepted forever (sensor_meta.h, sensor_type_is_ble_leak).

Run: python docs/telemetry/build_app_requirements_v4.py
"""
import os
import shutil
import sys

from docx import Document
from docx.shared import Pt

DOCS = r"C:\Work\Projects\EfloStop 2\Documents"
SRC = os.path.join(DOCS, "eFloStop_2_App_Requirements_Watts_Digital_Update-V3.docx")
DST = os.path.join(DOCS, "eFloStop_2_App_Requirements_Watts_Digital_Update-V4.docx")

FW = "1.9.0"
DATE = "03/08/2026"
AUTHOR = "Nayeem A"

# Sites that must NOT be touched: inbound C2D payloads still use sensor_id / valve_mac.
KEEP_INBOUND = [
    "p393  decommission command example",
    "p399  sensor_meta command example",
    "t19r1c2  decommission requirement text",
    "t28r6c1 / r6c3  decommission payload + errors",
    "t28r8c1 / r8c3  sensor_meta payload + errors",
    "t28r9c1  provision payload (valve_mac)",
]


# ---------------------------------------------------------------- text helpers

def set_para_text(p, text):
    """Replace a paragraph's text, keeping its style and the first run's formatting."""
    runs = p.runs
    if not runs:
        p.add_run(text)
        return
    runs[0].text = text
    for r in runs[1:]:
        r.text = ""


def sub_in_para(p, old, new):
    """Substring replace inside a paragraph, tolerating a needle split across runs."""
    if old not in p.text:
        return False
    for r in p.runs:
        if old in r.text:
            r.text = r.text.replace(old, new)
            return True
    set_para_text(p, p.text.replace(old, new))
    return True


def sub_in_cell(cell, old, new):
    hit = False
    for p in cell.paragraphs:
        if sub_in_para(p, old, new):
            hit = True
    return hit


def set_cell_text(cell, text):
    set_para_text(cell.paragraphs[0], text)
    for p in cell.paragraphs[1:]:
        set_para_text(p, "")


def insert_after(p, text, style=None):
    """Insert a new paragraph directly after `p`; returns the new paragraph."""
    new_p = p.insert_paragraph_before(text, style=style)
    p._p.addnext(new_p._p)
    return new_p


def insert_block_after(p, lines, style=None):
    """Insert several paragraphs after `p`, preserving order. Returns the last one."""
    cur = p
    for line in lines:
        cur = insert_after(cur, line, style=style)
    return cur


# ---------------------------------------------------------------- the edits

def main():
    if os.path.abspath(SRC) == os.path.abspath(DST):
        raise SystemExit("refusing to overwrite the source document")
    if not os.path.exists(SRC):
        raise SystemExit(f"source not found: {SRC}")

    doc = Document(SRC)
    paras = doc.paragraphs          # index-stable snapshot, taken before any insert
    tables = doc.tables
    changes = []

    def note(what):
        changes.append(what)

    # ---- §2.5 IDs and Naming (table 4) ----
    t = tables[4]
    set_cell_text(t.rows[2].cells[0], "valve.device_id")
    set_cell_text(t.rows[2].cells[2],
                  "BLE MAC address string for the valve. Was valve.mac before firmware 1.9.0.")
    set_cell_text(t.rows[3].cells[0], "ble_leak_sensors[].device_id")
    set_cell_text(t.rows[3].cells[2],
                  "BLE MAC address string for a leak sensor. Was sensor_id before firmware 1.9.0.")
    note("§2.5 identity rows renamed to device_id")

    # ---- §5.2.1 lifecycle ----
    sub_in_para(paras[208], '"valve_mac"', '"valve_device_id"')
    sub_in_cell(tables[22].rows[4].cells[0], "data.valve_mac", "data.valve_device_id")
    sub_in_cell(tables[22].rows[4].cells[2],
                "Valve BLE MAC. Present only if a valve is provisioned.",
                "Valve BLE MAC, uppercase. Present only if a valve is provisioned. "
                "Was data.valve_mac before firmware 1.9.0.")
    note("§5.2.1 lifecycle valve_mac -> valve_device_id")

    # ---- §5.2.2 snapshot ----
    sub_in_para(paras[230], '"mac"', '"device_id"')
    sub_in_para(paras[242], '"sensor_id"', '"device_id"')
    sub_in_para(paras[253], '"sensor_id"', '"device_id"')
    t = tables[23]
    set_cell_text(t.rows[3].cells[0], "data.valve.device_id")
    set_cell_text(t.rows[3].cells[2],
                  "Valve BLE MAC address, uppercase. Was data.valve.mac before firmware 1.9.0.")
    set_cell_text(t.rows[12].cells[0], "data.ble_leak_sensors[].device_id")
    set_cell_text(t.rows[12].cells[2],
                  'BLE MAC address string like "00:80:E1:27:B4:96", always uppercase. '
                  "Was ...[].sensor_id before firmware 1.9.0.")
    note("§5.2.2 snapshot valve.mac + array sensor_id -> device_id")

    # ---- §5.2.3 valve event: position changes only ----
    set_para_text(paras[269], "5.2.3 Valve Position Event")
    set_para_text(paras[270],
                  "Sent when the valve opens or closes. Water at the valve's own flood probe is "
                  "NOT reported here - since firmware 1.9.0 it is a water-detection event (see 5.2.4).")
    # Add the identity pair to the example, after "event": "valve_state_changed".
    # Four spaces: matches the surrounding keys inside the data object.
    insert_block_after(paras[277], [
        '    "source_type": "valve",',
        '    "device_id": "00:80:E1:27:9A:E6",',
    ], style="CodeBlock")
    set_para_text(paras[285],
                  "Valve position event names: valve_state_changed only. The former "
                  "valve_flood_detected and valve_flood_cleared events NO LONGER EXIST - water at the "
                  "valve probe now arrives as leak_detected / leak_cleared with source_type \"valve\". "
                  "Valve connect/disconnect transitions are reported via the health-alert event "
                  "(see 5.2.6), not as separate valve events.")
    note("§5.2.3 retitled, identity pair added, flood events removed")

    # ---- §5.2.4 leak sensor event -> water detection event ----
    set_para_text(paras[286], "5.2.4 Water Detection Event (all sources)")
    set_para_text(paras[287],
                  "ONE event family for \"water was detected somewhere\", from any source. Since "
                  "firmware 1.9.0 this includes the valve's own flood probe, which previously had its "
                  "own event names and an unrelated payload shape. Use data.source_type to tell the "
                  "sources apart; a single handler covers all three.")
    sub_in_para(paras[296], '"sensor_id"', '"device_id"')
    # Key ORDER matters: the document claims to reproduce wire order, and location is part of
    # the required core (emitted 6th, by add_location_for_source) while rssi is a source-specific
    # extra appended after it. V3 had them the other way round. Confirmed against a live capture
    # on 2026-08-03: all 14 leak events carried location 6th, rssi 7th.
    set_para_text(paras[299],
                  '    "location": { "code": "laundry", "label": "Behind washer" },')
    set_para_text(paras[300], '    "rssi": -72')
    set_para_text(paras[304],
                  "Event names: leak_detected, leak_cleared. data.leak_state always agrees with the "
                  "event name - true on leak_detected, false on leak_cleared.")
    set_para_text(paras[305],
                  "Required core, identical keys in identical order for every source: event, "
                  "source_type, device_id, leak_state, battery, location. Source-specific keys follow "
                  "it. source_type is \"ble_leak_sensor\", \"lora\" or \"valve\". BLE and LoRa sensors "
                  "add rssi. The valve instead adds valve_state, rmleak and fw_version, and carries no "
                  "rssi because it is a GATT link rather than an advertisement. location is always "
                  "present for every source; the valve reports {\"code\":\"unknown\",\"label\":\"\"} "
                  "because valve locations cannot yet be commissioned.")
    # The sensor example already present above becomes the first of two. Add the valve
    # variant beneath it so the shared core can be compared line by line.
    lead = insert_after(
        paras[305],
        "Example: the valve's own flood probe went wet. The first six keys are identical to the "
        "sensor example above, in the same order; only the trailing source-specific keys differ.")
    insert_block_after(lead, [
        "{",
        '  "schema": "eflostop.v2",',
        '  "ts": 1770589504,',
        '  "gateway": { "id": "GW-34B7DA6AAD54", "fw": "1.9.0", "uptime_s": 1074 },',
        '  "type": "event",',
        '  "data": {',
        '    "event": "leak_detected",',
        '    "source_type": "valve",',
        '    "device_id": "00:80:E1:27:9A:E6",',
        '    "leak_state": true,',
        '    "battery": 92,',
        '    "location": { "code": "unknown", "label": "" },',
        '    "valve_state": "open",',
        '    "rmleak": false,',
        '    "fw_version": "2.2.0"',
        "  }",
        "}",
    ], style="CodeBlock")
    note("§5.2.4 rewritten as the unified water-detection event, valve example added")

    # ---- §5.2.5 rules events ----
    sub_in_cell(tables[24].rows[1].cells[2], "sensor_id", "device_id")
    sub_in_cell(tables[24].rows[2].cells[2], "sensor_id", "device_id")
    sub_in_para(paras[318], '"sensor_id"', '"device_id"')
    note("§5.2.5 rules events sensor_id -> device_id")

    # ---- §5.2.6 health alert ----
    sub_in_para(paras[332], '"ble_leak"', '"ble_leak_sensor"')
    sub_in_para(paras[333], '"sensor_id"', '"device_id"')
    t = tables[25]
    set_cell_text(t.rows[3].cells[2],
                  '"valve", "ble_leak_sensor" or "lora". Same vocabulary as source_type on leak and '
                  'auto_close events - one lookup table covers both. Was "ble_leak" before firmware 1.8.0.')
    set_cell_text(t.rows[4].cells[0], "data.device_id")
    set_cell_text(t.rows[4].cells[2],
                  "Device identifier, uppercase (MAC for BLE / valve, 0xNNNNNNNN for LoRa). "
                  "Was data.sensor_id before firmware 1.9.0.")
    note("§5.2.6 health dev_type + device_id corrected")

    # ---- §5.4.1 twin reported ----
    sub_in_para(paras[426], '"valve_mac"', '"valve_device_id"')
    note("§5.4.1 twin reported valve_mac -> valve_device_id")

    # ---- app-facing requirement tables ----
    sub_in_cell(tables[12].rows[2].cells[2], "sensor_id", "device_id")
    sub_in_cell(tables[17].rows[3].cells[2],
                "leak_detected, leak_cleared, auto_close, valve_state_changed, valve_flood_detected",
                "leak_detected, leak_cleared, auto_close, valve_state_changed")
    sub_in_cell(tables[37].rows[4].cells[2], "sensor_id", "device_id")
    note("§3/§4 app requirement tables updated")

    # ---- inbound examples: teach the CANONICAL device-type spelling ----
    # The payload key names are unchanged (still sensor_id / valve_mac inbound), but the VALUE
    # "ble" is a legacy alias. "ble_leak_sensor" has been canonical since FW 1.8.0 and matches
    # source_type / dev_type outbound, so one lookup table covers both directions. "ble" and
    # "ble_leak" remain accepted forever (sensor_meta.h sensor_type_is_ble_leak), so nothing an
    # existing app sends breaks — this only changes what the document teaches.
    sub_in_para(paras[399], '"sensor_type":"ble"', '"sensor_type":"ble_leak_sensor"')
    sub_in_para(paras[393], '"target":"ble"', '"target":"ble_leak_sensor"')
    sub_in_cell(tables[28].rows[8].cells[1], '"sensor_type":"ble"', '"sensor_type":"ble_leak_sensor"')
    sub_in_cell(tables[28].rows[8].cells[1], '"sensor_type": "ble"', '"sensor_type": "ble_leak_sensor"')
    sub_in_cell(tables[28].rows[6].cells[1], '"valve|ble|all"', '"valve|ble_leak_sensor|all"')
    sub_in_cell(tables[19].rows[1].cells[2], 'target="ble"', 'target="ble_leak_sensor"')
    note('inbound examples use canonical "ble_leak_sensor" (aliases still accepted)')

    # ---- example envelopes still claimed firmware 1.3.0 ----
    # Cosmetic but confusing next to "since firmware 1.9.0" prose in the same section.
    n_fw = 0
    for p in paras:
        if p.style.name == "CodeBlock" and '"fw": "1.3.0"' in p.text:
            sub_in_para(p, '"fw": "1.3.0"', f'"fw": "{FW}"')
            n_fw += 1
    note(f"example envelopes: gateway.fw 1.3.0 -> {FW} ({n_fw} sites)")

    # ---- revision history ----
    t = tables[0]
    row = None
    for r in t.rows[1:]:
        if not r.cells[0].text.strip() and not r.cells[2].text.strip():
            row = r
            break
    if row is None:
        row = t.add_row()
    set_cell_text(row.cells[0], "V4")
    set_cell_text(row.cells[1], DATE)
    set_cell_text(row.cells[2],
                  f"Firmware {FW} wire-format update. Valve flood-probe events consolidated into "
                  "leak_detected / leak_cleared discriminated by source_type \"valve\"; "
                  "valve_flood_detected and valve_flood_cleared removed. Outbound identity key renamed "
                  "sensor_id / valve.mac / valve_mac to device_id across telemetry, health events and "
                  "twin reported properties, and device ids are now always uppercase. Health dev_type "
                  "\"ble_leak\" corrected to \"ble_leak_sensor\". Inbound command payloads unchanged.")
    set_cell_text(row.cells[3], AUTHOR)
    note("revision history row added")

    doc.save(DST)

    print(f"wrote: {DST}")
    print("\nchanges applied:")
    for c in changes:
        print("  -", c)
    print("\ndeliberately NOT changed (inbound command contract):")
    for k in KEEP_INBOUND:
        print("  -", k)


if __name__ == "__main__":
    main()
