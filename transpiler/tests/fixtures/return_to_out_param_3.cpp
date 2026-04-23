// Q-A (sturm-5kgu.2) fixture 3: qint return + const-qualified input
// parameter. The forward receives a `const qint& x` (per PRD §5.3
// the const-ref spelling is one of the accepted input shapes) and
// returns a qint — verifying Q-A preserves the const qualifier, the
// reference, and the parameter name verbatim in the twin signature.
// The returned expression is the identity `x`, mirroring a degenerate-
// but-real case where the user wraps a qint value for downstream
// consumption.
namespace sturm {
class qint {
public:
    qint() = default;
    qint(const qint&) = default;
    qint& operator=(const qint&) = default;
    qint& operator^=(const qint&) { return *this; }
};
} // namespace sturm

using sturm::qint;

[[clang::annotate("sturm::reversible")]]
qint echo(const qint& x) { return x; }
