# WORKFLOW.md — how this project is worked, and what is already built

Handoff document. Read this first; it is the accumulated method, the tooling inventory, and the
hard-won lessons. Everything here is evidence-backed — where something is an assumption it is
labelled as one.

---

## 1. The project in one paragraph

Build a **TOTK 1.4.2 native-mouse camera mod** (title `0100F2C0115B6000`) as an exefs `subsdk`
module, exlaunch-based, running under **Eden** on Windows. The mouse drives TOTK's *own*
gameplay camera rotation path so that camera lag, collision, pitch limits, lock-on and bow
aiming all stay stock. The user runs UltraCam 3.0.0 in the `subsdk3` slot; this mod lives in its
own folder (`!!NM_*`) in a different slot and must not disturb it.

---

## 2. THE most important method: delegate the hard reverse-engineering

**This has been the single highest-leverage technique in the project.** Five briefs have been
written and four returned; every major breakthrough came from one.

Why it works: the struggling task is almost always *search* — finding a function in a 60 MB
stripped ARM64 binary. That is bounded, well-specified work that a focused agent with a good
brief does far better than the main agent iterating blindly under context pressure.

### How to write a brief that works

Put these in every one (templates in `docs/DELEGATION_*.md`):

1. **The single question**, stated first and unambiguously.
2. **What is already proven**, with runtime evidence — so they do not re-derive it.
3. **Exact asset paths** — the decompressed image, the Ghidra project, the helper scripts.
4. **Negative results**, so they do not repeat known-dead ends. This is the highest-value
   section. Every returned brief that included it produced usable work.
5. **"A labelled *unknown* is more valuable than a confident guess"** — verbatim. Three wrong
   addresses each cost a real build-and-test cycle; saying "not found" is a useful result.
6. **Constraints**: read-only outside the project root.

### Ask them to verify their own claims

The most valuable returns *corrected the main agent*:

* brief #2 located Chase at `0x001DB01C`, outside the window a prior search had used
* brief #3 caught a register misread: `0x1DB09C` is `ldr x22,[x20,#0x20]`, **not** a write to
  `x20` — so an entire field-map correction was based on a misread destination register
* brief #5 disproved its own predecessor's "zero stores" premise by scanning properly

Treat a return as a *claim to verify*, not a fact. The main agent independently confirmed the
`ldr x22` decode at the instruction-word level before acting on it, and that was correct to do.

---

## 3. THE second most important method: measure at runtime, do not infer

**Every single offset guessed from static analysis was wrong.** The ones that worked came from
runtime measurement or from disassembling the exact hooked function.

The technique that should be used first, not last (**nm20** pattern):

> Hook the target. **Dump a window of the object as floats twice** — early and later — with the
> user performing a specific input in between. Diff the dumps. Fields that change are live;
> fields that do not are not. **Write nothing.**

This is read-only, cannot corrupt anything, and converts "which offset?" from a guess into a
measurement.

**Critical corollary:** when asking the user to test, **specify the input explicitly**. A dump
taken while the stick was neutral produced identical early/late values, which was misread as "this
window is inert". It was meaningless — nothing could have changed. State the exact input to
perform.

---

## 4. Calibrate addresses at runtime instead of hardcoding

**Never trust a static address.** Establish a calibration point at runtime, then derive the rest:

* resolve a symbol by name (`nn::ro::LookupSymbol("nnMain")`) — this is what UltraCam does
* or find the function by **byte signature** and derive its entry from a known offset

Then, if you must use a static VA:

```
module_base = runtime_address_of_known_symbol - its_image_VA
runtime_of_X = module_base + image_VA_of_X
```

Validate the calibration by checking the arithmetic lands on the intended image VA. Example that
worked:

```
sig hit            = 0x80362068
entry = hit - 0x4C = 0x8036201C
0x8036201C - base(0x80162000) = 0x1DB01C   <- exactly the expected image VA
```

That self-check is what makes a calibration trustworthy.

### Signature construction

* Check uniqueness in the image: the Chase 48-byte prologue matched **twice**; a 32-byte slice at
  `entry+0x4C` matched **once**. Always state the match count.
* Code bytes are safe (no relocations). **Vtable slots are not** — they hold relocated absolute
  pointers and cannot be pattern-matched at runtime.
* Prefer a slice *inside* the body, not the prologue: prologues repeat heavily (the first 32
  bytes of Chase-execute matched **237** functions).

---

## 5. Eden / exlaunch environment facts (all verified)

### Where things live

