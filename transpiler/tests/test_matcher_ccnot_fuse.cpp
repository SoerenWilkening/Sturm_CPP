// test_matcher_ccnot_fuse.cpp — Phase J PJ-1d tests for the zero-ancilla
// fusion peephole matcher (`register_ccnot_fuse_matcher`).
//
// The PJ-1d matcher anchors on a `qbool __t = a & b;` VarDecl whose init
// is a bare `&` op-call with two DeclRefExpr operands (nested init is
// rejected — those are handled by the Phase E compound flatten matcher).
// On a successful anchor the callback consults the adjacent next
// statement inside the same CompoundStmt; it fuses the pair iff that
// stmt is `x ^= __t;` on a qbool target AND the PJ-1a reader-count
// helper reports exactly one reader of `__t` within the enclosing
// scope (namely, the XOR RHS itself).
//
// On fusion the matcher appends:
//   - one `QReplacement` whose range spans BOTH statements (the VarDecl
//     and the `^=` stmt) with replacement text `ccnot_inplace(x, a, b);`
//   - one `QOperation{kind=CCNOT_INPLACE, result=x, operands=[a, b]}`
//     to the enclosing QScope so the M8 uncompute pass emits a second
//     `ccnot_inplace(x, a, b);` at the scope close (CCX is self-adjoint
//     per PJ-1c's render case).
//
// Fusion-reject cases stay on the runtime path — the unchanged
// `qbool __t = a & b;` VarDecl flows through the Phase E compound
// matcher (registered after this one in main.cpp per PJ-1f). The tests
// pin both sides of the decision tree.

#include "test_matcher_harness.hpp"

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

// Minimal qbool stub: enough for `operator&` on qbool pair + `operator^=`
// compound-assign to parse without dragging in the real backend headers.
constexpr std::string_view kQBoolFuseStub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    qbool& operator^=(int) { return *this; }
};

inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }

} // namespace sturm
using sturm::qbool;
)CPP";

// Run the PJ-1d fuse matcher against `user_src` (the qbool stub is
// prepended automatically). Returns the populated QUnit.
QUnit run_ccnot_fuse_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolFuseStub.size() + user_src.size());
    code.append(kQBoolFuseStub);
    code.append(user_src);

    QUnit unit;
    clang::ast_matchers::MatchFinder finder;
    register_ccnot_fuse_matcher(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PJ-1d ccnot_fuse)\n");
    }
    return unit;
}

// PJ-1e integration: run PJ-1d AND the downstream PA-3 / PA-4 / PE-4
// matchers on the same AST, then apply the post-matcher cleanup pass
// `apply_fused_stmt_guards`. Mirrors the `main.cpp` pipeline: the
// in-callback early-return is a best-effort fast path (fires whenever
// PJ-1d's Decl callback beats the Stmt callbacks), while the cleanup
// pass is the authoritative backstop that runs unconditionally after
// `matchAST` completes.
QUnit run_fuse_and_downstream(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolFuseStub.size() + user_src.size());
    code.append(kQBoolFuseStub);
    code.append(user_src);

    QUnit unit;
    // A custom ASTConsumer so we can invoke `apply_fused_stmt_guards`
    // at the same point `main.cpp` does — between `matchAST` and any
    // downstream consumer of the QUnit (M8 synthesize, etc.). Without
    // the cleanup call, callbacks that fire before PJ-1d's Decl
    // callback would leave stale ops in `unit.scopes`.
    class Consumer : public clang::ASTConsumer {
    public:
        Consumer(clang::ast_matchers::MatchFinder* finder, QUnit* unit)
            : finder_(finder), unit_(unit) {}
        void HandleTranslationUnit(clang::ASTContext& ctx) override {
            finder_->matchAST(ctx);
            sturm::transpile::apply_fused_stmt_guards(
                *unit_, ctx.getSourceManager());
        }
    private:
        clang::ast_matchers::MatchFinder* finder_;
        QUnit* unit_;
    };
    class Action : public clang::ASTFrontendAction {
    public:
        Action(clang::ast_matchers::MatchFinder* finder, QUnit* unit)
            : finder_(finder), unit_(unit) {}
        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance&, llvm::StringRef) override {
            return std::make_unique<Consumer>(finder_, unit_);
        }
    private:
        clang::ast_matchers::MatchFinder* finder_;
        QUnit* unit_;
    };
    class Factory : public clang::tooling::FrontendActionFactory {
    public:
        Factory(clang::ast_matchers::MatchFinder* finder, QUnit* unit)
            : finder_(finder), unit_(unit) {}
        std::unique_ptr<clang::FrontendAction> create() override {
            return std::make_unique<Action>(finder_, unit_);
        }
    private:
        clang::ast_matchers::MatchFinder* finder_;
        QUnit* unit_;
    };

    clang::ast_matchers::MatchFinder finder;
    // PJ-1d FIRST so its fused_stmt_ranges append fires before the
    // downstream matchers' range-probe runs. This mirrors the main.cpp
    // PJ-1f wiring constraint.
    register_ccnot_fuse_matcher(finder, unit);
    register_xor_assign_matcher(finder, unit);
    register_xor_assign_classical_matcher(finder, unit);
    register_compound_qbool_matcher(finder, unit);

    Factory factory(&finder, &unit);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PJ-1e integration)\n");
    }
    return unit;
}

