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
// Phase D: qint-qint comparison demo
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile recognizes each of the six relational operators on
// qint_t<W> when both operands are bare DeclRefExprs to qint_t locals
// (no converting constructor fires) and the result is bound to a fresh
// `qbool` VarDecl. It then injects a free-function inverse call before
// the enclosing scope closes. LIFO order:
//
//   Source pattern               | Phase | Injected inverse
//   -----------------------------|-------|-------------------------------------
//   qbool c_eq = a == b;         | PD-1  | uncompute_eq_qint
//   qbool c_ne = a != b;         | PD-2  | uncompute_ne_qint
//   qbool c_lt = a <  b;         | PD-3  | uncompute_lt_qint
//   qbool c_le = a <= b;         | PD-4  | uncompute_le_qint
//   qbool c_gt = a >  b;         | PD-5  | uncompute_gt_qint
//   qbool c_ge = a >= b;         | PD-6  | uncompute_ge_qint
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_comparison`, open:
//     build/sturm_gen/examples/comparison.cpp
// The six injected inverse lines appear just before the inner-block
// closing brace inside main(), in LIFO (reverse source) order — so
// uncompute_ge_qint is first, uncompute_eq_qint is last.
//
// HOW TO RUN
// ----------
//     ./build/examples/example_comparison
//
// Phase K removed RAII auto-uncompute entirely; the transpiler is now
// the sole source of uncompute gate emission.

int main() {
    constexpr uint32_t kNumQubits = 32;

    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND, kNumQubits);
    sturm_set_thread_context(ctx);

    // Inner scope so the transpiler-injected inverses fire BEFORE the
    // qbool/qint destructors run. Both qints are left on the classical
    // short-circuit path (qubits[0] < 0 throughout), so every forward
    // comparison evaluates purely classically:
    //   a = 5, b = 7 → a == b: 0, a != b: 1, a <  b: 1,
    //                  a <= b: 1, a >  b: 0, a >= b: 0.
    {
        sturm::qint_t<8> a;
        sturm::qint_t<8> b;
        a.value = 5;
        b.value = 7;

        sturm::qbool c_eq = a == b;        // PD-1 → uncompute_eq_qint
        sturm::qbool c_ne = a != b;        // PD-2 → uncompute_ne_qint
        sturm::qbool c_lt = a <  b;        // PD-3 → uncompute_lt_qint
        sturm::qbool c_le = a <= b;        // PD-4 → uncompute_le_qint
        sturm::qbool c_gt = a >  b;        // PD-5 → uncompute_gt_qint
        sturm::qbool c_ge = a >= b;        // PD-6 → uncompute_ge_qint

        // ↳ Transpiler injects six LIFO inverses just before the `}`
        //    below (each on `(c_xx, a, b)`): uncompute_ge_qint,
        //    uncompute_gt_qint, uncompute_le_qint, uncompute_lt_qint,
        //    uncompute_ne_qint, uncompute_eq_qint. The exact emitted
        //    text is visible in build/sturm_gen/examples/comparison.cpp.
    }

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
