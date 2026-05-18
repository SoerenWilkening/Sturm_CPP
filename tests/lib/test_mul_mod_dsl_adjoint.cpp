// test_mul_mod_dsl_adjoint.cpp -- sturm-kubb.5 P2.5 mul-mod-dsl 2.5 adjoint
//                                  round-trip.
//
// Plan §4.3 beat 2.5: forward `lib_mul_mod_dsl(a, b, n, W, r)` followed by
// `__lib_mul_mod_dsl_adj(a, b, n, W, r)` must return `r` to |0> for every
// (a, b, n) input that beats 2.3 / 2.4 cover.  Inputs `a, b, n` must remain
// unchanged across the full forward+adjoint pair, and `QubitPool::in_use()`
// must return to its pre-call value (no leaked ancillas).
//
// Mirrors `tests/lib/test_add_mod_dsl_adjoint.cpp` (sturm-yh3d.5) adapted
// for chain-style mul_mod (sturm-kubb.{2,3,4}).  Per the issue brief the
// W=2 exhaustive sweep uses the APPEND-mode classical-trace harness from
// beat 2.4 because per-case state-vector simulation costs ~250 s on
// chain-style mul_mod and 14 forward+adjoint cases would exceed ctest's
// 3600 s timeout; one single classical (2, 2, 3) case is also exercised
// through the orkan simulator to keep an end-to-end unitary-semantics
// witness for `invert<&lib_mul_mod_dsl<BitProxy>>()`.  W=3 uses the same
// classical-trace harness (the simulator is infeasible at W=3: ~35 live
// qubits ⇒ 2^35 ≈ 34 GB amplitudes per case).
//
// sturm-scin: the inline `apply_gate_classical` / `read_reg_classical`
// helpers and the bespoke `run_roundtrip_case_trace` driver have been
// replaced with the shared APPEND+classical-replay helper
// (`classical_replay.hpp`).  Forward and adjoint emissions append to the
// same ctx->ir; the full IR is replayed in a single pass over the
// classical bit-vector.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/mul_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/routines/invert.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include "classical_replay.hpp"  // sturm-scin: APPEND+replay helper

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <random>
#include <vector>

static constexpr std::size_t W  = 2u;
static constexpr std::size_t W3 = 3u;

// sturm-scin: the W=2 SIMULATE round-trip witness
// (run_roundtrip_case_w2_sim at n_orkan=25) was retired; it cost ~197 s
// per ctest run.  Algorithm-level SIMULATE coverage for `lib_mul_mod_dsl`
// is provided by test_mul_mod_dsl.cpp's `run_n_zero_case` smoke
// (n_orkan=17).  The forward+adjoint round-trip is exhaustively covered
// here by the W=2 (14 cases) and W=3 (50 cases) APPEND+replay sweeps
// below.

