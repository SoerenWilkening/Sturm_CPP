// LP3 RED fixture for the sturm-transpile lazy-path snapshot test.
//
// The MVP matcher (M8) matches the EAGER initializer shape — where
// `operator|` returns a bare `qbool`. Under `STURM_BACKEND_ENABLED=1`
// (examples/or_circuit.cpp, driven via lazy_expr.hpp), `operator|`
// instead returns an `OrExpr<qbool>` which is materialized to `qbool`
// via a user-defined conversion. The MVP matcher does not peel this
// chain and therefore fails to inject `uncompute_or(...)` on the
// real example.
//
// This hermetic fixture reproduces that exact shape minimally so the
// regression is visible without depending on backend headers. LP2's
// AST calibration confirmed the call-site tree here is byte-identical
// to the tree produced by the real `lazy_expr.hpp` — see the notes on
// issue sturm-ea7.
//
// LP3 lands this fixture with a PLACEHOLDER expected file; LP4 widens
// the matcher and freezes the real golden output. While LP3 is on
// HEAD and LP4 is not yet landed, `ctest -R snapshot_or_single_backend`
// MUST fail — that failure is the RED signal LP4 flips to GREEN.
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
};

template<class T>
struct OrExpr {
    const T& a;
    const T& b;
    OrExpr(const T& a_, const T& b_) : a(a_), b(b_) {}
    operator T() const { return T{}; }   // user-defined conversion
};

inline OrExpr<qbool> operator|(const qbool& a, const qbool& b) {
    return OrExpr<qbool>{a, b};
}
} // namespace sturm
using sturm::qbool;

void demo(qbool a, qbool b) { qbool tmp = a | b; }
