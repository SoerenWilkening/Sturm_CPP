// test_matcher_pn_rotation.cpp — Phase N PN-2: rotation matcher.
//
// The PN-2 rotation matcher anchors on `q.theta() += d;` / `q.theta() -= d;`
// / `q.phi() += d;` / `q.phi() -= d;` where `q` is a `qint_t<W>` and `d` is
// a plain double-valued expression. The AST anchor is one level deeper than
// Phase B: the LHS of the `operator+=` / `operator-=` is a
// `cxxMemberCallExpr` (`theta()` / `phi()`) whose implicit object argument
// is a `declRefExpr` to the qint_t. The RHS is a plain `Expr` (no
// `CXXConstructExpr` peel — the `double` parameter of the proxy's
// `operator+=` does not go through a converting constructor).
//
// Each positive case asserts (a) one QOperation is pushed with the
// expected QOpKind, (b) the LHS result name matches the qint ident, and
// (c) the operand name is the verbatim RHS source text captured via
// `Lexer::getSourceText`. Negative cases pin the matcher's disjointness
// against Phase B (`qint_t += C`), Phase C (`qint_t += qint_t`), and the
// Phase A / E qbool matchers (no theta() / phi() call).

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

// Minimal `sturm::qint_t<W>` stub with nested `ThetaProxy` / `PhiProxy`
// types, each exposing `operator+=(double)` / `operator-=(double)`. The
// real production runtime's `ThetaProxy::operator-=(double)` forwards to
// `operator+=(-delta)`; the stub's bodies are empty because the matcher
// only inspects AST structure, not semantic execution.
//
// `theta()` / `phi()` are value-returning member functions so the `q.theta()`
// call-site is a `CXXMemberCallExpr` (callee: `cxxMethodDecl(hasName
// ("theta"))`) whose implicit object argument peels to the `q`
// `DeclRefExpr` — the shape the PN-2 matcher keys off.
constexpr std::string_view kQIntRotationStub = R"CPP(
namespace sturm {

template <int W>
class qint_t {
public:
    struct ThetaProxy {
        void operator+=(double) {}
        void operator-=(double) {}
    };
    struct PhiProxy {
        void operator+=(double) {}
        void operator-=(double) {}
    };

    qint_t() {}
    qint_t(const qint_t&) {}
    // Phase B converting ctor — kept so negative disjointness fixtures can
    // exercise the `a += 3;` shape alongside the rotation shape.
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(long long) {}
    qint_t& operator+=(const qint_t&) { return *this; }
    qint_t& operator-=(const qint_t&) { return *this; }

    ThetaProxy theta() { return ThetaProxy{}; }
    PhiProxy   phi()   { return PhiProxy{}; }
};

} // namespace sturm

using qint_t = sturm::qint_t<1>;
)CPP";

QUnit run_pn_rotation_matchers(std::string_view user_src) {
    std::string code;
    code.reserve(kQIntRotationStub.size() + user_src.size());
    code.append(kQIntRotationStub);
    code.append(user_src);

    QUnit unit;
    clang::ast_matchers::MatchFinder finder;
    register_theta_add_matcher(finder, unit);
    register_theta_sub_matcher(finder, unit);
    register_phi_add_matcher(finder, unit);
    register_phi_sub_matcher(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PN rotation matchers)\n");
    }
    return unit;
}

// ── Positive cases ──────────────────────────────────────────────────────────

void test_pn_theta_add_binds_literal() {
    // `q.theta() += 0.5;` on qint_t must produce THETA_ADD_ASSIGN_CONST
    // with result name "q" and operand name "0.5" (verbatim double
    // literal text — no CXXConstructExpr peel needed because the RHS is
    // already a `double`-valued Expr).
    QUnit unit = run_pn_rotation_matchers(
        "void demo(qint_t q) {\n"
        "    q.theta() += 0.5;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;

    const auto& op = s.ops.front();
    CHECK(op.kind == QOpKind::THETA_ADD_ASSIGN_CONST);
    CHECK_EQ_STR(op.result.name, std::string("q"));
    CHECK(op.operands.size() == 1);
    if (op.operands.empty()) return;
    CHECK_EQ_STR(op.operands[0].name, std::string("0.5"));
}

void test_pn_theta_sub_binds_literal() {
    QUnit unit = run_pn_rotation_matchers(
        "void demo(qint_t q) {\n"
        "    q.theta() -= 0.25;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::THETA_SUB_ASSIGN_CONST);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("q"));
    CHECK(s.ops.front().operands.size() == 1);
    if (!s.ops.front().operands.empty()) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("0.25"));
    }
}

void test_pn_phi_add_binds_literal() {
    QUnit unit = run_pn_rotation_matchers(
        "void demo(qint_t q) {\n"
        "    q.phi() += 0.7;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::PHI_ADD_ASSIGN_CONST);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("q"));
    CHECK(s.ops.front().operands.size() == 1);
    if (!s.ops.front().operands.empty()) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("0.7"));
    }
}

void test_pn_phi_sub_binds_literal() {
    QUnit unit = run_pn_rotation_matchers(
        "void demo(qint_t q) {\n"
        "    q.phi() -= 0.1;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::PHI_SUB_ASSIGN_CONST);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("q"));
    CHECK(s.ops.front().operands.size() == 1);
    if (!s.ops.front().operands.empty()) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("0.1"));
    }
}

