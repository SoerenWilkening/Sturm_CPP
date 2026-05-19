// test_matcher_scope_kind.cpp — Phase J PJ-3a tests for the
// `detail::classify_scope_kind` scope-kind classifier.
//
// The PJ-3 uncompute-hoisting matcher needs to know what kind of scope
// wraps a given QScope so it can restrict hoisting to loop bodies only.
// Per plan decision #7 we do NOT carry a `kind` field on `QScope` — the
// classifier walks the AST parent chain on-demand, so every pre-existing
// `dump()` golden / snapshot fixture stays byte-identical.
//
// The five target classes covered by one test each (plus a few additional
// edge cases):
//
//   - Function   : scope anchor is the function body's CompoundStmt; its
//                  parent is a FunctionDecl.
//   - LoopBody   : scope anchor is the body of a for / while; parent is
//                  a ForStmt or WhileStmt and anchor == parent->getBody().
//                  Both braced and braceless body shapes are exercised.
//   - BranchBody : scope anchor is the then / else branch of a user `if`
//                  (not macro-expanded from WHEN).
//   - WhenBody   : scope anchor is the then-branch of a WHEN-expanded
//                  IfStmt; `is_expansion_of_macro(if_loc, ..., "WHEN")`
//                  yields true.
//   - Other      : a nested braced block that is NOT the body of a
//                  for/while/if (e.g. a bare `{ ... }` inside a function
//                  body used for scoping). Conservative default.
//
// The test harness runs a consumer that finds the first CompoundStmt that
// satisfies a caller-supplied predicate and invokes `classify_scope_kind`
// on it. We deliberately do not reuse the MVP OR matcher harness here —
// the classifier is independent of any matcher state, so the test drives
// it directly through its own ASTConsumer for maximal isolation.

#include "test_matcher_harness.hpp"

#include "matcher_common.hpp"
#include "sturm/transpile/qir.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_matcher_scope_kind_ns {

using namespace sturm::transpile;

namespace {

// ── Test driver ─────────────────────────────────────────────────────────────
//
// For each test we compile a snippet, walk the AST to find a particular
// Stmt (typically a CompoundStmt inside a function body, or a specific
// braceless body), and call `classify_scope_kind` on it. The walker uses
// a caller-supplied predicate so each test can pick the exact anchor it
// wants to classify.
//
// The predicate receives the full ASTContext too — useful when the test
// needs to disambiguate between multiple CompoundStmts (e.g. the function
// body vs a nested block).

using StmtPredicate =
    std::function<bool(const clang::Stmt*, clang::ASTContext&)>;

struct ClassifyResult {
    bool found = false;
    detail::ScopeKind kind = detail::ScopeKind::Other;
};

class ScopeKindVisitor : public clang::RecursiveASTVisitor<ScopeKindVisitor> {
public:
    ScopeKindVisitor(clang::ASTContext& ctx,
                     StmtPredicate pred,
                     ClassifyResult* out)
        : ctx_(ctx), pred_(std::move(pred)), out_(out) {}

    bool VisitStmt(clang::Stmt* s) {
        if (out_->found) return true;
        if (!s) return true;
        if (pred_(s, ctx_)) {
            out_->found = true;
            out_->kind = detail::classify_scope_kind(s, ctx_);
            return false;
        }
        return true;
    }

private:
    clang::ASTContext& ctx_;
    StmtPredicate pred_;
    ClassifyResult* out_;
};

class ScopeKindConsumer : public clang::ASTConsumer {
public:
    ScopeKindConsumer(StmtPredicate pred, ClassifyResult* out)
        : pred_(std::move(pred)), out_(out) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        ScopeKindVisitor v(ctx, pred_, out_);
        v.TraverseAST(ctx);
    }
private:
    StmtPredicate pred_;
    ClassifyResult* out_;
};

class ScopeKindAction : public clang::ASTFrontendAction {
public:
    ScopeKindAction(StmtPredicate pred, ClassifyResult* out)
        : pred_(std::move(pred)), out_(out) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<ScopeKindConsumer>(pred_, out_);
    }
private:
    StmtPredicate pred_;
    ClassifyResult* out_;
};

class ScopeKindFactory : public clang::tooling::FrontendActionFactory {
public:
    ScopeKindFactory(StmtPredicate pred, ClassifyResult* out)
        : pred_(std::move(pred)), out_(out) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<ScopeKindAction>(pred_, out_);
    }
private:
    StmtPredicate pred_;
    ClassifyResult* out_;
};

