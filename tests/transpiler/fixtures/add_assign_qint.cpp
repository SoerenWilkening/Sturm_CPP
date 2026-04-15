// Phase C / PC-1 input for the sturm-transpile snapshot test.
//
// Exercises the qint RHS form of ADD_ASSIGN on a qint_t: `a += b;` must
// be paired with `uncompute_add_qint(a, b);` injected before the scope's
// close brace. The RHS `b` is a bare DeclRefExpr to a second qint_t
// parameter — no converting constructor fires, so the matcher sees a
// qint-qint CXXOperatorCallExpr with a plain identifier on the right.
namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    qint_t& operator+=(const qint_t&) { return *this; }
};
} // namespace sturm
using qint = sturm::qint_t<1>;

void demo(qint a, qint b) { a += b; }
