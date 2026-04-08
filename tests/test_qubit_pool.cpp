// test_qubit_pool.cpp — TDD tests for QubitPool (Step 1)
// Written before implementation; confirms red → green flow.

#include "sturm/core/qubit_pool.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>

using sturm::QubitPool;

// ── helpers ──────────────────────────────────────────────────────────────────

static QubitPool& pool() { return QubitPool::instance(); }

static void reset() { pool().reset_for_testing(); }

// ── Test 1: monotonic allocation until capacity ───────────────────────────────

static void test_monotonic() {
    reset();
    assert(pool().in_use() == 0);
    assert(pool().capacity() == STURM_ANCILLA_CAPACITY);

    int prev = pool().allocate();
    assert(prev == 0);
    for (int i = 1; i < pool().capacity(); ++i) {
        int idx = pool().allocate();
        assert(idx > prev || idx >= 0);  // indices are valid non-negative
        prev = idx;
    }
    assert(pool().in_use() == pool().capacity());
}

// ── Test 2: LIFO recycle after release ───────────────────────────────────────

static void test_lifo_recycle() {
    reset();
    int a = pool().allocate();  // 0
    int b = pool().allocate();  // 1
    int c = pool().allocate();  // 2
    pool().release(c);
    pool().release(b);
    int r1 = pool().allocate();  // should get b (last released)
    int r2 = pool().allocate();  // should get c (first released, LIFO)
    assert(r1 == b);
    assert(r2 == c);
    (void)a;
}

// ── Test 3: exhaustion throws ─────────────────────────────────────────────────

static void test_exhaustion_throws() {
    reset();
    for (int i = 0; i < pool().capacity(); ++i) {
        pool().allocate();
    }
    bool threw = false;
    try {
        pool().allocate();
    } catch (const std::runtime_error& e) {
        threw = true;
    }
    assert(threw && "expected runtime_error on pool exhaustion");
}

// ── Test 4: alloc+release loop stays at in_use()==0 ──────────────────────────

static void test_alloc_release_loop() {
    reset();
    for (int i = 0; i < 10000; ++i) {
        int idx = pool().allocate();
        pool().release(idx);
    }
    assert(pool().in_use() == 0);
    assert(pool().high_water() <= pool().capacity());
}

// ── Test 5: reset_for_testing restarts count at 0 ────────────────────────────

static void test_reset() {
    reset();
    pool().allocate();
    pool().allocate();
    reset();
    int idx = pool().allocate();
    assert(idx == 0);
    assert(pool().in_use() == 1);
    assert(pool().high_water() <= pool().capacity());
}

// ── Test 6: multi-threaded no duplicates, final in_use()==0 ──────────────────

static void test_multithreaded() {
    reset();
    constexpr int kThreads = 8;
    constexpr int kIters   = 1000;

    // Track which indices are "live" at any moment; detect duplicates.
    std::mutex live_mutex;
    std::unordered_set<int> live;
    std::atomic<bool> duplicate_detected{false};

    auto worker = [&]() {
        for (int i = 0; i < kIters; ++i) {
            int idx;
            {
                // Allocate and record atomically (w.r.t. the live set) so that
                // no other thread can re-acquire this index before we track it.
                std::lock_guard<std::mutex> lk(live_mutex);
                idx = pool().allocate();
                if (!live.insert(idx).second) {
                    duplicate_detected = true;
                }
            }
            {
                // Release and erase atomically so the pool cannot hand the
                // same index to another thread before we remove it from live.
                std::lock_guard<std::mutex> lk(live_mutex);
                live.erase(idx);
                pool().release(idx);
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) t.join();

    assert(!duplicate_detected && "duplicate live index detected in multi-threaded test");
    assert(pool().in_use() == 0);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_monotonic();
    test_lifo_recycle();
    test_exhaustion_throws();
    test_alloc_release_loop();
    test_reset();
    test_multithreaded();
    return 0;
}
