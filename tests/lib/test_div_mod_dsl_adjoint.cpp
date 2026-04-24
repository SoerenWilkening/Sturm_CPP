// test_div_mod_dsl_adjoint.cpp -- LO-1b (sturm-1rjw): div/mod DSL adjoint.
//
// Contract: after `invert(lib_div_dsl)(a, b, q, r)` (resp. lib_mod_dsl) runs
// in the PRD §2.1 swap-then-uncompute shape, the quotient (q) and remainder
// (r) ancillas are returned to |0>.  See docs/plan_lossy_compound_reversibility
// §3 LO-1b.
//
// Unlike c_AND / OR, lib_div_dsl is NOT self-inverse.  The adjoint implements
// the reverse gate schedule (every constituent gate is self-inverse, so
// only order-of-operations changes).  For mod, the forward factors a
// lib_div_dsl call plus a quotient-uncompute via a second div into temp_rem
// and an XOR-zero; its adjoint runs those three phases in reverse.
//
// LO rewrite shape (PRD §2.1) for `a /= b` after compute+swap-undo:
//   state before adjoint: (a=old_a, b=b, tmp_q=old_a/b, tmp_r=old_a%b)
//   invariant (1): a == tmp_q * b + tmp_r         ← enforced by divide
//   invariant (2): invert(lib_div_dsl)(a, b, tmp_q, tmp_r) zeros tmp_q, tmp_r
//                  while preserving a and b.
//
// For `a %= b`:
//   state before adjoint: (a=old_a, b=b, tmp_r=old_a%b)
//   invariant: invert(lib_mod_dsl)(a, b, tmp_r) zeros tmp_r.

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/div_dsl.hpp"
#include "sturm/lib/mod_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/routines/invert.hpp"
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
static constexpr uint32_t n_orkan = 17u;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 128u) {
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

// ── Forward/adjoint roundtrip for lib_div_dsl. ───────────────────────────────
static void run_div_case(uint32_t a_val, uint32_t b_val) {
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W;  // a, b, q, r
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    SimCtx sc{n_orkan, 128u};
    Reg a = make_reg(0,     a_val, sc.sv());
    Reg b = make_reg(W,     b_val, sc.sv());
    Reg q = make_reg(2 * W, 0u,    sc.sv());
    Reg r = make_reg(3 * W, 0u,    sc.sv());

    sturm::lib_div_dsl<sturm::BitProxy>(a.bits.data(), W, b.bits.data(), W,
                                        q.bits.data(), r.bits.data());

    const uint32_t expect_q = (b_val != 0u) ? (a_val / b_val) : 0u;
    const uint32_t expect_r = (b_val != 0u) ? (a_val % b_val) : 0u;
    uint32_t a_sv = read_reg(sc.sv(), a.qi.data(), W, n_orkan);
    uint32_t b_sv = read_reg(sc.sv(), b.qi.data(), W, n_orkan);
    uint32_t qv   = read_reg(sc.sv(), q.qi.data(), W, n_orkan);
    uint32_t rv   = read_reg(sc.sv(), r.qi.data(), W, n_orkan);
    assert(a_sv == a_val && "forward: a preserved");
    assert(b_sv == b_val && "forward: b preserved");
    assert(qv == expect_q && "forward: q == a/b");
    assert(rv == expect_r && "forward: r == a%b");
    assert(qv * b_sv + rv == a_sv && "invariant a == q*b + r post-swap-undo");

    constexpr auto adj_ptr =
        sturm::invert(&sturm::lib_div_dsl<sturm::BitProxy>);
    static_assert(adj_ptr != nullptr,
                  "invert(lib_div_dsl<BitProxy>) must resolve to registered adjoint");
    adj_ptr(a.bits.data(), W, b.bits.data(), W, q.bits.data(), r.bits.data());

    qv   = read_reg(sc.sv(), q.qi.data(), W, n_orkan);
    rv   = read_reg(sc.sv(), r.qi.data(), W, n_orkan);
    a_sv = read_reg(sc.sv(), a.qi.data(), W, n_orkan);
    b_sv = read_reg(sc.sv(), b.qi.data(), W, n_orkan);
    assert(qv == 0u && "adjoint: q ancilla returned to |0>");
    assert(rv == 0u && "adjoint: r ancilla returned to |0>");
    assert(a_sv == a_val && "adjoint: a preserved");
    assert(b_sv == b_val && "adjoint: b preserved");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── Forward/adjoint roundtrip for lib_mod_dsl. ───────────────────────────────
static void run_mod_case(uint32_t a_val, uint32_t b_val) {
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 3u * W;  // a, b, r
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    SimCtx sc{n_orkan, 128u};
    Reg a = make_reg(0,     a_val, sc.sv());
    Reg b = make_reg(W,     b_val, sc.sv());
    Reg r = make_reg(2 * W, 0u,    sc.sv());

    sturm::lib_mod_dsl<sturm::BitProxy>(a.bits.data(), W, b.bits.data(), W,
                                        r.bits.data());

    const uint32_t expect_r = (b_val != 0u) ? (a_val % b_val) : 0u;
    uint32_t rv   = read_reg(sc.sv(), r.qi.data(), W, n_orkan);
    uint32_t a_sv = read_reg(sc.sv(), a.qi.data(), W, n_orkan);
    uint32_t b_sv = read_reg(sc.sv(), b.qi.data(), W, n_orkan);
    assert(rv == expect_r && "forward: r == a%b");
    assert(a_sv == a_val && "forward: a preserved");
    assert(b_sv == b_val && "forward: b preserved");

    constexpr auto adj_ptr =
        sturm::invert(&sturm::lib_mod_dsl<sturm::BitProxy>);
    static_assert(adj_ptr != nullptr,
                  "invert(lib_mod_dsl<BitProxy>) must resolve to registered adjoint");
    adj_ptr(a.bits.data(), W, b.bits.data(), W, r.bits.data());

    rv   = read_reg(sc.sv(), r.qi.data(), W, n_orkan);
    a_sv = read_reg(sc.sv(), a.qi.data(), W, n_orkan);
    b_sv = read_reg(sc.sv(), b.qi.data(), W, n_orkan);
    assert(rv == 0u && "adjoint: r ancilla returned to |0>");
    assert(a_sv == a_val && "adjoint: a preserved");
    assert(b_sv == b_val && "adjoint: b preserved");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-1rjw LO-1b: lib_div/mod_dsl adjoint tests:\n");
    run_div_case(3u, 2u); std::puts("  PASS: div_adj 3/2=1 rem 1");
    run_div_case(2u, 1u); std::puts("  PASS: div_adj 2/1=2 rem 0");
    run_div_case(0u, 1u); std::puts("  PASS: div_adj 0/1=0 rem 0");
    run_div_case(3u, 3u); std::puts("  PASS: div_adj 3/3=1 rem 0");
    run_div_case(1u, 2u); std::puts("  PASS: div_adj 1/2=0 rem 1");
    run_mod_case(3u, 2u); std::puts("  PASS: mod_adj 3%2=1");
    run_mod_case(2u, 1u); std::puts("  PASS: mod_adj 2%1=0");
    run_mod_case(0u, 1u); std::puts("  PASS: mod_adj 0%1=0");
    run_mod_case(3u, 3u); std::puts("  PASS: mod_adj 3%3=0");
    run_mod_case(1u, 2u); std::puts("  PASS: mod_adj 1%2=1");
    std::printf("All sturm-1rjw tests passed.\n");
    return 0;
}
