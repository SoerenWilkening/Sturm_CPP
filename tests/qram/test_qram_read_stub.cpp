// test_qram_read_stub.cpp -- sturm-u9ge.13 (Beat D1).
//
// Pins the runtime contract for `sturm::QRAM_read` introduced in
// `include/sturm/qram/qram_read.hpp` (PRD §8 / §11.1 / §11.2 / §11.4;
// plan §7 / D1).
//
// Coverage (per the issue's "what to ship" bullets):
//   1. For each of the THREE container shapes (PRD §7 / D0a):
//        (a) std::array<qint_t<W>, N>
//        (b) qint_t<W>(&)[N]                  (C-array reference)
//        (c) qint_t<W>*                       (pointer arm + length n)
//      construct + populate (P4a classical init), invoke
//      `QRAM_read(a, i, b)` (or `(a, n, i, b)`) and assert:
//        * the `qram_read` counter incremented by exactly 1 per call.
//        * the index `i`'s super_mask is unchanged across the call
//          (the read does not measure i — PRD §11.2.1).
//   2. Adjoint round-trip via the existing P9 lookup machinery
//      (`sturm::invert<&fn>()(args)`). The `STURM_REGISTER_ADJOINT`
//      macro keys on the address-of-template-id, which is unambiguous
//      for the pointer overload (template head `<W>`) — registered in
//      the header. The std::array / C-array overloads share the head
//      `<W, N>`, which makes the address-of-overloaded-template-id
//      genuinely ambiguous in standard C++; those registrations are
//      deferred to a follow-up beat (qram_read.hpp's TODO note). The
//      pointer-arm round-trip below exercises the full registration
//      + invert<&fn>() pipeline; the adjoint bodies for the array
//      shapes are still defined and callable directly.
//
// LoC budget: <= 200 (plan §1, §7 / D1).

// `STURM_REGISTER_ADJOINT(QRAM_read<W>, __QRAM_read_adj<W>)` in the
// header is gated on `STURM_BACKEND_ENABLED` per the existing
// pattern; defining the macro here pulls the trait specialisation
// into this TU so `invert<&QRAM_read<W>>()` resolves at compile time.
#define STURM_BACKEND_ENABLED 1

#include "sturm/qram/qram_read.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/routines/invert.hpp"
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

// ── (1a) std::array container shape — D0a §11.1.1 overload 1 ────────
static void test_qram_read_std_array() {
    using sturm::qram::qram_read_count;
    using sturm::qram::reset_qram_read_count;
    constexpr std::size_t W = 8;
    constexpr std::size_t N = 4;

    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);

    reset_qram_read_count();
    assert(qram_read_count() == 0u);
    assert(sink.count("qram_read") == 0u);

    std::array<sturm::qint_t<W>, N> a{};
    for (std::size_t k = 0; k < N; ++k) {
        a[k] = make_classical<W>(static_cast<std::int64_t>(k));
    }
    sturm::qint_t<W> i = make_classical<W>(2);
    sturm::qint_t<W> b = make_classical<W>(0);

    const auto i_mask_before = i.super_mask;
    sturm::QRAM_read(a, i, b);
    assert(qram_read_count() == 1u);                  // thread-local
    assert(sink.count("qram_read") == 1u);            // active sink
    assert(i.super_mask == i_mask_before);            // P2 / §11.2.1

    // Adjoint callable directly (no invert<> lookup for this shape, see
    // file-header note). Counter bumps in the adjoint body match the
    // forward (D1 stub).
    sturm::__QRAM_read_adj(a, i, b);
    assert(qram_read_count() == 2u);
    assert(sink.count("qram_read") == 2u);
    assert(i.super_mask == i_mask_before);

    reset_qram_read_count();
}

