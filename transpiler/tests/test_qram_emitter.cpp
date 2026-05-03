// test_qram_emitter.cpp — sturm-u9ge.15 (Beat D2) unit tests.
//
// Three golden-file fixture pairs (one per PRD §7 container shape),
// pure-string emission coverage, and the empty-hit-vector no-op.
// Plan §7 / D2; PRD §8 / M4. Per plan §14 the golden comparison
// runs both buffers through a whitespace-collapsing normaliser
// (insulating against formatting churn). LoC budget: <= 300.

#include "qram_emitter.hpp"
#include "matcher_qram_subscript.hpp"

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

#ifndef STURM_QRAM_EMITTER_FIXTURES_DIR
#error "CMake must define STURM_QRAM_EMITTER_FIXTURES_DIR"
#endif

// ── Test harness ────────────────────────────────────────────────────────────
static int tests_run = 0;
static int tests_pass = 0;

#define CHECK(cond) do { ++tests_run;                                  \
    if (cond) { ++tests_pass; }                                        \
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

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

std::filesystem::path fixture_path(std::string_view name) {
    std::filesystem::path root(STURM_QRAM_EMITTER_FIXTURES_DIR);
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
// copies the rewritten main-file buffer into `*out`.
using ProbeFn = std::function<void(clang::ASTContext&, clang::Rewriter&)>;

struct QramConsumer : public clang::ASTConsumer {
    QramConsumer(ProbeFn p, clang::Rewriter* r) : p_(std::move(p)), r_(r) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        r_->setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        if (p_) p_(ctx, *r_);
    }
    ProbeFn p_; clang::Rewriter* r_;
};

class QramAction : public clang::ASTFrontendAction {
public:
    QramAction(ProbeFn p, clang::Rewriter* r, std::string* o)
        : probe_(std::move(p)), rw_(r), out_(o) {}
    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<QramConsumer>(probe_, rw_);
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

class QramFactory : public clang::tooling::FrontendActionFactory {
public:
    QramFactory(ProbeFn p, clang::Rewriter* r, std::string* o)
        : probe_(std::move(p)), rw_(r), out_(o) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<QramAction>(probe_, rw_, out_);
    }
private:
    ProbeFn probe_; clang::Rewriter* rw_; std::string* out_;
};

// Run C1 + D2 against fixture src; returns the rewritten main-file.
std::string run_emitter_on_fixture(const std::string& src,
                                   const std::string& filename) {
    clang::Rewriter rw;
    std::string out;
    std::vector<QramSubscriptHit> hits;
    auto probe = [&hits](clang::ASTContext& ctx, clang::Rewriter& rw) {
        clang::ast_matchers::MatchFinder finder;
        register_qram_subscript_matcher(finder, hits);
        finder.matchAST(ctx);
        emit_qram_rewrites(rw, hits);
    };
    QramFactory factory(std::move(probe), &rw, &out);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), src, args, filename);
    if (!ok) std::fprintf(stderr, "FAIL  tool run on %s\n", filename.c_str());
    return out;
}

void golden_case(std::string_view input_name,
                 std::string_view expected_name) {
    const auto input_path = fixture_path(input_name);
    const auto gold_path = fixture_path(expected_name);
    const std::string input = slurp(input_path);
    const std::string gold = slurp(gold_path);
    CHECK(!input.empty());
    CHECK(!gold.empty());
    if (input.empty() || gold.empty()) return;
    const std::string out =
        run_emitter_on_fixture(input, std::string(input_name));
    CHECK(!out.empty());
    CHECK_EQ_STR(normalise(out), normalise(gold));
}

// Pure-string emission
void test_text_std_array_with_width() {
    QramEmission em = emit_qram_forward_text(
        QramContainerKind::StdArray, "b", "a", "i", "", 8);
    CHECK_EQ_STR(em.text,
        std::string("sturm::qint_t<8> b; ::sturm::QRAM_read(a, i, b);"));
    CHECK(em.kind == QramContainerKind::StdArray);
}

void test_text_std_array_without_width() {
    QramEmission em = emit_qram_forward_text(
        QramContainerKind::StdArray, "b", "a", "i", "", 0);
    CHECK_EQ_STR(em.text, std::string("qint b; ::sturm::QRAM_read(a, i, b);"));
}

void test_text_c_array_with_width() {
    QramEmission em = emit_qram_forward_text(
        QramContainerKind::CArray, "b", "a", "i", "", 16);
    CHECK_EQ_STR(em.text,
        std::string("sturm::qint_t<16> b; ::sturm::QRAM_read(a, i, b);"));
}

