/* test_core_abi_c.c — M2: C ABI sink header tests.
 *
 * Compiled as a pure C translation unit (.c, no C++).
 * Goals:
 *   1. The header compiles from C without errors.
 *   2. Every declared symbol is reachable (links against the stub).
 *   3. Basic runtime sanity on the stub implementation.
 */

#include "sturm/core/core.h"

#include <assert.h>
#include <stddef.h>

/* ── Helper: qubits array for a 1-qubit gate ────────────────────────────────── */

static const uint32_t kQ0[1] = {0};
static const uint32_t kQ01[2] = {0, 1};
static const uint32_t kQ012[3] = {0, 1, 2};

/* ── Test: header compiles and mode enum values are as specified ─────────────── */

static void test_mode_enum_values(void) {
    assert((int)STURM_MODE_COUNT_ONLY == 0);
    assert((int)STURM_MODE_APPEND     == 1);
    assert((int)STURM_MODE_SIMULATE   == 2);
}

/* ── Test: create / destroy roundtrip ──────────────────────────────────────── */

static void test_create_destroy(void) {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_COUNT_ONLY);
    assert(ctx != NULL);
    sturm_backend_destroy(ctx);
    /* Destroying NULL is a no-op, must not crash. */
    sturm_backend_destroy(NULL);
}

/* ── Test: gate counter starts at zero ─────────────────────────────────────── */

static void test_gate_counter_starts_zero(void) {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_COUNT_ONLY);
    assert(ctx != NULL);
    assert(sturm_gate_count(ctx) == 0u);
    sturm_backend_destroy(ctx);
}

/* ── Test: set/get thread context roundtrip ────────────────────────────────── */

static void test_set_get_thread_context(void) {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_COUNT_ONLY);
    assert(ctx != NULL);

    sturm_set_thread_context(ctx);
    assert(sturm_get_thread_context() == ctx);

    /* Clear the thread context. */
    sturm_set_thread_context(NULL);

    sturm_backend_destroy(ctx);
}

/* ── Test: sturm_execute_gate is callable (symbol is reachable) ─────────────── */

static void test_execute_gate_callable(void) {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_COUNT_ONLY);
    assert(ctx != NULL);
    sturm_set_thread_context(ctx);

    /* 1-qubit gate */
    sturm_execute_gate(STURM_GATE_X, kQ0, 1, 0.0);

    /* 2-qubit gate */
    sturm_execute_gate(STURM_GATE_CX, kQ01, 2, 0.0);

    /* 3-qubit gate */
    sturm_execute_gate(STURM_GATE_CCX, kQ012, 3, 0.0);

    /* parametric gate */
    sturm_execute_gate(STURM_GATE_RX, kQ0, 1, 1.5707963267948966);

    sturm_set_thread_context(NULL);
    sturm_backend_destroy(ctx);
}

/* ── Test: gate counter increments after execute_gate calls ────────────────── */

static void test_gate_counter_increments(void) {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_COUNT_ONLY);
    assert(ctx != NULL);
    sturm_set_thread_context(ctx);

    assert(sturm_gate_count(ctx) == 0u);
    sturm_execute_gate(STURM_GATE_X, kQ0, 1, 0.0);
    assert(sturm_gate_count(ctx) == 1u);
    sturm_execute_gate(STURM_GATE_CX, kQ01, 2, 0.0);
    assert(sturm_gate_count(ctx) == 2u);
    sturm_execute_gate(STURM_GATE_H, kQ0, 1, 0.0);
    assert(sturm_gate_count(ctx) == 3u);

    sturm_set_thread_context(NULL);
    sturm_backend_destroy(ctx);
}

/* ── Test: sturm_measure is callable ───────────────────────────────────────── */

static void test_measure_callable(void) {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_COUNT_ONLY);
    assert(ctx != NULL);
    sturm_set_thread_context(ctx);

    /* Stub returns 0 or 1; just verify no crash and valid range. */
    int result = sturm_measure(0u);
    assert(result == 0 || result == 1);

    sturm_set_thread_context(NULL);
    sturm_backend_destroy(ctx);
}

/* ── Test: multiple distinct contexts are independent ───────────────────────── */

static void test_independent_contexts(void) {
    sturm_backend_context_t* ctx_a = sturm_backend_create(STURM_MODE_COUNT_ONLY);
    sturm_backend_context_t* ctx_b = sturm_backend_create(STURM_MODE_COUNT_ONLY);
    assert(ctx_a != NULL);
    assert(ctx_b != NULL);
    assert(ctx_a != ctx_b);

    /* Drive gates through ctx_a. */
    sturm_set_thread_context(ctx_a);
    sturm_execute_gate(STURM_GATE_X, kQ0, 1, 0.0);
    sturm_execute_gate(STURM_GATE_X, kQ0, 1, 0.0);

    /* ctx_b should still be at 0. */
    assert(sturm_gate_count(ctx_a) == 2u);
    assert(sturm_gate_count(ctx_b) == 0u);

    sturm_set_thread_context(NULL);
    sturm_backend_destroy(ctx_a);
    sturm_backend_destroy(ctx_b);
}

/* ── main ───────────────────────────────────────────────────────────────────── */

int main(void) {
    test_mode_enum_values();
    test_create_destroy();
    test_gate_counter_starts_zero();
    test_set_get_thread_context();
    test_execute_gate_callable();
    test_gate_counter_increments();
    test_measure_callable();
    test_independent_contexts();
    return 0;
}
