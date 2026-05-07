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

#include "sturm/core/core.h"          // C ABI declarations (opaque handle, enums)
#include "sturm/core/qubit_pool.hpp"
#include "sturm/core/control_stack.hpp" // M12: per-context control stack
#include "sturm/backend/ir.hpp"       // GateIR — used in APPEND mode

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

    // Per-context qubit pool.  After sturm-zbzo (G5) the pool has no cap
    // and grows on demand; the global singleton (QubitPool::instance())
    // is a separate object used by the frontend qtypes.
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

    // Per-qubit classical values (one entry per physical qubit index, up to 17).
    // Used by COUNT_ONLY and APPEND measurement paths to return the stored value
    // without sampling the statevector.  Set to 0 at construction; updated by
    // the SIMULATE measurement path after each sample so the classical value
    // tracks the post-measurement computational basis state.
    static constexpr uint32_t kMaxClassicalQubits = 17u;
    int           classical_values[kMaxClassicalQubits]{};

    // Per-qubit superposition-tracking bitmask (bit i = qubit i).
    // Set by the frontend when a qubit enters superposition (e.g. after H).
    // Cleared by measure_qubit in SIMULATE mode after sampling + collapse.
    // TODO(backend): frontend qtypes will set bits here when M-future wires
    //                qubit state tracking through the context.
    uint32_t      super_mask{0};

    // Per-qubit promotion bitmask (bit i = qubit i).
    // Set by the frontend when a qubit is promoted to a quantum type.
    // Cleared by measure_qubit in SIMULATE mode after sampling + collapse.
    // TODO(backend): frontend qtypes will set bits here when M-future wires
    //                qubit promotion tracking through the context.
    uint32_t      promotion_mask{0};

    // M12: WHEN control stack — pushed by WHEN entry, popped on exit.
    // Readable by qbool operators without depending on WhenLift.
    ControlStack  control_stack;

    explicit BackendContext(sturm_mode_t m)
        : mode(m) {}
};

// ── set_mode / get_current_mode ───────────────────────────────────────────────
//
// Convenience wrappers that operate on the calling thread's active context.
// Panics (via assert) if no context is installed.

void set_mode(sturm_mode_t mode);
sturm_mode_t get_current_mode();

// ── current_thread_context_or_null (sturm-uoeb / PRD §5.6) ───────────────────
//
// Returns the per-thread context pointer *without* the process-wide-default
// fallback that sturm_get_thread_context() applies. When the calling thread
// has not installed a context (or has cleared it via
// sturm_set_thread_context(nullptr)), this returns nullptr — letting the
// no-argument renderer entry points (sturm::draw_ascii / print_ascii /
// gate_count) assert on the absence of an active lifecycle, which is a
// programming error per PRD §5.6.

sturm_backend_context_t* current_thread_context_or_null() noexcept;

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

// ── execute_gate observer hook (sturm-a3t4.7) ─────────────────────────────────
//
// Test-only thread-local hook fired by execute_gate after gate_count increment
// and before mode dispatch.  Exists so the depth-1 invariant regression test
// (tests/control/depth_invariant_test.cpp) can poll
// current_control_stack().depth() at every gate site without interleaving
// custom logic into the library primitives themselves.  Production callers
// leave the hook null (default) and pay no overhead beyond a null check.
//
// Lifetime: caller-owned function pointer; reset to nullptr before tearing
// down whatever state the callback closes over.  Defined in
// src/sturm/core/execute_gate.cpp.

using ExecuteGateHook = void (*)(BackendContext&,
                                 sturm_gate_kind_t,
                                 const uint32_t*,
                                 uint8_t,
                                 double);

void             set_execute_gate_hook(ExecuteGateHook hook) noexcept;
ExecuteGateHook  get_execute_gate_hook() noexcept;

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
