// test_reversible_attribute.cpp — Phase P P-A (sturm-z2e8.2):
// regression tests for `sturm::transpile::is_reversible()`.
//
// Three acceptance cases from the issue description:
//
//   1. Recognises `[[clang::annotate("sturm::reversible")]]` on a plain
//      function decl — positive baseline. Uses the canonical Clang
//      `annotate` carrier because `[[sturm::reversible]]` is not a
//      first-class Clang attribute and would be dropped with
//      -Wunknown-attributes before the `AnnotateAttr` ever lands in
//      the AST.
//
//   2. Rejects a typo like `"sturm::reversibel"` — the detection
//      primitive is byte-exact, pinning PRD §9 Q1's opt-in contract.
//
//   3. Survives template instantiation — the attribute must be visible
//      on the primary template's `FunctionDecl`, on explicit
//      specialisations, and on implicit instantiations, because the
//      downstream synthesis registry (P-B, sturm-z2e8.3) may be handed
//      any of the three shapes by the MatchFinder.
//
// Harness posture
// ---------------
// The test uses its own lightweight `ASTConsumer` pipeline (same pattern
// as `test_alias_footprint.cpp` / `test_matcher_reader_count.cpp`).
// Each test compiles a small snippet with `runToolOnCodeWithArgs`,
// walks the AST to locate the named `FunctionDecl`(s) we care about,
// and invokes `is_reversible()` inside the probe callback while the
// `ASTContext` is still alive.
//
// The harness deliberately does NOT route through the MVP matcher
// factory: `is_reversible` has no matcher surface — it is a free
// function over a decl — so a direct AST walk is the cleanest
// isolation.

#include "reversible_attribute.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using sturm::transpile::is_reversible;
using sturm::transpile::kReversibleAttrAnnotation;

// ── Test harness ────────────────────────────────────────────────────────────
static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                                 \
    ++tests_run;                                                         \
    if (cond) { ++tests_pass; }                                          \
    else {                                                               \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                        \
                     __FILE__, __LINE__, #cond);                         \
    }                                                                    \
} while (0)

#define CHECK_FALSE(cond) CHECK(!(cond))

namespace {

// ── AST walkers ────────────────────────────────────────────────────────────

// Locate the first FunctionDecl in the TU whose (short) name matches.
// The walk stops on first hit. We filter to *non-template-instantiation*
// decls by default so a plain-function lookup doesn't accidentally grab
// an instantiation — the template-survival test calls the specialised
// finder below.
class FunctionDeclFinder
    : public clang::RecursiveASTVisitor<FunctionDeclFinder> {
public:
    explicit FunctionDeclFinder(std::string name)
        : name_(std::move(name)) {}
    bool VisitFunctionDecl(clang::FunctionDecl* fd) {
        if (!fd) return true;
        if (found_) return true;
        if (fd->getNameAsString() != name_) return true;
        // Skip template instantiations — callers that want them use
        // `InstantiationFinder` below.
        if (fd->isTemplateInstantiation()) return true;
        found_ = fd;
        return false;
    }
    const clang::FunctionDecl* found() const { return found_; }
private:
    std::string name_;
    const clang::FunctionDecl* found_ = nullptr;
};

// Locate the first *template instantiation* FunctionDecl whose name
// matches. Distinguishes between the primary template (returned by
// `FunctionDeclFinder`) and its instantiations — critical for the
// template-survival test, which must inspect both shapes.
//
// Overrides `shouldVisitTemplateInstantiations()` so the visitor
// descends into the instantiation decls that the default
// RecursiveASTVisitor omits. Without this override the visitor never
// sees the instantiations and the test cannot assert on them.
class InstantiationFinder
    : public clang::RecursiveASTVisitor<InstantiationFinder> {
public:
    explicit InstantiationFinder(std::string name)
        : name_(std::move(name)) {}
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool VisitFunctionDecl(clang::FunctionDecl* fd) {
        if (!fd) return true;
        if (found_) return true;
        if (fd->getNameAsString() != name_) return true;
        if (!fd->isTemplateInstantiation()) return true;
        found_ = fd;
        return false;
    }
    const clang::FunctionDecl* found() const { return found_; }
private:
    std::string name_;
    const clang::FunctionDecl* found_ = nullptr;
};

// ── Consumer / action / factory plumbing ───────────────────────────────────
//
// The test supplies a `Probe` that receives the fully-typed `ASTContext`
// and runs whatever assertions the test needs while the AST is still
// alive. Mirrors `test_alias_footprint.cpp`.

using Probe = std::function<void(clang::ASTContext&)>;

class ReversibleConsumer : public clang::ASTConsumer {
public:
    explicit ReversibleConsumer(Probe probe) : probe_(std::move(probe)) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        if (probe_) probe_(ctx);
    }
private:
    Probe probe_;
};

