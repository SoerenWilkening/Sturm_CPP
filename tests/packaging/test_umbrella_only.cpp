// test_umbrella_only.cpp — sturm-ilz3 / Phase 3 acceptance test for the
// new `sturm.h` umbrella header (PRD §5.1 / G1, A5 first half).
//
// Pins the contract that `#include "sturm.h"` ALONE — with no other
// `<sturm/...>` includes — is sufficient to:
//   1. Compile `qint a = 5;` (the user-level alias is in scope).
//   2. Drive the C ABI lifecycle (sturm_backend_create / destroy and
//      sturm_set_thread_context are reachable through the umbrella's
//      forwarded `sturm/core/core.h`).
//   3. Reach `ctx->ir.append(...)` so the IR is observably populated;
//      this proves the umbrella does not drop a header that the
//      gate-emission path (GateIR / GateRecord / sturm_gate_kind_t) needs.
//
// The umbrella sets STURM_BACKEND_ENABLED=1 itself (PRD §5.1), so
// userland does NOT need the `#define STURM_BACKEND_ENABLED 1` line
// that the legacy `<sturm/sturm.hpp>` umbrella required (cf. the older
// `test_detail_layout.cpp` posture, which still tests the legacy
// umbrella).
//
// Auto-injected lifecycle (Phase 7 / sturm-e3ru) is NOT yet wired,
// so this test calls sturm_backend_create / destroy explicitly. Once
// Phase 7 lands, the explicit pair below collapses into the IIFE
// rewrite — the test still passes byte-identically because the C ABI
// is unchanged.

#include "sturm.h"

#include <cstdio>

int main() {
    // 1. Lifecycle (will become auto-injected in Phase 7).
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);

    // 2. The required syntax line — `qint` brought in by the umbrella's
    //    `using sturm::qint;` (PRD §5.1).
    qint a = 5;
    (void)a;  // silence unused-var warning; the contract is COMPILES.

    // 3. Append a gate directly to the IR. Going through the C ABI
    //    `sturm_execute_gate` would require linking the runtime TUs
    //    (`execute_gate.cpp` + `exec_append.cpp`); driving `ctx->ir`
    //    directly keeps the test linkage minimal (`context.cpp` +
    //    `gate_kind.c`) while still exercising every reach-point the
    //    umbrella promises (`GateIR`, `GateRecord`, `sturm_gate_kind_t`,
    //    `BackendContext`). The EXIT CRITERIA only require that the
    //    IR observably grows by ≥ 1 entry, not that the gate-emission
    //    path traverse `execute_gate`.
    sturm::GateRecord rec{};
    rec.kind = STURM_GATE_X;
    rec.qubits = {0u, 0u, 0u};
    rec.n = 1u;
    rec.param = 0.0;
    ctx->ir.append(rec);

    const std::size_t gate_count = ctx->ir.size();
    std::printf("test_umbrella_only: ctx->ir.size() = %zu\n", gate_count);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);

    return gate_count >= 1u ? 0 : 1;
}
