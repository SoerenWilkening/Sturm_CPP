// test_matcher_user_routine.cpp — Phase I PI-2 TDD tests for the user-
// defined-routine call matcher.
//
// Contract (see bd sturm-11wh + docs/roadmap_transpiler_post_mvp.md Phase I):
//   - The matcher fires on any callExpr whose callee FunctionDecl is in
//     the PI-1 RoutineRegistry.
//   - On match it classifies each argument by the callee's corresponding
//     parameter type:
//         non-const qbool&/qint& -> output  (outputs_mask bit set)
//         const qbool&/qint&     -> input   (outputs_mask bit clear)
//         classical scalar       -> input   (verbatim source text)
//   - It builds a QOperation{kind=USER_ROUTINE} with routine_name set to
//     the callee source name, operands listed in source order, and
//     outputs_mask encoding the output classification.
//
// These tests build a C++ stub declaring free functions with mixed
// parameter kinds (non-const qbool&, const qint&, classical int, ...) and
// register matching `adjoint_of` specializations via the PI-1 matcher so
// the registry is populated. They then run the PI-2 matcher and inspect
// the resulting QUnit via dump() to pin the contract.
//
// The stub defines minimal `qbool` / `qint` classes — the PI-2 matcher
// keys on class name alone (`qbool` / `qint` / `qint_t`), so a one-line
// stub suffices.

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

// Inline C++ stub declaring the forward-adjoint pairs + supporting types
// the PI-2 tests use. Mirrors the PI-1 test's pattern (inlined macro
// expansion rather than pulling in the runtime header) so the tests are
// self-contained.
constexpr std::string_view kUserRoutineStub = R"CPP(
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

// Forward routine + adjoint pairs exercised by the tests. The names are
// deliberately distinct so every dump() line pins a specific shape.
//   - both_out(qbool& a, qbool& b)               : two outputs
//   - mixed_io (qbool& out, const qbool& in)     : one output + one input
//   - scalar   (qbool& out, int k)               : output + classical scalar
//   - all_const(const qbool& a, const qbool& b)  : no outputs (all inputs)
//   - qint_mix (qint& a, const qint& b)          : qint output + qint input
void both_out(qbool&, qbool&) {}
void both_out_adj(qbool&, qbool&) {}

void mixed_io(qbool&, const qbool&) {}
void mixed_io_adj(qbool&, const qbool&) {}

void scalar_fn(qbool&, int) {}
void scalar_fn_adj(qbool&, int) {}

void all_const(const qbool&, const qbool&) {}
void all_const_adj(const qbool&, const qbool&) {}

void qint_mix(qint&, const qint&) {}
void qint_mix_adj(qint&, const qint&) {}

// Unregistered routine — the matcher must NOT fire on this one.
void unregistered(qbool&, qbool&) {}

namespace sturm { namespace _detail {
template <> struct adjoint_of<decltype(&::both_out)>   {
    static constexpr auto value = &::both_out_adj;
};
template <> struct adjoint_of<decltype(&::mixed_io)>   {
    static constexpr auto value = &::mixed_io_adj;
};
template <> struct adjoint_of<decltype(&::scalar_fn)>  {
    static constexpr auto value = &::scalar_fn_adj;
};
template <> struct adjoint_of<decltype(&::all_const)>  {
    static constexpr auto value = &::all_const_adj;
};
template <> struct adjoint_of<decltype(&::qint_mix)>   {
    static constexpr auto value = &::qint_mix_adj;
};
}}
)CPP";

// A tiny snapshot of a matched USER_ROUTINE op. We capture the raw fields
// so assertions can run after the tool's ASTContext is destroyed.
struct OpSnapshot {
    std::string routine_name;
    std::uint32_t outputs_mask = 0;
    std::vector<std::string> operand_names;
};

struct RunResult {
    std::string dump_text;
    std::vector<OpSnapshot> ops;
    int detection_count = 0;
};

