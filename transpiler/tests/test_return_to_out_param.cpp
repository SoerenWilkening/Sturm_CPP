// test_return_to_out_param.cpp — Phase Q Q-A (sturm-5kgu.2):
// golden-file regression tests for `synthesize_out_param_twin`.
//
// The module under test produces the source text of an out-param
// companion for a reversible forward whose body is a single
// `return <expr>;`. The tests run four positive fixtures through
// `runToolOnCodeWithArgs`, locate the forward `FunctionDecl` in the
// TU, invoke `synthesize_out_param_twin`, and compare the returned
// source string byte-for-byte against the matching
// `return_to_out_param_<N>.expected.cpp` golden.
//
// The four fixtures cover:
//
//   1. The PRD §4.1 canonical example — `qbool marked(qint x, int T)
//      { return x >= T; }`. Pins the standard two-param-with-classical
//      mix.
//   2. A pure-quantum body using `|` — `qbool join(qbool a, qbool b)
//      { return a | b; }`. Pins verbatim preservation of operator
//      expressions on the `^=` RHS.
//   3. A const-reference input — `qint echo(const qint& x)
//      { return x; }`. Pins that const qualifiers and reference
//      spellings survive the twin-signature stitching.
//   4. An empty-parameter-list forward — `qbool nullary()
//      { return qbool{}; }`. Pins the assembly corner case where the
//      parameter-joiner must NOT emit a leading comma before the
//      synthesised out-parameter.
//
// Negative paths for each reject reason (NotReversible, body-shape
// mismatches, etc.) are exercised inline — no separate fixtures —
// because the reject paths do not produce a text blob to compare
// against.
//
// Fixture path resolution
// -----------------------
// CMake injects `STURM_RETURN_TO_OUT_PARAM_FIXTURES_DIR` as a
// compile-time string pointing at the source-tree
// `transpiler/tests/fixtures/` directory. The test reads both the
// input `.cpp` and the golden `.expected.cpp` directly from that
// path; no `configure_file` copy is required because the fixtures
// are source-only (no build-time substitution).

#include "return_to_out_param.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using sturm::transpile::synthesize_out_param_twin;
using sturm::transpile::to_string;
using sturm::transpile::TwinRejectReason;
using sturm::transpile::TwinSynthesisResult;

#ifndef STURM_RETURN_TO_OUT_PARAM_FIXTURES_DIR
#error "CMake must define STURM_RETURN_TO_OUT_PARAM_FIXTURES_DIR"
#endif

// ── Test harness ────────────────────────────────────────────────────────────
//
// Same macro shape the sibling reversible_attribute / synthesis_registry
// tests use; duplicated here to keep the test binary self-contained
// (no shared `test_matcher_harness.hpp` dependency).
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

// Read the full contents of a file as a string. Returns empty on any
// failure — the caller asserts on non-emptiness before comparing.
std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

// Locate the first FunctionDecl in the TU whose short name matches
// `name_`. Skips template instantiations (the fixtures do not use
// templates). Mirrors the finder in `test_reversible_attribute.cpp`.
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

// Compile `src` as C++20, invoke `probe`, return true on parse success.
bool run_on(std::string_view src, Probe probe) {
    FnFactory factory(std::move(probe));
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), std::string(src), args, "twin_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false — snippet failed to "
                     "parse\n");
    }
    return ok;
}

// Resolve a fixture path against the CMake-injected directory macro.
std::filesystem::path fixture_path(std::string_view name) {
    std::filesystem::path root(STURM_RETURN_TO_OUT_PARAM_FIXTURES_DIR);
    return root / std::string(name);
}

