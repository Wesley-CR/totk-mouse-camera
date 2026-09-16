// NativeMouse — BUILD 19 (tag "nm19").
//
// MINIMAL READ-ONLY TEST WITH THE CORRECTED FIELD MAP.
//
// Corrections applied, verified independently against the image:
//
//   * There is NO register reload at 0x1DB09C.  The instruction is
//     `ldr x22,[x20,#0x20]` - destination x22, not x20.  An exhaustive scan for
//     writes to x20 between the entry and the stick read finds exactly one, the
//     initial `mov x20,x0`.  So x20 IS `this`.
//   * Therefore the real field map is THIS-relative, not input-relative:
//         1DB6EC  ldp s1,s2,[x20,#0x58]   ; raw stick   -> this+0x58 / +0x5C
//         1DB704  str s1,[x20,#0x60]      ; processed
//         1DB730  str s1,[x20,#0x68]      ; processed
//     My nm17 build wrote to (this+0x20)+0x58 - the wrong struct entirely.
//   * +0x420 / +0x424 ARE input-relative (reached via x26 or x22 = [this+0x20]),
//     but they are stick-derived scale/gate values, not raw input.
//
// Also: nm18 died before its own "entry=" log line, and the only new thing it did
// at init was threadCreate.  Thread creation at module-init has been unsafe in
// every build that tried it, and nm18 was supposed to be testing the field map -
// so this build removes the thread entirely.  No HID, no threads, no writes.
//
// Purpose: confirm the corrected offsets read plausible, changing stick values.

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

        if (g_Calls == 1 || (g_Calls % 120) == 0) {
            // THIS-relative per the corrected map. Also dump the input-relative
            // 0x420/0x424 for comparison.
            void* input = nullptr;
            __builtin_memcpy(&input, static_cast<const u8*>(self) + 0x20, sizeof(input));

            Logging.Log("NativeMouse: nm19 #%llu this=%p in=%p | s58=%.4f s5C=%.4f s60=%.4f s64=%.4f s68=%.4f s6C=%.4f s70=%.4f | in420=%.4f in424=%.4f",
                        static_cast<unsigned long long>(g_Calls), self, input,
                        LoadF(self, 0x58), LoadF(self, 0x5C),
                        LoadF(self, 0x60), LoadF(self, 0x64),
                        LoadF(self, 0x68), LoadF(self, 0x6C),
                        LoadF(self, 0x70),
                        input ? LoadF(input, 0x420) : 0.0f,
                        input ? LoadF(input, 0x424) : 0.0f);
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

    Logging.Log("NativeMouse: nm19 STEP1 entered");
    exl::hook::Initialize();

    const auto& main = exl::util::GetModuleInfo(exl::util::ModuleIndex::Main);
    const uintptr_t textStart = main.m_Text.m_Start;
    const uintptr_t textEnd   = textStart + main.m_Text.m_Size;

    static const u8 kSigAtEntryPlus4C[32] = {
        0xC8, 0xAE, 0x00, 0xB4, 0x13, 0xD9, 0x41, 0xF9,
        0x48, 0x33, 0x10, 0x91, 0x49, 0x17, 0x44, 0xB9,
        0xE0, 0x03, 0x14, 0xAA, 0x08, 0x01, 0x40, 0xF9,
        0xE9, 0x4B, 0x00, 0xB9, 0xE8, 0x23, 0x00, 0xF9,
    };

    const uintptr_t hit = FindPattern(textStart, textEnd, kSigAtEntryPlus4C,
                                      sizeof(kSigAtEntryPlus4C));
    if (hit == 0) {
        Logging.Log("NativeMouse: nm19 no signature match - idle");
        return;
    }

    const uintptr_t entry = hit - 0x4C;
    g_Orig = reinterpret_cast<ChaseExecuteFn>(
        exl::hook::Hook(entry, reinterpret_cast<uintptr_t>(&ChaseExecuteHook), true));

    Logging.Log("NativeMouse: nm19 entry=0x%llX orig=%p READY (no thread, no hid, no writes)",
                static_cast<unsigned long long>(entry),
                reinterpret_cast<void*>(g_Orig));
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
