// matcher_main_lifecycle.cpp — Frontend simpl. P7 (sturm-e3ru) +
// sturm-0tcv (entry-point attribute extension).
//
// Plan §3 Phase 7, PRD §5.4. See `matcher_main_lifecycle.hpp` for the
// public surface (Hit struct, registration entrypoint).
//
// Implementation strategy
// -----------------------
// Two AST anchors:
//
//   (a) functionDecl(isMain()).bind("main_fn") — the original P7
//       anchor; matches Clang's notion of `int main(...)`.
//
//   (b) functionDecl(hasAttr(attr::Annotate)).bind("entry_point_fn") —
//       sturm-0tcv anchor; matches every FunctionDecl carrying at
//       least one AnnotateAttr. The callback narrows to the
//       `sturm::entry_point` annotation string via
//       `has_entry_point_attr()`.
//
// The two anchors are deliberately disjoint at the AST level —
// `isMain()` only fires on the canonical `int main(...)` declaration,
// and AnnotateAttr coverage requires a `[[clang::annotate(...)]]`
// attribute to be attached. If a user spells
// `[[clang::annotate("sturm::entry_point")]] int main(...)` (legal
// per the standard) both anchors fire on the same FunctionDecl; the
// callback's de-dup walk over the existing hit vector collapses the
// duplicate to a single Main-shape hit (Main wins because the
// callback for the main anchor is registered first and writes its
// hit before the entry-point anchor's callback runs).
//
// Callback flow per match:
//
//   1. Recover the bound FunctionDecl. Bail on null.
//   2. The FunctionDecl must be a *definition* (the in-source body),
//      not a forward-declared prototype.
//   3. Form-of-decl gates:
//        * Main anchor: signature must match one of the three
//          accepted main shapes (PRD §5.4) — see
//          `is_supported_main_signature`.
//        * Entry-point anchor: subject must be a free FunctionDecl
//          (not a CXXMethodDecl), return type must be `void` or an
//          integral type (the IIFE return-capture is well-typed for
//          both), and `has_entry_point_attr(fn)` must answer true.
//   4. `STURM_NO_AUTO_LIFECYCLE` must NOT be defined (escape hatch).
//   5. `STURM_UMBRELLA_INCLUDED` must BE defined (sentinel — user's
//      TU includes the umbrella).
//   6. The body's first child statement must NOT be a DeclStmt
//      declaring a VarDecl named `__sturm_ctx` (idempotency probe).
//   7. Main anchor only: the source file containing the FunctionDecl
//      must NOT have `gtest` in its filename (R3 mitigation — gtest
//      provides its own `main`). The entry-point anchor is NOT gated
//      by the gtest filename probe; the attribute is opt-in and the
//      user takes responsibility for not double-wrapping. This is
//      the load-bearing relaxation that makes the sturm-0tcv
//      "GoogleTest TEST_F" use case work.
//
// On all gates passing, the callback pushes one MainLifecycleHit
// into the consumer's hit vector. The hit's `kind` field is set to
// `Main`, `EntryPointReturn`, or `EntryPointVoid` per the rules
// above; the consumer's drain step (in transpile_consumer.cpp)
// reads the FunctionDecl pointer and the kind to recover the body
// CompoundStmt and emit the IIFE rewrite via QReplacement.
//
// LoC budget: ≤ 380 (matcher), extended from 300 to absorb the
// entry-point gating logic.

#include "matcher_main_lifecycle.hpp"

