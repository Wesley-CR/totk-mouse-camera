# TOTK 1.4.2 Native Mouse Camera — Reverse-Engineering Notes

Target: **The Legend of Zelda: Tears of the Kingdom 1.4.2**, Title ID `0100F2C0115B6000`,
running under **Eden** (Windows). Deliverable: a TOTK exefs mod giving displacement-based
native mouse camera control while preserving TOTK's normal camera pipeline.

Status legend: **CONFIRMED** (verified against a primary artifact in this workspace),
**SUPPORTED** (evidence points this way, not yet proven), **UNTESTED**, **REJECTED**.

---

## 0. Verified environment facts

| Item | Value | Status |
|---|---|---|
| Game version | 1.4.2 | CONFIRMED (Eden log + `main.npdm`/NSP) |
| Title ID | `0100F2C0115B6000` | CONFIRMED |
| TOTK 1.4.2 `main` build ID | `5CB42B1CF25469FB0635FD046453D843C18BC8AB` | CONFIRMED |
| Eden `main` build ID | `5CB42B1CF25469FB` (short form) | CONFIRMED (Eden log `HasNSOPatch`) |
| UltraCam version | 3.0.0 | CONFIRMED (`README.txt`) |
| UltraCam module | `exefs/subsdk3`, 1,251,511 bytes | CONFIRMED |
| UltraCam SHA-256 | `3273A1F6CF5AF36A5135DA74CED66E292E4655174807E883AB4408B73A636FB9` | CONFIRMED |
| UltraCam own module id | `38EA2129009414362DBA656E3125F7D423F38607` | CONFIRMED |
| Modules Eden loads | `rtld, main, subsdk0, subsdk3, sdk` | CONFIRMED (Eden log) |
| Free exefs slots | `subsdk1`, `subsdk2`, `subsdk4`..`subsdk9` | CONFIRMED |

Build IDs (32-byte form) from the Eden log for the running title:

```
rtld    82A3CF78789F9D9858C52379D8D2D129F6297039
main    5CB42B1CF25469FB0635FD046453D843C18BC8AB   <- TOTK 1.4.2
subsdk0 3308C1D216F788397D1451C8345A16CDB4E864EF
subsdk3 38EA2129009414362DBA656E3125F7D423F38607   <- UltraCam 3.0.0
sdk     5CF080E800530A7F25BD528EC875C27B04E2F05A
```

Other TOTK versions (short form, for future version tables):
1.2.1 `9B4E43650501A4D4`, 1.4.0 `6265F94D606242CE`, 1.4.1 `965EAB9CEB8EB867`, 1.4.3 `277178B7DBA1B6D4`.

---

## 1. Container / module formats (needed for any offset work)

### 1.1 XCI extraction
The retail XCI's `secure` HFS0 partition is at file offset **0xF000** (not the spec's 0x130).
The gamecard header magic `HEAD` is at **0x100**.

```
nstool -k <prod.keys> --fsecure <outdir> game.xci        # secure partition NCAs
nstool -k <prod.keys> -x "\0" <outdir> program.nca       # ExeFS (partition 0)
```

Note: `--part0` is deprecated in nstool 1.9.2; use `-x "\0"`.

### 1.2 Base game vs update — IMPORTANT
The user's XCI is **TOTK v1.0.0** (build `082CE09B06E33A12...`); its `update` partition
contains only system NCAs (ProgIDs `0x0100000000000xxx`), **not** the TOTK update.
The running 1.4.2 program comes from the standalone update NSP:

```
updates/The Legend of Zelda Tears of the Kingdom [0100F2C0115B6800][v655360][1.4.2].nsp
  -> program NCA d214ebdad75878d540c13f3155640f39.nca  (523 MB, CT=Program)
  -> ExeFS main = 35,621,994 bytes, module id 5CB42B1C F25469FB ...
```

**CONFIRMED**: the extracted 1.4.2 `main` module id matches the build ID Eden logs, so this
is byte-for-byte the module the emulator runs.

### 1.3 NSO0 layout and LZ4 compression — CONFIRMED
TOTK's `main` NSO is **per-segment LZ4-block compressed** (header flag bit 0).
The file offsets in the header are *cumulative and loosely aligned*, which defeats naive
sequential parsing. `nstool -t nso --showlayout` reports the true layout:

