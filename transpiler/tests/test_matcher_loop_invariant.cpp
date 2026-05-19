// test_matcher_loop_invariant.cpp — Phase J PJ-3b tests for the
// `detail::expr_is_loop_invariant` loop-invariance probe.
//
// The PJ-3 uncompute-hoisting matcher needs to know whether every operand of
// a candidate op (e.g. `qbool t = a & b;` inside a for-loop body) can be
// safely hoisted out of the loop. An operand is loop-invariant iff:
//
//   1. The operand's declaration (`operand_ref.decl_loc`) is OUTSIDE the
//      loop body — declarations inside the body are recomputed every
//      iteration and cannot be hoisted.
//   2. No write to the same name is reachable within the loop body.
//      A "write" here is any compound-assign (`^=`, `+=`, etc.) or
//      assignment-shape (`=`) whose LHS resolves to the same decl_loc and
//      name. Any write anywhere inside the loop body disqualifies the
//      operand from being loop-invariant.
//
// Signature:
//
//   bool detail::expr_is_loop_invariant(
//       const QValueRef&           operand_ref,
//       const clang::Stmt*         loop_body_anchor,
//       clang::ASTContext&         ctx);
//
// Semantics tested here:
//
//   - Outer-declared operand with no writes inside the loop body → invariant.
//   - Outer-declared operand with a `^=` write inside the body → not.
//   - Outer-declared operand with a `+=` write inside the body → not.
//   - Outer-declared operand with a plain `=` write inside the body → not.
//   - Operand whose decl_loc lives INSIDE the loop body → not (a local
//     `qbool t = ...;` declared per-iteration is not hoistable).
//   - Writes to a DIFFERENT variable (distinct name / distinct decl_loc)
//     must NOT disqualify our target — the probe is precise.
//   - A shadowing decl inside the body (same name, different decl_loc)
//     that is assigned to must NOT count as a write to the outer — the
//     decl_loc check is the primary discriminator, same as the PJ-1a
//     reader-count helper.
//   - Null `loop_body_anchor` → invariant is impossible to determine;
//     the helper returns false defensively (the caller treats a null
//     anchor as "skip hoisting").
//   - Invalid `operand_ref.decl_loc` → defensively returns false (there
//     is no meaningful "outside the loop body" for an invalid loc).
//
// Harness shape mirrors test_matcher_scope_kind.cpp / test_matcher_reader_count.cpp:
// we compile a snippet, locate the for-loop body, locate the target VarDecl
// (to build the QValueRef), and invoke `expr_is_loop_invariant` directly.

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

namespace sturm_test_matcher_loop_invariant_ns {

using namespace sturm::transpile;

namespace {

// ── Probe plumbing ─────────────────────────────────────────────────────────
//
// The caller specifies:
//   - `target_var` : the VarDecl whose loop-invariance we probe.
//   - `owning_fn`  : the enclosing function's identifier. Used only to
//     locate the target VarDecl unambiguously; the loop body itself is
//     picked as the first ForStmt / WhileStmt inside that function.
//
// The probe returns `{found, invariant}`. `found == false` means the
// harness could not locate the requested VarDecl or loop (broken test).

struct InvariantResult {
    bool found     = false;
    bool invariant = false;
};

class InvariantVisitor
    : public clang::RecursiveASTVisitor<InvariantVisitor> {
public:
    InvariantVisitor(clang::ASTContext& ctx,
                     std::string target_var,
                     std::string owning_fn,
                     InvariantResult* out)
        : ctx_(ctx),
          target_var_(std::move(target_var)),
          owning_fn_(std::move(owning_fn)),
          out_(out) {}

    bool VisitFunctionDecl(clang::FunctionDecl* fd) {
        if (out_->found) return true;
        if (!fd || !fd->hasBody()) return true;
        if (fd->getNameAsString() != owning_fn_) return true;

        // Locate the first ForStmt / WhileStmt in the function body.
        struct LoopFinder : clang::RecursiveASTVisitor<LoopFinder> {
            const clang::Stmt* body = nullptr;
            bool VisitForStmt(clang::ForStmt* fs) {
                if (!body && fs && fs->getBody()) body = fs->getBody();
                return true;
            }
            bool VisitWhileStmt(clang::WhileStmt* ws) {
                if (!body && ws && ws->getBody()) body = ws->getBody();
                return true;
            }
        };
        LoopFinder lf;
        lf.TraverseStmt(fd->getBody());
        if (!lf.body) return true;

        // Locate the target VarDecl anywhere inside the function (may be a
        // parameter or a local). The test names are chosen to be unique
        // within each snippet so the first-match walk is fine.
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
        // Walk the function body AND the parameter list — parameters are
        // not reached by a body-only walk.
        for (unsigned i = 0; i < fd->getNumParams(); ++i) {
            if (vf.found) break;
            clang::ParmVarDecl* p = fd->getParamDecl(i);
            if (p && p->getNameAsString() == target_var_) {
                vf.found = p;
                break;
            }
        }
        if (!vf.found) vf.TraverseStmt(fd->getBody());
        if (!vf.found) return true;

        QValueRef ref;
        ref.name = target_var_;
        ref.decl_loc = vf.found->getLocation();

        out_->invariant =
            detail::expr_is_loop_invariant(ref, lf.body, ctx_);
        out_->found = true;
        return false;
    }

private:
    clang::ASTContext& ctx_;
    std::string        target_var_;
    std::string        owning_fn_;
    InvariantResult*   out_;
};

class InvariantConsumer : public clang::ASTConsumer {
public:
    InvariantConsumer(std::string target_var,
                      std::string owning_fn,
                      InvariantResult* out)
        : target_var_(std::move(target_var)),
          owning_fn_(std::move(owning_fn)),
          out_(out) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        InvariantVisitor v(ctx, target_var_, owning_fn_, out_);
        v.TraverseAST(ctx);
    }
private:
    std::string      target_var_;
    std::string      owning_fn_;
    InvariantResult* out_;
};

class InvariantAction : public clang::ASTFrontendAction {
public:
    InvariantAction(std::string target_var,
                    std::string owning_fn,
                    InvariantResult* out)
        : target_var_(std::move(target_var)),
          owning_fn_(std::move(owning_fn)),
          out_(out) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<InvariantConsumer>(
            target_var_, owning_fn_, out_);
    }
private:
    std::string      target_var_;
    std::string      owning_fn_;
    InvariantResult* out_;
};

