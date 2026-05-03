// test_qram_e2e_real.cpp -- sturm-ddgo end-to-end gate against the
// production headers (real `std::array` from `<array>` + real
// `frontend::qint` from `qint_alias.hpp` + real `qint_t<W>` from
// `qint_core.hpp`).
//
// Companion to `test_qram_e2e.cpp` which exercises the same C1 / H1 /
// H4 + emitter pipelines against a hermetic stub. The stub mirrors the
// production qint surface (implicit `operator size_t()`, converting
// ctor `qint(qint_t<W>&)`) but elides backend qubit-pool / sink /
// uncompute_api wiring — so the stub-driven test could never witness
// the AST-shape gap between the stub's `template <int W>` qint_t and
// the production `template <std::size_t W>` qint_t, nor the extra
// MaterializeTemporaryExpr / CXXBindTemporaryExpr / CXXConstructExpr
// layers Clang emits when the production header lacks a direct
// `qint& operator=(const qint_t<W>&)` overload (sturm-ddgo).
//
// The test transpiles a small program against the same production
// headers `examples/qram_demo.cpp` includes and asserts:
//   - C1 rewrites `qint b = a[i];` to
//     `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);`.
//   - H1 rewrites `b = a[i];` to
//     `::sturm::__QRAM_target_uncompute(b); ::sturm::QRAM_read(a, i, b)`.
//   - H4 rewrites `qint c = a[i] + d;` into the extract +
//     subscript-replace + post-call-adjoint triple.
//
// Each is a SHAPE assertion on the rewritten source text (no runtime
// invocation), matching the posture of `test_qram_e2e.cpp`'s
// `check_pipeline` helper. The runtime path is exercised end-to-end
// by `examples/qram_demo.cpp`'s `[A]` block (sturm-ddgo).

#define STURM_BACKEND_ENABLED 1

#include "matcher_qram_subscript.hpp"
#include "matcher_qram_subscript_assign.hpp"
#include "matcher_qram_subscript_expr.hpp"
#include "qram_emitter.hpp"
#include "qram_emitter_assign.hpp"
#include "qram_emitter_expr.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using sturm::transpile::QramSubscriptHit;
using sturm::transpile::QramSubscriptAssignHit;
using sturm::transpile::QramSubscriptExprHit;
using sturm::transpile::register_qram_subscript_matcher;
using sturm::transpile::register_qram_subscript_assign_matcher;
using sturm::transpile::register_qram_subscript_expr_matcher;
using sturm::transpile::emit_qram_rewrites;
using sturm::transpile::emit_qram_assign_rewrites;
using sturm::transpile::emit_qram_expr_rewrites;

namespace {

static int tests_run = 0, tests_pass = 0;
#define CHECK(cond) do { ++tests_run;                                 \
    if (cond) { ++tests_pass; }                                       \
    else { std::fprintf(stderr, "FAIL  %s:%d  %s\n",                  \
                        __FILE__, __LINE__, #cond); } } while (0)

// The production headers + a small body invoking each matcher arm.
// Mirrors the [A] block of `examples/qram_demo.cpp` plus the H1 / H4
// shapes the issue's "Related: H1 (sturm-u9ge.6) and H4 (sturm-u9ge.9)"
// cross-link calls out.
constexpr std::string_view kBodyC1 = R"CPP(
#define STURM_BACKEND_ENABLED 1
#include "sturm/qtypes/qint_alias.hpp"
#include "sturm/qtypes/qint.hpp"
#include <array>
using qint = sturm::frontend::qint;
[[clang::annotate("sturm::reversible")]]
void demo() {
    std::array<sturm::qint_t<4>, 4> a{};
    qint i = 2;
    qint b = a[i];
    (void)b;
})CPP";

constexpr std::string_view kBodyH1 = R"CPP(
#define STURM_BACKEND_ENABLED 1
#include "sturm/qtypes/qint_alias.hpp"
#include "sturm/qtypes/qint.hpp"
#include <array>
using qint = sturm::frontend::qint;
void demo() {
    std::array<sturm::qint_t<4>, 4> a{};
    qint i = 2;
    qint b;
    b = a[i];
    (void)b;
})CPP";

constexpr std::string_view kBodyH4 = R"CPP(
#define STURM_BACKEND_ENABLED 1
#include "sturm/qtypes/qint_alias.hpp"
#include "sturm/qtypes/qint.hpp"
#include <array>
using qint = sturm::frontend::qint;
void demo() {
    std::array<sturm::qint_t<4>, 4> a{};
    qint i = 2;
    qint d = 3;
    qint c = a[i] + d;
    (void)c;
})CPP";

using ProbeFn = std::function<void(clang::ASTContext&, clang::Rewriter&)>;
struct E2eConsumer : public clang::ASTConsumer {
    E2eConsumer(ProbeFn p, clang::Rewriter* r) : p_(std::move(p)), r_(r) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        r_->setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        if (p_) p_(ctx, *r_);
    }
    ProbeFn p_; clang::Rewriter* r_;
};
class E2eAction : public clang::ASTFrontendAction {
public:
    E2eAction(ProbeFn p, clang::Rewriter* r, std::string* o)
        : probe_(std::move(p)), rw_(r), out_(o) {}
    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<E2eConsumer>(probe_, rw_);
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
class E2eFactory : public clang::tooling::FrontendActionFactory {
public:
    E2eFactory(ProbeFn p, clang::Rewriter* r, std::string* o)
        : probe_(std::move(p)), rw_(r), out_(o) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<E2eAction>(probe_, rw_, out_);
    }
private:
    ProbeFn probe_; clang::Rewriter* rw_; std::string* out_;
};

