#!/usr/bin/env python3
"""Gzip one text asset of the setup portal's page at build time (2.1.4 C7).

The component's CMakeLists.txt runs it, with the build's own Python (the PYTHON build property,
ESP-IDF's virtual environment), for index.html, code.js and style.css:

    python gz_asset.py <source> <output.gz>

The output is the same bytes on every build: gzip level 9, mtime 0 and no file name in the
header (Python writes the OS byte as 255, "unknown"), so the image does not change with the
time or the folder of a build.

It fails loudly, never leaving a stale or partial output behind: the old output is removed
first; the new one is written to "<output>.tmp", read back, decompressed and compared with the
source byte for byte, and only then renamed onto the output. Any failure (no source, an empty
one, a write error, a mismatch) prints the reason and exits with 1, which stops the build.

An asset of more than SEND_BUFFER_BYTES once gzipped no longer goes out in one TCP send buffer
(plan 4.3): that prints a warning, and the build goes on.
"""

import gzip
import io
import os
import sys

# The lwIP TCP send buffer of the ESP32-S3 build (CONFIG_LWIP_TCP_SND_BUF_DEFAULT, 5,760 B).
SEND_BUFFER_BYTES = 5760


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
    try:
        if os.path.exists(out):
            os.remove(out)
        with open(src, "rb") as f:
            data = f.read()
        if not data:
            raise ValueError("the source is empty")
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
    print("gz_asset.py: %s %d B -> %s %d B" % (os.path.basename(src), len(data),
                                              os.path.basename(out), len(packed)))
    if len(packed) > SEND_BUFFER_BYTES:
        print("gz_asset.py: warning: %s is %d B gzipped, more than one %d B TCP send buffer"
              % (os.path.basename(src), len(packed), SEND_BUFFER_BYTES))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
