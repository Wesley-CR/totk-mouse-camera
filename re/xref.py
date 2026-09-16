#!/usr/bin/env python3
"""Find ARM64 ADRP(+ADD/LDR) references to given targets, and dump context.

Works on the flat `uc_img/image.bin` produced by nsoimg.py, whose layout is:

    text    file 0x00000000 .. 0x168EA0   va base 0x00000000
    rodata  file 0x168EA0 .. 0x1EEBD1     va base 0x168EA0
    tail    file 0x1EEBD1 .. EOF          va base 0x1EEBD1

For this image the file offset equals the virtual address inside each segment,
and because the segments are contiguous and start at their own base, file
offset == va for the whole image EXCEPT that the rodata base happens to equal
its file offset too.  In other words: va == file_offset everywhere here.

Usage:
    python xref.py <target_hex> [more targets...]
    python xref.py --str "some string"
"""
import re
import struct
import sys

IMAGE = "uc_img/image.bin"


def load(path=IMAGE):
    d = open(path, "rb").read()
    return d


def find_string_va(data, text):
    """Return VA of an ASCII string in the image."""
    needle = text.encode()
    idx = data.find(needle)
    return idx if idx >= 0 else None


def adrp_target(pc, insn):
    """Decode ADRP target from a 32-bit instruction at address pc."""
    if (insn & 0x9F000000) != 0x90000000:
        return None
    immlo = (insn >> 29) & 0x3
    immhi = (insn >> 5) & 0x7FFFF
    imm = (immhi << 2) | immlo
    if imm & (1 << 20):
        imm -= 1 << 21
    return (pc & ~0xFFF) + (imm << 12)


def scan(data, targets, window=6):
    """Scan the text segment for ADRP+ADD/LDR pairs landing near targets."""
    text_end = 0x168EA0
    results = []
    for pc in range(0, text_end - 4, 4):
        insn, = struct.unpack_from("<I", data, pc)
        tgt = adrp_target(pc, insn)
        if tgt is None:
            continue
        rd = insn & 0x1F
        # look ahead for ADD (imm) or LDR (imm, unsigned offset) using same reg
        for k in range(1, window + 1):
            p2 = pc + k * 4
            if p2 + 4 > text_end:
                break
            i2, = struct.unpack_from("<I", data, p2)
            # ADD (immediate), 64-bit: sf=1 op=0 S=0 100010 sh imm12 Rn Rd
            if (i2 & 0xFF000000) == 0x91000000 and ((i2 >> 5) & 0x1F) == rd:
                imm12 = (i2 >> 10) & 0xFFF
                sh = (i2 >> 22) & 0x1
                off = imm12 << (12 if sh else 0)
                addr = tgt + off
                for t in targets:
                    if addr == t:
                        results.append((pc, p2, addr, "ADD"))
                break
            # LDR (immediate) 64-bit unsigned offset: 1111100101 imm12 Rn Rt
            if (i2 & 0xFFC00000) == 0xF9400000 and ((i2 >> 5) & 0x1F) == rd:
                imm12 = (i2 >> 10) & 0xFFF
                addr = tgt + imm12 * 8
                for t in targets:
                    if addr == t:
                        results.append((pc, p2, addr, "LDR"))
                break
    return results


def dump(data, va, n=48):
    print("--- disasm around 0x%X ---" % va)
    try:
        from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
        md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
        start = va - (n // 2) * 4
        code = data[start:start + n * 4]
        for ins in md.disasm(code, start):
            mark = "  <== " if ins.address == va else ""
            print("  %08X  %-8s %s%s" % (ins.address, ins.mnemonic, ins.op_str, mark))
    except ImportError:
        print("  (capstone unavailable)")


def main():
    data = load()
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    if sys.argv[1] == "--str":
        va = find_string_va(data, sys.argv[2])
        if va is None:
            print("string not found")
            return 1
        print("string %r at VA 0x%X" % (sys.argv[2], va))
        targets = [va]
    else:
        targets = [int(a, 16) for a in sys.argv[1:]]
    res = scan(data, targets)
    print("found %d xref(s)" % len(res))
    for pc, p2, addr, kind in res:
        print("  0x%08X (pair 0x%08X) -> 0x%08X  %s" % (pc, p2, addr, kind))
        dump(data, pc, 24)
    return 0


if __name__ == "__main__":
    sys.exit(main())
