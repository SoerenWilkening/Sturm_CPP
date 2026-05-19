// test_consumer_qint_alias_subst.cpp — sturm-65rs.10 (Beat C3) tests.
// Plan §11, PRD §4.3 / R2. Two invariants the C3 wiring owes:
//   1. Overlap round-trip — `qint b = a[i];` rewritten ONLY by QRAM
//      and `frontend::qint x;` rewritten ONLY by alias-subst. Golden
//      pinned by `qint_alias_subst_overlap.expected.cpp`.
//   2. PRD R2 no-overlap — same VarDecl* in BOTH matcher outputs; the
//      consumer guard MUST drop the alias-subst rewrite for that vd.
// LoC budget: <= 200 (plan §1, §11 / C3).

#include "matcher_qint_alias_subst.hpp"
#include "matcher_qram_subscript.hpp"
#include "qint_alias_subst_emitter.hpp"
#include "qram_emitter.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
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
#include <unordered_set>
#include <vector>

namespace sturm_test_consumer_qint_alias_subst_ns {

using namespace sturm::transpile;

#ifndef STURM_CONSUMER_QINT_ALIAS_SUBST_FIXTURES_DIR
#error "CMake must define STURM_CONSUMER_QINT_ALIAS_SUBST_FIXTURES_DIR"
#endif

static int tests_run = 0, tests_pass = 0;

#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; }    \
    else { std::fprintf(stderr, "FAIL  %s:%d  %s\n",                 \
                        __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ_STR(got, want) do { ++tests_run;                    \
    if ((got) == (want)) { ++tests_pass; }                           \
    else { std::fprintf(stderr,                                      \
        "FAIL  %s:%d  diff\n  got:  <<<%s>>>\n  want: <<<%s>>>\n",   \
        __FILE__, __LINE__,                                          \
        std::string(got).c_str(), std::string(want).c_str()); }      \
} while (0)

namespace {

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary); if (!in) return {};
    std::ostringstream oss; oss << in.rdbuf(); return oss.str();
}
std::filesystem::path fpath(std::string_view name) {
    return std::filesystem::path(STURM_CONSUMER_QINT_ALIAS_SUBST_FIXTURES_DIR)
         / std::string(name);
}
// Plan §14: collapse whitespace runs to one space.
std::string normalise(std::string_view s) {
    std::string out; out.reserve(s.size()); bool ls = true;
    for (char c : s) {
        const bool ws = (c==' '||c=='\t'||c=='\n'||c=='\r');
        if (ws) { if (!ls) out.push_back(' '); ls = true; }
        else    { out.push_back(c); ls = false; }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

using ProbeFn = std::function<void(clang::ASTContext&, clang::Rewriter&)>;

// Minimal Action/Consumer/Factory; mirrors test_qint_alias_subst_emitter.cpp.
struct C : public clang::ASTConsumer {
    C(ProbeFn p, clang::Rewriter* r) : p_(std::move(p)), r_(r) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        r_->setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        if (p_) p_(ctx, *r_);
    }
    ProbeFn p_; clang::Rewriter* r_;
};
class A : public clang::ASTFrontendAction {
public:
    A(ProbeFn p, clang::Rewriter* r, std::string* o)
        : pr_(std::move(p)), rw_(r), out_(o) {}
    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<C>(pr_, rw_);
    }
    void EndSourceFileAction() override {
        const clang::SourceManager& sm = rw_->getSourceMgr();
        const clang::FileID main = sm.getMainFileID();
        const clang::RewriteBuffer* buf = rw_->getRewriteBufferFor(main);
        if (buf) { llvm::raw_string_ostream os(*out_);
                   buf->write(os); os.flush(); }
        else { llvm::StringRef s = sm.getBufferData(main);
               out_->assign(s.data(), s.size()); }
    }
private: ProbeFn pr_; clang::Rewriter* rw_; std::string* out_;
};
class F : public clang::tooling::FrontendActionFactory {
public:
    F(ProbeFn p, clang::Rewriter* r, std::string* o)
        : pr_(std::move(p)), rw_(r), out_(o) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<A>(pr_, rw_, out_);
    }
private: ProbeFn pr_; clang::Rewriter* rw_; std::string* out_;
};

// C3 drain order: matchAST → emit_qram_rewrites → build claimed set
// → emit_qint_alias_subst_rewrites with the gate.
std::string run_consumer_overlap(const std::string& src,
                                 const std::string& fn) {
    clang::Rewriter rw;
    std::string out;
    std::vector<QramSubscriptHit>     qram_hits;
    std::vector<QintAliasSubstMatch>  subst_matches;
    auto probe = [&](clang::ASTContext& ctx, clang::Rewriter& rw) {
        clang::ast_matchers::MatchFinder finder;
        register_qram_subscript_matcher(finder, qram_hits);
        register_qint_alias_subst_matcher(finder, subst_matches);
        finder.matchAST(ctx);
        emit_qram_rewrites(rw, qram_hits);
        QintAliasSubstClaimedDecls claimed;
        for (const auto& h : qram_hits)
            if (h.target_var) claimed.qram_var_decls.insert(h.target_var);
        emit_qint_alias_subst_rewrites(rw, subst_matches, claimed);
    };
    F factory(std::move(probe), &rw, &out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    if (!clang::tooling::runToolOnCodeWithArgs(
            factory.create(), src, args, fn))
        std::fprintf(stderr, "FAIL  tool run on %s\n", fn.c_str());
    return out;
}

struct Counts { std::size_t qram = 0, subst_var = 0, intersect = 0; };

Counts run_no_overlap_probe(const std::string& src, const std::string& fn) {
    Counts c;
    std::vector<QramSubscriptHit>     qram_hits;
    std::vector<QintAliasSubstMatch>  subst_matches;
    clang::ast_matchers::MatchFinder finder;
    register_qram_subscript_matcher(finder, qram_hits);
    register_qint_alias_subst_matcher(finder, subst_matches);
    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    if (!clang::tooling::runToolOnCodeWithArgs(
            factory->create(), src, args, fn)) {
        std::fprintf(stderr, "FAIL  tool run on %s\n", fn.c_str());
        return c;
    }
    std::unordered_set<const clang::VarDecl*> qset;
    for (const auto& h : qram_hits)
        if (h.target_var) qset.insert(h.target_var);
    c.qram = qset.size();
    for (const auto& m : subst_matches) {
        if (m.kind != QintAliasSubstKind::VarDecl || !m.vd) continue;
        ++c.subst_var;
        if (qset.count(m.vd) > 0) ++c.intersect;
    }
    return c;
}

void test_overlap_fixture_round_trip() {
    const std::string in_  = slurp(fpath("qint_alias_subst_overlap.cpp"));
    const std::string gold = slurp(fpath("qint_alias_subst_overlap.expected.cpp"));
    CHECK(!in_.empty()); CHECK(!gold.empty());
    if (in_.empty() || gold.empty()) return;
    const std::string out = run_consumer_overlap(in_,
        "qint_alias_subst_overlap.cpp");
    CHECK(!out.empty());
    CHECK_EQ_STR(normalise(out), normalise(gold));
}

void test_matcher_qint_alias_subst_no_overlap() {
    const std::string in_ = slurp(fpath("qint_alias_subst_overlap.cpp"));
    CHECK(!in_.empty());
    if (in_.empty()) return;
    const auto c = run_no_overlap_probe(in_,
        "qint_alias_subst_overlap.cpp");
    CHECK(c.qram == 1);          // QRAM fires on `b`.
    CHECK(c.subst_var == 2);     // alias-subst on `b` AND `x`.
    CHECK(c.intersect == 1);     // overlap on `b` — guard MUST drop.
}

} // anonymous namespace

}  // namespace sturm_test_consumer_qint_alias_subst_ns

int run_test_consumer_qint_alias_subst(int /*argc*/, char** /*argv*/) {
    using namespace sturm_test_consumer_qint_alias_subst_ns;
    test_overlap_fixture_round_trip();
    test_matcher_qint_alias_subst_no_overlap();
    std::fprintf(stderr,
                 "test_consumer_qint_alias_subst: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
