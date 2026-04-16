// Phase G / PG-2 input for the sturm-transpile snapshot test.
//
// Exercises the named+named nested-WHEN lift:
//
//     WHEN(a) {              // outer: bare DeclRefExpr to qbool
//         WHEN(b) {          // inner: bare DeclRefExpr to qbool
//             (void)c;
//         }
//     }
//
// The Phase G PG-2 matcher fires on the `(outer=a, inner=b)` pair and
// performs two source-level edits:
//
//   (1) Inject `qbool __stu_ctrl0 = a & b;\n` immediately before the
//       inner `WHEN(b)` spelling — staged as a `UncomputeInsertion` on
//       `QUnit::raw_insertions` with anchor `sm.getExpansionLoc(inner_if
//       ->getIfLoc())` (the file loc of the `W` in the inner `WHEN`).
//   (2) Rewrite the inner `materialize_when` argument from `b` to
//       `__stu_ctrl0` — staged as a `QReplacement` over the arg's spelling
//       range, normalised via `Lexer::makeFileCharRange`.
//
// PG-3 will layer the companion `uncompute_and(__stu_ctrl0, a, b);` call
// on top via a synthetic `QOperation{kind=AND}`. This PG-2 slice stops
// short of that, which means the generated fixture below has:
//   - the decl block before the inner `WHEN`;
//   - the inner WHEN arg replaced with `__stu_ctrl0`;
//   - NO `uncompute_and(...)` call — that is PG-3's job.
//
// The outer WHEN is NOT rewritten: Phase G emission only touches the
// INNER side of each matched pair (the outer's arg remains spelled
// verbatim as `a`). Phase F's named-passthrough short-circuit also skips
// the outer because its peeled payload is a bare DeclRefExpr.
//
// The stub inlined below replicates the structure the matcher inspects:
//   - `sturm::qbool` with `operator|`/`operator&` so the qbool type-checks.
//   - `sturm::detail::materialize_when` overload set the WHEN macro's
//     middle `if` calls in its init-stmt.
//   - The exact three-`if` WHEN macro from `include/sturm/control/when.hpp`.
//     A no-op `WhenCapture` / `make_when_guard` pair keeps the init-stmts
//     type-checked without dragging in the full runtime's `WhenGuard` /
//     `QubitPool`.
//
// The sturm-transpile binary runs with a FixedCompilationDatabase that
// carries no include paths, so this file must be hermetic — same rule
// the Phase A..F snapshot fixtures follow.
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

void demo(qbool a, qbool b, qbool c) {
    WHEN(a) { WHEN(b) { (void)c; } }
}
