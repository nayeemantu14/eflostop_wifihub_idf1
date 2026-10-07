# ruff: noqa: E501
"""Shared edit helpers for the V5 app-requirements builder.

Every section module receives one Ctx. Ctx.P(n) and Ctx.T(k) address V4's ORIGINAL body
paragraph n and body table k: the lists are captured once, before any module edits, and
python-docx objects wrap XML elements, so an insertion elsewhere never shifts them. The
numbering is the one in v4_dump.txt (P<n> counts every body paragraph, empty ones too;
T<k> counts body tables; cell C<j> counts DISTINCT cells, so a merged cell counts once).

Every helper is strict: an anchor that does not match raises EditError. A builder run either
applies every edit or writes nothing.
"""
import copy
import re

from docx.oxml.ns import qn
from docx.table import Table, _Row
from docx.text.paragraph import Paragraph

W14 = "http://schemas.microsoft.com/office/word/2010/wordml"
_W14_ATTRS = ("{%s}paraId" % W14, "{%s}textId" % W14)


class EditError(RuntimeError):
    pass


# ---------------------------------------------------------------- low-level

def strip_ids(el):
    """Remove w14:paraId / w14:textId from a copied subtree so ids stay unique."""
    for node in el.iter():
        for a in _W14_ATTRS:
            if a in node.attrib:
                del node.attrib[a]
    return el


def unique_cells(row):
    """A row's distinct cells, left to right (a merged cell appears once), as in the dump."""
    out, seen = [], set()
    for c in row.cells:
        if id(c._tc) in seen:
            continue
        seen.add(id(c._tc))
        out.append(c)
    return out


def _parse_rich(text):
    """Split 'plain **bold** plain' into [(text, bold)]."""
    parts = re.split(r"(\*\*[^*]+\*\*)", text)
    out = []
    for s in parts:
        if not s:
            continue
        if s.startswith("**") and s.endswith("**"):
            out.append((s[2:-2], True))
        else:
            out.append((s, None))
    return out


def set_text(p, text, rich=False):
    """Replace a paragraph's text, keeping its style and the first run's formatting.

    rich=True treats **x** as bold (any other text keeps the first run's formatting)."""
    runs = p.runs
    if not runs:
        r = p.add_run("")
        runs = [r]
    first = runs[0]
    for r in runs[1:]:
        r._r.getparent().remove(r._r)
    if not rich:
        first.text = text
        return p
    pieces = _parse_rich(text)
    first.text = pieces[0][0] if pieces else ""
    if pieces and pieces[0][1]:
        first.bold = True
    prev = first
    for s, bold in pieces[1:]:
        nr = copy.deepcopy(first._r)
        prev._r.addnext(nr)
        from docx.text.run import Run
        run = Run(nr, p)
        run.text = s
        run.bold = True if bold else None
        prev = run
    return p


def replace(p, old, new, count=1):
    """Strict substring replace inside one paragraph (tolerates a needle split across runs).

    Raises unless `old` occurs exactly `count` times (count=None: at least once)."""
    full = p.text
    n = full.count(old)
    if n == 0 or (count is not None and n != count):
        raise EditError(f"replace: expected {count} x {old!r}, found {n} in {full[:120]!r}")
    for r in p.runs:
        if old in r.text and (count is None or r.text.count(old) == n):
            r.text = r.text.replace(old, new)
            return p
    set_text(p, full.replace(old, new))
    return p


def cell_text(cell):
    return "\n".join(p.text for p in cell.paragraphs)


def set_cell(cell, text, rich=False):
    """Set a cell's text. '\\n' separates paragraphs; new ones copy the first's formatting."""
    lines = text.split("\n")
    paras = cell.paragraphs
    first = paras[0]
    for extra in paras[1:]:
        extra._p.getparent().remove(extra._p)
    set_text(first, lines[0], rich=rich)
    prev = first
    for line in lines[1:]:
        np_ = strip_ids(copy.deepcopy(first._p))
        prev._p.addnext(np_)
        para = Paragraph(np_, first._parent)
        set_text(para, line, rich=rich)
        prev = para
    return cell


def replace_in_cell(cell, old, new, count=1):
    hits = [p for p in cell.paragraphs if old in p.text]
    total = sum(p.text.count(old) for p in hits)
    if total == 0 or (count is not None and total != count):
        raise EditError(f"replace_in_cell: expected {count} x {old!r}, found {total} in {cell_text(cell)[:120]!r}")
    for p in hits:
        replace(p, old, new, count=None)
    return cell


# ---------------------------------------------------------------- insertion

def _anchor_el(anchor):
    if isinstance(anchor, Paragraph):
        return anchor._p
    if isinstance(anchor, Table):
        return anchor._tbl
    raise EditError(f"anchor must be a Paragraph or Table, got {type(anchor)}")


