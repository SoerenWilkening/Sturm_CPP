// test_qram_emitter_expr.cpp — sturm-u9ge.9 (Beat H4) unit tests.
// Plan §11 (post-v1 backlog) / Beat H4; PRD §9 row 4. Pure-string
// emission + AST-driven rewrite + empty-hit-vector no-op. LoC budget:
// <= 300 (split-along-axis allowed if needed; this file fits within
// budget after trimming the hermetic stub + harness boilerplate).

#include "qram_emitter_expr.hpp"
#include "matcher_qram_subscript_expr.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

static int tests_run = 0, tests_pass = 0;

#define CHECK(cond) do { ++tests_run; if (cond) { ++tests_pass; }      \
    else { std::fprintf(stderr, "FAIL  %s:%d  %s\n",                   \
                        __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_EQ_STR(got, want) do { ++tests_run;                      \
    if ((got) == (want)) { ++tests_pass; }                             \
    else { std::fprintf(stderr,                                        \
        "FAIL  %s:%d  diff\n  got:  <<<%s>>>\n  want: <<<%s>>>\n",     \
        __FILE__, __LINE__,                                            \
        std::string(got).c_str(), std::string(want).c_str()); }        \
} while (0)

namespace {

bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

// Hermetic stub: minimal frontend `qint` (UDC), backend `qint_t<W>`,
// `array<T,N>` stand-in. Mirrors the matcher test's stub but trimmed.
constexpr std::string_view kStub = R"CPP(
namespace sturm { namespace frontend { class qint; }
template <int W> class qint_t { public: qint_t() {} qint_t(const qint_t&) {}
  qint_t& operator=(const sturm::frontend::qint&) { return *this; } };
namespace frontend { class qint { public: qint() noexcept = default;
  qint(long long v) noexcept : value_(v) {}
  qint(const qint&) noexcept = default;
  template <int W> qint(const qint_t<W>&) noexcept {}
  template <int W> qint& operator=(const qint_t<W>&) noexcept { return *this; }
  operator unsigned long() const noexcept { return (unsigned long)value_; }
private: long long value_ = 0; };
inline qint operator+(const qint& a, const qint& b) noexcept { (void)a; (void)b; return qint{}; }
} }
using qint = sturm::frontend::qint;
template <typename T, unsigned long N> struct array {
  T data_[N];
  T& operator[](unsigned long i)             { return data_[i]; }
  const T& operator[](unsigned long i) const { return data_[i]; }
};
)CPP";

// Tooling wiring: ASTConsumer that runs `probe`, FrontendAction that
// captures the rewritten main file. Mirrors test_qram_emitter.cpp.
using ProbeFn = std::function<void(clang::ASTContext&, clang::Rewriter&)>;

struct TConsumer : public clang::ASTConsumer {
    TConsumer(ProbeFn p, clang::Rewriter* r) : p_(std::move(p)), r_(r) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        r_->setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        if (p_) p_(ctx, *r_);
    }
    ProbeFn p_; clang::Rewriter* r_;
};
class TAction : public clang::ASTFrontendAction {
public:
    TAction(ProbeFn p, clang::Rewriter* r, std::string* o)
        : probe_(std::move(p)), rw_(r), out_(o) {}
    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<TConsumer>(probe_, rw_);
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
private: ProbeFn probe_; clang::Rewriter* rw_; std::string* out_;
};
class TFactory : public clang::tooling::FrontendActionFactory {
public:
    TFactory(ProbeFn p, clang::Rewriter* r, std::string* o)
        : probe_(std::move(p)), rw_(r), out_(o) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<TAction>(probe_, rw_, out_);
    }
private: ProbeFn probe_; clang::Rewriter* rw_; std::string* out_;
};

std::string run_emitter(const std::string& src,
                        const std::string& filename) {
    clang::Rewriter rw;
    std::string out;
    std::vector<QramSubscriptExprHit> hits;
    auto probe = [&hits](clang::ASTContext& ctx, clang::Rewriter& rw) {
        clang::ast_matchers::MatchFinder finder;
        register_qram_subscript_expr_matcher(finder, hits);
        finder.matchAST(ctx);
        emit_qram_expr_rewrites(rw, hits);
    };
    TFactory factory(std::move(probe), &rw, &out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), src, args, filename);
    if (!ok) std::fprintf(stderr, "FAIL  tool run on %s\n", filename.c_str());
    return out;
}

