// LO-0.1 (sturm-nw7c): bare-scope `/=` desugar fixture (input).
//
// Exercises the PRD §2.2 / §2.4 desugar of the qint RHS form of
// DIV_ASSIGN on a qint_t: `a /= b;` must be paired with the LO emission
// `qint __sturm_tmp_div_0_q, __sturm_tmp_div_0_r;
// divide_oop(a, b, __sturm_tmp_div_0_q, __sturm_tmp_div_0_r);
// swap(a, __sturm_tmp_div_0_q);` plus the matching scope-exit cleanup
// `swap(a, __sturm_tmp_div_0_q);
// sturm::invert<&::sturm::lib_div_dsl>()(a, b, __sturm_tmp_div_0_q,
// __sturm_tmp_div_0_r);`. The RHS `b` is a bare DeclRefExpr to a second
// qint_t parameter — no converting constructor fires, so the matcher
// sees a qint-qint CXXOperatorCallExpr with a plain identifier on the
// right. See plan §2 and PRD §2.2 / §2.4.
namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    qint_t& operator/=(const qint_t&) { return *this; }
};
} // namespace sturm
using qint = sturm::qint_t<1>;

void demo(qint a, qint b) { a /= b; }
