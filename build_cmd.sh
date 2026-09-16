export DEVKITPRO=/c/devkitPro
export DEVKITA64=/c/devkitPro/devkitA64
export PATH=/c/devkitPro/devkitA64/bin:/c/devkitPro/tools/bin:/usr/bin:$PATH
cd <repo-root>/build-nativeMouse
make BINARY_NAME=subsdk1 APP_JSON=<repo-root>/build-nativeMouse/module.json LIBS=-lnx LIBDIRS=/c/devkitPro/libnx 2>&1 | tail -60
