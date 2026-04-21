// matcher_when_operand_mutation.cpp — PM3-4 Class 1 diagnostic.
//
// Detects `WHEN(expr) { body }` invocations in which the user mutates,
// inside the WHEN body, a qbool/qint `DeclRefExpr` that also appears in
// the control expression. Per P4 ("quantum control is lexical scope,
// unified with classical branching"), the operands of a `WHEN` control
// expression must not be modified within its scope — doing so is
// undefined behaviour. This matcher reports the violation as an
// `Error`-severity `DiagnosticsEngine` diagnostic so compilation aborts
// (non-zero exit) and the user sees the precise mutation line.
//
// Detection shape (algorithm)
// ---------------------------
// Anchor on the middle `IfStmt` in the three-`if` tower the `WHEN` macro
// expands to — the same init-stmt pattern the Phase F WHEN-lift matcher
// binds (`matcher_when_lift.cpp:499-511`). On match the callback:
//
//   1. Walks the `materialize_when` argument via a `RecursiveASTVisitor`,
//      collecting every `DeclRefExpr` whose referenced `VarDecl` has
//      declared type `sturm::qbool` / `sturm::qint_t` / `sturm::qint`
//      into a `SmallPtrSet<const VarDecl*, 4>`. The walk is exhaustive
//      so compound / comparator / ternary / unary shapes are all
//      accounted for (nested `ConditionalOperator` expressions are
//      visited uniformly by `RecursiveASTVisitor`).
//
//   2. Descends `IfStmt::getThen()` twice to reach the user's body
//      `CompoundStmt` (pattern copied from
//      `matcher_when_lift.cpp:55-57`). First hop lands on the inner
//      `_when_guard_` `IfStmt`; second hop lands on the body.
//
//   3. Runs a second `RecursiveASTVisitor` over the body looking for
//      `CXXOperatorCallExpr`s with an assignment-shape operator
//      (`=`, `^=`, `+=`, `-=`, `*=`, `/=`, `%=`) or a `UnaryOperator`
//      with an increment / decrement opcode (`++`, `--` — pre or post).
//      For each such node, peel the LHS (or operand) to a
//      `DeclRefExpr`; if the referenced `VarDecl` is in the set from
//      step 1, the match is a hit.
//
//   4. On a hit the callback funnels the mutation's begin loc through
//      `SourceManager::getFileLoc(...)` (matches the PM3-2 / PM3-3
//      plumbing so the `TextDiagnosticPrinter` cites the user's file
//      rather than Clang's `<memory-buffer>` fallback) and calls
//      `diag.report_when_operand_mutation(loc, name)`. The `unit_`
//      is NOT touched — this matcher is advisory only.
//
// Shapes that correctly do NOT fire
// ---------------------------------
//   - `WHEN(a | b) { qbool c; c ^= 1; }` — `c` is not in the operand
//     set, so its mutation is ignored.
//   - `WHEN(a | b) { (void)a; }`         — a read of an operand is not
//     a mutation; step 3 only visits assignment-shape op-calls and
//     inc / dec unary-ops.
//   - A WHEN whose argument is not a qbool / qint identifier (e.g. a
//     `bool` literal in a test harness) never contributes a
//     VarDecl to the set, so nothing inside the body can ever hit.
//
// Registration
// ------------
// PM3-4 is a pure diagnostic matcher; it does not mutate the QUnit. The
// `diag` reference it is handed must outlive the MatchFinder's run; the
// consumer owns the underlying `DiagContext`.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"

#include "diag_context.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Casting.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// Return true iff `qt` resolves to a CXXRecord whose name is one of
// `qbool`, `qint`, or `qint_t`. Mirrors the element-type guard used in
// `matcher_user_routine.cpp`'s `is_output_param` — any one of those
// spellings counts as a quantum element.
bool is_quantum_type(QualType qt) {
    // Strip top-level references and qualifications so both `qbool` and
    // `const qbool&` resolve to the same CXXRecord.
    if (qt.isNull()) return false;
    QualType stripped = qt.getNonReferenceType().getUnqualifiedType();
    const CXXRecordDecl* rd = stripped->getAsCXXRecordDecl();
    if (!rd) return false;
    const std::string name = rd->getNameAsString();
    return name == "qbool" || name == "qint" || name == "qint_t";
}

