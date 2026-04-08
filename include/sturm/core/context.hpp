// context.hpp — M3: C++ inline helpers over core.h (BackendContext + thread-local).
//
// Provides:
//   - sturm::BackendContext  (concrete C++ struct backing the opaque C handle)
//   - sturm::set_mode()      convenience wrapper
//   - sturm::get_current_mode()
//
// The C ABI functions declared in core.h (sturm_get_thread_context,
// sturm_set_thread_context, sturm_backend_create, etc.) are implemented in
// context.cpp and forward to/from the C++ internals here.
//
// NOTE: This header is included from C++ only.  The C ABI boundary is core.h.

#pragma once

#include "sturm/core/core.h"       // C ABI declarations (opaque handle, enums)
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/ir.hpp"   // GateIR — used in APPEND mode

#include <cstdint>
#include <memory>

namespace sturm {

// ── BackendContext ────────────────────────────────────────────────────────────
//
// Concrete definition of the opaque C type sturm_backend_context_t.
// Lives in the C++ world; C callers only ever hold a pointer.

struct BackendContext {
    sturm_mode_t  mode;
    uint64_t      gate_count{0};

    // Per-context qubit pool.  Capacity is set at construction time via
    // max_qubits; the global singleton (QubitPool::instance()) is a separate
    // object used by the frontend qtypes.
    // TODO(backend): wire allocate()/release() through this pool when the
    //                frontend qtype constructors accept a context argument (M-future).
    QubitPool     pool;

    // IR buffer — populated in APPEND mode (M8).
    // Always present so exec_append can unconditionally push; size()==0
    // unless mode is STURM_MODE_APPEND.
    GateIR        ir;

    // Pointer to the Orkan statevector (non-null only in SIMULATE mode, M9).
    // TODO(backend): replaced with OrkanBridge* when M9 lands.
    void*         orkan_state_ptr{nullptr};

    explicit BackendContext(sturm_mode_t m, uint32_t max_q)
        : mode(m), pool(max_q) {}
};

// ── set_mode / get_current_mode ───────────────────────────────────────────────
//
// Convenience wrappers that operate on the calling thread's active context.
// Panics (via assert) if no context is installed.

void set_mode(sturm_mode_t mode);
sturm_mode_t get_current_mode();

// ── execute_gate — C++ Layer B helper (M13) ───────────────────────────────────
//
// Increments ctx.gate_count unconditionally, then dispatches to one of the
// three mode executors (exec_count / exec_append / exec_simulate).
// Defined in src/sturm/core/execute_gate.cpp.

void execute_gate(BackendContext&   ctx,
                  sturm_gate_kind_t kind,
                  const uint32_t*   qubits,
                  uint8_t           n,
                  double            param);

} // namespace sturm

// ── C ABI concrete struct alias ───────────────────────────────────────────────
//
// Make the opaque C handle resolve to the C++ struct so that C and C++ code
// share one allocation.  The C header forward-declares:
//
//   typedef struct sturm_backend_context sturm_backend_context_t;
//
// We define the struct tag here:

struct sturm_backend_context : sturm::BackendContext {
    using sturm::BackendContext::BackendContext;
};
