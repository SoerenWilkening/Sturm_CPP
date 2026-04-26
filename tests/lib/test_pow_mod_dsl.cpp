// test_pow_mod_dsl.cpp -- sturm-a5te.{1,2,3,4} P3.{1,2,3,4} pow-mod-dsl
// beats 3.1, 3.2, 3.3, 3.4.
//
// Beat 3.1 (plan §5.3): `lib_pow_mod_dsl(... n=0 ...)` short-circuits and
// leaves r (and base, exp, n) unchanged.  Mirrors PRD §8 #3 and the shape
// of add-mod beat 1.1 / mul-mod beat 2.1.  Driven by run_n_zero_case via
// SimCtx (orkan-backed simulator).
//
// Beat 3.2 (plan §5.3): `0^0 == 1` convention (matches lib_pow_dsl).  With
// base=0 and exp=0 the loop body never enters the conditional
// `if exp[i]: acc := mul_mod(acc, sq, n)`; acc stays at its initial value
// 1, so r=1.  Driven by run_pow_zero_zero_case via APPEND-mode classical
// replay (cheapest executor) — at W=2 the chain-style algorithm peaks
// above orkan's 30-qubit ceiling so simulator-based testing is
// infeasible; the trace harness mirrors test_mul_mod_dsl.cpp's W=3 sweep.
//
// Beat 3.3 (plan §5.3): `pow_mod(2, 3, 5) == 3` — single-classical-case
// proof that the chain-style repeated-squaring algorithm in
// lib_pow_mod_dsl computes (base^exp) mod n correctly.  Plan §5.3 row 3.3
// literally says "W=2 single case", but the modulus n=5 (binary 101)
// does not fit in 2 bits — the minimum width to represent n=5 is W=3.
// The plan typo will be corrected in a follow-up; we use W=3 here.  At
// W=3 chain-style pow_mod peaks far above orkan's 30-qubit ceiling, so
// simulator-driven verification is infeasible; we use APPEND-mode
// classical replay (same pattern as beat 2.4 / beat 3.2).
//
// Beat 3.4 (plan §5.3, sturm-a5te.4): exhaustive W=2 sweep over every
// (base, exp, n) with `base ∈ [0, n)`, `exp ∈ [0, 2^W)`, `n ∈ [1, 2^W)`.
// PRD §5 sets the precondition `base ∈ [0, n)` for pow_mod (the modulus
// must reduce the base); the exponent is just a binary expansion that
// drives the chain-style repeated-squaring loop, so any value in the
// register range is valid (including exp ≥ n).  Mirrors mul-mod beat 2.3
// (sturm-kubb.3) shape but uses APPEND-mode classical replay, not orkan
// simulation: at W=2 the chain-style pow_mod peaks above orkan's 30-qubit
// ceiling, so simulator-driven verification is infeasible.  Each case
// asserts r == (base^exp) mod n, base/exp/n unchanged (reversibility of
// inputs), every ancilla bit cleaned to 0, and pool live-count returns
// to its pre-call value.  Total cases:
//   n=1 → 1×4 = 4
//   n=2 → 2×4 = 8
//   n=3 → 3×4 = 12
//   ────────────
//                24 cases.

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/pow_mod_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
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
#include <vector>

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

// ── Beat 3.2 — 0^0 == 1 convention (APPEND-mode classical replay) ─────────
//
// Replay supports only X / CX / CCX (pow_mod composes classical-reversible
// primitives only); any other gate kind aborts.  See header comment above
// for why APPEND mode is used here.

// Apply one gate record to the classical bit-vector.  Mirrors the
// dispatch used in test_mul_mod_dsl.cpp::apply_gate_classical.
static void apply_gate_classical(std::vector<uint8_t>& bits,
                                 const sturm::GateRecord& rec) {
    switch (rec.kind) {
    case STURM_GATE_X:
        bits[rec.qubits[0]] ^= 1u;
        break;
    case STURM_GATE_CX:
        if (bits[rec.qubits[0]]) bits[rec.qubits[1]] ^= 1u;
        break;
    case STURM_GATE_CCX:
        if (bits[rec.qubits[0]] && bits[rec.qubits[1]])
            bits[rec.qubits[2]] ^= 1u;
        break;
    default:
        std::fprintf(stderr,
                     "trace: unsupported gate kind %d at index %u\n",
                     static_cast<int>(rec.kind), rec.qubits[0]);
        std::abort();
    }
}

