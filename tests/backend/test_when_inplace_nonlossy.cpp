// test_when_inplace_nonlossy.cpp — sturm-h5it.1
//
// Regression baseline for the non-lossy (genuinely in-place) qint compound
// assignments under WHEN: operator^=, operator+=, operator-=.
//
// Siblings sturm-h5it.{2,3,4} cover the *lossy* compound assigns (&=, |=,
// *=, /=, %=), which allocate a fresh result register and need the
// CSWAP-and-leak tail to honour the ctrl=|0> branch. The ops covered here
// are DIFFERENT: they operate in place on this->qubits[i] via BitProxy (^=)
// or the Cuccaro in-place adder/subtractor (+=, -=), so:
//
//   - this->qubits[i] indices are preserved across the op (no relabel).
//   - No "result register" leak is produced.
//   - QubitPool usage returns to baseline after the op releases its
//     internal carry/borrow ancilla (for +=/-=); ^= allocates no ancilla.
//   - The BitProxy CX emitted per bit is controlled by WhenGuard, so
//     ctrl=|0> correctly suppresses the effect at simulation time.
//
// This child does NOT modify any operator — it locks in the current
// behaviour so subsequent epic children can refactor the lossy ops without
// regressing the non-lossy ones.
//
// Matrix per issue sturm-h5it.1:
//   (a) ctrl=|0>         — op must leave a unchanged.
//   (b) ctrl=|1>         — op must produce the classical result.
//   (c) ctrl=|+>         — superposition control, entangled output.
//   (d) nested WHEN(x){WHEN(y){ a op= b }} over all 4 classical (x,y).
//
// Asserts for each:
//   - this->qubits[i] indices preserved across the op (genuine in-place).
//   - QubitPool::in_use() returns to baseline after op + temp release.
//   - Simulated statevector matches the classical expected result.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>

static constexpr double kTol = 1e-9;

// ── SimCtx (SIMULATE) ────────────────────────────────────────────────────────
struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 128u) {
        bridge.allocate(n_qubits);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
        assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    orkan::state_t& sv() { return bridge.state(); }
};

// Read the register value whose bit-i is at qubit index reg[i] from the
// first non-zero-amplitude basis state. Assumes a pure basis state.
template <class Arr>
static uint32_t read_reg(orkan::state_t& sv, const Arr& reg, uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            uint32_t v = 0u;
            for (std::size_t i = 0; i < reg.size(); ++i) {
                if (reg[i] >= 0) {
                    v |= (static_cast<uint32_t>((s >> reg[i]) & 1u) << i);
                }
            }
            return v;
        }
    }
    return 0u;
}

// ── Helpers ──────────────────────────────────────────────────────────────────

// Encode an integer value into a freshly initialised register by applying X
// gates on the statevector directly.
template <std::size_t W>
static void init_reg_value(orkan::state_t& sv,
                           const std::array<int, W>& qubits,
                           uint32_t value) {
    for (std::size_t i = 0; i < W; ++i) {
        if ((value >> i) & 1u) {
            orkan::apply_x(sv, static_cast<uint32_t>(qubits[i]));
        }
    }
}

// Allocate the a/b input registers and an optional list of control qubits,
// returning the raw indices. Also populates the qint_t<W> views (non-owning).
template <std::size_t W>
struct Layout {
    std::array<int, W> a_qubits{};
    std::array<int, W> b_qubits{};
    std::array<int, 2> ctrls{{-1, -1}};  // up to 2 controls
    int pool_baseline_after_alloc = 0;
};

