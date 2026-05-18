#pragma once
// sturm/draw_svg.h — Opt-in renderer feature header for the
// debug-print SVG format (sturm-l43b; PRD §7 follow-up, split out of
// sturm-03ew). Sibling to sturm/draw_ascii.h and sturm/draw_json.h.
//
// Stability contract: debug-print format only. The emitted SVG shape
// may change freely between versions; no consumer outside the STURM
// tree should depend on it. This is a figure / slide generation aid,
// not an interchange format.
//
// Forwards `sturm/backend/draw_svg.hpp` so a TU that has already
// included the umbrella `sturm.h` can reach:
//   * the IR-taking overload `sturm::draw_svg(const GateIR&, std::size_t)`
//     (header-only; used by `tests/backend/test_draw_svg.cpp`).
//   * the no-argument entry points `sturm::draw_svg()`,
//     `sturm::print_svg()` (parallel to PRD §5.6 for ASCII).
//     These are DECLARED in the forwarded backend header; their
//     DEFINITIONS live in `src/sturm/backend/draw_svg.cpp`.
//
// Forwarding posture mirrors `sturm/draw_ascii.h` and `sturm/draw_json.h`:
// declarations stay in `sturm/backend/` and the public-facing top-level
// header is a thin include.

#include "sturm/backend/draw_svg.hpp"
