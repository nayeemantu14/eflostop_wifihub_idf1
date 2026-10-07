#!/usr/bin/env python3
# ruff: noqa: E501
"""Produce V5 of the Watts Digital app-requirements document from V4.

V4 documents the hub firmware 1.9.0 wire format. V5 documents hub firmware 2.1.4 (the
release the app ships against) and says where 2.1.4 differs from 2.1.3, the version in the
field. The trigger-mask section is rewritten: V4 left out bit 1 (LoRa sensors) and gave the
default as 5, so an app built to V4 would turn LoRa auto-close off on every Settings save.

How V5 was made: V4 was audited section by section against 2.1.4 (fix/2.1.4, last firmware
commit 545b8f2) and 2.1.3 (master, ae4d59a), and every finding was verified against the code.
Where a fact appears in more than one section, one wording was fixed first (the coordinator's
canon) and every section uses it. Where the code and a document disagree, the code wins.
Three whole-document reviews (integrity, cross-section consistency, completeness) followed; their
fixes are in the modules, each recorded with a "global fix" note.

Scope of change:
  - 4.8.1 trigger mask rewritten: bit 0 BLE leak sensors, bit 1 LoRa leak sensors, bit 2 the
    valve flood probe, default 7. The app writes it only with the per-source booleans
    trigger_ble_leak and trigger_valve_flood, never with trigger_mask or trigger_lora;
    auto_close_enabled is the master switch above it; the three moments the hub closes the valve
    without checking it (valve reconnect, override_cancel, end of an override window); the
    provision opt-in, sent only in a hub's first provision. New APP-FR-095..097, TC-013..015,
    TC-N09.
  - Identity keys: valve_id and sensor_id everywhere (V4's device_id is never sent). The history
    of the old keys is kept once, in 2.5.
  - LoRa leak sensors: the app and backend parse data.lora_sensors, lora_sensor_count and
    source_type "lora"; the app shows them read-only.
  - Delivery: at least once, byte-identical dedupe, order by ts, late events and their pushes.
    Stated in full once, in 5.2; new APP-NF-013, 014, 017, 018 and TC-N15..N18.
  - Commands: cmd_ack "ok" means queued for valve commands; confirmation from
    valve_state_changed or a snapshot, with a 60 s fallback; one exact error.detail table in
    5.2.7 with its Show/Log use; the refusals new in 2.1.4; the legacy keyword hazard; a 30 s
    expiry on the four valve-actuating commands; one auto_close push per closing.
  - Online/offline: IoT Hub connection state with a 60 s reconnect debounce (token renewal about
    every 18 h); the secondary gate is the larger of 2 x snapshot_interval_s and 5 minutes,
    restarted by a snapshot or a lifecycle.
  - Also: the hub with no devices ("valve": {} and "No devices set up yet"), the "Not heard yet"
    badge (APP-FR-086), the valve battery bands and the valve-battery push (APP-FR-117), health
    events for reachability only, the override-ended push (APP-FR-118), the automatic RMLEAK
    clear (10 s; 30 s up to 2.1.3), the hub label QR format, the hub name through the Device
    Twin only, the dashboard banner order, Wi-Fi setup and router outages, Appendix A.5 Message
    Timing, new tests TC-016..022 and TC-N11..N14.
  - Front matter: the V5 revision-history row, and a "Changes in V5 that need app or backend
    work" list after it.
  - Table of contents: the cached entries (already stale in V4) are rebuilt from V5's headings
    by _sync_cached_toc, so a viewer that shows the cached field (Protected View, Word Online, a
    preview pane, a PDF made without opening the file) lists the right headings. Page numbers of
    added entries are approximate. The builder also sets updateFields, so desktop Word offers to
    update the field on open; after the final build, update the whole table in Word and save
    before sending, so the page numbers are right too.
  - IDs: no V4 ID is renumbered or reused. 4.2's APP-FR-04..09 are written APP-FR-004..009.
    TC-N10 is cited in V4's revision history, so the new negative tests start at TC-N11.

Deliberately not changed:
  - The 11 figures. Their images are kept; captions are corrected where a picture contradicts
    V5, and the redraws are listed in the change log (Figures 1-6 and 8-11).
  - Earlier issues' revision-history rows stay as issued (V4's stray R7 is merged into R4 and
    the empty R6 is removed). The body drops "1.9.0" and every other note about firmware older
    than 2.1.3, except the one identity-key note in 2.5.
  - Planned firmware (RADIO_PORTAL_PLAN WP3-WP10) is not described; in particular nothing is
    said about BLE scanning during Wi-Fi setup, which WP8 changes.
  - Firmware behaviour queued for a later release is documented as it is today: the mask is not
    checked at a valve reconnect, override_cancel or the end of an override window; trigger_mask
    is not range-checked; auto_close_resumed ignores auto_close_enabled; a rules-only provision
    marks an empty hub provisioned; the legacy keyword scan; no D2C message properties;
    override_enable has no valve-battery check.

Like V4's builder, this EDITS A LOADED COPY of V4 and saves under a new name: V4 carries 11
embedded images, 45 tables and hand-applied styling that a regeneration would lose. V4 is
never written to; the script refuses to run if the output path equals the input path.

Structure: one module per document section in app_req_v5/ (s1 ... s7, tm = section 4.8,
front = revision history and the V5 change list). Every module addresses V4's ORIGINAL
paragraph and table numbering through Ctx (see app_req_v5/common.py), so modules do not
shift each other. Every anchor is checked; a mismatch aborts the run and nothing is written.

Run:   python docs/telemetry/build_app_requirements_v5.py
Test:  python docs/telemetry/build_app_requirements_v5.py --only s3 --out <scratch>.docx
"""
import argparse
import collections
import datetime
import importlib
import os
import re
import sys
import zipfile

