#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/draw_ascii.hpp"
#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/control/when.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qint.hpp"
// Pulls in the adjoint free-function API the transpiler injects below
// (uncompute_or, uncompute_*_qint, ...).
#include "sturm/sturm.hpp"

#include <cstdint>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// Combined Phase A + B + C demo
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile walks this file with Clang LibTooling, recognizes the
// twelve forward patterns below, and injects an inverse statement for each
// one just before the enclosing scope closes. Inverses land in LIFO
// (reverse source) order, so the "rewind" runs Phase C → Phase B →
// Phase A.
//
//   Phase | Source pattern              | Injected inverse
//   ------|-----------------------------|----------------------------
//   MVP   | sturm::qbool qc  = qa | qb; | uncompute_or(qc, qa, qb);
//   PA-1  | sturm::qbool qnc = ~qc;     | qnc = ~qnc;
//   PA-3  | qd ^= qc;                   | qd ^= qc;
//   PB-1  | ia += 3;                    | ia -= 3;
//   PB-2  | ia -= 3;                    | ia += 3;
//   PB-3  | ia *= 3;                    | ia /= 3;
//   PB-4  | ia /= 3;                    | ia *= 3;
//   PC-1  | ca += cb;                   | uncompute_add_qint(ca, cb);
//   PC-2  | ca -= cb;                   | uncompute_sub_qint(ca, cb);
//   PC-3  | ca *= cb;                   | uncompute_mul_qint(ca, cb);
//   PC-4  | ca /= cb;                   | uncompute_div_qint(ca, cb);
//   PC-5  | ca %= cb;                   | uncompute_mod_qint(ca, cb);
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_phase_abc_demo`, open:
//     build/sturm_gen/examples/phase_abc_demo.cpp
// The twelve injected inverse lines appear just before the inner-block
// closing brace inside main(). uncompute_mod_qint is the first to fire;
// the MVP OR-uncompute helper is the last.
//
// HOW TO RUN
// ----------
//     ./build/examples/example_phase_abc_demo
//
// NOTE on STURM_AUTO_UNCOMPUTE
// ----------------------------
// The build defaults to STURM_AUTO_UNCOMPUTE=ON, which layers the qbool /
// qint destructor's auto-uncompute on top of the transpiler's injected
// inverses. To see ONLY the transpiler's contribution, configure with:
//     cmake -B build -DSTURM_AUTO_UNCOMPUTE=OFF
//     cmake --build build --target example_phase_abc_demo

int main() {
    constexpr uint32_t kNumQubits = 32;

    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND, kNumQubits);
    sturm_set_thread_context(ctx);

    // Inner scope so every transpiler-injected inverse fires BEFORE the
    // diagram is printed below. If the forward ops lived directly in
    // main(), the inverses would be inserted after `return 0;` — dead
    // code — and the ASCII diagram would only show forward gates plus
    // the destructor auto-uncompute trail.
    {
        // ── Phase A block: self-inverse qbool operations ────────────────────
        // qa starts in |1⟩ and is promoted into superposition so the OR
        // below isn't a classical short-circuit. qb stays in |0⟩.
        sturm::qbool qa, qb;
        qa.value = 1;
        qb.value = 0;
        qa.theta() += 1;

        // MVP: OR initializer — allocates ancilla qc, emits the 3-gate
        // OR sequence. Transpiler injects `uncompute_or(qc, qa, qb);`.
        sturm::qbool qc = qa | qb;

        // PA-1: NOT initializer — allocates qnc, emits X. Transpiler
        // injects `qnc = ~qnc;` (a second X, cancelling the first).
        sturm::qbool qnc = ~qc;

        // PA-3: XOR-assign with qbool RHS — emits CX(qc → qd).
        // Transpiler injects `qd ^= qc;` (second CX cancels the first).
        sturm::qbool qd;
        qd.ensure_qubit();
        qd ^= qc;

        // ── Phase B block: constant-operand compound-assigns on qint ────────
        // Classical short-circuit: ia stays on the non-backend path the
        // entire time (qubits[0] < 0 throughout), so the forward chain
        // evaluates purely classically: 0 → 3 → 0 → 0 → 0. Injected
        // LIFO inverses restore ia to 0 at scope exit.
        sturm::qint_t<8> ia;
        ia += 3;                          // PB-1 → inv: ia -= 3;
        ia -= 3;                          // PB-2 → inv: ia += 3;
        ia *= 3;                          // PB-3 → inv: ia /= 3;
        ia /= 3;                          // PB-4 → inv: ia *= 3;

        // ── Phase C block: qint-qint compound-assigns ───────────────────────
        // Values chosen to keep every forward op on the classical short-
        // circuit path with no divide-by-zero:
        //   ca = 6, cb = 2 → ca += cb (8) → -= cb (6) → *= cb (12)
        //                   → /= cb (6) → %= cb (0).
        // mod breaks round-trip by design — uncompute_mod_qint is a
        // documented stub; the other four inverses restore correctly
        // from that point.
        sturm::qint_t<8> ca;
        sturm::qint_t<8> cb;
        ca.value = 6;
        cb.value = 2;
        ca += cb;                         // PC-1 → inv: uncompute_add_qint
        ca -= cb;                         // PC-2 → inv: uncompute_sub_qint
        ca *= cb;                         // PC-3 → inv: uncompute_mul_qint
        ca /= cb;                         // PC-4 → inv: uncompute_div_qint
        ca %= cb;                         // PC-5 → inv: uncompute_mod_qint

        // ↳ Transpiler injects, in LIFO order, just before the `}` below:
        //     uncompute_mod_qint(ca, cb);   // PC-5
        //     uncompute_div_qint(ca, cb);   // PC-4
        //     uncompute_mul_qint(ca, cb);   // PC-3
        //     uncompute_sub_qint(ca, cb);   // PC-2
        //     uncompute_add_qint(ca, cb);   // PC-1
        //     ia *= 3;                      // PB-4
        //     ia /= 3;                      // PB-3
        //     ia += 3;                      // PB-2
        //     ia -= 3;                      // PB-1
        //     qd  ^= qc;                    // PA-3
        //     qnc  = ~qnc;                  // PA-1
        //     uncompute_or(qc, qa, qb);     // MVP
    }

    std::string diagram = sturm::draw_ascii(ctx->ir, kNumQubits);
    std::fputs(diagram.c_str(), stdout);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