// Read a register's classical value from the bit-vector.
static uint32_t read_reg_classical(const std::vector<uint8_t>& bits,
                                   const int* qi, std::size_t n) {
    uint32_t v = 0u;
    for (std::size_t k = 0; k < n; ++k) {
        if (qi[k] >= 0 && bits[static_cast<std::size_t>(qi[k])])
            v |= (1u << k);
    }
    return v;
}

// Beat 3.2 driver — pow_mod(0, 0, n) == 1 convention.
//
// Asserts:
//   - r register holds 1 (the 0^0 = 1 convention),
//   - base, exp, n registers unchanged (reversibility of inputs),
//   - QubitPool::in_use() returns to its pre-call value (no leaked
//     ancillas; algorithm cleans up after itself).
static void run_pow_zero_zero_case(uint32_t n_val) {
    assert(n_val >= 1u && "test precondition: n >= 1 (n==0 is the no-op path)");
    assert(n_val < (1u << W) && "test precondition: n fits in W bits");

    sturm::QubitPool::instance().reset_for_testing();

    // Reserve the 4*W lowest qubit indices for base, exp, n, r so we
    // know exactly which slots in the classical bit-vector hold the
    // inputs.
    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(W);
    int qi_base[W], qi_exp[W], qi_n[W], qi_r[W];
    for (std::size_t i = 0; i < W; ++i)
        qi_base[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_exp[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_r[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    // Wrap the reserved indices as non-owning qbools / BitProxies so
    // the algorithm sees them as quantum (is_quantum() == true) and
    // emits gates rather than classical-folding.
    sturm::qbool base_own[W], exp_own[W], n_own[W], r_own[W];
    sturm::BitProxy base_bits[W], exp_bits[W], n_bits[W], r_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        base_own[i] = sturm::qbool::make_non_owning(qi_base[i]);
        exp_own[i]  = sturm::qbool::make_non_owning(qi_exp[i]);
        n_own[i]    = sturm::qbool::make_non_owning(qi_n[i]);
        r_own[i]    = sturm::qbool::make_non_owning(qi_r[i]);
        base_bits[i] = sturm::BitProxy(base_own[i]);
        exp_bits[i]  = sturm::BitProxy(exp_own[i]);
        n_bits[i]    = sturm::BitProxy(n_own[i]);
        r_bits[i]    = sturm::BitProxy(r_own[i]);
    }

    // Install an APPEND-mode context (no orkan needed: in APPEND mode
    // execute_gate just records to ctx.ir).
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 64u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_pow_mod_dsl<sturm::BitProxy>(base_bits, exp_bits, n_bits,
                                            W, r_bits);

    // Snapshot the high-water mark before tearing down the context so
    // we know how wide to size the classical bit-vector.
    const int high_water = sturm::QubitPool::instance().high_water();

    // Replay the captured gate stream classically.  base = 0, exp = 0
    // (the 0^0 case), and r starts at |0>; only the n register has a
    // non-zero seed.
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < W; ++i) {
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i) {
        apply_gate_classical(bits, ctx->ir.at(i));
    }

    // Verify.  base/exp/n untouched (reversible inputs), r == 1.
    const uint32_t base_out = read_reg_classical(bits, qi_base, W);
    const uint32_t exp_out  = read_reg_classical(bits, qi_exp,  W);
    const uint32_t n_out    = read_reg_classical(bits, qi_n,    W);
    const uint32_t r_out    = read_reg_classical(bits, qi_r,    W);
    assert(base_out == 0u && "0^0 trace: base register unchanged (still 0)");
    assert(exp_out  == 0u && "0^0 trace: exp register unchanged (still 0)");
    assert(n_out    == n_val && "0^0 trace: n register unchanged");
    assert(r_out    == 1u && "0^0 trace: r == 1 (0^0 convention)");

    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "0^0 trace: pool live-count returns to pre-call value");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);

    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_exp[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_base[i]);
}

// ── Beat 3.3 — W=3 single classical case `pow_mod(2, 3, 5) == 3` ─────────
//
// APPEND-mode capture + classical replay (same harness as beat 2.4 /
// beat 3.2).  At W=3 the chain-style pow_mod algorithm peaks far above
// orkan's 30-qubit ceiling, so simulator-based verification is
// infeasible.  Instead we run lib_pow_mod_dsl in APPEND mode (no
// statevector), capture the X/CX/CCX gate stream into ctx.ir, then
// replay the stream as a deterministic bit-flip program over a
// classical bit-vector seeded with the chosen (base, exp, n) values.
//
// Asserts:
//   - r register holds (base^exp) mod n,
//   - base, exp, n registers unchanged (reversibility of inputs),
//   - every ancilla bit (qubits beyond the 4*W input slots) is back to 0,
//   - QubitPool::in_use() returns to its pre-call value (no leaked
//     ancillas; the algorithm cleans up after itself).

