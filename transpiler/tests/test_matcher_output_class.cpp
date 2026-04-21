// test_matcher_output_class.cpp — Phase I PI-3 tests for the
// `detail::classify_output` output-param ownership classifier.
//
// Four target classes, each pinned by at least one fixture:
//
//   - Intermediate        : VD declared in the call's enclosing scope.
//                           Same-scope case — the default M8 uncompute
//                           anchor (call scope close brace) is used, so
//                           `insert_before_override` stays invalid and
//                           `skip_uncompute` stays false.
//   - IntermediateOuter   : VD declared in an ancestor scope of the call,
//                           WITHOUT a for/while/if/WHEN barrier between
//                           the two. `insert_before_override` is set to
//                           the declaring scope's close-brace loc so the
//                           uncompute lands at the outer scope's `}`.
//   - Final               : VD is a function parameter OR declared at
//                           file / namespace scope. `skip_uncompute`
//                           is set (no uncompute) but NO diagnostic is
//                           emitted — the value escapes per the user's
//                           final computation.
//   - SkipWithDiagnostic  : VD declared outside a for / while / if / else
//                           / WHEN body but the call (the mutation) is
//                           inside one. Mirrors PH-3: `skip_uncompute`
//                           set + stderr diagnostic.
//
// The tests run the PI-1 registry matcher + the PI-2 routine-call matcher
// (the PI-3 classifier is invoked from the PI-2 callback) and inspect the
// resulting QUnit for the expected per-op flags. We do NOT capture stderr
// here — the diagnostic text is pinned by test_matcher_ph3_outer_var.cpp
// already, and the PI-3 skip classes match the PH-3 guard's format.
//
// Multi-output conservative-skip rule coverage:
//   - Any SkipWithDiagnostic slot → whole op is skip+diagnostic.
//   - Any Final slot (no Skip)    → whole op is skip (no diagnostic).
//   - Mixed Intermediate / IntermediateOuter → whole op uncomputes, with
//     `insert_before_override` set to the OUTERMOST declaring scope
//     (so every output is still in scope when the inverse fires).

#include "test_matcher_harness.hpp"

#include "diag_context.hpp"
#include "matcher_user_routine.hpp"
#include "routine_registry.hpp"
#include "sturm/transpile/qir.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace sturm::transpile;

namespace {

// Minimal C++ stub declaring the forward / adjoint pairs the PI-3 tests
// exercise. Same shape as the PI-2 harness — we keep the stub self-
// contained so the tests do not depend on the runtime headers.
//
// The WHEN macro is a thin `if`-based shim (the same shape every other
// PI-* test uses): the classifier does not inspect the macro definition,
// only that the call lives inside a macro-expanded `if` body. Any
// wrapper that expands to `if (...) { ... }` triggers the barrier logic.
constexpr std::string_view kOutputClassStub = R"CPP(
namespace sturm { namespace _detail {
template <typename FnPtr> struct adjoint_of;
}}

namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
};
class qint {
public:
    qint() {}
    qint(const qbool&) {}
};
} // namespace sturm

using sturm::qbool;
using sturm::qint;

void one_out(qbool&) {}
void one_out_adj(qbool&) {}

void two_out(qbool&, qbool&) {}
void two_out_adj(qbool&, qbool&) {}

namespace sturm { namespace _detail {
template <> struct adjoint_of<decltype(&::one_out)> {
    static constexpr auto value = &::one_out_adj;
};
template <> struct adjoint_of<decltype(&::two_out)> {
    static constexpr auto value = &::two_out_adj;
};
}}

#define WHEN(cond) if (bool _when_val_ = (bool)(cond); _when_val_)
)CPP";

// Snapshot of a matched op. We capture only the PI-3-relevant fields.
struct OpSnap {
    std::string routine_name;
    std::uint32_t outputs_mask = 0;
    bool skip_uncompute = false;
    bool has_override = false;
    std::vector<std::string> operand_names;
};

struct Result {
    std::vector<OpSnap> ops;
    int detection_count = 0;
};

class OutputClassConsumer : public clang::ASTConsumer {
public:
    OutputClassConsumer(RoutineRegistry* reg,
                        QUnit* unit,
                        clang::ast_matchers::MatchFinder* reg_finder,
                        clang::ast_matchers::MatchFinder* user_finder,
                        Result* out)
        : reg_(reg), unit_(unit),
          reg_finder_(reg_finder), user_finder_(user_finder), out_(out) {}

    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        reg_finder_->matchAST(ctx);
        user_finder_->matchAST(ctx);
        for (const auto& scope : unit_->scopes) {
            for (const auto& op : scope.ops) {
                if (op.kind != QOpKind::USER_ROUTINE) continue;
                OpSnap snap;
                snap.routine_name  = op.routine_name;
                snap.outputs_mask  = op.outputs_mask;
                snap.skip_uncompute = op.skip_uncompute;
                snap.has_override = op.insert_before_override.isValid();
                for (const auto& ref : op.operands) {
                    snap.operand_names.push_back(ref.name);
                }
                out_->ops.push_back(std::move(snap));
            }
        }
        out_->detection_count = user_routine_detection_count_for_test();
    }

