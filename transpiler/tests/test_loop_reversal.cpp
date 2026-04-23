// test_loop_reversal.cpp — Phase S S-A (sturm-ha2k.2) unit tests for
// `reverse_for_header`.
//
// The module under test consumes a `clang::ForStmt` belonging to a
// reversible routine body and produces the reversed-iteration adjoint
// header text. Three properties anchor the test:
//
//   1. Stride-aware bound computation. Stride 1 (canonical), stride 2
//      (positive non-1), and negative-stride forwards each produce
//      the matching reversed-iteration header shape.
//
//   2. Zero-stride rejection. A forward `for` whose increment
//      evaluates to stride == 0 (e.g. `i += 0` or `i -= 0`) is a
//      no-progress loop whose trip count is undefined; S-A refuses
//      to reverse it and surfaces `ZeroStride`.
//
//   3. Non-canonical shape rejection. At least one shape outside the
//      supported set (compound condition, side-effecting increment)
//      rejects with the matching reason code — the caller (S-B /
//      R-C) is expected to route the reason through `DiagContext`.
//
// Harness posture
// ---------------
// Each case compiles a small C++ snippet via
// `clang::tooling::runToolOnCodeWithArgs`, locates the first
// ForStmt inside a named enclosing function, and invokes
// `reverse_for_header` on it. The expected header is asserted
// byte-for-byte via `CHECK_EQ_STR`, and the expected reject reasons
// are asserted via the public `LoopRejectReason` enum.

#include "loop_reversal.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using sturm::transpile::LoopRejectReason;
using sturm::transpile::LoopReversalResult;
using sturm::transpile::reverse_for_header;
using sturm::transpile::to_string;

// ── Test harness ────────────────────────────────────────────────────────────
// Local CHECK macros mirror the shape every sibling transpiler test
// uses. Kept local so the binary stays self-contained.
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

// Find the first ForStmt inside a named FunctionDecl.
class ForFinder : public clang::RecursiveASTVisitor<ForFinder> {
public:
    explicit ForFinder(std::string fn_name) : fn_name_(std::move(fn_name)) {}
    bool VisitFunctionDecl(clang::FunctionDecl* fd) {
        if (found_ != nullptr) return true;
        if (fd == nullptr) return true;
        if (fd->getNameAsString() != fn_name_) return true;
        if (fd->isTemplateInstantiation()) return true;
        if (!fd->hasBody()) return true;

        struct Scanner : clang::RecursiveASTVisitor<Scanner> {
            const clang::ForStmt* first = nullptr;
            bool VisitForStmt(clang::ForStmt* fs) {
                if (first == nullptr && fs != nullptr) first = fs;
                return true;
            }
        };
        Scanner s;
        s.TraverseStmt(fd->getBody());
        found_ = s.first;
        return false;
    }
    const clang::ForStmt* found() const { return found_; }
private:
    std::string fn_name_;
    const clang::ForStmt* found_ = nullptr;
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
        factory.create(), std::string(src), args, "loop_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false — snippet failed "
                     "to parse\n");
    }
    return ok;
}

// Helper: run `reverse_for_header` on the first ForStmt of `fn_name`
// and store the result in `out`.
void reverse_first_for(std::string_view src,
                       std::string fn_name,
                       LoopReversalResult* out) {
    bool ran = run_on(src, [&, fn_name](clang::ASTContext& ctx) {
        ForFinder f(fn_name);
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;
        *out = reverse_for_header(f.found(),
                                  ctx.getSourceManager(),
                                  ctx.getLangOpts());
    });
    CHECK(ran);
}

// ── (1) Stride-aware reversed-header synthesis ─────────────────────────────

void test_null_stmt_rejects() {
    // Synthesise a SourceManager + LangOpts via a minimal snippet so
    // the function's null-pointer short-circuit still dereferences
    // valid references when it does not exercise the early return.
    // (The early return does not read sm / lang, but the call-site
    // constructs real references regardless.)
    constexpr std::string_view src = "void x() {}";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        LoopReversalResult r = reverse_for_header(
            nullptr, ctx.getSourceManager(), ctx.getLangOpts());
        CHECK_FALSE(r.reversed);
        CHECK(r.reason == LoopRejectReason::NullStmt);
        CHECK(r.header.empty());
        CHECK(r.induction_var.empty());
    });
    CHECK(ran);
}