#include "entry_point_attribute.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/Support/Casting.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// ── R3 mitigation ───────────────────────────────────────────────────────────
//
// gtest defines its own `main` (when linked against `gtest_main`) — a
// user-defined `int main(int argc, char** argv)` in a `test_*.cpp` file
// would otherwise be eligible for our auto-injection. The PRD §3
// Non-goals section excludes test-framework auto-injection at the
// *main* level: "GoogleTest / Catch fixtures continue to call
// sturm_backend_create / sturm_set_thread_context explicitly." We use
// a filename probe rather than a smarter AST-level check because the
// filename is the cheapest reliable signal; downstream test wrappers
// can always opt into the explicit lifecycle via
// STURM_NO_AUTO_LIFECYCLE.
//
// The probe is consulted ONLY for the Main anchor — the entry-point
// anchor (sturm-0tcv) is opt-in via attribute and the gtest filename
// probe must NOT skip it (the attribute's whole point is to enable
// auto-injection on a non-main FunctionDecl inside a TU that already
// has another `main`).
bool filename_looks_like_gtest(const FunctionDecl& fn,
                               const SourceManager& sm) {
    const SourceLocation loc = fn.getLocation();
    if (loc.isInvalid()) return false;
    const SourceLocation expansion = sm.getExpansionLoc(loc);
    if (expansion.isInvalid()) return false;
    const FileID fid = sm.getFileID(expansion);
    if (fid.isInvalid()) return false;
    const StringRef path = sm.getFilename(expansion);
    if (path.empty()) return false;
    return path.contains("gtest");
}

// ── Macro-state probes ──────────────────────────────────────────────────────
bool macro_is_defined(Preprocessor& pp, std::string_view name) {
    IdentifierInfo* ii = pp.getIdentifierInfo(name);
    if (ii == nullptr) return false;
    return pp.isMacroDefined(ii->getName());
}

// ── Signature shape gate (Main anchor) ──────────────────────────────────────
//
// PRD §5.4 lists the three accepted forms:
//   int main();
//   int main(int, char**);
//   int main(int, char**, char**);
//
// The KR-style spelling `int main(int argc, char* argv[])` decays the
// array parameter to `char**` at the AST level (Clang adjusts the
// parameter type via the standard array-to-pointer adjustment), so it
// is covered by the same `char**` predicate.
bool is_supported_main_signature(const FunctionDecl& fn) {
    const QualType rt = fn.getReturnType();
    if (rt.isNull()) return false;
    if (!rt->isIntegerType()) return false;
    const QualType declared_rt = fn.getDeclaredReturnType();
    if (!declared_rt.isNull() && declared_rt->isUndeducedAutoType()) {
        return false;
    }

    const unsigned n_params = fn.getNumParams();
    if (n_params == 0) return true;
    if (n_params != 2 && n_params != 3) return false;

    const ParmVarDecl* p0 = fn.getParamDecl(0);
    if (p0 == nullptr) return false;
    const QualType t0 = p0->getType();
    if (t0.isNull() || !t0->isIntegerType()) return false;

    auto is_char_pp = [](const QualType& t) -> bool {
        if (t.isNull()) return false;
        if (!t->isPointerType()) return false;
        const QualType pointee = t->getPointeeType();
        if (pointee.isNull() || !pointee->isPointerType()) return false;
        const QualType inner = pointee->getPointeeType();
        if (inner.isNull()) return false;
        return inner->isCharType();
    };
    const ParmVarDecl* p1 = fn.getParamDecl(1);
    if (p1 == nullptr) return false;
    if (!is_char_pp(p1->getType())) return false;
    if (n_params == 2) return true;
    const ParmVarDecl* p2 = fn.getParamDecl(2);
    if (p2 == nullptr) return false;
    return is_char_pp(p2->getType());
}

// ── Form gate (Entry-point anchor) ──────────────────────────────────────────
//
// sturm-0tcv: a FunctionDecl carrying `[[sturm::entry_point]]` is
// eligible for auto-injection iff:
//   * It is NOT a CXXMethodDecl (free-function only — methods carry
//     an implicit `this` parameter that complicates the IIFE
//     wrapping). GoogleTest's TEST_F / TEST macros expand to a
//     CXXMethodDecl on a generated fixture class; users who want to
//     auto-inject test bodies should wrap the body in a free
//     function the test calls (the alternative — handling the
//     `this` capture in the IIFE — is out of scope here).
//   * Its return type is `void` OR an integral type. The matcher
//     classifies the return type so the emitter knows which IIFE
//     shape to use (see MainLifecycleHitKind in the header).
//   * It is a definition (the source body is what the rewriter
//     replaces).
//   * The corresponding kind selector is returned so the callback
//     can populate `MainLifecycleHit::kind`.
//
// Returns nullopt when the subject is ineligible.
enum class EntryPointShape { Void, IntegralReturn };

