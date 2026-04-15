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
    test_multi_kind_out_of_order_sorted_by_source();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