void test_stride_1_ascending_exclusive() {
    // `for (int i = 0; i < N; ++i)` → reversed walks from
    // `0 + ((N - 1 - 0) / 1) * 1` back down to 0 in -1 steps.
    constexpr std::string_view src = R"CPP(
void ripple(int N) {
    for (int i = 0; i < N; ++i) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "ripple", &r);
    CHECK(r.reversed);
    CHECK(r.reason == LoopRejectReason::None);
    CHECK_EQ_STR(r.induction_var, std::string("i"));
    CHECK_EQ_STR(
        r.header,
        std::string(
            "for (int i = (0) + (((N) - 1 - (0)) / (1)) * (1); "
            "i >= (0); i -= (1))"));
}

void test_stride_1_ascending_inclusive() {
    // `for (int i = 0; i <= N; ++i)` → reversed walks from
    // `0 + ((N - 0) / 1) * 1` back to 0. No `-1` in the last-value
    // arithmetic because the forward included the upper bound.
    constexpr std::string_view src = R"CPP(
void ripple_incl(int N) {
    for (int i = 0; i <= N; ++i) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "ripple_incl", &r);
    CHECK(r.reversed);
    CHECK(r.reason == LoopRejectReason::None);
    CHECK_EQ_STR(
        r.header,
        std::string(
            "for (int i = (0) + (((N) - (0)) / (1)) * (1); "
            "i >= (0); i -= (1))"));
}

void test_stride_2_positive() {
    // `for (int i = 0; i < N; i += 2)` → reversed walks from the
    // largest multiple-of-2 less than N, back down to 0 in -2 steps.
    // This pins the stride-aware arithmetic from PRD §5.2.
    constexpr std::string_view src = R"CPP(
void ripple_s2(int N) {
    for (int i = 0; i < N; i += 2) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "ripple_s2", &r);
    CHECK(r.reversed);
    CHECK(r.reason == LoopRejectReason::None);
    CHECK_EQ_STR(
        r.header,
        std::string(
            "for (int i = (0) + (((N) - 1 - (0)) / (2)) * (2); "
            "i >= (0); i -= (2))"));
}

void test_stride_negative_decrementing_exclusive() {
    // `for (int i = N; i > 0; --i)` → forward walks N, N-1, ..., 1.
    // Reversed walks from 1 back up to N in +1 steps:
    //   last = N - ((N - 0 - 1) / 1) * 1
    constexpr std::string_view src = R"CPP(
void down(int N) {
    for (int i = N; i > 0; --i) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "down", &r);
    CHECK(r.reversed);
    CHECK(r.reason == LoopRejectReason::None);
    CHECK_EQ_STR(r.induction_var, std::string("i"));
    CHECK_EQ_STR(
        r.header,
        std::string(
            "for (int i = (N) - (((N) - (0) - 1) / (1)) * (1); "
            "i <= (N); i += (1))"));
}

void test_stride_negative_via_minus_equals() {
    // `for (int i = N; i > 0; i -= 3)` → reversed walks upward in
    // +3 steps. Pins that `i -= C` parses as stride = -C, and the
    // reversed shape uses the absolute magnitude.
    constexpr std::string_view src = R"CPP(
void down3(int N) {
    for (int i = N; i > 0; i -= 3) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "down3", &r);
    CHECK(r.reversed);
    CHECK(r.reason == LoopRejectReason::None);
    CHECK_EQ_STR(
        r.header,
        std::string(
            "for (int i = (N) - (((N) - (0) - 1) / (3)) * (3); "
            "i <= (N); i += (3))"));
}

void test_stride_negative_via_plus_equals_negative_literal() {
    // `for (int i = N; i > 0; i += -2)` → the literal `-2` parses as
    // a UnaryOperator(-) wrapping IntLit(2); `EvaluateAsInt` returns
    // -2, so stride = -2 and the reversed shape is upward with
    // magnitude 2. Symmetric to `i -= 2`.
    constexpr std::string_view src = R"CPP(
void downNeg(int N) {
    for (int i = N; i > 0; i += -2) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "downNeg", &r);
    CHECK(r.reversed);
    CHECK(r.reason == LoopRejectReason::None);
    CHECK_EQ_STR(
        r.header,
        std::string(
            "for (int i = (N) - (((N) - (0) - 1) / (2)) * (2); "
            "i <= (N); i += (2))"));
}

