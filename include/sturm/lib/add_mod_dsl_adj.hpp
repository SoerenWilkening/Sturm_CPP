// add_mod_dsl_adj.hpp -- P0 scaffold (sturm-r5pn.1) for __lib_add_mod_dsl_adj.
//
// Stub adjoint sibling. Full implementation arrives in Phase 1 once the
// forward primitive is in place. The body asserts false when n != 0 and
// short-circuits silently otherwise, matching the forward stub.
//
// Auto-included from add_mod_dsl.hpp.

#pragma once

#include <cstddef>
#include <cassert>

namespace sturm {

template <typename Bit>
inline void __lib_add_mod_dsl_adj(Bit* /*a_bits*/, Bit* /*b_bits*/,
                                  Bit* /*n_bits*/, std::size_t n,
                                  Bit* /*r_bits*/) {
    if (n == 0u) return;
    assert(false && "__lib_add_mod_dsl_adj: not implemented (sturm-yh3d)");
}

} // namespace sturm
