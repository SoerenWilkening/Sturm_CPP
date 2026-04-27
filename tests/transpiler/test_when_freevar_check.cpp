// test_when_freevar_check.cpp — E7.M2 unit test for the WHEN free-
// variable write-set checker.
//
// E7.M1 (`compute_when_freevar_readset`) computes the set of `VarDecl`s
// that the WHEN control expression `expr` reads, transitively through
// callees. E7.M2 — this module's checker — walks the WHEN body looking
// for assignment-shape writes (`=`, `+=`, `^=`, `++`, etc., spelled as
// `BinaryOperator`, `CompoundAssignOperator`, or `CXXOperatorCallExpr`)
// and emits a hard-error `DiagnosticsEngine` diagnostic at the offending
// write source location whenever the writer's target VarDecl is in the
// read-set. Writes to variables declared *inside* the body are NOT
// errors — they are body-local and cannot corrupt the WHEN scope.
//
// The fixtures here are pure-classical so the checker is exercised
// without dragging in qbool / qint stubs — the checker is structural
// (assignment-shape walk + read-set membership), independent of qtype.
//
// Test harness:
//   - Each fixture is a small C++ snippet that contains two marker
//     calls:
//       __readset_probe(<expr>);   // identifies the WHEN's control expr
//       __body_probe([&]() {       // identifies the WHEN's body
//           ...                    // body statements live here
//       });
//     We parse the snippet via Clang's in-memory tooling, locate every
//     paired probe, compute the read-set from the `__readset_probe`
//     argument (E7.M1) and run the E7.M2 checker against the body
//     (the lambda body). The harness installs a counting / recording
//     DiagnosticConsumer on the tool's CompilerInstance so we can
//     assert (a) the number of hard-error diagnostics emitted and (b)
//     the substring content (variable name + WHEN scope mention).
//
// Pure unit, no MatchFinder wiring: this is M2 of epic E7. M3 will
// register the checker into the matcher pipeline and run it under the
// real `WHEN(...)` AST shape; this M2 unit operates against the
// extracted read-set directly so the test stays decoupled from the
// matcher anchor pattern.

#include "when_freevar_check.hpp"
#include "when_freevar_readset.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallString.h"

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

int tests_run = 0;
int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while (0)

// Recording consumer: captures every diagnostic at level >= Error so
// the tests can assert on count and message substring without having
// to parse a TextDiagnosticPrinter's formatted output. Mirrors the
// pattern in `transpiler/tests/test_diag_context.cpp`'s
// `CountingDiagConsumer` but additionally retains the formatted
// message text (via `Diagnostic::FormatDiagnostic`) so the substring
// asserts can run.
class RecordingDiagConsumer final : public clang::DiagnosticConsumer {
public:
    unsigned                 errors = 0;
    std::vector<std::string> messages;

    void HandleDiagnostic(clang::DiagnosticsEngine::Level lvl,
                          const clang::Diagnostic& info) override {
        if (lvl < clang::DiagnosticsEngine::Error) return;
        ++errors;
        llvm::SmallString<256> buf;
        info.FormatDiagnostic(buf);
        messages.emplace_back(buf.data(), buf.size());
    }
};

// Result bundle returned by `run_check`. Records:
//   - whether the read-set probe + body probe were both found,
//   - the number of hard-error diagnostics the checker fired,
//   - the formatted message text for substring-matching.
struct CheckResult {
    bool                     found_readset_probe = false;
    bool                     found_body_probe    = false;
    unsigned                 errors              = 0;
    std::vector<std::string> messages;
};

// Visitor that locates the matched probe pair: a CallExpr to
// `__readset_probe(<expr>)` (the WHEN control expr) and a CallExpr to
// `__body_probe([&]() { ...body... })` (the WHEN body, packaged as a
// no-arg lambda whose body is the CompoundStmt we want to check).
//
// The visitor does its work in `HandleTranslationUnit` rather than per-
// CallExpr because we need to compute the read-set BEFORE running the
// checker, and the checker needs the SourceManager.
class ProbePairVisitor
    : public clang::RecursiveASTVisitor<ProbePairVisitor> {
public:
    ProbePairVisitor() = default;

    bool VisitCallExpr(clang::CallExpr* ce) {
        if (!ce) return true;
        const clang::FunctionDecl* fd = ce->getDirectCallee();
        if (!fd) return true;
        const std::string name = fd->getNameAsString();
        if (name == "__readset_probe" && ce->getNumArgs() == 1) {
            readset_arg_ = ce->getArg(0);
            return true;
        }
        if (name == "__body_probe" && ce->getNumArgs() == 1) {
            // The argument is a (typically implicit-converted) lambda
            // expression. Peel implicit casts / paren / material
            // wrappers until we reach the LambdaExpr.
            const clang::Expr* arg = ce->getArg(0);
            const clang::Expr* peeled = arg ? arg->IgnoreImplicit()
                                            : nullptr;
            if (peeled) peeled = peeled->IgnoreParenImpCasts();
            const auto* lam =
                clang::dyn_cast_or_null<clang::LambdaExpr>(peeled);
            if (lam) body_ = lam->getBody();
            return true;
        }
        return true;
    }

    const clang::Expr* readset_arg() const { return readset_arg_; }
    const clang::Stmt* body() const        { return body_; }

private:
    const clang::Expr* readset_arg_ = nullptr;
    const clang::Stmt* body_        = nullptr;
};

