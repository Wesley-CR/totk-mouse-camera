#!/usr/bin/env python3
"""Mark the camera-injection milestone in docs/WORKFLOW.md and docs/STATUS.md."""
import io
import os

base = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))

# ---------------------------------------------------------------- WORKFLOW.md
p = os.path.join(base, "docs", "WORKFLOW.md")
with io.open(p, encoding="utf-8") as f:
    w = f.read()

OLD = """## 10. IMMEDIATE NEXT STEP

Brief #5's answer is **`(this+0x20)+0x420` / `+0x424`** — the per-frame stick producer is the
function containing `0x886824`:"""

NEW = """## 10. CAMERA INJECTION — SOLVED (runtime-confirmed)

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

The per-frame stick producer for reference is the function containing `0x886824`:"""

assert OLD in w
w = w.replace(OLD, NEW, 1)
with io.open(p, "w", encoding="utf-8") as f:
    f.write(w)
print("WORKFLOW.md updated")

# ------------------------------------------------------------------ STATUS.md
p = os.path.join(base, "docs", "STATUS.md")
with io.open(p, encoding="utf-8") as f:
    s = f.read()

BANNER = """# Status — CAMERA INJECTION WORKS

## Headline

**The mod moves TOTK's camera through the game's own rotation pipeline.** Confirmed at runtime
(nm21): writing the float pair at `(this+0x20)+0x420` / `+0x424` from a hook on
`ExecutePlayerCameraChase` (image VA `0x1DB01C`) makes the camera rotate, while the stock
pipeline — lag, collision, pitch limits, mode handling — remains in control.

What remains is plumbing, not discovery: feed **mouse counts** instead of the fixed oscillating
test value, with HID brought up somewhere safe (never in `exl_main`, never on the camera thread).

See `docs/WORKFLOW.md` §10 for the confirmed mechanism and the delivery method that worked.

"""

if s.startswith("# Status — CAMERA INJECTION WORKS"):
    print("STATUS.md already updated")
else:
    # keep the rest of the document, just replace its title line
    lines = s.split("\n")
    lines[0] = BANNER.rstrip()
    s = "\n".join(lines)
    with io.open(p, "w", encoding="utf-8") as f:
        f.write(s)
    print("STATUS.md updated")
