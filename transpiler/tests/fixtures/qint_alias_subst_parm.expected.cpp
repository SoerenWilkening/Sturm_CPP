// qint_alias_subst_parm.cpp -- sturm-65rs.9 (Beat C2) input fixture.
//
// PRD §4.3 / Plan §10. ParmVarDecl anchor. Per PRD §3 non-goal:
// "Width inference for ParmVarDecl / FieldDecl / return-type beyond
// the rule-3 default" is out of scope (follow-up sturm-65rs.17). The
// emitter falls through to `kDefaultWidth = 32` for parameter types.

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

void demo(sturm::qint_t<32> p) {
    (void)p;
}
