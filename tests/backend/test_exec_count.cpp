// test_exec_count.cpp — M7: COUNT_ONLY executor tests.
// TDD: written before implementation, drives exec_count.hpp.
//
// Tests:
//   1. exec_count is callable with a valid BackendContext and does not crash.
//   2. exec_count does not increment the gate counter (counter is the
//      dispatcher's responsibility, not the executor's).
//   3. Executing 1000 gates via exec_count does not allocate heap memory
//      (recording allocator fuzz).
//   4. exec_count is a no-op regardless of gate kind or param value.

#include "sturm/backend/exec_count.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <new>

// ── Recording allocator ───────────────────────────────────────────────────────
//
// We intercept global operator new/delete to count allocations.  The counter
// is reset before each fuzz run so incidental allocations elsewhere don't
// interfere.

static std::size_t g_alloc_count = 0;
static bool        g_tracking    = false;

void* operator new(std::size_t sz) {
    if (g_tracking) ++g_alloc_count;
    void* p = std::malloc(sz);
    if (!p) throw std::bad_alloc{};
    return p;
}

void* operator new[](std::size_t sz) {
    if (g_tracking) ++g_alloc_count;
    void* p = std::malloc(sz);
    if (!p) throw std::bad_alloc{};
    return p;
}

void operator delete(void* p) noexcept  { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept  { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build a minimal valid BackendContext in COUNT_ONLY mode.
static sturm_backend_context_t* make_count_ctx() {
    return sturm_backend_create(STURM_MODE_COUNT_ONLY, 17u);
}

// ── Test 1: exec_count is callable without crashing ───────────────────────────

static void test_exec_count_callable() {
    sturm_backend_context_t* ctx = make_count_ctx();
    assert(ctx != nullptr);

    uint32_t q[1] = {0};
    sturm::exec_count(*ctx,
                      STURM_GATE_X,
                      q, /*n=*/1u,
                      /*param=*/0.0);
    sturm_backend_destroy(ctx);
}

// ── Test 2: exec_count does NOT increment the gate counter ───────────────────
//
// Per PRD §5 and the implementation plan M7: "counter handled by dispatcher".
// The executor itself must leave ctx.gate_count unchanged.

static void test_exec_count_no_counter_increment() {
    sturm_backend_context_t* ctx = make_count_ctx();
    assert(ctx != nullptr);

    uint64_t before = sturm_gate_count(ctx);

    uint32_t q[2] = {0, 1};
    sturm::exec_count(*ctx, STURM_GATE_CX, q, 2u, 0.0);
    sturm::exec_count(*ctx, STURM_GATE_X,  q, 1u, 0.0);
    sturm::exec_count(*ctx, STURM_GATE_H,  q, 1u, 0.0);

    uint64_t after = sturm_gate_count(ctx);
    assert(after == before && "exec_count must not touch the gate counter");

    sturm_backend_destroy(ctx);
}

// ── Test 3: 1000 gates — zero heap allocations ────────────────────────────────
//
// exec_count is a no-op body; all 18 gate kinds are exercised in a round-robin
// fashion.  The recording allocator intercepts operator new; any allocation
// inside exec_count is a bug.

static void test_exec_count_no_alloc_1000_gates() {
    sturm_backend_context_t* ctx = make_count_ctx();
    assert(ctx != nullptr);

    // Warm up: call once outside tracking to ensure any one-time init is done.
    uint32_t q[3] = {0, 1, 2};
    sturm::exec_count(*ctx, STURM_GATE_CCX, q, 3u, 0.0);

    // Reset counter and start tracking.
    g_alloc_count = 0;
    g_tracking    = true;

    constexpr int kIterations = 1000;
    for (int i = 0; i < kIterations; ++i) {
        auto kind = static_cast<sturm_gate_kind_t>(i % static_cast<int>(STURM_GATE_COUNT));
        sturm::exec_count(*ctx, kind, q, 3u, static_cast<double>(i) * 0.001);
    }

    g_tracking = false;

    assert(g_alloc_count == 0 &&
           "exec_count allocated heap memory — must be zero-alloc");

    sturm_backend_destroy(ctx);
}

// ── Test 4: no-op regardless of gate kind or param ───────────────────────────
//
// Sweeps all 18 gate kinds and a variety of param values.  The result is
// verified by asserting that gate_count never changes (same proof as test 2,
// but exhaustive over the gate set).

static void test_exec_count_noop_all_gates() {
    sturm_backend_context_t* ctx = make_count_ctx();
    assert(ctx != nullptr);

    uint32_t q[3]   = {0, 1, 2};
    double   params[] = {0.0, 1.5707963, 3.1415926, -1.0, 42.0};

    uint64_t before = sturm_gate_count(ctx);

    for (int ki = 0; ki < static_cast<int>(STURM_GATE_COUNT); ++ki) {
        auto kind = static_cast<sturm_gate_kind_t>(ki);
        for (double p : params) {
            sturm::exec_count(*ctx, kind, q, 3u, p);
        }
    }

    uint64_t after = sturm_gate_count(ctx);
    assert(after == before && "exec_count must be a pure no-op");

    sturm_backend_destroy(ctx);
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    test_exec_count_callable();
    test_exec_count_no_counter_increment();
    test_exec_count_no_alloc_1000_gates();
    test_exec_count_noop_all_gates();
    return 0;
}
