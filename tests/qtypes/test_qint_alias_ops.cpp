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
    STURM_MEASURED(std::int64_t{5} + a, 1u);   // reverse-+ (the only reverse the alias carries)
    reset_measurement_count();
}

// ── 1a-mixed. Mixed-type qint OP <integral> for the 7 new ops ───────────
// sturm-65rs.3 / Beat A2: forward `qint OP int` for `- * / % & | ^`. Each
// measures the qint operand exactly once and bumps the counter exactly
// once (the int operand is classical). PRD §3: reverse `<integral> OP
// qint` NOT added for non-`+`; backend does not carry these either.
// Also exercises `/` and `%` zero-divisor guard (mirrors qint × qint at
// qint_alias_ops.hpp:104-114).
static void test_mixed_arith_ops() {
    {  qint a(12); STURM_MEASURED(a - std::int64_t{5},  1u); }
    {  qint a(12); STURM_MEASURED(a * std::int64_t{3},  1u); }
    {  qint a(12); STURM_MEASURED(a / std::int64_t{4},  1u); }
    {  qint a(12); STURM_MEASURED(a % std::int64_t{5},  1u); }
    {  qint a(12); STURM_MEASURED(a & std::int64_t{6},  1u); }
    {  qint a(12); STURM_MEASURED(a | std::int64_t{1},  1u); }
    {  qint a(12); STURM_MEASURED(a ^ std::int64_t{15}, 1u); }
    reset_measurement_count();
    // Value semantics. Result starts with a fresh classical_value().
    {  qint a(12); assert((a - std::int64_t{5}).classical_value() == 7); }
    {  qint a(12); assert((a * std::int64_t{3}).classical_value() == 36); }
    {  qint a(12); assert((a / std::int64_t{4}).classical_value() == 3); }
    {  qint a(12); assert((a % std::int64_t{5}).classical_value() == 2); }
    {  qint a(0b1100); assert((a & std::int64_t{0b1010}).classical_value() == 0b1000); }
    {  qint a(0b1100); assert((a | std::int64_t{0b1010}).classical_value() == 0b1110); }
    {  qint a(0b1100); assert((a ^ std::int64_t{0b1010}).classical_value() == 0b0110); }
    // Zero-divisor guard for `/` and `%` mirrors qint × qint policy.
    {  qint a(12); assert((a / std::int64_t{0}).classical_value() == 0); }
    {  qint a(12); assert((a % std::int64_t{0}).classical_value() == 0); }
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

// ── 1f. Phase / rotation proxy stubs: phi()/theta() += / -= ──────────────
// sturm-vm38 — `qint_t<W>::phi()`/theta() return PhiProxy/ThetaProxy
// whose `operator+=(double)`/`operator-=(double)` emit per-bit RZ/RY
// rotations against the active backend. The frontend alias mirrors the
// public surface (PRD §4.1 step 2) but, like every other A2 stub, has no
// classical equivalent: phase rotations are intrinsically quantum. The
// stub bodies bump `g_measurement_count` (matching the observability
// contract every other alias op uses — counter > 0 post-transpile means
// the matcher missed a site) and leave `classical_value()` unchanged
// (no classical mirror).
static void test_phi_theta_proxy_measure() {
    // phi() += / -= each bump the counter +1 per call (the proxy itself
    // is free; the bump is on the rotation application).
    {  qint q(7);
       reset_measurement_count();
       q.phi() += 0.5;
       assert(measurement_count() == 1u);
       q.phi() -= 0.25;
       assert(measurement_count() == 2u);
       // Phase rotations have no classical mirror — value_ unchanged.
       assert(q.classical_value() == 7);
    }
    // theta() += / -= same shape.
    {  qint q(11);
       reset_measurement_count();
       q.theta() += 0.5;
       assert(measurement_count() == 1u);
       q.theta() -= 0.25;
       assert(measurement_count() == 2u);
       assert(q.classical_value() == 11);
    }
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

// sturm-65rs.2 / Beat A1 — member ops added by the qint alias completion
// epic. The harness rule "qint_t<W> has it ⇒ alias has it" is identical
// for these three; macro reuse keeps the assertion shape uniform.
STURM_HAS_BIN(has_assign_i64, std::declval<T&>() = std::declval<std::int64_t>());
STURM_HAS_BIN(has_subscript,  std::declval<const T&>()[std::declval<std::size_t>()]);
STURM_HAS_BIN(has_explicit_i64, static_cast<std::int64_t>(std::declval<const T&>()));

// sturm-vm38 — phase / rotation proxy stubs. The drift-gate the previous
// SFINAE harness missed enumerated *operator overloads* (op+=, op-=, …)
// only — it never probed the named member methods `phi()` / `theta()`,
// which is why the alias's lack of these went undetected until a user
// hit the `i.phi() += 3;` parse error in examples/qram_demo.cpp. These
// traits close that gap by checking BOTH the named-method existence AND
// that the returned proxy has a well-formed `operator+=(double)` (the
// load-bearing piece — phase/rotation rate of change). `operator-=` is
// implementation-derivable from `+=` (parent calls `+=(-delta)`) so the
// gate pins `+=` as canonical; `-=` is exercised at runtime above.
STURM_HAS_BIN(has_phi_plus_double,
              std::declval<T&>().phi() += std::declval<double>());
STURM_HAS_BIN(has_theta_plus_double,
              std::declval<T&>().theta() += std::declval<double>());

// sturm-65rs.3 / Beat A2 — mixed-type free ops. Per op three traits:
// `_qi` (`T OP int64_t`), `_iq` (`int64_t OP T`), `_qb` (`T OP bool`).
// Assertions below: `_qi` for all 8, `_iq` only for `+`, `_qb` for none.
#define STURM_MIX_FWD(n,o)                                                     \
    template <class, class = void> struct n : std::false_type {};              \
    template <class T> struct n<T, std::void_t<                                \
        decltype(std::declval<T>() o std::declval<std::int64_t>())>>           \
        : std::true_type {}
#define STURM_MIX_REV(n,o)                                                     \
    template <class, class = void> struct n : std::false_type {};              \
    template <class T> struct n<T, std::void_t<                                \
        decltype(std::declval<std::int64_t>() o std::declval<T>())>>           \
        : std::true_type {}
#define STURM_MIX_BOOL(n,o)                                                    \
    template <class, class = void> struct n : std::false_type {};              \
    template <class T> struct n<T, std::void_t<                                \
        decltype(std::declval<T>() o std::declval<bool>())>>                   \
        : std::true_type {}
#define STURM_MIX_TRIPLE(o, qi, iq, qb)                                        \
    STURM_MIX_FWD(qi, o); STURM_MIX_REV(iq, o); STURM_MIX_BOOL(qb, o)
STURM_MIX_TRIPLE(+, has_add_qi,  has_add_iq,  has_add_qb);
STURM_MIX_TRIPLE(-, has_sub_qi,  has_sub_iq,  has_sub_qb);
STURM_MIX_TRIPLE(*, has_mul_qi,  has_mul_iq,  has_mul_qb);
STURM_MIX_TRIPLE(/, has_div_qi,  has_div_iq,  has_div_qb);
STURM_MIX_TRIPLE(%, has_mod_qi,  has_mod_iq,  has_mod_qb);
STURM_MIX_TRIPLE(&, has_band_qi, has_band_iq, has_band_qb);
STURM_MIX_TRIPLE(|, has_bor_qi,  has_bor_iq,  has_bor_qb);
STURM_MIX_TRIPLE(^, has_bxor_qi, has_bxor_iq, has_bxor_qb);
#undef STURM_MIX_TRIPLE
#undef STURM_MIX_BOOL
#undef STURM_MIX_REV
#undef STURM_MIX_FWD
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

// sturm-65rs.2 / Beat A1 — member ops added by the qint alias
// completion epic. The drift-gate rule "qint_t<W> has it ⇒ alias has
// it" is the same shape; just three more lines.
STURM_ALIAS_OP_PARITY(has_assign_i64,   "operator=(int64_t)");
STURM_ALIAS_OP_PARITY(has_subscript,    "operator[](size_t) const");
STURM_ALIAS_OP_PARITY(has_explicit_i64, "explicit operator int64_t() const");

// sturm-vm38 — phase / rotation proxy stubs. PhiProxy / ThetaProxy on
// `qint_t<W>` (qint_core.hpp:241/308) have `operator+=(double)`; the
// alias must expose proxies whose `+=(double)` is well-formed too,
// otherwise `i.phi() += 3;` is a parse error against the alias spelling
// (the original sturm-vm38 footgun in examples/qram_demo.cpp).
STURM_ALIAS_OP_PARITY(has_phi_plus_double,
                      "phi() returning a proxy with operator+=(double)");
STURM_ALIAS_OP_PARITY(has_theta_plus_double,
                      "theta() returning a proxy with operator+=(double)");

// sturm-65rs.3 / Beat A2 — mixed-type free ops on `frontend::qint`.
// Positive forward (`qint OP int64_t`) for all 8 ops; positive reverse
// only for `+`; negative reverse for the 7 non-`+` ops (PRD §3 — backend
// lacks reverse non-+); negative `qint OP bool` for all 8 (`bool` is
// excluded from `IntOp<T>` SFINAE — narrowing/promotion guard).
namespace fa = ::sturm::frontend;
#define STURM_M_FWD(t,m) static_assert( harness::t<fa::qint>::value, m)
#define STURM_M_NEG(t,m) static_assert(!harness::t<fa::qint>::value, m)
STURM_M_FWD(has_add_qi,  "missing op+(qint,int64_t)");
STURM_M_FWD(has_sub_qi,  "missing op-(qint,int64_t)");
STURM_M_FWD(has_mul_qi,  "missing op*(qint,int64_t)");
STURM_M_FWD(has_div_qi,  "missing op/(qint,int64_t)");
STURM_M_FWD(has_mod_qi,  "missing op%(qint,int64_t)");
STURM_M_FWD(has_band_qi, "missing op&(qint,int64_t)");
STURM_M_FWD(has_bor_qi,  "missing op|(qint,int64_t)");
STURM_M_FWD(has_bxor_qi, "missing op^(qint,int64_t)");
STURM_M_FWD(has_add_iq,  "missing op+(int64_t,qint) reverse");
STURM_M_NEG(has_sub_iq,  "must not have op-(int64_t,qint)");
STURM_M_NEG(has_mul_iq,  "must not have op*(int64_t,qint)");
STURM_M_NEG(has_div_iq,  "must not have op/(int64_t,qint)");
STURM_M_NEG(has_mod_iq,  "must not have op%(int64_t,qint)");
STURM_M_NEG(has_band_iq, "must not have op&(int64_t,qint)");
STURM_M_NEG(has_bor_iq,  "must not have op|(int64_t,qint)");
STURM_M_NEG(has_bxor_iq, "must not have op^(int64_t,qint)");
STURM_M_NEG(has_add_qb,  "must not have op+(qint,bool)");
STURM_M_NEG(has_sub_qb,  "must not have op-(qint,bool)");
STURM_M_NEG(has_mul_qb,  "must not have op*(qint,bool)");
STURM_M_NEG(has_div_qb,  "must not have op/(qint,bool)");
STURM_M_NEG(has_mod_qb,  "must not have op%(qint,bool)");
STURM_M_NEG(has_band_qb, "must not have op&(qint,bool)");
STURM_M_NEG(has_bor_qb,  "must not have op|(qint,bool)");
STURM_M_NEG(has_bxor_qb, "must not have op^(qint,bool)");
#undef STURM_M_FWD
#undef STURM_M_NEG

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
    test_mixed_arith_ops();
    test_compare_measure();
    test_compare_values();
    test_bitwise_measure();
    test_compound_assign_measure();
    test_phi_theta_proxy_measure();
    test_post_transpile_unreachability();
    std::puts("test_qint_alias_ops: OK");
    return 0;
}
