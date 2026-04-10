// test_when.cpp — TDD tests for WhenGuard and WHEN macro (Step 7, spec §6)
//
// Tests:
//  1. Classical false → body not run.
//  2. Classical true  → body run, current_control == nullptr inside.
//  3. Superposed qbool → body run, current_control != nullptr, points to flag;
//     after scope current_control == nullptr again, qubit allocated.
//  4. Nesting limitation documented via comment (not tested at runtime).
//
// NOTE: Passing a non-qbool to WHEN is a compile-time error enforced by
// static_assert inside make_when_guard.  Manual verification only — no
// runtime test for that case.

#include "sturm/control/when.hpp"

#include "sturm/core/recording_sink.hpp"

#include <cassert>
#include <cstdio>

using namespace sturm;
using namespace sturm::detail;

// ── helpers ──────────────────────────────────────────────────────────────────

static RecordingSink g_rec;

// Install recording sink for a test, restore afterwards.
struct SinkScope {
    Sink* prev;
    explicit SinkScope(Sink* s) : prev(current_sink()) { set_current_sink(s); }
    ~SinkScope() { set_current_sink(prev); }
};

// ── Test 1: Classical false → body skipped ───────────────────────────────────

static void test_classical_false() {
    int counter = 0;
    qbool flag(false);
    WHEN(flag) {
        ++counter;
    }
    assert(counter == 0 && "body must not execute when condition is classically false");
    // current_control must remain nullptr after the guard
    assert(current_control == nullptr);
    std::puts("PASS: test_classical_false");
}

// ── Test 2: Classical true → body runs, current_control unchanged ────────────

static void test_classical_true() {
    int counter = 0;
    sturm::qbool* ctrl_inside = reinterpret_cast<sturm::qbool*>(0x1); // sentinel
    qbool flag(true);
    WHEN(flag) {
        ++counter;
        ctrl_inside = current_control;
    }
    assert(counter == 1 && "body must execute when condition is classically true");
    assert(ctrl_inside == nullptr && "current_control must remain nullptr for classical true");
    assert(current_control == nullptr && "current_control must still be nullptr after scope");
    std::puts("PASS: test_classical_true");
}

// ── Test 3: Superposed → body runs, current_control points to flag ───────────

static void test_superposed() {
    SinkScope ss(&g_rec);
    g_rec.clear();

    QubitPool::instance().reset_for_testing();

    int counter = 0;
    sturm::qbool* ctrl_inside = nullptr;
    int qubit_inside = -2;

    qbool flag(0.5);  // superposed, qubit allocated, prepare() emitted
    assert(flag.super_mask & 1);
    assert(flag.qubits[0] >= 0 && "qubit must be allocated after superposed ctor");

    WHEN(flag) {
        ++counter;
        ctrl_inside = current_control;
        qubit_inside = (ctrl_inside != nullptr) ? ctrl_inside->qubits[0] : -2;
    }

    assert(counter == 1 && "body must execute for superposed flag");
    assert(ctrl_inside == &flag && "current_control must point to flag inside WHEN body");
    assert(qubit_inside == flag.qubits[0] && "qubit index must match flag's qubit");
    assert(current_control == nullptr && "current_control must be restored after scope");

    std::puts("PASS: test_superposed");
}

// ── Test 4: current_control restored after scope even with early return ───────

static void helper_early_return(qbool& flag) {
    WHEN(flag) {
        return; // early exit from the body — destructor must still run
    }
}

static void test_restore_after_early_return() {
    SinkScope ss(&g_rec);
    g_rec.clear();

    qbool flag(0.5);
    assert(current_control == nullptr);
    helper_early_return(flag);
    assert(current_control == nullptr && "current_control must be restored even after early return");
    std::puts("PASS: test_restore_after_early_return");
}

// ── Test 5: Nested WHEN — outer control not clobbered by inner ───────────────
// NOTE: Full nested control (AND-fold) is deferred to the backend stage.
// This test only checks that the TLS is restored correctly at each level,
// not that the quantum semantics are correct.

static void test_nested_when_restores() {
    SinkScope ss(&g_rec);
    g_rec.clear();

    qbool outer(0.5);
    qbool inner(0.5);

    sturm::qbool* outer_ctrl_before_inner = nullptr;
    sturm::qbool* inner_ctrl = nullptr;

    WHEN(outer) {
        outer_ctrl_before_inner = current_control; // should be &outer
        WHEN(inner) {
            inner_ctrl = current_control; // should be &inner (last set)
        }
        // After inner scope, current_control should be restored to &outer
        assert(current_control == &outer &&
               "current_control must be restored to outer after inner WHEN scope");
    }
    assert(current_control == nullptr &&
           "current_control must be nullptr after outermost WHEN scope");

    (void)outer_ctrl_before_inner;
    (void)inner_ctrl;

    std::puts("PASS: test_nested_when_restores");
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    test_classical_false();
    test_classical_true();
    test_superposed();
    test_restore_after_early_return();
    test_nested_when_restores();
    std::puts("All test_when tests passed.");
    return 0;
}
