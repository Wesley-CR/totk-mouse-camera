# TOTK 1.4.2 Native Mouse Camera — Mod

A **Tears of the Kingdom 1.4.2** exefs mod that drives the normal gameplay camera from
**mouse displacement** instead of an emulated analog stick, running under **Eden** on Windows.

```
Eden native mouse  ->  raw mouse dx/dy  ->  TOTK gameplay camera rotation
```

No virtual right stick, no deadzone, no stick-velocity cap, no acceleration.

> **Status: foundation complete, camera hook pending.** See
> [`docs/STATUS.md`](docs/STATUS.md) for exactly what works, what does not, and the one
> remaining blocker. Reverse-engineering detail is in [`docs/research.md`](docs/research.md).

---

## Why this is a mod and not an Eden setting

Eden can already turn the mouse into a virtual right stick
(`rstick="engine:mouse,..."`). That path is *velocity* based: the mouse deflects an analog
stick, and TOTK rotates the camera at a rate proportional to stick deflection, passing
through a deadzone, a range clamp and (in panning mode) a decay filter.

This mod instead makes rotation proportional to **how far the mouse moved**:

```
rotation_angle = mouse_counts * sensitivity
```

so a fast flick and a slow drag of the same physical distance produce the same rotation.

---

## The two halves of the problem

**1. A mouse source inside TOTK.** TOTK cannot see the host mouse by itself. Eden
implements an emulated **HID mouse** (`Controls.mouse_enabled`) which publishes real host
mouse position, deltas, buttons and wheel into the guest's HID shared memory — but only if
the guest first calls `hid` command **21 (`ActivateMouse`)**. No retail game does, so the mod
must. Verified in Eden's source; see `docs/research.md` §2.6.

**2. A place to put the rotation.** TOTK's camera is AI-driven (`CameraChase`,
`CameraAiming`, `CameraLockOn`, …) and its right-stick-to-rotation step is parameterised by
`LatStickScale`/`LngStickScale` (+ `*InAir`) and `horizontalSensitivity`/`verticalSensitivity`.
The mod should feed a synthetic right-stick value derived from mouse displacement *before*
those multipliers, so TOTK's own camera lag, collision, pitch limits, lock-on, and every
camera state keep working.

---

## Repository layout

```
docs/
  research.md      full reverse-engineering notes, every finding marked with confidence
  STATUS.md        what works / what does not / the remaining blocker
  INSTALL.md       Eden install + UltraCam compatibility
mod/
  config/          NativeMouse.ini (shipped defaults)
  source/          mod sources + superseded experiment variants (main_nm*.cpp, main_ri*.cpp)
keybinds/          Eden input profiles (G502 mouse, keyboard + mouse)
build-nativeMouse/ devkitA64 build wrapper that produces the `subsdk1` NSO
re/                reverse-engineering workspace: scripts, dumps, Eden sparse checkout (gitignored)
```

---

## Quick start (do not run yet — see STATUS)

1. Extract TOTK 1.4.2's `main` and confirm the build ID is `5CB42B1CF25469FB`
   (`re/` has the scripts; `docs/research.md` §1 documents the exact commands).
2. Configure Eden:
   * `Controls.mouse_enabled = true` (required — this is the mouse source)
   * `Controls.mouse_panning = false` (mutually exclusive with the above)
   * Keep the mouse **out of** the `rstick` binding, or the stick will fight the mod.
3. Install `mod/` as its **own** mod folder with a free `subsdk` slot — never replace
   `subsdk3`, which UltraCam owns. See `docs/INSTALL.md`.

---

## Compatibility with UltraCam / TOTK Optimizer

The mod is designed to sit alongside UltraCam:

* UltraCam occupies `exefs/subsdk3` inside `!!!!TOTK Optimizer`. Eden loads every
  `subsdk0`…`subsdk9` it finds, and **first match wins per filename across mod folders**
  (folders sorted ascending, so `!!!!TOTK Optimizer` wins any collision).
* This mod therefore ships `subsdk1` (free) in its own folder.
* UltraCam's `Gameplay.Stick_Horizontal_Speed` / `Stick_Vertical_Speed` multipliers stack
  naturally with mouse input, because both act on the requested camera rotation.

---

## License / provenance

The mod is licensed under the **GNU General Public License v2.0** (see [`LICENSE`](LICENSE)).
`build-nativeMouse/` is the vendored [exlaunch](https://github.com/shadowninja108/exlaunch)
framework (GPLv2), with oss-rtld components under ISC terms
(`build-nativeMouse/source/rtld/LICENSE.txt`).

Reverse-engineering notes were produced against the user's own installed game and mod files.
Eden and UltraCam sources were read for interoperability only; game dumps, keys and UltraCam
binaries are not committed to this repository. `re/eden_src/` is a read-only sparse checkout
of Eden used to confirm input behaviour and is not part of the mod.
