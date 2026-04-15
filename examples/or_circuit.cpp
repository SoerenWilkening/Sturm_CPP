#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/draw_ascii.hpp"
#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"
// Pulls in the adjoint free-function API the transpiler injects below.
#include "sturm/sturm.hpp"

#include <cstdint>
#include <cstdio>

// To SEE what the transpiler does, compare this file against the generated
// copy under build/sturm_gen/examples/or_circuit.cpp after a build. The
// transpiler fires on the canonical VarDecl pattern `sturm::qbool X = a | b;`
// and injects the matching adjoint call just before the enclosing scope's
// closing brace, in LIFO order.

int main() {
    // ── 1. Create an APPEND-mode backend context ──────────────────────────────
    constexpr std::size_t W = 3;                   // 3-bit qints, keeps diagram small
    constexpr uint32_t kNumQubits = 6 * W;         // a(0..2), b(3..5), a'(6..8)

    sturm_backend_context_t *ctx = sturm_backend_create(STURM_MODE_APPEND, kNumQubits);
    sturm_set_thread_context(ctx);

    // ── 2. Build two superposed qbools that share no qubits ───────────────────
    sturm::qbool a, b;
    a.value = 1;
    b.value = 1;
    a.theta() += 1;

    // ── 3. Declare an OR temporary — this is what the transpiler rewrites ────
    //    The initializer `a | b` matches the VarDecl pattern, so the
    //    generated sibling will have the adjoint call appended before the
    //    end of main().
    sturm::qbool c = a | b;

    std::string diagram = sturm::draw_ascii(ctx->ir, kNumQubits);
    std::fputs(diagram.c_str(), stdout);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}

