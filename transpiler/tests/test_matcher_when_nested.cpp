// test_matcher_when_nested.cpp — Phase G PG-1/2/3 nested-WHEN matcher.
//
// The Phase G matcher detects adjacent pairs of `WHEN(outer) { WHEN(inner)
// { ... } }` where BOTH args peel to bare DeclRefExpr naming non-synthetic
// qbools. PG-1 was detection-only; PG-2 added one QReplacement (inner arg
// rewrite) + one raw_insertions entry (`qbool __stu_ctrl<M> = outer & inner;`)
// per pair; PG-3 added one synthetic QOperation{kind=AND} on the enclosing
// scope with `insert_before_override` anchored past the inner WHEN body's
// closing brace.

#include "test_matcher_harness.hpp"

#include "sturm/transpile/matcher.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

struct PGOneRun {
    QUnit unit;
    int detections = 0;
};

PGOneRun run_pg_when_nested_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolWhenStub.size() + user_src.size());
    code.append(kQBoolWhenStub);
    code.append(user_src);

    PGOneRun out;
    reset_when_nested_detection_count_for_test();

    clang::ast_matchers::MatchFinder finder;
    register_when_nested_matcher(finder, out.unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PG-1 nested WHEN "
                     "matcher)\n");
    }
    out.detections = when_nested_detection_count_for_test();
    return out;
}

void test_pg_when_nested_named_depth2_detects_once() {
    // WHEN(a) { WHEN(b) { ... } } — the named+named base case. The inner
    // WHEN finds outer `a` as its nearest enclosing WHEN; both args peel
    // to bare DREs; detection counter reaches 1. PG-2 stages ONE
    // replacement + ONE raw insertion; PG-3 adds ONE AND op with a
    // valid `insert_before_override`.
    PGOneRun r = run_pg_when_nested_matcher(
        "void demo(qbool a, qbool b, qbool c) {\n"
        "    WHEN(a) { WHEN(b) { (void)c; } }\n"
        "}\n");
    CHECK(r.detections == 1);
    CHECK(r.unit.scopes.size() == 1);
    CHECK(r.unit.replacements.size() == 1);
    CHECK(r.unit.raw_insertions.size() == 1);
    if (r.unit.replacements.size() == 1) {
        CHECK_EQ_STR(r.unit.replacements[0].replacement,
                     std::string("__stu_ctrl0"));
    }
    if (r.unit.raw_insertions.size() == 1) {
        CHECK_EQ_STR(r.unit.raw_insertions[0].code,
                     std::string("qbool __stu_ctrl0 = a & b;\n"));
    }
    if (r.unit.scopes.size() == 1) {
        const auto& ops = r.unit.scopes[0].ops;
        CHECK(ops.size() == 1);
        if (ops.size() == 1) {
            CHECK(ops[0].kind == QOpKind::AND);
            CHECK_EQ_STR(ops[0].result.name, std::string("__stu_ctrl0"));
            CHECK(ops[0].operands.size() == 2);
            if (ops[0].operands.size() == 2) {
                CHECK_EQ_STR(ops[0].operands[0].name, std::string("a"));
                CHECK_EQ_STR(ops[0].operands[1].name, std::string("b"));
            }
            CHECK(ops[0].insert_before_override.isValid());
        }
    }
}

void test_pg_when_nested_named_depth3_detects_twice() {
    // WHEN(a) { WHEN(b) { WHEN(c) { ... } } } — the pairwise cascade:
    //   - inner `c` finds nearest outer `b` → pair 1
    //   - middle `b` finds nearest outer `a` → pair 2
    // `c` does NOT transitively pair with `a` because the ParentMap walk
    // stops at the nearest enclosing WHEN ancestor. Fresh-name allocator
    // picks `__stu_ctrl0` / `__stu_ctrl1` without colliding. The two
    // AND ops land in DIFFERENT QScopes because each pair's enclosing
    // CompoundStmt is the body of a distinct WHEN.
    PGOneRun r = run_pg_when_nested_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d) {\n"
        "    WHEN(a) { WHEN(b) { WHEN(c) { (void)d; } } }\n"
        "}\n");
    CHECK(r.detections == 2);
    CHECK(r.unit.replacements.size() == 2);
    CHECK(r.unit.raw_insertions.size() == 2);
    // MatchFinder callback order is NOT guaranteed, so we assert the
    // cumulative op count rather than per-scope placement.
    std::size_t total_ops = 0;
    for (const auto& s : r.unit.scopes) total_ops += s.ops.size();
    CHECK(total_ops == 2);
}

void test_pg_when_nested_compound_inner_rejected() {
    // WHEN(a) { WHEN(b | c) { ... } } — compound inner. The inner WHEN's
    // materialize arg peels to a CXXOperatorCallExpr (not a bare DRE) so
    // the named-only guard rejects the pair. Phase F handles this
    // inner's compound lift separately; Phase G stays out.
    PGOneRun r = run_pg_when_nested_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d) {\n"
        "    WHEN(a) { WHEN(b | c) { (void)d; } }\n"
        "}\n");
    CHECK(r.detections == 0);
    CHECK(r.unit.replacements.empty());
    CHECK(r.unit.raw_insertions.empty());
}

void test_pg_when_nested_compound_outer_rejected() {
    // WHEN((b | c)) { WHEN(d) { ... } } — compound outer. From the
    // inner WHEN we walk up to the outer WHEN IfStmt; the outer's
    // materialize arg peels to a CXXOperatorCallExpr; named-only guard
    // rejects the pair. Inner `d` by itself remains on the runtime path.
    PGOneRun r = run_pg_when_nested_matcher(
        "void demo(qbool b, qbool c, qbool d, qbool e) {\n"
        "    WHEN((b | c)) { WHEN(d) { (void)e; } }\n"
        "}\n");
    CHECK(r.detections == 0);
    CHECK(r.unit.replacements.empty());
    CHECK(r.unit.raw_insertions.empty());
}

void test_pg_when_nested_siblings_tolerated() {
    // WHEN(a) { foo(); WHEN(b) { body } bar(); } — siblings around the
    // inner WHEN. The Phase G plan's decision is "always lower
    // regardless of siblings"; detection must fire regardless of
    // sibling statements before/after the inner WHEN.
    PGOneRun r = run_pg_when_nested_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d) {\n"
        "    WHEN(a) {\n"
        "        (void)c;\n"
        "        WHEN(b) { (void)d; }\n"
        "        (void)c;\n"
        "    }\n"
        "}\n");
    CHECK(r.detections == 1);
    CHECK(r.unit.replacements.size() == 1);
    CHECK(r.unit.raw_insertions.size() == 1);
    std::size_t total_ops = 0;
    for (const auto& s : r.unit.scopes) total_ops += s.ops.size();
    CHECK(total_ops == 1);
}

} // namespace

void run_when_nested_tests() {
    test_pg_when_nested_named_depth2_detects_once();
    test_pg_when_nested_named_depth3_detects_twice();
    test_pg_when_nested_compound_inner_rejected();
    test_pg_when_nested_compound_outer_rejected();
    test_pg_when_nested_siblings_tolerated();
}