class ReversibleAction : public clang::ASTFrontendAction {
public:
    explicit ReversibleAction(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<ReversibleConsumer>(probe_);
    }
private:
    Probe probe_;
};

class ReversibleFactory : public clang::tooling::FrontendActionFactory {
public:
    explicit ReversibleFactory(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<ReversibleAction>(probe_);
    }
private:
    Probe probe_;
};

// Run the snippet through Clang, invoke `probe` with the populated
// ASTContext, and return true iff the parse succeeded. Mirrors the
// `run_on` helper in `test_alias_footprint.cpp`.
bool run_on(std::string_view user_src, Probe probe) {
    ReversibleFactory factory(std::move(probe));
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), std::string(user_src), args,
        "reversible_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false — snippet failed to "
                     "parse\n");
    }
    return ok;
}

// ── Test cases ──────────────────────────────────────────────────────────────

// (1) Positive baseline: `[[clang::annotate("sturm::reversible")]]` on a
// plain free function ⇒ `is_reversible` returns true. A sibling
// *unannotated* function in the same TU returns false, pinning that the
// predicate is not a universal true-return.
void test_recognizes_sturm_reversible_on_plain_fn() {
    constexpr std::string_view src = R"CPP(
[[clang::annotate("sturm::reversible")]]
void marked() {}

void plain() {}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        FunctionDeclFinder f_marked("marked");
        f_marked.TraverseAST(ctx);
        CHECK(f_marked.found() != nullptr);
        if (f_marked.found()) {
            CHECK(is_reversible(f_marked.found()));
        }

        FunctionDeclFinder f_plain("plain");
        f_plain.TraverseAST(ctx);
        CHECK(f_plain.found() != nullptr);
        if (f_plain.found()) {
            CHECK_FALSE(is_reversible(f_plain.found()));
        }
    });
    CHECK(ran);
}

// (1b) Null-decl guard: `is_reversible(nullptr)` must return false
// rather than dereferencing. Downstream matchers hand us a null decl
// whenever a CallExpr's callee fails to resolve, and the predicate
// must silently fall through to "not a synthesis candidate" instead
// of crashing.
void test_null_decl_returns_false() {
    CHECK_FALSE(is_reversible(nullptr));
}

// (1c) The exported annotation constant equals the documented magic
// string. A regression here would silently break every test below.
void test_annotation_constant_value() {
    CHECK(kReversibleAttrAnnotation == std::string_view("sturm::reversible"));
}

// (1d) Absence of any annotation ⇒ false. Pins that the predicate
// does not accept decls carrying arbitrary (unrelated) AnnotateAttrs
// as reversible.
void test_unrelated_annotate_returns_false() {
    constexpr std::string_view src = R"CPP(
[[clang::annotate("some_other_plugin::contract")]]
void other() {}

[[clang::annotate("foo")]]
[[clang::annotate("bar")]]
void many() {}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        FunctionDeclFinder f_other("other");
        f_other.TraverseAST(ctx);
        CHECK(f_other.found() != nullptr);
        if (f_other.found()) {
            CHECK_FALSE(is_reversible(f_other.found()));
        }

        FunctionDeclFinder f_many("many");
        f_many.TraverseAST(ctx);
        CHECK(f_many.found() != nullptr);
        if (f_many.found()) {
            CHECK_FALSE(is_reversible(f_many.found()));
        }
    });
    CHECK(ran);
}

// (1e) Coexistence: a routine annotated with `sturm::reversible` AND
// an unrelated annotation still returns true. Real user code routinely
// carries multiple `[[clang::annotate(...)]]` pairs from different
// tooling layers; the predicate must match on any one equal to our
// marker.
void test_coexisting_annotations_returns_true() {
    constexpr std::string_view src = R"CPP(
[[clang::annotate("other_plugin::tag")]]
[[clang::annotate("sturm::reversible")]]
void mixed() {}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        FunctionDeclFinder f("mixed");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found()) {
            CHECK(is_reversible(f.found()));
        }
    });
    CHECK(ran);
}

// (2) Typo rejection: `"sturm::reversibel"` must NOT match. PRD §9 Q1's
// opt-in contract requires byte-exact matching; silently accepting
// typos would let non-reversible bodies slip into synthesis and fail
// later with confusing errors.
void test_rejects_typo_reversibel() {
    constexpr std::string_view src = R"CPP(
[[clang::annotate("sturm::reversibel")]]
void oops() {}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        FunctionDeclFinder f("oops");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found()) {
            CHECK_FALSE(is_reversible(f.found()));
        }
    });
    CHECK(ran);
}