std::optional<EntryPointShape> classify_entry_point_shape(
    const FunctionDecl& fn) {
    // Member functions are out of scope (free-function only).
    // CXXMethodDecl includes implicit-this methods AND static
    // member functions; both carry the same complication for the
    // IIFE wrapping (the `[&]` capture clause in C++ does not
    // transparently make member names visible inside a lambda
    // body without an explicit `this` capture, so the rewrite
    // would need a different shape for methods).
    if (llvm::isa<CXXMethodDecl>(&fn)) {
        return std::nullopt;
    }

    // Definition required — forward declarations have no body to
    // wrap.
    if (!fn.doesThisDeclarationHaveABody()) {
        return std::nullopt;
    }
    if (fn.getBody() == nullptr) return std::nullopt;

    const QualType rt = fn.getReturnType();
    if (rt.isNull()) return std::nullopt;

    // Reject deduced returns. `auto` returns at this level are an
    // edge case the emitter cannot safely wrap (the IIFE's trailing
    // return type must be spelled explicitly).
    const QualType declared_rt = fn.getDeclaredReturnType();
    if (!declared_rt.isNull() && declared_rt->isUndeducedAutoType()) {
        return std::nullopt;
    }

    if (rt->isVoidType()) {
        return EntryPointShape::Void;
    }
    if (rt->isIntegerType()) {
        return EntryPointShape::IntegralReturn;
    }

    // Non-void, non-integral return types (pointers, classes, floats,
    // template-dependent types) are OUT OF SCOPE — the IIFE wrapper
    // would need a faithfully spelled trailing return type and the
    // emitter's current pipeline does not support arbitrary type
    // serialization at the IIFE level. Users with exotic return
    // types must fall back to the explicit lifecycle.
    return std::nullopt;
}

// ── Idempotency probe ───────────────────────────────────────────────────────
//
// PRD §5.4 mandates idempotency — re-running on already-rewritten
// source must be a no-op. The IIFE form's first statement is
//
//     sturm_backend_context_t* __sturm_ctx = sturm_backend_create(...);
//
// so we probe for a DeclStmt at position 0 of the body's CompoundStmt
// whose declared variable is named `__sturm_ctx`. The probe uses only
// the variable name — the type / initializer can mutate across
// transpiler revisions without breaking idempotency.
bool body_has_sturm_ctx_probe(const FunctionDecl& fn) {
    const Stmt* body = fn.getBody();
    if (body == nullptr) return false;
    const auto* compound = llvm::dyn_cast<CompoundStmt>(body);
    if (compound == nullptr) return false;
    if (compound->body_empty()) return false;
    const Stmt* first = *compound->body_begin();
    if (first == nullptr) return false;
    const auto* ds = llvm::dyn_cast<DeclStmt>(first);
    if (ds == nullptr) return false;
    for (const Decl* d : ds->decls()) {
        const auto* vd = llvm::dyn_cast_or_null<VarDecl>(d);
        if (vd == nullptr) continue;
        if (vd->getNameAsString() == "__sturm_ctx") return true;
    }
    return false;
}

// ── Callback (Main anchor) ──────────────────────────────────────────────────

class MainLifecycleCallback : public MatchFinder::MatchCallback {
public:
    MainLifecycleCallback(Preprocessor* pp,
                          std::vector<MainLifecycleHit>* out)
        : pp_(pp), out_(out) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* fn = r.Nodes.getNodeAs<FunctionDecl>("main_fn");
        if (fn == nullptr || pp_ == nullptr || out_ == nullptr) return;

        if (!fn->doesThisDeclarationHaveABody()) return;
        if (fn->getBody() == nullptr) return;

        if (!is_supported_main_signature(*fn)) return;

        if (macro_is_defined(*pp_, "STURM_NO_AUTO_LIFECYCLE")) return;
        if (!macro_is_defined(*pp_, "STURM_UMBRELLA_INCLUDED")) return;

        if (filename_looks_like_gtest(*fn, r.Context->getSourceManager())) {
            return;
        }

        if (body_has_sturm_ctx_probe(*fn)) return;

        // De-duplicate: at most one hit per FunctionDecl.
        for (const auto& hit : *out_) {
            if (hit.main_fn == fn) return;
        }

