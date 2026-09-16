# Install instructions — Eden (Windows)

Target: **Tears of the Kingdom 1.4.2**, title ID `0100F2C0115B6000`, running under Eden.
Design goal: install **alongside** the existing UltraCam / TOTK Optimizer setup without
disturbing it.

---

## 0. Before you start

You need a built module. See `docs/BUILD.md`. The build produces an NSO which you rename to
**`subsdk1`**.

If you have not built it yet, skip to §4 to confirm you can at least see whether the mod is
present — the rest of this document describes the final layout.

---

## 1. Why `subsdk1` and not `subsdk3`

Eden looks for these exefs members in a fixed order and loads every one it finds:

```
rtld, main, subsdk0, subsdk1, subsdk2, subsdk3, subsdk4, ... subsdk9, sdk
```

(`src/core/loader/deconstructed_rom_directory.cpp`; sparse Eden checkout at `re/eden_src/`).

UltraCam ships `!!!!TOTK Optimizer/exefs/subsdk3`. This mod therefore uses **`subsdk1`**,
which is free. Mod folders are applied in ascending name order and **the first match per
filename wins**, so as long as this mod never ships a file named `subsdk3` it cannot collide
with UltraCam regardless of folder naming.

---

## 2. Layout

Put the mod in its **own** folder. Do not merge it into `!!!!TOTK Optimizer`.

```
%APPDATA%\eden\load\0100F2C0115B6000\
    !!!!TOTK Optimizer\          <- leave exactly as it is (UltraCam)
        exefs\
            main.npdm
            subsdk3
    !!NativeMouse\               <- new
        exefs\
            subsdk1              <- the built NSO, renamed
            main.npdm            <- see below
```

`%APPDATA%` is normally `C:\Users\<you>\AppData\Roaming`. The Eden data directory can also be
a portable folder next to `eden.exe`; the `load\` tree lives wherever Eden's data directory is.

### `main.npdm`

Eden requires an ExeFS `main.npdm` to exist (`ErrorMissingNPDM` otherwise), but it only reads
the heap size out of it, and a module's own size comes from its NSO header. Two options:

* **Preferred** — copy the `main.npdm` from exlaunch's build output. It is generated from
  `mod/config/module.json`, which already carries TOTK's title id and the `hid` service the
  mod needs.
* Or reuse the game's own `main.npdm`. This also works, because Eden resolves `main.npdm` by
  layering and UltraCam's copy is already valid for this title.

Do **not** ship a second `main.npdm` with a different name — the filename is fixed.

---

## 3. Configuration

Copy the shipped config to Eden's SD-card root:

```
%APPDATA%\eden\sdmc\NativeMouse.ini
```

```ini
[NativeMouse]
Enabled      = true
SensitivityX = 1.0
SensitivityY = 1.0
InvertY      = false
AimMultiplier = 1.0
Smoothing    = 0.0
DebugLog     = false
```

`sdmc` maps to `%APPDATA%\eden\sdmc`, the same tree where UltraCam keeps
`UltraCam\TOTK\Config\maxlastbreath.ini`. The two configs are independent — nothing here reads
or writes UltraCam's file.

---

## 4. Eden settings that matter

These are the difference between the mod working and appearing to do nothing.

| Setting | Value | Why |
|---|---|---|
| `Controls.mouse_enabled` | **true** | this is the only source of real mouse data inside the guest |
| `Controls.mouse_panning` | **false** | mutually exclusive with the above — Eden forces this |
| `rstick` in your input profile | **remove `engine:mouse`** | otherwise the stick and the mod fight |

`mouse_enabled` lives in `%APPDATA%\eden\config\qt-config.ini` under `[Controls]`, and is also
reachable in the UI under input **advanced** settings ("Emulated mouse"). Note Eden's own UI
wording: *"Emulated mouse is enabled. This is incompatible with mouse panning."*

Edit the right-stick binding in your controller profile, e.g.
`%APPDATA%\eden\config\input\<...>.ini`:

```ini
; before
rstick="engine:mouse,range:1.000000,deadzone:0.000000,axis_x:0,axis_y:1,threshold:0.500000"
; after — keyboard/controller only, mouse removed
rstick=""
```

If you keep the mouse bound to `rstick` **and** the mod enabled, TOTK receives stick input
from two places at once and the camera will feel like it is fighting itself.

---

## 5. Confirming it loaded

Set `DebugLog = true` in `NativeMouse.ini`. The mod writes `sdmc:/NativeMouse.log`, i.e.
`%APPDATA%\eden\sdmc\NativeMouse.log`:

```
NativeMouse: starting
NativeMouse: unsupported game build - idle          <- build id check failed, nothing was hooked
NativeMouse: mouse activated
NativeMouse: no camera hook resolved (diagnostic mode)
```

Reading it:

| Line | Meaning |
|---|---|
| `unsupported game build - idle` | the loaded `main` is not build `5CB42B1CF25469FB`. Expected on any other game version. |
| `mouse activated` | `hid` cmd 21 (`ActivateMouse`) succeeded and Eden is now publishing real mouse data |
| `no camera hook resolved` | expected today: the stick-consumer offset is not filled in yet, so the mod samples but does not steer |
| `camera hook installed` | the camera path is live |

If the file does not appear at all, the module was not loaded — check the filename is exactly
`subsdk1` (lowercase, no extension) directly inside `exefs\`.

---

## 6. Compatibility notes

* **UltraCam / TOTK Optimizer** — unaffected. Different module slot, different config file.
  UltraCam's `[Gameplay] Stick_Horizontal_Speed` / `Stick_Vertical_Speed` multiply the same
  requested camera rotation the mod feeds, so they stack rather than conflict. Start both at
  `1.0` while testing.
* **TOTK Optimizer re-runs** — if you regenerate the mod through NX Optimizer, it rewrites
  `!!!!TOTK Optimizer\`. It does not touch `!!NativeMouse\`, so re-running it is safe.
* **Game updates** — the build-id check refuses to hook an unrecognised build, so an update
  degrades to "mod idle" rather than patching a function that moved. Re-derive the offset
  before expecting it to work again.
* **Other mouse-camera mods** — do not run this alongside another mod that patches the same
  camera/stick function; both would install a hook at the same address.

---

## 7. Uninstalling

Delete the `!!NativeMouse` folder. Optionally delete `sdmc:\NativeMouse.ini` and
`NativeMouse.log`. No other state is written, and nothing outside that folder is modified.
