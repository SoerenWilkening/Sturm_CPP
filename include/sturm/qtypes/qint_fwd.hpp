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
//
// The forward declaration of `sturm::frontend::qint` immediately below is
// load-bearing for the include-order case where qint_alias.hpp is the
// FIRST sturm header included (e.g. tests/qtypes/test_qint_alias.cpp:22 —
// fix for sturm-jysu, P1 bug). In that case the include cycle is:
//
//   qint_alias.hpp  (first include)
//     -> #pragma once stamps qint_alias.hpp
//     -> #include qint_fwd.hpp  (line 44 of qint_alias.hpp)
//          -> qint_t<W> forward decl above
//          -> #include qint_alias.hpp  (no-op via pragma once)
//          -> falls through to (4) here
//
// At this point class `sturm::frontend::qint` has NOT been parsed yet
// (qint_alias.hpp's class definition is still suspended in its first-pass
// include of qint_fwd.hpp). A bare `using qint = ::sturm::frontend::qint;`
// would therefore fail with "'frontend' in namespace 'sturm' does not
// name a type". The forward declaration is sufficient because a using-
// declaration that introduces a *type alias* only requires a type
// declaration in scope, not the full class definition. After the include
// graph unwinds, the full class definition lands and `sturm::qint` ends
// up as a complete type at every use site.
namespace sturm {
namespace frontend { class qint; }   // forward decl — sufficient for using-decl
using qint = ::sturm::frontend::qint;
} // namespace sturm
