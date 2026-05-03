// test_qram_split_counters.cpp -- sturm-2w6h.1 (Beat B0).
//
// Pins the split telemetry surface introduced on `Sink` for the QRAM
// backend gate-emission epic (PRD `docs/prd_qram_backend.md`; plan
// `docs/plan_qram_backend.md` §5 Beat B0).
//
// What this beat lands:
//   * Two new non-pure-virtual hooks on `Sink`:
//       virtual void qrom_read() {}
//       virtual void qreg_read() {}
//   * `CounterSink` overrides both, bumping `qrom_read` / `qreg_read`
//     counters in its existing counts map.
//   * `RecordingSink` overrides both, appending a `Record{op="qrom_read"}`
//     / `op="qreg_read"` (no qubit groups, control = -1).
//   * Existing umbrella `qram_read()` hook is preserved unchanged
//     (non-pure-virtual no-op on the base, bumps `qram_read` on the
//     CounterSink, bumps a record on the RecordingSink — kept for ABI
//     continuity per plan §5 B0).
//
// QROM/qreg dispatch helpers do NOT invoke the new hooks yet — that
// lands in B4 (sturm-2w6h.6). This test exercises the surface
// directly via `current_sink()->qrom_read()` / `qreg_read()`.
//
// Coverage matrix:
//   T1: CounterSink — `qrom_read` 0 → 1 after one `qrom_read()`,
//       `qreg_read` stays 0.
//   T2: CounterSink — symmetric for `qreg_read`.
//   T3: CounterSink — umbrella `qram_read` still observable (one call
//       bumps `qram_read` to 1, leaving split counters at 0).
//   T4: RecordingSink — `qrom_read()` appends one record with
//       `op="qrom_read"`, qubit_groups empty, control == -1.
//   T5: RecordingSink — `qreg_read()` appends one record with
//       `op="qreg_read"`.
//   T6: Default-installed sink (`current_sink()`): a direct call to
//       `current_sink()->qrom_read()` / `qreg_read()` is observable
//       on the locally-installed sink — proves `ScopedSink` routing
//       works for the new hooks.
//
// LoC budget: <= 200 (plan §1, §5 / B0).

#include "sturm/core/counter_sink.hpp"
#include "sturm/core/recording_sink.hpp"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <string>

namespace {

// ── T1 / T3: CounterSink — qrom_read split + umbrella coexistence ───
static void test_counter_sink_qrom_read_split() {
    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);

    // Initial state: every counter zero.
    assert(sink.count("qrom_read") == 0u);
    assert(sink.count("qreg_read") == 0u);
    assert(sink.count("qram_read") == 0u);

    // Single qrom_read bumps only the qrom_read slot.
    sturm::current_sink()->qrom_read();
    assert(sink.count("qrom_read") == 1u);
    assert(sink.count("qreg_read") == 0u);
    assert(sink.count("qram_read") == 0u);  // umbrella untouched

    // Idempotency check: a second qrom_read takes the count to 2 and
    // leaves the other counters alone — no aliasing in the bump path.
    sturm::current_sink()->qrom_read();
    assert(sink.count("qrom_read") == 2u);
    assert(sink.count("qreg_read") == 0u);
    assert(sink.count("qram_read") == 0u);
}

// ── T2: CounterSink — qreg_read split (symmetric) ──────────────────
static void test_counter_sink_qreg_read_split() {
    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);

    assert(sink.count("qreg_read") == 0u);
    assert(sink.count("qrom_read") == 0u);
    assert(sink.count("qram_read") == 0u);

    sturm::current_sink()->qreg_read();
    assert(sink.count("qreg_read") == 1u);
    assert(sink.count("qrom_read") == 0u);
    assert(sink.count("qram_read") == 0u);

    sturm::current_sink()->qreg_read();
    assert(sink.count("qreg_read") == 2u);
    assert(sink.count("qrom_read") == 0u);
    assert(sink.count("qram_read") == 0u);
}

// ── T3: CounterSink — umbrella qram_read still observable ──────────
// The plan promises ABI continuity — existing callers of
// `current_sink()->qram_read()` keep bumping the same `qram_read`
// counter; the new split hooks do not piggy-back on the umbrella.
static void test_counter_sink_umbrella_unchanged() {
    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);

    assert(sink.count("qram_read") == 0u);
    assert(sink.count("qrom_read") == 0u);
    assert(sink.count("qreg_read") == 0u);

    sturm::current_sink()->qram_read();
    assert(sink.count("qram_read") == 1u);
    assert(sink.count("qrom_read") == 0u);   // not aliased
    assert(sink.count("qreg_read") == 0u);   // not aliased

    // Mixing umbrella + split gives non-overlapping counters.
    sturm::current_sink()->qrom_read();
    sturm::current_sink()->qreg_read();
    sturm::current_sink()->qram_read();
    assert(sink.count("qram_read") == 2u);
    assert(sink.count("qrom_read") == 1u);
    assert(sink.count("qreg_read") == 1u);
}

// ── T4: RecordingSink — qrom_read appends one matching record ──────
static void test_recording_sink_qrom_read_record() {
    sturm::RecordingSink rec;
    sturm::ScopedSink scope(&rec);

    assert(rec.records().empty());

    sturm::current_sink()->qrom_read();
    assert(rec.records().size() == 1u);
    const auto& r = rec.records().back();
    assert(r.op == std::string("qrom_read"));
    assert(r.qubit_groups.empty());
    assert(r.scalars.empty());
    assert(r.control == -1);
}

// ── T5: RecordingSink — qreg_read appends one matching record ──────
static void test_recording_sink_qreg_read_record() {
    sturm::RecordingSink rec;
    sturm::ScopedSink scope(&rec);

    assert(rec.records().empty());

    sturm::current_sink()->qreg_read();
    assert(rec.records().size() == 1u);
    const auto& r = rec.records().back();
    assert(r.op == std::string("qreg_read"));
    assert(r.qubit_groups.empty());
    assert(r.scalars.empty());
    assert(r.control == -1);
}

// ── T6: ScopedSink routes the new hooks to the installed sink ──────
// Sanity: stacking two scoped sinks proves the new hooks dispatch
// through the same `current_sink()` indirection as every other op,
// rather than being routed via some ad-hoc free function.
static void test_scoped_sink_routes_split_hooks() {
    sturm::CounterSink outer;
    sturm::CounterSink inner;
    sturm::ScopedSink outer_scope(&outer);

    sturm::current_sink()->qrom_read();
    {
        sturm::ScopedSink inner_scope(&inner);
        sturm::current_sink()->qrom_read();
        sturm::current_sink()->qreg_read();
    }
    sturm::current_sink()->qreg_read();

    assert(outer.count("qrom_read") == 1u);
    assert(outer.count("qreg_read") == 1u);
    assert(inner.count("qrom_read") == 1u);
    assert(inner.count("qreg_read") == 1u);
}

}  // namespace

int main() {
    test_counter_sink_qrom_read_split();
    test_counter_sink_qreg_read_split();
    test_counter_sink_umbrella_unchanged();
    test_recording_sink_qrom_read_record();
    test_recording_sink_qreg_read_record();
    test_scoped_sink_routes_split_hooks();
    std::puts("test_qram_split_counters: OK");
    return 0;
}
