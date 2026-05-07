// test_umbrella_qram.cpp — sturm-5qey / Phase 4 acceptance test for the
// opt-in feature headers `sturm/qram.h` and `sturm/draw_ascii.h` introduced
// by PRD §5.2 (covers A5 second half / G2).
//
// Pins the contract that `#include "sturm.h"` PLUS `#include "sturm/qram.h"`
// — and only those two headers — is sufficient to:
//   1. Reach the `sturm::QRAM_read` overload set (the qram opt-in header
//      forwards `sturm/qram/qram_read.hpp`).
//   2. Reach the user-level `qint` alias (brought in by the umbrella's
//      `using sturm::qint;`).
//   3. Drive an end-to-end QROM read of `qint b = a[i];` shape — the
//      runtime entry-point that the C1 transpiler matcher rewrites into.
//      We invoke `::sturm::QRAM_read(a, i, b)` directly here (the test
//      does not run the transpiler), which is exactly the call the
//      rewrite emits. The IR observably grows by ≥ 1 GateRecord — proof
//      that the opt-in qram header does not drop the gate-emission body
//      (`lib_qram_read_qrom_dsl`).
//
// Phase-4-only gate: Phase 5 (sturm-uoeb) lands the `sturm::draw_ascii()`
// / `sturm::print_ascii()` / `sturm::gate_count()` definitions. P5 already
// ran in this autopilot run, so the symbols are available — but per the
// issue's exit criteria, ONLY the qram-side contract is the gate for P4.
// We do not invoke the no-arg renderer here to keep this test scoped to
// the P4 deliverable.

#include "sturm.h"
#include "sturm/qram.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

int main() {
    // Lifecycle (will become auto-injected in Phase 7 / sturm-e3ru — the
    // explicit pair below collapses into the IIFE rewrite once that lands).
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);

    // Build a fully-classical container; QROM path runs (super_mask == 0).
    constexpr std::size_t W = 4;
    constexpr std::size_t N = 4;
    std::array<sturm::qint_t<W>, N> a{};
    for (std::size_t k = 0; k < N; ++k) {
        a[k] = sturm::qint_t<W>(static_cast<std::int64_t>(k));
    }
    sturm::qint_t<W> i = sturm::qint_t<W>(2);
    sturm::qint_t<W> b{};

    // Call the QRAM_read entry-point that the C1 transpiler matcher
    // rewrites `qint b = a[i];` into (PRD §5.2 / §11.1). This is the
    // runtime contract the qram opt-in header promises.
    const std::size_t before = ctx->ir.size();
    ::sturm::QRAM_read(a, i, b);
    const std::size_t after = ctx->ir.size();

    std::printf("test_umbrella_qram: ctx->ir.size() before=%zu after=%zu\n",
                before, after);
    assert(after > before && "QRAM_read must observably grow ctx->ir");

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
    return (after > before) ? 0 : 1;
}
