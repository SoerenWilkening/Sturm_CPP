// test_qir.cpp — unit tests for the Quantum IR (M6).
//
// The IR is the contract between three pieces:
//   - M7 AST matcher (fills QUnit from user source)
//   - M8 uncompute pass (walks QUnit, produces UncomputeInsertion records)
//   - M9 emitter (consumes insertions, rewrites source)
//
// So the IR must be:
//   - Constructible without running a ClangTool (we build one manually here).
//   - Deterministic in its dump() output — M7/M8 golden tests compare against
//     strings produced by this function, so any format change is a breaking
//     change for every downstream module.
//   - Free of AST dependencies beyond clang::SourceLocation / SourceRange.
//
// Tests:
//   - QValueRef equality / inequality (ignores decl_loc for name-based compare?
//     No — the spec ties QValueRef to *a particular declaration*, so equality
//     must consider both name and decl_loc. See test_qvalueref_equality.)
//   - Single-op QUnit dump() produces the expected canonical string.
//   - Multi-op, multi-scope QUnit preserves ordering and operand order.
//   - Empty QUnit dumps as a single line.
//   - Invalid SourceLocations (the common case when constructing IR in tests)
//     render deterministically as "<invalid>" so the golden strings are
//     portable across builds / machines.

#include "sturm/transpile/qir.hpp"

// UncomputeInsertion is also defined in qir.hpp as of Phase F PF-1, so the
// test can use it directly without dragging in the M8 pass header. We still
// rely only on qir.hpp's transitive Clang dependency (SourceLocation).

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

// Build a SourceLocation with a non-zero raw encoding. Clang's SourceLocation
// treats ID == 0 as invalid; any other file-ID value is valid for formatting.
// We do NOT need a real SourceManager because dump() prints raw encodings.
static clang::SourceLocation make_loc(std::uint32_t raw) {
    return clang::SourceLocation::getFromRawEncoding(raw);
}

// ── QValueRef equality ───────────────────────────────────────────────────────

static void test_qvalueref_equality_same() {
    QValueRef a{"tmp", make_loc(100)};
    QValueRef b{"tmp", make_loc(100)};
    CHECK(a == b);
}

static void test_qvalueref_inequality_name() {
    QValueRef a{"tmp", make_loc(100)};
    QValueRef b{"other", make_loc(100)};
    CHECK(!(a == b));
}

static void test_qvalueref_inequality_loc() {
    // Two variables with the same name but different declarations (e.g. two
    // shadowed locals in nested scopes) must compare unequal — otherwise the
    // uncompute pass would alias them.
    QValueRef a{"tmp", make_loc(100)};
    QValueRef b{"tmp", make_loc(200)};
    CHECK(!(a == b));
}

// ── dump() determinism ───────────────────────────────────────────────────────

static void test_dump_empty_unit() {
    QUnit unit;
    std::string out = dump(unit);
    // Format locked down so M7/M8 goldens are stable. An empty unit always
    // emits exactly the header line and nothing else.
    CHECK_EQ_STR(out, std::string("QUnit: 0 scope(s)\n"));
}

