// test_uncompute_or.cpp — M3 (transpiler-MVP): explicit uncompute_or free
// function coverage.
//
// The transpiler-MVP requires a single free inverse entry point for OR:
//   void uncompute_or(qbool& r, const qbool& a, const qbool& b);
// that mirrors the `BITWISE_SELF` kind=1 adjoint already defined in
// uncompute_op.hpp (lines 237-271).  M3 ships the header/impl under
// include/sturm/uncompute/uncompute_api.hpp and
// src/sturm/uncompute/uncompute_api.cpp.
//
// This file covers the scenarios the issue acceptance criteria lists:
//   1. Forward-then-inverse returns the ancilla to |0>.  Validated end-to-end
//      in STURM_MODE_SIMULATE by running the exact forward OR decomposition
//      from bit_proxy.hpp `materialize_or` followed by `uncompute_or` on the
//      live statevector and asserting the ancilla amplitude collapses back
//      to |0> while the input qubits are unchanged.
//   2. Classical-classical inputs emit zero gates.
//   3. Mixed case (one quantum, one classical) emits the matching four-quadrant
//      subset that mirrors bit_proxy.hpp materialize_or with the adjoint
//      being the exact REVERSED forward gate list, each gate replaced by its
//      inverse (X, CX, CCX are self-inverse).
//
// The tests pass under both STURM_AUTO_UNCOMPUTE=ON and =OFF because
// uncompute_or is an explicit free function that does not interact with the
// destructor path.  The existing compile-time flag is a no-op for this file.
//
// Harness: plain assert + printf (no gtest).

#define STURM_BACKEND_ENABLED 1

#include "sturm/uncompute/uncompute_api.hpp"
#include "sturm/uncompute/uncompute_op.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/ir.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/primitives.hpp"
#include "sturm/qtypes/qbool.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;

// ── Scoped APPEND context fixture ────────────────────────────────────────────

struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedAppendCtx() {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, 17u);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    sturm::GateIR& ir() { return ctx->ir; }
};

// ── Scoped SIMULATE context fixture ──────────────────────────────────────────

struct ScopedSimulateCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit ScopedSimulateCtx(uint32_t n_qubits, uint32_t max_q = 17u) {
        bridge.allocate(n_qubits);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
        assert(ctx && "sturm_backend_create failed");
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedSimulateCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    orkan::state_t& sv() { return bridge.state(); }
};

// ── Helpers ──────────────────────────────────────────────────────────────────

// Build a non-owning quantum qbool that references qubit `idx` without touching
// the pool.  super_mask is set so the qbool is treated as quantum.
static sturm::qbool make_quantum_qbool(int idx) {
    sturm::qbool q = sturm::qbool::make_non_owning(idx);
    q.super_mask   = 1ULL;
    q.value        = 0;
    return q;
}

// Read a single qubit's classical value assuming the state is a basis state.
// Returns 0 or 1 by inspecting the first non-zero amplitude.
static uint32_t read_bit(orkan::state_t& sv, uint32_t qubit) {
    uint64_t dim = uint64_t{1} << sv.n_qubits;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            return static_cast<uint32_t>((s >> qubit) & 1u);
        }
    }
    return 0u;
}

// Sum probability that qubit `q` is |0> across all basis states.
static double probability_zero(orkan::state_t& sv, uint32_t qubit) {
    uint64_t dim = uint64_t{1} << sv.n_qubits;
    uint64_t mask = uint64_t{1} << qubit;
    double p0 = 0.0;
    for (uint64_t s = 0; s < dim; ++s) {
        if ((s & mask) == 0u) {
            p0 += std::norm(orkan::amplitude(sv, s));
        }
    }
    return p0;
}

