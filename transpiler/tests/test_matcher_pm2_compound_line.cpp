// test_matcher_pm2_compound_line.cpp — PM2-2 tests for the `#line`
// directive emission in the Phase E compound-flatten matcher.
//
// The PM2-2 contract (see the plan's Step 2):
//
//   - For EVERY intermediate `qbool __stu_tN = ...;` decl the matcher
//     synthesizes, the QReplacement text must carry a
//     `#line <compound.begin.line> "<basename>"` directive immediately
//     preceding that decl's source line.
//   - Immediately before the final (user-named) VarDecl line the
//     replacement text must carry a restoring `#line
//     <var.begin.line> "<basename>"` directive so that Clang's line
//     counter re-aligns with the user's original source before any
//     follow-on user code is emitted.
//   - Both locations are resolved via `getPresumedLoc()` so a user-
//     authored `#line` pragma upstream of the compound init is
//     honored verbatim (PM2-1's helper contract).
//   - The `<basename>` is the leaf path component of the presumed
//     filename — it must NOT contain any directory prefix, so that
//     snapshot fixtures stay byte-reproducible across build trees.
//
// The tests below are structured as the PM2-2 TDD front half: each
// sub-test drives the compound matcher over a minimal hermetic source
// and asserts a specific invariant of the resulting QReplacement text.
// Collectively they pin the two directives + the basename rule.
//
// These tests complement — not replace — the snapshot fixtures in
// `tests/transpiler/fixtures/compound_*.expected.cpp`. The snapshots
// exercise the full driver pipeline end-to-end; these unit tests
// isolate the matcher's textual contract so a regression at the
// matcher layer alone can be diagnosed without comparing full fixture
// diffs.

#include "test_matcher_harness.hpp"

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

// Minimal qbool stub: `operator|` and `operator&` are all the Phase E
// compound-flatten matcher needs to resolve the nested bitwise
// initializer. Kept local so the test does not cross-link with the
// shared `kQBoolStub` (which declares extra overloads that would
// distract from the `#line` assertions).
constexpr std::string_view kQBoolPM22Stub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }

} // namespace sturm
using sturm::qbool;
)CPP";

// Run the Phase E compound matcher against `user_src` with `source_name`
// as the virtual file-name Clang reports in `#line` directives. The
// qbool stub is prepended automatically so only the target function
// body needs to be supplied by the caller.
QUnit run_compound_matcher(std::string_view user_src,
                           std::string_view source_name) {
    std::string code;
    code.reserve(kQBoolPM22Stub.size() + user_src.size());
    code.append(kQBoolPM22Stub);
    code.append(user_src);

    QUnit unit;
    clang::ast_matchers::MatchFinder finder;
    register_compound_qbool_matcher(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, std::string(source_name));
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PM2-2 compound matcher)\n");
    }
    return unit;
}

// Count the number of lines in `text` that START with (optional
// leading whitespace then) the token `#line`. The matcher embeds
// every `#line` directive on its own source line — after a `\n` and
// optional indent — so counting on line starts is the robust shape.
int count_line_directives(const std::string& text) {
    int count = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        std::size_t end = (eol == std::string::npos) ? text.size() : eol;
        // Strip leading whitespace.
        std::size_t i = pos;
        while (i < end && (text[i] == ' ' || text[i] == '\t')) {
            ++i;
        }
        // Token match on `#line`.
        static const std::string needle = "#line";
        if (i + needle.size() <= end &&
            text.compare(i, needle.size(), needle) == 0) {
            ++count;
        }
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    return count;
}

// True iff `text` contains `needle` as a plain substring.
bool contains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

// ── Case 1 — depth-2 compound emits two `#line` directives (one
//             compound prefix + one restoring) with the main-file
//             basename. ─────────────────────────────────────────────
void test_compound_depth2_emits_two_line_directives() {
    // The stub occupies ~13 lines (see kQBoolPM22Stub above); the
    // user-supplied body fires at the first VarDecl of function
    // `demo`. The source_name is a bare `foo.cpp` so the basename
    // assertion is both visible and semantically identity.
    QUnit unit = run_compound_matcher(
        "void demo(qbool b, qbool c, qbool d) {\n"
        "    qbool r = (b | c) & d;\n"
        "    (void)r;\n"
        "}\n",
        "foo.cpp");

    // Exactly one compound VarDecl → exactly one QReplacement.
    CHECK(unit.replacements.size() == 1);
    if (unit.replacements.size() != 1) return;
    const std::string& rep = unit.replacements.front().replacement;

    // Depth-2 shape: ONE intermediate (`__stu_t0 = b | c;`) plus the
    // final VarDecl. That is:
    //   - one `#line <compound.begin>` before the intermediate, and
    //   - one restoring `#line <var.begin>` before the final VarDecl.
    // So the replacement must contain exactly TWO `#line` directives.
    CHECK(count_line_directives(rep) == 2);

    // Basename-only filename. Even though the helper consumes
    // `getPresumedLoc().getFilename()` which may return an absolute
    // path under ClangTool (see `format_line_directive`'s PM2-2
    // normalization note), the emitted directive must carry only the
    // leaf: `"foo.cpp"`, never `"/…/foo.cpp"`.
    CHECK(contains(rep, "\"foo.cpp\""));
    CHECK(!contains(rep, "/foo.cpp"));

    // The synthesized intermediate decl must appear after the first
    // `#line` directive (i.e. the compound anchor precedes the decl).
    const std::size_t intermediate = rep.find("qbool __stu_t0 = b | c;");
    const std::size_t first_line = rep.find("#line");
    CHECK(first_line != std::string::npos);
    CHECK(intermediate != std::string::npos);
    CHECK(first_line < intermediate);

    // The restoring `#line` must sit between the last flat_line's `;`
    // and the final (user) VarDecl `qbool r = __stu_t0 & d`. We
    // locate both markers and assert ordering holds.
    const std::size_t final_decl = rep.find("qbool r = __stu_t0 & d");
    const std::size_t last_line  = rep.rfind("#line");
    CHECK(final_decl != std::string::npos);
    CHECK(last_line  != std::string::npos);
    CHECK(intermediate < last_line);
    CHECK(last_line < final_decl);
}

