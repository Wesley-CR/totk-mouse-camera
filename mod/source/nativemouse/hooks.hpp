// Game hook table for TOTK 1.4.2.
//
// Offsets are relative to the `main` module, the same model UltraCam uses
// (exlaunch has no pattern scanning built in, so per-version hardcoded offsets
// are the norm for this title).  A signature override is also supported so the
// mod can become build independent once a reliable pattern is known.

#pragma once

#include <common.hpp>
#include <lib.hpp>

namespace nm::hooks {

    //! Offset of the right-stick consumer inside the `main` module.
    //!
    //! This is the point where TOTK's camera reads the right stick for the
    //! current update.  Everything upstream of it is input, everything
    //! downstream is camera AI (lag, collision, pitch limits, lock-on), so
    //! substituting the stick value here preserves the whole stock pipeline.
    //!
    //! Signature to look for once the address is confirmed:
    //!   - reads two adjacent floats from the pad/controller state
    //!   - scales them by the CameraChase `latStickScale` / `lngStickScale`
    //!     AIDef parameters (defaults 1.0 / 0.7)
    //!   - feeds the result into the camera yaw/pitch integration
    //!
    //! 0 means "not yet derived" — the mod then runs in diagnostic mode.
    constexpr ptrdiff_t RightStickConsumer = 0;

    //! Optional byte pattern (with '?' nibble wildcards) used to locate
    //! `RightStickConsumer` at runtime instead of trusting the constant above.
    //! Empty disables the scan. Must match exactly once, otherwise it is
    //! rejected so a wrong pattern can never patch the wrong function.
    constexpr const char* RightStickConsumerSignature = "";

    constexpr bool HasRightStickConsumer = RightStickConsumer != 0;

    //! True when this build is the TOTK 1.4.2 we validated.
    bool IsSupportedVersion();

}
