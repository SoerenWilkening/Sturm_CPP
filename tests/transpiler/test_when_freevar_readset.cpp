// test_when_freevar_readset.cpp — E7.M1 unit test for the WHEN
// free-variable read-set extractor.
//
// The extractor (`compute_when_freevar_readset`) takes the Expr that
// appears in `WHEN(expr) { body }` and returns the set of `VarDecl`s
// that `expr` reads, transitively through any function calls. The
// transitive walk visits a callee's body if its source is available;
// otherwise the result is flagged "conservative" so a downstream
// write-set checker (E7.M2) can decide how to react.
//
// The fixtures here are pure-classical so the extractor is exercised
// without dragging in qbool / qint stubs — the read-set computation
// is structural (DeclRefExpr / CallExpr walk), independent of qtype.
//
// Test harness:
//   - Each fixture is a small C++ snippet that contains a marker call
//     `__readset_probe(<expr>)`. We parse the snippet via Clang's
//     in-memory tooling, locate every CallExpr to `__readset_probe`,
//     pass its single argument to the extractor, and assert the
//     resulting var name set + conservative flag.
//   - This mirrors the in-memory tooling pattern used by every other
//     transpiler unit-test (see `transpiler/tests/test_matcher_*.cpp`).
//
// Pure unit, no diagnostic emission: this is M1 of epic E7 in the
// packaging-export plan — the read-set is an input to M2's write-set
// checker, which lives in a sibling TU and emits the user-facing
// diagnostic.

#include "when_freevar_readset.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <set>
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

struct ProbeResult {
    std::set<std::string> var_names;
    bool conservative = false;
    bool found_probe  = false;
};

// Visitor that locates every CallExpr to a function literally named
// `__readset_probe`, runs the extractor on its single argument, and
// folds the result into `out` (the union across every probe in the
// snippet — every fixture only carries one probe, so the union is a
// no-op for the single-probe shape).
class ProbeVisitor
    : public clang::RecursiveASTVisitor<ProbeVisitor> {
public:
    explicit ProbeVisitor(ProbeResult* out) : out_(out) {}

    bool VisitCallExpr(clang::CallExpr* ce) {
        if (!ce || !out_) return true;
        const clang::FunctionDecl* fd = ce->getDirectCallee();
        if (!fd) return true;
        if (fd->getNameAsString() != "__readset_probe") return true;
        if (ce->getNumArgs() != 1) return true;
        const clang::Expr* arg = ce->getArg(0);
        if (!arg) return true;
        out_->found_probe = true;
        auto rs =
            sturm::transpile::compute_when_freevar_readset(arg);
        for (const auto* vd : rs.vars) {
            if (vd) out_->var_names.insert(vd->getNameAsString());
        }
        if (rs.conservative) out_->conservative = true;
        return true;
    }

private:
    ProbeResult* out_;
};

class ProbeConsumer : public clang::ASTConsumer {
public:
    explicit ProbeConsumer(ProbeResult* out) : out_(out) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        ProbeVisitor v(out_);
        v.TraverseDecl(ctx.getTranslationUnitDecl());
    }
private:
    ProbeResult* out_;
};

class ProbeAction : public clang::ASTFrontendAction {
public:
    explicit ProbeAction(ProbeResult* out) : out_(out) {}
    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance&,
                      llvm::StringRef) override {
        return std::make_unique<ProbeConsumer>(out_);
    }
private:
    ProbeResult* out_;
};

class ProbeActionFactory
    : public clang::tooling::FrontendActionFactory {
public:
    explicit ProbeActionFactory(ProbeResult* out) : out_(out) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<ProbeAction>(out_);
    }
private:
    ProbeResult* out_;
};

ProbeResult run_probe(std::string_view user_src) {
    static constexpr std::string_view kProbeStub =
        "void __readset_probe(int);\n"
        "void __readset_probe(bool);\n";
    std::string code;
    code.reserve(kProbeStub.size() + user_src.size());
    code.append(kProbeStub);
    code.append(user_src);

    ProbeResult out;
    ProbeActionFactory factory(&out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_readset.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (readset probe)\n");
    }
    return out;
}

