// explicit_lifecycle.cpp — Didactic counter-example demonstrating the
// `STURM_NO_AUTO_LIFECYCLE` escape hatch (PRD §5.4, OQ3, plan §3
// Phase 8 — sturm-yggr).
//
// Almost every other example under `examples/` lets the transpiler
// (`matcher_main_lifecycle`, sturm-e3ru) wrap `main` with
// `sturm_backend_create` / `sturm_set_thread_context` / matching
// teardown automatically — the user just writes quantum logic. This
// example deliberately opts OUT of the auto-injection so a user who
// needs runtime mode selection (or simply wants the explicit form)
// has a runnable reference for the call shape.
//
// The opt-out is one preprocessor symbol defined BEFORE including
// `sturm.h`: `STURM_NO_AUTO_LIFECYCLE`. With that symbol set,
// `matcher_main_lifecycle` skips this TU and the user-written
// `sturm_backend_create` / `sturm_backend_destroy` pair is the only
// lifecycle in play.
//
// HOW TO RUN
// ----------
//     ./build/examples/example_explicit_lifecycle
//
// HOW TO SEE THE TRANSPILER'S DECISION
// ------------------------------------
// After `cmake --build build --target example_explicit_lifecycle`,
// open `build/sturm_gen/examples/explicit_lifecycle.cpp`. The
// `int main()` body is identical to this source — no IIFE, no
// `__sturm_ctx` local. The auto-injection skipped the TU because
// `STURM_NO_AUTO_LIFECYCLE` was defined.

#define STURM_NO_AUTO_LIFECYCLE 1

#include "sturm.h"

#include <cstdio>

int main() {
    // Manual lifecycle — equivalent to what `matcher_main_lifecycle`
    // would have injected automatically. The mode is fixed here, but
    // a user who needs runtime selection (e.g. a CLI flag) can swap
    // the constant for any `sturm_mode_t` value at run time.
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);

    qint a = 7;
    (void)a;

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