static constexpr std::size_t W3 = 3u;

static void run_pow_classical_case_w3(uint32_t base_val, uint32_t exp_val,
                                      uint32_t n_val) {
    assert(n_val < (1u << W3) && "test precondition: n fits in W3 bits");
    assert(base_val < n_val && "test precondition: base < n (PRD §5)");
    assert(exp_val < n_val && "test precondition: exp < n (PRD §5)");

    sturm::QubitPool::instance().reset_for_testing();

    // Reserve the 4*W=12 lowest qubit indices for base, exp, n, r so we
    // know exactly which slots in the classical bit-vector hold the
    // inputs.
    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(W3);
    int qi_base[W3], qi_exp[W3], qi_n[W3], qi_r[W3];
    for (std::size_t i = 0; i < W3; ++i)
        qi_base[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W3; ++i)
        qi_exp[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W3; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W3; ++i)
        qi_r[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    // Wrap the reserved indices as non-owning qbools / BitProxies so the
    // algorithm sees them as quantum (is_quantum() == true) and emits
    // gates rather than classical-folding.
    sturm::qbool base_own[W3], exp_own[W3], n_own[W3], r_own[W3];
    sturm::BitProxy base_bits[W3], exp_bits[W3], n_bits[W3], r_bits[W3];
    for (std::size_t i = 0; i < W3; ++i) {
        base_own[i] = sturm::qbool::make_non_owning(qi_base[i]);
        exp_own[i]  = sturm::qbool::make_non_owning(qi_exp[i]);
        n_own[i]    = sturm::qbool::make_non_owning(qi_n[i]);
        r_own[i]    = sturm::qbool::make_non_owning(qi_r[i]);
        base_bits[i] = sturm::BitProxy(base_own[i]);
        exp_bits[i]  = sturm::BitProxy(exp_own[i]);
        n_bits[i]    = sturm::BitProxy(n_own[i]);
        r_bits[i]    = sturm::BitProxy(r_own[i]);
    }

    // Install an APPEND-mode context (no orkan needed: in APPEND mode
    // execute_gate just records to ctx.ir).
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 64u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_pow_mod_dsl<sturm::BitProxy>(base_bits, exp_bits, n_bits,
                                            W3, r_bits);

    // Snapshot the high-water mark before tearing down the context so
    // we know how wide to size the classical bit-vector.
    const int high_water = sturm::QubitPool::instance().high_water();

    // Replay the captured gate stream classically.  Seed base/exp/n
    // bits at the qubit indices reserved above; r starts at |0>.
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < W3; ++i) {
        if ((base_val >> i) & 1u) bits[static_cast<std::size_t>(qi_base[i])] = 1u;
        if ((exp_val  >> i) & 1u) bits[static_cast<std::size_t>(qi_exp[i])]  = 1u;
        if ((n_val    >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])]    = 1u;
        // r starts at |0> (per PRD §5 precondition).
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i) {
        apply_gate_classical(bits, ctx->ir.at(i));
    }

    // Verify result.  Reference: (base^exp) mod n via repeated unsigned
    // multiplication (uint64_t guards against overflow at small values).
    uint64_t expect_r64 = 1u;
    for (uint32_t k = 0; k < exp_val; ++k)
        expect_r64 = (expect_r64 * static_cast<uint64_t>(base_val))
                     % static_cast<uint64_t>(n_val);
    const uint32_t expect_r = static_cast<uint32_t>(expect_r64);

    const uint32_t base_out = read_reg_classical(bits, qi_base, W3);
    const uint32_t exp_out  = read_reg_classical(bits, qi_exp,  W3);
    const uint32_t n_out    = read_reg_classical(bits, qi_n,    W3);
    const uint32_t r_out    = read_reg_classical(bits, qi_r,    W3);
    assert(base_out == base_val && "W=3 trace: base register unchanged");
    assert(exp_out  == exp_val  && "W=3 trace: exp register unchanged");
    assert(n_out    == n_val    && "W=3 trace: n register unchanged");
    assert(r_out    == expect_r && "W=3 trace: r == (base^exp) mod n");

    // Verify every ancilla bit (qubits beyond the 4*W input slots) is
    // back to 0 — the algorithm must clean up after itself.
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u && "W=3 trace: ancilla bit not cleaned up");
    }

    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "W=3 trace: pool live-count returns to pre-call value");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);

    for (std::size_t i = W3; i-- > 0;)
        sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = W3; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W3; i-- > 0;)
        sturm::QubitPool::instance().release(qi_exp[i]);
    for (std::size_t i = W3; i-- > 0;)
        sturm::QubitPool::instance().release(qi_base[i]);
}

