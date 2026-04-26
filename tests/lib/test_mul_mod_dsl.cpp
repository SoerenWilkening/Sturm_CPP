// test_mul_mod_dsl.cpp -- sturm-kubb.1 P2.1 mul-mod-dsl beat 2.1.
//
// Plan §4.3 beat 2.1: `lib_mul_mod_dsl(... n=0 ...)` short-circuits and
// leaves r (and a, b, n) unchanged.  This mirrors add-mod beat 1.1
// (sturm-yh3d.1) — the stub forward header
// `include/sturm/lib/mul_mod_dsl.hpp` already silences itself when n==0
// and asserts otherwise; this test exercises the n==0 no-op path so the
// scaffold is verifiably wired.  Subsequent beats (2.2…2.7) will fill
// in the full algorithm and broaden coverage; beat 2.1's job is to
// guarantee that the n==0 short-circuit lands callers back at the
// pre-call register state with no leaked ancillas.
//
// Test harness layout mirrors test_add_mod_dsl.cpp (beat 1.1):
//   - SimCtx wraps OrkanBridge + sturm_backend_context_t with a 17-qubit
//     state vector (kMaxQubits cap) — well above the four W=2 input
//     registers (4*W = 8) the n==0 path needs.
//   - read_reg decodes a register's classical value out of the simulator
//     state vector by scanning for the unique non-zero amplitude.
//   - run_n_zero_case asserts a, b, n, r are all unchanged after the
//     n==0 call.

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

int main() {
    std::printf("sturm-kubb.1 P2.1 mul-mod-dsl: n==0 no-op tests:\n");
    run_n_zero_case(/*a=*/1u, /*b=*/2u, /*n=*/3u, /*r=*/0u);
    std::puts("  PASS: n==0 with r=|0> leaves r at 0");
    run_n_zero_case(/*a=*/1u, /*b=*/2u, /*n=*/3u, /*r=*/3u);
    std::puts("  PASS: n==0 with r=3 leaves r at 3");
    run_n_zero_case(/*a=*/0u, /*b=*/0u, /*n=*/0u, /*r=*/2u);
    std::puts("  PASS: n==0 with all-zero inputs and r=2 leaves r at 2");
    std::printf("All sturm-kubb.1 tests passed.\n");
    return 0;
}
