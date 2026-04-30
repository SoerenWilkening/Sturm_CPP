// test_square_mod_dsl.cpp -- Beat D-B (sturm-3sfl.2) lib_square_mod_dsl
//                              tests: forward, adjoint round-trip, ancilla
//                              budget, and pool live-count.
//
// The forward primitive `lib_square_mod_dsl(x_bits, n_bits, n,
// x_copy_out_bits)` does in-place modular squaring `x := (x * x) mod n`,
// with `x_orig` XOR-loaded into the caller-owned `x_copy_out_bits`
// register as the non-injectivity witness (the squaring map x → x^2 mod n
// is 2-to-1 over [0, n) for n > 1, so reversibility requires exporting
// a witness — see square_mod_dsl.hpp's preamble for the full design
// rationale).  The adjoint `__lib_square_mod_dsl_adj` is the gate-reverse
// paired strictly with a forward call: it consumes `x_copy_out_bits`
// back to its pre-forward state and restores `x_bits` to `x_orig`.
//
// Coverage (mirrors the sturm-wdas Beat B / sturm-7cix Beat C test plans):
//   * n==0 short-circuit (no-op, no allocation).
//   * W=2 single classical case driven through orkan (witness against
//     the real backend, not just a bit-flip program).
//   * W=2 exhaustive sweep over (x, n) with n in [1, 4), including the
//     (n+1)/2 type case.
//   * W=3 random sweep including even n.
//   * x==0 and x==n-1 edge cases (both square to specific known values
//     for n > 1: 0 and 1 respectively).
//   * adjoint round-trip (forward + adjoint = identity on x).
//   * ancilla counter (peak ~4W + O(1) — see comment near
//     run_ancilla_probe; this is one slot more than the issue's "~3W"
//     hint because strategy (a) needs an explicit r_reg scratch on top
//     of the W-bit caller-owned x_copy_out_bits register, in exchange
//     for keeping the layering rule "no new arithmetic kernel").
//   * pool live-count round-trip (every allocate has a matching release).
//   * x_copy_out_bits XOR-into / threading round-trip — the witness
//     accumulates rather than overwrites, mirroring sturm-4oot.1's
//     lt_flag_out XOR-into semantic for double_mod.
//
// Most legs use the APPEND-mode classical-trace harness (no orkan
// statevector simulation): the squaring algorithm is built entirely from
// classical-reversible gates (X / CX / CCX), so a bit-flip replay over
// the captured IR is a faithful reference and avoids the O(2^N) per-gate
// cost the simulator pays.  One W=2 simulator-driven case is kept as an
// end-to-end witness for the unitary-semantics path (matches the strategy
// in test_mul_mod_dsl_oneshot.cpp).

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/square_mod_dsl.hpp"
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

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 128u, bool bypass = false) {
        if (bypass) orkan::allocate(bridge.state(), n_q);
        else        bridge.allocate(n_q);
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

template <std::size_t W>
struct Reg {
    std::array<int, W>              qi;
    std::array<sturm::qbool, W>     owners;
    std::array<sturm::BitProxy, W>  bits;
};

template <std::size_t W>
static Reg<W> make_reg(int base, uint32_t val, orkan::state_t& sv) {
    Reg<W> r;
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

// ── n==0 no-op test ─────────────────────────────────────────────────────
static void run_n_zero_case() {
    constexpr std::size_t W = 2u;
    sturm::QubitPool::instance().reset_for_testing();
    // x (W) + n (W) + x_copy (W) = 3W inputs.
    const uint32_t n_reg = 3u * static_cast<uint32_t>(W);
    int reserved[16];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{17u, 128u};
    Reg<W> x  = make_reg<W>(0,         /*val=*/1u, sc.sv());
    Reg<W> n  = make_reg<W>(W,         /*val=*/3u, sc.sv());
    Reg<W> xc = make_reg<W>(2 * W,     /*val=*/0u, sc.sv());

    sturm::lib_square_mod_dsl<sturm::BitProxy>(x.bits.data(), n.bits.data(),
                                               /*n=*/0u, xc.bits.data());

    assert(read_reg(sc.sv(), x.qi.data(),  W, 17u) == 1u
           && "n==0: x_bits unchanged");
    assert(read_reg(sc.sv(), n.qi.data(),  W, 17u) == 3u
           && "n==0: n_bits unchanged");
    assert(read_reg(sc.sv(), xc.qi.data(), W, 17u) == 0u
           && "n==0: x_copy_out_bits unchanged");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "n==0: pool live-count unchanged");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── single classical W=2 simulator witness (forward + adjoint round-trip) ──
template <std::size_t W>
static void run_simulator_roundtrip(uint32_t x_val, uint32_t n_val,
                                     uint32_t n_orkan, bool bypass_cap) {
    assert(x_val < n_val);
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 3u * static_cast<uint32_t>(W);
    int reserved[64];
    assert(n_reg <= 64u);
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan, 128u, bypass_cap};
    Reg<W> x  = make_reg<W>(0,         x_val, sc.sv());
    Reg<W> n  = make_reg<W>(W,         n_val, sc.sv());
    Reg<W> xc = make_reg<W>(2 * W,     0u,    sc.sv());

    sturm::lib_square_mod_dsl<sturm::BitProxy>(x.bits.data(), n.bits.data(),
                                               W, xc.bits.data());

    const uint32_t expect_x  = (x_val * x_val) % n_val;
    assert(read_reg(sc.sv(), x.qi.data(),  W, n_orkan) == expect_x
           && "forward: x == (x_orig^2) mod n");
    assert(read_reg(sc.sv(), n.qi.data(),  W, n_orkan) == n_val
           && "forward: n preserved");
    assert(read_reg(sc.sv(), xc.qi.data(), W, n_orkan) == x_val
           && "forward: x_copy_out_bits == x_orig (clean write)");

    // Resolve the registered adjoint and run the gate-reverse pass.
    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_square_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_square_mod_dsl<BitProxy>>() must resolve "
                  "to the registered adjoint");
    adj_ptr(x.bits.data(), n.bits.data(), W, xc.bits.data());

    assert(read_reg(sc.sv(), x.qi.data(),  W, n_orkan) == x_val
           && "adjoint: x restored to x_orig");
    assert(read_reg(sc.sv(), n.qi.data(),  W, n_orkan) == n_val
           && "adjoint: n preserved");
    assert(read_reg(sc.sv(), xc.qi.data(), W, n_orkan) == 0u
           && "adjoint: x_copy_out_bits consumed back to pre-state (|0>)");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "round-trip: pool live-count returns to pre-call value");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── classical-trace harness (APPEND mode + bit-vector replay) ────────────
