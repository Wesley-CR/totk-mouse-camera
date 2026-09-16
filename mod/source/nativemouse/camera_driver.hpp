// Routing mouse motion into TOTK's camera.
//
// Design intent
// -------------
// We do NOT write the camera transform.  TOTK's camera is AI driven
// (CameraChase / CameraAiming / CameraLockOn / ...) and owns lag, collision,
// pitch limits and every state transition.  Overwriting the transform would
// throw all of that away.
//
// Instead the mouse produces a synthetic *right-stick sample*, which is handed
// to the game at the same place the real right stick is consumed.  Everything
// downstream is stock TOTK behaviour, so exploration, bow aiming, lock-on and
// the rest keep their intended feel.
//
// Why this is displacement based rather than velocity based: a sampler thread
// drains Eden's HID mouse LIFO into an accumulator, and each camera update takes
// the whole accumulator and *clears* it.  So
//
//     rotation  ~  mouse counts moved since the previous update x sensitivity
//
// A fast flick and a slow drag over the same physical distance rotate the same
// amount, and there is no deadzone, no maximum angular velocity and no
// acceleration curve.
//
// The sampler is needed because Eden advances the mouse LIFO at a fixed 125 Hz
// independently of the frame rate, and the ring only holds 17 entries: at 30 FPS
// an undrained ring would silently drop motion, making fast movement lossy.

#pragma once

#include <common.hpp>
#include <switch.h>

#include "config.hpp"
#include "mouse_input.hpp"

namespace nm {

    class CameraDriver {
        public:
            // Starts the sampler thread and records the configuration.
            void Initialize(const Config& config);

            // Called from the game hook. `stick_x` / `stick_y` are the pad's
            // right-stick values for this update. Returns true if they were
            // replaced with mouse-derived values.
            bool Apply(float& stick_x, float& stick_y);

            //! True once the guest-visible mouse reports itself connected.
            bool MouseConnected() const { return m_Mouse.IsConnected(); }

            // Diagnostics.
            u64 AppliedCount() const { return m_AppliedCount; }
            u64 SampledCount() const { return m_SampledCount; }

        private:
            static void SamplerEntry(void* arg);
            void SamplerLoop();

            Config     m_Config{};
            bool       m_Enabled = false;
            MouseInput m_Mouse{};

            // Motion accumulated since the last camera update, in stick units.
            // Written by the sampler thread, consumed by the game thread.
            float      m_PendingX = 0.0f;
            float      m_PendingY = 0.0f;

            // Smoothed value actually handed to the game (Smoothing > 0 only).
            float      m_SmoothedX = 0.0f;
            float      m_SmoothedY = 0.0f;

            u64        m_AppliedCount = 0;
            u64        m_SampledCount = 0;

            bool       m_Running = false;
            Thread     m_Thread{};
            Mutex      m_Lock{};
    };

}
