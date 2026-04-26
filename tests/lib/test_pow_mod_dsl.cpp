// test_pow_mod_dsl.cpp -- sturm-a5te.1 P3.1 pow-mod-dsl beat 3.1.
//
// Plan §5.3 beat 3.1: `lib_pow_mod_dsl(... n=0 ...)` short-circuits and
// leaves r (and base, exp, n) unchanged.  Mirrors PRD §8 #3 and the
// shape of add-mod beat 1.1 (sturm-yh3d.1) / mul-mod beat 2.1
// (sturm-kubb.1).
//
// Pow-mod signature differs only in argument names: it is
// `(base_bits, exp_bits, n_bits, n, r_bits)`.  The forward header
// `include/sturm/lib/pow_mod_dsl.hpp` is currently a stub that
// short-circuits on n==0 and asserts otherwise — beat 3.1 just
// exercises that path.
//
// Asserts:
//   - the call returns without firing the assert(false) inside the stub,
//   - base, exp, n, r registers are bit-identical to their pre-call
//     values,
//   - QubitPool::in_use() returns to its pre-call value (no leaked
//     ancillas; the n==0 branch must not allocate any).
//
// Test harness layout mirrors test_add_mod_dsl.cpp / test_mul_mod_dsl.cpp:
//   - SimCtx wraps OrkanBridge + sturm_backend_context_t with a 17-qubit
//     state vector (kMaxQubits cap).  At W=2 with the four input
//     registers (base, exp, n, r) we need 4*W = 8 qubits; sizing the
//     simulator at the kMaxQubits=17 cap leaves headroom for whatever
//     beats 3.2+ wire on top of this harness.
//   - read_reg decodes a register's classical value out of the
//     simulator state vector by scanning for the unique non-zero
//     amplitude.
//   - run_n_zero_case asserts base, exp, n, r are all unchanged after
//     the n==0 call.

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/pow_mod_dsl.hpp"
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

// Beat 3.1: n==0 short-circuits, leaves r (and base, exp, n) unchanged.
//
// Asserts:
//   - the call returns without firing the assert(false) inside the stub,
//   - base, exp, n, r registers are bit-identical to their pre-call values,
//   - QubitPool::in_use() returns to its pre-call value (no leaked
//     ancillas; the n==0 branch must not allocate any).
static void run_n_zero_case(uint32_t base_val, uint32_t exp_val,
                            uint32_t n_val, uint32_t r_val) {
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W;  // base, exp, n, r
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan, 64u};
    Reg base = make_reg(0,         base_val, sc.sv());
    Reg exp_ = make_reg(W,         exp_val,  sc.sv());
    Reg n    = make_reg(2 * W,     n_val,    sc.sv());
    Reg r    = make_reg(3 * W,     r_val,    sc.sv());

    // Call with width n == 0; stub must short-circuit silently.
    sturm::lib_pow_mod_dsl<sturm::BitProxy>(base.bits.data(), exp_.bits.data(),
                                            n.bits.data(), /*n=*/0u,
                                            r.bits.data());

    uint32_t base_sv = read_reg(sc.sv(), base.qi.data(), W, n_orkan);
    uint32_t exp_sv  = read_reg(sc.sv(), exp_.qi.data(), W, n_orkan);
    uint32_t n_sv    = read_reg(sc.sv(), n.qi.data(),    W, n_orkan);
    uint32_t r_sv    = read_reg(sc.sv(), r.qi.data(),    W, n_orkan);
    assert(base_sv == base_val && "n==0: base register unchanged");
    assert(exp_sv  == exp_val  && "n==0: exp register unchanged");
    assert(n_sv    == n_val    && "n==0: n register unchanged");
    assert(r_sv    == r_val    && "n==0: r register unchanged (no-op)");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "n==0: pool live-count unchanged (no ancilla allocated)");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-a5te.1 P3.1 pow-mod-dsl: n==0 no-op tests:\n");
    run_n_zero_case(/*base=*/1u, /*exp=*/2u, /*n=*/3u, /*r=*/0u);
    std::puts("  PASS: n==0 with r=|0> leaves r at 0");
    run_n_zero_case(/*base=*/1u, /*exp=*/2u, /*n=*/3u, /*r=*/3u);
    std::puts("  PASS: n==0 with r=3 leaves r at 3");
    run_n_zero_case(/*base=*/0u, /*exp=*/0u, /*n=*/0u, /*r=*/2u);
    std::puts("  PASS: n==0 with all-zero inputs and r=2 leaves r at 2");

    std::printf("All sturm-a5te.1 tests passed.\n");
    return 0;
}
