#define STURM_BACKEND_ENABLED 1

#include "sturm/core/counter_sink.hpp"
#include "sturm/core/recording_sink.hpp"
#include "sturm/qram/qram_read.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qint_alias.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>  // std::exit — sturm-ddgo regression hard-pin in [A]

// ─────────────────────────────────────────────────────────────────────────────
// QRAM (QROM path) demo — sturm-2w6h v1 + sturm-u9ge frontend
// ─────────────────────────────────────────────────────────────────────────────
//
// Two halves:
//
//   [A] User-facing source: `qint b = a[i];` — what an end user actually
//       writes. `add_quantum_executable()` routes this file through
//       `sturm-transpile`, whose C1 matcher (`matcher_qram_subscript`)
//       rewrites the line BEFORE compilation into the explicit
//       `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);` pair (PRD §8 /
//       archive/prd_qram_subscript.md §9 row 3). We can see the rewrite
//       fired by checking that the frontend `qint`'s measurement counter
//       stays at 0 across the call — if the matcher missed the site, the
//       converting constructor `qint(qint_t<W>)` would bump it to 1
//       (sturm-qjt7).
//
//   [B] Backend dispatch: what the rewrite lowers to. We call the runtime
//       `sturm::QRAM_read` directly and inspect the umbrella + split
//       telemetry counters and the high-level RecordingSink op stream.
//
// USER-FACING SOURCE SHAPES (all v1)
//   qint b = a[i];                 // C1, bare init   (sturm-u9ge.12)
//   b      = a[j];                 // H1, existing    (sturm-u9ge.6)
//   qint c = a[i] + d;             // H4, expr pos    (sturm-u9ge.9)
//
// COMPANION DOCS
//   - docs/qram_user_intro.md                          — prose intro
//   - transpiler/tests/fixtures/qram_read_*.cpp        — what the user writes
//   - transpiler/tests/fixtures/qram_read_*.expected.cpp — what the
//     transpiler emits for each container shape (std::array, C-array, ptr)
//   - tests/qram/test_qram_read_qrom_gates.cpp          — exact gate-budget
//     pin (CX/CCX/X counts) under a real simulator
//
// HOW TO RUN
//   cmake --build build --target example_qram_demo --parallel 6
//   ./build/examples/example_qram_demo
//
// HOW TO INSPECT THE TRANSPILED SOURCE
//   open build/sturm_gen/examples/qram_demo.cpp
//   The natural `qint b = a[i];` line in [A] will appear as
//   `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);`.

using qint = sturm::frontend::qint;

namespace {

constexpr std::size_t W = 4;  // qint width  (4 data bits per element)
constexpr std::size_t N = 4;  // container length (=> ceil_log2(N) = 2 address bits)

template <std::size_t Width>
sturm::qint_t<Width> qcl(std::int64_t v) noexcept {
    return sturm::qint_t<Width>(v);
}

// [A] ── User-facing source: `qint b = a[i];` parses against production ────
// The line below parses via the converting ctor `qint(const qint_t<W>&)`
// added in sturm-qjt7. The C1 matcher (matcher_qram_subscript.cpp,
// sturm-u9ge.12) REPLACES this line with
// `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);` before codegen, so
// the converting ctor body is unreachable on the rewrite path. The
// matcher fires against both the hermetic fixtures (transpiler/tests
// /fixtures/qram_read_*.cpp) AND the production headers (real
// `std::array` from `<array>` + real `frontend::qint` from
// `qint_alias.hpp` + real `qint_t<W>` from `qint_core.hpp`) once
// transpile_consumer.cpp registers the matcher pool (sturm-ddgo).
//
// Post-rewrite, `i` is still a `frontend::qint` (the matcher rewrites
// only the `qint b = a[i];` LINE, not the `i` declaration), so the
// runtime `QRAM_read` overload set in `qram_read.hpp` (also sturm-ddgo)
// includes a thin frontend::qint-accepting wrapper that forwards to
// the canonical `qint_t<W>`-indexed overload via the alias's
// non-measuring `classical_value()` accessor. The combination keeps
// `measurement_count() == 0` across the call.
void demo_natural_syntax() {
    using sturm::frontend::qint_alias_detail::measurement_count;
    using sturm::frontend::qint_alias_detail::reset_measurement_count;

    reset_measurement_count();
    const auto m_before = measurement_count();

    std::array<sturm::qint_t<W>, N> a = {
        qcl<W>(0xA), qcl<W>(0x5), qcl<W>(0xF), qcl<W>(0x0),
    };
    qint i = 2;  // frontend qint — has implicit `operator size_t()`
    qint b = a[i];
    (void)b;

    const auto m_after = measurement_count();
    std::printf("  measurement_count before = %zu\n", m_before);
    std::printf("  measurement_count after  = %zu  "
                "(0 ⇒ matcher rewrote the line — ctor body unreachable)\n",
                m_after);
    // Hard-pin: the rewrite contract is `measurement_count == 0`
    // post-call (sturm-ddgo). Treat any non-zero count as a regression.
    if (m_after != 0u) {
        std::fprintf(stderr,
                     "  FAIL: expected measurement_count == 0 but got %zu — "
                     "the C1 matcher did not rewrite `qint b = a[i];`. "
                     "Check transpile_consumer.cpp registers "
                     "register_qram_subscript_matcher and the QRAM matcher "
                     "TUs are linked into the production binary.\n",
                     m_after);
        std::exit(1);
    }
    reset_measurement_count();
}

// [B] ── Backend dispatch: telemetry on a single QRAM_read call ─────────
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

// [B'] ── High-level sink op stream for forward + adjoint ──────────────
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

}  // namespace

int main() {
    std::puts("[A] user-facing source `qint b = a[i];` (C1 rewrite):");
    demo_natural_syntax();

    std::puts("[B] backend dispatch — split-counter telemetry on QRAM_read:");
    demo_telemetry();

    std::puts("[B'] backend dispatch — sink op stream for forward + adjoint:");
    demo_recorded_ops();

    std::puts("done.");
    return 0;
}
