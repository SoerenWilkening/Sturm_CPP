// matcher_reversible_validate.cpp — Phase P P-C (sturm-z2e8.5):
// implementation of the P9d validation pass. See the sibling header
// for the contract. The module walks the body of every
// `[[sturm::reversible]]` forward via a `RecursiveASTVisitor` and
// fires one diagnostic per offending site in the five P9d classes.
//
// Per-CallExpr priority order (so diagnostic text is pinnable):
// measurement → I/O → unregistered. A single CallExpr fires at most
// one diagnostic. Loop / cond classes anchor on different node
// kinds and never compete on the same node.
//
// First reject reason wins on `.reason`; every subsequent reject
// still fires via `DiagContext` so the user sees every site in
// one pass. All `SourceLocation`s funnel through `getFileLoc`.

#include "matcher_reversible_validate.hpp"

#include "diag_context.hpp"
#include "reversible_attribute.hpp"
#include "routine_registry.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/Casting.h"

#include <string>
#include <string_view>

namespace sturm::transpile {

// ── Public: human-readable reason spellings ─────────────────────────

std::string_view to_string(ReversibleRejectReason reason) {
    switch (reason) {
        case ReversibleRejectReason::None:               return "none";
        case ReversibleRejectReason::NullDecl:           return "null_decl";
        case ReversibleRejectReason::NotReversible:      return "not_reversible";
        case ReversibleRejectReason::NoBody:             return "no_body";
        case ReversibleRejectReason::Measurement:        return "measurement";
        case ReversibleRejectReason::ClassicalIO:        return "classical_io";
        case ReversibleRejectReason::UnregisteredCallee: return "unregistered_callee";
        case ReversibleRejectReason::WhileLoop:          return "while_loop";
        case ReversibleRejectReason::ClassicalCond:      return "classical_cond";
    }
    return {}; // unreachable for a well-formed enum
}

namespace {

using namespace clang;

// Type classifiers. `is_quantum_record` strips references +
// qualifications so both `qbool` and `const qbool&` resolve to the
// same record; `qint_t<N>` is a ClassTemplateSpecialization whose
// `getName()` returns the template name.
bool is_quantum_record(QualType qt) {
    if (qt.isNull()) return false;
    QualType stripped = qt.getNonReferenceType().getUnqualifiedType();
    const CXXRecordDecl* rd = stripped->getAsCXXRecordDecl();
    if (!rd) return false;
    const std::string name = rd->getNameAsString();
    return name == "qbool" || name == "qint" || name == "qint_t";
}

// True iff `qt` is a classical collapse destination — builtin /
// integral / floating / boolean. `void` is excluded (cast-to-void
// is an explicit discard, not a measurement).
bool is_classical_destination(QualType qt) {
    if (qt.isNull()) return false;
    QualType stripped = qt.getNonReferenceType().getUnqualifiedType();
    if (stripped->isVoidType()) return false;
    return stripped->isBooleanType() || stripped->isIntegerType() ||
           stripped->isFloatingType();
}

const FunctionDecl* resolve_callee(const CallExpr* ce) {
    if (!ce) return nullptr;
    if (const FunctionDecl* fd = ce->getDirectCallee()) return fd;
    return nullptr;
}

std::string qualified_name(const FunctionDecl* fd) {
    if (!fd) return {};
    std::string qn = fd->getQualifiedNameAsString();
    return qn.empty() ? fd->getNameAsString() : qn;
}

// Measurement name set. The transpiler does not yet define a
// canonical public `measure(qbool)` primitive, so the list tracks
// the C ABI + C++ helper spellings.
bool name_is_measurement(std::string_view qn) {
    return qn == "sturm::measure_qubit" ||
           qn == "measure_qubit"        ||
           qn == "sturm_measure"        ||
           qn == "sturm::sturm_measure" ||
           qn == "::sturm_measure"      ||
           qn == "measure";
}

// Classical-I/O name set covering `<cstdio>` + `<iostream>` +
// observable side-effects. Fixed list today; expanded on demand.
bool name_is_classical_io(std::string_view qn) {
    if (qn == "printf"        || qn == "std::printf"     ||
        qn == "fprintf"       || qn == "std::fprintf"    ||
        qn == "sprintf"       || qn == "std::sprintf"    ||
        qn == "snprintf"      || qn == "std::snprintf"   ||
        qn == "scanf"         || qn == "std::scanf"      ||
        qn == "fscanf"        || qn == "std::fscanf"     ||
        qn == "sscanf"        || qn == "std::sscanf"     ||
        qn == "puts"          || qn == "std::puts"       ||
        qn == "gets"          || qn == "std::gets"       ||
        qn == "putchar"       || qn == "std::putchar"    ||
        qn == "getchar"       || qn == "std::getchar"    ||
        qn == "putc"          || qn == "std::putc"       ||
        qn == "getc"          || qn == "std::getc"       ||
        qn == "fputs"         || qn == "std::fputs"      ||
        qn == "fgets"         || qn == "std::fgets"      ||
        qn == "fputc"         || qn == "std::fputc"      ||
        qn == "fgetc"         || qn == "std::fgetc"      ||
        qn == "perror"        || qn == "std::perror") {
        return true;
    }
    // `<iostream>` inserter / extractor operators.
    if (qn == "std::operator<<" || qn == "std::operator>>" ||
        qn == "operator<<"      || qn == "operator>>") {
        return true;
    }
    // Observable side effects via C ABI.
    if (qn == "abort"         || qn == "std::abort"      ||
        qn == "exit"          || qn == "std::exit"       ||
        qn == "_Exit"         || qn == "std::_Exit"      ||
        qn == "__assert_fail" || qn == "raise"           ||
        qn == "std::raise") {
        return true;
    }
    return false;
}

// ── Body walker ─────────────────────────────────────────────────────

class ReversibleBodyValidator
    : public RecursiveASTVisitor<ReversibleBodyValidator> {
public:
    ReversibleBodyValidator(const FunctionDecl* forward,
                            ASTContext& ctx,
                            DiagContext& diag,
                            const RoutineRegistry& routine_reg)
        : forward_(forward),
          forward_name_(forward ? forward->getNameAsString()
                                : std::string()),
          ctx_(ctx), diag_(diag), routine_reg_(routine_reg) {}

    // `while` / `do-while` — P9d reject (unbounded trip count not
    // invertible by B11 loop reversal).
    bool VisitWhileStmt(WhileStmt* ws) {
        if (!ws) return true;
        const SourceLocation loc = file_loc(ws->getBeginLoc());
        fire(ReversibleRejectReason::WhileLoop, loc);
        diag_.report_reversible_while_loop(
            loc, std::string_view(forward_name_));
        return true;
    }
    bool VisitDoStmt(DoStmt* ds) {
        if (!ds) return true;
        const SourceLocation loc = file_loc(ds->getBeginLoc());
        fire(ReversibleRejectReason::WhileLoop, loc);
        diag_.report_reversible_while_loop(
            loc, std::string_view(forward_name_));
        return true;
    }

    // Classical-cond check on the condition slot of `if` / `?:`.
    // The walk still visits the cond expression's children so any
    // measurement call inside fires its own diagnostic too.
    bool VisitIfStmt(IfStmt* is) {
        if (!is) return true;
        check_cond_is_quantum(is->getCond(), is->getIfLoc());
        return true;
    }
    bool VisitConditionalOperator(ConditionalOperator* co) {
        if (!co) return true;
        check_cond_is_quantum(co->getCond(), co->getQuestionLoc());
        return true;
    }

    // CallExpr — classify by callee name. CXXOperatorCallExpr and
    // CXXMemberCallExpr inherit from CallExpr, so stream operator
    // calls and member-function calls funnel through here.
    bool VisitCallExpr(CallExpr* ce) {
        if (!ce) return true;
        const FunctionDecl* callee = resolve_callee(ce);
        const std::string qn = qualified_name(callee);
        const SourceLocation loc = file_loc(ce->getBeginLoc());

        if (name_is_measurement(qn)) {
            fire(ReversibleRejectReason::Measurement, loc);
            diag_.report_reversible_measurement(
                loc, std::string_view(forward_name_));
            return true;
        }
        if (name_is_classical_io(qn)) {
            fire(ReversibleRejectReason::ClassicalIO, loc);
            diag_.report_reversible_io(
                loc, std::string_view(forward_name_));
            return true;
        }
        // Accept iff the callee is `[[sturm::reversible]]` OR in
        // the PI-1 RoutineRegistry. Recursive self-calls are
        // currently rejected per PRD §9 Q4.
        const bool callee_is_reversible =
            callee != nullptr && is_reversible(callee);
        const bool callee_in_registry =
            callee != nullptr && routine_reg_.contains(callee);
        if (callee_is_reversible || callee_in_registry) return true;

        // Skip primitive op-call shapes — qbool/qint operator
        // overloads are the primitive op path the MVP matchers
        // consume, not user function calls.
        if (const auto* ocall = llvm::dyn_cast<CXXOperatorCallExpr>(ce)) {
            const OverloadedOperatorKind op = ocall->getOperator();
            if (op == OO_Equal      || op == OO_CaretEqual ||
                op == OO_PipeEqual  || op == OO_AmpEqual   ||
                op == OO_PlusEqual  || op == OO_MinusEqual ||
                op == OO_StarEqual  || op == OO_SlashEqual ||
                op == OO_PercentEqual ||
                op == OO_Tilde      || op == OO_Pipe       ||
                op == OO_Amp        || op == OO_Caret      ||
                op == OO_EqualEqual || op == OO_ExclaimEqual ||
                op == OO_Less       || op == OO_LessEqual  ||
                op == OO_Greater    || op == OO_GreaterEqual ||
                op == OO_PlusPlus   || op == OO_MinusMinus ||
                op == OO_Exclaim) {
                return true;
            }
        }
        // Skip implicit user-defined-conversion calls — the
        // enclosing explicit-cast node handles the measurement
        // classification.
        if (const auto* mce = llvm::dyn_cast<CXXMemberCallExpr>(ce)) {
            if (const auto* method = mce->getMethodDecl()) {
                if (llvm::isa<CXXConversionDecl>(method)) return true;
            }
        }
        // Empty qn → unresolved callee; skip rather than misclassify.
        if (qn.empty()) return true;
        fire(ReversibleRejectReason::UnregisteredCallee, loc);
        diag_.report_reversible_unregistered_callee(
            loc, std::string_view(qn));
        return true;
    }

    // Explicit-cast shapes — measurement when src is quantum +
    // dst is classical. Superset of PM3-5 (fires everywhere in a
    // reversible body, not only branch conditions).
    bool VisitCXXStaticCastExpr(CXXStaticCastExpr* e)  { return check_quantum_to_classical_cast(e); }
    bool VisitCStyleCastExpr(CStyleCastExpr* e)        { return check_quantum_to_classical_cast(e); }
    bool VisitCXXFunctionalCastExpr(CXXFunctionalCastExpr* e) { return check_quantum_to_classical_cast(e); }

    ReversibleRejectReason first_reason() const { return first_reason_; }
    unsigned diagnostics_fired() const { return diagnostics_fired_; }

private:
    void fire(ReversibleRejectReason reason, SourceLocation /*loc*/) {
        if (first_reason_ == ReversibleRejectReason::None) {
            first_reason_ = reason;
        }
        ++diagnostics_fired_;
    }

    SourceLocation file_loc(SourceLocation raw) const {
        return ctx_.getSourceManager().getFileLoc(raw);
    }

    // Fire classical-cond if the cond's static type (post implicit-
    // cast strip) is quantum. Normal `if (i < N)` stays silent.
    void check_cond_is_quantum(const Expr* cond, SourceLocation anchor) {
        if (!cond) return;
        const Expr* inner = cond->IgnoreParenImpCasts();
        if (!inner) return;
        if (is_quantum_record(inner->getType())) {
            const SourceLocation loc = file_loc(anchor);
            fire(ReversibleRejectReason::ClassicalCond, loc);
            diag_.report_reversible_classical_cond(
                loc, std::string_view(forward_name_));
        }
    }

    // Load-bearing AST shape: for `static_cast<bool>(q)` with
    // `explicit operator bool()`, Clang wraps the cast's sub-expr
    // in an ImplicitCastExpr `<UserDefinedConversion>` over a
    // CXXMemberCallExpr to the conversion operator. The sub-expr's
    // static type is already the classical destination; we must
    // peel through the UDC member-call to the implicit object arg
    // to recover the quantum source type.
    bool check_quantum_to_classical_cast(CastExpr* cast) {
        if (!cast) return true;
        if (!is_classical_destination(cast->getType())) return true;
        const Expr* src = cast->getSubExpr();
        if (!src) return true;

        // Direct path: sub-expr resolves to quantum.
        const Expr* inner = src->IgnoreParenImpCasts();
        if (inner && is_quantum_record(inner->getType())) {
            return fire_measurement(cast);
        }
        // UDC path: peel through `<UserDefinedConversion>` to the
        // CXXMemberCallExpr's implicit object argument.
        if (const auto* ice = llvm::dyn_cast<ImplicitCastExpr>(src)) {
            if (ice->getCastKind() == CK_UserDefinedConversion) {
                if (const auto* mce =
                        llvm::dyn_cast_or_null<CXXMemberCallExpr>(
                            ice->getSubExpr())) {
                    if (const Expr* obj = mce->getImplicitObjectArgument()) {
                        if (is_quantum_record(
                                obj->IgnoreParenImpCasts()->getType())) {
                            return fire_measurement(cast);
                        }
                    }
                }
            }
        }
        return true;
    }

    bool fire_measurement(CastExpr* cast) {
        const SourceLocation loc = file_loc(cast->getBeginLoc());
        fire(ReversibleRejectReason::Measurement, loc);
        diag_.report_reversible_measurement(
            loc, std::string_view(forward_name_));
        return true;
    }

    const FunctionDecl*    forward_;
    std::string            forward_name_;
    ASTContext&            ctx_;
    DiagContext&           diag_;
    const RoutineRegistry& routine_reg_;
    ReversibleRejectReason first_reason_ = ReversibleRejectReason::None;
    unsigned               diagnostics_fired_ = 0;
};

} // namespace

// ── Public: entry point ─────────────────────────────────────────────

ReversibleValidationResult validate_reversible_body(
    const clang::FunctionDecl* fd,
    clang::ASTContext& ctx,
    DiagContext& diag,
    const RoutineRegistry& routine_reg) {
    ReversibleValidationResult result;

    // Silent rejects: null / non-reversible / no body. Opt-in
    // contract — no diagnostic fires on these paths.
    if (fd == nullptr) {
        result.valid = false;
        result.reason = ReversibleRejectReason::NullDecl;
        return result;
    }
    if (!is_reversible(fd)) {
        result.valid = false;
        result.reason = ReversibleRejectReason::NotReversible;
        return result;
    }
    const Stmt* body = fd->getBody();
    if (body == nullptr) {
        result.valid = false;
        result.reason = ReversibleRejectReason::NoBody;
        return result;
    }

    // `TraverseStmt` accepts non-const Stmt*; the visitor does
    // not mutate the tree — same const_cast pattern as
    // `matcher_when_operand_mutation.cpp`.
    ReversibleBodyValidator walker(fd, ctx, diag, routine_reg);
    walker.TraverseStmt(const_cast<Stmt*>(body));

    result.diagnostics_fired = walker.diagnostics_fired();
    result.reason = walker.first_reason();
    result.valid = (result.reason == ReversibleRejectReason::None);
    return result;
}

} // namespace sturm::transpile
