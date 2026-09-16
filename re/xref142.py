#!/usr/bin/env python3
"""ARM64 cross-reference scanner for a flat Switch `main` image.

Image layout (TOTK 1.4.2, see nso_unpack.py):
    text    file/va 0x00000000 .. 0x02BA61F0
    rodata  file/va 0x02BA7000 .. 0x0355605C
    data    file/va 0x03557000 .. 0x03A13D60

Because each segment is placed at its true memory offset, file offset == virtual
address throughout, so this scans the whole image as one address space.

Finds every instruction sequence that materialises a target address:
    ADRP+ADD   page base plus 12-bit add (optionally shifted)
    ADRP+LDR   page base plus unsigned scaled offset
    ADR        +-1MB pc-relative
    LDR literal +-1MB pc-relative

Usage:
    python xref142.py <target_hex> [more...]
    python xref142.py --str "Needle"
    python xref142.py --disasm 0x1234 [count]
"""
import re
import struct
import sys

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

IMAGE = "totk142_flat.bin"
TEXT_END = 0x02BA61F0
IMG_END = 0x03A13D60


def load(path=IMAGE):
    return open(path, "rb").read()


def adrp(pc, insn):
    if (insn & 0x9F000000) != 0x90000000:
        return None
    immlo = (insn >> 29) & 3
    immhi = (insn >> 5) & 0x7FFFF
    imm = (immhi << 2) | immlo
    if imm & (1 << 20):
        imm -= 1 << 21
    return (pc & ~0xFFF) + (imm << 12)


def adr(pc, insn):
    if (insn & 0x9F000000) != 0x10000000:
        return None
    immlo = (insn >> 29) & 3
    immhi = (insn >> 5) & 0x7FFFF
    imm = (immhi << 2) | immlo
    if imm & (1 << 20):
        imm -= 1 << 21
    return pc + imm


def ldr_lit(pc, insn):
    if (insn & 0x3B000000) != 0x18000000:
        return None
    imm19 = (insn >> 5) & 0x7FFFF
    if imm19 & (1 << 18):
        imm19 -= 1 << 19
    return pc + (imm19 << 2)


def scan(data, targets):
    tset = set(targets)
    out = []
    for i in range(TEXT_END // 4):
        pc = i * 4
        insn, = struct.unpack_from("<I", data, pc)
        t = adr(pc, insn)
        if t is not None and t in tset:
            out.append((pc, "ADR", t))
        t = ldr_lit(pc, insn)
        if t is not None and t in tset:
            out.append((pc, "LDR-LIT", t))
        page = adrp(pc, insn)
        if page is None:
            continue
        rd = insn & 0x1F
        for k in (1, 2, 3):
            p2 = pc + k * 4
            if p2 + 4 > TEXT_END:
                break
            i2, = struct.unpack_from("<I", data, p2)
            if (i2 & 0xFF000000) == 0x91000000 and ((i2 >> 5) & 0x1F) == rd:
                imm12 = (i2 >> 10) & 0xFFF
                sh = (i2 >> 22) & 1
                addr = page + (imm12 << (12 if sh else 0))
                if addr in tset:
                    out.append((pc, "ADRP+ADD", addr))
                break
            if (i2 & 0xFFC00000) == 0xF9400000 and ((i2 >> 5) & 0x1F) == rd:
                addr = page + ((i2 >> 10) & 0xFFF) * 8
                if addr in tset:
                    out.append((pc, "ADRP+LDR", addr))
                break
    return out


def disasm(data, va, count=24):
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    start = max(0, va - (count // 2) * 4)
    for ins in md.disasm(data[start:start + count * 4], start):
        mark = "   <== REF" if ins.address == va else ""
        print("  %08X  %-10s %s%s" % (ins.address, ins.mnemonic, ins.op_str, mark))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    data = load()
    if sys.argv[1] == "--disasm":
        disasm(data, int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 24)
        return 0
    if sys.argv[1] == "--str":
        for m in re.finditer(re.escape(sys.argv[2].encode()), data):
            print("%08X  %s" % (m.start(), "text" if m.start() < TEXT_END else "rodata/data"))
        return 0
    targets = [int(a, 16) for a in sys.argv[1:]]
    res = scan(data, targets)
    print("%d xref(s)" % len(res))
    for pc, kind, addr in res:
        print("  %08X  %-10s -> %08X" % (pc, kind, addr))
    for pc, kind, addr in res[:12]:
        print()
        print("--- 0x%08X (%s -> 0x%08X) ---" % (pc, kind, addr))
        disasm(data, pc, 18)
    return 0


if __name__ == "__main__":
    sys.exit(main())
