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
#include "sturm/qtypes/qint_core.hpp"
#include "sturm/qtypes/qint_arith.hpp"
#include "sturm/qtypes/qint_bitwise.hpp"
#include "sturm/qtypes/qint_compare.hpp"

// qint alias is already defined in qint_fwd.hpp via:
//   using qint = qint_t<64>;
// pow overloads are defined in qint_arith.hpp.
