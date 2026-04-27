// E7.M3 (sturm-va3z.3) positive-case input fixture for the
// `plugin_diagnostic_when_freevar_mutation_fires` CTest.
//
// Purpose
// -------
// Pin the E7 acceptance contract: a `WHEN(expr) { body }` invocation
// whose body mutates a free variable that the control expression
// reads MUST trigger a hard-error `clang::DiagnosticsEngine::Error`
// emitted by `check_when_freevar_writes` (E7.M2) and routed through
// the new `register_when_freevar_matcher` (E7.M3) — so the standalone
// driver exits non-zero and the user sees the offending write line
// cited in their own source file.
//
// Distinct from the PM3-4 operand-mutation matcher (a separate hard-
// error pass that targets *quantum-typed* operands of `WHEN(expr)`):
// E7's read-set is broader and includes classical variables read by
// the control expression. A classical-only fixture exercises the new
// pass without overlapping the PM3-4 matcher's coverage. We use a
// `qbool` argument as the WHEN's first operand (so the macro shape is
// the canonical one the lift matcher anchors on) but the variable
// being mutated is a *classical* `int` that participates in the
// control expression via `qbool::should_run()` short-circuiting.
//
// Shape
// -----
// Minimal hermetic `sturm::qbool` stub plus a WHEN macro mirroring
// `include/sturm/control/when.hpp`'s real definition. The control
// expression `cond_for(threshold)` reads classical `threshold` (int);
// the body mutates the same `threshold` variable via plain assign.
// `threshold` is in the WHEN free-variable read-set (E7.M1 walks the
// callee's body and folds `threshold` in) so the E7.M2 checker
// must fire one hard-error naming `threshold`.
//
// Expected stderr substring:
//     WHEN free variable 'threshold' is mutated inside the WHEN body
// AND exit code non-zero.
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(bool) {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    bool should_run() const { return true; }
};

namespace detail {

inline qbool& materialize_when(qbool& q) { return q; }
inline qbool  materialize_when(qbool&& q) { return static_cast<qbool&&>(q); }

inline qbool& make_when_guard(qbool& q) { return q; }

} // namespace detail
} // namespace sturm

using sturm::qbool;

#define WHEN(expr) \
    if (decltype(auto) _when_val_ = ::sturm::detail::materialize_when(expr); true) \
    if (auto& _when_guard_ = ::sturm::detail::make_when_guard(_when_val_); \
        _when_guard_.should_run())

// Helper whose body reads `threshold`. The E7.M1 read-set extractor
// walks the callee body when the call site sits inside the WHEN
// argument, so `threshold` ends up in the WHEN free-variable set even
// though the call site itself is `cond_for(threshold)` (only one
// DeclRefExpr to `threshold` syntactically).
qbool cond_for(int t) { return qbool(t > 0); }

void demo(int threshold) {
    // The E7 diagnostic MUST cite the mutation line below — the body
    // assigns to `threshold`, which is a free variable of the WHEN
    // control expression. P4 forbids mutating control-expression free
    // variables inside the body; the synthesized adjoint would be
    // incorrect.
    WHEN(cond_for(threshold)) { threshold = 0; }
}
