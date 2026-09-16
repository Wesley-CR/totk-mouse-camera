#!/usr/bin/env python3
"""Find every ARM64 reference to a virtual address inside the UltraCam module.

Written because the earlier ad-hoc scanners disagreed.  This walks the whole
.text of `uc_img/image.bin`, decodes ADRP/ADR/LDR-literal with capstone, and
resolves ADRP+ADD / ADRP+LDR pairs that share a destination register.  For the
UltraCam image file offset == VA.

Usage:
    python uc_refs.py 0x1BCC78 [0x1BCC45 ...]
    python uc_refs.py --addr-of "InitializeMouse"     # find the string VA first
"""
import struct
import sys

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

IMAGE = "uc_img/image.bin"
TEXT_END = 0x168EA0


def main():
    d = open(IMAGE, "rb").read()
    text = d[:TEXT_END]

    if sys.argv[1] == "--addr-of":
        needle = sys.argv[2].encode()
        # the name may be embedded in a longer mangled string; find the substring
        i = d.find(needle)
        while i != -1:
            # back up to the start of the symbol (previous NUL)
            j = d.rfind(b"\x00", 0, i)
            print("substring at 0x%X ; symbol starts at 0x%X -> %r"
                  % (i, j + 1, d[j + 1:i + len(needle) + 40].split(b"\x00")[0].decode("ascii", "replace")))
            i = d.find(needle, i + 1)
        return 0

    targets = set(int(a, 16) for a in sys.argv[1:])
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    md.detail = False

    # Index: pc -> instruction word
    words = struct.unpack_from("<%dI" % (TEXT_END // 4), text, 0)

    hits = []
    for i, w in enumerate(words):
        pc = i * 4
        # ADRP
        if (w & 0x9F000000) == 0x90000000:
            immlo = (w >> 29) & 3
            immhi = (w >> 5) & 0x7FFFF
            imm = (immhi << 2) | immlo
            if imm & (1 << 20):
                imm -= 1 << 21
            page = (pc & ~0xFFF) + (imm << 12)
            rd = w & 0x1F
            for k in (1, 2, 3):
                if i + k >= len(words):
                    break
                w2 = words[i + k]
                p2 = (i + k) * 4
                if (w2 & 0xFF000000) == 0x91000000 and ((w2 >> 5) & 0x1F) == rd:
                    imm12 = (w2 >> 10) & 0xFFF
                    sh = (w2 >> 22) & 1
                    a = page + (imm12 << (12 if sh else 0))
                    if a in targets:
                        hits.append((pc, "ADRP+ADD", a))
                    break
                if (w2 & 0xFFC00000) == 0xF9400000 and ((w2 >> 5) & 0x1F) == rd:
                    a = page + ((w2 >> 10) & 0xFFF) * 8
                    if a in targets:
                        hits.append((pc, "ADRP+LDR", a))
                    break
        # ADR
        if (w & 0x9F000000) == 0x10000000:
            immlo = (w >> 29) & 3
            immhi = (w >> 5) & 0x7FFFF
            imm = (immhi << 2) | immlo
            if imm & (1 << 20):
                imm -= 1 << 21
            a = pc + imm
            if a in targets:
                hits.append((pc, "ADR", a))
        # LDR literal
        if (w & 0x3B000000) == 0x18000000:
            imm19 = (w >> 5) & 0x7FFFF
            if imm19 & (1 << 18):
                imm19 -= 1 << 19
            a = pc + (imm19 << 2)
            if a in targets:
                hits.append((pc, "LDR-LIT", a))

    print("%d reference(s) to %s" % (len(hits), [hex(t) for t in targets]))
    for pc, kind, a in hits:
        print("  %08X  %-10s -> %08X" % (pc, kind, a))
        for ins in md.disasm(text[max(0, pc - 24):pc + 28], max(0, pc - 24)):
            mark = "  <== REF" if ins.address == pc else ""
            print("      %08X  %-9s %s%s" % (ins.address, ins.mnemonic, ins.op_str, mark))
    return 0


if __name__ == "__main__":
    sys.exit(main())
