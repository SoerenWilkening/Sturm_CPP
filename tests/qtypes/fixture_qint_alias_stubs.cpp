// fixture_qint_alias_stubs.cpp — sturm-v0db.6 (Wave 3 / W3.5 / A12).
//
// Plan §29, PRD §10.4 A12 — compile-time fixture whose only job is to
// force the host C++ compiler to emit the alias operator + ctor bodies
// into LLVM IR. The companion test `test_qint_alias_stubs.cpp` then
// scans that IR with `-S -emit-llvm -O0` for the two W3.3 / G8
// invariants:
//
//   1. No `getelementptr inbounds` + `load` against any
//      `frontend::qint` member, inside any qint member-function body
//      (excluding `classical_value()`, which legitimately reads its
//      single private field).
//   2. No `call` / `invoke` operand mentioning a symbol from the
//      legacy frontend detail namespace (deleted in W3.4 / G9).
//
// This TU intentionally pulls only the two alias headers
// (`qint_alias.hpp`, `qint_alias_ops.hpp`) plus the qbool header that
// the W3.1 / G7 retrofit makes mandatory. The transitive footprint
// (`qint_core.hpp` -> `counter_sink.hpp` -> STL containers) is the
// floor for a fixture that must instantiate every alias operator;
// pulling the umbrella header (`sturm/all.hpp`) or any backend
// translation unit would balloon the IR an order of magnitude further
// and is the failure mode the size sanity bound in
// `test_qint_alias_stubs.cpp` catches.
//
// Each of the [[gnu::used]] entry-point functions below references
// every alias surface we need scanned:
//   * Member ctors — default, implicit-int64_t, copy, move, converting-
//     from-`qint_t<W>`.
//   * Member operators — `operator=`, `operator[]`, `explicit operator
//     int64_t`, `operator size_t`, `phi()`, `theta()`, the proxy
//     `operator+=` / `operator-=`.
//   * Free operators — every arithmetic / compare / bitwise / shift /
//     compound-assign overload from `qint_alias_ops.hpp`, in both the
//     `qint x qint` and `qint x <integral>` shapes.
//
// `[[gnu::used]]` keeps each entry function in the emitted IR at -O0
// even though it is never linked against; without it Clang will
// internalise unused statics. The functions are deliberately defined
// at namespace scope rather than as static, so their mangled names
// preserve `_Z`-prefix and match the expectations in the IR scanner.

#include "sturm/qtypes/qint_alias.hpp"
#include "sturm/qtypes/qint_alias_ops.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"   // sturm::qint_t<W> for the converting-ctor probe

#include <cstddef>
#include <cstdint>

namespace sf = sturm::frontend;

// ── Member ctors + member operators ─────────────────────────────────────
// Default ctor, implicit int64_t ctor, copy ctor, move ctor, the
// `qint::operator=(int64_t)` member assign, the explicit `operator
// int64_t() const`, the implicit `operator size_t() const`, and the
// `qint::operator[](size_t) const` reader. The PhiProxyStub /
// ThetaProxyStub `operator+=` / `operator-=` are also touched here.
[[gnu::used]] void touch_member_surface() {
    sf::qint a;                    // default ctor
    sf::qint b{static_cast<std::int64_t>(7)};  // implicit int64_t ctor
    sf::qint c(b);                  // copy ctor
    sf::qint d(static_cast<sf::qint&&>(a));  // move ctor
    a = static_cast<std::int64_t>(13); // operator=(int64_t)
    auto x = static_cast<std::int64_t>(b); // explicit operator int64_t
    std::size_t s = b;             // implicit operator size_t
    sturm::qbool q = b[std::size_t{0}]; // operator[]
    a.phi() += 0.5;                // PhiProxyStub::operator+=
    a.phi() -= 0.5;                // PhiProxyStub::operator-=
    a.theta() += 0.25;             // ThetaProxyStub::operator+=
    a.theta() -= 0.25;             // ThetaProxyStub::operator-=
    (void)c; (void)d; (void)x; (void)s; (void)q;
}

// ── Converting ctor `qint(const qint_t<W>&)` ────────────────────────────
// W3.3 / G8 invariant: this body is `{}` — empty. Pin against any
// future reintroduction of `value_ = src.value;` or counter helpers.
[[gnu::used]] sf::qint touch_converting_ctor(const sturm::qint_t<8>& src) {
    return sf::qint(src);
}

// ── Free operators: arithmetic + bitwise + shift (qint x qint) ─────────
// All eight binary operators that return `qint`. Forces the emission
// of `sturm::frontend::operator+ / - / * / / / % / & / | / ^` bodies.
[[gnu::used]] sf::qint touch_arith_qq(const sf::qint& x, const sf::qint& y) {
    sf::qint r = x + y;
    r = x - y;
    r = x * y;
    r = x / y;
    r = x % y;
    r = x & y;
    r = x | y;
    r = x ^ y;
    return r;
}

// ── Free operators: arithmetic + bitwise (qint x int) and (int x qint) ─
// The mixed-type templates from qint_alias_ops.hpp. `Int = int` is the
// natural integral fit; nothing here selects `bool` (the templates
// SFINAE that out).
[[gnu::used]] sf::qint touch_arith_qi(const sf::qint& x) {
    sf::qint r = x + 3;
    r = 3 + x;
    r = x - 3;
    r = x * 3;
    r = x / 3;
    r = x % 3;
    r = x & 3;
    r = x | 3;
    r = x ^ 3;
    return r;
}

// ── Free operators: unary - and unary ~ ────────────────────────────────
[[gnu::used]] sf::qint touch_unary(const sf::qint& x) {
    sf::qint r = -x;
    r = ~x;
    return r;
}

// ── Free operators: shifts (rhs = int) ─────────────────────────────────
[[gnu::used]] sf::qint touch_shifts(const sf::qint& x) {
    sf::qint r = x << 2;
    r = x >> 2;
    return r;
}

// ── Free operators: compares (qint x qint) — return qbool ──────────────
[[gnu::used]] void touch_compare_qq(const sf::qint& x, const sf::qint& y) {
    sturm::qbool a = (x == y);
    sturm::qbool b = (x != y);
    sturm::qbool c = (x <  y);
    sturm::qbool d = (x <= y);
    sturm::qbool e = (x >  y);
    sturm::qbool f = (x >= y);
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
}

// ── Free operators: compares (qint x int) — return qbool ───────────────
[[gnu::used]] void touch_compare_qi(const sf::qint& x) {
    sturm::qbool a = (x == 1);
    sturm::qbool b = (x != 1);
    sturm::qbool c = (x <  1);
    sturm::qbool d = (x <= 1);
    sturm::qbool e = (x >  1);
    sturm::qbool f = (x >= 1);
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
}

// ── Free operators: compound assigns (qint x qint) ─────────────────────
[[gnu::used]] void touch_compound_qq(sf::qint& a, const sf::qint& b) {
    a += b;
    a -= b;
    a *= b;
    a /= b;
    a %= b;
    a &= b;
    a |= b;
    a ^= b;
}

// ── Free operators: compound shifts (qint x int) ───────────────────────
[[gnu::used]] void touch_compound_shifts(sf::qint& a) {
    a <<= 2;
    a >>= 2;
}
