// test_mul_div_upperw_clean.cpp -- LO-3c (sturm-0qu5): pool-drain invariant
// for the full-W `*=` upper-W product register and `/=` remainder register.
//
// Replaces tests/backend/test_mul_div_upperw_garbage.cpp (sturm-pqs0). That
// previous test asserted the per-call growth of the runtime leak registry
// (MUL_UPPER_W / DIV_REMAINDER tags). LO-2 (sturm-v0ur) wires the LO
// transpiler pass that auto-desugars lossy compound assignments into the PRD
// §2 swap-and-uncompute shape; LO-4 (sturm-pw2f) then deletes the
// runtime leak registry entirely. The post-LO PRD §2 invariant is observable at
// the runtime level: after the enclosing C++ block exits, the qubit pool
// must have zero in-use allocations.
//
// Scope (matches sturm-pqs0): full-W `*=` (Cuccaro upper-W product release)
// and full-W `/=` (remainder register release). Lower-W relabel and quotient
// relabel are exercised by LO-0.5's test_lossy_ancilla_cleaned.cpp; here we
// pin specifically that the upper-W / remainder ancilla halves of those two
// operators do not leak past the enclosing scope.
//
// Path taken (until LO-2 has fully replaced the runtime tail)
// -----------------------------------------------------------
// LO-2 emitters (sturm-9254 / sturm-yxxa / sturm-hbwr) are wired into the
// transpiler (sturm-v0ur), but the runtime tail in qint_arith_v3.hpp still
// releases the upper-W and remainder registers in the uncontrolled branch.
// The uncontrolled `a *= b` and `a /= b` therefore drain the pool today.
// When LO-2 fully owns the rewrite, the same invariant continues to hold by
// construction (allocate-compute-swap + scope-exit swap-uncompute zeros the
// tmp_* register; tmp_* destructor releases). LO-4 will delete the
// runtime-tail releases; this test continues to pass via the LO-2 path.
//
// Budget: <= 250 LoC.
// Mirrors the shape of tests/backend/test_lossy_ancilla_cleaned.cpp (LO-0.5).

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

// Force the gate-emission (non-fast-path) branch in `*=` / `/=` by promoting
// one operand to quantum. The fast path (both operands classical AND no
// active control) bypasses the allocator entirely, which would trivially
// satisfy `in_use() == 0` without exercising the upper-W release / remainder
// release that this test pins. See qint_arith_v3.hpp short-circuit.

