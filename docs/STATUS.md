# Status — CAMERA INJECTION WORKS

## Headline

**The mod moves TOTK's camera through the game's own rotation pipeline.** Confirmed at runtime
(nm21): writing the float pair at `(this+0x20)+0x420` / `+0x424` from a hook on
`ExecutePlayerCameraChase` (image VA `0x1DB01C`) makes the camera rotate, while the stock
pipeline — lag, collision, pitch limits, mode handling — remains in control.

What remains is plumbing, not discovery: feed **mouse counts** instead of the fixed oscillating
test value, with HID brought up somewhere safe (never in `exl_main`, never on the camera thread).

See `docs/WORKFLOW.md` §10 for the confirmed mechanism and the delivery method that worked.

_Every claim below is backed by an artifact in this repo or a source citation in
`docs/research.md`._

## Completed and verified

| # | Item | Evidence |
|---|---|---|
| 1 | Installed UltraCam identified | `exefs/subsdk3`, 1,251,511 B, SHA-256 `3273A1F6…36FB9` |
| 2 | UltraCam is exlaunch-based, C++/ImGui/NVN | string + symbol extraction from the decompressed module |
| 3 | UltraCam's config keys decoded | live `sdmc/UltraCam/TOTK/Config/maxlastbreath.ini` |
| 4 | UltraCam hooks "the camera controller" on 1.4.x | in-binary `Camera Controller Hook Fix 1.4.0+`, `LayerCalc Hook 1.4.0+` |
| 5 | UltraCam does **not** read mouse/keyboard via `nn::hid` | zero ARM64 refs to those symbol addresses (two independent methods) |
| 6 | UltraCam's game offsets are **not** statically recoverable | its strings live in a runtime-decoded `UCNVN` blob; no pointer/index tables for offsets |
| 7 | Eden exposes a real HID mouse to the guest | Eden `hid_core/resources/mouse/mouse.cpp` |
| 8 | That mouse requires `hid` cmd 21 `ActivateMouse` | Eden `hid_core/resources/controller_base.cpp` |
| 9 | `MouseState` layout + shared-memory offset 0x3400 | Eden `hid_core/hid_types.h`, `shared_memory_format.h` |
| 10 | TOTK 1.4.2 `main` extracted and decompressed | 45.7 MB `.text`; build id **matches** Eden's log exactly |
| 11 | TOTK camera anchors located | `QueryPlayerCameraControllerInput`, `LatStickScale`, `LngStickScale`, `*InAir`, `horizontal/verticalSensitivity`, `CameraRotateSpeed` |
| 12 | `PlayerCamera` vtable located | `0x038AF960`; `QueryPlayerCameraControllerInput` getter `0x026E509C` |
| 13 | Mod can coexist with UltraCam | Eden loads `subsdk0`…`subsdk9`; `subsdk1` is free, UltraCam owns `subsdk3` |
| 14 | **Mod source written** | `mod/source/**` — mouse input, config, displacement camera driver + sampler thread, hook table, entry point |
| 15 | **Build system prepared** | `mod/config.mk`, `mod/Makefile`, `mod/config/module.json` (TOTK title id + `hid` service) |
| 16 | exlaunch framework obtained | `re/exlaunch` (shadowninja108), API and build model mapped |
| 17 | **Real build-ID version guard** | build id found in a GNU note at module offset `0x03556048`; verified 1.4.2 `5CB42B1C…` vs 1.0.0 `082CE09B…` |
| 18 | libnx API usage verified | `hidInitializeMouse(void)`, `hidGetMouseStates(HidMouseState*, size_t)`, `HidMouseAttribute_IsConnected` checked against libnx `hid.h` |
| 19 | Wheel byte-order landmine documented | libnx orders `{wheel_delta_x, wheel_delta_y}` but Eden uses `{delta_wheel_y, delta_wheel_x}`; bytes line up, see `mouse_input.cpp` |
| 20 | Camera anchors ruled out | the ~20 `LatStickScale`/`LngStickScale` sites are AIDef *field definitions*, not rotation arithmetic (see `research.md` §3.1) |
| 21 | Pad-object anchors found | `QueryControllerGetStick` `0x02CD3D85` (getter `0x02607C90`, vtable `0x03849E80`), `PlayerCameraControllerInput`, `ControllerMgr` |
| 22 | **Signature scanner added** | `mod/source/nativemouse/scanner.{hpp,cpp}` — nibble-wildcard patterns, refuses ambiguous matches, so the mod can become build independent once a pattern is known |
| 23 | **Scanner algorithm unit-tested** | `re/test_scanner.py` — 11 cases, all pass (including "reject when matched twice") |
| 24 | Build-ID offset double-checked | NSO header confirms `.rodata` MemoryOffset `0x2BA7000` == my computed layout, so `0x03556048` is exact |

