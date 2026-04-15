#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/draw_ascii.hpp"
#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"
// Pulls in the adjoint free-function API the transpiler injects below
// (the OR-uncompute helper, ...).
#include "sturm/sturm.hpp"

#include <cstdint>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// What the transpiler does (Phase A, as of 2026-04-15)
// ─────────────────────────────────────────────────────────────────────────────
//
// sturm-transpile parses this file with Clang LibTooling, recognizes the
// patterns below, and emits a sibling source file that has an inverse
// statement injected before each enclosing scope's closing brace, in LIFO
// order. Pattern coverage right now:
//
//   Source pattern              | Phase | Injected inverse
//   ----------------------------|-------|--------------------------
//   sturm::qbool c = a | b;     | MVP   | OR-uncompute helper call
//   sturm::qbool nc = ~c;       | PA-1  | nc = ~nc;
//   sturm::qbool x = a ^ b;     | PA-2  | x ^= a; x ^= b;
//   d ^= c;       (qbool RHS)   | PA-3  | d ^= c;
//   d ^= 1;       (classical)   | PA-4  | d ^= 1;
//
// PA-2 and PA-4 are exercised only by snapshot fixtures under
// tests/transpiler/fixtures/ — their forward operators (operator^ on
// qbool, qbool::operator^=(int)) aren't shipped on the real runtime yet,
// so they're not in this runnable demo.
//
// HOW TO SEE WHAT WAS INJECTED
// ----------------------------
// After `cmake --build build --target example_or_circuit`, open:
//     build/sturm_gen/examples/or_circuit.cpp
// The three injected inverse lines appear just before the inner-block
// closing brace inside main().
//
// HOW TO RUN
// ----------
//     ./build/examples/example_or_circuit
//
// NOTE on STURM_AUTO_UNCOMPUTE
// ----------------------------
// The build defaults to STURM_AUTO_UNCOMPUTE=ON, which makes the qbool
// destructor also emit an adjoint when each local goes out of scope. With
// BOTH the transpiler's injected inverse AND the destructor's auto-
// uncompute firing, each forward op is undone twice — a no-op for self-
// inverse gates, but doubles the gate count in the diagram. To see ONLY
// the transpiler's contribution:
//     cmake -B build -DSTURM_AUTO_UNCOMPUTE=OFF
//     cmake --build build --target example_or_circuit
// Phase K of the roadmap retires the runtime auto-uncompute layer; once
// landed, the transpiler is the only inversion path.

int main() {
    constexpr uint32_t kNumQubits = 32;

    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND, kNumQubits);
    sturm_set_thread_context(ctx);

    // ── Phase A demo block ──────────────────────────────────────────────────
    // Wrapped in an inner scope so the transpiler-injected inverses fire
    // BEFORE the diagram is printed below. If the demo lived directly in
    // main(), the inverses would be inserted after `return 0;` (dead
    // code), and the diagram would only show forward operations plus the
    // destructor auto-uncompute.
    {
        sturm::qbool a, b;
        a.value = 1;
        b.value = 0;
        a.theta() += 1;     // promote `a` into superposition

        // MVP: OR initializer.
        //   Forward: allocates ancilla c, emits {CX(a→c), CX(b→c), CCX(a,b,c)}.
        //   Injected: an OR-uncompute helper call — the runtime adjoint
        //   that genuinely returns c's qubit to |0⟩. (See the injected
        //   line in the sturm_gen/ sibling for the exact signature.)
        sturm::qbool c = a | b;

        // PA-1: NOT initializer.
        //   Forward: allocates ancilla nc, emits X on nc.
        //   Injected: `nc = ~nc;` — re-applies X. Note: this allocates a
        //   FRESH ancilla and reassigns nc; the old ancilla is released
        //   by qbool's destructor on overwrite. A later phase will refine
        //   NOT's emission to a true in-place X (no realloc).
        sturm::qbool nc = ~c;

        // PA-3: XOR-assign with quantum operand.
        //   Forward: emits CX(c → d).
        //   Injected: `d ^= c;` — second CX cancels the first, returning
        //   d to its pre-forward state. True self-inverse.
        sturm::qbool d;
        d.ensure_qubit();   // allocate d's qubit before the CX (operator^=
                            // asserts both operands have qubits).
        d ^= c;

        // ↳ Transpiler injects, in LIFO order, just before the `}` below:
        //     d  ^= c;
        //     nc  = ~nc;
        //     <OR-uncompute helper call for c from a,b>
    }

    std::string diagram = sturm::draw_ascii(ctx->ir, kNumQubits);
    std::fputs(diagram.c_str(), stdout);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
