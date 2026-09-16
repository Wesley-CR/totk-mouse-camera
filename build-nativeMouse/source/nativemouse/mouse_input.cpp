#include "mouse_input.hpp"

#include <cstring>

namespace nm {

    void MouseInput::Initialize() {
        if (m_Activated) {
            return;
        }

        // Eden: hid cmd 21 ActivateMouse. Without this the emulated HID mouse
        // LIFO is zeroed each frame and no host mouse data is ever visible.
        hidInitializeMouse();
        m_Activated = true;
    }

    MouseDelta MouseInput::Consume() {
        MouseDelta out{};

        if (!m_Activated) {
            Initialize();
        }

        constexpr size_t MaxStates = 16;
        HidMouseState states[MaxStates];

        const size_t count = hidGetMouseStates(states, MaxStates);
        if (count == 0) {
            return out;
        }

        for (size_t i = 0; i < count && i < MaxStates; i++) {
            const HidMouseState& s = states[i];

            out.connected |= (s.attributes & HidMouseAttribute_IsConnected) != 0;

            // Ignore anything we have already integrated; Eden advances
            // sampling_number once per HID tick.
            if (s.sampling_number <= m_LastSamplingNumber) {
                continue;
            }
            m_LastSamplingNumber = s.sampling_number;
            m_SampleCount++;

            out.dx += static_cast<float>(s.delta_x);
            out.dy += static_cast<float>(s.delta_y);

            // Wheel field-order landmine, verified against both sides.
            //
            // libnx `HidMouseState` declares { wheel_delta_x, wheel_delta_y }.
            // Eden's `Core::HID::MouseState` declares { delta_wheel_y,
            // delta_wheel_x } with the comment "Axis Order in HW is switched
            // for the wheel", but its *writer* stores the host wheel Y first:
            //     next_state.delta_wheel_y = wheel.y - last.y;
            //     next_state.delta_wheel_x = wheel.x - last.x;
            // (hid_core/resources/mouse/mouse.cpp:54-55).  So at byte offset
            // 0x18 libnx's `wheel_delta_y` lands on Eden's `delta_wheel_y`,
            // which really does hold the host wheel's Y delta.  Reading
            // `wheel_delta_y` is therefore correct under Eden.
            out.wheel += static_cast<float>(s.wheel_delta_y);
            out.buttons = s.buttons;
            out.valid = true;
        }

        m_Connected = out.connected;
        return out;
    }

}
