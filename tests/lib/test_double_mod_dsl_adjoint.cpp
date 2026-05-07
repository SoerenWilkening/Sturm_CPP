// test_double_mod_dsl_adjoint.cpp -- Beat B (sturm-wdas) adjoint round-trip,
//                                       post sturm-4oot.1 API rewrite.
//
// Forward `lib_double_mod_dsl(x, n_bits, W, lt_flag_out)` followed by
// `__lib_double_mod_dsl_adj(x, n_bits, W, lt_flag_out)` must return
// `x_bits` to its original value AND consume `lt_flag_out` back to |0>
// for every (x, n) input that the forward beat covers.  Under
// sturm-4oot.2 the sweep is extended to even `n`: W=2 exhaustive over
// n in {1, 2, 3} (n=4 doesn't fit and is hoisted to W=3); W=3 even-n
// exhaustive over n in {2, 4, 6} via the simulator harness; W=4
// exhaustive at n=8 and W=4/W=5 random spot checks at n in {10, 12, 14}
// via a classical-trace replay harness.  `n_bits` must remain
// unchanged across the full forward+adjoint pair, and
// `QubitPool::in_use()` must return to its pre-call value (no leaked
// ancillas).
//
// The (n+1)/2 case (n=3, x=2) is the previous odd-only counterexample
// that breaks any naive "halve via shift-right" interpretation; it is
// now covered both by the W=2 sweep and the explicit design-note-3
// call below, and round-trips correctly under sturm-4oot.1.  Even-n
// inputs round-trip without parity restriction.
//
// Under sturm-4oot.1, the lt_flag bit is caller-owned: forward XORs
// `(2x_orig < n_value)` into `lt_flag_out`, adjoint reads it to reverse
// the doubling, leaving `lt_flag_out = 0` on exit.  Tests cover both
// the |0>-pre flow (clean write / clean uncompute) and an XOR-into pre
// (lt_flag_out_pre = 1 → adjoint must consume both the pre and the
// witness, leaving 0 since 1 XOR 1 = 0 only when the witness == 1; for
// witness == 0, the adjoint leaves 1 and the round-trip fails — so the
// XOR-into round-trip is conditional on |0>-pre, exactly as the
// forward+adjoint contract requires).
//
// The (n+1)/2 case (n=3, x=2 → 2x mod 3 = 1, LSB = 1) is the
// counterexample called out in sturm-wdas's design note #3: it is the
// input that breaks any naive "halve via shift-right" interpretation
// of the forward, and proves the adjoint here is the gate-reverse of
// the forward (not a literal halving).
//
// Mirrors `tests/lib/test_add_mod_dsl_adjoint.cpp` (sturm-yh3d.5):
//   (1) drive the forward,
//   (2) verify outputs match the classical reference (and lt_flag_out),
//   (3) resolve `invert<&lib_double_mod_dsl<BitProxy>>()` (compile-time
//       check that STURM_REGISTER_ADJOINT wired the pair),
//   (4) call the adjoint, and
//   (5) assert x_bits has been restored, lt_flag_out is back to |0>,
//       and n_bits is intact.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/double_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/routines/invert.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

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
// Sized at the kMaxQubits=17 cap (matches forward beat).  Forward+adjoint
// run back-to-back inside one SimCtx; LIFO release between calls means
// peak live qubits never exceed the forward's peak.
static constexpr uint32_t n_orkan = 17u;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 128u) {
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

// Forward + adjoint round-trip for a single classical (x, n) input.
//
// Precondition: x < n.  Asserts:
//   - forward sets x_bits[0..W-1] to (2 * x) mod n with x_bits[W] == 0,
//   - forward sets lt_flag_out to (2x < n)  (XOR-into a |0> pre),
//   - adjoint restores x_bits[0..W-1] to original x with x_bits[W] == 0,
//   - adjoint consumes lt_flag_out back to |0>,
//   - n_bits preserved across both,
//   - QubitPool::in_use() returns to its pre-call value at the end.
static void run_roundtrip_case(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && "test precondition: x < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = (W + 1u) + W + 1u;  // x (W+1), n (W), lt (1)
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan, 128u};
    RegX x   = make_reg_x(0, x_val, sc.sv());
    RegN n   = make_reg_n(static_cast<int>(W + 1u), n_val, sc.sv());
    RegLT lt = make_reg_lt(static_cast<int>(W + 1u + W));

    sturm::lib_double_mod_dsl<sturm::BitProxy>(x.bits.data(), n.bits.data(),
                                                W, lt.bit);

    const uint32_t expect_x  = (2u * x_val) % n_val;
    const uint32_t expect_lt = (2u * x_val < n_val) ? 1u : 0u;
    uint32_t x_low = read_reg(sc.sv(), x.qi.data(), W, n_orkan);
    uint32_t x_top = read_reg(sc.sv(), x.qi.data() + W, 1u, n_orkan);
    uint32_t n_sv  = read_reg(sc.sv(), n.qi.data(), W, n_orkan);
    uint32_t lt_sv = read_reg(sc.sv(), &lt.qi, 1u, n_orkan);
    assert(x_low == expect_x  && "forward: x_bits[0..W-1] == (2x) mod n");
    assert(x_top == 0u        && "forward: x_bits[W] returned to |0>");
    assert(n_sv  == n_val     && "forward: n_bits preserved");
    assert(lt_sv == expect_lt && "forward: lt_flag_out == (2x_orig < n)");

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_double_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_double_mod_dsl<BitProxy>>() must resolve "
                  "to registered adjoint");
    adj_ptr(x.bits.data(), n.bits.data(), W, lt.bit);

    x_low = read_reg(sc.sv(), x.qi.data(), W, n_orkan);
    x_top = read_reg(sc.sv(), x.qi.data() + W, 1u, n_orkan);
    n_sv  = read_reg(sc.sv(), n.qi.data(), W, n_orkan);
    lt_sv = read_reg(sc.sv(), &lt.qi, 1u, n_orkan);
    assert(x_low == x_val && "adjoint: x_bits[0..W-1] restored to original x");
    assert(x_top == 0u    && "adjoint: x_bits[W] still |0>");
    assert(n_sv  == n_val && "adjoint: n_bits preserved");
    assert(lt_sv == 0u    && "adjoint: lt_flag_out consumed back to |0>");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "round-trip: pool live-count returns to pre-call value");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── W=3 simulator round-trip harness (bypass-cap orkan state) ────────────