void test_pn_theta_add_binds_variable_rhs_verbatim() {
    // The RHS is captured verbatim via `Lexer::getSourceText`, so a
    // non-literal `double` variable flows through unchanged. This pins
    // the Phase N §14 risk 2 contract: any pure-value double-typed RHS
    // expression is acceptable; side-effect is the user's responsibility.
    QUnit unit = run_pn_rotation_matchers(
        "void demo(qint_t q, double d) {\n"
        "    q.theta() += d;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 1);
    if (s.ops.empty()) return;
    CHECK(s.ops.front().kind == QOpKind::THETA_ADD_ASSIGN_CONST);
    CHECK_EQ_STR(s.ops.front().result.name, std::string("q"));
    CHECK(s.ops.front().operands.size() == 1);
    if (!s.ops.front().operands.empty()) {
        CHECK_EQ_STR(s.ops.front().operands[0].name, std::string("d"));
    }
}

void test_pn_all_four_directions_fire_independently() {
    // A user that mixes all four rotation kinds in one function must
    // produce four independent QOperations in source order. The m12
    // gate-equivalence pair (PN-8) depends on this ordering invariant
    // so the LIFO uncompute rendering lines up with the counter-mode
    // GateRecord stream.
    QUnit unit = run_pn_rotation_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    a.theta() += 0.3;\n"
        "    a.theta() -= 0.1;\n"
        "    b.phi()   += 0.7;\n"
        "    b.phi()   -= 0.2;\n"
        "}\n");

    CHECK(unit.scopes.size() == 1);
    if (unit.scopes.empty()) return;
    const auto& s = unit.scopes.front();
    CHECK(s.ops.size() == 4);
    if (s.ops.size() != 4) return;
    CHECK(s.ops[0].kind == QOpKind::THETA_ADD_ASSIGN_CONST);
    CHECK(s.ops[1].kind == QOpKind::THETA_SUB_ASSIGN_CONST);
    CHECK(s.ops[2].kind == QOpKind::PHI_ADD_ASSIGN_CONST);
    CHECK(s.ops[3].kind == QOpKind::PHI_SUB_ASSIGN_CONST);
    CHECK_EQ_STR(s.ops[0].result.name, std::string("a"));
    CHECK_EQ_STR(s.ops[1].result.name, std::string("a"));
    CHECK_EQ_STR(s.ops[2].result.name, std::string("b"));
    CHECK_EQ_STR(s.ops[3].result.name, std::string("b"));
}

// ── Negative cases ──────────────────────────────────────────────────────────

void test_pn_negative_qint_const_form_no_match() {
    // Phase B shape — `a += 3;` on a qint_t. The LHS of the outer
    // `operator+=` is a `DeclRefExpr` to the qint_t directly (NOT a
    // `cxxMemberCallExpr(callee(hasName("theta")))`). The PN-2 matcher
    // must not fire on this shape.
    QUnit unit = run_pn_rotation_matchers(
        "void demo(qint_t a) {\n"
        "    a += 3;\n"
        "    a -= 7;\n"
        "}\n");

    // No rotation matcher fires; `unit.scopes` stays empty because only
    // the PN-2 matchers are registered by this harness.
    CHECK(unit.scopes.empty());
}

void test_pn_negative_qint_qint_form_no_match() {
    // Phase C shape — `a += b;` on qint_t. Same reasoning as the Phase B
    // negative: the LHS is a bare DeclRefExpr, not a member call.
    QUnit unit = run_pn_rotation_matchers(
        "void demo(qint_t a, qint_t b) {\n"
        "    a += b;\n"
        "    a -= b;\n"
        "}\n");

    CHECK(unit.scopes.empty());
}

void test_pn_negative_non_qint_member_call_no_match() {
    // A user writes `other.theta() += 0.5;` where `other` is a type that
    // happens to expose a `theta()` method returning a proxy with a
    // `double` `operator+=`. The qint_guard on the implicit-object
    // `declRefExpr` must restrict the match to `sturm::qint_t<W>`; any
    // other record type must NOT fire.
    const std::string user_src = R"CPP(
namespace other {
struct FakeProxy {
    void operator+=(double) {}
    void operator-=(double) {}
};
struct Other {
    FakeProxy theta() { return FakeProxy{}; }
    FakeProxy phi()   { return FakeProxy{}; }
};
} // namespace other

void demo(other::Other x) {
    x.theta() += 0.5;
    x.phi()   -= 0.3;
}
)CPP";
    QUnit unit = run_pn_rotation_matchers(user_src);
    CHECK(unit.scopes.empty());
}

} // namespace

void run_pn_rotation_tests() {
    test_pn_theta_add_binds_literal();
    test_pn_theta_sub_binds_literal();
    test_pn_phi_add_binds_literal();
    test_pn_phi_sub_binds_literal();
    test_pn_theta_add_binds_variable_rhs_verbatim();
    test_pn_all_four_directions_fire_independently();
    test_pn_negative_qint_const_form_no_match();
    test_pn_negative_qint_qint_form_no_match();
    test_pn_negative_non_qint_member_call_no_match();
}
