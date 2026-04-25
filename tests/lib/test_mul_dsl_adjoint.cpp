// test_mul_dsl_adjoint.cpp -- LO-1a (sturm-735v): lib_mul_dsl adjoint roundtrip.
//
// Contract: after `invert<&lib_mul_dsl>()(a, aw, b, bw, result, rw)` runs in
// the swap-then-uncompute shape, the result register (tmp_mul
// post-swap-undo) is returned to |0>.
//
// Unlike c_AND / OR, lib_mul_dsl is NOT self-inverse.  The adjoint runs the
// reverse gate schedule: the b-bit loop runs from bw-1 down to 0, and each
// `lib_add_dsl` call is replaced by its gate-reverse `__lib_add_dsl_adj`.
//
// LO rewrite shape (PRD §2.1) for `a *= b` after compute+swap-undo:
//   state before adjoint: (a=old_a, b=b, tmp_mul=old_a*b)
//   invariant: invert<&lib_mul_dsl>()(a, aw, b, bw, tmp_mul, rw) zeros tmp_mul
//              while preserving a and b.

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/mul_dsl.hpp"
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
static constexpr std::size_t W = 2;       // a_width = b_width = 2
static constexpr std::size_t RW = 2 * W;  // result_width = 4
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

template <std::size_t N>
struct Reg {
    std::array<int, N>              qi;
    std::array<sturm::qbool, N>     owners;
    std::array<sturm::BitProxy, N>  bits;
};

template <std::size_t N>
static Reg<N> make_reg(int base, uint32_t val, orkan::state_t& sv) {
    Reg<N> r;
    for (std::size_t i = 0; i < N; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if ((val >> i) & 1u) orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
    }
    for (std::size_t i = 0; i < N; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(r.qi[i]);
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

// ── Forward/adjoint roundtrip for lib_mul_dsl. ───────────────────────────────
static void run_mul_case(uint32_t a_val, uint32_t b_val) {
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 2u * W + RW;  // a, b, result
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    SimCtx sc{n_orkan, 128u};
    auto a   = make_reg<W>(0,        a_val, sc.sv());
    auto b   = make_reg<W>(W,        b_val, sc.sv());
    auto res = make_reg<RW>(2 * W,   0u,    sc.sv());

    sturm::lib_mul_dsl<sturm::BitProxy>(
        a.bits.data(), W, b.bits.data(), W, res.bits.data(), RW);

    const uint32_t expect_prod = (a_val * b_val) & ((1u << RW) - 1u);
    uint32_t a_sv   = read_reg(sc.sv(), a.qi.data(),   W,  n_orkan);
    uint32_t b_sv   = read_reg(sc.sv(), b.qi.data(),   W,  n_orkan);
    uint32_t res_sv = read_reg(sc.sv(), res.qi.data(), RW, n_orkan);
    assert(a_sv == a_val && "forward: a preserved");
    assert(b_sv == b_val && "forward: b preserved");
    assert(res_sv == expect_prod && "forward: result == a * b");

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_mul_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_mul_dsl<BitProxy>>() must resolve to registered adjoint");
    adj_ptr(a.bits.data(), W, b.bits.data(), W, res.bits.data(), RW);

    a_sv   = read_reg(sc.sv(), a.qi.data(),   W,  n_orkan);
    b_sv   = read_reg(sc.sv(), b.qi.data(),   W,  n_orkan);
    res_sv = read_reg(sc.sv(), res.qi.data(), RW, n_orkan);
    assert(res_sv == 0u && "adjoint: result ancilla returned to |0>");
    assert(a_sv == a_val && "adjoint: a preserved");
    assert(b_sv == b_val && "adjoint: b preserved");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-735v LO-1a: lib_mul_dsl adjoint tests:\n");
    run_mul_case(0u, 0u); std::puts("  PASS: mul_adj 0*0=0");
    run_mul_case(0u, 3u); std::puts("  PASS: mul_adj 0*3=0");
    run_mul_case(3u, 0u); std::puts("  PASS: mul_adj 3*0=0");
    run_mul_case(1u, 1u); std::puts("  PASS: mul_adj 1*1=1");
    run_mul_case(2u, 1u); std::puts("  PASS: mul_adj 2*1=2");
    run_mul_case(1u, 2u); std::puts("  PASS: mul_adj 1*2=2");
    run_mul_case(2u, 3u); std::puts("  PASS: mul_adj 2*3=6");
    run_mul_case(3u, 2u); std::puts("  PASS: mul_adj 3*2=6");
    run_mul_case(3u, 3u); std::puts("  PASS: mul_adj 3*3=9");
    std::printf("All sturm-735v tests passed.\n");
    return 0;
}
