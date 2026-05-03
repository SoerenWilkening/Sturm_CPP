// qram_read_std_array.cpp — sturm-u9ge.12 (Beat C1) input-only fixture.
//
// PRD §7 first row: `qint b = a[i];` where `a` is `std::array<qint_t<W>, N>`.
// AST shape: `CXXOperatorCallExpr` on `array::operator[]` with the index
// going through `frontend::qint::operator size_t()` (the UDC discriminator).
//
// The C1 matcher MUST surface exactly one `QramSubscriptHit` here with
// `kind == StdArray`. The hermetic stub mirrors the v1 frontend `qint`
// (implicit `operator size_t`) and a minimal `std::array` stand-in (a
// templated struct with a templated `operator[]`).

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

template <typename T, unsigned long N>
struct array {
    T data_[N];
    T& operator[](unsigned long i)             { return data_[i]; }
    const T& operator[](unsigned long i) const { return data_[i]; }
};

void demo(qint i) {
    array<sturm::qint_t<8>, 4> a;
    sturm::qint_t<8> b; ::sturm::QRAM_read(a, i, b);
    (void)b;
}
