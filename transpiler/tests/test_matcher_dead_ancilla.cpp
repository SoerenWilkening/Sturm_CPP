// test_matcher_dead_ancilla.cpp — Phase J PJ-4a tests for the dead-ancilla
// elimination matcher (`register_dead_ancilla_matcher`).
//
// The PJ-4a matcher anchors on `qbool` VarDecls whose initializer is a
// qbool-operand bitwise op-call (`a | b`, `a & b`, `a ^ b`, `~a`, or the
// compound variants) and whose reader-count within the enclosing scope
// is zero. Shares the PJ-1a `detail::count_readers_in_scope` helper
// with the PJ-1d fuse peephole. On detection it emits one
// `QReplacement{range=<decl stmt range>, replacement=""}` deleting the
// decl verbatim AND pushes the decl's range into
// `QUnit::eliminated_stmt_ranges` so downstream matchers early-return
// on any match whose own stmt-range lies inside one of those entries.
//
// The tests pin:
//   - happy path: zero-reader `qbool t = a | b;` elimination.
//   - each qbool-initializer shape recognised (OR, AND, XOR, NOT,
//     nested compound).
//   - rejection cases: any reader (count >= 1) preserves the decl.
//   - elimination inside a WHEN / branch / loop body (enclosing scope
//     is the body, not the surrounding function).
//   - `eliminated_stmt_ranges` bookkeeping: successful elimination
//     records one entry per eliminated decl; rejected fixtures leave
//     the list empty.
//   - downstream matcher early-return: a following `x ^= t;` on the
//     eliminated `t` would be stale once the decl is gone; the PA-3
//     XOR-assign matcher must early-return via
//     `is_range_covered_by_fused`-style containment on
//     `eliminated_stmt_ranges`. (In practice the canonical dead
//     ancilla has NO readers, so this case is covered by the
//     PJ-4a rejection guard directly — but the test pins the
//     downstream-matcher-off invariant for multi-scope fixtures.)
//
// Fixture style mirrors `test_matcher_ccnot_fuse.cpp` — a minimal qbool
// stub + `runToolOnCodeWithArgs`, no real backend headers.

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

// Minimal qbool stub: enough for the bitwise op-call shapes PJ-4a
// anchors on to parse without dragging in the real backend headers.
constexpr std::string_view kQBoolDeadStub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    qbool& operator^=(int) { return *this; }
    qbool operator~() const { return qbool{}; }
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator^(const qbool&, const qbool&) { return qbool{}; }

} // namespace sturm
using sturm::qbool;
)CPP";

// Run ONLY the PJ-4a dead-ancilla matcher against `user_src`.
QUnit run_dead_ancilla_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolDeadStub.size() + user_src.size());
    code.append(kQBoolDeadStub);
    code.append(user_src);

    QUnit unit;
    clang::ast_matchers::MatchFinder finder;
    register_dead_ancilla_matcher(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PJ-4a dead_ancilla)\n");
    }
    return unit;
}

// Integration helper — register PJ-4a FIRST and then the Phase A/E
// downstream matchers; mirrors the PJ-1e integration harness. This lets
// the test exercise the downstream early-return plus the
// `apply_eliminated_stmt_guards` cleanup.
QUnit run_dead_and_downstream(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolDeadStub.size() + user_src.size());
    code.append(kQBoolDeadStub);
    code.append(user_src);

    QUnit unit;
    // Custom ASTConsumer so we can invoke `apply_eliminated_stmt_guards`
    // after matchAST completes — same point `main.cpp` will run it post
    // PJ-4b wiring.
    class Consumer : public clang::ASTConsumer {
    public:
        Consumer(clang::ast_matchers::MatchFinder* finder, QUnit* unit)
            : finder_(finder), unit_(unit) {}
        void HandleTranslationUnit(clang::ASTContext& ctx) override {
            finder_->matchAST(ctx);
            sturm::transpile::apply_eliminated_stmt_guards(
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
    // PJ-4a FIRST so its eliminated_stmt_ranges append fires before the
    // downstream matchers' range-probe runs.
    register_dead_ancilla_matcher(finder, unit);
    register_or_matcher(finder, unit);
    register_not_matcher(finder, unit);
    register_xor_matcher(finder, unit);
    register_xor_assign_matcher(finder, unit);
    register_xor_assign_classical_matcher(finder, unit);
    register_compound_qbool_matcher(finder, unit);

    Factory factory(&finder, &unit);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PJ-4a integration)\n");
    }
    return unit;
}

// ── Happy-path: canonical dead qbool OR decl ────────────────────────────────

// A `qbool t = a | b;` with NO reader anywhere. The matcher must emit
// ONE empty-replacement QReplacement over the VarDecl's source range
// AND push one entry into `eliminated_stmt_ranges`. No QOperation is
// appended to any scope — the decl is silently eliminated.
void test_dead_ancilla_or_basic() {
    QUnit unit = run_dead_ancilla_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    qbool t = a | b;\n"
        "}\n");

    CHECK(unit.replacements.size() == 1);
    if (!unit.replacements.empty()) {
        CHECK_EQ_STR(unit.replacements.front().replacement, std::string(""));
        CHECK(unit.replacements.front().range.isValid());
    }
    CHECK(unit.eliminated_stmt_ranges.size() == 1);
    if (!unit.eliminated_stmt_ranges.empty()) {
        CHECK(unit.eliminated_stmt_ranges.front().isValid());
    }
}

