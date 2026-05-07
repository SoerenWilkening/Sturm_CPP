#pragma once
// sturm/draw_ascii.h — Frontend simplification opt-in feature header
// (sturm-5qey / Phase 4 of docs/impl_plan_frontend_simplification.md;
// drives PRD §5.2 / G2, A5 second half).
//
// Forwards `sturm/backend/draw_ascii.hpp` so a TU that has included the
// umbrella `sturm.h` can reach:
//   * the IR-taking overload `sturm::draw_ascii(const GateIR&, std::size_t)`
//     (header-only; tests still call this directly).
//   * the no-argument entry points `sturm::draw_ascii()`,
//     `sturm::print_ascii()`, `sturm::gate_count()` (PRD §5.6 / G6).
//     These are DECLARED in the forwarded backend header; their
//     DEFINITIONS live in `src/sturm/backend/draw_ascii_noarg.cpp`
//     (Phase 5 / sturm-uoeb). Phase 5 ran before this header in the
//     autopilot run — definitions are live.
//
// Forwarding posture (rather than re-declaring the no-arg symbols here)
// avoids duplicate-declaration noise: declarations and definitions both
// continue to live in the `sturm/backend/` namespace, and consumers who
// `#include <sturm/draw_ascii.h>` get the same surface.
//
// LOC budget: ≤ 25 (plan §3 Phase 4).

#include "sturm/backend/draw_ascii.hpp"
