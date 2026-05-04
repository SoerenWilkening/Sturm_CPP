// tests/packaging/test_qint_alias_first_include.cpp — sturm-jysu acceptance.
//
// Pins the include-order invariant that the original B0 audit
// (sturm-65rs.5) and B1 acceptance test (test_qint_resolution.cpp) MISSED:
// when `qint_alias.hpp` is THE VERY FIRST sturm header included in a TU,
// the cycle qint_alias.hpp -> qint_fwd.hpp -> qint_alias.hpp (re-entry
// blocked by pragma once) must STILL leave qint_fwd.hpp's
// `using qint = ::sturm::frontend::qint;` parseable.
//
// Before the sturm-jysu fix, this TU failed to compile at qint_fwd.hpp:36
// with: "'frontend' in namespace 'sturm' does not name a type" — because
// step (4) of the qint_fwd.hpp include order ran with class
// sturm::frontend::qint not yet parsed (its definition was still
// suspended in qint_alias.hpp's first-pass include of qint_fwd.hpp).
//
// The fix forward-declares `class sturm::frontend::qint` in qint_fwd.hpp
// immediately before the using-declaration on line 36, which is sufficient
// for a using-decl introducing a type alias. This test pins that the
// include-order invariant holds going forward.
//
// LoC budget: <= 80 (issue spec).

// ── (1) qint_alias.hpp MUST be the first sturm header — load-bearing ────────
// Do NOT add any earlier #include "sturm/..." line above this one. The
// whole point of this test is the qint_alias.hpp-first include order.
#include "sturm/qtypes/qint_alias.hpp"

// Now we may pull in the rest. qint.hpp gives us the backend qint_t<W>
// definition needed for the negative static_assert below.
#include "sturm/qtypes/qint.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

// ── (2) Pin the post-B1 type identity is preserved under the alias-first
//        include order ─────────────────────────────────────────────────────
static_assert(std::is_same_v<::sturm::qint, ::sturm::frontend::qint>,
              "sturm::qint must resolve to sturm::frontend::qint even when "
              "qint_alias.hpp is the FIRST sturm header included (sturm-jysu).");

static_assert(!std::is_same_v<::sturm::qint, ::sturm::qint_t<64>>,
              "sturm::qint must NOT resolve to sturm::qint_t<64> after B1.");

// ── (3) Smoke: the implicit-conversion contract still works through the
//        bare sturm::qint spelling under this include order ───────────────
int main() {
    ::sturm::frontend::qint_alias_detail::reset_measurement_count();
    if (::sturm::frontend::qint_alias_detail::measurement_count() != 0) {
        return 1;
    }

    // Implicit ctor int64_t -> qint (P4a, free, no counter bump).
    ::sturm::qint q = 7;
    if (::sturm::frontend::qint_alias_detail::measurement_count() != 0) {
        return 2;
    }

    // Implicit operator size_t() — bumps the counter (PRD §4.1).
    std::size_t s = q;
    (void)s;
    if (::sturm::frontend::qint_alias_detail::measurement_count() != 1) {
        return 3;
    }

    return 0;
}
