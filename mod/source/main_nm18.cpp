// NativeMouse — BUILD 18 (tag "nm18").
//
// nm17 installed its hook successfully ("chase entry=0x8101B01C ... READY") but the
// game died before the first "CHASE #1" log line. nm17 added TWO risky things at
// once, inside the camera hook:
//
//   (a) lazy hidInitialize() + hidInitializeMouse() on the camera thread
//   (b) writing mouse-derived floats into the input struct
//
// This build separates them so one run identifies the culprit:
//
//   * HID setup now happens on a BACKGROUND THREAD, several seconds after boot,
//     never on the camera thread.  The camera hook only *reads* a flag.
//   * the camera hook READS and LOGS the input struct but WRITES NOTHING.
//
// If this boots and logs CHASE lines with plausible values, then (a) was the
// problem and writing becomes the next step.  If it still dies, the fault is in
// merely touching those fields, and the offsets are wrong.

#include <switch.h>
#include <lib.hpp>

#include <lib/util/sys/mem_layout.hpp>
#include <lib/util/sys/modules.hpp>

namespace {

    using ChaseExecuteFn = void (*)(void* self, void* arg);
    ChaseExecuteFn g_Orig = nullptr;

    u64  g_Calls = 0;
    bool g_HidReady = false;          // written by the worker, read by the hook
    u64  g_LastSample = 0;
    float g_AccumX = 0.0f;
    float g_AccumY = 0.0f;

    Thread g_Worker;
    alignas(0x1000) u8 g_WorkerStack[0x8000];

    float LoadF(const void* base, size_t off) {
        float v = 0.0f;
        __builtin_memcpy(&v, static_cast<const u8*>(base) + off, sizeof(v));
        return v;
    }

    // Background HID bring-up. Runs long after boot, on its own thread - never on
    // the camera thread, which is where nm17 tried it.
    void WorkerEntry(void* arg) {
        (void)arg;
        svcSleepThread(20'000'000'000ULL);   // 20 s: game is well past loading

        Logging.Log("NativeMouse: nm18 worker starting HID init");
        const Result rc = hidInitialize();
        Logging.Log("NativeMouse: nm18 worker hidInitialize rc=0x%X", rc);
        if (R_SUCCEEDED(rc)) {
            hidInitializeMouse();
            Logging.Log("NativeMouse: nm18 worker mouse activated");
            g_HidReady = true;
        } else {
            Logging.Log("NativeMouse: nm18 worker HID failed - camera will not be driven");
        }
    }

    void ChaseExecuteHook(void* self, void* arg) {
        g_Calls++;

        void* input = nullptr;
        __builtin_memcpy(&input, static_cast<const u8*>(self) + 0x20, sizeof(input));

        // Drain mouse samples only if the worker succeeded. No writes anywhere.
        if (g_HidReady) {
            HidMouseState st[16];
            const size_t n = hidGetMouseStates(st, 16);
            for (size_t i = 0; i < n && i < 16; i++) {
                if (st[i].sampling_number <= g_LastSample) {
                    continue;
                }
                g_LastSample = st[i].sampling_number;
                g_AccumX += static_cast<float>(st[i].delta_x);
                g_AccumY += static_cast<float>(st[i].delta_y);
            }
        }

        if (g_Calls == 1 || (g_Calls % 240) == 0) {
            Logging.Log("NativeMouse: nm18 CHASE #%llu self=%p input=%p hid=%d mouse=%.1f,%.1f "
                        "s58=%.4f s5C=%.4f s60=%.4f s64=%.4f s68=%.4f s6C=%.4f s70=%.4f",
                        static_cast<unsigned long long>(g_Calls), self, input,
                        static_cast<int>(g_HidReady), g_AccumX, g_AccumY,
                        input ? LoadF(input, 0x58) : 0.0f,
                        input ? LoadF(input, 0x5C) : 0.0f,
                        input ? LoadF(input, 0x60) : 0.0f,
                        input ? LoadF(input, 0x64) : 0.0f,
                        input ? LoadF(input, 0x68) : 0.0f,
                        input ? LoadF(input, 0x6C) : 0.0f,
                        input ? LoadF(input, 0x70) : 0.0f);
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

    Logging.Log("NativeMouse: nm18 STEP1 entered");
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
        Logging.Log("NativeMouse: nm18 no signature match - idle");
        return;
    }

    const uintptr_t entry = hit - 0x4C;
    g_Orig = reinterpret_cast<ChaseExecuteFn>(
        exl::hook::Hook(entry, reinterpret_cast<uintptr_t>(&ChaseExecuteHook), true));

    // Start the HID worker. Deferred far past boot; the camera hook never blocks.
    const Result rcT = threadCreate(&g_Worker, WorkerEntry, nullptr, g_WorkerStack,
                                    sizeof(g_WorkerStack), 0x2C, -2);
    Result rcS = -1;
    if (R_SUCCEEDED(rcT)) {
        rcS = threadStart(&g_Worker);
    }

    Logging.Log("NativeMouse: nm18 entry=0x%llX orig=%p threadCreate=0x%X threadStart=0x%X",
                static_cast<unsigned long long>(entry),
                reinterpret_cast<void*>(g_Orig), rcT, rcS);
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
