#include "scanner.hpp"

#include <lib.hpp>

namespace nm {

    namespace {

        //! Parsed pattern byte with a mask, so nibble wildcards work.
        struct PatternByte {
            u8 value;
            u8 mask;
        };

        constexpr size_t MaxPatternBytes = 64;

        size_t ParsePattern(const char* pattern, PatternByte out[MaxPatternBytes]) {
            size_t n = 0;
            const char* p = pattern;

            while (*p != '\0' && n < MaxPatternBytes) {
                while (*p == ' ') {
                    p++;
                }
                if (*p == '\0') {
                    break;
                }

                u8 value = 0;
                u8 mask = 0;

                // high nibble
                if (*p == '?') {
                    p++;
                } else {
                    const char c = *p++;
                    const int d = (c >= '0' && c <= '9') ? c - '0'
                                : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                                : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                : -1;
                    if (d < 0) {
                        return 0;
                    }
                    value |= static_cast<u8>(d << 4);
                    mask |= 0xF0;
                }

                // low nibble
                if (*p == '?') {
                    p++;
                } else if (*p != '\0' && *p != ' ') {
                    const char c = *p++;
                    const int d = (c >= '0' && c <= '9') ? c - '0'
                                : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                                : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                : -1;
                    if (d < 0) {
                        return 0;
                    }
                    value |= static_cast<u8>(d);
                    mask |= 0x0F;
                }

                out[n].value = value;
                out[n].mask = mask;
                n++;
            }

            return n;
        }

    }

    void Scanner::Initialize() {
        // The mod is itself mapped inside the process, but we want the game's
        // `main` module. exlaunch resolves that for hooking, so we reuse it.
        const uintptr_t base = exl::util::modules::GetTargetStart();
        if (base == 0) {
            return;
        }

        // TOTK 1.4.2 layout, verified from the NSO header:
        //   .text   offset 0x0      size 0x2BA61F0
        //   .rodata offset 0x2BA7000
        // The code we want to match lives in .text, so bounding the scan to that
        // segment keeps it fast and avoids matching data.
        constexpr uintptr_t TextSize142 = 0x2BA61F0;

        m_Start = base;
        m_End = base + TextSize142;
    }

    uintptr_t Scanner::Find(const char* pattern, bool require_unique) const {
        if (!IsReady()) {
            return 0;
        }

        PatternByte bytes[MaxPatternBytes];
        const size_t count = ParsePattern(pattern, bytes);
        if (count == 0) {
            return 0;
        }

        uintptr_t found = 0;
        const u8* const begin = reinterpret_cast<const u8*>(m_Start);
        const u8* const end = reinterpret_cast<const u8*>(m_End) - count;

        for (const u8* p = begin; p <= end; p++) {
            bool match = true;
            for (size_t i = 0; i < count; i++) {
                if ((p[i] & bytes[i].mask) != (bytes[i].value & bytes[i].mask)) {
                    match = false;
                    break;
                }
            }
            if (!match) {
                continue;
            }

            if (!require_unique) {
                return reinterpret_cast<uintptr_t>(p);
            }

            if (found != 0) {
                // more than one match: ambiguous, refuse
                return 0;
            }
            found = reinterpret_cast<uintptr_t>(p);
        }

        return found;
    }

    uintptr_t Scanner::FindUnique(const char* pattern) const {
        return Find(pattern, true);
    }

    uintptr_t Scanner::FindFirst(const char* pattern) const {
        return Find(pattern, false);
    }

}