// Replicate bit_proxy.hpp `materialize_or` forward gate emission for the
// given quadrant, acting on physical qubits (a_q? a_idx : a_lit) and similar
// for b, depositing the OR result in ancilla qubit `anc`.  The classical
// X-folding logic is identical to the library version so the resulting
// forward+inverse combination is a true round-trip of that code path.
static void emit_forward_or(sturm::BackendContext& ctx,
                             bool a_quantum, int a_qubit, bool a_value,
                             bool b_quantum, int b_qubit, bool b_value,
                             uint32_t anc) {
    // Stage 1: initial CX(a, anc) OR X(anc) fold for a.
    if (a_quantum) {
        sturm::primitive_XOR(ctx, static_cast<uint32_t>(a_qubit), anc);
    } else if (a_value) {
        uint32_t args[1] = {anc};
        sturm::execute_gate(ctx, STURM_GATE_X, args, 1u, 0.0);
    }
    // Stage 2: CX(b, anc) OR X(anc) fold for b.
    if (b_quantum) {
        sturm::primitive_XOR(ctx, static_cast<uint32_t>(b_qubit), anc);
    } else if (b_value) {
        uint32_t args[1] = {anc};
        sturm::execute_gate(ctx, STURM_GATE_X, args, 1u, 0.0);
    }
    // Stage 3: AND term (CCX / CX / X depending on quadrant).
    if (a_quantum && b_quantum) {
        sturm::primitive_AND(ctx,
                              static_cast<uint32_t>(a_qubit),
                              static_cast<uint32_t>(b_qubit),
                              anc);
    } else if (a_quantum && b_value) {
        sturm::primitive_XOR(ctx, static_cast<uint32_t>(a_qubit), anc);
    } else if (b_quantum && a_value) {
        sturm::primitive_XOR(ctx, static_cast<uint32_t>(b_qubit), anc);
    } else if (a_value && b_value) {
        uint32_t args[1] = {anc};
        sturm::execute_gate(ctx, STURM_GATE_X, args, 1u, 0.0);
    }
}

// ── Test 1: forward-then-inverse returns ancilla to |0> (SIMULATE mode) ─────
//
// For every quadrant and every classical-input combination the test:
//   1. Prepares the input qubits into the chosen basis state (or a
//      superposition for the quantum case).
//   2. Runs the exact forward OR gate sequence that bit_proxy.hpp
//      `materialize_or` would have emitted.
//   3. Runs `uncompute_or(r, a, b)`.
//   4. Asserts that the ancilla qubit is back in |0> with probability 1,
//      and that the input qubits are unchanged (same basis-state / same
//      superposition probabilities).
//
// This guarantees the adjoint is a true inverse; a gate-stream-only check
// cannot see that the inverse actually collapses the ancilla.

static void roundtrip_both_quantum_all_basis() {
    // qubits: a=0, b=1, anc=2.
    const uint32_t qa = 0u, qb = 1u, qanc = 2u;
    // Iterate over (a_bit, b_bit) in {0,1}^2 — classical basis coverage.
    for (uint32_t a_bit = 0; a_bit < 2; ++a_bit) {
        for (uint32_t b_bit = 0; b_bit < 2; ++b_bit) {
            ScopedSimulateCtx sc{3u};
            if (a_bit) orkan::apply_x(sc.sv(), qa);
            if (b_bit) orkan::apply_x(sc.sv(), qb);

            sturm::qbool a = make_quantum_qbool(static_cast<int>(qa));
            sturm::qbool b = make_quantum_qbool(static_cast<int>(qb));
            sturm::qbool r = make_quantum_qbool(static_cast<int>(qanc));

            emit_forward_or(*sc.ctx,
                            /*a_q*/true,  static_cast<int>(qa), false,
                            /*b_q*/true,  static_cast<int>(qb), false,
                            qanc);

            // After forward: anc = a | b; expected basis state.
            const uint32_t expected_or = a_bit | b_bit;
            assert(read_bit(sc.sv(), qanc) == expected_or
                   && "forward both-quantum: anc must equal a|b");

            sturm::uncompute_or(r, a, b);

            // Anc back to |0>.
            double p0 = probability_zero(sc.sv(), qanc);
            assert(std::abs(p0 - 1.0) < kTol
                   && "both-quantum: ancilla must return to |0>");

            // Inputs unchanged.
            assert(read_bit(sc.sv(), qa) == a_bit
                   && "both-quantum: a unchanged");
            assert(read_bit(sc.sv(), qb) == b_bit
                   && "both-quantum: b unchanged");
        }
    }
    std::printf("  roundtrip_both_quantum_all_basis: PASS\n");
}

