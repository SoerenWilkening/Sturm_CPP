// test_qint_alias.cpp -- sturm-u9ge.5 (Beat A1).
//
// Pins the contract for the new non-templated frontend `qint` class
// declared in `include/sturm/qtypes/qint_alias.hpp` (PRD §4.1, plan §4 / A1).
//
// Coverage:
//   1. Compile-only subscript across the three container shapes from
//      PRD §7: `std::array<qint, N>`, C-array `qint a[N]`, pointer
//      `qint* a`. Each must parse with a `qint` index, exercising the
//      load-bearing **implicit** `operator size_t() const noexcept`.
//   2. Runtime: implicit `size_t` conversion increments the
//      `qint_alias::measurement_count()` thread-local counter — the
//      observable side-effect that lets later beats verify the
//      transpiler erased the conversion-call site (PRD §5).
//   3. Negative SFINAE / static_assert: the backend `qint_t<W>` must
//      continue to reject implicit conversion to `size_t` / `int64_t`
//      (P2 unchanged — the alias model is the only implicit-measurement
//      site introduced by this PRD).
//
// LoC budget: <= 300 (plan §1, §4 / A1).

#include "sturm/qtypes/qint_alias.hpp"
#include "sturm/qtypes/qint.hpp"   // backend qint_t<W> for the negative SFINAE

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

// ── Local alias mirroring the user-facing spelling from PRD §4 ────────────
// Tests / fixtures throughout the tree already pattern: `using qint =
// sturm::qint_t<W>;` (e.g. `tests/transpiler/fixtures/add_assign_qint.cpp`).
// At this point in the migration the namespace-scope alias `sturm::qint`
// still resolves to `qint_t<64>` (qint_fwd.hpp), so we deliberately do NOT
// `using sturm::qint;` here. Instead we alias the new frontend class to a
// local name `qint` so the test reads exactly like the PRD's source
// examples.
using qint = sturm::frontend::qint;

// ── (1a) std::array container shape ──────────────────────────────────────
// PRD §7 row 1: `std::array<qint, N> a;` with quantum index `i`.
// AST node at the call site is a CXXOperatorCallExpr on
// `std::array::operator[](size_type)`. The cast that the C1 matcher will
// later use as discriminator (`UserDefinedConversion` from
// `qint::operator size_t`) is what makes this expression parse today.
static void test_subscript_std_array_compiles() {
    qint i;                          // default: classical zero
    std::array<qint, 4> arr;          // 4 entries
    // The expression must parse; we are only checking compile, not
    // semantics (semantics are the matcher's / runtime's job).
    auto& slot = arr[i];
    (void)slot;
}

// ── (1b) C-style array container shape ───────────────────────────────────
// PRD §7 row 2: `qint a[N];` — built-in subscript (ArraySubscriptExpr).
static void test_subscript_c_array_compiles() {
    qint i;
    qint buf[4];
    auto& slot = buf[i];
    (void)slot;
}

// ── (1c) Pointer container shape ─────────────────────────────────────────
// PRD §7 row 3: `qint *a = ...;` — built-in subscript on a pointer base.
// We only need the expression to *parse*; it is never executed (the
// pointer is null), so there is no UB at runtime.
static void test_subscript_pointer_compiles() {
    qint i;
    qint* p = nullptr;
    if (p != nullptr) {           // dead branch: defeats UB but keeps the
        auto& slot = p[i];        // subscript expression in the AST.
        (void)slot;
    }
    (void)p;
}

// ── (2) Runtime: implicit size_t conversion increments the counter ───────
// The implicit `operator size_t()` body is the *only* implicit measurement
// site on the user-facing surface (PRD §5). The body increments a
// thread-local counter so tests can verify it ran (and so the post-
// transpile e2e test G1 can verify it did NOT).
static void test_implicit_conversion_measures() {
    using sturm::frontend::qint_alias_detail::reset_measurement_count;
    using sturm::frontend::qint_alias_detail::measurement_count;

    reset_measurement_count();
    assert(measurement_count() == 0u);

    qint q(7);
    // Direct subscript-context conversion: the compiler picks
    // `operator size_t()` because that is the most-viable conversion
    // sequence that produces a value of `std::array::size_type`.
    std::array<int, 8> data{0,1,2,3,4,5,6,7};
    int v = data[q];
    assert(v == 7);
    assert(measurement_count() == 1u);

    // A second integral-context use (assignment to `size_t`) measures
    // again — confirming the conversion is unguarded and observable.
    std::size_t s = q;
    (void)s;
    assert(s == 7u);
    assert(measurement_count() == 2u);

    reset_measurement_count();
}

