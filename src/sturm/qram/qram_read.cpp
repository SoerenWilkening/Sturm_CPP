// qram_read.cpp -- Counter-mode runtime body for the TU-private QROM /
// qreg dispatch helpers declared in `include/sturm/qram/qram_read.hpp`
// (sturm-u9ge.13 / Beat D1).
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
// LoC budget: <= 200 (plan §1, §7 / D1).

#include "sturm/qram/qram_read.hpp"
#include "sturm/core/counter_sink.hpp"   // current_sink()

namespace sturm {
namespace _qram_detail {

// ── QROM path (all elements classical) ──────────────────────────────
// PRD §11.2.1: when every element of the container is fully classical
// (each `qint_t<W>::super_mask == 0`), the read lowers to a multiplexed
// XOR-fanout indexed by `i`'s qubits — `O(N · W)` Toffolis, no
// element-side ancilla, the index unmeasured. In this beat the body
// is a counter-mode stub: it bumps the active sink's `qram_read`
// counter exactly once per call AND a process-wide thread-local
// observability counter (parallel to the alias-class
// `qint_alias_detail::g_measurement_count` shape) so tests that do
// not install a custom sink can still observe the increment. Gate
// emission lands later.
//
// TODO(backend): emit the QROM XOR-fanout primitive stream once the
// gate-emission epic lands. Bump a separate `qrom_read` counter in
// addition to the umbrella `qram_read` counter at that point so
// tests can observe which path fired (PRD §11.2.7).
void qram_read_qrom_impl() noexcept {
    if (Sink* s = current_sink()) s->qram_read();
    qram::bump_qram_read_count();
}

// ── Quantum-register path (some element superposed) ─────────────────
// PRD §11.2.1: when at least one element of the container carries a
// superposed bit, the read lowers to a SWAP-style fanout (or its
// uncompute-paired cousin) controlled on `i`. The index is still read
// but unmeasured. In counter mode (B1a default), this beat routes
// the bump to the same `qram_read` counter as the QROM path; the
// per-path counter split (PRD §11.2.7's `qreg_read`) lands with gate
// emission.
//
// TODO(backend): emit the quantum-register SWAP fanout primitive
// stream once the gate-emission epic lands. Bump a separate
// `qreg_read` counter in addition to the umbrella `qram_read`
// counter at that point.
void qram_read_qreg_impl() noexcept {
    if (Sink* s = current_sink()) s->qram_read();
    qram::bump_qram_read_count();
}

}  // namespace _qram_detail
}  // namespace sturm
