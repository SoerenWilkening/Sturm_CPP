// test_mul_mod_inplace_dsl.cpp -- Beat D-A (sturm-3sfl.1)
//                                    lib_mul_mod_inplace_dsl tests:
//                                    forward, adjoint round-trip, ancilla
//                                    budget, pool live-count, and witness
//                                    XOR-into round-trip.
//
// The forward primitive `lib_mul_mod_inplace_dsl(a_bits, dest_bits, n_bits,
// n, dest_copy_out_bits)` does in-place modular multiplication
// `dest := (a * dest) mod n`, with `dest_old` XOR-loaded into the
// caller-owned `dest_copy_out_bits` register as the non-injectivity
// witness.  When `gcd(a, n_value) > 1` the forward map is non-injective
// over [0, n_value), so reversibility requires exporting a witness — see
// mul_mod_inplace_dsl.hpp's preamble for the full design rationale (and
// the "issue brief deviation" note: the brief outlined a witness-less
// scheme that only works for `gcd(a, n_value) = 1`; this implementation
// follows the Beat D-B / sturm-3sfl.2 squaring-witness pattern instead,
// which works for general n).  The adjoint
// `__lib_mul_mod_inplace_dsl_adj` is the gate-reverse paired strictly
// with a forward call: it consumes `dest_copy_out_bits` back to its
// pre-forward state and restores `dest_bits` to `dest_old`.
//
// Coverage (mirrors the sturm-3sfl.2 Beat D-B / sturm-7cix Beat C test
// plans, with `(a, dest)` distinct factor/destination pairs in place of
// the squaring's single `x`):
//   * n==0 short-circuit (no-op, no allocation).
//   * W=2 single classical case driven through orkan (witness against
//     the real backend, not just a bit-flip program).
//   * W=2 exhaustive sweep over (a, dest, n) with a != dest, n in
//     [1, 4) — including non-coprime (a, n) cases (e.g. a=0, n=2, 3).
//   * W=3 random sweep including even n.
//   * squaring-aliasing case (a_bits == dest_bits) is **forbidden** by
//     the primitive's precondition (callers must use lib_square_mod_dsl
//     for the squaring case); we document this here without exercising
//     the aliased call (no runtime check is added; matches the trust
//     model of the Beat A / Beat B / Beat C / Beat D-B siblings).
//   * adjoint round-trip (forward + adjoint = identity on dest).
//   * ancilla counter (peak ~4W + O(1) — see comment near
//     run_ancilla_probe; this is one slot more than the issue's
//     "~3W + O(1)" hint because the witness-route impl needs an
//     explicit r_reg scratch on top of the W-bit caller-owned
//     dest_copy_out_bits register, mirroring Beat D-B's accounting).
//   * pool live-count round-trip (every allocate has a matching
//     release).
//   * dest_copy_out_bits XOR-into / threading round-trip — the
//     witness accumulates rather than overwrites, mirroring
//     sturm-4oot.1's lt_flag_out and sturm-3sfl.2's x_copy_out_bits
//     XOR-into semantics.
//
// Most legs use the APPEND-mode classical-trace harness (no orkan
// statevector simulation): the in-place mul algorithm is built entirely
// from classical-reversible gates (X / CX / CCX), so a bit-flip replay
// over the captured IR is a faithful reference and avoids the O(2^N)
// per-gate cost the simulator pays.  One W=2 simulator-driven case is
// kept as an end-to-end witness for the unitary-semantics path
// (matches the strategy in test_square_mod_dsl.cpp /
// test_mul_mod_dsl_oneshot.cpp).

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/mul_mod_inplace_dsl.hpp"
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
    // a (W) + dest (W) + n (W) + dest_copy (W) = 4W inputs.
    const uint32_t n_reg = 4u * static_cast<uint32_t>(W);
    int reserved[16];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{17u, 128u};
    Reg<W> a   = make_reg<W>(0,         /*val=*/2u, sc.sv());
    Reg<W> d   = make_reg<W>(W,         /*val=*/1u, sc.sv());
    Reg<W> n   = make_reg<W>(2 * W,     /*val=*/3u, sc.sv());
    Reg<W> dc  = make_reg<W>(3 * W,     /*val=*/0u, sc.sv());

    sturm::lib_mul_mod_inplace_dsl<sturm::BitProxy>(
        a.bits.data(), d.bits.data(), n.bits.data(),
        /*n=*/0u, dc.bits.data());

    assert(read_reg(sc.sv(), a.qi.data(),  W, 17u) == 2u
           && "n==0: a_bits unchanged");
    assert(read_reg(sc.sv(), d.qi.data(),  W, 17u) == 1u
           && "n==0: dest_bits unchanged");
    assert(read_reg(sc.sv(), n.qi.data(),  W, 17u) == 3u
           && "n==0: n_bits unchanged");
    assert(read_reg(sc.sv(), dc.qi.data(), W, 17u) == 0u
           && "n==0: dest_copy_out_bits unchanged");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "n==0: pool live-count unchanged");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── single classical W=2 simulator witness (forward + adjoint round-trip) ──
