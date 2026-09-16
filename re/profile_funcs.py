#!/usr/bin/env python3
"""Profile candidate camera functions to separate parameter *builders* from the
function that actually converts stick input into camera rotation.

The AIDef parameter-loading idiom (seen throughout) is:

    adrp/add  x9, "<param name>"
    add       x8, x20, #<field>
    str       x8, [sp, #..]
    adrp/add  x8, <default value ptr>
    stp       x9, x0, [sp, #..]
    ...
    blr       x8                ; property-set virtual

Such a builder is dominated by adrp/add/stp/str/blr and contains essentially no
floating point arithmetic.

The rotation code we want is the opposite: it consumes a stick value and scales
it, so it should be rich in fmul/fadd/fneg/fcmp/fmov and light on adrp.

Usage:
    python profile_funcs.py 0x01896610 0x0189ac44 ...
    python profile_funcs.py --scan 0x0189    # profile every function-ish region
"""
import struct
import sys
from collections import Counter

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

IMAGE = "totk142_flat.bin"
TEXT_END = 0x02BA61F0

FP = {'fmul', 'fadd', 'fsub', 'fdiv', 'fneg', 'fabs', 'fcmp', 'fmov',
      'fcsel', 'fmadd', 'fmsub', 'fnmadd', 'fnmsub', 'fmax', 'fmin',
      'fmaxnm', 'fminnm', 'frintm', 'frintp', 'scvtf', 'fcvt', 'fcvtzs'}
LOAD = {'adrp', 'add', 'ldr', 'str', 'ldp', 'stp', 'ldrb', 'strb', 'ldur', 'stur'}


def profile(img, start, length):
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    code = img[start:start + length]
    c = Counter()
    for ins in md.disasm(code, start):
        c[ins.mnemonic] += 1
    return c


def show(name, start, length, c):
    total = sum(c.values())
    if total == 0:
        print("%-12s 0x%08X  (no instructions)" % (name, start))
        return
    fp = sum(v for k, v in c.items() if k in FP)
    ld = sum(v for k, v in c.items() if k in LOAD)
    calls = sum(v for k, v in c.items() if k in ('bl', 'blr'))
    print("%-14s 0x%08X len=%-6d insns=%-6d fp=%-4d mem=%-5d calls=%-4d fp%%=%.1f"
          % (name, start, length, total, fp, ld, calls, 100.0 * fp / total))
    if fp > 4:
        interesting = {k: v for k, v in c.items() if k in FP}
        print("                 fp ops: %s" % dict(sorted(interesting.items(), key=lambda kv: -kv[1])))


def main():
    img = open(IMAGE, "rb").read()

    if sys.argv[1] == "--scan":
        # crude: walk 4-byte steps, treat each as a potential function start,
        # and profile the next 0x600 bytes. Loud but finds hot spots.
        lo = int(sys.argv[2], 16)
        for start in range(lo, lo + 0x8000, 0x10):
            c = profile(img, start, 0x600)
            fp = sum(v for k, v in c.items() if k in FP)
            if fp >= 25:
                show("cand", start, 0x600, c)
        return 0

    # explicit list of (label, start, end)
    for i in range(1, len(sys.argv) - 1, 2):
        start = int(sys.argv[i], 16)
        end = int(sys.argv[i + 1], 16)
        show(os.path.basename(sys.argv[i]), start, end - start, profile(img, start, end - start))
    return 0


import os  # noqa: E402  (used by show() label formatting above)

if __name__ == "__main__":
    sys.exit(main())
