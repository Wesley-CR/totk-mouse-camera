#!/usr/bin/env python3
"""Append the runtime-confirmed Chase camera map to docs/research.md."""
import io
import os

p = os.path.join(os.path.dirname(__file__), "..", "docs", "research.md")
p = os.path.normpath(p)

ADD = '''
### 3.5 Chase camera execute — RUNTIME-CONFIRMED map (supersedes earlier guesses)

`ExecutePlayerCameraChase` = image VA **0x001DB01C**, vtable `0x0377EFE0` slot 19.
Hooked via a 32-byte signature at `entry+0x4C` (unique in `.text`). Status: **CONFIRMED at
runtime** — 1320+ entries per run with a stable `this`.

Arguments: `x0 = this`, `x1 = delta-time struct` (`ldr sN,[x21,#8]`, 31 uses).

**`x20` IS `this` for the whole body.** An exhaustive scan for writes to `x20` between the entry
and `0x1DB6EC` finds exactly one - the initial `mov x20,x0`. An earlier claim that `0x1DB09C`
reloads `x20` was WRONG: that instruction is `ldr x22,[x20,#0x20]` (destination `x22`), verified
at the word level (`0xF9401296`, `Rt=22, Rn=20`).

**Fields Chase writes (`x20`-relative), exhaustive:**

| Offsets | Note |
|---|---|
| +0x060, +0x068 | derived from +0x58/+0x5C |
| +0x080, +0x084, +0x088, +0x090, +0x094, +0x098, +0x09C | scalars |
| +0x0B8, +0x0BC, +0x0C0 | scalars |
| **+0x0D0, +0x0D4, +0x0D8** | triple - likely position |
| **+0x0DC, +0x0E0, +0x0E4** | triple/pair - likely look-at |
| +0x0F0, +0x0F4, +0x108, +0x118, +0x11C, +0x128, +0x130 | scalars |
| +0x148, +0x14C, +0x150, +0x154, +0x158 | scalars |
| +0x180, +0x184, +0x190, +0x198, +0x19C, +0x1A0 | vectors/scalars |

**`this+0x58`/`+0x5C` are NOT stick input - proven two independent ways.**

1. Arithmetic at `0x1DB6EC`:
   `ldp s1,s2,[x20,#0x58]` / `fmul s1,s1,s2` / `fdiv s2,s15,s1` / `str s1,[x20,#0x60]` -
   a `1/(min*max)` distance blend factor.
2. Runtime: `this+0x58` read exactly `10.000` on every frame across 1320 calls, *including while
   the right stick was being moved*.

**Runtime measurement: the `+0x40`..`+0x7C` window is inert.** A read-only build dumped it at
frame 60 and frame 1200 with the stick deliberately moved in between - both dumps byte-identical.
The stick therefore does not reach this camera through that window.

**`input+0x420` / `+0x424`** are read by many camera executes, but there are **ZERO stores to them
anywhere in `.text`** - the producer was not found. Per brief #4 they are gate/scale values, not
injection points.

**Open (delegated as brief #5):** where the right stick actually reaches this camera, and the best
float or hook for injection. Three candidate offsets have each cost a build-and-test cycle:
`input+0x58` (wrong struct), `this+0x58` (distance blend), `input+0x420/424` (no producer found).

'''

marker = "**Status: SUPPORTED, not yet CONFIRMED at runtime.**"
with io.open(p, encoding="utf-8") as f:
    s = f.read()

if "3.5 Chase camera execute" in s:
    print("already present, not duplicating")
else:
    s = s.replace(marker, ADD.strip() + "\n\n" + marker, 1)
    with io.open(p, "w", encoding="utf-8") as f:
        f.write(s)
    print("research.md updated -> %d bytes" % len(s))