// Fixture 1: literal expression. The probe argument is `0`; the
// extractor must return an empty set with no conservative flag.
void test_literal_expr_empty() {
    ProbeResult r = run_probe(
        "void demo() {\n"
        "    __readset_probe(0);\n"
        "}\n");
    CHECK(r.found_probe);
    CHECK(r.var_names.empty());
    CHECK(!r.conservative);
}

// Fixture 2: bare DeclRefExpr to a single VarDecl. The extractor
// must report exactly that VarDecl.
void test_single_var() {
    ProbeResult r = run_probe(
        "void demo() {\n"
        "    int a = 1;\n"
        "    __readset_probe(a);\n"
        "}\n");
    CHECK(r.found_probe);
    CHECK(r.var_names.size() == 1);
    CHECK(r.var_names.count("a") == 1);
    CHECK(!r.conservative);
}

// Fixture 3: a function call inside the expression. The function's
// body is available (defined in the same TU) and reads variable `b`.
// The expression itself reads `a`. The extractor must return the
// union {a, b} — `a` from the direct DeclRefExpr in the argument,
// `b` from the transitive read through `read_b`'s body.
void test_function_call() {
    ProbeResult r = run_probe(
        "static int g_b = 7;\n"
        "int read_b() { return g_b; }\n"
        "void demo() {\n"
        "    int a = 1;\n"
        "    __readset_probe(a + read_b());\n"
        "}\n");
    CHECK(r.found_probe);
    // Direct read of `a` plus the transitive read of `g_b` through
    // `read_b()`. The extractor should not flag conservative since
    // every callee on the chain has its body available.
    CHECK(r.var_names.count("a") == 1);
    CHECK(r.var_names.count("g_b") == 1);
    CHECK(!r.conservative);
}

// Fixture 4: nested function call. The probe argument is
// `outer()`; `outer()` calls `inner()`; `inner()` reads `c`. The
// extractor must follow the chain through both calls and report
// `c` in the read-set without flagging conservative.
void test_nested_call() {
    ProbeResult r = run_probe(
        "static int c = 3;\n"
        "int inner() { return c; }\n"
        "int outer() { return inner() + 1; }\n"
        "void demo() {\n"
        "    __readset_probe(outer());\n"
        "}\n");
    CHECK(r.found_probe);
    CHECK(r.var_names.count("c") == 1);
    CHECK(!r.conservative);
}

// Fixture 5: an unanalyzed callee — function declared but not
// defined in the TU. The extractor must still return any directly-
// read VarDecls, but flag `conservative = true` so a downstream
// write-set checker can decide whether to bail or proceed.
void test_unanalyzed_callee_flags_conservative() {
    ProbeResult r = run_probe(
        "extern int extern_fn();\n"
        "void demo() {\n"
        "    int a = 1;\n"
        "    __readset_probe(a + extern_fn());\n"
        "}\n");
    CHECK(r.found_probe);
    CHECK(r.var_names.count("a") == 1);
    CHECK(r.conservative);
}

// Fixture 6: recursive callee — `recur()` calls itself. The
// extractor must terminate (not infinite-loop) and return the
// VarDecls reached through the function's body before the recursion
// folds back on itself. We assert termination + that the directly-
// read variable is present.
void test_recursive_callee_terminates() {
    ProbeResult r = run_probe(
        "static int d = 5;\n"
        "int recur(int n) { return n <= 0 ? d : recur(n - 1); }\n"
        "void demo() {\n"
        "    __readset_probe(recur(3));\n"
        "}\n");
    CHECK(r.found_probe);
    CHECK(r.var_names.count("d") == 1);
}

} // namespace

int main() {
    test_literal_expr_empty();
    test_single_var();
    test_function_call();
    test_nested_call();
    test_unanalyzed_callee_flags_conservative();
    test_recursive_callee_terminates();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