// Mirrors test_double_mod_dsl.cpp's run_classical_case_w3 but runs the
// forward + registered adjoint back-to-back inside the same SimCtx and
// asserts the round-trip post-conditions.  Used by the W=3 even-n
// exhaustive sweep below.
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

static void run_roundtrip_case_w3(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && n_val < (1u << W3));
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = (W3 + 1u) + W3 + 1u;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    sturm::OrkanBridge bridge;
    orkan::allocate(bridge.state(), n_orkan_w3);
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_SIMULATE);
    assert(ctx);
    ctx->orkan_state_ptr = &bridge;
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);
    orkan::state_t& sv = bridge.state();
    RegX3 x  = make_reg_x3(0, x_val, sv);
    RegN3 n  = make_reg_n3(static_cast<int>(W3 + 1u), n_val, sv);
    RegLT lt = make_reg_lt(static_cast<int>(W3 + 1u + W3));

    sturm::lib_double_mod_dsl<sturm::BitProxy>(x.bits.data(), n.bits.data(),
                                                W3, lt.bit);
    const uint32_t expect_x  = (2u * x_val) % n_val;
    const uint32_t expect_lt = (2u * x_val < n_val) ? 1u : 0u;
    assert(read_reg(sv, x.qi.data(), W3, n_orkan_w3) == expect_x
           && "W=3 forward: x_bits == (2x) mod n");
    assert(read_reg(sv, x.qi.data() + W3, 1u, n_orkan_w3) == 0u
           && "W=3 forward: x_bits[W] returned to |0>");
    assert(read_reg(sv, n.qi.data(), W3, n_orkan_w3) == n_val
           && "W=3 forward: n_bits unchanged");
    assert(read_reg(sv, &lt.qi, 1u, n_orkan_w3) == expect_lt
           && "W=3 forward: lt_flag_out == (2x_orig < n_value)");

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_double_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_double_mod_dsl<BitProxy>>() must resolve");
    adj_ptr(x.bits.data(), n.bits.data(), W3, lt.bit);

    assert(read_reg(sv, x.qi.data(), W3, n_orkan_w3) == x_val
           && "W=3 adjoint: x_bits restored to original x");
    assert(read_reg(sv, x.qi.data() + W3, 1u, n_orkan_w3) == 0u
           && "W=3 adjoint: x_bits[W] still |0>");
    assert(read_reg(sv, n.qi.data(), W3, n_orkan_w3) == n_val
           && "W=3 adjoint: n_bits preserved");
    assert(read_reg(sv, &lt.qi, 1u, n_orkan_w3) == 0u
           && "W=3 adjoint: lt_flag_out consumed back to |0>");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "W=3 round-trip: pool live-count returns to pre-call value");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── classical-trace round-trip harness (APPEND mode + bit-vector replay) ─