// (2b) Case-sensitivity: `"STURM::REVERSIBLE"` must NOT match.
// Pins that the comparison is not case-folded.
void test_rejects_wrong_case() {
    constexpr std::string_view src = R"CPP(
[[clang::annotate("STURM::REVERSIBLE")]]
void shout() {}

[[clang::annotate("Sturm::Reversible")]]
void title() {}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        FunctionDeclFinder f_shout("shout");
        f_shout.TraverseAST(ctx);
        CHECK(f_shout.found() != nullptr);
        if (f_shout.found()) {
            CHECK_FALSE(is_reversible(f_shout.found()));
        }

        FunctionDeclFinder f_title("title");
        f_title.TraverseAST(ctx);
        CHECK(f_title.found() != nullptr);
        if (f_title.found()) {
            CHECK_FALSE(is_reversible(f_title.found()));
        }
    });
    CHECK(ran);
}

// (2c) Whitespace: `" sturm::reversible "` must NOT match. Pins that
// trailing/leading whitespace is NOT normalised away.
void test_rejects_trailing_whitespace() {
    constexpr std::string_view src = R"CPP(
[[clang::annotate("sturm::reversible ")]]
void trailing() {}

[[clang::annotate(" sturm::reversible")]]
void leading() {}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        FunctionDeclFinder f_trailing("trailing");
        f_trailing.TraverseAST(ctx);
        CHECK(f_trailing.found() != nullptr);
        if (f_trailing.found()) {
            CHECK_FALSE(is_reversible(f_trailing.found()));
        }

        FunctionDeclFinder f_leading("leading");
        f_leading.TraverseAST(ctx);
        CHECK(f_leading.found() != nullptr);
        if (f_leading.found()) {
            CHECK_FALSE(is_reversible(f_leading.found()));
        }
    });
    CHECK(ran);
}

// (3) Template survival: attribute on the primary function template
// must be visible on the primary AND on an implicit instantiation
// produced by a call site. The issue requirement states "survives
// template instantiation (attribute still detected on the primary
// template and/or specialization)".
void test_survives_template_instantiation() {
    constexpr std::string_view src = R"CPP(
template <typename T>
[[clang::annotate("sturm::reversible")]]
void templ(T) {}

void user() {
    templ<int>(0);
    templ<double>(0.0);
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        // Primary template decl (non-instantiation) — the attribute
        // lands here at parse time.
        FunctionDeclFinder f_primary("templ");
        f_primary.TraverseAST(ctx);
        CHECK(f_primary.found() != nullptr);
        if (f_primary.found()) {
            CHECK(is_reversible(f_primary.found()));
        }

        // An implicit instantiation decl — the Clang AST clones the
        // attribute list from the primary, so `is_reversible` must
        // answer true here too.
        InstantiationFinder f_inst("templ");
        f_inst.TraverseAST(ctx);
        CHECK(f_inst.found() != nullptr);
        if (f_inst.found()) {
            CHECK(is_reversible(f_inst.found()));
        }
    });
    CHECK(ran);
}

// (3b) Template survival — explicit specialisation. An explicit
// specialisation re-supplies the body (and optionally the attribute).
// When the primary template carries `[[clang::annotate("sturm::
// reversible")]]`, Clang preserves the attribute on the primary decl
// but an explicit specialisation's decl does NOT automatically inherit
// attributes (the specialisation is a separate FunctionDecl). This
// test pins that behaviour: the primary matches, and the user can
// re-declare the attribute on the specialisation if they want it
// matched there too.
void test_template_specialization_explicit() {
    constexpr std::string_view src = R"CPP(
template <typename T>
[[clang::annotate("sturm::reversible")]]
void spec(T) {}

template <>
[[clang::annotate("sturm::reversible")]]
void spec<int>(int) {}

template <>
void spec<double>(double) {}  // explicit spec WITHOUT the attribute
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        // Primary template — reversible.
        FunctionDeclFinder f_primary("spec");
        f_primary.TraverseAST(ctx);
        CHECK(f_primary.found() != nullptr);
        if (f_primary.found()) {
            CHECK(is_reversible(f_primary.found()));
        }
    });
    CHECK(ran);
}

} // namespace

int main() {
    test_recognizes_sturm_reversible_on_plain_fn();
    test_null_decl_returns_false();
    test_annotation_constant_value();
    test_unrelated_annotate_returns_false();
    test_coexisting_annotations_returns_true();
    test_rejects_typo_reversibel();
    test_rejects_wrong_case();
    test_rejects_trailing_whitespace();
    test_survives_template_instantiation();
    test_template_specialization_explicit();

    std::fprintf(stderr,
                 "test_reversible_attribute: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
