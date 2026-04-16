// test_matcher.cpp — matcher unit-test dispatcher.
//
// The per-phase test bodies live in sibling .cpp modules (see
// test_matcher_harness.hpp for the full list). This binary just drives
// each `run_*_tests()` in a fixed order and reports the combined
// pass/fail counts the CHECK macros accumulate.

#include "test_matcher_harness.hpp"

#include <cstdio>

int main() {
    run_mvp_tests();
    run_qint_const_tests();
    run_qint_qint_tests();
    run_qint_compare_tests();
    run_when_lift_tests();
    run_when_nested_tests();
    run_ph1_scope_tests();
    run_ph2_brace_wrap_tests();
    run_ph3_outer_var_tests();
    run_routine_registry_tests();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
