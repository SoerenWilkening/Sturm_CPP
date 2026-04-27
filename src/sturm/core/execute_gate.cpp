// execute_gate.cpp — M13: Layer B join point.
//
// Provides:
//   sturm::execute_gate(ctx, kind, qubits, n, param)  — C++ helper
//   sturm_execute_gate(kind, qubits, n, param)         — C ABI entry point
//
// Responsibilities:
//   1. Increment ctx.gate_count unconditionally (PRD §5).
//   2. Switch on ctx.mode and call the appropriate executor:
//        COUNT_ONLY  → exec_count  (no-op after counting)
//        APPEND      → exec_append (push GateRecord onto ctx.ir)
//        SIMULATE    → exec_simulate_1q / exec_simulate_multiq / exec_simulate_crot
//                      depending on gate kind (arity)
//
// The C ABI sturm_execute_gate was previously implemented in context.cpp with
// only the counter increment and a TODO stub.  That stub is now removed and
// replaced by a forwarding call to this C++ helper.
//
// LOC budget: <120 (implementation plan).

#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"
#include "sturm/backend/exec_count.hpp"
#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/exec_simulate.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <stdexcept>

namespace sturm {

// ── execute_gate observer hook (sturm-a3t4.7) ─────────────────────────────────
//
// Thread-local function pointer fired by execute_gate after the gate_count
// increment and before the mode-dispatch switch.  Default-null: production
// callers pay only the cost of a null check.  The depth-1 invariant test
// installs a callback that polls current_control_stack().depth() so it can
// observe the stack state at every gate emission site without instrumenting
// the library primitives themselves.

namespace {
thread_local ExecuteGateHook s_execute_gate_hook = nullptr;
}  // namespace

void set_execute_gate_hook(ExecuteGateHook hook) noexcept {
    s_execute_gate_hook = hook;
}

ExecuteGateHook get_execute_gate_hook() noexcept {
    return s_execute_gate_hook;
}

// ── execute_gate — C++ helper ─────────────────────────────────────────────────
//
// Called by the C ABI entry point and (in future) directly from C++ Layer A
// dispatch code.

void execute_gate(BackendContext&   ctx,
                  sturm_gate_kind_t kind,
                  const uint32_t*   qubits,
                  uint8_t           n,
                  double            param) {
    // Step 1: unconditional gate count (PRD §5).
    ctx.gate_count++;

    // Step 1b: fire the optional observer hook (sturm-a3t4.7).  The hook is
    // null in production builds; tests install one transiently to poll
    // control_stack depth at every gate emission site.
    if (auto hook = s_execute_gate_hook) {
        hook(ctx, kind, qubits, n, param);
    }

    // Step 2: mode dispatch.
    switch (ctx.mode) {

    case STURM_MODE_COUNT_ONLY:
        exec_count(ctx, kind, qubits, n, param);
        break;

    case STURM_MODE_APPEND:
        exec_append(ctx, kind, qubits, n, param);
        break;

    case STURM_MODE_SIMULATE: {
        // The Orkan statevector is accessed through the bridge stored in
        // orkan_state_ptr.  When the pointer is null (e.g. context created
        // without a bridge — tests that only check the counter) we fall
        // through to exec_count behavior so the counter increment still
        // takes effect without crashing.
        if (!ctx.orkan_state_ptr) {
            // No statevector available; count only.
            break;
        }

        auto* bridge = static_cast<OrkanBridge*>(ctx.orkan_state_ptr);
        orkan::state_t& sv = bridge->state();

        // Route to the appropriate simulate sub-function based on gate kind.
        switch (kind) {
        // ── 1-qubit gates ──────────────────────────────────────────────────
        case STURM_GATE_X:
        case STURM_GATE_Y:
        case STURM_GATE_Z:
        case STURM_GATE_H:
        case STURM_GATE_S:
        case STURM_GATE_T:
        case STURM_GATE_P:
        case STURM_GATE_RX:
        case STURM_GATE_RY:
        case STURM_GATE_RZ:
            exec_simulate_1q(sv, kind, qubits[0], param);
            break;

        // ── 2/3-qubit permutation & phase gates ────────────────────────────
        case STURM_GATE_CX:
        case STURM_GATE_CY:
        case STURM_GATE_CZ:
        case STURM_GATE_CCX:
        case STURM_GATE_SWAP:
            exec_simulate_multiq(sv, kind,
                                 qubits[0],
                                 qubits[1],
                                 (n >= 3u) ? qubits[2] : 0u,
                                 param);
            break;

        // ── CRx/CRy/CRz — decomposed at SIMULATE time only ─────────────────
        case STURM_GATE_CRX:
        case STURM_GATE_CRY:
        case STURM_GATE_CRZ:
            exec_simulate_crot(sv, kind, qubits[0], qubits[1], param);
            break;

        default:
            // Unknown gate kind — should never reach here if Layer A is
            // correct.  Abort in debug; silently skip in release.
            assert(false && "execute_gate: unknown gate kind in SIMULATE mode");
            break;
        }
        break;
    } // STURM_MODE_SIMULATE

    default:
        assert(false && "execute_gate: unknown execution mode");
        break;
    }
}

} // namespace sturm

// ── C ABI entry point ─────────────────────────────────────────────────────────
//
// Replaces the stub in context.cpp.  Fetches the thread-local context and
// forwards to the C++ helper.
//
// NOTE: The old implementation in context.cpp only incremented the counter and
// left a TODO comment.  That TODO is now resolved here.  The declaration in
// context.cpp is removed and this translation unit owns the definition.

extern "C"
void sturm_execute_gate(sturm_gate_kind_t kind,
                        const uint32_t*   qubits,
                        uint8_t           n,
                        double            param) {
    sturm_backend_context_t* ctx = sturm_get_thread_context();
    if (!ctx) return;
    sturm::execute_gate(*ctx, kind, qubits, n, param);
}