// ── Happy-path: canonical fuse pair ─────────────────────────────────────────

// Canonical PJ-1d shape: `qbool __t = a & b; x ^= __t;` with __t read
// exactly once. The matcher must append one QReplacement over both
// statements and one QOperation{kind=CCNOT_INPLACE}.
void test_fuse_canonical_pair() {
    QUnit unit = run_ccnot_fuse_matcher(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool __t = a & b;\n"
        "    x ^= __t;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& scope = unit.scopes.front();
    CHECK(scope.ops.size() == 1);
    if (scope.ops.empty()) return;

    const auto& op = scope.ops.front();
    CHECK(op.kind == QOpKind::CCNOT_INPLACE);
    CHECK_EQ_STR(op.result.name, std::string("x"));
    CHECK(op.operands.size() == 2);
    if (op.operands.size() == 2) {
        CHECK_EQ_STR(op.operands[0].name, std::string("a"));
        CHECK_EQ_STR(op.operands[1].name, std::string("b"));
    }

    // One QReplacement over both stmts. PM2-4: the replacement text is
    // prefixed with a `\n#line <N> "<basename>"\n` directive anchored
    // at the fused range's begin (the VarDecl's begin loc). We assert
    // the trailing call substring and the directive presence rather
    // than the full byte sequence — the line number depends on the
    // prepended qbool stub size and the filename basename depends on
    // the virtual file passed to runToolOnCodeWithArgs.
    CHECK(unit.replacements.size() == 1);
    if (!unit.replacements.empty()) {
        const std::string& rep = unit.replacements.front().replacement;
        CHECK(rep.find("ccnot_inplace(x, a, b);") != std::string::npos);
        CHECK(rep.find("#line ") != std::string::npos);
        CHECK(unit.replacements.front().range.isValid());
    }
}

// Happy-path: tmp name other than __t. The matcher must not rely on the
// specific spelling `__t` — any fresh name works as long as the rest of
// the shape matches.
void test_fuse_arbitrary_tmp_name() {
    QUnit unit = run_ccnot_fuse_matcher(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool myfresh = a & b;\n"
        "    x ^= myfresh;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    CHECK(unit.scopes.front().ops.size() == 1);
    CHECK(unit.replacements.size() == 1);
    // PM2-4: the replacement is prefixed with a `#line` directive (see
    // test_fuse_canonical_pair for rationale). Assert the substring
    // rather than the full byte sequence so the test stays stable as
    // the directive's line number / basename vary by harness.
    if (!unit.replacements.empty()) {
        const std::string& rep = unit.replacements.front().replacement;
        CHECK(rep.find("ccnot_inplace(x, a, b);") != std::string::npos);
        CHECK(rep.find("#line ") != std::string::npos);
    }
}

// ── Rejection: reader-count != 1 ────────────────────────────────────────────

// Two readers — the fused form would discard one reader. Reject.
void test_reject_two_readers() {
    QUnit unit = run_ccnot_fuse_matcher(
        "void demo(qbool a, qbool b, qbool x, qbool y) {\n"
        "    qbool __t = a & b;\n"
        "    x ^= __t;\n"
        "    y = __t;\n"
        "}\n");

    CHECK(unit.replacements.empty());
    std::size_t total = 0;
    for (const auto& s : unit.scopes) total += s.ops.size();
    CHECK(total == 0);
}

// Zero readers — no `^=` consumer at all. The subsequent stmt does not
// reference __t, so the reader-count is 0 and the matcher must not fuse.
void test_reject_zero_readers() {
    QUnit unit = run_ccnot_fuse_matcher(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool __t = a & b;\n"
        "    x = a;\n"
        "}\n");

    CHECK(unit.replacements.empty());
    std::size_t total = 0;
    for (const auto& s : unit.scopes) total += s.ops.size();
    CHECK(total == 0);
}

// ── Rejection: next stmt is not `x ^= __t` ──────────────────────────────────

// The next stmt references __t but is not a `^=` form. Reject.
void test_reject_non_xor_next() {
    QUnit unit = run_ccnot_fuse_matcher(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool __t = a & b;\n"
        "    x = __t;\n"
        "}\n");

    CHECK(unit.replacements.empty());
}

// The next stmt is `^=` but the RHS is NOT __t — it is some other qbool.
// Reject (the fusion has no pair to fold away).
void test_reject_xor_rhs_not_temp() {
    QUnit unit = run_ccnot_fuse_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool x) {\n"
        "    qbool __t = a & b;\n"
        "    x ^= c;\n"
        "}\n");

    CHECK(unit.replacements.empty());
}

// ── Rejection: VarDecl shape guards ─────────────────────────────────────────

// Nested init — `qbool __t = (a | b) & c;` — is handled by the Phase E
// compound flatten matcher, not the fuse peephole. PJ-1d must reject.
void test_reject_nested_init() {
    QUnit unit = run_ccnot_fuse_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool x) {\n"
        "    qbool __t = (a | b) & c;\n"
        "    x ^= __t;\n"
        "}\n");

    CHECK(unit.replacements.empty());
}

