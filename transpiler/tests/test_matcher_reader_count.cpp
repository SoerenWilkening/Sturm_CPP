// test_matcher_reader_count.cpp — Phase J PJ-1a tests for the
// `detail::count_readers_in_scope` reader-count helper.
//
// The reader-count helper answers: "within `scope_anchor`'s extent, how
// many DeclRefExpr nodes refer to the decl at `decl_loc` (spelled
// `name`)?" PJ-1d (zero-ancilla fusion gate) needs this to decide
// whether `qbool __t = a & b; x ^= __t;` has exactly one reader of
// `__t` (namely, the XOR's RHS); PJ-4a (dead-ancilla elimination)
// reuses it to detect a reader-count of 0.
//
// The helper signature is:
//   int detail::count_readers_in_scope(
//       llvm::StringRef            name,
//       clang::SourceLocation      decl_loc,
//       const clang::Stmt*         scope_anchor,
//       clang::ASTContext&         ctx);
//
// Semantics:
//   - Walks every DeclRefExpr in the subtree rooted at `scope_anchor`.
//   - Bumps the count whenever `dre->getDecl()->getLocation() == decl_loc`
//     AND the spelled name matches `name` (belt-and-braces; the
//     decl_loc comparison alone already discriminates shadowed locals
//     at their distinct source locations, but name matching costs
//     nothing and catches typo-style mismatches early).
//   - Descends into nested CompoundStmts (braced blocks inside the
//     scope) AND into PH-1 braceless body stmts (single-stmt for/while/
//     if bodies without braces), mirroring PH-1's `is_user_braceless_body`
//     descent. This guarantees a reader buried inside `for (...) stmt;`
//     counts the same as one in `for (...) { stmt; }`.
//   - The VarDecl's own name-bound location is NOT a reader (decls are
//     not DeclRefExprs); only uses of the variable count.
//   - A null anchor returns 0 defensively (the callers always pass a
//     valid scope anchor, but a null guard keeps unit tests clean).
//
// The test harness compiles each snippet, walks the AST to find the
// enclosing CompoundStmt of a named function, locates the named
// VarDecl inside it, and then invokes `count_readers_in_scope` with
// the function body as the scope anchor. We deliberately mirror the
// PJ-3a test harness pattern — an independent ASTConsumer per test —
// rather than routing through a MatchFinder, so the helper is pinned
// without coupling to any particular matcher registration.

#include "test_matcher_harness.hpp"

#include "matcher_common.hpp"
#include "sturm/transpile/qir.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

// ── Probe plumbing ─────────────────────────────────────────────────────────
//
// The caller supplies:
//   - `target_var` : the VarDecl identifier whose readers we count
//     (used to both locate the decl AST node and pass its source
//     location / spelled name into the helper).
//   - `owning_fn`  : the enclosing function's identifier. Its body
//     CompoundStmt is used as the scope anchor by default.
//   - optional `anchor_picker` : if non-null, overrides the default
//     "function body" anchor — used by the braceless-body and
//     nested-block tests to pin the helper to a narrower extent.
//
// The probe returns `{found, count}`; `found == false` means the
// harness could not locate the requested VarDecl or anchor (indicates
// a broken test, not a legitimate zero).

struct ReaderCountResult {
    bool found = false;
    int  count = 0;
};

using AnchorPicker =
    std::function<const clang::Stmt*(const clang::FunctionDecl*,
                                     clang::ASTContext&)>;

