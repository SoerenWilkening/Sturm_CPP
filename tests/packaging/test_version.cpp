// tests/packaging/test_version.cpp — sturm-mjr2.1 / E4.M1 acceptance test.
//
// Pins the contract for include/sturm/version.hpp (generated from
// include/sturm/version.hpp.in via configure_file from the top-level
// `project(... VERSION x.y.z)` declaration). Cf. PRD §3.8 / Plan §E4.M1.
//
// We assert two things:
//   1. The macros exist and are usable in constant expressions
//      (static_assert) — i.e. downstream code can do
//      `#if STURM_VERSION_MAJOR >= 1` and the preprocessor sees a
//      defined integer literal.
//   2. The string macro matches CMake's ${PROJECT_VERSION} at the time
//      this test was configured. CMake feeds the expected value via
//      -DEXPECTED_VERSION="x.y.z" so the test is bytewise pinned to the
//      same source-of-truth as the generated header (no risk of two
//      copies of the version drifting against each other).

#include <sturm/version.hpp>

#include <cstring>

// (1) Compile-time: the major macro must be a defined integer literal.
static_assert(STURM_VERSION_MAJOR >= 0,
              "STURM_VERSION_MAJOR must be a defined non-negative integer");
static_assert(STURM_VERSION_MINOR >= 0,
              "STURM_VERSION_MINOR must be a defined non-negative integer");
static_assert(STURM_VERSION_PATCH >= 0,
              "STURM_VERSION_PATCH must be a defined non-negative integer");

#ifndef EXPECTED_VERSION
#  error "EXPECTED_VERSION must be defined by the build system (via -D) " \
         "to the same value as CMake's ${PROJECT_VERSION}."
#endif

int main() {
    // (2) Runtime: STURM_VERSION_STRING is bytewise equal to the version
    // CMake fed in for this test. Any drift between the template's
    // @PROJECT_VERSION@ expansion and the configure-time value would
    // surface here.
    if (std::strcmp(STURM_VERSION_STRING, EXPECTED_VERSION) != 0) {
        return 1;
    }
    return 0;
}
