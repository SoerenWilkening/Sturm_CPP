#define STURM_BACKEND_ENABLED 1

#include "sturm/core/counter_sink.hpp"
#include "sturm/core/recording_sink.hpp"
#include "sturm/qram/qram_read.hpp"
#include "sturm/qtypes/qint.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// QRAM (QROM path) demo — sturm-2w6h v1
// ─────────────────────────────────────────────────────────────────────────────
//
// Exercises the QRAM backend gate-emission entry-point
// `sturm::QRAM_read(a, i, b)` (`include/sturm/qram/qram_read.hpp`)
// against a fully-classical container `a` indexed by a fully-classical
// `qint_t<W>` index `i`. The demo focuses on what is OBSERVABLE without
// a live simulator: the dispatch path (QROM vs qreg), the umbrella +
// split telemetry counters, and the recorded high-level op stream.
//
// For value-side correctness (statevector P(b == a[i]) ≈ 1.0) and the
// exact CX / CCX / X gate-budget pin, see `tests/qram/test_qram_read_qrom_gates.cpp`
// (the B5 e2e test) — that test wires up the Orkan simulator and a
// real qubit pool so the QROM XOR-fanout actually computes.
//
// THE USER-FACING SOURCE SHAPE
// ----------------------------
// Most users will NOT call `QRAM_read` directly — they write
//
//     qint b = a[i];                 // C1, bare init   (sturm-u9ge.12)
//     b      = a[j];                 // H1, existing    (sturm-u9ge.6)
//     qint c = a[i] + d;             // H4, expr pos    (sturm-u9ge.9)
//
// and `sturm-transpile` rewrites each into the appropriate
// `::sturm::QRAM_read(a, …, b);` (+ adjoint, for H4) BEFORE compilation.
// See:
//   - docs/qram_user_intro.md                          — prose intro
//   - transpiler/tests/fixtures/qram_read_*.cpp        — what the user writes
//   - transpiler/tests/fixtures/qram_read_*.expected.cpp — what the
//     transpiler emits for each container shape (std::array, C-array, ptr)
//
// HOW TO RUN
// ----------
//     cmake --build build --target example_qram_demo --parallel 6
//     ./build/examples/example_qram_demo

namespace {

constexpr std::size_t W = 4;  // qint width  (4 data bits per element)
constexpr std::size_t N = 4;  // container length (=> ceil_log2(N) = 2 address bits)

template <std::size_t Width>
sturm::qint_t<Width> qcl(std::int64_t v) noexcept {
    return sturm::qint_t<Width>(v);
}

void demo_telemetry() {
    using sturm::qram::qram_read_count;
    using sturm::qram::reset_qram_read_count;

    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);
    reset_qram_read_count();

    std::array<sturm::qint_t<W>, N> a = {
        qcl<W>(0xA), qcl<W>(0x5), qcl<W>(0xF), qcl<W>(0x0),
    };
    sturm::qint_t<W> i = qcl<W>(2);  // classical index → QROM path
    sturm::qint_t<W> b = qcl<W>(0);

    sturm::QRAM_read(a, i, b);

    std::printf("  umbrella qram_read_count() = %zu  (one per QRAM_read)\n",
                qram_read_count());
    std::printf("  sink.count(\"qram_read\")    = %zu  (umbrella sink hook)\n",
                sink.count("qram_read"));
    std::printf("  sink.count(\"qrom_read\")    = %zu  (QROM path taken)\n",
                sink.count("qrom_read"));
    std::printf("  sink.count(\"qreg_read\")    = %zu  (qreg path NOT taken)\n",
                sink.count("qreg_read"));

    reset_qram_read_count();
}

void demo_recorded_ops() {
    sturm::RecordingSink rec;
    sturm::ScopedSink scope(&rec);

    std::array<sturm::qint_t<W>, N> a = {
        qcl<W>(0xA), qcl<W>(0x5), qcl<W>(0xF), qcl<W>(0x0),
    };
    sturm::qint_t<W> i = qcl<W>(2);
    sturm::qint_t<W> b = qcl<W>(0);

    sturm::QRAM_read(a, i, b);
    sturm::__QRAM_read_adj(a, i, b);

    std::printf("  forward + adjoint produced %zu sink record(s):\n",
                rec.records().size());
    for (const auto& r : rec.records()) {
        std::printf("    - %s\n", r.op.c_str());
    }
}

void demo_round_trip_counter() {
    using sturm::qram::qram_read_count;
    using sturm::qram::reset_qram_read_count;
    reset_qram_read_count();

    std::array<sturm::qint_t<W>, N> a = {
        qcl<W>(0xA), qcl<W>(0x5), qcl<W>(0xF), qcl<W>(0x0),
    };
    sturm::qint_t<W> i = qcl<W>(3);
    sturm::qint_t<W> b = qcl<W>(0);

    sturm::QRAM_read(a, i, b);
    const auto after_forward = qram_read_count();
    sturm::__QRAM_read_adj(a, i, b);
    const auto after_adjoint = qram_read_count();

    std::printf("  qram_read_count after forward = %zu\n",
                static_cast<std::size_t>(after_forward));
    std::printf("  qram_read_count after adjoint = %zu  (forward + adjoint = 2)\n",
                static_cast<std::size_t>(after_adjoint));

    reset_qram_read_count();
}

}  // namespace

int main() {
    std::puts("[1] split-counter telemetry on a single QRAM_read call:");
    demo_telemetry();

    std::puts("[2] high-level sink op stream for forward + adjoint:");
    demo_recorded_ops();

    std::puts("[3] umbrella counter pre/post round-trip:");
    demo_round_trip_counter();

    std::puts("done.");
    return 0;
}
