// NativeMouse — BUILD 20 (tag "nm20").
//
// FIELD DISCOVERY BY MEASUREMENT.
//
// Offsets guessed so far have all been wrong:
//   * nm17 wrote (this+0x20)+0x58   - wrong struct
//   * the investigation corrected it to this+0x58, but the arithmetic at
//     0x1DB6EC proves that is camera DISTANCE, not stick:
//         ldp  s1,s2,[x20,#0x58]   ; s1 = 10.0 (constant), s2 = 1.0
//         fmul s1,s1,s2            ; 10.0
//         fdiv s2,s15,s1           ; 1/10.0  -> blend factor
//         str  s1,[x20,#0x60]
//   * input+0x420 / +0x424 are read by several executes, but there are ZERO
//     stores to them anywhere in .text - their producer is unknown.
//
// So rather than guess a fourth time, this build MEASURES. It dumps a window of
// the camera object as floats once per N frames. Fields driven by the stick will
// change while the stick is held; constant fields will not. The dump is taken
// twice with a marker so a single run covers both cases.
//
// READ ONLY. No HID, no threads, no writes.

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

    void DumpWindow(const char* tag, const void* self) {
        // Two ranges: the object head (rotations live here in this engine) and
        // further down where the investigation placed state.
        for (size_t base = 0x40; base <= 0x40; base += 0x40) {
            Logging.Log("NativeMouse: nm20 %s [%02X] %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f",
                        tag, static_cast<unsigned>(base),
                        LoadF(self, base + 0x00), LoadF(self, base + 0x04),
                        LoadF(self, base + 0x08), LoadF(self, base + 0x0C),
                        LoadF(self, base + 0x10), LoadF(self, base + 0x14),
                        LoadF(self, base + 0x18), LoadF(self, base + 0x1C));
        }
        for (size_t base = 0x60; base <= 0x60; base += 0x20) {
            Logging.Log("NativeMouse: nm20 %s [%02X] %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f",
                        tag, static_cast<unsigned>(base),
                        LoadF(self, base + 0x00), LoadF(self, base + 0x04),
                        LoadF(self, base + 0x08), LoadF(self, base + 0x0C),
                        LoadF(self, base + 0x10), LoadF(self, base + 0x14),
                        LoadF(self, base + 0x18), LoadF(self, base + 0x1C));
        }
    }

    void ChaseExecuteHook(void* self, void* arg) {
        g_Calls++;

        // Sample early (stick likely neutral) and late, so one run shows which
        // fields move. 240 frames ~ 4 s at 60 fps.
        if (g_Calls == 60) {
            DumpWindow("EARLY", self);
        } else if (g_Calls == 1200) {
            DumpWindow("LATE ", self);
        } else if ((g_Calls % 600) == 0) {
            void* input = nullptr;
            __builtin_memcpy(&input, static_cast<const u8*>(self) + 0x20, sizeof(input));
            Logging.Log("NativeMouse: nm20 #%llu this=%p in=%p alive",
                        static_cast<unsigned long long>(g_Calls), self, input);
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

    Logging.Log("NativeMouse: nm20 STEP1 entered");
    exl::hook::Initialize();

    const auto& main = exl::util::GetModuleInfo(exl::util::ModuleIndex::Main);
    const uintptr_t textStart = main.m_Text.m_Start;
    const uintptr_t textEnd   = textStart + main.m_Text.m_Size;

    static const u8 kSig[32] = {
        0xC8, 0xAE, 0x00, 0xB4, 0x13, 0xD9, 0x41, 0xF9,
        0x48, 0x33, 0x10, 0x91, 0x49, 0x17, 0x44, 0xB9,
        0xE0, 0x03, 0x14, 0xAA, 0x08, 0x01, 0x40, 0xF9,
        0xE9, 0x4B, 0x00, 0xB9, 0xE8, 0x23, 0x00, 0xF9,
    };

    const uintptr_t hit = FindPattern(textStart, textEnd, kSig, sizeof(kSig));
    if (hit == 0) {
        Logging.Log("NativeMouse: nm20 no signature match - idle");
        return;
    }

    const uintptr_t entry = hit - 0x4C;
    g_Orig = reinterpret_cast<ChaseExecuteFn>(
        exl::hook::Hook(entry, reinterpret_cast<uintptr_t>(&ChaseExecuteHook), true));

    Logging.Log("NativeMouse: nm20 entry=0x%llX orig=%p READY (read-only field discovery)",
                static_cast<unsigned long long>(entry),
                reinterpret_cast<void*>(g_Orig));
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
