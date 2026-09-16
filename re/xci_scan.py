#!/usr/bin/env python3
"""Locate and parse the root HFS0 of an XCI, then report its partitions.

Rather than trusting a fixed 0x130 offset (which varies), scan the first few
MB for the HFS0 magic and validate the candidate by checking that its entry
names are printable and its declared size is sane.

Usage:
    python xci_scan.py <game.xci>
"""
import re
import struct
import sys

SECTOR = 0x200


def read_at(f, off, size):
    f.seek(off)
    return f.read(size)


def try_hfs0(f, base):
    hdr = read_at(f, base, 0x10)
    if len(hdr) < 0x10 or hdr[:4] != b"HFS0":
        return None
    count, strsz, _res = struct.unpack_from("<III", hdr, 4)
    if count == 0 or count > 32 or strsz > 0x10000:
        return None
    entries = []
    for i in range(count):
        e = read_at(f, base + 0x10 + i * 0x40, 0x40)
        if len(e) < 0x40:
            return None
        name_off, data_off, size, _pad = struct.unpack("<QQII", e[:24])
        raw = read_at(f, base + 0x10 + count * 0x40 + name_off, 0x40)
        name = raw.split(b"\x00")[0]
        if not name or not all(32 <= c < 127 for c in name):
            return None
        entries.append({"name": name.decode(), "offset": base + strsz + data_off,
                        "size": size})
    return {"count": count, "strsz": strsz, "entries": entries}


def main():
    path = sys.argv[1]
    f = open(path, "rb")
    head = f.read(0x400000)
    cands = [m.start() for m in re.finditer(b"HFS0", head)]
    print("HFS0 magic candidates: %s" % [hex(c) for c in cands[:10]])
    for c in cands:
        r = try_hfs0(f, c)
        if r:
            print()
            print("valid root HFS0 at 0x%X  (%d entries)" % (c, r["count"]))
            for e in r["entries"]:
                print("  %-10s off=0x%-12X size=0x%X" % (e["name"], e["offset"], e["size"]))
                sub = try_hfs0(f, e["offset"])
                if sub:
                    for s in sub["entries"]:
                        print("      %-44s off=0x%-12X size=0x%X"
                              % (s["name"], s["offset"], s["size"]))
            return 0
    print("no valid HFS0 found")
    return 1


if __name__ == "__main__":
    sys.exit(main())
