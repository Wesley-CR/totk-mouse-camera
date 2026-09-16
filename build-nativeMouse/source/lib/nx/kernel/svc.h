/**
 * @file svc.h
 * @brief Compatibility shim.
 *
 * exlaunch bundled a private copy of libnx's kernel/svc.h here.  It has drifted
 * from current libnx and the two now declare the same functions with different
 * signatures (`svcGetProcessList`, `svcGetProcessInfo`, `svcGetDebugEvent`,
 * `svcCallSecureMonitor`, ...), which is a hard compile error whenever both are
 * visible.
 *
 * Forward to libnx's own header instead of maintaining a fork.  exlaunch's
 * sources use the svc* wrappers, all of which libnx provides.
 */
#pragma once

#include <switch/kernel/svc.h>
