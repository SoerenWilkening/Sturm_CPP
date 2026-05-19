// test_qint_alias_subst_emitter.cpp -- sturm-65rs.9 (Beat C2) unit tests.
//
// Plan §10, PRD §4.3. Pins the C2 emitter that consumes the C1 matcher
// (sturm-65rs.8) and rewrites every `sturm::frontend::qint` type
// spelling to `sturm::qint_t<W>`. Five fixture pairs (one per anchor
// row of the §4.3 match-anchors table) plus pure-string emission
// coverage and the empty-hit no-op posture every emitter test in this
// project carries.
//
// Width inference contract (PRD §4.3, plan §10):
//   - VarDecl  → routes through `infer_width(VarDecl, InferContext)`;
//                rules 1/2 fall through on integer-literal initializers
//                (no qint_t<W> subscript on the RHS), so rule 3 wins
//                and the emitter renders `sturm::qint_t<32>`.
//   - ParmVarDecl / FieldDecl / FunctionDecl / FunctionalCast → rule
//                3 directly (per-Parm/Field width inference is OUT OF
//                SCOPE per follow-up sturm-65rs.17).
//
// Plan §14 normalisation: the golden comparison runs both buffers
// through a whitespace-collapsing normaliser (insulates against
// formatting churn).

#include "qint_alias_subst_emitter.hpp"
#include "matcher_qint_alias_subst.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

#ifndef STURM_QINT_ALIAS_SUBST_EMITTER_FIXTURES_DIR
#error "CMake must define STURM_QINT_ALIAS_SUBST_EMITTER_FIXTURES_DIR"
#endif

// ── Test harness ───────────────────────────────────────────────────────────
static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while (0)

#define CHECK_EQ_STR(got, want) do {                                  \
    ++tests_run;                                                      \
    if ((got) == (want)) { ++tests_pass; }                            \
    else {                                                            \
        std::fprintf(stderr,                                          \
            "FAIL  %s:%d  diff\n  got:  <<<%s>>>\n  want: <<<%s>>>\n",\
            __FILE__, __LINE__,                                       \
            std::string(got).c_str(), std::string(want).c_str());     \
    }                                                                 \
} while (0)

namespace {

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

std::filesystem::path fixture_path(std::string_view name) {
    std::filesystem::path root(STURM_QINT_ALIAS_SUBST_EMITTER_FIXTURES_DIR);
    return root / std::string(name);
}

// Plan §14 normalisation: collapse runs of whitespace to a single space.
std::string normalise(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool ls = true;
    for (char c : s) {
        const bool ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (ws) { if (!ls) out.push_back(' '); ls = true; }
        else    { out.push_back(c); ls = false; }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Tooling wiring: one Rewriter + ASTConsumer; EndSourceFileAction
// copies the rewritten main-file buffer into `*out`. Mirrors
// `test_qram_emitter.cpp`'s `QramConsumer` / `QramAction` plumbing.
using ProbeFn = std::function<void(clang::ASTContext&, clang::Rewriter&)>;

struct SubstConsumer : public clang::ASTConsumer {
    SubstConsumer(ProbeFn p, clang::Rewriter* r) : p_(std::move(p)), r_(r) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        r_->setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        if (p_) p_(ctx, *r_);
    }
    ProbeFn p_; clang::Rewriter* r_;
};

class SubstAction : public clang::ASTFrontendAction {
public:
    SubstAction(ProbeFn p, clang::Rewriter* r, std::string* o)
        : probe_(std::move(p)), rw_(r), out_(o) {}
    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<SubstConsumer>(probe_, rw_);
    }
    void EndSourceFileAction() override {
        const clang::SourceManager& sm = rw_->getSourceMgr();
        const clang::FileID main = sm.getMainFileID();
        const clang::RewriteBuffer* buf = rw_->getRewriteBufferFor(main);
        if (buf != nullptr) {
            llvm::raw_string_ostream os(*out_); buf->write(os); os.flush();
        } else {
            llvm::StringRef c = sm.getBufferData(main);
            out_->assign(c.data(), c.size());
        }
    }
private:
    ProbeFn probe_; clang::Rewriter* rw_; std::string* out_;
};

class SubstFactory : public clang::tooling::FrontendActionFactory {
public:
    SubstFactory(ProbeFn p, clang::Rewriter* r, std::string* o)
        : probe_(std::move(p)), rw_(r), out_(o) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<SubstAction>(probe_, rw_, out_);
    }
private:
    ProbeFn probe_; clang::Rewriter* rw_; std::string* out_;
};

// Run C1 + C2 against fixture src; returns the rewritten main-file.
std::string run_emitter_on_fixture(const std::string& src,
                                   const std::string& filename) {
    clang::Rewriter rw;
    std::string out;
    std::vector<QintAliasSubstMatch> matches;
    auto probe = [&matches](clang::ASTContext& ctx, clang::Rewriter& rw) {
        clang::ast_matchers::MatchFinder finder;
        register_qint_alias_subst_matcher(finder, matches);
        finder.matchAST(ctx);
        emit_qint_alias_subst_rewrites(rw, matches);
    };
    SubstFactory factory(std::move(probe), &rw, &out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), src, args, filename);
    if (!ok) std::fprintf(stderr, "FAIL  tool run on %s\n", filename.c_str());
    return out;
}

