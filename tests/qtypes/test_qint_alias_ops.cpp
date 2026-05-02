// test_qint_alias_ops.cpp — sturm-u9ge.10 (Beat A2).
//
// Pins the contract for the operator-stubs header
// `include/sturm/qtypes/qint_alias_ops.hpp` (PRD §4.1 step 2,
// plan §4 / A2).
//
// Coverage:
//   1. Per-stub measurement-counter assertion: every arithmetic / compare /
//      bitwise / compound stub on `sturm::frontend::qint` must invoke the
//      load-bearing implicit `operator size_t()` on each quantum operand it
//      consumes — observable as bumps to
//      `qint_alias_detail::g_measurement_count`. This is the "lossy by
//      design" contract from PRD §4.1: pre-transpile any `qint`-vs-`qint`
//      operator outside the subscript shape DOES measure; post-transpile
//      the matched shape has been rewritten to `QRAM_read(...)` and the
//      stub body is unreachable.
//   2. Static-assertion harness (plan §14): for every operator
//      `qint_t<W>` exposes, the same operator must be valid on
//      `sturm::frontend::qint` (return type ignored — what matters is
//      that user code that compiles against `qint_t<W>` keeps compiling
//      against the alias once the matcher rewrites it).
//   3. Post-transpile unreachability fixture: marked `[[skip-until-C1]]`
//      until the C1 matcher (sturm-u9ge.12) lands the per-shape fixture
//      pair under `transpiler/tests/fixtures/`. Per the issue: "the
//      fixture lands with C1; mark [[skip-until-C1]] until then."
//
// LoC budget: <= 300 (plan §1, §4 / A2).

#include "sturm/qtypes/qint_alias.hpp"
#include "sturm/qtypes/qint_alias_ops.hpp"
#include "sturm/qtypes/qint.hpp"   // backend qint_t<W> for the static-assertion harness

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

// ── Local alias mirroring the user-facing spelling from PRD §4 ────────────
// Same convention as test_qint_alias.cpp: at this point in the migration
// the namespace-scope alias `sturm::qint` still resolves to `qint_t<64>`
// (qint_fwd.hpp), so we explicitly point a local `qint` at the new
// frontend class. Tests then read exactly like the PRD's source examples.
using qint = sturm::frontend::qint;

using sturm::frontend::qint_alias_detail::reset_measurement_count;
using sturm::frontend::qint_alias_detail::measurement_count;

// ── (1) Per-stub measurement-counter assertions ───────────────────────────
// Each stub must measure each *quantum* operand exactly once. Classical
// operands (bare `int64_t`) do NOT bump the counter; only `qint -> size_t`
// implicit conversions do.

// Helper: measure-count expectation per stub call.
#define STURM_MEASURED(expr, expected_bumps) do {                              \
    reset_measurement_count();                                                 \
    auto _r = (expr); (void)_r;                                                \
    assert(measurement_count() == (expected_bumps));                           \
} while (0)

#define STURM_MEASURED_VOID(stmt, expected_bumps) do {                         \
    reset_measurement_count();                                                 \
    stmt;                                                                      \
    assert(measurement_count() == (expected_bumps));                           \
} while (0)

// ── 1a. Arithmetic binary + unary: + - * / % unary- ─────────────────────
static void test_arithmetic_measure() {
    qint a(7), b(3);
    STURM_MEASURED(a + b, 2u);
    STURM_MEASURED(a - b, 2u);
    STURM_MEASURED(a * b, 2u);
    STURM_MEASURED(a / b, 2u);
    STURM_MEASURED(a % b, 2u);
    STURM_MEASURED(-a,    1u);   // unary minus
    // Mixed-type qint + int64_t (qint_t<W> exposes these overloads in
    // qint_arith.hpp, so the alias must too — see the disambiguation
    // note in qint_alias_ops.hpp).
    STURM_MEASURED(a + std::int64_t{5}, 1u);
    STURM_MEASURED(std::int64_t{5} + a, 1u);
    reset_measurement_count();
}

// ── 1b. Compare: == != < <= > >= ─────────────────────────────────────────
// Stubs return classical `bool`. Per PRD §4.1: lossy by design.
static void test_compare_measure() {
    qint a(7), b(3);
    STURM_MEASURED(a == b, 2u); reset_measurement_count();
    STURM_MEASURED(a != b, 2u); reset_measurement_count();
    STURM_MEASURED(a <  b, 2u); reset_measurement_count();
    STURM_MEASURED(a <= b, 2u); reset_measurement_count();
    STURM_MEASURED(a >  b, 2u); reset_measurement_count();
    STURM_MEASURED(a >= b, 2u); reset_measurement_count();
    // Mixed-type compare `qint OP <integral>` — `qint_t<W>::operator OP`
    // is a member function so `q OP 3` works there. The alias preserves
    // surface parity via templated mixed-type overloads (see header).
    STURM_MEASURED(a == 3, 1u); reset_measurement_count();
    STURM_MEASURED(a != 3, 1u); reset_measurement_count();
    STURM_MEASURED(a <  3, 1u); reset_measurement_count();
    STURM_MEASURED(a <= 3, 1u); reset_measurement_count();
    STURM_MEASURED(a >  3, 1u); reset_measurement_count();
    STURM_MEASURED(a >= 3, 1u); reset_measurement_count();
}