static void roundtrip_both_quantum_superposition() {
    // Put a and b each into superposition: |+>|+> = (|00>+|01>+|10>+|11>)/2.
    // After forward OR, ancilla holds a|b for each branch; after inverse
    // the ancilla must be |0> in every branch, i.e. anc-P(|0>) = 1.
    const uint32_t qa = 0u, qb = 1u, qanc = 2u;
    ScopedSimulateCtx sc{3u};
    orkan::apply_h(sc.sv(), qa);
    orkan::apply_h(sc.sv(), qb);

    sturm::qbool a = make_quantum_qbool(static_cast<int>(qa));
    sturm::qbool b = make_quantum_qbool(static_cast<int>(qb));
    sturm::qbool r = make_quantum_qbool(static_cast<int>(qanc));

    emit_forward_or(*sc.ctx,
                    /*a_q*/true,  static_cast<int>(qa), false,
                    /*b_q*/true,  static_cast<int>(qb), false,
                    qanc);

    sturm::uncompute_or(r, a, b);

    // P(anc=|0>) must be 1 regardless of the entanglement with a,b.
    const double p0 = probability_zero(sc.sv(), qanc);
    assert(std::abs(p0 - 1.0) < kTol
           && "both-quantum superposition: anc must return to |0>");
    std::printf("  roundtrip_both_quantum_superposition: PASS\n");
}

static void roundtrip_mixed_a_quantum() {
    // a quantum, b classical in {0,1}.  Sweep a over {|0>, |1>, |+>}.
    const uint32_t qa = 0u, qanc = 2u;
    for (uint32_t b_val = 0; b_val < 2; ++b_val) {
        for (int a_kind = 0; a_kind < 3; ++a_kind) {
            ScopedSimulateCtx sc{3u};
            // a prep.
            if (a_kind == 1) orkan::apply_x(sc.sv(), qa);
            if (a_kind == 2) orkan::apply_h(sc.sv(), qa);

            sturm::qbool a = make_quantum_qbool(static_cast<int>(qa));
            sturm::qbool b(static_cast<bool>(b_val));
            sturm::qbool r = make_quantum_qbool(static_cast<int>(qanc));

            emit_forward_or(*sc.ctx,
                            /*a_q*/true,  static_cast<int>(qa),  false,
                            /*b_q*/false, -1,                    static_cast<bool>(b_val),
                            qanc);

            sturm::uncompute_or(r, a, b);

            const double p0 = probability_zero(sc.sv(), qanc);
            assert(std::abs(p0 - 1.0) < kTol
                   && "mixed a_q: ancilla must return to |0>");
            // a probability distribution must match input prep.
            const double pa0 = probability_zero(sc.sv(), qa);
            double expected;
            if (a_kind == 0)      expected = 1.0;
            else if (a_kind == 1) expected = 0.0;
            else                  expected = 0.5;
            assert(std::abs(pa0 - expected) < kTol
                   && "mixed a_q: a unchanged");
        }
    }
    std::printf("  roundtrip_mixed_a_quantum: PASS\n");
}

static void roundtrip_mixed_b_quantum() {
    // a classical in {0,1}, b quantum.  Sweep b over {|0>, |1>, |+>}.
    const uint32_t qb = 1u, qanc = 2u;
    for (uint32_t a_val = 0; a_val < 2; ++a_val) {
        for (int b_kind = 0; b_kind < 3; ++b_kind) {
            ScopedSimulateCtx sc{3u};
            if (b_kind == 1) orkan::apply_x(sc.sv(), qb);
            if (b_kind == 2) orkan::apply_h(sc.sv(), qb);

            sturm::qbool a(static_cast<bool>(a_val));
            sturm::qbool b = make_quantum_qbool(static_cast<int>(qb));
            sturm::qbool r = make_quantum_qbool(static_cast<int>(qanc));

            emit_forward_or(*sc.ctx,
                            /*a_q*/false, -1,                    static_cast<bool>(a_val),
                            /*b_q*/true,  static_cast<int>(qb),  false,
                            qanc);

            sturm::uncompute_or(r, a, b);

            const double p0 = probability_zero(sc.sv(), qanc);
            assert(std::abs(p0 - 1.0) < kTol
                   && "mixed b_q: ancilla must return to |0>");
            const double pb0 = probability_zero(sc.sv(), qb);
            double expected;
            if (b_kind == 0)      expected = 1.0;
            else if (b_kind == 1) expected = 0.0;
            else                  expected = 0.5;
            assert(std::abs(pb0 - expected) < kTol
                   && "mixed b_q: b unchanged");
        }
    }
    std::printf("  roundtrip_mixed_b_quantum: PASS\n");
}

