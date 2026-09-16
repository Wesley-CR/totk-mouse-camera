// NativeMouse — DIAGNOSTIC BUILD 13 (tag "nm13").
//
// nm12 introduced the UltraCam architecture (resolve nnMain by symbol, hook it,
// do deferred work inside the hook).  The game stopped booting.
//
// This build tests the SAME pieces but INSTALLS NO HOOK AT ALL, so it can only
// report - never patch.  That separates three suspects that nm12 bundled
// together:
//
//   (a) exl::hook::Initialize()  - allocates the JIT/RwPages hook pool, which
//       needs svcMapProcessMemory (absent from our original NPDM)
//   (b) nn::ro::LookupSymbol("nnMain") - resolves the symbol
//   (c) the trampoline install itself
//
// If this boots, the crash is in (c).  If it does not boot, it is (a) or (b).
// Nothing here writes to game code.

#include <switch.h>
#include <lib.hpp>

namespace nn::ro {
    Result LookupSymbol(uintptr_t* out, const char* name);
}

extern "C" void exl_main(void* x0, void* x1) {
    (void)x0;
    (void)x1;

    Logging.Log("NativeMouse: nm13 STEP1 entered");

    // (a) hook pool allocation
    exl::hook::Initialize();
    Logging.Log("NativeMouse: nm13 STEP2 exl::hook::Initialize returned");

    // (b) symbol resolution - read only, no patching
    uintptr_t nnMain = 0;
    const Result rc = nn::ro::LookupSymbol(&nnMain, "nnMain");
    Logging.Log("NativeMouse: nm13 STEP3 LookupSymbol(nnMain) rc=0x%X addr=0x%llX",
                rc, static_cast<unsigned long long>(nnMain));

    // Camera symbol, for future use - also read only.  If this resolves we can
    // locate the camera by name instead of a hardcoded RVA.
    uintptr_t dummy = 0;
    const Result rc2 = nn::ro::LookupSymbol(&dummy, "_ZN2nn2oe4nnMainEv");
    Logging.Log("NativeMouse: nm13 STEP4 LookupSymbol(mangled nnMain) rc=0x%X addr=0x%llX",
                rc2, static_cast<unsigned long long>(dummy));

    Logging.Log("NativeMouse: nm13 STEP5 done - NO hook installed, returning");
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