// Mirrors test_mul_mod_dsl_oneshot.cpp / test_double_mod_dsl.cpp's trace
// harness: drive the primitive in APPEND mode, replay the IR over a
// bit-vector to verify all classical post-conditions, including ancilla
// cleanup and pool live-count.
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

// Run lib_square_mod_dsl<Wn> for one (x, n) input via the trace harness,
// optionally also running the registered adjoint and asserting the
// round-trip post-conditions.
template <std::size_t Wn>
static void run_trace_case(uint32_t x_val, uint32_t n_val,
                           bool also_roundtrip) {
    assert(x_val < n_val && n_val < (1u << Wn));
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 3u * static_cast<uint32_t>(Wn);
    int qi_x[Wn], qi_n[Wn], qi_xc[Wn];
    for (std::size_t i = 0; i < Wn; ++i)
        qi_x[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_xc[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    sturm::qbool x_own[Wn], n_own[Wn], xc_own[Wn];
    sturm::BitProxy x_bits[Wn], n_bits[Wn], xc_bits[Wn];
    for (std::size_t i = 0; i < Wn; ++i) {
        x_own[i]  = sturm::qbool::make_non_owning(qi_x[i]);
        // Seed n.value classically (matches the convention in
        // test_mul_mod_dsl_oneshot.cpp; harmless when no dispatcher
        // reads it).
        n_own[i]  = sturm::qbool::make_non_owning(
            qi_n[i],
            static_cast<int64_t>((n_val >> i) & 1u),
            /*mask=*/0ULL);
        xc_own[i] = sturm::qbool::make_non_owning(qi_xc[i]);
        x_bits[i]  = sturm::BitProxy(x_own[i]);
        n_bits[i]  = sturm::BitProxy(n_own[i]);
        xc_bits[i] = sturm::BitProxy(xc_own[i]);
    }

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 128u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_square_mod_dsl<sturm::BitProxy>(x_bits, n_bits, Wn, xc_bits);

    if (also_roundtrip) {
        constexpr auto adj_ptr =
            sturm::invert<&sturm::lib_square_mod_dsl<sturm::BitProxy>>();
        static_assert(adj_ptr != nullptr,
                      "invert<&lib_square_mod_dsl<BitProxy>>() must resolve");
        adj_ptr(x_bits, n_bits, Wn, xc_bits);
    }

    const int high_water = sturm::QubitPool::instance().high_water();

    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < Wn; ++i) {
        if ((x_val >> i) & 1u) bits[static_cast<std::size_t>(qi_x[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
        // x_copy_out_bits and all ancillas start |0>.
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));

    if (also_roundtrip) {
        // Round-trip: x should be back to x_orig, x_copy back to |0>,
        // n unchanged, every ancilla |0>.
        assert(read_reg_classical(bits, qi_x, Wn) == x_val
               && "trace round-trip: x restored to x_orig");
        assert(read_reg_classical(bits, qi_n, Wn) == n_val
               && "trace round-trip: n preserved");
        assert(read_reg_classical(bits, qi_xc, Wn) == 0u
               && "trace round-trip: x_copy_out_bits back to |0>");
    } else {
        const uint32_t expect_x = (x_val * x_val) % n_val;
        assert(read_reg_classical(bits, qi_x, Wn) == expect_x
               && "trace forward: x == (x_orig^2) mod n");
        assert(read_reg_classical(bits, qi_n, Wn) == n_val
               && "trace forward: n preserved");
        assert(read_reg_classical(bits, qi_xc, Wn) == x_val
               && "trace forward: x_copy_out_bits == x_orig");
    }
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u && "trace: ancilla not cleaned");
    }
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "trace: pool live-count restored");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_xc[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_x[i]);
}