// ── 1c. Compare results: stubs are real classical values ────────────────
static void test_compare_values() {
    reset_measurement_count();
    qint a(7), b(3);
    assert((a == b) == false);
    assert((a != b) == true);
    assert((a <  b) == false);
    assert((a <= b) == false);
    assert((a >  b) == true);
    assert((a >= b) == true);
    reset_measurement_count();
}

// ── 1d. Bitwise binary + unary + shifts: & | ^ ~ << >> ───────────────────
// Shifts (`<< >>`) take a classical int rhs, so only one measurement.
static void test_bitwise_measure() {
    qint a(0b1100), b(0b1010);
    STURM_MEASURED(a & b,  2u);
    STURM_MEASURED(a | b,  2u);
    STURM_MEASURED(a ^ b,  2u);
    STURM_MEASURED(~a,     1u);
    STURM_MEASURED(a << 2, 1u);
    STURM_MEASURED(a >> 1, 1u);
    reset_measurement_count();
}

// ── 1e. Compound assigns: += -= *= /= %= &= |= ^= <<= >>= ─────────────────
// LHS measure + RHS measure for binary forms; LHS only for shifts.
static void test_compound_assign_measure() {
    {  qint a(7), b(3);          STURM_MEASURED_VOID(a += b, 2u); }
    {  qint a(7), b(3);          STURM_MEASURED_VOID(a -= b, 2u); }
    {  qint a(7), b(3);          STURM_MEASURED_VOID(a *= b, 2u); }
    {  qint a(7), b(3);          STURM_MEASURED_VOID(a /= b, 2u); }
    {  qint a(7), b(3);          STURM_MEASURED_VOID(a %= b, 2u); }
    {  qint a(0b1100), b(0b1010); STURM_MEASURED_VOID(a &= b, 2u); }
    {  qint a(0b1100), b(0b1010); STURM_MEASURED_VOID(a |= b, 2u); }
    {  qint a(0b1100), b(0b1010); STURM_MEASURED_VOID(a ^= b, 2u); }
    {  qint a(0b1100);            STURM_MEASURED_VOID(a <<= 2, 1u); }
    {  qint a(0b1100);            STURM_MEASURED_VOID(a >>= 1, 1u); }
    reset_measurement_count();
}

#undef STURM_MEASURED
#undef STURM_MEASURED_VOID

// ── (2) Static-assertion harness (plan §14) ─────────────────────────────
// Drift between A2 stubs and `qint_t<W>`'s operator surface is the
// number-one risk PRD §5 calls out: "every new `qint_t<W>` operator
// needs a matching stub on `qint`". This harness fails if `qint_t<W>`
// gains an operator the alias does not.
//
// Strategy: SFINAE-based detection. For each operator OP, define a
// trait `has_op_OP<T>` that is true iff `T{} OP T{}` (or unary `OP T{}`)
// is well-formed. Static-assert that
// `has_op_OP<qint_t<W>> ⇒ has_op_OP<qint_alias>` for representative W
// (W = 64 captures the common case; `qint_t<32>` and `qint_t<8>`
// catch any width-dependent specialisations).

namespace harness {

#define STURM_HAS_BIN(name, expr)                                              \
    template <class, class = void> struct name : std::false_type {};           \
    template <class T> struct name<T, std::void_t<decltype(expr)>>             \
        : std::true_type {}

STURM_HAS_BIN(has_add,   std::declval<T>() +  std::declval<T>());
STURM_HAS_BIN(has_sub,   std::declval<T>() -  std::declval<T>());
STURM_HAS_BIN(has_mul,   std::declval<T>() *  std::declval<T>());
STURM_HAS_BIN(has_div,   std::declval<T>() /  std::declval<T>());
STURM_HAS_BIN(has_mod,   std::declval<T>() %  std::declval<T>());
STURM_HAS_BIN(has_neg,   -std::declval<T>());
STURM_HAS_BIN(has_eq,    std::declval<T>() == std::declval<T>());
STURM_HAS_BIN(has_ne,    std::declval<T>() != std::declval<T>());
STURM_HAS_BIN(has_lt,    std::declval<T>() <  std::declval<T>());
STURM_HAS_BIN(has_le,    std::declval<T>() <= std::declval<T>());
STURM_HAS_BIN(has_gt,    std::declval<T>() >  std::declval<T>());
STURM_HAS_BIN(has_ge,    std::declval<T>() >= std::declval<T>());
STURM_HAS_BIN(has_band,  std::declval<T>() &  std::declval<T>());
STURM_HAS_BIN(has_bor,   std::declval<T>() |  std::declval<T>());
STURM_HAS_BIN(has_bxor,  std::declval<T>() ^  std::declval<T>());
STURM_HAS_BIN(has_bnot,  ~std::declval<T>());
STURM_HAS_BIN(has_shl,   std::declval<T>() << 1);
STURM_HAS_BIN(has_shr,   std::declval<T>() >> 1);
STURM_HAS_BIN(has_addeq, std::declval<T&>() +=  std::declval<T>());
STURM_HAS_BIN(has_subeq, std::declval<T&>() -=  std::declval<T>());
STURM_HAS_BIN(has_muleq, std::declval<T&>() *=  std::declval<T>());
STURM_HAS_BIN(has_diveq, std::declval<T&>() /=  std::declval<T>());
STURM_HAS_BIN(has_modeq, std::declval<T&>() %=  std::declval<T>());
STURM_HAS_BIN(has_andeq, std::declval<T&>() &=  std::declval<T>());
STURM_HAS_BIN(has_oreq,  std::declval<T&>() |=  std::declval<T>());
STURM_HAS_BIN(has_xoreq, std::declval<T&>() ^=  std::declval<T>());
STURM_HAS_BIN(has_shleq, std::declval<T&>() <<= 1);
STURM_HAS_BIN(has_shreq, std::declval<T&>() >>= 1);

#undef STURM_HAS_BIN

#define STURM_ALIAS_OP_PARITY(trait, msg)                                      \
    static_assert(!harness::trait<sturm::qint_t<64>>::value                    \
                  || harness::trait<sturm::frontend::qint>::value,             \
                  "drift between qint_t<64> and frontend::qint: " msg);        \
    static_assert(!harness::trait<sturm::qint_t<8>>::value                     \
                  || harness::trait<sturm::frontend::qint>::value,             \
                  "drift between qint_t<8> and frontend::qint: " msg)

} // namespace harness

