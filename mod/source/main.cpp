// NativeMouse — entry point.
//
// Brings up the guest-visible mouse, samples it, and routes the motion into
// TOTK's own camera path.  See docs/ for the reverse-engineering notes.

#include "hooks.hpp"
#include "camera_driver.hpp"
#include "config.hpp"
#include "scanner.hpp"

#include <cstdio>
#include <cstdarg>

namespace nm {

    // The hook body and the entry point both need these.
    namespace detail {

        Config       g_Config;
        CameraDriver g_Camera;
        std::FILE*   g_Log = nullptr;

        void Log(const char* fmt, ...) {
            if (g_Log == nullptr) {
                return;
            }
            va_list args;
            va_start(args, fmt);
            std::vfprintf(g_Log, fmt, args);
            va_end(args);
            std::fputc('\n', g_Log);
            std::fflush(g_Log);
        }

    }
}

namespace {

    using nm::detail::g_Camera;
    using nm::detail::g_Config;
    using nm::detail::Log;

    HOOK_DEFINE_TRAMPOLINE(RightStickHook) {

        // The callback signature must match the target function exactly. It is a
        // pair of floats because the camera consumer takes the stick values by
        // pointer; adjust to the real signature when the address is pinned.
        static void Callback(float* stick_x, float* stick_y) {
            if (stick_x != nullptr && stick_y != nullptr) {
                g_Camera.Apply(*stick_x, *stick_y);
            }
            Orig(stick_x, stick_y);
        }
    };

}

extern "C" void exl_main(void* x0, void* x1) {
    (void)x0;
    (void)x1;

    exl::hook::Initialize();

    nm::LoadConfig(g_Config);

    if (g_Config.DebugLog) {
        nm::detail::g_Log = std::fopen("sdmc:/NativeMouse.log", "w");
        Log("NativeMouse: starting");
    }

    // Refuse to touch a build we have not validated. The build id is read from
    // the loaded module itself, so this is a real check rather than a version
    // string the user could mis-set.
    if (!nm::hooks::IsSupportedVersion()) {
        Log("NativeMouse: unsupported game build - idle");
        return;
    }

    // Brings up the guest-visible mouse (Eden keeps the HID mouse LIFO zeroed
    // until ActivateMouse is issued) and starts the sampler thread.
    g_Camera.Initialize(g_Config);

    if (!g_Config.Enabled) {
        Log("NativeMouse: disabled in config - idle");
        return;
    }

    // Preferred: locate the consumer by signature, which keeps working across
    // game revisions. Fall back to the hardcoded offset for this build.
    nm::Scanner scanner;
    scanner.Initialize();

    uintptr_t target = 0;
    if (nm::hooks::RightStickConsumerSignature[0] != '\0') {
        target = scanner.FindUnique(nm::hooks::RightStickConsumerSignature);
        if (target == 0) {
            Log("NativeMouse: signature did not match exactly once - refusing to patch");
        } else {
            Log("NativeMouse: signature matched at main+0x%llX",
                static_cast<unsigned long long>(target - exl::util::modules::GetTargetStart()));
        }
    }

    if (target == 0 && nm::hooks::HasRightStickConsumer) {
        target = exl::util::modules::GetTargetOffset(nm::hooks::RightStickConsumer);
    }

    if (target != 0) {
        RightStickHook::InstallAtPtr(target);
        Log("NativeMouse: camera hook installed");
    } else {
        // Diagnostic build: no camera hook yet. The mod still proves out the
        // mouse path (ActivateMouse + sampling) and reports what it sees, which
        // is the prerequisite for everything else.
        Log("NativeMouse: no camera hook resolved (diagnostic mode)");
    }
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
