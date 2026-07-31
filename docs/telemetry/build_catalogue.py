#!/usr/bin/env python3
# ruff: noqa: E501
"""Deterministic build for the eFloStop II Hub Telemetry Catalogue.

Generates, from one source of truth (fields.json + the catalogue_*.py modules):

    eFloStop2_Hub_Telemetry_Catalogue_v1.0.docx
    telemetry_catalogue.md
    field_registry.csv
    schemas/<message_type>.schema.json

Run:  python docs/telemetry/build_catalogue.py
Requires: python-docx.  Optional: jsonschema (validates Appendix B), pywin32 (TOC + PDF).

The build FAILS rather than warns if the completeness contract is violated. That contract is:
91 D2C fields, every one cited, every one in exactly one §12 category and at least one message
type, and every JSON Schema validating its own maximal payload.
"""
import csv
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

import catalogue_data as D          # noqa: E402
import catalogue_content as C       # noqa: E402
import catalogue_reference as R     # noqa: E402
import catalogue_options as O       # noqa: E402
import catalogue_payloads as P      # noqa: E402

from docx import Document                                    # noqa: E402
from docx.enum.section import WD_ORIENT, WD_SECTION          # noqa: E402
from docx.enum.table import WD_TABLE_ALIGNMENT               # noqa: E402
from docx.enum.text import WD_ALIGN_PARAGRAPH                # noqa: E402
from docx.oxml import OxmlElement                            # noqa: E402
from docx.oxml.ns import qn                                  # noqa: E402
from docx.shared import Inches, Pt, RGBColor                 # noqa: E402

FIELDS = json.load(open(os.path.join(HERE, "fields.json"), encoding="utf-8"))
EXPECTED_FIELD_COUNT = 91
MONO = "Consolas"
BODY = "Calibri"

# --------------------------------------------------------------------------------------
# Build-time facts pulled from the repo so they cannot go stale
# --------------------------------------------------------------------------------------


def git(*args):
    try:
        return subprocess.check_output(["git", "-C", REPO, *args], text=True,
                                       stderr=subprocess.DEVNULL).strip()
    except Exception:
        return "unavailable"


def fw_version():
    """PROJECT_VER is the single source of truth for the firmware version."""
    path = os.path.join(REPO, "CMakeLists.txt")
    for i, line in enumerate(open(path, encoding="utf-8"), 1):
        m = re.search(r'set\(PROJECT_VER\s+"([^"]+)"\)', line)
        if m:
            return m.group(1), f"CMakeLists.txt:{i}"
    raise SystemExit("PROJECT_VER not found in CMakeLists.txt")


FW, FW_CITE = fw_version()
SHA = git("rev-parse", "HEAD")
SHA_SHORT = git("rev-parse", "--short", "HEAD")
BRANCH = git("rev-parse", "--abbrev-ref", "HEAD")
DIRTY = git("status", "--porcelain")
TREE_STATE = ("Working tree has uncommitted changes at build time; this document describes the "
              "working tree, not the committed HEAD." if DIRTY else "Working tree clean.")

# --------------------------------------------------------------------------------------
# Message-type normalisation.  fields.json carries free text; the rest of the build needs
# a controlled vocabulary, so map once here and assert nothing falls through.
# --------------------------------------------------------------------------------------

EVENT_FAMILY = {
    "valve_event": ["valve_state_changed", "valve_flood_detected", "valve_flood_cleared"],
    "leak_event": ["leak_detected", "leak_cleared"],
    "rules_event": ["auto_close", "auto_close_blocked_override", "auto_close_reenabled",
                    "rmleak_cleared", "rmleak_auto_cleared", "water_access_override_enabled",
                    "water_access_override_expired", "rules_engine"],
    "health_event": ["device_offline", "device_recovered", "health_engine"],
    "cmd_ack": ["cmd_ack"],
}


def message_types_for(field):
    """Map a field's free-text message_types onto the controlled D2C vocabulary.

    Matching is on WORD BOUNDARIES, not substrings. A naive `in` test is wrong here because
    several event names are substrings of others - notably "leak_cleared" inside
    "rmleak_cleared", which would file a rules-engine field as a leak event.
    """
    raw = (field.get("message_types") or "").lower()

    def has(token):
        return re.search(r"(?<![a-z0-9_])" + re.escape(token) + r"(?![a-z0-9_])", raw) is not None

    out = set()
    if has("lifecycle"):
        out.add("lifecycle")
    if has("snapshot"):
        out.add("snapshot")
    for fam, markers in EVENT_FAMILY.items():
        if any(has(m) for m in markers):
            out.add(fam)
    # A bare "event" with no named family means every event shape carries it (envelope fields).
    if has("event") and not (out - {"lifecycle", "snapshot"}):
        out.update(EVENT_FAMILY.keys())
    return sorted(out)


for f in FIELDS:
    f["_mt"] = message_types_for(f)

# --------------------------------------------------------------------------------------
# Contract assertions.  A violation fails the build.
# --------------------------------------------------------------------------------------

SECTION_IDS = [s[0] for s in D.SECTIONS_12]


def check_contract():
    errs = []
    if len(FIELDS) != EXPECTED_FIELD_COUNT:
        errs.append(f"field count {len(FIELDS)} != contracted {EXPECTED_FIELD_COUNT}")

    paths = [f["json_path"] for f in FIELDS]
    dupes = {p for p in paths if paths.count(p) > 1}
    if dupes:
        errs.append(f"duplicate json_path values: {sorted(dupes)}")

    for f in FIELDS:
        p = f["json_path"]
        if not (f.get("citation") or "").strip():
            errs.append(f"{p}: no citation")
        if not re.search(r"\.(c|h|cpp|csv|txt)\s*:\s*\d+", f.get("citation", "")):
            errs.append(f"{p}: citation has no path:line form -> {f.get('citation')!r}")
        if not (f.get("meaning") or "").strip():
            errs.append(f"{p}: no plain-English meaning")
        if f.get("section") not in SECTION_IDS:
            errs.append(f"{p}: section {f.get('section')!r} is not one of the 12 categories")
        if not f["_mt"]:
            errs.append(f"{p}: maps to no message type (message_types={f.get('message_types')!r})")

    # Every non-empty category must actually be populated, and the declared-empty ones must be empty.
    for sid, title, _desc in D.SECTIONS_12:
        n = sum(1 for f in FIELDS if f["section"] == sid)
        declared_empty = sid in ("12.7", "12.11")
        if declared_empty and n:
            errs.append(f"{sid} is declared empty but has {n} fields")
        if not declared_empty and not n:
            errs.append(f"{sid} has no fields but is not declared empty")

    # Enum-valued fields must list their members.
    enum_paths = {p for e in R.ENUMS for p in e["paths"]}
    for f in FIELDS:
        r = (f.get("range_or_enum") or "")
        if r.strip().lower().startswith("enum:") and f["json_path"] not in enum_paths:
            errs.append(f"{f['json_path']}: declared an enum but has no member list in ENUMS")

    if errs:
        print("BUILD FAILED - completeness contract violated:", file=sys.stderr)
        for e in errs:
            print("  -", e, file=sys.stderr)
        raise SystemExit(1)
    print(f"contract OK: {len(FIELDS)} fields, all cited, all categorised, all mapped to a message type")


# --------------------------------------------------------------------------------------
# JSON Schema generation (draft 2020-12) from the maximal payloads + field metadata
# --------------------------------------------------------------------------------------

BY_PATH = {f["json_path"]: f for f in FIELDS}
JSON_TYPE = {"string": "string", "number": "number", "boolean": "boolean",
             "object": "object", "array": "array"}


# fields.json describes the TELEMETRY plane only. Annotating a twin or command schema from it
# would attach, for example, the D2C `schema` literal "eflostop.v2" to the C2D envelope key of
# the same name - which is a different constant entirely. So metadata lookup is plane-aware.
TWIN_R_META = {k: (typ, presence, meaning, cite) for k, typ, presence, meaning, cite in R.TWIN_REPORTED}
TWIN_D_META = {k: (typ, effect, cite) for k, typ, effect, cite in R.TWIN_DESIRED}
C2D_META = {
    "schema": ("string", 'Envelope identifier. Canonical "eflostop.cmd"; the legacy form is '
                         '"eflostop.cmd.v1". A legacy plain-text form carries no envelope at all.',
               "main/commands/c2d_commands.h:13-17; rejected otherwise main/commands/c2d_commands.c:80-86"),
    "ver": ("number", "Envelope version. C2D_CMD_SCHEMA_VER is 1.", "main/commands/c2d_commands.h:14"),
    "id": ("string", "Optional correlation id, echoed back as data.id on the resulting cmd_ack. "
                     "If absent, cmd_ack omits data.id entirely.",
           "main/telemetry/telemetry_v2.c:705-706"),
    "cmd": ("string", "Which command to run. One of the eleven names in section 14.",
            "main/commands/c2d_commands.h:35-45"),
    "payload": ("object", "Command-specific arguments. The shape depends entirely on cmd, so this "
                          "schema does not constrain it - see section 14 for each command's keys.",
                "main/iothub/app_iothub.c:657-935"),
}
DPS_META = {
    "registrationId": ("string", "The device's registration id, which is its gateway id. The only "
                                 "key in the payload: no tags and no model id, so DPS enrolment "
                                 "tags cannot originate in firmware.",
                       "main/dps_client/dps_client.c:283-284"),
}


# Enum members come from the CURATED ENUMS tables, not from parsing the prose range_or_enum
# string. The prose carries trailing explanations ("disconnected (unknown = ...)") and quoting
# styles that a parser gets wrong, producing enums no real payload can satisfy.
ENUM_MEMBERS = {}
for _e in R.ENUMS:
    for _p in _e["paths"]:
        ENUM_MEMBERS[_p] = [m[0] for m in _e["members"]]


def parse_enum(path, range_or_enum):
    """Return ('enum', members) / ('const', value) / (None, None) for a field path."""
    if path in ENUM_MEMBERS:
        return ("enum", ENUM_MEMBERS[path])
    m = re.match(r'^Fixed literal\s+"(.+)"$', (range_or_enum or "").strip())
    if m:
        return ("const", m.group(1))
    return (None, None)


