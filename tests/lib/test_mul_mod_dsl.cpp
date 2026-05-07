// test_mul_mod_dsl.cpp -- sturm-kubb.{1,2,3,4} P2.{1,2,3,4} mul-mod-dsl
// beats 2.1, 2.2, 2.3, 2.4.
//
// Plan §4.3 beat 2.1: `lib_mul_mod_dsl(... n=0 ...)` short-circuits and
// leaves r (and a, b, n) unchanged.  This mirrors add-mod beat 1.1
// (sturm-yh3d.1).
// Plan §4.3 beat 2.2: full shift-and-add algorithm produces
// `r = (a * b) mod n` for the single classical W=2 case.  Per plan §4.1
// the body composes `lib_add_mod_dsl` in a doubling-and-add loop using a
// `shifted` ancilla register chain that starts as `a` and at each step
// holds `(a · 2^i) mod n`.  Beat 2.2 wires up the smallest body that
// gets one classical input correct.
// Plan §4.3 beat 2.3 (sturm-kubb.3): exhaustive W=2 sweep over all
// (a, b, n) with PRD §5 precondition `a, b < n` and `n >= 1`
// (14 cases total: n=1 → 1, n=2 → 4, n=3 → 9).  Mirrors add-mod
// beat 1.3 (sturm-yh3d.3).  Each case asserts r == (a*b) mod n,
// inputs unchanged, and pool live-count returns to its pre-call
// value.
// Plan §4.3 beat 2.4 (sturm-kubb.4): W=3 random sweep (50 cases) traced
// against a classical reference WITHOUT the orkan state-vector simulator.
// Rationale: the chain-style mul_mod algorithm peaks at ~35 live qubits
// at W=3, which means orkan would need 2^35 ≈ 34 GB of amplitudes per
// case (~35 h per case in the stub).  Beat 2.4 sidesteps that by:
//   1. running lib_mul_mod_dsl in APPEND mode (no simulator), capturing
//      every X / CX / CCX into ctx.ir,
//   2. seeding a classical bit-vector with the input register values
//      (a, b, n encoded into the qubit indices the test pre-allocates),
//   3. replaying the IR as a deterministic bit-flip program (X = flip,
//      CX = if ctrl then flip target, CCX = if both ctrls then flip),
//   4. asserting r == (a*b) mod n, inputs unchanged, every ancilla bit
//      back to 0, pool live-count restored to pre-call value.
// The replayer only handles the gate kinds the algorithm actually emits
// (X / CX / CCX); any other kind aborts (defensive — if a refactor adds
// non-classical gates to the multiplication path, the trace harness
// stops being a faithful reference and we want to know loudly).
// Beats 2.5–2.7 broaden coverage (adjoint round-trip, ancilla budget,
// pool live-count).
//
// Test harness layout mirrors test_add_mod_dsl.cpp (beat 1.{1,2,3,4}):
//   - SimCtx wraps OrkanBridge + sturm_backend_context_t with a 17-qubit
//     state vector (kMaxQubits cap) for the n==0 no-op tests; beat 2.2
//     uses the larger orkan stub directly (orkan::allocate(bridge.state,
//     n_orkan_w2_full)) because the W=2 chain algorithm peaks above 17
//     qubits — same workaround as test_add_mod_dsl.cpp's W=3 sweep.
//   - read_reg decodes a register's classical value out of the simulator
//     state vector by scanning for the unique non-zero amplitude.
//   - run_n_zero_case asserts a, b, n, r are all unchanged after the
//     n==0 call.
//   - run_classical_case asserts r == (a*b) mod n, that a, b, n are
//     unchanged (reversibility of inputs), and that QubitPool::in_use()
//     returns to its pre-call value (no leaked ancillas).

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/mul_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
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
#include <random>
#include <vector>

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

// ── Beat 2.2 — W=2 single classical case harness ─────────────────────────
//
// The chain-style shift-and-add algorithm (plan §4.1) keeps
//   shifted_chain[0..W-1]  (W·W qubits)  – (a · 2^i) mod n cache
//   r_chain[1..W]          (W·W qubits)  – partial sums
// alive across the loop, so peak live qubits exceed the kMaxQubits=17
// soft cap.  Bypass with orkan::allocate (same trick test_add_mod_dsl.cpp
// uses for its W=3 random sweep).  25 qubits stays within the orkan
// stub's 30-qubit ceiling.
static constexpr uint32_t n_orkan_w2_full = 25u;

