// test_bit_proxy_phi_theta.cpp -- sturm-51wc:
// Per-bit phase/rotation proxies on BitProxy (phi() = RZ, theta() = RY).
//
// Semantics under test:
//   * BitProxy::phi() += delta
//       - classical bit, no WHEN  -> SKIP (RZ on classical = unobservable global phase)
//       - classical bit, in WHEN  -> promote + CRZ (relative phase becomes observable)
//       - quantum bit             -> RZ (or CRZ under WHEN)
//   * BitProxy::theta() += delta
//       - any classical bit -> promote + RY/CRY (creates superposition)
//       - quantum bit       -> RY (or CRY under WHEN)
//   * operator-= forwards += with negated delta.
//
// Harness: APPEND-mode BackendContext + GateIR inspection (mirrors
// test_bitproxy_when_promotion.cpp).

#include "sturm/backend/ir.hpp"
#include "sturm/control/when.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qint.hpp"

#include <cassert>
#include <cstddef>
#include <cstdio>

// ── ScopedAppendCtx ─────────────────────────────────────────────────────────

struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedAppendCtx() {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, 32u);
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

static size_t count_kind(const sturm::GateIR& ir, sturm_gate_kind_t kind,
                         size_t from = 0) {
    size_t n = 0;
    for (size_t i = from; i < ir.size(); ++i) {
        if (ir.at(i).kind == kind) ++n;
    }
    return n;
}

// Find the unique record of `kind` at or after `from`, asserting it exists
// and is unique. Returns const ref into the IR.
static const sturm::GateRecord& only_record(const sturm::GateIR& ir,
                                            sturm_gate_kind_t kind,
                                            size_t from = 0) {
    const sturm::GateRecord* found = nullptr;
    for (size_t i = from; i < ir.size(); ++i) {
        if (ir.at(i).kind == kind) {
            assert(found == nullptr && "expected exactly one record of this kind");
            found = &ir.at(i);
        }
    }
    assert(found != nullptr && "expected at least one record of this kind");
    return *found;
}

// ── Test 1: phi() classical, no WHEN -> SKIP ────────────────────────────────

static void test_phi_classical_no_control_skips() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qint_t<4> reg(2);  // 0b0010 — bit 0 = 0, bit 1 = 1
    sturm::BitProxy bp0 = reg[0];
    sturm::BitProxy bp1 = reg[1];

    const size_t before = sc.ir().size();
    bp0.phi() += 0.25;
    bp1.phi() += 0.5;
    const size_t after = sc.ir().size();

    assert(after == before && "phi() on classical bit (no control) must emit no gates");
    assert(reg.qubits[0] == -1 && "bit 0 must stay classical (no qubit allocated)");
    assert(reg.qubits[1] == -1 && "bit 1 must stay classical");
    assert(reg.super_mask == 0 && "super_mask must be unchanged");
    assert(reg.value == 2 && "classical value must be unchanged");

    std::puts("PASS: test_phi_classical_no_control_skips");
}

// ── Test 2: phi() classical inside WHEN -> promote + CRZ ────────────────────

static void test_phi_classical_under_when_promotes_and_emits_crz() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qbool flag(0.5);
    assert(flag.qubits[0] >= 0);
    const int flag_q = flag.qubits[0];

    sturm::qint_t<4> reg(0);  // bit 0 = 0
    sturm::BitProxy bp = reg[0];

    const size_t before = sc.ir().size();
    WHEN(flag) {
        bp.phi() += 0.25;
    }

    assert(reg.qubits[0] >= 0 && "bit must be promoted to quantum under WHEN");
    assert((reg.super_mask & 1ULL) != 0 && "super_mask bit 0 must be set");

    const auto& crz = only_record(sc.ir(), STURM_GATE_CRZ, before);
    assert(crz.n == 2);
    assert(crz.qubits[0] == static_cast<uint32_t>(flag_q));
    assert(crz.qubits[1] == static_cast<uint32_t>(reg.qubits[0]));
    assert(crz.param == 0.25);

    // Bit was classical 0 — no X for init expected.
    assert(count_kind(sc.ir(), STURM_GATE_X, before) == 0 &&
           "bit 0 was classical 0, no init-X should be emitted");

    std::puts("PASS: test_phi_classical_under_when_promotes_and_emits_crz");
}

