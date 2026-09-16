// NativeMouse — BUILD 16 (tag "nm16").
//
// THE ACTUAL CAMERA HOOK.
//
// From the camera-function investigation (docs/DELEGATION_CAMERA_FUNCTION.md):
//
//   * `ExecutePlayerCameraChase` execute is at image VA **0x001DB01C**, reached
//     through vtable 0x0377EFE0 slot 19 (slot address 0x0377F078).
//   * My two previous targets (0x023BA670 / 0x023BA728) are REAL code but belong
//     to the `ExecutePlayerCameraEventTalk` vtable (0x0377FF58) - i.e. the Talk
//     event camera.  They are only dispatched in talk events, which is why they
//     never fired during normal exploration.  Not dead code: wrong mode.
//   * My FP-density ranking missed Chase entirely because it only scanned
//     0x23B0000-0x2400000; Chase execute lives at 0x1DB01C.
//
// Why a stub can trap you: Chase slot 20 (0x0377F080) holds 0x023B77D4, a lone
// `ret`; the real body is 4 bytes later at 0x023B77D8, reached only by a direct
// `bl`, never virtually.  Slot 18 of the abstract Base/AIBase vtables are stubs
// too.  Only slot 19 of Chase is the real per-frame execute.
//
// This build:
//   1. locates the function by its unique 32-byte signature (at entry+0x4C)
//   2. installs a trampoline on it
//   3. logs the stick pair and rotation deltas it reads/writes, to CONFIRM the
//      object offsets at runtime before we ever write to them
//
// It writes nothing. Observation only - deliberately.

#include <switch.h>
#include <lib.hpp>

#include <lib/util/sys/mem_layout.hpp>
#include <lib/util/sys/modules.hpp>

namespace {

    using ChaseExecuteFn = void (*)(void* self, void* arg);
    ChaseExecuteFn g_Orig = nullptr;
    u64 g_Calls = 0;

    float LoadF(const void* base, size_t off) {
        float v = 0.0f;
        __builtin_memcpy(&v, static_cast<const u8*>(base) + off, sizeof(v));
        return v;
    }

    void ChaseExecuteHook(void* self, void* arg) {
        g_Calls++;

        if (g_Calls == 1 || (g_Calls % 240) == 0) {
            // Fields per the investigation: 0x5C/0x60 stick pair, 0x64/0x68
            // second pair, 0x6C/0x70 rotation deltas.
            Logging.Log("NativeMouse: nm16 CHASE #%llu self=%p stick=%.4f,%.4f alt=%.4f,%.4f delta=%.4f,%.4f",
                        static_cast<unsigned long long>(g_Calls), self,
                        LoadF(self, 0x5C), LoadF(self, 0x60),
                        LoadF(self, 0x64), LoadF(self, 0x68),
                        LoadF(self, 0x6C), LoadF(self, 0x70));
        }

        if (g_Orig != nullptr) {
            g_Orig(self, arg);
        }
    }

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

    Logging.Log("NativeMouse: nm16 STEP1 entered");
    exl::hook::Initialize();

    const auto& main = exl::util::GetModuleInfo(exl::util::ModuleIndex::Main);
    const uintptr_t textStart = main.m_Text.m_Start;
    const uintptr_t textEnd   = textStart + main.m_Text.m_Size;

    // Unique 32-byte signature sitting at entry + 0x4C of Chase execute.
    // Verified: exactly one occurrence in the image.
    static const u8 kSigAtEntryPlus4C[32] = {
        0xC8, 0xAE, 0x00, 0xB4, 0x13, 0xD9, 0x41, 0xF9,
        0x48, 0x33, 0x10, 0x91, 0x49, 0x17, 0x44, 0xB9,
        0xE0, 0x03, 0x14, 0xAA, 0x08, 0x01, 0x40, 0xF9,
        0xE9, 0x4B, 0x00, 0xB9, 0xE8, 0x23, 0x00, 0xF9,
    };

    const uintptr_t hit = FindPattern(textStart, textEnd, kSigAtEntryPlus4C,
                                      sizeof(kSigAtEntryPlus4C));
    Logging.Log("NativeMouse: nm16 STEP2 sig hit = 0x%llX",
                static_cast<unsigned long long>(hit));

    if (hit == 0) {
        Logging.Log("NativeMouse: nm16 no match - not hooking");
        return;
    }

    // The signature sits 0x4C bytes into the function, so the entry is 0x4C lower.
    const uintptr_t entry = hit - 0x4C;
    Logging.Log("NativeMouse: nm16 STEP3 chase execute entry = 0x%llX (offset 0x%llX)",
                static_cast<unsigned long long>(entry),
                static_cast<unsigned long long>(entry - textStart));

    g_Orig = reinterpret_cast<ChaseExecuteFn>(
        exl::hook::Hook(entry, reinterpret_cast<uintptr_t>(&ChaseExecuteHook), true));

    Logging.Log("NativeMouse: nm16 STEP4 hooked, orig=%p", reinterpret_cast<void*>(g_Orig));
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
