// qint_modular.hpp -- P0 scaffold (sturm-r5pn.1) for the public modular
// arithmetic free functions on qint_t (PRD §3.1, plan §6.2).
//
// add_mod / mul_mod / pow_mod return a fresh qint_t<W> holding the modular
// result. Full implementations land in Phase 4 (sturm-...) as thin
// allocate-then-call-lib_*_mod_dsl wrappers. For now the bodies short-circuit
// silently when W == 0 (matches the n == 0 contract) and assert false
// otherwise, so any inadvertent caller fails loudly.
//
// Target: <=30 LoC.

#pragma once

#include "sturm/qtypes/qint.hpp"
#include <cstddef>
#include <cassert>

namespace sturm {

template <std::size_t W>
qint_t<W> add_mod(const qint_t<W>& /*a*/, const qint_t<W>& /*b*/, const qint_t<W>& /*n*/) {
    if constexpr (W == 0u) return qint_t<W>{};
    assert(false && "add_mod: not implemented (Phase 4)");
    return qint_t<W>{};
}

template <std::size_t W>
qint_t<W> mul_mod(const qint_t<W>& /*a*/, const qint_t<W>& /*b*/, const qint_t<W>& /*n*/) {
    if constexpr (W == 0u) return qint_t<W>{};
    assert(false && "mul_mod: not implemented (Phase 4)");
    return qint_t<W>{};
}

template <std::size_t W>
qint_t<W> pow_mod(const qint_t<W>& /*base*/, const qint_t<W>& /*exp*/, const qint_t<W>& /*n*/) {
    if constexpr (W == 0u) return qint_t<W>{};
    assert(false && "pow_mod: not implemented (Phase 4)");
    return qint_t<W>{};
}

} // namespace sturm