template <std::size_t W>
static void run_simulator_roundtrip(uint32_t a_val, uint32_t dest_val,
                                     uint32_t n_val,
                                     uint32_t n_orkan, bool bypass_cap) {
    assert(a_val < n_val);
    assert(dest_val < n_val);
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * static_cast<uint32_t>(W);
    int reserved[64];
    assert(n_reg <= 64u);
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    SimCtx sc{n_orkan, 128u, bypass_cap};
    Reg<W> a   = make_reg<W>(0,         a_val,    sc.sv());
    Reg<W> d   = make_reg<W>(W,         dest_val, sc.sv());
    Reg<W> n   = make_reg<W>(2 * W,     n_val,    sc.sv());
    Reg<W> dc  = make_reg<W>(3 * W,     0u,       sc.sv());

    sturm::lib_mul_mod_inplace_dsl<sturm::BitProxy>(
        a.bits.data(), d.bits.data(), n.bits.data(), W, dc.bits.data());

    const uint32_t expect_d = (a_val * dest_val) % n_val;
    assert(read_reg(sc.sv(), a.qi.data(),  W, n_orkan) == a_val
           && "forward: a preserved");
    assert(read_reg(sc.sv(), d.qi.data(),  W, n_orkan) == expect_d
           && "forward: dest == (a * dest_old) mod n");
    assert(read_reg(sc.sv(), n.qi.data(),  W, n_orkan) == n_val
           && "forward: n preserved");
    assert(read_reg(sc.sv(), dc.qi.data(), W, n_orkan) == dest_val
           && "forward: dest_copy_out_bits == dest_old (clean write)");

    // Resolve the registered adjoint and run the gate-reverse pass.
    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_mul_mod_inplace_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_mul_mod_inplace_dsl<BitProxy>>() must "
                  "resolve to the registered adjoint");
    adj_ptr(a.bits.data(), d.bits.data(), n.bits.data(), W, dc.bits.data());

    assert(read_reg(sc.sv(), a.qi.data(),  W, n_orkan) == a_val
           && "adjoint: a preserved");
    assert(read_reg(sc.sv(), d.qi.data(),  W, n_orkan) == dest_val
           && "adjoint: dest restored to dest_old");
    assert(read_reg(sc.sv(), n.qi.data(),  W, n_orkan) == n_val
           && "adjoint: n preserved");
    assert(read_reg(sc.sv(), dc.qi.data(), W, n_orkan) == 0u
           && "adjoint: dest_copy_out_bits consumed back to pre-state (|0>)");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "round-trip: pool live-count returns to pre-call value");

    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── classical-trace harness (APPEND mode + bit-vector replay) ────────────
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