// ── Test 3: phi() on already-quantum bit -> RZ ──────────────────────────────

static void test_phi_quantum_emits_rz() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qint_t<4> reg(0);
    reg.qubits[0]  = sturm::QubitPool::instance().allocate();
    reg.super_mask = 1ULL;
    const int q = reg.qubits[0];

    sturm::BitProxy bp = reg[0];
    assert(bp.is_quantum());

    const size_t before = sc.ir().size();
    bp.phi() += 0.5;

    assert(sc.ir().size() == before + 1 && "exactly one RZ expected");
    const auto& r = sc.ir().at(before);
    assert(r.kind == STURM_GATE_RZ);
    assert(r.n == 1);
    assert(r.qubits[0] == static_cast<uint32_t>(q));
    assert(r.param == 0.5);

    std::puts("PASS: test_phi_quantum_emits_rz");
}

// ── Test 4: phi() -= delta -> RZ(-delta) ────────────────────────────────────

static void test_phi_minus_emits_negated() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qint_t<4> reg(0);
    reg.qubits[0]  = sturm::QubitPool::instance().allocate();
    reg.super_mask = 1ULL;
    sturm::BitProxy bp = reg[0];

    const size_t before = sc.ir().size();
    bp.phi() -= 0.3;

    assert(sc.ir().size() == before + 1);
    const auto& r = sc.ir().at(before);
    assert(r.kind == STURM_GATE_RZ);
    assert(r.param == -0.3);

    std::puts("PASS: test_phi_minus_emits_negated");
}

// ── Test 5: theta() on classical bit promotes + RY ──────────────────────────
// bit_value=1 case: we expect an X (for init) before the RY.

static void test_theta_classical_promotes_and_emits_ry() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qint_t<4> reg(2);  // bit 0 = 0, bit 1 = 1
    sturm::BitProxy bp0 = reg[0];
    sturm::BitProxy bp1 = reg[1];

    // bit 0 = 0 -> promote + RY only (no X init).
    {
        const size_t before = sc.ir().size();
        bp0.theta() += 0.4;

        assert(reg.qubits[0] >= 0 && "bit 0 must be promoted");
        assert((reg.super_mask & 1ULL) != 0);
        assert(count_kind(sc.ir(), STURM_GATE_X, before) == 0 &&
               "bit value 0 -> no init-X");
        const auto& r = only_record(sc.ir(), STURM_GATE_RY, before);
        assert(r.n == 1);
        assert(r.qubits[0] == static_cast<uint32_t>(reg.qubits[0]));
        assert(r.param == 0.4);
    }

    // bit 1 = 1 -> X init then RY.
    {
        const size_t before = sc.ir().size();
        bp1.theta() += 0.6;

        assert(reg.qubits[1] >= 0 && "bit 1 must be promoted");
        assert((reg.super_mask & (1ULL << 1)) != 0);

        const auto& x  = only_record(sc.ir(), STURM_GATE_X,  before);
        const auto& ry = only_record(sc.ir(), STURM_GATE_RY, before);
        assert(x.qubits[0]  == static_cast<uint32_t>(reg.qubits[1]));
        assert(ry.qubits[0] == static_cast<uint32_t>(reg.qubits[1]));
        assert(ry.param == 0.6);
    }

    std::puts("PASS: test_theta_classical_promotes_and_emits_ry");
}

// ── Test 6: theta() classical inside WHEN -> CRY ────────────────────────────

static void test_theta_classical_under_when_emits_cry() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qbool flag(0.5);
    const int flag_q = flag.qubits[0];

    sturm::qint_t<4> reg(0);
    sturm::BitProxy bp = reg[0];

    const size_t before = sc.ir().size();
    WHEN(flag) {
        bp.theta() += 0.4;
    }

    assert(reg.qubits[0] >= 0 && "bit must be promoted under WHEN");
    const auto& cry = only_record(sc.ir(), STURM_GATE_CRY, before);
    assert(cry.n == 2);
    assert(cry.qubits[0] == static_cast<uint32_t>(flag_q));
    assert(cry.qubits[1] == static_cast<uint32_t>(reg.qubits[0]));
    assert(cry.param == 0.4);

    std::puts("PASS: test_theta_classical_under_when_emits_cry");
}

