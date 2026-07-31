#!/usr/bin/env python3
# ruff: noqa: E501
"""Build the telemetry message catalogue: every real message, as a concrete example.

Outputs:
    eFloStop2_Telemetry_Messages_v1.0.docx
    telemetry_messages.md

Run: python docs/telemetry/build_messages.py
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

import messages_data as M                                    # noqa: E402
from docx import Document                                    # noqa: E402
from docx.enum.text import WD_ALIGN_PARAGRAPH                # noqa: E402
from docx.oxml import OxmlElement                            # noqa: E402
from docx.oxml.ns import qn                                  # noqa: E402
from docx.shared import Inches, Pt, RGBColor                 # noqa: E402

MONO = "Consolas"
TITLE = "eFloStop II Wi-Fi Hub — Telemetry Message Catalogue"


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


def mono(doc, text, size=8.5, shade=True):
    p = doc.add_paragraph()
    for i, line in enumerate(text.split("\n")):
        if i:
            p.add_run().add_break()
        r = p.add_run(line)
        r.font.name = MONO
        r.font.size = Pt(size)
    p.paragraph_format.left_indent = Inches(0.12)
    p.paragraph_format.space_after = Pt(8)
    if shade:
        pPr = p._p.get_or_add_pPr()
        shd = OxmlElement("w:shd")
        shd.set(qn("w:val"), "clear"); shd.set(qn("w:fill"), "F4F4F4")
        pPr.append(shd)
    return p


def rich(doc, text, size=10.5, italic=False):
    p = doc.add_paragraph()
    for chunk in re.split(r"(`[^`]+`|\*\*[^*]+\*\*)", text):
        if not chunk:
            continue
        if chunk.startswith("`") and chunk.endswith("`"):
            r = p.add_run(chunk[1:-1]); r.font.name = MONO; r.font.size = Pt(size - 1)
        elif chunk.startswith("**") and chunk.endswith("**"):
            r = p.add_run(chunk[2:-2]); r.bold = True; r.font.size = Pt(size)
        else:
            r = p.add_run(chunk); r.font.size = Pt(size); r.italic = italic
    return p


def j(obj):
    return json.dumps(obj, indent=2, ensure_ascii=False)


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
    r = p.add_run("Every message the hub can send, as a real example"); r.italic = True; r.font.size = Pt(13)

    t = doc.add_table(rows=0, cols=2); t.style = "Table Grid"
    for k, v in [("Firmware version", f"{FW}  [{FW_CITE}]"),
                 ("Git commit", SHA),
                 ("Schema", "eflostop.v2"),
                 ("Topic", "devices/<device_id>/messages/events/  (QoS 1)"),
                 ("Message count", f"{len(M.MESSAGES)} distinct messages across {len(M.GROUP_ORDER)} families")]:
        c = t.add_row().cells
        c[0].text = ""; rr = c[0].paragraphs[0].add_run(k); rr.bold = True; rr.font.size = Pt(9)
        c[1].text = ""; rr = c[1].paragraphs[0].add_run(v); rr.font.size = Pt(9)
        c[0].width = Inches(1.5); c[1].width = Inches(5.2)
    doc.add_paragraph()

    for para in M.INTRO.split("\n\n"):
        rich(doc, para)

    doc.add_heading("Contents", 1)
    p = doc.add_paragraph(); add_field(p, 'TOC \\o "1-2" \\h \\z \\u')
    doc.add_paragraph("Word fills this in when fields are updated: select all (Ctrl+A) then press F9.")

    # Index of every message.
    doc.add_heading("Index of messages", 1)
    it = doc.add_table(rows=1, cols=4); it.style = "Table Grid"
    for i, h in enumerate(["ID", "type", "data.event", "Message"]):
        cell = it.rows[0].cells[i]; cell.text = ""
        rr = cell.paragraphs[0].add_run(h); rr.bold = True; rr.font.size = Pt(8.5)
        shd = OxmlElement("w:shd"); shd.set(qn("w:val"), "clear"); shd.set(qn("w:fill"), "DDDDDD")
        cell._tc.get_or_add_tcPr().append(shd)
    for m in M.MESSAGES:
        ev = m["msg"]["data"].get("event", "—")
        cells = it.add_row().cells
        for i, val in enumerate([m["id"], m["msg"]["type"], ev, m["title"]]):
            cells[i].text = ""
            rr = cells[i].paragraphs[0].add_run(val)
            rr.font.size = Pt(8)
            if i in (0, 1, 2):
                rr.font.name = MONO
        for i, w in enumerate([0.4, 0.8, 1.7, 3.8]):
            cells[i].width = Inches(w)
    doc.add_page_break()

    for group in M.GROUP_ORDER:
        doc.add_heading(group, 1)
        for m in [x for x in M.MESSAGES if x["group"] == group]:
            doc.add_heading(f"{m['id']} — {m['title']}", 2)
            rich(doc, m["when"])
            mono(doc, j(m["msg"]))
            p = doc.add_paragraph()
            r = p.add_run(m["cite"]); r.font.name = MONO; r.font.size = Pt(7.5)
            r.font.color.rgb = RGBColor(0x70, 0x70, 0x70)

    cp = doc.core_properties
    cp.title = TITLE
    cp.author = "Claude Code"
    import datetime
    fixed = datetime.datetime(2026, 7, 31, 0, 0, 0)
    cp.created = cp.modified = fixed
    doc.save(path)
    print(f"docx OK: {path}")


def build_md(path):
    L = [f"# {TITLE}", "", "*Every message the hub can send, as a real example*", "",
         "> GENERATED FILE — produced by `docs/telemetry/build_messages.py` from `messages_data.py`.", "",
         "| | |", "|---|---|",
         f"| Firmware version | {FW} — `{FW_CITE}` |",
         f"| Git commit | `{SHA}` |",
         "| Schema | `eflostop.v2` |",
         "| Topic | `devices/<device_id>/messages/events/` (QoS 1) |",
         f"| Message count | {len(M.MESSAGES)} distinct messages across {len(M.GROUP_ORDER)} families |",
         ""]
    L.append(M.INTRO)
    L += ["", "## Index of messages", "", "| ID | `type` | `data.event` | Message |", "|---|---|---|---|"]
    for m in M.MESSAGES:
        ev = m["msg"]["data"].get("event", "—")
        L.append(f"| {m['id']} | `{m['msg']['type']}` | `{ev}` | {m['title']} |")
    L.append("")
    for group in M.GROUP_ORDER:
        L += [f"## {group}", ""]
        for m in [x for x in M.MESSAGES if x["group"] == group]:
            L += [f"### {m['id']} — {m['title']}", "", m["when"], "", "```json", j(m["msg"]), "```", "",
                  f"*`{m['cite']}`*", ""]
    open(path, "w", encoding="utf-8").write("\n".join(L) + "\n")
    print(f"md OK: {path}")


def check():
    """Every message must be valid JSON, carry the full envelope, and contain no placeholder."""
    bad = []
    seen = set()
    for m in M.MESSAGES:
        msg = m["msg"]
        json.loads(json.dumps(msg))
        if m["id"] in seen:
            bad.append(f"{m['id']}: duplicate id")
        seen.add(m["id"])
        for k in ("schema", "ts", "gateway", "type", "data"):
            if k not in msg:
                bad.append(f"{m['id']}: envelope missing {k}")
        if msg["ts"] < 1704067200:
            bad.append(f"{m['id']}: ts below the firmware's minimum emittable value")
        for k in ("id", "short_id", "fw", "uptime_s"):
            if k not in msg["gateway"]:
                bad.append(f"{m['id']}: gateway missing {k}")
        blob = json.dumps(msg)
        for tok in ("%02X", "%08lX", "<hub name", "<label", "<correlation", "<valve DIS",
                    "<sensor firmware", "TODO", "TBD", "foo", "bar", "placeholder"):
            if tok in blob:
                bad.append(f"{m['id']}: placeholder or format string on the wire: {tok!r}")
    if bad:
        print("BUILD FAILED:", file=sys.stderr)
        for b in bad:
            print("  -", b, file=sys.stderr)
        raise SystemExit(1)
    print(f"checks OK: {len(M.MESSAGES)} messages, all valid JSON, full envelope, no placeholders")


if __name__ == "__main__":
    check()
    build_md(os.path.join(HERE, "telemetry_messages.md"))
    build_docx(os.path.join(HERE, "eFloStop2_Telemetry_Messages_v1.0.docx"))
    print("\nDone.")
