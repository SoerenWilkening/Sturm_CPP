#pragma once

// sturm.hpp — umbrella include for the STURM C++ quantum DSL front-end
// (Epic E3.M1 / PRD §3.3 / bd sturm-zmfk.1).
//
// This header re-exports the FULL public surface of the language:
//
//   * `sturm::qint_t<W>` / `sturm::qint`           — the W-bit quantum integer
//     class template (qtypes/qint_fwd.hpp + qtypes/qint_core.hpp +
//     qtypes/qint.hpp + the operator-overload trees)
//   * `sturm::qbool`                                — the quantum bool
//   * `WHEN(...)` macro                             — control-stack guard
//     (control/when.hpp; preprocessor symbol, global once this header is
//     included)
//   * `sturm::add_mod` / `mul_mod` / `pow_mod`      — modular-arithmetic
//     free functions emitted by the transpiler (PRD D2)
//   * `sturm::invert<&fn>()` and `STURM_REGISTER_ADJOINT`  — user-routine
//     adjoint registration (routines/invert.hpp)
//   * `sturm::uncompute_or` (and future `uncompute_*` siblings) — the
//     uncompute free-function API (uncompute/uncompute_api.hpp)
//   * `STURM_VERSION_*` macros                      — package version
//     (version.hpp, generated from version.hpp.in via configure_file)
//
// Per PRD D7 this header DOES NOT inject any names into the global
// namespace via `using` declarations. Users that want unprefixed
// `qint` / `qbool` should `#include <sturm/prelude.hpp>` instead
// (E3.M2). `WHEN` is a preprocessor macro and is unavoidably global.
//
// LOC budget: <= 100 (Plan §E3.M1). All entries must be EXPLICIT
// includes of public headers (no transitive-only reliance) so a
// reviewer / drift check can trace each public-API row in
// docs/transpiler_emit_targets.md to a line in this file.

// ── Versioning macros (E4.M1) ────────────────────────────────────────────────
// STURM_VERSION_{MAJOR,MINOR,PATCH,STRING}. Pulled in first so downstream
// `#if STURM_VERSION_MAJOR >= n` guards work even before any quantum
// type is named.
#include <sturm/version.hpp>

// ── qbool: quantum bool (PRD §3.3, D7) ───────────────────────────────────────
// Forward decl is what the transpiler's matchers reference in their
// diagnostic text (matcher_dropped_quantum_return.cpp); the full
// definition is needed for any TU that constructs a qbool.
#include <sturm/control/when_fwd.hpp>
#include <sturm/qtypes/qbool.hpp>
#include <sturm/qtypes/qbool_logic.hpp>
#include <sturm/qtypes/qbool_ops.hpp>

// ── qint: quantum W-bit integer (PRD §3.3) ───────────────────────────────────
// The canonical declaration tree per
// docs/transpiler_emit_targets.md is:
//   qint_fwd.hpp -> qint_core.hpp -> qint.hpp
// plus the arithmetic / bitwise / comparison / shift / qbool-conversion
// operator overload headers that round out the user-facing operator set.
#include <sturm/qtypes/qint_fwd.hpp>
#include <sturm/qtypes/qint_core.hpp>
#include <sturm/qtypes/qint.hpp>
#include <sturm/qtypes/qint_arith.hpp>
#include <sturm/qtypes/qint_arith_backend.hpp>
#include <sturm/qtypes/qint_arith_v3.hpp>
#include <sturm/qtypes/qint_bitwise.hpp>
#include <sturm/qtypes/qint_bitwise_backend.hpp>
#include <sturm/qtypes/qint_bitwise_v3.hpp>
#include <sturm/qtypes/qint_compare.hpp>
#include <sturm/qtypes/qint_compare_v3.hpp>
#include <sturm/qtypes/qint_shift_backend.hpp>
#include <sturm/qtypes/qint_qbool_conv.hpp>

// ── WHEN control-stack guard (PRD §3.3, D5) ──────────────────────────────────
// `control/when.hpp` defines the `WHEN(...)` preprocessor macro and the
// supporting `sturm::detail::when_guard`. `control/lift.hpp` carries
// the depth-1 lift helper used by adjoint emission.
#include <sturm/control/when.hpp>
#include <sturm/control/lift.hpp>

// ── Modular-arithmetic free functions (PRD D2 / §3.3) ────────────────────────
// `add_mod`, `mul_mod`, `pow_mod`. Transpiler matchers
// matcher_modular_*.cpp rewrite `(a + b) % n` etc. into calls to
// these symbols; they are part of the language ABI.
#include <sturm/ops/qint_modular.hpp>
#include <sturm/ops/lifted_primitives.hpp>

// ── Routine inversion (PRD §3.3) ─────────────────────────────────────────────
// `sturm::invert<&fn>()` plus `STURM_REGISTER_ADJOINT(fn, adj)`.
// Header-only; constexpr NTTP-keyed adjoint lookup.
#include <sturm/routines/invert.hpp>

// ── Uncompute free-function API (transpiler-MVP M3) ──────────────────────────
// `sturm::uncompute_or` (and future inverses added in post-MVP phases).
// Header-only surface; the implementation lives in
// src/sturm/uncompute/uncompute_api.cpp and must be linked by consumers
// that invoke any of these functions.
#include <sturm/uncompute/uncompute_api.hpp>