static void test_dump_single_op() {
    // qbool tmp = a | b;  (one scope, one OR)
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

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] OR tmp@30 = a@20, b@25  range=[28..40]\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_multi_scope_and_order() {
    // Two scopes, each with two ops. Confirms operand order and op order are
    // preserved by dump() — the uncompute pass's correctness depends on it.
    QUnit unit;

    {
        QScope s;
        s.open_brace  = make_loc(1);
        s.close_brace = make_loc(9);

        QOperation op1;
        op1.kind   = QOpKind::OR;
        op1.result = QValueRef{"t1", make_loc(3)};
        op1.operands = { QValueRef{"a", make_loc(2)},
                         QValueRef{"b", make_loc(2)} };
        op1.stmt_range = clang::SourceRange(make_loc(3), make_loc(4));

        QOperation op2;
        op2.kind   = QOpKind::OR;
        op2.result = QValueRef{"t2", make_loc(5)};
        op2.operands = { QValueRef{"t1", make_loc(3)},
                         QValueRef{"c", make_loc(2)} };
        op2.stmt_range = clang::SourceRange(make_loc(5), make_loc(6));

        s.ops = { op1, op2 };
        unit.scopes.push_back(s);
    }
    {
        QScope s;
        s.open_brace  = make_loc(100);
        s.close_brace = make_loc(200);

        QOperation op;
        op.kind   = QOpKind::OR;
        op.result = QValueRef{"x", make_loc(150)};
        op.operands = { QValueRef{"p", make_loc(110)},
                        QValueRef{"q", make_loc(120)} };
        op.stmt_range = clang::SourceRange(make_loc(150), make_loc(160));
        s.ops.push_back(op);
        unit.scopes.push_back(s);
    }

    const std::string want =
        "QUnit: 2 scope(s)\n"
        "  Scope[0] braces=[1..9]\n"
        "    Op[0] OR t1@3 = a@2, b@2  range=[3..4]\n"
        "    Op[1] OR t2@5 = t1@3, c@2  range=[5..6]\n"
        "  Scope[1] braces=[100..200]\n"
        "    Op[0] OR x@150 = p@110, q@120  range=[150..160]\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_invalid_locations_render_as_placeholder() {
    // In tests constructed without a ClangTool, decl_loc is often the default
    // (invalid) SourceLocation. dump() must render it as "<invalid>" instead
    // of "0" so that humans reading the golden string immediately understand
    // the value was never assigned by a real matcher run.
    QScope scope; // open_brace / close_brace default-constructed (invalid).
    QOperation op;
    op.kind   = QOpKind::OR;
    op.result = QValueRef{"r", clang::SourceLocation()};
    op.operands = { QValueRef{"a", clang::SourceLocation()} };
    // stmt_range also default: both endpoints invalid.
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[<invalid>..<invalid>]\n"
        "    Op[0] OR r@<invalid> = a@<invalid>  range=[<invalid>..<invalid>]\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_zero_operands() {
    // Even though the MVP matcher only emits OR (two operands), dump() must
    // handle zero- or one-operand ops gracefully so post-MVP kinds (NOT,
    // constants) do not require a format change.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(20);

    QOperation op;
    op.kind   = QOpKind::OR; // only kind in MVP; shape still exercised.
    op.result = QValueRef{"r", make_loc(12)};
    // no operands
    op.stmt_range = clang::SourceRange(make_loc(12), make_loc(15));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..20]\n"
        "    Op[0] OR r@12 =   range=[12..15]\n";
    CHECK_EQ_STR(dump(unit), want);
}

// ── Phase B: constant compound-assign enumerators ────────────────────────────
//
// Per the PB-1..PB-4 patterns in the post-MVP roadmap, `a += k;`, `a -= k;`,
// `a *= k;`, `a /= k;` (k classical) each need their own QOpKind entry so
// the matcher can tag the op at M7-time and the uncompute pass can emit the
// dual `-=` / `+=` / `/=` / `*=` at M8-time. dump() is the single textual
// representation shared by the matcher and uncompute goldens — if it did
// not know these kinds, every downstream fixture that exercised them would
// render as "<unknown-QOpKind>" and the golden test chain would silently
// stop working. This test locks the stringification in *before* the matcher
// and render cases land (those are the two blocked sibling issues).
//
// Shape per the issue description: each op carries one result QValueRef and
// one operand QValueRef whose name is the verbatim RHS source text (PA-4
// pattern — the operand is a classical literal, not a named qbool, so the
// "decl_loc" is unused but we exercise the normal <name>@<loc> rendering
// to guarantee the format is the same as for qbool operands).

