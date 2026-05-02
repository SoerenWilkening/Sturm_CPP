// qram_oos_existing_target.cpp — sturm-u9ge.16 (Beat E1) input-only fixture
// for the existing-target out-of-scope shape.
//
// PRD §9 row 1: `b = a[i];` where `b` is a pre-existing `qint`. The shape
// is out of scope for v1 because the QRAM-read unitary writes into the
// destination register — to honour `b`'s prior contents, the compiler
// must first uncompute the old value, an operation v1 deliberately does
// not synthesise. The OOS matcher (`matcher_qram_oos.cpp`,
// sturm-u9ge.16) MUST emit exactly one Error-severity diagnostic
// embedding the `qram-oos-existing-target` token.
//
// Hermetic stub: the load-bearing piece is the implicit `operator
// size_t()` on the frontend `qint` class — that is the UDC node the
// matcher's discriminator pivots on. The body is irrelevant; the
// matcher inspects the conversion DECL only.

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

void demo(qint a[4], qint i) {
    qint b;
    // Pre-existing target — needs uncompute of `b` before the QRAM
    // read writes into it. Out of scope for v1.
    b = a[i];
    (void)b;
}
