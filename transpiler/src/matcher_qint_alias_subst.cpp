// matcher_qint_alias_subst.cpp — sturm-65rs.8 (Beat C1) implementation.
//
// Plan §9, PRD §4.3. See `matcher_qint_alias_subst.hpp` for the
// public surface (Match struct, registration entrypoint).
//
// Implementation strategy
// -----------------------
// Five AST anchors, one per row of the §4.3 match-anchors table:
//
//   (vd)   `varDecl(hasType(... cxxRecordDecl(hasName("qint"),
//          hasParent(namespaceDecl(hasName("frontend"))))))`
//   (pmd)  `parmVarDecl(...)` — same predicate.
//   (fd)   `fieldDecl(...)` — same predicate.
//   (fn)   `functionDecl(returns(... cxxRecordDecl ...))` — anchors
//          on the return TypeLoc.
//   (cast) `cxxFunctionalCastExpr(hasType(... cxxRecordDecl ...))`.
//
// The discriminator
//
//     cxxRecordDecl(hasName("qint"),
//                   hasParent(namespaceDecl(hasName("frontend"))))
//
// is shared via a single helper so the five anchors stay symmetric.
// PRD §4.3 close: this naturally rejects the namespace alias
// `using qint = sturm::qint_t<W>;` because the alias is a
// TypeAliasDecl (not a CXXRecordDecl) and resolves to `qint_t<W>`
// whose record name is `qint_t`, not `qint`.
//
// Each callback resolves the anchor's TypeSourceInfo, reads the
// TypeLoc's source range, and stamps a `QintAliasSubstMatch` into the
// caller's vector. A null `TypeSourceInfo` (e.g. implicit return type
// on a deduced lambda — defensive; not expected on the five anchors
// we match) yields no Match — the matcher publishes only well-formed
// hits.

#include "matcher_qint_alias_subst.hpp"

#include "qint_alias_carrier_walk.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceLocation.h"

#include <memory>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// ── TypeLoc range recovery ──────────────────────────────────────────────────
//
// For a node with TypeSourceInfo, return the TypeLoc's source range.
// Returns an invalid SourceRange when the node lacks TypeSourceInfo
// (defensive — the matcher's gates exclude such nodes from the
// publish path, so this is mostly a documentation surface). The
// declarator walk (single-level array-element / pointer-pointee, with
// R5 typedef-decline) lives in `qint_alias_carrier_walk.{hpp,cpp}`
// (plan §19b / PRD §9.3.1) — see that file for the wave-2 rationale.
SourceRange typeloc_range_of(const DeclaratorDecl* dd) {
    return declarator_typeloc_range(dd);
}

SourceRange typeloc_range_of_return(const FunctionDecl* fn) {
    if (!fn) return {};
    const TypeSourceInfo* tsi = fn->getTypeSourceInfo();
    if (!tsi) return {};
    TypeLoc tl = tsi->getTypeLoc();
    // Walk through any sugared TypeLoc layers (Paren, Attributed, …)
    // until we reach a FunctionTypeLoc, then take its return-loc.
    if (auto ftl = tl.getAsAdjusted<FunctionTypeLoc>()) {
        TypeLoc rtl = ftl.getReturnLoc();
        return rtl.getSourceRange();
    }
    // Fall back to the function's declared return-type range when the
    // TypeSourceInfo path could not resolve a FunctionTypeLoc.
    return fn->getReturnTypeSourceRange();
}

SourceRange typeloc_range_of_cast(const CXXFunctionalCastExpr* ce) {
    if (!ce) return {};
    const TypeSourceInfo* tsi = ce->getTypeInfoAsWritten();
    if (!tsi) return {};
    return tsi->getTypeLoc().getSourceRange();
}

// ── Per-callback dispatch ──────────────────────────────────────────────────
//
// Each callback narrows on its anchor's bound name, populates the
// corresponding `QintAliasSubstMatch` field, and pushes onto the
// caller's vector. A null TypeLoc range gates the publish (defensive).

