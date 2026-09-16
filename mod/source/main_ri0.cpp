// NativeMouse — TRANSPORT PROBE ri0 (branch experiment/raw-input-bridge).
//
// Baseline: nm28 (cefc8a7). The Chase hook below is byte-identical in
// behaviour; g_HidReady is never set here so it keeps writing nm21's TEST
// oscillation as a heartbeat. The ONLY change is the worker: instead of HID
// it brings up sm + the socket driver, binds UDP 0.0.0.0:51987 and logs
// every magic-matching datagram (helper sends increasing sequence numbers
// from re/udp_probe.py). No camera writes from packets, no HID, no INI.

#include <switch.h>
#include <lib.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <lib/util/sys/mem_layout.hpp>
#include <lib/util/sys/modules.hpp>

namespace {

    using ChaseExecuteFn = void (*)(void* self, void* arg);
    ChaseExecuteFn g_Orig = nullptr;
    u64 g_Calls = 0;

    // Untouched by the probe worker; the hook keeps consuming zeros and the
    // TEST fallback keeps the camera swinging as a liveness heartbeat.
    volatile bool  g_HidReady = false;
    volatile float g_AccumX = 0.0f;
    volatile float g_AccumY = 0.0f;
    volatile u64   g_Wraps = 0;
    bool     g_ThreadStarted = false;
    Thread   g_Worker;
    alignas(0x1000) u8 g_WorkerStack[0x8000];

    constexpr float kCountsToStick = 0.02f;
    constexpr float kMaxStick      = 4.0f;

    volatile float g_SensX = 1.0f;
    volatile float g_SensY = 1.0f;

    static const u32 kProbeMagic = 0x4D4E5657u;
    static const u16 kProbePort = 51987;

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

        Logging.Log("NativeMouse: ri0 worker starting transport probe");
        Result rc = smInitialize();
        Logging.Log("NativeMouse: ri0 worker smInitialize rc=0x%X", rc);
        if (R_FAILED(rc)) {
            return;
        }

        static const SocketInitConfig kSockConf = {
            0x800, 0x800, 0, 0, 0x400, 0x1000, 2, 1, BsdServiceType_User,
        };
        rc = socketInitialize(&kSockConf);
        Logging.Log("NativeMouse: ri0 worker socketInitialize rc=0x%X", rc);
        if (R_FAILED(rc)) {
            return;
        }

        const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        Logging.Log("NativeMouse: ri0 worker socket fd=%d", fd);
        if (fd < 0) {
            return;
        }

        struct sockaddr_in addr;
        __builtin_memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kProbePort);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        const int brc = bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        Logging.Log("NativeMouse: ri0 worker bind rc=%d", brc);
        if (brc != 0) {
            return;
        }

        Logging.Log("NativeMouse: ri0 worker listening on UDP %u",
                    static_cast<unsigned>(kProbePort));
        u32 lastSeq = 0;
        u64 got = 0;
        while (true) {
            u8 pkt[16];
            const ssize_t n = recvfrom(fd, pkt, sizeof(pkt), MSG_DONTWAIT, nullptr, nullptr);
            if (n == 16) {
                u32 magic = 0, seq = 0;
                s32 dx = 0, dy = 0;
                __builtin_memcpy(&magic, pkt, 4);
                __builtin_memcpy(&seq, pkt + 4, 4);
                __builtin_memcpy(&dx, pkt + 8, 4);
                __builtin_memcpy(&dy, pkt + 12, 4);
                if (magic == kProbeMagic) {
                    got++;
                    if (lastSeq != 0 && seq != lastSeq && seq != lastSeq + 1) {
                        Logging.Log("NativeMouse: ri0 probe GAP last=%u got=%u",
                                    static_cast<unsigned>(lastSeq),
                                    static_cast<unsigned>(seq));
                    }
                    if (seq != lastSeq) {
                        lastSeq = seq;
                    }
                    Logging.Log("NativeMouse: ri0 probe #%llu seq=%u dx=%d dy=%d",
                                static_cast<unsigned long long>(got),
                                static_cast<unsigned>(seq),
                                static_cast<int>(dx), static_cast<int>(dy));
                }
            }
            svcSleepThread(4'000'000ULL);
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
            Logging.Log("NativeMouse: ri0 threadCreate=0x%X threadStart=0x%X", rcT, rcS);
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
                Logging.Log("NativeMouse: ri0 #%llu %s wrote=%.3f,%.3f readback=%.4f,%.4f",
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

    Logging.Log("NativeMouse: ri0 STEP1 entered");
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
        Logging.Log("NativeMouse: ri0 no signature match - idle");
        return;
    }

    const uintptr_t entry = hit - 0x4C;
    g_Orig = reinterpret_cast<ChaseExecuteFn>(
        exl::hook::Hook(entry, reinterpret_cast<uintptr_t>(&ChaseExecuteHook), true));

    Logging.Log("NativeMouse: ri0 entry=0x%llX orig=%p READY",
                static_cast<unsigned long long>(entry),
                reinterpret_cast<void*>(g_Orig));
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
