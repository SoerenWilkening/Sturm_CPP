// test_lossy_ancilla_cleaned.cpp -- LO-0.5 (sturm-qoq0): end-to-end pool-drain
// invariant for every lossy compound assignment.
//
// Contract (the post-LO PRD §2 invariant): when a lossy compound assignment
// (`*=`, `/=`, `%=`, `&=`, `|=`) appears inside a C++ block, the qubit pool
// has zero leaked allocations after the block exits. This is the runtime
// observable that the LO-2 transpiler auto-desugar pass (allocate-compute-swap
// + scope-exit swap-uncompute) is required to preserve.
//
// Path taken (until LO-2 lands)
// -----------------------------
// LO-2 (sturm-9254 / sturm-yxxa / sturm-hbwr) is **not yet landed** at the
// time of this test's introduction, so we exercise the existing runtime path
// (uncontrolled qint compound assigns + RAII destructors). Per PRD §5.1, the
// uncontrolled path inside each operator already releases all temporary
// registers and pointer-relabels A to the result register; the old A's
// qubits are released back to the pool. Combined with the qint destructor's
// release loop (B10), every qint that goes out of scope returns its qubits
// to the pool. The post-block invariant `pool.in_use() == 0` therefore must
// hold today.
//
// When LO-2 lands and rewrites `a *= b` into the §2 desugar shape, the same
// invariant continues to hold by construction (the scope-exit cleanup zeros
// the tmp_* register and the tmp_* destructor releases). LO-4 then deletes
// the runtime-tail in qint_arith_v3.hpp / qint_bitwise_v3.hpp; at that point
// this test continues to pass via the LO-2 path. The test does not need to
// change.
//
// Manual desugar fallback note
// ----------------------------
// If a future maintainer disables / breaks the runtime path before LO-2 is
// available, this test will fail. The fallback at that point is to manually
// emit the PRD §2 desugar at each call site (qint tmp = …; swap(a, tmp);
// at scope exit: swap(a, tmp); invert(<dsl>)(a, b, tmp);) and re-assert the
// pool invariant. The LO-1* family already provides the registered adjoints
// for all five DSLs (`invert<&lib_mul_dsl<BitProxy>>()` etc.). LO-2 will
// remove that scaffolding.
//
// Budget: <= 250 LoC.

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

// Force the gate-emission (non-fast-path) branch in each lossy operator by
// promoting one operand to quantum. The fast path (both operands classical
// AND no active control) bypasses the allocator entirely and would trivially
// satisfy `in_use() == 0` without exercising the lossy-cleanup machinery this
// test pins. See qint_arith_v3.hpp / qint_bitwise_v3.hpp short-circuit.

// ── ScopedCtx (COUNT_ONLY) ───────────────────────────────────────────────────
// COUNT_ONLY mode is sufficient: we only care about pool allocations, not
// state-vector evolution. SIMULATE would just slow the test down with no new
// signal.
struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit ScopedCtx(uint32_t max_q = 128u) {
        ctx  = sturm_backend_create(STURM_MODE_COUNT_ONLY);
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
        // registers in the uncontrolled branch, so the only qubits still
        // allocated at this point are the ones owned by the surviving qints.
        body();
    }
    const int leaked = sturm::QubitPool::instance().in_use();
    if (leaked != 0) {
        std::fprintf(stderr,
                     "  FAIL: %s — pool has %d leaked qubits after block exit\n",
                     tag, leaked);
    }
    assert(leaked == 0
           && "lossy compound assignment must drain the qubit pool by block exit");
    std::printf("  PASS: %s\n", tag);
}

// ── 1. *= (multiplicative, lower-W relabel + upper-W release) ───────────────
static void test_mul_assign_drained() {
    ScopedCtx sc;
    run_block_and_assert_drained("a *= b", [] {
        static constexpr std::size_t W = 2;
        sturm::qint_t<W> a(3), b(2);
        promote_to_quantum(a);
        // b can stay classical: the fast-path guard requires BOTH operands
        // classical, so promoting just A is enough to take the gate path.
        a *= b;
        // a, b destructors fire at the closing brace of this lambda body.
    });
}

// ── 2. /= (division, quotient relabel + remainder release) ──────────────────
static void test_div_assign_drained() {
    ScopedCtx sc;
    run_block_and_assert_drained("a /= b", [] {
        static constexpr std::size_t W = 2;
        sturm::qint_t<W> a(3), b(2);
        promote_to_quantum(a);
        a /= b;
    });
}

// ── 3. %= (modulo, remainder relabel; lib_mod_dsl handles its own ancillas) ─
static void test_mod_assign_drained() {
    ScopedCtx sc;
    run_block_and_assert_drained("a %= b", [] {
        static constexpr std::size_t W = 2;
        sturm::qint_t<W> a(3), b(2);
        promote_to_quantum(a);
        a %= b;
    });
}

// ── 4. &= (bitwise AND, result relabel via CCX sweep) ───────────────────────
static void test_and_assign_drained() {
    ScopedCtx sc;
    run_block_and_assert_drained("a &= b", [] {
        static constexpr std::size_t W = 2;
        sturm::qint_t<W> a(3), b(2);
        promote_to_quantum(a);
        a &= b;
    });
}

// ── 5. |= (bitwise OR, result relabel via 2 CX + CCX) ───────────────────────
static void test_or_assign_drained() {
    ScopedCtx sc;
    run_block_and_assert_drained("a |= b", [] {
        static constexpr std::size_t W = 2;
        sturm::qint_t<W> a(3), b(2);
        promote_to_quantum(a);
        a |= b;
    });
}

// ── 6. All five back-to-back inside one block (cumulative drain) ────────────
// LO-2 will rewrite each statement independently; the LIFO scope-exit emitter
// must compose. This sub-case asserts that running every lossy op inside the
// same block still drains by the end. Distinct qint variables prevent the
// pointer-relabel of one op from interacting with another's tail.
static void test_all_five_in_one_block_drained() {
    ScopedCtx sc;
    run_block_and_assert_drained("all five back-to-back", [] {
        static constexpr std::size_t W = 2;
        sturm::qint_t<W> a1(3), a2(3), a3(3), a4(3), a5(3), b(2);
        promote_to_quantum(a1);
        promote_to_quantum(a2);
        promote_to_quantum(a3);
        promote_to_quantum(a4);
        promote_to_quantum(a5);
        a1 *= b;
        a2 /= b;
        a3 %= b;
        a4 &= b;
        a5 |= b;
        // All six qints (5 *_ai + b) destruct at the closing brace.
    });
}

// ── main ─────────────────────────────────────────────────────────────────────
int main() {
    std::printf("sturm-qoq0 LO-0.5 lossy-op pool-drain tests:\n");
    test_mul_assign_drained();
    test_div_assign_drained();
    test_mod_assign_drained();
    test_and_assign_drained();
    test_or_assign_drained();
    test_all_five_in_one_block_drained();
    std::printf("All sturm-qoq0 tests passed.\n");
    return 0;
}
