// test_matcher_hoist_invariant.cpp — Phase J PJ-3d tests for the
// `register_hoist_invariant_matcher` uncompute-hoisting pass.
//
// The PJ-3d matcher runs LAST (after every Phase A–I + PJ-1/PJ-4
// matcher). It iterates `unit.scopes` and, for every scope whose
// `detail::classify_scope_kind` is `LoopBody`, scans decl-producing
// ops (OR / AND / NOT / XOR / compare kinds) whose operands are all
// loop-invariant (per `detail::expr_is_loop_invariant`). Matching ops
// have:
//
//   - `hoist_to_override`        set to the loop-enclosing scope's
//                                 close_brace (post-loop uncompute
//                                 anchor).
//   - `insert_before_override`   set to the loop-begin location (pre-
//                                 loop forward-compute anchor).
//
// A defensive `if (op.skip_uncompute) continue;` preserves PH-3
// disjointness — the PH-3 matcher already marks compound-assigns on
// outer vars with `skip_uncompute=true`, and this matcher must NOT
// touch those ops.
//
// These tests drive the matcher end-to-end through a real Clang tooling
// invocation so the parent-chain walk, loop-invariance probe, and
// scope-kind classifier are all exercised in-context.

#include "test_matcher_harness.hpp"

#include "diag_context.hpp"
#include "sturm/transpile/matcher.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_matcher_hoist_invariant_ns {

using namespace sturm::transpile;

namespace {

// Run the Phase E compound matcher + the hoist-invariant matcher on
// `user_src`. We use the compound matcher as the default decl-producing
// driver because it covers OR / AND / NOT shapes via nested VarDecl
// initializers; individual Phase A matchers (OR on a bare `|`, XOR on
// `^`, etc.) would cover slightly different AST shapes, but the
// hoist-invariant matcher consumes QOperations from `unit.scopes`
// regardless of which matcher populated them.
struct HoistRun {
    QUnit unit;
    int   detections = 0;
};

// Drive the hoist-invariant matcher alongside the MVP OR matcher (Phase
// A / M7). The MVP matcher handles `qbool t = a | b;` shapes which are
// exactly the decl-producing shapes the hoist matcher keys off.
HoistRun run_hoist_with_or(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolStub.size() + user_src.size());
    code.append(kQBoolStub);
    code.append(user_src);

    HoistRun out;
    reset_hoist_invariant_detection_count_for_test();

    clang::ast_matchers::MatchFinder finder;
    // Order: the OR matcher populates `unit.scopes[*].ops` with the
    // decl-producing ops. The hoist matcher runs LAST and scans those
    // ops. Registration order reflects the PJ-3e invariant.
    register_or_matcher(finder, out.unit);
    register_hoist_invariant_matcher(finder, out.unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PJ-3d hoist OR)\n");
    }
    out.detections = hoist_invariant_detection_count_for_test();
    return out;
}

// Drive the hoist-invariant matcher alongside the XOR_ASSIGN matchers
// (Phase A PA-3 / PA-4) plus the PH-3 outer-var guard — used to verify
// the `skip_uncompute` disjointness: PH-3 marks `a ^= b;` inside a loop
// when `a` is outer-scoped, and the hoist matcher must NOT touch it.
HoistRun run_hoist_with_xor_assign_and_ph3(std::string_view user_src) {
    // Use a qbool stub with `operator^=` so PA-3/PA-4 match.
    constexpr std::string_view kQBoolXorStub = R"CPP(
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    qbool& operator^=(int) { return *this; }
};
inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
} // namespace sturm
using sturm::qbool;
)CPP";

    std::string code;
    code.reserve(kQBoolXorStub.size() + user_src.size());
    code.append(kQBoolXorStub);
    code.append(user_src);

    HoistRun out;
    reset_hoist_invariant_detection_count_for_test();
    reset_outer_var_guard_detection_count_for_test();

    clang::ast_matchers::MatchFinder finder;
    // PM3-2: the guard matcher requires a DiagContext for its
    // diagnostic path. Unit tests do not own a CompilerInstance, so
    // we build a standalone DiagnosticsEngine with a throwaway
    // IgnoringDiagConsumer — the test asserts on the hoist detection
    // counter, not on formatted stderr output.
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> ids(
        new clang::DiagnosticIDs());
    llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> opts(
        new clang::DiagnosticOptions());
    clang::DiagnosticsEngine diag_engine(
        ids, opts.get(), new clang::IgnoringDiagConsumer(),
        /*ShouldOwnClient=*/true);
    DiagContext diag_ctx(diag_engine);

    register_or_matcher(finder, out.unit);
    register_xor_assign_matcher(finder, out.unit);
    register_xor_assign_classical_matcher(finder, out.unit);
    // PH-3 runs AFTER Phase A matchers so the xor-assign ops it flags
    // with `skip_uncompute=true` are already in `unit.scopes`.
    register_outer_var_guard_matcher(finder, out.unit, diag_ctx);
    // Hoist runs LAST so it observes PH-3's skip flags.
    register_hoist_invariant_matcher(finder, out.unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PJ-3d hoist XOR)\n");
    }
    out.detections = hoist_invariant_detection_count_for_test();
    return out;
}

