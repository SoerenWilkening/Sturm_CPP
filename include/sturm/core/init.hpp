// init.hpp — M4: CLI + mode init helper.
//
// Provides:
//   sturm::init_from_args(argc, argv)
//       Parses --mode=count|append|simulate from argv; calls set_mode() and
//       then std::abort() on an unrecognised mode value.  If --mode is absent
//       the active context is set to COUNT_ONLY (the default).
//
//   sturm::init_from_args_result(argc, argv)
//       Non-aborting variant for unit tests: returns true on success, false if
//       an unrecognised mode value was given.  Does not call std::abort().
//
// NOTE: This header is C++ only (includes context.hpp).

#pragma once

#include "sturm/core/core.h"       // sturm_mode_t
#include "sturm/core/context.hpp"  // set_mode / get_current_mode

namespace sturm {

// ── init_from_args_result ─────────────────────────────────────────────────────
//
// Parse --mode=<value> from argv.  On success sets the thread context mode and
// returns true.  On an unrecognised value, does NOT abort; returns false.
//
// If no --mode argument is present, sets COUNT_ONLY and returns true.

inline bool init_from_args_result(int argc, char** argv) {
    sturm_mode_t chosen = STURM_MODE_COUNT_ONLY;
    bool bad = false;

    // Lambda: byte-equal NUL-terminated string comparison (no <cstring> needed).
    auto str_eq = [](const char* a, const char* b) -> bool {
        while (*a && *b) { if (*a != *b) return false; ++a; ++b; }
        return *a == *b;
    };

    static const char prefix[] = "--mode=";
    constexpr int prefix_len = 7; // strlen("--mode=")

    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;

        // Check prefix "--mode="
        bool has_prefix = true;
        for (int k = 0; k < prefix_len; ++k) {
            if (argv[i][k] != prefix[k]) { has_prefix = false; break; }
        }
        if (!has_prefix) continue;

        const char* value = argv[i] + prefix_len;

        if      (str_eq(value, "count"))    chosen = STURM_MODE_COUNT_ONLY;
        else if (str_eq(value, "append"))   chosen = STURM_MODE_APPEND;
        else if (str_eq(value, "simulate")) chosen = STURM_MODE_SIMULATE;
        else                                { bad = true; break; }

        break; // first --mode= wins
    }

    if (bad) return false;

    // Apply the chosen mode (or the default COUNT_ONLY if no --mode was given).
    set_mode(chosen);
    return true;
}

// ── Out-of-line abort helper (defined in init.cpp) ───────────────────────────
//
// Prints a stable error message to stderr and calls std::abort().
// Declared here so init_from_args (inline below) can call it without
// pulling <cstdio> / <cstdlib> into every includer via the inline body.

[[noreturn]] void sturm_init_abort_invalid_mode();

// ── init_from_args ────────────────────────────────────────────────────────────
//
// Same as init_from_args_result but aborts the process with a stable message
// on an invalid mode value.

inline void init_from_args(int argc, char** argv) {
    if (!init_from_args_result(argc, argv)) {
        sturm_init_abort_invalid_mode();
    }
}

} // namespace sturm
