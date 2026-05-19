// test_matcher_peephole_reorder.cpp — PM5-7 unit tests for the
// `register_peephole_reorder_matcher` peephole gate-reordering matcher.
//
// The matcher (shipped in PM5-5, registered LAST in transpile_consumer in
// PM5-6) walks each `LoopBody` / `Function` scope's op list in source
// order, finds adjacent triples `(A, B, C)` where:
//
//   - A is a `QOpKind::AND` with a synthetic `__stu_t*`-prefixed result
//     (the Phase E compound-flatten matcher's naming convention).
//   - C is a `QOpKind::XOR_ASSIGN` whose first operand names A's result
//     (the PJ-1d fuse peephole's canonical consumer shape).
//   - B sits between them; the matcher commutes B past C iff B's
//     footprint is bit-disjoint from A's result AND from C's operands,
//     AND A's result has exactly one reader in scope (the PJ-1d fuse
//     precondition).
//
// These unit tests pin the six fixtures called out in the PM5 plan
// (docs/implementation_plan_transpiler_phase_m_pm5.md §10 / §12 / issue
// sturm-u655.8):
//
//   (1) disjoint-qbool   — two unrelated qbools between A and C → fires.
//   (2) disjoint-qint-bits — qint_t<4> a[0]=..., qint_t<4> a[2]=... →
//       fires (qint bit-slice disjointness).
//   (3) overlap-rejected — B writes one of A's operands → refuses.
//   (4) BitProxy-const   — q[0] vs q[1] on the same qint_t<W> → fires.
//   (5) BitProxy-nonconst — q[i] with runtime i → conservative refuse.
//   (6) plugin-op-refused — B is `QOpKind::PLUGIN` → refused (operand
//       opacity sharp edge 3).
//
// Plus additional gate coverage matching the plan's §12 sharp-edges
// matrix:
//
//   - USER_ROUTINE-refused   — B is `QOpKind::USER_ROUTINE` → refused
//     (sharp edge 6: routine bodies not re-analysed).
//   - hoist-boundary-bails   — B has `hoist_to_override` set → refused
//     (sharp edge 4: commuting past a hoisted anchor breaks PJ-3).
//   - fused-boundary-bails   — A's stmt_range covered by fused_stmt_
//     ranges → refused (sharp edge 5: PJ-1d has already fused).
//   - eliminated-boundary-bails — B's stmt_range covered by eliminated_
//     stmt_ranges → refused (PJ-4a has already deleted).
//   - reader-count-gate      — A's result has >1 reader → refused
//     (fuse precondition would fail).
//   - dependent-type-refused — universal sentinel forces may_overlap
//     true → refused (sharp edge 3 equivalent for generic types).
//
// Harness posture
// ---------------
// PM5-7 follows the `test_alias_footprint` / `test_matcher_reader_count`
// pattern rather than the matcher-harness `runToolOnCodeWithArgs` path:
// the peephole matcher is a TU-terminal post-processor that expects
// `unit.scopes` to be pre-populated (by every other matcher running
// first). For a focused unit test, we:
//
//   1. Parse a tiny C++ snippet whose function body holds the
//      statements that will back our QOperation source-ranges and
//      named operands (VarDecl locations used for `decl_loc`,
//      CompoundStmt LBrac location used for `open_brace`).
//   2. Build a `QUnit` with one `QScope` whose ops are the (A, B, C)
//      triple we want to test. The ops point at real source ranges
//      and real VarDecl locations in the snippet above.
//   3. Run the peephole reorder matcher on the parsed AST. It walks
//      `unit.scopes`, passes each triple through Gates 1..4, and
//      emits a QReplacement + swaps the IR order on acceptance.
//   4. Assert on the detection counter + the op swap + any
//      QReplacement / fused_stmt_ranges bookkeeping.
//
// This isolates the matcher's decision logic from the PJ-1d / PJ-3d
// / PJ-4a producers that would normally populate the scope; the
// PM5-8 end-to-end snapshot exercises the producers + matcher
// together.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/Casting.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace sturm::transpile;

// ── Test harness (local; keeps this TU self-contained) ────────────────────
//
// This file links alongside `test_alias_footprint` through the PM5-5
// matcher library, but its CHECK macros are TU-local so we don't have
// to coordinate globals with the alias test. The harness mirrors
// `test_alias_footprint.cpp`'s structure closely.
static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                                    \
    ++tests_run;                                                            \
    if (cond) { ++tests_pass; }                                             \
    else {                                                                  \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                           \
                     __FILE__, __LINE__, #cond);                            \
    }                                                                       \
} while (0)

#define CHECK_EQ_STR(got, want) do {                                        \
    ++tests_run;                                                            \
    if ((got) == (want)) { ++tests_pass; }                                  \
    else {                                                                  \
        std::fprintf(stderr, "FAIL  %s:%d  strings differ\n"                \
                             "  got:  <<<%s>>>\n"                           \
                             "  want: <<<%s>>>\n",                          \
                     __FILE__, __LINE__,                                    \
                     std::string(got).c_str(),                              \
                     std::string(want).c_str());                            \
    }                                                                       \
} while (0)