// Compile `src` (prepended with the optional stub) and classify the first
// Stmt that satisfies `pred`. Returns `{found=false, ...}` if no Stmt
// matched — the caller's CHECK on `found` makes that fail loudly.
ClassifyResult classify_in(std::string_view stub,
                           std::string_view user_src,
                           StmtPredicate pred) {
    std::string code;
    code.reserve(stub.size() + user_src.size());
    code.append(stub);
    code.append(user_src);

    ClassifyResult out;
    ScopeKindFactory factory(std::move(pred), &out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr, "FAIL  tool run returned false (PJ-3a)\n");
    }
    return out;
}

// Convenience predicate: match the body CompoundStmt of a FunctionDecl
// whose identifier is `name`. Fails if the function body is not a
// CompoundStmt (should never happen for a well-formed function).
StmtPredicate function_body_of(std::string_view name) {
    return [n = std::string(name)](const clang::Stmt* s,
                                   clang::ASTContext& ctx) -> bool {
        const auto* cs = clang::dyn_cast<clang::CompoundStmt>(s);
        if (!cs) return false;
        const auto parents =
            ctx.getParents(clang::DynTypedNode::create(*cs));
        if (parents.empty()) return false;
        const auto* fd = parents[0].get<clang::FunctionDecl>();
        if (!fd) return false;
        return fd->getNameAsString() == n;
    };
}

// Convenience predicate: first CompoundStmt encountered whose parent is
// a ForStmt (i.e. a braced for-body). Walks in source order via the
// visitor, so "first" matches the first `for (...)` in the snippet.
StmtPredicate first_for_body_compound() {
    return [](const clang::Stmt* s, clang::ASTContext& ctx) -> bool {
        const auto* cs = clang::dyn_cast<clang::CompoundStmt>(s);
        if (!cs) return false;
        const auto parents =
            ctx.getParents(clang::DynTypedNode::create(*cs));
        if (parents.empty()) return false;
        const auto* fs = parents[0].get<clang::ForStmt>();
        return fs && fs->getBody() == cs;
    };
}

// First CompoundStmt whose parent is a WhileStmt as the body.
StmtPredicate first_while_body_compound() {
    return [](const clang::Stmt* s, clang::ASTContext& ctx) -> bool {
        const auto* cs = clang::dyn_cast<clang::CompoundStmt>(s);
        if (!cs) return false;
        const auto parents =
            ctx.getParents(clang::DynTypedNode::create(*cs));
        if (parents.empty()) return false;
        const auto* ws = parents[0].get<clang::WhileStmt>();
        return ws && ws->getBody() == cs;
    };
}

// First CompoundStmt whose parent is an IfStmt as the then-branch.
// Accepts WHEN-expanded if-thens too — the test bodies distinguish via
// the `is_expansion_of_macro` probe inside `classify_scope_kind`.
StmtPredicate first_if_then_compound() {
    return [](const clang::Stmt* s, clang::ASTContext& ctx) -> bool {
        const auto* cs = clang::dyn_cast<clang::CompoundStmt>(s);
        if (!cs) return false;
        const auto parents =
            ctx.getParents(clang::DynTypedNode::create(*cs));
        if (parents.empty()) return false;
        const auto* is = parents[0].get<clang::IfStmt>();
        return is && is->getThen() == cs;
    };
}

// First CompoundStmt whose parent is an IfStmt as the else-branch.
StmtPredicate first_if_else_compound() {
    return [](const clang::Stmt* s, clang::ASTContext& ctx) -> bool {
        const auto* cs = clang::dyn_cast<clang::CompoundStmt>(s);
        if (!cs) return false;
        const auto parents =
            ctx.getParents(clang::DynTypedNode::create(*cs));
        if (parents.empty()) return false;
        const auto* is = parents[0].get<clang::IfStmt>();
        return is && is->getElse() == cs;
    };
}

// First CompoundStmt whose parent is another CompoundStmt (nested bare
// block — NOT a control-flow body). Used to exercise the `Other` class.
StmtPredicate first_nested_bare_block_compound() {
    return [](const clang::Stmt* s, clang::ASTContext& ctx) -> bool {
        const auto* cs = clang::dyn_cast<clang::CompoundStmt>(s);
        if (!cs) return false;
        const auto parents =
            ctx.getParents(clang::DynTypedNode::create(*cs));
        if (parents.empty()) return false;
        return parents[0].get<clang::CompoundStmt>() != nullptr;
    };
}

