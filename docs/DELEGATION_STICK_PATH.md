# Delegation brief #5 — where does the right stick reach the camera?

## Context

TOTK 1.4.2 native-mouse camera mod, exefs `subsdk` module (exlaunch) under Eden.

**Exploration is hooked and firing.** `ExecutePlayerCameraChase` (image VA `0x001DB01C`) is
trampolined and enters every frame — proven at runtime, 1320+ calls, stable `this`. Signature
scanning and hook installation both work.

**What is missing: the correct float to write.** Three candidate offsets have now been tried or
proposed and all are wrong, each for a different reason. This brief asks for the one answer that
ends the guessing.

## Established facts about `ExecutePlayerCameraChase` (0x001DB01C)

Verified from the disassembly of the exact function we hook:

* arguments: `x0 = this`, `x1 = a delta-time struct` (read 31 times as `ldr sN,[x21,#8]`)
* `x20 = this` for the whole body — **verified**: an exhaustive scan for writes to `x20` between
  entry and `0x1DB6EC` finds exactly one, the initial `mov x20,x0`. (An earlier claim that
  `0x1DB09C` reloads `x20` was wrong: that instruction is `ldr x22,[x20,#0x20]` — destination
  `x22`. Confirmed at the instruction-word level: `0xF9401296`, `Rt=22, Rn=20`.)
* `this+0x20` is a pointer to an input struct; `x26`/`x22` hold it.

### Fields written by Chase (`str`/`stp` with `x20` = `this`)

Grouped by region — this is from an exhaustive scan of the body:

| Offset | Notes |
|---|---|
| `+0x060`, `+0x068` | computed from `+0x58`,`+0x5C` — see below |
| `+0x080`, `+0x084`, `+0x088`, `+0x090`, `+0x094`, `+0x098`, `+0x09C` | scalars |
| `+0x0B8`, `+0x0BC`, `+0x0C0` | scalars |
| **`+0x0D0`, `+0x0D4`, `+0x0D8`** | **written 3× as a triple — likely camera position** |
| **`+0x0DC`, `+0x0E0`, `+0x0E4`** | **written as a pair/triple — likely look-at target** |
| `+0x0F0`, `+0x0F4`, `+0x108`, `+0x118`, `+0x11C`, `+0x128`, `+0x130` | scalars |
| `+0x148`, `+0x14C`, `+0x150`, `+0x154`, `+0x158` | scalars |
| `+0x180`, `+0x184`, `+0x190`, `+0x198`, `+0x19C`, `+0x1A0` | vectors/scalars |

### `this+0x58` / `+0x5C` are NOT stick input — this is proven by the arithmetic

```
1DB6EC  ldp  s1,s2,[x20,#0x58]   ; observed at runtime: s1 = 10.000, s2 = 1.000
1DB6F0  ldr  s0,[x21,#8]         ; delta time
1DB6F4  fmul s1,s1,s2            ; 10.0 * 1.0 = 10.0
1DB6F8  fdiv s2,s15,s1           ; 1 / 10.0 = 0.1
1DB704  str  s1,[x20,#0x60]      ; 0.1
```

`1/(min*max)` is a distance blend factor. Confirmed at runtime: `this+0x58` read exactly
`10.000` on every frame across 1320 calls, including while the right stick was being moved.

### Runtime measurement — the `0x40`–`0x7C` window is inert

A read-only build dumped `this+0x40..0x7C` as floats at frame 60 and frame 1200, with the right
stick deliberately moved in between. **Both dumps were byte-identical.** So whatever carries the
stick into this camera is not in that window.

## The question

**Where is the right-stick value that this camera uses, and what is the cleanest float (or float
pair) for a mod to write?**

Specific sub-questions:

1. **Trace the stick to this camera.** The `input+0x420` / `input+0x424` pair is read by many
   camera executes, but an exhaustive scan shows **zero stores to `+0x420`/`+0x424` anywhere in
   `.text`**. So either the producer writes through a different base register, or the input
   struct is populated by a function that takes it as a pointer parameter. Find the producer.
   Hooking the producer would let the mod supply the stick directly.

2. **`0x1DB088` calls `0x1E312C` with `x0 = this`.** That function begins
   `ldr x20,[x0,#0x28]` and does a lot of work. Is it the input gatherer? If so, what does it
   write, and to which struct?

3. **Is the rotation carried as an angle pair or as a direction vector?** Chase writes
   `+0xD0/+0xD4/+0xD8` and `+0xDC/+0xE0/+0xE4` as triples. If one is position and the other is
   look-at, then yaw/pitch are implicit. Is there an explicit yaw/pitch field anywhere in the
   object, and if so at what offset? (The Aiming camera reportedly keeps `+0x154` pitch and
   `+0x158` yaw-accum — check whether Chase has analogues.)

4. **Recommendation.** Given all of the above, what is the single best address to either
   (a) write a float to, or (b) hook, so that mouse displacement produces camera rotation while
   the stock pipeline (lag, collision, pitch limits) still applies? Rank the options and state
   the trade-offs. If the honest answer is "write the look-at vector at `this+0xDC..0xE4`", say
   so and show the code path that computes it.

## Assets

| Item | Path |
|---|---|
| Decompressed VA-mapped TOTK 1.4.2 image | `re/totk142_correct/image_mapped.bin` (VA == file offset) |
| Ghidra project, analysed | `re/ghidra_totk/TOTK142` |
| xref scanner | `re/xref142.py` |
| vtable / name-getter resolver | `re/vtables.py` |
| camera vtable dumper | `re/cameraclass.py` |
| prior reports | `docs/DELEGATION_CAMERA_FUNCTION.md`, `docs/DELEGATION_AIMING_CAMERA.md`, `docs/DELEGATION_CAMERA_MODES.md` |

All under `<repo-root>\`.

## Please do not repeat these

* **`input+0x58`** — that is `(this+0x20)+0x58`, a different struct. Tried; wrong.
* **`this+0x58`/`+0x5C`** — distance blend values, proven by arithmetic and by 1320 frames of a
  constant `10.000`. Proposed in brief #3's correction; wrong.
* **`input+0x420`/`+0x424`** — read by many executes but **never stored**; producer unknown. These
  are gate/scale values per brief #4, not injection points.
* **Vtable-slot reasoning alone.** Two earlier targets (`0x023BA670`, `0x023BA728`) were real code
  in the `EventTalk` vtable — never dispatched during exploration. Always check that slot 0's
  name getter names the mode actually in use.

## Constraints

* Read-only outside the project root.
* A labelled **"unknown"** is far more valuable than a confident guess: three wrong offsets have
  each cost a build-and-test cycle. If the stick path cannot be traced, saying so is a useful
  result and will redirect the approach.