// ── XOR-into accumulation case for x_copy_out_bits ──────────────────────
// Mirrors sturm-4oot.1's lt_flag_out XOR-into test for double_mod: pre-
// flip x_copy_out_bits to a known nonzero pattern, run the forward, and
// verify x_copy_out_bits accumulates (XORs in) x_orig rather than
// overwriting.  This pins the documented "XOR-into" contract; without
// this, a |0>-pre adjoint round-trip would silently still work, but
// XOR-into is the explicit semantic for cleaner caller-side composition
// (e.g. lib_pow_mod_dsl batching multiple square calls into a shared
// witness register).
template <std::size_t Wn>
static void run_xor_into_case(uint32_t x_val, uint32_t n_val,
                              uint32_t xc_pre) {
    assert(x_val < n_val && n_val < (1u << Wn) && xc_pre < (1u << Wn));
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 3u * static_cast<uint32_t>(Wn);
    int qi_x[Wn], qi_n[Wn], qi_xc[Wn];
    for (std::size_t i = 0; i < Wn; ++i)
        qi_x[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_xc[i] = sturm::QubitPool::instance().allocate();

    sturm::qbool x_own[Wn], n_own[Wn], xc_own[Wn];
    sturm::BitProxy x_bits[Wn], n_bits[Wn], xc_bits[Wn];
    for (std::size_t i = 0; i < Wn; ++i) {
        x_own[i]  = sturm::qbool::make_non_owning(qi_x[i]);
        n_own[i]  = sturm::qbool::make_non_owning(
            qi_n[i],
            static_cast<int64_t>((n_val >> i) & 1u), 0ULL);
        xc_own[i] = sturm::qbool::make_non_owning(qi_xc[i]);
        x_bits[i]  = sturm::BitProxy(x_own[i]);
        n_bits[i]  = sturm::BitProxy(n_own[i]);
        xc_bits[i] = sturm::BitProxy(xc_own[i]);
    }

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 128u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_square_mod_dsl<sturm::BitProxy>(x_bits, n_bits, Wn, xc_bits);

    const int high_water = sturm::QubitPool::instance().high_water();
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < Wn; ++i) {
        if ((x_val >> i)  & 1u) bits[static_cast<std::size_t>(qi_x[i])]  = 1u;
        if ((n_val >> i)  & 1u) bits[static_cast<std::size_t>(qi_n[i])]  = 1u;
        // Pre-flip x_copy_out_bits to xc_pre.
        if ((xc_pre >> i) & 1u) bits[static_cast<std::size_t>(qi_xc[i])] = 1u;
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));

    const uint32_t expect_xc = xc_pre ^ x_val;
    assert(read_reg_classical(bits, qi_xc, Wn) == expect_xc
           && "XOR-into: x_copy_out_bits_post == x_copy_out_bits_pre XOR "
              "x_orig");
    // Note: with non-|0> xc_pre, the inner mul_mod_dsl_oneshot(x, x_copy,
    //       n, r_reg) is invoked with x_copy = (xc_pre XOR x_orig) instead
    //       of x_orig, so r_reg = x_orig * (xc_pre XOR x_orig) mod n
    //       rather than x_orig^2 mod n.  We do not assert the post-
    //       forward value of x_bits here — the only contract this test
    //       pins is the XOR-into accumulation on x_copy_out_bits itself.

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_xc[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_x[i]);
}

