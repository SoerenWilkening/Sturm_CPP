// test_lower_assign.cpp — M9 (PRD v2): E2E tests for frontend lowering hooks.
//
// Tests (per impl plan M9):
//   1. test_lower_or_assign   — a |= b  (out-of-place OR + uncontrolled move)
//   2. test_lower_add_assign  — a += b  (in-place ADD via Cuccaro)
//   3. test_lower_xor_assign  — a ^= b  (in-place XOR, genuinely in-place)
//   4. test_lower_expr_add    — c = a + b  (out-of-place ADD + relabel into c)
//   5. test_lower_eq          — if (a == b)  (EQ comparison, result in qbool)
//   6. test_lower_swap        — SWAP(a,b)  (uncontrolled: index relabel, 0 gates)
//
// Qubit layout (3-bit registers to stay within 17-qubit cap):
//   q[0..2]  = a  (3 bits)
//   q[3..5]  = b  (3 bits)
//   q[6..8]  = c  (3 bits, for binary-result expr tests)
//   q[9]     = out/carry (1 bit)
//   q[10..]  = ancilla pool
//
// Total: 3+3+3+1+7 spare = 17 qubits (within the SimState 17-qubit cap).
//
// Harness: plain assert + printf (project convention).

#include "sturm/frontend/lower_assign.hpp"
#include "sturm/frontend/lower_expr.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/lib/logic_basic.hpp"
#include "sturm/lib/add_cuccaro.hpp"
#include "sturm/lib/compare.hpp"
#include "sturm/lib/swap.hpp"
#include "sturm/lib/move.hpp"
#include "sturm/lib/garbage.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <vector>

static constexpr double kTol = 1e-9;
static constexpr uint32_t N  = 3u;  // bits per register

// ── Helpers ────────────────────────────────────────────────────────────────────

static uint64_t dominant_basis(const sturm::v2::SimState& s) {
    uint64_t dim = uint64_t{1} << s.num_qubits();
    uint64_t best = 0;
    double   best_p = 0.0;
    for (uint64_t i = 0; i < dim; ++i) {
        double p = std::norm(s.amplitude(i));
        if (p > best_p) { best_p = p; best = i; }
    }
    return best;
}

static uint32_t read_bits(const sturm::v2::SimState& s,
                           const uint32_t* idxs, uint32_t n) {
    uint64_t basis = dominant_basis(s);
    uint32_t result = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if ((basis >> idxs[i]) & 1u) result |= (1u << i);
    }
    return result;
}

// Allocate state with a=a_val, b=b_val; c=0, out=0.
// Layout: a=[0..N-1], b=[N..2N-1], c=[2N..3N-1], out=[3N], anc=[3N+1..16]
static sturm::v2::SimState make_state(uint32_t a_val, uint32_t b_val) {
    const uint32_t total = 17u;  // 17-qubit cap
    sturm::v2::SimState s;
    s.allocate(total);
    uint64_t idx = 0u;
    for (uint32_t i = 0; i < N; ++i) if ((a_val >> i) & 1u) idx |= uint64_t{1} << i;
    for (uint32_t i = 0; i < N; ++i) if ((b_val >> i) & 1u) idx |= uint64_t{1} << (N + i);
    s.load_basis(idx);
    return s;
}

static constexpr uint32_t a_idxs[N] = {0u, 1u, 2u};
static constexpr uint32_t b_idxs[N] = {3u, 4u, 5u};
static constexpr uint32_t c_idxs[N] = {6u, 7u, 8u};
static constexpr uint32_t out_idx   = 9u;
static constexpr uint32_t anc_start = 10u;

// ── test_lower_or_assign ──────────────────────────────────────────────────────
//
// a |= b:
//   Allocate fresh result register r from pool.
//   Compute r[i] = a[i] | b[i] (out-of-place).
//   Index relabel: a ← r (result).
//
// After the call: a holds (a_in | b); b unchanged.
// Note: pool has N result qubits "in use" (now owned by a_idxs).
//       Displaced old a qubits are NOT freed here (GarbageManager path per PRD).
// Verified exhaustively for all 3-bit (a, b) pairs.

