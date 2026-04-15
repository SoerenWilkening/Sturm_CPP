#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qint.hpp"
#include "sturm/sturm.hpp"

#include <cstdint>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// Phase B: constant-operand compound-assign demo
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile recognizes each of the four compound-assign operators on
// qint_t<W> when the RHS is a classical constant lifted through the
// qint_t(long long) converting constructor, and injects the dual operator
// over the same verbatim token before the enclosing scope closes. LIFO
// order:
//
//   Source pattern   | Phase | Injected inverse
//   -----------------|-------|--------------------
//   a += <const>;    | PB-1  | a -= <const>;
//   a -= <const>;    | PB-2  | a += <const>;
//   a *= <const>;    | PB-3  | a /= <const>;
//   a /= <const>;    | PB-4  | a *= <const>;
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_constant_arith`, open:
//     build/sturm_gen/examples/constant_arith.cpp
// The four injected inverse lines appear just before the inner-block
// closing brace inside main(), in LIFO (reverse source) order.
//
// HOW TO RUN
// ----------
//     ./build/examples/example_constant_arith
//
// NOTE on STURM_AUTO_UNCOMPUTE: same caveat as example_or_circuit — the
// build defaults to ON, which layers the destructor's auto-uncompute on
// top of the transpiler's injection. To see ONLY the transpiler's
// contribution, configure with `-DSTURM_AUTO_UNCOMPUTE=OFF`.

int main() {
    constexpr uint32_t kNumQubits = 32;

    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND, kNumQubits);
    sturm_set_thread_context(ctx);

    // Inner scope so the transpiler-injected inverses fire BEFORE the
    // qint destructor runs. All four classical-const compound-assigns
    // keep `a` on the classical short-circuit path (both qubits[0] < 0
    // throughout) — value tracks 0 → 3 → 0 → 0 → 0 through the forward
    // chain, and the injected inverses keep it at 0 at scope exit.
    {
        sturm::qint_t<8> a;
        a += 3;
        a -= 3;
        a *= 3;
        a /= 3;

        // ↳ Transpiler injects four LIFO inverses just before the `}`
        //    below: the dual operator (/ ↔ *, + ↔ -) applied to the
        //    same verbatim RHS token. The exact emitted text is visible
        //    in build/sturm_gen/examples/constant_arith.cpp.
    }

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
