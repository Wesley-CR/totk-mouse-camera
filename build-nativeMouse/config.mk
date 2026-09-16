# exlaunch configuration for NativeMouse.

# A `subsdk` module injected into the game's exefs.
LOAD_KIND := Module

# Tears of the Kingdom.
PROGRAM_ID := 0100F2C0115B6000

# Optional path to copy the final ELF to.
ELF_EXTRACT :=

# Python 3 for the build scripts.
PYTHON := python

# Program metadata (copied from mod/config/module.json).
NPDM_JSON := module.json

# Release-ish flags.
#
# exlaunch's own common.mk compiles with -Werror, and:
#   * hook_impl.cpp uses an anonymous `typedef struct { ... } context;` that
#     GCC 16 rejects with -Werror=non-c-typedef-for-linkage;
#   * -Wno-format-zero-length is restated because C_FLAGS is overridden and
#     exlaunch sets it in its base flags.
#
# The typedef warning is C++-only, so it must NOT reach the C compiler (cc1
# errors on the unknown option).  C_FLAGS is applied to both C and C++; put the
# C++-only flag in CXX_FLAGS.
C_FLAGS := -Wno-format-zero-length
CXX_FLAGS := -Wno-non-c-typedef-for-linkage -Wno-format-zero-length

# Unused for Module builds, but exlaunch's Makefile parses these sections.
MOUNT_PATH := /mnt/k
FTP_IP := 127.0.0.1
FTP_PORT := 5000
FTP_USERNAME := anonymous
FTP_PASSWORD :=
RYU_PATH := /mnt/c/Users/none/AppData/Roaming/Ryujinx
