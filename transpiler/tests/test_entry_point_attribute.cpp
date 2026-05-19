// test_entry_point_attribute.cpp — sturm-0tcv unit tests for
// `sturm::transpile::has_entry_point_attr()`.
//
// Mirrors test_reversible_attribute.cpp. The detection primitive is
// a free function over a FunctionDecl that returns true iff the decl
// carries `[[clang::annotate("sturm::entry_point")]]`. The tests
// pin:
//
//   (1) Positive baseline: `[[clang::annotate("sturm::entry_point")]]`
//       on a plain free function returns true; a sibling unannotated
//       function returns false.
//   (2) Null-decl guard: `has_entry_point_attr(nullptr) == false`.
//   (3) Exported annotation constant value matches the magic string.
//   (4) Unrelated annotations (other plugins, the `sturm::reversible`
//       marker) return false.
//   (5) Coexistence: a function carrying multiple AnnotateAttrs where
//       one is `sturm::entry_point` still returns true.
//   (6) Typo rejection: `"sturm::entry_pont"` and the like do NOT match.
//   (7) Case sensitivity: `"STURM::ENTRY_POINT"` does NOT match.
//   (8) Whitespace: trailing / leading whitespace does NOT match.
//   (9) Template survival: attribute on the primary template is
//       visible on the primary template's FunctionDecl AND on any
//       implicit instantiation produced by a call site.
//
// Harness posture mirrors test_reversible_attribute.cpp — direct
// ASTConsumer pipeline + AST walker for named FunctionDecls.

#include "entry_point_attribute.hpp"

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

using sturm::transpile::has_entry_point_attr;
using sturm::transpile::kEntryPointAttrAnnotation;

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

class FunctionDeclFinder
    : public clang::RecursiveASTVisitor<FunctionDeclFinder> {
public:
    explicit FunctionDeclFinder(std::string name)
        : name_(std::move(name)) {}
    bool VisitFunctionDecl(clang::FunctionDecl* fd) {
        if (!fd) return true;
        if (found_) return true;
        if (fd->getNameAsString() != name_) return true;
        if (fd->isTemplateInstantiation()) return true;
        found_ = fd;
        return false;
    }
    const clang::FunctionDecl* found() const { return found_; }
private:
    std::string name_;
    const clang::FunctionDecl* found_ = nullptr;
};

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

using Probe = std::function<void(clang::ASTContext&)>;

class EntryConsumer : public clang::ASTConsumer {
public:
    explicit EntryConsumer(Probe probe) : probe_(std::move(probe)) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        if (probe_) probe_(ctx);
    }
private:
    Probe probe_;
};

class EntryAction : public clang::ASTFrontendAction {
public:
    explicit EntryAction(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<EntryConsumer>(probe_);
    }
private:
    Probe probe_;
};

class EntryFactory : public clang::tooling::FrontendActionFactory {
public:
    explicit EntryFactory(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<EntryAction>(probe_);
    }
private:
    Probe probe_;
};

bool run_on(std::string_view user_src, Probe probe) {
    EntryFactory factory(std::move(probe));
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), std::string(user_src), args,
        "entry_point_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false — snippet failed "
                     "to parse\n");
    }
    return ok;
}

// ── Test cases ──────────────────────────────────────────────────────────────

// (1) Positive baseline.
void test_recognizes_on_plain_fn() {
    constexpr std::string_view src = R"CPP(
[[clang::annotate("sturm::entry_point")]]
void marked() {}

void plain() {}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        FunctionDeclFinder f_marked("marked");
        f_marked.TraverseAST(ctx);
        CHECK(f_marked.found() != nullptr);
        if (f_marked.found()) {
            CHECK(has_entry_point_attr(f_marked.found()));
        }

        FunctionDeclFinder f_plain("plain");
        f_plain.TraverseAST(ctx);
        CHECK(f_plain.found() != nullptr);
        if (f_plain.found()) {
            CHECK_FALSE(has_entry_point_attr(f_plain.found()));
        }
    });
    CHECK(ran);
}

// (2) Null guard.
void test_null_decl_returns_false() {
    CHECK_FALSE(has_entry_point_attr(nullptr));
}

// (3) Constant value pin.
void test_annotation_constant_value() {
    CHECK(kEntryPointAttrAnnotation ==
          std::string_view("sturm::entry_point"));
}

