// qram_oos_expression_position.cpp — sturm-u9ge.16 (Beat E1) input-only
// fixture for the expression-position out-of-scope shape.
//
// PRD §9 row 4: `c = a[i] + d;` (subscript embedded inside a larger
// expression). v1 handles only the bare `qint b = a[i];` (or its
// existing-target variant); putting the subscript inside another
// operator requires materialising an ancilla for the read result and
// uncomputing it after the outer expression completes — a scope-exit
// dance the v1 emitter does not synthesise. The OOS matcher
// (`matcher_qram_oos.cpp`, sturm-u9ge.16) MUST emit exactly one
// Error-severity diagnostic embedding the `qram-oos-expression-position`
// token.
//
// Hermetic stub: same shape as the other E1 fixtures, with the binary
// `operator+` overload that lets the `a[i] + d` line type-check. The
// matcher fires on the AST shape.

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

void demo(qint a[4], qint i, qint d) {
    // Expression-position read: needs ancilla extract + uncompute.
    // Out of scope for v1.
    qint c = a[i] + d;
    (void)c;
}