// ── ancilla budget probe (peak high_water vs pre-call in_use) ───────────
// Strategy (a) allocates 1 W-bit scratch r_reg above the inner
// `lib_mul_mod_dsl_oneshot`, whose own peak interior is 3W + 7 (post
// sturm-4oot.3, see test_mul_mod_dsl_oneshot_ancilla.cpp).  Total peak
// above the 3W caller-allocated registers (x, n, x_copy_out):
//     W [r_reg] + (3W + 7) [oneshot interior] = 4W + 7.
//
// This is one slot more than the issue's "~3W + O(1)" hint because
// strategy (a) needs the explicit r_reg scratch as the home for the
// out-of-place product before the swap; the issue brief acknowledges
// the W-extra-qubit cost ("paying W extra qubits to break the alias")
// of the alternative copy_of_x route, and strategy (a) here uses the
// oneshot's a==b aliasing allowance to avoid a separate copy_of_x while
// still needing r_reg.  PRD §6.4 `W + O(1)` requirement is met.
static constexpr int kSquareModAncillaBudget(int W) { return 4 * W + 7; }

template <std::size_t W>
static int peak_ancilla_square_mod_for(uint32_t x_val, uint32_t n_val) {
    assert(x_val < n_val);
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 3u * static_cast<uint32_t>(W);
    int qi_x[W], qi_n[W], qi_xc[W];
    for (std::size_t i = 0; i < W; ++i)
        qi_x[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_xc[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    sturm::qbool x_own[W], n_own[W], xc_own[W];
    sturm::BitProxy x_bits[W], n_bits[W], xc_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        x_own[i]  = sturm::qbool::make_non_owning(qi_x[i]);
        n_own[i]  = sturm::qbool::make_non_owning(
            qi_n[i],
            static_cast<int64_t>((n_val >> i) & 1u), 0ULL);
        xc_own[i] = sturm::qbool::make_non_owning(qi_xc[i]);
        x_bits[i]  = sturm::BitProxy(x_own[i]);
        n_bits[i]  = sturm::BitProxy(n_own[i]);
        xc_bits[i] = sturm::BitProxy(xc_own[i]);
    }

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 128u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    (void)x_val;
    sturm::lib_square_mod_dsl<sturm::BitProxy>(x_bits, n_bits, W, xc_bits);

    const int peak = sturm::QubitPool::instance().high_water() - pre_in_use;
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "ancilla probe: pool live-count returns to pre-call value");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_xc[i]);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_x[i]);
    return peak;
}

