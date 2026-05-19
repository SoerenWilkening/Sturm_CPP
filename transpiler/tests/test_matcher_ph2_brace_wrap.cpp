// test_matcher_ph2_brace_wrap.cpp — Phase H PH-2: auto-brace-wrap matcher.
//
// The PH-2 matcher schedules `{` + `}` raw insertions around every
// braceless for/while/if/else body that transitively contains a
// recognised quantum op. Coverage:
//   - Positive: braceless for / while / if-then / if-else bodies each
//     produce exactly one pair of raw insertions.
//   - Negative: already-braced bodies produce zero raw insertions
//     (PH-2 leaves CompoundStmt bodies alone so existing snapshots
//     stay byte-identical).
//   - Negative: a purely-classical braceless body produces zero
//     insertions — PH-2 must not wrap arbitrary user code.
//   - Regression: WHEN(cond) { ... } does NOT trigger the matcher on
//     its macro-expanded inner `if`s — the `!body_loc.isMacroID()`
//     guard collapses the WHEN tower.

#include "test_matcher_harness.hpp"

#include "sturm/transpile/matcher.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_matcher_ph2_brace_wrap_ns {

using namespace sturm::transpile;

namespace {

// Local stub with `operator|` so the quantum-op probe's
// CXXOperatorCallExpr + argument-type check fires on positive fixtures.
// `should_run()` + a minimal WHEN `if` macro exercise the WHEN-body
// short-circuit. We deliberately do NOT prepend kQBoolWhenStub — a
// simpler stub keeps the positive tests independent of the PF
// materialize_when fixture.
constexpr std::string_view kQBoolPH2Stub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }

} // namespace sturm
using sturm::qbool;

#define WHEN(cond) if (bool _when_val_ = (bool)(cond); _when_val_)
)CPP";

struct PH2Run {
    QUnit unit;
    int detections = 0;
};

PH2Run run_ph2_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolPH2Stub.size() + user_src.size());
    code.append(kQBoolPH2Stub);
    code.append(user_src);

    PH2Run out;
    reset_brace_wrap_detection_count_for_test();

    clang::ast_matchers::MatchFinder finder;
    // Register brace-wrap ALONE so no other matchers contribute ops /
    // insertions that would muddy the raw_insertions assertions.
    register_brace_wrap_matcher(finder, out.unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PH-2 brace_wrap)\n");
    }
    out.detections = brace_wrap_detection_count_for_test();
    return out;
}

void test_ph2_braceless_for_body_is_wrapped() {
    // Braceless for-body containing a qbool VarDecl. PH-2 must schedule
    // two raw insertions: `{` at the body's begin loc and `}` at the
    // loc immediately past the body's terminating `;`.
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) qbool tmp = a | b;\n"
        "}\n");
    CHECK(r.detections == 1);
    CHECK(r.unit.raw_insertions.size() == 2);
    if (r.unit.raw_insertions.size() != 2) return;
    CHECK(r.unit.raw_insertions[0].insert_before.isValid());
    CHECK(r.unit.raw_insertions[1].insert_before.isValid());
    CHECK(r.unit.raw_insertions[0].code.find('{') != std::string::npos);
    CHECK(r.unit.raw_insertions[1].code.find('}') != std::string::npos);
}

void test_ph2_braceless_while_body_is_wrapped() {
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    int i = 0;\n"
        "    while (i < 3) qbool tmp = a | b;\n"
        "}\n");
    CHECK(r.detections == 1);
    CHECK(r.unit.raw_insertions.size() == 2);
}

void test_ph2_braceless_if_then_body_is_wrapped() {
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) qbool tmp = a | b;\n"
        "}\n");
    CHECK(r.detections == 1);
    CHECK(r.unit.raw_insertions.size() == 2);
}

