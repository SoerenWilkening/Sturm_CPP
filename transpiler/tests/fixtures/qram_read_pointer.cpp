// qram_read_pointer.cpp — sturm-u9ge.12 (Beat C1) input-only fixture.
//
// PRD §7 third row: `qint b = a[i];` where `a` is `qint_t<W>*`. AST
// shape: `ArraySubscriptExpr` (the built-in subscript operator on a
// pointer); the index slot carries the `UserDefinedConversion`
// ImplicitCastExpr from `frontend::qint::operator size_t()`.
//
// The C1 matcher MUST surface exactly one `QramSubscriptHit` here with
// `kind == Pointer`. The discriminator differs from the C-array arm
// only by the base-expression type (a `qint_t<W>*` pointer, not an
// array of length N).

namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
};
namespace frontend {
class qint {
public:
    qint() noexcept = default;
    qint(long long v) noexcept : value_(v) {}
    qint(const qint&) noexcept = default;
    template <int W> qint(const qint_t<W>&) noexcept {}
    operator unsigned long() const noexcept {
        return static_cast<unsigned long>(value_);
    }
private:
    long long value_ = 0;
};
} // namespace frontend
} // namespace sturm

using qint = sturm::frontend::qint;

void demo(sturm::qint_t<32>* a, qint i) {
    qint b = a[i];
    (void)b;
}
