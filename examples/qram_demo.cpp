#define STURM_BACKEND_ENABLED 1

#include "sturm/backend/draw_ascii.hpp"
#include "sturm/backend/exec_append.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qram/qram_read.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qint_alias.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

// QRAM demo — natural-syntax read `qint b = a[i];`, with the resulting
// gate stream printed as an ASCII circuit diagram.
//
// `add_quantum_executable()` routes this file through `sturm-transpile`,
// whose C1 matcher (`matcher_qram_subscript`) rewrites the read line into
// `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);` before codegen. The
// rewrite is observable: if it fires, the frontend qint's measurement
// counter stays at 0; if it does not, the converting ctor `qint(qint_t<W>)`
// runs and bumps it.
//
// With STURM_MODE_APPEND active, the QROM XOR-fanout DSL body emits its
// predicate-compute / WHEN-lifted CX / predicate-uncompute gate stream
// into `ctx->ir`, which we render with `sturm::draw_ascii` at the end.
//
//   cmake --build build --target example_qram_demo --parallel 6
//   ./build/examples/example_qram_demo
//
// Inspect the rewritten source at build/sturm_gen/examples/qram_demo.cpp.

// `using sturm::qint;` (here, or via `<sturm/prelude.hpp>`) is now sufficient:
// after sturm-65rs.6 (B1) the bare spelling `sturm::qint` resolves to the
// frontend alias class. Users who previously relied on the old `using qint =
// qint_t<64>;` for bit-level access on a backend type should re-spell as
// `sturm::qint_t<64>` -- see `docs/qram_user_intro.md` migration note +
// PRD §6 R3.
using sturm::qint;

namespace {

constexpr std::size_t W = 4;
constexpr std::size_t N = 4;

}  // namespace

int main() {
    
    // QROM path on classical i + classical a only allocates the `eq_k`
    // predicate ancilla and W=4 lazy qubits for `b`. 8 qubits is plenty.
    constexpr uint32_t kNumQubits = 8;
    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);
    
    qint a[4];
    for (int i = 0; i < 4; ++i) {
        a[i] = i;
    }

    qint i = 2;
    i[0].phi() += 3;
    i[1].phi() += 3;
    qint b = a[i];
    
    std::string diagram = sturm::draw_ascii(ctx->ir, kNumQubits);
    std::fputs("\n--- QRAM read circuit (APPEND-mode IR) ---\n", stdout);
    std::fputs(diagram.c_str(), stdout);
    std::fprintf(stdout, "\n[gate count = %zu]\n", ctx->ir.size());
    
    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
