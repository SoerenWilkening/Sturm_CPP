// test_end_to_end.cpp — M26: End-to-end acceptance — c = (a + 5) >= 0
//
// Exercises the canonical one-liner from PRD §3 under all three execution modes.
//
//   sturm::qint_t<8> a(input_value);
//   sturm::qbool     c = (a + 5LL) >= sturm::qint_t<8>(0);
//
// Note: uses 5LL (int64_t literal) to trigger the operator+(qint, int64_t) path
// which emits gates via add_const when the operand is quantum (super_mask != 0).
// Using Q(5) (qint+qint) takes the qint-qint path whose uncomputation moved
// to the transpiler in Phase C (no tag is stamped here). This is consistent
// with PRD §3 "c = (a+5) >= 0" semantics where the constant 5 is a
// compile-time integer literal.
//
// Phase D retirement (2026-04-15): the qbool-from-qint comparison no longer
// stamps a COMPARE uncompute tag, so the qbool destructor no longer emits the
// two compare-stub Z gates. The pinned COUNT_ONLY/APPEND sequence therefore
// drops from 18 records to 16. Uncomputation of the compare ancilla is the
// transpiler's responsibility (uncompute_ge_qint).
//
// Three test groups:
//
//   COUNT_ONLY — pin the expected gate count.
//     Classical a (super_mask==0): 0 gates (fast path, no quantum bits).
//     Quantum a (super_mask=0xFF): 16 gates with the current stub decomposition:
//       8 × H (add_const forward) + 8 × X (sub_const uncompute of temp)
//     If stubs are replaced by real circuits the count will change and this assertion
//     fires, prompting the developer to update the pinned value.
//
//   APPEND — build the golden gate sequence with a quantum a (super_mask=0xFF)
//     and verify the IR record-for-record.  Golden sequence (16 records):
//       [0..7]    STURM_GATE_H,  qubit=i (0..7),  param=5.0  (add_const stub)
//       [8..15]   STURM_GATE_X,  qubit=i (0..7),  param=5.0  (sub_const uncompute)
//
//   SIMULATE — sweep a ∈ {-10..10} (classical qint, super_mask==0).
//     For each input assert c.value == (a_val + 5 >= 0).
//     This covers the SIMULATE classical fast path; full quantum simulation is
//     deferred until a real adder/comparator circuit lands.
//
// Harness: plain assert + main (no gtest), matching the project pattern.

#define STURM_BACKEND_ENABLED 1
#define STURM_ANCILLA_CAPACITY 256

#include "sturm/core/core.h"
#include "sturm/core/context.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/qtypes/qint.hpp"

#include <cassert>
#include <cstdio>

// ── Fixture: scoped backend context ──────────────────────────────────────────
// Creates a context with the given mode, installs it as the thread-local
// context, and restores the previous context on destruction.

struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit ScopedCtx(sturm_mode_t mode) {
        ctx  = sturm_backend_create(mode, 17u);
        assert(ctx && "sturm_backend_create must not return null");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }

    ~ScopedCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    uint64_t gate_count() const {
        return sturm_gate_count(ctx);
    }

    const sturm::GateIR& ir() const {
        return ctx->ir;
    }
};

// ── Helper: run c = (a + 5) >= 0 with a fully classical qint value ────────────
// Returns c.value (the classical boolean result).
// No gates are emitted (super_mask==0 skips all stub loops).
// Uses int64_t literal 5LL to trigger operator+(qint, int64_t) path (which
// emits gates via add_const when super_mask != 0).

static bool run_classical(sturm_mode_t mode, int64_t a_val) {
    ScopedCtx sc(mode);
    using Q = sturm::qint_t<8>;
    Q a(a_val);
    sturm::qbool c = (a + 5LL) >= Q(0);
    return c.value;
}

// ── Test 1: COUNT_ONLY — classical operands yield zero gates ──────────────────
//
// With super_mask==0, add_const and sub_const both skip the per-bit loop.
// The compare stubs likewise skip (qbool.is_super==false → super_mask==0
// in as_qint_base()).  Pinned expected count: 0.

