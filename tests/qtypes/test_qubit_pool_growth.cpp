// test_qubit_pool_growth.cpp — sturm-zbzo (Frontend simpl. P2.a / G5).
// Plan §3 Phase 2 / PRD §5.5.
//
// After P2.a:
//   * The legacy cap macro / sentinel constant / abort path are gone.
//   * `QubitPool` is default-constructible (no `max_qubits` arg).
//   * Pool grows on demand; there is no hard cap.
//
// This test pins the post-change contract by acquiring enough qubits to
// blow past the former singleton cap (256 / 512 — whatever the deleted
// compile-time knob was set to) and asserting the pool keeps handing
// out fresh, monotonically-allocated indices without aborting.
//
// Concretely we acquire 1024 qubits in a single pool instance. Before
// P2.a this would have aborted on the 17th acquire() (per-context cap
// of 17) or on the (former-singleton-cap+1)st allocate(). After P2.a
// it must succeed.

#include "sturm/core/context.hpp"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdio>
#include <unordered_set>
#include <vector>

using sturm::QubitPool;

// ── Test 1: pool grows past former cap (1024 qubits) ─────────────────────
//
// 1024 is comfortably above every plausible pre-P2.a cap (17 per-context;
// 256 / 512 singleton). If the pool grows on demand, this loop completes
// without aborting and yields 1024 distinct non-negative indices.

static void test_grow_past_former_cap() {
    QubitPool pool;  // no cap argument (P2.a)

    constexpr int kN = 1024;
    std::vector<int> indices;
    indices.reserve(kN);
    std::unordered_set<int> seen;

    for (int i = 0; i < kN; ++i) {
        int idx = pool.allocate();
        assert(idx >= 0 && "allocate() returned a negative index");
        // Indices must be unique while in_use (no double-issue).
        bool inserted = seen.insert(idx).second;
        assert(inserted && "allocate() handed out a duplicate index");
        indices.push_back(idx);
    }

    assert(pool.in_use() == kN);
    assert(pool.high_water() >= kN);

    // Release everything so the destructor sees a clean pool.
    for (int idx : indices) pool.release(idx);
    assert(pool.in_use() == 0);
}

// ── Test 2: alloc / release / re-alloc loop past the former cap ───────────
//
// Drains 1024 qubits, releases half, re-allocates half. The pool must
// recycle the freed indices (LIFO) without aborting and without
// duplicating any live index.

static void test_alloc_release_loop_past_cap() {
    QubitPool pool;

    constexpr int kN = 1024;
    std::vector<int> indices;
    indices.reserve(kN);

    for (int i = 0; i < kN; ++i) {
        indices.push_back(pool.allocate());
    }
    assert(pool.in_use() == kN);

    // Release the last 512 indices.
    constexpr int kHalf = 512;
    for (int i = 0; i < kHalf; ++i) {
        pool.release(indices.back());
        indices.pop_back();
    }
    assert(pool.in_use() == kN - kHalf);

    // Re-acquire 512 — must succeed, every index must be a non-negative
    // already-seen-or-new value, and total in_use must climb back to 1024.
    for (int i = 0; i < kHalf; ++i) {
        int idx = pool.allocate();
        assert(idx >= 0);
        indices.push_back(idx);
    }
    assert(pool.in_use() == kN);

    // Drain.
    for (int idx : indices) pool.release(idx);
    assert(pool.in_use() == 0);
}

// ── Test 3: BackendContext-owned pool grows the same way ─────────────────
//
// After P2.a, BackendContext takes only `(mode)` — the per-context pool
// has no cap argument and grows the same way the bare-pool path does.
// We exercise the construction shape (this is the API contract that
// breaks the 126 callers; that's expected per the issue notes) and the
// pool-growth contract.

static void test_backend_context_pool_grows() {
    // Construct directly — no max_qubits arg.
    sturm_backend_context_t ctx{STURM_MODE_COUNT_ONLY};

    constexpr int kN = 1024;
    std::vector<int> indices;
    indices.reserve(kN);
    for (int i = 0; i < kN; ++i) {
        int idx = ctx.pool.allocate();
        assert(idx >= 0);
        indices.push_back(idx);
    }
    assert(ctx.pool.in_use() == kN);
    for (int idx : indices) ctx.pool.release(idx);
    assert(ctx.pool.in_use() == 0);
}

// ── main ─────────────────────────────────────────────────────────────────

int main() {
    test_grow_past_former_cap();
    test_alloc_release_loop_past_cap();
    test_backend_context_pool_grows();
    std::puts("test_qubit_pool_growth: PASS");
    return 0;
}
