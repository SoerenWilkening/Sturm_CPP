// test_double_mod_dsl.cpp -- Beat B (sturm-wdas) lib_double_mod_dsl forward
//                              tests, post sturm-4oot.1 API rewrite.
//
// As of sturm-4oot.1 (even-n-double-mod), the forward primitive takes an
// extra `Bit& lt_flag_out` parameter; the LSB-trick uncompute is gone, the
// internal lt_flag allocation is gone, and the odd-n precondition is
// dropped from the header.  Forward XORs `(2x_orig < n_value)` into
// `lt_flag_out`, so callers can pre-zero (clean write) or accumulate.  The
// adjoint reads `lt_flag_out` to reverse the doubling and leaves it = 0
// on exit.  This file exercises the forward direction; the adjoint
// round-trip lives in `test_double_mod_dsl_adjoint.cpp`.
//
// As of sturm-4oot.2, the W=2 / W=3 sweeps are extended to cover even
// `n` (now legal post sturm-4oot.1).  W=2 even-n exhaustive covers
// n=2 only — n=4 doesn't fit in a 2-bit modulus register and is hoisted
// to W=3 (this mirrors the convention used in the sturm-4oot.4 test
// extension).  W=3 even-n exhaustive covers n in {2, 4, 6} — n=8
// doesn't fit in a 3-bit modulus register.  W=4 and W=5 even-n random
// spot checks at n in {10, 12, 14} land too far above the orkan
// 30-qubit ceiling for the simulator harness, so they use a classical-
// trace replay harness (APPEND-mode IR + bit-vector replay) modelled on
// `test_mul_mod_dsl_oneshot.cpp` (sturm-4oot.4).  The (n+1)/2 odd-only
// counterexample (n=3, x=2) was previously the sole adjoint-undefined
// case; under sturm-4oot.1 the round-trip is now defined for it and is
// already covered by the W=2 sweep + the explicit design-note-3 call.
//
// Each call asserts:
//   - x_bits[0..W-1] == (2 · x_orig) mod n_value,
//   - x_bits[W] == 0 (overflow slot stays |0>),
//   - n_bits unchanged (reversibility of the modulus register),
//   - lt_flag_out == (2 · x_orig < n_value)  (NEW under sturm-4oot.1),
//   - QubitPool::in_use() returns to its pre-call value (no leaked
//     ancillas).
//
// The W=2 simulator harness sizes the orkan state at the kMaxQubits=17
// cap (matches add_mod beat 1.2/1.3); the W=3 leg bypasses the cap by
// calling `orkan::allocate(bridge.state(), n_orkan_w3)` directly (the
// algorithm peaks above 17 live qubits at W=3).  The W=3 random sweep
// uses the simulator (peak ≈ 4·W + 4 + transient ≈ 16-20 qubits, which
// fits inside orkan's 30-qubit ceiling).

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/double_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include "classical_replay.hpp"  // sturm-scin: shared APPEND+replay helpers

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <random>
#include <vector>

static constexpr double kTol = 1e-9;
static constexpr std::size_t W = 2;
// W=2: x_bits has W+1 = 3 slots, n_bits has W = 2 slots, lt_flag = 1 slot
// → 6 input qubits.  Plus n_pad + carry_anc + inner adder transients ≈ 4
// ancillas (the lt_flag is now caller-owned per sturm-4oot.1, so the
// internal peak drops by 1).  Sizing the simulator at the kMaxQubits=17
// cap leaves headroom and matches the add_mod_dsl test family.
static constexpr uint32_t n_orkan = 17u;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 64u) {
        bridge.allocate(n_q);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE);
        assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
    orkan::state_t& sv() { return bridge.state(); }
};

static uint32_t read_reg(orkan::state_t& sv, const int* qi, uint32_t n,
                         uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            uint32_t v = 0u;
            for (uint32_t k = 0; k < n; ++k)
                if (qi[k] >= 0)
                    v |= (static_cast<uint32_t>((s >> qi[k]) & 1u) << k);
            return v;
        }
    }
    return 0u;
}

// ── W=2 register helpers (W+1 slots for x_bits, W slots for n_bits) ─────
struct RegX {
    std::array<int, W + 1u>             qi;
    std::array<sturm::qbool, W + 1u>    owners;
    std::array<sturm::BitProxy, W + 1u> bits;
};