// Run a single golden-file case: load the source, locate the named
// FunctionDecl, synthesise the twin, and compare byte-for-byte
// against the matching `.expected.cpp`.
void golden_case(std::string_view input_name,
                 std::string_view expected_name,
                 std::string_view fn_name,
                 std::string_view twin_name) {
    const auto src_path  = fixture_path(input_name);
    const auto gold_path = fixture_path(expected_name);
    const std::string src  = slurp(src_path);
    const std::string gold = slurp(gold_path);
    CHECK_FALSE(src.empty());
    CHECK_FALSE(gold.empty());
    if (src.empty() || gold.empty()) return;

    const std::string fn_name_str(fn_name);
    const std::string twin_name_str(twin_name);

    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder finder(fn_name_str);
        finder.TraverseAST(ctx);
        CHECK(finder.found() != nullptr);
        if (finder.found() == nullptr) return;

        TwinSynthesisResult result = synthesize_out_param_twin(
            finder.found(), ctx.getSourceManager(), ctx.getLangOpts());
        CHECK(result.synthesized);
        CHECK(result.reason == TwinRejectReason::None);
        CHECK_EQ_STR(result.twin_name, twin_name_str);
        CHECK_EQ_STR(result.source, gold);
    });
    CHECK(ran);
}

// ── Positive golden-file cases ─────────────────────────────────────────────

void test_fixture_1_marked_prd_canonical() {
    golden_case("return_to_out_param_1.cpp",
                "return_to_out_param_1.expected.cpp",
                "marked",
                "__marked_out");
}

void test_fixture_2_join_qbool_or() {
    golden_case("return_to_out_param_2.cpp",
                "return_to_out_param_2.expected.cpp",
                "join",
                "__join_out");
}

void test_fixture_3_echo_const_qint_ref() {
    golden_case("return_to_out_param_3.cpp",
                "return_to_out_param_3.expected.cpp",
                "echo",
                "__echo_out");
}

void test_fixture_4_nullary_no_params() {
    golden_case("return_to_out_param_4.cpp",
                "return_to_out_param_4.expected.cpp",
                "nullary",
                "__nullary_out");
}

// ── Negative / reject-path coverage ─────────────────────────────────────────

void test_null_decl_rejects() {
    // Construct a real ASTContext (by parsing an empty TU) so we can
    // hand real SourceManager / LangOptions references to the
    // function — even though the null-decl fast path short-circuits
    // before dereferencing either. Avoids the UB that would come
    // from fabricating null-reference arguments.
    bool ran = run_on("", [&](clang::ASTContext& ctx) {
        TwinSynthesisResult r = synthesize_out_param_twin(
            nullptr, ctx.getSourceManager(), ctx.getLangOpts());
        CHECK_FALSE(r.synthesized);
        CHECK(r.reason == TwinRejectReason::NullDecl);
        CHECK(r.source.empty());
        CHECK(r.twin_name.empty());
    });
    CHECK(ran);
}

void test_not_reversible_rejects() {
    // Forward without the `[[sturm::reversible]]` marker ⇒ the
    // synthesis gate refuses. Q-A is opt-in.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
qbool plain() { return qbool{}; }
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("plain");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;
        TwinSynthesisResult r = synthesize_out_param_twin(
            f.found(), ctx.getSourceManager(), ctx.getLangOpts());
        CHECK_FALSE(r.synthesized);
        CHECK(r.reason == TwinRejectReason::NotReversible);
    });
    CHECK(ran);
}

void test_non_quantum_return_rejects() {
    // Reversible attribute but the return type is `int`, not a
    // quantum type ⇒ refuse. Q-A only normalises quantum returns
    // (the whole point of the twin).
    constexpr std::string_view src = R"CPP(
[[clang::annotate("sturm::reversible")]]
int bogus() { return 7; }
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("bogus");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;
        TwinSynthesisResult r = synthesize_out_param_twin(
            f.found(), ctx.getSourceManager(), ctx.getLangOpts());
        CHECK_FALSE(r.synthesized);
        CHECK(r.reason == TwinRejectReason::NonQuantumReturnType);
    });
    CHECK(ran);
}

