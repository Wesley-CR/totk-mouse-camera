/**
 * @file tls.h
 * @brief Compatibility shim - forwards to libnx.
 *
 * exlaunch bundled a private copy of libnx's arm/tls.h, which redefines
 * armGetTls() as a static inline function.  Current libnx provides the same
 * function, so both visible at once is a redefinition error.
 */
#pragma once

#include <switch/arm/tls.h>
