#pragma once
// qint_alias_ops.hpp — Operator stubs for the frontend `qint` alias class
// (sturm-u9ge.10 / Beat A2).
//
// PRD §4.1 step 2 / plan §4 / A2: "mirror the public surface of `qint_t<W>`
// so user code that uses `qint` outside subscript still compiles." The
// implementation strategy is **measure-then-classical**: each stub invokes
// the load-bearing implicit `operator size_t()` (in qint_alias.hpp) on each
// quantum operand and forwards to the corresponding classical operation.
// Lossy by design: pre-transpile, anything that is not the matched
// subscript shape silently measures; post-transpile, the C1 matcher
// (sturm-u9ge.12) rewrites the matched shape to `QRAM_read(...)` and the
// stub bodies are unreachable.
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
//   compare        : == != < <= > >=    (binary, return bool)
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
// `__builtin_unreachable()` policy (PRD §4.1 step 2): the only "transpiler-
// only" shape called out by the PRD is the `qint_t<W> b = QRAM_read(...)`
// temporary, which is a *backend* type and not part of this header. None
// of the alias-level operators here are transpiler-only; every one is a
// legal pre-transpile spelling and uses the measure-then-classical body.
//
// Drift cost: every new `qint_t<W>` operator must show up here too.
// `tests/qtypes/test_qint_alias_ops.cpp` runs a SFINAE-based static-
// assertion harness (plan §14) that fails if `qint_t<W>` exposes an
// operator the alias does not — so the cost is loud, not silent.
//
// ── Mixed-type integer overloads ─────────────────────────────────────
// `qint OP <integral>` for arithmetic `+` and all six comparisons: needed
// to break the otherwise-ambiguous lookup between built-in
// `size_t OP <integral>` (via implicit `qint -> size_t`) and our
// `qint OP qint` (via implicit `<integral> -> qint`). Templated over any
// integral type so an int literal `q + 3` matches without standard-
// conversion warnings. `bool` is excluded so `q + true` does not compile.
// Reverse-direction `<integral> OP qint` is provided **only** for
// arithmetic `+`, mirroring `qint_t<W>` (whose comparisons are members,
// so `3 < q` does not match there either).
//
// LoC budget: <= 300 (plan §1, §4 / A2).

