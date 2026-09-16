#!/usr/bin/env python3
"""Map the TOTK camera class hierarchy in .data.

Each camera class has a vtable; TOTK also emits per-method "name getters" of the
form `adrp x0,#page ; add x0,x0,#imm ; ret` that return the method's name string.
Resolving those turns a vtable slot into a readable method name, which is enough
to tell `PlayerCameraBase` from `PlayerCameraChase` and to see which slot is the
per-frame update.

Usage:
    python cameraclass.py              # scan for camera-ish vtables and name them
    python cameraclass.py 0x0377FF58   # dump one vtable in detail
"""
import re
import struct
import sys

IMAGE = "totk142_correct/image_mapped.bin"
TEXT_END = 0x02BA61F0
DATA_START = 0x03557000
DATA_END = 0x03A13D60


def name_at(img, va):
    """Resolve the adrp+add+ret name-getter idiom at `va`."""
    if va + 12 > len(img) or va < TEXT_END:
        return None
    w0, w1, w2 = struct.unpack_from("<III", img, va)
    if (w0 & 0x9F000000) != 0x90000000 or (w0 & 0x1F) != 0:
        return None
    if (w1 & 0xFF800000) != 0x91000000 or (w1 & 0x1F) != 0 or ((w1 >> 5) & 0x1F) != 0:
        return None
    if w2 != 0xD65F03C0:
        return None
    immlo = (w0 >> 29) & 3
    immhi = (w0 >> 5) & 0x7FFFF
    imm = (immhi << 2) | immlo
    if imm & (1 << 20):
        imm -= 1 << 21
    sva = ((va & ~0xFFF) + (imm << 12)) + ((w1 >> 10) & 0xFFF)
    if not (0 <= sva < len(img)):
        return None
    raw = img[sva:sva + 96].split(b"\x00")[0]
    if not raw or not all(32 <= c < 127 for c in raw):
        return None
    return raw.decode()


def dump(img, start, count):
    print("vtable @ 0x%08X" % start)
    for k in range(count):
        off = start + k * 8
        v, = struct.unpack_from("<Q", img, off)
        nm = name_at(img, v) or ""
        kind = ""
        if TEXT_END <= v < DATA_START:
            kind = "namegetter" if nm else "code"
        elif v:
            kind = "data"
        print("  [%2d] 0x%08X -> 0x%016X %-9s %s" % (k, off, v, kind, nm))


def main():
    img = open(IMAGE, "rb").read()

    if len(sys.argv) > 1:
        dump(img, int(sys.argv[1], 16), 40)
        return 0

    # Find vtable-like runs in .data whose slots resolve to ExecutePlayerCamera*
    # name getters, which is the signature of a camera class vtable.
    print("=== camera vtables (slots naming ExecutePlayerCamera*) ===")
    found = 0
    off = DATA_START
    while off + 8 <= DATA_END:
        v, = struct.unpack_from("<Q", img, off)
        nm = name_at(img, v)
        if nm and nm.startswith("ExecutePlayerCamera"):
            # walk back to a plausible vtable start (vtable slots are contiguous)
            print("  slot 0x%08X = %s   (fn 0x%X)" % (off, nm, v))
            found += 1
            if found > 60:
                break
        off += 8
    print("total: %d" % found)
    return 0


if __name__ == "__main__":
    sys.exit(main())