| Item | Path |
|---|---|
| Eden data | `%APPDATA%\eden` |
| Mods | `%APPDATA%\eden\load\0100F2C0115B6000\<ModFolder>\exefs\` |
| SD card | `%APPDATA%\eden\sdmc` |
| Eden log | `%APPDATA%\eden\log\eden_log.txt` |
| Eden exe | `%USERPROFILE%\Downloads\Eden-Windows-defddec47f-amd64-msvc-standard\eden.exe` |
| UltraCam | `...\!!!!TOTK Optimizer\exefs\subsdk3` (1,251,511 B, SHA-256 `3273A1F6…36FB9`) |

### Module slots

Eden loads `rtld, main, subsdk0..subsdk9, sdk` — **every one present**. Mod folders are applied in
ascending name order, **first match per filename wins**. UltraCam owns `subsdk3`; this mod uses
`subsdk1` in its own folder. Never ship a second `subsdk3`.

### NPDM is shared process metadata

Only one `main.npdm` is in effect. `!!!!TOTK Optimizer` sorts before `!!NM_*`, so **UltraCam's
npdm probably wins** — meaning NPDM permission edits to our copy may have no effect. UltraCam's is
1,580 bytes with filesystem perms `0xFFFFFFFFFFFFFFFF`; an exlaunch-generated one is ~1,508 with
`0x4000000000000000`.

### The hard constraints — learn from these, they cost many cycles

| Do NOT | Why |
|---|---|
| call `hidInitialize()` / `hidInitializeMouse()` in `exl_main` | **wedges the game** — `exl_main` is a module initialiser on the loader's thread |
| call `fopen("sdmc:/…")` in `exl_main` | same wedge — the filesystem is not mounted yet |
| call `threadCreate` in `exl_main` | has killed every build that tried it |
| hook several new things in one build | you lose the ability to attribute the failure |

**The safe pattern (UltraCam's, from the investigator's report):** `exl_main` does almost nothing.
Resolve `nnMain` via `nn::ro::LookupSymbol`, install a trampoline, and do all filesystem/service
work **inside that hook** — the game's own thread, after boot.

**The safe module-init budget, proven:** `exl::hook::Initialize()`, `nn::ro::LookupSymbol`, and
**one** `exl::hook::Hook()` install. That combination boots reliably (nm16, nm19, nm20).

### One change per build, always

This was violated three times (nm12, nm17, nm18) and each violation cost a cycle. When a build
fails, you must be able to name the single cause.

---

## 6. Testing loop

1. Build: `re/build_cmd.sh` logic (see §7), tag bumped each time
2. Install into a **fresh** folder `!!NM_<tag>` — delete previous `!!NM_*` first
3. **Tell the user the exact input to perform** (e.g. "hold the right stick left ~3 s")
4. User launches via the **GUI** (double-click Eden, then the game), plays, closes Eden normally
5. Read `%APPDATA%\eden\log\eden_log.txt` filtered on the build tag

### Eden logging gotcha — this wasted two cycles

Eden buffers `svcOutputDebugString`. **Enable Debug → Logging → "Flush log output on each line"**.
Before that, the log was frozen and appeared to "stop" at a point that did not match reality.
`showConsole=true` routes logging to a console instead of the file — another reason the file can
look stale.

**Always check the log's mtime** before drawing conclusions from it.

### Do not try to drive Eden from the CLI

Launching `eden.exe -g <rom>` from a script resolves paths to `C:/Users/user/...`, which **does not
exist**, so **no mods load at all** — it silently tests a stock game. The user must launch via the
GUI. (An automated CPU-sampling harness exists at `re/run_eden_test.ps1` but must not be trusted
for mod results.)

### Build tag = build identity

Each build sets `EXL_MODULE_NAME` to a unique tag (`nm17`, `nm18`, …) in
`build-nativeMouse/source/program/setting.hpp`. Exlaunch prefixes **every** log line with it, so
`[nm17|exlaunch]` proves which build ran. Combined with a fresh mod folder, stale launches are
detectable. Verify the installed file's SHA-256 matches the build output.

---

## 7. Tooling inventory

All under `<repo-root>\`.

### `re/` — reverse-engineering

| Tool | Purpose |
|---|---|
| `nso_unpack2.py` | **correct** NSO unpacker. Segment fields at `0x10/0x20/0x30`, compressed sizes at `0x60/0x64/0x68`. Produces a VA-mapped image. Verified by hash match. |
| `xref142.py` | ADRP+ADD / ADR / LDR-literal cross-reference scanner for the flat TOTK image |
| `vtables.py` | resolves the `adrp x0,#page ; add x0,x0,#imm ; ret` name-getter idiom; dumps vtables with method names |
| `cameraclass.py` | dumps camera vtables |
| `find_camera_callers.py` | finds ADRP+ADD and pointer-slot references to a function |
| `run_eden_test.ps1` | automated launch + CPU sampling (**not** for mod validation, see §6) |
| `test_scanner.py` | unit tests for the mod's pattern scanner |

