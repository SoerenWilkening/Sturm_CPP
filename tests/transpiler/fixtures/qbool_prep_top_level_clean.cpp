// PN-6 negative-case (clean) input fixture for the
// `plugin_diagnostic_qbool_prep_clean` CTest.
//
// Purpose
// -------
// Pin the NEGATIVE side of the PN-5 triage: a `qbool x(p);` VarDecl with
// the same probabilistic `double` initializer as the positive fixture,
// but at function-body top-level scope (NOT inside a WHEN body and NOT
// owned by any compound-expression intermediate scope), MUST NOT raise
// the PN-5 warning. Per the PN-5 matcher's scope classification, a
// `ScopeKind::Function` anchor is a valid P5 item 1 use — preparation
// at top level is the canonical entry point for probabilistic qbits
// and has no uncompute implications.
//
// Shape
// -----
// Identical hermetic `sturm::qbool` stub and WHEN macro as the positive
// fixture (`qbool_prep_in_when.cpp`) so the two fixtures share exactly
// the same `sturm::qbool` ctor set, `operator|`, `operator&`, WHEN
// macro expansion surface, and `using` declaration. The ONLY difference
// is the placement of the `qbool x(0.5);` VarDecl — positive: inside
// `WHEN(a) { ... }`; this clean fixture: at function-body top level,
// before any WHEN invocation.
//
// Acceptance (per PN-6 issue):
//   - sturm-transpile exits zero (no error; matcher stays silent).
//   - stderr does NOT contain the substring
//     `preparation in uncompute-eligible scope` (if it did, the matcher
//     fired on a shape it should have rejected — top-level prep is a
//     valid P5 use).
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
    // `x` is a probabilistic `qbool(double)` prep at function-body
    // top-level scope. The PN-5 matcher's `classify_scope_kind`
    // pathway returns `ScopeKind::Function` here and early-returns
    // silently — top-level prep is a valid P5 item 1 use and has no
    // uncompute implications.
    qbool x(0.5);
    (void)x;
    // Include the WHEN invocation so the fixture covers the same
    // parse surface as the positive fixture — the matcher must walk
    // past this WHEN body without surfacing any prep diagnostic
    // because no prep exists inside it.
    WHEN(a) { (void)a; }
}
