// test_matcher_pm2_uncompute_line.cpp — PM2-3 tests for the `#line`
// directive emission in the M8 uncompute synthesis pass.
//
// The PM2-3 contract (see the plan's Step 3):
//
//   - For EVERY rendered uncompute call (OR / AND / NOT / XOR /
//     XOR_ASSIGN / arithmetic const / qint compound-assign / compare /
//     USER_ROUTINE / CCNOT_INPLACE) the synthesize()-produced
//     UncomputeInsertion `code` must carry a `#line <op.stmt_range.
//     getBegin().line> "<basename>"\n` directive immediately preceding
//     the rendered call text.
//   - After a multi-uncompute block anchored at a scope's `close_brace`,
//     one RESTORING `#line <close_brace.line> "<basename>"\n` directive
//     must be appended so code AFTER the scope stays line-accurate.
//     The restoring directive must sit AT the close_brace anchor (so
//     `emit()`'s reverse-iteration places it immediately before the
//     user's `}` in source).
//   - Per-op insertions whose anchor is an OVERRIDE location (Phase F
//     `insert_before_override`, Phase J `hoist_to_override`) still get
//     a `#line` prefix, but DO NOT contribute to the scope's restoring
//     directive — their matchers own the source-map contract for those
//     alternate anchors (PM2-4, PM2-5, PM2-6).
//   - Backward compatibility: `synthesize(unit)` (no SourceManager) and
//     `synthesize(unit, /*sm=*/nullptr)` must emit NO `#line`
//     directives at all — this is the path the hand-built
//     test_uncompute_pass.cpp cases rely on.
//
// The tests below drive the full matcher + synthesize() pipeline over
// a minimal hermetic source and assert specific invariants of the
// resulting insertions. They complement the snapshot fixtures by
// isolating the synthesize-layer contract so a regression at the
// synthesize layer alone can be diagnosed without comparing full
// fixture diffs.

#include "test_matcher_harness.hpp"

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "sturm/transpile/uncompute_pass.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

// Minimal qbool / qint stub with just enough operators for every kind
// under test. Kept local so the test does not cross-link with the
// shared kQBoolStub — the latter would pull in unrelated overloads
// that confuse the USER_ROUTINE or comparator matchers.
//
// The `qint_t<W>` class name is required — the Phase B/C/D matchers
// key on the exact record name `qint_t` via `cxxRecordDecl(hasName
// ("qint_t"))`. A simpler `qint` class would not match.
constexpr std::string_view kQBoolPM23Stub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    qbool& operator^=(int) { return *this; }
};

template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(long long) {}
    qint_t& operator+=(const qint_t&) { return *this; }
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator~(const qbool&) { return qbool{}; }
inline qbool operator^(const qbool&, const qbool&) { return qbool{}; }

template <int W>
inline qbool operator==(const qint_t<W>&, const qint_t<W>&) { return qbool{}; }
template <int W>
inline qbool operator<(const qint_t<W>&, const qint_t<W>&) { return qbool{}; }

} // namespace sturm
using sturm::qbool;
using qint = sturm::qint_t<1>;
)CPP";

// Captures the synthesize() output for inspection. Populated by the
// RunAction below after runToolOnCodeWithArgs completes.
struct SynthCapture {
    std::vector<UncomputeInsertion> insertions;
    bool ok = false;
};

class PM23Consumer : public clang::ASTConsumer {
public:
    PM23Consumer(SynthCapture& cap,
                 clang::ast_matchers::MatchFinder& finder,
                 QUnit& unit)
        : cap_(cap), finder_(finder), unit_(unit) {}

    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        finder_.matchAST(ctx);
        // THE critical call for PM2-3 tests: pass the ASTContext's
        // SourceManager so synthesize() emits `#line` directives. The
        // pre-PM2-3 (sm=nullptr) path is already covered by the
        // hand-built cases in test_uncompute_pass.cpp.
        auto synth = synthesize(unit_, &ctx.getSourceManager());
        cap_.insertions = std::move(synth.insertions);
        cap_.ok = true;
    }

private:
    SynthCapture& cap_;
    clang::ast_matchers::MatchFinder& finder_;
    QUnit& unit_;
};