template <std::size_t W>
static Layout<W> layout_alloc(std::size_t n_ctrls) {
    Layout<W> L;
    for (std::size_t i = 0; i < W; ++i)
        L.a_qubits[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        L.b_qubits[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < n_ctrls; ++i)
        L.ctrls[i] = sturm::QubitPool::instance().allocate();
    L.pool_baseline_after_alloc = sturm::QubitPool::instance().in_use();
    return L;
}

template <std::size_t W>
static void layout_release(Layout<W>& L, std::size_t n_ctrls) {
    for (std::size_t i = 0; i < n_ctrls; ++i)
        sturm::QubitPool::instance().release(L.ctrls[i]);
    for (std::size_t i = 0; i < W; ++i)
        sturm::QubitPool::instance().release(L.b_qubits[i]);
    for (std::size_t i = 0; i < W; ++i)
        sturm::QubitPool::instance().release(L.a_qubits[i]);
}

// Shallow-detach a register view so its destructor does not try to release
// qubits we own externally via the Layout.
template <std::size_t W>
static void detach(sturm::qint_t<W>& r) {
    for (int& q : r.qubits) q = -1;
    r.owning_ = false;
}
static void detach(sturm::qbool& q) {
    q.qubits[0] = -1;
    q.owning_   = false;
}

// Snapshot the qint_t<W> qubit indices into a plain array for comparison.
template <std::size_t W>
static std::array<int, W> snapshot_qubits(const sturm::qint_t<W>& r) {
    std::array<int, W> out{};
    for (std::size_t i = 0; i < W; ++i) out[i] = r.qubits[i];
    return out;
}

// ── (a), (b): ctrl in a computational basis state ────────────────────────────
//
// Parameterised by op_tag to select ^=, +=, or -=.

enum class OpTag { Xor, Add, Sub };

static const char* op_name(OpTag t) {
    switch (t) {
        case OpTag::Xor: return "^=";
        case OpTag::Add: return "+=";
        case OpTag::Sub: return "-=";
    }
    return "?";
}

template <std::size_t W>
static uint32_t classical_apply(OpTag t, uint32_t a, uint32_t b) {
    const uint32_t mask = (W >= 32) ? ~0u : ((1u << W) - 1u);
    switch (t) {
        case OpTag::Xor: return (a ^ b) & mask;
        case OpTag::Add: return (a + b) & mask;
        case OpTag::Sub: return (a - b) & mask;
    }
    return 0u;
}

template <std::size_t W>
static void apply_op_under_when(OpTag t,
                                sturm::qint_t<W>& a,
                                sturm::qint_t<W>& b,
                                sturm::qbool& ctrl) {
    switch (t) {
        case OpTag::Xor: WHEN(ctrl) { a ^= b; } break;
        case OpTag::Add: WHEN(ctrl) { a += b; } break;
        case OpTag::Sub: WHEN(ctrl) { a -= b; } break;
    }
}

template <std::size_t W>
static void test_ctrl_basis(OpTag t, bool ctrl_bit,
                            uint32_t a_val, uint32_t b_val) {
    sturm::QubitPool::instance().reset_for_testing();

    auto L = layout_alloc<W>(/*n_ctrls=*/1);
    SimCtx sc{17u, 128u};

    init_reg_value<W>(sc.sv(), L.a_qubits, a_val);
    init_reg_value<W>(sc.sv(), L.b_qubits, b_val);
    if (ctrl_bit) orkan::apply_x(sc.sv(), static_cast<uint32_t>(L.ctrls[0]));

    sturm::qint_t<W> a, b;
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = L.a_qubits[i];
        b.qubits[i] = L.b_qubits[i];
    }
    a.super_mask = (1ULL << W) - 1ULL;
    b.super_mask = (1ULL << W) - 1ULL;
    a.value = static_cast<int64_t>(a_val);
    b.value = static_cast<int64_t>(b_val);
    a.owning_ = false;
    b.owning_ = false;

    sturm::qbool ctrl = sturm::qbool::make_non_owning(L.ctrls[0]);
    ctrl.super_mask   = 1ULL;
    ctrl.value        = 0;

    const auto qubits_before = snapshot_qubits(a);
    const int pool_before    = sturm::QubitPool::instance().in_use();

    apply_op_under_when<W>(t, a, b, ctrl);

    // In-place invariant: qubit indices unchanged across op.
    const auto qubits_after = snapshot_qubits(a);
    for (std::size_t i = 0; i < W; ++i) {
        assert(qubits_after[i] == qubits_before[i] &&
               "in-place op must preserve this->qubits[i] indices");
    }

    // Pool must return to baseline (all temps released inside the op).
    const int pool_after = sturm::QubitPool::instance().in_use();
    assert(pool_after == pool_before &&
           "non-lossy op must not leak qubits beyond pre-op baseline");

    // Simulated statevector matches classical expected result.
    const uint32_t expected =
        ctrl_bit ? classical_apply<W>(t, a_val, b_val) : a_val;
    const uint32_t got = read_reg(sc.sv(), a.qubits, 17u);
    assert(got == expected && "simulated a must match classical expected");

    detach(a); detach(b); detach(ctrl);
    layout_release<W>(L, /*n_ctrls=*/1);

    std::printf("  PASS: test_ctrl_basis %s ctrl=%d a=%u b=%u -> %u\n",
                op_name(t), ctrl_bit ? 1 : 0, a_val, b_val, got);
}