struct RegN {
    std::array<int, W>              qi;
    std::array<sturm::qbool, W>     owners;
    std::array<sturm::BitProxy, W>  bits;
};

// Caller-owned lt_flag_out register (1 qubit).  Allocated via the qubit
// pool so it shows up in the in_use bookkeeping like every other input.
struct RegLT {
    int                qi;
    sturm::qbool       own;
    sturm::BitProxy    bit;
};

static RegX make_reg_x(int base, uint32_t val, orkan::state_t& sv) {
    RegX r;
    for (std::size_t i = 0; i < W + 1u; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if (i < W && ((val >> i) & 1u))
            orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
        // Bit W (the overflow slot) starts |0> — never flipped at init.
    }
    for (std::size_t i = 0; i < W + 1u; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(r.qi[i]);
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

static RegN make_reg_n(int base, uint32_t val, orkan::state_t& sv) {
    RegN r;
    for (std::size_t i = 0; i < W; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if ((val >> i) & 1u) orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
    }
    for (std::size_t i = 0; i < W; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(r.qi[i]);
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

static RegLT make_reg_lt(int qi_base) {
    RegLT r;
    r.qi  = qi_base;
    r.own = sturm::qbool::make_non_owning(qi_base);
    r.bit = sturm::BitProxy(r.own);
    return r;
}

// Beat sturm-wdas.1: n==0 short-circuits, leaves x_bits, n_bits and
// lt_flag_out unchanged.
static void run_n_zero_case(uint32_t x_val, uint32_t n_val) {
    sturm::QubitPool::instance().reset_for_testing();
    // x_bits has W+1 slots, n_bits has W slots, lt_flag has 1 slot →
    // 2W+2 inputs.
    const uint32_t n_reg = (W + 1u) + W + 1u;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    SimCtx sc{n_orkan, 64u};
    RegX x   = make_reg_x(0, x_val, sc.sv());
    RegN n   = make_reg_n(static_cast<int>(W + 1u), n_val, sc.sv());
    RegLT lt = make_reg_lt(static_cast<int>(W + 1u + W));

    // Call with width n == 0; stub must short-circuit silently.
    sturm::lib_double_mod_dsl<sturm::BitProxy>(x.bits.data(), n.bits.data(),
                                                /*n=*/0u, lt.bit);

    uint32_t x_low_sv = read_reg(sc.sv(), x.qi.data(), W, n_orkan);
    uint32_t x_top_sv = (read_reg(sc.sv(), x.qi.data() + W, 1u, n_orkan)) & 1u;
    uint32_t n_sv     = read_reg(sc.sv(), n.qi.data(), W, n_orkan);
    uint32_t lt_sv    = read_reg(sc.sv(), &lt.qi, 1u, n_orkan);
    assert(x_low_sv == x_val && "n==0: x_bits[0..W-1] unchanged");
    assert(x_top_sv == 0u    && "n==0: x_bits[W] still |0>");
    assert(n_sv     == n_val && "n==0: n_bits unchanged");
    assert(lt_sv    == 0u    && "n==0: lt_flag_out still |0>");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// Beat sturm-wdas.2 (post sturm-4oot.1): full algorithm, single classical
// case.  Asserts:
//   - x_bits[0..W-1] holds (2·x) mod n,
//   - x_bits[W] == 0 (overflow slot returned to |0>),
//   - n_bits unchanged,
//   - lt_flag_out == (2x_orig < n_value)  (XOR-into a |0> entry → clean
//     write of the comparison bit per the new contract),
//   - QubitPool::in_use() returns to its pre-call value.
// Pre: x_val < n_val.  No parity restriction (sturm-4oot.1 lifted it;
// the W=2/W=3 sweeps below now exercise both parities under
// sturm-4oot.2).
static void run_classical_case(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && "test precondition: x < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = (W + 1u) + W + 1u;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan, 64u};
    RegX x   = make_reg_x(0, x_val, sc.sv());
    RegN n   = make_reg_n(static_cast<int>(W + 1u), n_val, sc.sv());
    RegLT lt = make_reg_lt(static_cast<int>(W + 1u + W));

    sturm::lib_double_mod_dsl<sturm::BitProxy>(x.bits.data(), n.bits.data(), W,
                                                lt.bit);

    const uint32_t expect_x  = (2u * x_val) % n_val;
    const uint32_t expect_lt = (2u * x_val < n_val) ? 1u : 0u;
    uint32_t x_low_sv = read_reg(sc.sv(), x.qi.data(), W, n_orkan);
    uint32_t x_top_sv = read_reg(sc.sv(), x.qi.data() + W, 1u, n_orkan);
    uint32_t n_sv     = read_reg(sc.sv(), n.qi.data(), W, n_orkan);
    uint32_t lt_sv    = read_reg(sc.sv(), &lt.qi, 1u, n_orkan);
    assert(x_low_sv == expect_x  && "forward: x_bits[0..W-1] == (2x) mod n");
    assert(x_top_sv == 0u        && "forward: x_bits[W] returned to |0>");
    assert(n_sv     == n_val     && "forward: n_bits unchanged");
    assert(lt_sv    == expect_lt &&
           "forward: lt_flag_out == (2x_orig < n_value)");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "forward: pool live-count returns to pre-call value");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// sturm-4oot.1: lt_flag_out is XOR-into.  Pre-flip the bit to |1>, run the
// forward, and verify lt_flag_out_post == 1 XOR (2x_orig < n_value) — i.e.
// the call accumulates rather than overwrites.  This pins the "XOR-into"
// semantic in the new contract (without it, the adjoint round-trip would
// silently still work via |0>-pre, but XOR-into is the documented
// contract for cleaner caller-side composition).
static void run_xor_into_case(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && "test precondition: x < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = (W + 1u) + W + 1u;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    SimCtx sc{n_orkan, 64u};
    RegX x   = make_reg_x(0, x_val, sc.sv());
    RegN n   = make_reg_n(static_cast<int>(W + 1u), n_val, sc.sv());
    RegLT lt = make_reg_lt(static_cast<int>(W + 1u + W));
    // Pre-flip lt_flag_out to |1> so the forward's XOR is observable.
    orkan::apply_x(sc.sv(), static_cast<uint32_t>(lt.qi));

    sturm::lib_double_mod_dsl<sturm::BitProxy>(x.bits.data(), n.bits.data(), W,
                                                lt.bit);

    const uint32_t cmp_bit   = (2u * x_val < n_val) ? 1u : 0u;
    const uint32_t expect_lt = 1u ^ cmp_bit;
    uint32_t lt_sv = read_reg(sc.sv(), &lt.qi, 1u, n_orkan);
    assert(lt_sv == expect_lt &&
           "forward XOR-into: lt_flag_out_post == lt_flag_out_pre XOR "
           "(2x_orig < n_value)");

    // Cleanup: flip lt back to |0> for LIFO release sanity.
    if (lt_sv != 0u)
        orkan::apply_x(sc.sv(), static_cast<uint32_t>(lt.qi));

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── W=3 sweep widths now share the APPEND+replay trace driver ────────────
// sturm-scin: the W=3 SIMULATE sweep driver (run_classical_case_w3) and its
// supporting Reg structs / orkan-allocate helpers have been retired in
// favour of `run_double_mod_trace_case<W3>` defined below.  The W=2
// SIMULATE smoke (run_classical_case) and the n==0 / XOR-into SIMULATE
// smokes remain at n_orkan=17 for end-to-end statevector coverage.
static constexpr std::size_t W3 = 3u;

// ── classical-trace harness (APPEND mode + bit-vector replay) ────────────────
// sturm-4oot.2: even-n random sweeps for W=4, W=5 land too far above the
// orkan 30-qubit ceiling for the simulator harness, so we drive the
// primitive in APPEND mode and replay the resulting IR bit-vector to
// verify the classical post-conditions.  Mirrors the trace harness in
// test_mul_mod_dsl_oneshot.cpp (sturm-4oot.4).  `lib_double_mod_dsl`
// emits only X / CX / CCX gates (it composes lib_add_dsl primitives,
// CNOT-based bit moves, and emit_CCX_lifted MAJ/UMA bodies); the shared
// apply_gate_classical helper in tests/lib/classical_replay.hpp aborts
// loudly on any other kind.

// Run lib_double_mod_dsl<Wn> for one (x, n) input via the trace harness.
// Asserts x_bits == (2x) mod n, x_bits[Wn]==0, n preserved, lt_flag_out ==
// (2x_orig < n_value), pool live-count restored, and every transient
// ancilla returns to |0>.
template <std::size_t Wn>
static void run_double_mod_trace_case(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && n_val < (1u << Wn));
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg_x  = static_cast<uint32_t>(Wn) + 1u;
    constexpr uint32_t n_reg_n  = static_cast<uint32_t>(Wn);
    constexpr uint32_t n_reg_lt = 1u;
    constexpr uint32_t n_reg    = n_reg_x + n_reg_n + n_reg_lt;
    int qi_x[Wn + 1u], qi_n[Wn], qi_lt;
    for (std::size_t i = 0; i < Wn + 1u; ++i)
        qi_x[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    qi_lt = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    sturm::qbool x_own[Wn + 1u], n_own[Wn], lt_own;
    sturm::BitProxy x_bits[Wn + 1u], n_bits[Wn], lt_bit;
    for (std::size_t i = 0; i < Wn + 1u; ++i) {
        x_own[i]  = sturm::qbool::make_non_owning(qi_x[i]);
        x_bits[i] = sturm::BitProxy(x_own[i]);
    }
    for (std::size_t i = 0; i < Wn; ++i) {
        n_own[i]  = sturm::qbool::make_non_owning(qi_n[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
    }
    lt_own = sturm::qbool::make_non_owning(qi_lt);
    lt_bit = sturm::BitProxy(lt_own);

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_double_mod_dsl<sturm::BitProxy>(x_bits, n_bits, Wn, lt_bit);

    const int high_water = sturm::QubitPool::instance().high_water();

    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < Wn; ++i) {
        if ((x_val >> i) & 1u) bits[static_cast<std::size_t>(qi_x[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
    }
    // x_bits[Wn] (overflow slot), lt_bit start |0> (already zero).
    sturm::test_helpers::replay_ir(ctx->ir, bits);

    const uint32_t expect_x  = (2u * x_val) % n_val;
    const uint32_t expect_lt = (2u * x_val < n_val) ? 1u : 0u;
    using sturm::test_helpers::read_reg_classical;
    assert(read_reg_classical(bits, qi_x, Wn) == expect_x
           && "trace: x_bits[0..Wn-1] == (2x) mod n");
    assert(bits[static_cast<std::size_t>(qi_x[Wn])] == 0u
           && "trace: x_bits[Wn] (overflow slot) returned to |0>");
    assert(read_reg_classical(bits, qi_n, Wn) == n_val
           && "trace: n_bits unchanged");
    assert(bits[static_cast<std::size_t>(qi_lt)] == expect_lt
           && "trace: lt_flag_out == (2x_orig < n_value)");
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u && "trace: ancilla not cleaned");
    }
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "trace: pool live-count restored");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    sturm::QubitPool::instance().release(qi_lt);
    for (std::size_t i = Wn; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn + 1u; i-- > 0;)
        sturm::QubitPool::instance().release(qi_x[i]);
}

int main() {
    std::printf("sturm-wdas.1 double-mod-dsl: n==0 no-op tests:\n");
    run_n_zero_case(/*x=*/1u, /*n=*/3u);
    std::puts("  PASS: n==0 with x=1, n=3 leaves x, n, lt_flag_out unchanged");
    run_n_zero_case(/*x=*/0u, /*n=*/0u);
    std::puts("  PASS: n==0 with x=0, n=0 (all |0>) leaves x, n, lt_flag_out unchanged");

    std::printf("sturm-wdas.2 double-mod-dsl: single classical case:\n");
    run_classical_case(/*x=*/1u, /*n=*/3u);
    std::puts("  PASS: (2 · 1) mod 3 == 2; lt_flag_out == 1 (since 2·1 < 3)");

    // sturm-wdas design note #3 — the (n+1)/2 case.
    // For n=3 (odd), (n+1)/2 = 2.  2 · 2 mod 3 = 1, with LSB = 1.  This
    // breaks any naive "halve via shift-right" forward; the conditional-
    // add-back / lt_flag_out path handles it correctly.
    std::printf("sturm-wdas double-mod-dsl: (n+1)/2 LSB-edge case "
                "(design note #3):\n");
    run_classical_case(/*x=*/2u, /*n=*/3u);
    std::puts("  PASS: (2 · 2) mod 3 == 1 (LSB=1; design-note-3 edge case); "
              "lt_flag_out == 0 (since 2·2 >= 3)");

    // sturm-4oot.1: lt_flag_out XOR-into semantic.  Pre-flip lt_flag_out
    // to |1>, run forward, observe accumulation.
    std::printf("sturm-4oot.1 double-mod-dsl: lt_flag_out XOR-into semantic:\n");
    run_xor_into_case(/*x=*/1u, /*n=*/3u);
    std::puts("  PASS: lt_flag_out_pre=1 XORs cleanly with (2·1 < 3)=1 → 0");
    run_xor_into_case(/*x=*/2u, /*n=*/3u);
    std::puts("  PASS: lt_flag_out_pre=1 XORs cleanly with (2·2 < 3)=0 → 1");

    std::printf("sturm-wdas.3/sturm-4oot.2 double-mod-dsl: W=2 exhaustive "
                "sweep (n in {1, 2, 3}, all x in [0, n); n=4 doesn't fit "
                "in a 2-bit modulus register and is hoisted to W=3 below):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_classical_case(x_val, n_val);
            ++cases_run;
        }
    }
    // n in [1, 4): n=1 → 1 case (x=0); n=2 → 2; n=3 → 3.  Total = 6.
    assert(cases_run == 6u && "W=2 sweep covered every (x, n) "
                              "with x < n and n in {1, 2, 3}");
    std::printf("  PASS: %zu W=2 cases covering every (x, n) with x < n, "
                "n in {1, 2, 3}\n", cases_run);

    // sturm-4oot.2 — W=3 even-n exhaustive sweep (n in {2, 4, 6}; n=8
    // doesn't fit in a 3-bit modulus register and is exercised at W=4
    // via the trace harness below).
    std::printf("sturm-4oot.2 double-mod-dsl: W=3 even-n exhaustive sweep "
                "(n in {2, 4, 6}, all x in [0, n)):\n");
    std::size_t cases_w3_even = 0u;
    for (uint32_t n_val : {2u, 4u, 6u}) {
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_double_mod_trace_case<W3>(x_val, n_val);
            ++cases_w3_even;
        }
    }
    // n=2 → 2; n=4 → 4; n=6 → 6.  Total = 12.
    assert(cases_w3_even == 12u
           && "W=3 even-n sweep covered every (x, n) with x < n and n even");
    std::printf("  PASS: %zu W=3 even-n cases\n", cases_w3_even);

    // sturm-wdas.4 — W=3 random sweep (50 cases, fixed seed=42).
    constexpr uint32_t    kW3Seed  = 42u;  // plan §12 risk mitigation
    constexpr std::size_t kW3Cases = 50u;
    std::printf("sturm-wdas.4 double-mod-dsl: W=3 random sweep "
                "(%zu cases, seed=%u, odd n in {1, 3, 5, 7}):\n",
                kW3Cases, kW3Seed);
    std::mt19937 rng(kW3Seed);
    // Sample n uniformly from {1, 3, 5, 7}.
    std::uniform_int_distribution<uint32_t> n_idx_dist(0u, 3u);
    static constexpr uint32_t kOddNs[4] = {1u, 3u, 5u, 7u};
    for (std::size_t i = 0; i < kW3Cases; ++i) {
        uint32_t n_val = kOddNs[n_idx_dist(rng)];
        std::uniform_int_distribution<uint32_t> x_dist(0u, n_val - 1u);
        uint32_t x_val = x_dist(rng);
        run_double_mod_trace_case<W3>(x_val, n_val);
    }
    std::printf("  PASS: %zu W=3 random cases (2 · x) mod n + lt_flag_out "
                "match classical reference\n", kW3Cases);

    // sturm-4oot.2 — W=4 exhaustive trace at n=8 (the third entry in the
    // issue's "W=3 n=2,4,6,8" list, hoisted to W=4 since it doesn't fit
    // in a 3-bit modulus register; mirrors the convention used in the
    // sturm-4oot.4 oneshot test extension).  Uses the classical-trace
    // harness because W=4 with a (W+1)-bit x register + W-bit n register +
    // 1-bit lt + ~4 transients lands close to the orkan ceiling and
    // makes the simulator a poor fit for sweep-style testing.
    constexpr std::size_t W4 = 4u;
    constexpr std::size_t W5 = 5u;
    std::array<uint32_t, 3> even_ns_4_5 = {10u, 12u, 14u};

    std::printf("sturm-4oot.2 double-mod-dsl: W=4 exhaustive trace sweep "
                "(even n=8):\n");
    {
        constexpr uint32_t n_val = 8u;
        std::size_t cases_w4_n8 = 0u;
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_double_mod_trace_case<W4>(x_val, n_val);
            ++cases_w4_n8;
        }
        assert(cases_w4_n8 == 8u);
        std::printf("  PASS: %zu W=4 trace cases at n=8\n", cases_w4_n8);
    }

    std::printf("sturm-4oot.2 double-mod-dsl: W=4 random trace spot checks "
                "(20 cases, seed=4204, even n in {10, 12, 14}):\n");
    {
        constexpr uint32_t kSeed = 4204u;
        constexpr std::size_t kCases = 20u;
        std::mt19937 rng4(kSeed);
        std::uniform_int_distribution<uint32_t> n_idx(0u, 2u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = even_ns_4_5[n_idx(rng4)];
            std::uniform_int_distribution<uint32_t> x_dist(0u, n_val - 1u);
            uint32_t x_val = x_dist(rng4);
            run_double_mod_trace_case<W4>(x_val, n_val);
        }
        std::printf("  PASS: %zu W=4 even-n trace spot checks\n", kCases);
    }

    std::printf("sturm-4oot.2 double-mod-dsl: W=5 random trace spot checks "
                "(20 cases, seed=4205, even n in {10, 12, 14}):\n");
    {
        constexpr uint32_t kSeed = 4205u;
        constexpr std::size_t kCases = 20u;
        std::mt19937 rng5(kSeed);
        std::uniform_int_distribution<uint32_t> n_idx(0u, 2u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = even_ns_4_5[n_idx(rng5)];
            std::uniform_int_distribution<uint32_t> x_dist(0u, n_val - 1u);
            uint32_t x_val = x_dist(rng5);
            run_double_mod_trace_case<W5>(x_val, n_val);
        }
        std::printf("  PASS: %zu W=5 even-n trace spot checks\n", kCases);
    }

    // sturm-8n73: high-W trace spot checks validating kMaxN > 32 works.
    // double_mod peaks at ~3W+5; sturm-5jta dropped the legacy compile-
    // time pool cap, so any W is supported (the pool grows on demand).
    // W=64 path is also exercised by the dedicated
    // test_modular_arith_highw_trace target.
    constexpr std::size_t W8_hi  = 8u;
    constexpr std::size_t W16_hi = 16u;

    std::printf("sturm-8n73 double-mod-dsl: W=8 random trace spot checks "
                "(10 cases, seed=8073):\n");
    {
        constexpr uint32_t kSeed = 8073u;
        constexpr std::size_t kCases = 10u;
        std::mt19937 rng8(kSeed);
        std::uniform_int_distribution<uint32_t> n_dist(2u,
                                                        (1u << W8_hi) - 1u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = n_dist(rng8);
            std::uniform_int_distribution<uint32_t> x_dist(0u, n_val - 1u);
            uint32_t x_val = x_dist(rng8);
            run_double_mod_trace_case<W8_hi>(x_val, n_val);
        }
        std::printf("  PASS: %zu W=8 double-mod trace spot checks\n",
                    kCases);
    }

    std::printf("sturm-8n73 double-mod-dsl: W=16 random trace spot checks "
                "(5 cases, seed=8074):\n");
    {
        constexpr uint32_t kSeed = 8074u;
        constexpr std::size_t kCases = 5u;
        std::mt19937 rng16(kSeed);
        std::uniform_int_distribution<uint32_t> n_dist(2u,
                                                        (1u << W16_hi) - 1u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = n_dist(rng16);
            std::uniform_int_distribution<uint32_t> x_dist(0u, n_val - 1u);
            uint32_t x_val = x_dist(rng16);
            run_double_mod_trace_case<W16_hi>(x_val, n_val);
        }
        std::printf("  PASS: %zu W=16 double-mod trace spot checks\n",
                    kCases);
    }

    std::printf("All sturm-wdas/sturm-4oot.1/sturm-4oot.2 double-mod-dsl "
                "forward tests passed.\n");
    return 0;
}
