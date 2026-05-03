// test_qint_alias_qint_t_init.cpp -- sturm-qjt7
//
// Pins that the frontend `qint` accepts implicit construction from
// `sturm::qint_t<W>` so the user-facing rewrite source line
//
//     qint b = a[i];          // a is std::array<qint_t<W>, N> / [N] / *
//
// PARSES against the production header. The C1 matcher then replaces
// the VarDecl initializer pre-codegen, so this constructor body is
// unreachable on the rewrite path; the body bumps the measurement
// counter so a missed rewrite would show up as a non-zero
// `qint_alias::measurement_count()` (parallel to `operator size_t()`).
//
// Compile-only across the three PRD §7 container shapes; runtime
// semantics are exercised by tests/qram/test_qram_e2e.cpp and
// tests/qram/test_qram_read_qrom_gates.cpp.

#include "sturm/qtypes/qint.hpp"           // backend qint_t<W>
#include "sturm/qtypes/qint_alias.hpp"     // frontend qint

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

using qint = sturm::frontend::qint;

// ── (1) std::array<qint_t<W>, N> ────────────────────────────────────────
static void test_init_from_std_array_compiles() {
    std::array<sturm::qint_t<4>, 4> a{};
    std::size_t i = 0;
    qint b = a[i];
    (void)b;
}

// ── (2) C-array qint_t<W>[N] ────────────────────────────────────────────
static void test_init_from_c_array_compiles() {
    sturm::qint_t<4> a[4]{};
    std::size_t i = 0;
    qint b = a[i];
    (void)b;
}

// ── (3) Pointer qint_t<W>* ──────────────────────────────────────────────
static void test_init_from_pointer_compiles() {
    sturm::qint_t<4> storage[4]{};
    sturm::qint_t<4>* a = storage;
    std::size_t i = 0;
    qint b = a[i];
    (void)b;
}

// ── (4) noexcept + non-explicit traits ──────────────────────────────────
// The implicit converting constructor is required for the natural
// `qint b = a[i]` syntax (no `qint b{a[i]}` ceremony) and must be
// noexcept so it never threatens the P4a "free at runtime" guarantee.
static_assert(std::is_constructible_v<qint, sturm::qint_t<4>>,
              "frontend::qint must be constructible from qint_t<W>");
static_assert(std::is_convertible_v<sturm::qint_t<4>, qint>,
              "frontend::qint must IMPLICITLY accept qint_t<W> "
              "(otherwise `qint b = a[i]` will not parse)");
static_assert(std::is_nothrow_constructible_v<qint, sturm::qint_t<4>>,
              "frontend::qint(qint_t<W>) must be noexcept");

// ── (5) Constructor body is observable via measurement counter ──────────
// On the rewrite path the body is unreachable. If a future change
// silently disables the matcher, the body still bumps the same
// per-thread counter as `operator size_t()` so the G1 e2e assertion
// (`measurement_count() == 0` post-transpile) flags the regression.
static void test_constructor_bumps_measurement_counter() {
    sturm::frontend::qint_alias_detail::reset_measurement_count();
    assert(sturm::frontend::qint_alias_detail::measurement_count() == 0u);

    sturm::qint_t<4> src(7);
    qint b = src;
    (void)b;

    assert(sturm::frontend::qint_alias_detail::measurement_count() == 1u
           && "converting ctor must bump the measurement counter so a "
              "missed C1 rewrite is observable");
    sturm::frontend::qint_alias_detail::reset_measurement_count();
}

int main() {
    test_init_from_std_array_compiles();
    test_init_from_c_array_compiles();
    test_init_from_pointer_compiles();
    test_constructor_bumps_measurement_counter();
    return 0;
}
