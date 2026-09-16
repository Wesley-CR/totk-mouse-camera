// NativeMouse — BUILD 15 (tag "nm15").
//
// nm14 proved the machinery works end to end:
//   * the camera function's 64-byte signature was located at runtime
//     (0x8325D728, and 0x8325D728 - 0x80EA3000 == 0x23BA728 exactly)
//   * exl::hook::Hook() installed a trampoline successfully
//   * the game booted and ran
//
// But the hook was never CALLED. The reason is now known: 0x23BA728 is a method
// of the generic PlayerCamera vtable, and the game drives a DERIVED class
// (PlayerCameraChase) whose vtable overrides those slots. So the base
// implementation is simply never dispatched in normal exploration.
//
// Method
// ------
// Rather than guess which derived method is the real per-frame update, this
// build hooks SEVERAL candidates at once, each with its own counter, and reports
// which one actually fires. One run then answers definitively what several
// rounds of static reasoning have not.
//
// Candidates are found by RUNTIME SIGNATURE (not absolute addresses), so the
// loaded base does not matter.

#include <switch.h>
#include <lib.hpp>

#include <lib/util/sys/mem_layout.hpp>
#include <lib/util/sys/modules.hpp>

namespace {

    constexpr int kMaxCandidates = 6;

    struct Candidate {
        const char* name;
        const u8*   sig;
        size_t      sigLen;
        void*       orig;
        u64         calls;
    };

    // ---------------------------------------------------------------------
    // Signatures: raw instruction bytes taken from the decompressed TOTK 1.4.2
    // image. These contain no relocations, so they are valid at any load base.
    // Each was checked for uniqueness in .text.
    // ---------------------------------------------------------------------

    // PlayerCameraChase vtable slot 40 -> image VA 0x023B77D4.
    // Chase-specific (base vtable slots 22..39 mirror the base class), so this is
    // the strongest candidate for the per-frame third-person camera update.
    // 48 bytes; exactly one match in .text.
    const u8 kSigChase40[48] = {
        0xFD, 0x7B, 0xBE, 0xA9, 0xF4, 0x4F, 0x01, 0xA9,
        0xFD, 0x03, 0x00, 0x91, 0xF3, 0x03, 0x01, 0xAA,
        0xF4, 0x03, 0x00, 0xAA, 0x71, 0xD1, 0xA6, 0x97,
        0xE0, 0x03, 0x14, 0xAA, 0xE1, 0x03, 0x13, 0xAA,
        0x4B, 0x00, 0x00, 0x94, 0x08, 0x4E, 0xA8, 0x52,
        0x9F, 0x42, 0x08, 0xF8,
    };

    // PlayerCamera/Chase stick-scale helper -> image VA 0x023BA670.
    // Contains the `ldr s0,[x0] ; fmul s0,s8,s0 ; str s0,[x19,#0x6c]` sequence,
    // i.e. it converts a stick value into a rotation delta. 48 bytes; one match.
    const u8 kSigStickHelper[48] = {
        0xE8, 0x0F, 0x1D, 0xFC, 0xFD, 0x7B, 0x01, 0xA9,
        0xF4, 0x4F, 0x02, 0xA9, 0xFD, 0x43, 0x00, 0x91,
        0xF3, 0x03, 0x00, 0xAA, 0x40, 0xF9, 0xFF, 0x97,
        0x68, 0x52, 0x40, 0xF9, 0x14, 0xF0, 0xA7, 0x52,
        0x00, 0x10, 0x2E, 0x1E, 0x74, 0x72, 0x00, 0xB9,
        0x01, 0xE4, 0x00, 0x2F, 0x60, 0xE2, 0x01, 0x91,
    };

    Candidate g_Candidates[kMaxCandidates];
    int g_CandidateCount = 0;

    // Generic hook body: increments the counter belonging to its own index.
    // A small trampoline per candidate is generated below.
    void CommonHook(int idx, void* self, void* arg) {
        Candidate& c = g_Candidates[idx];
        c.calls++;
        if (c.calls == 1 || (c.calls % 300) == 0) {
            Logging.Log("NativeMouse: nm15 [%s] call #%llu self=%p",
                        c.name, static_cast<unsigned long long>(c.calls), self);
        }
        using Fn = void (*)(void*, void*);
        if (c.orig != nullptr) {
            reinterpret_cast<Fn>(c.orig)(self, arg);
        }
    }

    void Hook0(void* a, void* b) { CommonHook(0, a, b); }
    void Hook1(void* a, void* b) { CommonHook(1, a, b); }
    void Hook2(void* a, void* b) { CommonHook(2, a, b); }

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

    Logging.Log("NativeMouse: nm15 STEP1 entered");
    exl::hook::Initialize();

    const auto& main = exl::util::GetModuleInfo(exl::util::ModuleIndex::Main);
    const uintptr_t textStart = main.m_Text.m_Start;
    const uintptr_t textEnd   = textStart + main.m_Text.m_Size;
    Logging.Log("NativeMouse: nm15 text 0x%llX-0x%llX",
                static_cast<unsigned long long>(textStart),
                static_cast<unsigned long long>(textEnd));

    // Register candidates. Only entries with a non-zero signature are used.
    struct Reg { const char* name; const u8* sig; size_t len; };
    const Reg regs[] = {
        { "chase_slot40",     kSigChase40,      sizeof(kSigChase40)      },
        { "stick_helper",     kSigStickHelper,  sizeof(kSigStickHelper)  },
    };

    for (const auto& r : regs) {
        if (g_CandidateCount >= kMaxCandidates) {
            break;
        }
        // A signature of all zeros is a placeholder - skip it rather than
        // matching arbitrary memory.
        bool allZero = true;
        for (size_t i = 0; i < r.len; i++) {
            if (r.sig[i] != 0) {
                allZero = false;
                break;
            }
        }
        if (allZero) {
            Logging.Log("NativeMouse: nm15 %s has placeholder signature - skipped", r.name);
            continue;
        }
        g_Candidates[g_CandidateCount] = { r.name, r.sig, r.len, nullptr, 0 };
        g_CandidateCount++;
    }

    int idx = 0;
    for (int i = 0; i < g_CandidateCount; i++) {
        Candidate& c = g_Candidates[i];
        const uintptr_t found = FindPattern(textStart, textEnd, c.sig, c.sigLen);
        Logging.Log("NativeMouse: nm15 [%s] signature -> 0x%llX", c.name,
                    static_cast<unsigned long long>(found));
        if (found == 0) {
            continue;
        }

        void (*fn)(void*, void*) = (i == 0) ? &Hook0 : (i == 1) ? &Hook1 : &Hook2;
        c.orig = reinterpret_cast<void*>(
            exl::hook::Hook(found, reinterpret_cast<uintptr_t>(fn), true));
        Logging.Log("NativeMouse: nm15 [%s] hooked, orig=%p", c.name, c.orig);
        idx++;
    }

    Logging.Log("NativeMouse: nm15 STEP-END %d hook(s) installed", idx);
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
