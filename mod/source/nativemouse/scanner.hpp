// Signature scanner for locating TOTK functions at runtime.
//
// Why this exists
// ---------------
// exlaunch itself has no pattern scanning: the usual approach for this title is
// a hardcoded offset per game version.  That is fine, but it means the mod can
// only ever run on the exact build the offset was derived from, and any new
// game revision needs a fresh reverse-engineering pass.
//
// The camera function this mod needs has not been pinned to a single address
// yet.  Rather than stop there, the mod can search for it by byte signature
// inside the `main` module at load time.  Two consequences:
//
//   * once a signature is known, the mod becomes build independent, which is
//     strictly better than a hardcoded offset;
//   * today, with no signature yet, the scanner is inert and costs nothing.
//
// It stays deliberately conservative: a signature that matches zero times or
// more than once is rejected, so a bad pattern can never patch the wrong
// function.

#pragma once

#include <common.hpp>

namespace nm {

    class Scanner {
        public:
            //! Restricts the scan to the `main` module's code segment.
            void Initialize();

            //! True when the module range was resolved and the scan is usable.
            bool IsReady() const { return m_Start != 0 && m_End > m_Start; }

            //! Scan for a byte pattern.
            //!
            //! `pattern` may contain '?' wildcards, which match any nibble, e.g.
            //! "1F 20 03 D5 ?? ?? ?? 94". Returns 0 unless the pattern matches
            //! exactly once.
            uintptr_t FindUnique(const char* pattern) const;

            //! First match, or 0. Use only when duplicates are genuinely fine.
            uintptr_t FindFirst(const char* pattern) const;

            uintptr_t Start() const { return m_Start; }
            uintptr_t End() const { return m_End; }

        private:
            uintptr_t Find(const char* pattern, bool require_unique) const;

            uintptr_t m_Start = 0;
            uintptr_t m_End = 0;
    };

}
