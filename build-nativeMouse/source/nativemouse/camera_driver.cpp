#include "camera_driver.hpp"

namespace nm {

    namespace {

        //! Mouse counts -> stick units, before the user's sensitivity.
        //!
        //! The game already scales the stick by its own in-game camera
        //! sensitivity and by the CameraChase lat/lng stick scales, so this is
        //! only the bridge between "pixels moved" and "stick deflection".  Kept
        //! deliberately small so both the user's SensitivityX/Y and TOTK's own
        //! camera sensitivity remain meaningful.
        constexpr float CountsToStick = 0.004f;

        //! Hard clamp so one enormous mouse jump cannot ask for an absurd
        //! rotation in a single camera update.  This is a sanity bound, not a
        //! speed cap: sustained movement is never limited.
        constexpr float MaxStickPerUpdate = 4.0f;

        //! Sampler period. Eden advances the HID mouse LIFO at 125 Hz (8 ms) and
        //! it holds 17 entries, so 2 ms guarantees we drain it well before it can
        //! wrap even if a frame stalls.
        constexpr u64 SamplerPeriodNs = 2'000'000ULL;

        float Clamp(float v, float lo, float hi) {
            return v < lo ? lo : (v > hi ? hi : v);
        }

    }

    void CameraDriver::Initialize(const Config& config) {
        m_Config  = config;
        m_Enabled = config.Enabled;
        m_PendingX = m_PendingY = 0.0f;
        m_SmoothedX = m_SmoothedY = 0.0f;
        mutexInit(&m_Lock);

        // Bring up the guest-visible mouse. Until ActivateMouse has been issued
        // Eden zeroes the HID mouse LIFO every frame.
        m_Mouse.Initialize();

        if (!m_Enabled) {
            return;
        }

        m_Running = true;
        if (R_SUCCEEDED(threadCreate(&m_Thread, &CameraDriver::SamplerEntry, this,
                                      nullptr, 0x4000, 0x2C, -2))) {
            threadStart(&m_Thread);
        } else {
            m_Running = false;
        }
    }

    void CameraDriver::SamplerEntry(void* arg) {
        static_cast<CameraDriver*>(arg)->SamplerLoop();
    }

    void CameraDriver::SamplerLoop() {
        while (m_Running) {
            const MouseDelta delta = m_Mouse.Consume();
            if (delta.valid) {
                float dx = delta.dx * m_Config.SensitivityX * CountsToStick;
                float dy = delta.dy * m_Config.SensitivityY * CountsToStick;
                if (m_Config.InvertY) {
                    dy = -dy;
                }

                mutexLock(&m_Lock);
                m_PendingX += dx;
                m_PendingY += dy;
                m_SampledCount++;
                mutexUnlock(&m_Lock);
            }
            svcSleepThread(SamplerPeriodNs);
        }
    }

    bool CameraDriver::Apply(float& stick_x, float& stick_y) {
        if (!m_Enabled) {
            return false;
        }

        mutexLock(&m_Lock);
        const float pending_x = m_PendingX;
        const float pending_y = m_PendingY;
        // Consume: the next update starts from zero. This is what makes the
        // mapping displacement based instead of velocity based.
        m_PendingX = 0.0f;
        m_PendingY = 0.0f;
        mutexUnlock(&m_Lock);

        // Nothing moved since the last update: leave the pad alone so a real
        // stick (if one is connected) keeps working.
        if (pending_x == 0.0f && pending_y == 0.0f) {
            m_SmoothedX = m_SmoothedY = 0.0f;
            return false;
        }

        float x = Clamp(pending_x, -MaxStickPerUpdate, MaxStickPerUpdate);
        float y = Clamp(pending_y, -MaxStickPerUpdate, MaxStickPerUpdate);

        if (m_Config.Smoothing > 0.0f) {
            const float a = Clamp(m_Config.Smoothing, 0.0f, 0.99f);
            m_SmoothedX = m_SmoothedX * a + x * (1.0f - a);
            m_SmoothedY = m_SmoothedY * a + y * (1.0f - a);
            x = m_SmoothedX;
            y = m_SmoothedY;
        }

        stick_x = x;
        stick_y = y;

        m_AppliedCount++;
        return true;
    }

}
