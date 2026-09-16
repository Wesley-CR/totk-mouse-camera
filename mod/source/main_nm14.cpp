// NativeMouse — BUILD 14 (tag "nm14").
//
// Two things are now established:
//
//   1. The game's main module base is resolved and `nn::ro::LookupSymbol`
//      works: nm13 resolved "nnMain" -> 0x80DDD1E0 with the module occupying
//      0x80224000-0x83cf1000.  Symbol/address work is therefore viable.
//   2. exl::hook::Initialize() and symbol lookup both run safely at module-init
//      (nm13 booted).  Only the trampoline INSTALL was untested.
//
// The camera target
// -----------------
// `PlayerCameraBase`'s per-frame update is at image VA 0x023BA728.  It is reached
// through vtable slot 14 of the table at image VA 0x0377FF80.  The vtable holds
// RELOCATED absolute pointers, so it cannot be found by a static byte pattern at
// runtime - but the function's own instructions carry no relocations in their
// first bytes, so they are a stable signature.
//
// So this build:
//   * takes the first 32 bytes of the function's code as a signature
//   * searches the main module's text range for it at runtime
//   * reports the matched address
//   * installs a trampoline hook there and counts calls
//
// It does not modify camera state - it only observes.  If this boots and the call
// count rises while in gameplay, the last unknown (trampoline install + reaching
// camera code) is solved.

#include <switch.h>
#include <lib.hpp>

#include <lib/util/sys/mem_layout.hpp>
#include <lib/util/sys/modules.hpp>

namespace {

    using CameraUpdateFn = void (*)(void* self, void* arg);
    CameraUpdateFn g_Orig = nullptr;

    // Not volatile: C++20 deprecates ++ on volatile-qualified types, and this is
    // only read by our own hooks on the game thread.
    u64 g_Calls = 0;

    void CameraUpdateHook(void* self, void* arg) {
        g_Calls++;
        if (g_Calls == 1 || (g_Calls % 300) == 0) {
            Logging.Log("NativeMouse: nm14 camera call #%llu self=%p",
                        static_cast<unsigned long long>(g_Calls), self);
        }
        if (g_Orig != nullptr) {
            g_Orig(self, arg);
        }
    }

    // Search a byte range for a pattern, returning the first match or 0.
    uintptr_t FindPattern(uintptr_t start, uintptr_t end, const u8* pat, size_t len) {
        if (end <= start || len == 0 || (end - start) < len) {
            return 0;
        }
        const u8* p = reinterpret_cast<const u8*>(start);
        const u8* last = reinterpret_cast<const u8*>(end - len);
        for (; p <= last; p++) {
            size_t i = 0;
            for (; i < len; i++) {
                if (p[i] != pat[i]) {
                    break;
                }
            }
            if (i == len) {
                return reinterpret_cast<uintptr_t>(p);
            }
        }
        return 0;
    }

}

extern "C" void exl_main(void* x0, void* x1) {
    (void)x0;
    (void)x1;

    Logging.Log("NativeMouse: nm14 STEP1 entered");
    exl::hook::Initialize();

    // First 64 bytes of the camera update function, read from the decompressed
    // TOTK 1.4.2 image at VA 0x023BA728.  These are plain instructions with no
    // relocations, so they survive the module being loaded at any base.
    //
    // Length matters: the first 32 bytes match 237 other functions (a common
    // register-save prologue), the first 48 match 2, and 64 bytes match exactly
    // one location in the whole .text - this function.
    static const u8 kCameraSig[64] = {
        0xEF, 0x3B, 0xB6, 0x6D, 0xED, 0x33, 0x01, 0x6D,
        0xEB, 0x2B, 0x02, 0x6D, 0xE9, 0x23, 0x03, 0x6D,
        0xFD, 0x7B, 0x04, 0xA9, 0xFD, 0x03, 0x01, 0x91,
        0xFC, 0x6F, 0x05, 0xA9, 0xFA, 0x67, 0x06, 0xA9,
        0xF8, 0x5F, 0x07, 0xA9, 0xF6, 0x57, 0x08, 0xA9,
        0xF4, 0x4F, 0x09, 0xA9, 0xFF, 0x83, 0x11, 0xD1,
        0x1A, 0x10, 0x40, 0xF9, 0xF4, 0x03, 0x01, 0xAA,
        0xF3, 0x03, 0x00, 0xAA, 0xF7, 0x33, 0x0E, 0x91,
    };

    const auto& main = exl::util::GetModuleInfo(exl::util::ModuleIndex::Main);
    const uintptr_t textStart = main.m_Text.m_Start;
    const uintptr_t textEnd   = textStart + main.m_Text.m_Size;

    Logging.Log("NativeMouse: nm14 STEP2 main text 0x%llX-0x%llX",
                static_cast<unsigned long long>(textStart),
                static_cast<unsigned long long>(textEnd));

    const uintptr_t found = FindPattern(textStart, textEnd, kCameraSig, sizeof(kCameraSig));
    Logging.Log("NativeMouse: nm14 STEP3 camera signature match = 0x%llX",
                static_cast<unsigned long long>(found));

    if (found == 0) {
        Logging.Log("NativeMouse: nm14 no match - not hooking");
        return;
    }

    g_Orig = reinterpret_cast<CameraUpdateFn>(
        exl::hook::Hook(found, reinterpret_cast<uintptr_t>(&CameraUpdateHook), true));
    Logging.Log("NativeMouse: nm14 STEP4 hook installed, orig=%p",
                reinterpret_cast<void*>(g_Orig));
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
