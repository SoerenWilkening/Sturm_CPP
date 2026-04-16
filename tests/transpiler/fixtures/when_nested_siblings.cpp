// Phase G / PG-3 input for the sturm-transpile snapshot test.
//
// Exercises the "siblings-around-inner-WHEN" shape:
//
//     WHEN(a) {
//         (void)c;            // sibling before the inner WHEN
//         WHEN(b) {
//             (void)d;
//         }
//         (void)c;            // sibling after the inner WHEN
//     }
//
// Phase G decision #3 (from docs/implementation_plan_transpiler_phase_g.md)
// pins the rewrite to always-lower regardless of siblings: the matcher
// still fires on the (outer=a, inner=b) pair even when the outer WHEN
// body contains extra statements before or after the inner WHEN. Only
// the inner WHEN is rewritten — the siblings are preserved verbatim.
//
// Three staged effects per pair:
//
//   (1) Inject `qbool __stu_ctrl0 = a & b;\n` immediately before the
//       inner `WHEN(b)` spelling.
//   (2) Rewrite the inner `materialize_when` argument from `b` to
//       `__stu_ctrl0`.
//   (3) Schedule an `uncompute_and(__stu_ctrl0, a, b);` call immediately
//       after the inner WHEN body's `}` via a `QOperation{kind=AND}`
//       with `insert_before_override = post_inner_brace`.
//
// The `(void)c;` placeholders before and after the inner WHEN are the
// "siblings" — ordinary statements inside the outer WHEN's body block.
// The generated file keeps them at their original positions, unchanged.
// We use placeholders instead of a free function call so the fixture
// stays hermetic (the sturm-transpile FixedCompilationDatabase has no
// include paths).
//
// The stub inlined below is byte-identical to `when_nested_named.cpp` so
// the matcher sees the same macro / materialize_when shape.
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    bool should_run() const { return true; }
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }

namespace detail {

inline qbool& materialize_when(qbool& q) { return q; }
inline qbool  materialize_when(qbool&& q) { return static_cast<qbool&&>(q); }

struct WhenCapture { WhenCapture() = default; };
inline qbool& make_when_guard(qbool& q) { return q; }

} // namespace detail
} // namespace sturm

using sturm::qbool;

#define WHEN(expr) \
    if (::sturm::detail::WhenCapture _when_capture_{}; true) \
    if (decltype(auto) _when_val_ = ::sturm::detail::materialize_when(expr); true) \
    if (auto& _when_guard_ = ::sturm::detail::make_when_guard(_when_val_); \
        _when_guard_.should_run())

void demo(qbool a, qbool b, qbool c, qbool d) {
    WHEN(a) {
        (void)c;
        WHEN(b) { (void)d; }
        (void)c;
    }
}
