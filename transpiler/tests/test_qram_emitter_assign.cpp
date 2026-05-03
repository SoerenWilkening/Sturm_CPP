// test_qram_emitter_assign.cpp -- sturm-u9ge.6 (Beat H1) unit tests.
// Plan §11 (post-v1 backlog) / Beat H1; PRD §9 row 1. Pure-string
// emission + AST-driven rewrite + empty-hit-vector no-op. LoC budget:
// <= 300.

#include "qram_emitter_assign.hpp"
#include "matcher_qram_subscript_assign.hpp"

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

// Hermetic stub mirroring `sturm::frontend::qint`, backend
// `sturm::qint_t<W>`, and a minimal `array<T,N>` stand-in. Includes
// `qint& operator=(qint_t<W>&)` so `b = a[i];` type-checks.
constexpr std::string_view kStub = R"CPP(
namespace sturm { namespace frontend { class qint; }
template <int W> class qint_t { public: qint_t() {} qint_t(const qint_t&) {}
  qint_t& operator=(const sturm::frontend::qint&) { return *this; } };
namespace frontend { class qint { public: qint() noexcept = default;
  qint(long long v) noexcept : value_(v) {}
  qint(const qint&) noexcept = default;
  template <int W> qint(const qint_t<W>&) noexcept {}
  qint& operator=(const qint&) noexcept = default;
  template <int W> qint& operator=(const qint_t<W>&) noexcept { return *this; }
  operator unsigned long() const noexcept { return (unsigned long)value_; }
private: long long value_ = 0; };
} }
using qint = sturm::frontend::qint;
template <typename T, unsigned long N> struct array {
  T data_[N];
  T& operator[](unsigned long i)             { return data_[i]; }
  const T& operator[](unsigned long i) const { return data_[i]; }
};
)CPP";

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
    std::vector<QramSubscriptAssignHit> hits;
    auto probe = [&hits](clang::ASTContext& ctx, clang::Rewriter& rw) {
        clang::ast_matchers::MatchFinder finder;
        register_qram_subscript_assign_matcher(finder, hits);
        finder.matchAST(ctx);
        emit_qram_assign_rewrites(rw, hits);
    };
    TFactory factory(std::move(probe), &rw, &out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), src, args, filename);
    if (!ok) std::fprintf(stderr, "FAIL  tool run on %s\n", filename.c_str());
    return out;
}

// -- Pure-string emission -------------------------------------------------

void test_text_std_array_with_width() {
    QramAssignEmission em = emit_qram_assign_text(
        QramContainerKind::StdArray, "b", "a", "i", "", 8);
    CHECK_EQ_STR(em.text,
        std::string("::sturm::__QRAM_target_uncompute(b); "
                    "::sturm::QRAM_read(a, i, b)"));
    CHECK(em.kind == QramContainerKind::StdArray);
}

void test_text_c_array_with_width() {
    QramAssignEmission em = emit_qram_assign_text(
        QramContainerKind::CArray, "b", "a", "i", "", 16);
    CHECK_EQ_STR(em.text,
        std::string("::sturm::__QRAM_target_uncompute(b); "
                    "::sturm::QRAM_read(a, i, b)"));
}

void test_text_pointer_with_length() {
    QramAssignEmission em = emit_qram_assign_text(
        QramContainerKind::Pointer, "b", "a", "i", "n", 32);
    CHECK_EQ_STR(em.text,
        std::string("::sturm::__QRAM_target_uncompute(b); "
                    "::sturm::QRAM_read(a, n, i, b)"));
}

void test_text_pointer_missing_length() {
    QramAssignEmission em = emit_qram_assign_text(
        QramContainerKind::Pointer, "b", "a", "i", "", 32);
    CHECK(contains(em.text, "qram-pointer-length-missing"));
    // The uncompute call MUST NOT include the length placeholder; it
    // takes only the target.
    CHECK(contains(em.text, "__QRAM_target_uncompute(b)"));
}

void test_text_zero_width_unqualified() {
    QramAssignEmission em = emit_qram_assign_text(
        QramContainerKind::StdArray, "b", "a", "i", "", 0);
    CHECK_EQ_STR(em.text,
        std::string("__QRAM_target_uncompute(b); QRAM_read(a, i, b)"));
}

