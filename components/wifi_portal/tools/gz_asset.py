#!/usr/bin/env python3
"""Gzip one text asset of the setup portal's page at build time (2.1.4 C7).

The component's CMakeLists.txt runs it, with the build's own Python (the PYTHON build property,
ESP-IDF's virtual environment), for index.html, code.js and style.css:

    python gz_asset.py <source> <output.gz>

The output is the same bytes on every build: gzip level 9, mtime 0 and no file name in the
header (Python writes the OS byte as 255, "unknown"), so the image does not change with the
time or the folder of a build.

A .js source loses its whole-line comments first (strip_js_comments()): they document the page
in the tree but cost the phone about 0.6 KB gzipped. Only lines that are nothing but a comment
go; code is never touched. Its blank lines go too, and the lines it keeps lose their
indentation, their trailing spaces and the CR of a CRLF (about 0.2 KB gzipped, and a code.js
checked out with CRLF embeds the same bytes as one with LF): with the refusals below no token
spans two lines, so none of it is significant, and every line break between two tokens stays
(automatic semicolon insertion reads them). The pass is line-based, so it refuses (and fails the build on) what a line-based
pass cannot be sure of: a template literal (a backtick), a line continued with a backslash, a
"/*" in a line that is kept (a block comment sharing a line with code, or one in a string), or
a line break JavaScript sees that this pass does not (a lone CR, U+2028, U+2029). Lines are
split on LF only. Every other asset is gzipped as it is.

It fails loudly, never leaving a stale or partial output behind: the old output is removed
first; the new one is written to "<output>.tmp", read back, decompressed and compared with what
was gzipped byte for byte, and only then renamed onto the output. Any failure (no source, an
empty one, a refused .js, a write error, a mismatch) prints the reason and exits with 1, which
stops the build.

An asset of more than SEND_BUFFER_BYTES once gzipped no longer goes out in one TCP send buffer
(plan 4.3): that prints a warning, and the build goes on.
"""

import gzip
import io
import os
import sys

# The lwIP TCP send buffer of the ESP32-S3 build (CONFIG_LWIP_TCP_SND_BUF_DEFAULT, 5,760 B).
SEND_BUFFER_BYTES = 5760


def strip_js_comments(data):
    """Leaves out the lines of a JavaScript source that hold only a comment: "// ..." lines, and
    "/* ... */" blocks that start a line and end one; then its blank lines, and the spaces, tabs
    and CR at either end of the lines it keeps. Returns (bytes, comment lines left out, blank lines
    left out). Raises ValueError for what a line-based pass cannot be sure of (see the module's
    docstring)."""
    text = data.decode("utf-8")
    if "`" in text:
        raise ValueError("a backtick (template literal): the comment pass is line-based")
    if "\u2028" in text or "\u2029" in text or "\r" in text.replace("\r\n", ""):
        raise ValueError("a line break other than LF or CRLF (a lone CR, U+2028 or U+2029)")
    out = []
    dropped = 0
    blank = 0
    in_block = False
    for line in text.split("\n"):
        body = line.rstrip("\r")
        stripped = body.strip(" \t")
        if body.endswith("\\"):
            raise ValueError("a line continued with a backslash")
        if in_block:
            dropped += 1
            if "*/" in stripped:
                if not stripped.endswith("*/") or stripped.count("*/") != 1:
                    raise ValueError("a block comment that does not end its line")
                in_block = False
            continue
        if stripped.startswith("//"):
            dropped += 1
            continue
        if stripped.startswith("/*"):
            dropped += 1
            ends = stripped.count("*/")
            if ends == 0:
                in_block = True
            elif ends != 1 or not stripped.endswith("*/"):
                raise ValueError("a block comment that shares its line with code")
            continue
        if "/*" in body:
            raise ValueError("a \"/*\" in a line of code (a block comment there, or in a string)")
        if not stripped:
            blank += 1
            continue
        out.append(stripped)
    if in_block:
        raise ValueError("a block comment that never ends")
    return "\n".join(out).encode("utf-8"), dropped, blank


def gzip_bytes(data):
    buf = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", compresslevel=9, fileobj=buf, mtime=0) as gz:
        gz.write(data)
    return buf.getvalue()


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: gz_asset.py <source> <output.gz>\n")
        return 2
    src, out = argv[1], argv[2]
    tmp = out + ".tmp"
    note = ""
    try:
        if os.path.exists(out):
            os.remove(out)
        with open(src, "rb") as f:
            data = f.read()
        if not data:
            raise ValueError("the source is empty")
        if src.lower().endswith(".js"):
            data, dropped, blank = strip_js_comments(data)
            note = " (%d comment lines left out, %d blank lines, the others trimmed)" % (dropped, blank)
        packed = gzip_bytes(data)
        if gzip.decompress(packed) != data:
            raise ValueError("the gzipped bytes do not decompress to the source")
        with open(tmp, "wb") as f:
            f.write(packed)
        with open(tmp, "rb") as f:
            if f.read() != packed:
                raise IOError("the written file does not read back as written")
        os.replace(tmp, out)
    except Exception as e:  # any failure stops the build
        sys.stderr.write("gz_asset.py: FAILED for %s: %s\n" % (src, e))
        for path in (tmp, out):
            try:
                os.remove(path)
            except OSError:
                pass
        return 1
    print("gz_asset.py: %s %d B -> %s %d B%s" % (os.path.basename(src), len(data),
                                                os.path.basename(out), len(packed), note))
    if len(packed) > SEND_BUFFER_BYTES:
        print("gz_asset.py: warning: %s is %d B gzipped, more than one %d B TCP send buffer"
              % (os.path.basename(src), len(packed), SEND_BUFFER_BYTES))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
