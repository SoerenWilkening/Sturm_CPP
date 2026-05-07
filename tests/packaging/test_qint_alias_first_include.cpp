// tests/packaging/test_qint_alias_first_include.cpp — sturm-jysu acceptance
// + sturm-v0db.1 / W3.0 cycle pre-flight (plan §24).
//
// (sturm-jysu) Pins the include-order invariant the B0 audit
// (sturm-65rs.5) and B1 acceptance test (test_qint_resolution.cpp) MISSED:
// when `qint_alias.hpp` is THE VERY FIRST sturm header in a TU, the cycle
// qint_alias.hpp -> qint_fwd.hpp -> qint_alias.hpp (re-entry blocked by
// pragma once) must STILL leave qint_fwd.hpp's
// `using qint = ::sturm::frontend::qint;` parseable. Before the fix, this
// TU failed to compile at qint_fwd.hpp:36 with "'frontend' in namespace
// 'sturm' does not name a type" — qint_fwd.hpp ran with class
// sturm::frontend::qint not yet parsed. The fix forward-declares
// `class sturm::frontend::qint` in qint_fwd.hpp before the using-decl.
//
// (sturm-v0db.1 / W3.0) Pre-flights the W3.2 include shape: qbool.hpp will
// be added *inside* qint_alias_ops.hpp by W3.2 so every alias-touching TU
// drags in qbool transitively (PRD §10.3.4). This TU pre-flights the probe
// here (reverted at end of W3.0 in the sense that W3.2 moves the include
// into qint_alias_ops.hpp). Verifies the compound cycle path
//   qint_alias.hpp -> qint_fwd.hpp -> qint_alias.hpp,
// followed by qint_alias_ops.hpp -> qbool.hpp -> qint_core.hpp, parses
// cleanly under the alias-header-first order. PRD §10 G10 / B0 cycle
// re-run gate: qint_core.hpp makes no frontend:: references, so qbool
// (which inherits qint_t<1>) introduces no fold-back into the alias.

// (1) qint_alias.hpp MUST be the first sturm header — load-bearing.
#include "sturm/qtypes/qint_alias.hpp"
// (1b) W3.0: qint_alias_ops.hpp immediately after, with a W3.2-probe
// qbool.hpp include exercising the new compound cycle.
#include "sturm/qtypes/qint_alias_ops.hpp"
#include "sturm/qtypes/qbool.hpp"  // W3.2 probe (moves into _ops.hpp at W3.2).

#include "sturm/qtypes/qint.hpp"   // backend qint_t<W> for negative below.

#include <cstddef>
#include <cstdint>
#include <type_traits>

// (2) Post-B1 type identity preserved under alias-first include order.
static_assert(std::is_same_v<::sturm::qint, ::sturm::frontend::qint>,
              "sturm::qint must resolve to sturm::frontend::qint even when "
              "qint_alias.hpp is the FIRST sturm header (sturm-jysu).");
static_assert(!std::is_same_v<::sturm::qint, ::sturm::qint_t<64>>,
              "sturm::qint must NOT resolve to sturm::qint_t<64> after B1.");

// (2b) sturm-v0db.1 / W3.0: qbool reachable as a complete type under the
// alias-first order. If the compound cycle re-introduced an incomplete
// type at qbool's declaration, sizeof(qbool) would fail to compile.
static_assert(sizeof(::sturm::qbool) > 0,
              "sturm::qbool must be a complete type when qint_alias.hpp is "
              "the first sturm header (sturm-v0db.1 / W3.0 cycle pre-flight).");

// (3) Smoke: the implicit-conversion contract still works through the bare
// sturm::qint spelling under this include order.
int main() {
    ::sturm::frontend::qint_alias_detail::reset_measurement_count();
    if (::sturm::frontend::qint_alias_detail::measurement_count() != 0) {
        return 1;
    }
    ::sturm::qint q = 7;  // implicit ctor int64_t -> qint (P4a, no bump).
    if (::sturm::frontend::qint_alias_detail::measurement_count() != 0) {
        return 2;
    }
    std::size_t s = q;    // implicit operator size_t() — bumps the counter.
    (void)s;
    if (::sturm::frontend::qint_alias_detail::measurement_count() != 1) {
        return 3;
    }
    return 0;
}
