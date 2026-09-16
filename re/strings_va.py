#!/usr/bin/env python3
"""Extract strings from the unpacked UltraCam image with true virtual addresses.

Layout of `uc_img/image.bin` (recovered, see nsoimg.py):
    text    file 0x00000000 .. 0x168EA0   va base 0x00000000
    rodata  file 0x168EA0 .. 0x1EEBD1     va base 0x168EA0  (MOD0 at base+0x26)
    tail    file 0x1EEBD1 .. EOF          va base 0x1EEBD1

Usage:
    python strings_va.py <image.bin> [min_len] [regex]
"""
import re
import sys

SEGMENTS = [
    (0x00000000, 0x00168EA0, 0x00000000, "text"),
    (0x00168EA0, 0x001EEBD1, 0x00168EA0, "rodata"),
    (0x001EEBD1, 1 << 40,    0x001EEBD1, "tail"),
]


def locate(off):
    """Return (va, segment_name) for a file offset."""
    for start, end, base, name in SEGMENTS:
        if start <= off < end:
            return base + (off - start), name
    return off, "?"


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "uc_img/image.bin"
    minlen = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    pattern = re.compile(sys.argv[3]) if len(sys.argv) > 3 else None
    data = open(path, "rb").read()
    rx = re.compile(rb"[\x20-\x7e]{%d,}" % minlen)
    for m in rx.finditer(data):
        s = m.group().decode("ascii", "replace")
        if pattern and not pattern.search(s):
            continue
        va, seg = locate(m.start())
        print("%08X  %-6s %s" % (va, seg, s))


if __name__ == "__main__":
    main()