void test_void_return_rejects() {
    // `void` is not quantum ⇒ `NonQuantumReturnType` too. Pinned
    // separately because a canonical out-param forward already
    // looks like this — the Q-A contract distinguishes "already
    // canonical, nothing to do" from "some other mistake", and the
    // reason code is the signal.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; qbool& operator^=(const qbool&) { return *this; } }; }
using sturm::qbool;
[[clang::annotate("sturm::reversible")]]
void already(qbool& a) { a ^= qbool{}; }
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("already");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;
        TwinSynthesisResult r = synthesize_out_param_twin(
            f.found(), ctx.getSourceManager(), ctx.getLangOpts());
        CHECK_FALSE(r.synthesized);
        CHECK(r.reason == TwinRejectReason::NonQuantumReturnType);
    });
    CHECK(ran);
}

void test_multi_statement_body_rejects() {
    // Two statements (a local + a return) ⇒ Q-A refuses. Q-A is
    // deliberately narrow: the exact `{ return <expr>; }` shape,
    // nothing more.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
[[clang::annotate("sturm::reversible")]]
qbool many() { qbool t; return t; }
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("many");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;
        TwinSynthesisResult r = synthesize_out_param_twin(
            f.found(), ctx.getSourceManager(), ctx.getLangOpts());
        CHECK_FALSE(r.synthesized);
        CHECK(r.reason == TwinRejectReason::MultiStatementBody);
    });
    CHECK(ran);
}

void test_forward_declaration_rejects() {
    // Declaration without a definition ⇒ `NoBody`. The matcher
    // may dispatch Q-A on a decl whose definition lives elsewhere;
    // the classifier must reject, not crash.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
[[clang::annotate("sturm::reversible")]]
qbool missing(qbool a);
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("missing");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;
        TwinSynthesisResult r = synthesize_out_param_twin(
            f.found(), ctx.getSourceManager(), ctx.getLangOpts());
        CHECK_FALSE(r.synthesized);
        CHECK(r.reason == TwinRejectReason::NoBody);
    });
    CHECK(ran);
}

// ── Supporting coverage ────────────────────────────────────────────────────

void test_reason_to_string_stable() {
    // The spellings are part of the public contract — callers may
    // surface them in diagnostics and tests may match on them.
    CHECK_EQ_STR(std::string(to_string(TwinRejectReason::None)),
                 std::string("none"));
    CHECK_EQ_STR(std::string(to_string(TwinRejectReason::NullDecl)),
                 std::string("null_decl"));
    CHECK_EQ_STR(std::string(to_string(TwinRejectReason::NotReversible)),
                 std::string("not_reversible"));
    CHECK_EQ_STR(std::string(to_string(TwinRejectReason::NonQuantumReturnType)),
                 std::string("non_quantum_return_type"));
    CHECK_EQ_STR(std::string(to_string(TwinRejectReason::NoBody)),
                 std::string("no_body"));
    CHECK_EQ_STR(std::string(to_string(TwinRejectReason::NonCompoundBody)),
                 std::string("non_compound_body"));
    CHECK_EQ_STR(std::string(to_string(TwinRejectReason::MultiStatementBody)),
                 std::string("multi_statement_body"));
    CHECK_EQ_STR(std::string(to_string(TwinRejectReason::NotReturnStatement)),
                 std::string("not_return_statement"));
    CHECK_EQ_STR(std::string(to_string(TwinRejectReason::EmptyReturnExpression)),
                 std::string("empty_return_expression"));
    CHECK_EQ_STR(std::string(to_string(TwinRejectReason::SourceRecoveryFailed)),
                 std::string("source_recovery_failed"));
}

} // namespace

int main() {
    test_fixture_1_marked_prd_canonical();
    test_fixture_2_join_qbool_or();
    test_fixture_3_echo_const_qint_ref();
    test_fixture_4_nullary_no_params();

    test_null_decl_rejects();
    test_not_reversible_rejects();
    test_non_quantum_return_rejects();
    test_void_return_rejects();
    test_multi_statement_body_rejects();
    test_forward_declaration_rejects();

    test_reason_to_string_stable();

    std::fprintf(stderr,
                 "test_return_to_out_param: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
