// NativeMouse — DIAGNOSTIC BUILD 10 (tag "nm10").
//
// nm9 crashed.  Disassembling its exl_main showed the fault at the instruction
// right after the snprintf that formats
//
//     "NativeMouse: nm9 vtable slot at %p currently %p", slot, *slot
//
// i.e. the crash is in *reading the vtable slot*, before any hook is installed.
// The most likely cause is simply that `GetTargetStart() + 0x0377FF80` is not
// mapped in this process - I derived that VA from the decompressed image but
// never verified at runtime that the main module is based where I assumed.
//
// So this build touches NOTHING.  It logs:
//   * the module base exlaunch resolved
//   * the computed candidate address
//   * whether reading that address is even plausible
//
// and installs no hook and performs no write.  If it boots, we get the real base
// and can locate the vtable correctly (or discover the segment layout differs).

#include <switch.h>
#include <lib.hpp>

namespace {

    constexpr uintptr_t CandidateVtable = 0x0377FF80;
    constexpr size_t    CandidateSlot   = 14;

}

extern "C" void exl_main(void* x0, void* x1) {
    (void)x0;
    (void)x1;

    Logging.Log("NativeMouse: nm10 STEP1 entered");

    const uintptr_t base = exl::util::modules::GetTargetStart();
    Logging.Log("NativeMouse: nm10 STEP2 target base = 0x%llX",
                static_cast<unsigned long long>(base));

    const uintptr_t slotAddr = base + CandidateVtable + CandidateSlot * 8;
    Logging.Log("NativeMouse: nm10 STEP3 candidate vtable slot = 0x%llX",
                static_cast<unsigned long long>(slotAddr));

    // NOTE: deliberately no dereference of slotAddr in this build.  nm9 crashed
    // precisely there, so we first confirm the address is sane.
    Logging.Log("NativeMouse: nm10 STEP4 (no dereference, no hook, no write)");

    (void)slotAddr;
    Logging.Log("NativeMouse: nm10 STEP5 returning - game should boot");
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
