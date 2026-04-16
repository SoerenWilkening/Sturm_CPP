#pragma once

// sturm.hpp — umbrella include for the STURM C++ quantum DSL front-end.
// This header grows as each step is implemented.

// ── Uncompute free-function API (transpiler-MVP M3) ──────────────────────────
// Exposes `sturm::uncompute_or` (and future inverses added in post-MVP phases
// A-D). Header-only surface: the implementation lives in
// src/sturm/uncompute/uncompute_api.cpp and must be linked by consumers that
// invoke any of these functions.
#include "sturm/uncompute/uncompute_api.hpp"

// ── User-defined-routine inversion (Phase I PI-0, bd sturm-hi66) ─────────────
// Exposes `sturm::invert(fn)` and the `STURM_REGISTER_ADJOINT(fn, adj)`
// macro that registers a forward/adjoint pair for a user-written routine.
// Header-only; zero runtime overhead.
#include "sturm/routines/invert.hpp"