private:
    RoutineRegistry* reg_;
    QUnit* unit_;
    clang::ast_matchers::MatchFinder* reg_finder_;
    clang::ast_matchers::MatchFinder* user_finder_;
    Result* out_;
};

class OutputClassAction : public clang::ASTFrontendAction {
public:
    OutputClassAction(RoutineRegistry* reg,
                      QUnit* unit,
                      clang::ast_matchers::MatchFinder* reg_finder,
                      clang::ast_matchers::MatchFinder* user_finder,
                      Result* out)
        : reg_(reg), unit_(unit),
          reg_finder_(reg_finder), user_finder_(user_finder), out_(out) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<OutputClassConsumer>(
            reg_, unit_, reg_finder_, user_finder_, out_);
    }
private:
    RoutineRegistry* reg_;
    QUnit* unit_;
    clang::ast_matchers::MatchFinder* reg_finder_;
    clang::ast_matchers::MatchFinder* user_finder_;
    Result* out_;
};

class OutputClassFactory : public clang::tooling::FrontendActionFactory {
public:
    OutputClassFactory(RoutineRegistry* reg,
                       QUnit* unit,
                       clang::ast_matchers::MatchFinder* reg_finder,
                       clang::ast_matchers::MatchFinder* user_finder,
                       Result* out)
        : reg_(reg), unit_(unit),
          reg_finder_(reg_finder), user_finder_(user_finder), out_(out) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<OutputClassAction>(
            reg_, unit_, reg_finder_, user_finder_, out_);
    }
private:
    RoutineRegistry* reg_;
    QUnit* unit_;
    clang::ast_matchers::MatchFinder* reg_finder_;
    clang::ast_matchers::MatchFinder* user_finder_;
    Result* out_;
};

Result run_pi3(std::string_view user_src) {
    std::string code;
    code.reserve(kOutputClassStub.size() + user_src.size());
    code.append(kOutputClassStub);
    code.append(user_src);

    Result out;
    QUnit unit;
    RoutineRegistry reg;
    clang::ast_matchers::MatchFinder reg_finder;
    clang::ast_matchers::MatchFinder user_finder;

    // PM3-3: the PI-2 matcher now requires a DiagContext for its
    // missing-adjoint diagnostic path. The PI-3 tests exercise the
    // REGISTERED path (adjoints are installed via STURM_REGISTER_ADJOINT
    // inside the fixture source), so the diagnostic branch never fires
    // — a throwaway DiagnosticsEngine with IgnoringDiagConsumer is
    // sufficient to honour the signature.
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> ids(
        new clang::DiagnosticIDs());
    llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> opts(
        new clang::DiagnosticOptions());
    clang::DiagnosticsEngine diag_engine(
        ids, opts.get(), new clang::IgnoringDiagConsumer(),
        /*ShouldOwnClient=*/true);
    DiagContext diag_ctx(diag_engine);

    register_routine_registry_matcher(reg_finder, reg);
    register_user_routine_matcher(user_finder, unit, reg, diag_ctx);

    reset_user_routine_detection_count_for_test();

    OutputClassFactory factory(&reg, &unit, &reg_finder, &user_finder, &out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr, "FAIL  tool run returned false (PI-3)\n");
    }
    return out;
}

// ── Tests ───────────────────────────────────────────────────────────────────

// Intermediate: `qbool r; one_out(r);` in the same function-body scope.
// The VarDecl's declaring scope IS the call's enclosing scope, so the
// default M8 anchor is the call scope's close brace.
// - skip_uncompute stays false
// - insert_before_override stays invalid (default)
void test_oc_intermediate_same_scope() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qbool r;
    one_out(r);
}
)CPP";
    Result r = run_pi3(src);
    CHECK(r.ops.size() == 1);
    if (r.ops.size() == 1) {
        CHECK(r.ops[0].skip_uncompute == false);
        CHECK(r.ops[0].has_override == false);
    }
}

// IntermediateOuter: VD declared in the function body, call inside a
// nested braced block (no for / while / if barrier between them).
// Expect insert_before_override to be valid (the declaring scope's
// close-brace loc) and skip_uncompute false.
void test_oc_intermediate_outer_via_nested_block() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qbool r;
    {
        one_out(r);
    }
}
)CPP";
    Result r = run_pi3(src);
    CHECK(r.ops.size() == 1);
    if (r.ops.size() == 1) {
        CHECK(r.ops[0].skip_uncompute == false);
        CHECK(r.ops[0].has_override == true);
    }
}

// Final (function parameter): the routine's output parameter is itself
// a parameter of the enclosing function — the value escapes through
// the return path. Expect skip_uncompute=true (no inverse), override
// stays invalid (nothing to plant).
void test_oc_final_function_parameter() {
    constexpr std::string_view src = R"CPP(
void caller(qbool& out) {
    one_out(out);
}
)CPP";
    Result r = run_pi3(src);
    CHECK(r.ops.size() == 1);
    if (r.ops.size() == 1) {
        CHECK(r.ops[0].skip_uncompute == true);
        CHECK(r.ops[0].has_override == false);
    }
}