**Older, partly-wrong tools** — do not trust their output without checking: `nsoimg.py`,
`nso_unpack.py`, `kiplz4.py` (12-byte stride bug), `uc_img/` (corrupt after `0x168EA0`).
**Use `totk142_correct/image_mapped.bin` and `uc_correct/image_mapped.bin` (UltraCam).**

### Build tree — `build-nativeMouse/`

An exlaunch checkout with the mod's sources copied in. Build via the devkitPro msys2 shell:

```sh
export DEVKITPRO=/c/devkitPro
export PATH=/c/devkitPro/devkitA64/bin:/c/devkitPro/tools/bin:$PATH
cd <repo-root>/build-nativeMouse
make BINARY_NAME=subsdk1 APP_JSON=.../module.json
```

Output: `build-nativeMouse/deploy/subsdk1` + `main.npdm`.

### exlaunch build gotchas (already fixed in the tree — do not regress)

exlaunch has bit-rotted against current libnx + GCC 16. Fixes applied in
`build-nativeMouse/source/`:

* `lib/nx/{types,result,smc,arm/tls,kernel/svc}.h` replaced with **forwarding shims** to libnx
* `lib/nx/smc.c` deleted (libnx provides those symbols)
* `lib/util/sys/cur_proc_handle.cpp` — `InfoType_MesosphereCurrentProcess` / `R_TRY` guarded out
* `config.mk`: C++-only warning flags must be in `CXX_FLAGS`, never `C_FLAGS`

---

## 8. What is PROVEN (runtime evidence)

* module loads, exlaunch initialises, coexists with UltraCam (UltraCam file hash-verified untouched)
* `exl_main` runs; `exl::hook::Initialize()`, `nn::ro::LookupSymbol`, and one hook install are safe
* game's main module base resolves; `nn::ro::LookupSymbol("nnMain")` works
* **signature scanning works** — matched exactly the intended image VA
* **`exl::hook::Hook()` trampolines work** — `ExecutePlayerCameraChase` at image VA `0x001DB01C`
  (signature at `entry+0x4C`) is entered **every frame**, 1320+ calls, stable `this`
* **mouse input reaches the game** — the user verified UltraCam's FreeCam responds to the mouse
* bow aiming and scope are distinct cameras with confirmed addresses

## 9. What is NOT proven

* **the mod does not yet move the camera.** Three candidate offsets were each wrong:
  `input+0x58` (wrong struct), `this+0x58` (a `1/(min*max)` distance blend, constant `10.0`),
  `input+0x420/424` (producer initially unfound)
* NPDM permission edits may be inert because UltraCam's npdm likely wins
* UltraCam's own offsets remain unrecoverable (runtime-decoded `UCNVN` blob)

---

## 10. CAMERA INJECTION — SOLVED (runtime-confirmed)

**Writing `(this+0x20)+0x420` / `+0x424` from the Chase hook MOVES THE CAMERA.** Confirmed by the
user watching the camera swing in response to a fixed oscillating test write (nm21).

Proof from Eden's log:

```
NativeMouse: nm21 #1   this=0x21a6697fc0 in=0x21a66917c8 wrote=0.00 readback=0.0000,0.0000
NativeMouse: nm21 #120 this=0x21a6697fc0 in=0x21a66917c8 wrote=1.00 readback=1.0000,0.0000
NativeMouse: nm21 #240 this=0x21a6697fc0 in=0x21a66917c8 wrote=0.00 readback=0.0000,0.0000
NativeMouse: nm21 #360 this=0x21a6697fc0 in=0x21a66917c8 wrote=1.00 readback=1.0000,0.0000
```

Stable `this`/`in` across the run; readback tracks the written value exactly; the camera visibly
responded. Because the write lands **before** the game's own consumers, the stock pipeline still
applies — camera lag, collision, pitch limits — and it bypasses the `0.005` stick deadzone.

**The delivery mechanism that mattered, and should be reused:**