int main() {
    // ── n==0 ───────────────────────────────────────────────────────────
    std::printf("sturm-3sfl.2 square-mod-dsl: n==0 short-circuit:\n");
    run_n_zero_case();
    std::puts("  PASS: n==0 leaves x/n/x_copy_out unchanged, no allocation");

    // ── W=2 single classical simulator witness (forward + adjoint) ────
    constexpr std::size_t W2 = 2u;
    // Peak above 3W=6 inputs is ~4W+7=15 → ~21 live qubits at W=2.
    // Bypass kMaxQubits=17 cap with a 25-qubit orkan state.
    constexpr uint32_t n_orkan_w2 = 25u;
    std::printf("sturm-3sfl.2 square-mod-dsl: single W=2 simulator witness "
                "(x=2, n=3) round-trip:\n");
    run_simulator_roundtrip<W2>(/*x=*/2u, /*n=*/3u, n_orkan_w2,
                                /*bypass=*/true);
    std::puts("  PASS: orkan-simulator forward+adjoint for (2^2) mod 3 == 1");

    // ── W=2 exhaustive sweep over (x, n) for n in [1, 4) ──────────────
    // Exercises the (n+1)/2 case (n=3, x=2: x^2 mod 3 = 1, x_copy = 2).
    std::printf("sturm-3sfl.2 square-mod-dsl: W=2 exhaustive trace sweep "
                "(n in {1, 2, 3}, all x in [0, n), forward+adjoint):\n");
    std::size_t cases_w2 = 0u;
    for (uint32_t n_val : {1u, 2u, 3u}) {
        for (uint32_t x_val = 0u; x_val < n_val; ++x_val) {
            run_trace_case<W2>(x_val, n_val, /*also_roundtrip=*/false);
            run_trace_case<W2>(x_val, n_val, /*also_roundtrip=*/true);
            ++cases_w2;
        }
    }
    // n=1 → 1 case; n=2 → 2; n=3 → 3.  Total = 6 (each run twice:
    // forward-only and round-trip).
    assert(cases_w2 == 6u);
    std::printf("  PASS: %zu W=2 cases (each forward and round-trip)\n",
                cases_w2);

    // ── x==0 and x==n-1 edge cases ─────────────────────────────────────
    // For n > 1: 0^2 mod n == 0 and (n-1)^2 mod n == 1 (since
    // (n-1)^2 = n^2 - 2n + 1 ≡ 1 mod n).  Pin both edges explicitly.
    std::printf("sturm-3sfl.2 square-mod-dsl: x=0 / x=n-1 edge cases:\n");
    {
        // x=0, n=3 → x^2 mod 3 = 0.
        run_trace_case<W2>(/*x=*/0u, /*n=*/3u, /*roundtrip=*/true);
        // x=n-1=2, n=3 → 2^2 mod 3 = 1.  Already in the W=2 sweep, but
        // re-running here makes the contract explicit.
        run_trace_case<W2>(/*x=*/2u, /*n=*/3u, /*roundtrip=*/true);
        // x=n-1=3, n=4 → 3^2 mod 4 = 9 mod 4 = 1.  Even-n edge case.
        constexpr std::size_t W3 = 3u;  // need W>=3 for n=4 in modulus reg.
        run_trace_case<W3>(/*x=*/3u, /*n=*/4u, /*roundtrip=*/true);
    }
    std::puts("  PASS: x=0 squares to 0; x=n-1 squares to 1 (n in {3, 4})");

    // ── W=3 random sweep including even n ─────────────────────────────
    constexpr std::size_t W3 = 3u;
    std::printf("sturm-3sfl.2 square-mod-dsl: W=3 random trace sweep "
                "(50 cases, seed=3582, n in {2..7}, includes even n):\n");
    {
        constexpr uint32_t kSeed = 3582u;  // sturm-3sfl.2-derived
        constexpr std::size_t kCases = 50u;
        std::mt19937 rng(kSeed);
        std::uniform_int_distribution<uint32_t> n_dist(2u, 7u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint32_t n_val = n_dist(rng);
            std::uniform_int_distribution<uint32_t> x_dist(0u, n_val - 1u);
            uint32_t x_val = x_dist(rng);
            // Run both forward-only and round-trip on each random case.
            run_trace_case<W3>(x_val, n_val, /*roundtrip=*/false);
            run_trace_case<W3>(x_val, n_val, /*roundtrip=*/true);
        }
        std::printf("  PASS: %zu W=3 random cases (forward + round-trip)\n",
                    kCases);
    }

    // ── XOR-into / threading round-trip for x_copy_out_bits ───────────
    // sturm-3sfl.2: x_copy_out_bits is XOR-into.  Pre-flip the witness
    // to a nonzero pattern, run forward, and verify it accumulates (XORs
    // in) x_orig rather than overwriting.  This pins the lt_flag_out
    // analogue's contract for the squaring witness.
    std::printf("sturm-3sfl.2 square-mod-dsl: x_copy_out_bits XOR-into "
                "threading round-trip:\n");
    run_xor_into_case<W2>(/*x=*/2u, /*n=*/3u, /*xc_pre=*/1u);
    run_xor_into_case<W3>(/*x=*/3u, /*n=*/5u, /*xc_pre=*/2u);
    run_xor_into_case<W3>(/*x=*/4u, /*n=*/6u, /*xc_pre=*/5u);  // even-n
    std::puts("  PASS: x_copy_out_bits_post == x_copy_out_bits_pre XOR x_orig "
              "across W=2 / W=3 cases (incl. even n)");

    // ── Ancilla budget pin (peak high_water) ──────────────────────────
    {
        constexpr std::size_t W       = 2u;
        constexpr int        kBudget  =
            kSquareModAncillaBudget(static_cast<int>(W));
        std::printf("sturm-3sfl.2 square-mod-dsl: peak ancilla bound "
                    "<= 4W + 7 (W=%zu, n=3, budget=%d):\n", W, kBudget);
        const int peak = peak_ancilla_square_mod_for<W>(/*x=*/2u, /*n=*/3u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak, 3u * static_cast<uint32_t>(W));
        std::fflush(stdout);
        assert(peak <= kBudget
               && "Beat D-B square_mod: peak ancilla exceeds budget at W=2");
        std::puts("  PASS: W=2 square_mod peak ancilla within budget");

        // Even n=4 doesn't fit at W=2, so jump to W=3.
        constexpr std::size_t W3a       = 3u;
        constexpr int        kBudget3  =
            kSquareModAncillaBudget(static_cast<int>(W3a));
        std::printf("sturm-3sfl.2 square-mod-dsl: peak ancilla bound "
                    "<= 4W + 7 (W=%zu, even n=4, budget=%d):\n", W3a, kBudget3);
        const int peak_w3 = peak_ancilla_square_mod_for<W3a>(/*x=*/3u,
                                                              /*n=*/4u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak_w3, 3u * static_cast<uint32_t>(W3a));
        std::fflush(stdout);
        assert(peak_w3 <= kBudget3
               && "Beat D-B square_mod: peak ancilla exceeds budget at W=3 "
                  "(even n)");
        std::puts("  PASS: W=3 square_mod peak ancilla within budget "
                  "(even n)");
    }

    std::printf("All sturm-3sfl.2 Beat D-B square-mod-dsl tests passed.\n");
    return 0;
}