// ── Tests ─────────────────────────────────────────────────────────────────

// Canonical hoisting shape: `qbool t = a | b;` inside a for-loop body
// where `a` and `b` are outer parameters and never written to in the
// body. Both operands are loop-invariant → the op should have
// `hoist_to_override` (post-loop anchor) AND `insert_before_override`
// (pre-loop anchor) set.
void test_hoist_basic_for_or_invariant() {
    HoistRun r = run_hoist_with_or(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        qbool t = a | b;\n"
        "        (void)t;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t hoisted = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.hoist_to_override.isValid() &&
                op.insert_before_override.isValid()) {
                ++hoisted;
            }
        }
    }
    CHECK(hoisted == 1);
}

// Negative: operand `a` is mutated inside the loop body → NOT
// loop-invariant → op must NOT be hoisted.
void test_hoist_reject_non_invariant_operand() {
    HoistRun r = run_hoist_with_or(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        a = b;\n"
        "        qbool t = a | b;\n"
        "        (void)t;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 0);
    std::size_t hoisted = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.hoist_to_override.isValid()) ++hoisted;
        }
    }
    CHECK(hoisted == 0);
}

// Negative: the scope is NOT a LoopBody (it's a function top scope).
// No op may be hoisted from a function-top scope.
void test_hoist_reject_non_loop_scope() {
    HoistRun r = run_hoist_with_or(
        "void demo(qbool a, qbool b) {\n"
        "    qbool t = a | b;\n"
        "    (void)t;\n"
        "}\n");
    CHECK(r.detections == 0);
    std::size_t hoisted = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.hoist_to_override.isValid()) ++hoisted;
        }
    }
    CHECK(hoisted == 0);
}

// Negative: the scope is an `if`-body (BranchBody), not a LoopBody.
// No op may be hoisted from a branch body.
void test_hoist_reject_if_body_scope() {
    HoistRun r = run_hoist_with_or(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) {\n"
        "        qbool t = a | b;\n"
        "        (void)t;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 0);
    std::size_t hoisted = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.hoist_to_override.isValid()) ++hoisted;
        }
    }
    CHECK(hoisted == 0);
}

// While-loop coverage: the matcher must treat WhileStmt bodies the
// same as ForStmt bodies.
void test_hoist_while_body_invariant() {
    HoistRun r = run_hoist_with_or(
        "void demo(qbool a, qbool b) {\n"
        "    int i = 0;\n"
        "    while (i < 3) {\n"
        "        qbool t = a | b;\n"
        "        (void)t;\n"
        "        ++i;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t hoisted = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.hoist_to_override.isValid() &&
                op.insert_before_override.isValid()) {
                ++hoisted;
            }
        }
    }
    CHECK(hoisted == 1);
}