from docx import Document

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from app_req_v5.common import W14, Ctx, EditError  # noqa: E402

DOCS = r"C:\Work\Projects\EfloStop 2\Documents"
SRC = os.path.join(DOCS, "eFloStop_2_App_Requirements_Watts_Digital_Update-V4.docx")
DST = os.path.join(DOCS, "eFloStop_2_App_Requirements_Watts_Digital_Update-V5.docx")

FW = "2.1.4"
FW_FIELD = "2.1.3"
DATE = "06/10/2026"
AUTHOR = "Nayeem A"

# Order matters only where two modules insert after the same anchor.
MODULES = ["s1", "s2", "tm", "s3", "s4", "s5", "s6", "s7", "front"]


def _blips(path):
    with zipfile.ZipFile(path) as z:
        return z.read("word/document.xml").decode("utf8").count("<a:blip ")


def _dedupe_para_ids(doc):
    """Drop a repeated w14:paraId (Word regenerates missing ones). w14:textId is left alone:
    Word itself repeats it (V4 carries "77777777" on 2,159 paragraphs)."""
    seen = set()
    dropped = 0
    a = "{%s}paraId" % W14
    for el in doc.element.body.iter():
        v = el.attrib.get(a)
        if v is None:
            continue
        if v in seen:
            del el.attrib[a]
            dropped += 1
        else:
            seen.add(v)
    return dropped


