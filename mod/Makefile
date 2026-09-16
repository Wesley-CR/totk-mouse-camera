# NativeMouse build wrapper.
#
# Uses the exlaunch framework (expected next to this mod as ../re/exlaunch, or
# pointed at by EXLAUNCH_DIR) and devkitPro's devkitA64 toolchain.
#
#   make            build
#   make clean      clean
#
# Output: $(OUT)/$(BINARY_NAME)  -> deploy into the game's exefs as `subsdk1`.

EXLAUNCH_DIR ?= ../re/exlaunch

ifeq ($(wildcard $(EXLAUNCH_DIR)/Makefile),)
$(error exlaunch not found at $(EXLAUNCH_DIR). Set EXLAUNCH_DIR=<path to exlaunch>)
endif

# Reuse exlaunch wholesale, but redirect its source directory at ours and fix
# the hardcoded subsdk9 name to the slot we actually want.
EXL_SOURCE_OVERRIDE := $(CURDIR)/source
EXL_BINARY_OVERRIDE := subsdk1

include $(EXLAUNCH_DIR)/Makefile
