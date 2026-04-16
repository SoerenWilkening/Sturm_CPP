// test_matcher_ph1_scope.cpp — Phase H PH-1: scope finder across braced
// and braceless for / if-then bodies, plus a braced-WHEN regression guard.
//
// The PH-1 refactor replaces the old `enclosing_compound_stmt` walk with
// `enclosing_scope` returning `{kind, anchor}` where kind is CompoundStmt
// (legacy braced) or BracelessBody (new: single-statement body of a
// for/while/if/else). The MVP OR matcher goes through the new helper via
// `find_or_create_scope`, so braceless bodies now produce QScopes that
// the old walk silently dropped.

#include "test_matcher_harness.hpp"

#include "sturm/transpile/matcher.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

void test_ph1_braced_for_body() {
    // Braced for body — legacy shape. One QScope keyed on the inner
    // CompoundStmt. Behaviour byte-identical to pre-PH-1.
    QUnit unit = run_or_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 1; ++i) {\n"
        "        qbool tmp = a | b;\n"
        "        (void)tmp;\n"
        "    }\n"
        "}\n");
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::OR);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("tmp"));
    CHECK(s.open_brace.isValid());
    CHECK(s.close_brace.isValid());
}

void test_ph1_braceless_for_body() {
    // Braceless for body — previously silently rejected by the old
    // `enclosing_compound_stmt` walk. After PH-1 the op surfaces in a
    // synthetic QScope whose open_brace is the body Stmt's begin loc
    // and close_brace is just past the terminating `;`.
    QUnit unit = run_or_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 1; ++i) qbool tmp = a | b;\n"
        "}\n");
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::OR);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("tmp"));
    CHECK(s.open_brace.isValid());
    CHECK(s.close_brace.isValid());
}

void test_ph1_braced_if_then() {
    // Braced if-then — legacy shape. One QScope keyed on the CompoundStmt.
    QUnit unit = run_or_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) {\n"
        "        qbool tmp = a | b;\n"
        "        (void)tmp;\n"
        "    }\n"
        "}\n");
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::OR);
    CHECK(s.open_brace.isValid());
    CHECK(s.close_brace.isValid());
}

void test_ph1_braceless_if_then() {
    // Braceless if-then — previously silently rejected. PH-1 surfaces
    // the op in a synthetic QScope keyed on the body Stmt.
    QUnit unit = run_or_matcher(
        "void demo(qbool a, qbool b, bool cond) {\n"
        "    if (cond) qbool tmp = a | b;\n"
        "}\n");
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::OR);
    CHECK(s.open_brace.isValid());
    CHECK(s.close_brace.isValid());
}

void test_ph1_braced_when_not_misidentified() {
    // Regression: WHEN(cond) { qop; } expands to three nested `if`s
    // whose then-arms walk would naively look like "braceless bodies".
    // PH-1's `is_user_braceless_body` guard rejects macro-spelled body
    // stmts, so the walk continues up past the WHEN tower and lands on
    // the user-written function body's CompoundStmt. The emitted
    // QScope's open_brace raw encoding MUST match the function body's
    // `{`, not any intermediate macro `if`.
    //
    // We drive this through the MVP OR matcher + WHEN macro stub. The
    // OR op lives inside the WHEN body's CompoundStmt (the braces the
    // user wrote), so exactly one QScope is produced corresponding to
    // the user-written scope.
    std::string code;
    code.reserve(kQBoolWhenStub.size() + 128);
    code.append(kQBoolWhenStub);
    code.append(
        "void demo(qbool a, qbool b, qbool c) {\n"
        "    WHEN(a) {\n"
        "        qbool tmp = b | c;\n"
        "        (void)tmp;\n"
        "    }\n"
        "}\n");

    QUnit unit;
    clang::ast_matchers::MatchFinder finder;
    register_or_matcher(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PH-1 braced WHEN "
                     "regression)\n");
    }
    // Exactly one QScope: the one the user wrote around the WHEN body.
    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::OR);
    CHECK(s.open_brace.isValid());
    // The open_brace must NOT be a macro-ID loc — the user-written
    // body opens at a file spelling; macro-synthetic braces would
    // spell inside the expansion.
    CHECK(!s.open_brace.isMacroID());
}

} // namespace

void run_ph1_scope_tests() {
    test_ph1_braced_for_body();
    test_ph1_braceless_for_body();
    test_ph1_braced_if_then();
    test_ph1_braceless_if_then();
    test_ph1_braced_when_not_misidentified();
}
