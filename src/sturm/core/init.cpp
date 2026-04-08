// init.cpp — M4: CLI + mode init helper (out-of-line implementations).
//
// Defines:
//   sturm::sturm_init_abort_invalid_mode()
//       Writes a stable diagnostic to stderr and calls std::abort().

#include "sturm/core/init.hpp"

#include <cstdio>
#include <cstdlib>

namespace sturm {

[[noreturn]] void sturm_init_abort_invalid_mode() {
    // Stable error message — the exact text is part of the spec contract so
    // that callers (scripts, test harnesses) can match against it.
    ::fputs("STURM: invalid --mode value; expected count, append, or simulate\n",
            stderr);
    ::abort();
}

} // namespace sturm