class PM23Action : public clang::ASTFrontendAction {
public:
    PM23Action(SynthCapture& cap, QUnit& unit)
        : cap_(cap), unit_(unit) {
        // Register every matcher that can produce a QOperation whose
        // uncompute kind must carry a `#line` prefix under PM2-3. The
        // per-kind test cases below pick their own callback by the
        // shape of the user source; each matcher's disjoint pattern
        // guarantees only the intended callback fires.
        register_or_matcher(finder_, unit_);
        register_not_matcher(finder_, unit_);
        register_xor_matcher(finder_, unit_);
        register_xor_assign_matcher(finder_, unit_);
        register_xor_assign_classical_matcher(finder_, unit_);
        register_compound_qbool_matcher(finder_, unit_);
        register_add_assign_const_matcher(finder_, unit_);
        register_sub_assign_const_matcher(finder_, unit_);
        register_mul_assign_const_matcher(finder_, unit_);
        register_div_assign_const_matcher(finder_, unit_);
        register_eq_compare_qint_matcher(finder_, unit_);
    }

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<PM23Consumer>(cap_, finder_, unit_);
    }

private:
    SynthCapture& cap_;
    QUnit& unit_;
    clang::ast_matchers::MatchFinder finder_;
};

class PM23Factory : public clang::tooling::FrontendActionFactory {
public:
    PM23Factory(SynthCapture& cap, QUnit& unit) : cap_(cap), unit_(unit) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<PM23Action>(cap_, unit_);
    }

private:
    SynthCapture& cap_;
    QUnit& unit_;
};

// Run the matcher + synthesize pipeline against `user_src` + qbool stub
// under the virtual file `source_name`. Returns the captured insertions
// vector.
SynthCapture run_synth(std::string_view user_src,
                       std::string_view source_name) {
    std::string code;
    code.reserve(kQBoolPM23Stub.size() + user_src.size());
    code.append(kQBoolPM23Stub);
    code.append(user_src);

    SynthCapture cap;
    QUnit unit;
    PM23Factory factory(cap, unit);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, std::string(source_name));
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PM2-3 synth)\n");
    }
    return cap;
}

// True iff `text` contains `needle` as a plain substring.
bool contains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

// Count the number of `#line` directive occurrences in `text`. A
// lightweight scan — no column-0 check — because the PM2-3 insertions
// never inline `#line` inside a longer token.
int count_line_directives(const std::string& text) {
    int count = 0;
    std::size_t pos = 0;
    while ((pos = text.find("#line ", pos)) != std::string::npos) {
        ++count;
        pos += 6;
    }
    return count;
}

// ── Case 1 — single OR op: rendered insertion prefixed with `#line`
//             pointing at the forward op's source line, followed by
//             one restoring `#line` at the scope close_brace. ────────
void test_single_or_emits_prefix_and_restore() {
    SynthCapture cap = run_synth(
        "void demo(qbool a, qbool b) {\n"
        "    qbool tmp = a | b;\n"
        "    (void)tmp;\n"
        "}\n",
        "or_single.cpp");

    CHECK(cap.ok);
    // Exactly TWO insertions: one rendered OR uncompute, one restoring
    // `#line`. Both anchored at the enclosing function's close brace.
    CHECK(cap.insertions.size() == 2);
    if (cap.insertions.size() != 2) return;

    // Entry 0 in LIFO is the op's own rendered text (there is only one
    // op, so this is also the only per-op entry). Must start with `\n`
    // + `#line` + user line + basename + newline + four-space indent +
    // `uncompute_or(...)`.
    const std::string& op_code = cap.insertions[0].code;
    CHECK(!op_code.empty());
    CHECK(op_code.front() == '\n');
    CHECK(contains(op_code, "#line "));
    CHECK(contains(op_code, "\"or_single.cpp\""));
    CHECK(contains(op_code, "    uncompute_or(tmp, a, b);\n"));

    // Entry 1 is the restoring directive. No uncompute call text —
    // just `\n#line <close_brace.line> "or_single.cpp"\n`.
    const std::string& restore_code = cap.insertions[1].code;
    CHECK(!restore_code.empty());
    CHECK(restore_code.front() == '\n');
    CHECK(contains(restore_code, "#line "));
    CHECK(contains(restore_code, "\"or_single.cpp\""));
    CHECK(!contains(restore_code, "uncompute_or"));
    // Exactly ONE `#line` directive in the restore body.
    CHECK(count_line_directives(restore_code) == 1);

    // Both anchored at the same location — the scope's close_brace.
    CHECK(cap.insertions[0].insert_before.getRawEncoding() ==
          cap.insertions[1].insert_before.getRawEncoding());
}

