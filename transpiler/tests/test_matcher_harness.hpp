// test_matcher_harness.hpp — shared pieces for the matcher test modules.
//
// The matcher unit test used to be a single ~2k-line binary; it now fans
// out across one .cpp per phase group. This header exposes:
//   - CHECK / CHECK_EQ_STR macros and the global tests_run / tests_pass
//     counters the macros bump.
//   - kQBoolStub / kQBoolWhenStub — inline C++ stubs used by more than
//     one module (MVP + PH-1 for kQBoolStub; PF + PG + PH-1 for
//     kQBoolWhenStub).
//   - run_or_matcher — the MVP OR-matcher runner, reused by PH-1's
//     braceless-body tests.
//   - run_*_tests() entry points that the thin dispatcher in
//     test_matcher.cpp calls in sequence.

#pragma once

#include "sturm/transpile/qir.hpp"

#include <cstdio>
#include <string>
#include <string_view>

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while (0)

#define CHECK_EQ_STR(got, want) do {                                  \
    ++tests_run;                                                      \
    if ((got) == (want)) { ++tests_pass; }                            \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  strings differ\n"          \
                             "  got:  <<<%s>>>\n"                     \
                             "  want: <<<%s>>>\n",                    \
                     __FILE__, __LINE__,                              \
                     std::string(got).c_str(),                        \
                     std::string(want).c_str());                      \
    }                                                                 \
} while (0)

extern int tests_run;
extern int tests_pass;

extern const std::string_view kQBoolStub;
extern const std::string_view kQBoolWhenStub;

sturm::transpile::QUnit run_or_matcher(std::string_view user_src);

void run_mvp_tests();
void run_qint_const_tests();
void run_qint_qint_tests();
void run_qint_compare_tests();
void run_when_lift_tests();
void run_when_nested_tests();
void run_ph1_scope_tests();
void run_ph2_brace_wrap_tests();
void run_ph3_outer_var_tests();
void run_routine_registry_tests();
void run_user_routine_tests();
void run_output_class_tests();
void run_scope_kind_tests();
void run_reader_count_tests();
void run_loop_invariant_tests();