STURM_ALIAS_OP_PARITY(has_add,   "operator+");
STURM_ALIAS_OP_PARITY(has_sub,   "operator-");
STURM_ALIAS_OP_PARITY(has_mul,   "operator*");
STURM_ALIAS_OP_PARITY(has_div,   "operator/");
STURM_ALIAS_OP_PARITY(has_mod,   "operator%");
STURM_ALIAS_OP_PARITY(has_neg,   "unary operator-");
STURM_ALIAS_OP_PARITY(has_eq,    "operator==");
STURM_ALIAS_OP_PARITY(has_ne,    "operator!=");
STURM_ALIAS_OP_PARITY(has_lt,    "operator<");
STURM_ALIAS_OP_PARITY(has_le,    "operator<=");
STURM_ALIAS_OP_PARITY(has_gt,    "operator>");
STURM_ALIAS_OP_PARITY(has_ge,    "operator>=");
STURM_ALIAS_OP_PARITY(has_band,  "operator&");
STURM_ALIAS_OP_PARITY(has_bor,   "operator|");
STURM_ALIAS_OP_PARITY(has_bxor,  "operator^");
STURM_ALIAS_OP_PARITY(has_bnot,  "operator~");
STURM_ALIAS_OP_PARITY(has_shl,   "operator<<");
STURM_ALIAS_OP_PARITY(has_shr,   "operator>>");
STURM_ALIAS_OP_PARITY(has_addeq, "operator+=");
STURM_ALIAS_OP_PARITY(has_subeq, "operator-=");
STURM_ALIAS_OP_PARITY(has_muleq, "operator*=");
STURM_ALIAS_OP_PARITY(has_diveq, "operator/=");
STURM_ALIAS_OP_PARITY(has_modeq, "operator%=");
STURM_ALIAS_OP_PARITY(has_andeq, "operator&=");
STURM_ALIAS_OP_PARITY(has_oreq,  "operator|=");
STURM_ALIAS_OP_PARITY(has_xoreq, "operator^=");
STURM_ALIAS_OP_PARITY(has_shleq, "operator<<=");
STURM_ALIAS_OP_PARITY(has_shreq, "operator>>=");

// ── (3) Post-transpile unreachability fixture: [[skip-until-C1]] ─────────
// The C1 matcher (sturm-u9ge.12) lands fixture pairs under
// `transpiler/tests/fixtures/qram_read_*.{cpp,expected.cpp}`. The
// expected file MUST NOT reference any frontend `qint` operator stub —
// that is the post-transpile safety net (PRD §5). Until C1 lands, this
// check is skipped; it lives here so the assertion is co-located with
// the stubs it covers.
static void test_post_transpile_unreachability() {
    // [[skip-until-C1]] — see issue sturm-u9ge.10. Body is intentionally
    // empty until the C1 fixture pairs land. When C1 lands, this body
    // will read the relevant `.expected.cpp` and grep for any
    // `sturm::frontend::qint::operator` substring; presence is a fail.
    std::puts("test_post_transpile_unreachability: skipped (waiting on C1).");
}

// ── main / runner ────────────────────────────────────────────────────────
int main() {
    test_arithmetic_measure();
    test_compare_measure();
    test_compare_values();
    test_bitwise_measure();
    test_compound_assign_measure();
    test_post_transpile_unreachability();
    std::puts("test_qint_alias_ops: OK");
    return 0;
}