static void test_count_only_classical() {
    ScopedCtx sc(STURM_MODE_COUNT_ONLY);

    {
        using Q = sturm::qint_t<8>;
        Q a(3LL);                                    // classical, super_mask==0
        sturm::qbool c = (a + 5LL) >= Q(0);        // no gates (classical fast path)
        (void)c;
    }

    const uint64_t count = sc.gate_count();
    // Pinned: 0 gates for classical operands
    assert(count == 0u
           && "COUNT_ONLY classical: expected gate count == 0 (no quantum bits)");

    std::printf("  test_count_only_classical: PASS (gate count = %llu)\n",
                (unsigned long long)count);
}

// ── Test 2: COUNT_ONLY — quantum operands yield pinned gate count ─────────────
//
// With super_mask=0xFF (8 quantum bits) on qint_t<8>:
//   - add_const emits 8 × H
//   - sub_const (uncompute of (a+5) temporary) emits 8 × X
// Pinned expected count: 16. (Phase D: compare-stub Z gates retired — the
// transpiler now emits uncompute_ge_qint instead.)
//
// NOTE: qubit indices are set manually (bypassing the pool) so the test does not
// consume pool capacity.  We clear qubits/super_mask on `a` before its destructor
// runs to prevent the pool from receiving non-allocated indices.

static void test_count_only_quantum() {
    ScopedCtx sc(STURM_MODE_COUNT_ONLY);

    {
        using Q = sturm::qint_t<8>;
        Q a;
        a.value      = 3LL;
        a.super_mask = 0xFFu;        // all 8 bits quantum
        for (int i = 0; i < 8; ++i) a.qubits[i] = i;

        // Use int64_t literal 5LL to trigger operator+(qint, int64_t) which emits
        // gates via add_const.  Q(5) would take the qint+qint path whose
        // uncomputation moved to the transpiler in Phase C (no stamp at runtime).
        sturm::qbool c = (a + 5LL) >= Q(0);
        (void)c;
        // c and (a+5LL) temporary destroyed here.

        // Guard: prevent a's destructor from releasing non-pool qubits.
        a.qubits.fill(-1);
        a.super_mask = 0;
    }

    const uint64_t count = sc.gate_count();
    // Pinned to current stub decomposition (Phase D retirement):
    //   8 H (add_const) + 8 X (sub_const uncompute) = 16
    static constexpr uint64_t kExpected = 16u;
    assert(count == kExpected
           && "COUNT_ONLY quantum: gate count must equal pinned value 16");

    std::printf("  test_count_only_quantum: PASS (gate count = %llu)\n",
                (unsigned long long)count);
}

// ── Test 3: APPEND — golden gate sequence ─────────────────────────────────────
//
// Verifies the exact gate sequence in APPEND mode.  With super_mask=0xFF on
// an 8-bit register, the IR must contain exactly 16 gate records matching the
// golden sequence described in the file header comment.
// (Phase D retirement: compare-stub Z gates at [16]/[17] removed.)

static void test_append_golden() {
    ScopedCtx sc(STURM_MODE_APPEND);

    {
        using Q = sturm::qint_t<8>;
        Q a;
        a.value      = 3LL;
        a.super_mask = 0xFFu;
        for (int i = 0; i < 8; ++i) a.qubits[i] = i;

        // Use 5LL (int64_t) to trigger operator+(qint, int64_t) → add_const gates.
        sturm::qbool c = (a + 5LL) >= Q(0);
        (void)c;
        // c and (a+5LL) temporary destroyed here.

        // Guard: prevent a's destructor from releasing non-pool qubits.
        a.qubits.fill(-1);
        a.super_mask = 0;
    }

    const auto& ir = sc.ir();

    // Total gate count
    assert(ir.size() == 16u
           && "APPEND golden: IR must contain exactly 16 gate records");

    // Gates [0..7]: H from add_const — one per quantum bit, param = 5.0
    for (std::size_t i = 0; i < 8; ++i) {
        const auto& r = ir.at(i);
        assert(r.kind       == STURM_GATE_H
               && "gate 0..7 must be H (add_const stub)");
        assert(r.qubits[0]  == static_cast<uint32_t>(i)
               && "gate 0..7 must target qubit i");
        assert(r.n          == 1u
               && "H gate arity must be 1");
        assert(r.param      == 5.0
               && "gate 0..7 param must be 5.0 (the constant c)");
    }

    // Gates [8..15]: X from sub_const — uncompute of (a+5) temporary, param = 5.0
    for (std::size_t i = 0; i < 8; ++i) {
        const auto& r = ir.at(8 + i);
        assert(r.kind       == STURM_GATE_X
               && "gate 8..15 must be X (sub_const uncompute stub)");
        assert(r.qubits[0]  == static_cast<uint32_t>(i)
               && "gate 8..15 must target qubit i");
        assert(r.n          == 1u
               && "X gate arity must be 1");
        assert(r.param      == 5.0
               && "gate 8..15 param must be 5.0 (the constant c)");
    }

    std::printf("  test_append_golden: PASS (%zu gate records, sequence verified)\n",
                ir.size());
}

