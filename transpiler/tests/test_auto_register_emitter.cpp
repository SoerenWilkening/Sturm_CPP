// test_auto_register_emitter.cpp — Phase R R-B (sturm-88d7.3) unit
// tests for `emit_auto_registration` + `emit_auto_registration_for_decl`.
//
// The module under test produces a TU-scope
// `STURM_REGISTER_ADJOINT(<fwd>, <adj>);` line for each synthesized
// adjoint (consumed from the SynthesisRegistry `adjoint_name` field).
// The emitted text MUST match the AST shape PI-1's matcher already
// consumes — a `STURM_REGISTER_ADJOINT` macro invocation at TU scope
// whose expansion is a full specialization of
// `::sturm::_detail::adjoint_of<decltype(&::fn)>` with a static
// `value = &::adj;` member. Byte-for-byte golden comparison is the
// primary correctness gate.
//
// Three properties anchor the test:
//
//   1. String API shape. `emit_auto_registration("fwd", "__fwd_adj")`
//      MUST produce exactly `"STURM_REGISTER_ADJOINT(fwd, __fwd_adj);\n"`
//      — no leading indent, trailing newline, trailing semicolon
//      (valid empty declaration at TU scope).
//
//   2. FD-based reject paths. `emit_auto_registration_for_decl` must
//      refuse nullptr, a decl without `[[sturm::reversible]]`, and an
//      empty adjoint_name. Mirrors R-A's (sibling) reject-without-
//      diagnostic contract.
//
//   3. Registry-entry passthrough. When R-A produced an adjoint text
//      and attached `__marked_adj` to a SynthesisEntry, feeding the
//      entry's forward + adjoint_name through R-B produces the line
//      that PI-1's matcher will pick up on the second pass.
//
// Harness posture
// ---------------
// The string API is exercised directly (no LibTooling). The FD-based
// API is exercised via `runToolOnCodeWithArgs` on small snippets, the
// same pattern `test_adjoint_emitter` / `test_return_to_out_param`
// use. No fixture files are needed — the goldens are short one-line
// macro invocations and the snippets hand-build the forward decls
// inline.

#include "auto_register_emitter.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using sturm::transpile::AutoRegisterRejectReason;
using sturm::transpile::AutoRegisterResult;
using sturm::transpile::emit_auto_registration;
using sturm::transpile::emit_auto_registration_for_decl;
using sturm::transpile::to_string;

// ── Test harness ────────────────────────────────────────────────────────────
//
// Local CHECK / CHECK_EQ_STR macros mirror the shape every sibling
// transpiler test uses. Kept local here so the binary stays self-
// contained (no shared harness counter coupling).
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

#define CHECK_EQ_STR(got, want) do {                                     \
    ++tests_run;                                                         \
    if ((got) == (want)) { ++tests_pass; }                               \
    else {                                                               \
        std::fprintf(stderr, "FAIL  %s:%d  strings differ\n"             \
                             "  got:  <<<%s>>>\n"                        \
                             "  want: <<<%s>>>\n",                       \
                     __FILE__, __LINE__,                                 \
                     std::string(got).c_str(),                           \
                     std::string(want).c_str());                         \
    }                                                                    \
} while (0)

