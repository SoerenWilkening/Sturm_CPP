// test_when_shift_promotion.cpp — M13: Shift operators fast-path bypass inside WHEN.
//
// When WHEN(superposed_c) { result = a << n; } is executed with a classical
// operand `a`, the shift operators must bypass the classical fast-path and
// allocate qubits + emit CNOT-based copy gates so that the result is quantum.
//
// Tests all four shift operators: <<, >>, <<=, >>=.
//
// Test cases:
//   1. WHEN + classical operator<< → result has qubits allocated
//   2. WHEN + classical operator>> → result has qubits allocated
//   3. WHEN + classical operator<<= → qubits allocated (not just relabeling)
//   4. WHEN + classical operator>>= → qubits allocated (not just relabeling)
//   5. No WHEN + classical << → fast path preserved (no qubits)
//   6. No WHEN + classical >>= → fast path preserved (no qubits)
//
// Harness: plain assert + printf (no gtest).

#define STURM_BACKEND_ENABLED 1

#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when_fwd.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/backend/primitives.hpp"

#include <cassert>
#include <cstdio>

// ── Helpers ──────────────────────────────────────────────────────────────────

static void reset_pool() {
    sturm::QubitPool::instance().reset_for_testing();
}

// RAII guard: install qbool as current_control, restore on destruction.
struct FakeWhenGuard {
    FakeWhenGuard(sturm::qbool& flag) {
        sturm::detail::current_control       = &flag;
        sturm::detail::current_control_qubit = flag.qubits[0];
    }
    ~FakeWhenGuard() {
        sturm::detail::current_control       = nullptr;
        sturm::detail::current_control_qubit = -1;
    }
};

// ── Test 1: WHEN + classical operator<< → qubits allocated ─────────────────

static void test_when_shl_promotes() {
    reset_pool();

    sturm::qint_t<4> a;
    a.value = 3;
    a.super_mask = 0;
    a.qubits.fill(-1);  // classical

    sturm::qbool flag(0.5);
    FakeWhenGuard guard(flag);

    sturm::qint_t<4> result = a << 1;

    // Inside WHEN, the result must have qubits allocated (not classical fast-path).
    bool has_qubits = false;
    for (int i = 0; i < 4; ++i) {
        if (result.qubits[i] >= 0) has_qubits = true;
    }
    assert(has_qubits && "operator<< inside WHEN must allocate qubits");
    assert(result.value == 6 && "classical value must be correct (3 << 1 == 6)");

    std::puts("PASS: test_when_shl_promotes");
}

// ── Test 2: WHEN + classical operator>> → qubits allocated ─────────────────

static void test_when_shr_promotes() {
    reset_pool();

    sturm::qint_t<4> a;
    a.value = 12;
    a.super_mask = 0;
    a.qubits.fill(-1);

    sturm::qbool flag(0.5);
    FakeWhenGuard guard(flag);

    sturm::qint_t<4> result = a >> 2;

    bool has_qubits = false;
    for (int i = 0; i < 4; ++i) {
        if (result.qubits[i] >= 0) has_qubits = true;
    }
    assert(has_qubits && "operator>> inside WHEN must allocate qubits");
    assert(result.value == 3 && "classical value must be correct (12 >> 2 == 3)");

    std::puts("PASS: test_when_shr_promotes");
}

// ── Test 3: WHEN + classical operator<<= → qubits allocated ────────────────

static void test_when_shl_assign_promotes() {
    reset_pool();

    sturm::qint_t<4> a;
    a.value = 2;
    a.super_mask = 0;
    a.qubits.fill(-1);

    sturm::qbool flag(0.5);
    FakeWhenGuard guard(flag);

    a <<= 1;

    bool has_qubits = false;
    for (int i = 0; i < 4; ++i) {
        if (a.qubits[i] >= 0) has_qubits = true;
    }
    assert(has_qubits && "operator<<= inside WHEN must allocate qubits");
    assert(a.value == 4 && "classical value must be correct (2 <<= 1 == 4)");

    std::puts("PASS: test_when_shl_assign_promotes");
}

// ── Test 4: WHEN + classical operator>>= → qubits allocated ────────────────

static void test_when_shr_assign_promotes() {
    reset_pool();

    sturm::qint_t<4> a;
    a.value = 8;
    a.super_mask = 0;
    a.qubits.fill(-1);

    sturm::qbool flag(0.5);
    FakeWhenGuard guard(flag);

    a >>= 2;

    bool has_qubits = false;
    for (int i = 0; i < 4; ++i) {
        if (a.qubits[i] >= 0) has_qubits = true;
    }
    assert(has_qubits && "operator>>= inside WHEN must allocate qubits");
    assert(a.value == 2 && "classical value must be correct (8 >>= 2 == 2)");

    std::puts("PASS: test_when_shr_assign_promotes");
}

// ── Test 5: No WHEN + classical << → fast path preserved ───────────────────

static void test_no_when_shl_fast_path() {
    reset_pool();

    sturm::qint_t<4> a;
    a.value = 3;
    a.super_mask = 0;
    a.qubits.fill(-1);

    // No WHEN active.
    sturm::qint_t<4> result = a << 1;

    // Must stay classical (no qubits allocated).
    for (int i = 0; i < 4; ++i) {
        assert(result.qubits[i] < 0 && "operator<< without WHEN must not allocate qubits");
    }
    assert(result.value == 6);

    std::puts("PASS: test_no_when_shl_fast_path");
}

// ── Test 6: No WHEN + classical >>= → fast path preserved ─────────────────

static void test_no_when_shr_assign_fast_path() {
    reset_pool();

    sturm::qint_t<4> a;
    a.value = 8;
    a.super_mask = 0;
    a.qubits.fill(-1);

    // No WHEN active.
    a >>= 2;

    // Must stay classical.
    for (int i = 0; i < 4; ++i) {
        assert(a.qubits[i] < 0 && "operator>>= without WHEN must not allocate qubits");
    }
    assert(a.value == 2);

    std::puts("PASS: test_no_when_shr_assign_fast_path");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_when_shl_promotes();
    test_when_shr_promotes();
    test_when_shl_assign_promotes();
    test_when_shr_assign_promotes();
    test_no_when_shl_fast_path();
    test_no_when_shr_assign_fast_path();

    std::puts("\nAll WHEN shift promotion tests passed.");
    return 0;
}
