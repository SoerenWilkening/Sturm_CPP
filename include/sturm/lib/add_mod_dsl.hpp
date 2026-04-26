// add_mod_dsl.hpp -- P0 scaffold (sturm-r5pn.1) for lib_add_mod_dsl.
//
// Stub forward primitive: out-of-place modular addition r = (a + b) mod n.
// Full algorithm lands in Phase 1 (sturm-yh3d). For now the body asserts
// false when invoked with a non-zero width; n == 0 short-circuits silently
// to keep stray callers from triggering accidental behavior.
//
// Sibling adjoint header is auto-included at the bottom (mirrors the
// mod_dsl.hpp / mod_dsl_adj.hpp pairing pattern).
//
// Target: <=30 LoC.

#pragma once

#include <cstddef>
#include <cassert>

namespace sturm {

template <typename Bit>
inline void lib_add_mod_dsl(Bit* /*a_bits*/, Bit* /*b_bits*/,
                            Bit* /*n_bits*/, std::size_t n,
                            Bit* /*r_bits*/) {
    if (n == 0u) return;
    assert(false && "lib_add_mod_dsl: not implemented (sturm-yh3d)");
}

} // namespace sturm

#include "sturm/lib/add_mod_dsl_adj.hpp"
