// Keeps libnx's switch_crt0.o out of this subsdk module.
//
// exlaunch links no libnx archive (misc/mk/common.mk leaves LIBS/LIBDIRS
// empty), so every real libnx .c function stayed an unresolved import and
// died in rtld at first call (nm22: "Unresolved symbol: 'threadCreate'").
// We link -lnx on the make command line. The archive member switch_crt0.o
// cannot link --shared (non-PIC relocations) and is pulled in only to
// satisfy __nx_exit, referenced by env.o/init.o. Defining it here keeps
// crt0 out. It never runs: this module never takes libnx exit paths.

#include <switch.h>

void __nx_exit(int rc) {
    (void)rc;
    while (true) {
        svcExitThread();
    }
}
