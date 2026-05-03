// qram_oos_expression_position.cpp — sturm-u9ge.16 (Beat E1) input-only
// fixture for the expression-position out-of-scope shape.
//
// PRD §9 row 4: subscript embedded inside a larger expression where v1
// does NOT synthesise the rewrite. As of sturm-u9ge.9 (Beat H4) the
// VarDecl-init form `qint c = a[i] + d;` is H4 territory (rewritten by
// `matcher_qram_subscript_expr.{hpp,cpp}` + `qram_emitter_expr.{hpp,cpp}`).
// The OOS diagnostic now fires only on TRUE non-init expression-position
// uses — function-call arguments, return-stmt bodies, if-conditions,
// etc. — where ancilla extraction + uncompute is still not synthesised.
//
// This fixture exercises the function-call-argument case: passing
// `a[i] + d` to a function that takes a `qint` parameter. The OOS
// matcher MUST emit exactly one Error-severity diagnostic embedding the
// `qram-oos-expression-position` token here. (The H4 matcher does NOT
// fire on this shape because `a[i] + d` is not a VarDecl initializer.)
//
// Hermetic stub: same shape as the other E1 fixtures, with the binary
// `operator+` overload that lets the `a[i] + d` expression type-check.

namespace sturm { namespace frontend {
class qint {
public:
    qint() noexcept = default;
    qint(long long v) noexcept : value_(v) {}
    qint(const qint&) noexcept = default;
    qint(qint&&)      noexcept = default;
    qint& operator=(const qint&) noexcept = default;
    qint& operator=(qint&&)      noexcept = default;
    ~qint()                      noexcept = default;
    operator unsigned long() const noexcept {
        return static_cast<unsigned long>(value_);
    }
private:
    long long value_ = 0;
};
inline qint operator+(const qint& a, const qint& b) noexcept {
    (void)a; (void)b; return qint{};
}
} } // namespace sturm::frontend

using qint = sturm::frontend::qint;

void sink(qint x) { (void)x; }

void demo(qint a[4], qint i, qint d) {
    // True non-init expression-position read: needs ancilla extract +
    // uncompute, which v1 does not synthesise even after H4 (H4 only
    // covers the VarDecl-init form).
    sink(a[i] + d);
}
