// test_control_stack_push_pop.cpp — M12 + sturm-a3t4.6:
//   ControlStack push/pop/depth/top tests under the depth-1 invariant.
//
// sturm-a3t4.6 (P5 promote depth-1 invariant to runtime assert):
//   push_control() now asserts depth_ == 0u — pushing while a control is
//   already on the stack is a programmer error. Library code must compose
//   nested controls via outer & flag + WHEN, never by raw stacking. These
//   tests exercise the surviving legal shapes (depth 0 → 1, then 1 → 0).
//
// Tests:
//   1. push 1 control, verify depth and top
//   2. pop returns to empty (depth == 0)
//   3. controls() span contains the single pushed qubit
//   4. push/pop cycle can be repeated (depth bounces 0 ↔ 1)
//
// Pushing a second control on top of an existing one violates the depth-1
// invariant and is covered separately by the `<<DEATH>>` regression in
// tests/control/depth_invariant_test.cpp (sturm-a3t4.7).

#include "sturm/core/control_stack.hpp"

#include <cassert>
#include <cstdint>

// ── Test 1: push 1 control, verify depth and top ─────────────────────────────

static void test_push_depth_top() {
    sturm::ControlStack cs;

    assert(cs.depth() == 0u);

    cs.push_control(10u);
    assert(cs.depth() == 1u);
    assert(cs.top() == 10u);

    cs.pop_control();
    assert(cs.depth() == 0u);
}

// ── Test 2: pop returns to empty ─────────────────────────────────────────────

static void test_pop_returns_empty() {
    sturm::ControlStack cs;

    cs.push_control(5u);
    cs.pop_control();

    assert(cs.depth() == 0u);
}

// ── Test 3: controls() span matches the single pushed qubit ──────────────────

static void test_controls_span() {
    sturm::ControlStack cs;

    cs.push_control(1u);

    auto span = cs.controls();
    assert(span.size() == 1u);
    assert(span[0] == 1u);

    // After pop, span shrinks to empty.
    cs.pop_control();
    auto span2 = cs.controls();
    assert(span2.size() == 0u);
}

// ── Test 4: push/pop cycle repeats (depth bounces 0 ↔ 1) ─────────────────────

static void test_push_pop_cycle() {
    sturm::ControlStack cs;

    for (uint32_t i = 0u; i < 4u; ++i) {
        cs.push_control(100u + i);
        assert(cs.depth() == 1u);
        assert(cs.top() == 100u + i);
        cs.pop_control();
        assert(cs.depth() == 0u);
    }
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    test_push_depth_top();
    test_pop_returns_empty();
    test_controls_span();
    test_push_pop_cycle();
    return 0;
}
