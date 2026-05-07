// matcher_main_lifecycle.cpp — Frontend simpl. P7 (sturm-e3ru) implementation.
//
// Plan §3 Phase 7, PRD §5.4. See `matcher_main_lifecycle.hpp` for the
// public surface (Hit struct, registration entrypoint).
//
// Implementation strategy
// -----------------------
// One AST anchor:
//
//   functionDecl(isMain(), isExpansionInMainFile()).bind("main_fn")
//
// `isMain()` matches any FunctionDecl whose IdentifierInfo says
// `isMain()` returns true — this is exactly Clang's notion of the
// program-entry main, gated on its being a top-level definition with
// the canonical signature shape. The matcher then narrows in the
// callback:
//
//   1. The FunctionDecl must be a *definition* (the in-source body),
//      not a forward-declared prototype.
//   2. The return type must be non-deduced `int` — the GCC trailing-
//      return-form `auto main() -> int` produces a deduced return
//      type that does not satisfy this check, matching the PRD §5.4
//      "OUT OF SCOPE" note.
//   3. The parameter list shape must be one of:
//        - 0 params
//        - 2 params: int, char**
//        - 3 params: int, char**, char**
//      Other arities are out of scope — they violate the PRD's
//      "unique main FunctionDecl" assumption.
//   4. `STURM_NO_AUTO_LIFECYCLE` must NOT be defined (escape hatch).
//   5. `STURM_UMBRELLA_INCLUDED` must BE defined (sentinel — user's
//      TU includes the umbrella).
//   6. The body's first child statement must NOT be a DeclStmt
//      declaring a VarDecl named `__sturm_ctx` (idempotency probe).
//   7. The source file containing the FunctionDecl must NOT have
//      `gtest` in its filename (R3 mitigation — gtest provides its
//      own `main`).
//
// On all gates passing, the callback pushes one
// `MainLifecycleHit{main_fn}` into the consumer's hit vector. The
// consumer's drain step (in transpile_consumer.cpp) reads the
// FunctionDecl pointer to recover the body CompoundStmt and emit
// the IIFE rewrite via `QReplacement`.
//
// LoC budget: ≤ 300 (matcher) — well under the 260-LoC predicted size.

#include "matcher_main_lifecycle.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
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
// Non-goals section explicitly excludes test-framework auto-injection:
// "GoogleTest / Catch fixtures continue to call sturm_backend_create /
// sturm_set_thread_context explicitly." We use a filename probe rather
// than a smarter AST-level check because the filename is the cheapest
// reliable signal; downstream test wrappers can always opt into the
// explicit lifecycle via STURM_NO_AUTO_LIFECYCLE.
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
//
// Both probes use the Preprocessor's identifier table to look up the
// macro name and ask whether a definition is currently in scope. The
// matcher fires from inside `matchAST`, which runs after the parse
// completes — so any macro `#define`d in the TU's source (even after
// the umbrella include) is visible. `isMacroDefined` walks the macro
// map, returning true for any active macro definition.
bool macro_is_defined(Preprocessor& pp, std::string_view name) {
    IdentifierInfo* ii = pp.getIdentifierInfo(name);
    if (ii == nullptr) return false;
    return pp.isMacroDefined(ii->getName());
}