class VarDeclCallback : public MatchFinder::MatchCallback {
public:
    explicit VarDeclCallback(std::vector<QintAliasSubstMatch>* out)
        : out_(out) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* vd = r.Nodes.getNodeAs<VarDecl>("vd");
        if (!vd || !out_) return;
        // Skip ParmVarDecl: it is a subclass of VarDecl, but we route
        // it through the ParmVarDecl arm so the bound `kind` is right.
        if (llvm::isa<ParmVarDecl>(vd)) return;
        QintAliasSubstMatch m;
        m.kind       = QintAliasSubstKind::VarDecl;
        m.vd         = vd;
        m.type_range = typeloc_range_of(vd);
        if (m.type_range.isInvalid()) return;
        out_->push_back(m);
    }
private:
    std::vector<QintAliasSubstMatch>* out_;
};

class ParmVarDeclCallback : public MatchFinder::MatchCallback {
public:
    explicit ParmVarDeclCallback(std::vector<QintAliasSubstMatch>* out)
        : out_(out) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* pmd = r.Nodes.getNodeAs<ParmVarDecl>("pmd");
        if (!pmd || !out_) return;
        QintAliasSubstMatch m;
        m.kind       = QintAliasSubstKind::ParmVarDecl;
        m.pmd        = pmd;
        m.type_range = typeloc_range_of(pmd);
        if (m.type_range.isInvalid()) return;
        out_->push_back(m);
    }
private:
    std::vector<QintAliasSubstMatch>* out_;
};

class FieldDeclCallback : public MatchFinder::MatchCallback {
public:
    explicit FieldDeclCallback(std::vector<QintAliasSubstMatch>* out)
        : out_(out) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* fd = r.Nodes.getNodeAs<FieldDecl>("fd");
        if (!fd || !out_) return;
        QintAliasSubstMatch m;
        m.kind       = QintAliasSubstKind::FieldDecl;
        m.fd         = fd;
        m.type_range = typeloc_range_of(fd);
        if (m.type_range.isInvalid()) return;
        out_->push_back(m);
    }
private:
    std::vector<QintAliasSubstMatch>* out_;
};

class FunctionDeclCallback : public MatchFinder::MatchCallback {
public:
    explicit FunctionDeclCallback(std::vector<QintAliasSubstMatch>* out)
        : out_(out) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* fn = r.Nodes.getNodeAs<FunctionDecl>("fn");
        if (!fn || !out_) return;
        QintAliasSubstMatch m;
        m.kind       = QintAliasSubstKind::FunctionDecl;
        m.fn         = fn;
        m.type_range = typeloc_range_of_return(fn);
        if (m.type_range.isInvalid()) return;
        out_->push_back(m);
    }
private:
    std::vector<QintAliasSubstMatch>* out_;
};

class FunctionalCastCallback : public MatchFinder::MatchCallback {
public:
    explicit FunctionalCastCallback(std::vector<QintAliasSubstMatch>* out)
        : out_(out) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* ce =
            r.Nodes.getNodeAs<CXXFunctionalCastExpr>("cast");
        if (!ce || !out_) return;
        QintAliasSubstMatch m;
        m.kind       = QintAliasSubstKind::FunctionalCast;
        m.cast       = ce;
        m.type_range = typeloc_range_of_cast(ce);
        if (m.type_range.isInvalid()) return;
        out_->push_back(m);
    }
private:
    std::vector<QintAliasSubstMatch>* out_;
};

// Per-callback pools owned by function-local statics. Mirrors the
// pattern in `matcher_qram_subscript.cpp::callback_pool<T>` so the
// finder's raw-pointer storage stays valid across the run.
template <class T>
std::vector<std::unique_ptr<T>>& callback_pool() {
    static std::vector<std::unique_ptr<T>> pool;
    return pool;
}

