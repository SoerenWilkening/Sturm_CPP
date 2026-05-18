// test_umbrella_draw_mermaid.cpp — sturm-5soh / PRD §7 follow-up packaging
// acceptance test for the opt-in feature header `sturm/draw_mermaid.h`
// (mirrors test_umbrella_only / test_umbrella_qram /
// test_umbrella_draw_json / test_umbrella_draw_svg).
//
// Pins the contract that `#include "sturm.h"` PLUS
// `#include "sturm/draw_mermaid.h"` — and only those two headers — is
// sufficient to:
//   1. Reach `sturm::draw_mermaid` / `sturm::print_mermaid` (the opt-in
//      header forwards `sturm/backend/draw_mermaid.hpp`).
//   2. Drive the no-argument path against the thread-local context
//      (the same posture `draw_ascii()` / `draw_json()` / `draw_svg()`
//      use; the Mermaid path is a sibling debug-print format with the
//      SAME no-arg surface).
//   3. Produce non-empty, well-formed Mermaid `graph LR` output for a
//      non-empty IR — proving the opt-in header does not drop a
//      transitive include needed by the renderer body.
//
// The packaging test does NOT pin the Mermaid schema (it is a debug-print
// format per the issue note, 2026-05-18). It only pins SHAPE: a `graph LR`
// envelope and rendered gate names.

#include "sturm.h"
#include "sturm/draw_mermaid.h"

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
    // (transitively via draw_mermaid.hpp's `sturm/backend/ir.hpp` include).
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

    std::string s = sturm::draw_mermaid();
    std::printf("test_umbrella_draw_mermaid: rendered %zu bytes\n", s.size());

    // Shape checks — debug-print format, not interchange. We do not pin
    // bytewise output (per stability contract).
    assert(!s.empty() && "non-empty IR must produce non-empty Mermaid output");
    assert(s.find("graph LR") != std::string::npos &&
           "Mermaid output must begin with the `graph LR` directive");
    // H gate renders as a node with label "H" on q0.
    assert(s.find("[\"H\"]") != std::string::npos);
    // CX renders split per-qubit: "*" control on q0, "X" target on q2.
    assert(s.find("[\"*\"]") != std::string::npos);
    assert(s.find("[\"X\"]") != std::string::npos);
    // First gate is on q0 → node id `g0_q0`. Second gate touches q0,q2 →
    // node ids `g1_q0`, `g1_q2`.
    assert(s.find("g0_q0") != std::string::npos);
    assert(s.find("g1_q0") != std::string::npos);
    assert(s.find("g1_q2") != std::string::npos);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
