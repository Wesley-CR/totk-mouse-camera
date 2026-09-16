# Test round 20 — SOLVED: camera injection confirmed

## Result

**The mod moves TOTK's camera through the game's own rotation pipeline.** Confirmed by the user
watching the camera swing in response to nm21's fixed oscillating write.

## The evidence

```
NativeMouse: nm21 entry=0x8077D01C orig=0x846de000 READY (writer, no hid, no threads)
NativeMouse: nm21 #1   this=0x21a6697fc0 in=0x21a66917c8 wrote=0.00 readback=0.0000,0.0000
NativeMouse: nm21 #120 this=0x21a6697fc0 in=0x21a66917c8 wrote=1.00 readback=1.0000,0.0000
NativeMouse: nm21 #240 this=0x21a6697fc0 in=0x21a66917c8 wrote=0.00 readback=0.0000,0.0000
NativeMouse: nm21 #360 this=0x21a6697fc0 in=0x21a66917c8 wrote=1.00 readback=1.0000,0.0000
```

* stable `this` and `in` across the whole run
* `readback` tracks `wrote` exactly — the value survives inside the struct
* the camera visibly responded

## The confirmed mechanism

```
hook ExecutePlayerCameraChase  (image VA 0x1DB01C, by 32-byte signature at entry+0x4C)
  input = *(self + 0x20)
  write float pair -> input+0x420 / input+0x424
  call original
```

Because the write lands **before** the game's own consumers, the stock pipeline still owns
everything: camera lag, collision, pitch limits, mode transitions. It also bypasses the `0.005`
stick deadzone, which is exactly what the brief wanted — displacement-based input rather than a
velocity-clamped analog stick.

The per-frame stick producer is the function containing `0x886824`, which writes those same
offsets after deadzone/scale/clamp. Writing from the Chase hook is preferred: no new hook, and
Chase only runs in exploration.

## Why it finally worked, and the lesson

**nm17 already wrote these exact offsets** and appeared to fail. Its crash came from the lazy
`hidInitialize()` on the camera thread — **not** from the write. nm18 removed the HID but added a
`threadCreate` and died before logging.

nm21 removed **every** other variable: no HID, no threads, no filesystem, and a fixed oscillating
value instead of mouse input. The offset had been right two builds earlier; the result was simply
never observable.

**Every single offset guessed from static analysis in this project was wrong.** The one that
worked came from a delegation brief that traced the producer to a specific instruction, and was
then verified by an experiment with one variable.

## What remains — plumbing, not discovery

1. Feed **mouse counts** instead of the oscillating test value
2. Bring HID up somewhere safe: a worker thread started **after** boot, never in `exl_main` and
   never on the camera thread
3. Read `sdmc:/NativeMouse.ini` for sensitivity / invert (only after the filesystem is mounted)
4. Optional smoothing

## Design goal check

The brief's success condition was:

> Mouse movement modifies TOTK's gameplay camera as displacement-based mouse input rather than
> velocity-based analog-stick input, while preserving TOTK's normal camera behavior.

The injection point satisfies this: a float pair is written once per camera update, so rotation
tracks mouse distance moved. No virtual stick, no deadzone, no stick-magnitude speed cap, no
acceleration curve — and the game's own lag/collision/limits remain.

## Remaining known work

| Item | State |
|---|---|
| Bow aiming | execute `0x023B2BAC`, vtable `0x0377EA80` slot `0x0377EB18`; reads the same `input+0x420/424`, so the same write technique should reach it |
| Scope | distinct camera `0x023B4AE0`, vtable `0x0377EBD8` slot `0x0377EC70` |
| Lock-on / Target / UltraHand | share execute `0x23CB374` — one hook covers all three |
| Horse | `0x23BE74C`, vtable `0x03780B78` slot `0x03780C10` |
| Other modes | full 42-mode table in `docs/DELEGATION_CAMERA_MODES.md` |
| Unbounded rotation | still limited by Eden clamping the cursor to the window; needs an Eden-side change (see `docs/STATUS.md`) |

## Note on the harness

The automated `re/run_eden_test.ps1` must not be used to validate mod behaviour: launching
`eden.exe -g <rom>` from a script resolves paths to `C:/Users/user/...`, which does not exist, so
**no mods load** and it silently tests a stock game. GUI launches only.