void test_text_empty_operands_return_empty() {
    using K = QramContainerKind;
    CHECK(emit_qram_assign_text(K::StdArray, "",  "a", "i", "", 8).text.empty());
    CHECK(emit_qram_assign_text(K::StdArray, "b", "",  "i", "", 8).text.empty());
    CHECK(emit_qram_assign_text(K::StdArray, "b", "a", "",  "", 8).text.empty());
}

// -- Empty hit vector => no-op -------------------------------------------
void test_empty_hits_is_noop() {
    clang::Rewriter rw;
    std::string out;
    auto probe = [](clang::ASTContext&, clang::Rewriter& rw) {
        std::vector<QramSubscriptAssignHit> empty;
        emit_qram_assign_rewrites(rw, empty);
    };
    TFactory factory(std::move(probe), &rw, &out);
    const std::string trivial = "void demo() {}\n";
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    CHECK(clang::tooling::runToolOnCodeWithArgs(
        factory.create(), trivial, args, "noop_assign_input.cpp"));
    CHECK_EQ_STR(out, trivial);
}

// -- AST-driven rewrite shape --------------------------------------------
void test_ast_std_array_rewrite() {
    std::string src(kStub);
    src += "void demo(qint i) {\n"
           "    array<sturm::qint_t<8>, 4> a;\n"
           "    qint b;\n"
           "    b = a[i];\n"
           "    (void)b;\n}\n";
    const std::string out = run_emitter(src, "h1_std_array.cpp");
    CHECK(!out.empty());
    CHECK(contains(out, "::sturm::__QRAM_target_uncompute(b)"));
    CHECK(contains(out, "::sturm::QRAM_read(a, i, b)"));
    // The original assignment text must have been replaced.
    CHECK(!contains(out, "b = a[i]"));
}

void test_ast_c_array_rewrite() {
    std::string src(kStub);
    src += "void demo(qint i) {\n"
           "    sturm::qint_t<16> a[4];\n"
           "    qint b;\n"
           "    b = a[i];\n"
           "    (void)b;\n}\n";
    const std::string out = run_emitter(src, "h1_c_array.cpp");
    CHECK(!out.empty());
    CHECK(contains(out, "::sturm::__QRAM_target_uncompute(b)"));
    CHECK(contains(out, "::sturm::QRAM_read(a, i, b)"));
}

void test_ast_pointer_rewrite_with_length() {
    std::string src(kStub);
    src += "void demo(sturm::qint_t<32>* a, unsigned long n, qint i) {\n"
           "    (void)n;\n"
           "    qint b;\n"
           "    b = a[i];\n"
           "    (void)b;\n}\n";
    const std::string out = run_emitter(src, "h1_pointer.cpp");
    CHECK(!out.empty());
    CHECK(contains(out, "::sturm::__QRAM_target_uncompute(b)"));
    CHECK(contains(out, "::sturm::QRAM_read(a, n, i, b)"));
}

void test_ast_two_assignments_rewrite() {
    std::string src(kStub);
    src += "void demo(qint i, qint j) {\n"
           "    sturm::qint_t<8> a[4];\n"
           "    qint b;\n"
           "    b = a[i];\n"
           "    b = a[j];\n"
           "    (void)b;\n}\n";
    const std::string out = run_emitter(src, "h1_two_assignments.cpp");
    CHECK(!out.empty());
    CHECK(contains(out, "::sturm::QRAM_read(a, i, b)"));
    CHECK(contains(out, "::sturm::QRAM_read(a, j, b)"));
    // Both uncomputes must be present (one per assignment).
    // Count occurrences via simple search.
    size_t count = 0; size_t pos = 0;
    const std::string needle = "__QRAM_target_uncompute(b)";
    while ((pos = out.find(needle, pos)) != std::string::npos) {
        ++count; pos += needle.size();
    }
    CHECK(count == 2);
}

// Bare C1 shape: H1 emitter must NOT touch it.
void test_bare_c1_shape_untouched() {
    std::string src(kStub);
    src += "void demo(qint i) {\n"
           "    sturm::qint_t<8> a[4];\n"
           "    qint b = a[i];\n"
           "    (void)b;\n}\n";
    const std::string out = run_emitter(src, "h1_bare.cpp");
    CHECK(!out.empty());
    CHECK(!contains(out, "__QRAM_target_uncompute"));
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
    test_ast_two_assignments_rewrite();
    test_bare_c1_shape_untouched();

    std::fprintf(stderr, "test_qram_emitter_assign: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
