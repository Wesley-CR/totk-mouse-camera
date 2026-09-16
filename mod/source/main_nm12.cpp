// NativeMouse — DIAGNOSTIC BUILD 12 (tag "nm12").
//
// ARCHITECTURE CHANGE, based on the UltraCam initialization report
// (docs/ULTRACAM_INIT_REPORT.md).
//
// What UltraCam actually does
// ---------------------------
// It does NOT do work during module-init and it does NOT sleep/retry.  It:
//
//   1. resolves `nnMain` through nn::ro::LookupSymbol          (by NAME, not RVA)
//   2. installs a trampoline on it                             (during module init)
//   3. inside the replacement: mount filesystem, read config,
//      run deferred callbacks
//   4. tail-calls the original nnMain
//
// Step 2 is cheap and safe; steps 3-4 happen when the game calls nnMain, i.e.
// from the game's own thread, with services available.  That is why UltraCam can
// mount the SD card and open files while we kept deadlocking.
//
// This build reproduces that shape with one question: does hooking nnMain work,
// and is the filesystem usable inside it?
//
// Note it also fixes the NPDM: our generated main.npdm was missing
// svcMapProcessMemory / svcUnmapProcessMemory (needed by exlaunch's hook
// runtime) and the SD-card filesystem permission bit.  Both are now granted,
// matching UltraCam's capability set.

#include <switch.h>
#include <lib.hpp>

namespace nn::ro {
    // Resolve a symbol by name from any loaded module. Returns 0 if not found.
    Result LookupSymbol(uintptr_t* out, const char* name);
}

namespace {

    using NnMainFn = void (*)();

    NnMainFn g_OrigNnMain = nullptr;
    bool     g_RanDeferred = false;

    // Performs the work that must not happen during module-init. Runs from
    // inside the hooked nnMain, on the game's thread, once the game is up.
    void RunDeferredOnce() {
        if (g_RanDeferred) {
            return;
        }
        g_RanDeferred = true;

        Logging.Log("NativeMouse: nm12 nnMain reached - starting deferred init");

        // 1. Filesystem. sdmc: was NOT mounted during module-init.
        // libnx's helper is fsdevMountSdmc() (returns a Result); it maps "sdmc:".
        const Result rcSd = fsdevMountSdmc();
        Logging.Log("NativeMouse: nm12 fsdevMountSdmc rc=0x%X", rcSd);

        std::FILE* f = std::fopen("sdmc:/NativeMouse.log", "w");
        if (f == nullptr) {
            Logging.Log("NativeMouse: nm12 fopen(sdmc:/NativeMouse.log) FAILED");
        } else {
            std::fputs("NativeMouse: nm12 deferred init reached nnMain\n", f);
            std::fclose(f);
            Logging.Log("NativeMouse: nm12 fopen OK - wrote sdmc:/NativeMouse.log");
        }

        // 2. HID. Safe here because services are up.
        const Result rcHid = hidInitialize();
        Logging.Log("NativeMouse: nm12 hidInitialize rc=0x%X", rcHid);
        if (R_SUCCEEDED(rcHid)) {
            hidInitializeMouse();
            HidMouseState st{};
            const size_t got = hidGetMouseStates(&st, 1);
            Logging.Log("NativeMouse: nm12 mouse count=%d connected=%d delta=%d,%d",
                        static_cast<int>(got),
                        static_cast<int>((st.attributes & HidMouseAttribute_IsConnected) != 0),
                        static_cast<int>(st.delta_x), static_cast<int>(st.delta_y));
        }
    }

    void NnMainHook() {
        RunDeferredOnce();
        if (g_OrigNnMain != nullptr) {
            g_OrigNnMain();
        }
    }

}

extern "C" void exl_main(void* x0, void* x1) {
    (void)x0;
    (void)x1;

    Logging.Log("NativeMouse: nm12 exl_main entered");

    // exlaunch must prepare its hook pool before we can install anything.
    exl::hook::Initialize();
    Logging.Log("NativeMouse: nm12 hook::Initialize done");

    uintptr_t nnMain = 0;
    const Result rc = nn::ro::LookupSymbol(&nnMain, "nnMain");
    Logging.Log("NativeMouse: nm12 LookupSymbol(nnMain) rc=0x%X addr=0x%llX",
                rc, static_cast<unsigned long long>(nnMain));

    if (nnMain == 0) {
        Logging.Log("NativeMouse: nm12 no nnMain - giving up (game still boots)");
        return;
    }

    // Minimal, safe amount of work at module-init: one trampoline install.
    // Everything real happens inside the hook.
    g_OrigNnMain = reinterpret_cast<NnMainFn>(
        exl::hook::Hook(nnMain, reinterpret_cast<uintptr_t>(&NnMainHook), true));

    Logging.Log("NativeMouse: nm12 nnMain hook installed, orig=%p",
                reinterpret_cast<void*>(g_OrigNnMain));
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