// Consumer that runs BOTH the PI-1 registry matcher and the PI-2
// user-routine matcher in one pass (so the registry is populated before
// the PI-2 callback fires) and snapshots the resulting QUnit while the
// ASTContext is still alive.
class UserRoutineConsumer : public clang::ASTConsumer {
public:
    UserRoutineConsumer(RoutineRegistry* reg,
                        QUnit* unit,
                        clang::ast_matchers::MatchFinder* reg_finder,
                        clang::ast_matchers::MatchFinder* user_finder,
                        RunResult* out)
        : reg_(reg), unit_(unit),
          reg_finder_(reg_finder), user_finder_(user_finder),
          out_(out) {}

    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        // Two-pass: populate the registry first, then run the PI-2
        // matcher so it sees a fully-populated registry. MatchFinder
        // ordering within a single `matchAST` call is per-node — and
        // the PI-1 matcher fires on ClassTemplateSpecializationDecl
        // nodes which are visited before most callExpr nodes in the
        // AST traversal order — but a dedicated two-pass invocation
        // removes that dependency from the tests entirely.
        reg_finder_->matchAST(ctx);
        user_finder_->matchAST(ctx);
        out_->dump_text = dump(*unit_);
        for (const auto& scope : unit_->scopes) {
            for (const auto& op : scope.ops) {
                if (op.kind != QOpKind::USER_ROUTINE) continue;
                OpSnapshot snap;
                snap.routine_name = op.routine_name;
                snap.outputs_mask = op.outputs_mask;
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
    RunResult* out_;
};

class UserRoutineAction : public clang::ASTFrontendAction {
public:
    UserRoutineAction(RoutineRegistry* reg,
                      QUnit* unit,
                      clang::ast_matchers::MatchFinder* reg_finder,
                      clang::ast_matchers::MatchFinder* user_finder,
                      RunResult* out)
        : reg_(reg), unit_(unit),
          reg_finder_(reg_finder), user_finder_(user_finder),
          out_(out) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<UserRoutineConsumer>(
            reg_, unit_, reg_finder_, user_finder_, out_);
    }
private:
    RoutineRegistry* reg_;
    QUnit* unit_;
    clang::ast_matchers::MatchFinder* reg_finder_;
    clang::ast_matchers::MatchFinder* user_finder_;
    RunResult* out_;
};

class UserRoutineFactory : public clang::tooling::FrontendActionFactory {
public:
    UserRoutineFactory(RoutineRegistry* reg,
                       QUnit* unit,
                       clang::ast_matchers::MatchFinder* reg_finder,
                       clang::ast_matchers::MatchFinder* user_finder,
                       RunResult* out)
        : reg_(reg), unit_(unit),
          reg_finder_(reg_finder), user_finder_(user_finder),
          out_(out) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<UserRoutineAction>(
            reg_, unit_, reg_finder_, user_finder_, out_);
    }
private:
    RoutineRegistry* reg_;
    QUnit* unit_;
    clang::ast_matchers::MatchFinder* reg_finder_;
    clang::ast_matchers::MatchFinder* user_finder_;
    RunResult* out_;
};

// Run the PI-1 + PI-2 matchers on `kUserRoutineStub + user_src` and
// return the snapshot.
RunResult run_user_routine_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kUserRoutineStub.size() + user_src.size());
    code.append(kUserRoutineStub);
    code.append(user_src);

    RunResult out;
    QUnit unit;
    RoutineRegistry reg;
    clang::ast_matchers::MatchFinder reg_finder;
    clang::ast_matchers::MatchFinder user_finder;

    // PM3-3: the PI-2 matcher now requires a DiagContext for its
    // missing-adjoint diagnostic path. The PI-2 tests exercise the
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

    UserRoutineFactory factory(&reg, &unit, &reg_finder, &user_finder, &out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (user_routine)\n");
    }
    return out;
}

// Find the first matched op whose routine_name matches.
const OpSnapshot* find_op(const RunResult& r, std::string_view name) {
    for (const auto& op : r.ops) {
        if (op.routine_name == name) return &op;
    }
    return nullptr;
}

// ── Tests ───────────────────────────────────────────────────────────────────

// A single call to `both_out(a, b)` — two non-const qbool& parameters,
// so both positions are outputs and outputs_mask == 0b11 == 0x3.
void test_user_routine_two_outputs() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qbool a;
    qbool b;
    both_out(a, b);
}
)CPP";
    RunResult r = run_user_routine_matcher(src);
    CHECK(r.detection_count == 1);
    CHECK(r.ops.size() == 1);
    const OpSnapshot* op = find_op(r, "both_out");
    CHECK(op != nullptr);
    if (op) {
        CHECK(op->outputs_mask == 0x3u);
        CHECK(op->operand_names.size() == 2);
        if (op->operand_names.size() == 2) {
            CHECK_EQ_STR(op->operand_names[0], std::string("a"));
            CHECK_EQ_STR(op->operand_names[1], std::string("b"));
        }
    }
}

// `mixed_io(out, in)` — non-const qbool& at slot 0 (output), const qbool&
// at slot 1 (input). outputs_mask == 0b01 == 0x1.
void test_user_routine_mixed_io() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qbool x;
    qbool y;
    mixed_io(x, y);
}
)CPP";
    RunResult r = run_user_routine_matcher(src);
    CHECK(r.detection_count == 1);
    const OpSnapshot* op = find_op(r, "mixed_io");
    CHECK(op != nullptr);
    if (op) {
        CHECK(op->outputs_mask == 0x1u);
        CHECK(op->operand_names.size() == 2);
        if (op->operand_names.size() == 2) {
            CHECK_EQ_STR(op->operand_names[0], std::string("x"));
            CHECK_EQ_STR(op->operand_names[1], std::string("y"));
        }
    }
}