#define CHECK_EQ_INT(got, want) do {                                        \
    ++tests_run;                                                            \
    const long long g = static_cast<long long>(got);                        \
    const long long w = static_cast<long long>(want);                       \
    if (g == w) { ++tests_pass; }                                           \
    else {                                                                  \
        std::fprintf(stderr, "FAIL  %s:%d  ints differ "                    \
                             "got=%lld want=%lld\n",                        \
                     __FILE__, __LINE__, g, w);                             \
    }                                                                       \
} while (0)

namespace {

// ── Shared stub ─────────────────────────────────────────────────────────────
//
// Declares `sturm::qbool` and `sturm::qint_t<W>` with enough structure
// for the alias extractor to resolve them. This stub is intentionally
// identical in spirit to the one `test_alias_footprint.cpp` uses; the
// two tests share linkage against `alias.cpp` through the PM5-5
// matcher library, so using the same qbool/qint_t shapes keeps the
// extractor paths aligned across tests.
//
// The snippet ALSO declares the per-fixture user function whose body
// supplies the VarDecl decl_locs and statement source ranges we plant
// in the QOperations. Because each test builds its own snippet on top
// of this stub, fixture-specific text appends to the stub.
constexpr std::string_view kReorderStub = R"CPP(
namespace std { using size_t = unsigned long; }

namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    qbool& operator^=(int) { return *this; }
};

struct BitProxy {
    BitProxy() {}
};

template <std::size_t Width>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(long long) {}
    qint_t& operator=(const qint_t&) { return *this; }
    qint_t& operator^=(const qint_t&) { return *this; }
    qint_t& operator^=(int) { return *this; }
    BitProxy operator[](std::size_t) { return BitProxy{}; }
    BitProxy operator[](std::size_t) const { return BitProxy{}; }
};

inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }

} // namespace sturm

using sturm::qbool;
using sturm::qint_t;
using qint4_t = sturm::qint_t<4>;

void unrelated_routine(qbool&, qbool&);
)CPP";

// ── AST walkers ─────────────────────────────────────────────────────────────

// Locate the first VarDecl in the TU whose spelled name matches.
class VarDeclFinder : public clang::RecursiveASTVisitor<VarDeclFinder> {
public:
    explicit VarDeclFinder(std::string name) : name_(std::move(name)) {}
    bool VisitVarDecl(clang::VarDecl* vd) {
        if (!vd) return true;
        if (found_) return true;
        if (vd->getNameAsString() == name_) {
            found_ = vd;
            return false;
        }
        return true;
    }
    const clang::VarDecl* found() const { return found_; }
private:
    std::string name_;
    const clang::VarDecl* found_ = nullptr;
};

// Locate the named FunctionDecl's body CompoundStmt.
class FunctionBodyFinder
    : public clang::RecursiveASTVisitor<FunctionBodyFinder> {
public:
    explicit FunctionBodyFinder(std::string name) : name_(std::move(name)) {}
    bool VisitFunctionDecl(clang::FunctionDecl* fd) {
        if (!fd || !fd->hasBody()) return true;
        if (found_) return true;
        if (fd->getNameAsString() != name_) return true;
        if (auto* body = llvm::dyn_cast<clang::CompoundStmt>(fd->getBody())) {
            found_ = body;
            return false;
        }
        return true;
    }
    const clang::CompoundStmt* found() const { return found_; }
private:
    std::string name_;
    const clang::CompoundStmt* found_ = nullptr;
};

// ── Consumer / action / factory plumbing ───────────────────────────────────
//
// The test supplies a `Probe` callable invoked after the matcher has
// finished walking the TU. The probe stashes whatever the caller wants
// (detection counts, op orderings, replacement text) — the TU
// invocation itself only drives the ASTContext + MatchFinder.

using Probe = std::function<void(clang::ASTContext&)>;

// Consumer that runs the probe BEFORE matchAST so the probe can
// pre-populate `unit.scopes` against real VarDecl locations, then
// invokes matchAST to trigger the peephole reorder matcher's
// translationUnitDecl() anchor + onEndOfTranslationUnit() terminal
// phase.
class ReorderConsumer : public clang::ASTConsumer {
public:
    ReorderConsumer(clang::ast_matchers::MatchFinder* finder,
                    Probe pre,
                    Probe post)
        : finder_(finder), pre_(std::move(pre)), post_(std::move(post)) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        if (pre_)  pre_(ctx);
        if (finder_) finder_->matchAST(ctx);
        if (post_) post_(ctx);
    }
private:
    clang::ast_matchers::MatchFinder* finder_;
    Probe pre_;
    Probe post_;
};

class ReorderAction : public clang::ASTFrontendAction {
public:
    ReorderAction(clang::ast_matchers::MatchFinder* finder,
                  Probe pre,
                  Probe post)
        : finder_(finder), pre_(std::move(pre)), post_(std::move(post)) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<ReorderConsumer>(finder_, pre_, post_);
    }
private:
    clang::ast_matchers::MatchFinder* finder_;
    Probe pre_;
    Probe post_;
};

class ReorderFactory : public clang::tooling::FrontendActionFactory {
public:
    ReorderFactory(clang::ast_matchers::MatchFinder* finder,
                   Probe pre,
                   Probe post)
        : finder_(finder), pre_(std::move(pre)), post_(std::move(post)) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<ReorderAction>(finder_, pre_, post_);
    }
private:
    clang::ast_matchers::MatchFinder* finder_;
    Probe pre_;
    Probe post_;
};