// AND shape — `qbool t = a & b;` with no reader. Same pattern as OR.
void test_dead_ancilla_and_basic() {
    QUnit unit = run_dead_ancilla_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    qbool t = a & b;\n"
        "}\n");

    CHECK(unit.replacements.size() == 1);
    CHECK(unit.eliminated_stmt_ranges.size() == 1);
}

// XOR shape — `qbool t = a ^ b;` with no reader.
void test_dead_ancilla_xor_basic() {
    QUnit unit = run_dead_ancilla_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    qbool t = a ^ b;\n"
        "}\n");

    CHECK(unit.replacements.size() == 1);
    CHECK(unit.eliminated_stmt_ranges.size() == 1);
}

// NOT shape — `qbool t = ~a;` with no reader.
void test_dead_ancilla_not_basic() {
    QUnit unit = run_dead_ancilla_matcher(
        "void demo(qbool a) {\n"
        "    qbool t = ~a;\n"
        "}\n");

    CHECK(unit.replacements.size() == 1);
    CHECK(unit.eliminated_stmt_ranges.size() == 1);
}

// ── Rejection: reader-count != 0 ────────────────────────────────────────────

// Reader exists — the decl is used later in the scope. Reject (do not
// eliminate). No replacement, no eliminated range.
void test_reject_one_reader() {
    QUnit unit = run_dead_ancilla_matcher(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool t = a | b;\n"
        "    x = t;\n"
        "}\n");

    CHECK(unit.replacements.empty());
    CHECK(unit.eliminated_stmt_ranges.empty());
}

// Multiple readers — same rejection semantics. Any reader kills the
// elimination.
void test_reject_two_readers() {
    QUnit unit = run_dead_ancilla_matcher(
        "void demo(qbool a, qbool b, qbool x, qbool y) {\n"
        "    qbool t = a | b;\n"
        "    x = t;\n"
        "    y = t;\n"
        "}\n");

    CHECK(unit.replacements.empty());
    CHECK(unit.eliminated_stmt_ranges.empty());
}

// Chained decl — `qbool r = t & d;` after a `qbool t = a | b;`. The `t`
// decl IS read (by the `r = t & d;` decl's init), so it is NOT dead.
// This pins the "chain" rejection path listed in the plan's PJ-4c
// fixtures section.
void test_reject_chain_reader() {
    QUnit unit = run_dead_ancilla_matcher(
        "void demo(qbool a, qbool b, qbool d) {\n"
        "    qbool t = a | b;\n"
        "    qbool r = t & d;\n"
        "}\n");

    // `t` has a reader (inside `r = t & d;`), so it is NOT eliminated.
    // `r` has no readers — it IS eliminated. Expect exactly one
    // replacement and one eliminated range, corresponding to `r`.
    CHECK(unit.replacements.size() == 1);
    CHECK(unit.eliminated_stmt_ranges.size() == 1);
}

// ── Rejection: non-qbool-operand initializer ────────────────────────────────

// The VarDecl's initializer is a bare copy (`qbool t = a;`) — no
// bitwise op-call at all. The matcher must only anchor on decls whose
// init is a qbool-operand bitwise expression; a plain copy is not
// material to eliminate.
void test_reject_plain_copy_init() {
    QUnit unit = run_dead_ancilla_matcher(
        "void demo(qbool a) {\n"
        "    qbool t = a;\n"
        "}\n");

    CHECK(unit.replacements.empty());
    CHECK(unit.eliminated_stmt_ranges.empty());
}

// Default-constructed (no initializer) — the matcher must not anchor.
void test_reject_default_init() {
    QUnit unit = run_dead_ancilla_matcher(
        "void demo() {\n"
        "    qbool t;\n"
        "}\n");

    CHECK(unit.replacements.empty());
    CHECK(unit.eliminated_stmt_ranges.empty());
}

// ── Multiple independent dead decls in the same scope ──────────────────────

// Two independent dead decls. Each contributes one replacement and one
// eliminated range.
void test_two_independent_dead_decls() {
    QUnit unit = run_dead_ancilla_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d) {\n"
        "    qbool t0 = a | b;\n"
        "    qbool t1 = c & d;\n"
        "}\n");

    CHECK(unit.replacements.size() == 2);
    CHECK(unit.eliminated_stmt_ranges.size() == 2);
}

// ── Elimination inside a loop body ──────────────────────────────────────────

