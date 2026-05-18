#pragma once
// sturm/draw_mermaid.h — Opt-in renderer feature header for the
// debug-print Mermaid format (sturm-5soh; PRD §7 follow-up, split out
// of sturm-03ew). Sibling to sturm/draw_ascii.h, sturm/draw_json.h, and
// sturm/draw_svg.h.
//
// Stability contract: debug-print format only. The emitted Mermaid
// markup may change freely between versions; no consumer outside the
// STURM tree should depend on it. This is a figure / GitHub-markdown
// generation aid (Mermaid renders natively in GitHub diffs and PR
// reviews), NOT an interchange format.
//
// Forwards `sturm/backend/draw_mermaid.hpp` so a TU that has already
// included the umbrella `sturm.h` can reach:
//   * the IR-taking overload
//     `sturm::draw_mermaid(const GateIR&, std::size_t)`
//     (header-only; used by `tests/backend/test_draw_mermaid.cpp`).
//   * the no-argument entry points `sturm::draw_mermaid()`,
//     `sturm::print_mermaid()` (parallel to PRD §5.6 for ASCII).
//     These are DECLARED in the forwarded backend header; their
//     DEFINITIONS live in `src/sturm/backend/draw_mermaid.cpp`.
//
// Forwarding posture mirrors `sturm/draw_ascii.h` / `sturm/draw_json.h`
// / `sturm/draw_svg.h`: declarations stay in `sturm/backend/` and the
// public-facing top-level header is a thin include.

#include "sturm/backend/draw_mermaid.hpp"