// Run lib_mul_mod_inplace_dsl<Wn> for one (a, dest, n) input via the
// trace harness, optionally also running the registered adjoint and
// asserting the round-trip post-conditions.
template <std::size_t Wn>
static void run_trace_case(uint32_t a_val, uint32_t dest_val, uint32_t n_val,
                           bool also_roundtrip) {
    assert(a_val < n_val && dest_val < n_val && n_val < (1u << Wn));
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(Wn);
    int qi_a[Wn], qi_d[Wn], qi_n[Wn], qi_dc[Wn];
    for (std::size_t i = 0; i < Wn; ++i)
        qi_a[i]  = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_d[i]  = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_n[i]  = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_dc[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    sturm::qbool a_own[Wn], d_own[Wn], n_own[Wn], dc_own[Wn];
    sturm::BitProxy a_bits[Wn], d_bits[Wn], n_bits[Wn], dc_bits[Wn];
    for (std::size_t i = 0; i < Wn; ++i) {
        a_own[i]  = sturm::qbool::make_non_owning(qi_a[i]);
        d_own[i]  = sturm::qbool::make_non_owning(qi_d[i]);
        // Seed n.value classically (matches the convention in
        // test_square_mod_dsl.cpp / test_mul_mod_dsl_oneshot.cpp;
        // harmless when no dispatcher reads it).
        n_own[i]  = sturm::qbool::make_non_owning(
            qi_n[i],
            static_cast<int64_t>((n_val >> i) & 1u),
            /*mask=*/0ULL);
        dc_own[i] = sturm::qbool::make_non_owning(qi_dc[i]);
        a_bits[i]  = sturm::BitProxy(a_own[i]);
        d_bits[i]  = sturm::BitProxy(d_own[i]);
        n_bits[i]  = sturm::BitProxy(n_own[i]);
        dc_bits[i] = sturm::BitProxy(dc_own[i]);
    }

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 128u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_mul_mod_inplace_dsl<sturm::BitProxy>(
        a_bits, d_bits, n_bits, Wn, dc_bits);

    if (also_roundtrip) {
        constexpr auto adj_ptr =
            sturm::invert<&sturm::lib_mul_mod_inplace_dsl<sturm::BitProxy>>();
        static_assert(adj_ptr != nullptr,
                      "invert<&lib_mul_mod_inplace_dsl<BitProxy>>() must "
                      "resolve");
        adj_ptr(a_bits, d_bits, n_bits, Wn, dc_bits);
    }

    const int high_water = sturm::QubitPool::instance().high_water();

    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < Wn; ++i) {
        if ((a_val >> i)    & 1u) bits[static_cast<std::size_t>(qi_a[i])]  = 1u;
        if ((dest_val >> i) & 1u) bits[static_cast<std::size_t>(qi_d[i])]  = 1u;
        if ((n_val >> i)    & 1u) bits[static_cast<std::size_t>(qi_n[i])]  = 1u;
        // dest_copy_out_bits and all ancillas start |0>.
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));

    if (also_roundtrip) {
        // Round-trip: dest should be back to dest_old, dest_copy back
        // to |0>, a / n unchanged, every ancilla |0>.
        assert(read_reg_classical(bits, qi_a, Wn) == a_val
               && "trace round-trip: a preserved");
        assert(read_reg_classical(bits, qi_d, Wn) == dest_val
               && "trace round-trip: dest restored to dest_old");
        assert(read_reg_classical(bits, qi_n, Wn) == n_val
               && "trace round-trip: n preserved");
        assert(read_reg_classical(bits, qi_dc, Wn) == 0u
               && "trace round-trip: dest_copy_out_bits back to |0>");
    } else {
        const uint32_t expect_d = (a_val * dest_val) % n_val;
        assert(read_reg_classical(bits, qi_a, Wn) == a_val
               && "trace forward: a preserved");
        assert(read_reg_classical(bits, qi_d, Wn) == expect_d
               && "trace forward: dest == (a * dest_old) mod n");
        assert(read_reg_classical(bits, qi_n, Wn) == n_val
               && "trace forward: n preserved");
        assert(read_reg_classical(bits, qi_dc, Wn) == dest_val
               && "trace forward: dest_copy_out_bits == dest_old");
    }
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u && "trace: ancilla not cleaned");
    }
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "trace: pool live-count restored");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_dc[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_d[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_a[i]);
}