namespace {

// ── (1) Pure-string API — golden-file comparisons ───────────────────────────

void test_string_api_canonical_pair() {
    // Primary golden: the PRD §4.1 canonical example's registration.
    // `marked` forward synthesised into `__marked_adj`; R-B produces
    // the TU-scope registration line.
    const std::string got =
        emit_auto_registration("marked", "__marked_adj");
    const std::string want =
        "STURM_REGISTER_ADJOINT(marked, __marked_adj);\n";
    CHECK_EQ_STR(got, want);
}

void test_string_api_short_name() {
    // Single-letter forward — contract asserts no trimming or name
    // mangling; the forward's short name is dropped straight into
    // the macro argument.
    const std::string got = emit_auto_registration("f", "__f_adj");
    const std::string want = "STURM_REGISTER_ADJOINT(f, __f_adj);\n";
    CHECK_EQ_STR(got, want);
}

void test_string_api_multi_underscore_fn_name() {
    // Forward names containing underscores survive verbatim — the
    // macro expansion is textually spliced, and the adjoint's
    // sibling-name convention (`__fn_adj`) stitches around the
    // forward's underscores without ambiguity.
    const std::string got = emit_auto_registration(
        "my_oracle", "__my_oracle_adj");
    const std::string want =
        "STURM_REGISTER_ADJOINT(my_oracle, __my_oracle_adj);\n";
    CHECK_EQ_STR(got, want);
}

void test_string_api_qualified_forward() {
    // Namespaced forward: the STURM_REGISTER_ADJOINT macro already
    // accepts a qualified name (the runtime header documents
    // `STURM_REGISTER_ADJOINT(foo::bar, foo::bar_adj)`). R-B passes
    // whatever forward spelling the driver hands in — verbatim —
    // into the macro argument. The adjoint name stays as R-A
    // produced it (short, `__bar_adj`); R-C is responsible for
    // choosing how to qualify the forward when it calls R-B.
    const std::string got = emit_auto_registration(
        "ns::bar", "ns::__bar_adj");
    const std::string want =
        "STURM_REGISTER_ADJOINT(ns::bar, ns::__bar_adj);\n";
    CHECK_EQ_STR(got, want);
}

void test_string_api_empty_forward_is_noop() {
    // No forward identifier ⇒ nothing to register. Empty output is
    // the safe default — emitting `STURM_REGISTER_ADJOINT(, __adj);`
    // would be ill-formed. Mirrors R-A's empty-fn-name contract.
    const std::string got = emit_auto_registration("", "__f_adj");
    CHECK(got.empty());
}

void test_string_api_empty_adjoint_is_noop() {
    // Symmetric: no adjoint identifier ⇒ nothing to register.
    // R-A produced no adjoint (e.g. a validation failure) and the
    // driver must not emit a half-formed macro line.
    const std::string got = emit_auto_registration("fwd", "");
    CHECK(got.empty());
}

void test_string_api_both_empty_is_noop() {
    CHECK(emit_auto_registration("", "").empty());
}

// ── (2) Reason stringification ──────────────────────────────────────────────

void test_reason_to_string_stable() {
    // Spellings are public contract.
    CHECK_EQ_STR(
        std::string(to_string(AutoRegisterRejectReason::None)),
        std::string("none"));
    CHECK_EQ_STR(
        std::string(to_string(AutoRegisterRejectReason::NullDecl)),
        std::string("null_decl"));
    CHECK_EQ_STR(
        std::string(to_string(AutoRegisterRejectReason::NotReversible)),
        std::string("not_reversible"));
    CHECK_EQ_STR(
        std::string(to_string(AutoRegisterRejectReason::EmptyAdjointName)),
        std::string("empty_adjoint_name"));
}

// ── (3) FD-based entry point — integration via LibTooling ───────────────────

// Probe invoked by the consumer with a fully-populated ASTContext.
using Probe = std::function<void(clang::ASTContext&)>;

class FnConsumer : public clang::ASTConsumer {
public:
    explicit FnConsumer(Probe probe) : probe_(std::move(probe)) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        if (probe_) probe_(ctx);
    }
private:
    Probe probe_;
};

class FnAction : public clang::ASTFrontendAction {
public:
    explicit FnAction(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<FnConsumer>(probe_);
    }
private:
    Probe probe_;
};

class FnFactory : public clang::tooling::FrontendActionFactory {
public:
    explicit FnFactory(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<FnAction>(probe_);
    }
private:
    Probe probe_;
};

// Locate the first FunctionDecl in the TU whose short name matches
// `name_`. Skips template instantiations.
class NamedFnFinder
    : public clang::RecursiveASTVisitor<NamedFnFinder> {
public:
    explicit NamedFnFinder(std::string name) : name_(std::move(name)) {}
    bool VisitFunctionDecl(clang::FunctionDecl* fd) {
        if (found_) return true;
        if (fd == nullptr) return true;
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

// Compile `src` as C++20, invoke `probe`, return true on parse success.
bool run_on(std::string_view src, Probe probe) {
    FnFactory factory(std::move(probe));
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), std::string(src), args, "auto_register_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false — snippet failed to "
                     "parse\n");
    }
    return ok;
}

