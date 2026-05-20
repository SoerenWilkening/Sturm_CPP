// test_qram_read_bb_pointer_dispatch.cpp — sturm-44bt.4 (Beat BB4).
// Pointer-overload table coverage for `QRAM_read(a_ptr, n, i, b)`
// (plan `docs/plan_qram_backend_bb.md` §5 BB4 / PRD §7). Required
// assertions (issue sturm-44bt.4):
//   (4) for n ∈ {0,1,2,3,4,5,7,8,16,32,1023,1024}: umbrella bumps
//       once; split counter fires on correct arm (QROM vs qreg); no
//       `qram_read_n_over_cap_diagnose` fires. n=3,5,7,1023 exercise
//       phantom-padding routing (real padding test in BB5).
//   (5) over-cap n=1025: fires `qram_read_n_over_cap_diagnose`.
//       Debug: assert; release: sink-side error counter increments
//       (pinned via custom diagnose-trace hook here).
//   (6) forwarding pin (regression guard from B1): each call still
//       fires `set_forwarding_trace` with (a, n, i, b, path_tag).
//
// We compile this TU with `NDEBUG` so the over-cap diagnostic's
// `assert(false)` is suppressed — the test pins the RELEASE-mode
// "sink-side error counter increments" behaviour. Local checks use
// `CHECK(cond)` which always aborts on failure (NDEBUG-independent).
//
// LoC budget: ≤ 300 (plan §1, §5 / BB4).

#define STURM_BACKEND_ENABLED 1
#ifndef NDEBUG
#  define NDEBUG 1
#endif

#include "sturm/qram/qram_read.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/counter_sink.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "BB4 pointer_dispatch CHECK failed: %s " \
                                 "(file %s line %d)\n",                   \
                         #cond, __FILE__, __LINE__);                      \
            std::abort();                                                 \
        }                                                                 \
    } while (0)

