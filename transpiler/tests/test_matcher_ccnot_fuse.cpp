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

    // One QReplacement over both stmts.
    CHECK(unit.replacements.size() == 1);
    if (!unit.replacements.empty()) {
        CHECK_EQ_STR(unit.replacements.front().replacement,
                     std::string("ccnot_inplace(x, a, b);"));
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
    if (!unit.replacements.empty()) {
        CHECK_EQ_STR(unit.replacements.front().replacement,
                     std::string("ccnot_inplace(x, a, b);"));
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
}
