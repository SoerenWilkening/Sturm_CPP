/* core.h — M2: STURM C ABI sink header.
 *
 * Pure C header (usable from both C and C++).
 * Exposes the Layer B sink (execute_gate), the BackendContext opaque handle,
 * mode selection, thread-local context management, and measurement.
 *
 * All parameters are POD. No C++ types leak through this boundary.
 */

#ifndef STURM_CORE_CORE_H
#define STURM_CORE_CORE_H

#include <stdint.h>

#include "sturm/core/gate_kind.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque context handle ──────────────────────────────────────────────────── */

/* Forward declaration only; callers never dereference directly.
 * The concrete type lives in context.cpp (C++ side, M3).
 */
typedef struct sturm_backend_context sturm_backend_context_t;

/* ── Execution-mode enum ────────────────────────────────────────────────────── */

typedef enum sturm_mode {
    STURM_MODE_COUNT_ONLY = 0, /* default: count gates, no state or IR */
    STURM_MODE_APPEND     = 1, /* push GateRecords to in-memory IR buffer */
    STURM_MODE_SIMULATE   = 2  /* call Orkan statevector backend */
} sturm_mode_t;

/* ── Lifecycle ──────────────────────────────────────────────────────────────── */

/* Create a new backend context.
 *
 * mode       — initial execution mode (can be changed via sturm_set_mode).
 * max_qubits — qubit pool capacity; pass 17 for the PRD §6 default.
 *
 * Returns a heap-allocated context. The caller owns the object; free with
 * sturm_backend_destroy.  Returns NULL on allocation failure.
 */
sturm_backend_context_t* sturm_backend_create(sturm_mode_t mode,
                                               uint32_t     max_qubits);

/* Destroy a context previously returned by sturm_backend_create.
 * Passing NULL is a no-op.
 */
void sturm_backend_destroy(sturm_backend_context_t* ctx);

/* ── Thread-local context pointer ──────────────────────────────────────────── */

/* Install ctx as the active context for the calling thread.
 * sturm_execute_gate, sturm_gate_count, and sturm_measure all
 * operate on the thread-local context.
 * Passing NULL clears the pointer (subsequent calls without a context
 * installed fall back to the process-wide default, see M3).
 */
void sturm_set_thread_context(sturm_backend_context_t* ctx);

/* Return the context currently installed for the calling thread.
 * Returns NULL if none is set (process default is wired up in M3).
 */
sturm_backend_context_t* sturm_get_thread_context(void);

/* ── Layer B sink ───────────────────────────────────────────────────────────── */

/* Execute (or record) a single gate.
 *
 * kind   — one of the 18 STURM primitive gates.
 * qubits — array of physical qubit indices, exactly n entries.
 * n      — arity; must equal sturm_gate_info_of(kind)->arity.
 * param  — rotation angle θ (radians) for parametric gates; 0.0 otherwise.
 *
 * Behaviour is mode-dependent:
 *   COUNT_ONLY — increments gate counter; no other side-effects.
 *   APPEND     — increments gate counter; pushes a GateRecord to the IR.
 *   SIMULATE   — increments gate counter; forwards to Orkan (CRx/CRy/CRz
 *                are decomposed inline at this layer, see PRD §5 / M12).
 *
 * Thread-safe only if distinct threads use distinct contexts.
 */
void sturm_execute_gate(sturm_gate_kind_t kind,
                        const uint32_t*   qubits,
                        uint8_t           n,
                        double            param);

/* ── Gate counter ───────────────────────────────────────────────────────────── */

/* Return the total number of times sturm_execute_gate has been called on ctx.
 * The counter is cumulative and never resets unless the context is destroyed
 * and recreated.
 */
uint64_t sturm_gate_count(const sturm_backend_context_t* ctx);

/* ── Measurement ────────────────────────────────────────────────────────────── */

/* Measure a single qubit.
 *
 * qubit — physical qubit index.
 *
 * Returns 0 or 1 (the measurement outcome).
 * Mode-dependent semantics (PRD §5):
 *   COUNT_ONLY — deterministic placeholder; returns the stored classical value.
 *   APPEND     — same as COUNT_ONLY.
 *   SIMULATE   — samples from Orkan; updates classical value.
 *
 * TODO(backend): SIMULATE path is stubbed until M9/M10 land.
 */
int sturm_measure(uint32_t qubit);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* STURM_CORE_CORE_H */