// ── Test 7: theta() on already-quantum bit -> RY ────────────────────────────

static void test_theta_quantum_emits_ry() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qint_t<4> reg(0);
    reg.qubits[0]  = sturm::QubitPool::instance().allocate();
    reg.super_mask = 1ULL;
    const int q = reg.qubits[0];

    sturm::BitProxy bp = reg[0];

    const size_t before = sc.ir().size();
    bp.theta() += 0.6;

    // Already quantum -> no init-X. Exactly one RY.
    assert(count_kind(sc.ir(), STURM_GATE_X, before) == 0);
    assert(sc.ir().size() == before + 1);
    const auto& r = sc.ir().at(before);
    assert(r.kind == STURM_GATE_RY);
    assert(r.qubits[0] == static_cast<uint32_t>(q));
    assert(r.param == 0.6);

    std::puts("PASS: test_theta_quantum_emits_ry");
}

// ── Test 8: theta() -= delta -> RY(-delta) ──────────────────────────────────

static void test_theta_minus_emits_negated() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qint_t<4> reg(0);
    reg.qubits[0]  = sturm::QubitPool::instance().allocate();
    reg.super_mask = 1ULL;
    sturm::BitProxy bp = reg[0];

    const size_t before = sc.ir().size();
    bp.theta() -= 0.2;

    assert(sc.ir().size() == before + 1);
    const auto& r = sc.ir().at(before);
    assert(r.kind == STURM_GATE_RY);
    assert(r.param == -0.2);

    std::puts("PASS: test_theta_minus_emits_negated");
}

// ── Test 9: per-bit ops via the user-facing operator[] ──────────────────────
// `reg[k].phi() += d;` and `reg[k].theta() += d;` — the natural-syntax shape
// from the qram_demo failure that drove this issue.

static void test_per_bit_via_subscript() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    sturm::qint_t<4> reg(0);  // all classical 0

    // theta() on a classical bit promotes that bit (and only that bit).
    const size_t before_t = sc.ir().size();
    reg[1].theta() += 0.4;
    assert(reg.qubits[0] == -1 && "bit 0 must remain classical");
    assert(reg.qubits[1] >= 0  && "bit 1 must be promoted by theta");
    assert(reg.qubits[2] == -1);
    assert(reg.qubits[3] == -1);
    const auto& ry = only_record(sc.ir(), STURM_GATE_RY, before_t);
    assert(ry.qubits[0] == static_cast<uint32_t>(reg.qubits[1]));

    // phi() on a classical (still-classical) bit with no control -> SKIP.
    const size_t before_p = sc.ir().size();
    reg[2].phi() += 0.7;
    assert(sc.ir().size() == before_p && "phi() on classical bit, no WHEN, must skip");
    assert(reg.qubits[2] == -1 && "bit 2 must remain classical");

    // phi() on the already-quantum bit 1 -> emits RZ.
    const size_t before_q = sc.ir().size();
    reg[1].phi() += 0.9;
    assert(sc.ir().size() == before_q + 1);
    const auto& rz = sc.ir().at(before_q);
    assert(rz.kind == STURM_GATE_RZ);
    assert(rz.qubits[0] == static_cast<uint32_t>(reg.qubits[1]));
    assert(rz.param == 0.9);

    std::puts("PASS: test_per_bit_via_subscript");
}

// ── main ────────────────────────────────────────────────────────────────────

int main() {
    test_phi_classical_no_control_skips();
    test_phi_classical_under_when_promotes_and_emits_crz();
    test_phi_quantum_emits_rz();
    test_phi_minus_emits_negated();
    test_theta_classical_promotes_and_emits_ry();
    test_theta_classical_under_when_emits_cry();
    test_theta_quantum_emits_ry();
    test_theta_minus_emits_negated();
    test_per_bit_via_subscript();

    std::puts("\nAll BitProxy phi()/theta() tests passed.");
    return 0;
}
