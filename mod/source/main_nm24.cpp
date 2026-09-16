// NativeMouse — BUILD 23 (tag "nm24").
//
// nm21 PROVED the injection point: writing input+0x420/+0x424
// (input = *(self+0x20)) from the Chase hook moves the camera through the
// stock pipeline. nm24 keeps that write byte-identical and adds the mouse.
//
// Why this shape:
//   * nm17 called hidInitialize() ON the camera thread -> crash.
//   * nm18 called threadCreate() IN exl_main (loader thread) -> died before
//     its own log line.
//   * So: exl_main does only the proven-safe budget (hook init + one hook).
//     The worker thread is spawned from the FIRST Chase call (game thread,
//     after boot) and OWNS every HID call. The hook never calls HID; it only
//     reads the worker's accumulator.
//   * Until the worker reports ready, the hook writes nm21's oscillating test
//     value, so the run stays attributable: swinging = write alive, HID fault
//     if it never switches; no boot at all = thread-spawn fault.

#include <switch.h>
#include <lib.hpp>

#include <lib/util/sys/mem_layout.hpp>
#include <lib/util/sys/modules.hpp>

namespace {

    using ChaseExecuteFn = void (*)(void* self, void* arg);
    ChaseExecuteFn g_Orig = nullptr;
    u64 g_Calls = 0;

    // Worker-owned HID state. Single writer (worker) / single reader (hook);
    // a torn consume loses a few counts at most, which is invisible on a
    // camera. ponytail: plain shared floats, atomics if this ever matters.
    volatile bool  g_HidReady = false;
    volatile float g_AccumX = 0.0f;
    volatile float g_AccumY = 0.0f;
    bool     g_ThreadStarted = false;
    Thread   g_Worker;
    alignas(0x1000) u8 g_WorkerStack[0x8000];

    constexpr float kCountsToStick = 0.004f;   // mouse counts -> stick units (nm17 value)
    constexpr float kMaxStick      = 4.0f;     // sanity bound per update, not a speed cap

    float LoadF(const void* base, size_t off) {
        float v = 0.0f;
        __builtin_memcpy(&v, static_cast<const u8*>(base) + off, sizeof(v));
        return v;
    }

    void StoreF(void* base, size_t off, float v) {
        __builtin_memcpy(static_cast<u8*>(base) + off, &v, sizeof(v));
    }

    float Clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    void WorkerEntry(void* arg) {
        (void)arg;
        svcSleepThread(5'000'000'000ULL);   // game well past loading, user still watching boot

        Logging.Log("NativeMouse: nm24 worker starting service init");
        Result rc = smInitialize();
        Logging.Log("NativeMouse: nm24 worker smInitialize rc=0x%X", rc);
        if (R_FAILED(rc)) {
            return;
        }
        rc = appletInitialize();
        Logging.Log("NativeMouse: nm24 worker appletInitialize rc=0x%X", rc);
        if (R_FAILED(rc)) {
            return;
        }
        rc = hidInitialize();
        Logging.Log("NativeMouse: nm24 worker hidInitialize rc=0x%X", rc);
        if (R_FAILED(rc)) {
            return;
        }
        hidInitializeMouse();
        Logging.Log("NativeMouse: nm24 worker mouse activated");
        g_HidReady = true;

        u64 lastSample = 0;
        while (true) {
            HidMouseState st[16];
            const size_t n = hidGetMouseStates(st, 16);
            for (size_t i = 0; i < n && i < 16; i++) {
                if (st[i].sampling_number <= lastSample) {
                    continue;
                }
                lastSample = st[i].sampling_number;
                g_AccumX += static_cast<float>(st[i].delta_x);
                g_AccumY += static_cast<float>(st[i].delta_y);
            }
            svcSleepThread(8'000'000ULL);   // ~120 Hz poll
        }
    }

    void ChaseExecuteHook(void* self, void* arg) {
        g_Calls++;

        if (!g_ThreadStarted) {
            g_ThreadStarted = true;
            const Result rcT = threadCreate(&g_Worker, WorkerEntry, nullptr,
                                            g_WorkerStack, sizeof(g_WorkerStack), 0x2C, -2);
            Result rcS = -1;
            if (R_SUCCEEDED(rcT)) {
                rcS = threadStart(&g_Worker);
            }
            Logging.Log("NativeMouse: nm24 threadCreate=0x%X threadStart=0x%X", rcT, rcS);
        }

        void* input = nullptr;
        __builtin_memcpy(&input, static_cast<const u8*>(self) + 0x20, sizeof(input));

        if (input != nullptr) {
            float wx, wy;
            bool usedMouse = false;

            if (g_HidReady) {
                // Consume: displacement this frame, then clear. Rotation tracks
                // distance moved since the last camera update, not deflection.
                const float ax = g_AccumX;
                const float ay = g_AccumY;
                g_AccumX = 0.0f;
                g_AccumY = 0.0f;
                wx = Clamp(ax * kCountsToStick, -kMaxStick, kMaxStick);
                wy = Clamp(ay * kCountsToStick, -kMaxStick, kMaxStick);
                usedMouse = true;
            } else {
                // nm21 fallback: 2 s neutral, 2 s full deflection. Proves the
                // write is alive while HID is still coming up.
                const u64 phase = (g_Calls / 120) % 2;
                wx = (phase == 1) ? 1.0f : 0.0f;
                wy = 0.0f;
            }

            StoreF(input, 0x420, wx);
            StoreF(input, 0x424, wy);

            if (g_Calls == 1 || (g_Calls % 120) == 0) {
                Logging.Log("NativeMouse: nm24 #%llu %s wrote=%.3f,%.3f readback=%.4f,%.4f",
                            static_cast<unsigned long long>(g_Calls),
                            usedMouse ? "MOUSE" : "TEST ",
                            wx, wy, LoadF(input, 0x420), LoadF(input, 0x424));
            }
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

    Logging.Log("NativeMouse: nm24 STEP1 entered");
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
        Logging.Log("NativeMouse: nm24 no signature match - idle");
        return;
    }

    const uintptr_t entry = hit - 0x4C;
    g_Orig = reinterpret_cast<ChaseExecuteFn>(
        exl::hook::Hook(entry, reinterpret_cast<uintptr_t>(&ChaseExecuteHook), true));

    Logging.Log("NativeMouse: nm24 entry=0x%llX orig=%p READY",
                static_cast<unsigned long long>(entry),
                reinterpret_cast<void*>(g_Orig));
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