// ── Beat 3.4 — W=2 exhaustive sweep (APPEND-mode classical replay) ────────
//
// Same harness as run_pow_classical_case_w3 but instantiated at W=2 and
// with the precondition relaxed to PRD §5 (`base ∈ [0, n)`; `exp` may
// take any value the W-bit register represents — i.e. exp ∈ [0, 2^W) —
// because the chain-style algorithm only reads exp bit-by-bit).  At W=2
// the chain-style pow_mod peaks above orkan's 30-qubit ceiling, so
// simulator-driven verification is infeasible; APPEND-mode capture +
// classical replay is the same trick beats 3.2/3.3 already use.
//
// Asserts (per case):
//   - r register holds (base^exp) mod n,
//   - base, exp, n registers unchanged (reversibility of inputs),
//   - every ancilla bit (qubits beyond the 4*W input slots) is back to 0,
//   - QubitPool::in_use() returns to its pre-call value (no leaked
//     ancillas; the algorithm cleans up after itself).
static void run_pow_classical_case_w2(uint32_t base_val, uint32_t exp_val,
                                      uint32_t n_val) {
    assert(n_val >= 1u && "test precondition: n >= 1 (n==0 is the no-op path)");
    assert(n_val < (1u << W) && "test precondition: n fits in W bits");
    assert(base_val < n_val && "test precondition: base < n (PRD §5)");
    assert(exp_val < (1u << W) && "test precondition: exp fits in W bits");

    sturm::QubitPool::instance().reset_for_testing();

    // Reserve the 4*W=8 lowest qubit indices for base, exp, n, r so we
    // know exactly which slots in the classical bit-vector hold the
    // inputs.
    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(W);
    int qi_base[W], qi_exp[W], qi_n[W], qi_r[W];
    for (std::size_t i = 0; i < W; ++i)
        qi_base[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_exp[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_r[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    // Wrap the reserved indices as non-owning qbools / BitProxies so the
    // algorithm sees them as quantum (is_quantum() == true) and emits
    // gates rather than classical-folding.
    sturm::qbool base_own[W], exp_own[W], n_own[W], r_own[W];
    sturm::BitProxy base_bits[W], exp_bits[W], n_bits[W], r_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        base_own[i] = sturm::qbool::make_non_owning(qi_base[i]);
        exp_own[i]  = sturm::qbool::make_non_owning(qi_exp[i]);
        n_own[i]    = sturm::qbool::make_non_owning(qi_n[i]);
        r_own[i]    = sturm::qbool::make_non_owning(qi_r[i]);
        base_bits[i] = sturm::BitProxy(base_own[i]);
        exp_bits[i]  = sturm::BitProxy(exp_own[i]);
        n_bits[i]    = sturm::BitProxy(n_own[i]);
        r_bits[i]    = sturm::BitProxy(r_own[i]);
    }

    // Install an APPEND-mode context (no orkan needed: in APPEND mode
    // execute_gate just records to ctx.ir).
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 64u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_pow_mod_dsl<sturm::BitProxy>(base_bits, exp_bits, n_bits,
                                            W, r_bits);

    // Snapshot the high-water mark before tearing down the context so
    // we know how wide to size the classical bit-vector.
    const int high_water = sturm::QubitPool::instance().high_water();

    // Replay the captured gate stream classically.  Seed base/exp/n
    // bits at the qubit indices reserved above; r starts at |0>.
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < W; ++i) {
        if ((base_val >> i) & 1u) bits[static_cast<std::size_t>(qi_base[i])] = 1u;
        if ((exp_val  >> i) & 1u) bits[static_cast<std::size_t>(qi_exp[i])]  = 1u;
        if ((n_val    >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])]    = 1u;
        // r starts at |0> (per PRD §5 precondition).
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i) {
        apply_gate_classical(bits, ctx->ir.at(i));
    }

    // Verify result.  Reference: (base^exp) mod n via repeated unsigned
    // multiplication (uint64_t guards against overflow at small values).
    // Convention: 0^0 = 1 — matches lib_pow_dsl and the algorithm's
    // expected output (the loop never multiplies by base for exp=0).
    uint64_t expect_r64 = 1u;
    for (uint32_t k = 0; k < exp_val; ++k)
        expect_r64 = (expect_r64 * static_cast<uint64_t>(base_val))
                     % static_cast<uint64_t>(n_val);
    const uint32_t expect_r = static_cast<uint32_t>(expect_r64);

    const uint32_t base_out = read_reg_classical(bits, qi_base, W);
    const uint32_t exp_out  = read_reg_classical(bits, qi_exp,  W);
    const uint32_t n_out    = read_reg_classical(bits, qi_n,    W);
    const uint32_t r_out    = read_reg_classical(bits, qi_r,    W);
    assert(base_out == base_val && "W=2 trace: base register unchanged");
    assert(exp_out  == exp_val  && "W=2 trace: exp register unchanged");
    assert(n_out    == n_val    && "W=2 trace: n register unchanged");
    assert(r_out    == expect_r && "W=2 trace: r == (base^exp) mod n");

    // Verify every ancilla bit (qubits beyond the 4*W input slots) is
    // back to 0 — the algorithm must clean up after itself.
    for (std::size_t q = static_cast<std::size_t>(n_reg);
         q < bits.size(); ++q) {
        assert(bits[q] == 0u && "W=2 trace: ancilla bit not cleaned up");
    }

    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "W=2 trace: pool live-count returns to pre-call value");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);

    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_exp[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_base[i]);
}

