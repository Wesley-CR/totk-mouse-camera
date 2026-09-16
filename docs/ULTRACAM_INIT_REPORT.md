# UltraCam 3.0.0 initialization: binary evidence

Analysis of the installed `subsdk3` identified in `DELEGATION_ULTRACAM_INIT.md`. Addresses below are **module-relative virtual addresses**, unless explicitly marked as TOTK-main offsets. This was static analysis; the installed mod, game and Eden configuration were not changed.

## Main finding

**UltraCam installs a trampoline on the symbol `nnMain` during module initialization. Its replacement at `0xEACF0` mounts the filesystem and reads configuration before transferring control to the original `nnMain`.** This is a startup interception point, not a wait-until-gameplay loop or a sleeping worker.

There is a crucial qualification: **UltraCam initializes keyboard, mouse and touchscreen, and calls `nn::oe::GetDisplayVersion`, during module init itself.** It calls imported Nintendo SDK functions, including `nn::hid::InitializeMouse`; that is a different runtime path from NativeMouse's libnx `hidInitialize()` / `hidInitializeMouse()`. This binary does not support the blanket diagnosis that service calls cannot run during module init. It does support moving filesystem/config setup behind a game entry hook. It does **not** establish the cause of NativeMouse's crashes or deadlocks.

## The previous analysis image is invalid after text

The input module's SHA-256 is `3273a1f6cf5af36a5135da74ced66e292e4655174807e883ab4408b73a636fb9` (1,251,511 bytes), matching the installed file and the brief.

`re/nsoimg.py`, `re/nso_unpack.py` and `re/kiplz4.py` read segment headers at 12-byte strides. Actual NSO segment fields begin at `0x10`, `0x20`, `0x30`; compressed sizes are at `0x60`, `0x64`, `0x68`. The NSO contains three separate LZ4 blocks. This matches both Eden's `NSOHeader` definition and the [NSO format reference](https://switchbrew.org/wiki/NSO0).

The existing `re/uc_img/image.bin` matches the correct text bytes through `0x168E9F`, but first diverges at `0x168EA0`. Its rodata/data layout and old string addresses must not be used, and a single constant adjustment does not repair them.

| Segment | NSO file offset | Compressed size | Memory VA | Decompressed size |
|---|---:|---:|---:|---:|
| text | `0x100` | `0xF720C` | `0` | `0x168EA0` |
| rodata | `0xF730C` | `0x362F1` | `0x169000` | `0x7CA10` |
| data | `0x12D5FD` | `0x42BA` | `0x1E6000` | `0xBA78` |

All three decompressed segment hashes match their NSO-header SHA-256 hashes. The corrected, gap-preserving image is [`image_mapped.bin`](../.agent-work/ultracam-init/entry/image_mapped.bin), size `0x1F1A78` (2,038,392 bytes), SHA-256 `cd9a407cb8ab48b943ef7576d57b365106d679cb35a72057c82a41bc2923d2b6`. BSS is not stored in this file. The NSO declares BSS size `0x4AE588`.

## Entry and immediate initialization order

| Stage | Evidence |
|---|---|
| NSO text entry stub | `0x0`; word at `0x4` points to MOD0 at `0x16A000`. This stub is not the module's `DT_INIT`. |
| Dynamic table | MOD0 + `0x87540` = `0x1F1540`; `DT_INIT = 0x830E0`. |
| exlaunch module initialization | `0x830E0`: initializes fake-heap bounds, calls `0x83040`, iterates preinit/init arrays, sets `x0=x1=0`, branches from `0x83188` to `0xEBF60`. |
| Constructors | `DT_INIT_ARRAY = 0x1F16B0`, size `0x250` = 74 entries. This includes constructing config-path strings, which is not file I/O. |
| `exl_main` equivalent | `0xEBF60`; alternate process-entry wrapper at `0x83190` also reaches it with forwarded arguments. |
| HID initialization | `0xEBF88` calls `0xE1460`: keyboard at `0xE1468`, mouse at `0xE146C`, touchscreen tail-call at `0xE1474`. |
| Hook/JIT initialization | `0xEBF8C` calls `0x79280`, which invokes `RwPages` setup `0x84320`, then inline-hook setup `0x7E000`. |
| Display version | `0xEBF98` calls imported `nn::oe::GetDisplayVersion` via PLT `0x168E40`, once in this entry body. No version-polling loop here. |
| Runtime symbol lookups | `malloc` and `free` at `0xEBFCC` and `0xEBFE0`; `nnMain` at `0xEC008`, through `nn::ro::LookupSymbol` PLT `0x168B60`. |
| First proven bootstrap trampoline | At `0xEC020`, calls hook installer `0x792B0` with target = resolved `nnMain`, replacement = `0xEACF0`, trampoline enabled. Stores original trampoline at `0x69D020`. |
| Remaining init work | Executes the fast-patch vector at `0x69D058` (`0xEC034..0xEC054`), installs the `nvnBootstrapLoader` hook through `0xEAB40`, then registers an ImGui draw callback through `0xEA8D0`. |