void golden_case(std::string_view input_name,
                 std::string_view expected_name) {
    const auto input_path = fixture_path(input_name);
    const auto gold_path  = fixture_path(expected_name);
    const std::string input = slurp(input_path);
    const std::string gold  = slurp(gold_path);
    CHECK(!input.empty());
    CHECK(!gold.empty());
    if (input.empty() || gold.empty()) return;
    const std::string out =
        run_emitter_on_fixture(input, std::string(input_name));
    CHECK(!out.empty());
    CHECK_EQ_STR(normalise(out), normalise(gold));
}

// ── Pure-string emission ───────────────────────────────────────────────────
// Pins the rendered shape independent of the AST plumbing — the
// VarDecl arm always renders `sturm::qint_t<W>`, and the emitter
// reuses `render_qint_typename.hpp` (sturm-65rs.7 / Beat C0) so this
// is a thin smoke check that no duplicate render path leaked in.
void test_text_var_decl_width_32() {
    CHECK_EQ_STR(emit_qint_alias_subst_text(32),
                 std::string("sturm::qint_t<32>"));
}

void test_text_var_decl_width_8() {
    CHECK_EQ_STR(emit_qint_alias_subst_text(8),
                 std::string("sturm::qint_t<8>"));
}

void test_text_var_decl_width_64() {
    CHECK_EQ_STR(emit_qint_alias_subst_text(64),
                 std::string("sturm::qint_t<64>"));
}

// ── Negative: empty match vector ⇒ no-op ───────────────────────────────────
void test_empty_matches_is_noop() {
    clang::Rewriter rw;
    std::string out;
    auto probe = [](clang::ASTContext&, clang::Rewriter& rw) {
        std::vector<QintAliasSubstMatch> empty_matches;
        emit_qint_alias_subst_rewrites(rw, empty_matches);
    };
    SubstFactory factory(std::move(probe), &rw, &out);
    const std::string trivial = "void demo() {}\n";
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    CHECK(clang::tooling::runToolOnCodeWithArgs(
        factory.create(), trivial, args, "noop_input.cpp"));
    CHECK_EQ_STR(out, trivial);
}

// ── Golden-file fixture pairs (PRD A4) ─────────────────────────────────────

// VarDecl: integer-literal initializer (rule 2 falls through, rule 3
// wins → 32) plus a no-init VarDecl (rule 3 directly → 32). Both
// rewrite to `sturm::qint_t<32>`.
void test_fixture_var_decl() {
    golden_case("qint_alias_subst_var.cpp",
                "qint_alias_subst_var.expected.cpp");
}

void test_fixture_parm_decl() {
    golden_case("qint_alias_subst_parm.cpp",
                "qint_alias_subst_parm.expected.cpp");
}

void test_fixture_field_decl() {
    golden_case("qint_alias_subst_field.cpp",
                "qint_alias_subst_field.expected.cpp");
}

void test_fixture_function_return() {
    golden_case("qint_alias_subst_ret.cpp",
                "qint_alias_subst_ret.expected.cpp");
}

void test_fixture_functional_cast() {
    golden_case("qint_alias_subst_cast.cpp",
                "qint_alias_subst_cast.expected.cpp");
}

// ── Wave-2 carrier round-trips (sturm-7t85.3 / G2) ─────────────────────────
//
// Plan §19c, PRD §9.3.2 / A7, A8. The G1 matcher (sturm-7t85.1) walks
// one level into `ArrayTypeLoc::getElementLoc()` /
// `PointerTypeLoc::getPointeeLoc()` so VarDecl, ParmVarDecl, and
// FieldDecl anchors carrying an array or pointer of
// `sturm::frontend::qint` capture the element / pointee TypeLoc range
// only. The emitter substitutes whatever range the matcher hands it,
// so the wave-1 emission path already covers these — these golden
// pairs pin the round-trip end-to-end (matcher → emitter → byte-equal
// to `.expected.cpp` after whitespace normalisation).
//
// `carray_typedef` pins R5: when the user names the array carrier
// through a TypeAliasDecl, the matcher's TypeLoc walk lands on a
// `TypedefTypeLoc` and returns an invalid `SourceRange`; the emitter
// then drops the match and the file round-trips byte-identical.

void test_fixture_carray() {
    golden_case("qint_alias_subst_carray.cpp",
                "qint_alias_subst_carray.expected.cpp");
}

void test_fixture_ptr() {
    golden_case("qint_alias_subst_ptr.cpp",
                "qint_alias_subst_ptr.expected.cpp");
}

void test_fixture_carray_typedef() {
    golden_case("qint_alias_subst_carray_typedef.cpp",
                "qint_alias_subst_carray_typedef.expected.cpp");
}

} // anonymous namespace

int run_test_qint_alias_subst_emitter(int /*argc*/, char** /*argv*/) {
    test_text_var_decl_width_32();
    test_text_var_decl_width_8();
    test_text_var_decl_width_64();
    test_empty_matches_is_noop();
    test_fixture_var_decl();
    test_fixture_parm_decl();
    test_fixture_field_decl();
    test_fixture_function_return();
    test_fixture_functional_cast();
    test_fixture_carray();
    test_fixture_ptr();
    test_fixture_carray_typedef();

    std::fprintf(stderr,
                 "test_qint_alias_subst_emitter: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