static void test_dump_add_assign_const() {
    // `a += 7;` — classical constant RHS stored as the operand's name.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::ADD_ASSIGN_CONST;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"7", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] ADD_ASSIGN_CONST a@20 = 7@25  range=[28..40]\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_sub_assign_const() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::SUB_ASSIGN_CONST;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"3", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] SUB_ASSIGN_CONST a@20 = 3@25  range=[28..40]\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_mul_assign_const() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::MUL_ASSIGN_CONST;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"5", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] MUL_ASSIGN_CONST a@20 = 5@25  range=[28..40]\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_div_assign_const() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::DIV_ASSIGN_CONST;
    op.result = QValueRef{"a", make_loc(20)};
    op.operands.push_back(QValueRef{"2", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] DIV_ASSIGN_CONST a@20 = 2@25  range=[28..40]\n";
    CHECK_EQ_STR(dump(unit), want);
}

// ── Phase C: qint-qint compound-assign enumerators ───────────────────────────
//
// Per the PC-1..PC-5 patterns in the post-MVP roadmap, `a += b;`, `a -= b;`,
// `a *= b;`, `a /= b;`, `a %= b;` (b another qint) each need their own
// QOpKind entry. The operand shape mirrors Phase B — one result + one named
// operand — but the operand is a qint identifier rather than a classical
// literal. dump() must stringify each kind so that M7 matcher / M8 uncompute
// goldens remain stable; without these cases the kinds render as
// "<unknown-QOpKind>" and fixtures silently go dark.

static void test_dump_add_assign_qint() {
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

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] ADD_ASSIGN_QINT a@20 = b@25  range=[28..40]\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_sub_assign_qint() {
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

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] SUB_ASSIGN_QINT a@20 = b@25  range=[28..40]\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_mul_assign_qint() {
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

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] MUL_ASSIGN_QINT a@20 = b@25  range=[28..40]\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_div_assign_qint() {
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

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] DIV_ASSIGN_QINT a@20 = b@25  range=[28..40]\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_mod_assign_qint() {
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

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] MOD_ASSIGN_QINT a@20 = b@25  range=[28..40]\n";
    CHECK_EQ_STR(dump(unit), want);
}

// ── Phase E: AND enumerator ──────────────────────────────────────────────────
//
// PE-1 adds `QOpKind::AND` as the second qbool bitwise kind. The enum sits
// immediately after OR in qir.hpp and must stringify to the literal "AND" in
// dump() so the M7/M8 golden-test chain stays stable once the compound
// matcher lands in PE-4. Operand shape mirrors OR exactly: one result + two
// named operands.

static void test_dump_and_op() {
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

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] AND tmp@30 = a@20, b@25  range=[28..40]\n";
    CHECK_EQ_STR(dump(unit), want);
}

// ── Phase F / PF-1: insert_before_override surfaces in dump() ─────────────────
//
// PF-1 introduces `QOperation::insert_before_override` — an optional per-op
// SourceLocation that the M8 synthesis pass uses as the insertion anchor
// instead of the enclosing scope's `close_brace` when the override is valid.
// dump() must surface the override whenever it is set so the M7 matcher's
// golden tests (and future Phase F snapshots) can witness which ops carry a
// custom anchor — but it must produce byte-identical output for every prior
// snapshot, which means: invalid (default) overrides must render NOTHING
// extra. Both directions are covered below.

static void test_dump_op_with_invalid_override_unchanged() {
    // Mirror test_dump_single_op exactly: no override set means the dump
    // line is byte-identical to the pre-PF-1 format. Existing Phase A..E
    // snapshot fixtures depend on this property.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::OR;
    op.result = QValueRef{"tmp", make_loc(30)};
    op.operands.push_back(QValueRef{"a", make_loc(20)});
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    // insert_before_override left default (invalid).
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] OR tmp@30 = a@20, b@25  range=[28..40]\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_op_with_valid_override_surfaces() {
    // When the override is valid, dump() appends a trailing
    // " insert_before_override=<raw>" annotation to the op line.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::AND;
    op.result = QValueRef{"r", make_loc(30)};
    op.operands.push_back(QValueRef{"a", make_loc(20)});
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    op.insert_before_override = make_loc(77);
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] AND r@30 = a@20, b@25  range=[28..40] insert_before_override=77\n";
    CHECK_EQ_STR(dump(unit), want);
}

