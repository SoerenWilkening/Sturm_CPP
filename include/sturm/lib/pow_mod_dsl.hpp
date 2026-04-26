// pow_mod_dsl.hpp -- P0 scaffold (sturm-r5pn.1) for lib_pow_mod_dsl.
//
// Stub forward primitive: out-of-place modular exponentiation
// r = (base ^ exp) mod n via repeated squaring. Full algorithm lands in
// Phase 3. The body asserts false when invoked with a non-zero width;
// n == 0 short-circuits silently to keep stray callers from triggering
// accidental behavior.
//
// Sibling adjoint header is auto-included at the bottom.
//
// Target: <=30 LoC.

#pragma once

#include <cstddef>
#include <cassert>

namespace sturm {

template <typename Bit>
inline void lib_pow_mod_dsl(Bit* /*base_bits*/, Bit* /*exp_bits*/,
                            Bit* /*n_bits*/, std::size_t n,
                            Bit* /*r_bits*/) {
    if (n == 0u) return;
    assert(false && "lib_pow_mod_dsl: not implemented (Phase 3)");
}

} // namespace sturm

#include "sturm/lib/pow_mod_dsl_adj.hpp"
