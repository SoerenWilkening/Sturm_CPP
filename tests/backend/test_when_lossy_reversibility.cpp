// test_when_lossy_reversibility.cpp -- LO-3b (sturm-wrmg): post-WHEN ancilla
// reversibility for all five controlled lossy compound assignments.
//
// Replaces tests/test_when_scope_garbage_consume.cpp (sturm-njul). That test
// observed the runtime garbage_registry's snapshot/consume API; the LO epic
// (sturm-lggp) replaces that runtime registry with the compile-time PRD §2
// desugar `qint tmp = a op b; swap(a, tmp); ... ; swap(a, tmp);
// invert(<dsl>)(a, b, tmp);`. The post-LO contract is observable at the
// state-vector level: after the WHEN scope closes (the desugar's tmp_*
// scope-exit cleanup runs), the tmp_* ancilla is back in |0> regardless of
// the control branch taken.
//
// This test pins that contract for all five lossy ops -- &=, |=, *=, /=, %= --
// inside `WHEN(ctrl)`. Because LO-2's transpiler pass does not run on
// backend-test sources, the desugar is emitted manually at each call site
// using the same DSL primitives + `invert<&dsl>()` adjoints LO-2 emits for
// transpiled code (see test_when_lossy_gate_equiv.cpp and PRD §5 for the
// lifting contract). The runtime `qint::operator op=` paths still leak
// (LO-4 deletes them); we deliberately do NOT call the leaky operators here.
//
// Verification: SIMULATE-mode amplitude check on the tmp_* qubits after the
// WHEN scope exits. In a basis-state simulation, the tmp_* qubits must read
// 0 in the surviving non-zero-amplitude basis vector.
//
// Budget: <= 280 LoC.

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/c_and_dsl.hpp"
#include "sturm/lib/div_dsl.hpp"
#include "sturm/lib/logic_dsl.hpp"
#include "sturm/lib/mul_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"
#include "sturm/routines/invert.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>

static constexpr double kTol = 1e-9;
static constexpr uint32_t kNQ = 17u;   // simulator qubit budget

// ── SimCtx (SIMULATE) ────────────────────────────────────────────────────────
struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 128u) {
        bridge.allocate(n_qubits);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
        assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
    orkan::state_t& sv() { return bridge.state(); }
};

// Find first basis state with non-zero amplitude; assumes pure basis.
static uint64_t pure_basis(orkan::state_t& sv, uint32_t n_total) {
    const uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) return s;
    }
    return 0u;
}

// Read W-bit register value out of qubits[]; pure-basis path.
template <std::size_t W>
static uint32_t read_w(orkan::state_t& sv, const int* qubits) {
    const uint64_t s = pure_basis(sv, kNQ);
    uint32_t v = 0u;
    for (std::size_t i = 0; i < W; ++i)
        if (qubits[i] >= 0) v |= (uint32_t((s >> qubits[i]) & 1u) << i);
    return v;
}

// Per-bit Fredkin (CSWAP under WHEN(ctrl)) via the BitProxy XOR idiom
// `a^=b; b^=a; a^=b`. PRD §5.1: this is what LO emits as `swap(a, tmp_*)`
// inside a WHEN scope.
template <std::size_t W>
static void fredkin_w(sturm::qint_t<W>& a, sturm::qint_t<W>& tmp) {
    for (std::size_t i = 0; i < W; ++i) {
        sturm::BitProxy ab(a, i), tb(tmp, i);
        ab ^= tb; tb ^= ab; ab ^= tb;
    }
}

// Promote `q` to W quantum qubits with a known classical value; uses
// pre-reserved indices passed in via `idx_base`. Skips the re-X step --
// callers `apply_x` directly on the orkan state to encode the value.
template <std::size_t W>
static void wire_qint(sturm::qint_t<W>& q, uint32_t idx_base, int64_t val) {
    for (std::size_t i = 0; i < W; ++i) q.qubits[i] = static_cast<int>(idx_base + i);
    q.super_mask = (W >= 64) ? ~0ULL : ((1ULL << W) - 1ULL);
    q.value      = val;
    q.owning_    = false;   // physical qubits owned by the test, not the qint
}

// Manually allocate an OOP-owned tmp_* register. Pool index is pre-reserved
// before SimCtx construction; we only set fields on the qint and let the
// destructor release on scope exit.
template <std::size_t W>
static sturm::qint_t<W> make_tmp(uint32_t idx_base) {
    sturm::qint_t<W> tmp;
    for (std::size_t i = 0; i < W; ++i) tmp.qubits[i] = static_cast<int>(idx_base + i);
    tmp.super_mask = (W >= 64) ? ~0ULL : ((1ULL << W) - 1ULL);
    tmp.value      = 0;
    tmp.owning_    = false; // physical qubits released by the harness
    return tmp;
}

