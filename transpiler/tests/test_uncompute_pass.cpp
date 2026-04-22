// test_uncompute_pass.cpp — unit tests for the M8 uncompute synthesis pass.
//
// M8 takes a QUnit (produced by the M7 matcher) and returns a flat vector of
// UncomputeInsertion records describing what text to inject and at which
// SourceLocation. The pass performs NO AST rewriting — that's M9.
//
// The two properties under test here are:
//
//   1. **LIFO ordering within a scope.** The whole PRD hinges on emitting
//      inverses in reverse of the forward order. Given `tmp0 = a|b; tmp1 =
//      c|d;` the pass must emit `uncompute_or(tmp1, c, d);` BEFORE
//      `uncompute_or(tmp0, a, b);` in the insertion list. The reviewer for
//      M8 will check this explicitly.
//
//   2. **Scope anchoring.** Each insertion's `insert_before` must be the
//      close-brace of the scope that originally contained the op, even when
//      the QUnit has multiple scopes. Cross-scope contamination would mean
//      the emitter inserts a `uncompute_or` into the wrong block.
//
// Tests are intentionally *synthetic*: we build QUnits by hand, no Clang
// tool invocation. This keeps the suite in the microsecond range and makes
// ordering regressions reproducible without a compiler.

#include "sturm/transpile/qir.hpp"
#include "sturm/transpile/uncompute_pass.hpp"
// PM4-3: exercise the new `case QOpKind::PLUGIN:` arm in
// `render_uncompute`. The test hand-builds a `Registry`, registers a
// render function under a kind_id, constructs a `QOperation` with
// `kind = QOpKind::PLUGIN` + `plugin_kind_id = kind_id`, and asserts
// `synthesize(unit, sm, &registry)` emits the text the registered
// render function returns.
#include "sturm/transpile/plugin_api.hpp"

#include "clang/Basic/SourceLocation.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace sturm::transpile;

// ── Test harness ──────────────────────────────────────────────────────────────
static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                              \
    ++tests_run;                                                      \
    if (cond) { ++tests_pass; }                                       \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                     \
                     __FILE__, __LINE__, #cond);                      \
    }                                                                 \
} while (0)

#define CHECK_EQ_STR(got, want) do {                                  \
    ++tests_run;                                                      \
    if ((got) == (want)) { ++tests_pass; }                            \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  strings differ\n"          \
                             "  got:  <<<%s>>>\n"                     \
                             "  want: <<<%s>>>\n",                    \
                     __FILE__, __LINE__,                              \
                     std::string(got).c_str(),                        \
                     std::string(want).c_str());                      \
    }                                                                 \
} while (0)

#define CHECK_EQ_SIZE(got, want) do {                                 \
    ++tests_run;                                                      \
    if (static_cast<std::size_t>(got) ==                              \
        static_cast<std::size_t>(want)) { ++tests_pass; }             \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  sizes differ "             \
                             "got=%zu want=%zu\n",                    \
                     __FILE__, __LINE__,                              \
                     static_cast<std::size_t>(got),                   \
                     static_cast<std::size_t>(want));                 \
    }                                                                 \
} while (0)

// SourceLocation is opaque value type; its raw encoding 0 is "invalid". Any
// non-zero raw encoding yields a valid-looking location for equality checks.
static clang::SourceLocation make_loc(std::uint32_t raw) {
    return clang::SourceLocation::getFromRawEncoding(raw);
}

static std::uint32_t raw(clang::SourceLocation loc) {
    return loc.getRawEncoding();
}

// ── Test: single op → single insertion, anchored at scope close_brace ───────

static void test_single_op_one_insertion() {
    // Hand-built QUnit representing:
    //   { qbool tmp = a | b; }
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::OR;
    op.result = QValueRef{"tmp", make_loc(30)};
    op.operands.push_back(QValueRef{"a", make_loc(20)});
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    // Code must match the exact format documented in the PRD and M8 plan.
    // Four-space indent, function name `uncompute_or`, result then operands.
    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_or(tmp, a, b);\n"));

    // Insertion point is the scope's close brace — emitter injects BEFORE it.
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

// ── Test: two ops in one scope → LIFO order ─────────────────────────────────

static void test_two_ops_lifo_order() {
    // Hand-built QUnit representing:
    //   { qbool tmp0 = a | b;
    //     qbool tmp1 = c | d; }
    // The forward order is [tmp0, tmp1]. LIFO uncompute ordering requires
    // the insertion list to emit the inverse for `tmp1` FIRST, then `tmp0`.
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(99);

    QOperation op0;
    op0.kind   = QOpKind::OR;
    op0.result = QValueRef{"tmp0", make_loc(10)};
    op0.operands = { QValueRef{"a", make_loc(2)},
                     QValueRef{"b", make_loc(3)} };
    op0.stmt_range = clang::SourceRange(make_loc(10), make_loc(20));

    QOperation op1;
    op1.kind   = QOpKind::OR;
    op1.result = QValueRef{"tmp1", make_loc(30)};
    op1.operands = { QValueRef{"c", make_loc(4)},
                     QValueRef{"d", make_loc(5)} };
    op1.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));

    scope.ops = { op0, op1 };

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 2u);
    if (ins.size() != 2) return;

    // LIFO: tmp1 uncompute comes FIRST, tmp0 uncompute comes SECOND. This is
    // the whole reason M8 exists as its own pass — if either line below
    // flips, the post-MVP roadmap falls over.
    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_or(tmp1, c, d);\n"));
    CHECK_EQ_STR(ins[1].code,
                 std::string("    uncompute_or(tmp0, a, b);\n"));

    // Both insertions anchored at the same scope's close_brace.
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(99)));
    CHECK_EQ_SIZE(raw(ins[1].insert_before), raw(make_loc(99)));
}

// ── Test: two scopes with one op each → each anchored to own close_brace ────