// ── Pure-string emission ────────────────────────────────────────────────────

void test_text_std_array_with_width() {
    QramExprEmission em = emit_qram_expr_text(
        QramContainerKind::StdArray, "__qram_h4_0", "a", "i", "", 8);
    CHECK_EQ_STR(em.extract_text,
        std::string("sturm::qint_t<8> __qram_h4_0; "
                    "::sturm::QRAM_read(a, i, __qram_h4_0);"));
    CHECK_EQ_STR(em.replace_text, std::string("__qram_h4_0"));
    CHECK_EQ_STR(em.adjoint_text,
        std::string("::sturm::__QRAM_read_adj(a, i, __qram_h4_0);"));
    CHECK(em.kind == QramContainerKind::StdArray);
}

void test_text_c_array_with_width() {
    QramExprEmission em = emit_qram_expr_text(
        QramContainerKind::CArray, "__qram_h4_0", "a", "i", "", 16);
    CHECK_EQ_STR(em.extract_text,
        std::string("sturm::qint_t<16> __qram_h4_0; "
                    "::sturm::QRAM_read(a, i, __qram_h4_0);"));
}

void test_text_pointer_with_length() {
    QramExprEmission em = emit_qram_expr_text(
        QramContainerKind::Pointer, "__qram_h4_0", "a", "i", "n", 32);
    CHECK_EQ_STR(em.extract_text,
        std::string("sturm::qint_t<32> __qram_h4_0; "
                    "::sturm::QRAM_read(a, n, i, __qram_h4_0);"));
    CHECK_EQ_STR(em.adjoint_text,
        std::string("::sturm::__QRAM_read_adj(a, n, i, __qram_h4_0);"));
}

void test_text_pointer_missing_length() {
    QramExprEmission em = emit_qram_expr_text(
        QramContainerKind::Pointer, "__qram_h4_0", "a", "i", "", 32);
    CHECK(contains(em.extract_text, "qram-pointer-length-missing"));
    CHECK(contains(em.adjoint_text, "qram-pointer-length-missing"));
}

void test_text_zero_width_unqualified() {
    QramExprEmission em = emit_qram_expr_text(
        QramContainerKind::StdArray, "__qram_h4_0", "a", "i", "", 0);
    CHECK_EQ_STR(em.extract_text,
        std::string("qint __qram_h4_0; "
                    "::sturm::QRAM_read(a, i, __qram_h4_0);"));
}

void test_text_empty_operands_return_empty() {
    using K = QramContainerKind;
    CHECK(emit_qram_expr_text(K::StdArray, "",  "a", "i", "", 8).extract_text.empty());
    CHECK(emit_qram_expr_text(K::StdArray, "x", "",  "i", "", 8).extract_text.empty());
    CHECK(emit_qram_expr_text(K::StdArray, "x", "a", "",  "", 8).extract_text.empty());
}

// ── Empty hit vector ⇒ no-op ────────────────────────────────────────────────
void test_empty_hits_is_noop() {
    clang::Rewriter rw;
    std::string out;
    auto probe = [](clang::ASTContext&, clang::Rewriter& rw) {
        std::vector<QramSubscriptExprHit> empty;
        emit_qram_expr_rewrites(rw, empty);
    };
    TFactory factory(std::move(probe), &rw, &out);
    const std::string trivial = "void demo() {}\n";
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    CHECK(clang::tooling::runToolOnCodeWithArgs(
        factory.create(), trivial, args, "noop_input.cpp"));
    CHECK_EQ_STR(out, trivial);
}

// ── AST-driven rewrite shape ────────────────────────────────────────────────
// Per case: pre-call extract + in-place replace + LHS type rewrite +
// post-call uncompute. Pointer arm carries `n`. Multi-subscript yields
// two distinct ancillas + LHS rewritten exactly once.
void test_ast_std_array_rewrite() {
    std::string src(kStub);
    src += "void demo(qint i, qint d) {\n"
           "    array<sturm::qint_t<8>, 4> a;\n"
           "    qint c = a[i] + d;\n"
           "    (void)c;\n}\n";
    const std::string out = run_emitter(src, "h4_std_array.cpp");
    CHECK(!out.empty());
    CHECK(contains(out, "sturm::qint_t<8> __qram_h4_0"));
    CHECK(contains(out, "::sturm::QRAM_read(a, i, __qram_h4_0)"));
    CHECK(contains(out, "__qram_h4_0 + d"));
    CHECK(contains(out, "sturm::qint_t<8> c ="));
    CHECK(contains(out, "::sturm::__QRAM_read_adj(a, i, __qram_h4_0)"));
}