void test_stride_zero_rejects_via_plus_equals() {
    // `for (int i = 0; i < N; i += 0)` → stride evaluates to 0;
    // the forward is a no-progress loop, not reversible.
    constexpr std::string_view src = R"CPP(
void noop(int N) {
    for (int i = 0; i < N; i += 0) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "noop", &r);
    CHECK_FALSE(r.reversed);
    CHECK(r.reason == LoopRejectReason::ZeroStride);
    CHECK(r.header.empty());
}

void test_stride_zero_rejects_via_minus_equals() {
    // Symmetric: `i -= 0` also yields stride == 0.
    constexpr std::string_view src = R"CPP(
void noop2(int N) {
    for (int i = 0; i < N; i -= 0) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "noop2", &r);
    CHECK_FALSE(r.reversed);
    CHECK(r.reason == LoopRejectReason::ZeroStride);
}

// ── (2) Non-canonical shape rejections ─────────────────────────────────────

void test_compound_condition_rejects() {
    // `for (int i = 0; i < N && flag; ++i)` → the condition is a
    // `BinaryOperator(&&)` whose LHS is the actual comparison.
    // S-A's single-comparison guard refuses.
    constexpr std::string_view src = R"CPP(
void compound(int N, bool flag) {
    for (int i = 0; i < N && flag; ++i) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "compound", &r);
    CHECK_FALSE(r.reversed);
    CHECK(r.reason == LoopRejectReason::NonCanonicalCond);
}

void test_side_effecting_increment_rejects() {
    // `for (int i = 0; i < N; i = foo(i))` → the increment is an
    // assignment whose RHS is a CallExpr, not a literal-offset
    // BinaryOperator. S-A refuses.
    constexpr std::string_view src = R"CPP(
int foo(int x) { return x + 1; }
void sideff(int N) {
    for (int i = 0; i < N; i = foo(i)) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "sideff", &r);
    CHECK_FALSE(r.reversed);
    CHECK(r.reason == LoopRejectReason::NonCanonicalInc);
}

void test_multi_decl_init_rejects() {
    // `for (int i = 0, j = 0; ... ; ...)` → DeclStmt with multiple
    // declarations. S-A's single-VarDecl guard refuses.
    constexpr std::string_view src = R"CPP(
void multidecl(int N) {
    for (int i = 0, j = 0; i < N; ++i) { (void)j; }
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "multidecl", &r);
    CHECK_FALSE(r.reversed);
    CHECK(r.reason == LoopRejectReason::NonCanonicalInit);
}

void test_non_integer_init_rejects() {
    // `for (float i = 0.0f; ...)` → non-integer induction variable.
    // S-A is integer-only.
    constexpr std::string_view src = R"CPP(
void floaty() {
    for (float i = 0.0f; i < 1.0f; i += 0.1f) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "floaty", &r);
    CHECK_FALSE(r.reversed);
    CHECK(r.reason == LoopRejectReason::NonCanonicalInit);
}

void test_missing_clause_rejects() {
    // `for (;;)` → the forward has no init / cond / inc clauses.
    // This is an infinite loop; trip count is undefined; not
    // reversible.
    constexpr std::string_view src = R"CPP(
void infinite() {
    for (;;) { break; }
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "infinite", &r);
    CHECK_FALSE(r.reversed);
    CHECK(r.reason == LoopRejectReason::MissingClause);
}

void test_non_comparison_condition_rejects() {
    // `for (int i = 0; i; ++i)` → bare DeclRefExpr is not a
    // comparison. S-A rejects.
    constexpr std::string_view src = R"CPP(
void plainref(int N) {
    for (int i = 0; i; ++i) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "plainref", &r);
    CHECK_FALSE(r.reversed);
    CHECK(r.reason == LoopRejectReason::NonCanonicalCond);
}

// ── (3) Assignment-shape increments (i = i + C / C + i / i - C) ────────────