        MainLifecycleHit h;
        h.main_fn = fn;
        h.kind    = MainLifecycleHitKind::Main;
        out_->push_back(h);
    }

private:
    Preprocessor* pp_ = nullptr;
    std::vector<MainLifecycleHit>* out_ = nullptr;
};

// ── Callback (Entry-point anchor) ───────────────────────────────────────────

class EntryPointCallback : public MatchFinder::MatchCallback {
public:
    EntryPointCallback(Preprocessor* pp,
                       std::vector<MainLifecycleHit>* out)
        : pp_(pp), out_(out) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* fn = r.Nodes.getNodeAs<FunctionDecl>("entry_point_fn");
        if (fn == nullptr || pp_ == nullptr || out_ == nullptr) return;

        // Anchor matched any AnnotateAttr — narrow to the
        // `sturm::entry_point` annotation.
        if (!has_entry_point_attr(fn)) return;

        // Form gate.
        const auto shape = classify_entry_point_shape(*fn);
        if (!shape.has_value()) return;

        // Macro gates.
        if (macro_is_defined(*pp_, "STURM_NO_AUTO_LIFECYCLE")) return;
        if (!macro_is_defined(*pp_, "STURM_UMBRELLA_INCLUDED")) return;

        // NOTE: the gtest filename probe deliberately does NOT
        // apply here — the entry_point attribute is opt-in and the
        // canonical use case (GoogleTest TEST_F bodies, gtest-named
        // SetUp routines) lives in `*gtest*.cpp` files.

        if (body_has_sturm_ctx_probe(*fn)) return;

        // De-duplicate against existing hits. If a hit for this
        // FunctionDecl already exists (e.g. the Main anchor fired
        // first), skip to keep the hit vector unique.
        for (const auto& hit : *out_) {
            if (hit.main_fn == fn) return;
        }

        MainLifecycleHit h;
        h.main_fn = fn;
        h.kind = (*shape == EntryPointShape::Void)
                     ? MainLifecycleHitKind::EntryPointVoid
                     : MainLifecycleHitKind::EntryPointReturn;
        out_->push_back(h);
    }

private:
    Preprocessor* pp_ = nullptr;
    std::vector<MainLifecycleHit>* out_ = nullptr;
};

} // namespace

// ── Public registration entrypoint ─────────────────────────────────────────

void register_main_lifecycle_matcher(
    clang::ast_matchers::MatchFinder& finder,
    clang::Preprocessor& pp,
    std::vector<MainLifecycleHit>& hits) {
    // Function-local static unique_ptr pools keep the callbacks alive
    // for the duration of the finder's run. Two pools (one per
    // callback class) — MatchFinder stores raw callback pointers, so
    // the owning storage must outlive the run.
    using MainCallbackPool =
        std::vector<std::unique_ptr<MainLifecycleCallback>>;
    using EntryCallbackPool =
        std::vector<std::unique_ptr<EntryPointCallback>>;
    static MainCallbackPool s_main_pool;
    static EntryCallbackPool s_entry_pool;

    s_main_pool.emplace_back(std::make_unique<MainLifecycleCallback>(&pp, &hits));
    auto& main_cb = *s_main_pool.back();

    s_entry_pool.emplace_back(std::make_unique<EntryPointCallback>(&pp, &hits));
    auto& entry_cb = *s_entry_pool.back();

    // Anchor 1: a FunctionDecl whose IdentifierInfo flags it as `main`.
    // `isMain()` is the canonical AST predicate.
    finder.addMatcher(
        functionDecl(isMain()).bind("main_fn"),
        &main_cb);

    // Anchor 2: any FunctionDecl carrying an AnnotateAttr (sturm-0tcv).
    // The callback narrows on `has_entry_point_attr` so unrelated
    // annotations (other plugins, `sturm::reversible`, etc.) are
    // filtered out at the callback boundary. We use `hasAttr` with
    // `clang::attr::Annotate` so the matcher anchor stays cheap —
    // AST node matching only fires on decls that already carry at
    // least one annotation, avoiding a full FunctionDecl scan.
    finder.addMatcher(
        functionDecl(hasAttr(clang::attr::Annotate))
            .bind("entry_point_fn"),
        &entry_cb);
}

} // namespace sturm::transpile
