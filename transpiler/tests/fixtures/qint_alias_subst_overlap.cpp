// qint_alias_subst_overlap.cpp — sturm-65rs.10 (Beat C3) overlap fixture.
//
// Plan §11, PRD §4.3 / R2. Pins the C3 consumer's claimed-decls
// overlap guard: a single TU containing BOTH the QRAM-subscript C1
// shape (`qint b = a[i];`) AND a bare alias-subst shape (`qint x;`).
//
// After the consumer drains both pipelines:
//   - `b` is rewritten EXACTLY ONCE by the QRAM emitter into
//     `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);` — the
//     alias-subst emitter MUST NOT also rewrite `b`'s type spelling
//     (PRD R2 single-VarDecl-single-rewrite invariant).
//   - `x` is rewritten EXACTLY ONCE by the alias-subst emitter into
//     `sturm::qint_t<32> x;` — the QRAM emitter never sees `x`
//     because its anchor (a subscript-init VarDecl) does not match.
//
// The hermetic stub mirrors `sturm::frontend::qint` (with the
// `operator size_t` UDC the QRAM C1 matcher pivots on) plus
// `sturm::qint_t<W>` for the C-array element type.

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
    sturm::frontend::qint x;
    (void)b;
    (void)x;
}
