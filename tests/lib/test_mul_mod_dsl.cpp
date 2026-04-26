// test_mul_mod_dsl.cpp -- sturm-kubb.{1,2} P2.{1,2} mul-mod-dsl beats 2.1, 2.2.
//
// Plan §4.3 beat 2.1: `lib_mul_mod_dsl(... n=0 ...)` short-circuits and
// leaves r (and a, b, n) unchanged.  This mirrors add-mod beat 1.1
// (sturm-yh3d.1).
// Plan §4.3 beat 2.2: full shift-and-add algorithm produces
// `r = (a * b) mod n` for the single classical W=2 case.  Per plan §4.1
// the body composes `lib_add_mod_dsl` in a doubling-and-add loop using a
// `shifted` ancilla register chain that starts as `a` and at each step
// holds `(a · 2^i) mod n`.  Beat 2.2 wires up the smallest body that
// gets one classical input correct; beats 2.3–2.7 broaden coverage,
// adjoint round-trip, ancilla budget, and pool live-count.
//
// Test harness layout mirrors test_add_mod_dsl.cpp (beat 1.{1,2,3,4}):
//   - SimCtx wraps OrkanBridge + sturm_backend_context_t with a 17-qubit
//     state vector (kMaxQubits cap) for the n==0 no-op tests; beat 2.2
//     uses the larger orkan stub directly (orkan::allocate(bridge.state,
//     n_orkan_w2_full)) because the W=2 chain algorithm peaks above 17
//     qubits — same workaround as test_add_mod_dsl.cpp's W=3 sweep.
//   - read_reg decodes a register's classical value out of the simulator
//     state vector by scanning for the unique non-zero amplitude.
//   - run_n_zero_case asserts a, b, n, r are all unchanged after the
//     n==0 call.
//   - run_classical_case asserts r == (a*b) mod n, that a, b, n are
//     unchanged (reversibility of inputs), and that QubitPool::in_use()
//     returns to its pre-call value (no leaked ancillas).

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/mul_mod_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;
static constexpr std::size_t W = 2;
// W=2 with the four input registers (a, b, n, r) needs 4*W = 8 qubits
// for the n==0 no-op path; sizing the simulator at the kMaxQubits=17 cap
// leaves headroom for whatever beats 2.2+ wire on top of this harness.
static constexpr uint32_t n_orkan = 17u;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 64u) {
        bridge.allocate(n_q);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
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

struct Reg {
    std::array<int, W>              qi;
    std::array<sturm::qbool, W>     owners;
    std::array<sturm::BitProxy, W>  bits;
};

static Reg make_reg(int base, uint32_t val, orkan::state_t& sv) {
    Reg r;
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

// Beat 2.1: n==0 short-circuits, leaves r (and a, b, n) unchanged.
//
// Asserts:
//   - the call returns without firing the assert(false) inside the stub,
//   - a, b, n, r registers are bit-identical to their pre-call values,
//   - QubitPool::in_use() returns to its pre-call value (no leaked
//     ancillas; the n==0 branch must not allocate any).
static void run_n_zero_case(uint32_t a_val, uint32_t b_val,
                            uint32_t n_val, uint32_t r_val) {
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W;  // a, b, n, r
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan, 64u};
    Reg a = make_reg(0,         a_val, sc.sv());
    Reg b = make_reg(W,         b_val, sc.sv());
    Reg n = make_reg(2 * W,     n_val, sc.sv());
    Reg r = make_reg(3 * W,     r_val, sc.sv());

    // Call with width n == 0; stub must short-circuit silently.
    sturm::lib_mul_mod_dsl<sturm::BitProxy>(a.bits.data(), b.bits.data(),
                                            n.bits.data(), /*n=*/0u,
                                            r.bits.data());

    uint32_t a_sv = read_reg(sc.sv(), a.qi.data(), W, n_orkan);
    uint32_t b_sv = read_reg(sc.sv(), b.qi.data(), W, n_orkan);
    uint32_t n_sv = read_reg(sc.sv(), n.qi.data(), W, n_orkan);
    uint32_t r_sv = read_reg(sc.sv(), r.qi.data(), W, n_orkan);
    assert(a_sv == a_val && "n==0: a register unchanged");
    assert(b_sv == b_val && "n==0: b register unchanged");
    assert(n_sv == n_val && "n==0: n register unchanged");
    assert(r_sv == r_val && "n==0: r register unchanged (no-op)");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "n==0: pool live-count unchanged (no ancilla allocated)");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── Beat 2.2 — W=2 single classical case harness ─────────────────────────
//
// The chain-style shift-and-add algorithm (plan §4.1) keeps
//   shifted_chain[0..W-1]  (W·W qubits)  – (a · 2^i) mod n cache
//   r_chain[1..W]          (W·W qubits)  – partial sums
// alive across the loop, so peak live qubits exceed the kMaxQubits=17
// soft cap.  Bypass with orkan::allocate (same trick test_add_mod_dsl.cpp
// uses for its W=3 random sweep).  25 qubits stays within the orkan
// stub's 30-qubit ceiling.
static constexpr uint32_t n_orkan_w2_full = 25u;

// Beat 2.2: full algorithm, single classical case (a, b, n=3) -> r = (a*b) % n.
//
// Asserts:
//   - r register holds (a * b) mod n,
//   - a, b, n registers unchanged,
//   - QubitPool::in_use() returns to its pre-call value (no leaked ancillas).
static void run_classical_case(uint32_t a_val, uint32_t b_val,
                               uint32_t n_val) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    sturm::OrkanBridge bridge;
    orkan::allocate(bridge.state(), n_orkan_w2_full);  // bypass kMaxQubits cap
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_SIMULATE, 64u);
    assert(ctx);
    ctx->orkan_state_ptr = &bridge;
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);
    orkan::state_t& sv = bridge.state();
    Reg a = make_reg(0,         a_val, sv);
    Reg b = make_reg(W,         b_val, sv);
    Reg n = make_reg(2 * W,     n_val, sv);
    Reg r = make_reg(3 * W,     0u,    sv);

    sturm::lib_mul_mod_dsl<sturm::BitProxy>(a.bits.data(), b.bits.data(),
                                            n.bits.data(), W,
                                            r.bits.data());

    const uint32_t expect_r = (a_val * b_val) % n_val;
    uint32_t a_sv = read_reg(sv, a.qi.data(), W, n_orkan_w2_full);
    uint32_t b_sv = read_reg(sv, b.qi.data(), W, n_orkan_w2_full);
    uint32_t n_sv = read_reg(sv, n.qi.data(), W, n_orkan_w2_full);
    uint32_t r_sv = read_reg(sv, r.qi.data(), W, n_orkan_w2_full);
    assert(a_sv == a_val && "forward: a register unchanged");
    assert(b_sv == b_val && "forward: b register unchanged");
    assert(n_sv == n_val && "forward: n register unchanged");
    assert(r_sv == expect_r && "forward: r == (a*b) mod n");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "forward: pool live-count returns to pre-call value");
    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-kubb.1 P2.1 mul-mod-dsl: n==0 no-op tests:\n");
    run_n_zero_case(/*a=*/1u, /*b=*/2u, /*n=*/3u, /*r=*/0u);
    std::puts("  PASS: n==0 with r=|0> leaves r at 0");
    run_n_zero_case(/*a=*/1u, /*b=*/2u, /*n=*/3u, /*r=*/3u);
    std::puts("  PASS: n==0 with r=3 leaves r at 3");
    run_n_zero_case(/*a=*/0u, /*b=*/0u, /*n=*/0u, /*r=*/2u);
    std::puts("  PASS: n==0 with all-zero inputs and r=2 leaves r at 2");

    std::printf("sturm-kubb.2 P2.2 mul-mod-dsl: single classical case:\n");
    run_classical_case(/*a=*/2u, /*b=*/2u, /*n=*/3u);
    std::printf("  PASS: (2 * 2) mod 3 == 1\n");

    std::printf("All sturm-kubb.{1,2} tests passed.\n");
    return 0;
}