// OR init is not the fuse shape — the matcher targets `&` only because
// the zero-ancilla fusion is CCX-specific (two controls + one target).
void test_reject_or_init() {
    QUnit unit = run_ccnot_fuse_matcher(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool __t = a | b;\n"
        "    x ^= __t;\n"
        "}\n");

    CHECK(unit.replacements.empty());
}

// ── No adjacent stmt / last stmt in scope ──────────────────────────────────

// The AND VarDecl is the LAST stmt in its enclosing scope — there is no
// adjacent next stmt to fuse with. Reject cleanly without crashing.
void test_reject_no_next_stmt() {
    QUnit unit = run_ccnot_fuse_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    qbool __t = a & b;\n"
        "}\n");

    CHECK(unit.replacements.empty());
}

// ── Multiple independent pairs in the same scope ────────────────────────────

void test_fuse_two_independent_pairs() {
    // Two independent fuse pairs in the same scope. Each pair gates on
    // its own temp's reader-count (== 1) and its own adjacent next stmt.
    // The matcher must emit two QReplacements and two QOperations.
    QUnit unit = run_ccnot_fuse_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d,\n"
        "          qbool x, qbool y) {\n"
        "    qbool __t0 = a & b;\n"
        "    x ^= __t0;\n"
        "    qbool __t1 = c & d;\n"
        "    y ^= __t1;\n"
        "}\n");

    CHECK(unit.replacements.size() == 2);
    std::size_t total_ops = 0;
    for (const auto& s : unit.scopes) total_ops += s.ops.size();
    CHECK(total_ops == 2);
}

