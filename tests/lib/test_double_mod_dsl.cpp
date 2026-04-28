// test_double_mod_dsl.cpp -- Beat B (sturm-wdas) lib_double_mod_dsl forward
//                              tests: n==0 no-op, single classical case,
//                              W=2 exhaustive (odd-n) sweep, W=3 random
//                              (odd-n) sweep, plus the (n+1)/2 LSB-edge
//                              case from sturm-wdas's design note #3.
//
// Mirrors `tests/lib/test_add_mod_dsl.cpp` (sturm-yh3d.{1,2,3,4}) adapted
// for in-place modular doubling on a (W+1)-qubit register.  The forward
// primitive's precondition restricts `n_value` to be ODD (see the
// double_mod_dsl.hpp doxygen) — the lt_flag uncomputation step uses the
// result LSB, which only carries the lt_flag bit when n is odd.  The W=2
// exhaustive sweep therefore visits n in {1, 3} only; the W=3 random
// sweep restricts to odd n in {1, 3, 5, 7}.  The (n+1)/2 case (n=3,
// x=2) exercises the LSB=1 result (2·2 mod 3 = 1) flagged by sturm-wdas's
// design note #3 — this is the input that breaks any naive "halve via
// shift-right" forward; the conditional-add-back/lt_flag-uncompute path
// in lib_double_mod_dsl handles it correctly.
//
// Each call asserts:
//   - x_bits[0..W-1] == (2 · x_orig) mod n_value,
//   - x_bits[W] == 0 (overflow slot stays |0>),
//   - n_bits unchanged (reversibility of the modulus register),
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

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <random>

static constexpr double kTol = 1e-9;
static constexpr std::size_t W = 2;
// W=2: x_bits has W+1 = 3 slots, n_bits has W = 2 slots → 5 input qubits.
// Plus n_pad + lt_flag + carry_anc + inner adder transients ≈ 5 ancillas.
// Sizing the simulator at the kMaxQubits=17 cap leaves headroom and
// matches the add_mod_dsl test family.
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

// ── W=2 register helpers (W+1 slots for x_bits, W slots for n_bits) ─────
// x_bits has W+1 slots: bits 0..W-1 hold the input value x in [0, n_value);
// bit W is the overflow slot (must enter |0>).
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

// Beat sturm-wdas.1: n==0 short-circuits, leaves x_bits and n_bits unchanged.
static void run_n_zero_case(uint32_t x_val, uint32_t n_val) {
    sturm::QubitPool::instance().reset_for_testing();
    // x_bits has W+1 slots, n_bits has W slots → 2W+1 inputs.
    const uint32_t n_reg = (W + 1u) + W;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    SimCtx sc{n_orkan, 64u};
    RegX x = make_reg_x(0, x_val, sc.sv());
    RegN n = make_reg_n(static_cast<int>(W + 1u), n_val, sc.sv());

    // Call with width n == 0; stub must short-circuit silently.
    sturm::lib_double_mod_dsl<sturm::BitProxy>(x.bits.data(), n.bits.data(),
                                                /*n=*/0u);

    uint32_t x_low_sv = read_reg(sc.sv(), x.qi.data(), W, n_orkan);
    uint32_t x_top_sv = (read_reg(sc.sv(), x.qi.data() + W, 1u, n_orkan)) & 1u;
    uint32_t n_sv     = read_reg(sc.sv(), n.qi.data(), W, n_orkan);
    assert(x_low_sv == x_val && "n==0: x_bits[0..W-1] unchanged");
    assert(x_top_sv == 0u    && "n==0: x_bits[W] still |0>");
    assert(n_sv     == n_val && "n==0: n_bits unchanged");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// Beat sturm-wdas.2: full algorithm, single classical case.  Asserts:
//   - x_bits[0..W-1] holds (2·x) mod n,
//   - x_bits[W] == 0 (overflow slot returned to |0>),
//   - n_bits unchanged,
//   - QubitPool::in_use() returns to its pre-call value.
// Pre: x_val < n_val and n_val is odd.
static void run_classical_case(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && "test precondition: x < n");
    assert((n_val & 1u) == 1u && "test precondition: n is odd");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = (W + 1u) + W;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan, 64u};
    RegX x = make_reg_x(0, x_val, sc.sv());
    RegN n = make_reg_n(static_cast<int>(W + 1u), n_val, sc.sv());

    sturm::lib_double_mod_dsl<sturm::BitProxy>(x.bits.data(), n.bits.data(), W);

    const uint32_t expect_x = (2u * x_val) % n_val;
    uint32_t x_low_sv = read_reg(sc.sv(), x.qi.data(), W, n_orkan);
    uint32_t x_top_sv = read_reg(sc.sv(), x.qi.data() + W, 1u, n_orkan);
    uint32_t n_sv     = read_reg(sc.sv(), n.qi.data(), W, n_orkan);
    assert(x_low_sv == expect_x && "forward: x_bits[0..W-1] == (2x) mod n");
    assert(x_top_sv == 0u       && "forward: x_bits[W] returned to |0>");
    assert(n_sv     == n_val    && "forward: n_bits unchanged");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "forward: pool live-count returns to pre-call value");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── Beat sturm-wdas.4 — W=3 random sweep harness ─────────────────────────
// W=3 peaks at ~16-20 live qubits (4 input + ~5 ancillas + transients);
// stays inside orkan's 30-qubit ceiling.  We bypass kMaxQubits=17 cap by
// calling `orkan::allocate(bridge.state(), n_orkan_w3)` directly (matches
// the W=3 sweep workaround in test_add_mod_dsl.cpp).
static constexpr std::size_t W3        = 3u;
static constexpr uint32_t    n_orkan_w3 = 21u;

