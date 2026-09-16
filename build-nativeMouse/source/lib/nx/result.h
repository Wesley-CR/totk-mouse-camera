/**
 * @file result.h
 * @brief Compatibility shim.
 *
 * exlaunch bundled a private copy of libnx's result.h.  It duplicates libnx's
 * `R_*` macros and the Libnx*Error enums, so having both visible is a hard
 * compile error ("conflicting declaration '<unnamed enum> LibnxNvidiaError_*'").
 *
 * Forward to libnx.  The R_* macros and error enums exlaunch relies on all come
 * from there; exlaunch's own result codes live in exl::result (lib/result.hpp)
 * and are unaffected.
 */
#pragma once

#include <switch/result.h>
