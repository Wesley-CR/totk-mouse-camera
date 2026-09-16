/**
 * @file types.h
 * @brief Compatibility shim.
 *
 * exlaunch historically bundled a private copy of libnx's types.h here.  That
 * copy has drifted from the libnx shipped with current devkitPro, so the two
 * disagree (most visibly on `Result`), and because exlaunch puts its own source
 * root on the include path this file shadows the real one.
 *
 * Rather than maintain a fork, forward to libnx's types.h and re-add the handful
 * of convenience macros exlaunch's own sources rely on (current libnx defines
 * neither PACKED nor NORETURN globally).
 */
#pragma once

#include <switch/types.h>

#include <stdalign.h>

#ifndef PACKED
#define PACKED     __attribute__((packed))
#endif

#ifndef NORETURN
#define NORETURN   __attribute__((noreturn))
#endif

#ifndef IGNORE_ARG
#define IGNORE_ARG(x) (void)(x)
#endif

#ifndef DEPRECATED
#define DEPRECATED __attribute__((deprecated))
#endif

#ifndef NX_INLINE
#define NX_INLINE __attribute__((always_inline)) static inline
#endif

#ifndef NX_CONSTEXPR
#define NX_CONSTEXPR NX_INLINE constexpr
#endif
