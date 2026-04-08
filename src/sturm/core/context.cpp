// context.cpp — M3: BackendContext + thread-local mode storage (C++ implementation).
//
// Implements:
//   - sturm_backend_create / sturm_backend_destroy  (lifecycle)
//   - sturm_set_thread_context / sturm_get_thread_context  (thread-local)
//   - sturm_execute_gate / sturm_gate_count / sturm_measure  (C ABI sink)
//   - sturm::set_mode / sturm::get_current_mode  (C++ convenience)
//
// The process-wide default context is lazily created on first access to
// sturm_get_thread_context() within any thread that has not installed its own.

#include "sturm/core/context.hpp"
#include "sturm/core/core.h"

#include <cassert>
#include <cstdlib>
#include <new>

// ── Process-wide default context ─────────────────────────────────────────────
//
// Created once (lazy, via a function-local static) and shared across threads
// that have not installed a per-thread context.  The default starts in
// COUNT_ONLY mode with a 17-qubit cap.

static sturm_backend_context_t* get_process_default() {
    static sturm_backend_context_t s_default{STURM_MODE_COUNT_ONLY, 17u};
    return &s_default;
}

// ── Thread-local context pointer ──────────────────────────────────────────────

static thread_local sturm_backend_context_t* s_thread_ctx = nullptr;

// ── C ABI: lifecycle ──────────────────────────────────────────────────────────

extern "C"
sturm_backend_context_t* sturm_backend_create(sturm_mode_t mode,
                                               uint32_t     max_qubits) {
    auto* ctx = new (std::nothrow) sturm_backend_context_t{mode, max_qubits};
    return ctx; // nullptr on allocation failure
}

extern "C"
void sturm_backend_destroy(sturm_backend_context_t* ctx) {
    if (!ctx) return;
    // Do not destroy the process-wide default (it's static storage).
    if (ctx == get_process_default()) return;
    // TODO(backend): free IR buffer when M6 lands.
    // TODO(backend): free Orkan state when M9 lands.
    delete ctx;
}

// ── C ABI: thread-local context ───────────────────────────────────────────────

extern "C"
void sturm_set_thread_context(sturm_backend_context_t* ctx) {
    s_thread_ctx = ctx;
}

extern "C"
sturm_backend_context_t* sturm_get_thread_context(void) {
    if (s_thread_ctx) return s_thread_ctx;
    // Fall back to the process-wide default (lazily created).
    return get_process_default();
}

// ── C ABI: gate counter ───────────────────────────────────────────────────────

extern "C"
uint64_t sturm_gate_count(const sturm_backend_context_t* ctx) {
    if (!ctx) return 0u;
    return ctx->gate_count;
}

// ── C ABI: Layer B sink ───────────────────────────────────────────────────────

extern "C"
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

    // Unconditional counter increment (PRD §5).
    ctx->gate_count++;

    // TODO(backend): mode switch to exec_count / exec_append / exec_simulate
    // is wired up in M13 (execute_gate.cpp).
}

// ── C ABI: measurement ────────────────────────────────────────────────────────

extern "C"
int sturm_measure(uint32_t qubit) {
    (void)qubit;
    // TODO(backend): COUNT_ONLY/APPEND return stored classical value;
    // SIMULATE samples from Orkan.  Implemented in M5.
    return 0;
}

// ── C++ helpers ───────────────────────────────────────────────────────────────

namespace sturm {

void set_mode(sturm_mode_t mode) {
    sturm_backend_context_t* ctx = sturm_get_thread_context();
    assert(ctx && "set_mode called with no active context");
    ctx->mode = mode;
}

sturm_mode_t get_current_mode() {
    sturm_backend_context_t* ctx = sturm_get_thread_context();
    assert(ctx && "get_current_mode called with no active context");
    return ctx->mode;
}

} // namespace sturm