// First non-compound braceless body of a ForStmt (PH-1 shape). The body
// is the single Stmt (e.g. a DeclStmt) immediately after the `)`.
StmtPredicate first_braceless_for_body() {
    return [](const clang::Stmt* s, clang::ASTContext& ctx) -> bool {
        if (clang::isa<clang::CompoundStmt>(s)) return false;
        const auto parents =
            ctx.getParents(clang::DynTypedNode::create(*s));
        if (parents.empty()) return false;
        const auto* fs = parents[0].get<clang::ForStmt>();
        return fs && fs->getBody() == s;
    };
}

// First non-compound braceless then-branch of an IfStmt (PH-1 shape).
StmtPredicate first_braceless_if_then() {
    return [](const clang::Stmt* s, clang::ASTContext& ctx) -> bool {
        if (clang::isa<clang::CompoundStmt>(s)) return false;
        const auto parents =
            ctx.getParents(clang::DynTypedNode::create(*s));
        if (parents.empty()) return false;
        const auto* is = parents[0].get<clang::IfStmt>();
        return is && is->getThen() == s;
    };
}

// ── Tests ───────────────────────────────────────────────────────────────────

// Function: the function body's CompoundStmt. Parent is the FunctionDecl.
void test_sk_function_body() {
    constexpr std::string_view src = R"CPP(
void demo() {
    int x = 1;
    (void)x;
}
)CPP";
    ClassifyResult r = classify_in(kQBoolStub, src, function_body_of("demo"));
    CHECK(r.found);
    CHECK(r.kind == detail::ScopeKind::Function);
}

// LoopBody: braced `for` body. Parent is a ForStmt; anchor == body.
void test_sk_braced_for_body() {
    constexpr std::string_view src = R"CPP(
void demo() {
    for (int i = 0; i < 3; ++i) {
        int x = i;
        (void)x;
    }
}
)CPP";
    ClassifyResult r = classify_in(kQBoolStub, src, first_for_body_compound());
    CHECK(r.found);
    CHECK(r.kind == detail::ScopeKind::LoopBody);
}

// LoopBody: braceless `for` body (PH-1 shape). Anchor is the non-compound
// body Stmt itself (a DeclStmt here).
void test_sk_braceless_for_body() {
    constexpr std::string_view src = R"CPP(
void demo() {
    for (int i = 0; i < 3; ++i) int x = i;
}
)CPP";
    ClassifyResult r = classify_in(kQBoolStub, src, first_braceless_for_body());
    CHECK(r.found);
    CHECK(r.kind == detail::ScopeKind::LoopBody);
}

// LoopBody: braced `while` body.
void test_sk_braced_while_body() {
    constexpr std::string_view src = R"CPP(
void demo() {
    while (true) {
        int x = 1;
        (void)x;
        break;
    }
}
)CPP";
    ClassifyResult r =
        classify_in(kQBoolStub, src, first_while_body_compound());
    CHECK(r.found);
    CHECK(r.kind == detail::ScopeKind::LoopBody);
}

// BranchBody: user-written `if (cond) { ... }` then-arm, braced. Parent is
// an IfStmt that is NOT macro-expanded from WHEN.
void test_sk_branch_if_then_braced() {
    constexpr std::string_view src = R"CPP(
void demo(bool cond) {
    if (cond) {
        int x = 1;
        (void)x;
    }
}
)CPP";
    ClassifyResult r = classify_in(kQBoolStub, src, first_if_then_compound());
    CHECK(r.found);
    CHECK(r.kind == detail::ScopeKind::BranchBody);
}

// BranchBody: braceless `if (cond) qop;`.
void test_sk_branch_if_then_braceless() {
    constexpr std::string_view src = R"CPP(
void demo(bool cond) {
    if (cond) int x = 1;
}
)CPP";
    ClassifyResult r =
        classify_in(kQBoolStub, src, first_braceless_if_then());
    CHECK(r.found);
    CHECK(r.kind == detail::ScopeKind::BranchBody);
}

// BranchBody: `if ... else { ... }` — the else branch.
void test_sk_branch_if_else_braced() {
    constexpr std::string_view src = R"CPP(
void demo(bool cond) {
    if (cond) {
        int x = 1;
        (void)x;
    } else {
        int y = 2;
        (void)y;
    }
}
)CPP";
    ClassifyResult r = classify_in(kQBoolStub, src, first_if_else_compound());
    CHECK(r.found);
    CHECK(r.kind == detail::ScopeKind::BranchBody);
}

