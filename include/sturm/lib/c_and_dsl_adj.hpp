// c_and_dsl_adj.hpp — LO-1c (sturm-eum8): adjoint + STURM_REGISTER_ADJOINT for
// lib_c_AND_dsl.  Carved out of c_and_dsl.hpp under sturm-nmf1 to keep the
// forward header within its per-file LoC budget.
//
// Adjoint of lib_c_AND_dsl for the LO-2 rewrite's scope-exit cleanup
// (PRD §2.2/§2.3).  The forward sweep is a single CCX, which is its own
// adjoint (self-inverse).  The body is therefore identical to the forward —
// we register the pair explicitly so `invert(lib_c_AND_dsl)(...)` resolves
// at the cleanup call site rather than relying on call-site inlining.
//
// Auto-included from c_and_dsl.hpp at the bottom of that file so all callers
// of lib_c_AND_dsl pick up the registration without an extra #include.

#pragma once

#include "sturm/lib/c_and_dsl.hpp"
#include "sturm/routines/invert.hpp"

// Forward-declare BitProxy for the LO-1c adjoint registration (backend-only).
namespace sturm {
#ifdef STURM_BACKEND_ENABLED
struct BitProxy;
#endif
}  // namespace sturm

namespace sturm {

template <typename Bit>
inline void __lib_c_AND_dsl_adj(Bit& c0, Bit& c1, Bit& tgt) {
    tgt ^= (c0 & c1);   // CCX self-inverse: re-applying the sweep zeros tgt
}

} // namespace sturm

#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::lib_c_AND_dsl<sturm::BitProxy>,
                       sturm::__lib_c_AND_dsl_adj<sturm::BitProxy>)
#endif