void test_assign_plus_literal() {
    // `for (int i = 0; i < N; i = i + 2)` → equivalent to `i += 2`.
    constexpr std::string_view src = R"CPP(
void plus2(int N) {
    for (int i = 0; i < N; i = i + 2) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "plus2", &r);
    CHECK(r.reversed);
    CHECK(r.reason == LoopRejectReason::None);
    CHECK_EQ_STR(
        r.header,
        std::string(
            "for (int i = (0) + (((N) - 1 - (0)) / (2)) * (2); "
            "i >= (0); i -= (2))"));
}

void test_assign_literal_plus_i() {
    // `i = 3 + i` — symmetric to `i += 3`.
    constexpr std::string_view src = R"CPP(
void swapped(int N) {
    for (int i = 0; i < N; i = 3 + i) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "swapped", &r);
    CHECK(r.reversed);
    CHECK(r.reason == LoopRejectReason::None);
    CHECK_EQ_STR(
        r.header,
        std::string(
            "for (int i = (0) + (((N) - 1 - (0)) / (3)) * (3); "
            "i >= (0); i -= (3))"));
}

void test_assign_minus_literal() {
    // `i = i - 2` — equivalent to `i -= 2`.
    constexpr std::string_view src = R"CPP(
void minus2(int N) {
    for (int i = N; i > 0; i = i - 2) {}
}
)CPP";
    LoopReversalResult r;
    reverse_first_for(src, "minus2", &r);
    CHECK(r.reversed);
    CHECK(r.reason == LoopRejectReason::None);
    CHECK_EQ_STR(
        r.header,
        std::string(
            "for (int i = (N) - (((N) - (0) - 1) / (2)) * (2); "
            "i <= (N); i += (2))"));
}

// ── (4) Reason-code stringification ────────────────────────────────────────

void test_reason_to_string_stable() {
    // Spellings are part of the public contract — the diagnostic
    // surface (when S-B / R-C wires in rejection reporting) and
    // tests compare against these exact strings.
    CHECK_EQ_STR(std::string(to_string(LoopRejectReason::None)),
                 std::string("none"));
    CHECK_EQ_STR(std::string(to_string(LoopRejectReason::NullStmt)),
                 std::string("null_stmt"));
    CHECK_EQ_STR(std::string(to_string(LoopRejectReason::MissingClause)),
                 std::string("missing_clause"));
    CHECK_EQ_STR(std::string(to_string(LoopRejectReason::NonCanonicalInit)),
                 std::string("non_canonical_init"));
    CHECK_EQ_STR(std::string(to_string(LoopRejectReason::NonCanonicalCond)),
                 std::string("non_canonical_cond"));
    CHECK_EQ_STR(std::string(to_string(LoopRejectReason::NonCanonicalInc)),
                 std::string("non_canonical_inc"));
    CHECK_EQ_STR(std::string(to_string(LoopRejectReason::ZeroStride)),
                 std::string("zero_stride"));
    CHECK_EQ_STR(std::string(
                     to_string(LoopRejectReason::SourceRecoveryFailed)),
                 std::string("source_recovery_failed"));
}

} // namespace

int main() {
    // Reject-gate.
    test_null_stmt_rejects();
    test_missing_clause_rejects();

    // Stride 1 (canonical).
    test_stride_1_ascending_exclusive();
    test_stride_1_ascending_inclusive();

    // Stride 2 (positive non-1).
    test_stride_2_positive();

    // Negative stride (both --/i-- and i -= C / i += -C shapes).
    test_stride_negative_decrementing_exclusive();
    test_stride_negative_via_minus_equals();
    test_stride_negative_via_plus_equals_negative_literal();

    // Zero stride.
    test_stride_zero_rejects_via_plus_equals();
    test_stride_zero_rejects_via_minus_equals();

    // Non-canonical shape rejections.
    test_compound_condition_rejects();
    test_side_effecting_increment_rejects();
    test_multi_decl_init_rejects();
    test_non_integer_init_rejects();
    test_non_comparison_condition_rejects();

    // Assignment-shape increments (i = i + C / i = C + i / i = i - C).
    test_assign_plus_literal();
    test_assign_literal_plus_i();
    test_assign_minus_literal();

    // Reason stringification.
    test_reason_to_string_stable();

    std::fprintf(stderr,
                 "test_loop_reversal: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
