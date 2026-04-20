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
    run_user_routine_tests();
    run_output_class_tests();
    run_scope_kind_tests();
    run_reader_count_tests();
    run_loop_invariant_tests();
    run_hoist_invariant_tests();
    run_ccnot_fuse_tests();
    run_dead_ancilla_tests();
    // PM2-2: Phase E compound `#line` directive emission.
    run_pm2_compound_line_tests();
    // PM2-3: M8 uncompute synthesis `#line` directive emission.
    run_pm2_uncompute_line_tests();
    // PM2-4: QReplacement `#line` prefix for ccnot-fuse + WHEN-lift.
    run_pm2_replacement_line_tests();
    // PM2-5: hoist #line attribution policy — hoisted op emits #line
    // pointing at the ORIGINAL in-loop stmt_range.getBegin(), not the
    // hoist_to_override target.
    run_pm2_hoist_line_tests();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