def _sync_cached_toc(doc):
    """Rebuild the cached entries of the table of contents from the body headings.

    The TOC field's cached result is what a viewer shows until Word updates the field (Protected
    View, Word Online, preview panes, a PDF made without opening the file); it was already stale in
    V4. Every Heading 1-3 body paragraph gets one entry, in order, at its level. An entry that already
    points at the heading's _Toc bookmark is kept, with its text set to the heading's. A heading with
    no _Toc bookmark gets one, and its entry is a copy of the first entry of the same level, with the
    page number of the entry before it (approximate until Word updates the field). Entries whose
    bookmark is in no heading are dropped. Returns (kept, renamed, added, dropped texts)."""
    import copy
    from docx.oxml import OxmlElement
    from docx.oxml.ns import qn
    from app_req_v5.common import strip_ids

    body = doc.element.body
    w_t = qn("w:t")
    xml_space = "{http://www.w3.org/XML/1998/namespace}space"

    def instr(el):
        return "".join(t.text or "" for t in el.iter(qn("w:instrText")))

    def pstyle(p):
        ps = p.find(qn("w:pPr") + "/" + qn("w:pStyle"))
        return ps.get(qn("w:val")) if ps is not None else None

    def link(p):
        h = p.find(qn("w:hyperlink"))
        if h is None:
            raise SystemExit("TOC entry without a hyperlink")
        return h

    def text_runs(p):
        runs = []
        for child in link(p):
            if child.tag != qn("w:r"):
                continue
            if child.find(qn("w:tab")) is not None:
                break
            if child.find(w_t) is not None:
                runs.append(child)
        if not runs:
            raise SystemExit("TOC entry without text")
        return runs

    def entry_text(p):
        return "".join(t.text or "" for r in text_runs(p) for t in r.iter(w_t))

    def set_entry_text(p, text):
        runs = text_runs(p)
        t = runs[0].find(w_t)
        t.text = text
        t.set(xml_space, "preserve")
        for r in runs[1:]:
            r.getparent().remove(r)

    def page_t(p):
        ts = list(link(p).iter(w_t))
        if len(ts) < 2:
            raise SystemExit("TOC entry without a page number")
        return ts[-1]

    sdt = next((el for el in body.iterchildren(qn("w:sdt")) if "TOC \\o" in instr(el)), None)
    if sdt is None:
        raise SystemExit("TOC content control not found")
    content = sdt.find(qn("w:sdtContent"))
    levels = {"Heading1": "TOC1", "Heading2": "TOC2", "Heading3": "TOC3"}
    entries = [p for p in content.findall(qn("w:p")) if pstyle(p) in levels.values()]
    if not entries or "TOC \\o" not in instr(entries[0]):
        raise SystemExit("the first TOC entry does not hold the field start")
    by_anchor = {link(p).get(qn("w:anchor")): p for p in entries}
    template = {}
    for p in entries[1:]:
        template.setdefault(pstyle(p), p)

    starts = list(body.iter(qn("w:bookmarkStart")))
    names = {b.get(qn("w:name")) or "" for b in starts}
    next_id = max((int(b.get(qn("w:id"))) for b in starts), default=0) + 1
    nums = [int(n[4:]) for n in names if n.startswith("_Toc") and n[4:].isdigit()]
    next_num = (max(nums) + 1) if nums else 100000000

    out, kept, renamed, added = [], 0, 0, 0
    for el in body.iterchildren(qn("w:p")):
        lvl = levels.get(pstyle(el))
        if lvl is None:
            continue
        text = "".join(t.text or "" for t in el.iter(w_t)).strip()
        if not text:
            continue
        bm = next((b.get(qn("w:name")) for b in el.iter(qn("w:bookmarkStart"))
                   if (b.get(qn("w:name")) or "").startswith("_Toc")), None)
        if bm is None:
            bm = f"_Toc{next_num}"
            next_num += 1
            start, end = OxmlElement("w:bookmarkStart"), OxmlElement("w:bookmarkEnd")
            start.set(qn("w:id"), str(next_id))
            start.set(qn("w:name"), bm)
            end.set(qn("w:id"), str(next_id))
            next_id += 1
            ppr = el.find(qn("w:pPr"))
            if ppr is not None:
                ppr.addnext(start)
            else:
                el.insert(0, start)
            el.append(end)
        entry = by_anchor.get(bm)
        if entry is not None:
            if pstyle(entry) != lvl:
                entry.find(qn("w:pPr") + "/" + qn("w:pStyle")).set(qn("w:val"), lvl)
            if entry_text(entry) != text:
                set_entry_text(entry, text)
                renamed += 1
            else:
                kept += 1
        else:
            if lvl not in template:
                raise SystemExit(f"no TOC entry of level {lvl} to copy")
            entry = strip_ids(copy.deepcopy(template[lvl]))
            link(entry).set(qn("w:anchor"), bm)
            for it in entry.iter(qn("w:instrText")):
                if "PAGEREF" in (it.text or ""):
                    it.text = f" PAGEREF {bm} \\h "
            set_entry_text(entry, text)
            page_t(entry).text = page_t(out[-1]).text if out else "1"
            added += 1
        out.append(entry)
    if not out or out[0] is not entries[0]:
        raise SystemExit("the first heading must keep the TOC field's first entry")

    keep = {id(p) for p in out}
    dropped = [entry_text(p) for p in entries if id(p) not in keep]
    anchor = entries[0].getprevious()
    for p in entries:
        content.remove(p)
    for p in out:
        anchor.addnext(p)
        anchor = p
    return kept, renamed, added, dropped