def schema_for(value, path, minimal, plane):
    """Build a schema node.

    `minimal` is the corresponding subtree of the minimal payload; a key is `required` only if it
    is present there. Deriving `required` from the field's own optional flag is wrong for union
    payloads such as the rules-engine family, where the maximal example is a union across seven
    shapes and no single message carries all of them.
    """
    meta = BY_PATH.get(path) if plane == "d2c" else None
    node = {}

    if isinstance(value, dict):
        node["type"] = "object"
        props, required = {}, []
        for k, v in value.items():
            child = f"{path}.{k}" if path else k
            sub_min = minimal.get(k) if isinstance(minimal, dict) else None
            props[k] = schema_for(v, child, sub_min if sub_min is not None else {}, plane)
            if isinstance(minimal, dict) and k in minimal:
                required.append(k)
        node["properties"] = props
        if required:
            node["required"] = required
        # The C2D payload shape depends on which command it belongs to, so it is left open.
        node["additionalProperties"] = (path == "payload" and plane == "c2d")
    elif isinstance(value, list):
        node["type"] = "array"
        sub_min = minimal[0] if isinstance(minimal, list) and minimal else {}
        node["items"] = schema_for(value[0], f"{path}[]", sub_min, plane) if value else {}
    elif isinstance(value, bool):
        node["type"] = "boolean"
    elif isinstance(value, (int, float)):
        node["type"] = "number"
    else:
        node["type"] = "string"

    if meta:
        declared = (meta.get("type") or "").lower()
        if "null" in declared and node.get("type") not in (None, "object"):
            node["type"] = [node["type"], "null"]
        bits = [meta.get("meaning", "")]
        units = meta.get("units", "")
        if units and units not in ("n/a", ""):
            bits.append(f"Units: {units}.")
        rng = meta.get("range_or_enum", "")
        if rng and rng not in ("n/a", ""):
            bits.append(f"Range/enum: {rng}.")
        gating = meta.get("gating", "")
        if gating and gating != "always":
            bits.append(f"Presence: {gating}.")
        node["description"] = " ".join(b for b in bits if b)
        node["x-source"] = meta.get("citation", "")
        node["x-category"] = meta.get("section", "")
        # Only STRING-typed fields get a JSON Schema enum. "true | false" on a boolean field is
        # prose describing the two boolean values, not a set of string literals.
        is_string = node.get("type") == "string"
        kind, members = parse_enum(path, rng)
        if kind == "enum" and is_string and "null" not in declared:
            node["enum"] = members
        elif kind == "const" and is_string:
            node["const"] = members
    else:
        leaf = path.split(".")[-1]
        table = {"twin_r": TWIN_R_META, "twin_d": TWIN_D_META, "c2d": C2D_META, "dps": DPS_META}.get(plane)
        if table and leaf in table:
            entry = table[leaf]
            node["description"] = entry[-2] if len(entry) >= 3 else entry[0]
            node["x-source"] = entry[-1]
    return node


PLANE_OF = {"twin_reported": "twin_r", "twin_desired": "twin_d",
            "c2d_command": "c2d", "dps_registration": "dps"}


def build_schemas(outdir):
    os.makedirs(outdir, exist_ok=True)
    written = []
    for mt_id, spec in P.PAYLOADS.items():
        plane_key = PLANE_OF.get(mt_id, "d2c")
        mt = next((m for m in D.MESSAGE_TYPES if m["id"] == mt_id), None)
        title = mt["title"] if mt else mt_id
        plane = mt["plane"] if mt else spec.get("plane_note", "")
        body = schema_for(spec["maximal"], "", spec.get("minimal") or {}, plane_key)
        # Union-shaped families declare their own intersection explicitly.
        if spec.get("data_required") is not None and "data" in body.get("properties", {}):
            body["properties"]["data"]["required"] = list(spec["data_required"])
        # A key whose vocabulary differs per message type narrows its enum here, so a schema
        # never permits a value that message type cannot actually carry.
        for dotted, members in (spec.get("enum_overrides") or {}).items():
            node = body
            for seg in dotted.split("."):
                node = node.get("properties", {}).get(seg)
                if node is None:
                    break
            if node is not None:
                node["enum"] = list(members)
        # The envelope discriminator is a fixed literal per telemetry message family.
        if plane_key == "d2c" and mt and mt.get("type_value"):
            tnode = body.setdefault("properties", {}).setdefault("type", {})
            tnode["const"] = mt["type_value"]
            # const is narrower than the three-member envelope enum, so drop the enum here.
            tnode.pop("enum", None)
        schema = {
            "$schema": "https://json-schema.org/draft/2020-12/schema",
            "$id": f"https://eflostop.local/schemas/{mt_id}.schema.json",
            "title": title,
            "description": (
                f"DESCRIPTIVE, NOT NORMATIVE. Generated from firmware source at FW {FW}, which is the "
                f"WORKING TREE and is not committed. The nearest commit is {SHA_SHORT}, which is a "
                f"different firmware version - do not read x-commit as containing this behaviour. "
                f"This is not a specification and not a proposal; it records what the device emits "
                f"today. Plane: {plane}. `required` lists only keys present in the minimal payload, "
                f"i.e. those always emitted for this message type."),
            "x-plane": plane,
            "x-firmware-version": FW,
            "x-firmware-version-source": FW_CITE,
            "x-tree-state": ("working tree, uncommitted" if DIRTY else "clean, matches x-nearest-commit"),
            "x-nearest-commit": SHA,
            **body,
        }
        path = os.path.join(outdir, f"{mt_id}.schema.json")
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(schema, fh, indent=2, ensure_ascii=False)
            fh.write("\n")
        written.append((mt_id, path, schema))
    return written


def validate_schemas(written):
    try:
        import jsonschema
    except ImportError:
        print("jsonschema not installed - skipping Appendix B validation")
        return
    for mt_id, _path, schema in written:
        for kind in ("maximal", "minimal"):
            payload = P.PAYLOADS[mt_id].get(kind)
            if payload is None:
                continue
            json.loads(json.dumps(payload))  # must be JSON-serialisable
            # BOTH forms must validate. Validating only the maximal payload hides an
            # over-strict `required` list, which is exactly how the union-shaped rules-engine
            # schema wrongly demanded ten keys that no single message carries.
            jsonschema.Draft202012Validator(schema).validate(payload)
    print(f"schemas OK: {len(written)} generated; maximal AND minimal payloads both validate")


# --------------------------------------------------------------------------------------
# CSV
# --------------------------------------------------------------------------------------

CSV_COLUMNS = ["json_path", "field_name", "section", "category", "type", "units",
               "range_or_enum", "default", "optional", "gating", "message_types",
               "meaning", "citation"]