```
TOTK 1.4.2 main (flags 0x3F):
  .module_name file 0x100    size 0x8
  .text        file 0x108    size 0x1b16bff (COMPRESSED)  -> mem 0x0        size 0x2ba61f0
  .ro          file 0x1b16d07 size 0x594a34 (COMPRESSED)  -> mem 0x2ba7000  size 0x9af05c
  .data        file 0x20ab73b size 0x14d52f (COMPRESSED)  -> mem 0x3557000  size 0x4bcd60
  .bss                                                      mem 0x3a13d60  size 0xb92a0
```

Each segment decompresses as **one LZ4 block** with `uncompressed_size = MemorySize`:
`lz4.block.decompress(data[file_offset : file_offset+file_size], uncompressed_size=mem_size)`.

Decompressed result (verified: 30,915 `stp x29,x30,[sp,#-N]!` prologues in `.text`):

```
.text   0x2BA61F0 (45.7 MB)   va 0x00000000
.rodata 0x9AF05C  (10.2 MB)   va 0x02BA7000
.data   0x4BCD60  ( 5.0 MB)   va 0x03557000
```

`re/totk142_flat.bin` (58.1 MB) places all three at their true VAs, so **file offset == VA**.
Scripts: `re/nso_unpack.py` (unpack), `re/xref142.py` (ARM64 xref scanner, ADRP+ADD/LDR,
ADRP/ADR/LDR-literal), `re/xci_scan.py`, `re/xci_extract.py`.

---

## 2. UltraCam 3.0.0 reverse-engineering map