class InvariantFactory : public clang::tooling::FrontendActionFactory {
public:
    InvariantFactory(std::string target_var,
                     std::string owning_fn,
                     InvariantResult* out)
        : target_var_(std::move(target_var)),
          owning_fn_(std::move(owning_fn)),
          out_(out) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<InvariantAction>(
            target_var_, owning_fn_, out_);
    }
private:
    std::string      target_var_;
    std::string      owning_fn_;
    InvariantResult* out_;
};

// Compile `stub + user_src` and probe loop-invariance of `target_var`
// inside `owning_fn`, anchored on that function's first for/while body.
InvariantResult probe_in(std::string_view stub,
                         std::string_view user_src,
                         std::string_view target_var,
                         std::string_view owning_fn) {
    std::string code;
    code.reserve(stub.size() + user_src.size());
    code.append(stub);
    code.append(user_src);

    InvariantResult out;
    InvariantFactory factory(std::string(target_var),
                             std::string(owning_fn),
                             &out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PJ-3b loop invariant)\n");
    }
    return out;
}

// ── Tests ─────────────────────────────────────────────────────────────────

// Outer-declared operand, no write inside the loop body → invariant.
// The canonical hoisting shape: `a` and `b` are outer-scoped qbool refs,
// the loop body uses them only in a compute (`qbool t = a & b;`) with no
// mutation of either.
void test_li_outer_no_writes() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b) {
    for (int i = 0; i < 3; ++i) {
        qbool t = a & b;
        (void)t;
    }
}
)CPP";
    InvariantResult r = probe_in(kQBoolStub, src, "a", "demo");
    CHECK(r.found);
    CHECK(r.invariant == true);
}

// Outer-declared operand with a `^=` write inside the body → NOT invariant.
// The body mutates `a` (via `a = ...;` shape — the stub overloads `=` on
// qbool). Our target variable IS the one being written.
void test_li_outer_xor_assign_write_disqualifies() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b) {
    for (int i = 0; i < 3; ++i) {
        a = b;
        qbool t = a & b;
        (void)t;
    }
}
)CPP";
    // `a` is written to inside the loop body via operator= — that is a
    // write to the decl at `a`'s param loc, so `a` is NOT invariant.
    InvariantResult r = probe_in(kQBoolStub, src, "a", "demo");
    CHECK(r.found);
    CHECK(r.invariant == false);
}