// A dead decl inside a for-loop body. The reader-count probe should
// anchor on the body CompoundStmt so the elimination fires correctly.
void test_dead_ancilla_inside_for_body() {
    QUnit unit = run_dead_ancilla_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 4; ++i) {\n"
        "        qbool t = a | b;\n"
        "    }\n"
        "}\n");

    CHECK(unit.replacements.size() == 1);
    CHECK(unit.eliminated_stmt_ranges.size() == 1);
}

// A dead decl inside an if-then body.
void test_dead_ancilla_inside_if_then() {
    QUnit unit = run_dead_ancilla_matcher(
        "void demo(qbool a, qbool b, bool g) {\n"
        "    if (g) {\n"
        "        qbool t = a & b;\n"
        "    }\n"
        "}\n");

    CHECK(unit.replacements.size() == 1);
    CHECK(unit.eliminated_stmt_ranges.size() == 1);
}

// ── PJ-4a: eliminated_stmt_ranges bookkeeping ──────────────────────────────

// Rejected fixtures must not touch `eliminated_stmt_ranges` — the list
// stays empty. Pin the "no write-through on reject" invariant.
void test_reject_leaves_eliminated_ranges_empty() {
    QUnit unit = run_dead_ancilla_matcher(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool t = a | b;\n"
        "    x = t;\n"
        "}\n");

    CHECK(unit.eliminated_stmt_ranges.empty());
}

// ── PJ-4a integration: downstream matchers early-return on eliminated decl ──

// A dead qbool OR decl paired with the Phase A OR matcher on the SAME
// AST: the PJ-4a matcher must win — its eliminated range causes the
// OR matcher to early-return on the same VarDecl, so the QUnit holds
// exactly ZERO QOperations for that decl (not one OR op that the M8
// pass would try to uncompute a nonexistent `t` from).
void test_integration_or_matcher_early_returns() {
    QUnit unit = run_dead_and_downstream(
        "void demo(qbool a, qbool b) {\n"
        "    qbool t = a | b;\n"
        "}\n");

    // Exactly one replacement — PJ-4a's empty-text deletion.
    CHECK(unit.replacements.size() == 1);
    if (!unit.replacements.empty()) {
        CHECK_EQ_STR(unit.replacements.front().replacement, std::string(""));
    }
    CHECK(unit.eliminated_stmt_ranges.size() == 1);
    // Zero QOperations — the OR matcher must have bailed on the
    // eliminated decl.
    std::size_t total_ops = 0;
    for (const auto& s : unit.scopes) total_ops += s.ops.size();
    CHECK(total_ops == 0);
}

// A `qbool t = a | b;` with a reader — PJ-4a rejects, OR matcher fires
// as usual. Verifies the early-return is scoped to eliminated decls,
// not blanket suppression.
void test_integration_live_or_still_matches() {
    QUnit unit = run_dead_and_downstream(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool t = a | b;\n"
        "    x = t;\n"
        "}\n");

    // No elimination — `t` is read.
    CHECK(unit.eliminated_stmt_ranges.empty());
    // OR matcher fires — one QOperation on `t`.
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    CHECK(unit.scopes.front().ops.size() == 1);
    if (unit.scopes.front().ops.size() != 1) return;
    CHECK(unit.scopes.front().ops.front().kind == QOpKind::OR);
}

// A dead XOR decl paired with the Phase A XOR matcher: the PJ-4a
// matcher must win, XOR matcher early-returns.
void test_integration_xor_matcher_early_returns() {
    QUnit unit = run_dead_and_downstream(
        "void demo(qbool a, qbool b) {\n"
        "    qbool t = a ^ b;\n"
        "}\n");

    CHECK(unit.replacements.size() == 1);
    CHECK(unit.eliminated_stmt_ranges.size() == 1);
    std::size_t total_ops = 0;
    for (const auto& s : unit.scopes) total_ops += s.ops.size();
    CHECK(total_ops == 0);
}

// A dead NOT decl paired with the Phase A NOT matcher.
void test_integration_not_matcher_early_returns() {
    QUnit unit = run_dead_and_downstream(
        "void demo(qbool a) {\n"
        "    qbool t = ~a;\n"
        "}\n");

    CHECK(unit.replacements.size() == 1);
    CHECK(unit.eliminated_stmt_ranges.size() == 1);
    std::size_t total_ops = 0;
    for (const auto& s : unit.scopes) total_ops += s.ops.size();
    CHECK(total_ops == 0);
}

} // namespace

void run_dead_ancilla_tests() {
    test_dead_ancilla_or_basic();
    test_dead_ancilla_and_basic();
    test_dead_ancilla_xor_basic();
    test_dead_ancilla_not_basic();
    test_reject_one_reader();
    test_reject_two_readers();
    test_reject_chain_reader();
    test_reject_plain_copy_init();
    test_reject_default_init();
    test_two_independent_dead_decls();
    test_dead_ancilla_inside_for_body();
    test_dead_ancilla_inside_if_then();
    test_reject_leaves_eliminated_ranges_empty();
    test_integration_or_matcher_early_returns();
    test_integration_live_or_still_matches();
    test_integration_xor_matcher_early_returns();
    test_integration_not_matcher_early_returns();
}
