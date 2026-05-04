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

template<std::size_t Width>
sturm::qint_t<Width> qcl(std::int64_t v) noexcept {
    return sturm::qint_t<Width>(v);
}

}  // namespace

int main() {
    using sturm::frontend::qint_alias_detail::measurement_count;
    using sturm::frontend::qint_alias_detail::reset_measurement_count;
    
    reset_measurement_count();
    
    // QROM path on classical i + classical a only allocates the `eq_k`
    // predicate ancilla and W=4 lazy qubits for `b`. 8 qubits is plenty.
    constexpr uint32_t kNumQubits = 8;
    sturm_backend_context_t *ctx =
        sturm_backend_create(STURM_MODE_APPEND, kNumQubits);
    sturm_set_thread_context(ctx);
    
    std::array<sturm::qint_t<W>, N> a = {
        qcl<W>(0xA), qcl<W>(0x5), qcl<W>(0xF), qcl<W>(0x0),
    };
    qint i = 10;
    qint b = a[i];
    (void) b;
    
    
    const auto m = measurement_count();
    std::printf("measurement_count = %zu  (0 => matcher rewrote `qint b = a[i];`)\n", m);
    if (m != 0u) {
        std::fprintf(stderr,
                     "FAIL: expected measurement_count == 0 but got %zu — "
                     "the C1 matcher did not rewrite `qint b = a[i];`.\n",
                     m);
        sturm_set_thread_context(nullptr);
        sturm_backend_destroy(ctx);
        std::exit(1);
    }
    
    std::string diagram = sturm::draw_ascii(ctx->ir, kNumQubits);
    std::fputs("\n--- QRAM read circuit (APPEND-mode IR) ---\n", stdout);
    std::fputs(diagram.c_str(), stdout);
    std::fprintf(stdout, "\n[gate count = %zu]\n", ctx->ir.size());
    
    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return 0;
}
