// test_qint_alias_ops.cpp — sturm-u9ge.10 (Beat A2) + sturm-v0db.2 (W3.1).
//
// Pins the contract for the operator-stubs header
// `include/sturm/qtypes/qint_alias_ops.hpp` (PRD §4.1 step 2,
// plan §4 / A2; PRD §10.3 / G7 + plan §25 update for Wave 3).
//
// Wave 3 update (sturm-v0db.2 / W3.1, PRD §10.3.5):
//   * Runtime measurement-counter assertions are GONE. The counter
//     infrastructure is being removed in W3.4 (G9); pre-transpile
//     execution is unsupported under Wave 3 (the transpiler is
//     mandatory). The Wave-2 G6 sturm_gen-clean gate is the strictly
//     stronger replacement for the Wave-1 "counter == 0 post-transpile"
//     contract.
//   * Compares now return `sturm::qbool`, not classical `bool`. This
//     file pins that via positive `static_assert(std::is_same_v<
//     decltype(a OP b), sturm::qbool>)` for every alias compare
//     (six member-shape, six mixed-type-shape, twelve total) and one
//     for `decltype(a[0])` (PRD §10.3.5 / A11).
//   * The SFINAE drift-gate harness stays — it verifies the *signatures*
//     are present (return type ignored by `STURM_HAS_BIN`) and remains
//     load-bearing as the alias-vs-`qint_t<W>` parity check.
//
// LoC budget: <= 300 (plan §1, §4 / A2).

#include "sturm/qtypes/qint_alias.hpp"
#include "sturm/qtypes/qint_alias_ops.hpp"
#include "sturm/qtypes/qint.hpp"   // backend qint_t<W> for the static-assertion harness
#include "sturm/qtypes/qbool.hpp"  // sturm::qbool — the W3.1 return-type pin target

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

// ── Local alias mirroring the user-facing spelling from PRD §4 ────────────
// Same convention as test_qint_alias.cpp: at this point in the migration
// the namespace-scope alias `sturm::qint` still resolves to `qint_t<64>`
// (qint_fwd.hpp), so we explicitly point a local `qint` at the new
// frontend class. Tests then read exactly like the PRD's source examples.
using qint = sturm::frontend::qint;

// ── (1) Static-assertion harness (plan §14) ─────────────────────────────
// Drift between A2 stubs and `qint_t<W>`'s operator surface is the
// number-one risk PRD §5 calls out: "every new `qint_t<W>` operator
// needs a matching stub on `qint`". This harness fails if `qint_t<W>`
// gains an operator the alias does not.
//
// Strategy: SFINAE-based detection. For each operator OP, define a
// trait `has_op_OP<T>` that is true iff `T{} OP T{}` (or unary `OP T{}`)
// is well-formed. Static-assert that
// `has_op_OP<qint_t<W>> ⇒ has_op_OP<qint_alias>` for representative W.

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
// hit the `i.phi() += 3;` parse error in examples/qram_demo.cpp.
STURM_HAS_BIN(has_phi_plus_double,
              std::declval<T&>().phi() += std::declval<double>());
STURM_HAS_BIN(has_theta_plus_double,
              std::declval<T&>().theta() += std::declval<double>());

// sturm-65rs.3 / Beat A2 — mixed-type free ops. Per op three traits:
// `_qi` (`T OP int64_t`), `_iq` (`int64_t OP T`), `_qb` (`T OP bool`).
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

// sturm-vm38 — phase / rotation proxy stubs.
STURM_ALIAS_OP_PARITY(has_phi_plus_double,
                      "phi() returning a proxy with operator+=(double)");
STURM_ALIAS_OP_PARITY(has_theta_plus_double,
                      "theta() returning a proxy with operator+=(double)");

// sturm-65rs.3 / Beat A2 — mixed-type free ops on `frontend::qint`.
// Positive forward (`qint OP int64_t`) for all 8 ops; positive reverse
// only for `+`; negative reverse for the 7 non-`+` ops; negative
// `qint OP bool` for all 8.
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