// ── (c): ctrl=|+> — superposition control, entangled output ──────────────────
//
// With ctrl prepared as H|0> = (|0>+|1>)/sqrt(2), WHEN(ctrl){ a op= b }
// should yield
//   (|ctrl=0>|a_old>|b> + |ctrl=1>|a_new>|b>) / sqrt(2).
// We check amplitudes on the two expected basis states directly.

template <std::size_t W>
static void test_ctrl_plus(OpTag t, uint32_t a_val, uint32_t b_val) {
    sturm::QubitPool::instance().reset_for_testing();

    auto L = layout_alloc<W>(/*n_ctrls=*/1);
    SimCtx sc{17u, 128u};

    init_reg_value<W>(sc.sv(), L.a_qubits, a_val);
    init_reg_value<W>(sc.sv(), L.b_qubits, b_val);
    // Prepare ctrl = H|0> = |+>.
    orkan::apply_h(sc.sv(), static_cast<uint32_t>(L.ctrls[0]));

    sturm::qint_t<W> a, b;
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = L.a_qubits[i];
        b.qubits[i] = L.b_qubits[i];
    }
    a.super_mask = (1ULL << W) - 1ULL;
    b.super_mask = (1ULL << W) - 1ULL;
    a.value = static_cast<int64_t>(a_val);
    b.value = static_cast<int64_t>(b_val);
    a.owning_ = false;
    b.owning_ = false;

    sturm::qbool ctrl = sturm::qbool::make_non_owning(L.ctrls[0]);
    ctrl.super_mask   = 1ULL;
    ctrl.value        = 0;

    const auto qubits_before = snapshot_qubits(a);
    const int pool_before    = sturm::QubitPool::instance().in_use();

    apply_op_under_when<W>(t, a, b, ctrl);

    // In-place invariant.
    const auto qubits_after = snapshot_qubits(a);
    for (std::size_t i = 0; i < W; ++i) {
        assert(qubits_after[i] == qubits_before[i] &&
               "ctrl=|+> in-place op must preserve this->qubits[i] indices");
    }

    // Pool baseline restoration.
    const int pool_after = sturm::QubitPool::instance().in_use();
    assert(pool_after == pool_before &&
           "ctrl=|+> non-lossy op must not leak qubits beyond baseline");

    // Expected statevector: two basis states with amp = 1/sqrt(2).
    // Basis index encoding: bit k set iff physical qubit k is 1.
    auto make_basis = [&](uint32_t ctrl_bit, uint32_t a_out) -> uint64_t {
        uint64_t s = 0;
        for (std::size_t i = 0; i < W; ++i) {
            if ((a_out >> i) & 1u) s |= (1ULL << a.qubits[i]);
            if ((b_val >> i) & 1u) s |= (1ULL << b.qubits[i]);
        }
        if (ctrl_bit) s |= (1ULL << ctrl.qubits[0]);
        return s;
    };

    const uint64_t idx0 = make_basis(0u, a_val);                           // ctrl=0, a unchanged
    const uint64_t idx1 = make_basis(1u, classical_apply<W>(t, a_val, b_val)); // ctrl=1, a mutated

    const double amp_sq = 0.5;
    const double a0 = std::norm(orkan::amplitude(sc.sv(), idx0));
    const double a1 = std::norm(orkan::amplitude(sc.sv(), idx1));

    assert(std::fabs(a0 - amp_sq) < kTol &&
           "ctrl=|+>: |amp(ctrl=0, a=old)|^2 must be 1/2");
    assert(std::fabs(a1 - amp_sq) < kTol &&
           "ctrl=|+>: |amp(ctrl=1, a=new)|^2 must be 1/2");

    // No other basis state may carry weight (sum-to-one check).
    // We verify by summing all amplitude norms over the 17-qubit space
    // except idx0/idx1 and asserting zero.
    double other = 0.0;
    const uint64_t dim = uint64_t{1} << 17u;
    for (uint64_t s = 0; s < dim; ++s) {
        if (s == idx0 || s == idx1) continue;
        other += std::norm(orkan::amplitude(sc.sv(), s));
    }
    assert(other < kTol &&
           "ctrl=|+>: no weight outside the two expected basis states");

    detach(a); detach(b); detach(ctrl);
    layout_release<W>(L, /*n_ctrls=*/1);

    std::printf("  PASS: test_ctrl_plus %s a=%u b=%u\n",
                op_name(t), a_val, b_val);
}