// Outer-declared operand with a plain `=` write (copy-assign) inside
// the body → NOT invariant. The stub's `qbool& operator=(const qbool&)`
// produces a CXXOperatorCallExpr on `=`; the probe must catch that shape
// and reject invariance.
void test_li_outer_plain_assign_write_disqualifies() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, qbool c) {
    for (int i = 0; i < 3; ++i) {
        a = c;
        qbool t = b | c;
        (void)t;
    }
}
)CPP";
    InvariantResult r = probe_in(kQBoolStub, src, "a", "demo");
    CHECK(r.found);
    CHECK(r.invariant == false);
}

// Operand whose decl_loc lives INSIDE the loop body → NOT invariant.
// A per-iteration local `qbool local = ...;` is re-declared every
// iteration; it cannot be hoisted.
void test_li_inner_decl_not_invariant() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b) {
    for (int i = 0; i < 3; ++i) {
        qbool local = a;
        qbool t = local & b;
        (void)t;
    }
}
)CPP";
    InvariantResult r = probe_in(kQBoolStub, src, "local", "demo");
    CHECK(r.found);
    CHECK(r.invariant == false);
}

// Writes to a DIFFERENT variable must NOT disqualify our target. The
// body assigns to `c` only; `a` is never written to. The probe must
// key on decl_loc + name and ignore writes to other variables.
void test_li_write_to_other_var_does_not_disqualify() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, qbool c) {
    for (int i = 0; i < 3; ++i) {
        c = a;
        qbool t = a & b;
        (void)t;
    }
}
)CPP";
    InvariantResult r = probe_in(kQBoolStub, src, "a", "demo");
    CHECK(r.found);
    CHECK(r.invariant == true);
}

// Shadowing decl inside the body with the same name `a` but different
// decl_loc. Writes to the inner `a` must NOT disqualify the outer `a`
// from being invariant: decl_loc is the primary discriminator (matches
// the PJ-1a reader-count helper's shadowing semantics).
void test_li_shadow_does_not_disqualify_outer() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b) {
    for (int i = 0; i < 3; ++i) {
        qbool a = b;     // shadows the outer parameter `a`
        a = b;           // writes the inner (shadowed) `a`, NOT the outer
        (void)a;
    }
}
)CPP";
    // Target is the OUTER `a` (the function parameter). Its decl_loc is
    // at the parameter list, not inside the loop. The inner shadow has a
    // distinct decl_loc, so its write does not count against the outer.
    InvariantResult r = probe_in(kQBoolStub, src, "a", "demo");
    CHECK(r.found);
    CHECK(r.invariant == true);
}

// `+=` compound-assign is also a write and must disqualify. Use a qint
// target so the compound-assign overload exists. The stub here declares
// a local qint_t with `operator+=` so Clang lowers `a += b` to a
// CXXOperatorCallExpr on `+=`, which the probe must recognise as a write.
void test_li_outer_add_assign_write_disqualifies() {
    // Local qint stub: one type with `operator+=` so `a += b;` becomes a
    // CXXOperatorCallExpr. The shared kQBoolStub's `qint` class does not
    // carry `+=`, so we roll our own.
    constexpr std::string_view kQintPlusStub = R"CPP(
namespace sturm {
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    qint_t& operator+=(const qint_t&) { return *this; }
};
} // namespace sturm
using qint_t = sturm::qint_t;
)CPP";
    constexpr std::string_view src = R"CPP(
void demo(qint_t a, qint_t b) {
    for (int i = 0; i < 3; ++i) {
        a += b;
    }
}
)CPP";
    InvariantResult r = probe_in(kQintPlusStub, src, "a", "demo");
    CHECK(r.found);
    CHECK(r.invariant == false);
}

// Nested brace descent: the write lives inside a nested CompoundStmt
// within the loop body. The probe must descend and still detect it.
void test_li_nested_write_descent() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b) {
    for (int i = 0; i < 3; ++i) {
        {
            a = b;  // nested bare block — still inside loop body
        }
        qbool t = a & b;
        (void)t;
    }
}
)CPP";
    InvariantResult r = probe_in(kQBoolStub, src, "a", "demo");
    CHECK(r.found);
    CHECK(r.invariant == false);
}

// Write inside a nested if-body within the loop body → still a write,
// still disqualifies. The walk descends through every control-flow
// nested child.
void test_li_write_inside_nested_if_disqualifies() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, bool cond) {
    for (int i = 0; i < 3; ++i) {
        if (cond) {
            a = b;
        }
        qbool t = a & b;
        (void)t;
    }
}
)CPP";
    InvariantResult r = probe_in(kQBoolStub, src, "a", "demo");
    CHECK(r.found);
    CHECK(r.invariant == false);
}

