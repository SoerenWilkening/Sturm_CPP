// test_mul_oop.cpp -- sturm-ph6f.2: sturm::detail::mul_oop reversible-network tests.
//
// Contract: mul_oop(a, b, tmp) emits a shift-and-add multiplication network via
// lib_mul_dsl when at least one operand carries allocated qubits, and falls
// back to the pure classical update when both operands are on the qubits[0] < 0
// short-circuit path (preserves example_qint_arith / example_phase_abc_demo).
// mul_oop_adj is the structural inverse — re-applying __lib_mul_dsl_adj on the
// post-swap-undo state where tmp == lower W bits of (a * b) returns the lower
// AND upper W result qubits to |0…0>.
//
// Invariants pinned by this test:
//   (1) Classical short-circuit path: both qubits[0] < 0 -- bookkeeping only.
//   (2) Gate path: tmp.qubits is freshly allocated (W lower bits of the
//       2W-bit multiplication result register; the upper W bits are stashed
//       internally for the matching adjoint to consume).
//   (3) Simulator readout of tmp == (a * b) & ((1<<W) - 1) on the gate path.
//   (4) mul_oop followed by mul_oop_adj returns the lower W tmp register AND
//       the stashed upper-W ancillas to |0…0>; the upper-W ancilla qubit
//       indices are released back to the pool.
//   (5) `sturm::invert<&sturm::detail::mul_oop<W>>()` resolves to mul_oop_adj
//       at compile time via STURM_REGISTER_ADJOINT.

#define STURM_BACKEND_ENABLED 1
#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/lossy_oop.hpp"
#include "sturm/routines/invert.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 64u) {
        bridge.allocate(n_qubits);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
        assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    orkan::state_t& sv() { return bridge.state(); }
};

static uint32_t read_reg_idxs(orkan::state_t& sv, const int* qidx,
                              uint32_t n, uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            uint32_t val = 0u;
            for (uint32_t k = 0; k < n; ++k) {
                if (qidx[k] >= 0) {
                    val |= (static_cast<uint32_t>((s >> qidx[k]) & 1u) << k);
                }
            }
            return val;
        }
    }
    return 0u;
}

template <std::size_t W>
static void run_mul_gate_path(uint32_t a_val, uint32_t b_val) {
    // Layout: a in [0..W), b in [W..2W), tmp lower-W + upper-W anc allocated by
    // mul_oop. Use a fixed Orkan budget large enough for the widest case the
    // test exercises (W=2 -> peak ~12 qubits) plus headroom.
    static constexpr uint32_t n_orkan = 16u;
    const uint32_t a_base = 0u, b_base = W, n_reg = 2u * W;

    sturm::QubitPool::instance().reset_for_testing();

    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i)
        reserved[i] = sturm::QubitPool::instance().allocate();

    SimCtx sc{n_orkan, 64u};
    for (uint32_t i = 0; i < W; ++i) {
        if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value = static_cast<int64_t>(a_val);
        b.value = static_cast<int64_t>(b_val);
        a.super_mask = b.super_mask = (1ULL << W) - 1ULL;
        a.owning_ = false; b.owning_ = false;

        sturm::qint_t<W> tmp;
        sturm::detail::mul_oop(a, b, tmp);

        // tmp must have its own freshly-allocated qubits (gate path).
        for (uint32_t i = 0; i < W; ++i) {
            assert(tmp.qubits[i] >= 0 && "tmp bit allocated on gate path");
            for (uint32_t j = 0; j < W; ++j) {
                assert(tmp.qubits[i] != a.qubits[j] && "tmp != a");
                assert(tmp.qubits[i] != b.qubits[j] && "tmp != b");
            }
        }

        const uint64_t mask = (1ULL << W) - 1ULL;
        const uint32_t expect =
            (a_val * b_val) & static_cast<uint32_t>(mask);
        assert(tmp.value == static_cast<int64_t>(expect)
               && "tmp.value bookkeeping = (a * b) & mask");

        uint32_t tmp_sv = read_reg_idxs(sc.sv(), tmp.qubits.data(), W, n_orkan);
        uint32_t a_sv   = read_reg_idxs(sc.sv(), a.qubits.data(), W, n_orkan);
        uint32_t b_sv   = read_reg_idxs(sc.sv(), b.qubits.data(), W, n_orkan);
        assert(tmp_sv == expect && "tmp register == (a * b) lower W in simulator");
        assert(a_sv == a_val && "a preserved");
        assert(b_sv == b_val && "b preserved");

        // Adjoint roundtrip: post-swap-undo state has tmp lower-W = lower W of
        // a*b (and upper-W stashed bits == upper W of a*b). Running mul_oop_adj
        // clears both halves to |0…0>.
        sturm::detail::mul_oop_adj(a, b, tmp);
        uint32_t tmp_after = read_reg_idxs(sc.sv(), tmp.qubits.data(), W, n_orkan);
        assert(tmp_after == 0u && "adjoint zeros tmp register");

        // Operands still preserved.
        assert(read_reg_idxs(sc.sv(), a.qubits.data(), W, n_orkan) == a_val
               && "a preserved after adjoint");
        assert(read_reg_idxs(sc.sv(), b.qubits.data(), W, n_orkan) == b_val
               && "b preserved after adjoint");

        // Detach borrowed views before destruction.
        a.qubits.fill(-1); b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i)
        sturm::QubitPool::instance().release(reserved[i]);
}