// ── XOR-into accumulation case for dest_copy_out_bits ──────────────────
// Mirrors sturm-3sfl.2's run_xor_into_case: pre-flip
// dest_copy_out_bits to a known nonzero pattern, run the forward, and
// verify dest_copy_out_bits accumulates (XORs in) dest_old rather than
// overwriting.  This pins the documented "XOR-into" contract; without
// this, a |0>-pre adjoint round-trip would silently still work, but
// XOR-into is the explicit semantic for cleaner caller-side
// composition (e.g. lib_pow_mod_dsl batching multiple in-place mul
// calls into a shared witness register).
template <std::size_t Wn>
static void run_xor_into_case(uint32_t a_val, uint32_t dest_val,
                              uint32_t n_val, uint32_t dc_pre) {
    assert(a_val < n_val && dest_val < n_val && n_val < (1u << Wn)
           && dc_pre < (1u << Wn));
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(Wn);
    int qi_a[Wn], qi_d[Wn], qi_n[Wn], qi_dc[Wn];
    for (std::size_t i = 0; i < Wn; ++i)
        qi_a[i]  = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_d[i]  = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_n[i]  = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_dc[i] = sturm::QubitPool::instance().allocate();

    sturm::qbool a_own[Wn], d_own[Wn], n_own[Wn], dc_own[Wn];
    sturm::BitProxy a_bits[Wn], d_bits[Wn], n_bits[Wn], dc_bits[Wn];
    for (std::size_t i = 0; i < Wn; ++i) {
        a_own[i]  = sturm::qbool::make_non_owning(qi_a[i]);
        d_own[i]  = sturm::qbool::make_non_owning(qi_d[i]);
        n_own[i]  = sturm::qbool::make_non_owning(
            qi_n[i],
            static_cast<int64_t>((n_val >> i) & 1u), 0ULL);
        dc_own[i] = sturm::qbool::make_non_owning(qi_dc[i]);
        a_bits[i]  = sturm::BitProxy(a_own[i]);
        d_bits[i]  = sturm::BitProxy(d_own[i]);
        n_bits[i]  = sturm::BitProxy(n_own[i]);
        dc_bits[i] = sturm::BitProxy(dc_own[i]);
    }

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 128u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_mul_mod_inplace_dsl<sturm::BitProxy>(
        a_bits, d_bits, n_bits, Wn, dc_bits);

    const int high_water = sturm::QubitPool::instance().high_water();
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < Wn; ++i) {
        if ((a_val >> i)    & 1u) bits[static_cast<std::size_t>(qi_a[i])]  = 1u;
        if ((dest_val >> i) & 1u) bits[static_cast<std::size_t>(qi_d[i])]  = 1u;
        if ((n_val >> i)    & 1u) bits[static_cast<std::size_t>(qi_n[i])]  = 1u;
        // Pre-flip dest_copy_out_bits to dc_pre.
        if ((dc_pre >> i)   & 1u) bits[static_cast<std::size_t>(qi_dc[i])] = 1u;
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));

    const uint32_t expect_dc = dc_pre ^ dest_val;
    assert(read_reg_classical(bits, qi_dc, Wn) == expect_dc
           && "XOR-into: dest_copy_out_bits_post == "
              "dest_copy_out_bits_pre XOR dest_old");
    // Note: with non-|0> dc_pre, the inner mul_mod_dsl_oneshot is
    //       still called with the original (a, dest_old) factors (the
    //       dest_copy_out XOR-load happens BEFORE the oneshot in
    //       step 1, but oneshot reads dest_bits, not
    //       dest_copy_out_bits), so r_reg = a * dest_old mod n still
    //       holds.  After swap and XOR-uncopy with the (now non-zero)
    //       witness, r_reg = dest_old XOR dest_copy_out_bits =
    //       dest_old XOR (dc_pre XOR dest_old) = dc_pre.  This means
    //       r_reg is NOT cleared to |0> when dc_pre != 0 — matching
    //       Beat D-B's same caveat (the witness must be pre-zeroed
    //       OR the caller must downstream-consume the entanglement
    //       via the matched adjoint).  We do not assert post-forward
    //       value of dest_bits / r_reg cleanup here — the only
    //       contract this test pins is the XOR-into accumulation on
    //       dest_copy_out_bits itself.

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_dc[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_d[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_a[i]);
}

// ── ancilla budget probe (peak high_water vs pre-call in_use) ───────────
// The witness-route impl (mirroring Beat D-B) allocates 1 W-bit
// scratch r_reg above the inner `lib_mul_mod_dsl_oneshot`, whose own
// peak interior is 3W + 7 (post sturm-4oot.3, see
// test_mul_mod_dsl_oneshot_ancilla.cpp).  Total peak above the 4W
// caller-allocated registers (a, dest, n, dest_copy_out):
//     W [r_reg] + (3W + 7) [oneshot interior] = 4W + 7.
//
// This is one slot more than the issue's "~3W + O(1)" hint because
// the witness route needs the explicit r_reg scratch as the home for
// the out-of-place product before the swap.  Same accounting as
// Beat D-B's lib_square_mod_dsl (also 4W + 7).  PRD §6.4 `W + O(1)`
// requirement is met.
static constexpr int kMulModInplaceAncillaBudget(int W) { return 4 * W + 7; }