Installed artifact: `re/ultracam_subsdk3.bin` (copy of the user's `subsdk3`), SHA-256
`3273A1F6...36FB9`. Also stored raw-NSO form (it is a compressed NSO0 too).

### 2.1 What it is — CONFIRMED
* **exlaunch**-based (`exl::hook::nx64::Hook` present).
* Closed source. Build path leaked in rodata:
  `%I:/GitHub/UltraCam/SRC/source/UCNVN/MemoryBuffer.cpp`.
  The UI text is stored in a **custom encoded blob named `UCNVN/MemoryBuffer.cpp`** — its
  strings are *not* individually addressable C literals.
* C++ with **Dear ImGui** (`ImGui`, `ImDrawData`, `PackIdMouseCursors`, `ImGuiInputFlags_*`).
* Renders via NVN (`NVN`, `nvn*` symbols).
* Ships a full replacement NSO at the `subsdk3` slot (not merged with the game's own subsdk3).
* Header symbol names for `nn::ro::LookupSymbol`, `nn::fs`, `nn::os`, `nn::hid` (npad) are
  present — consistent with exlaunch symbol resolution.

### 2.2 Config — CONFIRMED
Live config file (SD card path, **not** romfs):
`%APPDATA%/eden/sdmc/UltraCam/TOTK/Config/maxlastbreath.ini`

```
[Gameplay]
Stick_Horizontal_Speed = 1.00     <- multiplier on the PLAYER camera right-stick rotation
Stick_Vertical_Speed   = 1.00     <- multiplier on the PLAYER camera right-stick rotation
[UltraCam]
CameraSpeed = 90.00               <- FreeCam ROTATION speed
Speed       = 15.00               <- FreeCam TRANSLATION speed
AnimationSmoothing = 0.25
AutoHideUI = True
TriggerWithController = True
[UltraCamFPS]  MaxFPS = 60.00, DynamicFPS = True, ...
[UltraCamMenu] CameraAnimation, ToolTips
```

Also read from `sd:/UltraCam/TOTK/`: `Config/Teleports.json`, `Config/PaletteCFG.json`,
`Palettes/`, `Presets/`, `Benchmark/`.

**REJECTED**: `FirstPerson.Speed` and `FirstPerson.BowSpeed` **do not exist**. There is no
`[FirstPerson]` section in the authoritative NX Optimizer settings template, and the strings
`FirstPerson` / `BowSpeed` do not occur in this binary at all. First-person mode is an
Early-Access feature only. The brief's clues about those keys are **not usable** for 3.0.0.

### 2.3 Version handling — CONFIRMED
Version-gated feature strings in rodata:

```
LayerCalc Hook 1.4.0+
Camera Controller Hook Fix 1.4.0+
Game Time Speed 1.4.0+
Version %s - %s / UltraCam Version : %s / Game Version : %s
```

So UltraCam **does hook TOTK's camera controller** (there is an explicit per-version
"camera controller hook fix"), and addresses are hardcoded per game version, gated on 1.4.x.
Game version detection uses `nn::oe::GetDisplayVersion`.

**Useful consequence**: the existence of these hooks is independent evidence that
(a) exefs code injection at `subsdk*` works on Eden for this title, and
(b) the player-camera update path is a viable hook target on 1.4.2.

### 2.4 The UltraCam game offsets are NOT statically recoverable — evidence
Attempts made and their results:

| Attempt | Result |
|---|---|
| Scan `.data` for u64 runs of small (`<0x04000000`) 4-aligned offsets | none found |
| Scan `.rodata` for `0x7100_0000_0000 + off` absolute VAs | none |
| Scan `.rodata` for u32 runs of plausible offsets | only float/param tables, no offset tables |
| Find ARM64 refs to camera feature strings (`Camera Controller Hook Fix 1.4.0+`, …) | **0 refs** |
| Find ARM64 refs to `nn::hid` mangled names | **0 refs** |
| Find u64/u32 pointers to those name addresses anywhere in the image | **0** |
| Ghidra headless reference analysis on the same strings | **0 refs** |

**Conclusion (CONFIRMED)**: UltraCam's UI strings live in the `UCNVN` encoded blob and are
decoded at runtime, so they are never materialised as addresses. Its game offsets live inside
exlaunch's compile-time offset tables, which in this build present no readable pointer/index
structure. **The UltraCam "breadcrumbs" cannot be turned into TOTK 1.4.2 offsets by static
analysis of `subsdk3`.** Any approach that depends on extracting UltraCam's addresses is a
dead end; TOTK must be reverse-engineered directly (which this document does).

### 2.5 Does UltraCam use `nn::hid` mouse/keyboard? — REJECTED
The binary's symbol-name list in `.rodata` **does** contain:

```
_ZN2nn3hid15InitializeMouseEv
_ZN2nn3hid13GetMouseStateEPNS0_10MouseStateE
_ZN2nn3hid16GetKeyboardStateEPNS0_13KeyboardStateE
_ZN2nn3hid13GetNpadStatesEPNS0_16NpadFullKeyStateEiRKj
_ZN2nn3hid19GetTouchScreenStateILm5EEEvPNS0_16TouchScreenStateIXT_EEE
_ZN2nn2ro12LookupSymbolEPmPKc
...
```

…but `.text` contains **zero** ARM64 references to any of those addresses (verified two
independent ways: hand-written capstone scanner `re/uc_refs.py`, and Ghidra reference
analysis). The names are residue of the build, not a runtime lookup table.

**Therefore: UltraCam does not read the mouse or keyboard through `nn::hid`.**

**Consequence for the user's premise.** The observed "UltraCam mouse works in FreeCam" and
"F12 menu receives keyboard" are almost certainly *not* UltraCam reading raw mouse/keyboard.
They are the **gamepad path**: Eden maps host keyboard/mouse into Switch controller inputs
(`engine:keyboard,code:69` on `button_r`, `engine:mouse,axis_x:0,axis_y:1` on `rstick`, …),
and UltraCam reads `nn::hid::GetNpadStates`. Under that model:
* "mouse works in FreeCam" == the mouse is being delivered as right-stick deflection.
* "F12 receives keyboard" == F12 is mapped to a pad button, or UltraCam's menu is opened by
  the documented pad chord `ZL + ZR + Left Stick Press`.

This matters because it means **there is no existing raw-mouse source inside TOTK to reuse**,
so the native-mouse mod must create one. That is exactly what Eden's emulated HID mouse
provides (section 3).

### 2.6 Eden input path — CONFIRMED (from Eden source, read-only)
* `Controls.mouse_enabled` (default **false**; the user's `qt-config.ini` has **true**) makes
  Eden populate the emulated **HID mouse** shared-memory LIFO with real host mouse data:
  `x`, `y` (absolute, scaled to 1280x720), `delta_x`, `delta_y`, buttons, wheel, and sets
  `attribute.is_connected = 1`.
* **Gate**: `Service::HID::Mouse::OnUpdate` returns early and zeroes the LIFO unless
  `IsControllerActivated()`, which is only set by the guest calling `hid` command **21
  `ActivateMouse`**. No retail game does this — **a mod must call it**.
* `MouseState` layout (Eden `hid_core/hid_types.h`, sizeof 0x28):
  `0x00 s64 sampling_number; 0x08 s32 x; 0x0C s32 y; 0x10 s32 delta_x; 0x14 s32 delta_y;
   0x18 s32 delta_wheel_y; 0x1C s32 delta_wheel_x; 0x20 u32 buttons; 0x24 u32 attribute`
* HID shared memory: total 0x40000, **mouse LIFO at 0x3400** (17 entries of 0x28).
* `Controls.mouse_panning` (default false) is mutually exclusive with `mouse_enabled`
  (`IsMousePanningEnabled() = mouse_panning && !mouse_enabled`). With `mouse_enabled=true`
  the `rstick="engine:mouse,..."` binding only deflects the stick **while a mouse button is
  held**, using absolute delta from the press point at 0.06/px.
* **Limitation (CONFIRMED)**: with `mouse_enabled=true` Eden's cursor is *not* recentred
  (recentring is the `mouse_panning && !mouse_enabled` branch), and the reported `x`/`y` are
  clamped to the window. So `delta_x/delta_y` stop when the cursor hits the window edge —
  rotation is **not** unbounded. See section 5, open question.

---

## 3. TOTK 1.4.2 camera anchors — CONFIRMED

All addresses are VAs in `main` (module base + offset), recovered by string search in
`.rodata` of the decompressed 1.4.2 image:

| Symbol / AIDef param | VA | Note |
|---|---|---|
| `QueryPlayerCameraControllerInput` | `0x02C4FCB1` | name getter at `0x026E509C` |
| `CameraRotateSpeed` | `0x02C1328D` | |
| `StickSensitivity` | `0x02CAE359` | |
| `StickSensitivityLat2LngRatio` | `0x02C2A519` | gates separate lat/lng sensitivity |
| `LatStickScale` | `0x02C8D379` | BOTW `CameraChase.latStickScale` |
| `LngStickScale` | `0x02C6C32B` | BOTW `CameraChase.lngStickScale` |
| `LatStickScaleInAir` | `0x02CBE8A3` | |
| `LngStickScaleInAir` | `0x02C84FF3` | |
| `horizontalSensitivity` | `0x02C28186` | |
| `verticalSensitivity` | `0x02C30236` | |
| `LeftStickX` / `LeftStickY` / `LeftStickMagnitude` | in rodata | |
| `CameraControlStopStick` | in rodata | |
| `ExecutePlayerCameraChase` | `0x02CB9E05` | name getter at `0x023B7A20` |
| `ExecutePlayerCameraSwirl` / `…Tail` / `…EventMovePos` / `…SpecialPower` / `…Switcher` | in rodata | AI action family |

`LatStickScale` and `LngStickScale` are referenced by **40 sites** (~20 camera AI actions),
all sharing one `ADRP+ADD` idiom — the AIDef parameter-loading pattern. Default values
`latStickScale = 1.0`, `lngStickScale = 0.7` were located as a 48-byte-stride parameter table
around `.data` `0x036AA380`, matching the BOTW `CameraAiming` defaults from the zeldamods
AIDef documentation.

**Interpretation (SUPPORTED)**: TOTK keeps BOTW's architecture — the camera is AI-driven
(`CameraChase`, `CameraAiming`, `CameraLockOn`, …), and the right-stick-to-rotation step is
parameterised by `LatStickScale`/`LngStickScale` (+ `*InAir`) and/or
`horizontalSensitivity`/`verticalSensitivity`. The ideal injection point is therefore one
step *before* those multipliers: supply a synthetic right-stick value derived from mouse
displacement and let the stock pipeline (sensitivity, lag, collision, pitch limits, lock-on)
do the rest.

**Not yet located (the remaining work)**: the concrete function that converts the right-stick
value into the camera yaw/pitch delta.

### 3.1 What the camera anchors are NOT — CONFIRMED by inspection

An important negative result, established by disassembling the reference sites:

* The ~20 `LatStickScale` / `LngStickScale` reference sites are **not** the rotation
  arithmetic. Each is the same `AIDef` *field-definition* idiom:

  ```
  adrp/add   x9, "<param name>"          ; e.g. "LatStickScale"
  add        x8, x20, #<field offset>   ; where the value will land
  str        x8, [sp, #0x18]
  mov        w8, #<count>
  adrp/add   x8, 0x2CEF014               ; pointer to that field's default value
  stp        x9, x0, [sp, #8]            ; {name, value}
  ...
  ldr        x8, [x19] ; ldr x8, [x8] ; blr x8   ; property-set virtual call
  cmp        w0, #1 ; b.eq <fail>
  ```

  So they register parameters; they do not compute rotation. Decompiling them yields no
  floating point work, which is why this route stalls.
* The values used by that idiom (`0x2CEF014`, `0x2CEF018`, …) are **default parameter
  values**, a packed table of floats right after a run of `3.65 / 12.166 / 2268.0` style
  constants. The only code reference found near it (`0x003B0850`) is generic float
  range-validation, not camera logic.

### 3.2 Controller / player-camera interface anchors — CONFIRMED

| Symbol | VA | Note |
|---|---|---|
| `QueryControllerGetStick` | `0x02CD3D85` | the `SeadController`-style stick accessor |
| `PlayerCameraControllerInput` | `0x02C4FCB6` | part of `QueryPlayerCameraControllerInput` |
| `ControllerMgr` | `0x02BE4286` | matches the BOTW `sead::ControllerMgr` architecture |
| `Controller`, `LeftStick`, `LeftStickX/Y`, `LeftStickMagnitude`, `CameraControlStopStick` | in rodata | input-side names |

`QueryControllerGetStick`'s name-getter is at `0x02607C90` and is referenced from exactly one
vtable slot, `0x03849ED8` (vtable base `0x03849E80`). The vtable's other slots contain real
implementations (`0x02607F20`, `0x02608118`, `0x02608134`), so this class is a concrete
controller/pad object rather than the AI-name indirection seen on `PlayerCamera`.

### 3.3 Build-id note — CONFIRMED, enables a real version guard

The 20-byte build id lives in a GNU note as the **last 0x14 bytes of the `.rodata` segment**:

```
14 00 00 00  03 00 00 00  "GNU\0"  <20-byte build id>
```

Verified in both dumps held here:

| Version | module-relative VA of build id | value |
|---|---|---|
| 1.4.2 | `0x03556048` | `5CB42B1CF25469FB0635FD046453D843C18BC8AB` |
| 1.0.0 | end of its `.rodata` | `082CE09B06E33A123CB1E2770F5F9147709033DB` |

The mod reads these 8 bytes from the loaded module and refuses to install hooks unless they
are `5CB42B1CF25469FB`. This satisfies the brief's "build-ID validation / safe failure on
unknown versions" requirement without guessing.

**Now located — see §3.4.**

### 3.4 THE CAMERA HOOK — LOCATED (vtable slot + object field map)

Found by profiling functions for floating-point density rather than by string references.
The AIDef parameter builders all have **FP=0**, so ranking the camera-AI region
(`0x023B0000`–`0x02400000`, 1248 functions) by FP op count isolates the real arithmetic.

**`PlayerCameraBase` vtable at module offset `0x0377FF80`** (`.data`; resolved by dumping
pointer slots and naming them with the `adrp/add/ret` getter idiom):

| Slot | Address | Role |
|---|---|---|
| 12 | `0x023BA66C` | allocate / ctor |
| **13** | **`0x023BA670`** | **stick-scale helper** — contains `ldr s0,[x0]` / `fmul s0,s8,s0` / `str s0,[x19,#0x6c]` |
| **14** | **`0x023BA728`** | **per-frame camera update** — `fmov:49, fsub:42, fadd:42, fcmp:32, fmadd:23, fmul:22`, body size `0x1624` |
| 15 | `0x023BC008` | |
| 16 | `0x023BC00C` | |
| 17 | `0x023B8BA4` | called at the top of slot 14 |
| 18 | `0x023BC0B8` | |

Slot 14 is dispatched virtually, so patching the **slot** (one aligned 64-bit store, trivially
reversible) is safer than patching the function prologue, and it yields `this` directly.

**Object field map** (offsets accessed through `x19` = `this` inside slot 14, plus the
`LookAtCamera` base recovered independently from public TOTK camera work):

| Offset | Role | Evidence |
|---|---|---|
| `0x00/04/08` | camera position (`mPos`) | `sead::LookAtCamera` layout |
| `0x0C/10/14` | look-at target (`mAt`) | same |
| `0x18/1C/20` | up vector | same |
| `0x28` | pointer loaded into `x26`, read throughout | `ldr x26,[x19,#0x28]` |
| `0x58` | state flag / bool | `ldr [x19,#0x58]` |
| **`0x5C`, `0x60`** | **stick pair (read)** | slot 13 does `ldr s0,[x0]` on these |
| `0x64`, `0x68` | second float pair (read) | |
| **`0x6C`, `0x70`** | **rotation deltas (written)** | slot 13 does `str s0,[x19,#0x6c]`; slot 12 writes `1.0` to `0x70` |
| `0x80` | `20.0f` constant stored on entry to slot 12 | `mov w8,#0x41a00000` |
| `0x8C` | float set on entry to slot 12 | |
| `0x90`, `0x98`, `0xA8` | floats | |

This is the insertion point the project needs: the hook can write mouse-derived values into
`0x5C`/`0x60` before calling the original, i.e. hand the game a **synthetic right stick**, and
every downstream behaviour (lag, collision, pitch limits, lock-on, bow aiming) remains stock.

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

**Status: SUPPORTED, not yet CONFIRMED at runtime.** The next test observes slot 14 to verify
(i) the hook is called at all, and (ii) the values at `0x5C/0x60` and `0x6C/0x70` move when the
right stick moves. Only then should the write path be trusted.

---

## 4. What the config settings actually modify (brief's question 1–4)
For UltraCam 3.0.0, with confidence:

| Setting | Stage it modifies | Status |
|---|---|---|
| `Gameplay.Stick_Horizontal_Speed` / `Stick_Vertical_Speed` | **requested camera rotation** — multiplier on the player camera's right-stick rotation rate, i.e. stage 2 (not input, not interpolation, not final transform) | SUPPORTED (documented as "Increase Horizontal/Vertical Camera Speed", range 0.25–3.0) |
| `UltraCam.CameraSpeed` | FreeCam **rotation** speed (its own camera, stage 4 for FreeCam only) | SUPPORTED |
| `UltraCam.Speed` | FreeCam **translation** speed | SUPPORTED |
| `UltraCam.AnimationSmoothing/Fadeout` | FreeCam keyframe interpolation | SUPPORTED |
| `UltraCam.CameraLag` | **REJECTED — does not exist** in this build's config/template | CONFIRMED |
| `Features.GameCameraSpeed` | **REJECTED as named**; the real keys are `[Gameplay] Stick_*_Speed` | CONFIRMED |
| `UltraCam.Mouse` | **REJECTED — no such setting**; UltraCam has no mouse feature | CONFIRMED |
| `FirstPerson.Speed` / `FirstPerson.BowSpeed` | **REJECTED — absent from binary and template** | CONFIRMED |

---

## 5. Open questions / risks

1. **Unbounded rotation.** Eden's native-mouse mode clamps the cursor to the window, so
   `delta_x`/`delta_y` eventually stop. Options: (a) accept edge-stop and document it,
   (b) have the user hold a button to re-centre, (c) patch Eden to centre the cursor after
   each event (user has chosen "TOTK mod only for now"), (d) a host-side agent that sends raw
   relative deltas over UDP to a socket in the mod (works, but adds a process).
2. **Camera hook address** on 1.4.2 — remaining RE work (section 3).
3. **`ActivateMouse`** must be called by the mod before any mouse data appears.
4. **Coexistence with UltraCam** — both mods must not patch the same game function. UltraCam
   hooks the camera controller for 1.4.x, so the mouse mod should prefer a *different* site
   (e.g. the stick-value producer) or chain cleanly. Put the new module in its own mod folder
   and use `subsdk1`/`subsdk2`/`subsdk4`+ — never ship a second `subsdk3`
   (`!!!!TOTK Optimizer` sorts first because `!` = 0x21).
5. **Bow aiming** uses `CameraAiming` (separate AI action) with its own `latStickScale`
   defaults — expect a separate injection site; do not assume exploration support covers it.
6. **Lock-on** state should come from TOTK's own camera/lock-on state, not controller buttons.

---

## 6. Artifacts in this workspace

```
re/ultracam_subsdk3.bin     copy of the installed UltraCam module (hash-verified)
re/uc_img/image.bin         decompressed UltraCam image (flat, VA == file offset)
re/uc_report.txt            Ghidra reference report for UltraCam strings (0 refs)
re/exefs142b/main           TOTK 1.4.2 ExeFS main NSO (35,621,994 bytes, build id verified)
re/totk142_flat.bin         decompressed TOTK 1.4.2 segments at true VAs (58.1 MB)
re/t142_text.bin .rodata.bin .data.bin    the three decompressed segments
re/base_exefs/main          TOTK v1.0.0 base main NSO (for cross-version comparison)
re/nso_unpack.py            per-segment LZ4 NSO unpacker
re/xref142.py               ARM64 xref scanner for the flat 1.4.2 image
re/uc_refs.py               ARM64 xref scanner used to disprove the UltraCam mouse path
re/xci_scan.py, xci_extract.py, nsotool.py, nsoimg.py, strings_va.py   container helpers
re/eden_src/                Eden source (sparse, read-only reference for compatibility)
```