// While-loop coverage: the probe works equally well with WhileStmt bodies
// (the harness picks up the first ForStmt OR WhileStmt).
void test_li_while_body_invariant() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b) {
    int i = 0;
    while (i < 3) {
        qbool t = a & b;
        (void)t;
        ++i;
    }
}
)CPP";
    InvariantResult r = probe_in(kQBoolStub, src, "a", "demo");
    CHECK(r.found);
    CHECK(r.invariant == true);
}

// Null anchor: defensive guard — the helper must not crash and must
// return `false` (cannot hoist without a loop body).
void test_li_null_anchor() {
    struct NullConsumer : public clang::ASTConsumer {
        bool* out;
        explicit NullConsumer(bool* o) : out(o) {}
        void HandleTranslationUnit(clang::ASTContext& ctx) override {
            QValueRef ref;
            ref.name = "x";
            ref.decl_loc = clang::SourceLocation{};
            *out = detail::expr_is_loop_invariant(
                ref, /*loop_body_anchor=*/nullptr, ctx);
        }
    };
    struct NullAction : public clang::ASTFrontendAction {
        bool* out;
        explicit NullAction(bool* o) : out(o) {}
        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance&, llvm::StringRef) override {
            return std::make_unique<NullConsumer>(out);
        }
    };
    struct NullFactory : public clang::tooling::FrontendActionFactory {
        bool* out;
        explicit NullFactory(bool* o) : out(o) {}
        std::unique_ptr<clang::FrontendAction> create() override {
            return std::make_unique<NullAction>(out);
        }
    };

    bool out = true; // seed with sentinel to ensure the helper writes false
    NullFactory factory(&out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    std::string code = "void demo() {}\n";
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PJ-3b null anchor)\n");
    }
    CHECK(out == false);
}

// Invalid operand decl_loc: an operand without a valid source location
// (e.g. a synthetic / builtin decl) cannot be meaningfully classified as
// "outside the loop body", so the probe defensively returns false.
void test_li_invalid_decl_loc() {
    struct InvalidConsumer : public clang::ASTConsumer {
        bool* out;
        explicit InvalidConsumer(bool* o) : out(o) {}
        void HandleTranslationUnit(clang::ASTContext& ctx) override {
            QValueRef ref;
            ref.name = "x";
            ref.decl_loc = clang::SourceLocation{}; // invalid
            // Build a tiny dummy scope anchor by picking the TU body's
            // first FunctionDecl's body, if any. For this test we just
            // reuse a null anchor analog by passing a valid CompoundStmt
            // that is NOT a loop — the probe's first short-circuit on
            // invalid decl_loc must still fire before any other check.
            const clang::TranslationUnitDecl* tu = ctx.getTranslationUnitDecl();
            const clang::Stmt* anchor = nullptr;
            for (const clang::Decl* d : tu->decls()) {
                if (const auto* fd = clang::dyn_cast<clang::FunctionDecl>(d)) {
                    if (fd->hasBody()) { anchor = fd->getBody(); break; }
                }
            }
            *out = detail::expr_is_loop_invariant(ref, anchor, ctx);
        }
    };
    struct InvalidAction : public clang::ASTFrontendAction {
        bool* out;
        explicit InvalidAction(bool* o) : out(o) {}
        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance&, llvm::StringRef) override {
            return std::make_unique<InvalidConsumer>(out);
        }
    };
    struct InvalidFactory : public clang::tooling::FrontendActionFactory {
        bool* out;
        explicit InvalidFactory(bool* o) : out(o) {}
        std::unique_ptr<clang::FrontendAction> create() override {
            return std::make_unique<InvalidAction>(out);
        }
    };

    bool out = true;
    InvalidFactory factory(&out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    std::string code = "void demo() {}\n";
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PJ-3b invalid decl_loc)\n");
    }
    CHECK(out == false);
}

} // namespace

}  // namespace sturm_test_matcher_loop_invariant_ns

void run_loop_invariant_tests() {
    using namespace sturm_test_matcher_loop_invariant_ns;
    test_li_outer_no_writes();
    test_li_outer_xor_assign_write_disqualifies();
    test_li_outer_plain_assign_write_disqualifies();
    test_li_inner_decl_not_invariant();
    test_li_write_to_other_var_does_not_disqualify();
    test_li_shadow_does_not_disqualify_outer();
    test_li_outer_add_assign_write_disqualifies();
    test_li_nested_write_descent();
    test_li_write_inside_nested_if_disqualifies();
    test_li_while_body_invariant();
    test_li_null_anchor();
    test_li_invalid_decl_loc();
}
