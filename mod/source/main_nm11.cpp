// NativeMouse — DIAGNOSTIC BUILD 11 (tag "nm11").
//
// nm9 crashed while merely READING the candidate vtable address, so the address
// `GetTargetStart() + 0x0377FF80` is wrong in practice even though the
// arithmetic looked right.  Rather than guess again, this build asks exlaunch
// directly and prints the ground truth: every module it discovered, with its
// name and ranges.
//
// With that we can see
//   * what base exlaunch actually resolved for `main`
//   * where .text / .rodata / .data sit for it
//   * whether some other module is enumerated where we expected `main`
//
// It installs no hook and reads nothing it did not enumerate itself.

#include <switch.h>
#include <lib.hpp>

#include <lib/util/sys/mem_layout.hpp>
#include <lib/util/sys/modules.hpp>

extern "C" void exl_main(void* x0, void* x1) {
    (void)x0;
    (void)x1;

    Logging.Log("NativeMouse: nm11 STEP1 entered");

    const uintptr_t targetStart = exl::util::modules::GetTargetStart();
    Logging.Log("NativeMouse: nm11 STEP2 GetTargetStart = 0x%llX",
                static_cast<unsigned long long>(targetStart));

    using exl::util::ModuleIndex;
    constexpr int End = static_cast<int>(ModuleIndex::End);

    for (int i = 0; i < End; i++) {
        const auto idx = static_cast<ModuleIndex>(i);
        if (!exl::util::HasModule(idx)) {
            continue;
        }

        const auto& info = exl::util::GetModuleInfo(idx);

        char name[64] = {0};
        const auto view = info.GetModuleName();
        const size_t n = view.size() < sizeof(name) - 1 ? view.size() : sizeof(name) - 1;
        for (size_t k = 0; k < n; k++) {
            name[k] = view[k];
        }

        Logging.Log("NativeMouse: nm11 MODULE[%d] '%s' total=0x%llX+0x%llX text=0x%llX+0x%llX ro=0x%llX+0x%llX data=0x%llX+0x%llX",
                    i, name,
                    static_cast<unsigned long long>(info.m_Total.m_Start),
                    static_cast<unsigned long long>(info.m_Total.m_Size),
                    static_cast<unsigned long long>(info.m_Text.m_Start),
                    static_cast<unsigned long long>(info.m_Text.m_Size),
                    static_cast<unsigned long long>(info.m_Rodata.m_Start),
                    static_cast<unsigned long long>(info.m_Rodata.m_Size),
                    static_cast<unsigned long long>(info.m_Data.m_Start),
                    static_cast<unsigned long long>(info.m_Data.m_Size));
    }

    Logging.Log("NativeMouse: nm11 STEP3 done - returning");
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("NativeMouse: unexpected exception");
}