The wrapper matches the exlaunch Module lifecycle rather than replacing it. The local source reference is `re/exlaunch/source/lib/init/init.cpp`; function identities in the stripped binary are inferred from matching control flow and embedded diagnostics, while instruction addresses and imported symbol identities are directly observed.

The JIT constructor at `0x79540` stores code-region base `0x7A000`, size `0x4000`; the inline-hook constructor at `0x7E110` stores base `0x7F000`, size `0x4000`. Both are **16 KiB reserved code regions with writable aliases**, prepared synchronously at init. `0x84320` is identified by embedded `exl::util::RwPages` diagnostics; this is more precise than assuming a new executable heap allocation.

## Deferral and configuration

`0xEACF0`, the `nnMain` replacement, executes:

1. `0xE64D0` at call site `0xEAD00`: `nn::fs::MountSdCard("sd")` at `0xE64EC`, then `nn::fs::MountRom("content")` at `0xE64F8`; also checks/removes an existing `sd:/UltraCam/DEBUG.log` through file helpers.
2. `0xE6090` at `0xEAD04`: startup configuration loading and application.
3. `0xD6910` at `0xEAD08`, then registered startup callbacks from the vector at `0x69D028` (`0xEAD14..0xEAD34`).
4. Releases the callback-vector allocations, then tail-branches through the saved original `nnMain` trampoline at `0xEADA8..0xEADAC`.

The config constructor `0xE5FA0`, init-array entry 63 (zero-based), constructs `sd:/UltraCam/TOTK/Config/maxlastbreath.ini` at object `0x5F82C0` + `0x90`. String literals are at `0x19A1C8` (directory) and `0x19A1E8` (filename). It does not open the file.

Actual startup read chain:

```text
hooked nnMain 0xEACF0
  -> filesystem mounts 0xE64D0
  -> config startup 0xE6090
       -> config object accessor 0xE3D50
       -> parse/load 0xE3D60 (call site 0xE60A8)
            -> existence check 0xE0070
            -> whole-file read 0xE1300 (call site 0xE3DA0)
                 -> nn::fs::OpenFile (read mode 1) at 0xE1320
                 -> nn::fs::GetFileSize at 0xE1340
                 -> nn::fs::ReadFile at 0xE1368
                 -> nn::fs::CloseFile at 0xE1370
```

`0xE3D60` parses INI sections, keys and values. The binary also contains a reload-like path at `0xE6100`; its user/UI trigger is outside this initialization finding. The startup read above is proven and uses the SDK filesystem API, not `fopen("sdmc:/...")`.

## Threads, sleeps and readiness

There is a conditional exlaunch **process-handle acquisition helper**, not a proven delayed service worker. `0x83E00` first tries `svcGetInfo` with ID `0xFDE9`; on failure it calls `0x83CA0`. That creates a temporary helper thread at `0x83CF4`, starts it at `0x83D00`, exchanges a process handle over a session, waits for termination and closes handles. Its entry is `0x83C20`. This can occur during runtime/JIT setup. No assumption is made about which branch the user's Eden executable takes.

The only direct call to imported `nn::os::SleepThread` is at `0xAB6CC`, in code computing a delay from system ticks and a target frame rate. That supports frame pacing, not a startup back-off. No readiness polling loop or sleeping worker was found on the demonstrated initialization path. Static analysis cannot rule out every indirect call or SDK-internal thread.

## Gameplay hook installation

