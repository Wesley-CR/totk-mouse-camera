#!/usr/bin/env python3
"""Robust ARM64 cross-reference scanner for the unpacked UltraCam image.

Uses capstone to find every instruction that materialises a target address:
  - ADRP+ADD   (page + 12-bit add, optionally shifted)
  - ADRP+LDR   (page + unsigned scaled offset)
  - ADR        (pc-relative, +-1MB)
  - LDR literal (pc-relative, +-1MB)

The image is flat and file offset == virtual address for every byte (see
nsoimg.py / strings_va.py), so addresses printed here are module VAs directly.

Usage:
    python a64xref.py 0x1BCC80 [0x1BCCB0 ...]
    python a64xref.py --disasm 0x1234 [count]
    python a64xref.py --find "pattern-regex"      # string search w/ context
"""
import re
import struct
import sys

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

IMAGE = "uc_img/image.bin"
TEXT_END = 0x168EA0


def load():
    return open(IMAGE, "rb").read()


def decode_adrp(pc, insn):
    if (insn & 0x9F000000) != 0x90000000:
        return None
    immlo = (insn >> 29) & 0x3
    immhi = (insn >> 5) & 0x7FFFF
    imm = (immhi << 2) | immlo
    if imm & (1 << 20):
        imm -= 1 << 21
    return (pc & ~0xFFF) + (imm << 12)


def decode_adr(pc, insn):
    if (insn & 0x9F000000) != 0x10000000:
        return None
    immlo = (insn >> 29) & 0x3
    immhi = (insn >> 5) & 0x7FFFF
    imm = (immhi << 2) | immlo
    if imm & (1 << 20):
        imm -= 1 << 21
    return pc + imm


def decode_ldr_literal(pc, insn):
    if (insn & 0x3B000000) != 0x18000000:
        return None
    imm19 = (insn >> 5) & 0x7FFFF
    if imm19 & (1 << 18):
        imm19 -= 1 << 19
    return pc + (imm19 << 2)


def scan(data, targets):
    tset = set(targets)
    out = []
    n = TEXT_END // 4
    for i in range(n):
        pc = i * 4
        insn, = struct.unpack_from("<I", data, pc)
        # ADR / LDR-literal: single instruction
        for dec, kind in ((decode_adr, "ADR"), (decode_ldr_literal, "LDR-LIT")):
            t = dec(pc, insn)
            if t is not None and t in tset:
                out.append((pc, kind, t))
        # ADRP: remember, then pair with a following ADD/LDR using same reg
        tgt = decode_adrp(pc, insn)
        if tgt is None:
            continue
        rd = insn & 0x1F
        for k in (1, 2, 3):
            p2 = pc + k * 4
            if p2 + 4 > TEXT_END:
                break
            i2, = struct.unpack_from("<I", data, p2)
            if (i2 & 0xFF000000) == 0x91000000 and ((i2 >> 5) & 0x1F) == rd:
                imm12 = (i2 >> 10) & 0xFFF
                sh = (i2 >> 22) & 0x1
                addr = tgt + (imm12 << (12 if sh else 0))
                if addr in tset:
                    out.append((pc, "ADRP+ADD", addr))
                break
            if (i2 & 0xFFC00000) == 0xF9400000 and ((i2 >> 5) & 0x1F) == rd:
                addr = tgt + ((i2 >> 10) & 0xFFF) * 8
                if addr in tset:
                    out.append((pc, "ADRP+LDR", addr))
                break
    return out


def disasm(data, va, count=24):
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    start = max(0, va - (count // 2) * 4)
    code = data[start:start + count * 4]
    for ins in md.disasm(code, start):
        mark = "   <== TARGET" if ins.address == va else ""
        print("  %08X  %-10s %s%s" % (ins.address, ins.mnemonic, ins.op_str, mark))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    data = load()

    if sys.argv[1] == "--disasm":
        disasm(data, int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 24)
        return 0

    if sys.argv[1] == "--find":
        rx = re.compile(sys.argv[2].encode())
        for m in rx.finditer(data):
            va = m.start()
            seg = "text" if va < TEXT_END else "rodata"
            print("%08X %-6s %r" % (va, seg, m.group()[:80]))
        return 0

    targets = [int(a, 16) for a in sys.argv[1:]]
    res = scan(data, targets)
    print("%d xref(s) found" % len(res))
    for pc, kind, addr in res:
        print("  %08X  %-10s -> %08X" % (pc, kind, addr))
    for pc, kind, addr in res:
        print()
        print("--- context at 0x%08X (%s -> 0x%08X) ---" % (pc, kind, addr))
        disasm(data, pc, 20)
    return 0


if __name__ == "__main__":
    sys.exit(main())