class ReaderCountVisitor
    : public clang::RecursiveASTVisitor<ReaderCountVisitor> {
public:
    ReaderCountVisitor(clang::ASTContext& ctx,
                       std::string target_var,
                       std::string owning_fn,
                       AnchorPicker picker,
                       ReaderCountResult* out)
        : ctx_(ctx),
          target_var_(std::move(target_var)),
          owning_fn_(std::move(owning_fn)),
          picker_(std::move(picker)),
          out_(out) {}

    bool VisitFunctionDecl(clang::FunctionDecl* fd) {
        if (out_->found) return true;
        if (!fd || !fd->hasBody()) return true;
        if (fd->getNameAsString() != owning_fn_) return true;

        // Locate the target VarDecl inside the function body.
        struct VarFinder : clang::RecursiveASTVisitor<VarFinder> {
            std::string target;
            const clang::VarDecl* found = nullptr;
            bool VisitVarDecl(clang::VarDecl* vd) {
                if (!vd) return true;
                if (found) return true;
                if (vd->getNameAsString() == target) found = vd;
                return true;
            }
        };
        VarFinder vf;
        vf.target = target_var_;
        vf.TraverseStmt(fd->getBody());
        if (!vf.found) return true;

        const clang::Stmt* anchor = picker_ ? picker_(fd, ctx_)
                                            : fd->getBody();
        if (!anchor) return true;

        out_->count = detail::count_readers_in_scope(
            llvm::StringRef(target_var_),
            vf.found->getLocation(),
            anchor,
            ctx_);
        out_->found = true;
        return false;
    }

private:
    clang::ASTContext& ctx_;
    std::string        target_var_;
    std::string        owning_fn_;
    AnchorPicker       picker_;
    ReaderCountResult* out_;
};

class ReaderCountConsumer : public clang::ASTConsumer {
public:
    ReaderCountConsumer(std::string target_var,
                        std::string owning_fn,
                        AnchorPicker picker,
                        ReaderCountResult* out)
        : target_var_(std::move(target_var)),
          owning_fn_(std::move(owning_fn)),
          picker_(std::move(picker)),
          out_(out) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        ReaderCountVisitor v(ctx, target_var_, owning_fn_, picker_, out_);
        v.TraverseAST(ctx);
    }
private:
    std::string        target_var_;
    std::string        owning_fn_;
    AnchorPicker       picker_;
    ReaderCountResult* out_;
};

class ReaderCountAction : public clang::ASTFrontendAction {
public:
    ReaderCountAction(std::string target_var,
                      std::string owning_fn,
                      AnchorPicker picker,
                      ReaderCountResult* out)
        : target_var_(std::move(target_var)),
          owning_fn_(std::move(owning_fn)),
          picker_(std::move(picker)),
          out_(out) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<ReaderCountConsumer>(
            target_var_, owning_fn_, picker_, out_);
    }
private:
    std::string        target_var_;
    std::string        owning_fn_;
    AnchorPicker       picker_;
    ReaderCountResult* out_;
};

class ReaderCountFactory : public clang::tooling::FrontendActionFactory {
public:
    ReaderCountFactory(std::string target_var,
                       std::string owning_fn,
                       AnchorPicker picker,
                       ReaderCountResult* out)
        : target_var_(std::move(target_var)),
          owning_fn_(std::move(owning_fn)),
          picker_(std::move(picker)),
          out_(out) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<ReaderCountAction>(
            target_var_, owning_fn_, picker_, out_);
    }
private:
    std::string        target_var_;
    std::string        owning_fn_;
    AnchorPicker       picker_;
    ReaderCountResult* out_;
};

// Compile `stub + user_src` and return the reader count for
// `target_var` inside `owning_fn`'s body.
ReaderCountResult count_in(std::string_view stub,
                           std::string_view user_src,
                           std::string_view target_var,
                           std::string_view owning_fn,
                           AnchorPicker picker = nullptr) {
    std::string code;
    code.reserve(stub.size() + user_src.size());
    code.append(stub);
    code.append(user_src);

    ReaderCountResult out;
    ReaderCountFactory factory(std::string(target_var),
                               std::string(owning_fn),
                               std::move(picker),
                               &out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PJ-1a reader count)\n");
    }
    return out;
}

// ── Tests ─────────────────────────────────────────────────────────────────

// Single reader: the canonical PJ-1 fusion shape. `qbool t = a & b; x ^= t;`
// with `t` read exactly once (the XOR RHS). Count must be 1.
void test_rc_single_reader() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, qbool x) {
    qbool t = a & b;
    x = t;
}
)CPP";
    ReaderCountResult r = count_in(kQBoolStub, src, "t", "demo");
    CHECK(r.found);
    CHECK(r.count == 1);
}