// ── Signature shape gate ────────────────────────────────────────────────────
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
    // Return type must be a non-deduced `int`. The trailing-return-form
    // `auto main() -> int` carries a deduced AutoType in the source
    // syntax; `getReturnType()` resolves to `int` AFTER deduction, but
    // the source form is OOS per spec. Detect by inspecting the
    // declared return type's source-spelling — if the FunctionDecl was
    // declared with `auto`, the TypeSourceInfo carries an AutoTypeLoc.
    const QualType rt = fn.getReturnType();
    if (rt.isNull()) return false;
    if (!rt->isIntegerType()) return false;
    // Reject deduced (`auto`) returns. `getDeclaredReturnType()` returns
    // the as-written type from the function declarator; if the user
    // wrote `auto main() -> int`, that type is `auto` (a deduced type),
    // not `int`. Clang's `Type::isUndeducedAutoType` handles the
    // detection cleanly.
    const QualType declared_rt = fn.getDeclaredReturnType();
    if (!declared_rt.isNull() && declared_rt->isUndeducedAutoType()) {
        return false;
    }

    const unsigned n_params = fn.getNumParams();
    if (n_params == 0) return true;
    if (n_params != 2 && n_params != 3) return false;

    // First param must be `int`.
    const ParmVarDecl* p0 = fn.getParamDecl(0);
    if (p0 == nullptr) return false;
    const QualType t0 = p0->getType();
    if (t0.isNull() || !t0->isIntegerType()) return false;

    // Subsequent params must be `char**`.
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
// transpiler revisions without breaking idempotency. Defensively,
// the probe also walks subsequent declarations in the same DeclStmt
// (a DeclStmt can introduce multiple VarDecls in C++) so a `T x, y;`
// shape does not slip through.
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

// ── Callback ────────────────────────────────────────────────────────────────

class MainLifecycleCallback : public MatchFinder::MatchCallback {
public:
    MainLifecycleCallback(Preprocessor* pp,
                          std::vector<MainLifecycleHit>* out)
        : pp_(pp), out_(out) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* fn = r.Nodes.getNodeAs<FunctionDecl>("main_fn");
        if (fn == nullptr || pp_ == nullptr || out_ == nullptr) return;

        // Definition gate — forward-declared prototypes have no body to
        // rewrite. `isMain()` matches both prototype and definition;
        // we only want the definition.
        if (!fn->doesThisDeclarationHaveABody()) return;
        if (fn->getBody() == nullptr) return;

        // Form gate.
        if (!is_supported_main_signature(*fn)) return;

        // Macro gates.
        if (macro_is_defined(*pp_, "STURM_NO_AUTO_LIFECYCLE")) return;
        if (!macro_is_defined(*pp_, "STURM_UMBRELLA_INCLUDED")) return;

        // R3: skip gtest-named source files.
        if (filename_looks_like_gtest(*fn, r.Context->getSourceManager())) {
            return;
        }

        // Idempotency probe.
        if (body_has_sturm_ctx_probe(*fn)) return;

        // De-duplicate: at most one hit per TU. The matcher's anchor
        // (`isMain()`) only ever fires once per TU because a program
        // can have at most one `main` definition (the C++ standard
        // makes a multi-definition `main` ill-formed), but the
        // MatchFinder may invoke the callback multiple times if the
        // TU is parsed in a configuration that exposes multiple
        // re-declarations. Walk the existing hit list and skip if
        // we already published this FunctionDecl.
        for (const auto& hit : *out_) {
            if (hit.main_fn == fn) return;
        }

        MainLifecycleHit h;
        h.main_fn = fn;
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
    // Function-local static unique_ptr pool keeps the callback alive
    // for the duration of the finder's run. Mirrors the pattern used
    // by every other matcher in this directory (matcher_qram_subscript
    // et al.) — MatchFinder stores raw callback pointers, so the
    // owning storage must outlive the run.
    using CallbackPool =
        std::vector<std::unique_ptr<MainLifecycleCallback>>;
    static CallbackPool s_pool;
    s_pool.emplace_back(std::make_unique<MainLifecycleCallback>(&pp, &hits));
    auto& cb = *s_pool.back();

    // Anchor: a FunctionDecl whose IdentifierInfo flags it as `main`.
    // `isMain()` is the canonical AST predicate. We do NOT add
    // `isExpansionInMainFile()` because the hermetic snapshot fixtures
    // place the entire main file at the top of the TU — main lives
    // in the same FileID — and the gtest filename probe in the
    // callback handles the test-framework collision separately.
    finder.addMatcher(
        functionDecl(isMain()).bind("main_fn"),
        &cb);
}

} // namespace sturm::transpile
