#include "config.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nm {

    namespace {

        constexpr const char* ConfigPath = "sdmc:/NativeMouse.ini";

        char* SkipSpace(char* p) {
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            return p;
        }

        void TrimTrailing(char* p) {
            size_t n = std::strlen(p);
            while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == '\r' ||
                             p[n - 1] == ' '  || p[n - 1] == '\t')) {
                p[--n] = '\0';
            }
        }

        // POSIX strcasecmp is not in namespace std and may not be declared at all
        // on this toolchain, so roll a tiny ASCII case-insensitive compare.
        bool EqualsIgnoreCase(const char* a, const char* b) {
            for (; *a != '\0' && *b != '\0'; a++, b++) {
                char ca = *a;
                char cb = *b;
                if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
                if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
                if (ca != cb) {
                    return false;
                }
            }
            return *a == '\0' && *b == '\0';
        }

        bool ParseBool(const char* v, bool fallback) {
            if (EqualsIgnoreCase(v, "true") || !std::strcmp(v, "1") || EqualsIgnoreCase(v, "yes")) {
                return true;
            }
            if (EqualsIgnoreCase(v, "false") || !std::strcmp(v, "0") || EqualsIgnoreCase(v, "no")) {
                return false;
            }
            return fallback;
        }

        void Apply(Config& c, const char* key, const char* value) {
            if (EqualsIgnoreCase(key, "Enabled")) {
                c.Enabled       = ParseBool(value, c.Enabled);
            } else if (EqualsIgnoreCase(key, "SensitivityX")) {
                c.SensitivityX  = std::strtof(value, nullptr);
            } else if (EqualsIgnoreCase(key, "SensitivityY")) {
                c.SensitivityY  = std::strtof(value, nullptr);
            } else if (EqualsIgnoreCase(key, "InvertY")) {
                c.InvertY       = ParseBool(value, c.InvertY);
            } else if (EqualsIgnoreCase(key, "AimMultiplier")) {
                c.AimMultiplier = std::strtof(value, nullptr);
            } else if (EqualsIgnoreCase(key, "Smoothing")) {
                c.Smoothing     = std::strtof(value, nullptr);
            } else if (EqualsIgnoreCase(key, "DebugLog")) {
                c.DebugLog      = ParseBool(value, c.DebugLog);
            }
        }

    }

    void LoadConfig(Config& out) {
        std::FILE* f = std::fopen(ConfigPath, "r");
        if (f == nullptr) {
            return;
        }

        char line[512];
        while (std::fgets(line, sizeof(line), f) != nullptr) {
            char* p = SkipSpace(line);
            if (*p == ';' || *p == '#' || *p == '[' || *p == '\0') {
                continue;
            }

            char* eq = std::strchr(p, '=');
            if (eq == nullptr) {
                continue;
            }
            *eq = '\0';

            char* key = p;
            char* value = SkipSpace(eq + 1);
            TrimTrailing(key);
            TrimTrailing(value);

            // strip an inline comment
            if (char* sc = std::strchr(value, ';')) {
                *sc = '\0';
                TrimTrailing(value);
            }

            Apply(out, key, value);
        }

        std::fclose(f);
    }

}