// Assert all qubits in `idxs[0..N-1]` read zero in the surviving basis.
static void assert_zero(orkan::state_t& sv, const int* idxs, std::size_t n,
                        const char* tag) {
    const uint64_t s = pure_basis(sv, kNQ);
    for (std::size_t i = 0; i < n; ++i) {
        const int q = idxs[i];
        if (q < 0) continue;
        const bool bit = ((s >> q) & 1u) != 0u;
        if (bit) {
            std::fprintf(stderr, "  FAIL: %s -- qubit %d is |1> after WHEN exit\n",
                         tag, q);
            std::abort();
        }
    }
}

// Reserve `n` qubit indices contiguously starting from the current pool head.
// Returns the first index. Uses the global pool's allocate() so SimCtx sees
// them at the same physical positions.
static int reserve(uint32_t n) {
    int first = sturm::QubitPool::instance().allocate();
    for (uint32_t i = 1; i < n; ++i) sturm::QubitPool::instance().allocate();
    return first;
}

// ── controlled &= ────────────────────────────────────────────────────────────
// W=1. Manually emits the PRD §2.2 `&=` desugar inside WHEN(ctrl):
// forward and_oop = `tmp ^= (a & b)`; swap(a, tmp). Cleanup (still inside
// WHEN scope, before close-brace): swap(a, tmp); invert(lib_c_AND_dsl)(a,b,tmp).
static void test_when_lossy_and_assign() {
    sturm::QubitPool::instance().reset_for_testing();
    constexpr std::size_t W = 1;
    const int a_b = reserve(W), b_b = reserve(W), tmp_b = reserve(W);
    const int ctrl_q = reserve(1);
    SimCtx sc{kNQ};
    orkan::apply_x(sc.sv(), uint32_t(a_b));     // a = 1
    orkan::apply_x(sc.sv(), uint32_t(b_b));     // b = 1 -- a&b = 1
    orkan::apply_x(sc.sv(), uint32_t(ctrl_q));  // ctrl = |1>
    sturm::qint_t<W> a, b; wire_qint(a, uint32_t(a_b), 1); wire_qint(b, uint32_t(b_b), 1);
    sturm::qbool ctrl = sturm::qbool::make_non_owning(ctrl_q);
    ctrl.super_mask = 1ULL; ctrl.value = 0;
    constexpr auto and_adj = sturm::invert<&sturm::lib_c_AND_dsl<sturm::BitProxy>>();
    {
        sturm::qint_t<W> tmp = make_tmp<W>(uint32_t(tmp_b));
        WHEN(ctrl) {
            sturm::BitProxy ab(a, 0), bb(b, 0), tb(tmp, 0);
            sturm::lib_c_AND_dsl<sturm::BitProxy>(ab, bb, tb);   // tmp ^= a&b
            fredkin_w(a, tmp);                                    // swap(a, tmp)
            fredkin_w(a, tmp);                                    // reverse swap
            and_adj(ab, bb, tb);                                  // invert(c_AND)
        }
    }
    assert_zero(sc.sv(), &tmp_b, W, "controlled &= tmp");
    std::puts("  PASS: test_when_lossy_and_assign");
}

// ── controlled |= ────────────────────────────────────────────────────────────
static void test_when_lossy_or_assign() {
    sturm::QubitPool::instance().reset_for_testing();
    constexpr std::size_t W = 1;
    const int a_b = reserve(W), b_b = reserve(W), tmp_b = reserve(W);
    const int ctrl_q = reserve(1);
    SimCtx sc{kNQ};
    orkan::apply_x(sc.sv(), uint32_t(a_b));     // a = 1, b = 0 -- a|b = 1
    orkan::apply_x(sc.sv(), uint32_t(ctrl_q));  // ctrl = |1>
    sturm::qint_t<W> a, b; wire_qint(a, uint32_t(a_b), 1); wire_qint(b, uint32_t(b_b), 0);
    sturm::qbool ctrl = sturm::qbool::make_non_owning(ctrl_q);
    ctrl.super_mask = 1ULL; ctrl.value = 0;
    constexpr auto or_adj = sturm::invert<&sturm::lib_or_dsl<sturm::BitProxy>>();
    {
        sturm::qint_t<W> tmp = make_tmp<W>(uint32_t(tmp_b));
        WHEN(ctrl) {
            sturm::BitProxy ab(a, 0), bb(b, 0), tb(tmp, 0);
            sturm::lib_or_dsl<sturm::BitProxy>(ab, bb, tb);  // tmp ^= a|b
            fredkin_w(a, tmp);
            fredkin_w(a, tmp);
            or_adj(ab, bb, tb);                              // invert(or)
        }
    }
    assert_zero(sc.sv(), &tmp_b, W, "controlled |= tmp");
    std::puts("  PASS: test_when_lossy_or_assign");
}

