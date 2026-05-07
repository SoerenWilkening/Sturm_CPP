/* core_stub.c — M2: Stub implementation of the C ABI sink (core.h).
 *
 * Pure C. Implements all symbols declared in core.h with minimal logic
 * so that the header can be tested from a C translation unit before the
 * full C++ BackendContext (M3) is in place.
 *
 * Every function that will be replaced by a real implementation in M3+
 * is marked TODO(backend).
 *
 * Thread-local storage: C11 _Thread_local or MSVC __declspec(thread).
 */

#include "sturm/core/core.h"

#include <stddef.h>  /* NULL */
#include <stdint.h>
#include <stdlib.h>  /* malloc, free */

/* ── Concrete context struct (stub-internal) ────────────────────────────────── */

struct sturm_backend_context {
    sturm_mode_t mode;
    uint64_t     gate_count;
};

/* ── Thread-local context pointer ──────────────────────────────────────────── */

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
#  define STURM_THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
#  define STURM_THREAD_LOCAL __thread
#elif defined(_MSC_VER)
#  define STURM_THREAD_LOCAL __declspec(thread)
#else
#  define STURM_THREAD_LOCAL  /* fallback: not thread-safe */
#endif

static STURM_THREAD_LOCAL sturm_backend_context_t* s_thread_ctx = NULL;

/* ── Lifecycle ──────────────────────────────────────────────────────────────── */

sturm_backend_context_t* sturm_backend_create(sturm_mode_t mode) {
    sturm_backend_context_t* ctx =
        (sturm_backend_context_t*)malloc(sizeof(sturm_backend_context_t));
    if (!ctx) return NULL;
    ctx->mode       = mode;
    ctx->gate_count = 0u;
    return ctx;
}

void sturm_backend_destroy(sturm_backend_context_t* ctx) {
    if (!ctx) return;
    /* TODO(backend): release IR buffer, Orkan state when M3/M6/M9 land. */
    free(ctx);
}

/* ── Thread-local context ───────────────────────────────────────────────────── */

void sturm_set_thread_context(sturm_backend_context_t* ctx) {
    s_thread_ctx = ctx;
}

sturm_backend_context_t* sturm_get_thread_context(void) {
    return s_thread_ctx;
    /* TODO(backend): fall back to process-wide default when M3 lands. */
}

/* ── Layer B sink ───────────────────────────────────────────────────────────── */

void sturm_execute_gate(sturm_gate_kind_t kind,
                        const uint32_t*   qubits,
                        uint8_t           n,
                        double            param) {
    (void)kind;
    (void)qubits;
    (void)n;
    (void)param;

    sturm_backend_context_t* ctx = sturm_get_thread_context();
    if (!ctx) return;

    /* Unconditional counter increment (PRD §5). */
    ctx->gate_count++;

    /* TODO(backend): mode switch to exec_count / exec_append / exec_simulate
     * is wired up in M13 (execute_gate.cpp). */
}

/* ── Gate counter ───────────────────────────────────────────────────────────── */

uint64_t sturm_gate_count(const sturm_backend_context_t* ctx) {
    if (!ctx) return 0u;
    return ctx->gate_count;
}

/* ── Measurement ────────────────────────────────────────────────────────────── */

int sturm_measure(uint32_t qubit) {
    (void)qubit;
    /* TODO(backend): COUNT_ONLY/APPEND return stored classical value;
     * SIMULATE samples from Orkan.  Implemented in M5. */
    return 0; /* deterministic placeholder */
}
