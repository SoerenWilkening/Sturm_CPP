// Q-A (sturm-5kgu.2) fixture 1: canonical PRD §4.1 example.
//
// `qbool marked(qint x, int T) { return x >= T; }` is the return-style
// forward the PRD uses to motivate out-param normalisation. The
// synthesised twin MUST have the shape the `.expected.cpp` sibling
// carries — PRD §4.1 pins the identifier convention (`__<fn>_out`),
// the trailing out-parameter slot, and the `^=` rewrite of the return
// expression.
namespace sturm {
class qbool {
public:
    qbool() = default;
    qbool(bool) {}
    qbool& operator=(const qbool&) = default;
    qbool& operator^=(const qbool&) { return *this; }
};
class qint {
public:
    qint() = default;
    qbool operator>=(int) const { return qbool{}; }
};
} // namespace sturm

using sturm::qbool;
using sturm::qint;

[[clang::annotate("sturm::reversible")]]
qbool marked(qint x, int T) { return x >= T; }
