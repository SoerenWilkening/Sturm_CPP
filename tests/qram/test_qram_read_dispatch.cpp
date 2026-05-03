// test_qram_read_dispatch.cpp -- sturm-2w6h.2 (Beat B1).
//
// Pins the refactor of `_qram_detail::qram_read_qrom_impl` and
// `qram_read_qreg_impl` from no-arg shims into templates parameterised
// on `(W)` taking `(const qint_t<W>* a, std::size_t n,
// const qint_t<W>& i, qint_t<W>& b)`. The three public `QRAM_read`
// overloads in `include/sturm/qram/qram_read.hpp` and the three
// `__QRAM_read_adj` overloads forward `(a, i, b)` (and `n`, inferred
// from `N` for the array shapes) to the helper instead of discarding
// them. Bodies remain counter-mode bumps in this beat — arguments
// are forwarded but unused, so observable behaviour is unchanged.
//
// Reference: docs/plan_qram_backend.md §5 Beat B1.
//
// Coverage matrix (per the issue's "what to ship" bullets):
//   T1: All three container shapes (std::array, C-array, pointer + n)
//       bump the umbrella `qram_read_count` exactly once per call.
//   T2: Mask-OR dispatch still routes correctly — set `super_mask` on
//       a middle element and assert the qreg path fires.
//   T3: `i.super_mask` unchanged across the call (regression guard for
//       the refactor; hard pin lands in B5).
//   T4: Argument forwarding observable: forwarding-trace callback
//       captures `&a[0]`, `n`, `&i`, `&b` seen by the helper.
//
// LoC budget: <= 250 (plan §1, §5 / B1).

#include "sturm/qram/qram_read.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/counter_sink.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

// P4a: classical `qint_t<W>` constructed from int64_t — fully
// classical (super_mask=0, no qubits allocated).
template <std::size_t W>
sturm::qint_t<W> make_classical(std::int64_t v) noexcept {
    return sturm::qint_t<W>(v);
}

// ── Forwarding-trace helper ─────────────────────────────────────────
// The B1 refactor exposes a TU-local thread-local trace callback in
// `_qram_detail` that the QROM/qreg helpers invoke with the args
// they received plus a path tag (1 = QROM, 2 = QREG). The test
// installs the callback and inspects the captured args after.
struct Trace {
    int          path  = 0;          // 1 = QROM, 2 = QREG, 0 = unset
    const void*  a0    = nullptr;
    std::size_t  n     = 0;
    const void*  i     = nullptr;
    const void*  b     = nullptr;
    std::size_t  calls = 0;
};

inline Trace& trace_slot() {
    static thread_local Trace t;
    return t;
}

inline void reset_trace() { trace_slot() = Trace{}; }

struct InstallTrace {
    InstallTrace() {
        sturm::_qram_detail::set_forwarding_trace(
            [](const void* a0, std::size_t n,
               const void* i_addr, const void* b_addr,
               int path_tag_in) {
                trace_slot().path = path_tag_in;
                trace_slot().a0   = a0;
                trace_slot().n    = n;
                trace_slot().i    = i_addr;
                trace_slot().b    = b_addr;
                ++trace_slot().calls;
            });
    }
    ~InstallTrace() { sturm::_qram_detail::set_forwarding_trace(nullptr); }
};

// Assert T1 (umbrella counter), T3 (i.super_mask), and T4 (args
// forwarded) for one call. `expected_path` is 1 = QROM, 2 = QREG.
template <std::size_t W>
static void check_call(int expected_path,
                       const sturm::CounterSink& sink,
                       const sturm::qint_t<W>& i,
                       std::uint64_t i_mask_before,
                       const void* expected_a0,
                       std::size_t expected_n,
                       const sturm::qint_t<W>& b) {
    assert(sturm::qram::qram_read_count() == 1u);   // T1 thread-local
    assert(sink.count("qram_read")        == 1u);   // T1 active sink
    assert(i.super_mask                   == i_mask_before);   // T3
    const auto& t = trace_slot();                                // T4
    assert(t.calls == 1);
    assert(t.path  == expected_path);
    assert(t.a0    == expected_a0);
    assert(t.n     == expected_n);
    assert(t.i     == static_cast<const void*>(&i));
    assert(t.b     == static_cast<const void*>(&b));
}

