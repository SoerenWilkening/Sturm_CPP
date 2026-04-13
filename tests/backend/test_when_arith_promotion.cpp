// test_when_arith_promotion.cpp — Verify dispatch.hpp classical fast-path
// behavior with and without WHEN scope.
//
// After M16 revert, dispatch.hpp no longer does full-mask WHEN promotion.
// Classical operands inside WHEN take the fast path (remain classical) at
// the dispatch level; per-bit promotion is handled by BitProxy in the
// backend path instead.
//
// Remaining tests:
//   1. No WHEN + classical add      -> fast path preserved (super_mask == 0)
//   2. WHEN + classical operands    -> fast path still taken (dispatch no longer promotes)
//   3. Result value correctness (fast path)
//   4. Original operands unchanged
//
// Harness: plain assert + printf (no gtest).

#include "sturm/core/dispatch.hpp"
#include "sturm/control/when_fwd.hpp"
#include "sturm/qtypes/qbool.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <array>
#include <vector>

// -- Minimal qint-shaped POD --------------------------------------------------
struct FakeQint4 {
    static constexpr int kWidth = 4;

    int64_t  value      = 0;
    uint64_t super_mask = 0;
    std::array<int, kWidth> qubits{};

    FakeQint4() { qubits.fill(-1); }
    explicit FakeQint4(int64_t v) : value(v) { qubits.fill(-1); }

    std::vector<int> qubits_vec() const {
        std::vector<int> out;
        for (int q : qubits) {
            if (q >= 0) out.push_back(q);
        }
        return out;
    }
};

// -- Helpers ------------------------------------------------------------------

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

// -- Test 1: No WHEN + classical add -> fast path preserved -------------------

static void test_no_when_classical_fast_path() {
    reset_pool();

    FakeQint4 a(3), b(2);
    // No WHEN active (current_control == nullptr by default after reset).

    auto classical_fn = [](int64_t x, int64_t y) { return x + y; };
    auto mask_fn      = [](uint64_t ma, uint64_t mb) { return ma | mb; };
    auto sink_fn = [](const FakeQint4&, const FakeQint4&,
                      const FakeQint4&, int) {
        assert(false && "sink must NOT be called on classical fast path");
    };

    FakeQint4 result = sturm::detail::dispatch_binary<FakeQint4>(
        a, b, classical_fn, mask_fn, sink_fn);

    assert(result.super_mask == 0 && "classical result must stay classical without WHEN");
    assert(result.value == 5);

    std::puts("PASS: test_no_when_classical_fast_path");
}

// -- Test 2: WHEN + classical operands -> fast path still taken ---------------
// After M16 revert, dispatch.hpp does NOT promote classical operands inside
// WHEN. The fast path returns a classical result. Per-bit promotion is
// handled by BitProxy in the backend path, not by dispatch.

static void test_when_classical_takes_fast_path() {
    reset_pool();

    FakeQint4 a(3), b(2);

    sturm::qbool flag(0.5);
    FakeWhenGuard guard(flag);

    auto classical_fn = [](int64_t x, int64_t y) { return x + y; };
    auto mask_fn      = [](uint64_t ma, uint64_t mb) { return ma | mb; };
    auto sink_fn = [](const FakeQint4&, const FakeQint4&,
                      const FakeQint4&, int) {
        assert(false && "sink must NOT be called — dispatch fast path applies even inside WHEN");
    };

    FakeQint4 result = sturm::detail::dispatch_binary<FakeQint4>(
        a, b, classical_fn, mask_fn, sink_fn);

    // Dispatch fast path: classical operands stay classical even inside WHEN.
    assert(result.super_mask == 0 && "dispatch must NOT promote classical operands inside WHEN (M16 revert)");
    assert(result.value == 5 && "classical value must be correct");
    // No qubits allocated on result.
    for (int i = 0; i < 4; ++i) {
        assert(result.qubits[i] < 0 && "no qubits should be allocated for classical fast path");
    }

    std::puts("PASS: test_when_classical_takes_fast_path");
}

// -- Test 3: WHEN + classical unary -> fast path still taken ------------------

static void test_when_unary_takes_fast_path() {
    reset_pool();

    FakeQint4 a(5);
    sturm::qbool flag(0.5);
    FakeWhenGuard guard(flag);

    auto classical_fn = [](int64_t x) { return ~x; };
    auto mask_fn      = [](uint64_t ma) { return ma; };  // mask_not: pass through
    auto sink_fn = [](const FakeQint4&, const FakeQint4&, int) {
        assert(false && "unary sink must NOT be called — fast path applies");
    };

    FakeQint4 result = sturm::detail::dispatch_unary<FakeQint4>(
        a, classical_fn, mask_fn, sink_fn);

    assert(result.super_mask == 0 && "dispatch must NOT promote classical unary inside WHEN (M16 revert)");

    std::puts("PASS: test_when_unary_takes_fast_path");
}

// -- Test 4: operand b unchanged after dispatch (copies are used internally) --

static void test_operand_b_unchanged() {
    reset_pool();

    FakeQint4 a(3), b(2);
    // One operand superposed so sink IS called (exercises the copy path).
    a.super_mask = 0x1;
    a.qubits[0] = sturm::QubitPool::instance().allocate();

    auto classical_fn = [](int64_t x, int64_t y) { return x + y; };
    auto mask_fn      = [](uint64_t ma, uint64_t mb) { return ma | mb; };
    auto sink_fn = [](const FakeQint4&, const FakeQint4&,
                      const FakeQint4&, int) {};

    FakeQint4 result = sturm::detail::dispatch_binary<FakeQint4>(
        a, b, classical_fn, mask_fn, sink_fn);

    // The original b must not have its super_mask mutated (dispatch works on copies).
    assert(b.super_mask == 0 && "original operand b must stay classical");
    (void)result;

    std::puts("PASS: test_operand_b_unchanged");
}

// -- main ---------------------------------------------------------------------

int main() {
    test_no_when_classical_fast_path();
    test_when_classical_takes_fast_path();
    test_when_unary_takes_fast_path();
    test_operand_b_unchanged();

    std::puts("\nAll WHEN arith promotion tests passed (post-M16 revert).");
    return 0;
}