void test_ast_c_array_rewrite() {
    std::string src(kStub);
    src += "void demo(qint i, qint d) {\n"
           "    sturm::qint_t<16> a[4];\n"
           "    qint c = a[i] + d;\n"
           "    (void)c;\n}\n";
    const std::string out = run_emitter(src, "h4_c_array.cpp");
    CHECK(!out.empty());
    CHECK(contains(out, "sturm::qint_t<16> __qram_h4_0"));
    CHECK(contains(out, "::sturm::QRAM_read(a, i, __qram_h4_0)"));
    CHECK(contains(out, "__qram_h4_0 + d"));
    CHECK(contains(out, "sturm::qint_t<16> c ="));
    CHECK(contains(out, "::sturm::__QRAM_read_adj(a, i, __qram_h4_0)"));
}

void test_ast_pointer_rewrite_with_length() {
    std::string src(kStub);
    src += "void demo(sturm::qint_t<32>* a, unsigned long n, qint i, qint d) {\n"
           "    (void)n;\n"
           "    qint c = a[i] + d;\n"
           "    (void)c;\n}\n";
    const std::string out = run_emitter(src, "h4_pointer.cpp");
    CHECK(!out.empty());
    CHECK(contains(out, "sturm::qint_t<32> __qram_h4_0"));
    CHECK(contains(out, "::sturm::QRAM_read(a, n, i, __qram_h4_0)"));
    CHECK(contains(out, "__qram_h4_0 + d"));
    CHECK(contains(out, "::sturm::__QRAM_read_adj(a, n, i, __qram_h4_0)"));
}

void test_ast_multi_subscript_rewrite() {
    std::string src(kStub);
    src += "void demo(qint i, qint j, qint d) {\n"
           "    sturm::qint_t<8> a[4];\n"
           "    qint c = (a[i] + d) + a[j];\n"
           "    (void)c;\n}\n";
    const std::string out = run_emitter(src, "h4_multi.cpp");
    CHECK(!out.empty());
    CHECK(contains(out, "__qram_h4_0"));
    CHECK(contains(out, "__qram_h4_1"));
    CHECK(contains(out, "::sturm::QRAM_read(a, i, __qram_h4_0)"));
    CHECK(contains(out, "::sturm::QRAM_read(a, j, __qram_h4_1)"));
    CHECK(contains(out, "::sturm::__QRAM_read_adj(a, i, __qram_h4_0)"));
    CHECK(contains(out, "::sturm::__QRAM_read_adj(a, j, __qram_h4_1)"));
    CHECK(contains(out, "sturm::qint_t<8> c ="));
}

// Bare C1 shape: H4 emitter must NOT touch it.
void test_bare_c1_shape_untouched() {
    std::string src(kStub);
    src += "void demo(qint i) {\n"
           "    sturm::qint_t<8> a[4];\n"
           "    qint b = a[i];\n"
           "    (void)b;\n}\n";
    const std::string out = run_emitter(src, "h4_bare.cpp");
    CHECK(!out.empty());
    CHECK(!contains(out, "__qram_h4_"));
    CHECK(contains(out, "qint b = a[i]"));
}

} // anonymous namespace

int main() {
    test_text_std_array_with_width();
    test_text_c_array_with_width();
    test_text_pointer_with_length();
    test_text_pointer_missing_length();
    test_text_zero_width_unqualified();
    test_text_empty_operands_return_empty();
    test_empty_hits_is_noop();
    test_ast_std_array_rewrite();
    test_ast_c_array_rewrite();
    test_ast_pointer_rewrite_with_length();
    test_ast_multi_subscript_rewrite();
    test_bare_c1_shape_untouched();

    std::fprintf(stderr, "test_qram_emitter_expr: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
