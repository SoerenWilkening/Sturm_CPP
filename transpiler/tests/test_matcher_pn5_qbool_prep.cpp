// test_matcher_pn5_qbool_prep.cpp — Phase N PN-5: qbool(p) prep diagnostic matcher.
//
// The PN-5 matcher fires a Warning-severity DiagContext diagnostic when a
// `qbool x(p);` VarDecl whose initializer is a probabilistic `double`
// appears inside an uncompute-eligible scope (a WHEN body or a
// compound-expression intermediate). At top-level function-body scope
// the matcher is silent — prep there is a valid P5 item 1 use.
//
// Tests exercise the full PN-5 pipeline by registering the matcher
// against a unit-local MatchFinder + standalone DiagnosticsEngine
// (Ignoring client; the test asserts on the detection counter, not on
// formatted stderr). The shared `DiagContext` is constructed from the
// throwaway engine so the matcher's report path lines up end-to-end.

#include "test_matcher_harness.hpp"

#include "diag_context.hpp"
#include "sturm/transpile/matcher.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_matcher_pn5_qbool_prep_ns {

using namespace sturm::transpile;

namespace {

// Minimal `sturm::qbool` stub that exposes:
//   - a default constructor (used when the body declares a fresh qbool
//     with no initializer for negative classical shapes);
//   - a `qbool(bool)` converting constructor — classical init; the
//     PN-5 guard's isBooleanType check must swallow any call through
//     this overload silently;
//   - a `qbool(double)` constructor — probabilistic prep; the matcher
//     only fires when the argument's peeled shape does NOT satisfy
//     `isBooleanType()` / is NOT a `CXXBoolLiteralExpr`. Mirroring
//     the real `include/sturm/qtypes/qbool.hpp:58` probabilistic ctor
//     keeps the fixture AST shape close to production source.
//
// WHEN is defined as the same two-`if` tower the PM3-4 /
// when_operand_mutation fixtures use. This stub does NOT include
// `operator|` / `operator&` / `materialize_when` as thin free
// functions — a compound-expression argument is captured verbatim
// through the macro's `decltype(auto)` init-stmt by the real
// `sturm::detail::materialize_when` overload; our hermetic stub
// spells the same overload so the WHEN body compiles.
constexpr std::string_view kQBoolPrepStub = R"CPP(
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
)CPP";

struct PN5Run {
    QUnit unit;
    int detections = 0;
};

PN5Run run_pn5_qbool_prep_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolPrepStub.size() + user_src.size());
    code.append(kQBoolPrepStub);
    code.append(user_src);

    PN5Run out;
    reset_qbool_prep_detection_count_for_test();

    clang::ast_matchers::MatchFinder finder;
    // The PN-5 matcher requires a DiagContext for its diagnostic
    // path. Unit tests do not own a CompilerInstance, so we build a
    // standalone DiagnosticsEngine with a throwaway
    // IgnoringDiagConsumer — the test asserts on the detection
    // counter, not on formatted stderr output. Same pattern the PH-3
    // outer-var-guard test harness uses.
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> ids(
        new clang::DiagnosticIDs());
    llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> opts(
        new clang::DiagnosticOptions());
    clang::DiagnosticsEngine diag_engine(
        ids, opts.get(), new clang::IgnoringDiagConsumer(),
        /*ShouldOwnClient=*/true);
    DiagContext diag_ctx(diag_engine);

    register_qbool_prep_matcher(finder, out.unit, diag_ctx);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PN-5 qbool prep matcher)\n");
    }
    out.detections = qbool_prep_detection_count_for_test();
    return out;
}

// ── Positive cases ──────────────────────────────────────────────────────────

void test_pn5_qbool_prep_inside_when_is_flagged() {
    // qbool x(0.5); inside a WHEN body — the canonical PN-5 shape. The
    // matcher walks the VarDecl's parent chain to the enclosing IfStmt
    // whose begin loc is inside the `WHEN` macro expansion, fires the
    // diagnostic, and bumps the detection counter.
    PN5Run r = run_pn5_qbool_prep_matcher(
        "void demo(qbool a) {\n"
        "    WHEN(a) { qbool x(0.5); (void)x; }\n"
        "}\n");
    CHECK(r.detections == 1);
}

void test_pn5_qbool_prep_with_variable_double_inside_when_is_flagged() {
    // qbool x(p); where `p` is a `double` variable — the argument type
    // is not a bool literal and isBooleanType() returns false, so the
    // classical guard does not kick in. Fires the diagnostic.
    PN5Run r = run_pn5_qbool_prep_matcher(
        "void demo(qbool a, double p) {\n"
        "    WHEN(a) { qbool x(p); (void)x; }\n"
        "}\n");
    CHECK(r.detections == 1);
}

void test_pn5_qbool_prep_with_literal_zero_inside_when_is_flagged() {
    // qbool x(0.0); — the argument is a FloatingLiteral, NOT a
    // CXXBoolLiteralExpr, and isBooleanType() is false. Per PN-5
    // §14 risk 4 the designed behavior is to fire the warning on
    // every double-typed argument regardless of value.
    PN5Run r = run_pn5_qbool_prep_matcher(
        "void demo(qbool a) {\n"
        "    WHEN(a) { qbool x(0.0); (void)x; }\n"
        "}\n");
    CHECK(r.detections == 1);
}