There are separate immediate and deferred callback vectors. Constructor `0xEC120` (init-array entry 67) calls `0xEADF0(&state_at_0x69D028)`. Its stores and GOT relocations recover which functions belong to which vector, even though the vectors themselves are allocated at runtime.

The immediate vector at state + `0x30` (`0x69D058`) starts with `0xCA740`, followed by `0x968C0`, `0xB4FA0` and `0x85640`. `exl_main` executes it after installing `nnMain`. The first proven hooks in this sequence are:

| Target resolved by symbol | Replacement VA | Installer call site | Timing |
|---|---:|---:|---|
| `nnMain` | `0xEACF0` | `0xEC020` | Module init |
| `nn::fs::OpenFile` | `0xC8690` | `0xCA780` | First fast-patch callback at module init |
| `nn::fs::MountSdCard` | `0xCA460` | `0xCA7BC` | Same callback |
| `nn::fs::MountRom` | `0xC60F0` | `0xCA7F0` | Same callback |
| `nvnBootstrapLoader` | `0xE71A0` | `0xEAB78` | Module init, after fast-patch callbacks |

Installing the filesystem hooks is distinct from opening a file. The first bootstrap hook is resolved by the literal `nnMain`; it does not require a hard-coded TOTK 1.4.2 RVA.

The deferred vector at `0x69D028`, consumed by the hooked `nnMain` after mounts/config, includes these gameplay installers:

| Feature | Installer VA | Record key / actual patch path | Replacement VA |
|---|---:|---|---:|
| Camera controller | `0xA6D80` | Looks up `CameraControlFix`, `CameraControl`, and `CameraFunc`; includes an instruction patch and a trampoline hook | Camera-function trampoline replacement `0xA65D0` |
| Layer calculation, 1.4.0+ | `0xB5140` | `LayerCalc140`; calls inline-hook installer `0x7E020` | `0xB5080` |
| Game time speed, 1.4.0+ | `0xABB60` | `GamespeedNEW`; calls trampoline installer `0x792B0` | `0xABA90` |

Their deferred-vector pointers come from GOT entries `0x1F13F8`, `0x1F1118` and `0x1F0400`, respectively; `0xEADF0` appends them to the vector at state + `0`, not the immediate vector at state + `0x30`.

`0xE6530` classifies cached display-version text: `1.0.0` → 0; `1.1.0/1.1.1/1.1.2/1.2.0/1.2.1` → 1; otherwise → 2. Thus the literal `1.4.2` selects bucket 2, which chooses `LayerCalc140` and `GamespeedNEW`. The camera path has its own record/availability tests. Ghidra's uncorrected prototype prints the classifier result as a caller parameter; the assembly branches on `w0` immediately after the classifier call.

The three descriptive strings from the brief are record-construction labels: `LayerCalc Hook 1.4.0+` at `0x190F20` (code reference `0xBDD84`), `Camera Controller Hook Fix 1.4.0+` at `0x191138` (`0xBE2DC`), and `Game Time Speed 1.4.0+` at `0x1918B8` (`0xBEDA8`). They are not installer call sites.

**Not recovered: the final TOTK-main RVAs selected for these three features on 1.4.2.** `0x84E70` looks up a named record in a map; `0xC58E0`/`0xC56D0` select version/default values, with AOB/AOBSDK fallback paths. The installers add a nonzero resolved offset to the discovered main-module base. Recovering the final values requires tracing those records and, where used, their pattern matches. The addresses in the table identify UltraCam's actual installers and replacements, not guessed game offsets. No proposed camera target should be taken from the old image.

## NPDM and installation comparison

Full dumps, service lists, capability decoding and source locations are in [`NPDM findings`](../.agent-work/ultracam-init/npdm/findings.md) and the adjacent dump artifacts.

| Item | Installed UltraCam | `build-nativeMouse/deploy/main.npdm` |
|---|---|---|
| Size | 1,580 bytes | 1,508 bytes |
| SHA-256 | `d593a80e5e53f8603afab0655af31bac0aa37ebf8ab8e205017e38b581d9a006` | `6d851e043cb9a10e114053a5133dc18116b6a958d9c21ca95a52b3639ff3904c` |
| Filesystem permissions, ACI and ACID | `0xFFFFFFFFFFFFFFFF` | `0x4000000000000000` |
| `hid`, `fsp-srv` service access | Present | Present |
| Additional UltraCam services | `ectx:aw`, `mnpp:app`, `ngct:u`, `notif:a` | Absent |
| KAC descriptors | 11 | 9 |
| Allowed SVC IDs | `0–55, 57–58, 60–61, 64–109, 111–127` | `1–41, 44–45, 47, 50–54, 95` |

