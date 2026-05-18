// draw_mermaid.cpp — sturm-5soh / PRD §7 follow-up: no-argument entry
// points for the debug-print Mermaid renderer. Mirrors
// src/sturm/backend/draw_svg.cpp (sturm-l43b) and draw_json.cpp
// (sturm-a4we), which mirror src/sturm/backend/draw_ascii_noarg.cpp.
//
// Implements:
//   - sturm::draw_mermaid()   — render the calling thread's context IR as
//                               Mermaid (`graph LR`).
//   - sturm::print_mermaid()  — write draw_mermaid() to stdout.
//
// Both assert on a non-null per-thread context (parallel to the ASCII /
// JSON / SVG no-arg paths). The check uses
// `sturm::current_thread_context_or_null()` rather than
// `sturm_get_thread_context()`, because the latter falls back to the
// lazy process-wide default — which would mask the "no lifecycle
// installed" programming error the ASCII path also calls out.
//
// Canvas-width derivation mirrors `canvas_width` from `draw_svg.cpp` /
// `draw_json.cpp` / `draw_ascii_noarg.cpp`: width = max(qubit_index
// over all GateRecords) + 1, 0 for empty IR. Module size budget: ≤ 300
// LOC (well under).

#include "sturm/backend/draw_mermaid.hpp"
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
                  "did you call print_mermaid() / draw_mermaid() outside an "
                  "auto-injected main or explicit lifecycle?");
    (void)caller;
    return ctx;
}

} // namespace

std::string draw_mermaid() {
    sturm_backend_context_t* ctx = current_context_or_assert("draw_mermaid");
    const GateIR& ir = ctx->ir;
    std::size_t n = canvas_width(ir);
    return draw_mermaid(ir, n);
}

void print_mermaid() {
    (void)current_context_or_assert("print_mermaid");
    std::string s = draw_mermaid();
    if (!s.empty()) {
        std::fputs(s.c_str(), stdout);
    }
}

} // namespace sturm
