#pragma once
// sturm.h — Frontend simplification umbrella header (sturm-ilz3 / Phase 3 of
// docs/impl_plan_frontend_simplification.md, drives PRD §5.1 / G1).
//
// Single front-door header for the curated public API. A user TU that
// `#include "sturm.h"` reaches the qint / qbool surface (qtypes/qint.hpp
// + qint_alias.hpp + qbool.hpp) plus the C-ABI lifecycle (core/core.h +
// core/context.hpp). Heavier / output-only concerns (qram, draw_ascii)
// live behind the opt-in headers `sturm/qram.h` and `sturm/draw_ascii.h`
// (Phase 4 / sturm-5qey).
//
// Sentinels (PRD §5.1, OQ1 sentinel for Phase 7):
//   * STURM_BACKEND_ENABLED        — default-on; the new shape always pulls
//                                    the runtime (PRD §3 non-goal: backend-
//                                    disabled compilation goes away).
//   * the OQ1 umbrella sentinel    — the single, grep-able witness used
//                                    by the transpiler matcher
//                                    `matcher_main_lifecycle` (Phase 7 /
//                                    sturm-e3ru) to decide whether to
//                                    auto-inject the `sturm_backend_create
//                                    / destroy` pair around `int main(...)`.
//                                    Defined exactly once below — see the
//                                    annotated `#define` line.
//
// Auto-injection control-flow gaps (R7 mitigation; PRD §3 non-goals):
// The transpiler-injected lifecycle (Phase 7) wraps `int main(...)` with
// an IIFE that calls `sturm_backend_destroy` after the user body
// returns. That post-body teardown is SKIPPED on the following control-
// flow paths because they bypass normal C++ stack unwinding:
//   * `setjmp` / `longjmp` — non-local jumps out of the IIFE skip the
//     enclosing destructor sequence; the lambda's destructor never
//     fires and the post-call `sturm_backend_destroy` line is never
//     reached. Userland code that uses `<csetjmp>` from inside `main`
//     must call `sturm_backend_destroy` explicitly (or use the
//     `STURM_NO_AUTO_LIFECYCLE` escape hatch).
//   * `std::exit` / `std::_Exit` / `std::quick_exit` — process
//     termination skips the enclosing IIFE return path. Userland code
//     that calls these from inside `main` must perform any required
//     teardown explicitly.
//   * Uncaught exceptions — escape from the IIFE; the auto-injected
//     teardown does not fire. Catch and handle exceptions inside the
//     user body, or use the explicit lifecycle.
// These are the same gaps that already apply to any RAII guard in a
// C++ program; they are documented here so users diagnosing leaked
// `sturm_backend_context_t` heap allocations have a single reference.
//
// LOC budget: ≤ 35 (plan §3 Phase 3).

// Default-on backend (PRD §3 non-goal). Users who want to opt out of the
// runtime define the symbol to 0 BEFORE including this header.
#ifndef STURM_BACKEND_ENABLED
#define STURM_BACKEND_ENABLED 1
#endif

// OQ1 sentinel — a single, grep-able definition. The transpiler's
// matcher_main_lifecycle (Phase 7) checks for this macro to decide
// whether the user's TU includes the new umbrella.
#define STURM_UMBRELLA_INCLUDED 1

// ── Forwarded public surface (PRD §5.1) ──────────────────────────────────
// qint_alias.hpp is pulled in transitively by qint.hpp via qint_fwd.hpp,
// but we include it explicitly so a reviewer / drift-grep can trace the
// PRD §5.1 list directly to a line below.
#include "sturm/core/core.h"            // C ABI: sturm_backend_create / destroy / set_thread_context
#include "sturm/core/context.hpp"       // C++ side: BackendContext, execute_gate
#include "sturm/qtypes/qint.hpp"        // qint_t<W>, full operator surface
#include "sturm/qtypes/qint_alias.hpp"  // sturm::frontend::qint (the user-level alias)
#include "sturm/qtypes/qbool.hpp"       // sturm::qbool full definition

// Bring the user-level type names into the including TU's namespace so
// `qint a = 5;` and `qbool b;` parse without an explicit `using sturm::...`
// line (PRD §5.1).
using sturm::qint;
using sturm::qbool;