class CheckConsumer : public clang::ASTConsumer {
public:
    explicit CheckConsumer(CheckResult* out,
                           clang::DiagnosticsEngine* engine)
        : out_(out), engine_(engine) {}

    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        ProbePairVisitor v;
        v.TraverseDecl(ctx.getTranslationUnitDecl());
        out_->found_readset_probe = v.readset_arg() != nullptr;
        out_->found_body_probe    = v.body()        != nullptr;
        if (!v.readset_arg() || !v.body() || !engine_) return;

        const auto rs =
            sturm::transpile::compute_when_freevar_readset(v.readset_arg());

        // The checker is the unit under test. It walks the body for
        // assignment-shape writes whose target is in the read-set and
        // fires a hard-error diagnostic for each through `*engine_`.
        // The WHEN scope's source location is the readset_arg's begin
        // loc — that's where the matcher would anchor for the
        // diagnostic's "scope" reference.
        sturm::transpile::check_when_freevar_writes(
            v.body(),
            rs,
            *engine_,
            ctx.getSourceManager(),
            v.readset_arg()->getBeginLoc());
    }

private:
    CheckResult*              out_;
    clang::DiagnosticsEngine* engine_;
};

class CheckAction : public clang::ASTFrontendAction {
public:
    CheckAction(CheckResult* out, RecordingDiagConsumer* recorder)
        : out_(out), recorder_(recorder) {}

    bool BeginInvocation(clang::CompilerInstance& ci) override {
        // Replace the default DiagnosticConsumer with our recorder so
        // every Report() landing on the engine is captured. The engine
        // takes ownership (`ShouldOwnClient=true`); we keep a non-
        // owning pointer to read the counters back out after the run
        // completes. This is the same pattern Clang itself uses in
        // `Tooling.cpp`'s ToolInvocation when a custom DiagConsumer
        // is supplied via runToolOnCodeWithArgs's optional consumer
        // param — but we install it explicitly here so the tooling
        // call site stays simple.
        ci.getDiagnostics().setClient(recorder_, /*ShouldOwn=*/false);
        return true;
    }

    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance& ci,
                      llvm::StringRef) override {
        return std::make_unique<CheckConsumer>(
            out_, &ci.getDiagnostics());
    }

    void EndSourceFileAction() override {
        // Mirror counters out before the action tears down its
        // diagnostic engine.
        if (out_ && recorder_) {
            out_->errors   = recorder_->errors;
            out_->messages = recorder_->messages;
        }
    }

private:
    CheckResult*           out_;
    RecordingDiagConsumer* recorder_;
};

class CheckActionFactory
    : public clang::tooling::FrontendActionFactory {
public:
    CheckActionFactory(CheckResult* out, RecordingDiagConsumer* recorder)
        : out_(out), recorder_(recorder) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<CheckAction>(out_, recorder_);
    }

private:
    CheckResult*           out_;
    RecordingDiagConsumer* recorder_;
};

