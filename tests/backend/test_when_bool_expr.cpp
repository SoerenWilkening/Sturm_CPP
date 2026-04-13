// test_when_bool_expr.cpp — Tests for WHEN(c | d), WHEN(c & d), WHEN(~c).
//
// Verifies that the WHEN macro accepts boolean expressions (temporaries)
// in both classical and superposed scenarios, and that TLS is correctly
// saved/restored including nested cases.
//
// Tests:
//   1-4.  WHEN(c | d) classical: 4 truth-table combinations
//   5-8.  WHEN(c & d) classical: 4 truth-table combinations
//   9.    WHEN(c | d) superposed: body runs, control set, TLS restored
//  10.    WHEN(c & d) superposed: same
//  11.    WHEN(~c) superposed: negation works
//  12.    Nested WHEN(a) { WHEN(b | c) { ... } }: TLS correctly managed
//  13.    Regression: WHEN(plain_qbool) lvalue still works
//
// Harness: plain assert + printf (no gtest).

#include "sturm/control/when.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/qtypes/qint.hpp"

#include <cassert>
#include <cstdio>

using namespace sturm;

// ── ScopedAppendCtx ───────────────────────────────────────────────────────────

struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedAppendCtx() {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, 32u);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

// ── Tests 1-4: WHEN(c | d) classical truth table ────────────────────────────

static void test_or_classical_ff() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;
    int ran = 0;
    qbool c(false), d(false);
    WHEN(c | d) { ++ran; }
    assert(ran == 0 && "WHEN(false | false) must not run body");
    std::puts("PASS: test_or_classical_ff");
}

static void test_or_classical_ft() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;
    int ran = 0;
    qbool c(false), d(true);
    WHEN(c | d) { ++ran; }
    assert(ran == 1 && "WHEN(false | true) must run body");
    std::puts("PASS: test_or_classical_ft");
}

static void test_or_classical_tf() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;
    int ran = 0;
    qbool c(true), d(false);
    WHEN(c | d) { ++ran; }
    assert(ran == 1 && "WHEN(true | false) must run body");
    std::puts("PASS: test_or_classical_tf");
}

static void test_or_classical_tt() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;
    int ran = 0;
    qbool c(true), d(true);
    WHEN(c | d) { ++ran; }
    assert(ran == 1 && "WHEN(true | true) must run body");
    std::puts("PASS: test_or_classical_tt");
}

// ── Tests 5-8: WHEN(c & d) classical truth table ────────────────────────────

static void test_and_classical_ff() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;
    int ran = 0;
    qbool c(false), d(false);
    WHEN(c & d) { ++ran; }
    assert(ran == 0 && "WHEN(false & false) must not run body");
    std::puts("PASS: test_and_classical_ff");
}

static void test_and_classical_ft() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;
    int ran = 0;
    qbool c(false), d(true);
    WHEN(c & d) { ++ran; }
    assert(ran == 0 && "WHEN(false & true) must not run body");
    std::puts("PASS: test_and_classical_ft");
}

static void test_and_classical_tf() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;
    int ran = 0;
    qbool c(true), d(false);
    WHEN(c & d) { ++ran; }
    assert(ran == 0 && "WHEN(true & false) must not run body");
    std::puts("PASS: test_and_classical_tf");
}

static void test_and_classical_tt() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;
    int ran = 0;
    qbool c(true), d(true);
    WHEN(c & d) { ++ran; }
    assert(ran == 1 && "WHEN(true & true) must run body");
    std::puts("PASS: test_and_classical_tt");
}

// ── Test 9: WHEN(c | d) superposed ──────────────────────────────────────────

static void test_or_superposed() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    qbool c(0.5), d(0.5);  // both superposed
    int ran = 0;
    qbool* ctrl_inside = nullptr;

    WHEN(c | d) {
        ++ran;
        ctrl_inside = detail::current_control;
    }

    assert(ran == 1 && "WHEN(super | super) must run body");
    assert(ctrl_inside != nullptr && "current_control must be set inside superposed WHEN");
    assert(detail::current_control == nullptr && "current_control must be restored after WHEN");
    std::puts("PASS: test_or_superposed");
}

// ── Test 10: WHEN(c & d) superposed ─────────────────────────────────────────

static void test_and_superposed() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    qbool c(0.5), d(0.5);
    int ran = 0;
    qbool* ctrl_inside = nullptr;

    WHEN(c & d) {
        ++ran;
        ctrl_inside = detail::current_control;
    }

    assert(ran == 1 && "WHEN(super & super) must run body");
    assert(ctrl_inside != nullptr && "current_control must be set inside superposed WHEN");
    assert(detail::current_control == nullptr && "current_control must be restored after WHEN");
    std::puts("PASS: test_and_superposed");
}

// ── Test 11: WHEN(~c) superposed ────────────────────────────────────────────

static void test_not_superposed() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    qbool c(0.5);
    int ran = 0;
    qbool* ctrl_inside = nullptr;

    WHEN(~c) {
        ++ran;
        ctrl_inside = detail::current_control;
    }

    assert(ran == 1 && "WHEN(~super) must run body");
    assert(ctrl_inside != nullptr && "current_control must be set inside WHEN(~c)");
    assert(detail::current_control == nullptr && "current_control must be restored after WHEN");
    std::puts("PASS: test_not_superposed");
}

// ── Test 12: Nested WHEN(a) { WHEN(b | c) { ... } } ────────────────────────

static void test_nested_when_with_or_expr() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    qbool a(0.5), b(0.5), c(0.5);
    int outer_ran = 0, inner_ran = 0;
    qbool* outer_ctrl = nullptr;
    qbool* inner_ctrl = nullptr;

    WHEN(a) {
        ++outer_ran;
        outer_ctrl = detail::current_control;
        WHEN(b | c) {
            ++inner_ran;
            inner_ctrl = detail::current_control;
        }
        // After inner WHEN, control must be restored to outer's control
        assert(detail::current_control == outer_ctrl &&
               "control must be restored to outer after inner WHEN(b|c)");
    }

    assert(outer_ran == 1 && "outer WHEN body must run");
    assert(inner_ran == 1 && "inner WHEN(b|c) body must run");
    assert(outer_ctrl != nullptr && "outer control must be set");
    assert(inner_ctrl != nullptr && "inner control must be set");
    assert(detail::current_control == nullptr && "control must be nullptr after all WHENs");
    std::puts("PASS: test_nested_when_with_or_expr");
}

// ── Test 13: Regression — plain lvalue WHEN still works ─────────────────────

static void test_lvalue_regression() {
    QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    qbool flag(0.5);
    int ran = 0;
    qbool* ctrl_inside = nullptr;

    WHEN(flag) {
        ++ran;
        ctrl_inside = detail::current_control;
    }

    assert(ran == 1 && "WHEN(lvalue) must still run body");
    assert(ctrl_inside == &flag && "current_control must point to the lvalue qbool");
    assert(detail::current_control == nullptr && "current_control must be restored");
    std::puts("PASS: test_lvalue_regression");
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    test_or_classical_ff();
    test_or_classical_ft();
    test_or_classical_tf();
    test_or_classical_tt();

    test_and_classical_ff();
    test_and_classical_ft();
    test_and_classical_tf();
    test_and_classical_tt();

    test_or_superposed();
    test_and_superposed();
    test_not_superposed();

    test_nested_when_with_or_expr();
    test_lvalue_regression();

    std::puts("\nAll test_when_bool_expr tests passed.");
    return 0;
}