// ── (d): nested WHEN(x){WHEN(y){...}} over all 4 classical (x,y) ─────────────
//
// Runtime semantics note: WhenGuard maintains a depth-1 invariant on the
// backend context's control_stack (sturm-d9n / sturm-ewto). When the outer
// WHEN's qbool is superposed (super_mask bit set) and the inner WHEN is
// also superposed, the guard swaps the outer control qubit off the stack
// and pushes the inner in its place. So a purely runtime
// `WHEN(xq){WHEN(yq){...}}` emits gates controlled by yq ALONE, not by
// (xq AND yq).
//
// Semantic (xq AND yq) gating is recovered at compile time by sturm-
// transpile's Phase G `matcher_when_nested` pass, which rewrites
// named-named nested WHENs to an explicit AND temp plus uncompute_and.
// These test executables are NOT routed through the transpiler (plain
// add_executable), so the expected runtime mutation fires iff y_bit = 1,
// regardless of x_bit. The sibling h5it.{2,3,4} nested tests sidestep
// this by only covering (x=1, y=*) cases, where runtime-inner-only
// happens to coincide with semantic AND.
//
// This characterisation test locks in the depth-1 invariant explicitly
// across all 4 (x,y) combinations. If a future change ever pushes both
// outer + inner controls (breaking depth-1), cases (x=0, y=1) and
// (x=1, y=1) would diverge from the expectations encoded here and flag
// the regression.