// ── T1 / T3 / T4: std::array shape ─────────────────────────────────
static void test_dispatch_std_array() {
    constexpr std::size_t W = 8, N = 4;
    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);
    InstallTrace trace_scope;
    sturm::qram::reset_qram_read_count();
    sink.reset();
    reset_trace();

    std::array<sturm::qint_t<W>, N> a{};
    for (std::size_t k = 0; k < N; ++k) a[k] = make_classical<W>(k + 1);
    sturm::qint_t<W> i = make_classical<W>(2);
    sturm::qint_t<W> b = make_classical<W>(0);
    const auto im = i.super_mask;

    sturm::QRAM_read(a, i, b);
    check_call(/*QROM*/ 1, sink, i, im, a.data(), N, b);
    sturm::qram::reset_qram_read_count();
}

// ── T1 / T3 / T4: C-array shape ────────────────────────────────────
static void test_dispatch_c_array() {
    constexpr std::size_t W = 8, N = 4;
    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);
    InstallTrace trace_scope;
    sturm::qram::reset_qram_read_count();
    sink.reset();
    reset_trace();

    sturm::qint_t<W> a[N]{};
    for (std::size_t k = 0; k < N; ++k) a[k] = make_classical<W>(k * 3);
    sturm::qint_t<W> i = make_classical<W>(1);
    sturm::qint_t<W> b = make_classical<W>(0);
    const auto im = i.super_mask;

    sturm::QRAM_read(a, i, b);
    check_call(/*QROM*/ 1, sink, i, im, &a[0], N, b);
    sturm::qram::reset_qram_read_count();
}

// ── T1 / T3 / T4: pointer shape ────────────────────────────────────
static void test_dispatch_pointer() {
    constexpr std::size_t W = 8, N = 4;
    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);
    InstallTrace trace_scope;
    sturm::qram::reset_qram_read_count();
    sink.reset();
    reset_trace();

    sturm::qint_t<W> storage[N]{};
    for (std::size_t k = 0; k < N; ++k) storage[k] = make_classical<W>(k + 7);
    const sturm::qint_t<W>* a = storage;
    sturm::qint_t<W> i = make_classical<W>(3);
    sturm::qint_t<W> b = make_classical<W>(0);
    const auto im = i.super_mask;

    sturm::QRAM_read(a, N, i, b);
    check_call(/*QROM*/ 1, sink, i, im, static_cast<const void*>(a), N, b);
    sturm::qram::reset_qram_read_count();
}

// ── T2: Mask-OR dispatch routes qreg on superposed slot, QROM otherwise
static void test_dispatch_qreg_routing_on_superposed_slot() {
    constexpr std::size_t W = 8, N = 4;
    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);
    InstallTrace trace_scope;
    sturm::qram::reset_qram_read_count();
    sink.reset();
    reset_trace();

    std::array<sturm::qint_t<W>, N> a{};
    for (std::size_t k = 0; k < N; ++k) a[k] = make_classical<W>(k);
    // Superpose the middle element (index 2): the dispatch's
    // any_super_mask must OR-reduce across slots and route to qreg.
    a[2].super_mask = 1ULL;
    sturm::qint_t<W> i = make_classical<W>(0);
    sturm::qint_t<W> b = make_classical<W>(0);

    sturm::QRAM_read(a, i, b);
    assert(sturm::qram::qram_read_count() == 1u);
    assert(trace_slot().path == 2);  // QREG fired

    // Inverse: classical container → QROM path.
    a[2].super_mask = 0ULL;
    reset_trace();
    sturm::qram::reset_qram_read_count();
    sink.reset();
    sturm::QRAM_read(a, i, b);
    assert(sturm::qram::qram_read_count() == 1u);
    assert(trace_slot().path == 1);  // QROM fired

    sturm::qram::reset_qram_read_count();
}

}  // namespace

int main() {
    test_dispatch_std_array();
    test_dispatch_c_array();
    test_dispatch_pointer();
    test_dispatch_qreg_routing_on_superposed_slot();
    std::puts("test_qram_read_dispatch: OK");
    return 0;
}