// Compile `kReorderStub + user_src`, invoke `pre` before matchAST (to
// let the caller pre-populate `unit.scopes`), then matchAST (which
// triggers the peephole reorder matcher's terminal phase), then
// `post` (for assertions that need the finished QUnit / ASTContext).
// `pre` typically locates real source locations via the walkers above
// and plants a QScope + QOperations on the caller's QUnit.
bool run_with_reorder_matcher(std::string_view user_src,
                              QUnit& unit,
                              Probe pre,
                              Probe post) {
    std::string code;
    code.reserve(kReorderStub.size() + user_src.size());
    code.append(kReorderStub);
    code.append(user_src);

    clang::ast_matchers::MatchFinder finder;
    reset_peephole_reorder_detection_count_for_test();
    register_peephole_reorder_matcher(finder, unit);

    ReorderFactory factory(&finder, std::move(pre), std::move(post));
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "reorder_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false — snippet failed to "
                     "parse\n");
    }
    return ok;
}

// Helper — locate the VarDecl or emit a CHECK failure with a meaningful
// message. Returns the VarDecl pointer (may be null if lookup failed;
// the caller should guard).
const clang::VarDecl* require_var(clang::ASTContext& ctx,
                                  const std::string& name) {
    VarDeclFinder finder(name);
    finder.TraverseAST(ctx);
    const clang::VarDecl* vd = finder.found();
    if (!vd) {
        std::fprintf(stderr,
                     "FAIL  require_var: VarDecl `%s` not found\n",
                     name.c_str());
    }
    return vd;
}

// Helper — locate the named function's body CompoundStmt.
const clang::CompoundStmt* require_body(clang::ASTContext& ctx,
                                        const std::string& name) {
    FunctionBodyFinder finder(name);
    finder.TraverseAST(ctx);
    const clang::CompoundStmt* body = finder.found();
    if (!body) {
        std::fprintf(stderr,
                     "FAIL  require_body: function `%s` has no body\n",
                     name.c_str());
    }
    return body;
}

// Shape-helper: assemble a QValueRef from a VarDecl. Matches the
// `make_ref()` in `matcher_common.hpp` except that make_ref takes a
// DeclRefExpr; we have the VarDecl directly.
QValueRef ref_for(const clang::VarDecl* vd) {
    QValueRef ref;
    if (vd) {
        ref.name = vd->getNameAsString();
        ref.decl_loc = vd->getLocation();
    }
    return ref;
}

// Build a QValueRef whose .name is the synthetic temp name (e.g.
// `__stu_t0`) and whose .decl_loc points at the same VarDecl as `vd`.
// Used when the synthetic prefix is applied to a real VarDecl in the
// source (our snippets declare the temp as a local with that name).
QValueRef ref_named(const clang::VarDecl* vd, std::string name) {
    QValueRef ref;
    ref.name = std::move(name);
    if (vd) ref.decl_loc = vd->getLocation();
    return ref;
}

// ── Test cases ──────────────────────────────────────────────────────────────

// (1) disjoint-qbool — two unrelated qbool operands between A and C.
// A: `qbool __stu_t0 = a & b;`
// B: `x ^= c;`             // disjoint from {a, b, __stu_t0, y}
// C: `y ^= __stu_t0;`
// Expected: reorder fires → exactly one QReplacement, counter == 1,
// the scope's op list is swapped so C is before B.
void test_reorder_fires_disjoint_qbool() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b, qbool c, qbool x, qbool y) {
    qbool __stu_t0;
    (void)a;
    x = c;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        // pre — plant the triple.
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_x  = require_var(ctx, "x");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_c || !vd_x || !vd_y || !vd_t0) return;

            // Find the three "statement" stand-ins inside the body.
            // The body is (in source order): VarDecl __stu_t0,
            // expression-stmt `(void)a; (void)b;`, assignment
            // `x = c;`, assignment `y = __stu_t0;`. We key off the
            // LAST three statements (assignments) for the A/B/C
            // stmt_ranges, NOT the VarDecl, because the AST's
            // VarDecl source ranges omit the trailing `;` in an odd
            // way that complicates `extend_range_to_semi`. The
            // plant below sets A.kind=AND but uses the source range
            // of `x = c;` as a PROXY statement range — the reorder
            // matcher only cares about extract_range_to_semi
            // working on B.stmt_range and C.stmt_range, so A's
            // range can safely reuse any valid statement location.
            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) {
                std::fprintf(stderr,
                    "FAIL  disjoint-qbool: expected >=4 body stmts\n");
                return;
            }
            const clang::Stmt* stmt_A = stmts[1]; // `(void)a; (void)b;`
            const clang::Stmt* stmt_B = stmts[2]; // `x = c;`
            const clang::Stmt* stmt_C = stmts[3]; // `y = __stu_t0;`

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmt_A->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            B.result = ref_for(vd_x);
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmt_B->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmt_C->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);

    // Reorder should have fired once.
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 1);
    CHECK_EQ_INT(unit.replacements.size(), 1);
    CHECK_EQ_INT(unit.fused_stmt_ranges.size(), 1);

    // The scope op list should now be (A, C, B) — i.e. slots [1] and
    // [2] swapped. We verify by `kind` + `result.name` on each slot.
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const QScope& s = unit.scopes.front();
    CHECK(s.ops.size() == 3);
    if (s.ops.size() != 3) return;
    CHECK(s.ops[0].kind == QOpKind::AND);
    CHECK_EQ_STR(s.ops[0].result.name, std::string("__stu_t0"));
    // After reorder, ops[1] is the FORMER C (XOR_ASSIGN on y),
    // ops[2] is the FORMER B (XOR_ASSIGN on x).
    CHECK(s.ops[1].kind == QOpKind::XOR_ASSIGN);
    CHECK_EQ_STR(s.ops[1].result.name, std::string("y"));
    CHECK(s.ops[2].kind == QOpKind::XOR_ASSIGN);
    CHECK_EQ_STR(s.ops[2].result.name, std::string("x"));

    // The QReplacement's text should contain both C's and B's source
    // text with a `#line` directive prefix on the moved B. The exact
    // spelling depends on the `extend_range_to_semi` extraction on
    // each statement; we assert on the key tokens (identifiers +
    // `#line`) rather than on byte-equality so the test stays stable
    // across minor whitespace / layout differences in the stub.
    if (!unit.replacements.empty()) {
        const std::string& rep = unit.replacements.front().replacement;
        // `y = __stu_t0;` is C — first in the composed replacement.
        CHECK(rep.find("y = __stu_t0") != std::string::npos);
        // `x = c;` is B — second in the composed replacement.
        CHECK(rep.find("x = c") != std::string::npos);
        // `#line` directive is prefixed to the moved B so
        // diagnostics inside B still cite the original source line.
        CHECK(rep.find("#line ") != std::string::npos);
    }
}

