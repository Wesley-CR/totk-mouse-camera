# Delegation brief #2 — find TOTK 1.4.2's per-frame camera update

## Context

We are building a native-mouse camera mod for **TOTK 1.4.2** (title `0100F2C0115B6000`) as an
exefs `subsdk` module, exlaunch-based, running under **Eden** on Windows.

Goal of the mod: feed mouse displacement into TOTK's *own* camera rotation path, so camera lag,
collision, pitch limits, lock-on and bow aiming all stay stock.

**Everything except one thing now works.** Proven at runtime from Eden's log:

* the module loads, exlaunch initialises, coexists with UltraCam, game boots
* the game's main module base is resolved: `0x809a5000`–`0x84472000`
* `nn::ro::LookupSymbol("nnMain")` works
* **runtime signature scanning works.** A 48-byte signature of a camera function matched at
  `0x82D5F670`, and `0x82D5F670 - 0x809a5000 = 0x23BA670` — exactly the intended image VA
* **exl::hook::Hook() installs trampolines successfully**; the game boots with hooks applied

**The single blocker: the right function to hook has not been identified.** Two attempts hooked
functions that are never called.

## Environment and assets

Project root: `<repo-root>\`

| Item | Path |
|---|---|
| Decompressed, VA-mapped TOTK 1.4.2 image (**use this**) | `re/totk142_correct/image_mapped.bin` (60,898,656 bytes, VA == file offset) |
| Installed UltraCam module | `%APPDATA%\eden\load\0100F2C0115B6000\!!!!TOTK Optimizer\exefs\subsdk3` |
| UltraCam, corrected image | `re/uc_correct/image_mapped.bin` |
| Ghidra project (TOTK) | `re/ghidra_totk/TOTK142` (analysed, 144,216 functions) |
| Useful scripts | `re/xref142.py` (ADRP+ADD/ADR/LDR-literal xref scan), `re/vtables.py` (resolves the `adrp/add/ret` "name getter" idiom), `re/cameraclass.py`, `re/find_camera_callers.py`, `re/nso_unpack2.py` |

Game module layout (module-relative VAs): `.text` `0x0`+`0x2BA61F0`, `.rodata` `0x2BA7000`,
`.data` `0x3557000`. Runtime base in the observed run: `0x809a5000`.

## What has been established about the camera

**Object/field map** (from disassembly of camera methods; `x19` = `this`):

| Offset | Role |
|---|---|
| `0x00/04/08` | position (`mPos`) |
| `0x0C/10/14` | look-at target (`mAt`) |
| `0x18/1C/20` | up vector |
| `0x5C`, `0x60` | **stick pair (read)** |
| `0x6C`, `0x70` | **rotation deltas (written)** |

This matches `sead::LookAtCamera`, including the layout independently recovered from the public
TOTK mouse-cam mod.

**Class name getters located** (each is an `adrp x0,#page ; add x0,x0,#imm ; ret` leaf returning
the method name string). Each is referenced by exactly ONE vtable slot:

| Method | getter VA | vtable slot holding it |
|---|---|---|
| `ExecutePlayerCameraChase` | `0x023B7A20` | `0x0377EFE0` |
| `ExecutePlayerCameraAiming` | `0x023B4118` | `0x0377EA80` |
| `ExecutePlayerCameraBase` | `0x023B6444` | `0x0377ED30` |
| `ExecutePlayerCameraHorse` | `0x023BFEC4` | `0x3780B78` |
| `ExecutePlayerCameraAbyss` | `0x023B1E88` | `0x0377E7D0` |
| `ExecutePlayerCameraAIBase` | `0x023B21F8` | `0x0377E940` |

**Two functions hooked, neither ever called:**

1. `0x023BA728` — in the vtable at `0x0377FF58`-region (which also names
   `ExecutePlayerCameraEventTalk`). Dense FP (`fmov:49, fsub:42, fcmp:32, fmadd:23, fmul:22`),
   body 1417 instructions. **Zero direct `bl` callers**; referenced only by vtable slot
   `0x0377FFF0`.
2. `0x023BA670` — the stick-scale helper containing
   `ldr s0,[x0]` / `fmul s0,s8,s0` / `str s0,[x19,#0x6c]`. Also referenced only by a vtable slot.

**A trap worth knowing:** `PlayerCameraChase` vtable slot `0x0377F080` = `0x023B77D4`, and
`0x023B77D4` is a lone `ret`. The real body begins at `0x023B77D8`, which **nothing points to**.
So at least one camera vtable contains stub slots, and vtable-slot reasoning must be checked
against the actual bytes.

## The question

**Which TOTK 1.4.2 function is executed once per frame to update the normal third-person
player camera, and how can the mod reach it?**

Specifically:

1. **Identify the per-frame player-camera update.** It should read the right-stick values and
   write camera position/look-at (offsets `0x00`–`0x20`) or the rotation deltas (`0x6C/0x70`).
   A concrete module-relative VA is the deliverable.
2. **Explain why the two candidates above are never dispatched.** Are their vtables attached to
   objects that are only constructed for other camera modes? Is the real update reached via a
   non-virtual call from the camera AI's tick?
3. **Characterise the camera vtables**: for the Chase camera specifically, which slots hold real
   code versus stubs, and what is the object layout of the class instance?
4. **If the update is not practically hookable, propose the best alternative injection point** —
   ideally the earliest point where the right-stick value for the camera is available as a
   `float` pair, whether that is a virtual, a plain function, or a data structure the mod could
   write.

## Method suggestions

* The Ghidra project is already analysed — prefer it over rebuilding.
* `re/vtables.py` resolves name getters; `re/xref142.py` finds ADRP+ADD references (note that a
  function used only through a vtable has **no** `bl` callers and no ADRP+ADD reference — that is
  expected, not a dead end).
* Consider starting from `nn::hid` / pad-state consumers: the camera ultimately reads controller
  input, so finding what reads pad state near the camera code may locate the update.
* Finding the **constructor** of the Chase camera class would reveal its vtable and its true
  method slots.
* `0x023BA728` being 1417 instructions and never called is suspicious — check whether it is dead
  code, or reached only via a function-pointer table that is itself relocated.

## Constraints

* Read-only outside `<repo-root>\`. Do not modify the
  installed game, mod, or Eden configuration.
* The mod is a user-owned binary being analysed for interoperability.
* Where you cannot determine something, say so explicitly. Two earlier wrong guesses each cost a
  build-and-test cycle, so a clearly-labelled "unknown" is more valuable than a confident guess.