## Blocker 1 — camera hook address

**The concrete TOTK 1.4.2 function that consumes the right stick for the camera is still not
identified.** That module-relative address is the one value the mod is missing;
`mod/source/nativemouse/hooks.hpp` exposes it as `RightStickConsumer` (currently `0`), which
puts the mod into diagnostic mode (loads, activates the mouse, samples, logs — does not move
the camera).

Routes already **ruled out** rather than assumed:

* UltraCam's offsets — its strings live in a runtime-decoded `UCNVN` blob with zero code
  references, and its offset tables have no recoverable pointer/index structure.
* The ~20 `LatStickScale` / `LngStickScale` sites — those are `AIDef` *field definitions*
  (build `{name, value}`, call a property-set virtual, branch on failure), not rotation math.
  Documented with disassembly in `research.md` §3.1.
* The values beside them (`0x2CEF014`…) — a defaults table, not type information.
* `PlayerCamera` vtable slots — mostly name-getter leaves plus `ret` stubs.
* Direct `bl` callers of the name-getter functions — there are none; they are vtable-only.

Still in flight: Ghidra headless over the 58 MB flat image (bounded run, post-script attached).
Fallbacks: vtable walk from the pad object `0x03849E80`, or tracing `nn::hid::GetNpadStates`
consumers.

## Blocker 2 — no build toolchain on this machine

`aarch64-none-elf-gcc`, `make`, `cmake` and `ninja` are all absent, and devkitPro is **not**
available via winget (`winget search devkitpro` → no match). Its Windows installer is
GUI-only, so the mod cannot be compiled here without either a manual GUI install or an
unattended msys2/pacman path. See `docs/BUILD.md`. Until then the mod source is unbuilt and
therefore unverified at the binary level — everything above is source-level and
static-analysis evidence only.

## Second blocker: unbounded rotation needs Eden

Confirmed from Eden's source:

* With `mouse_enabled=true`, Eden reports the **absolute cursor position**, clamped to the
  window, and derives `delta_x`/`delta_y` from it.
* Eden only re-centres the cursor when `mouse_panning=true && mouse_enabled=false` — and in
  that mode the HID mouse LIFO is **zeroed**, so the guest sees nothing at all.

So with any current Eden configuration the mouse deltas **stop at a window edge**. The brief's
requirement —

> Continuous rotation: the camera must not stop because a virtual stick or cursor reached an
> artificial boundary

— cannot be satisfied from inside TOTK. It needs a small Eden-side change: publish **raw
relative deltas** and release/re-centre the cursor while the mod is active. Roughly the size
of Eden's existing mouse-panning branch.

## Next steps, in order

1. **Decide on the Eden patch** for unbounded relative mouse deltas (above).
2. **Finish locating the camera hook** and record the address plus a byte signature.
3. **Install devkitPro** and build `subsdk1` from `mod/` (see `docs/BUILD.md`).
4. **Validate**: tiny motion responds immediately; a flick is not speed-capped; equal
   physical distance gives equal rotation at 30/60 FPS; collision and pitch limits intact.
5. **Then** bow aiming (`CameraAiming`), lock-on, and the other camera states.

## Explicitly **not** done

Per the brief: no work on improving Eden's mouse-to-stick curves, deadzones, acceleration or
stick range. That path appears only as a comparison baseline.
