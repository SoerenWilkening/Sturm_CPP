// test_width_inference.cpp — sturm-u9ge.11 (Beat B1) unit tests.
// Plan §5 / B1; PRD §6 + §11.3 (D0c). One positive test per rule (1/2/3)
// plus the rule-2 ambiguity negative case. Each test parses a synthetic
// TU via clang tooling, walks for a named `VarDecl`, and runs
// `infer_width()` on it under a controlled `InferContext`.
// LoC budget: <= 300 (plan §1, §5 / B1).

#include "width_inference.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallString.h"

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_width_inference_ns {

using namespace sturm::transpile;

static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else { std::fprintf(stderr, "FAIL  %s:%d  %s\n",                  \
                        __FILE__, __LINE__, #cond); }                 \
} while (0)
#define CHECK_EQ_INT(got, want) do {                                  \
    ++tests_run;                                                      \
    const long long g = static_cast<long long>(got);                  \
    const long long w = static_cast<long long>(want);                 \
    if (g == w) { ++tests_pass; }                                     \
    else { std::fprintf(stderr, "FAIL  %s:%d  got=%lld want=%lld\n",  \
                        __FILE__, __LINE__, g, w); }                  \
} while (0)

namespace {

// Hermetic stub. Two frontend qint forms coexist in distinct
// namespaces so a single TU can exercise both rule 1 (templated
// annotation form, primary template name `qint`) and rule 2/3
// (non-templated alias form). In real source the v1 alias and the
// v2 templated form are mutually exclusive — same identifier, one
// resolution. Backend `sturm::qint_t<W>` matches the production
// primary template name `sturm::qint_t`. `pick_two` is the helper
// for the rule-2 ambiguity test (a 2-arg function whose call
// expression admits two distinct `qint_t<W_e>` element-type arms
// inside one initializer subtree; the conditional `c ? a : b` shape
// is rejected by the parser when the arms have different types).
constexpr std::string_view kStub = R"CPP(
namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    qint_t(long long) {}
};
namespace frontend { class qint {
public:
    qint() {}
    qint(long long) {}
    template <int W> qint(const qint_t<W>&) {}
    operator unsigned long() const { return 0; }
}; }
namespace v2 { template <int W> class qint {
public:
    qint() {}
    qint(long long) {}
    operator unsigned long() const { return 0; }
}; }
} // namespace sturm
using qint   = sturm::frontend::qint;
template <int W> using qint_ann = sturm::v2::qint<W>;
template <typename T, unsigned long N>
struct array {
    T data_[N];
    T& operator[](unsigned long i)             { return data_[i]; }
    const T& operator[](unsigned long i) const { return data_[i]; }
};
inline qint pick_two(const sturm::qint_t<8>& x,
                     const sturm::qint_t<16>& y) {
    (void)x; (void)y; return qint{};
}
inline qint pick_two(const sturm::qint_t<8>& x,
                     const sturm::qint_t<32>& y) {
    (void)x; (void)y; return qint{};
}
)CPP";

class VarDeclByNameFinder
    : public clang::RecursiveASTVisitor<VarDeclByNameFinder> {
public:
    explicit VarDeclByNameFinder(std::string n) : name_(std::move(n)) {}
    bool VisitVarDecl(clang::VarDecl* vd) {
        if (vd && vd->getNameAsString() == name_) { found_ = vd; return false; }
        return true;
    }
    const clang::VarDecl* find() const { return found_; }
private:
    std::string name_;
    const clang::VarDecl* found_ = nullptr;
};

class CountingDiagConsumer final : public clang::DiagnosticConsumer {
public:
    unsigned warnings = 0;
    unsigned errors   = 0;
    unsigned notes    = 0;
    std::vector<std::string> error_texts;
    void HandleDiagnostic(clang::DiagnosticsEngine::Level lvl,
                          const clang::Diagnostic& info) override {
        llvm::SmallString<256> buf;
        info.FormatDiagnostic(buf);
        if (lvl >= clang::DiagnosticsEngine::Error) {
            ++errors;
            error_texts.emplace_back(buf.str());
        } else if (lvl == clang::DiagnosticsEngine::Warning) {
            ++warnings;
        } else if (lvl == clang::DiagnosticsEngine::Note) {
            ++notes;
        }
    }
};

struct ParseResult {
    std::unique_ptr<clang::ASTUnit> ast;
};

ParseResult parse(std::string_view user_src) {
    std::string code;
    code.reserve(kStub.size() + user_src.size());
    code.append(kStub);
    code.append(user_src);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    return {clang::tooling::buildASTFromCodeWithArgs(
        code, args, "width_inference_input.cpp")};
}

const clang::VarDecl* find_var(clang::ASTUnit& ast, std::string n) {
    VarDeclByNameFinder f(std::move(n));
    f.TraverseDecl(ast.getASTContext().getTranslationUnitDecl());
    return f.find();
}

bool contains(const std::string& h, std::string_view n) {
    return h.find(n) != std::string::npos;
}

// Build a fresh DiagnosticsEngine bound to the AST's source manager
// so reported source locations resolve into the parsed TU.
struct Harness {
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs>     ids;
    llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> opts;
    CountingDiagConsumer*                              counter;
    std::unique_ptr<clang::DiagnosticsEngine>          engine;
    explicit Harness(clang::ASTUnit& ast)
        : ids(new clang::DiagnosticIDs()),
          opts(new clang::DiagnosticOptions()),
          counter(new CountingDiagConsumer()),
          engine(std::make_unique<clang::DiagnosticsEngine>(
              ids, opts.get(), counter, /*ShouldOwnClient=*/true)) {
        engine->setSourceManager(&ast.getSourceManager());
    }
};

} // anonymous namespace