static void test_two_scopes_each_one_op() {
    // Hand-built QUnit representing two sibling compound statements:
    //   { qbool x = p | q; }   // close_brace @ 70
    //   { qbool y = r | s; }   // close_brace @ 200
    // Each insertion must target its own scope's close brace; contamination
    // across scopes would put an uncompute_or in the wrong block.
    QUnit unit;

    {
        QScope s;
        s.open_brace  = make_loc(10);
        s.close_brace = make_loc(70);

        QOperation op;
        op.kind   = QOpKind::OR;
        op.result = QValueRef{"x", make_loc(20)};
        op.operands = { QValueRef{"p", make_loc(12)},
                        QValueRef{"q", make_loc(14)} };
        op.stmt_range = clang::SourceRange(make_loc(20), make_loc(30));
        s.ops.push_back(op);
        unit.scopes.push_back(s);
    }
    {
        QScope s;
        s.open_brace  = make_loc(100);
        s.close_brace = make_loc(200);

        QOperation op;
        op.kind   = QOpKind::OR;
        op.result = QValueRef{"y", make_loc(150)};
        op.operands = { QValueRef{"r", make_loc(110)},
                        QValueRef{"s", make_loc(120)} };
        op.stmt_range = clang::SourceRange(make_loc(150), make_loc(160));
        s.ops.push_back(op);
        unit.scopes.push_back(s);
    }

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 2u);
    if (ins.size() != 2) return;

    // Scopes are processed in source order (the order they appear in
    // `unit.scopes`), so scope 0's insertion(s) come before scope 1's.
    CHECK_EQ_STR(ins[0].code, std::string("    uncompute_or(x, p, q);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(70)));

    CHECK_EQ_STR(ins[1].code, std::string("    uncompute_or(y, r, s);\n"));
    CHECK_EQ_SIZE(raw(ins[1].insert_before), raw(make_loc(200)));
}

// ── Empty-unit and empty-scope edge cases ────────────────────────────────────

static void test_empty_unit() {
    QUnit unit;
    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

static void test_empty_scope() {
    // A scope that the matcher discovered (e.g. because it contained some
    // other quantum op that M7 ignored) but which has no OR ops produces
    // no insertions.
    QUnit unit;
    QScope s;
    s.open_brace  = make_loc(1);
    s.close_brace = make_loc(2);
    // no ops
    unit.scopes.push_back(s);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

// ── Multi-scope with multiple ops ────────────────────────────────────────────

static void test_multi_scope_multi_op_lifo() {
    // Confirms that LIFO is per-scope: within each scope we reverse, but the
    // scope order itself is preserved.
    QUnit unit;
    {
        QScope s;
        s.open_brace  = make_loc(1);
        s.close_brace = make_loc(50);
        QOperation op0;
        op0.kind = QOpKind::OR;
        op0.result = QValueRef{"a0", make_loc(2)};
        op0.operands = { QValueRef{"x", make_loc(3)},
                         QValueRef{"y", make_loc(4)} };
        QOperation op1;
        op1.kind = QOpKind::OR;
        op1.result = QValueRef{"a1", make_loc(5)};
        op1.operands = { QValueRef{"x", make_loc(3)},
                         QValueRef{"z", make_loc(6)} };
        s.ops = { op0, op1 };
        unit.scopes.push_back(s);
    }
    {
        QScope s;
        s.open_brace  = make_loc(100);
        s.close_brace = make_loc(300);
        QOperation op0;
        op0.kind = QOpKind::OR;
        op0.result = QValueRef{"b0", make_loc(110)};
        op0.operands = { QValueRef{"m", make_loc(111)},
                         QValueRef{"n", make_loc(112)} };
        QOperation op1;
        op1.kind = QOpKind::OR;
        op1.result = QValueRef{"b1", make_loc(120)};
        op1.operands = { QValueRef{"m", make_loc(111)},
                         QValueRef{"p", make_loc(113)} };
        s.ops = { op0, op1 };
        unit.scopes.push_back(s);
    }

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 4u);
    if (ins.size() != 4) return;

    // Scope 0 (close_brace=50): LIFO → a1 first, a0 second.
    CHECK_EQ_STR(ins[0].code, std::string("    uncompute_or(a1, x, z);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
    CHECK_EQ_STR(ins[1].code, std::string("    uncompute_or(a0, x, y);\n"));
    CHECK_EQ_SIZE(raw(ins[1].insert_before), raw(make_loc(50)));

    // Scope 1 (close_brace=300): LIFO → b1 first, b0 second.
    CHECK_EQ_STR(ins[2].code, std::string("    uncompute_or(b1, m, p);\n"));
    CHECK_EQ_SIZE(raw(ins[2].insert_before), raw(make_loc(300)));
    CHECK_EQ_STR(ins[3].code, std::string("    uncompute_or(b0, m, n);\n"));
    CHECK_EQ_SIZE(raw(ins[3].insert_before), raw(make_loc(300)));
}

// ── Phase A / PA-1: NOT self-inverse render ─────────────────────────────────

static void test_not_op_emits_self_inverse() {
    // Hand-built QUnit for `qbool tmp = ~a;`. The expected inverse is an
    // in-place self-inverse: `tmp = ~tmp;` re-applied to the result qubit.
    // Operand `a` is recorded on the op but does not appear in the inverse
    // (the uncompute acts only on the result ancilla).
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::NOT;
    op.result = QValueRef{"tmp", make_loc(30)};
    op.operands.push_back(QValueRef{"a", make_loc(20)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code, std::string("    tmp = ~tmp;\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_not_op_zero_operands_emits_nothing() {
    // Defensive: a NOT op seeded with zero operands is malformed (the
    // matcher always records the single source operand). The render
    // function must return an empty string so no invalid C++ lands.
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(2);

    QOperation op;
    op.kind   = QOpKind::NOT;
    op.result = QValueRef{"r", make_loc(1)};
    // operands deliberately empty
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

// ── Phase A / PA-2: XOR two-line self-inverse render ────────────────────────

// ── Phase A / PA-3: XOR_ASSIGN self-adjoint render ──────────────────────────

static void test_xor_assign_op_emits_verbatim() {
    // `a ^= b;` has a one-line inverse that re-emits the same statement.
    // The IR stores a as `result` and b as the single operand.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::XOR_ASSIGN;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code, std::string("    a ^= b;\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_xor_op_emits_two_xor_assigns() {
    // Hand-built QUnit for `qbool tmp = a ^ b;`. Expected inverse is the
    // two-line `tmp ^= a;` then `tmp ^= b;` — both in a single insertion
    // record because M8 collapses contiguous lines for one op into one
    // UncomputeInsertion.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::XOR;
    op.result = QValueRef{"tmp", make_loc(30)};
    op.operands.push_back(QValueRef{"a", make_loc(20)});
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    tmp ^= a;\n    tmp ^= b;\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

// ── Phase B: constant compound-assign inverses ──────────────────────────────
//
// PB-1..PB-4: `a += 3;` / `a -= 3;` / `a *= 3;` / `a /= 3;` on a qint whose RHS
// is an int64_t lifted through the converting constructor. The matcher stores
// the verbatim RHS source text (e.g. "3") in `operands[0].name`. The inverse
// flips the operator to the classical-math inverse (+↔-, *↔/).

static void test_add_assign_const_emits_sub() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::ADD_ASSIGN_CONST;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"3", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code, std::string("    a -= 3;\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_sub_assign_const_emits_add() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::SUB_ASSIGN_CONST;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"7", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code, std::string("    a += 7;\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_mul_assign_const_emits_div() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::MUL_ASSIGN_CONST;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"2", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code, std::string("    a /= 2;\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_div_assign_const_emits_mul() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::DIV_ASSIGN_CONST;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"5", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code, std::string("    a *= 5;\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

// ── Phase N: rotation compound-assign inverses (PN-4) ───────────────────────
//
// `q.theta() += C;` / `q.theta() -= C;` / `q.phi() += C;` / `q.phi() -= C;`
// where C is a verbatim source fragment captured in operands[0].name. Each
// inverse flips the sign via the runtime's self-dual
// ThetaProxy::operator-= / PhiProxy::operator-= at
// include/sturm/qtypes/qint_core.hpp:305,372 — no free-function helper in
// uncompute_api.hpp. The emitted form is
// `    <lhs>.theta() -= <rhs>;` (for THETA_ADD_ASSIGN_CONST) and symmetric.

static void test_theta_add_assign_const_emits_theta_sub() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::THETA_ADD_ASSIGN_CONST;
    op.result = QValueRef{"q", make_loc(20)};
    op.operands.push_back(QValueRef{"0.5", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code, std::string("    q.theta() -= 0.5;\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_theta_sub_assign_const_emits_theta_add() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::THETA_SUB_ASSIGN_CONST;
    op.result = QValueRef{"q", make_loc(20)};
    op.operands.push_back(QValueRef{"0.1", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code, std::string("    q.theta() += 0.1;\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_phi_add_assign_const_emits_phi_sub() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::PHI_ADD_ASSIGN_CONST;
    op.result = QValueRef{"q", make_loc(20)};
    op.operands.push_back(QValueRef{"0.7", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code, std::string("    q.phi() -= 0.7;\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_phi_sub_assign_const_emits_phi_add() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::PHI_SUB_ASSIGN_CONST;
    op.result = QValueRef{"q", make_loc(20)};
    op.operands.push_back(QValueRef{"0.2", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code, std::string("    q.phi() += 0.2;\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

// ── Phase C: qint-qint compound-assign inverses ─────────────────────────────
//
// PC-1..PC-5: `a += b;` / `a -= b;` / `a *= b;` / `a /= b;` / `a %= b;` where
// b is another qint named in source. The matcher stores the verbatim RHS
// identifier (e.g. "b") in `operands[0].name`; the render emits a
// free-function call `uncompute_{add,sub,mul,div,mod}_qint(a, b);` declared
// in include/sturm/uncompute/uncompute_api.hpp. The call-site shape is
// uniform across all five ops so that later phases can extend it (mask
// checks, instrumentation) in one place — see the PC-ir section of
// docs/implementation_plan_transpiler_phase_c.md.

static void test_add_assign_qint_emits_uncompute_add_qint() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::ADD_ASSIGN_QINT;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_add_qint(a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_sub_assign_qint_emits_uncompute_sub_qint() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::SUB_ASSIGN_QINT;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_sub_qint(a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_mul_assign_qint_emits_uncompute_mul_qint() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::MUL_ASSIGN_QINT;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_mul_qint(a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_div_assign_qint_emits_uncompute_div_qint() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::DIV_ASSIGN_QINT;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_div_qint(a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_mod_assign_qint_emits_uncompute_mod_qint() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::MOD_ASSIGN_QINT;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_mod_qint(a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

// ── Phase D: qint-qint comparison inverses ──────────────────────────────────
//
// PD-1..PD-6: `qbool c = (a OP b);` where OP is ==, !=, <, <=, >, >= and a, b
// are qints. The matcher stores the result qbool as `op.result` and the LHS
// and RHS qint identifiers in `operands[0].name` / `operands[1].name`. The
// render emits `uncompute_{eq,ne,lt,le,gt,ge}_qint(c, a, b);` — a free
// function declared in include/sturm/uncompute/uncompute_api.hpp that
// re-dispatches to the self-adjoint DSL routines in
// include/sturm/lib/compare_dsl.hpp. The call shape matches the Phase C
// free-function convention but carries two operand names (LHS + RHS)
// instead of one, because a comparator's inverse depends on both inputs.

static void test_eq_qint_emits_uncompute_eq_qint() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::EQ_QINT;
    op.result = QValueRef{"c", make_loc(20)};
    op.operands.push_back(QValueRef{"a", make_loc(24)});
    op.operands.push_back(QValueRef{"b", make_loc(28)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_eq_qint(c, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_ne_qint_emits_uncompute_ne_qint() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::NE_QINT;
    op.result = QValueRef{"c", make_loc(20)};
    op.operands.push_back(QValueRef{"a", make_loc(24)});
    op.operands.push_back(QValueRef{"b", make_loc(28)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_ne_qint(c, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_lt_qint_emits_uncompute_lt_qint() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::LT_QINT;
    op.result = QValueRef{"c", make_loc(20)};
    op.operands.push_back(QValueRef{"a", make_loc(24)});
    op.operands.push_back(QValueRef{"b", make_loc(28)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_lt_qint(c, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_le_qint_emits_uncompute_le_qint() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::LE_QINT;
    op.result = QValueRef{"c", make_loc(20)};
    op.operands.push_back(QValueRef{"a", make_loc(24)});
    op.operands.push_back(QValueRef{"b", make_loc(28)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_le_qint(c, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_gt_qint_emits_uncompute_gt_qint() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::GT_QINT;
    op.result = QValueRef{"c", make_loc(20)};
    op.operands.push_back(QValueRef{"a", make_loc(24)});
    op.operands.push_back(QValueRef{"b", make_loc(28)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_gt_qint(c, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_ge_qint_emits_uncompute_ge_qint() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::GE_QINT;
    op.result = QValueRef{"c", make_loc(20)};
    op.operands.push_back(QValueRef{"a", make_loc(24)});
    op.operands.push_back(QValueRef{"b", make_loc(28)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_ge_qint(c, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

// Defensive: a comparison op seeded with the wrong operand count (not 2) is
// malformed — the matcher always records exactly the LHS + RHS. The render
// function must return empty so no invalid C++ lands.
static void test_eq_qint_wrong_operand_count_emits_nothing() {
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(2);

    QOperation op;
    op.kind   = QOpKind::EQ_QINT;
    op.result = QValueRef{"c", make_loc(1)};
    op.operands.push_back(QValueRef{"a", make_loc(1)}); // only one operand
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

// ── Phase E / PE-1: AND render parity with OR ────────────────────────────────
//
// Phase E introduces a second qbool bitwise op (AND) as a compound-expression
// building block. This slice is pure IR plumbing — no matcher yet — so the
// test mirrors the OR single-op case exactly: hand-seed a QOperation with
// `kind = QOpKind::AND` and assert the emitted inverse is the `uncompute_and`
// free-function call declared in include/sturm/uncompute/uncompute_api.hpp
// (shipped in PE-0 as sturm-oheo). The emission format mirrors OR byte-for-
// byte: four-space indent, `uncompute_and(<result>, <op0>, <op1>);\n`.
//
// The AND render must also guard against malformed ops (operand count != 2),
// because a future IR producer could seed a stray unary AND and we would
// rather emit nothing than corrupt the user's file with invalid C++.

static void test_and_op_emits_uncompute_and() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::AND;
    op.result = QValueRef{"tmp", make_loc(30)};
    op.operands.push_back(QValueRef{"a", make_loc(20)});
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_and(tmp, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_and_op_wrong_operand_count_emits_nothing() {
    // Defensive: an AND op with the wrong operand count is malformed. The
    // render function must return an empty string so no invalid C++ lands.
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(2);

    QOperation op;
    op.kind   = QOpKind::AND;
    op.result = QValueRef{"r", make_loc(1)};
    op.operands.push_back(QValueRef{"a", make_loc(1)}); // only one operand
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

// ── sturm-ny2: multi-kind ops seeded out of source order ────────────────────

static void test_multi_kind_out_of_order_sorted_by_source() {
    // Regression for sturm-ny2: when multiple Phase A matchers contribute
    // ops to the same QScope, clang::ast_matchers::MatchFinder dispatches
    // per-matcher callbacks in registration order, not source order, so
    // scope.ops accumulates in matcher-firing order. A naive reverse walk
    // would then yield LIFO-of-matcher-order instead of LIFO-of-source.
    //
    // Repro shape matches examples/or_circuit.cpp after Phase A:
    //   forward source order is OR@begin=10, NOT@begin=20, XOR_ASSIGN@30.
    // The observed buggy push order in the real transpiler is
    //   [NOT, OR, XOR_ASSIGN] — the OR matcher fires AFTER NOT despite
    // OR appearing earlier in source. A naive reverse walk would emit
    //   [XOR_ASSIGN, OR, NOT] — wrong (OR and NOT flipped relative to
    // source). The sort-by-stmt_range.getBegin() fix pins emission to
    //   [XOR_ASSIGN, NOT, OR] — the true source-LIFO.
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(99);

    QOperation op_or;
    op_or.kind = QOpKind::OR;
    op_or.result = QValueRef{"c", make_loc(10)};
    op_or.operands = { QValueRef{"a", make_loc(10)},
                       QValueRef{"b", make_loc(10)} };
    op_or.stmt_range = clang::SourceRange(make_loc(10), make_loc(15));

    QOperation op_not;
    op_not.kind = QOpKind::NOT;
    op_not.result = QValueRef{"nc", make_loc(20)};
    op_not.operands.push_back(QValueRef{"c", make_loc(20)});
    op_not.stmt_range = clang::SourceRange(make_loc(20), make_loc(25));

    QOperation op_xa;
    op_xa.kind = QOpKind::XOR_ASSIGN;
    op_xa.result = QValueRef{"d", make_loc(30)};
    op_xa.operands.push_back(QValueRef{"c", make_loc(30)});
    op_xa.stmt_range = clang::SourceRange(make_loc(30), make_loc(35));

    // Push in matcher-firing order (NOT, OR, XOR_ASSIGN) — NOT the true
    // source order. Without the sort, reverse iteration produces
    // [XOR_ASSIGN, OR, NOT]. With the sort, it produces [XOR_ASSIGN,
    // NOT, OR], which is the correct source-LIFO.
    scope.ops = { op_not, op_or, op_xa };

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 3u);
    if (ins.size() != 3) return;

    // True source-LIFO: XOR_ASSIGN (source-last) first, then NOT, then OR.
    CHECK_EQ_STR(ins[0].code, std::string("    d ^= c;\n"));
    CHECK_EQ_STR(ins[1].code, std::string("    nc = ~nc;\n"));
    CHECK_EQ_STR(ins[2].code, std::string("    uncompute_or(c, a, b);\n"));

    // All three insertions anchored at the same scope's close brace.
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(99)));
    CHECK_EQ_SIZE(raw(ins[1].insert_before), raw(make_loc(99)));
    CHECK_EQ_SIZE(raw(ins[2].insert_before), raw(make_loc(99)));
}

// ── Phase F / PF-1: per-op insert_before_override precedence ────────────────
//
// PF-1 introduces `QOperation::insert_before_override`. When the matcher
// sets it to a valid SourceLocation, the M8 synthesis pass must use it as
// the insertion anchor, NOT the enclosing scope's `close_brace`. When it
// is left default (invalid), the pass must fall back to `close_brace` —
// which is the behaviour every Phase A..E snapshot fixture relies on.
// Both directions are covered below.

static void test_insert_before_override_takes_precedence() {
    // Hand-built QUnit representing two ops in one scope:
    //   - op0: legacy (no override) → anchored at scope.close_brace.
    //   - op1: override set to make_loc(77) → anchored there instead.
    // Both ops share the same kind/operand shape so the only thing the
    // test exercises is the anchor selection.
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(99);

    QOperation op0;
    op0.kind   = QOpKind::OR;
    op0.result = QValueRef{"r0", make_loc(10)};
    op0.operands = { QValueRef{"a", make_loc(2)},
                     QValueRef{"b", make_loc(3)} };
    op0.stmt_range = clang::SourceRange(make_loc(10), make_loc(20));
    // op0.insert_before_override default-constructed (invalid).

    QOperation op1;
    op1.kind   = QOpKind::OR;
    op1.result = QValueRef{"r1", make_loc(30)};
    op1.operands = { QValueRef{"c", make_loc(4)},
                     QValueRef{"d", make_loc(5)} };
    op1.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));
    op1.insert_before_override = make_loc(77);

    scope.ops = { op0, op1 };

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 2u);
    if (ins.size() != 2) return;

    // LIFO within the scope: r1 first (source-last), r0 second. r1 must
    // honour its override (77); r0 falls back to close_brace (99).
    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_or(r1, c, d);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(77)));

    CHECK_EQ_STR(ins[1].code,
                 std::string("    uncompute_or(r0, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[1].insert_before), raw(make_loc(99)));
}

static void test_invalid_override_falls_back_to_close_brace() {
    // Defensive companion: a default-constructed (invalid) override must
    // not poison the anchor — the pass falls back to close_brace exactly
    // as it did pre-PF-1. Without this guarantee every prior snapshot
    // fixture would silently retarget to <invalid> and break.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::OR;
    op.result = QValueRef{"tmp", make_loc(30)};
    op.operands.push_back(QValueRef{"a", make_loc(20)});
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    // op.insert_before_override left default (invalid). Note we do NOT
    // explicitly assign clang::SourceLocation() — we want the test to
    // exercise the documented "default-constructed = invalid" contract.
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_or(tmp, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

// ── Phase F / PF-1: QUnit::raw_insertions pass-through ──────────────────────
//
// PF-1 also adds `QUnit::raw_insertions` — pre-staged UncomputeInsertion
// records the matcher assembles directly. The M8 pass concatenates them
// into its `QSynthesisResult.insertions` verbatim, after the per-op
// renderings. Order is preserved exactly. The test below asserts both
// shape (count + concatenation order) and identity (each record's
// `insert_before` and `code` survive untouched).

static void test_raw_insertions_appended_verbatim() {
    // One QOperation produces one synthesised insertion ([uncompute_or]).
    // Two raw insertions are pre-staged. Result: 1 + 2 = 3 insertions in
    // the M8 output, with the synthesised one first and the two raws in
    // their original order after it.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::OR;
    op.result = QValueRef{"tmp", make_loc(30)};
    op.operands.push_back(QValueRef{"a", make_loc(20)});
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    UncomputeInsertion pre0;
    pre0.insert_before = make_loc(200);
    pre0.code = "qbool __stu_t0 = a | b;\n";
    UncomputeInsertion pre1;
    pre1.insert_before = make_loc(300);
    pre1.code = "    uncompute_and(__stu_t1, __stu_t0, d);\n";
    unit.raw_insertions = { pre0, pre1 };

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 3u);
    if (ins.size() != 3) return;

    // Synthesised op comes first.
    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_or(tmp, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));

    // raw_insertions follow in their original order.
    CHECK_EQ_STR(ins[1].code, std::string("qbool __stu_t0 = a | b;\n"));
    CHECK_EQ_SIZE(raw(ins[1].insert_before), raw(make_loc(200)));

    CHECK_EQ_STR(ins[2].code,
                 std::string("    uncompute_and(__stu_t1, __stu_t0, d);\n"));
    CHECK_EQ_SIZE(raw(ins[2].insert_before), raw(make_loc(300)));
}

// ── Phase H / PH-3: skip_uncompute drops the op from the insertion list ─────
//
// PH-3 introduces `QOperation::skip_uncompute`. When true, the M8
// synthesis pass must emit NOTHING for that op — no UncomputeInsertion
// record, no rendered code. Other ops in the same scope (with
// `skip_uncompute == false`) are unaffected: the synthesis pass
// processes them normally, so per-op flag granularity is preserved.

static void test_skip_uncompute_true_emits_no_insertion() {
    // Single op in a scope with `skip_uncompute=true`. The insertion
    // vector must end up empty — no inverse was rendered.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::XOR_ASSIGN;
    op.result = QValueRef{"a", make_loc(30)};
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    op.skip_uncompute = true;
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

static void test_skip_uncompute_false_still_emits_normally() {
    // Defensive companion: the default `skip_uncompute == false` case
    // must produce the same insertion as pre-PH-3. Without this
    // guarantee every prior fixture would silently lose its uncompute.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::XOR_ASSIGN;
    op.result = QValueRef{"a", make_loc(30)};
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    // skip_uncompute left default (false).
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;
    CHECK_EQ_STR(ins[0].code, std::string("    a ^= b;\n"));
}

static void test_skip_uncompute_mixed_per_op_granularity() {
    // Two ops in the same scope: the first (op0) is flagged skip,
    // the second (op1) is not. The insertion list must contain
    // exactly ONE entry (op1's inverse), and op0 must NOT appear.
    // This is the "per-op flag, not per-scope" acceptance criterion
    // spelled out in the PH-3 issue description.
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(99);

    QOperation op0;
    op0.kind   = QOpKind::XOR_ASSIGN;
    op0.result = QValueRef{"a", make_loc(10)};
    op0.operands.push_back(QValueRef{"b", make_loc(11)});
    op0.stmt_range = clang::SourceRange(make_loc(10), make_loc(20));
    op0.skip_uncompute = true;

    QOperation op1;
    op1.kind   = QOpKind::OR;
    op1.result = QValueRef{"t", make_loc(30)};
    op1.operands = { QValueRef{"c", make_loc(31)},
                     QValueRef{"d", make_loc(32)} };
    op1.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));
    // op1.skip_uncompute left default (false).

    scope.ops = { op0, op1 };

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;
    // op1 is the non-skipped op — its inverse is the only one emitted.
    CHECK_EQ_STR(ins[0].code, std::string("    uncompute_or(t, c, d);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(99)));
}

// ── Phase I / PI-4: USER_ROUTINE render case ─────────────────────────────────
//
// PI-4 finalises the uncompute pass's render path for the USER_ROUTINE kind
// the PI-2 matcher produces. The emitted form is
// `    invert(<routine_name>)(<op0>, <op1>, ...);\n` with the operands
// listed in source order — there is NO result-name prefix (a USER_ROUTINE
// op mutates through its output parameters, not through a single named
// result) and the insertion is anchored at the enclosing scope's
// `close_brace` exactly like every other kind. The four-space leading
// indent matches the other render cases so the emitter injects uniform
// text into the user's source.
//
// Defensive: an empty `routine_name` renders nothing. The PI-2 matcher
// never produces a USER_ROUTINE op without a name, but a future IR
// consumer or a hand-built fixture could; returning an empty code string
// keeps the synthesis pass from planting `invert()(...);` into the user
// file.

static void test_user_routine_emits_invert_two_outputs() {
    // Single-scope USER_ROUTINE op with two output operands, mirroring
    // the PI-2 `both_out(a, b)` shape. outputs_mask = 0b11, but the
    // render case does not read the mask — every operand is listed in
    // source order regardless of input/output classification.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind           = QOpKind::USER_ROUTINE;
    op.routine_name   = "both_out";
    op.outputs_mask   = 0x3u;
    op.operands.push_back(QValueRef{"a", make_loc(20)});
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;
    CHECK_EQ_STR(ins[0].code, std::string("    invert(both_out)(a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(99)));
}

static void test_user_routine_emits_invert_mixed_io() {
    // Mixed input/output shape (`mixed_io(out, in)`). The render case
    // still emits both operands in source order — classical / const-ref
    // inputs are part of the inverse's call signature.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind           = QOpKind::USER_ROUTINE;
    op.routine_name   = "mixed_io";
    op.outputs_mask   = 0x1u;
    op.operands.push_back(QValueRef{"x", make_loc(20)});
    op.operands.push_back(QValueRef{"y", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;
    CHECK_EQ_STR(ins[0].code, std::string("    invert(mixed_io)(x, y);\n"));
}

static void test_user_routine_emits_invert_with_classical_scalar() {
    // PI-2 records classical scalar arguments verbatim in operand.name.
    // The render case emits them as-is — this is the whole point of
    // the positional operand list.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind           = QOpKind::USER_ROUTINE;
    op.routine_name   = "scalar_fn";
    op.outputs_mask   = 0x1u;
    op.operands.push_back(QValueRef{"out", make_loc(20)});
    // A classical argument is captured by lexer text and has no decl_loc.
    op.operands.push_back(QValueRef{"42", clang::SourceLocation()});
    op.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;
    CHECK_EQ_STR(ins[0].code, std::string("    invert(scalar_fn)(out, 42);\n"));
}

static void test_user_routine_zero_operands_still_emits_call() {
    // A no-argument routine renders as `invert(name)();` — the inner
    // parentheses are empty but still present. This case can arise if
    // a user registers a parameterless compute routine (rare but not
    // forbidden by the registrar).
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind           = QOpKind::USER_ROUTINE;
    op.routine_name   = "noop_fn";
    op.outputs_mask   = 0x0u;
    op.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;
    CHECK_EQ_STR(ins[0].code, std::string("    invert(noop_fn)();\n"));
}

static void test_user_routine_empty_name_emits_nothing() {
    // Defensive case from the issue description: an empty routine_name
    // must NOT produce an invert call — synthesize skips this op
    // silently, exactly like a malformed OR with the wrong operand
    // count.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind           = QOpKind::USER_ROUTINE;
    op.routine_name   = "";  // empty — e.g. unresolved callee
    op.outputs_mask   = 0x0u;
    op.operands.push_back(QValueRef{"a", make_loc(20)});
    op.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

static void test_user_routine_honours_skip_uncompute() {
    // PI-3 piggybacks on the generic `skip_uncompute` flag — the PI-4
    // render case inherits the pre-existing PH-3 guard in synthesize()
    // for free. This pins that no insertion is produced when skip is
    // set, so a future refactor that moves the guard into the render
    // case regresses visibly here.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind           = QOpKind::USER_ROUTINE;
    op.routine_name   = "both_out";
    op.outputs_mask   = 0x3u;
    op.operands.push_back(QValueRef{"a", make_loc(20)});
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));
    op.skip_uncompute = true;
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

static void test_user_routine_honours_insert_before_override() {
    // PI-3 sets `insert_before_override` on USER_ROUTINE ops with
    // IntermediateOuter slots so the inverse lands at the outermost
    // declaring scope's close brace. This pins the override path is
    // honoured for USER_ROUTINE exactly like every other kind.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind           = QOpKind::USER_ROUTINE;
    op.routine_name   = "mixed_io";
    op.outputs_mask   = 0x1u;
    op.operands.push_back(QValueRef{"x", make_loc(20)});
    op.operands.push_back(QValueRef{"y", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));
    op.insert_before_override = make_loc(77);
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;
    CHECK_EQ_STR(ins[0].code, std::string("    invert(mixed_io)(x, y);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(77)));
}

static void test_user_routine_lifo_with_other_kinds() {
    // Two ops in one scope: an OR followed by a USER_ROUTINE. The LIFO
    // reverse walk must emit the USER_ROUTINE inverse FIRST (it appears
    // later in source) and the OR inverse SECOND.
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(99);

    QOperation op0;
    op0.kind   = QOpKind::OR;
    op0.result = QValueRef{"t", make_loc(30)};
    op0.operands = { QValueRef{"a", make_loc(20)},
                     QValueRef{"b", make_loc(25)} };
    op0.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));

    QOperation op1;
    op1.kind         = QOpKind::USER_ROUTINE;
    op1.routine_name = "both_out";
    op1.outputs_mask = 0x3u;
    op1.operands = { QValueRef{"x", make_loc(50)},
                     QValueRef{"y", make_loc(55)} };
    op1.stmt_range = clang::SourceRange(make_loc(50), make_loc(60));

    scope.ops = { op0, op1 };

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 2u);
    if (ins.size() != 2) return;
    CHECK_EQ_STR(ins[0].code, std::string("    invert(both_out)(x, y);\n"));
    CHECK_EQ_STR(ins[1].code, std::string("    uncompute_or(t, a, b);\n"));
}

static void test_raw_insertions_only_no_ops() {
    // QUnit with zero scopes (no QOperations) but one raw insertion. The
    // pass must still emit the raw verbatim — Phase F's WHEN-lift matcher
    // can pre-stage its decl block on a unit that has no qualifying scope
    // ops (e.g. a WHEN at top level), so this edge case is load-bearing.
    QUnit unit;
    UncomputeInsertion pre;
    pre.insert_before = make_loc(42);
    pre.code = "qbool __stu_t0 = b | c;\n";
    unit.raw_insertions.push_back(pre);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code, std::string("qbool __stu_t0 = b | c;\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(42)));
}

// ── Phase J / PJ-3c: hoist_to_override uncompute-anchor routing ─────────────
//
// PJ-3c introduces `QOperation::hoist_to_override`. When valid, the M8
// synthesis pass must use it as the uncompute insertion anchor — the hoisted
// forward computation lives BEFORE the loop begin (rewritten in place by a
// matcher-owned QReplacement / raw_insertion) and the uncompute lands AFTER
// the loop end at the location recorded in `hoist_to_override`. The new
// field takes precedence over the Phase F `insert_before_override`
// (the hoisting matcher sets `insert_before_override` to the loop-BEGIN
// location for the forward compute anchor, and `hoist_to_override` to the
// loop-enclosing scope's close_brace for the post-loop uncompute anchor;
// synthesize must pick `hoist_to_override` for the uncompute). When
// `hoist_to_override` is left default (invalid), the pass falls back to
// the Phase F / pre-Phase-F behaviour — which is what every prior snapshot
// fixture relies on.

static void test_hoist_to_override_takes_precedence_over_close_brace() {
    // Single op in one scope with `hoist_to_override = 88`. No
    // `insert_before_override` is set. The uncompute insertion's
    // `insert_before` must be 88 (hoist anchor), NOT the scope's
    // close_brace (99).
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind   = QOpKind::OR;
    op.result = QValueRef{"t", make_loc(10)};
    op.operands = { QValueRef{"a", make_loc(2)},
                    QValueRef{"b", make_loc(3)} };
    op.stmt_range = clang::SourceRange(make_loc(10), make_loc(20));
    op.hoist_to_override = make_loc(88);
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;
    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_or(t, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(88)));
}

static void test_hoist_to_override_takes_precedence_over_insert_before_override() {
    // The PJ-3d hoisting matcher sets BOTH fields on a hoisted op:
    //   - insert_before_override = loop-begin location (for the forward
    //     compute anchor, consumed by the matcher-owned raw_insertion /
    //     QReplacement pair).
    //   - hoist_to_override = post-loop close_brace (for the uncompute
    //     anchor, consumed by synthesize()).
    // synthesize() must prefer `hoist_to_override` over
    // `insert_before_override` when placing the uncompute. If the two
    // were equal-priority the uncompute would land INSIDE the loop body
    // (at the loop-begin anchor) rather than after it, breaking the
    // semantic the optimization is built around.
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind   = QOpKind::OR;
    op.result = QValueRef{"t", make_loc(10)};
    op.operands = { QValueRef{"a", make_loc(2)},
                    QValueRef{"b", make_loc(3)} };
    op.stmt_range = clang::SourceRange(make_loc(10), make_loc(20));
    op.insert_before_override = make_loc(77);   // loop-begin (forward)
    op.hoist_to_override      = make_loc(88);   // post-loop (uncompute)
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;
    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_or(t, a, b);\n"));
    // hoist_to_override wins; insert_before_override is NOT the uncompute
    // anchor for a hoisted op.
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(88)));
}

static void test_invalid_hoist_override_falls_back_to_insert_before_override() {
    // Defensive companion: a default-constructed (invalid)
    // `hoist_to_override` must not poison the anchor — when only
    // `insert_before_override` is set, the pass falls back to it
    // exactly as it did pre-PJ-3c. Without this guarantee every Phase
    // F WHEN-lift fixture would silently retarget to <invalid>.
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind   = QOpKind::OR;
    op.result = QValueRef{"t", make_loc(10)};
    op.operands = { QValueRef{"a", make_loc(2)},
                    QValueRef{"b", make_loc(3)} };
    op.stmt_range = clang::SourceRange(make_loc(10), make_loc(20));
    op.insert_before_override = make_loc(77);
    // hoist_to_override left default (invalid).
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;
    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_or(t, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(77)));
}

static void test_invalid_hoist_override_falls_back_to_close_brace() {
    // Defensive companion: neither override set — the pass falls back
    // to `scope.close_brace`, which is the pre-Phase-F behaviour every
    // Phase A..E snapshot fixture relies on.
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind   = QOpKind::OR;
    op.result = QValueRef{"t", make_loc(10)};
    op.operands = { QValueRef{"a", make_loc(2)},
                    QValueRef{"b", make_loc(3)} };
    op.stmt_range = clang::SourceRange(make_loc(10), make_loc(20));
    // Both overrides left default (invalid).
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;
    CHECK_EQ_STR(ins[0].code,
                 std::string("    uncompute_or(t, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(99)));
}

static void test_hoist_to_override_honours_skip_uncompute() {
    // Defensive companion: a hoisted op with `skip_uncompute == true`
    // must still produce NO insertion — the PH-3 skip contract takes
    // precedence over the PJ-3c routing. The PJ-3d matcher already
    // skips any op with `skip_uncompute == true` (PH-3 disjointness
    // guard), but synthesize() must be robust to a hand-built fixture
    // or a future matcher that sets both flags.
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind   = QOpKind::OR;
    op.result = QValueRef{"t", make_loc(10)};
    op.operands = { QValueRef{"a", make_loc(2)},
                    QValueRef{"b", make_loc(3)} };
    op.stmt_range = clang::SourceRange(make_loc(10), make_loc(20));
    op.hoist_to_override = make_loc(88);
    op.skip_uncompute = true;
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

static void test_hoist_to_override_user_routine_kind() {
    // PJ-3d's matcher is restricted to decl-producing kinds (OR / AND /
    // NOT / XOR / compare); USER_ROUTINE is excluded. But synthesize()
    // must remain agnostic to kind when routing the uncompute anchor —
    // the hoist override takes precedence over the USER_ROUTINE-specific
    // insert_before_override path used by PI-3 just as it does for OR
    // or AND.
    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind           = QOpKind::USER_ROUTINE;
    op.routine_name   = "mixed_io";
    op.outputs_mask   = 0x1u;
    op.operands = { QValueRef{"x", make_loc(20)},
                    QValueRef{"y", make_loc(25)} };
    op.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));
    op.insert_before_override = make_loc(77);
    op.hoist_to_override      = make_loc(88);
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;
    CHECK_EQ_STR(ins[0].code, std::string("    invert(mixed_io)(x, y);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(88)));
}

// ── Phase J / PJ-1c: CCNOT_INPLACE render case ──────────────────────────────
//
// PJ-1c wires the M8 render path for the CCNOT_INPLACE kind that the PJ-1d
// peephole matcher seeds. The emitted form is
// `    ccnot_inplace(x, a, b);\n` — self-adjoint, so the forward emission
// (text replacement on the fused pair) and the uncompute emission (this
// render case) share a single identifier. Operand shape: one result
// QValueRef (the `x` target) plus two named operand QValueRefs (the two
// qbool controls). Defensive: operand count != 2 emits nothing so a
// malformed hand-built op does not inject invalid C++.

static void test_ccnot_inplace_emits_self_adjoint_call() {
    // Canonical shape from the PJ-1d peephole:
    //   qbool __t = a & b;
    //   x ^= __t;
    // fuses into CCNOT_INPLACE{result=x, operands=[a, b]}. The render
    // case emits `    ccnot_inplace(x, a, b);\n` anchored at the
    // enclosing scope's close_brace exactly like every other kind.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind   = QOpKind::CCNOT_INPLACE;
    op.result = QValueRef{"x", make_loc(30)};
    op.operands.push_back(QValueRef{"a", make_loc(20)});
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;
    CHECK_EQ_STR(ins[0].code,
                 std::string("    ccnot_inplace(x, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(99)));
}

static void test_ccnot_inplace_wrong_operand_count_emits_nothing() {
    // Defensive guard — a malformed CCNOT_INPLACE with only one operand
    // (a peephole-matcher bug, or a hand-built fixture) must NOT render
    // `ccnot_inplace(x, a);` into the user file. Mirrors the identical
    // guard on OR / AND / NOT / XOR / XOR_ASSIGN.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind   = QOpKind::CCNOT_INPLACE;
    op.result = QValueRef{"x", make_loc(30)};
    op.operands.push_back(QValueRef{"a", make_loc(20)});  // only one
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

static void test_ccnot_inplace_zero_operands_emits_nothing() {
    // Zero-operand edge case — same defensive guard as the one-operand
    // case. The PJ-1d matcher always produces exactly two operands but
    // a fixture can hand-build a malformed op.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(99);

    QOperation op;
    op.kind   = QOpKind::CCNOT_INPLACE;
    op.result = QValueRef{"x", make_loc(30)};
    // no operands
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

// ── Phase M / PM4-3: QOpKind::PLUGIN render dispatch ────────────────────────
//
// The PLUGIN case in `render_uncompute` consults the per-consumer plugin
// Registry via `find_render_fn(plugin_kind_id)` and invokes the returned
// `UncomputeRenderFn` to produce the inverse source text. No in-tree
// matcher ever constructs a `QOpKind::PLUGIN` op, so every existing
// snapshot fixture stays byte-identical — the tests below are the sole
// coverage of the new arm.
//
// Invariants pinned:
//   1. When a Registry is passed AND the op carries a kind_id registered
//      against a render fn, `synthesize()` emits exactly what the render
//      fn returns (verbatim, no four-space re-indent on top of the
//      plugin's own indent discipline).
//   2. When the Registry is null — a hand-built test fixture or a
//      pre-PM4 caller that forgot to thread it — the PLUGIN op renders
//      to an empty string (same defensive posture as every other kind).
//   3. When the op's `plugin_kind_id` is empty, same defensive-skip
//      posture (equivalent to USER_ROUTINE's empty-routine-name guard).
//   4. When the kind_id is not registered in the Registry, same skip.
//   5. LIFO ordering is preserved when a PLUGIN op co-exists with
//      in-tree kinds in the same scope.

static void test_plugin_kind_emits_registered_render_fn_output() {
    // Build a Registry and register a render function that emits the
    // demo plugin's `pm4_demo_tag_inverse(<q>);\n` exact text.  This
    // mirrors what `examples/plugin_demo/plugin_demo.cpp`'s
    // `sturm_register_plugin_v1` wires at production runtime.
    plugin::Registry registry;
    registry.register_op(
        "pm4.demo.tag",
        // matcher-fn half: never invoked by `synthesize()`. We pass a
        // no-op lambda so the register_op collision detection stays
        // symmetric with the production path. `MatcherRegisterFn` is
        // `std::function<void(MatchFinder&, QUnit&)>` — the forward
        // declaration of MatchFinder in plugin_api.hpp is sufficient
        // since the parameter is a reference and the body never
        // dereferences it.
        [](clang::ast_matchers::MatchFinder&, QUnit&) {},
        // render-fn half: consulted by the PM4-3 `case QOpKind::PLUGIN:`
        // arm. Returns the exact text the production plugin produces
        // so the test pins the byte-level output contract.
        [](const QOperation& op) -> std::string {
            if (op.result.name.empty()) return {};
            std::string out;
            out.reserve(40);
            out.append("    pm4_demo_tag_inverse(");
            out.append(op.result.name);
            out.append(");\n");
            return out;
        });

    // Hand-build a QUnit representing a single PLUGIN op in a scope.
    // The matcher + scope locations mirror the pattern every other
    // test case in this file uses (make_loc(...) opaque ids).
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind            = QOpKind::PLUGIN;
    op.plugin_kind_id  = "pm4.demo.tag";
    op.result          = QValueRef{"q", make_loc(30)};
    op.stmt_range      = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    // `sm=nullptr` to keep the test free of a SourceManager; the
    // per-op `#line` prefix only fires when sm is non-null, so the
    // returned `code` is the plugin's render text verbatim.
    auto ins = synthesize(unit, /*sm=*/nullptr, &registry).insertions;
    CHECK_EQ_SIZE(ins.size(), 1u);
    if (ins.size() != 1) return;

    CHECK_EQ_STR(ins[0].code,
                 std::string("    pm4_demo_tag_inverse(q);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(50)));
}

static void test_plugin_kind_without_registry_emits_nothing() {
    // Defensive: a `QOpKind::PLUGIN` op rendered with no Registry must
    // emit nothing rather than crash. Exercises the
    // `registry == nullptr` guard in `render_uncompute`.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind            = QOpKind::PLUGIN;
    op.plugin_kind_id  = "pm4.demo.tag";
    op.result          = QValueRef{"q", make_loc(30)};
    op.stmt_range      = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    // Two equivalent call shapes: (unit) and (unit, nullptr, nullptr).
    // The default argument on `synthesize()` gives us a null registry
    // on the single-arg form, which is the same behaviour we want.
    auto ins = synthesize(unit).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

static void test_plugin_kind_without_kind_id_emits_nothing() {
    // Defensive: empty `plugin_kind_id` triggers the same skip posture
    // as USER_ROUTINE's empty-routine-name guard. A registry lookup
    // with an empty key would never collide with a real registration
    // (`register_op` rejects empty keys via the collision map, but a
    // hand-built op could still carry an empty string).
    plugin::Registry registry;
    registry.register_op(
        "pm4.demo.tag",
        [](clang::ast_matchers::MatchFinder&, QUnit&) {},
        [](const QOperation&) -> std::string {
            return "    THIS_SHOULD_NOT_BE_CALLED;\n";
        });

    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind            = QOpKind::PLUGIN;
    op.plugin_kind_id  = "";  // empty
    op.result          = QValueRef{"q", make_loc(30)};
    op.stmt_range      = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit, /*sm=*/nullptr, &registry).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

static void test_plugin_kind_unregistered_emits_nothing() {
    // Defensive: the op's kind_id is not present in the Registry. The
    // `find_render_fn` accessor returns null; the render case must
    // emit nothing rather than crash or inject garbage. Mirrors the
    // USER_ROUTINE empty-name guard posture.
    plugin::Registry registry;
    registry.register_op(
        "some.other.kind",
        [](clang::ast_matchers::MatchFinder&, QUnit&) {},
        [](const QOperation&) -> std::string {
            return "    THIS_SHOULD_NOT_BE_CALLED;\n";
        });

    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind            = QOpKind::PLUGIN;
    op.plugin_kind_id  = "not.registered";
    op.result          = QValueRef{"q", make_loc(30)};
    op.stmt_range      = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit, /*sm=*/nullptr, &registry).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

static void test_plugin_kind_lifo_with_in_tree_kinds() {
    // Invariant: LIFO ordering in a scope with mixed in-tree and
    // PLUGIN ops. The PLUGIN op renders via the Registry; the OR op
    // renders via the in-tree case. Both anchor at the scope's
    // close_brace. The begin-loc sort + reverse iteration in
    // `synthesize()` must interleave them in reverse-source order.
    plugin::Registry registry;
    registry.register_op(
        "pm4.demo.tag",
        [](clang::ast_matchers::MatchFinder&, QUnit&) {},
        [](const QOperation& op) -> std::string {
            std::string out;
            out.reserve(40);
            out.append("    pm4_demo_tag_inverse(");
            out.append(op.result.name);
            out.append(");\n");
            return out;
        });

    QScope scope;
    scope.open_brace  = make_loc(1);
    scope.close_brace = make_loc(99);

    // Forward source order: OR first (stmt_range begin=10), PLUGIN
    // second (stmt_range begin=20).
    QOperation op_or;
    op_or.kind       = QOpKind::OR;
    op_or.result     = QValueRef{"tmp", make_loc(12)};
    op_or.operands   = { QValueRef{"a", make_loc(13)},
                         QValueRef{"b", make_loc(14)} };
    op_or.stmt_range = clang::SourceRange(make_loc(10), make_loc(18));

    QOperation op_plugin;
    op_plugin.kind             = QOpKind::PLUGIN;
    op_plugin.plugin_kind_id   = "pm4.demo.tag";
    op_plugin.result           = QValueRef{"q", make_loc(22)};
    op_plugin.stmt_range       = clang::SourceRange(make_loc(20), make_loc(28));

    scope.ops = { op_or, op_plugin };

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit, /*sm=*/nullptr, &registry).insertions;
    CHECK_EQ_SIZE(ins.size(), 2u);
    if (ins.size() != 2) return;

    // LIFO: PLUGIN op is last-in (stmt_range begin=20), so its inverse
    // emits FIRST. OR op's inverse emits SECOND.
    CHECK_EQ_STR(ins[0].code,
                 std::string("    pm4_demo_tag_inverse(q);\n"));
    CHECK_EQ_STR(ins[1].code,
                 std::string("    uncompute_or(tmp, a, b);\n"));
    CHECK_EQ_SIZE(raw(ins[0].insert_before), raw(make_loc(99)));
    CHECK_EQ_SIZE(raw(ins[1].insert_before), raw(make_loc(99)));
}

static void test_plugin_kind_honours_skip_uncompute() {
    // Pinning cross-cut: the PH-3 `skip_uncompute` flag applies to
    // PLUGIN ops too. `synthesize()`'s skip path runs before
    // `render_uncompute` is called, so a PLUGIN op with
    // skip_uncompute=true MUST emit nothing even if the registry has
    // a renderer for its kind_id.
    plugin::Registry registry;
    registry.register_op(
        "pm4.demo.tag",
        [](clang::ast_matchers::MatchFinder&, QUnit&) {},
        [](const QOperation&) -> std::string {
            return "    THIS_SHOULD_NOT_BE_CALLED;\n";
        });

    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind            = QOpKind::PLUGIN;
    op.plugin_kind_id  = "pm4.demo.tag";
    op.result          = QValueRef{"q", make_loc(30)};
    op.stmt_range      = clang::SourceRange(make_loc(28), make_loc(40));
    op.skip_uncompute  = true;
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    auto ins = synthesize(unit, /*sm=*/nullptr, &registry).insertions;
    CHECK_EQ_SIZE(ins.size(), 0u);
}

int main() {
    test_single_op_one_insertion();
    test_two_ops_lifo_order();
    test_two_scopes_each_one_op();
    test_empty_unit();
    test_empty_scope();
    test_multi_scope_multi_op_lifo();
    test_not_op_emits_self_inverse();
    test_not_op_zero_operands_emits_nothing();
    test_xor_op_emits_two_xor_assigns();
    test_xor_assign_op_emits_verbatim();
    test_add_assign_const_emits_sub();
    test_sub_assign_const_emits_add();
    test_mul_assign_const_emits_div();
    test_div_assign_const_emits_mul();
    test_theta_add_assign_const_emits_theta_sub();
    test_theta_sub_assign_const_emits_theta_add();
    test_phi_add_assign_const_emits_phi_sub();
    test_phi_sub_assign_const_emits_phi_add();
    test_add_assign_qint_emits_uncompute_add_qint();
    test_sub_assign_qint_emits_uncompute_sub_qint();
    test_mul_assign_qint_emits_uncompute_mul_qint();
    test_div_assign_qint_emits_uncompute_div_qint();
    test_mod_assign_qint_emits_uncompute_mod_qint();
    test_eq_qint_emits_uncompute_eq_qint();
    test_ne_qint_emits_uncompute_ne_qint();
    test_lt_qint_emits_uncompute_lt_qint();
    test_le_qint_emits_uncompute_le_qint();
    test_gt_qint_emits_uncompute_gt_qint();
    test_ge_qint_emits_uncompute_ge_qint();
    test_eq_qint_wrong_operand_count_emits_nothing();
    test_and_op_emits_uncompute_and();
    test_and_op_wrong_operand_count_emits_nothing();
    test_multi_kind_out_of_order_sorted_by_source();

    test_insert_before_override_takes_precedence();
    test_invalid_override_falls_back_to_close_brace();
    test_raw_insertions_appended_verbatim();
    test_raw_insertions_only_no_ops();

    test_hoist_to_override_takes_precedence_over_close_brace();
    test_hoist_to_override_takes_precedence_over_insert_before_override();
    test_invalid_hoist_override_falls_back_to_insert_before_override();
    test_invalid_hoist_override_falls_back_to_close_brace();
    test_hoist_to_override_honours_skip_uncompute();
    test_hoist_to_override_user_routine_kind();

    test_skip_uncompute_true_emits_no_insertion();
    test_skip_uncompute_false_still_emits_normally();
    test_skip_uncompute_mixed_per_op_granularity();

    test_user_routine_emits_invert_two_outputs();
    test_user_routine_emits_invert_mixed_io();
    test_user_routine_emits_invert_with_classical_scalar();
    test_user_routine_zero_operands_still_emits_call();
    test_user_routine_empty_name_emits_nothing();
    test_user_routine_honours_skip_uncompute();
    test_user_routine_honours_insert_before_override();
    test_user_routine_lifo_with_other_kinds();

    test_ccnot_inplace_emits_self_adjoint_call();
    test_ccnot_inplace_wrong_operand_count_emits_nothing();
    test_ccnot_inplace_zero_operands_emits_nothing();

    // PM4-3 (sturm-4oyr.4) — QOpKind::PLUGIN + render_uncompute dispatch.
    test_plugin_kind_emits_registered_render_fn_output();
    test_plugin_kind_without_registry_emits_nothing();
    test_plugin_kind_without_kind_id_emits_nothing();
    test_plugin_kind_unregistered_emits_nothing();
    test_plugin_kind_lifo_with_in_tree_kinds();
    test_plugin_kind_honours_skip_uncompute();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
