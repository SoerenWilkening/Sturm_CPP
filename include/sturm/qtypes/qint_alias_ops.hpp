#pragma once
// qint_alias_ops.hpp — Operator type-stubs for the frontend `qint` alias
// class (sturm-u9ge.10 / Beat A2; Wave-3-stripped per sturm-v0db.4 / W3.3).
//
// PRD §4.1 step 2 / plan §4 / A2: "mirror the public surface of `qint_t<W>`
// so user code that uses `qint` outside subscript still compiles." Wave 3
// (PRD §10.3.2 / G8) collapsed every operator body to a pure type-stub
// (`return qint{};`, `return qbool();`, or `return a;`); the matched
// subscript shape is rewritten by the C1 matcher to `QRAM_read(...)` and
// every stub body is unreachable on the rewrite path. The Wave-3
// transpiler is mandatory (host-clang invariant sturm-yial), so the
// pre-transpile reachable path is no longer a concern.
//
// The stubs deliberately do **NOT** route through backend `qint_t<W>`
// operators: that would commit to a width on the frontend (defeating the
// "non-templated" requirement, plan §4 / A1) and bleed quantum semantics
// into the pre-transpile path.
//
// ── Surface coverage (plan §14 static-assertion harness drives this) ──
//   arithmetic     : + - * / %     (binary, qint × qint)
//                    + with int    (qint × <integral>; symmetric)
//                    -             (unary)
//   compare        : == != < <= > >=    (binary, return qbool — W3 G7)
//                                       qint × qint and qint × <integral>
//   bitwise        : & | ^         (binary)
//                    ~             (unary)
//                    << >>         (binary, rhs = int)
//   compound assign: += -= *= /= %= &= |= ^=   (qint × qint)
//                    <<= >>=                   (qint × int)
//
// All compound-assigns are free functions (C++ allows non-member compound
// assigns). qint_alias.hpp (A1) stays untouched.
//
// Drift cost: every new `qint_t<W>` operator must show up here too —
// `tests/qtypes/test_qint_alias_ops.cpp`'s SFINAE harness (plan §14)
// fires if `qint_t<W>` exposes an operator the alias does not.
//
// ── Mixed-type integer overloads ─────────────────────────────────────
// `qint OP <integral>` for arithmetic `+` and all six comparisons:
// breaks the ambiguity between built-in `size_t OP <integral>` (via
// implicit `qint -> size_t`) and our `qint OP qint` (via implicit
// `<integral> -> qint`). Templated over any integral except `bool` so
// `q + 3` matches without conversion warnings while `q + true` rejects.
// Reverse `<integral> OP qint` is provided only for arithmetic `+`
// (mirrors `qint_t<W>`, whose compares are members).
//
// LoC budget: <= 300 (plan §1, §4 / A2).