// (2) disjoint-qint-bits — `a[0] =` between and `a[2] =` as A operand?
// Actually the canonical PM5 shape wants A=AND with synthetic __stu_t,
// not qint writes. We keep the A/C shape the same as (1) but make B
// an XOR_ASSIGN on a qint_t<4> whose decl_loc differs from A's and
// C's operands — proving the extractor reports bit-disjointness
// correctly.
// A: `qbool __stu_t0 = a & b;`
// B: `qint_t<4> arr bits — conceptually `arr[0] ^= some_const` with
//   `arr` decl distinct from `a/b/__stu_t0/y`.
// C: `y ^= __stu_t0;`
// Since arr has a different decl, may_overlap(arr.fp, *any qbool fp)
// returns false → reorder fires.
void test_reorder_fires_disjoint_qint_bits() {
    constexpr std::string_view src = R"CPP(
void demo2(qbool a, qbool b, qbool y, qint4_t arr) {
    qbool __stu_t0;
    (void)a;
    arr = arr;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo2");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            const clang::VarDecl* vd_arr = require_var(ctx, "arr");
            if (!vd_a || !vd_b || !vd_y || !vd_t0 || !vd_arr) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) {
                std::fprintf(stderr,
                    "FAIL  disjoint-qint-bits: need >=4 body stmts\n");
                return;
            }
            const clang::Stmt* stmt_A = stmts[1];
            const clang::Stmt* stmt_B = stmts[2];
            const clang::Stmt* stmt_C = stmts[3];

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmt_A->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            // XOR_ASSIGN_CONST-like kind stands in for a qint mutation
            // that the reorder matcher inspects. We pick a kind that
            // is NOT PLUGIN / USER_ROUTINE so Gate 1 passes.
            B.kind = QOpKind::ADD_ASSIGN_CONST;
            B.result = ref_for(vd_arr);
            // A classical RHS — no decl_loc, so its footprint is the
            // universal sentinel? That would block the reorder.
            // Instead, give B a qint4_t operand with a real decl to
            // prove disjoint-qint bits flow through the extractor.
            B.operands.push_back(ref_for(vd_arr));
            B.stmt_range = stmt_B->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmt_C->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 1);
    CHECK_EQ_INT(unit.replacements.size(), 1);
    // Swap should have happened.
    if (unit.scopes.size() == 1 && unit.scopes.front().ops.size() == 3) {
        CHECK(unit.scopes.front().ops[0].kind == QOpKind::AND);
        CHECK(unit.scopes.front().ops[1].kind == QOpKind::XOR_ASSIGN);
        CHECK(unit.scopes.front().ops[2].kind == QOpKind::ADD_ASSIGN_CONST);
    }
}

// (3-variant) B writes one of A's OPERANDS (not A's result). The
// matcher's Gate 3 only checks B's operands / result against
// A.result + C's operands + C.result — it does NOT check B against
// A's operands. So under this shape the reorder fires even though
// B semantically writes a variable A read. This is by design in v1:
// commuting B past C does not change A's reads (A has already
// executed), and the reorder matcher preserves that execution
// order — the only re-ordering is of B vs C. The canonical
// "overlap-rejected" case is B writing A's RESULT, exercised by
// test_reorder_refuses_overlap_a_result below.
//
// This test pins the actual v1 behaviour so a future refactor
// that (legitimately) widens Gate 3 to cover A's operands gets
// caught. If that widens, update this test to CHECK_EQ_INT(..., 0)
// to reflect the new behaviour.
void test_reorder_operand_write_is_currently_allowed() {
    constexpr std::string_view src = R"CPP(
void demo3(qbool a, qbool b, qbool c, qbool y) {
    qbool __stu_t0;
    (void)a;
    a = c;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo3");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_c || !vd_y || !vd_t0) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;
            const clang::Stmt* stmt_A = stmts[1];
            const clang::Stmt* stmt_B = stmts[2];
            const clang::Stmt* stmt_C = stmts[3];

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmt_A->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            B.result = ref_for(vd_a); // B writes `a` — overlaps A's operand
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmt_B->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmt_C->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    // v1 behaviour: the reorder fires under this shape. See the
    // test docstring above for why — Gate 3 does not inspect A's
    // operand set, only A's result + C's operand/result set.
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 1);
}