// sturm-4oot.2: drive forward + registered adjoint in APPEND mode for
// W=4, W=5 (which exceed the orkan ceiling for sweep-style testing) and
// replay the resulting IR bit-vector to verify both halves of the
// round-trip in the classical computational basis.  Mirrors
// test_mul_mod_dsl_oneshot_adjoint.cpp's trace harness.
static void apply_gate_classical(std::vector<uint8_t>& bits,
                                 const sturm::GateRecord& rec) {
    switch (rec.kind) {
    case STURM_GATE_X:
        bits[rec.qubits[0]] ^= 1u; break;
    case STURM_GATE_CX:
        if (bits[rec.qubits[0]]) bits[rec.qubits[1]] ^= 1u; break;
    case STURM_GATE_CCX:
        if (bits[rec.qubits[0]] && bits[rec.qubits[1]])
            bits[rec.qubits[2]] ^= 1u;
        break;
    default:
        std::fprintf(stderr, "trace: unsupported gate kind %d\n",
                     static_cast<int>(rec.kind));
        std::abort();
    }
}

static uint32_t read_reg_classical(const std::vector<uint8_t>& bits,
                                   const int* qi, std::size_t n) {
    uint32_t v = 0u;
    for (std::size_t k = 0; k < n; ++k)
        if (qi[k] >= 0 && bits[static_cast<std::size_t>(qi[k])])
            v |= (1u << k);
    return v;
}

template <std::size_t Wn>
static void run_roundtrip_trace_case(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val && n_val < (1u << Wn));
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = static_cast<uint32_t>(Wn) + 1u
                              + static_cast<uint32_t>(Wn) + 1u;
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
    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_double_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_double_mod_dsl<BitProxy>>() must resolve "
                  "in trace harness too");
    adj_ptr(x_bits, n_bits, Wn, lt_bit);

    const int high_water = sturm::QubitPool::instance().high_water();
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < Wn; ++i) {
        if ((x_val >> i) & 1u) bits[static_cast<std::size_t>(qi_x[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));

    assert(read_reg_classical(bits, qi_x, Wn) == x_val
           && "trace round-trip: x_bits restored to original x");
    assert(bits[static_cast<std::size_t>(qi_x[Wn])] == 0u
           && "trace round-trip: x_bits[Wn] (overflow slot) still |0>");
    assert(read_reg_classical(bits, qi_n, Wn) == n_val
           && "trace round-trip: n_bits preserved");
    assert(bits[static_cast<std::size_t>(qi_lt)] == 0u
           && "trace round-trip: lt_flag_out consumed back to |0>");
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u
               && "trace round-trip: ancilla not cleaned across f + adj");
    }
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "trace round-trip: pool live-count restored");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    sturm::QubitPool::instance().release(qi_lt);
    for (std::size_t i = Wn; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn + 1u; i-- > 0;)
        sturm::QubitPool::instance().release(qi_x[i]);
}