* hook Chase at `0x1DB01C` by signature (32 bytes at `entry+0x4C`, unique in `.text`)
* in the hook: `input = *(self+0x20)`; write the float pair to `input+0x420` / `+0x424`; call orig
* **remove every other variable.** nm21 had no HID, no threads, no filesystem, and wrote a fixed
  oscillating value instead of mouse input. That is why it finally worked: nm17 had already
  written these same offsets and appeared to fail, but its crash came from the lazy
  `hidInitialize()` on the camera thread. **The offset was right two builds earlier and the result
  was never observable.**

The per-frame stick producer for reference is the function containing `0x886824`:

```
str s9, [x20,#0x420]
str s10,[x20,#0x424]
```

**Crucial irony worth understanding:** nm17 *already wrote exactly these offsets* — and then
crashed. The nm17 crash is attributed to its **lazy `hidInitialize()` on the camera thread**, not
to the write. nm18 removed the HID but added a `threadCreate` and died before its own log line.
**So the correct next build is: the nm17 write, with the HID brought up somewhere safe (a worker
thread started after boot, or a deferred hook) and nothing else new.**

Recommended build:
1. hook Chase at `0x1DB01C` by signature (proven)
2. in the hook: read `input = *(self+0x20)`; write mouse-derived `[-1,1]` floats to
   `input+0x420` / `+0x424`; call orig
3. HID: bring up on a worker thread **created from inside the Chase hook on first call**, then
   only read; or skip HID entirely for one build and write a **fixed oscillating test value** to
   prove the write reaches the camera
4. log a readback of `input+0x420/424` to confirm the values survive to `0x1DB548`

Step 3's "fixed oscillating value" variant is the cleanest possible experiment: it removes HID as
a variable entirely and answers "does writing here move the camera?" in one run.

### After exploration works

| Mode | Execute | Vtable | Slot addr |
|---|---|---|---|
| Chase (exploration) | `0x1DB01C` | `0x0377EFE0` | `0x0377F078` |
| Aiming (bow) | `0x023B2BAC` | `0x0377EA80` | `0x0377EB18` |
| AimingTelescope (scope) | `0x023B4AE0` | `0x0377EBD8` | `0x0377EC70` |
| LockOn = Target = UltraHand | `0x23CB374` | `0x037811F8` | `0x03781290` |
| Horse | `0x23BE74C` | `0x03780B78` | `0x03780C10` |

Aiming reads `input+0x420/424` too (shared convention); it is gated on those being non-zero, so a
Chase-style write reaches it in principle. Lock-On/Target/UltraHand share one execute — one hook
covers three modes. Menus/map have **no** camera execute.

Full 42-mode table: `docs/DELEGATION_CAMERA_MODES.md`.

---

## 11. Rule: check mode identity before choosing a target

Two early hooks (`0x023BA670`, `0x023BA728`) were **real, live code in the wrong vtable** — they
belong to `ExecutePlayerCameraEventTalk`, dispatched only during talk events. The hooks silently
never fired.

**Rule:** a vtable slot is only a valid target if **slot 0's name getter names the mode actually in
use**. Then confirm the target's first word is not `ret` (Chase slot 20 is a lone `ret` with the
real body 4 bytes later, reached only by a direct `bl` — never virtually) and not a `b` delegate.

---

## 12. Deliverables status

| Brief item | State |
|---|---|
| Source code | `mod/source/` — mouse input, config, scanner, driver; needs the final offset |
| Build docs | `docs/BUILD.md` |
| Install docs | `docs/INSTALL.md` |
| Config | `mod/config/NativeMouse.ini` |
| RE notes | `docs/research.md` (25 KB, confidence-tagged) |
| UltraCam map | `docs/research.md` §2 + `docs/ULTRACAM_INIT_REPORT.md` |
| Camera hook | located, hooked, firing — **offset still wrong** |
| Known limitations | `docs/STATUS.md` |
| Workflow | this file |

---

## 13. Tone / process notes for the next agent

* **Do not claim success you have not measured.** The user has been patient through many cycles;
  what earned trust was saying "the camera does not move yet" plainly.
* **One change per build.** This was violated three times and cost three cycles.
* **Verify your own claims before acting on them** — especially register-level reads. A misread
  destination register produced a whole wrong field map.
* **When a user report seems to contradict the evidence, believe the user.** "It did not launch"
  was right twice when the CPU harness said otherwise; "I never moved the stick" invalidated a
  conclusion.
* The user is willing to run GUI tests and to delegate RE tasks. Use both — delegation especially.
  Ask for a delegation task when stuck on *search*; do the *engineering* yourself.