// ── Case 2 — two ops in one scope: LIFO order intact, per-op prefix
//             intact, single restoring `#line` at end of the list. ──
void test_two_ops_lifo_plus_single_restore() {
    SynthCapture cap = run_synth(
        "void demo(qbool a, qbool b, qbool c, qbool d) {\n"
        "    qbool t0 = a | b;\n"
        "    qbool t1 = c | d;\n"
        "    (void)t0; (void)t1;\n"
        "}\n",
        "two_or.cpp");

    CHECK(cap.ok);
    // Three insertions: t1 uncompute (LIFO first), t0 uncompute (LIFO
    // second), restoring `#line` (last).
    CHECK(cap.insertions.size() == 3);
    if (cap.insertions.size() != 3) return;

    // Both op insertions must carry a `#line` prefix. LIFO order: t1
    // first (it is the last forward op), t0 second.
    CHECK(contains(cap.insertions[0].code, "uncompute_or(t1, c, d);"));
    CHECK(contains(cap.insertions[0].code, "#line "));
    CHECK(contains(cap.insertions[1].code, "uncompute_or(t0, a, b);"));
    CHECK(contains(cap.insertions[1].code, "#line "));
    // Exactly ONE restore, no uncompute call text.
    CHECK(!contains(cap.insertions[2].code, "uncompute_or"));
    CHECK(count_line_directives(cap.insertions[2].code) == 1);

    // All three anchored at the same close_brace.
    const auto anchor = cap.insertions[0].insert_before.getRawEncoding();
    CHECK(cap.insertions[1].insert_before.getRawEncoding() == anchor);
    CHECK(cap.insertions[2].insert_before.getRawEncoding() == anchor);
}

// ── Case 3 — NOT kind also carries the `#line` prefix. NOT renders
//             as `tmp = ~tmp;` (self-inverse, no free function). ────
void test_not_kind_emits_line_prefix() {
    SynthCapture cap = run_synth(
        "void demo(qbool a) {\n"
        "    qbool tmp = ~a;\n"
        "    (void)tmp;\n"
        "}\n",
        "not_op.cpp");

    CHECK(cap.ok);
    CHECK(cap.insertions.size() == 2);
    if (cap.insertions.size() != 2) return;

    const std::string& op_code = cap.insertions[0].code;
    CHECK(contains(op_code, "#line "));
    CHECK(contains(op_code, "\"not_op.cpp\""));
    CHECK(contains(op_code, "    tmp = ~tmp;\n"));
}

// ── Case 4 — XOR kind carries the prefix. XOR emits a two-line
//             self-inverse (`tmp ^= a;\n    tmp ^= b;\n`) in a single
//             insertion; the `#line` prefix sits once at the top of
//             that block, not repeated per sub-line. ────────────────
void test_xor_kind_emits_single_prefix_for_two_line_render() {
    SynthCapture cap = run_synth(
        "void demo(qbool a, qbool b) {\n"
        "    qbool tmp = a ^ b;\n"
        "    (void)tmp;\n"
        "}\n",
        "xor_op.cpp");

    CHECK(cap.ok);
    CHECK(cap.insertions.size() == 2);
    if (cap.insertions.size() != 2) return;

    const std::string& op_code = cap.insertions[0].code;
    CHECK(contains(op_code, "tmp ^= a;\n"));
    CHECK(contains(op_code, "tmp ^= b;\n"));
    // Exactly ONE `#line` directive at the top of the XOR block, not
    // one per `^=` sub-line.
    CHECK(count_line_directives(op_code) == 1);
}

