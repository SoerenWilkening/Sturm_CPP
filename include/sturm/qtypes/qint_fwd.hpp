#pragma once
// qint_fwd.hpp — Forward declaration of qint_t<Width> + canonical qint alias.
// Step 6 Module A, spec §3, Implementation Plan §6 (post B1: sturm-65rs.6).
//
// Include this header to break include cycles. After B1, the canonical
// `sturm::qint` is the FRONTEND alias class `sturm::frontend::qint` (not
// `qint_t<64>`). The PRD §4.2 / Plan §6 include order this file MUST follow:
//
//   (1) #pragma once
//   (2) forward declaration of `template <std::size_t Width = 64> class qint_t;`
//   (3) #include "sturm/qtypes/qint_alias.hpp"
//       (it includes qint_fwd.hpp back, but #pragma once + the forward decl
//        above keep that re-entry a safe no-op — qint_alias.hpp only ever
//        needs the forward declaration of qint_t<W>; the converting ctor
//        inline body never dereferences `src` at parse time.)
//   (4) namespace sturm { using qint = ::sturm::frontend::qint; }
//
// If steps (2) and (3) are inverted, qint_alias.hpp re-enters this header's
// #pragma once guard with no qint_t<W> forward declaration in scope, and
// the templated converting ctor in qint_alias.hpp fails to parse.

#include <cstddef>  // std::size_t
#include <cstdint>  // int64_t, uint64_t

// (2) Forward declaration of the backend template — MUST precede (3).
namespace sturm {
template <std::size_t Width = 64>
class qint_t;
} // namespace sturm

// (3) Pull in the frontend alias class definition.
#include "sturm/qtypes/qint_alias.hpp"

// (4) Re-export the frontend alias under the bare `sturm::qint` spelling.
namespace sturm {
using qint = ::sturm::frontend::qint;
} // namespace sturm
