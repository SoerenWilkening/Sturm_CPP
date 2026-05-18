// test_umbrella_draw_svg.cpp — sturm-l43b / PRD §7 follow-up packaging
// acceptance test for the opt-in feature header `sturm/draw_svg.h`
// (mirrors test_umbrella_only / test_umbrella_qram / test_umbrella_draw_json).
//
// Pins the contract that `#include "sturm.h"` PLUS
// `#include "sturm/draw_svg.h"` — and only those two headers — is
// sufficient to:
//   1. Reach `sturm::draw_svg` / `sturm::print_svg` (the opt-in
//      header forwards `sturm/backend/draw_svg.hpp`).
//   2. Drive the no-argument path against the thread-local context
//      (the same posture `draw_ascii()` / `draw_json()` use; the SVG path
//      is a sibling debug-print format with the SAME no-arg surface).
//   3. Produce non-empty, well-formed SVG output for a non-empty IR —
//      proving the opt-in header does not drop a transitive include
//      needed by the renderer body.
//
// The packaging test does NOT pin the SVG schema (it is a debug-print
// format per the issue note, 2026-05-18). It only pins SHAPE: a `<svg`
// envelope and a matching `</svg>` close, plus the rendered gate names.

#include "sturm.h"
#include "sturm/draw_svg.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

int main() {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);

    // Append two gates directly (mirrors test_umbrella_only). The opt-in
    // header must reach the GateRecord / GateIR types from sturm.h
    // (transitively via draw_svg.hpp's `sturm/backend/ir.hpp` include).
    sturm::GateRecord h{};
    h.kind = STURM_GATE_H;
    h.qubits = {0u, 0u, 0u};
    h.n = 1u;
    h.param = 0.0;
    ctx->ir.append(h);

    sturm::GateRecord cx{};
    cx.kind = STURM_GATE_CX;
    cx.qubits = {0u, 2u, 0u};
    cx.n = 2u;
    cx.param = 0.0;
    ctx->ir.append(cx);

    std::string s = sturm::draw_svg();
    std::printf("test_umbrella_draw_svg: rendered %zu bytes\n", s.size());

    // Shape checks — debug-print format, not interchange. We do not pin
    // bytewise output (per stability contract).
    assert(!s.empty() && "non-empty IR must produce non-empty SVG");
    assert(s.find("<svg")   != std::string::npos);
    assert(s.find("</svg>") != std::string::npos);
    assert(s.find(">H<")    != std::string::npos);
    assert(s.find(">X<")    != std::string::npos);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
