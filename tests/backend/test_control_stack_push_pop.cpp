// test_control_stack_push_pop.cpp — M12: ControlStack push/pop/depth/top tests.
// TDD: written before implementation.
//
// Tests:
//   1. push 3 controls, verify depth and top at each level
//   2. pop all, verify empty (depth == 0)
//   3. controls() span contains all pushed qubits

#include "sturm/core/control_stack.hpp"

#include <cassert>
#include <cstdint>

// ── Test 1: push 3 controls, verify depth and top ────────────────────────────

static void test_push_depth_top() {
    sturm::ControlStack cs;

    assert(cs.depth() == 0u);

    cs.push_control(10u);
    assert(cs.depth() == 1u);
    assert(cs.top() == 10u);

    cs.push_control(20u);
    assert(cs.depth() == 2u);
    assert(cs.top() == 20u);

    cs.push_control(30u);
    assert(cs.depth() == 3u);
    assert(cs.top() == 30u);

    // Pop one: depth drops, top reveals previous entry.
    cs.pop_control();
    assert(cs.depth() == 2u);
    assert(cs.top() == 20u);

    // Pop all.
    cs.pop_control();
    cs.pop_control();
    assert(cs.depth() == 0u);
}

// ── Test 2: pop all, verify empty ────────────────────────────────────────────

static void test_pop_all_empty() {
    sturm::ControlStack cs;

    cs.push_control(5u);
    cs.push_control(6u);
    cs.pop_control();
    cs.pop_control();

    assert(cs.depth() == 0u);
}

// ── Test 3: controls() span matches pushed qubits ────────────────────────────

static void test_controls_span() {
    sturm::ControlStack cs;

    cs.push_control(1u);
    cs.push_control(2u);
    cs.push_control(3u);

    auto span = cs.controls();
    assert(span.size() == 3u);
    assert(span[0] == 1u);
    assert(span[1] == 2u);
    assert(span[2] == 3u);

    // After pop, span shrinks.
    cs.pop_control();
    auto span2 = cs.controls();
    assert(span2.size() == 2u);
    assert(span2[0] == 1u);
    assert(span2[1] == 2u);
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    test_push_depth_top();
    test_pop_all_empty();
    test_controls_span();
    return 0;
}
