// PN-6 positive-case input fixture for the
// `plugin_diagnostic_qbool_prep_fires` CTest.
//
// Purpose
// -------
// Pin the PN-5 "Class 6 — probabilistic prep inside an uncompute-eligible
// scope" contract: a `qbool x(p);` VarDecl whose initializer is a
// `double`-typed argument AND whose enclosing scope is a `WHEN(expr) { ... }`
// body MUST raise the `DiagnosticsEngine::Warning` configured by
// `DiagContext::report_prep_in_uncompute_scope`. Warning severity — NOT
// Error — because the forward emission of a prep inside a WHEN body still
// produces a physically-meaningful gate stream (a conditional Ry
// preparation). Only the uncompute half is broken (no adjoint exists per
// P9), and compilation should continue so every PN-5 shape in the TU is
// surfaced in a single pass.
//
// Shape
// -----
// A minimal hermetic `sturm::qbool` stub identical to the PM3-4 WHEN
// operand mutation fixture shape, extended with the `qbool(double)`
// probabilistic constructor that mirrors the real
// `include/sturm/qtypes/qbool.hpp:58` ctor. The WHEN macro mirrors the
// real `include/sturm/control/when.hpp:225-227` definition (two nested
// `if`s whose init-stmts declare `_when_val_` and `_when_guard_`). The
// `demo(a)` function invokes `WHEN(a) { qbool x(0.5); (void)x; }` — `x`
// is a qbool probabilistic prep inside a WHEN body, so the PN-5 matcher
// MUST fire the warning.
//
// Expected diagnostic substring on stderr:
//     qbool x preparation in uncompute-eligible scope has no adjoint (P9)
// AND exit code ZERO (Warning severity, not Error — compilation continues
// so every PN-5 violation in the TU is surfaced in a single pass).
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool(bool) {}
    explicit qbool(double) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    qbool& operator^=(int) { return *this; }
    bool should_run() const { return true; }
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }

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

void demo(qbool a) {
    // The PN-5 diagnostic MUST cite the VarDecl line below — `x` is a
    // probabilistic `qbool(double)` prep, its enclosing scope is the WHEN
    // body, and preparation is a CP map whose inverse (a discard /
    // measurement) cannot be synthesized by the transpiler without
    // violating P9.
    WHEN(a) { qbool x(0.5); (void)x; }
}