// Walk an arbitrary expression tree and push every DeclRefExpr to a
// qbool / qint VarDecl into the caller's set. Exhaustive — descends
// into every child (compound bitwise ops, comparator ops, ternary
// conditionals, parens, implicit casts, function-call arguments, etc.).
// `RecursiveASTVisitor` handles the traversal uniformly.
class OperandCollector
    : public RecursiveASTVisitor<OperandCollector> {
public:
    explicit OperandCollector(
        llvm::SmallPtrSetImpl<const VarDecl*>* out)
        : out_(out) {}

    bool VisitDeclRefExpr(DeclRefExpr* dre) {
        if (!dre || !out_) return true;
        const auto* vd = llvm::dyn_cast_or_null<VarDecl>(dre->getDecl());
        if (!vd) return true;
        if (!is_quantum_type(vd->getType())) return true;
        out_->insert(vd);
        return true;
    }

private:
    llvm::SmallPtrSetImpl<const VarDecl*>* out_;
};

// Walk a body CompoundStmt and, for every assignment-shape
// CXXOperatorCallExpr or inc/dec UnaryOperator whose target resolves
// to a VarDecl in the operand set, invoke the caller's reporter with
// the mutation loc and the variable name.
//
// The reporter is a std::function<void(SourceLocation, std::string)>
// so the walker does not have to carry a DiagContext reference itself
// — the callback captures one in its enclosing scope.
class MutationFinder
    : public RecursiveASTVisitor<MutationFinder> {
public:
    using Reporter =
        std::function<void(SourceLocation, const std::string&)>;

    MutationFinder(const llvm::SmallPtrSetImpl<const VarDecl*>* operands,
                   Reporter reporter)
        : operands_(operands), reporter_(std::move(reporter)) {}

    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr* call) {
        if (!call || !operands_) return true;
        // Restrict to assignment-shape operators. The set must match
        // the PH-3 outer-var guard's invariant (xor-assign PA-3 / PA-4,
        // compound-assigns PB / PC) and additionally plain `=` because
        // P4 forbids ANY mutation — including a bare copy-assign — of
        // a WHEN operand inside the body.
        const auto op = call->getOperator();
        const bool is_assign_shape =
            (op == clang::OO_Equal)        ||
            (op == clang::OO_CaretEqual)   ||
            (op == clang::OO_PlusEqual)    ||
            (op == clang::OO_MinusEqual)   ||
            (op == clang::OO_StarEqual)    ||
            (op == clang::OO_SlashEqual)   ||
            (op == clang::OO_PercentEqual) ||
            (op == clang::OO_PipeEqual)    ||
            (op == clang::OO_AmpEqual);
        if (!is_assign_shape) return true;
        if (call->getNumArgs() < 1) return true;

        check_lhs(call->getArg(0), call->getBeginLoc());
        return true;
    }

    bool VisitUnaryOperator(UnaryOperator* uop) {
        if (!uop || !operands_) return true;
        const auto op = uop->getOpcode();
        const bool is_inc_dec =
            op == UO_PreInc || op == UO_PreDec ||
            op == UO_PostInc || op == UO_PostDec;
        if (!is_inc_dec) return true;
        check_lhs(uop->getSubExpr(), uop->getBeginLoc());
        return true;
    }

private:
    // Common LHS classifier for both op-call and unary-op shapes.
    // Strips implicit / paren / temp wrappers and, if the peeled
    // expression is a DeclRefExpr to a VarDecl in the operand set,
    // reports the mutation at `report_loc`.
    void check_lhs(const Expr* lhs, SourceLocation report_loc) {
        if (!lhs || !reporter_) return;
        const Expr* inner = detail::peel_to_payload(lhs);
        const auto* dre =
            llvm::dyn_cast_or_null<DeclRefExpr>(inner);
        if (!dre) return;
        const auto* vd =
            llvm::dyn_cast_or_null<VarDecl>(dre->getDecl());
        if (!vd) return;
        if (!operands_->count(vd)) return;
        reporter_(report_loc, vd->getNameAsString());
    }

    const llvm::SmallPtrSetImpl<const VarDecl*>* operands_;
    Reporter reporter_;
};

class WhenOperandMutationCallback : public MatchFinder::MatchCallback {
public:
    WhenOperandMutationCallback(QUnit* unit, DiagContext* diag)
        : unit_(unit), diag_(diag) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* when_if = r.Nodes.getNodeAs<IfStmt>("when_if");
        const auto* mat_call =
            r.Nodes.getNodeAs<CallExpr>("materialize_call");
        if (!when_if || !mat_call || !r.Context || !diag_) return;

        const SourceManager& sm = r.Context->getSourceManager();
        const LangOptions& lang = r.Context->getLangOpts();

