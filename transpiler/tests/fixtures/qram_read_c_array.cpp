// qram_read_c_array.cpp — sturm-u9ge.12 (Beat C1) input-only fixture.
//
// PRD §7 second row: `qint b = a[i];` where `a` is a built-in C-array
// `qint_t<W>[N]`. AST shape: `ArraySubscriptExpr` (the built-in subscript
// operator); the index slot carries the `UserDefinedConversion`
// ImplicitCastExpr produced by `frontend::qint::operator size_t()`.
//
// The C1 matcher MUST surface exactly one `QramSubscriptHit` here with
// `kind == CArray`. The discriminator differs from the pointer arm
// only by the base-expression type (a true C-array, not a pointer).

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

void demo(qint i) {
    sturm::qint_t<16> a[4];
    qint b = a[i];
    (void)b;
}