// ── PJ-1e: fused_stmt_ranges bookkeeping ────────────────────────────────────

// A successful fuse must record ONE entry in `QUnit::fused_stmt_ranges`
// whose source range covers the SECOND statement of the pair (the
// `x ^= __t;` stmt, terminating-`;` included). The downstream Phase A
// `^=` and Phase E compound matchers consume this list to early-return
// on any match whose own stmt-range lies inside this entry, preventing
// double-emission on the fused pair.
void test_fuse_records_second_stmt_range() {
    QUnit unit = run_ccnot_fuse_matcher(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool __t = a & b;\n"
        "    x ^= __t;\n"
        "}\n");

    CHECK(unit.fused_stmt_ranges.size() == 1);
    if (unit.fused_stmt_ranges.empty()) return;
    CHECK(unit.fused_stmt_ranges.front().isValid());
}

// Multiple independent fuses: each should contribute its own entry to
// `fused_stmt_ranges`. The PJ-1d tests already pin the replacement /
// QOperation count at 2 for the canonical two-pair input; this test
// pins the same for `fused_stmt_ranges`.
void test_fuse_records_two_stmt_ranges() {
    QUnit unit = run_ccnot_fuse_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d,\n"
        "          qbool x, qbool y) {\n"
        "    qbool __t0 = a & b;\n"
        "    x ^= __t0;\n"
        "    qbool __t1 = c & d;\n"
        "    y ^= __t1;\n"
        "}\n");

    CHECK(unit.fused_stmt_ranges.size() == 2);
}

// A rejected fuse (any of the PJ-1d reject cases) must NOT touch
// `fused_stmt_ranges` — the list stays empty. Exercise the no-next-stmt
// reject path; the other reject paths share the same bail-before-push
// discipline.
void test_reject_leaves_fused_ranges_empty() {
    QUnit unit = run_ccnot_fuse_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    qbool __t = a & b;\n"
        "}\n");

    CHECK(unit.fused_stmt_ranges.empty());
}

// ── PJ-1e integration: downstream matchers early-return on fused pair ──────

// Canonical fuse pair + PA-3 xor-assign matcher: the PJ-1d callback
// populates `fused_stmt_ranges` with the second stmt's range, and
// the PA-3 XorAssignCallback's `is_range_covered_by_fused` guard
// causes it to early-return on `x ^= __t;`. Net effect: the QUnit
// holds exactly ONE QOperation (the PJ-1d CCNOT_INPLACE), NOT two
// (where the second would be a stale XOR_ASSIGN op that would
// emit `x ^= __t;` at scope close).
void test_integration_fuse_suppresses_xor_assign() {
    QUnit unit = run_fuse_and_downstream(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool __t = a & b;\n"
        "    x ^= __t;\n"
        "}\n");

    // Exactly one scope with exactly one op — the CCNOT_INPLACE.
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    CHECK(unit.scopes.front().ops.size() == 1);
    if (unit.scopes.front().ops.size() != 1) return;
    CHECK(unit.scopes.front().ops.front().kind == QOpKind::CCNOT_INPLACE);

    // One replacement (the `ccnot_inplace(x, a, b);` fusion). The
    // PA-3 callback does NOT contribute a QOperation, so there is
    // no stale `x ^= __t;` queued for scope-close emission.
    CHECK(unit.replacements.size() == 1);
    CHECK(unit.fused_stmt_ranges.size() == 1);
}

