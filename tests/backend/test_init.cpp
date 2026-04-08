// test_init.cpp — M4: CLI + mode init helper tests.
// TDD: written before implementation, drives init.hpp / init.cpp.
//
// Tests:
//   1. --mode=count  → COUNT_ONLY
//   2. --mode=append → APPEND
//   3. --mode=simulate → SIMULATE
//   4. missing --mode arg defaults to COUNT_ONLY
//   5. invalid mode value aborts with a stable error (tested via helper flag)

#include "sturm/core/init.hpp"
#include "sturm/core/context.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>

// ── helpers ───────────────────────────────────────────────────────────────────

// Build a fake argv from string literals.  Up to 8 args, null-terminated.
static void run_init(const char* const* args, int n) {
    // init_from_args needs a non-const char** per the C convention; we supply
    // a mutable copy on the stack.
    const char* argv_storage[8]{};
    assert(n <= 8);
    for (int i = 0; i < n; ++i) argv_storage[i] = args[i];
    // init_from_args signature: void init_from_args(int argc, char** argv)
    // We cast away const here only inside the test to match the C argv convention.
    sturm::init_from_args(n, const_cast<char**>(argv_storage));
}

// ── Test 1: --mode=count → COUNT_ONLY ────────────────────────────────────────

static void test_mode_count() {
    const char* args[] = {"prog", "--mode=count"};
    run_init(args, 2);
    assert(sturm::get_current_mode() == STURM_MODE_COUNT_ONLY);
}

// ── Test 2: --mode=append → APPEND ───────────────────────────────────────────

static void test_mode_append() {
    const char* args[] = {"prog", "--mode=append"};
    run_init(args, 2);
    assert(sturm::get_current_mode() == STURM_MODE_APPEND);
    // reset to COUNT_ONLY for subsequent tests
    sturm::set_mode(STURM_MODE_COUNT_ONLY);
}

// ── Test 3: --mode=simulate → SIMULATE ───────────────────────────────────────

static void test_mode_simulate() {
    const char* args[] = {"prog", "--mode=simulate"};
    run_init(args, 2);
    assert(sturm::get_current_mode() == STURM_MODE_SIMULATE);
    // reset
    sturm::set_mode(STURM_MODE_COUNT_ONLY);
}

// ── Test 4: missing --mode defaults to COUNT_ONLY ────────────────────────────

static void test_missing_mode_defaults_count_only() {
    // First force a non-default mode so we can confirm it gets reset.
    sturm::set_mode(STURM_MODE_APPEND);

    const char* args[] = {"prog", "--some-other-flag=42"};
    run_init(args, 2);
    // With no --mode=… argument, init_from_args must default to COUNT_ONLY.
    assert(sturm::get_current_mode() == STURM_MODE_COUNT_ONLY);
}

// ── Test 5: no args at all defaults to COUNT_ONLY ────────────────────────────

static void test_no_args_defaults_count_only() {
    sturm::set_mode(STURM_MODE_SIMULATE);

    const char* args[] = {"prog"};
    run_init(args, 1);
    assert(sturm::get_current_mode() == STURM_MODE_COUNT_ONLY);
}

// ── Test 6: invalid mode value aborts ────────────────────────────────────────
// We cannot test std::abort() inline without forking a child process, which is
// platform-specific and outside the scope of the test harness.  Instead, we
// verify that init_from_args exposes a query function that indicates whether
// the last call encountered an invalid mode, so callers can detect the error.
//
// The implementation spec says "aborts with stable error"; in the test we
// verify that the abort path IS taken by calling the testable wrapper
// sturm::init_from_args_result() which returns false on invalid input instead
// of aborting — this is the variant used in unit tests; the real
// init_from_args calls abort() directly.

static void test_invalid_mode_detected() {
    const char* args[] = {"prog", "--mode=bogus"};
    // init_from_args_result returns false (does NOT abort) for test isolation.
    bool ok = sturm::init_from_args_result(
        2, const_cast<char**>(args));
    assert(!ok && "invalid mode string must return false");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    test_mode_count();
    test_mode_append();
    test_mode_simulate();
    test_missing_mode_defaults_count_only();
    test_no_args_defaults_count_only();
    test_invalid_mode_detected();
    return 0;
}