CheckResult run_check(std::string_view user_src) {
    static constexpr std::string_view kProbeStub =
        "void __readset_probe(int);\n"
        "void __readset_probe(bool);\n"
        // Inline definition so the implicit-instantiation lookup
        // succeeds without a separate TU. A non-inline declaration
        // would draw a "used but not defined" link warning that the
        // recording consumer would mistake for a checker diagnostic.
        "template <class F> inline void __body_probe(F&&) {}\n";
    std::string code;
    code.reserve(kProbeStub.size() + user_src.size());
    code.append(kProbeStub);
    code.append(user_src);

    CheckResult           out;
    RecordingDiagConsumer recorder;
    CheckActionFactory    factory(&out, &recorder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_check.cpp");
    if (!ok) {
        // Compilation failures are not themselves a test failure —
        // the recorded diagnostics are what we assert on. But surface
        // a debug print so a regression that breaks the tooling
        // invocation is visible.
        std::fprintf(stderr,
                     "DEBUG  tool run returned false (writeset check)\n");
    }
    return out;
}

bool message_contains(const std::vector<std::string>& msgs,
                      std::string_view needle) {
    for (const auto& m : msgs) {
        if (m.find(needle) != std::string::npos) return true;
    }
    return false;
}

// ── Fixture 1: direct mutation of a control-set variable ────────────
//
// `WHEN(a) { a = 7; }` — `a` is in the read-set (it is the WHEN
// control); the body assigns to it via a plain `BinaryOperator`
// (operator= on int). Must fire one hard-error diagnostic naming `a`.
void test_direct_mutation_of_control_var_errors() {
    CheckResult r = run_check(
        "void demo() {\n"
        "    int a = 1;\n"
        "    __readset_probe(a);\n"
        "    __body_probe([&]() {\n"
        "        a = 7;\n"
        "    });\n"
        "}\n");
    CHECK(r.found_readset_probe);
    CHECK(r.found_body_probe);
    CHECK(r.errors == 1);
    CHECK(message_contains(r.messages, "'a'"));
    CHECK(message_contains(r.messages, "WHEN"));
}

// ── Fixture 2: compound-assign mutation of a free var ───────────────
//
// `WHEN(b + 1) { b += 3; }` — the read-set contains `b`; the body's
// `+=` is a `CompoundAssignOperator` shape. Must fire exactly one
// hard-error naming `b`.
void test_compound_assign_to_free_var_errors() {
    CheckResult r = run_check(
        "void demo() {\n"
        "    int b = 2;\n"
        "    __readset_probe(b + 1);\n"
        "    __body_probe([&]() {\n"
        "        b += 3;\n"
        "    });\n"
        "}\n");
    CHECK(r.errors == 1);
    CHECK(message_contains(r.messages, "'b'"));
}

// ── Fixture 3: mutation via call to a function that mutates a free var
//
// The body calls `mutate_c()` which writes to global `c`. `c` is in
// the read-set (read directly by the WHEN expr). The checker walks
// callee bodies for writes the same way the read-set walked them for
// reads, so the indirect mutation must fire exactly one hard-error
// naming `c`.
void test_mutation_via_call_errors() {
    CheckResult r = run_check(
        "static int c = 0;\n"
        "void mutate_c() { c = 99; }\n"
        "void demo() {\n"
        "    __readset_probe(c);\n"
        "    __body_probe([&]() {\n"
        "        mutate_c();\n"
        "    });\n"
        "}\n");
    CHECK(r.errors >= 1);
    CHECK(message_contains(r.messages, "'c'"));
}

// ── Fixture 4: mutation of the control variable itself ──────────────
//
// `WHEN(d) { ++d; }` — `d` is the bare control; the body uses pre-
// increment, which is a `UnaryOperator` shape. The issue lists the
// three core write shapes (BinaryOperator / CompoundAssignOperator /
// CXXOperatorCallExpr) but the inc/dec UnaryOperator is the natural
// extension and matches the operand-mutation matcher's coverage in
// `matcher_when_operand_mutation.cpp`. Must fire one hard-error
// naming `d`.
void test_increment_of_control_var_errors() {
    CheckResult r = run_check(
        "void demo() {\n"
        "    int d = 0;\n"
        "    __readset_probe(d);\n"
        "    __body_probe([&]() {\n"
        "        ++d;\n"
        "    });\n"
        "}\n");
    CHECK(r.errors == 1);
    CHECK(message_contains(r.messages, "'d'"));
}

// ── Fixture 5: NEGATIVE case — body-local mutation must NOT error ──
//
// `WHEN(e) { int local = 0; local = 7; }` — `local` is declared inside
// the body (it is body-local), so its mutation cannot corrupt the
// WHEN scope's read-set. The checker must NOT fire any hard-error
// diagnostic.
void test_body_local_mutation_does_not_error() {
    CheckResult r = run_check(
        "void demo() {\n"
        "    int e = 0;\n"
        "    __readset_probe(e);\n"
        "    __body_probe([&]() {\n"
        "        int local = 0;\n"
        "        local = 7;\n"
        "        local += 3;\n"
        "        ++local;\n"
        "    });\n"
        "}\n");
    CHECK(r.found_readset_probe);
    CHECK(r.found_body_probe);
    CHECK(r.errors == 0);
}

// ── Fixture 6: empty body ─ no writes anywhere ──────────────────────
//
// A WHEN body that performs no writes must compile cleanly even when
// the read-set is non-empty.
void test_empty_body_does_not_error() {
    CheckResult r = run_check(
        "void demo() {\n"
        "    int f = 0;\n"
        "    __readset_probe(f);\n"
        "    __body_probe([&]() {\n"
        "        (void)f;\n"
        "    });\n"
        "}\n");
    CHECK(r.errors == 0);
}

// ── Fixture 7: write to variable NOT in the read-set ────────────────
//
// `WHEN(g) { h = 5; }` — `g` is the only var read by the expr; `h` is
// declared in the enclosing function and is mutated in the body. Even
// though `h` is not body-local, it is not in the read-set, so the
// checker must NOT fire. (P4 only forbids mutating *control-set*
// variables — unrelated outer mutations are flagged by a different
// matcher, PM3-2 / outer-var guard.)
void test_write_to_non_readset_var_does_not_error() {
    CheckResult r = run_check(
        "void demo() {\n"
        "    int g = 0;\n"
        "    int h = 0;\n"
        "    __readset_probe(g);\n"
        "    __body_probe([&]() {\n"
        "        h = 5;\n"
        "    });\n"
        "}\n");
    CHECK(r.errors == 0);
}

} // namespace

int main() {
    test_direct_mutation_of_control_var_errors();
    test_compound_assign_to_free_var_errors();
    test_mutation_via_call_errors();
    test_increment_of_control_var_errors();
    test_body_local_mutation_does_not_error();
    test_empty_body_does_not_error();
    test_write_to_non_readset_var_does_not_error();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
