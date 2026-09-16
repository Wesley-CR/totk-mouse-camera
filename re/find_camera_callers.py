#!/usr/bin/env python3
"""Find real callers of candidate camera functions in the corrected TOTK image.

`0x23BA728` turned out to have zero direct callers and a vtable slot that is
never dispatched, so it is a base-class/fallback method.  The function actually
driving the third-person camera must be reachable some other way.

Two reference kinds matter in AArch64 position-independent code:
  * ADRP+ADD  -- code materialises the function's address (vtable fill, callback
                 table, or a function-pointer assignment)
  * vtable    -- an 8-byte slot in .data/.rodata pointing at it

Both are searched here, and results are grouped so a function used by many
vtables can be told apart from one referenced exactly once (which is usually the
real virtual implementation).

Usage:
    python find_camera_callers.py 0x23BA728 0x23C6F74 ...
"""
import struct
import sys

IMAGE = "totk142_correct/image_mapped.bin"
TEXT_END = 0x02BA61F0


def adrp(pc, w):
    if (w & 0x9F000000) != 0x90000000:
        return None
    immlo = (w >> 29) & 3
    immhi = (w >> 5) & 0x7FFFF
    imm = (immhi << 2) | immlo
    if imm & (1 << 20):
        imm -= 1 << 21
    return (pc & ~0xFFF) + (imm << 12)


def main():
    img = open(IMAGE, "rb").read()
    targets = {int(a, 16): [] for a in sys.argv[1:]}
    if not targets:
        print(__doc__)
        return 1

    # --- ADRP+ADD references from .text ---
    for pc in range(0, TEXT_END - 8, 4):
        w, = struct.unpack_from("<I", img, pc)
        page = adrp(pc, w)
        if page is None:
            continue
        rd = w & 0x1F
        w2, = struct.unpack_from("<I", img, pc + 4)
        if (w2 & 0xFF000000) == 0x91000000 and ((w2 >> 5) & 0x1F) == rd:
            sh = (w2 >> 22) & 1
            a = page + (((w2 >> 10) & 0xFFF) << (12 if sh else 0))
            if a in targets:
                targets[a].append(("ADRP+ADD", pc))

    # --- 8-byte pointer slots anywhere ---
    for t in list(targets):
        pat = struct.pack("<Q", t)
        i = img.find(pat)
        while i != -1:
            targets[t].append(("PTR", i))
            i = img.find(pat, i + 1)

    for t, refs in targets.items():
        ptrs = [r for k, r in refs if k == "PTR"]
        adds = [r for k, r in refs if k == "ADRP+ADD"]
        print("0x%08X : %d pointer slot(s), %d ADRP+ADD site(s)"
              % (t, len(ptrs), len(adds)))
        for p in ptrs[:6]:
            print("      PTR at 0x%08X" % p)
        for a in adds[:6]:
            print("      ADRP+ADD at 0x%08X" % a)
    return 0


if __name__ == "__main__":
    sys.exit(main())
