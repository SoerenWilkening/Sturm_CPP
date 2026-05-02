// qram_oos_write.cpp — sturm-u9ge.16 (Beat E1) input-only fixture for
// the write out-of-scope shape.
//
// PRD §9 row 2: `a[i] = b;` (subscript on the LHS of an assignment).
// QRAM-write is a different unitary from QRAM-read; the v1 matcher
// (sturm-u9ge.12 / Beat C1) handles only the read direction. The OOS
// matcher (`matcher_qram_oos.cpp`, sturm-u9ge.16) MUST emit exactly
// one Error-severity diagnostic embedding the `qram-oos-write` token.
//
// Hermetic stub: same shape as the other E1 fixtures. The implicit
// `operator size_t()` is the load-bearing UDC site.

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
} } // namespace sturm::frontend

using qint = sturm::frontend::qint;

void demo(qint a[4], qint i, qint b) {
    // Write — different unitary from read. v1 only rewrites reads.
    a[i] = b;
}