// Final (file scope): the output backs a namespace-scope VarDecl. Same
// escape semantics as a function parameter — skip_uncompute=true.
void test_oc_final_file_scope() {
    constexpr std::string_view src = R"CPP(
qbool g_escape;
void caller() {
    one_out(g_escape);
}
)CPP";
    Result r = run_pi3(src);
    CHECK(r.ops.size() == 1);
    if (r.ops.size() == 1) {
        CHECK(r.ops[0].skip_uncompute == true);
        CHECK(r.ops[0].has_override == false);
    }
}

// SkipWithDiagnostic: VD declared in the function body, call inside a
// for-loop body. Reverse-loop synthesis hazard → flag as skip.
void test_oc_skip_with_diagnostic_for_body() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qbool r;
    for (int i = 0; i < 3; ++i) {
        one_out(r);
    }
}
)CPP";
    Result r = run_pi3(src);
    CHECK(r.ops.size() == 1);
    if (r.ops.size() == 1) {
        CHECK(r.ops[0].skip_uncompute == true);
    }
}

// SkipWithDiagnostic: VD outside, call inside a while body.
void test_oc_skip_with_diagnostic_while_body() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qbool r;
    while (true) {
        one_out(r);
        break;
    }
}
)CPP";
    Result r = run_pi3(src);
    CHECK(r.ops.size() == 1);
    if (r.ops.size() == 1) {
        CHECK(r.ops[0].skip_uncompute == true);
    }
}

// SkipWithDiagnostic: VD outside, call inside an if body.
void test_oc_skip_with_diagnostic_if_body() {
    constexpr std::string_view src = R"CPP(
void caller(bool cond) {
    qbool r;
    if (cond) {
        one_out(r);
    }
}
)CPP";
    Result r = run_pi3(src);
    CHECK(r.ops.size() == 1);
    if (r.ops.size() == 1) {
        CHECK(r.ops[0].skip_uncompute == true);
    }
}

// SkipWithDiagnostic: VD outside, call inside a WHEN body (WHEN is a
// macro-expanded if). The classifier treats macro-expanded ifs exactly
// like user-written ifs for the barrier check.
void test_oc_skip_with_diagnostic_when_body() {
    constexpr std::string_view src = R"CPP(
void caller(bool cond) {
    qbool r;
    WHEN(cond) {
        one_out(r);
    }
}
)CPP";
    Result r = run_pi3(src);
    CHECK(r.ops.size() == 1);
    if (r.ops.size() == 1) {
        CHECK(r.ops[0].skip_uncompute == true);
    }
}

// Multi-output conservative skip: one slot is Intermediate (local), the
// other is a function parameter (Final). Final dominates → whole op
// is skipped (no partial inversion of a multi-output routine).
void test_oc_multi_out_final_dominates() {
    constexpr std::string_view src = R"CPP(
void caller(qbool& escaping) {
    qbool r;
    two_out(r, escaping);
}
)CPP";
    Result r = run_pi3(src);
    CHECK(r.ops.size() == 1);
    if (r.ops.size() == 1) {
        CHECK(r.ops[0].skip_uncompute == true);
    }
}

// Multi-output conservative skip: one slot is Intermediate, the other
// is SkipWithDiagnostic (outer var + control-flow barrier). Skip
// dominates → whole op is skipped with diagnostic.
void test_oc_multi_out_skip_dominates() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qbool a;
    for (int i = 0; i < 2; ++i) {
        qbool local;
        two_out(local, a);
    }
}
)CPP";
    Result r = run_pi3(src);
    CHECK(r.ops.size() == 1);
    if (r.ops.size() == 1) {
        CHECK(r.ops[0].skip_uncompute == true);
    }
}

// Multi-output mixed Intermediate + IntermediateOuter (no barriers, no
// Finals). The conservative rule for this case is "uncompute" — but
// with `insert_before_override` set to the OUTERMOST declaring scope's
// close brace so every output is still in scope when the inverse
// fires.
void test_oc_multi_out_mixed_intermediate_uses_outermost() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qbool outer_v;
    {
        qbool inner_v;
        two_out(outer_v, inner_v);
    }
}
)CPP";
    Result r = run_pi3(src);
    CHECK(r.ops.size() == 1);
    if (r.ops.size() == 1) {
        // Not skipped — we have a valid uncompute anchor.
        CHECK(r.ops[0].skip_uncompute == false);
        // Override is set (outermost declaring scope's close brace —
        // the outer function-body close, NOT the inner block's).
        CHECK(r.ops[0].has_override == true);
    }
}

} // namespace

void run_output_class_tests() {
    test_oc_intermediate_same_scope();
    test_oc_intermediate_outer_via_nested_block();
    test_oc_final_function_parameter();
    test_oc_final_file_scope();
    test_oc_skip_with_diagnostic_for_body();
    test_oc_skip_with_diagnostic_while_body();
    test_oc_skip_with_diagnostic_if_body();
    test_oc_skip_with_diagnostic_when_body();
    test_oc_multi_out_final_dominates();
    test_oc_multi_out_skip_dominates();
    test_oc_multi_out_mixed_intermediate_uses_outermost();
}