static void test_lower_or_assign() {
    uint32_t pass = 0;
    for (uint32_t av = 0; av < (1u << N); ++av) {
        for (uint32_t bv = 0; bv < (1u << N); ++bv) {
            auto s = make_state(av, bv);
            sturm::v2::AncillaManager mgr(s, anc_start);

            // Working copies of index arrays (lower_or_assign modifies a_idxs).
            uint32_t a[N] = {a_idxs[0], a_idxs[1], a_idxs[2]};
            uint32_t b[N] = {b_idxs[0], b_idxs[1], b_idxs[2]};

            sturm::v2::lower_or_assign(s, mgr, a, b, N);

            // After lower_or_assign: a[] holds the new qubit indices (result).
            uint32_t got_a  = read_bits(s, a, N);
            uint32_t got_b  = read_bits(s, b, N);
            uint32_t exp_a  = av | bv;

            assert(got_a == exp_a && "lower_or_assign: a != a_in | b");
            assert(got_b == bv    && "lower_or_assign: b was modified");
            // mgr has N result qubits in use (the new a register); that is expected.
            // No per-bit ancilla leaked (lib_OR borrows+frees 1 per bit).
            assert(mgr.num_in_use() == N && "lower_or_assign: unexpected ancilla count");
            ++pass;
        }
    }
    std::printf("  PASS: test_lower_or_assign (%u cases)\n", pass);
}

// ── test_lower_add_assign ─────────────────────────────────────────────────────
//
// a += b:  In-place Cuccaro addition.
// After the call: a holds (a_in + b_in) mod 2^N; b_in unchanged.
// carry_out (qubit `out_idx`) receives the overflow bit (outside ancilla pool).

static void test_lower_add_assign() {
    uint32_t pass = 0;
    for (uint32_t av = 0; av < (1u << N); ++av) {
        for (uint32_t bv = 0; bv < (1u << N); ++bv) {
            auto s = make_state(av, bv);
            sturm::v2::AncillaManager mgr(s, anc_start);

            uint32_t a[N] = {a_idxs[0], a_idxs[1], a_idxs[2]};
            uint32_t b[N] = {b_idxs[0], b_idxs[1], b_idxs[2]};

            // carry_out is outside the ancilla pool (qubit out_idx = 9).
            sturm::v2::lower_add_assign(s, mgr, a, b, N, out_idx);

            uint32_t mask    = (1u << N) - 1u;
            uint32_t got_a   = read_bits(s, a, N);
            uint32_t got_b   = read_bits(s, b, N);
            uint32_t exp_a   = (av + bv) & mask;

            assert(got_a == exp_a && "lower_add_assign: a != a_in + b_in");
            assert(got_b == bv    && "lower_add_assign: b was modified");
            assert(mgr.num_in_use() == 0u && "lower_add_assign: ancilla leaked");
            ++pass;
        }
    }
    std::printf("  PASS: test_lower_add_assign (%u cases)\n", pass);
}

// ── test_lower_xor_assign ─────────────────────────────────────────────────────
//
// a ^= b: In-place XOR (genuinely in-place, one CNOT per bit).
// After the call: a holds (a_in ^ b_in); b unchanged.

static void test_lower_xor_assign() {
    uint32_t pass = 0;
    for (uint32_t av = 0; av < (1u << N); ++av) {
        for (uint32_t bv = 0; bv < (1u << N); ++bv) {
            auto s = make_state(av, bv);
            sturm::v2::AncillaManager mgr(s, anc_start);

            uint32_t a[N] = {a_idxs[0], a_idxs[1], a_idxs[2]};
            uint32_t b[N] = {b_idxs[0], b_idxs[1], b_idxs[2]};

            sturm::v2::lower_xor_assign(s, mgr, a, b, N);

            uint32_t got_a = read_bits(s, a, N);
            uint32_t got_b = read_bits(s, b, N);
            uint32_t exp_a = av ^ bv;

            assert(got_a == exp_a && "lower_xor_assign: a != a_in ^ b_in");
            assert(got_b == bv    && "lower_xor_assign: b was modified");
            assert(mgr.num_in_use() == 0u && "lower_xor_assign: ancilla leaked");
            ++pass;
        }
    }
    std::printf("  PASS: test_lower_xor_assign (%u cases)\n", pass);
}

// ── test_lower_expr_add ───────────────────────────────────────────────────────
//
// c = a + b: out-of-place ADD with result moved into c.
// c starts as |0>, a and b are inputs.
// After: c holds (a_in + b_in) mod 2^N; a and b unchanged.
// carry_out (qubit `out_idx`) receives overflow bit (outside ancilla pool).