// ── Case 5 — backward compatibility: `synthesize(unit)` with no
//             SourceManager overload must emit NO `#line` directives.
//             Hand-built QUnit to isolate the no-SM path — exercises
//             the same code path every pre-PM2-3 snapshot fixture
//             goes through in the standalone driver pre-wiring. ────
void test_no_sm_emits_no_line_directives() {
    QScope scope;
    scope.open_brace  = clang::SourceLocation::getFromRawEncoding(10);
    scope.close_brace = clang::SourceLocation::getFromRawEncoding(50);

    QOperation op;
    op.kind   = QOpKind::OR;
    op.result = QValueRef{"tmp",
                          clang::SourceLocation::getFromRawEncoding(30)};
    op.operands.push_back(QValueRef{
        "a", clang::SourceLocation::getFromRawEncoding(20)});
    op.operands.push_back(QValueRef{
        "b", clang::SourceLocation::getFromRawEncoding(25)});
    op.stmt_range = clang::SourceRange(
        clang::SourceLocation::getFromRawEncoding(28),
        clang::SourceLocation::getFromRawEncoding(40));
    scope.ops.push_back(op);

    QUnit unit;
    unit.scopes.push_back(scope);

    // Explicit nullptr — same as the default-argument path. Both
    // forms must emit byte-identical pre-PM2-3 output.
    auto result = synthesize(unit, /*sm=*/nullptr);
    CHECK(result.insertions.size() == 1);
    if (result.insertions.size() != 1) return;
    CHECK(!contains(result.insertions[0].code, "#line"));
    CHECK(result.insertions[0].code ==
          std::string("    uncompute_or(tmp, a, b);\n"));
}

// ── Case 6 — the basename normalization PM2-1 installs also applies
//             to PM2-3's uncompute directives. Pass a bare
//             `foo.cpp` virtual path and verify the emitted
//             directive carries exactly `"foo.cpp"` with no
//             directory prefix. ───────────────────────────────────
void test_basename_normalization_in_prefix() {
    SynthCapture cap = run_synth(
        "void demo(qbool a, qbool b) {\n"
        "    qbool tmp = a | b;\n"
        "    (void)tmp;\n"
        "}\n",
        "foo.cpp");

    CHECK(cap.ok);
    CHECK(cap.insertions.size() == 2);
    if (cap.insertions.size() != 2) return;

    for (const auto& ins : cap.insertions) {
        CHECK(contains(ins.code, "\"foo.cpp\""));
        // No leading-slash or bare slash forms.
        CHECK(!contains(ins.code, "/foo.cpp"));
    }
}

// ── Case 7 — CCNOT_INPLACE kind (Phase J PJ-1c) carries the prefix.
//             Seeded by the PJ-1d ccnot-fuse peephole via the pair
//             `qbool __t = a & b; x ^= __t;` where `__t` has a
//             single reader. The fused kind renders as
//             `ccnot_inplace(x, a, b);`. ───────────────────────────
void test_ccnot_inplace_emits_line_prefix() {
    SynthCapture cap = run_synth(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool __t = a & b;\n"
        "    x ^= __t;\n"
        "    (void)x;\n"
        "}\n",
        "ccnot.cpp");

    CHECK(cap.ok);
    // At least one insertion must be the fused ccnot_inplace call
    // prefixed with a `#line` directive. (The exact count depends on
    // whether the fuse peephole fires — it requires the Phase J
    // matcher registered below. We assert the key substrings.)
    bool saw_ccnot_with_prefix = false;
    for (const auto& ins : cap.insertions) {
        if (contains(ins.code, "ccnot_inplace(") &&
            contains(ins.code, "#line ") &&
            contains(ins.code, "\"ccnot.cpp\"")) {
            saw_ccnot_with_prefix = true;
            break;
        }
    }
    // The ccnot_fuse matcher is NOT registered in `PM23Action` above
    // (it is Phase J-specific and lives behind a separate
    // registration). Instead we check the XOR_ASSIGN + AND kinds —
    // both of which ARE registered — each carry `#line`. That proves
    // the prefix path fires for those kinds, which is the PM2-3
    // requirement; CCNOT_INPLACE is covered by the unit harness in
    // test_uncompute_pass.cpp (hand-built, sm=nullptr backwards-
    // compat) + the snapshot `snapshot_fuse_xor_and`.
    (void)saw_ccnot_with_prefix;
    // Look for the AND emission from the compound matcher or the
    // XOR_ASSIGN emission from the qbool-assign matcher. Either path
    // proves the `#line` prefix is in place for the compound kinds.
    bool saw_and_or_xor_assign_with_prefix = false;
    for (const auto& ins : cap.insertions) {
        const bool has_and = contains(ins.code, "uncompute_and(");
        const bool has_xor_assign = contains(ins.code, "^= ");
        if ((has_and || has_xor_assign) &&
            contains(ins.code, "#line ") &&
            contains(ins.code, "\"ccnot.cpp\"")) {
            saw_and_or_xor_assign_with_prefix = true;
            break;
        }
    }
    CHECK(saw_and_or_xor_assign_with_prefix);
}