NativeMouse grants the basic thread, synchronization, named-port and IPC SVCs, but **does not grant all SVCs used by UltraCam's hook runtime**. In particular:

- `RwPages` setup `0x84320`, reached during module init, uses `svcMapProcessMemory` (`0x74`, raw stub `0x24C`); the paired teardown uses `svcUnmapProcessMemory` (`0x75`, stub `0x254`). UltraCam allows both; NativeMouse omits both. Permission for `svcMapMemory` (`0x04`) does not cover this different mechanism.
- The process-handle fallback also needs `svcCreateSession` (`0x40`) and `svcReplyAndReceive` (`0x43`), both allowed by UltraCam and omitted by NativeMouse.
- NativeMouse's filesystem mask is only bit 62 (named `Bit62` in this Eden source). It omits the `SdCard` permission, bit 21; UltraCam includes it. Listing `fsp-srv` in SAC is not equivalent to granting SD-card filesystem access.

These are concrete permission differences relevant to reproducing UltraCam's initialization. Eden's inspected source parses the permissions, but searches did not find SAC/FAC enforcement in service lookup/filesystem dispatch or a caller of the SVC-permission accessors on the execution path. This does **not** prove the behavior of the separately installed emulator executable. If those permissions are enforced, NativeMouse's NPDM is insufficient for these UltraCam runtime paths; the static comparison alone does not establish permission denial as the cause of the observed failures.

Eden loads one patched process `main.npdm`; it does not merge a separate permission set for every `subsdk`. The installed UltraCam ExeFS directory contains only `subsdk3` and `main.npdm`, with no replacement `rtld` or `sdk`. The mod imports the game's SDK and uses external config/content paths; the directory inventory alone does not prove that every optional UltraCam feature works without its other assets.

## Limits and practical implication

The concrete pattern to investigate for NativeMouse is: normal exlaunch module setup → resolve/hook `nnMain` → perform filesystem/config setup there → continue the original entry. UltraCam also reuses the game's `nn::hid` API directly at init. Reproducing this requires accounting for the process-memory mapping, fallback-session and SD-card permission differences above, not simply moving existing code. Neither switching APIs nor reproducing the hook has been implemented or runtime-tested here.

Unresolved: the cause of NativeMouse's register-dump crash, its libnx/TLS/service-runtime compatibility, the effective NPDM selected in a specific launch, and the exact behavior of the installed Eden executable. No evidence here justifies adding an arbitrary multi-second sleep or calling the previous diagnostic's timing explanation proven.

## Reproduction artifacts

Everything created for the investigation is under `.agent-work/ultracam-init/`; the final report is this file. Useful artifacts:

- `entry/unpack_nso.py`: three LZ4 blocks, verified against all embedded segment hashes; writes packed and VA-mapped images. **Use `image_mapped.bin`.**
- `verify_evidence.py`, `verification.txt`: runnable recheck of source identity, all three segment hashes, mapped layout, `DT_INIT`, constructor entries and 28 decisive branch/call targets; passed.
- `entry/metadata.txt`, `entry/dynamic.txt`, `entry/relocs.tsv`: header, dynamic-table and symbol/relocation evidence.
- `disasm.py`, `plt.tsv`: objdump helper with imported-PLT annotations.
- `scripts/prepare.py`, `scripts/export_all.py`, `run-ghidra.ps1`: corrected Ghidra import, relocation application, symbol/PLT labeling and decompilation.
- `decomp-correct/`: decompiled functions with caller/reference addresses. Unknown function prototypes and BSS reads can mislead decompilation; assembly and relocations take precedence.
- `npdm/`: comparison script, both decoded NPDMs, source evidence and findings.

The pre-existing `re/ghidra_proj/UltraCam` project and old image were left intact. A failed early import used the new packed image; its `.agent-work/.../decomp` output is not evidence. Only the `Corrected` project and `decomp-correct` output refer to the VA-mapped image.