def build_csv(path):
    with open(path, "w", encoding="utf-8", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(CSV_COLUMNS)
        for f in sorted(FIELDS, key=lambda x: x["json_path"]):
            w.writerow([
                f["json_path"], f["json_path"].split(".")[-1], f["section"], f["category"],
                f["type"], f["units"], f["range_or_enum"], f["default"], f["optional"],
                f["gating"], "; ".join(f["_mt"]), f["meaning"], f["citation"],
            ])
    n = sum(1 for _ in open(path, encoding="utf-8")) - 1
    assert n == EXPECTED_FIELD_COUNT, f"CSV wrote {n} rows, expected {EXPECTED_FIELD_COUNT}"
    print(f"csv OK: {n} rows")


# --------------------------------------------------------------------------------------
# DOCX helpers
# --------------------------------------------------------------------------------------


def set_cell_bg(cell, hexcolor):
    tcPr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:fill"), hexcolor)
    tcPr.append(shd)


def repeat_header(row):
    trPr = row._tr.get_or_add_trPr()
    el = OxmlElement("w:tblHeader")
    el.set(qn("w:val"), "true")
    trPr.append(el)


def add_field(paragraph, instr):
    r = paragraph.add_run()
    fc = OxmlElement("w:fldChar"); fc.set(qn("w:fldCharType"), "begin"); r._r.append(fc)
    r2 = paragraph.add_run()
    it = OxmlElement("w:instrText"); it.set(qn("xml:space"), "preserve"); it.text = instr
    r2._r.append(it)
    r3 = paragraph.add_run()
    fс = OxmlElement("w:fldChar"); fс.set(qn("w:fldCharType"), "separate"); r3._r.append(fс)
    r4 = paragraph.add_run("Right-click and choose Update Field")
    r5 = paragraph.add_run()
    fe = OxmlElement("w:fldChar"); fe.set(qn("w:fldCharType"), "end"); r5._r.append(fe)
    return r4


def mono_para(doc, text, size=8, style=None):
    p = doc.add_paragraph(style=style)
    for i, line in enumerate(text.split("\n")):
        if i:
            p.add_run().add_break()
        run = p.add_run(line)
        run.font.name = MONO
        run.font.size = Pt(size)
    p.paragraph_format.space_after = Pt(6)
    return p


def rich_para(doc, text, size=10.5):
    """Body text: `code` -> Consolas, **bold** -> bold, *italic* -> italic.

    No markup may survive into the rendered document, so every marker recognised here is
    consumed. assert_no_markdown() re-checks the output for anything that slipped through.
    """
    p = doc.add_paragraph()
    # ** must be tried before * so bold wins over italic.
    for chunk in re.split(r"(`[^`]+`|\*\*[^*]+\*\*|\*[^*\s][^*]*\*)", text):
        if not chunk:
            continue
        if chunk.startswith("`") and chunk.endswith("`"):
            r = p.add_run(chunk[1:-1]); r.font.name = MONO; r.font.size = Pt(size - 1)
        elif chunk.startswith("**") and chunk.endswith("**"):
            r = p.add_run(chunk[2:-2]); r.bold = True; r.font.size = Pt(size)
        elif chunk.startswith("*") and chunk.endswith("*") and len(chunk) > 2:
            r = p.add_run(chunk[1:-1]); r.italic = True; r.font.size = Pt(size)
        else:
            r = p.add_run(chunk); r.font.size = Pt(size)
    return p


def add_table(doc, headers, rows, widths=None, mono_cols=(), size=8):
    t = doc.add_table(rows=1, cols=len(headers))
    t.style = "Table Grid"
    t.alignment = WD_TABLE_ALIGNMENT.CENTER
    hdr = t.rows[0]
    repeat_header(hdr)
    for i, h in enumerate(headers):
        cell = hdr.cells[i]
        cell.text = ""
        run = cell.paragraphs[0].add_run(h)
        run.bold = True
        run.font.size = Pt(size)
        set_cell_bg(cell, "DDDDDD")
    for row in rows:
        cells = t.add_row().cells
        for i, val in enumerate(row):
            cells[i].text = ""
            para = cells[i].paragraphs[0]
            # Table cells are not markdown-rendered, so any backtick a source string carries
            # would print literally. Identifier columns already use mono_cols for emphasis.
            text = "" if val is None else str(val).replace("`", "")
            run = para.add_run(text)
            run.font.size = Pt(size)
            if i in mono_cols:
                run.font.name = MONO
                run.font.size = Pt(size - 0.5)
            para.paragraph_format.space_after = Pt(0)
    if widths:
        for i, w in enumerate(widths):
            for row in t.rows:
                row.cells[i].width = Inches(w)
    doc.add_paragraph()
    return t


def h(doc, text, level):
    p = doc.add_heading(text, level=level)
    return p


def landscape_section(doc):
    s = doc.add_section(WD_SECTION.NEW_PAGE)
    s.orientation = WD_ORIENT.LANDSCAPE
    s.page_width, s.page_height = s.page_height, s.page_width
    s.left_margin = s.right_margin = Inches(0.5)
    s.top_margin = s.bottom_margin = Inches(0.5)
    return s


def portrait_section(doc):
    s = doc.add_section(WD_SECTION.NEW_PAGE)
    s.orientation = WD_ORIENT.PORTRAIT
    if s.page_width > s.page_height:
        s.page_width, s.page_height = s.page_height, s.page_width
    s.left_margin = s.right_margin = Inches(0.8)
    s.top_margin = s.bottom_margin = Inches(0.8)
    return s


def json_block(doc, obj):
    mono_para(doc, json.dumps(obj, indent=2, ensure_ascii=False), size=7.5)


def setup_header_footer(section, title):
    hdr = section.header.paragraphs[0]
    hdr.text = ""
    r = hdr.add_run(title)
    r.font.size = Pt(8); r.font.color.rgb = RGBColor(0x60, 0x60, 0x60)
    hdr.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    ftr = section.footer.paragraphs[0]
    ftr.text = ""
    ftr.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = ftr.add_run("Page "); r.font.size = Pt(8)
    add_field(ftr, "PAGE")
    r = ftr.add_run(" of "); r.font.size = Pt(8)
    add_field(ftr, "NUMPAGES")


# --------------------------------------------------------------------------------------
# The document
# --------------------------------------------------------------------------------------


def build_docx(out_path):
    doc = Document()
    st = doc.styles["Normal"]
    st.font.name = BODY
    st.font.size = Pt(10.5)

    sec0 = doc.sections[0]
    sec0.left_margin = sec0.right_margin = Inches(0.8)
    sec0.top_margin = sec0.bottom_margin = Inches(0.8)
    setup_header_footer(sec0, D.META["title"])

    # ---- Front matter ----
    t = doc.add_heading(D.META["title"], level=0)
    p = doc.add_paragraph(); r = p.add_run(D.META["subtitle"]); r.italic = True; r.font.size = Pt(13)

    add_table(doc, ["", ""], [
        ("Document version", D.META["doc_version"]),
        ("Date", D.META["date"]),
        ("Author", D.META["author"]),
        ("Repository", D.META["repo"]),
        ("Git commit", f"{SHA}  (branch {BRANCH})"),
        ("Working tree", TREE_STATE),
        ("Firmware version", f"{FW}   [{FW_CITE}]"),
        ("Telemetry schema", f"{D.META['schema_string']}   [{D.META['schema_citation']}]"),
        ("Classification", D.META["classification"]),
    ], widths=[1.8, 5.0], size=9)

    h(doc, "Revision history", 2)
    add_table(doc, ["Version", "Date", "Author", "Change"],
              [list(r) for r in D.REVISION_HISTORY], widths=[0.8, 1.0, 1.2, 3.6], size=9)

    h(doc, "Contents", 1)
    p = doc.add_paragraph()
    add_field(p, 'TOC \\o "1-3" \\h \\z \\u')
    doc.add_paragraph(
        "Word does not populate a table of contents until fields are updated. On first open, "
        "select all (Ctrl+A) and press F9, or right-click the table above and choose Update Field.")
    doc.add_page_break()

    # ==================================================================================
    # PART I
    # ==================================================================================
    h(doc, "Part I — Consolidation review", 1)
    rich_para(doc, "This half is written to be read in the meeting. Part II is the reference "
                   "catalogue you will use afterwards.")

    # ---- 1 Executive summary ----
    h(doc, "1. Executive summary", 1)
    rich_para(doc,
        "The hub sends one kind of thing to the cloud on the telemetry plane: a JSON message with a fixed "
        "envelope and one of three `type` values — `lifecycle`, `snapshot` or `event`. Those three carry seven "
        "payload families between them, built by one serializer and published through one call. A typical "
        "installation sends a full state snapshot every five minutes plus an event whenever something happens, "
        "which works out at roughly 608 KiB per device per day.")

    h(doc, "Cloud interface census", 2)
    rich_para(doc, C.CLOUD_INTERFACE_CENSUS["intro"])
    add_table(doc, ["Plane", "Mechanism", "Named things", "Direction", "Section"],
              [list(r) for r in C.CLOUD_INTERFACE_CENSUS["rows"]],
              widths=[1.4, 2.5, 0.9, 1.2, 0.9], size=9)
    rich_para(doc, C.CLOUD_INTERFACE_CENSUS["total_note"])

    h(doc, "The four issues most worth this meeting's time", 2)
    for n, (title, body) in enumerate([
        ("A leak sensor that goes silent reports itself dry (F-01)",
         "When the cache merge is skipped the snapshot writes `leak_state:false` while writing `null` for "
         "`battery`, `rssi` and `fw_version` in the same object. A sensor that reported a leak and then went "
         "offline therefore flips to 'dry' on the next snapshot, while the `leak_detected` event already sent "
         "for it is never retracted. A consumer treating snapshots as authoritative will silently clear a live "
         "leak. This is the one place where the encoding can cause under-reporting of a safety-critical "
         "condition. `last_seen_age_s` keeps its real value in this case and is the only in-payload signal "
         "that contradicts the false."),
        ("Telemetry produced before the clock syncs is destroyed, not delayed (F-03)",
         "Messages built before SNTP completes are deleted inside the envelope builder and never reach the "
         "offline buffer. Leak events are affected, and so are command acknowledgements — so the cloud can see "
         "a timeout on a command that actually executed, which matters for anything non-idempotent."),
        ("Three inconsistent representations of 'a device' (§4.6)",
         "The valve, a LoRa sensor and a BLE leak sensor are described by three differently-shaped objects, and "
         "the same sensor appears nested inside an array in a snapshot but flattened onto `data` in an event. "
         "Any consumer needs several extraction paths for what is conceptually one entity."),
        ("The constraints are unusually weak, and that is the opportunity (§6)",
         "This is a prototype-phase product: every hub can be updated, and the Watts Digital application has not "
         "been written yet, so no downstream parser has to be migrated in step. Almost nothing about the current "
         "wire format is actually frozen. The practical cost of a change is a reflash per hub plus keeping two "
         "identifier couplings in step (§6.3). That makes this the cheapest moment in the product's life to "
         "settle the structure — and it means an irregularity left in place now is one the application will be "
         "built around and will carry forward."),
    ], 1):
        h(doc, f"1.{n} {title}", 3)
        rich_para(doc, body)

    h(doc, "Deliberately not on the agenda", 2)
    rich_para(doc,
        "Three things are worth explicitly recording as settled so the meeting does not reopen them. "
        "**Naming is already consistent** — every key on every plane is lowercase with underscores; there is no "
        "camelCase anywhere (§4.1). **The envelope is uniform** — one builder, no conditional branches, "
        "identical across all seven payload families (§4.6). **Booleans and enums are already consistent** — "
        "all 15 booleans are real JSON booleans and all 19 enums are strings (§4.5).")

    h(doc, "Consent agenda — decidable in two minutes each", 2)
    rich_para(doc,
        "The 12 `data.lora_sensors[]` fields describe hardware that is not populated on the current board, so "
        "the array is always empty. Four fields are constant on the wire and carry no information (§5, O-13). "
        "Eleven of the twelve twin-reported keys duplicate a telemetry field (§4.8). Each of these can be "
        "decided by exception, which is what buys the time for the four issues above.")

    h(doc, "Two product-level absences", 2)
    rich_para(doc,
        "**This product reports no flow or water-usage data at all.** No field in the contract carries volume, "
        "flow rate or consumption. Usage analytics and consumption billing cannot be built on this telemetry as "
        "it stands (§12.7, Q-15). **There is no audit surface.** Eleven commands can change the system's state, "
        "including closing the valve, and the only record is a `cmd_ack` that says whether the command "
        "succeeded — it carries no actor identity, no authentication result and no tamper events. A "
        "who-closed-my-valve trail is not available today (§12.11, Q-16).")

    h(doc, "Ground rules", 2)
    rich_para(doc, C.GROUND_RULES)

    # ---- 2 Purpose & how to read ----
    h(doc, "2. Purpose, audience and how to read this document", 1)
    rich_para(doc,
        "This document is an input to a joint firmware/backend consolidation review. It is not a specification "
        "and not a proposal. Its purpose is to put the entire telemetry surface in front of both teams so the "
        "conversation starts from shared facts rather than half-remembered field names.")
    rich_para(doc,
        "**Stance.** Findings are stated as *this is what exists*, not as *this is wrong*. Where change is "
        "possible, §7 lays out options with their costs and risks, and every option set begins with a no-change "
        "option. No option is marked as chosen, and no proposed schema appears anywhere. Those decisions belong "
        "to the meeting.")
    rich_para(doc,
        "**Reading contract.** Pre-read §1, about fifteen minutes. In the meeting, work from §4, §6 and §7. "
        "Part II is for afterwards, when you are building against the contract.")
    h(doc, "Conventions", 2)
    rich_para(doc,
        "Every factual claim carries a `path:line` citation into the firmware source, so anything here can be "
        "checked in the room by someone who knows the code. `[INFERRED]` marks a statement that goes beyond "
        "literal code, with the reasoning given. `[UNVERIFIED]` marks something that could not be resolved from "
        "the firmware repository; all such items are collected in §19. `[EXTERNAL]` marks a citation pointing "
        "outside this repository, into ESP-IDF or cJSON.")
    rich_para(doc,
        "**Plane** means one of the four independent channels between hub and cloud — telemetry, twin, command "
        "and provisioning. They have different topics, directions and delivery guarantees, and conflating them "
        "is the most common way to build a wrong parser, so every path in this document states its plane.")
    rich_para(doc,
        "**Out of scope.** The valve and leak-sensor firmware are separate projects and are not in this "
        "repository; what is documented here is what the hub parses and forwards. The Watts Digital app is also "
        "outside this repository, which is why §6.2 states its contract as unknown rather than assuming it.")

    h(doc, "How many fields must your ingest really handle?", 2)
    rich_para(doc, C.LIVENESS_RECONCILIATION["intro"])
    add_table(doc, ["Class", "Count", "What it means for you"],
              [[a, b, c] for a, b, c in C.LIVENESS_RECONCILIATION["rows"]],
              widths=[1.6, 0.7, 4.5], size=9)
    rich_para(doc, f"Total: {C.LIVENESS_RECONCILIATION['total']} fields. {C.LIVENESS_RECONCILIATION['citation']}")

    # ---- 3 Architecture ----
    h(doc, "3. Telemetry architecture overview", 1)
    h(doc, "3.1 What the hardware actually is", 2)
    for para in D.ARCHITECTURE_INTRO.split("\n\n"):
        rich_para(doc, para)
    mono_para(doc, """
   STM32WBA leak sensors  ──BLE advertising──┐
   (broadcast only, never connect)           │
                                             ├──►  ESP32-S3 hub  ──MQTT/TLS──►  Azure IoT Hub
   STM32WB valve  ────────BLE GATT───────────┤          │
   (connects, notifies, accepts commands)    │          └── DPS, first boot only
                                             │
   LoRa sensors  ─────────not populated──────┘
   (supported in firmware, DNP on this PCBA)
""", size=8.5)

    h(doc, "3.2 The four planes", 2)
    rich_para(doc,
        "Everything between the hub and Azure runs over one of four planes. The telemetry plane is the subject "
        "of this document; the other three are described so the boundary is unambiguous.")
    add_table(doc, ["Plane", "Direction", "Topic", "Envelope", "Note"],
              [[p["plane"], p["direction"], p["topic"], p["envelope"], p["note"]] for p in D.PLANES],
              widths=[1.1, 1.0, 1.7, 1.4, 1.9], size=8, mono_cols=(2,))

    h(doc, "3.3 Provisioning, briefly", 2)
    rich_para(doc,
        "On first boot the hub contacts the Azure Device Provisioning Service to find out which IoT Hub it "
        "belongs to. It authenticates with a device key derived by HMAC-SHA256 from an enrolment group key, "
        "using its own gateway ID as the registration ID. The result — a hub hostname and the derived key — is "
        "cached in non-volatile storage, so subsequent boots skip the exchange entirely. Mechanics are in §15. "
        "No credential material appears in any telemetry, twin or command payload.")

    h(doc, "3.4 The five publish call sites", 2)
    rich_para(doc,
        "Ten logical message types are documented in this catalogue, but they are produced by only five places "
        "in the code that actually publish. The distinction matters: message types are a wire-contract concept, "
        "call sites are a code concept, and the mapping between them is not one to one. Note in particular that "
        "the entire telemetry plane leaves through a single call.")
    add_table(doc, ["Call site", "Plane", "Topic", "QoS", "Retain", "What triggers it"],
              [list(r) for r in D.PUBLISH_SITES],
              widths=[1.5, 1.0, 1.7, 0.35, 0.45, 2.6], size=7.5, mono_cols=(0, 2))

    # ---- 4 Consistency audit ----
    h(doc, "4. Current-state consistency audit", 1)
    rich_para(doc,
        "Each subsection below is self-contained: the evidence it relies on is printed with it, so Part I can "
        "be read without turning to the catalogue. Findings are stated neutrally.")
    for dim in C.CONSISTENCY:
        h(doc, f"{dim['id']} {dim['title']}", 2)
        add_table(doc, ["Severity", "Fixable where", "Suggested meeting time"],
                  [[dim["severity"], dim["fixable_where"], dim["meeting_time"]]],
                  widths=[2.2, 2.0, 2.4], size=9)
        for para in dim["summary"].split("\n\n"):
            rich_para(doc, para)
        if dim.get("evidence_json"):
            h(doc, "Evidence", 3)
            mono_para(doc, dim["evidence_json"], size=7.5)
        if dim.get("variants"):
            h(doc, "Variants present", 3)
            add_table(doc, ["Variant", "Where it occurs", "Citation"],
                      [list(v) for v in dim["variants"]],
                      widths=[1.9, 2.7, 2.0], size=8, mono_cols=(2,))
        if dim.get("abbreviations_note"):
            rich_para(doc, dim["abbreviations_note"])

    h(doc, "4.8 Plane collision index", 2)
    rich_para(doc, C.PLANE_COLLISIONS["intro"])
    add_table(doc, ["Leaf name", "Telemetry path", "Twin key", "Can they diverge?", "Authoritative for", "Citation"],
              [list(r) for r in C.PLANE_COLLISIONS["rows"]],
              widths=[0.9, 1.4, 1.0, 1.5, 1.6, 1.6], size=7.5, mono_cols=(1, 2, 5))

    # ---- 5 Overlap ----
    h(doc, "5. Overlap and derivation relationships", 1)
    rich_para(doc, O.OVERLAP_INTRO)
    for oid, title, desc, cost, buys, cite in O.OVERLAP:
        h(doc, f"{oid} — {title}", 3)
        rich_para(doc, desc)
        add_table(doc, ["Byte cost", "What the overlap buys"], [[cost, buys]],
                  widths=[1.2, 5.4], size=9)
        p = doc.add_paragraph(); r = p.add_run(cite); r.font.name = MONO; r.font.size = Pt(7.5)

    # ---- 6 Compatibility ----
    h(doc, "6. Compatibility envelope — what is genuinely fixed", 1)
    rich_para(doc,
        "This section separates constraints that are real from habits that merely look like constraints. Where "
        "evidence for a constraint could not be found in this repository, that is stated rather than assumed.")
    for sid, title, status, body, cite in O.COMPAT:
        h(doc, f"{sid} {title}", 2)
        p = doc.add_paragraph(); r = p.add_run(f"Status: {status}"); r.bold = True; r.font.size = Pt(10)
        for para in body.split("\n\n"):
            rich_para(doc, para)
        p = doc.add_paragraph(); r = p.add_run(cite); r.font.name = MONO; r.font.size = Pt(7.5)

    # ---- 7 Options ----
    h(doc, "7. Options per issue", 1)
    rich_para(doc,
        "Every option set below opens with **Option 0 — no change**, stated with its ongoing cost, so the price of "
        "doing nothing stays visible and comparable. Note that Option 0 is a genuine choice here rather than the "
        "only reachable outcome: all hubs can be updated and no downstream parser is committed yet (§6), so the "
        "device-side options are as available as the cloud-side ones. Nothing here is recommended and nothing is "
        "chosen. Each set ends with a decision box for the meeting to fill in.")
    rich_para(doc,
        f"**Scope of this section.** Option analysis was deliberately limited to the "
        f"{len(O.OPTIONS)} issues judged most likely to need a joint decision. Sixteen findings are raised in "
        "§19; the remainder are put to the meeting as questions in §9 rather than as option tables, because "
        "they are either single-owner decisions or too narrow to need a trade-off analysis. If the meeting "
        "wants an option table for any of the others, that is a reasonable request rather than an omission.")
    for grp in O.OPTIONS:
        h(doc, f"{grp['issue_id']} — {grp['title']}", 2)
        rich_para(doc, grp["context"])
        add_table(doc,
                  ["ID", "Option", "What it costs", "What it risks", "Blast radius", "Owner", "FW change on shipped units?", "Reversible?"],
                  [list(o) for o in grp["options"]],
                  widths=[0.6, 1.5, 1.6, 1.7, 1.2, 0.7, 0.7, 0.7], size=7.5, mono_cols=(0,))
        add_table(doc, ["Decision", "Owner", "Date", "Follow-up"], [["", "", "", ""]],
                  widths=[2.6, 1.2, 1.0, 1.8], size=9)

    # ---- 8 Migration ----
    h(doc, "8. Migration and versioning considerations", 1)
    rich_para(doc, O.VERSION_DELTA["intro"])
    add_table(doc, ["What changed", "At HEAD (1.7.0)", "In the working tree (1.8.0)", "Consequence", "Citation"],
              [list(r) for r in O.VERSION_DELTA["rows"]],
              widths=[1.4, 1.0, 1.4, 2.0, 1.4], size=8, mono_cols=(4,))
    rich_para(doc, O.VERSION_DELTA["mixed_fleet"])
    rich_para(doc,
        "**What bumping the schema string would and would not achieve.** The `eflostop.v2` constant identifies "
        "the envelope shape, not the field set. Fields have been added and an enum value renamed without it "
        "moving, so bumping it does not by itself tell a consumer which fields to expect. If a version marker "
        "is wanted for capability detection, that is a design question for the meeting rather than a property "
        "the current constant already has.")
    rich_para(doc,
        "**Dual-schema support cost.** Because fielded units cannot be updated, any change means the cloud "
        "parses two formats only for the duration of a rollout, not permanently, because every hub can be reflashed. "
        "The cost is a transition window whose length is set by how quickly hubs are updated, rather than an "
        "open-ended maintenance burden. That materially lowers the bar for making a change at all.")
    rich_para(doc,
        "**Rollback.** A device-side change cannot be rolled back remotely, for the same reason it cannot be "
        "deployed remotely. Cloud-side changes remain reversible. The reversibility of each option is recorded "
        "in its own column in §7 so the meeting can weigh it directly.")

    # ---- 9 Open questions ----
    h(doc, "9. Open questions", 1)
    rich_para(doc,
        "These are written as questions, not as leading statements. They are split by who can answer them; the "
        "firmware list is genuinely populated, because this is a joint review rather than a list of requests in "
        "one direction.")
    for key, title in (("backend", "9.1 For the backend team"),
                       ("firmware", "9.2 For the firmware team"),
                       ("joint", "9.3 Neither side can answer alone — product or owner decisions")):
        h(doc, title, 2)
        add_table(doc, ["ID", "Question", "What it unblocks"],
                  [list(q) for q in O.OPEN_QUESTIONS[key]],
                  widths=[0.6, 4.2, 1.8], size=8.5, mono_cols=(0,))

    # ==================================================================================
    # PART II
    # ==================================================================================
    doc.add_page_break()
    h(doc, "Part II — Reference catalogue", 1)
    rich_para(doc, "This half is for building against the contract after the meeting.")

    # ---- 10 Message catalogue ----
    h(doc, "10. Telemetry message catalogue", 1)
    rich_para(doc,
        "One subsection per telemetry payload family. The twin, command and provisioning planes are documented "
        "separately in §13, §14 and §15 respectively, because they are not telemetry.")
    for mt in D.MESSAGE_TYPES:
        if mt["id"] not in D.D2C_MESSAGE_IDS:
            continue
        h(doc, f"10.{D.D2C_MESSAGE_IDS.index(mt['id']) + 1} {mt['title']}", 2)
        rows = [
            ("Plane", mt["plane"]),
            ("Envelope type", mt["type_value"] or "n/a"),
            ("data.event value", mt["event_value"] or "n/a"),
            ("Purpose", mt["purpose"]),
            ("Trigger", mt["trigger"]),
            ("Cadence", mt["cadence"]),
            ("Topic", "devices/<device_id>/messages/events/"),
            ("QoS / retain", "1 / 0"),
            ("Retry behaviour", mt["retry"]),
            ("Builder", mt["builder"]),
            ("Trigger citation", mt["citation_trigger"]),
        ]
        add_table(doc, ["", ""], [list(r) for r in rows], widths=[1.3, 5.3], size=8.5)
        rich_para(doc, mt["notes"])
        fields_here = [f for f in FIELDS if mt["id"] in f["_mt"]]
        h(doc, f"Fields carried ({len(fields_here)})", 3)
        add_table(doc, ["JSON path", "Type", "Presence", "Meaning"],
                  [[f["json_path"], f["type"], f["gating"], f["meaning"]] for f in
                   sorted(fields_here, key=lambda x: x["json_path"])],
                  widths=[1.9, 0.9, 1.7, 2.1], size=7.5, mono_cols=(0,))
        h(doc, "Example payload", 3)
        rich_para(doc, "Maximal — every key that can appear, shown together. See Appendix B for the "
                       "reading convention and for the minimal form.")
        json_block(doc, P.PAYLOADS[mt["id"]]["maximal"])

    # ---- 11 Master dictionary (landscape) ----
    landscape_section(doc)
    setup_header_footer(doc.sections[-1], D.META["title"])
    h(doc, "11. Master telemetry data dictionary", 1)
    rich_para(doc,
        f"All {len(FIELDS)} telemetry-plane fields, one row each, sorted by JSON path. This is the canonical "
        "row store: §12 indexes into it and does not repeat it. Twin, command and provisioning items are NOT "
        "in this table — they are in §13, §14 and §15. Every row is on the telemetry plane.")
    add_table(doc,
              ["JSON path", "Field name", "Type", "Units", "Range / enum", "Default", "Opt?",
               "Gating condition", "Message types", "Meaning", "Source"],
              [[f["json_path"], f["json_path"].split(".")[-1], f["type"], f["units"],
                f["range_or_enum"], f["default"], f["optional"], f["gating"],
                ", ".join(f["_mt"]), f["meaning"], f["citation"]]
               for f in sorted(FIELDS, key=lambda x: x["json_path"])],
              widths=[1.5, 0.85, 0.6, 0.6, 1.2, 0.7, 0.35, 1.5, 1.1, 2.5, 1.85],
              size=6.5, mono_cols=(0, 10))

    portrait_section(doc)
    setup_header_footer(doc.sections[-1], D.META["title"])

    # ---- 12 Category index ----
    h(doc, "12. Categorised catalogue", 1)
    rich_para(doc,
        "This is an index, not a second catalogue: it lists which fields fall in each category and leaves the "
        "detail to §11, so there is exactly one place where any field's type or units is stated. Every field "
        "appears in exactly one category, and the twelve counts sum to the census total.")
    rich_para(doc,
        "**Tie-break order**, applied in this order when a field could sit in more than one category: entity "
        "role, then alarm-bearing, then physical quantity, then lifecycle. So a sensor's battery is filed under "
        "power rather than under leak detection, and a sensor's `connected` flag under connectivity rather than "
        "under the sensor itself.")

    h(doc, "Device × category matrix", 2)
    rich_para(doc, "Where each entity's fields live, for readers whose unit of work is the device rather than "
                   "the physical quantity.")
    # A prefix matches the path itself and its descendants. A prefix ending in "$" matches ONLY
    # that exact path - needed for `data`, which is a field in its own right but must not swallow
    # every path beneath it and hand the hub all 91 fields.
    ENTITY = [
        ("Hub / envelope", ["schema", "ts", "type", "data$", "gateway", "data.event",
                            "data.reason", "data.reset_reason", "data.provisioned", "data.raw",
                            "data.valve_mac", "data.lora_sensor_count", "data.ble_leak_sensor_count",
                            "data.rules"]),
        ("Valve", ["data.valve", "data.valve_state", "data.rmleak", "data.fw_version"]),
        ("BLE leak sensor", ["data.ble_leak_sensors"]),
        ("LoRa sensor", ["data.lora_sensors"]),
        ("Leak event (flat)", ["data.source_type", "data.sensor_id", "data.leak_state",
                               "data.battery", "data.rssi", "data.location"]),
        ("Override window", ["data.override_active", "data.override_remaining_s",
                             "data.override_cancelled", "data.expires_ts", "data.remaining_s",
                             "data.trigger", "data.active_leak_count", "data.auto_close_resumed",
                             "data.previous_remaining_s", "data.clear_after_seconds",
                             "data.rmleak_asserted"]),
        ("Command ack", ["data.id", "data.cmd", "data.status", "data.error"]),
        ("Health alert", ["data.category", "data.dev_type", "data.rating", "data.prev_rating",
                          "data.offline_duration_s", "data.system_health"]),
    ]
    def entity_match(path, prefixes):
        for p in prefixes:
            if p.endswith("$"):
                if path == p[:-1]:
                    return True
            elif path == p or path.startswith(p + ".") or path.startswith(p + "["):
                return True
        return False

    ent_rows, claimed = [], {}
    for name, prefixes in ENTITY:
        matched = [f for f in FIELDS if entity_match(f["json_path"], prefixes)]
        for f in matched:
            claimed.setdefault(f["json_path"], []).append(name)
        secs = sorted({f["section"] for f in matched}, key=lambda s: [int(x) for x in s.split(".")])
        ent_rows.append([name, len(matched), ", ".join(secs)])
    overlaps = {p: e for p, e in claimed.items() if len(e) > 1}
    unclaimed = [f["json_path"] for f in FIELDS if f["json_path"] not in claimed]
    assert not overlaps, f"entity matrix rows overlap: {overlaps}"
    assert not unclaimed, f"entity matrix leaves fields unclaimed: {unclaimed}"
    ent_rows.append(["Total", sum(int(r[1]) for r in ent_rows), "all twelve categories"])
    add_table(doc, ["Entity", "Fields", "Categories its fields live in"], ent_rows,
              widths=[1.7, 0.7, 4.2], size=9)
    rich_para(doc, "Entity rows are disjoint and exhaustive: every field is claimed by exactly one "
                   "entity, so the column sums to the census total. This is asserted at build time.")

    for sid, title, desc in D.SECTIONS_12:
        members = sorted([f for f in FIELDS if f["section"] == sid], key=lambda x: x["json_path"])
        h(doc, f"{sid} {title} ({len(members)})", 2)
        rich_para(doc, desc)
        if members:
            mono_para(doc, "\n".join(f["json_path"] for f in members), size=8)
        # Related surface on other planes: listed, never counted.
        leaves = {f["json_path"].split(".")[-1] for f in members}
        twin_rel = [k for k, *_ in R.TWIN_REPORTED if k in leaves or k.replace("gateway_", "") in leaves]
        rich_para(doc, "*Related surface on other planes* — Twin reported: "
                       + (", ".join(twin_rel) if twin_rel else "none")
                       + f". Twin desired: {'hub_name' if sid == '12.1' else 'none'}"
                       + f". Commands: {'see §14' if sid in ('12.6', '12.9', '12.10') else 'none'}."
                       + " (Listed for navigation; not counted in the total above.)")

    rich_para(doc, f"**Sum across all twelve categories: {sum(1 for _ in FIELDS)} fields.**")

    h(doc, "Build-time gating", 2)
    for para in D.BUILD_GATES_NOTE.split("\n\n"):
        rich_para(doc, para)

    # ---- 13 Twin ----
    h(doc, "13. Device twin plane", 1)
    p = doc.add_paragraph(); r = p.add_run("NOT D2C TELEMETRY. "); r.bold = True
    r2 = p.add_run("The twin is a durable document held by IoT Hub, not a message stream. It is documented here "
                   "because it is part of the cloud interface and because 11 of its 12 keys share a name with a "
                   "telemetry field — see the collision index in §4.8.")
    h(doc, "13.1 Twin reported properties (device → cloud)", 2)
    rich_para(doc, "Twelve flat keys, no envelope, no timestamp. Written on only three triggers, so `uptime_s` "
                   "in particular can be arbitrarily stale. Not subject to the pre-clock-sync suppression that "
                   "applies to telemetry.")
    add_table(doc, ["Key", "Type", "Presence", "Meaning", "Citation"],
              [list(r) for r in R.TWIN_REPORTED],
              widths=[1.3, 0.7, 1.3, 2.2, 1.1], size=8, mono_cols=(0, 4))
    h(doc, "13.2 Twin desired properties (cloud → device)", 2)
    rich_para(doc, "Two keys. The hub never issues a twin GET, so a desired property written while the hub is "
                   "offline is never fetched (F-07).")
    add_table(doc, ["Key", "Type", "Effect", "Citation"],
              [list(r) for r in R.TWIN_DESIRED],
              widths=[1.2, 0.6, 3.6, 1.2], size=8, mono_cols=(0, 3))
    h(doc, "13.3 Two inbound paths reach one value", 2)
    for para in R.HUB_NAME_TRACE.split("\n\n"):
        if para.strip().startswith("INBOUND") or "──►" in para:
            mono_para(doc, para, size=8)
        else:
            rich_para(doc, para)

    # ---- 14 Commands ----
    h(doc, "14. Command plane", 1)
    p = doc.add_paragraph(); r = p.add_run("INBOUND — cloud to device. "); r.bold = True
    p.add_run("Documented for completeness of the cloud interface. These are not telemetry.")
    h(doc, "14.1 The eleven commands", 2)
    add_table(doc, ["Command", "What it does", "Payload keys", "Notes", "Citation"],
              [list(r) for r in R.C2D_COMMANDS],
              widths=[1.1, 1.5, 1.1, 1.8, 1.2], size=7.5, mono_cols=(0, 2, 4))
    h(doc, "14.2 Acknowledgement contract", 2)
    for para in R.ACK_CONTRACT.split("\n\n"):
        rich_para(doc, para)

    # ---- 15 DPS ----
    h(doc, "15. Provisioning plane (DPS)", 1)
    p = doc.add_paragraph()
    r = p.add_run("None of these payloads appear in your telemetry ingest. "); r.bold = True
    p.add_run("DPS uses a different broker, a different credential and a different topic namespace, and it "
              "completes before the IoT Hub connection exists. It is documented so the publish-site count in "
              "§3.4 is complete.")
    dps = next(m for m in D.MESSAGE_TYPES if m["id"] == "dps_registration")
    add_table(doc, ["", ""], [
        ("Purpose", dps["purpose"]), ("Trigger", dps["trigger"]), ("Cadence", dps["cadence"]),
        ("Builder", dps["builder"]), ("Retry", dps["retry"]), ("Notes", dps["notes"]),
        ("Citation", dps["citation_trigger"]),
    ], widths=[1.2, 5.4], size=8.5)
    json_block(doc, P.PAYLOADS["dps_registration"]["maximal"])

    # ---- 16 Trigger matrix ----
    h(doc, "16. Trigger and cadence matrix", 1)
    rich_para(doc, "All four planes, so nothing is assumed to be telemetry-only.")
    add_table(doc, ["Trigger", "Plane", "Produces", "Fields affected", "Citation"],
              [list(r) for r in O.TRIGGER_MATRIX],
              widths=[1.5, 0.8, 1.4, 1.6, 1.5], size=7.5, mono_cols=(4,))

    # ---- 17 Bandwidth ----
    h(doc, "17. Payload volume and bandwidth", 1)
    rich_para(doc, BANDWIDTH_SCOPE := O.BANDWIDTH["scope"])
    rich_para(doc, f"**Reference configuration.** {O.BANDWIDTH['reference_config']}")
    add_table(doc, ["Message", "Serialized size", "Note"],
              [list(r) for r in O.BANDWIDTH["rows"]], widths=[2.2, 1.2, 3.2], size=9)
    h(doc, "Arithmetic", 2)
    mono_para(doc, O.BANDWIDTH["arithmetic"], size=8.5)
    p = doc.add_paragraph(); r = p.add_run(O.BANDWIDTH["citation"]); r.font.name = MONO; r.font.size = Pt(7.5)

    # ---- 18 Versioning ----
    h(doc, "18. Schema version handling", 1)
    rich_para(doc, O.VERSION_DELTA["intro"])
    add_table(doc, ["What changed", "At HEAD (1.7.0)", "In the working tree (1.8.0)", "Consequence", "Citation"],
              [list(r) for r in O.VERSION_DELTA["rows"]],
              widths=[1.4, 1.0, 1.4, 2.0, 1.4], size=8, mono_cols=(4,))
    rich_para(doc, O.VERSION_DELTA["mixed_fleet"])

    # ---- 19 Findings ----
    h(doc, "19. Findings, gaps and risks", 1)
    rich_para(doc,
        "These were found while reading the firmware for this catalogue. **Nothing here has been changed** — "
        "this was a read-only investigation, and every item is reported for the team to triage.")
    for fid, kind, sev, title, body, cite in R.FINDINGS:
        h(doc, f"{fid} [{sev}] {title}", 3)
        rich_para(doc, body)
        p = doc.add_paragraph(); r = p.add_run(f"{kind} · {cite}"); r.font.name = MONO; r.font.size = Pt(7.5)
    h(doc, "19.1 The previous wire shape — unreachable paths", 2)
    for para in R.DEAD_PATHS["intro"].split("\n\n"):
        rich_para(doc, para)
    add_table(doc, ["Deprecated serializer", "Location"],
              [list(b) for b in R.DEAD_PATHS["builders"]], widths=[2.6, 4.0], size=9, mono_cols=(0, 1))
    rich_para(doc, f"The {len(R.DEAD_PATHS['paths'])} distinct paths these three builders can construct, "
                   "none of which is in the 91:")
    add_table(doc, ["Unreachable path", "Citation"],
              [list(p) for p in R.DEAD_PATHS["paths"]], widths=[3.2, 3.4], size=7.5, mono_cols=(0, 1))

    h(doc, "19.2 Gaps — what this repository cannot answer", 2)
    add_table(doc, ["Gap", "Why it matters", "Status"],
              [list(g) for g in R.GAPS], widths=[1.6, 4.0, 1.0], size=8.5)
    h(doc, "19.3 Security material", 2)
    for para in R.SECRETS_NOTE.split("\n\n"):
        rich_para(doc, para)

    # ---- Appendices ----
    doc.add_page_break()
    h(doc, "Appendix A — Citation index by file", 1)
    by_file = {}
    for f in FIELDS:
        for m in re.finditer(r"([\w/\.\-]+\.(?:c|h|cpp))\s*:\s*([\d,\s:\-]+)", f["citation"]):
            by_file.setdefault(m.group(1), set()).add(f["json_path"])
    add_table(doc, ["Source file", "Fields citing it", "Field paths"],
              [[fn, len(paths), ", ".join(sorted(paths))] for fn, paths in sorted(by_file.items())],
              widths=[1.8, 0.6, 4.2], size=7.5, mono_cols=(0, 2))

    h(doc, "Appendix B — Maximal and minimal payloads", 1)
    for para in P.PREAMBLE.split("\n\n"):
        rich_para(doc, para)
    h(doc, "Source-derived values used below", 2)
    add_table(doc, ["Value as printed", "What it is", "Citation"],
              [list(r) for r in P.SOURCE_VALUES], widths=[1.9, 2.6, 2.1], size=7.5, mono_cols=(0, 2))
    for mt_id, spec in P.PAYLOADS.items():
        mt = next((m for m in D.MESSAGE_TYPES if m["id"] == mt_id), None)
        h(doc, f"B.{list(P.PAYLOADS).index(mt_id) + 1} {mt['title'] if mt else mt_id}", 2)
        if spec.get("plane_note"):
            p = doc.add_paragraph(); r = p.add_run(spec["plane_note"]); r.bold = True; r.font.size = Pt(9)
        h(doc, "Maximal", 3); json_block(doc, spec["maximal"])
        h(doc, "Minimal", 3); json_block(doc, spec["minimal"])
        if spec.get("exclusive_notes"):
            h(doc, "Conditional and mutually exclusive keys", 3)
            add_table(doc, ["Key", "Condition"], [list(n) for n in spec["exclusive_notes"]],
                      widths=[1.9, 4.7], size=7.5, mono_cols=(0,))

    h(doc, "Appendix C — JSON Schema listings", 1)
    p = doc.add_paragraph()
    r = p.add_run("DESCRIPTIVE, NOT NORMATIVE. "); r.bold = True
    p.add_run("These schemas record what the firmware emits today at FW " + FW + ". They are not a "
              "specification and not a proposal. They deliberately encode the awkward parts faithfully — "
              "for example leak_state is typed as a plain boolean and its description states that false is "
              "emitted both for a genuine dry reading and for a sensor with no cache entry (F-01). A schema "
              "that cleaned that up would contradict §4.5 and mislead an implementer.")
    rich_para(doc, "Machine-readable copies are committed alongside this document in "
                   "`docs/telemetry/schemas/`, one file per message type, so they can be diffed and used for "
                   "validation directly.")
    add_table(doc, ["Message type", "Plane", "Schema file"],
              [[mt_id, (next((m["plane"] for m in D.MESSAGE_TYPES if m["id"] == mt_id),
                             P.PAYLOADS[mt_id].get("plane_note", ""))), f"schemas/{mt_id}.schema.json"]
               for mt_id in P.PAYLOADS],
              widths=[1.4, 2.6, 2.6], size=8.5, mono_cols=(0, 2))

    h(doc, "Appendix D — Glossary and abbreviations", 1)
    add_table(doc, ["Term", "Meaning"], [list(g) for g in O.GLOSSARY],
              widths=[1.1, 5.5], size=9)

    h(doc, "Appendix D.1 — Enumerated values in full", 2)
    rich_para(doc, "Every enum-valued field with every member. No field says 'see the enum definition'.")
    for e in R.ENUMS:
        h(doc, e["title"], 3)
        mono_para(doc, "Applies to: " + ", ".join(e["paths"]), size=8)
        add_table(doc, ["Value", "Meaning"], [list(m) for m in e["members"]],
                  widths=[1.4, 5.2], size=8, mono_cols=(0,))
        rich_para(doc, e["note"])
        p = doc.add_paragraph(); r = p.add_run(e["citation"]); r.font.name = MONO; r.font.size = Pt(7.5)

    # Deterministic core properties so rebuilds are byte-comparable.
    cp = doc.core_properties
    cp.title = D.META["title"]
    cp.author = D.META["author"]
    cp.comments = f"Generated from {SHA} at FW {FW}"
    cp.revision = 1
    import datetime
    fixed = datetime.datetime(2026, 7, 31, 0, 0, 0)
    cp.created = fixed
    cp.modified = fixed

    doc.save(out_path)
    print(f"docx OK: {out_path}")


# --------------------------------------------------------------------------------------
# Markdown (generated, never hand-maintained)
# --------------------------------------------------------------------------------------


def build_md(path):
    L = []
    a = L.append
    a(f"# {D.META['title']}")
    a("")
    a(f"*{D.META['subtitle']}*")
    a("")
    a("> GENERATED FILE — do not edit by hand. Produced by `docs/telemetry/build_catalogue.py`")
    a("> from `fields.json` and the `catalogue_*.py` modules. Edit those and re-run the build.")
    a("")
    a(f"| | |\n|---|---|")
    a(f"| Document version | {D.META['doc_version']} |")
    a(f"| Date | {D.META['date']} |")
    a(f"| Git commit | `{SHA}` (branch `{BRANCH}`) |")
    a(f"| Working tree | {TREE_STATE} |")
    a(f"| Firmware version | {FW} — `{FW_CITE}` |")
    a(f"| Telemetry schema | `{D.META['schema_string']}` — `{D.META['schema_citation']}` |")
    a("")
    a("## 1. Executive summary — cloud interface census")
    a("")
    a("| Plane | Mechanism | Named things | Direction | Section |")
    a("|---|---|---|---|---|")
    for r in C.CLOUD_INTERFACE_CENSUS["rows"]:
        a("| " + " | ".join(str(x) for x in r) + " |")
    a("")
    a(C.CLOUD_INTERFACE_CENSUS["total_note"])
    a("")
    a("## 3.4 Publish call sites")
    a("")
    a("| Call site | Plane | Topic | QoS | Retain | Trigger |")
    a("|---|---|---|---|---|---|")
    for r in D.PUBLISH_SITES:
        a("| " + " | ".join(f"`{x}`" if i in (0, 2) else str(x) for i, x in enumerate(r)) + " |")
    a("")
    a("## 2. How many fields must your ingest really handle?")
    a("")
    a(C.LIVENESS_RECONCILIATION["intro"])
    a("")
    a("| Class | Count | What it means for you |")
    a("|---|---|---|")
    for r in C.LIVENESS_RECONCILIATION["rows"]:
        a("| " + " | ".join(str(x).replace("|", "\\|") for x in r) + " |")
    a("")
    a("## 3.2 The four planes")
    a("")
    a("| Plane | Direction | Topic | Envelope | Note |")
    a("|---|---|---|---|---|")
    for p in D.PLANES:
        a("| " + " | ".join(str(x).replace("|", "\\|") for x in
                            [p["plane"], p["direction"], f"`{p['topic']}`", p["envelope"], p["note"]]) + " |")
    a("")
    a("## 2.1 Ground rules")
    a("")
    a(C.GROUND_RULES)
    a("")
    a("## 4. Current-state consistency audit")
    a("")
    for dim in C.CONSISTENCY:
        a(f"### {dim['id']} {dim['title']}")
        a("")
        a(f"**Severity:** {dim['severity']} · **Fixable where:** {dim['fixable_where']} · "
          f"**Meeting time:** {dim['meeting_time']}")
        a("")
        a(dim["summary"])
        a("")
        if dim.get("evidence_json"):
            a("```c"); a(dim["evidence_json"]); a("```"); a("")
        if dim.get("variants"):
            a("| Variant | Where it occurs | Citation |")
            a("|---|---|---|")
            for v in dim["variants"]:
                a("| " + " | ".join(str(x).replace("|", "\\|") for x in v) + " |")
            a("")
    a("### 4.8 Plane collision index")
    a("")
    a(C.PLANE_COLLISIONS["intro"])
    a("")
    a("| Leaf name | Telemetry path | Twin key | Can they diverge? | Authoritative for | Citation |")
    a("|---|---|---|---|---|---|")
    for r in C.PLANE_COLLISIONS["rows"]:
        a("| " + " | ".join(str(x).replace("|", "\\|") for x in r) + " |")
    a("")
    a("## 5. Overlap and derivation relationships")
    a("")
    a(O.OVERLAP_INTRO)
    a("")
    for oid, title, desc, cost, buys, cite in O.OVERLAP:
        a(f"### {oid} — {title}")
        a("")
        a(desc)
        a("")
        a(f"**Byte cost:** {cost} · **What the overlap buys:** {buys}")
        a("")
        a(f"*`{cite}`*")
        a("")
    a("## 6. Compatibility envelope")
    a("")
    for sid, title, status, body, cite in O.COMPAT:
        a(f"### {sid} {title}")
        a("")
        a(f"**Status: {status}**")
        a("")
        a(body)
        a("")
        a(f"*`{cite}`*")
        a("")
    a("## 7. Options per issue")
    a("")
    a("Every set opens with Option 0 (no change). Nothing here is recommended or chosen.")
    a("")
    for grp in O.OPTIONS:
        a(f"### {grp['issue_id']} — {grp['title']}")
        a("")
        a(grp["context"])
        a("")
        a("| ID | Option | Cost | Risk | Blast radius | Owner | FW change on shipped units? | Reversible? |")
        a("|---|---|---|---|---|---|---|---|")
        for o in grp["options"]:
            a("| " + " | ".join(str(x).replace("|", "\\|") for x in o) + " |")
        a("")
    a("## 8. Migration and versioning")
    a("")
    a(O.VERSION_DELTA["intro"])
    a("")
    a("| What changed | At HEAD (1.7.0) | In the working tree (1.8.0) | Consequence | Citation |")
    a("|---|---|---|---|---|")
    for r in O.VERSION_DELTA["rows"]:
        a("| " + " | ".join(str(x).replace("|", "\\|") for x in r) + " |")
    a("")
    a(O.VERSION_DELTA["mixed_fleet"])
    a("")
    a("## 10. Telemetry message catalogue")
    a("")
    for mt in D.MESSAGE_TYPES:
        if mt["id"] not in D.D2C_MESSAGE_IDS:
            continue
        a(f"### {mt['title']}  (`type` = `{mt['type_value']}`)")
        a("")
        a(f"**Purpose.** {mt['purpose']}")
        a("")
        a(f"**Trigger.** {mt['trigger']}  \n**Cadence.** {mt['cadence']}  \n"
          f"**Retry.** {mt['retry']}  \n**Builder.** `{mt['builder']}`")
        a("")
        a(mt["notes"])
        a("")
        a("Maximal payload — every key that can appear on this message type, shown together. Keys that are")
        a("mutually exclusive in practice are included deliberately; this is not a single observed message.")
        if P.PAYLOADS[mt["id"]].get("data_required") is not None:
            a("")
            a("> NOTE: this family is a UNION across several mutually exclusive event shapes. No single")
            a("> message carries all of these keys. Only `data.event` is present on every shape.")
        a("")
        a("```json")
        a(json.dumps(P.PAYLOADS[mt["id"]]["maximal"], indent=2, ensure_ascii=False))
        a("```")
        a("")
    a("## 12. Categorised catalogue (index into §11)")
    a("")
    for sid, title, desc in D.SECTIONS_12:
        members = sorted([f["json_path"] for f in FIELDS if f["section"] == sid])
        a(f"### {sid} {title} ({len(members)})")
        a("")
        a(desc)
        a("")
        if members:
            for m in members:
                a(f"- `{m}`")
            a("")
    a("## 13. Device twin plane (NOT telemetry)")
    a("")
    a("### 13.1 Twin reported (device to cloud)")
    a("")
    a("| Key | Type | Presence | Meaning | Citation |")
    a("|---|---|---|---|---|")
    for r in R.TWIN_REPORTED:
        a("| " + " | ".join(str(x).replace("|", "\\|") for x in r) + " |")
    a("")
    a("### 13.2 Twin desired (cloud to device)")
    a("")
    a("| Key | Type | Effect | Citation |")
    a("|---|---|---|---|")
    for r in R.TWIN_DESIRED:
        a("| " + " | ".join(str(x).replace("|", "\\|") for x in r) + " |")
    a("")
    a(R.HUB_NAME_TRACE)
    a("")
    a("## 14. Command plane (INBOUND)")
    a("")
    a("| Command | What it does | Payload keys | Notes | Citation |")
    a("|---|---|---|---|---|")
    for r in R.C2D_COMMANDS:
        a("| " + " | ".join(str(x).replace("|", "\\|") for x in r) + " |")
    a("")
    a(R.ACK_CONTRACT)
    a("")
    a("## 15. Provisioning plane (DPS)")
    a("")
    a("None of these payloads appear in your telemetry ingest. DPS uses a different broker, a different")
    a("credential and a different topic namespace, and completes before the IoT Hub connection exists.")
    a("")
    a("```json")
    a(json.dumps(P.PAYLOADS["dps_registration"]["maximal"], indent=2, ensure_ascii=False))
    a("```")
    a("")
    a("## 16. Trigger and cadence matrix")
    a("")
    a("| Trigger | Plane | Produces | Fields affected | Citation |")
    a("|---|---|---|---|---|")
    for r in O.TRIGGER_MATRIX:
        a("| " + " | ".join(str(x).replace("|", "\\|") for x in r) + " |")
    a("")
    a("## 17. Payload volume")
    a("")
    a(O.BANDWIDTH["scope"])
    a("")
    a("```")
    a(O.BANDWIDTH["arithmetic"])
    a("```")
    a("")
    a("## Appendix D.1 Enumerated values in full")
    a("")
    for e in R.ENUMS:
        a(f"### {e['title']}")
        a("")
        a("Applies to: " + ", ".join(f"`{p}`" for p in e["paths"]))
        a("")
        a("| Value | Meaning |")
        a("|---|---|")
        for m in e["members"]:
            a("| " + " | ".join(str(x).replace("|", "\\|") for x in m) + " |")
        a("")
        a(e["note"])
        a("")
    a("## Appendix D Glossary")
    a("")
    a("| Term | Meaning |")
    a("|---|---|")
    for g in O.GLOSSARY:
        a("| " + " | ".join(str(x).replace("|", "\\|") for x in g) + " |")
    a("")
    a("## 11. Master telemetry data dictionary")
    a("")
    a("| JSON path | Type | Units | Range/enum | Opt? | Gating | Message types | Meaning | Source |")
    a("|---|---|---|---|---|---|---|---|---|")
    for f in sorted(FIELDS, key=lambda x: x["json_path"]):
        a("| " + " | ".join(str(x).replace("|", "\\|") for x in [
            f"`{f['json_path']}`", f["type"], f["units"], f["range_or_enum"], f["optional"],
            f["gating"], ", ".join(f["_mt"]), f["meaning"], f"`{f['citation']}`"]) + " |")
    a("")
    a("## 19. Findings, gaps and risks")
    a("")
    for fid, kind, sev, title, body, cite in R.FINDINGS:
        a(f"### {fid} [{sev}] {title}")
        a("")
        a(body)
        a("")
        a(f"*{kind} — `{cite}`*")
        a("")
    a("### 19.1 The previous wire shape — unreachable paths")
    a("")
    a(R.DEAD_PATHS["intro"])
    a("")
    a("| Unreachable path | Citation |")
    a("|---|---|")
    for _p, _c in R.DEAD_PATHS["paths"]:
        a(f"| `{_p}` | `{_c}` |")
    a("")
    a("### 19.2 Gaps — what this repository cannot answer")
    a("")
    a("| Gap | Why it matters | Status |")
    a("|---|---|---|")
    for _g in R.GAPS:
        a("| " + " | ".join(str(x).replace("|", "\|") for x in _g) + " |")
    a("")
    a("### 19.3 Security material")
    a("")
    a(R.SECRETS_NOTE)
    a("")
    a("## 9. Open questions")
    a("")
    for key, title in (("backend", "For the backend team"), ("firmware", "For the firmware team"),
                       ("joint", "Product / owner decisions")):
        a(f"### {title}")
        a("")
        for qid, q, unblocks in O.OPEN_QUESTIONS[key]:
            a(f"- **{qid}** {q}  \n  *{unblocks}*")
        a("")
    # Sections are emitted in generator order; reorder to document order so the heading tree
    # reads monotonically and section numbers ascend as a reader scrolls.
    text = "\n".join(L) + "\n"
    head, sep, body = text.partition("\n## ")
    if sep:
        blocks = ["## " + b for b in ("" + body).split("\n## ")]

        def order_key(b):
            title = b.split("\n", 1)[0][3:].strip()
            if title.lower().startswith("appendix"):
                return (999, 0, title)
            first = title.split(" ")[0].rstrip(".")
            parts = first.split(".")
            try:
                major = int(parts[0])
            except ValueError:
                return (998, 0, title)
            minor = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 0
            return (major, minor, "")

        blocks.sort(key=order_key)
        text = head + "\n" + "\n".join(blocks)
    open(path, "w", encoding="utf-8").write(text)
    print(f"md OK: {path}")


# --------------------------------------------------------------------------------------


BANNED = [
    r"\bTODO\b", r"\bTBD\b", r"\bXXX\b", r"\bFIXME\b", r"<placeholder>", r"\[example\]",
    r"\bField1\b", r"\bfoo\b", r"\bbar\b", r"\blorem\b", r"\byour_", r"\bsample_", r"\bdummy\b",
]
ELISION = [
    r"\.\.\.", r"\betc\b", r"\band so on\b", r"\bamong others\b", r"\bfor brevity\b",
    r"\bsimilar fields follow\b", r"\band \d+ more\b", r"\brepresentative (subset|sample)\b",
]
MARKDOWN = [r"\*\*", r"^#{1,6}\s", r"\|---", r"`"]


def assert_clean(path):
    """Scan the RENDERED document — body paragraphs and every table cell."""
    d = Document(path)
    texts = []
    for p in d.paragraphs:
        texts.append(p.text)
    for t in d.tables:
        for row in t.rows:
            for c in row.cells:
                for p in c.paragraphs:
                    texts.append(p.text)

    problems = []
    for text in texts:
        for pat in BANNED:
            for m in re.finditer(pat, text, re.IGNORECASE):
                problems.append(("banned", m.group(0), text[:110]))
        for pat in ELISION:
            for m in re.finditer(pat, text, re.IGNORECASE):
                problems.append(("elision", m.group(0), text[:110]))
        for pat in MARKDOWN:
            for m in re.finditer(pat, text, re.MULTILINE):
                problems.append(("markdown", m.group(0), text[:110]))

    if problems:
        print(f"\nSWEEP FAILED - {len(problems)} occurrence(s) in the rendered document:", file=sys.stderr)
        seen = set()
        for kind, tok, ctx in problems:
            key = (kind, tok, ctx)
            if key in seen:
                continue
            seen.add(key)
            print(f"  [{kind}] {tok!r} in: {ctx}", file=sys.stderr)
        raise SystemExit(1)
    print(f"sweep OK: {len(texts)} text blocks, zero banned tokens, zero elision markers, zero markdown artefacts")


def assert_no_invented_payload_values():
    """Appendix B promises no invented values. Enforce it instead of trusting it.

    Every string value in every payload must be one of:
      * a descriptor in angle brackets (<label, up to 31 chars>) - explicitly not a value;
      * a printf format string (contains %);
      * a documented enum member;
      * a literal that actually appears in firmware source under main/.
    Two fabricated strings reached the appendix before this check existed.
    """
    src = []
    for root, _dirs, files in os.walk(os.path.join(REPO, "main")):
        for fn in files:
            if fn.endswith((".c", ".h", ".cpp")):
                try:
                    src.append(open(os.path.join(root, fn), encoding="utf-8", errors="replace").read())
                except OSError:
                    pass
    blob = "\n".join(src)
    enum_values = {m for members in ENUM_MEMBERS.values() for m in members}
    known = enum_values | {FW, D.META["schema_string"]}

    bad = []

    def walk(node, path, mt):
        if isinstance(node, dict):
            for k, v in node.items():
                walk(v, f"{path}.{k}" if path else k, mt)
        elif isinstance(node, list):
            for v in node:
                walk(v, f"{path}[]", mt)
        elif isinstance(node, str):
            if node.startswith("<") and node.endswith(">"):
                return          # explicit descriptor, not a value
            if "%" in node:
                return          # printf format string
            if node in known:
                return
            if f'"{node}"' in blob or node in blob:
                return
            # C adjacent-string concatenation: a long runtime string may be several literals
            # in source, so match on a substantial leading fragment instead.
            if len(node) >= 40 and node[:40] in blob:
                return
            bad.append((mt, path, node))

    for mt_id, spec in P.PAYLOADS.items():
        for kind in ("maximal", "minimal"):
            if spec.get(kind):
                walk(spec[kind], "", f"{mt_id}/{kind}")

    if bad:
        print("BUILD FAILED - Appendix B contains values not traceable to source:", file=sys.stderr)
        for mt, path, val in bad:
            print(f"  {mt}  {path} = {val!r}", file=sys.stderr)
        raise SystemExit(1)
    print("payload values OK: every Appendix B string is a source literal, an enum member, "
          "a format string, or an explicit descriptor")


def assert_bare_line_refs_resolve():
    """A citation like 'a.c:10 vs b.c:20; also :30' binds ':30' to b.c, not a.c.

    That rebinding silently pointed the F-01/F-02 evidence line at an unrelated deprecated
    write in another file. Check every bare ':N' against the file it actually binds to.
    """
    cache = {}

    def lines_of(rel):
        if rel not in cache:
            fp = os.path.join(REPO, rel)
            try:
                cache[rel] = open(fp, encoding="utf-8", errors="replace").read().splitlines()
            except OSError:
                cache[rel] = []
        return cache[rel]

    suspicious = []
    for f in FIELDS:
        cit = f.get("citation", "")
        leaf = f["json_path"].split(".")[-1].replace("[]", "")
        last_file, checked = None, False
        for tok in re.finditer(r"([\w/\.\-]+\.(?:c|h|cpp))?\s*:\s*(\d+)", cit):
            if tok.group(1):
                last_file = tok.group(1)
                continue
            if not last_file:
                continue
            checked = True
            n = int(tok.group(2))
            src = lines_of(last_file)
            if not src or n > len(src):
                continue
            window = " ".join(src[max(0, n - 3):n + 2])
            if leaf and leaf not in window:
                suspicious.append((f["json_path"], f"{last_file}:{n}", src[n - 1].strip()[:70]))
        del checked
    if suspicious:
        print("WARNING - bare :N citations whose bound file does not mention the field leaf:",
              file=sys.stderr)
        for path, where, line in suspicious:
            print(f"  {path}  ->  {where}   {line!r}", file=sys.stderr)
    else:
        print("citation binding OK: every bare :N resolves to a line mentioning its field")


def main():
    check_contract()
    assert_no_invented_payload_values()
    assert_bare_line_refs_resolve()
    out_docx = os.path.join(HERE, "eFloStop2_Hub_Telemetry_Catalogue_v1.0.docx")
    written = build_schemas(os.path.join(HERE, "schemas"))
    validate_schemas(written)
    build_csv(os.path.join(HERE, "field_registry.csv"))
    build_md(os.path.join(HERE, "telemetry_catalogue.md"))
    build_docx(out_docx)
    assert_clean(out_docx)
    print("\nAll artefacts generated.")


if __name__ == "__main__":
    main()