// Independent `^=` stmt NOT inside a fuse pair: the PA-3 matcher
// fires as usual. Verifies the early-return is a guard, not a
// wholesale disablement of the xor-assign matcher.
void test_integration_unfused_xor_assign_still_matches() {
    QUnit unit = run_fuse_and_downstream(
        "void demo(qbool a, qbool x) {\n"
        "    x ^= a;\n"
        "}\n");

    // No fuse candidate — `fused_stmt_ranges` stays empty.
    CHECK(unit.fused_stmt_ranges.empty());
    // PA-3 matched the `x ^= a;` stmt and pushed one XOR_ASSIGN op.
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    CHECK(unit.scopes.front().ops.size() == 1);
    if (unit.scopes.front().ops.size() != 1) return;
    CHECK(unit.scopes.front().ops.front().kind == QOpKind::XOR_ASSIGN);
}

// Mixed scope: one fuse pair + one standalone `^=`. The PA-3 matcher
// must early-return on the fused pair's second stmt but match the
// standalone `^=`. Net QOperation count: 2 — one CCNOT_INPLACE (from
// PJ-1d on the fused pair) and one XOR_ASSIGN (from PA-3 on the
// standalone).
void test_integration_mixed_fused_and_standalone() {
    QUnit unit = run_fuse_and_downstream(
        "void demo(qbool a, qbool b, qbool c, qbool x, qbool y) {\n"
        "    qbool __t = a & b;\n"
        "    x ^= __t;\n"
        "    y ^= c;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& scope = unit.scopes.front();
    CHECK(scope.ops.size() == 2);
    if (scope.ops.size() != 2) return;

    // The CCNOT_INPLACE op lands first (source order for its anchor
    // is the fused range's begin), XOR_ASSIGN second. The exact
    // order depends on registration + source order; we just assert
    // one-of-each is present.
    bool has_ccnot = false;
    bool has_xor_assign = false;
    for (const auto& op : scope.ops) {
        if (op.kind == QOpKind::CCNOT_INPLACE) has_ccnot = true;
        if (op.kind == QOpKind::XOR_ASSIGN)    has_xor_assign = true;
    }
    CHECK(has_ccnot);
    CHECK(has_xor_assign);
    CHECK(unit.fused_stmt_ranges.size() == 1);
}

// Canonical fuse pair + PE-4 compound matcher: the compound matcher
// anchors on a VarDecl whose init is a nested bitwise op-call; the
// fused pair's first stmt (`qbool __t = a & b;`) has BARE DREs as
// operands — NOT a nested init — so the compound matcher's own
// pattern guard already rejects it. The PJ-1e guard is defensive
// (future PJ-1 relaxations), so this test exercises the happy path
// where the compound matcher's pattern and the PJ-1e guard both
// agree "not my shape". Expectation: one CCNOT_INPLACE op, zero
// compound-flatten ops / replacements from the compound matcher.
void test_integration_compound_matcher_does_not_double_fire() {
    QUnit unit = run_fuse_and_downstream(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool __t = a & b;\n"
        "    x ^= __t;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    CHECK(unit.scopes.front().ops.size() == 1);
    if (unit.scopes.front().ops.size() != 1) return;
    CHECK(unit.scopes.front().ops.front().kind == QOpKind::CCNOT_INPLACE);

    // Only one replacement — the PJ-1d fusion. The compound matcher
    // contributes zero.
    CHECK(unit.replacements.size() == 1);
}

} // namespace

void run_ccnot_fuse_tests() {
    test_fuse_canonical_pair();
    test_fuse_arbitrary_tmp_name();
    test_reject_two_readers();
    test_reject_zero_readers();
    test_reject_non_xor_next();
    test_reject_xor_rhs_not_temp();
    test_reject_nested_init();
    test_reject_or_init();
    test_reject_no_next_stmt();
    test_fuse_two_independent_pairs();
    test_fuse_records_second_stmt_range();
    test_fuse_records_two_stmt_ranges();
    test_reject_leaves_fused_ranges_empty();
    test_integration_fuse_suppresses_xor_assign();
    test_integration_unfused_xor_assign_still_matches();
    test_integration_mixed_fused_and_standalone();
    test_integration_compound_matcher_does_not_double_fire();
}
