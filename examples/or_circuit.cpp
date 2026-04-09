// or_circuit.cpp — Example: a |= b on qint_t<W> in gate-recording (APPEND)
//
// Semantics: a |= b writes the OR result into a's register. Because a bitwise
// OR is not reversible in place, the canonical decomposition writes into a
// fresh register (shown below as "a'") that conceptually replaces a.
// mode. Prints the resulting circuit as ASCII art via sturm::draw_ascii.
//
// Build:
//   cmake --build build --target example_or_circuit
// Run:
//   ./build/examples/example_or_circuit
//
// What this example shows:
//   1. How to install a thread-local backend context in APPEND mode so every
//      gate ends up in ctx->ir instead of being simulated.
//   2. How to invoke the high-level `operator|` on two qint_t<W> values.
//   3. How to render the recorded GateIR with sturm::draw_ascii.
//
// Note: the current backend-enabled operator| is a stub and does not itself
// emit gates (see qint_bitwise.hpp — "TODO(backend): emit OR circuit"). So
// that the circuit is non-trivial to look at, the example also emits the
// canonical per-bit OR decomposition into a fresh output register:
//
//     out[i] = a[i] OR b[i]
//            = CX a[i]->out[i] ; CX b[i]->out[i] ; CCX a[i],b[i]->out[i]
//
// Once the backend path for operator| is implemented, the manual emission
// block below can be removed and the drawing will still work.

#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/draw_ascii.hpp"
#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"

#include <cstdint>
#include <cstdio>

namespace {

// Emit a 3-gate OR decomposition for one bit position.
void emit_or_bit(sturm::BackendContext& ctx,
                 uint32_t a_q, uint32_t b_q, uint32_t out_q) {
    uint32_t cx_a[2]  = {a_q,  out_q};
    uint32_t cx_b[2]  = {b_q,  out_q};
    uint32_t ccx[3]   = {a_q,  b_q, out_q};
    sturm::exec_append(ctx, STURM_GATE_CX,  cx_a, 2, 0.0);
    sturm::exec_append(ctx, STURM_GATE_CX,  cx_b, 2, 0.0);
    sturm::exec_append(ctx, STURM_GATE_CCX, ccx,  3, 0.0);
}

} // namespace

int main() {
    // ── 1. Create an APPEND-mode backend context ──────────────────────────────
    constexpr std::size_t W = 3;                   // 3-bit qints, keeps diagram small
    constexpr uint32_t kNumQubits = 3 * W;         // a(0..2), b(3..5), a'(6..8)

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, kNumQubits);
    sturm_set_thread_context(ctx);

    // ── 2. Build two superposed qints that share no qubits ────────────────────
    using Q = sturm::qint_t<W>;
    Q a, b;
    a.value = 0; b.value = 0;
    a.super_mask = (1u << W) - 1u;
    b.super_mask = (1u << W) - 1u;
    for (int i = 0; i < (int)W; ++i) {
        a.qubits[i] = i;              // qubits 0..W-1
        b.qubits[i] = (int)W + i;     // qubits W..2W-1
    }
    
    sturm::qbool c(false);
    
    // ── 3. The high-level call: a |= b ─────────────────────────────────────
    // (operator| backend path is currently a stub — see file header note.)
    a & b;
//    a |= b;

    // ── 4. Manually emit the OR decomposition into the IR ─────────────────────
    // out register occupies qubits 2W..3W-1 (already allocated via kNumQubits).
    for (uint32_t i = 0; i < (uint32_t)W; ++i) {
        const uint32_t a_q   = (uint32_t)i;
        const uint32_t b_q   = (uint32_t)(W + i);
        const uint32_t out_q = (uint32_t)(2 * W + i);
        emit_or_bit(*ctx, a_q, b_q, out_q);
    }

    // ── 5. Print the recorded circuit ─────────────────────────────────────────
    std::printf("Recorded %zu gates for a |= b (W=%zu)\n\n",
                ctx->ir.size(), W);
    std::printf("Qubit layout: q0..q%zu = a, q%zu..q%zu = b, q%zu..q%zu = a' (new a)\n\n",
                W - 1, W, 2 * W - 1, 2 * W, 3 * W - 1);

    std::string diagram = sturm::draw_ascii(ctx->ir, kNumQubits);
    std::fputs(diagram.c_str(), stdout);

    // ── 6. Cleanup — prevent qint destructors from touching the pool ──────────
    a.qubits.fill(-1); a.super_mask = 0;
    b.qubits.fill(-1); b.super_mask = 0;

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