def _new_para_like(like, parent):
    """An empty paragraph carrying `like`'s paragraph properties and first-run properties."""
    p_el = copy.deepcopy(like._p)
    strip_ids(p_el)
    for child in list(p_el):
        if child.tag != qn("w:pPr"):
            p_el.remove(child)
    ppr = p_el.find(qn("w:pPr"))
    if ppr is not None:
        for sect in ppr.findall(qn("w:sectPr")):
            ppr.remove(sect)
    para = Paragraph(p_el, parent)
    r = para.add_run("")
    if like.runs and like.runs[0]._r.rPr is not None:
        r._r.insert(0, copy.deepcopy(like.runs[0]._r.rPr))
    return para


def insert_after(anchor, text, like=None, style=None, rich=False):
    """Insert a paragraph directly after `anchor` (a Paragraph or a Table); return it.

    Formatting: `like` (a Paragraph) wins; else `style` (a style name); else the anchor's own
    formatting when the anchor is a paragraph."""
    el = _anchor_el(anchor)
    parent = anchor._parent
    if like is None and style is None:
        if not isinstance(anchor, Paragraph):
            raise EditError("insert_after a table needs like= or style=")
        like = anchor
    if like is not None:
        para = _new_para_like(like, parent)
    else:
        from docx.oxml import OxmlElement
        para = Paragraph(OxmlElement("w:p"), parent)
        para.style = style
        para.add_run("")
    el.addnext(para._p)
    set_text(para, text, rich=rich)
    if style is not None and like is not None:
        para.style = style
    return para


def insert_lines_after(anchor, lines, like=None, style=None, rich=False):
    """Insert several paragraphs after `anchor`, in order. Returns the last one."""
    cur = anchor
    for line in lines:
        cur = insert_after(cur, line, like=like, style=style, rich=rich)
        like = cur if like is None and style is None else like
    return cur


def delete_para(p):
    p._p.getparent().remove(p._p)


# ---------------------------------------------------------------- tables

def add_row(table, values, like_row=-1, after_row=None, rich=False):
    """Deep-copy row `like_row` of `table`, fill its distinct cells with `values`, insert it
    after `after_row` (a _Row, e.g. from Ctx.row() or a previous add_row; or a CURRENT row
    index; default: at the end). Returns the new _Row, so several rows can be chained.
    Prefer _Row anchors: indices shift as rows are added."""
    rows = table.rows
    src = rows[like_row]._tr if not isinstance(like_row, _Row) else like_row._tr
    tr = strip_ids(copy.deepcopy(src))
    if isinstance(after_row, _Row):
        anchor = after_row._tr
    else:
        anchor = rows[after_row]._tr if after_row is not None else rows[-1]._tr
    anchor.addnext(tr)
    row = _Row(tr, table)
    cells = unique_cells(row)
    if len(values) != len(cells):
        raise EditError(f"add_row: {len(values)} values for {len(cells)} cells")
    for c, v in zip(cells, values):
        set_cell(c, v, rich=rich)
    return row


def set_row_font_size(row, pt):
    """Give every run in `row` an explicit size, for rows whose runs carry none and so print
    at the 11 pt default inside a table that uses a smaller size."""
    from docx.shared import Pt
    for c in unique_cells(row):
        for p in c.paragraphs:
            for r in p.runs:
                r.font.size = Pt(pt)
    return row


def delete_row(table, r):
    tr = table.rows[r]._tr
    tr.getparent().remove(tr)


# tcPr children that must follow w:shd (ECMA-376 CT_TcPr sequence).
_SHD_AFTER = {qn(t) for t in ("w:noWrap", "w:tcMar", "w:textDirection", "w:tcFitText", "w:vAlign",
                              "w:hideMark", "w:headers", "w:cellIns", "w:cellDel", "w:cellMerge",
                              "w:tcPrChange")}


def zebra(table, shd_tpl):
    """V4's row banding: body rows 1, 3, 5 ... carry `shd_tpl` (V4's F5F5F5 fill, Ctx.body_shd),
    rows 2, 4, 6 ... none. Row 0 (the header) is left as it is. Run once every row is in place."""
    for i, row in enumerate(table.rows):
        if i == 0:
            continue
        for tc in row._tr.findall(qn("w:tc")):
            tcpr = tc.get_or_add_tcPr()
            for old in tcpr.findall(qn("w:shd")):
                tcpr.remove(old)
            if i % 2 == 0:
                continue
            shd = copy.deepcopy(shd_tpl)
            nxt = next((ch for ch in tcpr if ch.tag in _SHD_AFTER), None)
            if nxt is not None:
                nxt.addprevious(shd)
            else:
                tcpr.append(shd)


PRIORITY_RGB = {"P0": "C62828", "P1": "E65100", "P2": "2E7D32"}


def colour_priority(row, col=1):
    """V4's house style for a requirement row: the Priority cell's text is P0 red, P1 orange,
    P2 green. Rows copied from another row carry that row's colour, so set it explicitly."""
    from docx.shared import RGBColor
    cell = unique_cells(row)[col]
    key = cell.text.strip()
    if key not in PRIORITY_RGB:
        raise EditError(f"colour_priority: {key!r} is not P0, P1 or P2")
    rgb = RGBColor.from_string(PRIORITY_RGB[key])
    for p in cell.paragraphs:
        for r in p.runs:
            r.font.color.rgb = rgb