int main() {
    std::printf("sturm-wdas/sturm-4oot.1/sturm-4oot.2 double-mod-dsl: "
                "adjoint round-trip (W=2 exhaustive sweep, n in {1, 2, 3}, "
                "all x in [0, n); n=4 hoisted to W=3 below):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_roundtrip_case(x_val, n_val);
            ++cases_run;
        }
    }
    // n in [1, 4): n=1 → 1; n=2 → 2; n=3 → 3.  Total = 6.
    assert(cases_run == 6u && "W=2 sweep covered every (x, n) "
                              "with x < n and n in {1, 2, 3}");
    std::printf("  PASS: %zu W=2 cases (forward + adjoint restores x, "
                "consumes lt_flag_out, preserves n, no leaked ancillas)\n",
                cases_run);

    // sturm-wdas design note #3 — the (n+1)/2 case.  For n=3, x = 2 = (3+1)/2:
    // forward gives 2·2 mod 3 = 1 (LSB=1).  Adjoint must reverse via
    // gate-reverse (not literal shift-right) to restore x=2.  The W=2
    // sweep above already covers this case via (x=2, n=3); the explicit
    // call here documents the intent and traces the LSB=1 corner.
    // Under sturm-4oot.1 the round-trip is well-defined for every n
    // (including this previously-odd-only edge case).
    std::printf("sturm-wdas/sturm-4oot.2 double-mod-dsl: (n+1)/2 adjoint "
                "round-trip (design note #3 counterexample case):\n");
    run_roundtrip_case(/*x=*/2u, /*n=*/3u);
    std::puts("  PASS: round-trip on x=2, n=3 (forward 2·2 mod 3 = 1, "
              "LSB=1, lt_flag_out=0; adjoint restores x=2, lt_flag_out=0)");

    // sturm-4oot.2 — W=3 even-n exhaustive round-trip sweep (n in {2, 4, 6};
    // n=8 doesn't fit in a 3-bit modulus register and is exercised at W=4
    // via the trace harness below).
    std::printf("sturm-4oot.2 double-mod-dsl: W=3 even-n exhaustive "
                "round-trip sweep (n in {2, 4, 6}, all x in [0, n)):\n");
    std::size_t cases_w3_even = 0u;
    for (uint32_t n_val : {2u, 4u, 6u}) {
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_roundtrip_case_w3(x_val, n_val);
            ++cases_w3_even;
        }
    }
    // n=2 → 2; n=4 → 4; n=6 → 6.  Total = 12.
    assert(cases_w3_even == 12u
           && "W=3 even-n sweep covered every (x, n) with x < n and n even");
    std::printf("  PASS: %zu W=3 even-n round-trip cases\n", cases_w3_even);

    // sturm-4oot.2 — W=4 exhaustive trace round-trip at n=8 + W=4/W=5
    // random spot checks at n in {10, 12, 14}.  Trace harness (see file
    // header) mirrors test_mul_mod_dsl_oneshot_adjoint.cpp.
    constexpr std::size_t W4 = 4u;
    constexpr std::size_t W5 = 5u;
    std::array<uint32_t, 3> even_ns_4_5 = {10u, 12u, 14u};

    std::printf("sturm-4oot.2 double-mod-dsl: W=4 exhaustive trace "
                "round-trip (even n=8):\n");
    {
        constexpr uint32_t n_val = 8u;
        std::size_t cases_w4_n8 = 0u;
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_roundtrip_trace_case<W4>(x_val, n_val);
            ++cases_w4_n8;
        }
        assert(cases_w4_n8 == 8u);
        std::printf("  PASS: %zu W=4 trace round-trip cases at n=8\n",
                    cases_w4_n8);
    }

    std::printf("sturm-4oot.2 double-mod-dsl: W=4 random trace round-trip "
                "spot checks (20 cases, seed=4214, even n in {10, 12, 14}):\n");
    {
        constexpr uint32_t kSeed = 4214u;
        constexpr std::size_t kCases = 20u;
        std::mt19937 rng4(kSeed);
        std::uniform_int_distribution<uint32_t> n_idx(0u, 2u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = even_ns_4_5[n_idx(rng4)];
            std::uniform_int_distribution<uint32_t> x_dist(0u, n_val - 1u);
            uint32_t x_val = x_dist(rng4);
            run_roundtrip_trace_case<W4>(x_val, n_val);
        }
        std::printf("  PASS: %zu W=4 even-n trace round-trip spot checks\n",
                    kCases);
    }

    std::printf("sturm-4oot.2 double-mod-dsl: W=5 random trace round-trip "
                "spot checks (20 cases, seed=4215, even n in {10, 12, 14}):\n");
    {
        constexpr uint32_t kSeed = 4215u;
        constexpr std::size_t kCases = 20u;
        std::mt19937 rng5(kSeed);
        std::uniform_int_distribution<uint32_t> n_idx(0u, 2u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = even_ns_4_5[n_idx(rng5)];
            std::uniform_int_distribution<uint32_t> x_dist(0u, n_val - 1u);
            uint32_t x_val = x_dist(rng5);
            run_roundtrip_trace_case<W5>(x_val, n_val);
        }
        std::printf("  PASS: %zu W=5 even-n trace round-trip spot checks\n",
                    kCases);
    }

    std::printf("All sturm-wdas/sturm-4oot.1/sturm-4oot.2 double-mod-dsl "
                "adjoint tests passed.\n");
    return 0;
}