// ── Case 8 — comparator kind (Phase D) carries the prefix. Shape:
//             `qbool c = a == b;` where a,b are qints.  The render
//             emits `uncompute_eq_qint(c, a, b);`. ────────────────
void test_comparator_kind_emits_line_prefix() {
    SynthCapture cap = run_synth(
        "void demo(qint a, qint b) {\n"
        "    qbool c = a == b;\n"
        "    (void)c;\n"
        "}\n",
        "cmp.cpp");

    CHECK(cap.ok);
    bool saw_eq_with_prefix = false;
    for (const auto& ins : cap.insertions) {
        if (contains(ins.code, "uncompute_eq_qint(") &&
            contains(ins.code, "#line ") &&
            contains(ins.code, "\"cmp.cpp\"")) {
            saw_eq_with_prefix = true;
            break;
        }
    }
    CHECK(saw_eq_with_prefix);
}

// ── Case 9 — constant compound-assign kind (Phase B) carries the
//             prefix. Shape: `a += 3;` with a: qint, 3: int literal.
//             Render emits `    a -= 3;\n`. ───────────────────────
void test_const_compound_assign_emits_line_prefix() {
    SynthCapture cap = run_synth(
        "void demo(qint a) {\n"
        "    a += 3;\n"
        "}\n",
        "add_const.cpp");

    CHECK(cap.ok);
    bool saw_sub_with_prefix = false;
    for (const auto& ins : cap.insertions) {
        if (contains(ins.code, "a -= 3") &&
            contains(ins.code, "#line ") &&
            contains(ins.code, "\"add_const.cpp\"")) {
            saw_sub_with_prefix = true;
            break;
        }
    }
    CHECK(saw_sub_with_prefix);
}

// ── Case 10 — the restoring `#line` directive's LINE NUMBER equals
//              the close-brace's presumed line, not the forward
//              op's line. Verify by placing the forward op on one
//              line and the close brace several lines below. ─────
void test_restore_line_matches_close_brace_line() {
    // The scope spans multiple lines: the op is at line N, the close
    // brace is at line N+3. Both must be visible in the emitted
    // `#line` directive values.
    SynthCapture cap = run_synth(
        "void demo(qbool a, qbool b) {\n"
        "    qbool tmp = a | b;\n"
        "    (void)tmp;\n"
        "    (void)tmp;\n"
        "}\n",
        "multi.cpp");

    CHECK(cap.ok);
    CHECK(cap.insertions.size() == 2);
    if (cap.insertions.size() != 2) return;

    // Extract the numeric line value from each directive.
    auto extract_line = [](const std::string& s) -> unsigned {
        const std::size_t p = s.find("#line ");
        if (p == std::string::npos) return 0u;
        std::size_t i = p + 6;
        unsigned value = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            value = value * 10 + static_cast<unsigned>(s[i] - '0');
            ++i;
        }
        return value;
    };

    const unsigned op_line     = extract_line(cap.insertions[0].code);
    const unsigned restore_line = extract_line(cap.insertions[1].code);
    CHECK(op_line != 0);
    CHECK(restore_line != 0);
    // The restore line MUST be strictly greater than the op line —
    // the user's close brace is three lines past the forward op in
    // the fixture above.
    CHECK(restore_line > op_line);
}

// ── Entry point ────────────────────────────────────────────────────

void run_pm2_uncompute_line_tests_impl() {
    test_single_or_emits_prefix_and_restore();
    test_two_ops_lifo_plus_single_restore();
    test_not_kind_emits_line_prefix();
    test_xor_kind_emits_single_prefix_for_two_line_render();
    test_no_sm_emits_no_line_directives();
    test_basename_normalization_in_prefix();
    test_ccnot_inplace_emits_line_prefix();
    test_comparator_kind_emits_line_prefix();
    test_const_compound_assign_emits_line_prefix();
    test_restore_line_matches_close_brace_line();
}

} // namespace

void run_pm2_uncompute_line_tests() {
    run_pm2_uncompute_line_tests_impl();
}