def insert_table_after(anchor, like_table, header, rows, rich=False):
    """New table after `anchor`, copying `like_table`'s properties, its row 0 as the header
    template and its row 1 (or 0) as the body template. Column count must match."""
    tbl = copy.deepcopy(like_table._tbl)
    strip_ids(tbl)
    trs = tbl.findall(qn("w:tr"))
    head_tpl = trs[0]
    body_tpl = trs[1] if len(trs) > 1 else trs[0]
    for tr in trs:
        tbl.remove(tr)
    new = Table(tbl, like_table._parent)
    _anchor_el(anchor).addnext(tbl)

    def _fill(tpl, values):
        tr = copy.deepcopy(tpl)
        tbl.append(tr)
        row = _Row(tr, new)
        cells = unique_cells(row)
        if len(values) != len(cells):
            raise EditError(f"insert_table_after: {len(values)} values for {len(cells)} cells")
        for c, v in zip(cells, values):
            set_cell(c, v, rich=rich)

    _fill(head_tpl, header)
    for vals in rows:
        _fill(body_tpl, vals)
    return new


# ---------------------------------------------------------------- context

class Ctx:
    """Original-index access to V4 plus bookkeeping. One instance per builder run."""

    def __init__(self, doc):
        self.doc = doc
        self.paras = list(doc.paragraphs)
        self.tables = list(doc.tables)
        self.module = None
        self.notes = []            # (module, text)
        self.touched = {}          # "P12" / "T3" -> set(modules)
        self._code_like = next((p for p in self.paras if p.style is not None and p.style.name == "CodeBlock"), None)
        # ORIGINAL V4 text, so `expect` guards hold whatever another module already changed.
        self._p_text = [p.text for p in self.paras]
        self._rows = [list(t.rows) for t in self.tables]          # V4's _Row objects
        self._cells = [[[cell_text(c) for c in unique_cells(r)] for r in rows] for rows in self._rows]
        # V4's body-row fill (<w:shd w:fill="F5F5F5"/>), copied before any edit, for zebra().
        self.body_shd = next((copy.deepcopy(s) for t in self.tables for s in t._tbl.iter(qn("w:shd"))
                              if s.get(qn("w:fill")) == "F5F5F5"), None)
        if self.body_shd is None:
            raise EditError("V4 has no F5F5F5 body-row shading to copy")

    def orig(self, n):
        """V4's original text of body paragraph n."""
        return self._p_text[n]

    def orig_cell(self, k, r, c):
        return self._cells[k][r][c]

    def _touch(self, key):
        self.touched.setdefault(key, set()).add(self.module)

    def P(self, n, expect=None):
        """V4 body paragraph n. `expect`: a substring its ORIGINAL text must contain."""
        p = self.paras[n]
        if expect is not None and expect not in self._p_text[n]:
            raise EditError(f"P{n}: expected {expect!r} in V4 text {self._p_text[n][:120]!r}")
        self._touch(f"P{n}")
        return p

    def T(self, k, expect=None):
        """V4 body table k. `expect`: a substring its V4 first row must contain."""
        t = self.tables[k]
        if expect is not None:
            head = " | ".join(self._cells[k][0])
            if expect not in head:
                raise EditError(f"T{k}: expected {expect!r} in header {head[:120]!r}")
        self._touch(f"T{k}")
        return t

    def cell(self, k, r, c, expect=None):
        """Distinct cell C<c> of V4's ORIGINAL row R<r> of table T<k> (dump numbering)."""
        cells = unique_cells(self._rows[k][r])
        if c >= len(cells):
            raise EditError(f"T{k} R{r}: no C{c} (row has {len(cells)} cells)")
        cl = cells[c]
        if expect is not None and expect not in self._cells[k][r][c]:
            raise EditError(f"T{k} R{r} C{c}: expected {expect!r} in V4 text {self._cells[k][r][c][:120]!r}")
        self._touch(f"T{k}")
        return cl

    def row(self, k, r, expect=None):
        """V4's ORIGINAL row r of table k, whatever rows were added since (use it as the
        after_row anchor of add_row). A row deleted by another module raises."""
        row = self._rows[k][r]
        if row._tr.getparent() is None:
            raise EditError(f"T{k} R{r} was deleted")
        if expect is not None:
            txt = " | ".join(self._cells[k][r])
            if expect not in txt:
                raise EditError(f"T{k} R{r}: expected {expect!r} in {txt[:120]!r}")
        self._touch(f"T{k}")
        return row

    def code_after(self, anchor, lines):
        """Insert JSON/code lines in the CodeBlock style after `anchor`. Returns the last."""
        return insert_lines_after(anchor, lines, like=self._code_like)

    def note(self, text):
        self.notes.append((self.module, text))