// ── Test 4: SIMULATE — swept classical inputs ─────────────────────────────────
//
// For classical a (super_mask==0), no quantum gates are emitted and the result
// is determined purely by classical value propagation.  Sweeps a ∈ {-10..10}
// and asserts c.value == (a_val + 5 >= 0).

static void test_simulate_swept() {
    for (int64_t a_val = -10LL; a_val <= 10LL; ++a_val) {
        const bool expected = (a_val + 5LL >= 0LL);
        const bool result   = run_classical(STURM_MODE_SIMULATE, a_val);

        assert(result == expected
               && "SIMULATE: c = (a+5)>=0 must equal mathematical answer");
    }

    std::printf("  test_simulate_swept: PASS (a in [-10..10], c == (a+5>=0))\n");
}

// ── Test 5: SIMULATE — boundary inputs ───────────────────────────────────────
//
// Explicitly verify the boundary cases for c = (a + 5) >= 0:
//   a = -5 → (a+5) = 0  → 0 >= 0 → true
//   a = -6 → (a+5) = -1 → -1 >= 0 → false
//   a = -4 → (a+5) = 1  → 1 >= 0 → true

static void test_simulate_boundary() {
    struct Case { int64_t a_val; bool expected; const char* label; };
    static const Case cases[] = {
        { -5LL,  true,  "a=-5  → (a+5)=0  >= 0 → true"  },
        { -6LL,  false, "a=-6  → (a+5)=-1 >= 0 → false" },
        { -4LL,  true,  "a=-4  → (a+5)=1  >= 0 → true"  },
        {  0LL,  true,  "a=0   → (a+5)=5  >= 0 → true"  },
        {  100LL, true,  "a=100 → large +  >= 0 → true"  },
        { -100LL, false, "a=-100→ large -  >= 0 → false" },
    };

    for (const auto& tc : cases) {
        const bool result = run_classical(STURM_MODE_SIMULATE, tc.a_val);
        assert(result == tc.expected && tc.label);
    }

    std::printf("  test_simulate_boundary: PASS (boundary cases verified)\n");
}

// ── Test 6: SIMULATE — result correct over all three modes ───────────────────
//
// The mathematical answer (a + 5 >= 0) must be identical across all three modes
// for classical inputs.

static void test_all_modes_agree() {
    for (int64_t a_val = -5LL; a_val <= 5LL; ++a_val) {
        const bool r_count    = run_classical(STURM_MODE_COUNT_ONLY, a_val);
        const bool r_append   = run_classical(STURM_MODE_APPEND,     a_val);
        const bool r_simulate = run_classical(STURM_MODE_SIMULATE,   a_val);
        const bool expected   = (a_val + 5LL >= 0LL);

        assert(r_count    == expected && "COUNT_ONLY mode: wrong classical result");
        assert(r_append   == expected && "APPEND mode: wrong classical result");
        assert(r_simulate == expected && "SIMULATE mode: wrong classical result");
    }

    std::printf("  test_all_modes_agree: PASS (all three modes give correct c.value)\n");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M26 end-to-end acceptance tests: c = (a + 5) >= 0\n");
    test_count_only_classical();
    test_count_only_quantum();
    test_append_golden();
    test_simulate_swept();
    test_simulate_boundary();
    test_all_modes_agree();
    std::printf("All M26 end-to-end tests passed.\n");
    return 0;
}
