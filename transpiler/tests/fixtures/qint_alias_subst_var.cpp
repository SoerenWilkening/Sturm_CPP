// qint_alias_subst_var.cpp -- sturm-65rs.9 (Beat C2) input fixture.
//
// PRD §4.3 / Plan §10. VarDecl anchor with integer-literal initializer
// AND a VarDecl anchor without initializer. Width inference (rule 2)
// applies only to the LHS subscript shape; a bare `frontend::qint x{0xff};`
// has no qint_t<W> container on the RHS, so rule 2 falls through and
// rule 3 (kDefaultWidth = 32) wins. The second VarDecl `frontend::qint y;`
// has no initializer, so rule 2 cannot fire and rule 3 wins directly.
//
// Both anchors should rewrite to `sturm::qint_t<32>` per the C2 emitter's
// rule-3 fallback. The hermetic stub mirrors `sturm::frontend::qint`
// closely enough to type-check.

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

void demo() {
    sturm::frontend::qint x{0xff};
    sturm::frontend::qint y;
    (void)x;
    (void)y;
}