namespace {

// Storage sized to the largest unrolled N (1024) so calls at n=3, 5,
// 7, 1023 routing to N'=4, 8, 8, 1024 do NOT read past the user's
// container — phantom-padding test in BB5 is the gate-count pin for
// these; here we only verify dispatch counters fire correctly.
constexpr std::size_t W = 2u;
constexpr std::size_t kStorage = 1024u;
sturm::qint_t<W> g_storage[kStorage];

// Forwarding-trace capture slot.
struct Trace {
    int          path  = 0;
    const void*  a0    = nullptr;
    std::size_t  n     = 0;
    const void*  i     = nullptr;
    const void*  b     = nullptr;
    std::size_t  calls = 0;
};
inline Trace& trace_slot() { static thread_local Trace t; return t; }
inline void reset_trace() { trace_slot() = Trace{}; }

struct InstallTrace {
    InstallTrace() {
        sturm::_qram_detail::set_forwarding_trace(
            [](const void* a0, std::size_t n,
               const void* i_addr, const void* b_addr,
               int path_tag) {
                trace_slot().path  = path_tag;
                trace_slot().a0    = a0;
                trace_slot().n     = n;
                trace_slot().i     = i_addr;
                trace_slot().b     = b_addr;
                ++trace_slot().calls;
            });
    }
    ~InstallTrace() { sturm::_qram_detail::set_forwarding_trace(nullptr); }
};

// Over-cap diagnose-trace capture.
struct OverCapCounter {
    std::size_t fires = 0;
    std::size_t last_n = 0;
};
inline OverCapCounter& over_cap_slot() {
    static thread_local OverCapCounter c; return c;
}
inline void reset_over_cap() { over_cap_slot() = OverCapCounter{}; }

struct InstallOverCap {
    InstallOverCap() {
        sturm::_qram_detail::set_n_over_cap_diagnose_trace(
            [](std::size_t n) {
                ++over_cap_slot().fires;
                over_cap_slot().last_n = n;
            });
    }
    ~InstallOverCap() { sturm::_qram_detail::set_n_over_cap_diagnose_trace(nullptr); }
};

// ── (4) + (6): table coverage + forwarding-trace pin ────────────────
// For each n in the family-arm sweep + the phantom-padding cases,
// call QRAM_read both with classical-only a (QROM arm) and with one
// slot's super_mask set (qreg arm). Assert umbrella + split counters
// + forwarding trace.
static void test_dispatch_table_and_forwarding() {
    using namespace sturm;
    InstallTrace trace_scope;
    InstallOverCap overcap_scope;

    constexpr std::size_t kNs[] = {0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u,
                                    16u, 32u, 1023u, 1024u};

    for (std::size_t arm : kNs) {
        // ── QROM sub-case: a fully classical (every slot.super_mask == 0).
        for (std::size_t k = 0; k < kStorage; ++k) g_storage[k] = qint_t<W>(0);

        CounterSink sink;
        ScopedSink scope(&sink);
        qram::reset_qram_read_count();
        reset_trace();
        reset_over_cap();

        qint_t<W> i_reg = qint_t<W>(0);
        qint_t<W> b_reg = qint_t<W>(0);
        const auto im_before = i_reg.super_mask;

        QRAM_read(g_storage, arm, i_reg, b_reg);

        // Umbrella + split + trace pins.
        CHECK(qram::qram_read_count() == 1u);
        CHECK(sink.count("qram_read") == 1u);
        CHECK(sink.count("qrom_read") == 1u);
        CHECK(sink.count("qreg_read") == 0u);
        CHECK(over_cap_slot().fires == 0u);
        // Forwarding trace pin: each in-range arm fires exactly once
        // (the body itself may recurse via dispatch_common, but only
        // one path_tag fires per dispatched call).
        CHECK(trace_slot().calls == 1u);
        CHECK(trace_slot().path == /*QROM*/ 1);
        CHECK(trace_slot().a0 == static_cast<const void*>(g_storage));
        CHECK(trace_slot().n == arm);
        CHECK(trace_slot().i == static_cast<const void*>(&i_reg));
        CHECK(trace_slot().b == static_cast<const void*>(&b_reg));
        CHECK(i_reg.super_mask == im_before);

        // ── qreg sub-case: set storage[0].super_mask = 1 (slot 0 only;
        // ensures qreg routing for every n >= 1). For n == 0 the OR-
        // reduce is over zero elements so it stays QROM.
        if (arm == 0u) {
            qram::reset_qram_read_count();
            continue;
        }
        g_storage[0].super_mask = 1ULL;

        CounterSink sink2;
        ScopedSink scope2(&sink2);
        qram::reset_qram_read_count();
        reset_trace();
        reset_over_cap();

        QRAM_read(g_storage, arm, i_reg, b_reg);

        CHECK(qram::qram_read_count() == 1u);
        CHECK(sink2.count("qram_read") == 1u);
        CHECK(sink2.count("qreg_read") == 1u);
        CHECK(sink2.count("qrom_read") == 0u);
        CHECK(over_cap_slot().fires == 0u);
        CHECK(trace_slot().calls == 1u);
        CHECK(trace_slot().path == /*QREG*/ 2);
        CHECK(trace_slot().n == arm);

        g_storage[0].super_mask = 0;
        qram::reset_qram_read_count();
    }
}

// ── (5) Over-cap diagnose pin ───────────────────────────────────────
// n=1025 triggers `qram_read_n_over_cap_diagnose` from the pointer
// overload's switch-default. Debug asserts (suppressed via NDEBUG at
// the top of this TU); release path increments the diagnose-trace
// counter we installed below. No split / umbrella counters fire
// because the diagnose returns BEFORE `dispatch_common` runs.
static void test_over_cap_diagnose() {
    using namespace sturm;
    InstallTrace trace_scope;
    InstallOverCap overcap_scope;

    for (std::size_t k = 0; k < kStorage; ++k) g_storage[k] = qint_t<W>(0);

    CounterSink sink;
    ScopedSink scope(&sink);
    qram::reset_qram_read_count();
    reset_trace();
    reset_over_cap();

    qint_t<W> i_reg = qint_t<W>(0);
    qint_t<W> b_reg = qint_t<W>(0);

    QRAM_read(g_storage, /*n=*/1025u, i_reg, b_reg);

    // Diagnose-trace fired exactly once with the right n.
    CHECK(over_cap_slot().fires == 1u);
    CHECK(over_cap_slot().last_n == 1025u);
    // dispatch_common is NOT invoked on the over-cap arm → no split
    // counter bump, no umbrella thread-local bump, no forwarding trace.
    CHECK(qram::qram_read_count() == 0u);
    CHECK(sink.count("qrom_read") == 0u);
    CHECK(sink.count("qreg_read") == 0u);
    // The umbrella sink hook DOES fire at the public entry-point
    // (before the switch table). This is the only counter that bumps
    // for an over-cap call — split + thread-local only fire inside
    // `dispatch_common`, which the switch-default skips.
    CHECK(sink.count("qram_read") == 1u);
    CHECK(trace_slot().calls == 0u);

    // Adjoint mirror — same behaviour on `__QRAM_read_adj`.
    sink.reset();
    qram::reset_qram_read_count();
    reset_trace();
    reset_over_cap();

    __QRAM_read_adj(g_storage, /*n=*/1025u, i_reg, b_reg);

    CHECK(over_cap_slot().fires == 1u);
    CHECK(over_cap_slot().last_n == 1025u);
    CHECK(qram::qram_read_count() == 0u);
    CHECK(sink.count("qrom_read") == 0u);
    CHECK(sink.count("qreg_read") == 0u);
    CHECK(sink.count("qram_read") == 1u);
    CHECK(trace_slot().calls == 0u);

    qram::reset_qram_read_count();
}

// ── Adjoint forwarding pin ──────────────────────────────────────────
// `__QRAM_read_adj` routes through `qram_read_dispatch_adj` which
// also calls `dispatch_common` — the forwarding trace must fire on
// the adjoint mirror too.
static void test_adjoint_forwarding() {
    using namespace sturm;
    InstallTrace trace_scope;
    InstallOverCap overcap_scope;

    for (std::size_t k = 0; k < kStorage; ++k) g_storage[k] = qint_t<W>(0);

    CounterSink sink;
    ScopedSink scope(&sink);
    qram::reset_qram_read_count();
    reset_trace();
    reset_over_cap();

    qint_t<W> i_reg = qint_t<W>(0);
    qint_t<W> b_reg = qint_t<W>(0);

    __QRAM_read_adj(g_storage, /*n=*/4u, i_reg, b_reg);

    CHECK(qram::qram_read_count() == 1u);
    CHECK(sink.count("qram_read") == 1u);
    CHECK(sink.count("qrom_read") == 1u);
    CHECK(over_cap_slot().fires == 0u);
    CHECK(trace_slot().calls == 1u);
    CHECK(trace_slot().path == 1);
    CHECK(trace_slot().n == 4u);
    CHECK(trace_slot().a0 == static_cast<const void*>(g_storage));
    CHECK(trace_slot().i  == static_cast<const void*>(&i_reg));
    CHECK(trace_slot().b  == static_cast<const void*>(&b_reg));

    qram::reset_qram_read_count();
}

}  // namespace

int main() {
    std::puts("sturm-44bt.4 Beat BB4: QRAM_read pointer-overload dispatch:");
    test_dispatch_table_and_forwarding();
    std::puts("  PASS: family-arm sweep n ∈ {0..1024} fires correct split + trace");
    test_over_cap_diagnose();
    std::puts("  PASS: n=1025 fires over-cap diagnose; no split/umbrella thread-local bump");
    test_adjoint_forwarding();
    std::puts("  PASS: __QRAM_read_adj mirror also fires dispatch_common + trace");
    std::puts("test_qram_read_bb_pointer_dispatch: OK");
    return 0;
}
