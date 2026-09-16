# exlaunch configuration for NativeMouse.
#
# Loaded by the exlaunch Makefile via `include config.mk`.

# A `subsdk` module injected into the game's exefs.
LOAD_KIND := Module

# Tears of the Kingdom.
PROGRAM_ID := 0100F2C0115B6000

# Python 3 for the build scripts.
PYTHON := python

# Program metadata template (from exlaunch/misc/npdm-json).
NPDM_JSON := module.json

# Extra flags. NDEBUG keeps the log macros quiet in release builds.
C_FLAGS := -O2
CXX_FLAGS := -O2 -std=c++20

# ---------------------------------------------------------------------------
# Slot selection
# ---------------------------------------------------------------------------
# exlaunch's Makefile hardcodes BINARY_NAME := subsdk9 for LOAD_KIND=Module.
# Eden loads any of subsdk0..subsdk9 that it finds, and UltraCam already owns
# subsdk3, so this mod uses subsdk1. The main Makefile overrides BINARY_NAME
# after including this file.
NATIVEMOUSE_SUBSDK := subsdk1