template <std::size_t W>
static int peak_ancilla_for(uint32_t a_val, uint32_t dest_val, uint32_t n_val) {
    assert(a_val < n_val);
    assert(dest_val < n_val);
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(W);
    int qi_a[W], qi_d[W], qi_n[W], qi_dc[W];
    for (std::size_t i = 0; i < W; ++i)
        qi_a[i]  = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_d[i]  = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_n[i]  = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_dc[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    sturm::qbool a_own[W], d_own[W], n_own[W], dc_own[W];
    sturm::BitProxy a_bits[W], d_bits[W], n_bits[W], dc_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        a_own[i]  = sturm::qbool::make_non_owning(qi_a[i]);
        d_own[i]  = sturm::qbool::make_non_owning(qi_d[i]);
        n_own[i]  = sturm::qbool::make_non_owning(
            qi_n[i],
            static_cast<int64_t>((n_val >> i) & 1u), 0ULL);
        dc_own[i] = sturm::qbool::make_non_owning(qi_dc[i]);
        a_bits[i]  = sturm::BitProxy(a_own[i]);
        d_bits[i]  = sturm::BitProxy(d_own[i]);
        n_bits[i]  = sturm::BitProxy(n_own[i]);
        dc_bits[i] = sturm::BitProxy(dc_own[i]);
    }

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 128u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    (void)a_val;
    (void)dest_val;
    sturm::lib_mul_mod_inplace_dsl<sturm::BitProxy>(
        a_bits, d_bits, n_bits, W, dc_bits);

    const int peak = sturm::QubitPool::instance().high_water() - pre_in_use;
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "ancilla probe: pool live-count returns to pre-call value");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_dc[i]);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_d[i]);
    for (std::size_t i = W; i-- > 0;) sturm::QubitPool::instance().release(qi_a[i]);
    return peak;
}

