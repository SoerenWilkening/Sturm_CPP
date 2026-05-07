// test_context.cpp — M3: BackendContext + thread-local mode storage tests.
// TDD: written before implementation, drives context.hpp / context.cpp.
//
// Tests:
//   1. default context exists (process-wide default is non-null)
//   2. mode roundtrips (set_mode, then observe mode on ctx)
//   3. gate counter starts at 0
//   4. switching context does not leak (destroy old without crash)
//   5. thread-local default is shared within one thread

#include "sturm/core/context.hpp"

#include <cassert>
#include <cstdint>

// ── Test 1: default context exists ───────────────────────────────────────────

static void test_default_context_exists() {
    sturm_backend_context_t* ctx = sturm_get_thread_context();
    // The process-wide default must be lazily created on first access.
    assert(ctx != nullptr);
}

// ── Test 2: mode roundtrip ────────────────────────────────────────────────────

static void test_mode_roundtrip() {
    sturm::set_mode(STURM_MODE_COUNT_ONLY);
    assert(sturm::get_current_mode() == STURM_MODE_COUNT_ONLY);

    sturm::set_mode(STURM_MODE_APPEND);
    assert(sturm::get_current_mode() == STURM_MODE_APPEND);

    sturm::set_mode(STURM_MODE_SIMULATE);
    assert(sturm::get_current_mode() == STURM_MODE_SIMULATE);

    // Restore to default.
    sturm::set_mode(STURM_MODE_COUNT_ONLY);
}

// ── Test 3: gate counter starts at 0 ─────────────────────────────────────────

static void test_counter_starts_zero() {
    // Create a fresh context to get a clean counter.
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_COUNT_ONLY);
    assert(ctx != nullptr);
    assert(sturm_gate_count(ctx) == 0u);
    sturm_backend_destroy(ctx);
}

// ── Test 4: context switch does not leak the previous ────────────────────────
//
// Strategy: save the current thread context, install a new one, destroy the
// new one, restore the previous.  If this triggers ASan/valgrind, there's a
// double-free or leak.  Here we just assert that all pointers stay consistent.

static void test_context_switch_no_leak() {
    sturm_backend_context_t* original = sturm_get_thread_context();

    sturm_backend_context_t* fresh =
        sturm_backend_create(STURM_MODE_APPEND);
    assert(fresh != nullptr);

    // Install the fresh context.
    sturm_set_thread_context(fresh);
    assert(sturm_get_thread_context() == fresh);

    // Switch back to original; destroy the one we created.
    sturm_set_thread_context(original);
    sturm_backend_destroy(fresh);

    // After restoring, thread context should be the original.
    assert(sturm_get_thread_context() == original);
}

// ── Test 5: thread-local default is shared within one thread ─────────────────
//
// Two calls to sturm_get_thread_context() from the same thread must return
// the same pointer (the lazily-created process-wide default is the same
// instance every time the thread has not overridden it).

static void test_thread_local_shared_within_thread() {
    sturm_backend_context_t* a = sturm_get_thread_context();
    sturm_backend_context_t* b = sturm_get_thread_context();
    assert(a == b);
    assert(a != nullptr);
}

// ── Test 6: BackendContext has an accessible QubitPool member ─────────────────
//
// The BackendContext.pool field must be a sturm::QubitPool instance.
// sturm-5jta (P2.b / G5): the legacy max_qubits cap is gone — the pool
// grows on demand and exposes only in_use() / high_water().

static void test_context_has_qubit_pool() {
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_COUNT_ONLY);
    assert(ctx != nullptr);
    // pool starts empty and exposes its diagnostic counters.
    assert(ctx->pool.in_use() == 0);
    assert(ctx->pool.high_water() == 0);
    sturm_backend_destroy(ctx);
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    test_default_context_exists();
    test_mode_roundtrip();
    test_counter_starts_zero();
    test_context_switch_no_leak();
    test_thread_local_shared_within_thread();
    test_context_has_qubit_pool();
    return 0;
}
