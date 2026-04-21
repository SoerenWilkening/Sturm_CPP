// PM3-4 positive-case input fixture for the
// `plugin_diagnostic_when_operand_mutation_fires` CTest.
//
// Purpose
// -------
// Pin the PM3-4 "Class 1 — WHEN operand mutation" contract: a
// `WHEN(expr) { body }` invocation whose body mutates a qbool/qint
// `DeclRefExpr` that also appears in `expr` MUST raise the
// `DiagnosticsEngine::Error` configured by
// `DiagContext::report_when_operand_mutation`. Error severity — per
// P4 ("the operands of a WHEN control expression must not be modified
// within the scope; violation is undefined behaviour") — compilation
// MUST abort non-zero.
//
// Shape
// -----
// A minimal hermetic `sturm::qbool` stub plus a WHEN macro that mirrors
// the real `include/sturm/control/when.hpp:225-227` definition (two
// nested `if`s whose init-stmts declare `_when_val_` and `_when_guard_`).
// The `demo(a, b)` function invokes `WHEN(a | b)` and mutates `a ^= 1;`
// inside the body — `a` is an operand of the control expression, so
// the PM3-4 matcher MUST fire.
//
// Expected diagnostic substring on stderr:
//     WHEN operand 'a' is mutated inside the WHEN body
// AND exit code non-zero.
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
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

void demo(qbool a, qbool b) {
    // The PM3-4 diagnostic MUST cite the mutation line below —
    // `a` is an operand of the control expression `a | b` and is
    // mutated inside the body, which is undefined behaviour per P4.
    WHEN(a | b) { a ^= 1; }
}
