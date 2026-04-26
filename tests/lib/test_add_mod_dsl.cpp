// test_add_mod_dsl.cpp -- sturm-yh3d.{1,2,3} P1 beats 1.1, 1.2, 1.3.
//
// Plan §3.3 beat 1.1: assert that `lib_add_mod_dsl` short-circuits when the
// width parameter `n` is 0 and leaves the `r` register unchanged.
//
// Plan §3.3 beat 1.2: assert that the full algorithm computes
// (a + b) mod n correctly for the single classical case W=2, (a=1, b=1, n=3).
// The expected result is r = 2.  The implementation must (per plan §3.1):
//   1. allocate a (W+1)-bit sum register `s`,
//   2. copy a into s_low via per-bit XOR,
//   3. lib_add_dsl(b, s_low, s_high, W) so s = a+b,
//   4. compute a "needs subtract" flag from a (W+1)-bit comparison
//      against n_extended,
//   5. controlled subtract of n via the gate-reverse of lib_add_dsl,
//   6. copy s_low into r via per-bit XOR,
//   7. paired uncompute of the flag and the controlled subtract,
//   8. uncompute s LIFO so the pool returns to its pre-call state.
//
// Plan §3.3 beat 1.3: exhaustive W=2 sweep over all (a, b, n) with the
// PRD §5 precondition `a, b < n` and `n >= 1`.  For W=2 that's 14 inputs
// (n=1: 1, n=2: 4, n=3: 9 = 14 total).  Each call must produce
// r = (a+b) mod n while leaving a, b, n unchanged and returning the qubit
// pool to its pre-call live-count (no leaked ancillas).
//
// In addition to checking r, the test asserts that a, b, n are unchanged
// (reversibility of inputs) and that QubitPool::live_count() returns to its
// pre-call value (no leaked ancillas).  Beats 1.4..1.7 expand the coverage
// (W=3 random, adjoint round-trip, ancilla counter, pool live-count).

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/add_mod_dsl.hpp"
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
// Beat 1.2's algorithm needs a (W+1)-bit s register, an n_pad qubit, an
// lt_flag, a carry_anc, plus the transient ancillas inside the lib_add_*
// calls and their controlled-Toffoli folds.  Sizing the simulator at the
// hard 17-qubit cap (kMaxQubits) gives us the headroom every branch needs
// without forcing the budget to be re-tuned per beat.
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

// Beat 1.1: n==0 short-circuits, leaves r (and a, b, n) unchanged.
static void run_n_zero_case(uint32_t a_val, uint32_t b_val,
                            uint32_t n_val, uint32_t r_val) {
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W;  // a, b, n, r
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    SimCtx sc{n_orkan, 64u};
    Reg a = make_reg(0,         a_val, sc.sv());
    Reg b = make_reg(W,         b_val, sc.sv());
    Reg n = make_reg(2 * W,     n_val, sc.sv());
    Reg r = make_reg(3 * W,     r_val, sc.sv());

    // Call with width n == 0; stub must short-circuit silently.
    sturm::lib_add_mod_dsl<sturm::BitProxy>(a.bits.data(), b.bits.data(),
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

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// Beat 1.2: full algorithm, single classical case (a, b, n=3) -> r = (a+b) % n.
//
// Asserts:
//   - r register holds (a + b) mod n,
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
    SimCtx sc{n_orkan, 64u};
    Reg a = make_reg(0,         a_val, sc.sv());
    Reg b = make_reg(W,         b_val, sc.sv());
    Reg n = make_reg(2 * W,     n_val, sc.sv());
    Reg r = make_reg(3 * W,     0u,    sc.sv());

    sturm::lib_add_mod_dsl<sturm::BitProxy>(a.bits.data(), b.bits.data(),
                                            n.bits.data(), W,
                                            r.bits.data());

    const uint32_t expect_r = (a_val + b_val) % n_val;
    uint32_t a_sv = read_reg(sc.sv(), a.qi.data(), W, n_orkan);
    uint32_t b_sv = read_reg(sc.sv(), b.qi.data(), W, n_orkan);
    uint32_t n_sv = read_reg(sc.sv(), n.qi.data(), W, n_orkan);
    uint32_t r_sv = read_reg(sc.sv(), r.qi.data(), W, n_orkan);
    assert(a_sv == a_val && "forward: a register unchanged");
    assert(b_sv == b_val && "forward: b register unchanged");
    assert(n_sv == n_val && "forward: n register unchanged");
    assert(r_sv == expect_r && "forward: r == (a+b) mod n");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "forward: pool live-count returns to pre-call value");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-yh3d.1 P1.1 add-mod-dsl: n==0 no-op tests:\n");
    run_n_zero_case(/*a=*/1u, /*b=*/2u, /*n=*/3u, /*r=*/0u);
    std::puts("  PASS: n==0 with r=|0> leaves r at 0");
    run_n_zero_case(/*a=*/1u, /*b=*/2u, /*n=*/3u, /*r=*/3u);
    std::puts("  PASS: n==0 with r=3 leaves r at 3");
    run_n_zero_case(/*a=*/0u, /*b=*/0u, /*n=*/0u, /*r=*/2u);
    std::puts("  PASS: n==0 with all-zero inputs and r=2 leaves r at 2");

    std::printf("sturm-yh3d.2 P1.2 add-mod-dsl: single classical case:\n");
    run_classical_case(/*a=*/1u, /*b=*/1u, /*n=*/3u);
    std::puts("  PASS: (1 + 1) mod 3 == 2");

    std::printf("sturm-yh3d.3 P1.3 add-mod-dsl: W=2 exhaustive sweep "
                "(all a, b in [0, n) for n in [1, 4)):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_classical_case(a_val, b_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 → 1 case; n=2 → 4 cases; n=3 → 9 cases; total = 14 cases.
    assert(cases_run == 14u && "W=2 sweep covered every (a, b, n) "
                                "with a, b < n and n >= 1");
    std::printf("  PASS: %zu W=2 cases covering every (a, b, n) "
                "with a, b < n, n >= 1\n", cases_run);

    std::printf("All sturm-yh3d.{1,2,3} tests passed.\n");
    return 0;
}