        // Guard #1: the IfStmt must originate inside a macro *body*
        // expansion. A direct user `if (auto _when_val_ = ...)` is
        // spelled in file, not in a macro body, and must not match.
        // Mirrors the Phase F WHEN-lift matcher's three-guard stack.
        const SourceLocation if_loc = when_if->getIfLoc();
        if (!if_loc.isValid() || !if_loc.isMacroID()) return;
        if (!sm.isMacroBodyExpansion(if_loc)) return;

        // Guard #2: the immediate-expansion macro must spell `WHEN`.
        if (!detail::is_expansion_of_macro(if_loc, sm, lang, "WHEN")) {
            return;
        }

        // Guard #3: the materialize call must have exactly one
        // argument (the user's WHEN expression). Any other arity is
        // a sign we've matched an unrelated overload shape.
        if (mat_call->getNumArgs() != 1) return;
        const Expr* arg = mat_call->getArg(0);
        if (!arg) return;

        // Step 1: collect every DeclRefExpr to a qbool / qint VarDecl
        // inside the WHEN argument. SmallPtrSet<4> is plenty — even
        // an elaborate compound like `(a | b) & (c | d)` only
        // registers four operands.
        llvm::SmallPtrSet<const VarDecl*, 4> operands;
        OperandCollector collector(&operands);
        collector.TraverseStmt(const_cast<Expr*>(arg));
        if (operands.empty()) {
            // No quantum-typed operand in the argument — nothing the
            // body could mutate that P4 cares about. Bail cleanly.
            return;
        }

        // Step 2: descend `IfStmt::getThen()` twice to reach the
        // user's body CompoundStmt. Pattern copied from
        // `matcher_when_lift.cpp:55-57`. Any structural mismatch
        // (e.g. the macro body shape is not the two-`if` tower we
        // expect) returns a null and the callback bails.
        const Stmt* first = when_if->getThen();
        const auto* inner_if = llvm::dyn_cast_or_null<IfStmt>(first);
        if (!inner_if) return;
        const Stmt* second = inner_if->getThen();
        const auto* body =
            llvm::dyn_cast_or_null<CompoundStmt>(second);
        if (!body) return;

        // Step 3: scan the body for assignment-shape mutations that
        // target any operand in the set. The reporter closure
        // captures `diag_` and funnels each hit through the
        // file-loc conversion before delegating to the shared
        // `DiagContext::report_when_operand_mutation` stub.
        auto reporter =
            [&](SourceLocation mut_loc, const std::string& name) {
                // Funnel through `getFileLoc` so diagnostics cite
                // the user's filename under the plugin's nested
                // CompilerInvocation as well as under the
                // standalone driver. Mirrors the PM3-2 /
                // PM3-3 plumbing.
                const SourceLocation file_loc =
                    sm.getFileLoc(mut_loc);
                diag_->report_when_operand_mutation(
                    file_loc, std::string_view(name));
            };
        MutationFinder finder(&operands, std::move(reporter));
        finder.TraverseStmt(const_cast<CompoundStmt*>(body));
        (void)unit_; // intentionally not mutated — advisory only.
    }

private:
    QUnit* unit_;
    DiagContext* diag_;
};

// Callback pool — matches the lifetime convention used by every other
// matcher_*.cpp module. The callback is owned here so the MatchFinder
// (which stores a raw pointer) does not outlive it.
std::vector<std::unique_ptr<WhenOperandMutationCallback>>&
when_operand_mutation_callback_pool() {
    static std::vector<std::unique_ptr<WhenOperandMutationCallback>> pool;
    return pool;
}

} // namespace

void register_when_operand_mutation_matcher(
    clang::ast_matchers::MatchFinder& finder,
    QUnit& unit,
    DiagContext& diag) {
    // Anchor on the middle `IfStmt` in the three-`if` WHEN tower —
    // the same init-stmt pattern the Phase F WHEN-lift matcher uses.
    // Binding both `when_if` and `materialize_call` gives the
    // callback direct access to the argument expression and the
    // user's body without re-walking the AST.
    auto materialize_call = callExpr(
        callee(functionDecl(hasName("materialize_when"))),
        argumentCountIs(1)
    ).bind("materialize_call");

    auto pattern = ifStmt(
        hasInitStatement(declStmt(hasSingleDecl(
            varDecl(
                hasName("_when_val_"),
                hasInitializer(ignoringImplicit(materialize_call))
            )
        )))
    ).bind("when_if");

    auto& pool = when_operand_mutation_callback_pool();
    pool.push_back(std::make_unique<WhenOperandMutationCallback>(
        &unit, &diag));
    finder.addMatcher(pattern, pool.back().get());
}

} // namespace sturm::transpile