// ── (2) A11 — qbool return-type pins (W3.1 / PRD §10.3.5 / sturm-v0db.2) ──
// Twelve compare assertions + one subscript assertion = 13 positive
// `static_assert(std::is_same_v<decltype(...), sturm::qbool>)`. These
// FAIL by design under Wave 1/2 source bodies (compares return `bool`,
// op[] returns `bool`); W3.2 (sturm-v0db.3) makes them pass by retrofitting
// the return types in `qint_alias_ops.hpp` and `qint_alias.hpp`.

// Six qint × qint compares.
static_assert(std::is_same_v<decltype(std::declval<const qint&>()
                                      == std::declval<const qint&>()),
                             sturm::qbool>,
              "frontend::qint::operator==(qint, qint) must return qbool "
              "(W3.1 / PRD §10.3.5 / G7).");
static_assert(std::is_same_v<decltype(std::declval<const qint&>()
                                      != std::declval<const qint&>()),
                             sturm::qbool>,
              "frontend::qint::operator!=(qint, qint) must return qbool.");
static_assert(std::is_same_v<decltype(std::declval<const qint&>()
                                      <  std::declval<const qint&>()),
                             sturm::qbool>,
              "frontend::qint::operator<(qint, qint) must return qbool.");
static_assert(std::is_same_v<decltype(std::declval<const qint&>()
                                      <= std::declval<const qint&>()),
                             sturm::qbool>,
              "frontend::qint::operator<=(qint, qint) must return qbool.");
static_assert(std::is_same_v<decltype(std::declval<const qint&>()
                                      >  std::declval<const qint&>()),
                             sturm::qbool>,
              "frontend::qint::operator>(qint, qint) must return qbool.");
static_assert(std::is_same_v<decltype(std::declval<const qint&>()
                                      >= std::declval<const qint&>()),
                             sturm::qbool>,
              "frontend::qint::operator>=(qint, qint) must return qbool.");

// Six qint × int64_t mixed compares.
static_assert(std::is_same_v<decltype(std::declval<const qint&>()
                                      == std::int64_t{0}),
                             sturm::qbool>,
              "frontend::qint::operator==(qint, int64_t) must return qbool.");
static_assert(std::is_same_v<decltype(std::declval<const qint&>()
                                      != std::int64_t{0}),
                             sturm::qbool>,
              "frontend::qint::operator!=(qint, int64_t) must return qbool.");
static_assert(std::is_same_v<decltype(std::declval<const qint&>()
                                      <  std::int64_t{0}),
                             sturm::qbool>,
              "frontend::qint::operator<(qint, int64_t) must return qbool.");
static_assert(std::is_same_v<decltype(std::declval<const qint&>()
                                      <= std::int64_t{0}),
                             sturm::qbool>,
              "frontend::qint::operator<=(qint, int64_t) must return qbool.");
static_assert(std::is_same_v<decltype(std::declval<const qint&>()
                                      >  std::int64_t{0}),
                             sturm::qbool>,
              "frontend::qint::operator>(qint, int64_t) must return qbool.");
static_assert(std::is_same_v<decltype(std::declval<const qint&>()
                                      >= std::int64_t{0}),
                             sturm::qbool>,
              "frontend::qint::operator>=(qint, int64_t) must return qbool.");

// One subscript-read pin.
static_assert(std::is_same_v<decltype(std::declval<const qint&>()[std::size_t{}]),
                             sturm::qbool>,
              "frontend::qint::operator[](size_t) const must return qbool "
              "(W3.1 / PRD §10.3.5 / G7).");

// ── (3) Post-transpile unreachability fixture: [[skip-until-C1]] ─────────
// The C1 matcher (sturm-u9ge.12) lands fixture pairs under
// `transpiler/tests/fixtures/qram_read_*.{cpp,expected.cpp}`. The
// expected file MUST NOT reference any frontend `qint` operator stub.
static void test_post_transpile_unreachability() {
    // [[skip-until-C1]] — see issue sturm-u9ge.10. Body is intentionally
    // empty until the C1 fixture pairs land.
    std::puts("test_post_transpile_unreachability: skipped (waiting on C1).");
}

// ── main / runner ────────────────────────────────────────────────────────
int main() {
    test_post_transpile_unreachability();
    std::puts("test_qint_alias_ops: OK");
    return 0;
}
