// test_matcher_when_lift.cpp — Phase F PF-2 / PF-3 WHEN-lift matcher.
//
// Drives the lift detection signal (PF-2) and the source-edit rewrite
// path (PF-3) through the shared kQBoolWhenStub from the harness. PF-2
// was detection-only; PF-3 adds one QReplacement (the inner arg rewrite),
// one raw_insertions entry (the flat decl block) and one QOperation on
// the enclosing scope per matched lift.

#include "test_matcher_harness.hpp"

#include "sturm/transpile/matcher.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_matcher_when_lift_ns {

using namespace sturm::transpile;

namespace {

struct PFTwoRun {
    QUnit unit;
    int detections = 0;
};

PFTwoRun run_pf_when_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolWhenStub.size() + user_src.size());
    code.append(kQBoolWhenStub);
    code.append(user_src);

    PFTwoRun out;
    reset_when_lift_detection_count_for_test();

    clang::ast_matchers::MatchFinder finder;
    register_when_lift_matcher(finder, out.unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PF-2 WHEN matcher)\n");
    }
    out.detections = when_lift_detection_count_for_test();
    return out;
}

void test_pf_when_compound_detects_once() {
    // WHEN(b | c) must fire the PF-3 rewrite path exactly once:
    //   (1) one QReplacement on the `b | c` spelling range → `__stu_t0`,
    //   (2) one raw_insertions entry with `qbool __stu_t0 = b | c;\n`,
    //   (3) one QOperation{kind=OR, result=__stu_t0, operands=[b,c]}
    //       with a valid `insert_before_override` (post-WHEN-body brace).
    PFTwoRun r = run_pf_when_matcher(
        "void demo(qbool a, qbool b, qbool c) {\n"
        "    WHEN(b | c) { (void)a; }\n"
        "}\n");
    CHECK(r.detections == 1);

    CHECK(r.unit.replacements.size() == 1);
    if (r.unit.replacements.size() == 1) {
        CHECK_EQ_STR(r.unit.replacements[0].replacement,
                     std::string("__stu_t0"));
        CHECK(r.unit.replacements[0].range.isValid());
    }

    CHECK(r.unit.raw_insertions.size() == 1);
    if (r.unit.raw_insertions.size() == 1) {
        // PM2-4: the raw insertion now carries a leading `#line`
        // directive anchored at the replacement's `range.getBegin()`
        // (the spelling begin of the user's WHEN argument). Assert
        // the trailing decl and the directive substring rather than
        // a strict byte equality — the directive's line number and
        // basename depend on the harness's source layout.
        const std::string& code = r.unit.raw_insertions[0].code;
        CHECK(code.find("qbool __stu_t0 = b | c;\n") != std::string::npos);
        CHECK(code.find("#line ") != std::string::npos);
        CHECK(r.unit.raw_insertions[0].insert_before.isValid());
    }

    CHECK(r.unit.scopes.size() == 1);
    if (r.unit.scopes.size() == 1) {
        const auto& s = r.unit.scopes.front();
        CHECK(s.ops.size() == 1);
        if (s.ops.size() == 1) {
            const auto& op = s.ops.front();
            CHECK(op.kind == QOpKind::OR);
            CHECK_EQ_STR(op.result.name, std::string("__stu_t0"));
            CHECK(op.operands.size() == 2);
            if (op.operands.size() == 2) {
                CHECK_EQ_STR(op.operands[0].name, std::string("b"));
                CHECK_EQ_STR(op.operands[1].name, std::string("c"));
            }
            CHECK(op.insert_before_override.isValid());
        }
    }
}

void test_pf_when_named_passthrough_short_circuits() {
    // WHEN(named_qbool): the materialize argument peels to a bare
    // DeclRefExpr to a named qbool — the callback takes the named-
    // passthrough short-circuit and records zero detections. The
    // QUnit must be entirely untouched so the emitter's output is
    // byte-identical to the input.
    PFTwoRun r = run_pf_when_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    WHEN(a) { (void)b; }\n"
        "}\n");
    CHECK(r.detections == 0);
    CHECK(r.unit.scopes.empty());
    CHECK(r.unit.replacements.empty());
    CHECK(r.unit.raw_insertions.empty());
}

void test_pf_when_not_lifts_once() {
    // WHEN(~a) must lift via flatten_arg's unary-NOT branch, producing
    // one QReplacement, one raw_insertions entry, and one QOperation
    // {kind=NOT, result=__stu_t0, operands=[a]}.
    PFTwoRun r = run_pf_when_matcher(
        "namespace sturm { inline qbool operator~(const qbool&)"
        "    { return qbool{}; } }\n"
        "void demo(qbool a, qbool b) {\n"
        "    WHEN(~a) { (void)b; }\n"
        "}\n");
    CHECK(r.detections == 1);

    CHECK(r.unit.replacements.size() == 1);
    if (r.unit.replacements.size() == 1) {
        CHECK_EQ_STR(r.unit.replacements[0].replacement,
                     std::string("__stu_t0"));
    }

    CHECK(r.unit.raw_insertions.size() == 1);
    if (r.unit.raw_insertions.size() == 1) {
        // PM2-4: same substring-only assertion as the OR case; the
        // raw insertion now carries a leading `#line` directive.
        const std::string& code = r.unit.raw_insertions[0].code;
        CHECK(code.find("qbool __stu_t0 = ~a;\n") != std::string::npos);
        CHECK(code.find("#line ") != std::string::npos);
    }

    CHECK(r.unit.scopes.size() == 1);
    if (r.unit.scopes.size() == 1) {
        const auto& s = r.unit.scopes.front();
        CHECK(s.ops.size() == 1);
        if (s.ops.size() == 1) {
            const auto& op = s.ops.front();
            CHECK(op.kind == QOpKind::NOT);
            CHECK_EQ_STR(op.result.name, std::string("__stu_t0"));
            CHECK(op.operands.size() == 1);
            if (op.operands.size() == 1) {
                CHECK_EQ_STR(op.operands[0].name, std::string("a"));
            }
            CHECK(op.insert_before_override.isValid());
        }
    }
}

void test_pf_when_bare_if_no_match() {
    // A plain `if (auto x = f(); ...)` init-stmt is NOT inside any macro
    // body expansion. The `isMacroBodyExpansion` guard rejects. We name
    // the local `_when_val_` here to prove the match-shape alone is not
    // sufficient — the macro-body guard is load-bearing.
    PFTwoRun r = run_pf_when_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    if (decltype(auto) _when_val_ = \n"
        "            ::sturm::detail::materialize_when(a | b); true) {\n"
        "        (void)b;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 0);
}

void test_pf_when_direct_materialize_call_no_match() {
    // A direct user-level call to `materialize_when(...)` OUTSIDE any
    // WHEN macro body must NOT match. The `if`-anchored pattern rules
    // this out at the pattern level; the macro-body guard rejects
    // anyway as a second line of defence.
    PFTwoRun r = run_pf_when_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    auto x = ::sturm::detail::materialize_when(a | b);\n"
        "    (void)x;\n"
        "}\n");
    CHECK(r.detections == 0);
}

} // namespace

}  // namespace sturm_test_matcher_when_lift_ns

void run_when_lift_tests() {
    using namespace sturm_test_matcher_when_lift_ns;
    test_pf_when_compound_detects_once();
    test_pf_when_named_passthrough_short_circuits();
    test_pf_when_not_lifts_once();
    test_pf_when_bare_if_no_match();
    test_pf_when_direct_materialize_call_no_match();
}
