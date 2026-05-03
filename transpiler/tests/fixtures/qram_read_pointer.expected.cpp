// qram_read_pointer.cpp — sturm-u9ge.12 (Beat C1) + sturm-u9ge.15 (D2)
// input fixture.
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
//
// PRD §11.1.6 (D2 follow-up): the pointer overload of `QRAM_read`
// needs an explicit `n` length argument. The matcher recovers `n`
// from the parameter immediately following the container in the
// enclosing function's parameter list — that's why the canonical
// fixture shape carries `(sturm::qint_t<W>* a, std::size_t n, qint i)`.

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

void demo(sturm::qint_t<32>* a, unsigned long n, qint i) {
    sturm::qint_t<32> b; ::sturm::QRAM_read(a, n, i, b);
    (void)b;
    (void)n;
}
