#pragma once
// sturm/qram.h — Frontend simplification opt-in feature header
// (sturm-5qey / Phase 4 of docs/impl_plan_frontend_simplification.md;
// drives PRD §5.2 / G2, A5 second half).
//
// Pulls `sturm/qram/qram_read.hpp` so the `sturm::QRAM_read` /
// `__QRAM_read_adj` overload set is reachable from a TU that has only
// included the umbrella `sturm.h`. The C1 transpiler matcher
// (`matcher_qram_subscript`) rewrites the user-facing source shape
// `qint b = a[i];` (where `i` is a `sturm::frontend::qint`) into
// `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);` — this header is
// the runtime contract that rewrite depends on.
//
// LOC budget: ≤ 12 (plan §3 Phase 4).

#include "sturm/qram/qram_read.hpp"