// Zero readers: `qbool t = a | b;` with no use of `t` anywhere. This is
// the PJ-4 dead-ancilla case: the decl stands alone and must yield 0.
// The VarDecl's own name-bound location MUST NOT be counted.
void test_rc_zero_readers() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b) {
    qbool t = a | b;
}
)CPP";
    ReaderCountResult r = count_in(kQBoolStub, src, "t", "demo");
    CHECK(r.found);
    CHECK(r.count == 0);
}

// Multiple readers: `x = t; y = t;` — two distinct DeclRefExprs refer
// to `t`. Count must be 2, not 1 (fusion gate rejects this case).
void test_rc_two_readers() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, qbool x, qbool y) {
    qbool t = a & b;
    x = t;
    y = t;
}
)CPP";
    ReaderCountResult r = count_in(kQBoolStub, src, "t", "demo");
    CHECK(r.found);
    CHECK(r.count == 2);
}

// Nested CompoundStmt descent: the reader is inside a bare nested
// `{ ... }` block within the function body. The helper must descend
// into the nested block.
void test_rc_nested_compound_descent() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, qbool x) {
    qbool t = a & b;
    {
        x = t;
    }
}
)CPP";
    ReaderCountResult r = count_in(kQBoolStub, src, "t", "demo");
    CHECK(r.found);
    CHECK(r.count == 1);
}

// Nested brace descent with multiple blocks: one reader in each of two
// sibling nested blocks, plus one at the top level of the scope. Count
// must be 3.
void test_rc_multi_nested_blocks() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, qbool x, qbool y, qbool z) {
    qbool t = a & b;
    x = t;
    {
        y = t;
    }
    {
        z = t;
    }
}
)CPP";
    ReaderCountResult r = count_in(kQBoolStub, src, "t", "demo");
    CHECK(r.found);
    CHECK(r.count == 3);
}

// Braceless for-body descent: the reader is inside a PH-1 braceless
// `for (...) stmt;` body. The helper must descend into the non-compound
// body stmt just like `is_user_braceless_body` does for scope-finding.
void test_rc_braceless_for_body() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, qbool x) {
    qbool t = a & b;
    for (int i = 0; i < 1; ++i) x = t;
}
)CPP";
    ReaderCountResult r = count_in(kQBoolStub, src, "t", "demo");
    CHECK(r.found);
    CHECK(r.count == 1);
}

// Braceless if-body descent: mirror of the for-body case but with an
// `if (cond) stmt;` branch body.
void test_rc_braceless_if_body() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, qbool x, bool cond) {
    qbool t = a & b;
    if (cond) x = t;
}
)CPP";
    ReaderCountResult r = count_in(kQBoolStub, src, "t", "demo");
    CHECK(r.found);
    CHECK(r.count == 1);
}

// Shadowed variable: an inner block redeclares `t`. The inner `t`'s
// readers must NOT count against the outer `t`'s decl_loc because
// their `getDecl()->getLocation()` values differ. Anchoring the helper
// to the outer function body and targeting the outer `t`: readers of
// the outer `t` are zero (the inner block shadows every potential
// use).
void test_rc_shadowed_inner_not_counted() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, qbool c, qbool d, qbool x) {
    qbool t = a & b;
    {
        qbool t = c & d; // shadows the outer `t`
        x = t;           // reads the inner `t`, NOT the outer
    }
}
)CPP";
    ReaderCountResult r = count_in(kQBoolStub, src, "t", "demo");
    CHECK(r.found);
    // The outer `t` has zero readers: the inner block's DeclRefExpr
    // resolves to the inner decl, whose location differs from the
    // outer's. Key discriminator is `getDecl()->getLocation()`.
    CHECK(r.count == 0);
}

// Reader inside a nested `for` loop body (braced). Exercises the
// descent into a CompoundStmt that is the body of a control-flow
// statement, not a bare block. Equivalent to the nested-block test
// above for the helper's behaviour, but pins the code path where the
// descent walks through a ForStmt.
void test_rc_reader_inside_for_body() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, qbool x) {
    qbool t = a & b;
    for (int i = 0; i < 3; ++i) {
        x = t;
    }
}
)CPP";
    ReaderCountResult r = count_in(kQBoolStub, src, "t", "demo");
    CHECK(r.found);
    // One reader per iteration at AST level is still one DeclRefExpr.
    CHECK(r.count == 1);
}