static void test_lower_expr_add() {
    uint32_t pass = 0;
    for (uint32_t av = 0; av < (1u << N); ++av) {
        for (uint32_t bv = 0; bv < (1u << N); ++bv) {
            auto s = make_state(av, bv);
            sturm::v2::AncillaManager mgr(s, anc_start);

            uint32_t a[N] = {a_idxs[0], a_idxs[1], a_idxs[2]};
            uint32_t b[N] = {b_idxs[0], b_idxs[1], b_idxs[2]};
            uint32_t c[N] = {c_idxs[0], c_idxs[1], c_idxs[2]};

            // carry_out is qubit out_idx (= 9), outside the ancilla pool.
            sturm::v2::lower_expr_add(s, mgr, c, a, b, N, out_idx);

            uint32_t mask   = (1u << N) - 1u;
            uint32_t got_c  = read_bits(s, c, N);
            uint32_t got_a  = read_bits(s, a, N);
            uint32_t got_b  = read_bits(s, b, N);
            uint32_t exp_c  = (av + bv) & mask;

            assert(got_c == exp_c && "lower_expr_add: c != a + b");
            assert(got_a == av    && "lower_expr_add: a was modified");
            assert(got_b == bv    && "lower_expr_add: b was modified");
            // mgr has N result qubits in use (scratch r[] now owned by c); expected.
            assert(mgr.num_in_use() == N && "lower_expr_add: unexpected ancilla count");
            ++pass;
        }
    }
    std::printf("  PASS: test_lower_expr_add (%u cases)\n", pass);
}

// ── test_lower_eq ─────────────────────────────────────────────────────────────
//
// if (a == b):  EQ comparison into a 1-qubit result register.
// After: out = 1 if a_in == b_in, else 0.  a, b unchanged.

static void test_lower_eq() {
    uint32_t pass = 0;
    for (uint32_t av = 0; av < (1u << N); ++av) {
        for (uint32_t bv = 0; bv < (1u << N); ++bv) {
            auto s = make_state(av, bv);
            sturm::v2::AncillaManager mgr(s, anc_start);

            uint32_t a[N] = {a_idxs[0], a_idxs[1], a_idxs[2]};
            uint32_t b[N] = {b_idxs[0], b_idxs[1], b_idxs[2]};
            uint32_t o    = out_idx;

            sturm::v2::lower_eq(s, mgr, a, b, o, N);

            uint64_t basis  = dominant_basis(s);
            uint32_t got_o  = static_cast<uint32_t>((basis >> o) & 1u);
            uint32_t exp_o  = (av == bv) ? 1u : 0u;

            assert(got_o == exp_o && "lower_eq: wrong EQ result");
            assert(mgr.num_in_use() == 0u && "lower_eq: ancilla leaked");
            ++pass;
        }
    }
    std::printf("  PASS: test_lower_eq (%u cases)\n", pass);
}

// ── test_lower_swap ───────────────────────────────────────────────────────────
//
// SWAP(a,b): uncontrolled → zero gates, index relabel only.
// Verifies:
//   1. After lower_swap, a's index array holds b's original qubit indices and
//      vice versa.
//   2. No gates are emitted (tracked via gate_count on SimState — proxy: the
//      amplitude vector is unchanged since no gates run).

static void test_lower_swap() {
    uint32_t pass = 0;
    for (uint32_t av = 0; av < (1u << N); ++av) {
        for (uint32_t bv = 0; bv < (1u << N); ++bv) {
            auto s = make_state(av, bv);
            sturm::v2::AncillaManager mgr(s, anc_start);

            // Snapshot amplitude vector before swap.
            uint64_t dim = uint64_t{1} << s.num_qubits();
            std::vector<std::complex<double>> before(dim);
            for (uint64_t i = 0; i < dim; ++i) before[i] = s.amplitude(i);

            uint32_t a[N] = {a_idxs[0], a_idxs[1], a_idxs[2]};
            uint32_t b[N] = {b_idxs[0], b_idxs[1], b_idxs[2]};

            sturm::v2::lower_swap(s, mgr, a, b, N);

            // Verify index relabel: a now holds old b indices, b holds old a indices.
            for (uint32_t i = 0; i < N; ++i) {
                assert(a[i] == b_idxs[i] && "lower_swap: a indices not swapped");
                assert(b[i] == a_idxs[i] && "lower_swap: b indices not swapped");
            }

            // Verify zero gates: amplitude vector unchanged.
            for (uint64_t i = 0; i < dim; ++i) {
                double diff = std::abs(s.amplitude(i) - before[i]);
                assert(diff < kTol && "lower_swap: state changed (gates were emitted)");
            }

            assert(mgr.num_in_use() == 0u && "lower_swap: ancilla leaked");
            ++pass;
        }
    }
    std::printf("  PASS: test_lower_swap (%u cases)\n", pass);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M9 frontend lowering hooks tests\n");
    std::printf("  running test_lower_or_assign...\n");
    test_lower_or_assign();
    std::printf("  running test_lower_add_assign...\n");
    test_lower_add_assign();
    std::printf("  running test_lower_xor_assign...\n");
    test_lower_xor_assign();
    std::printf("  running test_lower_expr_add...\n");
    test_lower_expr_add();
    std::printf("  running test_lower_eq...\n");
    test_lower_eq();
    std::printf("  running test_lower_swap...\n");
    test_lower_swap();
    std::printf("All M9 lower_assign / lower_expr tests passed.\n");
    return 0;
}
