#!/usr/bin/env python3
# ruff: noqa: E501
"""Build the C2D command catalogue: every command the cloud can send, as a real example.

Companion to build_messages.py, which documents the other direction. Same house style, same
guarantees: every example is a complete message exactly as it goes on the wire, no placeholders,
and the build fails rather than shipping a page with leaked markdown or invalid JSON.

Outputs:
    eFloStop2_C2D_Commands_v<DOC_VERSION>.docx
    c2d_commands_catalogue.md

Run: python docs/telemetry/build_commands.py
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

import commands_data as C                                  # noqa: E402
from docx import Document                                  # noqa: E402
from docx.enum.text import WD_ALIGN_PARAGRAPH              # noqa: E402
from docx.oxml import OxmlElement                          # noqa: E402
from docx.oxml.ns import qn                                # noqa: E402
from docx.shared import Inches, Pt, RGBColor               # noqa: E402

MONO = "Consolas"
TITLE = "eFloStop II Wi-Fi Hub — Cloud-to-Device Command Catalogue"
DOC_VERSION = "3.0"

# The revision this one supersedes, named in the "What changed" section.
PREV_VERSION = "1.0"
PREV_FW = "1.9.0"


def git(*a):
    try:
        return subprocess.check_output(["git", "-C", REPO, *a], text=True,
                                       stderr=subprocess.DEVNULL).strip()
    except Exception:
        return "unavailable"


def fw_version():
    for i, line in enumerate(open(os.path.join(REPO, "CMakeLists.txt"), encoding="utf-8"), 1):
        m = re.search(r'set\(PROJECT_VER\s+"([^"]+)"\)', line)
        if m:
            return m.group(1), f"CMakeLists.txt:{i}"
    raise SystemExit("PROJECT_VER not found")


FW, FW_CITE = fw_version()
SHA = git("rev-parse", "HEAD")


def add_field(p, instr):
    r = p.add_run()
    fc = OxmlElement("w:fldChar"); fc.set(qn("w:fldCharType"), "begin"); r._r.append(fc)
    r2 = p.add_run()
    it = OxmlElement("w:instrText"); it.set(qn("xml:space"), "preserve"); it.text = instr
    r2._r.append(it)
    r3 = p.add_run()
    fs = OxmlElement("w:fldChar"); fs.set(qn("w:fldCharType"), "separate"); r3._r.append(fs)
    p.add_run("Update field")
    r5 = p.add_run()
    fe = OxmlElement("w:fldChar"); fe.set(qn("w:fldCharType"), "end"); r5._r.append(fe)


def shade(cell, fill="DDDDDD"):
    shd = OxmlElement("w:shd"); shd.set(qn("w:val"), "clear"); shd.set(qn("w:fill"), fill)
    cell._tc.get_or_add_tcPr().append(shd)


def mono(doc, text, size=8.5, fill="F4F4F4"):
    p = doc.add_paragraph()
    for i, line in enumerate(text.split("\n")):
        if i:
            p.add_run().add_break()
        r = p.add_run(line)
        r.font.name = MONO
        r.font.size = Pt(size)
    p.paragraph_format.left_indent = Inches(0.12)
    p.paragraph_format.space_after = Pt(8)
    if fill:
        pPr = p._p.get_or_add_pPr()
        shd = OxmlElement("w:shd")
        shd.set(qn("w:val"), "clear"); shd.set(qn("w:fill"), fill)
        pPr.append(shd)
    return p


def _runs(p, text, size, bold=False, italic=False):
    """Emit runs for `text`, honouring `code` spans nested inside bold/italic."""
    for part in re.split(r"(`[^`]+`)", text):
        if not part:
            continue
        if part.startswith("`") and part.endswith("`"):
            r = p.add_run(part[1:-1]); r.font.name = MONO; r.font.size = Pt(size - 1)
        else:
            r = p.add_run(part); r.font.size = Pt(size)
        r.bold = bold
        r.italic = italic


def rich(doc, text, size=10.5, italic=False):
    """Inline markdown -> Word runs. Bold alternative MUST precede italic in the pattern."""
    p = doc.add_paragraph()
    for chunk in re.split(r"(`[^`]+`|\*\*[^*]+\*\*|\*[^*]+\*)", text):
        if not chunk:
            continue
        if chunk.startswith("`") and chunk.endswith("`"):
            r = p.add_run(chunk[1:-1]); r.font.name = MONO; r.font.size = Pt(size - 1)
        elif chunk.startswith("**") and chunk.endswith("**"):
            _runs(p, chunk[2:-2], size, bold=True)
        elif chunk.startswith("*") and chunk.endswith("*"):
            _runs(p, chunk[1:-1], size, italic=True)
        else:
            _runs(p, chunk, size, italic=italic)
    return p


def j(obj):
    return json.dumps(obj, indent=2, ensure_ascii=False)


def table(doc, headers, rows, widths, size=8):
    t = doc.add_table(rows=1, cols=len(headers)); t.style = "Table Grid"
    for i, h in enumerate(headers):
        cell = t.rows[0].cells[i]; cell.text = ""
        rr = cell.paragraphs[0].add_run(h); rr.bold = True; rr.font.size = Pt(size + 0.5)
        shade(cell)
    for row in rows:
        cells = t.add_row().cells
        for i, val in enumerate(row):
            cells[i].text = ""
            rr = cells[i].paragraphs[0].add_run(str(val)); rr.font.size = Pt(size)
            if i == 0:
                rr.font.name = MONO
        for i, w in enumerate(widths):
            cells[i].width = Inches(w)
    for i, w in enumerate(widths):
        t.rows[0].cells[i].width = Inches(w)
    return t


def build_docx(path):
    doc = Document()
    doc.styles["Normal"].font.name = "Calibri"
    doc.styles["Normal"].font.size = Pt(10.5)
    s = doc.sections[0]
    s.left_margin = s.right_margin = Inches(0.8)
    s.top_margin = s.bottom_margin = Inches(0.7)

    hdr = s.header.paragraphs[0]; hdr.text = ""
    r = hdr.add_run(TITLE); r.font.size = Pt(8); r.font.color.rgb = RGBColor(0x60, 0x60, 0x60)
    hdr.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    ftr = s.footer.paragraphs[0]; ftr.text = ""; ftr.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = ftr.add_run("Page "); r.font.size = Pt(8)
    add_field(ftr, "PAGE"); r = ftr.add_run(" of "); r.font.size = Pt(8); add_field(ftr, "NUMPAGES")

    doc.add_heading(TITLE, level=0)
    p = doc.add_paragraph()
    r = p.add_run(f"Every command the cloud can send, as a real example  ·  v{DOC_VERSION}")
    r.italic = True; r.font.size = Pt(13)

    t = doc.add_table(rows=0, cols=2); t.style = "Table Grid"
    for k, v in [("Document version", f"{DOC_VERSION}  (supersedes v{PREV_VERSION}, which documented firmware {PREV_FW})"),
                 ("Firmware version", f"{FW}  [{FW_CITE}]"),
                 ("Git commit", SHA),
                 ("Schema", C.CMD_SCHEMA),
                 ("Direction", "cloud → hub (C2D)"),
                 ("Transport", C.TRANSPORT),
                 ("Command count", f"{len(C.COMMANDS)} commands across {len(C.GROUP_ORDER)} groups")]:
        cells = t.add_row().cells
        cells[0].text = ""; rr = cells[0].paragraphs[0].add_run(k); rr.bold = True; rr.font.size = Pt(9)
        cells[1].text = ""; rr = cells[1].paragraphs[0].add_run(v); rr.font.size = Pt(9)
        cells[0].width = Inches(1.5); cells[1].width = Inches(5.2)
    doc.add_paragraph()

    for para in C.INTRO.split("\n\n"):
        rich(doc, para)

    doc.add_heading("Contents", 1)
    p = doc.add_paragraph(); add_field(p, 'TOC \\o "1-2" \\h \\z \\u')
    doc.add_paragraph("Word fills this in when fields are updated: select all (Ctrl+A) then press F9.")

    # ---- what changed against the previous revision ----
    doc.add_heading(f"What changed since v{PREV_VERSION}", 1)
    rich(doc, f"Document **v{PREV_VERSION}** described firmware **{PREV_FW}**; this is **v{DOC_VERSION}**, "
              f"describing firmware **{FW}**. v{PREV_VERSION} is kept unchanged so the two can be read side "
              "by side. Unlike the telemetry plane, the command plane keeps a compatibility path: the one "
              "renamed key still accepts its old spelling.")
    ct = doc.add_table(rows=1, cols=3); ct.style = "Table Grid"
    for i, h in enumerate(["", f"v{PREV_VERSION} — firmware {PREV_FW}", f"v{DOC_VERSION} — firmware {FW}"]):
        cell = ct.rows[0].cells[i]; cell.text = ""
        rr = cell.paragraphs[0].add_run(h); rr.bold = True; rr.font.size = Pt(8.5)
        shd = OxmlElement("w:shd"); shd.set(qn("w:val"), "clear"); shd.set(qn("w:fill"), "DDDDDD")
        cell._tc.get_or_add_tcPr().append(shd)
    for what, old, new in C.CHANGES:
        cells = ct.add_row().cells
        for i, val in enumerate([what, old, new]):
            cells[i].text = ""
            rr = cells[i].paragraphs[0].add_run(val)
            rr.font.size = Pt(8)
            if i:
                rr.font.name = MONO
        cells[0].width = Inches(1.9); cells[1].width = Inches(2.4); cells[2].width = Inches(2.4)
    doc.add_paragraph()

    # ---- envelope ----
    doc.add_heading("The command envelope", 1)
    for para in C.ENVELOPE_INTRO.split("\n\n"):
        rich(doc, para)
    mono(doc, j(C.ENVELOPE_EXAMPLE))
    table(doc, ["Key", "Type", "Required", "Notes"], C.ENVELOPE_KEYS, [1.1, 0.8, 0.8, 4.1])

    doc.add_heading("Acknowledgements", 1)
    for para in C.ACK_INTRO.split("\n\n"):
        rich(doc, para)
    for para in C.ACK_SNAPSHOT_NOTE.split("\n\n"):
        rich(doc, para)
    rich(doc, "**Success:**")
    mono(doc, j(C.ACK_OK_EXAMPLE))
    rich(doc, "**Failure:**")
    mono(doc, j(C.ACK_ERR_EXAMPLE))

    # ---- index ----
    doc.add_heading("Index of commands", 1)
    table(doc, ["ID", "cmd", "Payload", "What it does"],
          [[m["id"], m["name"], "yes" if m.get("params") else "none", m["title"]] for m in C.COMMANDS],
          [0.4, 1.5, 0.7, 4.2])
    doc.add_page_break()

    # ---- one section per command ----
    for group in C.GROUP_ORDER:
        doc.add_heading(group, 1)
        if group in C.GROUP_NOTES:
            rich(doc, C.GROUP_NOTES[group], size=10)
        for m in [x for x in C.COMMANDS if x["group"] == group]:
            doc.add_heading(f"{m['id']} — {m['name']}", 2)
            rich(doc, m["when"])
            mono(doc, j(m["req"]))
            if m.get("params"):
                table(doc, ["Key", "Type", "Required", "Rules"], m["params"], [1.2, 0.8, 0.8, 4.0])
            if m.get("notes"):
                rich(doc, m["notes"], size=10)
            if m.get("ack_ok"):
                rich(doc, "**Acknowledgement on success:**", size=10)
                mono(doc, j(m["ack_ok"]))
            if m.get("ack_errs"):
                rich(doc, "**Rejections.** Each is a `cmd_ack` with `status:\"error\"`; the "
                          "`error.detail` text below is exact.", size=10)
                table(doc, ["error.detail", "When"], m["ack_errs"], [3.0, 3.8])
            p = doc.add_paragraph()
            r = p.add_run(m["cite"]); r.font.name = MONO; r.font.size = Pt(7.5)
            r.font.color.rgb = RGBColor(0x70, 0x70, 0x70)

    # ---- Device Twin: the settings that are NOT commands ----
    doc.add_page_break()
    doc.add_heading("Device Twin", 1)
    for para in C.TWIN_INTRO.split("\n\n"):
        rich(doc, para)
    doc.add_heading("Desired properties (cloud writes, hub reads)", 2)
    table(doc, ["Property", "Type", "Range", "Notes"], C.TWIN_DESIRED, [1.5, 0.7, 0.9, 3.7])
    mono(doc, j(C.TWIN_DESIRED_EXAMPLE))
    doc.add_heading("Reported properties (hub writes, cloud reads)", 2)
    for para in C.TWIN_REPORTED_INTRO.split("\n\n"):
        rich(doc, para)
    table(doc, ["Property", "Type", "Notes"], C.TWIN_REPORTED, [1.6, 0.9, 4.3])
    for para in C.TWIN_NOTES.split("\n\n"):
        rich(doc, para)

    if getattr(C, "APPENDIX", None):
        doc.add_page_break()
        for heading, body in C.APPENDIX:
            doc.add_heading(heading, 1)
            for para in body.split("\n\n"):
                rich(doc, para)

    cp = doc.core_properties
    cp.title = f"{TITLE} v{DOC_VERSION}"
    cp.author = "Claude Code"
    import datetime
    cp.created = cp.modified = datetime.datetime(2026, 8, 3, 0, 0, 0)
    doc.save(path)
    assert_no_leaked_markdown(doc)
    print(f"docx OK: {path}")


def assert_no_leaked_markdown(doc):
    bad = []
    for p in doc.paragraphs:
        t = p.text
        if "`" in t or "**" in t or re.search(r"(?<!\*)\*(?!\*)", t):
            bad.append(t[:160].replace("\n", " "))
    if bad:
        print("BUILD FAILED: markdown markers leaked into the rendered document:", file=sys.stderr)
        for b in bad:
            print("  -", b, file=sys.stderr)
        raise SystemExit(1)


def build_md(path):
    L = [f"# {TITLE}", "",
         f"*Every command the cloud can send, as a real example — v{DOC_VERSION}*", "",
         "> GENERATED FILE — produced by `docs/telemetry/build_commands.py` from `commands_data.py`.", "",
         "| | |", "|---|---|",
         f"| Document version | {DOC_VERSION} (supersedes v{PREV_VERSION}, which documented firmware {PREV_FW}) |",
         f"| Firmware version | {FW} — `{FW_CITE}` |",
         f"| Git commit | `{SHA}` |",
         f"| Schema | `{C.CMD_SCHEMA}` |",
         "| Direction | cloud → hub (C2D) |",
         f"| Transport | {C.TRANSPORT} |",
         f"| Command count | {len(C.COMMANDS)} commands across {len(C.GROUP_ORDER)} groups |",
         "", C.INTRO, "",
         f"## What changed since v{PREV_VERSION}", "",
         f"Document **v{PREV_VERSION}** described firmware **{PREV_FW}**; this is **v{DOC_VERSION}**, "
         f"describing firmware **{FW}**. Unlike the telemetry plane, the command plane keeps a "
         "compatibility path: the one renamed key still accepts its old spelling.", "",
         f"| | v{PREV_VERSION} — firmware {PREV_FW} | v{DOC_VERSION} — firmware {FW} |", "|---|---|---|"]
    for what, old, new in C.CHANGES:
        L.append(f"| {what} | `{old}` | `{new}` |")
    L += ["",
         "## The command envelope", "", C.ENVELOPE_INTRO, "",
         "```json", j(C.ENVELOPE_EXAMPLE), "```", "",
         "| Key | Type | Required | Notes |", "|---|---|---|---|"]
    for k, ty, req, notes in C.ENVELOPE_KEYS:
        L.append(f"| `{k}` | {ty} | {req} | {notes} |")
    L += ["", "## Acknowledgements", "", C.ACK_INTRO, "", C.ACK_SNAPSHOT_NOTE, "",
          "**Success:**", "", "```json", j(C.ACK_OK_EXAMPLE), "```", "",
          "**Failure:**", "", "```json", j(C.ACK_ERR_EXAMPLE), "```", "",
          "## Index of commands", "",
          "| ID | `cmd` | Payload | What it does |", "|---|---|---|---|"]
    for m in C.COMMANDS:
        L.append(f"| {m['id']} | `{m['name']}` | {'yes' if m.get('params') else 'none'} | {m['title']} |")
    L.append("")
    for group in C.GROUP_ORDER:
        L += [f"## {group}", ""]
        if group in C.GROUP_NOTES:
            L += ["> " + C.GROUP_NOTES[group], ""]
        for m in [x for x in C.COMMANDS if x["group"] == group]:
            L += [f"### {m['id']} — `{m['name']}`", "", m["when"], "",
                  "```json", j(m["req"]), "```", ""]
            if m.get("params"):
                L += ["| Key | Type | Required | Rules |", "|---|---|---|---|"]
                for k, ty, req, rules in m["params"]:
                    L.append(f"| `{k}` | {ty} | {req} | {rules} |")
                L.append("")
            if m.get("notes"):
                L += [m["notes"], ""]
            if m.get("ack_ok"):
                L += ["**Acknowledgement on success:**", "", "```json", j(m["ack_ok"]), "```", ""]
            if m.get("ack_errs"):
                L += ["**Rejections.** Each is a `cmd_ack` with `status:\"error\"`; the `error.detail` "
                      "text below is exact.", "",
                      "| `error.detail` | When |", "|---|---|"]
                for detail, when in m["ack_errs"]:
                    L.append(f"| `{detail}` | {when} |")
                L.append("")
            L += [f"*`{m['cite']}`*", ""]
    L += ["## Device Twin", "", C.TWIN_INTRO, "",
          "### Desired properties (cloud writes, hub reads)", "",
          "| Property | Type | Range | Notes |", "|---|---|---|---|"]
    for prop, ty, rng, notes in C.TWIN_DESIRED:
        L.append(f"| `{prop}` | {ty} | {rng} | {notes} |")
    L += ["", "```json", j(C.TWIN_DESIRED_EXAMPLE), "```", "",
          "### Reported properties (hub writes, cloud reads)", "", C.TWIN_REPORTED_INTRO, "",
          "| Property | Type | Notes |", "|---|---|---|"]
    for prop, ty, notes in C.TWIN_REPORTED:
        L.append(f"| `{prop}` | {ty} | {notes} |")
    L += ["", C.TWIN_NOTES, ""]

    for heading, body in (getattr(C, "APPENDIX", None) or []):
        L += [f"## {heading}", "", body, ""]
    open(path, "w", encoding="utf-8").write("\n".join(L) + "\n")
    print(f"md OK: {path}")


def check():
    """Every example must be valid JSON, carry the envelope, and contain no placeholder."""
    bad = []
    seen = set()
    names = set()
    for m in C.COMMANDS:
        mid = m["id"]
        if mid in seen:
            bad.append(f"{mid}: duplicate id")
        seen.add(mid)
        names.add(m["name"])
        if m["group"] not in C.GROUP_ORDER:
            bad.append(f"{mid}: group {m['group']!r} not in GROUP_ORDER")
        for label, obj in [("req", m["req"])] + \
                          ([("ack_ok", m["ack_ok"])] if m.get("ack_ok") else []):
            json.loads(json.dumps(obj))
            if label == "req":
                for k in ("schema", "ver", "id", "cmd"):
                    if k not in obj:
                        bad.append(f"{mid}: command envelope missing {k}")
                if obj.get("schema") != C.CMD_SCHEMA:
                    bad.append(f"{mid}: wrong schema {obj.get('schema')!r}")
                if obj.get("cmd") != m["name"]:
                    bad.append(f"{mid}: cmd {obj.get('cmd')!r} != name {m['name']!r}")
                if m.get("params") and "payload" not in obj:
                    bad.append(f"{mid}: declares params but example has no payload")
                if not m.get("params") and "payload" in obj:
                    bad.append(f"{mid}: no params declared but example carries a payload")
            blob = json.dumps(obj)
            for tok in ("<", "TODO", "TBD", "foo", "bar", "placeholder", "xxx", "XXX", "..."):
                if tok in blob:
                    bad.append(f"{mid}: placeholder {tok!r} in {label}")
        # ack correlation must match the request id
        if m.get("ack_ok") and m["ack_ok"]["data"].get("id") != m["req"].get("id"):
            bad.append(f"{mid}: ack_ok correlation id does not match the request id")
    missing = set(C.FIRMWARE_COMMAND_NAMES) - names
    extra = names - set(C.FIRMWARE_COMMAND_NAMES)
    if missing:
        bad.append(f"commands in firmware but not documented: {sorted(missing)}")
    if extra:
        bad.append(f"documented but not in firmware: {sorted(extra)}")
    if bad:
        print("BUILD FAILED:", file=sys.stderr)
        for b in bad:
            print("  -", b, file=sys.stderr)
        raise SystemExit(1)
    print(f"checks OK: {len(C.COMMANDS)} commands, all valid JSON, "
          f"complete coverage of the {len(C.FIRMWARE_COMMAND_NAMES)} firmware commands")


if __name__ == "__main__":
    check()
    build_md(os.path.join(HERE, "c2d_commands_catalogue.md"))
    build_docx(os.path.join(HERE, f"eFloStop2_C2D_Commands_v{DOC_VERSION}.docx"))
    print("\nDone.")
