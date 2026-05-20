// qram_read.cpp -- Runtime-side dispatch body for the QRAM-read
// helpers declared in `include/sturm/qram/qram_read.hpp`.
//
// History: sturm-u9ge.13 D1 (counter-mode stub) → sturm-2w6h.2 B1
// (refactored to forward (a, n, i, b)) → sturm-2w6h.6 B4 (split
// telemetry counters wired) → **sturm-44bt.4 BB4** (umbrella sink
// hook moved to public entry-point; over-cap diagnostic landed).
//
// PRD §11.2.2 / §11.2.7: the public `QRAM_read` overloads inspect
// the OR-reduction of `super_mask` across the container at entry
// and dispatch via `_qram_detail::qram_read_dispatch<W, N>`. That
// helper calls into `dispatch_common` here, which:
//   1. Bumps the path-specific split counter on the active sink
//      (`qrom_read()` for path tag 1, `qreg_read()` for path tag 2).
//   2. Bumps the umbrella thread-local `qram::g_qram_read_count` —
//      preserves the D1 contract pinned by
//      `tests/qram/test_qram_read_stub.cpp` and
//      `transpiler/tests/test_qram_e2e.cpp`.
//   3. Fires the test-only forwarding-trace hook if installed.
//
// The umbrella `Sink::qram_read()` sink hook is bumped at the public
// entry-point (in `qram_read.hpp`) — split + umbrella sink hooks fire
// from non-overlapping sites so they cannot be double-counted.
//
// LoC budget: ≤ 200 (plan §1, §5 / BB4).

#include "sturm/qram/qram_read.hpp"
#include "sturm/core/counter_sink.hpp"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace sturm {
namespace _qram_detail {

// ── Test-only forwarding-trace hook ─────────────────────────────────
// Thread-local function pointer the dispatch helper fires on each
// call. Production code never installs. `set_forwarding_trace`
// returns the previous hook so callers can stash + restore for
// nested scopes.
namespace {
    thread_local ForwardingTraceFn g_trace = nullptr;
}  // namespace

ForwardingTraceFn set_forwarding_trace(ForwardingTraceFn hook) noexcept {
    ForwardingTraceFn prev = g_trace;
    g_trace = hook;
    return prev;
}

// ── Shared dispatch body — split counter bump + trace ───────────────
// PRD §11.2.7: bumps the path-specific split counter on the active
// sink (`qrom_read()` / `qreg_read()`) AND the process-wide
// thread-local umbrella counter `qram::g_qram_read_count`. Then
// fires the forwarding-trace hook (test-only) with the path tag
// (1 = QROM, 2 = QREG) and the type-erased argument quadruple.
//
// The umbrella `Sink::qram_read()` hook is NOT fired here — it
// fires at the public `QRAM_read` / `__QRAM_read_adj` entry-points
// so split + umbrella sink hooks land from non-overlapping sites
// and cannot be double-counted.
void dispatch_common(int path_tag,
                     const void* a0_addr,
                     std::size_t n,
                     const void* i_addr,
                     const void* b_addr) noexcept {
    if (Sink* s = current_sink()) {
        if (path_tag == /*QROM*/ 1)      s->qrom_read();
        else if (path_tag == /*QREG*/ 2) s->qreg_read();
    }
    qram::bump_qram_read_count();
    if (auto h = g_trace) {
        h(a0_addr, n, i_addr, b_addr, path_tag);
    }
}

// ── BB4 over-cap diagnostic (sturm-44bt.4) ──────────────────────────
// Fired by the pointer overload's switch-default when `n > 1024`
// (outside the unrolled BB family `{2, 4, 8, ..., 1024}` per PRD §7).
//
// Debug: `assert(false)` — aborts loudly. The release behaviour is
// documented as UB (Q5 of plan §5: no other QRAM helpers do runtime
// range checks; the BB body assumes the dispatch table covered it).
// To keep release-mode tests observable, the function ALSO fires the
// thread-local diagnose-trace hook (test-only, set via
// `set_n_over_cap_diagnose_trace`) BEFORE the assert so callers
// compiled with `NDEBUG` can pin the bump without aborting.
//
// The hook lives here (TU-private thread-local) for the same reason
// as `g_trace` — keeps the `Sink` ABI unchanged across the BB4
// landing, and lets a test subclass capture the over-cap event via
// a free function pointer rather than overriding a virtual method.
namespace {
    thread_local NOverCapDiagnoseFn g_n_over_cap_trace = nullptr;
}  // namespace

NOverCapDiagnoseFn set_n_over_cap_diagnose_trace(NOverCapDiagnoseFn hook) noexcept {
    NOverCapDiagnoseFn prev = g_n_over_cap_trace;
    g_n_over_cap_trace = hook;
    return prev;
}

void qram_read_n_over_cap_diagnose(std::size_t n) noexcept {
    if (auto h = g_n_over_cap_trace) {
        h(n);
    }
    // Loud stderr so a release-mode abort leaves a forensic trace;
    // the assert below fires in debug, the message stays available
    // in release for the sink-side counter test.
    std::fprintf(stderr,
        "qram_read_n_over_cap_diagnose: n=%zu exceeds the unrolled BB "
        "family cap of 1024 — recompile with a larger STURM_QRAM_NMAX "
        "(see qram_read.hpp dispatch table) or use a smaller container.\n",
        n);
    std::fflush(stderr);
    // Debug aborts; release is UB-with-counter per Q5 of plan §5.
    assert(false && "qram_read_n_over_cap_diagnose: n > 1024 dispatch cap");
}

}  // namespace _qram_detail
}  // namespace sturm
