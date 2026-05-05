// qint_alias_subst_carray.cpp -- sturm-7t85.2 (Beat G3) input fixture.
//
// PRD §9.3.1 / §9.3.2, Plan §19d. Wave-2 carrier coverage: the
// alias-subst matcher (G1, sturm-7t85.1) walks one level into
// `ArrayTypeLoc::getElementLoc()` so that VarDecl, ParmVarDecl, and
// FieldDecl anchors carrying a C-array of `sturm::frontend::qint`
// still bind to the element TypeLoc. The captured TypeLoc range
// covers the `qint` token only — the `[N]` / `[]` / `[3]` array
// bounds survive verbatim into the rewritten output.
//
// Width inference for ParmVarDecl / FieldDecl / function returns
// remains rule-3 (`kDefaultWidth = 32`) per PRD §3 non-goal
// (follow-up sturm-65rs.17). The VarDecl arm also lands on rule 3
// here: a bare `frontend::qint a[4];` has no `qint_t<W>` container
// on the RHS, so width inference falls through to the default.
//
// Hermetic stub mirrors `sturm::frontend::qint` and `sturm::qint_t<W>`
// closely enough to type-check; matches the wave-1 alias-subst
// fixture preamble exactly.

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

struct S {
    sturm::frontend::qint c[3];
};

void f(sturm::frontend::qint b[]);

void demo() {
    sturm::frontend::qint a[4];
    S s;
    (void)a;
    (void)s;
    f(a);
}
