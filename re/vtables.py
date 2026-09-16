#!/usr/bin/env python3
"""Map TOTK `main` vtables by resolving their name-getter slots.

TOTK's reflection idiom is a tiny leaf function:

    adrp x0, #PAGE
    add  x0, x0, #IMM      ; -> VA of a NUL-terminated name string
    ret

so a vtable slot that points at such a function effectively *names* that slot.
Walking a vtable and resolving each slot therefore recovers the class interface.

Usage:
    python vtables.py --at 0x38AF9A0 [--count 24]     # resolve one vtable
    python vtables.py --find "Camera"                 # scan all name strings
    python vtables.py --vtables-containing 0x2C8D379  # which vtables name this param
"""
import re
import struct
import sys

IMAGE = "totk142_flat.bin"
TEXT_END = 0x02BA61F0
DATA_START = 0x03557000
NAME_END = 0x03A13D60


def load():
    return open(IMAGE, "rb").read()


def name_at(img, va):
    """Decode the adrp+add+ret idiom at `va`; return the string it names, or None."""
    if va + 12 > len(img):
        return None
    w0, w1, w2 = struct.unpack_from("<III", img, va)
    # adrp x0, #page
    if (w0 & 0x9F000000) != 0x90000000 or (w0 & 0x1F) != 0:
        return None
    # add x0, x0, #imm
    if (w1 & 0xFF800000) != 0x91000000 or (w1 & 0x1F) != 0 or ((w1 >> 5) & 0x1F) != 0:
        return None
    # ret
    if w2 != 0xD65F03C0:
        return None
    immlo = (w0 >> 29) & 3
    immhi = (w0 >> 5) & 0x7FFFF
    imm = (immhi << 2) | immlo
    if imm & (1 << 20):
        imm -= 1 << 21
    page = (va & ~0xFFF) + (imm << 12)
    imm12 = (w1 >> 10) & 0xFFF
    sh = (w1 >> 22) & 1
    sva = page + (imm12 << (12 if sh else 0))
    if not (0 <= sva < len(img)):
        return None
    end = img.find(b"\x00", sva, sva + 128)
    if end < 0:
        return None
    raw = img[sva:sva + 80].split(b"\x00")[0]
    if not raw or not all(32 <= c < 127 for c in raw):
        return None
    return raw.decode("ascii")


def resolve_vtable(img, base, count):
    out = []
    for i in range(count):
        off = base + i * 8
        if off + 8 > len(img):
            break
        w, = struct.unpack_from("<Q", img, off)
        nm = name_at(img, w) if w < TEXT_END else None
        out.append((i, off, w, nm))
    return out


def main():
    img = load()
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    if sys.argv[1] == "--at":
        base = int(sys.argv[2], 16)
        count = 24
        if "--count" in sys.argv:
            count = int(sys.argv[sys.argv.index("--count") + 1])
        print("vtable @ 0x%08X" % base)
        for i, off, ptr, nm in resolve_vtable(img, base, count):
            seg = "text" if ptr < TEXT_END else ("data" if ptr else "-")
            print("  [%2d] 0x%08X -> %016X  %-4s %s" % (i, off, ptr, seg, nm or ""))
        return 0

    if sys.argv[1] == "--name-at":
        for a in sys.argv[2:]:
            va = int(a, 16)
            print("0x%08X -> %r" % (va, name_at(img, va)))
        return 0

    if sys.argv[1] == "--refs":
        # all 64-bit pointer references to a function/string address
        for a in sys.argv[2:]:
            tgt = int(a, 16)
            pat = struct.pack("<Q", tgt)
            found = []
            i = img.find(pat)
            while i != -1:
                found.append(i)
                i = img.find(pat, i + 1)
            print("0x%08X referenced by %d pointer(s): %s"
                  % (tgt, len(found), [hex(x) for x in found[:12]]))
        return 0

    if sys.argv[1] == "--find":
        pat = sys.argv[2].encode()
        for m in re.finditer(re.escape(pat), img):
            va = m.start()
            if va < TEXT_END:
                continue
            # only report the start of NUL-terminated strings
            if va > 0 and img[va - 1] != 0:
                continue
            end = img.find(b"\x00", va)
            s = img[va:end]
            if all(32 <= c < 127 for c in s):
                print("  0x%08X  %s" % (va, s.decode()))
        return 0

    if sys.argv[1] == "--vtables-containing":
        tgt = int(sys.argv[2], 16)
        pat = struct.pack("<Q", tgt)
        for m in re.finditer(re.escape(pat), img):
            off = m.start()
            if off < DATA_START:
                continue
            print("0x%08X references 0x%X" % (off, tgt))
        return 0

    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main())