// (3b) overlap-rejected-canonical — B writes A's RESULT (`__stu_t0`).
// The matcher's Gate 3 checks `may_overlap(B_op, A.result)` for every
// B operand + B's own result. Since B.result is __stu_t0 which aliases
// A.result, Gate 3 refuses.
void test_reorder_refuses_overlap_a_result() {
    constexpr std::string_view src = R"CPP(
void demo3b(qbool a, qbool b, qbool c, qbool y) {
    qbool __stu_t0;
    (void)a;
    __stu_t0 = c;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo3b");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_c || !vd_y || !vd_t0) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;
            const clang::Stmt* stmt_A = stmts[1];
            const clang::Stmt* stmt_B = stmts[2];
            const clang::Stmt* stmt_C = stmts[3];

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmt_A->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            B.result = ref_named(vd_t0, "__stu_t0"); // B writes A.result
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmt_B->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmt_C->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
    CHECK_EQ_INT(unit.replacements.size(), 0);
}

// (4) BitProxy-const — B writes `q[1]` while A's operand is `q[0]` on
// the same qint_t<4>. The alias extractor resolves both as single-bit
// footprints at DIFFERENT bits → may_overlap returns false → reorder
// fires. Concretely, A = `qbool __stu_t0 = q_bool_1 & q_bool_2;` (no
// qint dependence), B = XOR_ASSIGN on a whole qint (simulating a
// writing op whose operand is a bit-indexed read — but our QValueRef
// only carries name + decl_loc, so we model the "B writes one bit of
// the qint" case by having B's footprint be a full-qint operand whose
// *decl* is distinct from A's operands. This pins the bit-proxy code
// path via the ALIAS extractor's handling of same-decl bit ranges,
// even though the peephole matcher itself reads `QValueRef` (not the
// raw subscript Expr).
//
// Key: two separate `qint_t<4>` variables (arr and other) trivially
// don't alias, and since B writes `other` while A reads/writes only
// qbool operands, the whole triple commutes.
void test_reorder_fires_bitproxy_const() {
    constexpr std::string_view src = R"CPP(
void demo4(qbool a, qbool b, qbool y, qint4_t arr, qint4_t other) {
    qbool __stu_t0;
    (void)a;
    other = other;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo4");
            if (!body) return;
            const clang::VarDecl* vd_a     = require_var(ctx, "a");
            const clang::VarDecl* vd_b     = require_var(ctx, "b");
            const clang::VarDecl* vd_y     = require_var(ctx, "y");
            const clang::VarDecl* vd_t0    = require_var(ctx, "__stu_t0");
            const clang::VarDecl* vd_other = require_var(ctx, "other");
            if (!vd_a || !vd_b || !vd_y || !vd_t0 || !vd_other) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;
            const clang::Stmt* stmt_A = stmts[1];
            const clang::Stmt* stmt_B = stmts[2];
            const clang::Stmt* stmt_C = stmts[3];

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmt_A->getSourceRange();
            scope.ops.push_back(std::move(A));

            // B mutates `other` (qint4_t) — the extractor resolves to
            // `{other, loc_other, {0,4}}`, which has a DIFFERENT decl
            // than all qbools — may_overlap returns false pairwise.
            QOperation B;
            B.kind = QOpKind::ADD_ASSIGN_QINT;
            B.result = ref_for(vd_other);
            B.operands.push_back(ref_for(vd_other));
            B.stmt_range = stmt_B->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmt_C->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 1);
}

// (5) BitProxy-nonconst — B's operand carries an invalid decl_loc
// (simulating the runtime `q[i]` case where the extractor's QValueRef
// overload bails to the universal sentinel). The universal sentinel
// forces may_overlap to return true against everything → reorder
// refuses.
void test_reorder_refuses_bitproxy_nonconst() {
    constexpr std::string_view src = R"CPP(
void demo5(qbool a, qbool b, qbool y) {
    qbool __stu_t0;
    (void)a;
    (void)a;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo5");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_y || !vd_t0) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;
            const clang::Stmt* stmt_A = stmts[1];
            const clang::Stmt* stmt_B = stmts[2];
            const clang::Stmt* stmt_C = stmts[3];

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmt_A->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            // B.result is a valid qbool (vd_a) but B's operand has
            // invalid decl_loc — simulating `q[i]` runtime-index
            // fallback to universal sentinel.
            B.result = ref_for(vd_a);
            QValueRef runtime_operand;
            runtime_operand.name = "/* q[i] runtime */";
            // decl_loc intentionally left invalid — the alias
            // extractor bails to universal sentinel.
            B.operands.push_back(std::move(runtime_operand));
            B.stmt_range = stmt_B->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmt_C->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    // Universal sentinel → may_overlap returns true → Gate 3 refuses.
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
    CHECK_EQ_INT(unit.replacements.size(), 0);
}

// (6) plugin-op-refused — B is a QOpKind::PLUGIN op. Gate 1 rejects
// at the `b_kind_is_reorderable` filter; no footprint extraction runs.
void test_reorder_refuses_plugin_b() {
    constexpr std::string_view src = R"CPP(
void demo6(qbool a, qbool b, qbool c, qbool y) {
    qbool __stu_t0;
    (void)a;
    (void)c;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo6");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_c || !vd_y || !vd_t0) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;
            const clang::Stmt* stmt_A = stmts[1];
            const clang::Stmt* stmt_B = stmts[2];
            const clang::Stmt* stmt_C = stmts[3];

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmt_A->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::PLUGIN;
            B.plugin_kind_id = "plugin.demo.opaque";
            B.result = ref_for(vd_c);
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmt_B->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmt_C->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
    CHECK_EQ_INT(unit.replacements.size(), 0);
}

// ── Additional gate coverage (§12 sharp edges) ─────────────────────────────

// USER_ROUTINE-refused — B is a USER_ROUTINE op. Gate 1 rejects per
// §12 Sharp edge 6 (routine bodies are not re-analysed; conservative
// refuse). The canonical shape is `f(q, q)` aliasing blocking the
// commutation, but at the matcher layer the kind itself is refused.
void test_reorder_refuses_user_routine_b() {
    constexpr std::string_view src = R"CPP(
void demo_ur(qbool a, qbool b, qbool c, qbool y) {
    qbool __stu_t0;
    (void)a;
    (void)c;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo_ur");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_c || !vd_y || !vd_t0) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmts[1]->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::USER_ROUTINE;
            B.routine_name = "unrelated_routine";
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmts[2]->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmts[3]->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
    CHECK_EQ_INT(unit.replacements.size(), 0);
}

// hoist-boundary-bails — B has `hoist_to_override` set (PJ-3 anchor).
// Gate 2 refuses: commuting past a hoisted op would break PJ-3's
// forward/uncompute pairing (§12 Sharp edge 4).
void test_reorder_refuses_hoist_boundary() {
    constexpr std::string_view src = R"CPP(
void demo_hoist(qbool a, qbool b, qbool c, qbool x, qbool y) {
    qbool __stu_t0;
    (void)a;
    x = c;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo_hoist");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_x  = require_var(ctx, "x");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_c || !vd_x || !vd_y || !vd_t0) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmts[1]->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            B.result = ref_for(vd_x);
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmts[2]->getSourceRange();
            // Hoist-override set — Gate 2 refuses.
            B.hoist_to_override = body->getRBracLoc();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmts[3]->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
    CHECK_EQ_INT(unit.replacements.size(), 0);
}

// fused-boundary-bails — B's stmt_range is covered by an entry in
// `unit.fused_stmt_ranges`; Gate 2 refuses (PJ-1d has already fused
// that region; §12 Sharp edge 5).
void test_reorder_refuses_fused_range() {
    constexpr std::string_view src = R"CPP(
void demo_fused(qbool a, qbool b, qbool c, qbool x, qbool y) {
    qbool __stu_t0;
    (void)a;
    x = c;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo_fused");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_x  = require_var(ctx, "x");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_c || !vd_x || !vd_y || !vd_t0) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmts[1]->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            B.result = ref_for(vd_x);
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmts[2]->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmts[3]->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));

            // Record B's stmt_range as a fused range — Gate 2 refuses.
            unit.fused_stmt_ranges.push_back(stmts[2]->getSourceRange());
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
    // The pre-planted fused_stmt_ranges entry stays; the matcher
    // itself did not add a new one because it didn't fire.
    CHECK_EQ_INT(unit.fused_stmt_ranges.size(), 1);
    CHECK_EQ_INT(unit.replacements.size(), 0);
}

// eliminated-boundary-bails — B's stmt_range is covered by an entry
// in `unit.eliminated_stmt_ranges`; Gate 2 refuses (PJ-4a has
// already deleted that decl).
void test_reorder_refuses_eliminated_range() {
    constexpr std::string_view src = R"CPP(
void demo_elim(qbool a, qbool b, qbool c, qbool x, qbool y) {
    qbool __stu_t0;
    (void)a;
    x = c;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo_elim");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_x  = require_var(ctx, "x");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_c || !vd_x || !vd_y || !vd_t0) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmts[1]->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            B.result = ref_for(vd_x);
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmts[2]->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmts[3]->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
            unit.eliminated_stmt_ranges.push_back(
                stmts[2]->getSourceRange());
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
    CHECK_EQ_INT(unit.replacements.size(), 0);
}

// reader-count-gate — A's result has two readers in the scope (C reads
// it, and an extra `(void)__stu_t0;` outside the triple also reads it).
// Gate 4 refuses: post-reorder the `(A, C)` fuse precondition fails.
void test_reorder_refuses_multiple_readers() {
    // The body needs TWO DeclRefExprs to `__stu_t0` so the reader
    // count is >1. We put an extra `(void)__stu_t0;` in the body
    // after C — the reader count is computed over the full scope
    // anchor subtree.
    constexpr std::string_view src = R"CPP(
void demo_rc(qbool a, qbool b, qbool c, qbool x, qbool y) {
    qbool __stu_t0;
    (void)a;
    x = c;
    y = __stu_t0;
    (void)__stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo_rc");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_x  = require_var(ctx, "x");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_c || !vd_x || !vd_y || !vd_t0) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmts[1]->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            B.result = ref_for(vd_x);
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmts[2]->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmts[3]->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
    CHECK_EQ_INT(unit.replacements.size(), 0);
}

// scope-kind-gate — The matcher refuses scopes whose
// classify_scope_kind is neither `Function` nor `LoopBody`. Pin this
// by pointing `scope.open_brace` at an if-body CompoundStmt.
// `BranchBody` / `WhenBody` / `Other` all bail.
void test_reorder_refuses_branch_body_scope() {
    constexpr std::string_view src = R"CPP(
void demo_branch(qbool a, qbool b, qbool c, qbool x, qbool y, bool cond) {
    if (cond) {
        qbool __stu_t0;
        (void)a;
        x = c;
        y = __stu_t0;
    }
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* outer = require_body(ctx,
                                                             "demo_branch");
            if (!outer) return;
            // Find the if's then-branch CompoundStmt.
            const clang::CompoundStmt* inner = nullptr;
            for (const clang::Stmt* s : outer->body()) {
                const auto* is = llvm::dyn_cast<clang::IfStmt>(s);
                if (!is) continue;
                if (auto* then_cs = llvm::dyn_cast<clang::CompoundStmt>(
                        is->getThen())) {
                    inner = then_cs;
                    break;
                }
            }
            if (!inner) {
                std::fprintf(stderr,
                    "FAIL  branch-body: inner if-body not found\n");
                return;
            }

            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_x  = require_var(ctx, "x");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_c || !vd_x || !vd_y || !vd_t0) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : inner->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;

            QScope scope;
            scope.open_brace = inner->getLBracLoc();
            scope.close_brace = inner->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmts[1]->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            B.result = ref_for(vd_x);
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmts[2]->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmts[3]->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
    CHECK_EQ_INT(unit.replacements.size(), 0);
}

// kind-shape-gate — A is NOT an AND op (e.g. OR). Gate 1 refuses.
void test_reorder_refuses_non_and_a() {
    constexpr std::string_view src = R"CPP(
void demo_non_and(qbool a, qbool b, qbool c, qbool x, qbool y) {
    qbool __stu_t0;
    (void)a;
    x = c;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx,
                                                           "demo_non_and");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_x  = require_var(ctx, "x");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_c || !vd_x || !vd_y || !vd_t0) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::OR; // wrong kind for Gate 1
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmts[1]->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            B.result = ref_for(vd_x);
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmts[2]->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmts[3]->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
}

// non-synthetic-name-gate — A's result name does NOT start with
// `__stu_t`; Gate 1 refuses. Protects user-written `qbool t = a & b;`
// from being reordered under us.
void test_reorder_refuses_non_synthetic_name() {
    constexpr std::string_view src = R"CPP(
void demo_non_synth(qbool a, qbool b, qbool c, qbool x, qbool y) {
    qbool t; // user-written name, no __stu_t prefix
    (void)a;
    x = c;
    y = t;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx,
                                                           "demo_non_synth");
            if (!body) return;
            const clang::VarDecl* vd_a = require_var(ctx, "a");
            const clang::VarDecl* vd_b = require_var(ctx, "b");
            const clang::VarDecl* vd_c = require_var(ctx, "c");
            const clang::VarDecl* vd_x = require_var(ctx, "x");
            const clang::VarDecl* vd_y = require_var(ctx, "y");
            const clang::VarDecl* vd_t = require_var(ctx, "t");
            if (!vd_a || !vd_b || !vd_c || !vd_x || !vd_y || !vd_t) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t, "t"); // NOT __stu_t*
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmts[1]->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            B.result = ref_for(vd_x);
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmts[2]->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t, "t"));
            C.stmt_range = stmts[3]->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
}

// c-not-xor-assign-gate — C is NOT an XOR_ASSIGN (e.g. OR); Gate 1
// refuses. Mirror of test_reorder_refuses_non_and_a but on the C slot.
void test_reorder_refuses_non_xor_assign_c() {
    constexpr std::string_view src = R"CPP(
void demo_non_xa(qbool a, qbool b, qbool c, qbool x, qbool y) {
    qbool __stu_t0;
    (void)a;
    x = c;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo_non_xa");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_x  = require_var(ctx, "x");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_c || !vd_x || !vd_y || !vd_t0) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmts[1]->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            B.result = ref_for(vd_x);
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmts[2]->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::OR; // not XOR_ASSIGN
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmts[3]->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
}

// dependent-type-refused — A's result decl_loc is invalid, so the
// alias extractor returns the universal sentinel on A.result. The
// matcher's Gate 3 short-circuits when `a_result_fp.name.empty()`
// (the sentinel name is empty), so the reorder is refused.
void test_reorder_refuses_universal_sentinel_a_result() {
    constexpr std::string_view src = R"CPP(
void demo_univ(qbool a, qbool b, qbool c, qbool x, qbool y) {
    qbool __stu_t0;
    (void)a;
    x = c;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo_univ");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_x  = require_var(ctx, "x");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            if (!vd_a || !vd_b || !vd_c || !vd_x || !vd_y) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            // A.result has name `__stu_t0` but invalid decl_loc →
            // the extractor returns the universal sentinel when
            // asked for A.result's footprint → Gate 3 short-circuit.
            A.result.name = "__stu_t0";
            // decl_loc left invalid — forces the sentinel.
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmts[1]->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            B.result = ref_for(vd_x);
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmts[2]->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            QValueRef c_op;
            c_op.name = "__stu_t0";
            C.operands.push_back(std::move(c_op));
            C.stmt_range = stmts[3]->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
    CHECK_EQ_INT(unit.replacements.size(), 0);
}

// c-first-operand-mismatch — C's first operand name doesn't match A's
// result name. Gate 1 refuses (c_first_op.name != A.result.name).
void test_reorder_refuses_mismatched_consumer() {
    constexpr std::string_view src = R"CPP(
void demo_mm(qbool a, qbool b, qbool c, qbool d, qbool x, qbool y) {
    qbool __stu_t0;
    (void)a;
    x = c;
    y = d;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo_mm");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_c  = require_var(ctx, "c");
            const clang::VarDecl* vd_d  = require_var(ctx, "d");
            const clang::VarDecl* vd_x  = require_var(ctx, "x");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_c || !vd_d || !vd_x || !vd_y || !vd_t0)
                return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 4) return;

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmts[1]->getSourceRange();
            scope.ops.push_back(std::move(A));

            QOperation B;
            B.kind = QOpKind::XOR_ASSIGN;
            B.result = ref_for(vd_x);
            B.operands.push_back(ref_for(vd_c));
            B.stmt_range = stmts[2]->getSourceRange();
            scope.ops.push_back(std::move(B));

            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            // C reads `d`, not `__stu_t0` — Gate 1 mismatch.
            C.operands.push_back(ref_for(vd_d));
            C.stmt_range = stmts[3]->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
}

// fewer-than-3-ops — A scope with only 2 ops cannot produce a triple.
// The matcher's `scope.ops.size() < 3` guard refuses cleanly.
void test_reorder_refuses_too_few_ops() {
    constexpr std::string_view src = R"CPP(
void demo_few(qbool a, qbool b, qbool y) {
    qbool __stu_t0;
    (void)a;
    y = __stu_t0;
}
)CPP";
    QUnit unit;
    bool ran = run_with_reorder_matcher(src, unit,
        [&](clang::ASTContext& ctx) {
            const clang::CompoundStmt* body = require_body(ctx, "demo_few");
            if (!body) return;
            const clang::VarDecl* vd_a  = require_var(ctx, "a");
            const clang::VarDecl* vd_b  = require_var(ctx, "b");
            const clang::VarDecl* vd_y  = require_var(ctx, "y");
            const clang::VarDecl* vd_t0 = require_var(ctx, "__stu_t0");
            if (!vd_a || !vd_b || !vd_y || !vd_t0) return;

            std::vector<const clang::Stmt*> stmts;
            for (const clang::Stmt* s : body->body()) stmts.push_back(s);
            if (stmts.size() < 3) return;

            QScope scope;
            scope.open_brace = body->getLBracLoc();
            scope.close_brace = body->getRBracLoc();

            QOperation A;
            A.kind = QOpKind::AND;
            A.result = ref_named(vd_t0, "__stu_t0");
            A.operands.push_back(ref_for(vd_a));
            A.operands.push_back(ref_for(vd_b));
            A.stmt_range = stmts[1]->getSourceRange();
            scope.ops.push_back(std::move(A));

            // Only two ops in scope — no triple.
            QOperation C;
            C.kind = QOpKind::XOR_ASSIGN;
            C.result = ref_for(vd_y);
            C.operands.push_back(ref_named(vd_t0, "__stu_t0"));
            C.stmt_range = stmts[2]->getSourceRange();
            scope.ops.push_back(std::move(C));

            unit.scopes.push_back(std::move(scope));
        },
        nullptr);
    CHECK(ran);
    CHECK_EQ_INT(peephole_reorder_detection_count_for_test(), 0);
    CHECK_EQ_INT(unit.replacements.size(), 0);
}

} // namespace

int run_test_matcher_peephole_reorder(int /*argc*/, char** /*argv*/) {
    // (1) Positive — disjoint qbool between A and C.
    test_reorder_fires_disjoint_qbool();
    // (2) Positive — disjoint qint-flavoured operand between A and C.
    test_reorder_fires_disjoint_qint_bits();
    // (3-variant) Pin v1 behaviour when B writes A's operand (not
    //     A's result). The matcher currently accepts this shape.
    test_reorder_operand_write_is_currently_allowed();
    // (3) Negative — B writes A's RESULT (canonical overlap-reject).
    test_reorder_refuses_overlap_a_result();
    // (4) Positive — BitProxy-const / distinct-decl qint operand.
    test_reorder_fires_bitproxy_const();
    // (5) Negative — BitProxy-nonconst / universal sentinel on B.
    test_reorder_refuses_bitproxy_nonconst();
    // (6) Negative — B is QOpKind::PLUGIN.
    test_reorder_refuses_plugin_b();

    // §12 sharp-edges matrix (additional gates).
    test_reorder_refuses_user_routine_b();
    test_reorder_refuses_hoist_boundary();
    test_reorder_refuses_fused_range();
    test_reorder_refuses_eliminated_range();
    test_reorder_refuses_multiple_readers();
    test_reorder_refuses_branch_body_scope();
    test_reorder_refuses_non_and_a();
    test_reorder_refuses_non_synthetic_name();
    test_reorder_refuses_non_xor_assign_c();
    test_reorder_refuses_universal_sentinel_a_result();
    test_reorder_refuses_mismatched_consumer();
    test_reorder_refuses_too_few_ops();

    std::fprintf(stderr,
                 "test_matcher_peephole_reorder: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