// ── Forward+adjoint round-trip via APPEND-mode capture + classical
//    bit-vector replay (sturm-scin) ──────────────────────────────────────
//
// Drives the forward `lib_mul_mod_dsl` then `invert<>()`-resolved adjoint
// under a *single* STURM_MODE_APPEND context so both emissions append to
// the same ctx->ir.  The full IR is replayed in one pass over a classical
// bit-vector sized to QubitPool::high_water().  Asserts:
//   - a, b, n registers unchanged across the full round-trip,
//   - r register returns to 0 after the adjoint,
//   - every ancilla bit (qubits beyond the 4*W_VAL input slots) is back
//     to 0 (algorithm cleans up after itself),
//   - QubitPool::in_use() returns to its pre-call value.
//
// Per-case cost is O(|IR|) instead of O(2^n_orkan * |IR|); the W=2
// 14-case sweep + W=3 50-case sweep collectively drop from ~366 s to
// a few seconds total.
template <std::size_t W_VAL>
static void run_replay_case_adjoint(uint32_t a_val, uint32_t b_val,
                                    uint32_t n_val) {
    assert(a_val < n_val && b_val < n_val && n_val < (1u << W_VAL));

    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(W_VAL);
    int qi_a[W_VAL], qi_b[W_VAL], qi_n[W_VAL], qi_r[W_VAL];
    for (std::size_t i = 0; i < W_VAL; ++i)
        qi_a[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W_VAL; ++i)
        qi_b[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W_VAL; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W_VAL; ++i)
        qi_r[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    sturm::qbool a_own[W_VAL], b_own[W_VAL], n_own[W_VAL], r_own[W_VAL];
    sturm::BitProxy a_bits[W_VAL], b_bits[W_VAL], n_bits[W_VAL], r_bits[W_VAL];
    for (std::size_t i = 0; i < W_VAL; ++i) {
        a_own[i] = sturm::qbool::make_non_owning(qi_a[i]);
        b_own[i] = sturm::qbool::make_non_owning(qi_b[i]);
        n_own[i] = sturm::qbool::make_non_owning(qi_n[i]);
        r_own[i] = sturm::qbool::make_non_owning(qi_r[i]);
        a_bits[i] = sturm::BitProxy(a_own[i]);
        b_bits[i] = sturm::BitProxy(b_own[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
        r_bits[i] = sturm::BitProxy(r_own[i]);
    }

    sturm::test_helpers::AppendContext app;

    // Forward + adjoint both append into the same ctx->ir.
    sturm::lib_mul_mod_dsl<sturm::BitProxy>(a_bits, b_bits, n_bits,
                                            W_VAL, r_bits);

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_mul_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_mul_mod_dsl<BitProxy>>() must resolve");
    adj_ptr(a_bits, b_bits, n_bits, W_VAL, r_bits);

    const int high_water = sturm::QubitPool::instance().high_water();

    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < W_VAL; ++i) {
        if ((a_val >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])] = 1u;
        if ((b_val >> i) & 1u) bits[static_cast<std::size_t>(qi_b[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
        // r starts at |0>; ancillas start at |0>.
    }
    sturm::test_helpers::replay_ir(app.ctx()->ir, bits);

    using sturm::test_helpers::read_reg_classical;
    const uint32_t a_out = read_reg_classical(bits, qi_a, W_VAL);
    const uint32_t b_out = read_reg_classical(bits, qi_b, W_VAL);
    const uint32_t n_out = read_reg_classical(bits, qi_n, W_VAL);
    const uint32_t r_out = read_reg_classical(bits, qi_r, W_VAL);
    assert(a_out == a_val && "replay: a register unchanged");
    assert(b_out == b_val && "replay: b register unchanged");
    assert(n_out == n_val && "replay: n register unchanged");
    assert(r_out == 0u && "replay: r returned to |0> after adjoint");

    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u && "replay: ancilla bit not cleaned up");
    }

    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "replay: pool live-count returns to pre-call value");

    for (std::size_t i = W_VAL; i-- > 0;)
        sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = W_VAL; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W_VAL; i-- > 0;)
        sturm::QubitPool::instance().release(qi_b[i]);
    for (std::size_t i = W_VAL; i-- > 0;)
        sturm::QubitPool::instance().release(qi_a[i]);
}

int main() {
    std::printf("sturm-kubb.5 P2.5 mul-mod-dsl: adjoint round-trip "
                "(W=2 exhaustive trace, all a, b in [0, n) for n in [1, 4)):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_replay_case_adjoint<W>(a_val, b_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 → 1; n=2 → 4; n=3 → 9; total = 14.
    assert(cases_run == 14u);
    std::printf("  PASS: %zu W=2 cases (forward + adjoint zeros r, "
                "preserves a/b/n, no leaked ancillas)\n", cases_run);

    // W=3 random sweep — classical-trace mode (simulator infeasible at W=3).
    constexpr uint32_t    kW3Seed  = 42u;
    constexpr std::size_t kW3Cases = 50u;
    std::printf("sturm-kubb.5 P2.5 mul-mod-dsl: adjoint round-trip "
                "(W=3 random sweep, %zu cases, seed=%u, classical trace):\n",
                kW3Cases, kW3Seed);
    std::mt19937 rng(kW3Seed);
    std::uniform_int_distribution<uint32_t> n_dist(1u, (1u << W3) - 1u);
    for (std::size_t i = 0; i < kW3Cases; ++i) {
        uint32_t n_val = n_dist(rng);
        std::uniform_int_distribution<uint32_t> ab_dist(0u, n_val - 1u);
        uint32_t a_val = ab_dist(rng);
        uint32_t b_val = ab_dist(rng);
        run_replay_case_adjoint<W3>(a_val, b_val, n_val);
    }
    std::printf("  PASS: %zu W=3 random cases (forward + adjoint round-trip "
                "in trace mode)\n", kW3Cases);

    std::printf("All sturm-kubb.5 tests passed.\n");
    return 0;
}