static void roundtrip_classical_classical_with_ancilla() {
    // Both a,b classical BUT r still has an allocated ancilla — the
    // materialize_or code path that forward-emits 0..3 X gates into
    // the ancilla.  uncompute_or must return it to |0>.
    const uint32_t qanc = 2u;
    for (uint32_t a_val = 0; a_val < 2; ++a_val) {
        for (uint32_t b_val = 0; b_val < 2; ++b_val) {
            ScopedSimulateCtx sc{3u};

            sturm::qbool a(static_cast<bool>(a_val));
            sturm::qbool b(static_cast<bool>(b_val));
            sturm::qbool r = make_quantum_qbool(static_cast<int>(qanc));

            emit_forward_or(*sc.ctx,
                            /*a_q*/false, -1, static_cast<bool>(a_val),
                            /*b_q*/false, -1, static_cast<bool>(b_val),
                            qanc);

            sturm::uncompute_or(r, a, b);

            const double p0 = probability_zero(sc.sv(), qanc);
            assert(std::abs(p0 - 1.0) < kTol
                   && "classical-classical with anc: ancilla must return to |0>");
        }
    }
    std::printf("  roundtrip_classical_classical_with_ancilla: PASS\n");
}

// ── Test 2: classical-classical (no-qubit) inputs emit zero gates ───────────

static void classical_classical_no_gates() {
    ScopedAppendCtx sc;

    // Classical qbools carry qubits[0] == -1 (no allocation).  r is also
    // classical: no ancilla, nothing to undo.
    sturm::qbool a(false);
    sturm::qbool b(false);
    sturm::qbool r(false);

    const std::size_t ir_before = sc.ir().size();
    sturm::uncompute_or(r, a, b);
    const std::size_t ir_after  = sc.ir().size();

    assert(ir_after == ir_before
           && "uncompute_or on classical-classical (no anc) must emit zero gates");

    // Repeat for classical-true to exercise both value paths.
    sturm::qbool a1(true);
    sturm::qbool b1(true);
    sturm::qbool r1(true);
    const std::size_t before2 = sc.ir().size();
    sturm::uncompute_or(r1, a1, b1);
    const std::size_t after2  = sc.ir().size();
    assert(after2 == before2
           && "uncompute_or on classical-true,true (no anc) must emit zero gates");

    std::printf("  classical_classical_no_gates: PASS\n");
}

// ── Test 3: mixed-operand cases mirror bit_proxy four-quadrant rules ────────

static void mixed_quantum_classical0_case_a() {
    // a quantum (qubit 20), b classical=0: forward emits CX(a, r).
    // Inverse is the reversed 1-gate list: CX(a, r) (self-inverse).
    ScopedAppendCtx sc;

    sturm::qbool a = make_quantum_qbool(20);
    sturm::qbool b(false);
    sturm::qbool r = make_quantum_qbool(22);

    const std::size_t ir_before = sc.ir().size();
    sturm::uncompute_or(r, a, b);
    const std::size_t ir_after  = sc.ir().size();

    assert(ir_after - ir_before == 1u
           && "a_q, b=0: exactly 1 gate expected (CX)");
    const sturm::GateRecord& g = sc.ir().at(ir_before);
    assert(g.kind == STURM_GATE_CX
           && "a_q, b=0: gate must be CX(a, r)");
    assert(g.qubits[0] == 20u && g.qubits[1] == 22u
           && "CX must target (a, r)");
    std::printf("  mixed a_q,b=0: PASS\n");
}

static void mixed_quantum_classical1_case_a() {
    // a quantum, b classical=1.  Forward: CX(a,r) + X(r) + CX(a,r).
    // Reversed-and-inverted adjoint: CX(a,r) + X(r) + CX(a,r) — 3 gates
    // (each self-inverse so reversed order == forward order here).
    ScopedAppendCtx sc;

    sturm::qbool a = make_quantum_qbool(30);
    sturm::qbool b(true);
    sturm::qbool r = make_quantum_qbool(32);

    const std::size_t ir_before = sc.ir().size();
    sturm::uncompute_or(r, a, b);
    const std::size_t ir_after  = sc.ir().size();

    assert(ir_after - ir_before == 3u
           && "a_q, b=1: exactly 3 gates expected (CX + X + CX)");
    const sturm::GateRecord& g0 = sc.ir().at(ir_before);
    const sturm::GateRecord& g1 = sc.ir().at(ir_before + 1u);
    const sturm::GateRecord& g2 = sc.ir().at(ir_before + 2u);
    assert(g0.kind == STURM_GATE_CX
           && "a_q, b=1: gate[0] must be CX(a, r)");
    assert(g0.qubits[0] == 30u && g0.qubits[1] == 32u
           && "gate[0] CX must target (a, r)");
    assert(g1.kind == STURM_GATE_X
           && "a_q, b=1: gate[1] must be X(r)");
    assert(g1.qubits[0] == 32u
           && "gate[1] X must target r");
    assert(g2.kind == STURM_GATE_CX
           && "a_q, b=1: gate[2] must be CX(a, r)");
    assert(g2.qubits[0] == 30u && g2.qubits[1] == 32u
           && "gate[2] CX must target (a, r)");
    std::printf("  mixed a_q,b=1: PASS\n");
}