// ── controlled *= ────────────────────────────────────────────────────────────
// W=1: 1*1 = 1. lib_mul_dsl writes a 2W=2 result register; LO-2's *= desugar
// owns the full 2W tmp; the swap targets only the lower-W. Cleanup invert
// zeroes both halves of tmp.
static void test_when_lossy_mul_assign() {
    sturm::QubitPool::instance().reset_for_testing();
    constexpr std::size_t W = 1;
    const int a_b = reserve(W), b_b = reserve(W), tmp_b = reserve(2 * W);
    const int ctrl_q = reserve(1);
    SimCtx sc{kNQ};
    orkan::apply_x(sc.sv(), uint32_t(a_b));     // a = 1
    orkan::apply_x(sc.sv(), uint32_t(b_b));     // b = 1
    orkan::apply_x(sc.sv(), uint32_t(ctrl_q));  // ctrl = |1>
    sturm::qint_t<W> a, b; wire_qint(a, uint32_t(a_b), 1); wire_qint(b, uint32_t(b_b), 1);
    sturm::qbool ctrl = sturm::qbool::make_non_owning(ctrl_q);
    ctrl.super_mask = 1ULL; ctrl.value = 0;
    constexpr auto mul_adj = sturm::invert<&sturm::lib_mul_dsl<sturm::BitProxy>>();
    {
        sturm::qint_t<2 * W> tmp = make_tmp<2 * W>(uint32_t(tmp_b));
        WHEN(ctrl) {
            sturm::BitProxy ab[W], bb[W], tb[2 * W];
            for (std::size_t i = 0; i < W; ++i) { ab[i] = sturm::BitProxy(a, i); bb[i] = sturm::BitProxy(b, i); }
            for (std::size_t i = 0; i < 2 * W; ++i) tb[i] = sturm::BitProxy(tmp, i);
            sturm::lib_mul_dsl<sturm::BitProxy>(ab, W, bb, W, tb, 2 * W);   // tmp = a*b
            // Swap targets the lower-W of tmp with a (PRD §2.4 -- *= swap target).
            for (std::size_t i = 0; i < W; ++i) {
                sturm::BitProxy x(a, i), y(tmp, i);
                x ^= y; y ^= x; x ^= y;
            }
            // Cleanup -- reverse swap then invert(mul).
            for (std::size_t i = 0; i < W; ++i) {
                sturm::BitProxy x(a, i), y(tmp, i);
                x ^= y; y ^= x; x ^= y;
            }
            mul_adj(ab, W, bb, W, tb, 2 * W);
        }
    }
    int tmp_idxs[2 * W]; for (std::size_t i = 0; i < 2 * W; ++i) tmp_idxs[i] = tmp_b + int(i);
    assert_zero(sc.sv(), tmp_idxs, 2 * W, "controlled *= tmp");
    std::puts("  PASS: test_when_lossy_mul_assign");
}