// Mirrors the LO-2 emitter's full triplet: forward mul_oop, swap(a, tmp),
// reverse swap, then mul_oop_adj. swap is a pure pointer/value exchange
// (sturm::swap on qint_t<W> emits no gates), so this should leave both `a`
// preserved AND tmp's lower-W back to |0…0> after the adjoint runs.
template <std::size_t W>
static void run_mul_lo2_emitter_pattern(uint32_t a_val, uint32_t b_val) {
    static constexpr uint32_t n_orkan = 16u;
    const uint32_t a_base = 0u, b_base = W, n_reg = 2u * W;

    sturm::QubitPool::instance().reset_for_testing();

    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i)
        reserved[i] = sturm::QubitPool::instance().allocate();

    SimCtx sc{n_orkan, 64u};
    for (uint32_t i = 0; i < W; ++i) {
        if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value = static_cast<int64_t>(a_val);
        b.value = static_cast<int64_t>(b_val);
        a.super_mask = b.super_mask = (1ULL << W) - 1ULL;
        a.owning_ = false; b.owning_ = false;

        sturm::qint_t<W> tmp;

        // Capture the original a.qubits so we can restore them across the
        // swap pair (swap is destructive on qint_t value/qubits/owning).
        std::array<int, W> a_qubits_orig = a.qubits;

        // Forward: mul_oop + swap(a, tmp) — the LO-2 emitter's triplet shape.
        sturm::detail::mul_oop(a, b, tmp);
        sturm::swap(a, tmp);

        // Cleanup: reverse swap + mul_oop_adj — the LO-2 scope-exit shape.
        sturm::swap(a, tmp);
        sturm::detail::mul_oop_adj(a, b, tmp);

        // After cleanup, tmp's lower-W qubits are back to |0…0>.
        uint32_t tmp_after = read_reg_idxs(sc.sv(), tmp.qubits.data(), W, n_orkan);
        assert(tmp_after == 0u
               && "LO-2 triplet round-trip zeros tmp register");

        // a.qubits restored; a value preserved.
        assert(a.qubits == a_qubits_orig && "a.qubits restored after swap-pair");
        assert(read_reg_idxs(sc.sv(), a.qubits.data(), W, n_orkan) == a_val
               && "a value preserved after LO-2 triplet round-trip");
        assert(read_reg_idxs(sc.sv(), b.qubits.data(), W, n_orkan) == b_val
               && "b value preserved after LO-2 triplet round-trip");

        a.qubits.fill(-1); b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i)
        sturm::QubitPool::instance().release(reserved[i]);
}

template <std::size_t W>
static void run_classical_short_circuit(int64_t a_val, int64_t b_val) {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::qint_t<W> a, b, tmp;
    a.value = a_val; b.value = b_val;
    // Both qubits arrays remain at -1 (default) -- classical short-circuit.
    sturm::detail::mul_oop(a, b, tmp);
    const uint64_t mask = (W >= 64) ? ~0ULL : ((1ULL << W) - 1ULL);
    const int64_t expect = static_cast<int64_t>(
        (static_cast<uint64_t>(a_val) * static_cast<uint64_t>(b_val)) & mask);
    assert(tmp.value == expect);
    // tmp.qubits stay at -1 on the classical path (no allocation).
    for (auto q : tmp.qubits) assert(q == -1 && "tmp.qubits stay -1");

    sturm::detail::mul_oop_adj(a, b, tmp);
    assert(tmp.value == 0);
    assert(tmp.super_mask == 0);
}

// invert<&mul_oop<W>>() must resolve to mul_oop_adj<W> at compile time.
static void test_invert_resolution() {
    constexpr auto adj_ptr =
        sturm::invert<&sturm::detail::mul_oop<2>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&mul_oop<W>>() must resolve to a registered adjoint");
    static_assert(adj_ptr == &sturm::detail::mul_oop_adj<2>,
                  "mul_oop_adj must be the registered structural inverse");
    std::puts("  PASS: test_mul_oop_invert_resolves_to_adj");
}

int main() {
    std::printf("sturm-ph6f.2 LO-2: mul_oop reversible-network tests:\n");

    test_invert_resolution();

    // Classical short-circuit path -- must continue to work post-rewrite.
    run_classical_short_circuit<8>(6, 2);     // 12, fits in 8 bits.
    run_classical_short_circuit<8>(0x10, 0x10);  // 256 -> truncates to 0.
    run_classical_short_circuit<2>(3, 3);     // 9 mod 4 = 1.
    std::puts("  PASS: test_mul_oop_classical_short_circuit");

    // Gate path -- W=2, all 16 (a,b) combinations.
    for (uint32_t a = 0; a < 4u; ++a)
        for (uint32_t b = 0; b < 4u; ++b)
            run_mul_gate_path<2>(a, b);
    std::puts("  PASS: test_mul_oop_gate_path_W2_full_matrix");

    // LO-2 emitter pattern -- forward mul_oop + swap, then reverse swap +
    // mul_oop_adj at scope exit. Mirrors the desugared text of `a *= b;`.
    for (uint32_t a = 0; a < 4u; ++a)
        for (uint32_t b = 0; b < 4u; ++b)
            run_mul_lo2_emitter_pattern<2>(a, b);
    std::puts("  PASS: test_mul_oop_lo2_emitter_pattern_W2_full_matrix");

    std::printf("All sturm-ph6f.2 tests passed.\n");
    return 0;
}
