// NativeMouse — mouse acquisition for TOTK 1.4.2 under Eden.
//
// The host mouse reaches the guest through Eden's emulated HID mouse
// (`Controls.mouse_enabled = true`).  Eden only publishes that data once the
// guest has issued `hid` command 21 (ActivateMouse); until then it zeroes the
// mouse LIFO every frame.  libnx's hidInitializeMouse() is exactly that command,
// so we call it once and then poll hidGetMouseStates().
//
// Layout note: Eden's MouseState is 0x28 bytes and matches libnx's HidMouseState
// field-for-field for everything we use (sampling_number, x, y, delta_x,
// delta_y, buttons, attributes).

#pragma once

#include <switch.h>
#include <common.hpp>

namespace nm {

    struct MouseDelta {
        float dx = 0.0f;
        float dy = 0.0f;
        float wheel = 0.0f;
        u32 buttons = 0;
        bool connected = false;
        bool valid = false;
    };

    class MouseInput {
        public:
            // Issues ActivateMouse. Safe to call more than once.
            void Initialize();

            // Drains every pending sample and integrates the deltas.
            // Returns the accumulated motion since the previous call.
            MouseDelta Consume();

            bool IsConnected() const { return m_Connected; }
            bool IsActivated() const { return m_Activated; }

            // Total samples observed, for diagnostics.
            u64 SampleCount() const { return m_SampleCount; }

        private:
            bool m_Activated = false;
            bool m_Connected = false;
            u64 m_LastSamplingNumber = 0;
            u64 m_SampleCount = 0;
    };

}