// Run the supplied probe against `body` with the production sturm
// include path on the cmd line. Returns the rewritten main-file
// buffer.
std::string transpile_with_real_headers(std::string_view body,
                                        const ProbeFn& probe) {
    clang::Rewriter rw;
    std::string out;
    E2eFactory factory(probe, &rw, &out);
    std::vector<std::string> args{
        "-std=c++20", "-fsyntax-only",
        "-DSTURM_BACKEND_ENABLED=1",
        "-DORKAN_USING_STUB=1",
        "-DSTURM_ANCILLA_CAPACITY=512",
        std::string("-I") + STURM_INCLUDE_DIR,
        std::string("-I") + STURM_VENDOR_DIR,
    };
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), std::string(body), args, "qram_e2e_real_input.cpp");
    if (!ok) std::fprintf(stderr, "FAIL  tool run on real-headers input\n");
    return out;
}

std::size_t count_occurrences(std::string_view hay, std::string_view needle) {
    std::size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string_view::npos) {
        ++n; pos += needle.size();
    }
    return n;
}

// ── C1 against production headers ──────────────────────────────────────────
// Asserts the rewrite text matches the spec's `sturm::qint_t<W> b;
// ::sturm::QRAM_read(a, i, b);` shape AND that no `.operator size_t(`
// / `.operator unsigned long(` UDC tokens survive (PRD §5).
void test_c1_real() {
    auto probe = [](clang::ASTContext& ctx, clang::Rewriter& r) {
        clang::ast_matchers::MatchFinder finder;
        std::vector<QramSubscriptHit> hits;
        register_qram_subscript_matcher(finder, hits);
        finder.matchAST(ctx);
        emit_qram_rewrites(r, hits);
    };
    const std::string out = transpile_with_real_headers(kBodyC1, probe);
    CHECK(!out.empty());
    CHECK(out.find(".operator size_t(") == std::string::npos);
    CHECK(out.find(".operator unsigned long(") == std::string::npos);
    CHECK(count_occurrences(out, "::sturm::QRAM_read(") == 1u);
    // Rewritten declaration carries the inferred W=4 from the
    // container's `qint_t<4>` element type.
    CHECK(out.find("sturm::qint_t<4> b;") != std::string::npos);
    // Reversible scope plants the matching adjoint at the close brace.
    CHECK(count_occurrences(
        out, "::sturm::invert<&::sturm::QRAM_read>()") == 1u);
}

// ── H1 against production headers ──────────────────────────────────────────
// The fixture-driven H1 matcher fires because the fixture stub adds
// `qint& operator=(const qint_t<W>&)`. The production qint (from
// qint_alias.hpp) lacks that overload, so `b = a[i];` parses as an
// assignment of a converting-temporary `qint(qint_t<W>&)` -- the RHS
// is wrapped in MaterializeTemporaryExpr / CXXBindTemporaryExpr /
// CXXConstructExpr layers the original H1 matcher's `IgnoreParenImpCasts`
// peel did not handle. The `peel_to_subscript` helper added in
// sturm-ddgo strips those layers; this test pins that fix.
void test_h1_real() {
    auto probe = [](clang::ASTContext& ctx, clang::Rewriter& r) {
        clang::ast_matchers::MatchFinder finder;
        std::vector<QramSubscriptAssignHit> hits;
        register_qram_subscript_assign_matcher(finder, hits);
        finder.matchAST(ctx);
        emit_qram_assign_rewrites(r, hits);
    };
    const std::string out = transpile_with_real_headers(kBodyH1, probe);
    CHECK(!out.empty());
    CHECK(out.find(".operator size_t(") == std::string::npos);
    // H1's emitted text plants both the pre-call uncompute and the
    // forward QRAM_read.
    CHECK(count_occurrences(out, "::sturm::__QRAM_target_uncompute(") == 1u);
    CHECK(count_occurrences(out, "::sturm::QRAM_read(") == 1u);
}

// ── H4 against production headers ──────────────────────────────────────────
// H4's matcher walks the VarDecl's initializer subtree for non-immediate
// subscripts; it already fired against real headers (the AST shape
// has `a[i]` inside an `operator+` call, which the matcher's
// `is_immediate_init` gate filters correctly). This test pins that
// the production-header AST still emits the expected H4 triple.
void test_h4_real() {
    auto probe = [](clang::ASTContext& ctx, clang::Rewriter& r) {
        clang::ast_matchers::MatchFinder finder;
        std::vector<QramSubscriptExprHit> hits;
        register_qram_subscript_expr_matcher(finder, hits);
        finder.matchAST(ctx);
        emit_qram_expr_rewrites(r, hits);
    };
    const std::string out = transpile_with_real_headers(kBodyH4, probe);
    CHECK(!out.empty());
    // H4 emits an ancilla, the forward QRAM_read, the subscript
    // replacement, and the post-call adjoint.
    CHECK(out.find("__qram_h4_") != std::string::npos);
    CHECK(count_occurrences(out, "::sturm::QRAM_read(") == 1u);
    CHECK(count_occurrences(out, "::sturm::__QRAM_read_adj(") == 1u);
}

}  // namespace

int main() {
    test_c1_real();
    test_h1_real();
    test_h4_real();
    std::fprintf(stderr, "test_qram_e2e_real: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