// PH-3 disjointness: a `qbool a; for (...) { a ^= b; }` shape is flagged
// by PH-3 (`skip_uncompute=true`). The hoist matcher must NOT treat that
// op as a hoist candidate — XOR_ASSIGN is a compound-assign, not a
// decl-producing op, but the defensive `skip_uncompute` guard ensures
// that even if some future change widens the kind set, the PH-3 flag
// keeps those ops off the hoist path.
void test_hoist_respects_ph3_skip_uncompute() {
    HoistRun r = run_hoist_with_xor_assign_and_ph3(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        a ^= b;\n"
        "    }\n"
        "}\n");
    // PH-3 should have flagged exactly one op.
    CHECK(outer_var_guard_detection_count_for_test() == 1);
    // The hoist matcher should NOT have hoisted anything — XOR_ASSIGN
    // is not in the decl-producing kind set AND its skip_uncompute flag
    // is set. Both guards keep the op off the hoist path.
    CHECK(r.detections == 0);
    std::size_t hoisted = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.hoist_to_override.isValid()) ++hoisted;
        }
    }
    CHECK(hoisted == 0);
}

// Nested loops: inner-loop body is a LoopBody whose operands are
// loop-invariant w.r.t. the inner loop. The inner body's decl-producing
// op should be hoisted with `hoist_to_override` landing past the inner
// loop's enclosing scope (the outer loop body).
void test_hoist_nested_loop_inner_invariant() {
    HoistRun r = run_hoist_with_or(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        for (int j = 0; j < 3; ++j) {\n"
        "            qbool t = a | b;\n"
        "            (void)t;\n"
        "        }\n"
        "    }\n"
        "}\n");
    // Exactly one op is eligible — the inner `qbool t = a | b;`. The
    // outer scope has no decl-producing op (only the inner for-stmt),
    // so it contributes nothing to the hoist count.
    CHECK(r.detections == 1);
    std::size_t hoisted = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.hoist_to_override.isValid() &&
                op.insert_before_override.isValid()) {
                ++hoisted;
            }
        }
    }
    CHECK(hoisted == 1);
}

// Braceless for-body coverage: the matcher must treat PH-1 synthetic
// scopes (braceless body stmts) as LoopBody too. `for (...) qbool t = a
// | b;` produces a QScope anchored on the body Stmt, not a
// CompoundStmt, and `classify_scope_kind` returns LoopBody for it.
void test_hoist_braceless_for_body_invariant() {
    HoistRun r = run_hoist_with_or(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) qbool t = a | b;\n"
        "}\n");
    CHECK(r.detections == 1);
    std::size_t hoisted = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.hoist_to_override.isValid() &&
                op.insert_before_override.isValid()) {
                ++hoisted;
            }
        }
    }
    CHECK(hoisted == 1);
}

// Verify the anchors the matcher sets have the correct relative order:
// the `insert_before_override` (pre-loop forward) must be BEFORE the
// `hoist_to_override` (post-loop uncompute) in source order. This
// catches any regression where the two anchors are swapped.
void test_hoist_anchors_have_correct_ordering() {
    HoistRun r = run_hoist_with_or(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        qbool t = a | b;\n"
        "        (void)t;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (!op.hoist_to_override.isValid()) continue;
            // Both anchors must be valid.
            CHECK(op.insert_before_override.isValid());
            // Raw-encoding ordering check: insert_before_override
            // (loop begin) must come BEFORE hoist_to_override (post-loop
            // close). SourceLocation raw encoding is monotonic within
            // a single FileID, so the strict-less comparison is sound
            // for our single-file test input.
            CHECK(op.insert_before_override.getRawEncoding() <
                  op.hoist_to_override.getRawEncoding());
        }
    }
}

} // namespace

}  // namespace sturm_test_matcher_hoist_invariant_ns

void run_hoist_invariant_tests() {
    using namespace sturm_test_matcher_hoist_invariant_ns;
    test_hoist_basic_for_or_invariant();
    test_hoist_reject_non_invariant_operand();
    test_hoist_reject_non_loop_scope();
    test_hoist_reject_if_body_scope();
    test_hoist_while_body_invariant();
    test_hoist_respects_ph3_skip_uncompute();
    test_hoist_nested_loop_inner_invariant();
    test_hoist_braceless_for_body_invariant();
    test_hoist_anchors_have_correct_ordering();
}