// ── Phase F / PF-1: QUnit::raw_insertions storage ────────────────────────────
//
// PF-1 also introduces `QUnit::raw_insertions` — a vector of
// pre-staged `UncomputeInsertion` records the matcher assembles directly
// (used in PF-3 for the WHEN-lift decl-block injection). The IR must be
// constructible with these records and the field must be accessible
// without a SourceManager. dump() does NOT serialise this vector (it is
// outside the locked golden-string format), but the field must be a
// vector of complete UncomputeInsertion objects so both the matcher and
// the M8 pass can read/write it directly.

static void test_qunit_raw_insertions_is_default_empty() {
    QUnit unit;
    CHECK(unit.raw_insertions.empty());
}

static void test_qunit_raw_insertions_round_trip() {
    QUnit unit;
    UncomputeInsertion rec;
    rec.insert_before = make_loc(123);
    rec.code = "    uncompute_or(t, a, b);\n";
    unit.raw_insertions.push_back(rec);
    CHECK_EQ_SIZE(unit.raw_insertions.size(), 1u);
    CHECK_EQ_STR(unit.raw_insertions[0].code,
                 std::string("    uncompute_or(t, a, b);\n"));
    CHECK(unit.raw_insertions[0].insert_before.getRawEncoding() == 123u);
}

// ── Phase H / PH-3: skip_uncompute dump suffix ──────────────────────────────
//
// PH-3 introduces `QOperation::skip_uncompute`. When false (the default),
// dump() must emit NOTHING extra — byte-identical to pre-PH-3 output.
// When true, dump() appends a `" [skip_uncompute]"` token to the op line
// so humans reading the IR can see at a glance which ops the matcher
// refused to auto-uncompute.

static void test_dump_skip_uncompute_default_unchanged() {
    // Default-constructed QOperation has `skip_uncompute == false`, so
    // the dump output must be byte-identical to the legacy format.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind   = QOpKind::XOR_ASSIGN;
    op.result = QValueRef{"a", make_loc(30)};
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(28), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] XOR_ASSIGN a@30 = b@25  range=[28..40]\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_skip_uncompute_true_appends_suffix() {
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

    // The suffix is a single space-prefixed " [skip_uncompute]" token
    // appended after the range annotation. No interaction with the
    // insert_before_override annotation (both can coexist on the same
    // op if a future matcher sets both).
    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] XOR_ASSIGN a@30 = b@25  range=[28..40] [skip_uncompute]\n";
    CHECK_EQ_STR(dump(unit), want);
}

// ── Phase I / PI-4: USER_ROUTINE dump surface ───────────────────────────────
//
// PI-4 pins dump()'s rendering for the USER_ROUTINE kind the PI-2 matcher
// produces: the op line carries the trailing `routine_name="<name>"
// outputs_mask=0x<hex>` annotation, the operand list renders in source
// order exactly like every other kind, and `result` is default-constructed
// (no single named result for a user-routine call) so its operand-slot
// prints as `@<invalid>`. A USER_ROUTINE op with zero operands (rare — a
// no-argument callee) still renders the empty operand list cleanly.