// ── (1b) C-array container shape — D0a §11.1.1 overload 2 ───────────
static void test_qram_read_c_array() {
    using sturm::qram::qram_read_count;
    using sturm::qram::reset_qram_read_count;
    constexpr std::size_t W = 8;
    constexpr std::size_t N = 4;

    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);
    reset_qram_read_count();

    sturm::qint_t<W> a[N]{};
    for (std::size_t k = 0; k < N; ++k) {
        a[k] = make_classical<W>(static_cast<std::int64_t>(k * 2));
    }
    sturm::qint_t<W> i = make_classical<W>(1);
    sturm::qint_t<W> b = make_classical<W>(0);

    const auto i_mask_before = i.super_mask;
    sturm::QRAM_read(a, i, b);
    assert(qram_read_count() == 1u);
    assert(sink.count("qram_read") == 1u);
    assert(i.super_mask == i_mask_before);

    sturm::__QRAM_read_adj(a, i, b);
    assert(qram_read_count() == 2u);
    assert(sink.count("qram_read") == 2u);
    assert(i.super_mask == i_mask_before);

    reset_qram_read_count();
}

// ── (1c) Pointer container shape — D0a §11.1.1 overload 3 ───────────
// Round-trip pin via `sturm::invert<&QRAM_read<W>>()` per the
// issue's "callable via the existing P9 lookup" bullet. The pointer
// overload has a unique `<W>` template head, so the macro
// expansion + invert<> lookup land cleanly.
static void test_qram_read_pointer() {
    using sturm::qram::qram_read_count;
    using sturm::qram::reset_qram_read_count;
    constexpr std::size_t W = 8;
    constexpr std::size_t N = 4;

    sturm::CounterSink sink;
    sturm::ScopedSink scope(&sink);
    reset_qram_read_count();

    sturm::qint_t<W> storage[N]{};
    for (std::size_t k = 0; k < N; ++k) {
        storage[k] = make_classical<W>(static_cast<std::int64_t>(k + 7));
    }
    const sturm::qint_t<W>* a = storage;
    sturm::qint_t<W> i = make_classical<W>(3);
    sturm::qint_t<W> b = make_classical<W>(0);

    const auto i_mask_before = i.super_mask;
    sturm::QRAM_read(a, N, i, b);
    assert(qram_read_count() == 1u);
    assert(sink.count("qram_read") == 1u);
    assert(i.super_mask == i_mask_before);

    constexpr auto adj_ptr = sturm::invert<&sturm::QRAM_read<W>>();
    static_assert(adj_ptr == &sturm::__QRAM_read_adj<W>,
                  "invert<&QRAM_read<W>>() must resolve to "
                  "&__QRAM_read_adj<W> per D0d (sturm-u9ge.4).");
    adj_ptr(a, N, i, b);
    assert(qram_read_count() == 2u);
    assert(sink.count("qram_read") == 2u);
    assert(i.super_mask == i_mask_before);

    reset_qram_read_count();
}

// ── (2) noexcept on the public surface (PRD §11.1.2) ────────────────
// Pin via the noexcept operator on declvalish unevaluated calls.
namespace noexcept_pin {
    using A = std::array<sturm::qint_t<8>, 4>;
    using C = sturm::qint_t<8>[4];
    using P = const sturm::qint_t<8>*;
    inline const A& cref_a() noexcept;
    inline const C& cref_c() noexcept;
    inline const sturm::qint_t<8>& cref_i() noexcept;
    inline sturm::qint_t<8>& mref_b() noexcept;
    inline P  ptr_a() noexcept;
    inline std::size_t len_n() noexcept;

    static_assert(noexcept(sturm::QRAM_read(cref_a(), cref_i(), mref_b())),
                  "QRAM_read std::array overload must be noexcept (PRD §11.1.2).");
    static_assert(noexcept(sturm::QRAM_read(cref_c(), cref_i(), mref_b())),
                  "QRAM_read C-array overload must be noexcept (PRD §11.1.2).");
    static_assert(noexcept(sturm::QRAM_read(ptr_a(), len_n(), cref_i(), mref_b())),
                  "QRAM_read pointer overload must be noexcept (PRD §11.1.2).");
}  // namespace noexcept_pin

}  // namespace

int main() {
    test_qram_read_std_array();
    test_qram_read_c_array();
    test_qram_read_pointer();
    std::puts("test_qram_read_stub: OK");
    return 0;
}
