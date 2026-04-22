// Phase N / PN-3 input for the sturm-transpile rotation snapshot test.
//
// Exercises the double-RHS form of THETA_SUB_ASSIGN_CONST on a qint_t:
// `a.theta() -= 0.25;` must be paired with `a.theta() += 0.25;` injected
// before the scope's close brace. The RHS `0.25` reaches ThetaProxy's
// `operator-=(double)` directly — no converting-constructor peel needed
// on the PN-2 matcher. The LHS qint_t identifier `a` is bound as the IR
// op's `result`, and the verbatim RHS source text `"0.25"` is captured
// via `Lexer::getSourceText`.
namespace sturm {
template <int W>
class qint_t {
public:
    struct ThetaProxy {
        void operator+=(double) {}
        void operator-=(double) {}
    };
    ThetaProxy theta() { return ThetaProxy{}; }
};
} // namespace sturm
using qint = sturm::qint_t<1>;

void demo(qint a) { a.theta() -= 0.25; }