template <std::size_t W>
static void test_nested_basis(OpTag t, bool x_bit, bool y_bit,
                              uint32_t a_val, uint32_t b_val) {
    sturm::QubitPool::instance().reset_for_testing();

    auto L = layout_alloc<W>(/*n_ctrls=*/2);
    SimCtx sc{17u, 128u};

    init_reg_value<W>(sc.sv(), L.a_qubits, a_val);
    init_reg_value<W>(sc.sv(), L.b_qubits, b_val);
    if (x_bit) orkan::apply_x(sc.sv(), static_cast<uint32_t>(L.ctrls[0]));
    if (y_bit) orkan::apply_x(sc.sv(), static_cast<uint32_t>(L.ctrls[1]));

    sturm::qint_t<W> a, b;
    for (std::size_t i = 0; i < W; ++i) {
        a.qubits[i] = L.a_qubits[i];
        b.qubits[i] = L.b_qubits[i];
    }
    a.super_mask = (1ULL << W) - 1ULL;
    b.super_mask = (1ULL << W) - 1ULL;
    a.value = static_cast<int64_t>(a_val);
    b.value = static_cast<int64_t>(b_val);
    a.owning_ = false;
    b.owning_ = false;

    sturm::qbool xq = sturm::qbool::make_non_owning(L.ctrls[0]);
    xq.super_mask = 1ULL; xq.value = 0;
    sturm::qbool yq = sturm::qbool::make_non_owning(L.ctrls[1]);
    yq.super_mask = 1ULL; yq.value = 0;

    const auto qubits_before = snapshot_qubits(a);
    const int pool_before    = sturm::QubitPool::instance().in_use();

    switch (t) {
        case OpTag::Xor:
            WHEN(xq) { WHEN(yq) { a ^= b; } }
            break;
        case OpTag::Add:
            WHEN(xq) { WHEN(yq) { a += b; } }
            break;
        case OpTag::Sub:
            WHEN(xq) { WHEN(yq) { a -= b; } }
            break;
    }

    // In-place invariant.
    const auto qubits_after = snapshot_qubits(a);
    for (std::size_t i = 0; i < W; ++i) {
        assert(qubits_after[i] == qubits_before[i] &&
               "nested WHEN non-lossy op must preserve qubit indices");
    }

    // Pool baseline restoration.
    const int pool_after = sturm::QubitPool::instance().in_use();
    assert(pool_after == pool_before &&
           "nested WHEN non-lossy op must not leak qubits beyond baseline");

    // Runtime-inner-only gating under depth-1 invariant (see block comment
    // above): fires iff y_bit = 1. Both xq and yq are flagged superposed
    // (super_mask=1), so WhenGuard takes the swap path and the effective
    // stack control is yq alone.
    const bool fires = y_bit;
    const uint32_t expected =
        fires ? classical_apply<W>(t, a_val, b_val) : a_val;
    const uint32_t got = read_reg(sc.sv(), a.qubits, 17u);
    assert(got == expected && "nested WHEN: simulated a must match classical");

    detach(a); detach(b); detach(xq); detach(yq);
    layout_release<W>(L, /*n_ctrls=*/2);

    std::printf("  PASS: test_nested_basis %s x=%d y=%d a=%u b=%u -> %u\n",
                op_name(t), x_bit ? 1 : 0, y_bit ? 1 : 0,
                a_val, b_val, got);
    std::fflush(stdout);
}

// ── Per-op driver covering (a), (b), (c), (d) ────────────────────────────────

template <std::size_t W>
static void run_op_matrix(OpTag t, uint32_t a_val, uint32_t b_val) {
    std::printf("== %s matrix (W=%zu, a=%u, b=%u) ==\n",
                op_name(t), W, a_val, b_val);

    // (a) ctrl=|0>
    test_ctrl_basis<W>(t, /*ctrl_bit=*/false, a_val, b_val);
    // (b) ctrl=|1>
    test_ctrl_basis<W>(t, /*ctrl_bit=*/true,  a_val, b_val);
    // (c) ctrl=|+>
    test_ctrl_plus<W>(t, a_val, b_val);
    // (d) nested over all 4 classical (x,y)
    for (int xy = 0; xy < 4; ++xy) {
        const bool x_bit = (xy >> 0) & 1;
        const bool y_bit = (xy >> 1) & 1;
        test_nested_basis<W>(t, x_bit, y_bit, a_val, b_val);
    }
}

// ── main ─────────────────────────────────────────────────────────────────────
int main() {
    std::printf("sturm-h5it.1 non-lossy WHEN in-place regression baseline:\n");

    // Keep the qubit budget comfortably under STURM_MAX_QUBITS=17.
    // Per-test layout is 2W inputs + (1 or 2) ctrls + 1 carry/borrow ancilla
    // for +=/-= (^= allocates none). For W=2: 4 + 2 + 1 = 7 physical qubits
    // at most; well within 17. We use distinct a/b values to exercise
    // every bit path.
    //
    // ^=  test:  a=0b10, b=0b11  -> 0b01
    // +=  test:  a=0b10, b=0b01  -> 0b11   (no carry-out)
    // -=  test:  a=0b11, b=0b01  -> 0b10   (no borrow-out)

    run_op_matrix<2>(OpTag::Xor, 0b10u, 0b11u);
    run_op_matrix<2>(OpTag::Add, 0b10u, 0b01u);
    run_op_matrix<2>(OpTag::Sub, 0b11u, 0b01u);

    std::printf("All sturm-h5it.1 tests passed.\n");
    return 0;
}