// (4) Unrelated annotations.
void test_unrelated_annotate_returns_false() {
    constexpr std::string_view src = R"CPP(
[[clang::annotate("some_other_plugin::contract")]]
void other() {}

[[clang::annotate("sturm::reversible")]]
void reversible_only() {}

[[clang::annotate("foo")]]
[[clang::annotate("bar")]]
void many() {}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        FunctionDeclFinder f_other("other");
        f_other.TraverseAST(ctx);
        CHECK(f_other.found() != nullptr);
        if (f_other.found()) {
            CHECK_FALSE(has_entry_point_attr(f_other.found()));
        }

        FunctionDeclFinder f_rev("reversible_only");
        f_rev.TraverseAST(ctx);
        CHECK(f_rev.found() != nullptr);
        if (f_rev.found()) {
            CHECK_FALSE(has_entry_point_attr(f_rev.found()));
        }

        FunctionDeclFinder f_many("many");
        f_many.TraverseAST(ctx);
        CHECK(f_many.found() != nullptr);
        if (f_many.found()) {
            CHECK_FALSE(has_entry_point_attr(f_many.found()));
        }
    });
    CHECK(ran);
}

// (5) Coexistence.
void test_coexisting_annotations_returns_true() {
    constexpr std::string_view src = R"CPP(
[[clang::annotate("other_plugin::tag")]]
[[clang::annotate("sturm::entry_point")]]
[[clang::annotate("sturm::reversible")]]
void mixed() {}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        FunctionDeclFinder f("mixed");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found()) {
            CHECK(has_entry_point_attr(f.found()));
        }
    });
    CHECK(ran);
}

// (6) Typo rejection.
void test_rejects_typo() {
    constexpr std::string_view src = R"CPP(
[[clang::annotate("sturm::entry_pont")]]
void oops() {}

[[clang::annotate("sturm::entrypoint")]]
void no_underscore() {}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        FunctionDeclFinder f1("oops");
        f1.TraverseAST(ctx);
        CHECK(f1.found() != nullptr);
        if (f1.found()) {
            CHECK_FALSE(has_entry_point_attr(f1.found()));
        }
        FunctionDeclFinder f2("no_underscore");
        f2.TraverseAST(ctx);
        CHECK(f2.found() != nullptr);
        if (f2.found()) {
            CHECK_FALSE(has_entry_point_attr(f2.found()));
        }
    });
    CHECK(ran);
}

// (7) Case-sensitivity.
void test_rejects_wrong_case() {
    constexpr std::string_view src = R"CPP(
[[clang::annotate("STURM::ENTRY_POINT")]]
void shout() {}

[[clang::annotate("Sturm::Entry_Point")]]
void title() {}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        FunctionDeclFinder f_shout("shout");
        f_shout.TraverseAST(ctx);
        if (f_shout.found()) {
            CHECK_FALSE(has_entry_point_attr(f_shout.found()));
        }

        FunctionDeclFinder f_title("title");
        f_title.TraverseAST(ctx);
        if (f_title.found()) {
            CHECK_FALSE(has_entry_point_attr(f_title.found()));
        }
    });
    CHECK(ran);
}

// (8) Whitespace.
void test_rejects_trailing_whitespace() {
    constexpr std::string_view src = R"CPP(
[[clang::annotate("sturm::entry_point ")]]
void trailing() {}

[[clang::annotate(" sturm::entry_point")]]
void leading() {}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        FunctionDeclFinder f_trailing("trailing");
        f_trailing.TraverseAST(ctx);
        if (f_trailing.found()) {
            CHECK_FALSE(has_entry_point_attr(f_trailing.found()));
        }

        FunctionDeclFinder f_leading("leading");
        f_leading.TraverseAST(ctx);
        if (f_leading.found()) {
            CHECK_FALSE(has_entry_point_attr(f_leading.found()));
        }
    });
    CHECK(ran);
}

// (9) Template survival.
void test_survives_template_instantiation() {
    constexpr std::string_view src = R"CPP(
template <typename T>
[[clang::annotate("sturm::entry_point")]]
void templ(T) {}

void user() {
    templ<int>(0);
    templ<double>(0.0);
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        FunctionDeclFinder f_primary("templ");
        f_primary.TraverseAST(ctx);
        CHECK(f_primary.found() != nullptr);
        if (f_primary.found()) {
            CHECK(has_entry_point_attr(f_primary.found()));
        }

        InstantiationFinder f_inst("templ");
        f_inst.TraverseAST(ctx);
        CHECK(f_inst.found() != nullptr);
        if (f_inst.found()) {
            CHECK(has_entry_point_attr(f_inst.found()));
        }
    });
    CHECK(ran);
}

} // namespace

int run_test_entry_point_attribute(int /*argc*/, char** /*argv*/) {
    test_recognizes_on_plain_fn();
    test_null_decl_returns_false();
    test_annotation_constant_value();
    test_unrelated_annotate_returns_false();
    test_coexisting_annotations_returns_true();
    test_rejects_typo();
    test_rejects_wrong_case();
    test_rejects_trailing_whitespace();
    test_survives_template_instantiation();

    std::fprintf(stderr,
                 "test_entry_point_attribute: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
