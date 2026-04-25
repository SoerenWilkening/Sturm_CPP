#pragma once
// qint.hpp — Umbrella header for the complete qint_t<Width> type.
// Step 6 Module F, spec §3, Implementation Plan §6.
//
// Include order:
//   A. qint_fwd.hpp   — forward declarations
//   B. qint_core.hpp  — class definition + member declarations
//   C. qint_arith.hpp — arithmetic operator bodies + compound assigns
//   D. qint_bitwise.hpp — bitwise operator bodies + compound assigns
//   E. qint_compare.hpp — comparison + subscript operator bodies
//
// After including this header the canonical alias is available:
//   using qint = qint_t<64>;
// and the free functions pow(qint, qint) and pow(qint, int64_t) are declared.

#include "sturm/qtypes/qint_fwd.hpp"
#include "sturm/qtypes/qint_core.hpp"      // qint_t<W> (forward-declares qbool)
#include "sturm/qtypes/qbool.hpp"           // qbool full definition
#include "sturm/qtypes/qint_qbool_conv.hpp" // qint_t<W>(const qbool&) and operator qbool()
#include "sturm/qtypes/qint_arith.hpp"
#include "sturm/qtypes/qint_bitwise.hpp"
#include "sturm/qtypes/qint_compare.hpp"

// sturm-czfi: Pulls in the LO-2 runtime helpers
// (sturm::swap, sturm::detail::{mul,and,or,divide}_oop and adjoints) the
// transpiler emits unqualified-by-ADL calls into. Backend-gated because
// `divide_oop.hpp` is.
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/qtypes/lossy_oop.hpp"
#endif

// qint alias is already defined in qint_fwd.hpp via:
//   using qint = qint_t<64>;
// pow overloads are defined in qint_arith.hpp.

// ── M25: Explicit template instantiation — extern declarations ────────────────
// instantiations.cpp provides explicit definition-strength instantiations of
// qint_t<4/8/16/32> for the backend build profile.  Any translation unit that
// is linked against that object can suppress its local copy by defining
// STURM_USE_EXPLICIT_INSTANTIATIONS before including this header (or via a
// target compile definition).
//
// Note: this suppression is opt-in so that existing backend test targets that
// do NOT link instantiations.cpp continue to instantiate the templates locally
// (the overhead is acceptable for test binaries).
#if defined(STURM_BACKEND_ENABLED) && defined(STURM_USE_EXPLICIT_INSTANTIATIONS)
extern template class sturm::qint_t<1>;
extern template class sturm::qint_t<4>;
extern template class sturm::qint_t<8>;
extern template class sturm::qint_t<16>;
extern template class sturm::qint_t<32>;
#endif  // STURM_BACKEND_ENABLED && STURM_USE_EXPLICIT_INSTANTIATIONS
