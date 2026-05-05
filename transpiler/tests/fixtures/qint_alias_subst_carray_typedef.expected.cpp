// qint_alias_subst_carray_typedef.cpp -- sturm-7t85.2 (Beat G3) input.
//
// PRD §9.3.1 / §9.5 R5, Plan §19d / §21. R5 pin: when a user names
// the array carrier through a TypeAliasDecl
// (`using QArr = qint[4];`), the alias-subst matcher (G1,
// sturm-7t85.1) MUST NOT rewrite the resulting VarDecl. The G1
// `typeloc_range_of(DeclaratorDecl*)` walk into
// `ArrayTypeLoc::getElementLoc()` lands on a `TypedefTypeLoc` for
// `QArr`; the matcher returns an invalid `SourceRange` and the
// emitter drops the match. Net effect: the input file round-trips
// byte-identical through the C1+C2 pipeline.
//
// Why this matters: rewriting `QArr a;` to anything other than
// `QArr a;` would either erase the user's typedef name (silent
// rewrite to `sturm::qint_t<32> a[4];`) or insert a backend type on
// the RHS of a typedef definition the user wrote with frontend
// semantics — both surprises the wave-2 PRD explicitly forbids
// (§9.5 R5: "Element TypeLoc walks through user typedef" risk).
//
// Hermetic stub mirrors `sturm::frontend::qint` and `sturm::qint_t<W>`
// closely enough to type-check; matches the wave-1 alias-subst
// fixture preamble exactly. The local `using qint = ...;` binding
// keeps the user-typedef shape (`using QArr = qint[4];`) faithful
// to the PRD spelling.

namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
};
namespace frontend {
class qint {
public:
    qint() noexcept = default;
    qint(long long v) noexcept : value_(v) {}
    qint(const qint&) noexcept = default;
    qint& operator=(const qint&) noexcept = default;
private:
    long long value_ = 0;
};
} // namespace frontend
} // namespace sturm

using qint = sturm::frontend::qint;
using QArr = qint[4];

void demo() {
    QArr a;
    (void)a;
}