def _update_fields_on_open(doc):
    """The document has a TOC: ask Word to refresh fields when it opens V5."""
    from docx.oxml import OxmlElement
    from docx.oxml.ns import qn
    settings = doc.settings.element
    if settings.find(qn("w:updateFields")) is None:
        uf = OxmlElement("w:updateFields")
        uf.set(qn("w:val"), "true")
        settings.append(uf)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", help="comma-separated modules to run (default: all)")
    ap.add_argument("--out", help="output path (default: the V5 path in Documents)")
    args = ap.parse_args()

    out = os.path.abspath(args.out or DST)
    if out == os.path.abspath(SRC):
        raise SystemExit("refusing to overwrite the source document")
    if not os.path.exists(SRC):
        raise SystemExit(f"source not found: {SRC}")
    mods = MODULES if not args.only else [m.strip() for m in args.only.split(",") if m.strip()]
    unknown = [m for m in mods if m not in MODULES]
    if unknown:
        raise SystemExit(f"unknown module(s): {unknown}")

    doc = Document(SRC)
    ctx = Ctx(doc)
    ctx.FW, ctx.FW_FIELD, ctx.DATE, ctx.AUTHOR = FW, FW_FIELD, DATE, AUTHOR
    for name in mods:
        mod = importlib.import_module(f"app_req_v5.{name}")
        ctx.module = name
        try:
            mod.apply(ctx)
        except EditError as e:
            raise SystemExit(f"[{name}] edit failed, nothing written: {e}")
    ctx.module = None

    toc_kept, toc_renamed, toc_added, toc_dropped = _sync_cached_toc(doc)
    dropped = _dedupe_para_ids(doc)
    _update_fields_on_open(doc)
    cp = doc.core_properties
    cp.author = AUTHOR
    cp.last_modified_by = AUTHOR
    cp.modified = datetime.datetime.strptime(DATE, "%d/%m/%Y")
    cp.revision = (cp.revision or 0) + 1
    cp.comments = ""
    doc.save(out)

    # Post-save checks.
    Document(out)  # re-opens
    b4, b5 = _blips(SRC), _blips(out)
    if b4 != b5:
        raise SystemExit(f"image count changed: V4 {b4}, output {b5}")
    with zipfile.ZipFile(out) as z:
        xml = z.read("word/document.xml").decode("utf8")
    ids = re.findall(r'w14:paraId="([0-9A-Fa-f]+)"', xml)
    dup = [k for k, v in collections.Counter(ids).items() if v > 1]
    if dup:
        raise SystemExit(f"duplicate w14:paraId after save: {dup[:5]}")
    with zipfile.ZipFile(SRC) as z:
        xml4 = z.read("word/document.xml").decode("utf8")
    for bad in ("TBD", "TODO", "FIXME", "<placeholder", "[insert"):
        if xml.count(bad) > xml4.count(bad):
            raise SystemExit(f"placeholder text {bad!r} added in the output")

    print(f"wrote: {out}  (modules: {', '.join(mods)}; images {b5}; paraIds dropped {dropped})")
    print(f"cached TOC: {toc_kept} entries kept, {toc_renamed} renamed, {toc_added} added, "
          f"{len(toc_dropped)} dropped {toc_dropped}")
    shared = {k: sorted(v) for k, v in ctx.touched.items() if len(v) > 1}
    if shared:
        print("anchors touched by more than one module:")
        for k, v in sorted(shared.items()):
            print(f"  {k}: {', '.join(v)}")
    print("\nchanges applied:")
    for m, t in ctx.notes:
        print(f"  [{m}] {t}")


if __name__ == "__main__":
    main()
