/**
 * @file smc.h
 * @brief Wrappers for secure monitor calls.
 * @copyright libnx Authors
 *
 * exlaunch bundles a stale, private copy of libnx's spl config enums here.  The
 * libnx shipped with current devkitPro defines the same names, so including both
 * collides ("conflicting declaration 'SplConfigItem'").  exlaunch's own source
 * root is on the include path, so this file shadows libnx's.
 *
 * Fix: take the enums from libnx itself and keep only the SMC prototypes that
 * exlaunch actually calls (`smcGetConfig` for the SOC type, plus the reboot and
 * IRAM helpers).  The enum values are identical, so behaviour is unchanged.
 */
#pragma once

#include <switch/types.h>
#include <switch/services/spl.h>

/* libnx's spl.h defines SplConfigItem but NOT SplHardwareType, which exlaunch's
 * soc.hpp needs.  Keep just that one enum here. */
typedef enum {
    SplHardwareType_Icosa  = 0,
    SplHardwareType_Copper = 1,
    SplHardwareType_Hoag   = 2,
    SplHardwareType_Iowa   = 3,
    SplHardwareType_Calcio = 4,
    SplHardwareType_Aula   = 5,
} SplHardwareType;

Result smcGetConfig(SplConfigItem config_item, u64 *out_config);

void smcRebootToRcm(void);
void smcRebootToIramPayload(void);
void smcPerformShutdown(void);

Result smcCopyToIram(uintptr_t iram_addr, const void *src_addr, u32 size);
Result smcCopyFromIram(void *dst_addr, uintptr_t iram_addr, u32 size);

Result smcReadWriteRegister(u32 phys_addr, u32 value, u32 mask);

Result smcGenerateRandomBytes(void *dst, u32 size);
Result smcGenerateRandomU64(u64* out);