// ── Negative cases ──────────────────────────────────────────────────────────

void test_pn5_qbool_prep_at_function_top_level_is_silent() {
    // qbool x(0.5); at function-body top-level scope — classified as
    // Function by classify_scope_kind, so the matcher early-returns
    // silently. Prep here is a valid P5 use.
    PN5Run r = run_pn5_qbool_prep_matcher(
        "void demo() {\n"
        "    qbool x(0.5);\n"
        "    (void)x;\n"
        "}\n");
    CHECK(r.detections == 0);
}

void test_pn5_qbool_prep_bool_literal_true_is_silent() {
    // qbool x(true); — CXXBoolLiteralExpr argument. Classical init.
    // The guard swallows this silently even inside a WHEN body.
    PN5Run r = run_pn5_qbool_prep_matcher(
        "void demo(qbool a) {\n"
        "    WHEN(a) { qbool x(true); (void)x; }\n"
        "}\n");
    CHECK(r.detections == 0);
}

void test_pn5_qbool_prep_bool_literal_false_is_silent() {
    // qbool x(false); — CXXBoolLiteralExpr argument. Same classical
    // guard as the `true` case — silent.
    PN5Run r = run_pn5_qbool_prep_matcher(
        "void demo(qbool a) {\n"
        "    WHEN(a) { qbool x(false); (void)x; }\n"
        "}\n");
    CHECK(r.detections == 0);
}

void test_pn5_qbool_prep_bool_variable_is_silent() {
    // qbool x(b); where `b` is a `bool` — not a literal, but
    // isBooleanType() returns true, so the guard swallows silently.
    PN5Run r = run_pn5_qbool_prep_matcher(
        "void demo(qbool a, bool b) {\n"
        "    WHEN(a) { qbool x(b); (void)x; }\n"
        "}\n");
    CHECK(r.detections == 0);
}

void test_pn5_qbool_no_initializer_is_silent() {
    // qbool x; at any scope — no initializer, so the
    // `hasInitializer(cxxConstructExpr(...))` pattern does not bind.
    // The matcher never fires.
    PN5Run r = run_pn5_qbool_prep_matcher(
        "void demo(qbool a) {\n"
        "    WHEN(a) { qbool x; (void)x; }\n"
        "}\n");
    CHECK(r.detections == 0);
}

void test_pn5_qbool_prep_copy_init_is_silent() {
    // qbool x(y); where `y` is another qbool — copy-construct. The
    // CXXConstructExpr binds, but the argument is a DeclRefExpr to a
    // qbool; neither a CXXBoolLiteralExpr nor isBooleanType nor a
    // double. The matcher is specifically about probabilistic
    // preparation via the `qbool(double)` ctor, so a qbool-typed
    // copy must not fire.
    //
    // Implementation-wise: the PN-5 matcher pattern restricts the
    // CXXConstructExpr to one whose constructed type is qbool AND
    // whose single argument's type resolves to a non-qbool classical
    // expression (either bool or double). A qbool-to-qbool copy
    // would trip the outer CXXConstructExpr pattern but should be
    // rejected either by the classical-init guard or by an
    // additional "not constructing from qbool" guard. We assert the
    // matcher does NOT fire on this shape — the observable contract.
    PN5Run r = run_pn5_qbool_prep_matcher(
        "void demo(qbool a) {\n"
        "    qbool y(0.5);\n"  // top-level prep, silent.
        "    WHEN(a) { qbool x(y); (void)x; }\n"
        "}\n");
    // The top-level `qbool y(0.5);` is silent (Function scope). The
    // `qbool x(y);` is copy-construct from a qbool; classical guard
    // handles it via the non-bool-non-double argument-type check.
    CHECK(r.detections == 0);
}

void test_pn5_qbool_prep_top_level_prep_mixed_with_when_is_flagged_once() {
    // Mixed TU: one top-level prep (silent) + one WHEN-body prep
    // (flagged). The counter bumps exactly once.
    PN5Run r = run_pn5_qbool_prep_matcher(
        "void demo(qbool a) {\n"
        "    qbool ok(0.5);\n"            // top-level — silent.
        "    WHEN(a) { qbool bad(0.5); (void)bad; }\n"  // fires.
        "    (void)ok;\n"
        "}\n");
    CHECK(r.detections == 1);
}

} // namespace

}  // namespace sturm_test_matcher_pn5_qbool_prep_ns

void run_pn5_qbool_prep_tests() {
    using namespace sturm_test_matcher_pn5_qbool_prep_ns;
    test_pn5_qbool_prep_inside_when_is_flagged();
    test_pn5_qbool_prep_with_variable_double_inside_when_is_flagged();
    test_pn5_qbool_prep_with_literal_zero_inside_when_is_flagged();
    test_pn5_qbool_prep_at_function_top_level_is_silent();
    test_pn5_qbool_prep_bool_literal_true_is_silent();
    test_pn5_qbool_prep_bool_literal_false_is_silent();
    test_pn5_qbool_prep_bool_variable_is_silent();
    test_pn5_qbool_no_initializer_is_silent();
    test_pn5_qbool_prep_copy_init_is_silent();
    test_pn5_qbool_prep_top_level_prep_mixed_with_when_is_flagged_once();
}