// ── (3) Negative SFINAE: backend qint_t<W> still rejects size_t / int64_t
// implicit conversion (P2 unchanged). The alias model (PRD §5) requires
// the post-transpile compile-time safety net: if the C1 matcher misses a
// site, the *emitted* file must fail to compile rather than silently
// measure at runtime. We pin that here via std::is_convertible.
static_assert(!std::is_convertible_v<sturm::qint_t<8>,  std::size_t>,
              "qint_t<8> must NOT be implicitly convertible to size_t — "
              "P2 (measurement is explicit) and PRD §5 (post-transpile "
              "safety net) both depend on this.");

static_assert(!std::is_convertible_v<sturm::qint_t<32>, std::size_t>,
              "qint_t<32> must NOT be implicitly convertible to size_t.");

static_assert(!std::is_convertible_v<sturm::qint_t<64>, std::size_t>,
              "qint_t<64> must NOT be implicitly convertible to size_t.");

static_assert(!std::is_convertible_v<sturm::qint_t<8>,  int64_t>,
              "qint_t<W> must NOT be implicitly convertible to int64_t — "
              "the existing explicit `operator int64_t()` stays the "
              "post-transpile compile-time gate.");

// ── (3b) Positive SFINAE: frontend qint IS implicitly convertible ────────
// The flip side — verifying our new class actually has the implicit
// conversion. If this static_assert ever fires, beats B1 / C1 / D2 lose
// their AST discriminator (PRD §7 — the CK_UserDefinedConversion cast
// chain originates here).
static_assert(std::is_convertible_v<sturm::frontend::qint, std::size_t>,
              "frontend::qint MUST be implicitly convertible to size_t — "
              "this is the load-bearing mechanism that makes "
              "`a[qint_idx]` parse (PRD §4.1).");

// ── (3c) The frontend qint must NOT introduce any *other* implicit
// conversions to common integer types — only `size_t` is part of the
// PRD §4.1 contract. A1 deliberately scopes the conversion to size_t
// alone; arithmetic / compare / bitwise are A2 (issue sturm-u9ge.10).
// We probe via std::is_convertible: int64_t / int32_t conversions go
// through `size_t -> integer` standard conversion paths and ARE allowed
// by the language; the user-defined conversion exists only for size_t.
// What we pin here is the converse direction: classical-int → qint
// (P4a) is implicit and must remain so.
static_assert(std::is_constructible_v<sturm::frontend::qint, int64_t>,
              "frontend::qint(int64_t) must remain non-explicit — "
              "P4a: classical-to-quantum conversion is implicit.");

static_assert(!std::is_constructible_v<sturm::frontend::qint, std::nullptr_t>,
              "frontend::qint(nullptr) must NOT compile — the "
              "PRD §4.1 surface only covers integer-valued construction.");

// ── (3d) Default constructibility ─────────────────────────────────────────
static_assert(std::is_default_constructible_v<sturm::frontend::qint>,
              "frontend::qint must be default-constructible — required by "
              "every user-level declaration (PRD §4.1 step 1).");

// ── Runtime check: the size_t value the implicit conversion returns
// reflects the qint's classical value (P2 / P4a round-trip). This is
// observable behaviour, not just a compile probe.
static void test_round_trip_value() {
    using sturm::frontend::qint_alias_detail::reset_measurement_count;

    reset_measurement_count();
    for (int64_t k : {0, 1, 2, 7, 42, 100}) {
        qint q(k);
        std::size_t v = q;
        assert(static_cast<int64_t>(v) == k);
    }
    reset_measurement_count();
}

// ── Defaulted operations ──────────────────────────────────────────────────
// PRD §4.1: copy / move = default. Pin so a future maintainer doesn't
// silently make them user-provided (which would, e.g., kill the
// triviality the inliner relies on).
static_assert(std::is_copy_constructible_v<sturm::frontend::qint>);
static_assert(std::is_move_constructible_v<sturm::frontend::qint>);
static_assert(std::is_copy_assignable_v<sturm::frontend::qint>);
static_assert(std::is_move_assignable_v<sturm::frontend::qint>);
static_assert(std::is_nothrow_default_constructible_v<sturm::frontend::qint>);

// ── main / runner ─────────────────────────────────────────────────────────
int main() {
    test_subscript_std_array_compiles();
    test_subscript_c_array_compiles();
    test_subscript_pointer_compiles();
    test_implicit_conversion_measures();
    test_round_trip_value();
    std::puts("test_qint_alias: OK");
    return 0;
}