void test_for_decl_null_rejects() {
    // nullptr forward → NullDecl reject. Mirrors R-A's shape.
    AutoRegisterResult r = emit_auto_registration_for_decl(
        nullptr, "__f_adj");
    CHECK_FALSE(r.registered);
    CHECK(r.reason == AutoRegisterRejectReason::NullDecl);
    CHECK(r.source.empty());
}

void test_for_decl_not_reversible_rejects() {
    // Forward without the `[[sturm::reversible]]` marker → refuse.
    // R-B is opt-in by the same P9 contract R-A uses; we never
    // register a routine the user did not mark.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
void plain(qbool& a) {}
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        (void)ctx;
        NamedFnFinder f("plain");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;
        AutoRegisterResult r = emit_auto_registration_for_decl(
            f.found(), "__plain_adj");
        CHECK_FALSE(r.registered);
        CHECK(r.reason == AutoRegisterRejectReason::NotReversible);
        CHECK(r.source.empty());
    });
    CHECK(ran);
}

void test_for_decl_empty_adjoint_name_rejects() {
    // Reversible forward but adjoint_name blank → refuse. R-A never
    // produces an empty adjoint_name on success, but the driver may
    // pass the entry's `adjoint_name` field through verbatim even
    // when a prior stage left it empty (e.g. R-A rejected).
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
[[clang::annotate("sturm::reversible")]]
void marked(qbool& a) {}
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("marked");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;
        AutoRegisterResult r = emit_auto_registration_for_decl(
            f.found(), "");
        CHECK_FALSE(r.registered);
        CHECK(r.reason == AutoRegisterRejectReason::EmptyAdjointName);
        CHECK(r.source.empty());
    });
    CHECK(ran);
}

void test_for_decl_success() {
    // Golden-file regression: a reversible forward named `marked`
    // with R-A's adjoint_name `__marked_adj` produces the exact
    // TU-scope registration line R-C will inject into the rewritten
    // buffer.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
[[clang::annotate("sturm::reversible")]]
void marked(qbool& a) {}
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("marked");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;
        AutoRegisterResult r = emit_auto_registration_for_decl(
            f.found(), "__marked_adj");
        CHECK(r.registered);
        CHECK(r.reason == AutoRegisterRejectReason::None);
        const std::string want =
            "STURM_REGISTER_ADJOINT(marked, __marked_adj);\n";
        CHECK_EQ_STR(r.source, want);
    });
    CHECK(ran);
}

void test_for_decl_template_instantiation_rejects() {
    // PI-1's matcher keys on `STURM_REGISTER_ADJOINT(fn, adj)` which
    // takes a concrete function pointer `&::fn` — template
    // specialisation instantiations were never registry
    // candidates. If R-C ever hands us an instantiation, R-B
    // declines: the macro's `decltype(&::fn)` would not type-check
    // against an uninstantiated template-parameter-dependent
    // signature.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
template <typename T>
[[clang::annotate("sturm::reversible")]]
void tmpl(T& a) { (void)a; }
// Force instantiation so the visitor finds the instantiated FD.
void use() {
    qbool q;
    tmpl(q);
}
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        // The finder skips template instantiations by design; here
        // we drive a separate visitor that deliberately captures
        // the instantiated decl to exercise R-B's defence. The
        // visitor overrides `shouldVisitTemplateInstantiations` so
        // Clang includes implicit instantiations in the walk (the
        // default `RecursiveASTVisitor` only visits primary
        // templates).
        class Capture
            : public clang::RecursiveASTVisitor<Capture> {
        public:
            bool shouldVisitTemplateInstantiations() const {
                return true;
            }
            bool VisitFunctionDecl(clang::FunctionDecl* fd) {
                if (!fd) return true;
                if (fd->getNameAsString() != "tmpl") return true;
                if (fd->getTemplateSpecializationKind() ==
                    clang::TSK_Undeclared) {
                    // Primary template — we want the instantiated
                    // sibling, not this node.
                    return true;
                }
                inst = fd;
                return false;
            }
            const clang::FunctionDecl* inst = nullptr;
        } cap;
        cap.TraverseAST(ctx);
        CHECK(cap.inst != nullptr);
        if (cap.inst == nullptr) return;
        AutoRegisterResult r = emit_auto_registration_for_decl(
            cap.inst, "__tmpl_adj");
        CHECK_FALSE(r.registered);
        CHECK(r.reason == AutoRegisterRejectReason::TemplateInstantiation);
        CHECK(r.source.empty());
    });
    CHECK(ran);
}