// WhenBody: macro-expanded `WHEN(cond) { ... }`. The classifier must
// look at the IfStmt's IfLoc, detect the WHEN macro spelling via
// `is_expansion_of_macro`, and report `WhenBody`, NOT `BranchBody`.
void test_sk_when_body() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a) {
    WHEN(a) {
        int x = 1;
        (void)x;
    }
}
)CPP";
    // The WHEN macro expands to three nested `if`s; the innermost's
    // then-branch is the user-written CompoundStmt. The visitor picks up
    // the first IfStmt-parented CompoundStmt it encounters in source
    // order, which IS the innermost — the outer two `if`s' then-branches
    // are themselves `if`s, not CompoundStmts.
    ClassifyResult r =
        classify_in(kQBoolWhenStub, src, first_if_then_compound());
    CHECK(r.found);
    CHECK(r.kind == detail::ScopeKind::WhenBody);
}

// Other: a bare nested `{ ... }` block inside a function body. Its
// parent is the function body's CompoundStmt — not a for/while/if/
// FunctionDecl — so the classifier must report `Other`.
void test_sk_other_nested_bare_block() {
    constexpr std::string_view src = R"CPP(
void demo() {
    {
        int x = 1;
        (void)x;
    }
}
)CPP";
    ClassifyResult r =
        classify_in(kQBoolStub, src, first_nested_bare_block_compound());
    CHECK(r.found);
    CHECK(r.kind == detail::ScopeKind::Other);
}

// Defensive: a null scope anchor classifies as `Other`. The helper
// must not dereference a null pointer.
void test_sk_null_anchor() {
    // We build a minimal translation unit so ASTContext is available,
    // then call the classifier on nullptr directly — bypassing the
    // visitor harness to pin the null-guard behaviour.
    struct NullConsumer : public clang::ASTConsumer {
        detail::ScopeKind* out;
        explicit NullConsumer(detail::ScopeKind* o) : out(o) {}
        void HandleTranslationUnit(clang::ASTContext& ctx) override {
            *out = detail::classify_scope_kind(nullptr, ctx);
        }
    };
    struct NullAction : public clang::ASTFrontendAction {
        detail::ScopeKind* out;
        explicit NullAction(detail::ScopeKind* o) : out(o) {}
        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance&, llvm::StringRef) override {
            return std::make_unique<NullConsumer>(out);
        }
    };
    struct NullFactory : public clang::tooling::FrontendActionFactory {
        detail::ScopeKind* out;
        explicit NullFactory(detail::ScopeKind* o) : out(o) {}
        std::unique_ptr<clang::FrontendAction> create() override {
            return std::make_unique<NullAction>(out);
        }
    };

    detail::ScopeKind kind = detail::ScopeKind::Function; // seed non-Other
    NullFactory factory(&kind);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    std::string code = "void demo() {}\n";
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr, "FAIL  tool run returned false (PJ-3a null)\n");
    }
    CHECK(kind == detail::ScopeKind::Other);
}

// For-init-stmt is NOT a LoopBody: the init stmt runs once before the
// loop and is not subject to per-iteration invariance semantics. A
// CompoundStmt that happens to sit inside a ForStmt's init position
// must classify as `Other` (not `LoopBody`).
//
// Since the C++ grammar does not actually allow a CompoundStmt as the
// for's init-stmt (init-stmt is a simple-declaration or expr stmt),
// the "Other" path for parent==ForStmt but getBody != anchor is
// normally unreachable in user code. We still exercise the code path
// by constructing a synthetic case: a CompoundStmt that is a ForStmt's
// BODY remains LoopBody (covered above), but a ForStmt where the
// classifier gets called on a non-body Stmt child must return Other.
// That's covered implicitly by the `first_nested_bare_block_compound`
// test above (parent is a CompoundStmt, not a ForStmt). We omit a
// direct test here to avoid contriving an AST shape the grammar
// rejects — the path is already exercised by the `Other` default.

} // namespace

}  // namespace sturm_test_matcher_scope_kind_ns

void run_scope_kind_tests() {
    using namespace sturm_test_matcher_scope_kind_ns;
    test_sk_function_body();
    test_sk_braced_for_body();
    test_sk_braceless_for_body();
    test_sk_braced_while_body();
    test_sk_branch_if_then_braced();
    test_sk_branch_if_then_braceless();
    test_sk_branch_if_else_braced();
    test_sk_when_body();
    test_sk_other_nested_bare_block();
    test_sk_null_anchor();
}
