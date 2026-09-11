#!/usr/bin/env python3
# ruff: noqa: E501
"""Build the telemetry message catalogue: every real message, as a concrete example.

Outputs:
    eFloStop2_Telemetry_Messages_v4.0.docx
    telemetry_messages.md

Earlier .docx revisions are deliberately NOT regenerated — each stays on disk as the
record of the firmware it described, so any two can be compared side by side.
Bump DOC_VERSION / PREV_* (and only those) when the wire format changes again.

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

# Document revision, independent of the firmware version below it.
# v1.0 = firmware 1.8.0, separate valve_flood_* events, sensor_id identity key.
# v2.0 = firmware 1.9.0, unified leak events, device_id identity key everywhere.
# v3.0 = firmware 2.0.0, type-named identity keys (valve_id / sensor_id) on
#        device-reported messages, one source_type vocabulary on every outbound
#        message. Hub-generated events still used device_id.
# v4.0 = firmware 2.1.0, type-named identity keys EVERYWHERE — device_id is gone
#        from the telemetry plane entirely.
DOC_VERSION = "4.0"

# Baseline of the "What changed" comparison table in messages_data.CHANGES.
# NOT simply "the previous revision": the table is cumulative from 1.9.0, so that
# a reader still holding the v2.0 document sees every change in one place rather
# than having to chain three side-by-side tables. v3.0 (firmware 2.0.0-2.0.2) is
# an interim revision between the two — named in INTERIM below so the sequence on
# disk is not left unexplained. Keep these two in step with the CHANGES rows.
PREV_VERSION = "2.0"
PREV_FW = "1.9.0"
INTERIM = "3.0"


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


def _runs(p, text, size, bold=False, italic=False):
    """Emit runs for `text`, honouring `code` spans NESTED inside bold/italic.

    Without this, **bold with `code` inside** matches as one bold chunk and the
    backticks render literally on the page.
    """
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
    """Inline markdown -> Word runs: `code`, **bold**, *italic*, and code nested in either.

    The bold alternative MUST precede the italic one in the pattern, or `**x**`
    matches as an empty italic and the asterisks leak into the rendered page.
    """
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
    r = p.add_run(f"Every message the hub can send, as a real example  ·  v{DOC_VERSION}")
    r.italic = True; r.font.size = Pt(13)

    t = doc.add_table(rows=0, cols=2); t.style = "Table Grid"
    for k, v in [("Document version", f"{DOC_VERSION}  (supersedes v{PREV_VERSION}, which documented firmware {PREV_FW})"),
                 ("Firmware version", f"{FW}  [{FW_CITE}]"),
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

    # What changed against the previous document revision — for side-by-side comparison.
    doc.add_heading(f"What changed since v{PREV_VERSION}", 1)
    rich(doc, f"Document **v{PREV_VERSION}** described firmware **{PREV_FW}**; this is **v"
              f"{DOC_VERSION}**, describing firmware **{FW}**. v{PREV_VERSION} is kept unchanged so the two "
              "can be read side by side. Every row below is a breaking change — there is no "
              "compatibility shim on the telemetry plane, and every hub runs the new shape.\n\n"
              f"The table is cumulative, so a reader holding either earlier revision can use it. "
              f"Rows are annotated with the firmware release that introduced them; v{INTERIM} is the "
              f"interim revision between v{PREV_VERSION} and this one, and anything marked 2.1.0 is new "
              f"since v{INTERIM}.")
    ct = doc.add_table(rows=1, cols=3); ct.style = "Table Grid"
    for i, h in enumerate(["", f"v{PREV_VERSION} — firmware {PREV_FW}", f"v{DOC_VERSION} — firmware {FW}"]):
        cell = ct.rows[0].cells[i]; cell.text = ""
        rr = cell.paragraphs[0].add_run(h); rr.bold = True; rr.font.size = Pt(8.5)
        shd = OxmlElement("w:shd"); shd.set(qn("w:val"), "clear"); shd.set(qn("w:fill"), "DDDDDD")
        cell._tc.get_or_add_tcPr().append(shd)
    for what, old, new in M.CHANGES:
        cells = ct.add_row().cells
        for i, val in enumerate([what, old, new]):
            cells[i].text = ""
            rr = cells[i].paragraphs[0].add_run(val)
            rr.font.size = Pt(8)
            if i:
                rr.font.name = MONO
                rr.font.size = Pt(7.5)
        for i, w in enumerate([1.6, 2.5, 2.6]):
            cells[i].width = Inches(w)
    doc.add_page_break()

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
        if group in M.GROUP_NOTES:
            rich(doc, M.GROUP_NOTES[group], size=10)
        for m in [x for x in M.MESSAGES if x["group"] == group]:
            doc.add_heading(f"{m['id']} — {m['title']}", 2)
            rich(doc, m["when"])
            mono(doc, j(m["msg"]))
            p = doc.add_paragraph()
            r = p.add_run(m["cite"]); r.font.name = MONO; r.font.size = Pt(7.5)
            r.font.color.rgb = RGBColor(0x70, 0x70, 0x70)

    cp = doc.core_properties
    cp.title = f"{TITLE} v{DOC_VERSION}"
    cp.author = "Claude Code"
    import datetime
    # Pinned so rebuilds are byte-comparable. Distinct per doc version so the two
    # revisions do not look identical in Windows file properties.
    fixed = datetime.datetime(2026, 8, 3, 0, 0, 0)
    cp.created = cp.modified = fixed
    doc.save(path)
    assert_no_leaked_markdown(doc)
    print(f"docx OK: {path}")


def assert_no_leaked_markdown(doc):
    """No `code`, **bold** or *italic* marker may survive into the rendered page.

    JSON bodies legitimately contain no markers, so any hit is a renderer bug —
    historically a bold span swallowing a nested code span. Fails the build rather
    than shipping a page with visible asterisks.
    """
    bad = []
    for p in doc.paragraphs:
        t = p.text
        if "`" in t or "**" in t or re.search(r"(?<!\*)\*(?!\*)", t):
            bad.append(t[:160].replace("\n", " "))
    if bad:
        print("BUILD FAILED: markdown markers leaked into the rendered document:",
              file=sys.stderr)
        for b in bad:
            print("  -", b, file=sys.stderr)
        raise SystemExit(1)


def build_md(path):
    L = [f"# {TITLE}", "",
         f"*Every message the hub can send, as a real example — v{DOC_VERSION}*", "",
         "> GENERATED FILE — produced by `docs/telemetry/build_messages.py` from `messages_data.py`.", "",
         "| | |", "|---|---|",
         f"| Document version | {DOC_VERSION} (supersedes v{PREV_VERSION}, which documented firmware {PREV_FW}) |",
         f"| Firmware version | {FW} — `{FW_CITE}` |",
         f"| Git commit | `{SHA}` |",
         "| Schema | `eflostop.v2` |",
         "| Topic | `devices/<device_id>/messages/events/` (QoS 1) |",
         f"| Message count | {len(M.MESSAGES)} distinct messages across {len(M.GROUP_ORDER)} families |",
         ""]
    L.append(M.INTRO)
    L += ["", f"## What changed since v{PREV_VERSION}", "",
          f"Document **v{PREV_VERSION}** described firmware **{PREV_FW}**; this is **v{DOC_VERSION}**, describing "
          f"firmware **{FW}**. v{PREV_VERSION} of the `.docx` is kept unchanged so the two can be read side by "
          "side. Every row below is a breaking change — there is no compatibility shim on the "
          "telemetry plane.", "",
          f"The table is cumulative, so a reader holding either earlier revision can use it. Rows are "
          f"annotated with the firmware release that introduced them; v{INTERIM} is the interim revision "
          f"between v{PREV_VERSION} and this one, and anything marked 2.1.0 is new since v{INTERIM}.", "",
          f"| | v{PREV_VERSION} — firmware {PREV_FW} | v{DOC_VERSION} — firmware {FW} |", "|---|---|---|"]
    for what, old, new in M.CHANGES:
        L.append(f"| {what} | `{old}` | `{new}` |")
    L += ["", "## Index of messages", "", "| ID | `type` | `data.event` | Message |", "|---|---|---|---|"]
    for m in M.MESSAGES:
        ev = m["msg"]["data"].get("event", "—")
        L.append(f"| {m['id']} | `{m['msg']['type']}` | `{ev}` | {m['title']} |")
    L.append("")
    for group in M.GROUP_ORDER:
        L += [f"## {group}", ""]
        if group in M.GROUP_NOTES:
            L += ["> " + M.GROUP_NOTES[group], ""]
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
    # v1.0 is NOT regenerated — it stays on disk as the firmware-1.8.0 record.
    build_docx(os.path.join(
        HERE, f"eFloStop2_Telemetry_Messages_v{DOC_VERSION}.docx"))
    print("\nDone.")