// ── Rule 1 positive: `qint_ann<12> b;` → reserved diag, falls to default ────
static void test_rule1_annotation_reserved() {
    auto pr = parse("void demo() { qint_ann<12> b; (void)b; }\n");
    CHECK(pr.ast != nullptr);
    if (!pr.ast) return;
    const auto* vd = find_var(*pr.ast, "b");
    CHECK(vd != nullptr);
    if (!vd) return;
    Harness h(*pr.ast);
    InferContext ctx{h.engine.get(), kDefaultWidth, /*allow=*/false};
    // Falls through to rule 3 default (PRD §11.3 / D0c.1 row 1).
    CHECK_EQ_INT(infer_width(*vd, ctx), kDefaultWidth);
    CHECK_EQ_INT(h.counter->errors, 1);
    if (!h.counter->error_texts.empty()) {
        CHECK(contains(h.counter->error_texts[0],
                       kQramWidthAnnotationReservedId));
    }
}

// ── Rule 2 positive: subscript on std::array<qint_t<8>, 4> → 8 ──────────────
static void test_rule2_rhs_driven() {
    auto pr = parse(
        "void demo(qint i) { array<sturm::qint_t<8>, 4> a; "
        "qint b = a[i]; (void)b; }\n");
    CHECK(pr.ast != nullptr);
    if (!pr.ast) return;
    const auto* vd = find_var(*pr.ast, "b");
    CHECK(vd != nullptr);
    if (!vd) return;
    Harness h(*pr.ast);
    InferContext ctx{h.engine.get(), kDefaultWidth, kAllowAnnotation};
    CHECK_EQ_INT(infer_width(*vd, ctx), 8u);
    CHECK_EQ_INT(h.counter->errors, 0);
}

// ── Rule 3 positive: bare `qint b;` falls through to default ────────────────
static void test_rule3_default() {
    auto pr = parse("void demo() { qint b; (void)b; }\n");
    CHECK(pr.ast != nullptr);
    if (!pr.ast) return;
    const auto* vd = find_var(*pr.ast, "b");
    CHECK(vd != nullptr);
    if (!vd) return;
    Harness h(*pr.ast);
    InferContext ctx{h.engine.get(), kDefaultWidth, kAllowAnnotation};
    CHECK_EQ_INT(infer_width(*vd, ctx), kDefaultWidth);
    CHECK_EQ_INT(h.counter->errors, 0);
    CHECK_EQ_INT(h.counter->notes, 0);
}

// ── Rule 2 ambiguity (negative): two distinct widths fire mismatch ──────────
//
// `infer_width` walks the initializer subtree, finds two subscripts on
// containers of distinct `qint_t<W_e>`, and fires the
// `qram-width-mismatch` Error plus one `Note` per candidate. Asserts:
//   - Diag id token appears in the error text.
//   - Both candidate widths appear in the error message.
//   - Source range identifies the LHS VarDecl (the formatted message
//     surfaces the variable name `mismatched_target` via %0).
//   - One Note per candidate (PRD §11.3 / D0c.2).
//   - Falls through to rule 3 default.
static void test_rule2_ambiguity_diag() {
    auto pr = parse(
        "void demo(qint i) {\n"
        "    sturm::qint_t<8>  a8[4];\n"
        "    sturm::qint_t<16> a16[4];\n"
        "    qint mismatched_target = pick_two(a8[i], a16[i]);\n"
        "    (void)mismatched_target;\n"
        "}\n");
    CHECK(pr.ast != nullptr);
    if (!pr.ast) return;
    const auto* vd = find_var(*pr.ast, "mismatched_target");
    CHECK(vd != nullptr);
    if (!vd) return;
    Harness h(*pr.ast);
    InferContext ctx{h.engine.get(), kDefaultWidth, kAllowAnnotation};
    CHECK_EQ_INT(infer_width(*vd, ctx), kDefaultWidth);
    CHECK_EQ_INT(h.counter->errors, 1);
    CHECK_EQ_INT(h.counter->notes, 2);
    if (!h.counter->error_texts.empty()) {
        const auto& msg = h.counter->error_texts[0];
        CHECK(contains(msg, kQramWidthMismatchId));
        CHECK(contains(msg, "8"));
        CHECK(contains(msg, "16"));
        CHECK(contains(msg, "mismatched_target"));
    }
}

// ── Null-diag safety: ambiguity must NOT crash with diag == nullptr ─────────
static void test_null_diag_safe() {
    auto pr = parse(
        "void demo(qint i) {\n"
        "    sturm::qint_t<8>  a8[4];\n"
        "    sturm::qint_t<32> a32[4];\n"
        "    qint b = pick_two(a8[i], a32[i]);\n"
        "    (void)b;\n"
        "}\n");
    CHECK(pr.ast != nullptr);
    if (!pr.ast) return;
    const auto* vd = find_var(*pr.ast, "b");
    CHECK(vd != nullptr);
    if (!vd) return;
    InferContext ctx{nullptr, kDefaultWidth, kAllowAnnotation};
    CHECK_EQ_INT(infer_width(*vd, ctx), kDefaultWidth);
}

}  // namespace sturm_test_width_inference_ns

int run_test_width_inference(int /*argc*/, char** /*argv*/) {
    using namespace sturm_test_width_inference_ns;
    using sturm_test_width_inference_ns::tests_run;
    using sturm_test_width_inference_ns::tests_pass;
    test_rule1_annotation_reserved();
    test_rule2_rhs_driven();
    test_rule3_default();
    test_rule2_ambiguity_diag();
    test_null_diag_safe();
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