void test_text_pointer_with_length() {
    QramEmission em = emit_qram_forward_text(
        QramContainerKind::Pointer, "b", "a", "i", "n", 32);
    CHECK_EQ_STR(em.text,
        std::string("sturm::qint_t<32> b; ::sturm::QRAM_read(a, n, i, b);"));
    CHECK(em.kind == QramContainerKind::Pointer);
}

void test_text_pointer_missing_length() {
    // PRD §11.1.6: emit a placeholder comment so the rewritten file
    // is unambiguous about the missing argument.
    QramEmission em = emit_qram_forward_text(
        QramContainerKind::Pointer, "b", "a", "i", "", 32);
    CHECK(em.text.find("qram-pointer-length-missing") != std::string::npos);
}

void test_text_empty_operands_return_empty() {
    using K = QramContainerKind;
    CHECK(emit_qram_forward_text(K::StdArray, "",  "a", "i", "", 8).text.empty());
    CHECK(emit_qram_forward_text(K::StdArray, "b", "",  "i", "", 8).text.empty());
    CHECK(emit_qram_forward_text(K::StdArray, "b", "a", "",  "", 8).text.empty());
}

// Negative: empty hit vector ⇒ no-op
void test_empty_hits_is_noop() {
    // emit_qram_rewrites must be a no-op on an empty hit vector.
    // We bypass the matcher and pass an empty vector directly; the
    // expected output is the original source verbatim.
    clang::Rewriter rw;
    std::string out;
    auto probe = [](clang::ASTContext&, clang::Rewriter& rw) {
        std::vector<QramSubscriptHit> empty_hits;
        emit_qram_rewrites(rw, empty_hits);
    };
    QramFactory factory(std::move(probe), &rw, &out);
    const std::string trivial = "void demo() {}\n";
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    CHECK(clang::tooling::runToolOnCodeWithArgs(
        factory.create(), trivial, args, "noop_input.cpp"));
    CHECK_EQ_STR(out, trivial);
}

// Adjoint placement (PRD §D0d.5)
constexpr std::string_view kStub = R"CPP(
namespace sturm { template <int W> class qint_t { public: qint_t() {} qint_t(const qint_t&) {} };
namespace frontend { class qint { public: qint() noexcept = default; qint(long long v) noexcept : value_(v) {}
    qint(const qint&) noexcept = default; template <int W> qint(const qint_t<W>&) noexcept {}
    operator unsigned long() const noexcept { return static_cast<unsigned long>(value_); }
private: long long value_ = 0; }; } }
using qint = sturm::frontend::qint;
)CPP";

void test_reversible_scope_plants_adjoint() {
    std::string src(kStub);
    src += R"CPP(
[[clang::annotate("sturm::reversible")]]
void demo(qint i) { sturm::qint_t<8> a[4]; qint b = a[i]; (void)b; }
)CPP";
    const std::string out = run_emitter_on_fixture(src, "rev_input.cpp");
    CHECK(!out.empty());
    CHECK(out.find("::sturm::QRAM_read(a, i, b)") != std::string::npos);
    // Adjoint planted just before the close brace of the reversible scope.
    CHECK(out.find("::sturm::invert<&::sturm::QRAM_read>()(a, i, b)")
          != std::string::npos);
}

void test_non_reversible_scope_no_adjoint() {
    std::string src(kStub);
    src += R"CPP(
void demo(qint i) { sturm::qint_t<8> a[4]; qint b = a[i]; (void)b; }
)CPP";
    const std::string out = run_emitter_on_fixture(src, "nr_input.cpp");
    CHECK(!out.empty());
    CHECK(out.find("::sturm::QRAM_read(a, i, b)") != std::string::npos);
    CHECK(out.find("::sturm::invert<&::sturm::QRAM_read>") == std::string::npos);
}

// Golden-file fixture pairs
void test_fixture_std_array() {
    golden_case("qram_read_std_array.cpp",
                "qram_read_std_array.expected.cpp");
}

void test_fixture_c_array() {
    golden_case("qram_read_c_array.cpp",
                "qram_read_c_array.expected.cpp");
}

void test_fixture_pointer() {
    golden_case("qram_read_pointer.cpp",
                "qram_read_pointer.expected.cpp");
}

} // anonymous namespace

int main() {
    test_text_std_array_with_width();
    test_text_std_array_without_width();
    test_text_c_array_with_width();
    test_text_pointer_with_length();
    test_text_pointer_missing_length();
    test_text_empty_operands_return_empty();
    test_empty_hits_is_noop();
    test_reversible_scope_plants_adjoint();
    test_non_reversible_scope_no_adjoint();
    test_fixture_std_array();
    test_fixture_c_array();
    test_fixture_pointer();

    std::fprintf(stderr, "test_qram_emitter: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
