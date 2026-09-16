# Build instructions — NativeMouse

## What you need

| Tool | Why | Notes |
|---|---|---|
| **devkitPro** with the `switch-dev` group | provides `devkitA64` (`aarch64-none-elf-gcc`) and libnx | the mod is a Switch NSO module |
| **exlaunch** | hooking framework (`exl::hook::nx64`) | already cloned at `re/exlaunch` |
| **Python 3** | exlaunch's build scripts (npdm generation) | any 3.4+ |
| **GNU make** | build driver | comes with devkitPro's msys2 |

Eden's own `nstool.exe` (repo root) is only needed if you want to re-extract the game; it is
not part of the mod build.

## Installing devkitPro (Windows)

1. Download the graphical installer from <https://github.com/devkitPro/installer/releases>
   and run it.
2. When asked which components to install, select **Switch Development** (`switch-dev`).
   That group pulls in devkitA64, libnx and the msys2 shell.
3. Default install location is `C:\devkitPro`. If you use another path, note it.
4. Verify from the **MSYS2** shell that ships with devkitPro:

   ```bash
   aarch64-none-elf-gcc --version
   echo $DEVKITPRO      # should print /opt/devkitpro
   ```

   `DEVKITPRO` must be set (exlaunch's `common.mk` errors out without it).

## One-time layout

exlaunch expects to *be* the project: its `Makefile` compiles `$(TOPDIR)/source`, and the
output binary is named after the directory. The supported way to use it is to copy it and
drop your sources in.

```bash
# from the repo root, in the devkitPro MSYS2 shell
cp -r re/exlaunch build-nativeMouse
cd build-nativeMouse

# our module sources replace the exlaunch sample program
rm -rf source/program source/lib/init/../../program 2>/dev/null
cp -r ../mod/source/nativemouse   source/
cp    ../mod/source/main.cpp      source/program/main.cpp

# our build settings
cp ../mod/config.mk          config.mk
cp ../mod/config/module.json module.json
```

`mod/config.mk` already sets:

```
LOAD_KIND  := Module
PROGRAM_ID := 0100F2C0115B6000     # TOTK
NPDM_JSON  := module.json
```

`module.json` is exlaunch's `application.json` with `title_id` set to TOTK and the `hid`
service present (required — the mod calls `hidInitializeMouse` / `hidGetMouseStates`).

## Shipping it as `subsdk1`, not `subsdk9`

exlaunch hardcodes `BINARY_NAME := subsdk9` for `LOAD_KIND=Module` (see
`misc/mk/…`/`Makefile` line ~21). `subsdk9` would also work, but this mod targets
**`subsdk1`** to keep slots tidy and well away from UltraCam's `subsdk3`.

After building, simply rename the output:

```bash
make
mv NativeMouse.nso subsdk1
```

## Building

```bash
cd build-nativeMouse
export DEVKITPRO=/opt/devkitpro      # or C:/devkitPro
make
```

The result is `<dir>.nso` plus `<dir>.npdm`. Rename the `.nso` to `subsdk1`.

## Installing (Eden)

Create a **new** mod folder next to UltraCam's — do not merge into `!!!!TOTK Optimizer`:

```
%APPDATA%\eden\load\0100F2C0115B6000\!!NativeMouse\
    exefs\
        subsdk1        <- the renamed .nso
        main.npdm      <- from exefs\main.npdm of the game, or exlaunch's generated one
```

Eden searches `rtld`, `main`, `subsdk0`…`subsdk9`, `sdk` and loads every one it finds, so
`subsdk1` loads alongside UltraCam's `subsdk3`. Mod folders are applied in ascending name
order with **first match winning per filename**; because this mod ships only `subsdk1` it can
never collide with UltraCam's `subsdk3`.

Also copy the config:

```
%APPDATA%\eden\sdmc\NativeMouse.ini
```

## Eden settings that must match

```
Controls.mouse_enabled = true     # provides the mouse; required
Controls.mouse_panning = false    # mutually exclusive with the above
```

Keep the **mouse out of the `rstick` binding** in your input profile, otherwise the stick and
the mod will fight:

```ini
; config\input\<profile>.ini  — remove the engine:mouse from rstick
rstick=""
```

## Verifying the build loaded

Set `DebugLog = true` in `NativeMouse.ini`; the mod writes `sdmc:/NativeMouse.log`:

```
NativeMouse: starting
NativeMouse: mouse activated
NativeMouse: camera hook installed at main+0x...
```

If you see `mouse activated` the guest-visible mouse is up. If you then see the hook line,
the camera path is live.

## Troubleshooting

* **`Please set DEVKITPRO`** — export `DEVKITPRO` (MSYS2 path `/opt/devkitpro`).
* **`exlaunch not found`** — clone it: `git clone --depth 1 https://github.com/shadowninja108/exlaunch re/exlaunch`.
* **No `NativeMouse.log`** — the mod did not load: check the `subsdk1` filename casing and
  that it sits directly under `exefs\`.
* **`mouse activated` but no camera movement** — expected until the camera hook offset is
  filled in; see `docs/STATUS.md`.