struct RegX3 {
    std::array<int, W3 + 1u>             qi;
    std::array<sturm::qbool, W3 + 1u>    owners;
    std::array<sturm::BitProxy, W3 + 1u> bits;
};

struct RegN3 {
    std::array<int, W3>              qi;
    std::array<sturm::qbool, W3>     owners;
    std::array<sturm::BitProxy, W3>  bits;
};

static RegX3 make_reg_x3(int base, uint32_t val, orkan::state_t& sv) {
    RegX3 r;
    for (std::size_t i = 0; i < W3 + 1u; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if (i < W3 && ((val >> i) & 1u))
            orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
    }
    for (std::size_t i = 0; i < W3 + 1u; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(r.qi[i]);
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

static RegN3 make_reg_n3(int base, uint32_t val, orkan::state_t& sv) {
    RegN3 r;
    for (std::size_t i = 0; i < W3; ++i) {
        r.qi[i] = base + static_cast<int>(i);
        if ((val >> i) & 1u) orkan::apply_x(sv, static_cast<uint32_t>(r.qi[i]));
    }
    for (std::size_t i = 0; i < W3; ++i) {
        r.owners[i] = sturm::qbool::make_non_owning(r.qi[i]);
        r.bits[i]   = sturm::BitProxy(r.owners[i]);
    }
    return r;
}

// W=3 driver — mirrors run_classical_case but sized for W=3 and uses the
// bypass-cap simulator.  Asserts x_bits == (2·x) mod n, n unchanged, pool
// live-count clean.  Pre: x_val < n_val, n_val odd.
static void run_classical_case_w3(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && n_val < (1u << W3));
    assert((n_val & 1u) == 1u && "test precondition: n is odd");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = (W3 + 1u) + W3;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    sturm::OrkanBridge bridge;
    orkan::allocate(bridge.state(), n_orkan_w3);
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_SIMULATE, 64u);
    assert(ctx);
    ctx->orkan_state_ptr = &bridge;
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);
    orkan::state_t& sv = bridge.state();
    RegX3 x = make_reg_x3(0, x_val, sv);
    RegN3 n = make_reg_n3(static_cast<int>(W3 + 1u), n_val, sv);
    sturm::lib_double_mod_dsl<sturm::BitProxy>(x.bits.data(), n.bits.data(),
                                                W3);
    const uint32_t expect_x = (2u * x_val) % n_val;
    assert(read_reg(sv, x.qi.data(), W3, n_orkan_w3) == expect_x
           && "W=3: x_bits[0..W-1] == (2x) mod n");
    assert(read_reg(sv, x.qi.data() + W3, 1u, n_orkan_w3) == 0u
           && "W=3: x_bits[W] returned to |0>");
    assert(read_reg(sv, n.qi.data(), W3, n_orkan_w3) == n_val
           && "W=3: n_bits unchanged");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "W=3: pool live-count returns to pre-call value");
    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-wdas.1 double-mod-dsl: n==0 no-op tests:\n");
    run_n_zero_case(/*x=*/1u, /*n=*/3u);
    std::puts("  PASS: n==0 with x=1, n=3 leaves x and n unchanged");
    run_n_zero_case(/*x=*/0u, /*n=*/0u);
    std::puts("  PASS: n==0 with x=0, n=0 (all |0>) leaves x and n unchanged");

    std::printf("sturm-wdas.2 double-mod-dsl: single classical case:\n");
    run_classical_case(/*x=*/1u, /*n=*/3u);
    std::puts("  PASS: (2 · 1) mod 3 == 2");

    // sturm-wdas design note #3 — the (n+1)/2 case.
    // For n=3 (odd), (n+1)/2 = 2.  2 · 2 mod 3 = 1, with LSB = 1.  This
    // breaks any naive "halve via shift-right" forward; the conditional-
    // add-back / lt_flag-uncompute path handles it correctly.
    std::printf("sturm-wdas double-mod-dsl: (n+1)/2 LSB-edge case "
                "(design note #3):\n");
    run_classical_case(/*x=*/2u, /*n=*/3u);
    std::puts("  PASS: (2 · 2) mod 3 == 1 (LSB=1; design-note-3 edge case)");

    std::printf("sturm-wdas.3 double-mod-dsl: W=2 exhaustive sweep "
                "(odd n in [1, 4): n in {1, 3}, all x in [0, n)):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); n_val += 2u) {  // odd-only
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_classical_case(x_val, n_val);
            ++cases_run;
        }
    }
    // odd n in [1, 4): n=1 → 1 case (x=0); n=3 → 3 cases.  Total = 4.
    assert(cases_run == 4u && "W=2 odd-n sweep covered every (x, n) "
                              "with x < n and n odd");
    std::printf("  PASS: %zu W=2 cases covering every (x, n) with x < n, "
                "n in {1, 3}\n", cases_run);

    // sturm-wdas.4 — W=3 random sweep (50 cases, fixed seed=42).  n is
    // restricted to odd values in {1, 3, 5, 7} per the forward primitive's
    // precondition (see double_mod_dsl.hpp).
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
        run_classical_case_w3(x_val, n_val);
    }
    std::printf("  PASS: %zu W=3 random cases (2 · x) mod n matches "
                "classical reference\n", kW3Cases);

    std::printf("All sturm-wdas double-mod-dsl forward tests passed.\n");
    return 0;
}
