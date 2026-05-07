// test_qint_alias.cpp -- sturm-u9ge.5 (Beat A1) + sturm-v0db.2 (W3.1).
//
// Pins the contract for the non-templated frontend `qint` class
// declared in `include/sturm/qtypes/qint_alias.hpp` (PRD §4.1, plan §4 / A1;
// PRD §10 / plan §25 update for Wave 3).
//
// Wave 3 update (sturm-v0db.2 / W3.1, PRD §10.3.5):
//   * Runtime measurement-counter assertions are GONE — the counter
//     is being deleted in W3.4 (G9), and pre-transpile execution is
//     unsupported (the transpiler is mandatory).
//   * The Wave-1 G1 contract ("counter == 0 post-transpile") is
//     replaced by Wave-2 G6 (`test_sturm_gen_clean`), which is a
//     strictly stronger contract — observed at build-time on the
//     transpiler's *output*, not the alias internals.
//   * Compile-only parse-tests and `is_convertible_v` pins remain
//     load-bearing: they verify the implicit `qint -> size_t`
//     conversion is what makes `a[qint_idx]` parse pre-transpile.
//
// Coverage retained (W3.1):
//   1. Compile-only subscript across the three container shapes from
//      PRD §7: `std::array<qint, N>`, C-array `qint a[N]`, pointer
//      `qint* a`. Each must parse with a `qint` index, exercising the
//      load-bearing **implicit** `operator size_t() const noexcept`.
//   2. Negative SFINAE: backend `qint_t<W>` must continue to reject
//      implicit conversion to `size_t` / `int64_t`.
//   3. Positive SFINAE: `frontend::qint` IS implicitly convertible to
//      `size_t` (the load-bearing parse mechanism).
//
// LoC budget: <= 300 (plan §1, §4 / A1).

#include "sturm/qtypes/qint_alias.hpp"
#include "sturm/qtypes/qint.hpp"   // backend qint_t<W> for the negative SFINAE

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

// ── Local alias mirroring the user-facing spelling from PRD §4 ────────────
// Tests / fixtures throughout the tree already pattern: `using qint =
// sturm::qint_t<W>;`. We alias the new frontend class to a local name
// `qint` so the test reads exactly like the PRD's source examples.
using qint = sturm::frontend::qint;

// ── (1a) std::array container shape ──────────────────────────────────────
// PRD §7 row 1: `std::array<qint, N> a;` with quantum index `i`. The cast
// the C1 matcher uses as discriminator (`UserDefinedConversion` from
// `qint::operator size_t`) is what makes this expression parse today.
static void test_subscript_std_array_compiles() {
    qint i;                          // default: classical zero
    std::array<qint, 4> arr;          // 4 entries
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
// We only need the expression to *parse*; it is never executed.
static void test_subscript_pointer_compiles() {
    qint i;
    qint* p = nullptr;
    if (p != nullptr) {           // dead branch: defeats UB but keeps the
        auto& slot = p[i];        // subscript expression in the AST.
        (void)slot;
    }
    (void)p;
}

// ── (2) Negative SFINAE: backend qint_t<W> still rejects size_t / int64_t
// implicit conversion (P2 unchanged). The alias model (PRD §5) requires
// the post-transpile compile-time safety net: if the C1 matcher misses a
// site, the *emitted* file must fail to compile rather than silently
// measure at runtime.
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

// ── (3) Positive SFINAE: frontend qint IS implicitly convertible to size_t.
// The flip side — verifying our new class actually has the implicit
// conversion. If this static_assert ever fires, beats B1 / C1 / D2 lose
// their AST discriminator (PRD §7 — the CK_UserDefinedConversion cast
// chain originates here).
static_assert(std::is_convertible_v<sturm::frontend::qint, std::size_t>,
              "frontend::qint MUST be implicitly convertible to size_t — "
              "this is the load-bearing mechanism that makes "
              "`a[qint_idx]` parse (PRD §4.1).");

// The frontend qint must NOT introduce any other implicit conversions to
// common integer types beyond size_t. (int64_t / int32_t go through the
// `size_t -> integer` standard conversion path, which is allowed by the
// language; that is not under our control.) We pin the converse: classical
// int → qint (P4a) is implicit and must remain so.
static_assert(std::is_constructible_v<sturm::frontend::qint, int64_t>,
              "frontend::qint(int64_t) must remain non-explicit — "
              "P4a: classical-to-quantum conversion is implicit.");

static_assert(!std::is_constructible_v<sturm::frontend::qint, std::nullptr_t>,
              "frontend::qint(nullptr) must NOT compile — the "
              "PRD §4.1 surface only covers integer-valued construction.");

// ── (4) Default constructibility ─────────────────────────────────────────
static_assert(std::is_default_constructible_v<sturm::frontend::qint>,
              "frontend::qint must be default-constructible — required by "
              "every user-level declaration (PRD §4.1 step 1).");

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
    std::puts("test_qint_alias: OK");
    return 0;
}