// ── The shared discriminator ────────────────────────────────────────────────
//
// `cxxRecordDecl(hasName("qint"),
//                hasParent(namespaceDecl(hasName("frontend"))))`
//
// PRD §4.3 close: this rejects the backend `qint_t<W>` (record name
// is `qint_t`, not `qint`) and the namespace alias `using qint = ...;`
// (which is a TypeAliasDecl, not a CXXRecordDecl) by construction.
auto frontend_qint_record() {
    return cxxRecordDecl(hasName("qint"),
                         hasParent(namespaceDecl(hasName("frontend"))));
}

// Wave 2 (sturm-7t85.1, plan §19b / PRD §9.3.1): single-level carrier
// disjunction used by the three declarator anchors (VarDecl,
// ParmVarDecl, FieldDecl). Direct + array-element + pointer-pointee.
// Multi-dim arrays / multi-level pointers are out of scope in v1
// (R6 — `sturm-7t85.6` follow-up).
auto frontend_qint_carrier() {
    return qualType(anyOf(
        // direct: qint x;
        hasCanonicalType(hasDeclaration(frontend_qint_record())),
        // array carrier: qint a[N] / qint a[]
        hasCanonicalType(arrayType(hasElementType(
            hasDeclaration(frontend_qint_record())))),
        // pointer carrier: qint* p
        hasCanonicalType(pointerType(pointee(
            hasDeclaration(frontend_qint_record()))))));
}

} // anonymous namespace

void register_qint_alias_subst_matcher(
    clang::ast_matchers::MatchFinder& finder,
    std::vector<QintAliasSubstMatch>& matches) {

    // (vd) VarDecl anchor — `sturm::frontend::qint x;` plus the
    // wave-2 carrier shapes `qint a[N];` and `qint* p;` via the shared
    // `frontend_qint_carrier()` disjunction. We bind on varDecl() and
    // exclude ParmVarDecl in the callback so the ParmVarDecl arm is
    // the single source of truth for parameters.
    {
        auto& pool = callback_pool<VarDeclCallback>();
        pool.push_back(std::make_unique<VarDeclCallback>(&matches));
        finder.addMatcher(
            varDecl(hasType(frontend_qint_carrier())).bind("vd"),
            pool.back().get());
    }

    // (pmd) ParmVarDecl anchor — `void demo(sturm::frontend::qint p)`,
    // `void f(qint b[]);`, `void g(qint* q);`.
    {
        auto& pool = callback_pool<ParmVarDeclCallback>();
        pool.push_back(std::make_unique<ParmVarDeclCallback>(&matches));
        finder.addMatcher(
            parmVarDecl(hasType(frontend_qint_carrier())).bind("pmd"),
            pool.back().get());
    }

    // (fd) FieldDecl anchor — `struct S { sturm::frontend::qint f; };`,
    // plus carrier shapes `struct S { qint c[3]; };` and
    // `struct T { qint* d; };`.
    {
        auto& pool = callback_pool<FieldDeclCallback>();
        pool.push_back(std::make_unique<FieldDeclCallback>(&matches));
        finder.addMatcher(
            fieldDecl(hasType(frontend_qint_carrier())).bind("fd"),
            pool.back().get());
    }

    // (fn) FunctionDecl anchor — return type is `sturm::frontend::qint`.
    {
        auto& pool = callback_pool<FunctionDeclCallback>();
        pool.push_back(std::make_unique<FunctionDeclCallback>(&matches));
        finder.addMatcher(
            functionDecl(returns(hasCanonicalType(hasDeclaration(
                frontend_qint_record())))).bind("fn"),
            pool.back().get());
    }

    // (cast) CXXFunctionalCastExpr anchor — `sturm::frontend::qint(0)`.
    {
        auto& pool = callback_pool<FunctionalCastCallback>();
        pool.push_back(std::make_unique<FunctionalCastCallback>(&matches));
        finder.addMatcher(
            cxxFunctionalCastExpr(hasType(hasCanonicalType(hasDeclaration(
                frontend_qint_record())))).bind("cast"),
            pool.back().get());
    }
}

} // namespace sturm::transpile