// ── controlled /= and %= (shared divide_oop scaffold) ────────────────────────
// W=1: 1/1 = 1, 1%1 = 0. /= swaps a with quotient half of tmp; %= swaps a
// with remainder half. Cleanup invert zeroes both halves of tmp regardless.
template <bool IsMod>
static void run_div_mod_test(const char* tag) {
    sturm::QubitPool::instance().reset_for_testing();
    constexpr std::size_t W = 1;
    const int a_b = reserve(W), b_b = reserve(W);
    const int q_b = reserve(W), r_b = reserve(W);   // tmp_q, tmp_r
    const int ctrl_q = reserve(1);
    SimCtx sc{kNQ};
    orkan::apply_x(sc.sv(), uint32_t(a_b));     // a = 1
    orkan::apply_x(sc.sv(), uint32_t(b_b));     // b = 1
    orkan::apply_x(sc.sv(), uint32_t(ctrl_q));  // ctrl = |1>
    sturm::qint_t<W> a, b; wire_qint(a, uint32_t(a_b), 1); wire_qint(b, uint32_t(b_b), 1);
    sturm::qbool ctrl = sturm::qbool::make_non_owning(ctrl_q);
    ctrl.super_mask = 1ULL; ctrl.value = 0;
    constexpr auto div_adj = sturm::invert<&sturm::lib_div_dsl<sturm::BitProxy>>();
    {
        sturm::qint_t<W> tmp_q = make_tmp<W>(uint32_t(q_b));
        sturm::qint_t<W> tmp_r = make_tmp<W>(uint32_t(r_b));
        WHEN(ctrl) {
            sturm::BitProxy ab[W], bb[W], qb[W], rb[W];
            for (std::size_t i = 0; i < W; ++i) {
                ab[i] = sturm::BitProxy(a, i);     bb[i] = sturm::BitProxy(b, i);
                qb[i] = sturm::BitProxy(tmp_q, i); rb[i] = sturm::BitProxy(tmp_r, i);
            }
            sturm::lib_div_dsl<sturm::BitProxy>(ab, W, bb, W, qb, rb);
            // Swap target depends on /= vs. %= per PRD §2.4.
            for (std::size_t i = 0; i < W; ++i) {
                sturm::BitProxy x(a, i);
                sturm::BitProxy y(IsMod ? tmp_r : tmp_q, i);
                x ^= y; y ^= x; x ^= y;
            }
            for (std::size_t i = 0; i < W; ++i) {
                sturm::BitProxy x(a, i);
                sturm::BitProxy y(IsMod ? tmp_r : tmp_q, i);
                x ^= y; y ^= x; x ^= y;
            }
            div_adj(ab, W, bb, W, qb, rb);
        }
    }
    int idxs[2 * W]; for (std::size_t i = 0; i < W; ++i) { idxs[i] = q_b + int(i); idxs[W + i] = r_b + int(i); }
    assert_zero(sc.sv(), idxs, 2 * W, tag);
    std::puts(tag);
}

static void test_when_lossy_div_assign() { run_div_mod_test<false>("  PASS: test_when_lossy_div_assign"); }
static void test_when_lossy_mod_assign() { run_div_mod_test<true >("  PASS: test_when_lossy_mod_assign"); }

// ── ctrl=|0> branch: ancilla still |0> after scope, no observable change ────
// PRD §5.1: the controlled swap and controlled invert are both no-ops when
// ctrl=|0>; tmp_and stays |0> trivially because the forward `tmp ^= (a&b)`
// never fired (it was lifted under the same ctrl). Pin the invariant.
static void test_when_lossy_and_assign_ctrl_zero() {
    sturm::QubitPool::instance().reset_for_testing();
    constexpr std::size_t W = 1;
    const int a_b = reserve(W), b_b = reserve(W), tmp_b = reserve(W);
    const int ctrl_q = reserve(1);
    SimCtx sc{kNQ};
    orkan::apply_x(sc.sv(), uint32_t(a_b));   // a = 1, b = 1
    orkan::apply_x(sc.sv(), uint32_t(b_b));
    // ctrl NOT flipped -> |0>.
    sturm::qint_t<W> a, b; wire_qint(a, uint32_t(a_b), 1); wire_qint(b, uint32_t(b_b), 1);
    sturm::qbool ctrl = sturm::qbool::make_non_owning(ctrl_q);
    ctrl.super_mask = 1ULL; ctrl.value = 0;
    constexpr auto and_adj = sturm::invert<&sturm::lib_c_AND_dsl<sturm::BitProxy>>();
    {
        sturm::qint_t<W> tmp = make_tmp<W>(uint32_t(tmp_b));
        WHEN(ctrl) {
            sturm::BitProxy ab(a, 0), bb(b, 0), tb(tmp, 0);
            sturm::lib_c_AND_dsl<sturm::BitProxy>(ab, bb, tb);
            fredkin_w(a, tmp);
            fredkin_w(a, tmp);
            and_adj(ab, bb, tb);
        }
    }
    // tmp is |0> (the controlled forward never fired). a should still be 1.
    assert_zero(sc.sv(), &tmp_b, W, "ctrl=|0> &= tmp");
    assert(read_w<W>(sc.sv(), a.qubits.data()) == 1u && "ctrl=|0>: a unchanged");
    std::puts("  PASS: test_when_lossy_and_assign_ctrl_zero");
}

// ── main ─────────────────────────────────────────────────────────────────────
int main() {
    std::printf("sturm-wrmg LO-3b WHEN-controlled lossy reversibility tests:\n");
    test_when_lossy_and_assign();
    test_when_lossy_or_assign();
    test_when_lossy_mul_assign();
    test_when_lossy_div_assign();
    test_when_lossy_mod_assign();
    test_when_lossy_and_assign_ctrl_zero();
    std::printf("All sturm-wrmg tests passed.\n");
    return 0;
}
