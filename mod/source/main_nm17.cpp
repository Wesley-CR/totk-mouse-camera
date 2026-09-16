// NativeMouse — BUILD 17 (tag "nm17").
//
// FIRST BUILD THAT ACTUALLY DRIVES THE CAMERA.
//
// nm16 proved the hook lands: `ExecutePlayerCameraChase` (image VA 0x1DB01C) was
// entered 1200+ times with a stable `self`.  This build adds the mouse.
//
// Where the stick actually lives
// ------------------------------
// Disassembling the execute body shows the input struct pointer is loaded once:
//
//     1DB058  ldr x26,[x0,#0x20]      ; x26 = this->inputStruct
//     1DB09C  ldr x22,[x20,#0x20]     ; and x20 := this->inputStruct too
//
// After that reload, `x20` is the INPUT STRUCT, not the camera object.  So the
// stick fields are at (this+0x20)+offset, not at this+offset:
//
//     1DB6EC  ldp  s1,s2,[x20,#0x58]  ; raw stick pair        -> input+0x58 / +0x5C
//     1DB704  str  s1,[x20,#0x60]     ; processed horizontal
//     1DB730  str  s1,[x20,#0x68]     ; processed vertical
//
// That is the injection point.  We write mouse-derived values into the RAW pair
// at input+0x58 / input+0x5C, before the game's own processing runs, so the whole
// stock pipeline still applies: deadzone-free scaling, camera lag, collision,
// pitch limits, lock-on, and every other camera mode.
//
// Mouse source
// ------------
// Eden's emulated HID mouse (Controls.mouse_enabled = true) publishes real host
// mouse deltas once the guest issues hid cmd 21.  hidInitialize()/mouse are
// called LAZILY on the first camera frame - not at module init, which is what
// wedged the game in earlier builds.
//
// Conversion: displacement-based, not velocity-based.  Mouse counts accumulated
// since the previous camera update become the stick value for exactly one update,
// then the accumulator is cleared.  So rotation ~ mouse distance moved, with no
// deadzone, no speed cap and no acceleration curve.

#include <switch.h>
#include <lib.hpp>

#include <lib/util/sys/mem_layout.hpp>
#include <lib/util/sys/modules.hpp>

namespace {

    using ChaseExecuteFn = void (*)(void* self, void* arg);
    ChaseExecuteFn g_Orig = nullptr;

    u64   g_Calls = 0;
    bool  g_HidReady = false;
    float g_AccumX = 0.0f;     // mouse counts pending, X
    float g_AccumY = 0.0f;     // mouse counts pending, Y
    u64   g_LastSample = 0;

    constexpr float kCountsToStick = 0.004f;   // mouse counts -> stick units
    constexpr float kMaxStick      = 4.0f;     // sanity bound per update (not a speed cap)

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

    // Lazily bring up HID + mouse. Safe here: we are on the game's thread inside
    // its own camera update, long after boot.
    void EnsureHid() {
        if (g_HidReady) {
            return;
        }
        g_HidReady = true;              // only try once, never retry-storm
        const Result rc = hidInitialize();
        Logging.Log("NativeMouse: nm17 hidInitialize rc=0x%X", rc);
        if (R_SUCCEEDED(rc)) {
            hidInitializeMouse();
            Logging.Log("NativeMouse: nm17 mouse activated");
        }
    }

    // Drain every pending HID mouse sample into the accumulator.
    void PumpMouse() {
        HidMouseState states[16];
        const size_t n = hidGetMouseStates(states, 16);
        for (size_t i = 0; i < n && i < 16; i++) {
            if (states[i].sampling_number <= g_LastSample) {
                continue;
            }
            g_LastSample = states[i].sampling_number;
            g_AccumX += static_cast<float>(states[i].delta_x);
            g_AccumY += static_cast<float>(states[i].delta_y);
        }
    }

    void ChaseExecuteHook(void* self, void* arg) {
        g_Calls++;

        EnsureHid();
        PumpMouse();

        // this->inputStruct
        void* input = nullptr;
        __builtin_memcpy(&input, static_cast<const u8*>(self) + 0x20, sizeof(input));

        if (input != nullptr) {
            // Consume the accumulator: this is what makes the mapping
            // displacement-based rather than velocity-based.
            const float dx = Clamp(g_AccumX * kCountsToStick, -kMaxStick, kMaxStick);
            const float dy = Clamp(g_AccumY * kCountsToStick, -kMaxStick, kMaxStick);
            g_AccumX = 0.0f;
            g_AccumY = 0.0f;

            const bool moved = (dx != 0.0f) || (dy != 0.0f);

            if (moved) {
                // Write the RAW stick pair the game reads at 0x1DB6EC.
                StoreF(input, 0x58, dx);
                StoreF(input, 0x5C, dy);
            }

            if (g_Calls == 1 || (g_Calls % 240) == 0) {
                Logging.Log("NativeMouse: nm17 CHASE #%llu input=%p raw=%.4f,%.4f proc=%.4f,%.4f "
                            "s420=%.4f s424=%.4f mouse=%.1f,%.1f",
                            static_cast<unsigned long long>(g_Calls), input,
                            LoadF(input, 0x58), LoadF(input, 0x5C),
                            LoadF(input, 0x60), LoadF(input, 0x68),
                            LoadF(input, 0x420), LoadF(input, 0x424),
                            dx, dy);
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

    Logging.Log("NativeMouse: nm17 STEP1 entered");
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
        Logging.Log("NativeMouse: nm17 no signature match - idle");
        return;
    }

    const uintptr_t entry = hit - 0x4C;
    g_Orig = reinterpret_cast<ChaseExecuteFn>(
        exl::hook::Hook(entry, reinterpret_cast<uintptr_t>(&ChaseExecuteHook), true));

    Logging.Log("NativeMouse: nm17 chase entry=0x%llX orig=%p READY",
                static_cast<unsigned long long>(entry),
                reinterpret_cast<void*>(g_Orig));
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
