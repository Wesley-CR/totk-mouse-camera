# Delegation brief — how does UltraCam initialise inside TOTK?

## Context

We are building a TOTK 1.4.2 native-mouse camera mod as an exefs `subsdk` module (exlaunch-based),
running under the **Eden** Switch emulator on Windows. Target title ID `0100F2C0115B6000`,
game version 1.4.2 (build `5CB42B1CF25469FB`).

**The problem in one line:** our module loads and exlaunch initialises, but any work attempted
during module-init either deadlocks the game or crashes it.

Observed failures:

| What we did in `exl_main` | Result |
|---|---|
| `hidInitializeMouse()` (HID IPC) | game wedges on the "launching" screen, CPU idle |
| `hidInitialize()` (HID setup) | same |
| `fopen("sdmc:/NativeMouse.log", "w")` | same |
| `threadCreate` + `threadStart` (worker, then return) | boots once, crashed on a later build |
| nothing but `Logging.Log(...)` then return | boots |

Eden's debug log ends with a **full register dump** on the crashing runs, so at least one of
these is an outright crash, not a hang.

**The key asset:** a working exlaunch mod is already installed on this machine. If it can do
all of the above successfully, the question is *how*, and the answer is recoverable from the
binary.

---

## The artifact

```
%USERPROFILE%\AppData\Roaming\eden\load\0100F2C0115B6000\!!!!TOTK Optimizer\exefs\subsdk3
```

* Size 1,251,511 bytes, SHA-256 `3273A1F6CF5AF36A5135DA74CED66E292E4655174807E883AB4408B73A636FB9`
* "UltraCam Version 3.0.0" by MaxLastBreath (NX Optimizer / TOTK Optimizer distribution)
* Magic `NSO0`; **per-segment LZ4-block compressed**, header flag bit 0
* exlaunch-based: contains `exl::hook::nx64::Hook`, `nn::ro::LookupSymbol`, C++ with Dear ImGui, NVN rendering
* A decompressed copy already exists at `<repo-root>\re\uc_img\image.bin`
  (2,026,449 bytes; layout: text at 0, rodata at 0x168EA0, MOD0 at 0x168EC6)

Useful scripts already in `<repo-root>\re\`:
`nsoimg.py` (unpack the NSO), `strings_va.py` (strings with true VAs), `uc_refs.py` and
`a64xref.py` (ARM64 xref scanners), `vtables.py` (resolve the `adrp/add/ret` name-getter idiom).
A Ghidra project exists at `re\ghidra_proj\UltraCam` (already analysed).

---

## The questions that matter

Answer these concretely, with addresses and evidence. Where you cannot determine something,
say so explicitly rather than guessing — a wrong answer here costs a lot.

1. **What is UltraCam's module entry path?** exlaunch for `LOAD_KIND=Module` wires `DT_INIT` to
   `exl_module_init()` → `exl_init()` → `__init_array()` → `exl_main(NULL, NULL)`. Find
   UltraCam's equivalent and confirm whether it follows that path or replaces it. Locate
   `exl_main`'s body.

2. **What does UltraCam do in its first moments, and in what order?** Specifically:
   * Does it call `hidInitialize()`, `hidInitializeMouse()`, `nn::hid::InitializeMouse`, or
     `nn::oe::*`, or open any file, **during** init?
   * Does it spawn threads? If so, when — during init or later?
   * Does it allocate a JIT region (`exl::hook::nx64::Initialize` / `JitSize`) during init?

3. **How does it defer work?** This is the crux. Look for:
   * a worker thread created at init, with a back-off/sleep before touching services
   * work performed from inside a **hook** on a game function (i.e. lazily, on the game's own
     thread, once the game is running)
   * a "wait for game to be ready" loop, e.g. polling a game global, waiting on
     `nn::oe::GetDisplayVersion`, or waiting for a specific system state
   * hooking a main-loop function purely to get a safe per-frame tick

   If any of these exist, describe the mechanism precisely. A hook used as a safe tick would
   explain everything and is the most likely answer.

4. **What are its first hooks, and are they installed at init or deferred?** It contains the
   strings `Camera Controller Hook Fix 1.4.0+`, `LayerCalc Hook 1.4.0+`, `Game Time Speed 1.4.0+`
   and a table of game-version-gated offsets. Find where hooks are actually installed.

5. **How does it read its config** (`sd:/UltraCam/TOTK/Config/maxlastbreath.ini`)? At init, or
   later? If later, what triggers it?

6. **Installation mechanics.** Does it require anything beyond `exefs/subsdk3` + `main.npdm`?
   Compare its `main.npdm` (1,580 bytes) with a minimal exlaunch-generated one (1,508 bytes):
   are there service or kernel-capability entries that a module needs in order to open files or
   call `hid`? This could be the whole story — our generated NPDM may simply not grant what
   we need, and a denied service call could hang.

---

## What would be most valuable to return

1. **The deferral mechanism, stated plainly.** "UltraCam installs hook X at offset Y during
   init and does all service work from inside it" is worth more than any amount of detail.
2. **Whether its NPDM grants services ours does not** — dump both and diff them.
3. **Concrete addresses** in the decompressed image for: entry point, any thread creation, any
   first hook installation.
4. A clear statement of which of the above you could **not** determine.

## Constraints

* Do not modify anything outside `<repo-root>\` — the
  installed game, mod, and Eden config must be left untouched (read-only inspection is fine).
* The mod is closed source; this is legitimate interoperability analysis of a binary the user
  already owns and runs.
