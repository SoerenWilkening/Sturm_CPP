// draw_json.cpp — sturm-a4we / PRD §7 follow-up: no-argument entry points
// for the debug-print JSON renderer. Mirrors src/sturm/backend/draw_ascii_noarg.cpp.
//
// Implements:
//   - sturm::draw_json()   — render the calling thread's context IR as JSON.
//   - sturm::print_json()  — write draw_json() to stdout.
//
// Both assert on a non-null per-thread context (parallel to the ASCII
// no-arg path). The check uses `sturm::current_thread_context_or_null()`
// rather than `sturm_get_thread_context()`, because the latter falls
// back to the lazy process-wide default — which would mask the
// "no lifecycle installed" programming error the ASCII path also calls
// out.
//
// Canvas-width derivation mirrors `canvas_width` from `draw_ascii_noarg.cpp`:
// width = max(qubit_index over all GateRecords) + 1, 0 for empty IR.
// Module size budget: ≤ 300 LOC (well under).

#include "sturm/backend/draw_json.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace sturm {

namespace {

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

sturm_backend_context_t* current_context_or_assert(const char* caller) {
    sturm_backend_context_t* ctx = current_thread_context_or_null();
    assert(ctx && "no active sturm thread context — "
                  "did you call print_json() / draw_json() outside an "
                  "auto-injected main or explicit lifecycle?");
    (void)caller;
    return ctx;
}

} // namespace

std::string draw_json() {
    sturm_backend_context_t* ctx = current_context_or_assert("draw_json");
    const GateIR& ir = ctx->ir;
    std::size_t n = canvas_width(ir);
    return draw_json(ir, n);
}

void print_json() {
    (void)current_context_or_assert("print_json");
    std::string s = draw_json();
    if (!s.empty()) {
        std::fputs(s.c_str(), stdout);
    }
}

} // namespace sturm
