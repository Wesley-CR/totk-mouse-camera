// NativeMouse — BUILD 21 (tag "nm21").
//
// THE DECISIVE EXPERIMENT: does writing to input+0x420/0x424 move the camera?
//
// History that produced this build
// --------------------------------
// Delegation brief #5 traced the per-frame stick producer to the function
// containing 0x886824, which does:
//
//     str s9, [x20,#0x420]     ; processed horizontal stick
//     str s10,[x20,#0x424]     ; processed vertical stick
//
// after reading the raw stick pair, applying a 0.005 deadzone, scaling by ~1.01
// and clamping.  Chase consumes them at 0x1DB548:
//
//     ldr s1,[x26,#0x420]      ; x26 = *(this+0x20)
//     ldr s2,[x26,#0x424]
//
// So the injection point is (this+0x20)+0x420 / +0x424 - written from the Chase
// hook, before calling the original, so the stock pipeline (scale by this+0x154,
// magnitude gating, smoothing, collision, limits) still applies.
//
// CRUCIAL IRONY: nm17 already wrote exactly these offsets - and then crashed.
// Its crash is attributed to the lazy hidInitialize() it performed on the camera
// thread, NOT to the write.  nm18 removed HID but added a threadCreate and died
// before its own log line.
//
// Therefore this build removes HID ENTIRELY and writes a FIXED OSCILLATING test
// value instead.  That isolates one question with no other variables:
//
//     if the camera swings, the write reaches it.
//
// No HID. No threads. No filesystem. Readback logged to prove the values survive
// to the consumer site.

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

    void StoreF(void* base, size_t off, float v) {
        __builtin_memcpy(static_cast<u8*>(base) + off, &v, sizeof(v));
    }

    void ChaseExecuteHook(void* self, void* arg) {
        g_Calls++;

        void* input = nullptr;
        __builtin_memcpy(&input, static_cast<const u8*>(self) + 0x20, sizeof(input));

        if (input != nullptr) {
            // Fixed oscillation: 2 s neutral, then 2 s full deflection, repeating
            // at ~60 fps. Slow enough that the camera visibly swings if the write
            // takes effect, and unmistakable if it does not.
            const u64 phase = (g_Calls / 120) % 2;
            const float test = (phase == 1) ? 1.0f : 0.0f;

            StoreF(input, 0x420, test);
            StoreF(input, 0x424, 0.0f);

            if (g_Calls == 1 || (g_Calls % 120) == 0) {
                Logging.Log("NativeMouse: nm21 #%llu this=%p in=%p wrote=%.2f readback=%.4f,%.4f",
                            static_cast<unsigned long long>(g_Calls), self, input, test,
                            LoadF(input, 0x420), LoadF(input, 0x424));
            }
        } else if (g_Calls == 1) {
            Logging.Log("NativeMouse: nm21 input pointer NULL - cannot write");
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

    Logging.Log("NativeMouse: nm21 STEP1 entered");
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
        Logging.Log("NativeMouse: nm21 no signature match - idle");
        return;
    }

    const uintptr_t entry = hit - 0x4C;
    g_Orig = reinterpret_cast<ChaseExecuteFn>(
        exl::hook::Hook(entry, reinterpret_cast<uintptr_t>(&ChaseExecuteHook), true));

    Logging.Log("NativeMouse: nm21 entry=0x%llX orig=%p READY (writer, no hid, no threads)",
                static_cast<unsigned long long>(entry),
                reinterpret_cast<void*>(g_Orig));
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
