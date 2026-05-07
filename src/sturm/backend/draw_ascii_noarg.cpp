// draw_ascii_noarg.cpp — Frontend simpl. P5 (sturm-uoeb): no-argument
// entry points for the ASCII renderer. PRD §5.6 / impl plan §3 P5.
//
// Implements:
//   - sturm::draw_ascii()   — render the calling thread's context IR.
//   - sturm::print_ascii()  — write draw_ascii() to stdout.
//   - sturm::gate_count()   — current context's ir.size() (cheap accessor).
//
// All three assert on a non-null per-thread context. The check is against
// sturm::current_thread_context_or_null() rather than sturm_get_thread_context(),
// because the latter always returns the lazy process-wide default — which
// would mask the "no lifecycle installed" programming error PRD §5.6 calls
// out as not recoverable.
//
// Canvas-width derivation:
//   width = (max referenced qubit index over all GateRecords) + 1
//   width = 0 (empty diagram) when the IR is empty.
// We enumerate the IROp/GateRecord variants exhaustively via
// GateRecord::n / GateRecord::qubits[0..n-1] (R2 mitigation in the impl plan
// risk register): every record stores its arity and the active qubit slots
// in the leading n positions, so a single linear scan over [0, n) covers
// all gate kinds without per-kind specialisation.
//
// Module size: ~120 LOC predicted; cap 300 LOC. If we ever exceed the cap,
// the impl plan §3 P5 split target is to pull the canvas-width scan into
// src/sturm/backend/draw_ascii_canvas.cpp.

#include "sturm/backend/draw_ascii.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace sturm {

namespace {

// ── canvas_width — max(qubit_index) + 1, 0 for empty IR ──────────────────────
//
// Single linear scan over IR records; for each record visit the first
// `rec.n` slots of `rec.qubits`. This is exhaustive across all sturm gate
// kinds because GateRecord normalises the active operands into the leading
// `n` slots regardless of kind (R2 in impl plan risk register).

std::size_t canvas_width(const GateIR& ir) noexcept {
    if (ir.size() == 0u) return 0u;
    uint32_t max_q = 0u;
    bool seen = false;
    for (std::size_t g = 0; g < ir.size(); ++g) {
        const GateRecord& rec = ir.at(g);
        for (uint8_t i = 0; i < rec.n; ++i) {
            if (!seen || rec.qubits[i] > max_q) {
                max_q = rec.qubits[i];
                seen = true;
            }
        }
    }
    if (!seen) return 0u;
    return static_cast<std::size_t>(max_q) + 1u;
}

// ── current_context — fetch + assert ─────────────────────────────────────────
//
// Resolves the calling thread's installed BackendContext, *without* the
// process-wide-default fallback. Asserts on null per PRD §5.6:
// "Calling them outside an auto-injected main … is a programming error,
//  not a recoverable condition."

sturm_backend_context_t* current_context_or_assert(const char* caller) {
    sturm_backend_context_t* ctx = current_thread_context_or_null();
    assert(ctx && "no active sturm thread context — "
                  "did you call print_ascii() / draw_ascii() / gate_count() "
                  "outside an auto-injected main or explicit lifecycle?");
    (void)caller; // Reserved for richer diagnostics.
    return ctx;
}

} // namespace

// ── sturm::draw_ascii() ──────────────────────────────────────────────────────

std::string draw_ascii() {
    sturm_backend_context_t* ctx = current_context_or_assert("draw_ascii");
    const GateIR& ir = ctx->ir;
    std::size_t n = canvas_width(ir);
    if (n == 0u) {
        // Empty IR locks PRD §5.6 ambiguity to the empty-string form
        // (impl plan §10: "empty string, not a zero-rail canvas").
        return std::string{};
    }
    return draw_ascii(ir, n);
}

// ── sturm::print_ascii() ─────────────────────────────────────────────────────
//
// Convenience: writes draw_ascii() to stdout. The renderer already emits
// trailing newlines for each rail, so we don't add another one here.

void print_ascii() {
    (void)current_context_or_assert("print_ascii");
    std::string s = draw_ascii();
    if (!s.empty()) {
        std::fputs(s.c_str(), stdout);
    }
}

// ── sturm::gate_count() ──────────────────────────────────────────────────────
//
// Returns the IR size for the calling thread's context. NOT the same as
// BackendContext::gate_count (which is a cumulative counter incremented by
// the dispatcher in execute_gate). PRD §5.6 specifies "current context's
// ir.size()", so this is what we expose to user code.

std::size_t gate_count() {
    sturm_backend_context_t* ctx = current_context_or_assert("gate_count");
    return ctx->ir.size();
}

} // namespace sturm
