// qram_oos_rmw.cpp — sturm-u9ge.16 (Beat E1) input-only fixture for the
// read-modify-write out-of-scope shape.
//
// PRD §9 row 3: `a[i] += b;` (and other compound-assigns). RMW
// decomposes as a QRAM-read followed by an adjusted QRAM-write — a
// composition v1 does not synthesise. The OOS matcher
// (`matcher_qram_oos.cpp`, sturm-u9ge.16) MUST emit exactly one
// Error-severity diagnostic embedding the `qram-oos-rmw` token.
//
// Hermetic stub: same shape as the other E1 fixtures, with the
// in-place compound-assign overload (`operator+=`) that lets the
// `a[i] += b` line type-check. The matcher fires on the AST shape, not
// on the operator's body.

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
inline qint& operator+=(qint& a, const qint& b) noexcept {
    (void)b; return a;
}
} } // namespace sturm::frontend

using qint = sturm::frontend::qint;

void demo(qint a[4], qint i, qint b) {
    // RMW: read + adjusted-write composition. Out of scope for v1.
    a[i] += b;
}