// Beat 2.2: full algorithm, single classical case (a, b, n=3) -> r = (a*b) % n.
//
// Asserts:
//   - r register holds (a * b) mod n,
//   - a, b, n registers unchanged,
//   - QubitPool::in_use() returns to its pre-call value (no leaked ancillas).
static void run_classical_case(uint32_t a_val, uint32_t b_val,
                               uint32_t n_val) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");
    sturm::QubitPool::instance().reset_for_testing();
    const uint32_t n_reg = 4u * W;
    int reserved[n_reg];
    for (uint32_t k = 0; k < n_reg; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    sturm::OrkanBridge bridge;
    orkan::allocate(bridge.state(), n_orkan_w2_full);  // bypass kMaxQubits cap
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_SIMULATE);
    assert(ctx);
    ctx->orkan_state_ptr = &bridge;
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);
    orkan::state_t& sv = bridge.state();
    Reg a = make_reg(0,         a_val, sv);
    Reg b = make_reg(W,         b_val, sv);
    Reg n = make_reg(2 * W,     n_val, sv);
    Reg r = make_reg(3 * W,     0u,    sv);

    sturm::lib_mul_mod_dsl<sturm::BitProxy>(a.bits.data(), b.bits.data(),
                                            n.bits.data(), W,
                                            r.bits.data());

    const uint32_t expect_r = (a_val * b_val) % n_val;
    uint32_t a_sv = read_reg(sv, a.qi.data(), W, n_orkan_w2_full);
    uint32_t b_sv = read_reg(sv, b.qi.data(), W, n_orkan_w2_full);
    uint32_t n_sv = read_reg(sv, n.qi.data(), W, n_orkan_w2_full);
    uint32_t r_sv = read_reg(sv, r.qi.data(), W, n_orkan_w2_full);
    assert(a_sv == a_val && "forward: a register unchanged");
    assert(b_sv == b_val && "forward: b register unchanged");
    assert(n_sv == n_val && "forward: n register unchanged");
    assert(r_sv == expect_r && "forward: r == (a*b) mod n");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "forward: pool live-count returns to pre-call value");
    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (uint32_t k = 0; k < n_reg; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── Beat 2.4 — W=3 random sweep traced classically (no orkan sim) ─────────
//
// Strategy: run lib_mul_mod_dsl in APPEND mode (no statevector), capturing
// the X/CX/CCX gate stream into ctx.ir, then replay the stream as a
// deterministic bit-flip program over a classical bit-vector of width
// `high_water` qubits.  Classical inputs are seeded into the bit-vector
// at the qubit indices the test pre-allocates for the a/b/n/r registers.
//
// At W=3 the algorithm peaks at ~35 live qubits, which would require
// 2^35 ≈ 34 GB of orkan amplitudes per case (~35 h per case in the
// stub).  The classical replay is O(gates) per case and runs in
// milliseconds, so a 50-case sweep is comfortably interactive.

static constexpr std::size_t W3 = 3u;

// Apply one gate record to the classical bit-vector.  Only X / CX / CCX
// are supported — the modular-multiply algorithm is built entirely from
// classical-reversible primitives (no H/Rx/Ry/Rz/S/T/P/Y/Z), so any
// other kind here means the algorithm has gained a non-classical gate
// path and the trace harness is no longer a faithful reference.
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

// Beat 2.4 driver — APPEND-mode capture + classical replay for W=3.
//
// Asserts:
//   - r register holds (a * b) mod n,
//   - a, b, n registers unchanged,
//   - every ancilla bit (qubits beyond the 4*W input slots) is back to 0,
//   - QubitPool::in_use() returns to its pre-call value (no leaked ancillas).
static void run_classical_case_w3(uint32_t a_val, uint32_t b_val,
                                  uint32_t n_val) {
    assert(a_val < n_val && b_val < n_val && n_val < (1u << W3));

    sturm::QubitPool::instance().reset_for_testing();

    // Reserve the 4*W=12 lowest qubit indices for a, b, n, r so we know
    // exactly which slots in the classical bit-vector hold the inputs.
    constexpr uint32_t n_reg = 4u * static_cast<uint32_t>(W3);
    int qi_a[W3], qi_b[W3], qi_n[W3], qi_r[W3];
    for (std::size_t i = 0; i < W3; ++i)
        qi_a[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W3; ++i)
        qi_b[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W3; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W3; ++i)
        qi_r[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    assert(pre_in_use == static_cast<int>(n_reg));

    // Wrap the reserved indices as non-owning qbools / BitProxies so the
    // algorithm sees them as quantum (is_quantum() == true) and emits
    // gates rather than classical-folding.
    sturm::qbool a_own[W3], b_own[W3], n_own[W3], r_own[W3];
    sturm::BitProxy a_bits[W3], b_bits[W3], n_bits[W3], r_bits[W3];
    for (std::size_t i = 0; i < W3; ++i) {
        a_own[i] = sturm::qbool::make_non_owning(qi_a[i]);
        b_own[i] = sturm::qbool::make_non_owning(qi_b[i]);
        n_own[i] = sturm::qbool::make_non_owning(qi_n[i]);
        r_own[i] = sturm::qbool::make_non_owning(qi_r[i]);
        a_bits[i] = sturm::BitProxy(a_own[i]);
        b_bits[i] = sturm::BitProxy(b_own[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
        r_bits[i] = sturm::BitProxy(r_own[i]);
    }

    // Install an APPEND-mode context (no orkan needed: in APPEND mode
    // execute_gate just records to ctx.ir).
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_mul_mod_dsl<sturm::BitProxy>(a_bits, b_bits, n_bits,
                                            W3, r_bits);

    // Snapshot the high-water mark before tearing down the context so
    // we know how wide to size the classical bit-vector.
    const int high_water = sturm::QubitPool::instance().high_water();

    // Replay the captured gate stream classically.
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < W3; ++i) {
        if ((a_val >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])] = 1u;
        if ((b_val >> i) & 1u) bits[static_cast<std::size_t>(qi_b[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
        // r starts at |0> (per PRD §5 precondition).
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i) {
        apply_gate_classical(bits, ctx->ir.at(i));
    }

    // Verify result.
    const uint32_t expect_r = (a_val * b_val) % n_val;
    const uint32_t a_out = read_reg_classical(bits, qi_a, W3);
    const uint32_t b_out = read_reg_classical(bits, qi_b, W3);
    const uint32_t n_out = read_reg_classical(bits, qi_n, W3);
    const uint32_t r_out = read_reg_classical(bits, qi_r, W3);
    assert(a_out == a_val && "W=3 trace: a register unchanged");
    assert(b_out == b_val && "W=3 trace: b register unchanged");
    assert(n_out == n_val && "W=3 trace: n register unchanged");
    assert(r_out == expect_r && "W=3 trace: r == (a*b) mod n");

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
        sturm::QubitPool::instance().release(qi_b[i]);
    for (std::size_t i = W3; i-- > 0;)
        sturm::QubitPool::instance().release(qi_a[i]);
}

int main() {
    std::printf("sturm-kubb.1 P2.1 mul-mod-dsl: n==0 no-op tests:\n");
    run_n_zero_case(/*a=*/1u, /*b=*/2u, /*n=*/3u, /*r=*/0u);
    std::puts("  PASS: n==0 with r=|0> leaves r at 0");
    run_n_zero_case(/*a=*/1u, /*b=*/2u, /*n=*/3u, /*r=*/3u);
    std::puts("  PASS: n==0 with r=3 leaves r at 3");
    run_n_zero_case(/*a=*/0u, /*b=*/0u, /*n=*/0u, /*r=*/2u);
    std::puts("  PASS: n==0 with all-zero inputs and r=2 leaves r at 2");

    std::printf("sturm-kubb.2 P2.2 mul-mod-dsl: single classical case:\n");
    run_classical_case(/*a=*/2u, /*b=*/2u, /*n=*/3u);
    std::printf("  PASS: (2 * 2) mod 3 == 1\n");

    std::printf("sturm-kubb.3 P2.3 mul-mod-dsl: W=2 exhaustive sweep "
                "(all a, b in [0, n) for n in [1, 4)):\n");
    std::size_t cases_run = 0u;
    for (uint32_t n_val = 1u; n_val < (1u << W); ++n_val) {
        for (uint32_t a_val = 0u; a_val < n_val; ++a_val) {
            for (uint32_t b_val = 0u; b_val < n_val; ++b_val) {
                run_classical_case(a_val, b_val, n_val);
                ++cases_run;
            }
        }
    }
    // n=1 → 1 case; n=2 → 4 cases; n=3 → 9 cases; total = 14 cases.
    assert(cases_run == 14u && "W=2 sweep covered every (a, b, n) "
                                "with a, b < n and n >= 1");
    std::printf("  PASS: %zu W=2 cases covering every (a, b, n) "
                "with a, b < n, n >= 1\n", cases_run);

    // ── Beat 2.4 — W=3 random sweep (50 cases, fixed seed=42) ────────────
    // No orkan simulator: APPEND-mode capture + classical bit-vector
    // replay (see comments above run_classical_case_w3).  Plan §4.3
    // row 2.4 (amended) wording.  Seed matches add-mod beat 1.4
    // (sturm-yh3d.4) for reproducibility across the modular family.
    constexpr uint32_t    kW3Seed  = 42u;
    constexpr std::size_t kW3Cases = 50u;
    std::printf("sturm-kubb.4 P2.4 mul-mod-dsl: W=3 random sweep "
                "(%zu cases, seed=%u, n in [1, 8), classical trace):\n",
                kW3Cases, kW3Seed);
    std::mt19937 rng(kW3Seed);
    std::uniform_int_distribution<uint32_t> n_dist(1u, (1u << W3) - 1u);
    for (std::size_t i = 0; i < kW3Cases; ++i) {
        uint32_t n_val = n_dist(rng);
        std::uniform_int_distribution<uint32_t> ab_dist(0u, n_val - 1u);
        uint32_t a_val = ab_dist(rng);
        uint32_t b_val = ab_dist(rng);
        run_classical_case_w3(a_val, b_val, n_val);
    }
    std::printf("  PASS: %zu W=3 random cases (a * b) mod n matches "
                "classical reference (trace mode)\n", kW3Cases);

    std::printf("All sturm-kubb.{1,2,3,4} tests passed.\n");
    return 0;
}
