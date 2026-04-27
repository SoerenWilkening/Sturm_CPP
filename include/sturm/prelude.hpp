#pragma once

// prelude.hpp — opt-in unprefixed aliases for the STURM C++ public API
// (Epic E3.M2 / PRD §3.4 / bd sturm-zmfk.2).
//
// Consumers that prefer to write `qint`/`qbool` without the `sturm::`
// qualifier include THIS header instead of `<sturm/sturm.hpp>`. It pulls
// in the umbrella (so the full public surface is reachable as
// `sturm::...`) and then injects ONLY the two type-name aliases
// `qint` and `qbool` into the including TU's namespace.
//
// Per PRD D7 we deliberately do NOT `using` free functions
// (`add_mod`, `mul_mod`, `pow_mod`, `invert`, `uncompute_or`, …) —
// those stay `sturm::`-prefixed to avoid global-namespace pollution
// and ADL surprises. `WHEN` is a preprocessor macro and is already
// global once the umbrella is included.
//
// LOC budget: <= 30 (Plan §E3.M2).

#include <sturm/sturm.hpp>

using sturm::qint;
using sturm::qbool;
