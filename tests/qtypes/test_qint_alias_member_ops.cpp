// test_qint_alias_member_ops.cpp — sturm-65rs.2 (Beat A1) + sturm-v0db.2 (W3.1).
//
// Pins the contract for the new MEMBER operators on
// `sturm::frontend::qint` declared in `include/sturm/qtypes/qint_alias.hpp`.
// Plan §4 / A1 names three:
//
//   * `qint& operator=(int64_t v) noexcept`           — classical assign.
//   * `qbool operator[](std::size_t k) const noexcept` — bit read
//                                                        (W3.1: returns
//                                                        `qbool`, was
//                                                        `bool` pre-W3).
//   * `explicit operator int64_t() const noexcept`    — cast to int64_t.
//
// Wave 3 update (sturm-v0db.2 / W3.1, PRD §10.3.5):
//   * Runtime measurement-counter assertions are GONE — the counter
//     is being deleted in W3.4 (G9). One ctor-driven probe replaces
//     the runtime bodies (round-trip a classical value through the
//     `qint(int64_t)` ctor and read it back via `classical_value()`,
//     no operator bodies executed).
//   * Three SFINAE drift-gates retained (`has_assign_int64`,
//     `has_subscript`, `has_explicit_int64`) — they pin signatures.
//   * NEW: `is_same_v` pin for `decltype(q[size_t{}])` returning
//     `sturm::qbool` (PRD §10.3.5 / G7). Duplicates the assertion in
//     `test_qint_alias_ops.cpp` intentionally — local-to-file
//     coverage so this test file stays self-sufficient if the wider
//     drift-gate moves.
//
// LoC budget: <= 300 (plan §1, §4 / A1).

#include "sturm/qtypes/qint_alias.hpp"
#include "sturm/qtypes/qbool.hpp"   // sturm::qbool — W3.1 return-type pin target

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

// ── Local alias mirroring the user-facing spelling from PRD §4 ────────────
using qint = sturm::frontend::qint;

// ── (1) Ctor-driven probe — round-trip a classical value ─────────────────
// PRD §10.3.5: "Any test that constructs a `frontend::qint` and reads its
// value through an operator (rather than `classical_value()`): rewrite
// to use the `classical_value()` accessor or delete." We use the
// implicit `qint(int64_t)` ctor (P4a — classical-to-quantum is free)
// and read via `classical_value()`. No operator bodies execute, so the
// W3.3 stub-body strip is invisible to this probe.
static void test_ctor_classical_round_trip() {
    qint q(0x1234);
    assert(q.classical_value() == 0x1234);
}

// ── (2) Drift-gate: the alias must continue to expose the operators ──────
// Plan §4 / A1 names all three; we pin them at compile time so any
// future refactor that drops one fires.
namespace harness {

// has_assign_int64<T>: true iff `T{} = int64_t{}` is well-formed.
template <class, class = void>
struct has_assign_int64 : std::false_type {};
template <class T>
struct has_assign_int64<T, std::void_t<decltype(std::declval<T&>() = std::declval<std::int64_t>())>>
    : std::true_type {};

// has_subscript<T>: true iff `T{}[size_t{}]` is well-formed (const).
template <class, class = void>
struct has_subscript : std::false_type {};
template <class T>
struct has_subscript<T, std::void_t<decltype(std::declval<const T&>()[std::declval<std::size_t>()])>>
    : std::true_type {};

// has_explicit_int64<T>: true iff `static_cast<int64_t>(T{})` is
// well-formed. Distinct from `is_convertible_v` (the implicit path).
template <class, class = void>
struct has_explicit_int64 : std::false_type {};
template <class T>
struct has_explicit_int64<T, std::void_t<decltype(static_cast<std::int64_t>(std::declval<const T&>()))>>
    : std::true_type {};

} // namespace harness

static_assert(harness::has_assign_int64<sturm::frontend::qint>::value,
              "frontend::qint must expose `operator=(int64_t)` (plan §4 / A1).");
static_assert(harness::has_subscript<sturm::frontend::qint>::value,
              "frontend::qint must expose `operator[](size_t) const` "
              "(plan §4 / A1).");
static_assert(harness::has_explicit_int64<sturm::frontend::qint>::value,
              "frontend::qint must expose `explicit operator int64_t() "
              "const` (plan §4 / A1).");

// ── (3) W3.1 — qbool return-type pin for op[] (PRD §10.3.5 / G7) ─────────
// `q[k]` returns `sturm::qbool` (was classical `bool` pre-W3). Duplicates
// the assertion in `test_qint_alias_ops.cpp` intentionally — local to
// the file under test, so this file fails if the contract drifts even
// when the wider drift-gate is silent.
static_assert(std::is_same_v<decltype(std::declval<const qint&>()[std::size_t{}]),
                             sturm::qbool>,
              "frontend::qint::operator[](size_t) const must return qbool "
              "(W3.1 / PRD §10.3.5 / G7).");

// ── main / runner ─────────────────────────────────────────────────────────
int main() {
    test_ctor_classical_round_trip();
    std::puts("test_qint_alias_member_ops: OK");
    return 0;
}