int main() {
    std::printf("sturm-a5te.1 P3.1 pow-mod-dsl: n==0 no-op tests:\n");
    run_n_zero_case(/*base=*/1u, /*exp=*/2u, /*n=*/3u, /*r=*/0u);
    std::puts("  PASS: n==0 with r=|0> leaves r at 0");
    run_n_zero_case(/*base=*/1u, /*exp=*/2u, /*n=*/3u, /*r=*/3u);
    std::puts("  PASS: n==0 with r=3 leaves r at 3");
    run_n_zero_case(/*base=*/0u, /*exp=*/0u, /*n=*/0u, /*r=*/2u);
    std::puts("  PASS: n==0 with all-zero inputs and r=2 leaves r at 2");

    std::printf("sturm-a5te.2 P3.2 pow-mod-dsl: 0^0 == 1 convention "
                "(APPEND-mode classical trace):\n");
    run_pow_zero_zero_case(/*n=*/3u);
    std::puts("  PASS: pow_mod(0, 0, 3) == 1");
    run_pow_zero_zero_case(/*n=*/2u);
    std::puts("  PASS: pow_mod(0, 0, 2) == 1");

    // Beat 3.3 — single classical case `pow_mod(2, 3, 5) == 3`.  Plan
    // §5.3 row 3.3 says "W=2 single case", but n=5 (binary 101) does
    // not fit in 2 bits — minimum width to represent n=5 is W=3.  We
    // use W=3 for this test (plan typo to be corrected in follow-up).
    std::printf("sturm-a5te.3 P3.3 pow-mod-dsl: W=3 single case "
                "(APPEND-mode classical trace):\n");
    run_pow_classical_case_w3(/*base=*/2u, /*exp=*/3u, /*n=*/5u);
    std::puts("  PASS: pow_mod(2, 3, 5) == 3");

    // Beat 3.4 — exhaustive W=2 sweep over every (base, exp, n) with
    // `base ∈ [0, n)`, `exp ∈ [0, 2^W)`, `n ∈ [1, 2^W)`.  PRD §5
    // precondition: `base ∈ [0, n)`.  Exponent iterates the full
    // register range — the algorithm only inspects exp bit-by-bit.
    // Total cases: n=1 → 1×4 = 4; n=2 → 2×4 = 8; n=3 → 3×4 = 12;
    // grand total 24.
    std::printf("sturm-a5te.4 P3.4 pow-mod-dsl: W=2 exhaustive sweep "
                "(all base, exp, n with base in [0,n), exp in [0,4), "
                "n in [1,4); APPEND-mode classical trace):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t base_val = 0u; base_val < n_val; ++base_val) {
            for (uint32_t exp_val = 0u; exp_val < (1u << W); ++exp_val) {
                run_pow_classical_case_w2(base_val, exp_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 → 1 base × 4 exp = 4; n=2 → 2×4 = 8; n=3 → 3×4 = 12; total = 24.
    assert(cases_run == 24u && "W=2 sweep covered every (base, exp, n) "
                               "with base < n, exp < 2^W, and n >= 1");
    std::printf("  PASS: %zu W=2 cases covering every (base, exp, n) "
                "with base in [0, n), exp in [0, 2^W), n in [1, 2^W)\n",
                cases_run);

    std::printf("All sturm-a5te.{1,2,3,4} tests passed.\n");
    return 0;
}