// ── ScopedCtx (COUNT_ONLY) ───────────────────────────────────────────────────
// COUNT_ONLY mode is sufficient: we only care about pool allocations, not
// state-vector evolution. SIMULATE would just slow the test down with no new
// signal.
struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit ScopedCtx(uint32_t max_q = 128u) {
        ctx  = sturm_backend_create(STURM_MODE_COUNT_ONLY, max_q);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

// Promote the W bits of `q` from classical to quantum so the lossy compound
// assignment exits the all-classical fast path. Allocates W qubits from the
// pool; ownership is on `q` (qint destructor releases). Sets super_mask all-1.
template <std::size_t W>
static void promote_to_quantum(sturm::qint_t<W>& q) {
    for (std::size_t i = 0; i < W; ++i) {
        if (q.qubits[i] < 0) {
            q.qubits[i] = sturm::QubitPool::instance().allocate();
        }
    }
    q.super_mask = (W >= 64) ? ~0ULL : ((1ULL << W) - 1ULL);
    q.owning_ = true;
}

// Run `body` inside an inner block, asserting the pool drains (in_use() == 0)
// after the block exits. The block itself sees a known clean baseline because
// reset_for_testing() is called before the body. Captures `tag` for the
// failing-assertion diagnostic.
template <class Fn>
static void run_block_and_assert_drained(const char* tag, Fn body) {
    sturm::QubitPool::instance().reset_for_testing();
    {
        // Inner C++ scope: the qints created inside body's lambda go out of
        // scope at the closing brace, releasing their qubits via the qint
        // destructor (B10). The lossy-op runtime tail releases all temporary
        // registers in the uncontrolled branch (including the upper-W / 2W
        // result-register tail of `*=` and the W-bit remainder of `/=`).
        body();
    }
    const int leaked = sturm::QubitPool::instance().in_use();
    if (leaked != 0) {
        std::fprintf(stderr,
                     "  FAIL: %s — pool has %d leaked qubits after block exit\n",
                     tag, leaked);
    }
    assert(leaked == 0
           && "*= upper-W / /= remainder must drain the qubit pool by block exit");
    std::printf("  PASS: %s\n", tag);
}

// ── 1. *= drains the upper-W half of the 2W Cuccaro result register ─────────
// `*=` allocates a 2W result register, runs lib_mul_dsl, then releases the
// upper W bits unconditionally and pointer-relabels A to the lower W. The
// invariant we pin: by block exit, the upper-W release plus the qint
// destructors leave the pool fully drained.
static void test_mul_assign_upperw_drained() {
    ScopedCtx sc;
    run_block_and_assert_drained("a *= b (upper-W cleaned)", [] {
        static constexpr std::size_t W = 2;
        sturm::qint_t<W> a(3), b(2);
        promote_to_quantum(a);
        // b can stay classical: the fast-path guard requires BOTH operands
        // classical, so promoting just A is enough to take the gate path.
        a *= b;
        // a, b destructors fire at the closing brace of this lambda body.
    });
}

// ── 2. /= drains the W-bit remainder register ───────────────────────────────
// `/=` allocates W quotient + W remainder qubits, runs lib_div_dsl, then
// releases the remainder unconditionally and pointer-relabels A to the
// quotient. Pin: by block exit, the remainder release plus the qint
// destructors leave the pool fully drained.
static void test_div_assign_remainder_drained() {
    ScopedCtx sc;
    run_block_and_assert_drained("a /= b (remainder cleaned)", [] {
        static constexpr std::size_t W = 2;
        sturm::qint_t<W> a(3), b(2);
        promote_to_quantum(a);
        a /= b;
    });
}

// ── 3. *= followed by /= in one block — cumulative drain ────────────────────
// Walks both lossy ops back-to-back to confirm composition: the upper-W
// release of *= and the remainder release of /= cumulatively drain the
// pool, even when both ops appear in the same enclosing scope. LO-2's LIFO
// scope-exit emitter must compose; this asserts the runtime equivalent.
static void test_mul_then_div_in_one_block_drained() {
    ScopedCtx sc;
    run_block_and_assert_drained("a *= b ; a /= b (back-to-back)", [] {
        static constexpr std::size_t W = 2;
        sturm::qint_t<W> a(6), b(2);
        promote_to_quantum(a);
        a *= b;   // result 12 mod 2^W
        a /= b;   // quotient of (a mod 2^W) / b
        // a, b destructors release any qubits they still own.
    });
}

// ── 4. *= with W=3 (wider operand) — upper-W release scales with W ──────────
// Pin: pool drainage holds for W>2, exercising the full RW=2W release loop
// in qint_arith_v3.hpp lines 130–137 (release upper W, pointer-relabel
// lower W). Uses W=3 for an asymmetric size that still fits within the
// COUNT_ONLY ScopedCtx default budget (max_q=128).
static void test_mul_assign_upperw_drained_w3() {
    ScopedCtx sc;
    run_block_and_assert_drained("a *= b (W=3 upper-W cleaned)", [] {
        static constexpr std::size_t W = 3;
        sturm::qint_t<W> a(5), b(3);
        promote_to_quantum(a);
        a *= b;
    });
}

// ── 5. /= with W=3 (wider operand) — remainder release scales with W ────────
// Pin: pool drainage holds for the wider W=3 division: W quotient bits
// pointer-relabeled, W remainder bits released. Mirrors test 4 but for /=.
static void test_div_assign_remainder_drained_w3() {
    ScopedCtx sc;
    run_block_and_assert_drained("a /= b (W=3 remainder cleaned)", [] {
        static constexpr std::size_t W = 3;
        sturm::qint_t<W> a(7), b(2);
        promote_to_quantum(a);
        a /= b;
    });
}

// ── main ─────────────────────────────────────────────────────────────────────
int main() {
    std::printf("sturm-0qu5 LO-3c *= upper-W / /= remainder pool-drain tests:\n");
    test_mul_assign_upperw_drained();
    test_div_assign_remainder_drained();
    test_mul_then_div_in_one_block_drained();
    test_mul_assign_upperw_drained_w3();
    test_div_assign_remainder_drained_w3();
    std::printf("All sturm-0qu5 tests passed.\n");
    return 0;
}
