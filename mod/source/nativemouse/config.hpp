// Minimal INI reader for the mod's own config.
//
// Deliberately self-contained: we parse the handful of keys we need rather than
// pull in a dependency, and we never write back (the file is user-owned).

#pragma once

#include <common.hpp>

namespace nm {

    struct Config {
        bool  Enabled          = true;
        float SensitivityX     = 1.0f;
        float SensitivityY     = 1.0f;
        bool  InvertY          = false;
        float AimMultiplier    = 1.0f;
        float Smoothing        = 0.0f;

        // Diagnostic: log mouse activity to sdmc:/NativeMouse.log.
        bool  DebugLog         = false;
    };

    // Reads sdmc:/NativeMouse.ini. Missing file or keys leave defaults in place.
    void LoadConfig(Config& out);

}