// ── (4) End-to-end compilability: the emitted line parses cleanly ──────────

void test_emitted_line_parses_against_runtime_header() {
    // Structural sanity: the emitted `STURM_REGISTER_ADJOINT(fwd,
    // __fwd_adj);` line, when dropped after the macro definition
    // that matches the runtime header, must parse without diagnostic
    // and produce exactly the AST shape PI-1's matcher consumes
    // (`ClassTemplateSpecializationDecl` of
    // `::sturm::_detail::adjoint_of`). This test doesn't run the
    // PI-1 matcher — that's `test_routine_registry`'s job — it only
    // asserts the line we emit is well-formed C++ at TU scope.
    const std::string reg = emit_auto_registration(
        "marked", "__marked_adj");
    // Hand-simulated minimal preamble: definitions of the runtime
    // trait header and stub forward/adjoint functions. No inclusion
    // of sturm.hpp — keeps the test binary independent of the
    // runtime library's include search path.
    const std::string src =
        "namespace sturm {\n"
        "namespace _detail {\n"
        "template <typename FnPtr>\n"
        "struct adjoint_of;\n"
        "}\n"
        "}\n"
        "#define STURM_REGISTER_ADJOINT(fn, adj) \\\n"
        "    namespace sturm {                    \\\n"
        "    namespace _detail {                  \\\n"
        "    template <>                          \\\n"
        "    struct adjoint_of<decltype(&::fn)> { \\\n"
        "        static constexpr auto value = &::adj; \\\n"
        "    };                                   \\\n"
        "    }                                    \\\n"
        "    }\n"
        "void marked() {}\n"
        "void __marked_adj() {}\n"
        + reg;
    // We also check the line we emitted actually appears verbatim
    // in the assembled source — pinning the contract that R-B's
    // output is a drop-in TU-scope statement.
    const std::string want_line =
        "STURM_REGISTER_ADJOINT(marked, __marked_adj);\n";
    CHECK(src.find(want_line) != std::string::npos);
    bool ran = run_on(src, [&](clang::ASTContext&) {
        // If we got here, the snippet parsed. Nothing else to
        // verify — PI-1 matcher coverage lives in
        // `test_routine_registry`.
    });
    CHECK(ran);
}

} // namespace

int run_test_auto_register_emitter(int /*argc*/, char** /*argv*/) {
    // (1) Pure-string API golden comparisons.
    test_string_api_canonical_pair();
    test_string_api_short_name();
    test_string_api_multi_underscore_fn_name();
    test_string_api_qualified_forward();
    test_string_api_empty_forward_is_noop();
    test_string_api_empty_adjoint_is_noop();
    test_string_api_both_empty_is_noop();

    // (2) Reason stringification.
    test_reason_to_string_stable();

    // (3) FD-based entry-point coverage.
    test_for_decl_null_rejects();
    test_for_decl_not_reversible_rejects();
    test_for_decl_empty_adjoint_name_rejects();
    test_for_decl_success();
    test_for_decl_template_instantiation_rejects();

    // (4) End-to-end compilability.
    test_emitted_line_parses_against_runtime_header();

    std::fprintf(stderr,
                 "test_auto_register_emitter: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