static void test_dump_user_routine_two_outputs() {
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind           = QOpKind::USER_ROUTINE;
    op.routine_name   = "both_out";
    op.outputs_mask   = 0x3u;
    op.operands.push_back(QValueRef{"a", make_loc(20)});
    op.operands.push_back(QValueRef{"b", make_loc(25)});
    op.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));
    // op.result left default (name="", decl_loc invalid).
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] USER_ROUTINE @<invalid> = a@20, b@25  range=[30..40]"
        " routine_name=\"both_out\" outputs_mask=0x3\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_user_routine_mixed_io_with_scalar() {
    // PI-2 captures classical scalar arguments via Lexer-extracted source
    // text in operand.name with an invalid decl_loc. dump() renders those
    // operands as `<text>@<invalid>` alongside the named qbool/qint
    // operands.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind           = QOpKind::USER_ROUTINE;
    op.routine_name   = "scalar_fn";
    op.outputs_mask   = 0x1u;
    op.operands.push_back(QValueRef{"out", make_loc(20)});
    op.operands.push_back(QValueRef{"42",  clang::SourceLocation()});
    op.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] USER_ROUTINE @<invalid> = out@20, 42@<invalid>  range=[30..40]"
        " routine_name=\"scalar_fn\" outputs_mask=0x1\n";
    CHECK_EQ_STR(dump(unit), want);
}

static void test_dump_user_routine_zero_operands() {
    // No-argument routine. The operand list is empty; the trailing
    // `routine_name=...` / `outputs_mask=0x0` annotation still renders.
    QScope scope;
    scope.open_brace  = make_loc(10);
    scope.close_brace = make_loc(50);

    QOperation op;
    op.kind           = QOpKind::USER_ROUTINE;
    op.routine_name   = "noop_fn";
    op.outputs_mask   = 0x0u;
    op.stmt_range = clang::SourceRange(make_loc(30), make_loc(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    const std::string want =
        "QUnit: 1 scope(s)\n"
        "  Scope[0] braces=[10..50]\n"
        "    Op[0] USER_ROUTINE @<invalid> =   range=[30..40]"
        " routine_name=\"noop_fn\" outputs_mask=0x0\n";
    CHECK_EQ_STR(dump(unit), want);
}

// ── dump() round-trip determinism ─────────────────────────────────────────────

static void test_dump_is_stable_across_calls() {
    // Running dump() twice on the same QUnit must produce byte-identical
    // strings. No internal mutation, no timestamps, no iteration-order
    // nondeterminism.
    QUnit unit;
    QScope s;
    s.open_brace  = make_loc(1);
    s.close_brace = make_loc(2);
    QOperation op;
    op.kind   = QOpKind::OR;
    op.result = QValueRef{"t", make_loc(1)};
    op.operands = { QValueRef{"a", make_loc(1)}, QValueRef{"b", make_loc(1)} };
    op.stmt_range = clang::SourceRange(make_loc(1), make_loc(2));
    s.ops.push_back(op);
    unit.scopes.push_back(s);

    std::string a = dump(unit);
    std::string b = dump(unit);
    CHECK_EQ_STR(a, b);
}

int main() {
    test_qvalueref_equality_same();
    test_qvalueref_inequality_name();
    test_qvalueref_inequality_loc();

    test_dump_empty_unit();
    test_dump_single_op();
    test_dump_multi_scope_and_order();
    test_dump_invalid_locations_render_as_placeholder();
    test_dump_zero_operands();
    test_dump_is_stable_across_calls();

    test_dump_add_assign_const();
    test_dump_sub_assign_const();
    test_dump_mul_assign_const();
    test_dump_div_assign_const();

    test_dump_add_assign_qint();
    test_dump_sub_assign_qint();
    test_dump_mul_assign_qint();
    test_dump_div_assign_qint();
    test_dump_mod_assign_qint();

    test_dump_and_op();

    test_dump_op_with_invalid_override_unchanged();
    test_dump_op_with_valid_override_surfaces();
    test_qunit_raw_insertions_is_default_empty();
    test_qunit_raw_insertions_round_trip();

    test_dump_skip_uncompute_default_unchanged();
    test_dump_skip_uncompute_true_appends_suffix();

    test_dump_user_routine_two_outputs();
    test_dump_user_routine_mixed_io_with_scalar();
    test_dump_user_routine_zero_operands();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
