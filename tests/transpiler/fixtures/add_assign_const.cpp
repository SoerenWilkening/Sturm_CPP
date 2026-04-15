// Phase B / PB-1 input for the sturm-transpile snapshot test.
//
// Exercises the classical-constant RHS form of ADD_ASSIGN on a qint_t:
// `a += 3;` must be paired with `a -= 3;` injected before the scope's
// close brace. The RHS `3` reaches `operator+=(const qint_t&)` through
// the implicit qint_t(long long) converting constructor; the matcher
// peels the CXXConstructExpr wrapper and extracts the verbatim "3"
// token via Lexer::getSourceText.
namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(long long) {}
    qint_t& operator+=(const qint_t&) { return *this; }
};
} // namespace sturm
using qint = sturm::qint_t<1>;

void demo(qint a) { a += 3; }