void test_ph2_braceless_if_else_body_is_wrapped() {
    // Only the else-arm is braceless + quantum. The then-arm is
    // already braced, so PH-2 leaves it alone.
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) { (void)a; } else qbool tmp = a | b;\n"
        "}\n");
    CHECK(r.detections == 1);
    CHECK(r.unit.raw_insertions.size() == 2);
}

void test_ph2_braceless_if_both_arms_wrapped() {
    // Both arms are braceless + quantum: the matcher wraps both,
    // producing two pairs (four raw_insertions total).
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) qbool x = a | b; else qbool y = a | b;\n"
        "}\n");
    CHECK(r.detections == 2);
    CHECK(r.unit.raw_insertions.size() == 4);
}

void test_ph2_braced_for_body_is_not_wrapped() {
    // Regression: an already-braced for-body must produce zero raw
    // insertions so existing Phase A-G snapshot fixtures stay
    // byte-identical to pre-PH-2.
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        qbool tmp = a | b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 0);
    CHECK(r.unit.raw_insertions.empty());
}

void test_ph2_braced_if_then_body_is_not_wrapped() {
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) { qbool tmp = a | b; }\n"
        "}\n");
    CHECK(r.detections == 0);
    CHECK(r.unit.raw_insertions.empty());
}

void test_ph2_classical_braceless_for_is_not_wrapped() {
    // Classical-only braceless body: no qbool / qint operand anywhere.
    // PH-2 must NOT wrap, otherwise it churns arbitrary user code.
    PH2Run r = run_ph2_matcher(
        "void demo() {\n"
        "    int counter = 0;\n"
        "    for (int i = 0; i < 3; ++i) counter += 1;\n"
        "}\n");
    CHECK(r.detections == 0);
    CHECK(r.unit.raw_insertions.empty());
}

void test_ph2_when_macro_inner_ifs_not_wrapped() {
    // Regression guard: `WHEN(cond) body;` (braceless body) has a
    // user-spelled statement after `WHEN(cond)` with a non-macro begin
    // loc — PH-2 wraps it exactly once. The macro-expanded inner `if`
    // is skipped by `begin.isMacroID()`. Net: exactly one pair of raw
    // insertions. The guard we are pinning is that PH-2 does NOT fire
    // additionally on the macro-expanded `if`'s synthetic then-arm.
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    WHEN(cond) qbool tmp = a | b;\n"
        "}\n");
    CHECK(r.detections == 1);
    CHECK(r.unit.raw_insertions.size() == 2);
}

void test_ph2_nested_braceless_fors_both_wrapped() {
    // Regression: nested braceless fors — the outer body IS a ForStmt
    // but not a CompoundStmt, so `unless(compoundStmt())` fires. The
    // quantum-op probe walks into the inner for and finds the qbool
    // VarDecl; the outer body qualifies as "contains a quantum op".
    // The inner body is `qbool tmp = a | b;` — also braceless + quantum.
    // Both bodies get wrapped independently.
    PH2Run r = run_ph2_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 2; ++i)\n"
        "        for (int j = 0; j < 2; ++j) qbool tmp = a | b;\n"
        "}\n");
    CHECK(r.detections == 2);
    CHECK(r.unit.raw_insertions.size() == 4);
}

} // namespace

}  // namespace sturm_test_matcher_ph2_brace_wrap_ns

void run_ph2_brace_wrap_tests() {
    using namespace sturm_test_matcher_ph2_brace_wrap_ns;
    test_ph2_braceless_for_body_is_wrapped();
    test_ph2_braceless_while_body_is_wrapped();
    test_ph2_braceless_if_then_body_is_wrapped();
    test_ph2_braceless_if_else_body_is_wrapped();
    test_ph2_braceless_if_both_arms_wrapped();
    test_ph2_braced_for_body_is_not_wrapped();
    test_ph2_braced_if_then_body_is_not_wrapped();
    test_ph2_classical_braceless_for_is_not_wrapped();
    test_ph2_when_macro_inner_ifs_not_wrapped();
    test_ph2_nested_braceless_fors_both_wrapped();
}
