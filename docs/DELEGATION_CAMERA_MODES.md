# Delegation brief #4 — complete camera vtable map and mode taxonomy

## Why this is needed

We are building a native-mouse camera mod for **TOTK 1.4.2** (title `0100F2C0115B6000`) as an
exefs `subsdk` module under **Eden** on Windows.

Exploration is now hooked — `ExecutePlayerCameraChase` execute (image VA `0x001DB01C`) fires
every frame. The project's later phases require handling the other camera states:

```
throwing, Ultrahand, Recall, scope, paraglider, climbing, swimming,
horses, Zonai vehicles, special abilities, menus, map, dialogue,
cutscenes, scripted cameras
```

The brief's instruction is explicit: *do not immediately write a bespoke handler for every one —
first identify whether several states share the same camera rotation pathway.*

Brief #2 produced a partial map (indices 17–20 for Chase / Aiming / Base / AIBase / Abyss /
Horse / EventTalk). This brief asks for the **complete** version, so target selection becomes a
lookup rather than a reasoning exercise.

## Why the complete map matters — a concrete failure it would have prevented

Our first two hooks targeted `0x023BA670` and `0x023BA728`. Both are **real, live code** — but
they belong to the `ExecutePlayerCameraEventTalk` vtable (`0x0377FF58`), which only dispatches
during talk events. During normal exploration they never run, so the hooks silently did nothing.

The delegate's own correction on this was accurate and worth repeating:

> this map alone would **not** have flagged your two hooks — they are REAL code, just in the
> wrong vtable. What prevents repeats is the vtable-identity half of the rule: slot 0 of
> `0x0377FF58` resolves to `ExecutePlayerCameraEventTalk`, not `Chase`.

So the map must carry **identity** (which mode a vtable belongs to), not just real-vs-stub.

## Requested deliverables

### 1. The full vtable table

For **every** camera-class vtable in the image:

| Column | Meaning |
|---|---|
| mode name | from the slot-0 name getter (`ExecutePlayerCamera*`) |
| vtable base VA | start of the table |
| slot count / end | where the next base begins |
| per-slot classification | **REAL** code / **STUB** (lone `ret`, or `b`+`mov`+`ret`) / name getter / common-base / null |
| the override region | which slot indices differ from the shared base — for Chase this was indices 17–20 only |

Format it so a reader can answer "which slot do I hook to intercept mode X" without further
analysis. A machine-readable form (TSV or JSON) alongside the human table would be ideal.

### 2. The shared base, separated from per-mode overrides

Several slots recur in every vtable (reportedly 3, 11, 13, 14, and the `0x1A20BC`/`0x99AF8`
tail). Identify:

* which slots are **always** shared (never a target)
* which slots are the **per-mode override region** (the candidates)
* whether the override region is consistently the same index range across modes, or varies

If it is consistent, state the rule: *"hook slot N of the vtable whose slot-0 getter is
`ExecutePlayerCamera<Mode>`"*.

### 3. Mode → pathway grouping

The brief's real question: **do several states share one camera rotation pathway?** Group the
modes by which execute they actually use. Specifically:

* which modes reuse `ExecutePlayerCameraChase`?
* which have their own execute (Aiming, Horse, Abyss, …)?
* which are *event/scripted* cameras that override everything (cutscenes, dialogue)?
* is there a mode where camera rotation is not stick-driven at all (menus, map, scope)?

A grouping table with the evidence is more valuable than an exhaustive slot dump.

### 4. The input-struct convention

For Chase we established:

```
this+0x20                  -> input struct pointer
input+0x58 / input+0x5C    -> RAW stick pair the execute reads
input+0x60 / input+0x68    -> processed values the execute writes
input+0x420 / input+0x424  -> stick scalars
```

Note the register aliasing that made this non-obvious — `x20` is reloaded with `this+0x20`, so
later `[x20,#off]` are input-struct accesses:

```
1DB058  ldr x26,[x0,#0x20]
1DB09C  ldr x22,[x20,#0x20]     ; x20 := this->inputStruct   <-- RELOAD
1DB6EC  ldp s1,s2,[x20,#0x58]   ; RAW stick
```

**Question:** is this convention shared by all camera executes? If the other modes read the raw
stick at the same struct offsets, then a single injection technique covers every mode that uses
the standard input path — which would answer the brief's grouping question decisively.

### 5. Explicitly out of scope

* Constructors / how camera actions are instantiated. Brief #2 could not recover these (zero
  `ADRP+ADD` refs to any camera vtable base; creation appears to go through an AI factory). Do
  not spend budget on it unless it is cheap.
* UltraCam offset extraction — already triaged as unrecoverable (runtime-decoded `UCNVN` blob).
* Decompiling everything: assembly plus vtable/name-getter evidence sufficed for Chase and is
  preferred.

## Assets

Project root: `<repo-root>\`

| Item | Path |
|---|---|
| Decompressed VA-mapped TOTK 1.4.2 image (**use this**) | `re/totk142_correct/image_mapped.bin` |
| Ghidra project, analysed | `re/ghidra_totk/TOTK142` |
| vtable / name-getter resolver | `re/vtables.py` |
| camera vtable dumper | `re/cameraclass.py` |
| xref scanner | `re/xref142.py` |
| caller/reference finder | `re/find_camera_callers.py` |
| brief #2 result | `docs/DELEGATION_CAMERA_FUNCTION.md` |
| brief #3 (aiming, in flight) | `docs/DELEGATION_AIMING_CAMERA.md` |

Module layout: `.text` `0x0`+`0x2BA61F0`, `.rodata` `0x2BA7000`, `.data` `0x3557000`.

## Known traps to check for on every slot

* **Stub slots.** Chase slot 20 = `0x023B77D4` is a lone `ret`; the real body is 4 bytes later at
  `0x023B77D8`, reached only by a direct `bl`, never virtually. Reported stub forms:
  `ret` alone, and `b`+`mov`+`ret`.
* **Abstract bases.** `Base` (`0x0377ED30`) and `AIBase` (`0x0377E940`) have stubs in slots
  18–20; they are never valid targets.
* **Signature uniqueness.** The Chase prologue matched twice at 48 bytes; a unique signature had
  to be taken from inside the body (`entry+0x4C`, 32 bytes, one match). If you propose a
  signature, state its uniqueness count and the offset from the entry.

## Constraints

* Read-only outside `<repo-root>\`.
* A labelled "unknown" is more valuable than a confident guess — two earlier wrong addresses each
  cost a build-and-test cycle.
* Prioritise deliverable 3 (mode grouping) and 4 (input convention) over exhaustive slot dumps if
  budget is limited: those two determine whether the mod needs one technique or many.
