# Delegation brief #3 — the bow/aiming camera path

## Why this is needed

We are building a native-mouse camera mod for **TOTK 1.4.2** (title `0100F2C0115B6000`) as an
exefs `subsdk` module (exlaunch-based) under **Eden** on Windows.

**The exploration camera now works at the hook level.** From Eden's log:

```
NativeMouse: nm16 STEP3 chase execute entry = 0x8036201C (offset 0x1DB01C)
NativeMouse: nm16 CHASE #1    self=0x21a575a248
NativeMouse: nm16 CHASE #1200 self=0x21a575a248
```

`ExecutePlayerCameraChase` execute is hooked and fires every frame. That came from delegation
brief #2, which located it at image VA `0x001DB01C` via Chase vtable `0x0377EFE0` slot 19.

**Bow aiming is a separate camera action and is NOT covered by Chase.** The brief explicitly
requires it as phase 4, and the earlier report already noted:

> Bow aiming is separate: Aiming execute `0x023B2BAC` ← slot `0x0377EB18`. Do not assume Chase
> covers it.

That line is a pointer, not a verified analysis. This brief asks for the same depth of
verification that made the Chase finding usable.

## Critical context: where the stick actually lives

Our first offset guess was wrong, and the reason is worth knowing so it is not repeated.

Inside Chase execute the input-struct pointer is loaded **and then a register is reloaded with
it**, so the same register later refers to the input struct rather than the camera object:

```
1DB058  ldr x26,[x0,#0x20]      ; x26 = this->inputStruct
1DB09C  ldr x22,[x20,#0x20]     ; x20 := this->inputStruct   <-- RELOAD
1DB6EC  ldp s1,s2,[x20,#0x58]   ; RAW stick pair        -> input+0x58 / +0x5C
1DB704  str s1,[x20,#0x60]      ; processed horizontal
1DB730  str s1,[x20,#0x68]      ; processed vertical
1DB0xx  ldr s1,[x26,#0x420]     ; stick scalar
1DB0xx  ldr s2,[x26,#0x424]     ; stick scalar
```

So the mod writes mouse-derived values into the **raw stick pair at `(this+0x20)+0x58` /
`+0x5C`**, before the game's own processing runs, and lets the stock pipeline do the rest.

**The aiming camera may not use the same offsets or the same struct.** That is the core question
below.

## Requested deliverables

1. **Confirm the aiming execute function.** Verify `0x023B2BAC` (or correct it) the way Chase was
   verified:
   * its vtable slot address and index, and that slot 0's name getter resolves to
     `ExecutePlayerCameraAiming` (getter `0x023B4118`)
   * entry point, and a **byte signature unique in `.text`** with the exact offset from the
     signature to the entry (the Chase signature was unique at `entry+0x4C`, and the prologue
     alone was *not* unique — the 48-byte prologue matched twice)
   * confirmation the entry is real code and not a stub (Chase slot 20 held a lone `ret` with the
     real body 4 bytes later, never referenced — check for that pattern)

2. **The input-struct field map for aiming.** Where does the aiming execute read the raw stick,
   and where does it write the processed values? Give offsets relative to the input-struct
   pointer, with the instructions as evidence. If it differs from Chase's `+0x58/+0x5C` raw and
   `+0x60/+0x68` processed, say so explicitly.

3. **The object layout for aiming**, insofar as it differs: what is at `this+0x20`, `this+0x28`,
   and anything else the execute touches.

4. **Any aiming-specific gating.** Bow aiming has its own state (draw, aim, fire, scope). Is the
   execute called continuously while aiming, or only in some sub-states? Is there a separate
   first-person / scope camera that takes over?

5. **The equivalent for the scope** if it is a distinct camera (the brief mentions
   `ScopeMultiplier` as a later feature).

## Assets

Project root: `<repo-root>\`

| Item | Path |
|---|---|
| Decompressed VA-mapped TOTK 1.4.2 image (**use this**) | `re/totk142_correct/image_mapped.bin` (VA == file offset) |
| Ghidra project, already analysed (144k functions) | `re/ghidra_totk/TOTK142` |
| xref scanner (ADRP+ADD / ADR / LDR-literal) | `re/xref142.py` |
| vtable / name-getter resolver | `re/vtables.py` |
| camera vtable dumper | `re/cameraclass.py` |
| caller/reference finder | `re/find_camera_callers.py` |
| prior delegation result (Chase) | `docs/DELEGATION_CAMERA_FUNCTION.md` |
| current test state | `docs/TEST_NOW.md` |

Module-relative layout: `.text` `0x0`+`0x2BA61F0`, `.rodata` `0x2BA7000`, `.data` `0x3557000`.

## Useful negative results, so they are not repeated

* **Vtable-slot reasoning alone is not enough.** Our two earlier hooks
  (`0x023BA670`, `0x023BA728`) were *real code in the wrong vtable* — they belong to
  `ExecutePlayerCameraEventTalk` (`0x0377FF58`) and only dispatch during talk events. The
  safeguard is checking that **slot 0's getter names the mode you are actually in**.
* **Stub slots exist.** Chase slot 20 = `0x023B77D4` is a lone `ret`; the real body is 4 bytes
  later at `0x023B77D8` and is reached only by a direct `bl`. Abstract `Base`/`AIBase` cameras
  have stubs in slots 18–20.
* Floating-point density ranking over `0x23B0000`–`0x2400000` **missed Chase entirely** because
  its execute lives at `0x1DB01C`. Do not bound a search to that window.

## Constraints

* Read-only outside `<repo-root>\`. Do not modify the
  installed game, mod, or Eden configuration.
* Where something cannot be determined, say so explicitly. Two earlier confident-but-wrong
  addresses each cost a build-and-test cycle; a labelled "unknown" is cheaper than a guess.