// Extent confinement: the anchor is a narrower inner block, not the
// whole function body. Readers of `t` outside that block must NOT
// count. Uses the AnchorPicker to anchor on the nested CompoundStmt
// inside demo(): only the reader inside that block is in-scope.
void test_rc_extent_confined_to_anchor() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, qbool x, qbool y) {
    qbool t = a & b;
    x = t;      // outside the inner block — MUST NOT count
    {
        y = t;  // inside the inner block — MUST count
    }
}
)CPP";
    // Anchor-picker: find the first nested CompoundStmt inside demo()
    // whose parent is also a CompoundStmt (i.e. the bare `{ ... }`
    // nested block, not the function body itself).
    AnchorPicker pick_inner =
        [](const clang::FunctionDecl* fd,
           clang::ASTContext& ctx) -> const clang::Stmt* {
        struct NestedFinder : clang::RecursiveASTVisitor<NestedFinder> {
            clang::ASTContext* ctx = nullptr;
            const clang::CompoundStmt* found = nullptr;
            bool VisitCompoundStmt(clang::CompoundStmt* cs) {
                if (found) return true;
                const auto parents =
                    ctx->getParents(clang::DynTypedNode::create(*cs));
                if (parents.empty()) return true;
                if (parents[0].get<clang::CompoundStmt>() != nullptr) {
                    found = cs;
                }
                return true;
            }
        };
        NestedFinder nf;
        nf.ctx = &ctx;
        nf.TraverseStmt(fd->getBody());
        return nf.found;
    };

    ReaderCountResult r =
        count_in(kQBoolStub, src, "t", "demo", pick_inner);
    CHECK(r.found);
    CHECK(r.count == 1);
}

// Null scope anchor: defensive guard — the helper must not crash on a
// null anchor and must return 0. Callers always pass a valid anchor,
// but the null-path is a cheap sanity check.
void test_rc_null_anchor() {
    struct NullConsumer : public clang::ASTConsumer {
        int* count_out;
        explicit NullConsumer(int* o) : count_out(o) {}
        void HandleTranslationUnit(clang::ASTContext& ctx) override {
            *count_out = detail::count_readers_in_scope(
                llvm::StringRef("t"),
                clang::SourceLocation{},
                /*scope_anchor=*/nullptr,
                ctx);
        }
    };
    struct NullAction : public clang::ASTFrontendAction {
        int* count_out;
        explicit NullAction(int* o) : count_out(o) {}
        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance&, llvm::StringRef) override {
            return std::make_unique<NullConsumer>(count_out);
        }
    };
    struct NullFactory : public clang::tooling::FrontendActionFactory {
        int* count_out;
        explicit NullFactory(int* o) : count_out(o) {}
        std::unique_ptr<clang::FrontendAction> create() override {
            return std::make_unique<NullAction>(count_out);
        }
    };

    int count = 42; // seed with sentinel to ensure the helper writes 0
    NullFactory factory(&count);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    std::string code = "void demo() {}\n";
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr, "FAIL  tool run returned false "
                             "(PJ-1a null anchor)\n");
    }
    CHECK(count == 0);
}

// Distinct-name non-match: a different variable (`s`) with the same
// shape must not contribute to `t`'s count. Keys off decl_loc (the
// name check is belt-and-braces), so this also covers the case where
// a same-named variable at a different decl_loc is present.
void test_rc_distinct_name_not_counted() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, qbool x) {
    qbool t = a & b;
    qbool s = a | b;
    x = s;   // reads `s`, NOT `t`
}
)CPP";
    ReaderCountResult r = count_in(kQBoolStub, src, "t", "demo");
    CHECK(r.found);
    CHECK(r.count == 0);
}

} // namespace

void run_reader_count_tests() {
    test_rc_single_reader();
    test_rc_zero_readers();
    test_rc_two_readers();
    test_rc_nested_compound_descent();
    test_rc_multi_nested_blocks();
    test_rc_braceless_for_body();
    test_rc_braceless_if_body();
    test_rc_shadowed_inner_not_counted();
    test_rc_reader_inside_for_body();
    test_rc_extent_confined_to_anchor();
    test_rc_null_anchor();
    test_rc_distinct_name_not_counted();
}