#include "sturm/qtypes/qint_alias.hpp"   // sturm::frontend::qint + measurement counter

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace sturm {
namespace frontend {

// ── detail — measure helper ───────────────────────────────────────────
// Going through `static_cast<std::size_t>(q)` exercises the load-bearing
// implicit `operator size_t()`, which is the *only* surface that bumps
// `qint_alias_detail::g_measurement_count`. Tests pin per-stub bumps;
// the post-transpile coverage check pins the total stays at zero.
namespace qint_alias_detail {

inline std::int64_t measure_to_int(const qint& q) noexcept {
    const std::size_t v = q;   // implicit -> bumps counter.
    return static_cast<std::int64_t>(v);
}

// SFINAE alias: any integral type other than `bool` and `qint`. Used in
// the mixed-type overloads (arithmetic `+ - * / % & | ^`, all six compares).
template <class T>
using IntOp = std::enable_if_t<std::is_integral_v<T>
                               && !std::is_same_v<T, bool>>;

// sturm-65rs.3 / Beat A2 — single helper for the seven mixed-type
// arithmetic/bitwise free ops (`-`, `*`, `/`, `%`, `&`, `|`, `^`). The
// helper measures the qint operand exactly once (via `measure_to_int`)
// and forwards to a classical callable `op(int64_t, int64_t) -> int64_t`.
// `/` and `%` pass a divisor-guarding callable, mirroring the
// `qint × qint` policy (`y != 0 ? x OP y : 0`) at lines 104-114 above.
template <class Int, class F>
inline qint mixed_arith(const qint& a, Int c, F op) noexcept {
    return qint(op(measure_to_int(a), static_cast<std::int64_t>(c)));
}

} // namespace qint_alias_detail

// ── Arithmetic: binary + - * / % ─────────────────────────────────────
inline qint operator+(const qint& a, const qint& b) noexcept {
    return qint(qint_alias_detail::measure_to_int(a)
              + qint_alias_detail::measure_to_int(b));
}

inline qint operator-(const qint& a, const qint& b) noexcept {
    return qint(qint_alias_detail::measure_to_int(a)
              - qint_alias_detail::measure_to_int(b));
}

inline qint operator*(const qint& a, const qint& b) noexcept {
    return qint(qint_alias_detail::measure_to_int(a)
              * qint_alias_detail::measure_to_int(b));
}

inline qint operator/(const qint& a, const qint& b) noexcept {
    const auto x = qint_alias_detail::measure_to_int(a);
    const auto y = qint_alias_detail::measure_to_int(b);
    return qint(y != 0 ? (x / y) : 0);
}

inline qint operator%(const qint& a, const qint& b) noexcept {
    const auto x = qint_alias_detail::measure_to_int(a);
    const auto y = qint_alias_detail::measure_to_int(b);
    return qint(y != 0 ? (x % y) : 0);
}

// Mixed-type `qint + <integral>` (and symmetric). Mirrors the explicit
// overloads `qint_t<W>` carries in qint_arith{,_backend}.hpp.
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline qint operator+(const qint& a, Int c) noexcept {
    return qint(qint_alias_detail::measure_to_int(a)
              + static_cast<std::int64_t>(c));
}

template <class Int, class = qint_alias_detail::IntOp<Int>>
inline qint operator+(Int c, const qint& a) noexcept {
    return a + c;
}

// ── Mixed-type `qint OP <integral>` for the 7 non-+ ops ─────────────────
// sturm-65rs.3 / Beat A2. Forward direction only (PRD §3 — backend
// lacks reverse non-+; the alias stays symmetric). Routed through the
// inline helper `mixed_arith` to keep marginal LoC small (PRD R4).
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline qint operator-(const qint& a, Int c) noexcept {
    return qint_alias_detail::mixed_arith(a, c,
        [](std::int64_t x, std::int64_t y) noexcept { return x - y; });
}
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline qint operator*(const qint& a, Int c) noexcept {
    return qint_alias_detail::mixed_arith(a, c,
        [](std::int64_t x, std::int64_t y) noexcept { return x * y; });
}
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline qint operator/(const qint& a, Int c) noexcept {
    return qint_alias_detail::mixed_arith(a, c,
        [](std::int64_t x, std::int64_t y) noexcept { return y != 0 ? x / y : 0; });
}
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline qint operator%(const qint& a, Int c) noexcept {
    return qint_alias_detail::mixed_arith(a, c,
        [](std::int64_t x, std::int64_t y) noexcept { return y != 0 ? x % y : 0; });
}
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline qint operator&(const qint& a, Int c) noexcept {
    return qint_alias_detail::mixed_arith(a, c,
        [](std::int64_t x, std::int64_t y) noexcept { return x & y; });
}
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline qint operator|(const qint& a, Int c) noexcept {
    return qint_alias_detail::mixed_arith(a, c,
        [](std::int64_t x, std::int64_t y) noexcept { return x | y; });
}
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline qint operator^(const qint& a, Int c) noexcept {
    return qint_alias_detail::mixed_arith(a, c,
        [](std::int64_t x, std::int64_t y) noexcept { return x ^ y; });
}

// ── Arithmetic: unary - ───────────────────────────────────────────────
inline qint operator-(const qint& a) noexcept {
    return qint(-qint_alias_detail::measure_to_int(a));
}

// ── Compare: == != < <= > >= ─────────────────────────────────────────
// Lossy by design (PRD §4.1): post-measurement compare returns classical
// `bool`. `qbool` (the qint_t<W> compare result) requires committing to a
// backend width, which the alias deliberately does not.
inline bool operator==(const qint& a, const qint& b) noexcept {
    return qint_alias_detail::measure_to_int(a)
        == qint_alias_detail::measure_to_int(b);
}
inline bool operator!=(const qint& a, const qint& b) noexcept {
    return qint_alias_detail::measure_to_int(a)
        != qint_alias_detail::measure_to_int(b);
}
inline bool operator<(const qint& a, const qint& b) noexcept {
    return qint_alias_detail::measure_to_int(a)
         < qint_alias_detail::measure_to_int(b);
}
inline bool operator<=(const qint& a, const qint& b) noexcept {
    return qint_alias_detail::measure_to_int(a)
        <= qint_alias_detail::measure_to_int(b);
}
inline bool operator>(const qint& a, const qint& b) noexcept {
    return qint_alias_detail::measure_to_int(a)
         > qint_alias_detail::measure_to_int(b);
}
inline bool operator>=(const qint& a, const qint& b) noexcept {
    return qint_alias_detail::measure_to_int(a)
        >= qint_alias_detail::measure_to_int(b);
}

// Mixed-type compare `qint OP <integral>` — see header banner.
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline bool operator==(const qint& a, Int c) noexcept {
    return qint_alias_detail::measure_to_int(a) == static_cast<std::int64_t>(c);
}
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline bool operator!=(const qint& a, Int c) noexcept {
    return qint_alias_detail::measure_to_int(a) != static_cast<std::int64_t>(c);
}
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline bool operator<(const qint& a, Int c) noexcept {
    return qint_alias_detail::measure_to_int(a) <  static_cast<std::int64_t>(c);
}
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline bool operator<=(const qint& a, Int c) noexcept {
    return qint_alias_detail::measure_to_int(a) <= static_cast<std::int64_t>(c);
}
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline bool operator>(const qint& a, Int c) noexcept {
    return qint_alias_detail::measure_to_int(a) >  static_cast<std::int64_t>(c);
}
template <class Int, class = qint_alias_detail::IntOp<Int>>
inline bool operator>=(const qint& a, Int c) noexcept {
    return qint_alias_detail::measure_to_int(a) >= static_cast<std::int64_t>(c);
}

// ── Bitwise: binary & | ^ ─────────────────────────────────────────────
inline qint operator&(const qint& a, const qint& b) noexcept {
    return qint(qint_alias_detail::measure_to_int(a)
              & qint_alias_detail::measure_to_int(b));
}
inline qint operator|(const qint& a, const qint& b) noexcept {
    return qint(qint_alias_detail::measure_to_int(a)
              | qint_alias_detail::measure_to_int(b));
}
inline qint operator^(const qint& a, const qint& b) noexcept {
    return qint(qint_alias_detail::measure_to_int(a)
              ^ qint_alias_detail::measure_to_int(b));
}

// ── Bitwise: unary ~ ─────────────────────────────────────────────────
inline qint operator~(const qint& a) noexcept {
    return qint(~qint_alias_detail::measure_to_int(a));
}

// ── Bitwise: shifts (rhs = classical int) ────────────────────────────
inline qint operator<<(const qint& a, int n) noexcept {
    const auto x = qint_alias_detail::measure_to_int(a);
    return qint((n >= 0 && n < 64) ? (x << n) : 0);
}

inline qint operator>>(const qint& a, int n) noexcept {
    const auto x = qint_alias_detail::measure_to_int(a);
    return qint((n >= 0 && n < 64) ? (x >> n) : 0);
}

// ── Compound assigns ──────────────────────────────────────────────────
// Free-function form keeps qint_alias.hpp's class body minimal. Each
// stub goes through the corresponding `a OP b` free operator above —
// which measures both operands — and copy-assigns into LHS. The copy-
// assign does NOT measure (RHS is a freshly-constructed classical qint).

inline qint& operator+=(qint& a, const qint& b) noexcept { a = a + b; return a; }
inline qint& operator-=(qint& a, const qint& b) noexcept { a = a - b; return a; }
inline qint& operator*=(qint& a, const qint& b) noexcept { a = a * b; return a; }
inline qint& operator/=(qint& a, const qint& b) noexcept { a = a / b; return a; }
inline qint& operator%=(qint& a, const qint& b) noexcept { a = a % b; return a; }
inline qint& operator&=(qint& a, const qint& b) noexcept { a = a & b; return a; }
inline qint& operator|=(qint& a, const qint& b) noexcept { a = a | b; return a; }
inline qint& operator^=(qint& a, const qint& b) noexcept { a = a ^ b; return a; }
inline qint& operator<<=(qint& a, int n)        noexcept { a = a << n; return a; }
inline qint& operator>>=(qint& a, int n)        noexcept { a = a >> n; return a; }

} // namespace frontend
} // namespace sturm
