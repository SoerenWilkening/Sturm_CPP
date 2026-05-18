#pragma once
// sturm/draw_json.h — Opt-in renderer feature header for the
// debug-print JSON format (sturm-a4we; PRD §7 follow-up, split out of
// sturm-03ew).
//
// Stability contract: debug-print format only. The emitted JSON shape
// may change freely between versions; no consumer outside the STURM
// tree should depend on it. The "schema_version" field is intentionally
// omitted — there is no stable schema.
//
// Forwards `sturm/backend/draw_json.hpp` so a TU that has already
// included the umbrella `sturm.h` can reach:
//   * the IR-taking overload `sturm::draw_json(const GateIR&, std::size_t)`
//     (header-only; used by `tests/backend/test_draw_json.cpp`).
//   * the no-argument entry points `sturm::draw_json()`,
//     `sturm::print_json()` (parallel to PRD §5.6 for ASCII).
//     These are DECLARED in the forwarded backend header; their
//     DEFINITIONS live in `src/sturm/backend/draw_json.cpp`.
//
// Forwarding posture mirrors `sturm/draw_ascii.h`: declarations stay in
// `sturm/backend/` and the public-facing top-level header is a thin
// include.

#include "sturm/backend/draw_json.hpp"
