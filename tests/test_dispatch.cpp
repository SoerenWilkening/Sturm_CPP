// test_dispatch.cpp — Tests for dispatch_binary/unary/compare/shift helpers
// Step 5, spec §4, Implementation Plan §5
//
// Strategy: hand-construct fake qint-shaped POD structs and call dispatch
// templates directly with lambdas. This avoids depending on qint_t (Step 6).

#include "sturm/core/dispatch.hpp"
#include "sturm/control/when_fwd.hpp"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/counter_sink.hpp"
#include "sturm/qtypes/qbool.hpp"

#include <cassert>
#include <cstdint>
#include <array>
#include <vector>

// ── Minimal qint-shaped POD for dispatch tests ────────────────────────────────
// Mirrors the fields expected by dispatch helpers without requiring qint_t.
struct FakeQint {
    static constexpr int kWidth = 8;  // use small width for tests

    int64_t  value      = 0;
    uint64_t super_mask = 0;
    std::array<int, kWidth> qubits{};

    FakeQint() { qubits.fill(-1); }
    explicit FakeQint(int64_t v) : value(v) { qubits.fill(-1); }

    // Helper expected by dispatch helpers: collect allocated qubit indices.
    std::vector<int> qubits_vec() const {
        std::vector<int> out;
        for (int q : qubits) {
            if (q >= 0) out.push_back(q);
        }
        return out;
    }
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static void reset_pool() {
    sturm::QubitPool::instance().reset_for_testing();
}

// ── Test: fast path (new_mask == 0, no sink call) ─────────────────────────────
static void test_binary_fast_path() {
    reset_pool();
    sturm::RecordingSink rs;
    sturm::ScopedSink scope(&rs);

    FakeQint a(3), b(5);
    // Both classical (super_mask == 0).

    auto classical_fn = [](int64_t x, int64_t y) { return x + y; };
    auto mask_fn      = [](uint64_t ma, uint64_t mb) { return ma | mb; };
    auto sink_fn = [](const FakeQint&, const FakeQint&,
                      const FakeQint&, int) {
        // Should never be called on fast path.
        assert(false && "sink_fn called on fast path");
    };

    FakeQint result = sturm::detail::dispatch_binary<FakeQint>(
        a, b, classical_fn, mask_fn, sink_fn);

    assert(result.value == 8);
    assert(result.super_mask == 0);
    assert(rs.records().empty());  // no sink call
}

// ── Test: allocation path (super_mask set → qubits materialise) ───────────────
static void test_binary_allocation_path() {
    reset_pool();
    sturm::RecordingSink rs;
    sturm::ScopedSink scope(&rs);

    FakeQint a(1), b(2);
    a.super_mask = 0x1;  // bit 0 is superposed in a
    b.super_mask = 0x2;  // bit 1 is superposed in b

    bool sink_called = false;
    auto classical_fn = [](int64_t x, int64_t y) { return x + y; };
    // mask_addsub-style: any super bit → fill upward from lowest
    auto mask_fn = [](uint64_t ma, uint64_t mb) -> uint64_t {
        uint64_t m = ma | mb;
        if (m == 0) return 0;
        int lowest = 0;
        uint64_t tmp = m;
        while ((tmp & 1ULL) == 0) { ++lowest; tmp >>= 1; }
        return (m | (~0ULL << lowest)) & 0xFFULL;  // clamp to 8 bits
    };
    auto sink_fn = [&sink_called](const FakeQint& fa, const FakeQint& fb,
                                  const FakeQint& /*out*/, int ctrl) {
        sink_called = true;
        assert(ctrl == -1);  // no active WHEN
        // a has qubit at bit 0, b has qubit at bit 1
        auto av = fa.qubits_vec();
        auto bv = fb.qubits_vec();
        assert(!av.empty());
        assert(!bv.empty());
    };

    FakeQint result = sturm::detail::dispatch_binary<FakeQint>(
        a, b, classical_fn, mask_fn, sink_fn);

    assert(sink_called);
    assert(result.super_mask != 0);
    assert(result.value == 3);
    // Result qubits should be allocated at each set bit of new_mask.
    for (int i = 0; i < FakeQint::kWidth; ++i) {
        if (result.super_mask & (1ULL << i)) {
            assert(result.qubits[i] >= 0);
        }
    }
}

// ── Test: control routing (TLS current_control set → control arg forwarded) ───
static void test_binary_control_routing() {
    reset_pool();
    sturm::RecordingSink rs;
    sturm::ScopedSink scope(&rs);

    FakeQint a(7), b(3);
    a.super_mask = 0x1;

    // Manually install a qbool as current_control.
    sturm::qbool flag(0.5);  // allocates qubit, sets is_super=true
    sturm::detail::current_control = &flag;

    int seen_ctrl = -999;
    auto classical_fn = [](int64_t x, int64_t y) { return x + y; };
    auto mask_fn      = [](uint64_t ma, uint64_t mb) { return ma | mb; };
    auto sink_fn = [&seen_ctrl](const FakeQint&, const FakeQint&,
                                const FakeQint&, int ctrl) {
        seen_ctrl = ctrl;
    };

    FakeQint result = sturm::detail::dispatch_binary<FakeQint>(
        a, b, classical_fn, mask_fn, sink_fn);

    // Restore TLS.
    sturm::detail::current_control = nullptr;

    assert(seen_ctrl == flag.qubits[0]);
    assert(seen_ctrl >= 0);
    (void)result;
}

// ── Test: dispatch_unary ──────────────────────────────────────────────────────
static void test_unary() {
    reset_pool();
    sturm::RecordingSink rs;
    sturm::ScopedSink scope(&rs);

    FakeQint a(5);
    a.super_mask = 0x3;  // bits 0 and 1 are superposed

    bool sink_called = false;
    auto classical_fn = [](int64_t x) { return ~x; };
    auto mask_fn      = [](uint64_t ma) { return ma; };  // mask_not
    auto sink_fn = [&sink_called](const FakeQint&, const FakeQint& /*out*/,
                                  int ctrl) {
        sink_called = true;
        assert(ctrl == -1);
    };

    FakeQint result = sturm::detail::dispatch_unary<FakeQint>(
        a, classical_fn, mask_fn, sink_fn);

    assert(sink_called);
    assert(result.super_mask == 0x3);
}

// ── Test: dispatch_unary fast path ────────────────────────────────────────────
static void test_unary_fast_path() {
    reset_pool();
    sturm::RecordingSink rs;
    sturm::ScopedSink scope(&rs);

    FakeQint a(42);
    // super_mask == 0 → fast path

    auto classical_fn = [](int64_t x) { return -x; };
    auto mask_fn      = [](uint64_t ma) { return ma; };
    auto sink_fn = [](const FakeQint&, const FakeQint&, int) {
        assert(false && "sink called on fast path");
    };

    FakeQint result = sturm::detail::dispatch_unary<FakeQint>(
        a, classical_fn, mask_fn, sink_fn);

    assert(result.value == -42);
    assert(result.super_mask == 0);
    assert(rs.records().empty());
}

// ── Test: dispatch_compare → qbool ───────────────────────────────────────────
static void test_compare() {
    reset_pool();
    sturm::RecordingSink rs;
    sturm::ScopedSink scope(&rs);

    FakeQint a(3), b(5);
    a.super_mask = 0x1;  // result must be superposed

    bool sink_called = false;
    auto classical_fn = [](int64_t x, int64_t y) { return x == y; };
    auto mask_fn      = [](uint64_t ma, uint64_t mb) { return ma | mb; };
    auto sink_fn = [&sink_called](const FakeQint& fa, const FakeQint& fb,
                                  sturm::qbool& out, int ctrl) {
        sink_called = true;
        assert(out.qubits[0] >= 0);   // qubit allocated for result
        assert(ctrl == -1);
        (void)fa; (void)fb;
    };

    sturm::qbool result = sturm::detail::dispatch_compare<FakeQint>(
        a, b, classical_fn, mask_fn, sink_fn);

    assert(sink_called);
    assert(result.is_super);
    assert(result.qubits[0] >= 0);
}

// ── Test: dispatch_compare fast path (both classical) ────────────────────────
static void test_compare_fast_path() {
    reset_pool();
    sturm::RecordingSink rs;
    sturm::ScopedSink scope(&rs);

    FakeQint a(7), b(7);
    // Both classical → fast path

    auto classical_fn = [](int64_t x, int64_t y) { return x == y; };
    auto mask_fn      = [](uint64_t ma, uint64_t mb) { return ma | mb; };
    auto sink_fn = [](const FakeQint&, const FakeQint&,
                      sturm::qbool&, int) {
        assert(false && "sink called on fast path");
    };

    sturm::qbool result = sturm::detail::dispatch_compare<FakeQint>(
        a, b, classical_fn, mask_fn, sink_fn);

    assert(!result.is_super);
    assert(result.value == true);
    assert(rs.records().empty());
}

// ── Test: dispatch_shift ──────────────────────────────────────────────────────
static void test_shift() {
    reset_pool();
    sturm::RecordingSink rs;
    sturm::ScopedSink scope(&rs);

    FakeQint a(1);
    a.super_mask = 0x1;  // bit 0 superposed
    int shift_amount = 2;

    bool sink_called = false;
    auto classical_fn = [](int64_t x, int n) { return x << n; };
    auto mask_fn = [](uint64_t ma, int n) -> uint64_t {
        if (n <= 0) return ma;
        if (n >= 64) return 0;
        return (ma << n) & 0xFFULL;
    };
    auto sink_fn = [&sink_called](const FakeQint&, const FakeQint& /*out*/,
                                  int n, int ctrl) {
        sink_called = true;
        assert(n == 2);
        assert(ctrl == -1);
    };

    FakeQint result = sturm::detail::dispatch_shift<FakeQint>(
        a, shift_amount, classical_fn, mask_fn, sink_fn);

    assert(sink_called);
    assert(result.super_mask == 0x4);  // bit 0 shifted left 2 → bit 2
}

// ── Test: dispatch_shift fast path ───────────────────────────────────────────
static void test_shift_fast_path() {
    reset_pool();
    sturm::RecordingSink rs;
    sturm::ScopedSink scope(&rs);

    FakeQint a(8);
    // classical → fast path
    auto classical_fn = [](int64_t x, int n) { return x >> n; };
    auto mask_fn = [](uint64_t ma, int n) -> uint64_t {
        if (n >= 64) return 0;
        return ma >> n;
    };
    auto sink_fn = [](const FakeQint&, const FakeQint&, int, int) {
        assert(false && "sink called on fast path");
    };

    FakeQint result = sturm::detail::dispatch_shift<FakeQint>(
        a, 1, classical_fn, mask_fn, sink_fn);

    assert(result.value == 4);
    assert(result.super_mask == 0);
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    test_binary_fast_path();
    test_binary_allocation_path();
    test_binary_control_routing();
    test_unary();
    test_unary_fast_path();
    test_compare();
    test_compare_fast_path();
    test_shift();
    test_shift_fast_path();
    return 0;
}
