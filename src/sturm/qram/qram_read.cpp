// qram_read.cpp -- Counter-mode runtime body for the TU-private QROM /
// qreg dispatch helpers declared in `include/sturm/qram/qram_read.hpp`
// (sturm-u9ge.13 / Beat D1; refactored in sturm-2w6h.2 / Beat B1).
//
// PRD §11.2.2 / §11.2.7: the public `QRAM_read` overload set (D0a /
// §11.1.1) inspects the OR-reduction of `super_mask` across the
// container's elements at entry and routes to one of two TU-private
// helpers — `qram_read_qrom_impl` (all elements classical) or
// `qram_read_qreg_impl` (any element superposed). In counter mode
// (B1a default), each helper bumps the `qram_read` counter on the
// active sink via the `Sink::qram_read()` hook (the CounterSink
// override increments a `qram_read` slot in its counts map). Per
// the issue D1 description, both helpers route to the same counter
// in this beat — the gate-level QROM vs. qreg distinction is out
// of scope and lands with the gate-emission epic.
//
// ── B1 (sturm-2w6h.2) refactor ──────────────────────────────────────
// The two QROM/qreg helpers are now `inline template <std::size_t W>`
// in the header, taking the same `(const qint_t<W>* a, std::size_t n,
// const qint_t<W>& i, qint_t<W>& b)` quadruple as the public surface.
// The shared (non-template) `dispatch_common` helper here:
//   1. Bumps the path-specific split counter on the active sink
//      (`qrom_read()` / `qreg_read()` — surface from B0 / sturm-2w6h.1,
//      wired here in B4 / sturm-2w6h.6).
//   2. Bumps the umbrella thread-local `qram::g_qram_read_count` —
//      preserves the D1 contract pinned by
//      `tests/qram/test_qram_read_stub.cpp` and
//      `transpiler/tests/test_qram_e2e.cpp`.
//   3. Fires the test-only forwarding-trace hook if installed (used
//      by `tests/qram/test_qram_read_dispatch.cpp` to pin "args are
//      forwarded, not discarded").
//
// ── B4 (sturm-2w6h.6) wiring ───────────────────────────────────────
// The umbrella `Sink::qram_read()` hook is no longer fired from this
// shared helper — it is now bumped at the **public** `QRAM_read` /
// `__QRAM_read_adj` entry-points in
// `include/sturm/qram/qram_read.hpp`. Split + umbrella sink hooks
// fire from non-overlapping sites and cannot be double-counted, even
// though both bumps land exactly once per dispatched call.
//
// LoC budget: <= 200 (plan §1, §5 / B1).

#include "sturm/qram/qram_read.hpp"
#include "sturm/core/counter_sink.hpp"   // current_sink()

namespace sturm {
namespace _qram_detail {

// ── Test-only forwarding trace hook ─────────────────────────────────
// Thread-local function pointer the QROM/qreg helpers fire on each
// dispatched call. Production code never installs this hook — it
// only exists so the B1 dispatch test
// (`tests/qram/test_qram_read_dispatch.cpp`) can pin that the
// helpers actually receive their forwarded `(a, n, i, b)` quadruple
// rather than dropping it on the floor. `set_forwarding_trace`
// returns the previous hook so callers can stash + restore for
// nested scopes.
//
// The variable is `inline thread_local` only inside this TU (it is
// a TU-local static — not exposed to other compilation units). The
// `set_forwarding_trace` accessor is the sole public entry-point.
namespace {
    thread_local ForwardingTraceFn g_trace = nullptr;
}  // namespace

ForwardingTraceFn set_forwarding_trace(ForwardingTraceFn hook) noexcept {
    ForwardingTraceFn prev = g_trace;
    g_trace = hook;
    return prev;
}

// ── Shared dispatch body — split counter bump + trace ───────────────
// PRD §11.2.7: bumps the **path-specific split counter** on the active
// sink (`qrom_read()` for path tag 1, `qreg_read()` for path tag 2;
// surface lives on `Sink` from B0 / sturm-2w6h.1) AND the process-wide
// thread-local umbrella counter `qram::g_qram_read_count` (parallel to
// the alias-class `qint_alias_detail::g_measurement_count` shape) so
// tests that do not install a custom sink can still observe the
// dispatched-call increment. Then fires the forwarding-trace hook
// (test-only) with the path tag (1 = QROM, 2 = QREG) and the type-
// erased argument quadruple.
//
// ── B4 (sturm-2w6h.6) wiring ───────────────────────────────────────
// Per the issue's call-order spec the umbrella `current_sink()->
// qram_read()` hook is **no longer** fired from this shared helper —
// it is now bumped exactly once per dispatched read at the public
// `QRAM_read` (and `__QRAM_read_adj`) entry-points in
// `include/sturm/qram/qram_read.hpp`. That keeps the split + umbrella
// firing from non-overlapping sites, so they cannot be double-counted
// even though both bumps land per dispatched call.
//
// The thread-local `qram::bump_qram_read_count()` continues to fire
// here so the D1 contract pinned by `tests/qram/test_qram_read_stub.cpp`
// (one thread-local bump per dispatched call, regardless of which
// sink — if any — is installed) stays green.
//
// TODO(backend): emit the qreg SWAP-style fanout primitive stream
// when the v2 PRD opens the qreg path; v1 ships it as a counter-only
// stub (umbrella + qreg_read split bumps fire, no gates leak).
void dispatch_common(int path_tag,
                     const void* a0_addr,
                     std::size_t n,
                     const void* i_addr,
                     const void* b_addr) noexcept {
    // Split counter on the active sink (B0 surface; B4 wires it here).
    if (Sink* s = current_sink()) {
        if (path_tag == /*QROM*/ 1)      s->qrom_read();
        else if (path_tag == /*QREG*/ 2) s->qreg_read();
    }
    // Thread-local umbrella — preserves D1 contract.
    qram::bump_qram_read_count();
    if (auto h = g_trace) {
        h(a0_addr, n, i_addr, b_addr, path_tag);
    }
}

}  // namespace _qram_detail
}  // namespace sturm
