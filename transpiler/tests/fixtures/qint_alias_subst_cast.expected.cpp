// qint_alias_subst_cast.cpp -- sturm-65rs.9 (Beat C2) input fixture.
//
// PRD §4.3 / Plan §10. CXXFunctionalCastExpr anchor — width = 32.
// Per the C2 contract, functional casts route through the rule-3
// default (no VarDecl, no infer_width call); the emitted text is
// `sturm::qint_t<32>(...)`.

namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(long long) {}
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
    (void)sturm::qint_t<32>(0);
}
