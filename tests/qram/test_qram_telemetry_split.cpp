// test_qram_telemetry_split.cpp -- sturm-2w6h.6 (Beat B4).
//
// Pins the wiring of the split telemetry counters (`qrom_read` /
// `qreg_read`, landed in B0 / sturm-2w6h.1) through the QROM / qreg
// dispatch helpers (PRD `docs/prd_qram_backend.md` §11.2.7; plan
// `docs/plan_qram_backend.md` §5 Beat B4).
//
// Pins:
//   * QROM helper bumps `current_sink()->qrom_read()` per call.
//   * qreg helper bumps `current_sink()->qreg_read()` per call.
//   * Umbrella `current_sink()->qram_read()` continues to fire from
//     the public `QRAM_read` entry-point (so existing telemetry-shaped
//     tests stay green).
//   * Thread-local `qram::g_qram_read_count` continues to bump from
//     `dispatch_common` (preserves the D1 contract pinned by
//     `test_qram_read_stub.cpp`).
//
// Coverage matrix:
//   T1: counter-sink, classical → qrom=1, qreg=0, umbrella=1.
//   T2: counter-sink, superposed slot → qrom=0, qreg=1, umbrella=1.
//   T3: recording-sink, classical → 1 qrom_read record, 0 qreg_read.
//   T4: recording-sink, superposed → 1 qreg_read record, 0 qrom_read.
//   T5: multiple calls — split + umbrella stay aligned (no double-
//       count, no missed bumps; pins the issue's "non-overlapping
//       sites" guarantee).
//
// LoC budget: <= 200 (plan §1, §5 / B4).

// STURM_BACKEND_ENABLED mirrors `test_qram_read_stub.cpp` — pulls in
// the gate-emission DSL body alongside the new telemetry wiring. The
// test never asserts on emitted gates, only on the counter / record
// stream from the dispatch helpers.
#define STURM_BACKEND_ENABLED 1

#include "sturm/qram/qram_read.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/counter_sink.hpp"
#include "sturm/core/recording_sink.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

template <std::size_t W>
sturm::qint_t<W> qcl(std::int64_t v) noexcept { return sturm::qint_t<W>(v); }

// Count records of a given op name inside a RecordingSink's record list.
std::size_t count_records(const sturm::RecordingSink& rec,
                          const std::string& op) noexcept {
    std::size_t n = 0;
    for (const auto& r : rec.records()) if (r.op == op) ++n;
    return n;
}

// ── T1: Counter-sink, classical container → QROM split + umbrella ───
static void test_counter_sink_qrom_path_split() {
    using sturm::qram::reset_qram_read_count;
    using sturm::qram::qram_read_count;
    constexpr std::size_t W = 8, N = 4;

    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);
    reset_qram_read_count();

    std::array<sturm::qint_t<W>, N> a{};
    for (std::size_t k = 0; k < N; ++k) a[k] = qcl<W>(k + 1);
    sturm::qint_t<W> i = qcl<W>(2), b = qcl<W>(0);

    sturm::QRAM_read(a, i, b);

    assert(sink.count("qrom_read") == 1u);   // split fired (QROM)
    assert(sink.count("qreg_read") == 0u);   // qreg silent
    assert(sink.count("qram_read") == 1u);   // umbrella sink hook
    assert(qram_read_count() == 1u);         // umbrella thread-local

    reset_qram_read_count();
}

// ── T2: Counter-sink, superposed slot → qreg split + umbrella ──────
static void test_counter_sink_qreg_path_split() {
    using sturm::qram::reset_qram_read_count;
    using sturm::qram::qram_read_count;
    constexpr std::size_t W = 8, N = 4;

    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);
    reset_qram_read_count();

    std::array<sturm::qint_t<W>, N> a{};
    for (std::size_t k = 0; k < N; ++k) a[k] = qcl<W>(k);
    a[2].super_mask = 1ULL;  // mask-OR routes to qreg
    sturm::qint_t<W> i = qcl<W>(0), b = qcl<W>(0);

    sturm::QRAM_read(a, i, b);

    assert(sink.count("qreg_read") == 1u);   // split fired (qreg)
    assert(sink.count("qrom_read") == 0u);   // QROM silent
    assert(sink.count("qram_read") == 1u);   // umbrella sink hook
    assert(qram_read_count() == 1u);         // umbrella thread-local

    reset_qram_read_count();
}