// ── Case 2 — depth-2 compound with both arms nested emits THREE
//             `#line` directives (two intermediates + one restoring).
void test_compound_both_arms_nested_emits_three_line_directives() {
    // Shape: `qbool r = (a | b) | (c | d);` — both arms of the outer
    // `|` are themselves `|` op-calls. The compound matcher flattens
    // into two intermediates (`__stu_t0 = a | b;`, `__stu_t1 = c | d;`)
    // plus the final `qbool r = __stu_t0 | __stu_t1`. That produces
    // three `#line` directives: TWO compound anchors (one before each
    // intermediate) plus one restoring `#line` before the final decl.
    QUnit unit = run_compound_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d) {\n"
        "    qbool r = (a | b) | (c | d);\n"
        "    (void)r;\n"
        "}\n",
        "nested.cpp");

    CHECK(unit.replacements.size() == 1);
    if (unit.replacements.size() != 1) return;
    const std::string& rep = unit.replacements.front().replacement;

    CHECK(count_line_directives(rep) == 3);
    CHECK(contains(rep, "\"nested.cpp\""));
    CHECK(contains(rep, "qbool __stu_t0 = a | b;"));
    CHECK(contains(rep, "qbool __stu_t1 = c | d;"));
    CHECK(contains(rep, "qbool r = __stu_t0 | __stu_t1"));
}

// ── Case 3 — The replacement text must begin with `\n` so that the
//             first `#line` directive lands on its own line. Without
//             the leading newline the `#line` would sit on the same
//             line as the preceding user source (`{ `) and the C
//             preprocessor would reject the directive.
void test_compound_replacement_starts_with_newline() {
    QUnit unit = run_compound_matcher(
        "void demo(qbool b, qbool c, qbool d) {\n"
        "    qbool r = (b | c) & d;\n"
        "    (void)r;\n"
        "}\n",
        "leading.cpp");

    CHECK(unit.replacements.size() == 1);
    if (unit.replacements.size() != 1) return;
    const std::string& rep = unit.replacements.front().replacement;

    CHECK(!rep.empty());
    if (rep.empty()) return;
    // First character must be `\n` so the first `#line` directive
    // starts a fresh line no matter what text precedes the range the
    // matcher is replacing.
    CHECK(rep.front() == '\n');
    // And the very next non-empty content must be a `#line` directive.
    const std::size_t first_non_nl = rep.find_first_not_of('\n');
    CHECK(first_non_nl != std::string::npos);
    CHECK(rep.compare(first_non_nl, 5, "#line") == 0);
}

// ── Case 4 — idempotency / line-number consistency. The compound
//             expression begin loc and the VarDecl begin loc sit on
//             the same physical source line in this fixture, so both
//             `#line` directives must report the same line number.
//             A divergence would mean the helper used the wrong
//             anchor for one of the two.
void test_compound_same_line_same_line_numbers() {
    // Stub is ~13 lines; the user body's VarDecl is at the first
    // substantive line after the function's `{`. Both `(b | c) & d`
    // and `qbool r` spell on the same source line, so their presumed
    // lines must match.
    QUnit unit = run_compound_matcher(
        "void demo(qbool b, qbool c, qbool d) {\n"
        "    qbool r = (b | c) & d;\n"
        "    (void)r;\n"
        "}\n",
        "matching_lines.cpp");

    CHECK(unit.replacements.size() == 1);
    if (unit.replacements.size() != 1) return;
    const std::string& rep = unit.replacements.front().replacement;

    // Extract each directive's line number. The stable shape is
    // `#line <N> "matching_lines.cpp"`; we do a conservative scan
    // that reads up to the first non-digit character past `#line `.
    auto extract_line_num = [&](std::size_t from) -> unsigned {
        const std::size_t start = rep.find("#line ", from);
        if (start == std::string::npos) return 0u;
        const std::size_t num_start = start + 6;  // past "#line "
        std::size_t p = num_start;
        unsigned value = 0;
        while (p < rep.size() && rep[p] >= '0' && rep[p] <= '9') {
            value = value * 10 + static_cast<unsigned>(rep[p] - '0');
            ++p;
        }
        return value;
    };

    const unsigned first_num  = extract_line_num(0);
    const std::size_t first_end = rep.find("#line ", 0);
    CHECK(first_end != std::string::npos);
    const unsigned second_num =
        first_end == std::string::npos ? 0u
                                       : extract_line_num(first_end + 5);

    CHECK(first_num != 0);
    CHECK(second_num != 0);
    CHECK(first_num == second_num);
}

// ── Entry point ────────────────────────────────────────────────────

void run_pm2_compound_line_tests_impl() {
    test_compound_depth2_emits_two_line_directives();
    test_compound_both_arms_nested_emits_three_line_directives();
    test_compound_replacement_starts_with_newline();
    test_compound_same_line_same_line_numbers();
}

} // namespace

// Declared in test_matcher_harness.hpp so the dispatcher in
// test_matcher.cpp can call us.
void run_pm2_compound_line_tests() {
    run_pm2_compound_line_tests_impl();
}
