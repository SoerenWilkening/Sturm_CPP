// sturm-qzab.1 (P5 beat 5.1) input — `(a + b) % n` modular-add rewrite.
//
// The modular-op matcher (matcher_modular_op.cpp) recognises the AST
// shape `qint_t<W> r = (a + b) % n;` and the rewrite emitter
// (modular_rewrite_emitter.cpp) replaces the entire VarDecl statement
// with a single call to the `sturm::add_mod` free function (PRD §2.1
// / §3.1). The hermetic `qint_t<W>` stub below mirrors the Phase B
// `add_assign_const.cpp` fixture so the matcher can resolve the
// CXXOperatorCallExpr AST shape without an `#include <sturm/sturm.hpp>`
// (the transpiler's FixedCompilationDatabase carries no include paths).
namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    qint_t& operator=(const qint_t&) { return *this; }
};
template <int W>
inline qint_t<W> operator+(const qint_t<W>&, const qint_t<W>&) {
    return qint_t<W>{};
}
template <int W>
inline qint_t<W> operator%(const qint_t<W>&, const qint_t<W>&) {
    return qint_t<W>{};
}
} // namespace sturm
using qint = sturm::qint_t<2>;

void demo(qint a, qint b, qint n) {
    qint r = (a + b) % n;
    (void)r;
}