// ── T3: Recording-sink, classical path → qrom_read record once ─────
static void test_recording_sink_qrom_path_record() {
    using sturm::qram::reset_qram_read_count;
    constexpr std::size_t W = 8, N = 4;

    sturm::RecordingSink rec;
    sturm::ScopedSink scope(&rec);
    reset_qram_read_count();

    std::array<sturm::qint_t<W>, N> a{};
    for (std::size_t k = 0; k < N; ++k) a[k] = qcl<W>(k);
    sturm::qint_t<W> i = qcl<W>(1), b = qcl<W>(0);

    sturm::QRAM_read(a, i, b);

    // RecordingSink overrides qrom/qreg_read to push a Record (B0
    // surface). Pin: split telemetry record appears exactly once
    // along the QROM path; the qreg counterpart never fires.
    assert(count_records(rec, "qrom_read") == 1u);
    assert(count_records(rec, "qreg_read") == 0u);

    reset_qram_read_count();
}

// ── T4: Recording-sink, superposed slot → qreg_read record once ────
static void test_recording_sink_qreg_path_record() {
    using sturm::qram::reset_qram_read_count;
    constexpr std::size_t W = 8, N = 4;

    sturm::RecordingSink rec;
    sturm::ScopedSink scope(&rec);
    reset_qram_read_count();

    std::array<sturm::qint_t<W>, N> a{};
    for (std::size_t k = 0; k < N; ++k) a[k] = qcl<W>(k);
    a[1].super_mask = 1ULL;  // superpose → qreg path
    sturm::qint_t<W> i = qcl<W>(0), b = qcl<W>(0);

    sturm::QRAM_read(a, i, b);

    assert(count_records(rec, "qreg_read") == 1u);
    assert(count_records(rec, "qrom_read") == 0u);

    reset_qram_read_count();
}

// ── T5: Multiple dispatched calls — split + umbrella stay aligned ──
// Two QROM calls then one qreg call: pins the issue's
// "non-overlapping sites" guarantee — no double-count, no missed bumps.
static void test_multiple_calls_align_split_and_umbrella() {
    using sturm::qram::reset_qram_read_count;
    using sturm::qram::qram_read_count;
    constexpr std::size_t W = 8, N = 4;

    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);
    reset_qram_read_count();

    std::array<sturm::qint_t<W>, N> a{};
    for (std::size_t k = 0; k < N; ++k) a[k] = qcl<W>(k);
    sturm::qint_t<W> i = qcl<W>(0), b = qcl<W>(0);

    sturm::QRAM_read(a, i, b);  // QROM #1
    assert(sink.count("qrom_read") == 1u && sink.count("qreg_read") == 0u);
    assert(sink.count("qram_read") == 1u && qram_read_count() == 1u);

    sturm::QRAM_read(a, i, b);  // QROM #2
    assert(sink.count("qrom_read") == 2u && sink.count("qreg_read") == 0u);
    assert(sink.count("qram_read") == 2u && qram_read_count() == 2u);

    a[2].super_mask = 1ULL;
    sturm::QRAM_read(a, i, b);  // qreg
    assert(sink.count("qrom_read") == 2u && sink.count("qreg_read") == 1u);
    assert(sink.count("qram_read") == 3u && qram_read_count() == 3u);

    reset_qram_read_count();
}

}  // namespace

int main() {
    test_counter_sink_qrom_path_split();
    test_counter_sink_qreg_path_split();
    test_recording_sink_qrom_path_record();
    test_recording_sink_qreg_path_record();
    test_multiple_calls_align_split_and_umbrella();
    std::puts("test_qram_telemetry_split: OK");
    return 0;
}
