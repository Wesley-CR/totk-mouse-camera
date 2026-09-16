// NativeMouse — BUILD 28 (tag "nm28").
//
// nm21 PROVED the injection point: writing input+0x420/+0x424
// (input = *(self+0x20)) from the Chase hook moves the camera through the
// stock pipeline. nm28 keeps that write and the nm24 mouse path, and drops
// edge-wrap-helper teleport samples (see re/edge_wrap.py): a sample whose
// absolute (x,y) jumps to the window centre is the helper recentering, not
// the hand, so its dx/dy is discarded. EXPERIMENTAL, isolated.
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

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
    volatile u64   g_Wraps = 0;      // helper-teleport samples dropped (worker-only writer)
    bool     g_ThreadStarted = false;
    Thread   g_Worker;
    alignas(0x1000) u8 g_WorkerStack[0x8000];

    constexpr float kCountsToStick = 0.02f;    // base scale; INI SensitivityX/Y multiply it
    constexpr float kMaxStick      = 4.0f;     // sanity bound per update, not a speed cap

    volatile float g_SensX = 1.0f;   // from sdmc:/NativeMouse.ini; defaults if missing/unreadable
    volatile float g_SensY = 1.0f;

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

    float ReadKey(const char* text, const char* key, float def) {
        const char* p = std::strstr(text, key);
        if (p == nullptr) {
            return def;
        }
        p = std::strchr(p, '=');
        if (p == nullptr) {
            return def;
        }
        const float v = std::strtof(p + 1, nullptr);
        if (v < 0.05f || v > 20.0f) {
            return def;
        }
        return v;
    }

    // Worker-only (post-boot), never exl_main: opens our own fsp-srv session,
    // reads SensitivityX/Y, keeps defaults on any failure. Runs after the
    // mouse is live so a filesystem problem can never take the camera down.
    void LoadConfig() {
        const Result rc = fsInitialize();
        if (R_FAILED(rc)) {
            Logging.Log("NativeMouse: nm28 config fs rc=0x%X, defaults", rc);
            return;
        }
        fsdevMountSdmc();
        std::FILE* f = std::fopen("sdmc:/NativeMouse.ini", "r");
        if (f == nullptr) {
            Logging.Log("NativeMouse: nm28 config no ini, defaults sens=1.0,1.0");
            return;
        }
        char buf[1024];
        const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        std::fclose(f);
        buf[n] = '\0';
        g_SensX = ReadKey(buf, "SensitivityX", 1.0f);
        g_SensY = ReadKey(buf, "SensitivityY", 1.0f);
        const float sx = g_SensX;
        const float sy = g_SensY;
        Logging.Log("NativeMouse: nm28 config sens=%.2f,%.2f", sx, sy);
    }

    void WorkerEntry(void* arg) {
        (void)arg;
        svcSleepThread(5'000'000'000ULL);   // game well past loading, user still watching boot

        Logging.Log("NativeMouse: nm28 worker starting service init");
        Result rc = smInitialize();
        Logging.Log("NativeMouse: nm28 worker smInitialize rc=0x%X", rc);
        if (R_FAILED(rc)) {
            return;
        }
        rc = appletInitialize();
        Logging.Log("NativeMouse: nm28 worker appletInitialize rc=0x%X", rc);
        if (R_FAILED(rc)) {
            return;
        }
        rc = hidInitialize();
        Logging.Log("NativeMouse: nm28 worker hidInitialize rc=0x%X", rc);
        if (R_FAILED(rc)) {
            return;
        }
        hidInitializeMouse();
        Logging.Log("NativeMouse: nm28 worker mouse activated");
        g_HidReady = true;

        LoadConfig();

        u64 lastSample = 0;
        s32 prevX = 0, prevY = 0;
        bool havePrev = false;
        u64 nextWrapLog = 10;
        while (true) {
            HidMouseState st[16];
            const size_t n = hidGetMouseStates(st, 16);
            for (size_t i = 0; i < n && i < 16; i++) {
                if (st[i].sampling_number <= lastSample) {
                    continue;
                }
                lastSample = st[i].sampling_number;
                if (!havePrev) {
                    prevX = st[i].x;
                    prevY = st[i].y;
                    havePrev = true;
                    continue;
                }
                // Edge-wrap helper teleports the cursor to the window centre;
                // that sample's delta is the teleport, not the hand. Spot it
                // by the absolute jump (Eden scales the window to 1280x720,
                // so centre is exactly (640,360)) and drop that axis.
                int jx = st[i].x - prevX;
                if (jx < 0) {
                    jx = -jx;
                }
                int jy = st[i].y - prevY;
                if (jy < 0) {
                    jy = -jy;
                }
                int cx = st[i].x - 640;
                if (cx < 0) {
                    cx = -cx;
                }
                int cy = st[i].y - 360;
                if (cy < 0) {
                    cy = -cy;
                }
                prevX = st[i].x;
                prevY = st[i].y;
                if (jx > 500 && cx <= 150) {
                    g_Wraps = g_Wraps + 1;
                } else {
                    g_AccumX += static_cast<float>(st[i].delta_x);
                }
                if (jy > 300 && cy <= 120) {
                    g_Wraps = g_Wraps + 1;
                } else {
                    g_AccumY += static_cast<float>(st[i].delta_y);
                }
            }
            if (g_Wraps >= nextWrapLog) {
                Logging.Log("NativeMouse: nm28 worker dropped %llu wrap samples",
                            static_cast<unsigned long long>(g_Wraps));
                nextWrapLog = g_Wraps + 10;
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
            Logging.Log("NativeMouse: nm28 threadCreate=0x%X threadStart=0x%X", rcT, rcS);
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
                const float sx = g_SensX;
                const float sy = g_SensY;
                wx = Clamp(ax * kCountsToStick * sx, -kMaxStick, kMaxStick);
                wy = Clamp(-ay * kCountsToStick * sy, -kMaxStick, kMaxStick);
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
                Logging.Log("NativeMouse: nm28 #%llu %s wrote=%.3f,%.3f readback=%.4f,%.4f",
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

    Logging.Log("NativeMouse: nm28 STEP1 entered");
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
        Logging.Log("NativeMouse: nm28 no signature match - idle");
        return;
    }

    const uintptr_t entry = hit - 0x4C;
    g_Orig = reinterpret_cast<ChaseExecuteFn>(
        exl::hook::Hook(entry, reinterpret_cast<uintptr_t>(&ChaseExecuteHook), true));

    Logging.Log("NativeMouse: nm28 entry=0x%llX orig=%p READY",
                static_cast<unsigned long long>(entry),
                reinterpret_cast<void*>(g_Orig));
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