#include "sturm/qtypes/qint_alias.hpp"   // sturm::frontend::qint (alias class)
#include "sturm/qtypes/qbool.hpp"        // sturm::qbool — Wave-3 G7 + G10 (return
                                         // type for compares + op[] read).

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace sturm {
namespace frontend {

// ── Wave 3 / W3.4 / G9 (sturm-v0db.5, PRD §10.3.3) ────────────────────
// The legacy frontend detail namespace (the measure-helper, the mixed-
// arith forwarder, and the `IntOp<T>` SFINAE alias) has been deleted.
// Under Wave 3 the alias's operator bodies are pure type-stubs
// (W3.3 / G8); none of them measure or forward to a classical
// fallback. The mixed-type integer-overload templates below inline
// the SFINAE predicate directly — `class = std::enable_if_t<
// std::is_integral_v<Int> && !std::is_same_v<Int, bool>>` — in place
// of the old `IntOp<Int>` typedef. Future re-introduction of any
// deleted symbol is gated by W3.6's tree-grep audit
// (`tests/qtypes/test_qint_alias_no_counter_infra`).

// ── Out-of-line member: qint::operator[](size_t) const ──────────────────
// Wave-3 G7 + G10 (PRD §10.3.4). Lives here so `qint_alias.hpp` stays
// free of qbool.hpp / qint_core.hpp includes. W3.3 (sturm-v0db.4 /
// PRD §10.3.2 / G8): pure type-stub `return qbool();`. No counter
// bump, no `value_` access — the body is unreachable post-transpile
// (the C1 matcher rewrites the alias usage).
inline qbool qint::operator[](std::size_t /*k*/) const noexcept {
    return qbool();
}

// ── Wave 3 / W3.3 — pure type-stub bodies (PRD §10.3.2 / G8) ─────────
// Every free-operator body below is one of:
//   return qint{};       arithmetic, bitwise, unary, shifts
//   return qbool();      compares (qint × qint and qint × <integral>)
//   return a;            compound assigns and compound shifts
//
// No body reads or writes `value_`. No body calls into the legacy
// frontend detail namespace (its surviving helpers — the measure
// forwarder, the mixed-arith helper, and the `IntOp` SFINAE alias —
// were deleted in W3.4 / sturm-v0db.5; the SFINAE predicate is now
// inlined at each callsite). Compound assigns return `a` unchanged
// — they MUST NOT compute `a OP b` even speculatively (that would
// re-invoke the free op which now returns a default).
//
// The transpiler (sturm-yial host-clang invariant) is mandatory under
// Wave 3; the C1 matcher rewrites every alias usage so the bodies are
// unreachable on the rewrite path. They exist purely as type-stubs the
// host C++ compiler can typecheck pre-transpile.

// ── Arithmetic: binary + - * / % ─────────────────────────────────────
inline qint operator+(const qint& /*a*/, const qint& /*b*/) noexcept { return qint{}; }
inline qint operator-(const qint& /*a*/, const qint& /*b*/) noexcept { return qint{}; }
inline qint operator*(const qint& /*a*/, const qint& /*b*/) noexcept { return qint{}; }
inline qint operator/(const qint& /*a*/, const qint& /*b*/) noexcept { return qint{}; }
inline qint operator%(const qint& /*a*/, const qint& /*b*/) noexcept { return qint{}; }

// Mixed-type `qint + <integral>` (and symmetric). Mirrors the explicit
// overloads `qint_t<W>` carries in qint_arith{,_backend}.hpp.
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qint operator+(const qint& /*a*/, Int /*c*/) noexcept { return qint{}; }

template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qint operator+(Int /*c*/, const qint& /*a*/) noexcept { return qint{}; }

// ── Mixed-type `qint OP <integral>` for the 7 non-+ ops ─────────────────
// sturm-65rs.3 / Beat A2. Forward direction only (PRD §3 — backend
// lacks reverse non-+; the alias stays symmetric).
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qint operator-(const qint& /*a*/, Int /*c*/) noexcept { return qint{}; }
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qint operator*(const qint& /*a*/, Int /*c*/) noexcept { return qint{}; }
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qint operator/(const qint& /*a*/, Int /*c*/) noexcept { return qint{}; }
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qint operator%(const qint& /*a*/, Int /*c*/) noexcept { return qint{}; }
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qint operator&(const qint& /*a*/, Int /*c*/) noexcept { return qint{}; }
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qint operator|(const qint& /*a*/, Int /*c*/) noexcept { return qint{}; }
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qint operator^(const qint& /*a*/, Int /*c*/) noexcept { return qint{}; }

// ── Arithmetic: unary - ───────────────────────────────────────────────
inline qint operator-(const qint& /*a*/) noexcept { return qint{}; }

// ── Compare: == != < <= > >= ─────────────────────────────────────────
// Wave-3 G7 (PRD §10.3.5): all twelve compare overloads (six qint × qint
// + six qint × Int templated) return `sturm::qbool` to match the
// backend's `qint_t<W>::operator==/!=/<…` return type. W3.3 strips
// bodies to `return qbool();` (default-constructed classical false).
inline qbool operator==(const qint& /*a*/, const qint& /*b*/) noexcept { return qbool(); }
inline qbool operator!=(const qint& /*a*/, const qint& /*b*/) noexcept { return qbool(); }
inline qbool operator< (const qint& /*a*/, const qint& /*b*/) noexcept { return qbool(); }
inline qbool operator<=(const qint& /*a*/, const qint& /*b*/) noexcept { return qbool(); }
inline qbool operator> (const qint& /*a*/, const qint& /*b*/) noexcept { return qbool(); }
inline qbool operator>=(const qint& /*a*/, const qint& /*b*/) noexcept { return qbool(); }

// Mixed-type compare `qint OP <integral>` — Wave-3 G7: returns `qbool`.
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qbool operator==(const qint& /*a*/, Int /*c*/) noexcept { return qbool(); }
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qbool operator!=(const qint& /*a*/, Int /*c*/) noexcept { return qbool(); }
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qbool operator< (const qint& /*a*/, Int /*c*/) noexcept { return qbool(); }
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qbool operator<=(const qint& /*a*/, Int /*c*/) noexcept { return qbool(); }
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qbool operator> (const qint& /*a*/, Int /*c*/) noexcept { return qbool(); }
template <class Int, class = std::enable_if_t<std::is_integral_v<Int> && !std::is_same_v<Int, bool>>>
inline qbool operator>=(const qint& /*a*/, Int /*c*/) noexcept { return qbool(); }

// ── Bitwise: binary & | ^ ─────────────────────────────────────────────
inline qint operator&(const qint& /*a*/, const qint& /*b*/) noexcept { return qint{}; }
inline qint operator|(const qint& /*a*/, const qint& /*b*/) noexcept { return qint{}; }
inline qint operator^(const qint& /*a*/, const qint& /*b*/) noexcept { return qint{}; }

// ── Bitwise: unary ~ ─────────────────────────────────────────────────
inline qint operator~(const qint& /*a*/) noexcept { return qint{}; }

// ── Bitwise: shifts (rhs = classical int) ────────────────────────────
inline qint operator<<(const qint& /*a*/, int /*n*/) noexcept { return qint{}; }
inline qint operator>>(const qint& /*a*/, int /*n*/) noexcept { return qint{}; }

// ── Compound assigns ──────────────────────────────────────────────────
// W3.3 constraint (PRD §10.3.2 / plan §27): "Compound assigns return
// `a` unchanged — they MUST NOT compute `a OP b` even speculatively,
// because that would re-invoke the free op (which now returns a
// default)." Body is just `return a;`.
inline qint& operator+=(qint& a, const qint& /*b*/) noexcept { return a; }
inline qint& operator-=(qint& a, const qint& /*b*/) noexcept { return a; }
inline qint& operator*=(qint& a, const qint& /*b*/) noexcept { return a; }
inline qint& operator/=(qint& a, const qint& /*b*/) noexcept { return a; }
inline qint& operator%=(qint& a, const qint& /*b*/) noexcept { return a; }
inline qint& operator&=(qint& a, const qint& /*b*/) noexcept { return a; }
inline qint& operator|=(qint& a, const qint& /*b*/) noexcept { return a; }
inline qint& operator^=(qint& a, const qint& /*b*/) noexcept { return a; }
inline qint& operator<<=(qint& a, int /*n*/)         noexcept { return a; }
inline qint& operator>>=(qint& a, int /*n*/)         noexcept { return a; }

} // namespace frontend
} // namespace sturm