int main() {
    // ── n==0 ───────────────────────────────────────────────────────────
    std::printf("sturm-3sfl.1 mul-mod-inplace-dsl: n==0 short-circuit:\n");
    run_n_zero_case();
    std::puts("  PASS: n==0 leaves a/dest/n/dest_copy_out unchanged, "
              "no allocation");

    // ── W=2 single classical simulator witness (forward + adjoint) ────
    constexpr std::size_t W2 = 2u;
    // Peak above 4W=8 inputs is ~4W+7=15 → ~23 live qubits at W=2.
    // Bypass kMaxQubits=17 cap with a 25-qubit orkan state.
    constexpr uint32_t n_orkan_w2 = 25u;
    std::printf("sturm-3sfl.1 mul-mod-inplace-dsl: single W=2 simulator "
                "witness (a=2, dest=1, n=3) round-trip:\n");
    run_simulator_roundtrip<W2>(/*a=*/2u, /*dest=*/1u, /*n=*/3u, n_orkan_w2,
                                /*bypass=*/true);
    std::puts("  PASS: orkan-simulator forward+adjoint for "
              "(2*1) mod 3 == 2");

    // ── W=2 exhaustive sweep over (a, dest, n) with a != dest ─────────
    // n in [1, 4); for each n, all (a, dest) in [0, n)^2 with a != dest.
    // Includes non-coprime (a, n) cases (a=0; a=2 with n=2; etc.) which
    // the witness-route impl handles correctly (the squaring-style
    // witness register makes the non-injective cases reversible).
    std::printf("sturm-3sfl.1 mul-mod-inplace-dsl: W=2 exhaustive trace "
                "sweep (n in {1, 2, 3}, all (a, dest) in [0, n)^2 with "
                "a != dest, forward+adjoint):\n");
    std::size_t cases_w2 = 0u;
    for (uint32_t n_val : {1u, 2u, 3u}) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t dest_val = 0u; dest_val < n_val; ++dest_val) {
                if (a_val == dest_val) continue;  // squaring case
                                                  // forbidden — handled
                                                  // by lib_square_mod_dsl.
                run_trace_case<W2>(a_val, dest_val, n_val,
                                    /*also_roundtrip=*/false);
                run_trace_case<W2>(a_val, dest_val, n_val,
                                    /*also_roundtrip=*/true);
                ++cases_w2;
            }
        }
    }
    // n=1: 0 cases (only (0,0), aliased).
    // n=2: 2 cases ((0,1), (1,0)).
    // n=3: 6 cases ((0,1),(0,2),(1,0),(1,2),(2,0),(2,1)).
    // Total = 8 (each run twice: forward-only and round-trip).
    assert(cases_w2 == 8u);
    std::printf("  PASS: %zu W=2 cases (each forward and round-trip)\n",
                cases_w2);

    // ── W=3 random sweep including even n ─────────────────────────────
    constexpr std::size_t W3 = 3u;
    std::printf("sturm-3sfl.1 mul-mod-inplace-dsl: W=3 random trace sweep "
                "(50 cases, seed=3581, n in {2..7}, includes even n "
                "and non-coprime (a, n) pairs):\n");
    {
        constexpr uint32_t kSeed = 3581u;  // sturm-3sfl.1-derived
        constexpr std::size_t kCases = 50u;
        std::mt19937 rng(kSeed);
        std::uniform_int_distribution<uint32_t> n_dist(2u, 7u);
        std::size_t accepted = 0u;
        std::size_t attempted = 0u;
        while (accepted < kCases) {
            ++attempted;
            uint32_t n_val = n_dist(rng);
            std::uniform_int_distribution<uint32_t> ad_dist(0u, n_val - 1u);
            uint32_t a_val    = ad_dist(rng);
            uint32_t dest_val = ad_dist(rng);
            if (a_val == dest_val) continue;  // skip squaring case
            // Run both forward-only and round-trip on each random case.
            run_trace_case<W3>(a_val, dest_val, n_val,
                                /*roundtrip=*/false);
            run_trace_case<W3>(a_val, dest_val, n_val,
                                /*roundtrip=*/true);
            ++accepted;
        }
        std::printf("  PASS: %zu W=3 random cases (forward + round-trip; "
                    "%zu attempts after squaring-case skips)\n",
                    accepted, attempted);
    }

    // ── XOR-into / threading round-trip for dest_copy_out_bits ────────
    // sturm-3sfl.1: dest_copy_out_bits is XOR-into.  Pre-flip the
    // witness to a nonzero pattern, run forward, and verify it
    // accumulates (XORs in) dest_old rather than overwriting.  Mirrors
    // sturm-3sfl.2's x_copy_out_bits XOR-into test.
    std::printf("sturm-3sfl.1 mul-mod-inplace-dsl: dest_copy_out_bits "
                "XOR-into threading round-trip:\n");
    run_xor_into_case<W2>(/*a=*/2u, /*dest=*/1u, /*n=*/3u, /*dc_pre=*/1u);
    run_xor_into_case<W3>(/*a=*/3u, /*dest=*/4u, /*n=*/5u, /*dc_pre=*/2u);
    run_xor_into_case<W3>(/*a=*/3u, /*dest=*/5u, /*n=*/6u, /*dc_pre=*/5u);  // even-n
    std::puts("  PASS: dest_copy_out_bits_post == "
              "dest_copy_out_bits_pre XOR dest_old across W=2 / W=3 "
              "cases (incl. even n)");

    // ── Ancilla budget pin (peak high_water) ──────────────────────────
    {
        constexpr std::size_t W       = 2u;
        constexpr int        kBudget  =
            kMulModInplaceAncillaBudget(static_cast<int>(W));
        std::printf("sturm-3sfl.1 mul-mod-inplace-dsl: peak ancilla bound "
                    "<= 4W + 7 (W=%zu, n=3, budget=%d):\n", W, kBudget);
        const int peak = peak_ancilla_for<W>(/*a=*/2u, /*dest=*/1u,
                                              /*n=*/3u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak, 4u * static_cast<uint32_t>(W));
        std::fflush(stdout);
        assert(peak <= kBudget
               && "Beat D-A mul_mod_inplace: peak ancilla exceeds budget "
                  "at W=2");
        std::puts("  PASS: W=2 mul_mod_inplace peak ancilla within budget");

        // Even n=4 doesn't fit at W=2, so jump to W=3 for the even-n leg.
        constexpr std::size_t W3a       = 3u;
        constexpr int        kBudget3  =
            kMulModInplaceAncillaBudget(static_cast<int>(W3a));
        std::printf("sturm-3sfl.1 mul-mod-inplace-dsl: peak ancilla bound "
                    "<= 4W + 7 (W=%zu, even n=4, budget=%d):\n", W3a,
                    kBudget3);
        const int peak_w3 = peak_ancilla_for<W3a>(/*a=*/3u, /*dest=*/1u,
                                                    /*n=*/4u);
        std::printf("  measured peak = %d ancillas above %u inputs\n",
                    peak_w3, 4u * static_cast<uint32_t>(W3a));
        std::fflush(stdout);
        assert(peak_w3 <= kBudget3
               && "Beat D-A mul_mod_inplace: peak ancilla exceeds budget "
                  "at W=3 (even n)");
        std::puts("  PASS: W=3 mul_mod_inplace peak ancilla within budget "
                  "(even n)");
    }

    std::printf("All sturm-3sfl.1 Beat D-A mul-mod-inplace-dsl tests "
                "passed.\n");
    return 0;
}
