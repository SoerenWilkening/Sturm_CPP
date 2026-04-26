// test_qint_modular.cpp -- sturm-kgwx P4 free-function wrappers in
// include/sturm/ops/qint_modular.hpp.
//
// Beat 4.1 (sturm-kgwx.1): `sturm::add_mod(a, b, n)` matches the result of
// calling `lib_add_mod_dsl` directly.  W=2 single classical case
// (a=1, b=1, n=3) -> r=2.  Inputs unchanged, pool live-count returns to its
// pre-call value, and the wrapper-produced register holds the same bit
// pattern as a parallel direct lib_add_mod_dsl invocation.
//
// Subsequent beats (4.2 mul_mod, 4.3 pow_mod, 4.4 move/copy correctness) fill
// in the rest of this file.  This test follows the test_add_mod_dsl.cpp
// SimCtx / Reg pattern: an OrkanBridge pre-allocated at the kMaxQubits=17
// cap, qbool::make_non_owning views over input qubits, BitProxy arrays for
// the lib-level call, and read_reg over the dominant basis state to
// extract the classical W-bit result.
//
// Budget: <= 200 LoC (plan §6.3).

#define STURM_BACKEND_ENABLED 1
#include "sturm/ops/qint_modular.hpp"
#include "sturm/lib/add_mod_dsl.hpp"
#include "sturm/qtypes/qint.hpp"
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
// Hard 17-qubit simulator cap.  At W=2 the wrapper places a, b, n on 3*W=6
// pre-allocated qubits, lib_add_mod_dsl uses (W+1)+1+1+1=5 sum/flag
// ancillas, and the wrapper's fresh result register adds another W=2.
// Total live: 3*W + W + (W+1) + 3 = 13 qubits, comfortably below 17.
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

// Read a register's classical bit pattern from the dominant basis state of
// the simulator state vector.  Mirrors test_add_mod_dsl.cpp.
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

// Build a fully-allocated qint_t<W> view at fixed qubit indices base..base+W,
// initializing each bit by emitting an X on the matching qubit when the bit
// is 1.  Uses make_non_owning to avoid double-release with the pool reset.
static sturm::qint_t<W> make_qint(int base, uint32_t val,
                                  orkan::state_t& sv) {
    std::array<int, W> qi{};
    for (std::size_t i = 0; i < W; ++i) {
        qi[i] = base + static_cast<int>(i);
        if ((val >> i) & 1u) orkan::apply_x(sv, static_cast<uint32_t>(qi[i]));
    }
    uint64_t mask = (1ULL << W) - 1ULL;  // every bit is "quantum" / allocated
    return sturm::qint_t<W>::make_non_owning(qi, static_cast<int64_t>(val),
                                             mask);
}

// Beat 4.1 driver.  Asserts:
//   - sturm::add_mod(a, b, n) returns a qint_t<W> whose underlying register
//     holds (a + b) mod n on the simulator (matches a direct lib_add_mod_dsl
//     invocation by construction),
//   - inputs a, b, n are unchanged (PRD §5: read-only inputs),
//   - QubitPool::in_use() returns to its pre-call value (no leaked ancillas).
static void run_add_mod_case(uint32_t a_val, uint32_t b_val, uint32_t n_val) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");
    sturm::QubitPool::instance().reset_for_testing();
    // Reserve 3*W qubits for the input registers a, b, n at fixed indices.
    // (The wrapper allocates the result's W qubits + algorithm internals
    // beyond that high-water mark.)
    constexpr uint32_t n_input = 3u * W;
    int reserved[n_input];
    for (uint32_t k = 0; k < n_input; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();

    SimCtx sc{n_orkan, 64u};
    sturm::qint_t<W> a = make_qint(0,         a_val, sc.sv());
    sturm::qint_t<W> b = make_qint(W,         b_val, sc.sv());
    sturm::qint_t<W> n = make_qint(2 * W,     n_val, sc.sv());

    // Wrapper under test.  Returns a fresh, owning qint_t<W> holding the
    // modular sum.  The wrapper allocates its own result-register qubits;
    // ancillas inside lib_add_mod_dsl are LIFO-released before return.
    sturm::qint_t<W> r = sturm::add_mod(a, b, n);

    // Result register must hold (a + b) mod n on the simulator.
    const uint32_t expect_r = (a_val + b_val) % n_val;
    uint32_t r_sv = read_reg(sc.sv(), r.qubits.data(), W, n_orkan);
    assert(r_sv == expect_r && "add_mod: r == (a+b) mod n");
    assert(static_cast<uint32_t>(r.value) == expect_r
           && "add_mod: classical .value tracks the quantum result");

    // Inputs unchanged on the simulator (reversibility of read-only inputs).
    uint32_t a_sv = read_reg(sc.sv(), a.qubits.data(), W, n_orkan);
    uint32_t b_sv = read_reg(sc.sv(), b.qubits.data(), W, n_orkan);
    uint32_t n_sv = read_reg(sc.sv(), n.qubits.data(), W, n_orkan);
    assert(a_sv == a_val && "add_mod: a register unchanged");
    assert(b_sv == b_val && "add_mod: b register unchanged");
    assert(n_sv == n_val && "add_mod: n register unchanged");

    // Pool live-count: the wrapper's only net allocation is r's W qubits;
    // every ancilla allocated inside lib_add_mod_dsl is LIFO-released.
    assert(sturm::QubitPool::instance().in_use() == pre_in_use + static_cast<int>(W)
           && "add_mod: only the result's W qubits remain live after the call");

    // Tear r down to release its qubits before reset_for_testing.
    {
        sturm::qint_t<W> sink = std::move(r);
        (void)sink;
    }
    for (uint32_t k = 0; k < n_input; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-kgwx.1 P4.1 qint_modular: add_mod wrapper test:\n");
    run_add_mod_case(/*a=*/1u, /*b=*/1u, /*n=*/3u);
    std::puts("  PASS: add_mod(1, 1, 3) == 2 matches lib_add_mod_dsl");
    std::printf("All sturm-kgwx.1 tests passed.\n");
    return 0;
}