// `scalar_fn(out, 42)` — non-const qbool& at slot 0, classical int at
// slot 1. outputs_mask == 0x1; operand[1].name is the verbatim source
// text "42".
void test_user_routine_classical_scalar() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qbool q;
    scalar_fn(q, 42);
}
)CPP";
    RunResult r = run_user_routine_matcher(src);
    CHECK(r.detection_count == 1);
    const OpSnapshot* op = find_op(r, "scalar_fn");
    CHECK(op != nullptr);
    if (op) {
        CHECK(op->outputs_mask == 0x1u);
        CHECK(op->operand_names.size() == 2);
        if (op->operand_names.size() == 2) {
            CHECK_EQ_STR(op->operand_names[0], std::string("q"));
            CHECK_EQ_STR(op->operand_names[1], std::string("42"));
        }
    }
}

// `all_const(a, b)` — both slots const qbool&, no outputs. The matcher
// still fires (the call IS a registered routine) but outputs_mask is
// zero. This pins the classification rule against a negative for the
// output-mask bit.
void test_user_routine_all_const_no_outputs() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qbool a;
    qbool b;
    all_const(a, b);
}
)CPP";
    RunResult r = run_user_routine_matcher(src);
    CHECK(r.detection_count == 1);
    const OpSnapshot* op = find_op(r, "all_const");
    CHECK(op != nullptr);
    if (op) {
        CHECK(op->outputs_mask == 0x0u);
        CHECK(op->operand_names.size() == 2);
    }
}

// `qint_mix(a, b)` — non-const qint& at slot 0, const qint& at slot 1.
// The matcher's output-param check must accept qint as well as qbool.
void test_user_routine_qint_operands() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qint a;
    qint b;
    qint_mix(a, b);
}
)CPP";
    RunResult r = run_user_routine_matcher(src);
    CHECK(r.detection_count == 1);
    const OpSnapshot* op = find_op(r, "qint_mix");
    CHECK(op != nullptr);
    if (op) {
        CHECK(op->outputs_mask == 0x1u);
    }
}

// Unregistered call — the matcher must NOT fire on calls to functions
// that do not appear in the registry. This pins the
// "registry membership is required" guard.
void test_user_routine_unregistered_is_skipped() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qbool a;
    qbool b;
    unregistered(a, b);
}
)CPP";
    RunResult r = run_user_routine_matcher(src);
    CHECK(r.detection_count == 0);
    CHECK(r.ops.empty());
}

// Two sibling calls in one scope yield two sibling QOperations in the
// same QScope, in source order. The MatchFinder traversal is AST-order,
// but dump() renders ops in insertion order; we pin that the second
// call fires AFTER the first.
void test_user_routine_two_calls_same_scope() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qbool a;
    qbool b;
    both_out(a, b);
    mixed_io(a, b);
}
)CPP";
    RunResult r = run_user_routine_matcher(src);
    CHECK(r.detection_count == 2);
    CHECK(r.ops.size() == 2);
    if (r.ops.size() == 2) {
        CHECK_EQ_STR(r.ops[0].routine_name, std::string("both_out"));
        CHECK_EQ_STR(r.ops[1].routine_name, std::string("mixed_io"));
    }
}

// Smoke test: the dump() output contains the USER_ROUTINE marker along
// with routine_name and outputs_mask annotations. This verifies the
// qir.cpp render path for the new kind is wired through.
void test_user_routine_dump_surface() {
    constexpr std::string_view src = R"CPP(
void caller() {
    qbool a;
    qbool b;
    both_out(a, b);
}
)CPP";
    RunResult r = run_user_routine_matcher(src);
    CHECK(r.dump_text.find("USER_ROUTINE") != std::string::npos);
    CHECK(r.dump_text.find("routine_name=\"both_out\"") != std::string::npos);
    CHECK(r.dump_text.find("outputs_mask=0x3") != std::string::npos);
}

} // namespace

void run_user_routine_tests() {
    test_user_routine_two_outputs();
    test_user_routine_mixed_io();
    test_user_routine_classical_scalar();
    test_user_routine_all_const_no_outputs();
    test_user_routine_qint_operands();
    test_user_routine_unregistered_is_skipped();
    test_user_routine_two_calls_same_scope();
    test_user_routine_dump_surface();
}