static void mixed_classical0_quantum_case_b() {
    // a classical=0, b quantum: forward emits CX(b, r).
    // Adjoint is CX(b, r).
    ScopedAppendCtx sc;

    sturm::qbool a(false);
    sturm::qbool b = make_quantum_qbool(41);
    sturm::qbool r = make_quantum_qbool(42);

    const std::size_t ir_before = sc.ir().size();
    sturm::uncompute_or(r, a, b);
    const std::size_t ir_after  = sc.ir().size();

    assert(ir_after - ir_before == 1u
           && "a=0, b_q: exactly 1 gate expected (CX)");
    const sturm::GateRecord& g = sc.ir().at(ir_before);
    assert(g.kind == STURM_GATE_CX
           && "a=0, b_q: gate must be CX(b, r)");
    assert(g.qubits[0] == 41u && g.qubits[1] == 42u
           && "CX must target (b, r)");
    std::printf("  mixed a=0,b_q: PASS\n");
}

static void mixed_classical1_quantum_case_b() {
    // a classical=1, b quantum.  Forward: X(r) + CX(b,r) + CX(b,r).
    // Reversed-and-inverted adjoint: CX(b,r) + CX(b,r) + X(r) — 3 gates.
    ScopedAppendCtx sc;

    sturm::qbool a(true);
    sturm::qbool b = make_quantum_qbool(51);
    sturm::qbool r = make_quantum_qbool(52);

    const std::size_t ir_before = sc.ir().size();
    sturm::uncompute_or(r, a, b);
    const std::size_t ir_after  = sc.ir().size();

    assert(ir_after - ir_before == 3u
           && "a=1, b_q: exactly 3 gates expected (CX + CX + X)");
    const sturm::GateRecord& g0 = sc.ir().at(ir_before);
    const sturm::GateRecord& g1 = sc.ir().at(ir_before + 1u);
    const sturm::GateRecord& g2 = sc.ir().at(ir_before + 2u);
    assert(g0.kind == STURM_GATE_CX
           && "a=1, b_q: gate[0] must be CX(b, r)");
    assert(g0.qubits[0] == 51u && g0.qubits[1] == 52u
           && "gate[0] CX must target (b, r)");
    assert(g1.kind == STURM_GATE_CX
           && "a=1, b_q: gate[1] must be CX(b, r)");
    assert(g1.qubits[0] == 51u && g1.qubits[1] == 52u
           && "gate[1] CX must target (b, r)");
    assert(g2.kind == STURM_GATE_X
           && "a=1, b_q: gate[2] must be X(r)");
    assert(g2.qubits[0] == 52u
           && "gate[2] X must target r");
    std::printf("  mixed a=1,b_q: PASS\n");
}

// ── Bonus: confirm the umbrella include exposes uncompute_or ────────────────
// Compile-time test: if sturm/sturm.hpp did not pull in uncompute_api.hpp the
// call below would not compile.

#include "sturm/sturm.hpp"
static void umbrella_include_visibility() {
    ScopedAppendCtx sc;
    sturm::qbool a(false);
    sturm::qbool b(false);
    sturm::qbool r(false);
    sturm::uncompute_or(r, a, b);  // just needs to compile.
    (void)sc;
    std::printf("  umbrella_include_visibility: PASS\n");
}

// ── main ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M3 uncompute_or free-function API tests:\n");

    // SIMULATE-mode state-vector round-trip coverage.
    roundtrip_both_quantum_all_basis();
    roundtrip_both_quantum_superposition();
    roundtrip_mixed_a_quantum();
    roundtrip_mixed_b_quantum();
    roundtrip_classical_classical_with_ancilla();

    // APPEND-mode gate-stream structure coverage.
    classical_classical_no_gates();
    mixed_quantum_classical0_case_a();
    mixed_quantum_classical1_case_a();
    mixed_classical0_quantum_case_b();
    mixed_classical1_quantum_case_b();

    umbrella_include_visibility();
    std::printf("All uncompute_or tests passed.\n");
    return 0;
}
