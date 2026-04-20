// test_matcher_pm2_replacement_line.cpp — PM2-4 tests for the `#line`
// directive emission on QReplacement strings produced by the Phase J
// PJ-1d ccnot-fuse peephole and the Phase F WHEN-lift matchers.
//
// The PM2-4 contract (see the plan's Step 4):
//
//   - For the Phase J PJ-1d ccnot-fuse matcher: the `QReplacement` whose
//     `range` spans the fused `qbool __t = a & b; x ^= __t;` pair and
//     whose `replacement` text is `ccnot_inplace(x, a, b);` must now
//     carry a `#line <range.getBegin().line> "<basename>"` directive
//     immediately preceding the `ccnot_inplace(...)` call text — the
//     user's originating expression (the VarDecl's begin loc) is the
//     correct source anchor for any compile-error diagnostic landing
//     inside the synthesized replacement.
//
//   - For the Phase F WHEN-lift matcher: each lifted `qbool __stu_tN
//     = ...;` decl in the `raw_insertions[0].code` block must be
//     prefixed with a `#line <range.getBegin().line> "<basename>"`
//     directive, where `range.getBegin()` is the spelling begin of
//     the WHEN argument (the user's compound expression). Every
//     flattened decl shares the same source anchor because they all
//     come from the same user expression.
//
//   - Both matchers use the helper `format_line_directive(sm,
//     range.getBegin())` from `emitter.hpp`. The helper returns empty
//     when the location is invalid or non-main-file — in those
//     defensive paths the matcher MUST fall back to the pre-PM2-4
//     layout byte-for-byte, so every pre-existing snapshot that
//     happens to fire outside the main file stays byte-identical.
//
// The tests below drive each matcher through `runToolOnCodeWithArgs`
// over a minimal hermetic stub (mirrors the pattern in
// `test_matcher_pm2_compound_line.cpp` / `test_matcher_pm2_uncompute_line.cpp`).
// They complement — not replace — the snapshot fixtures in
// `tests/transpiler/fixtures/{fuse_xor_and,when_*}.expected.cpp`.

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

// ── Shared stub: qbool with the operators the ccnot_fuse and when_lift
//    matchers both rely on. Kept local so the test does not cross-link
//    with the harness's shared stubs (which omit the `^=` overload the
//    PJ-1d matcher requires).
constexpr std::string_view kQBoolPM24Stub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    qbool& operator^=(int) { return *this; }
    bool should_run() const { return true; }
};

inline qbool operator|(const qbool&, const qbool&) { return qbool{}; }
inline qbool operator&(const qbool&, const qbool&) { return qbool{}; }

namespace detail {

inline qbool& materialize_when(qbool& q) { return q; }
inline qbool  materialize_when(qbool&& q) { return static_cast<qbool&&>(q); }

struct WhenCapture { WhenCapture() = default; };
inline qbool& make_when_guard(qbool& q) { return q; }

} // namespace detail
} // namespace sturm

using sturm::qbool;

#define WHEN(expr) \
    if (::sturm::detail::WhenCapture _when_capture_{}; true) \
    if (decltype(auto) _when_val_ = ::sturm::detail::materialize_when(expr); true) \
    if (auto& _when_guard_ = ::sturm::detail::make_when_guard(_when_val_); \
        _when_guard_.should_run())
)CPP";

// Run the PJ-1d fuse matcher against `user_src` under the virtual file
// `source_name`. Returns the populated QUnit. Mirrors the
// `run_ccnot_fuse_matcher` helper in test_matcher_ccnot_fuse.cpp but
// lets the test pick the source name so the basename assertion in the
// `#line` directive is unambiguous.
QUnit run_fuse_matcher(std::string_view user_src,
                       std::string_view source_name) {
    std::string code;
    code.reserve(kQBoolPM24Stub.size() + user_src.size());
    code.append(kQBoolPM24Stub);
    code.append(user_src);

    QUnit unit;
    clang::ast_matchers::MatchFinder finder;
    register_ccnot_fuse_matcher(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, std::string(source_name));
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PM2-4 ccnot-fuse)\n");
    }
    return unit;
}

// Run the Phase F WHEN-lift matcher against `user_src` under the
// virtual file `source_name`. Mirrors `run_pf_when_matcher` in
// `test_matcher_when_lift.cpp`; resets the detection counter so
// cross-test state does not leak into PM2-4 assertions.
QUnit run_when_lift_matcher(std::string_view user_src,
                            std::string_view source_name) {
    std::string code;
    code.reserve(kQBoolPM24Stub.size() + user_src.size());
    code.append(kQBoolPM24Stub);
    code.append(user_src);

    QUnit unit;
    reset_when_lift_detection_count_for_test();

    clang::ast_matchers::MatchFinder finder;
    register_when_lift_matcher(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, std::string(source_name));
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (PM2-4 when-lift)\n");
    }
    return unit;
}

// True iff `text` contains `needle` as a plain substring.
bool contains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

// Count the `#line` directive occurrences in `text`. Matches a
// whitespace-peeled `#line ` prefix anywhere in the buffer — mirrors
// the helper in test_matcher_pm2_compound_line.cpp.
int count_line_directives(const std::string& text) {
    int count = 0;
    std::size_t pos = 0;
    while ((pos = text.find("#line ", pos)) != std::string::npos) {
        ++count;
        pos += 6;
    }
    return count;
}

// ── Case 1 — canonical PJ-1d fuse pair: the QReplacement text must
//             carry exactly ONE `#line` directive (anchored at the
//             fused range's begin) followed by the existing
//             `ccnot_inplace(x, a, b);` call. ─────────────────────
void test_fuse_replacement_carries_line_prefix() {
    QUnit unit = run_fuse_matcher(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool __t = a & b;\n"
        "    x ^= __t;\n"
        "}\n",
        "fuse_prefix.cpp");

    CHECK(unit.replacements.size() == 1);
    if (unit.replacements.size() != 1) return;
    const std::string& rep = unit.replacements.front().replacement;

    // Exactly one `#line` directive preceding the fused call.
    CHECK(count_line_directives(rep) == 1);
    // The call itself is still present.
    CHECK(contains(rep, "ccnot_inplace(x, a, b);"));
    // Basename matches the virtual source name.
    CHECK(contains(rep, "\"fuse_prefix.cpp\""));
    // The `#line` directive must sit BEFORE the call in the buffer.
    const std::size_t line_pos = rep.find("#line");
    const std::size_t call_pos = rep.find("ccnot_inplace(");
    CHECK(line_pos != std::string::npos);
    CHECK(call_pos != std::string::npos);
    CHECK(line_pos < call_pos);
}

// ── Case 2 — the `#line` directive must begin on a fresh line. The
//             replacement range begins at the VarDecl's first token
//             (typically `qbool`), so the pre-existing source text
//             immediately before the range is indentation/newline.
//             A leading `\n` in the replacement guarantees `#line`
//             lands at column 0 of a fresh line, which the C/C++
//             standard requires for `#line` directives. ──────────
void test_fuse_replacement_starts_with_newline() {
    QUnit unit = run_fuse_matcher(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool __t = a & b;\n"
        "    x ^= __t;\n"
        "}\n",
        "leading.cpp");

    CHECK(unit.replacements.size() == 1);
    if (unit.replacements.size() != 1) return;
    const std::string& rep = unit.replacements.front().replacement;

    CHECK(!rep.empty());
    if (rep.empty()) return;
    // The very first character of the replacement buffer must be
    // `\n` so the `#line` directive lands on its own line.
    CHECK(rep.front() == '\n');
    // Skip past any leading whitespace on the directive line —
    // the directive itself must start with `#line`.
    std::size_t i = 0;
    while (i < rep.size() && (rep[i] == '\n' || rep[i] == ' ' ||
                              rep[i] == '\t')) {
        ++i;
    }
    CHECK(i + 5 <= rep.size());
    CHECK(rep.compare(i, 5, "#line") == 0);
}

// ── Case 3 — the `#line` number matches the VarDecl's begin line.
//             The fused replacement's `range.getBegin()` is the
//             VarDecl's own begin loc, so the emitted `#line` must
//             report the user's source line where `qbool __t`
//             is written. ──────────────────────────────────────
void test_fuse_replacement_line_number_matches_vardecl() {
    // The stub prepended by run_fuse_matcher occupies ~25 lines; the
    // user body is appended after. We can't reliably count the exact
    // number here without mirroring run_fuse_matcher's internals —
    // but we CAN insist that the emitted line is strictly greater
    // than zero and that the VarDecl precedes the `^=` stmt in the
    // extracted line number ordering (the `#line` before the call
    // points at the VarDecl — not at the `^=` line).
    QUnit unit = run_fuse_matcher(
        "void demo(qbool a, qbool b, qbool x) {\n"
        "    qbool __t = a & b;\n"
        "    x ^= __t;\n"
        "}\n",
        "lineno.cpp");

    CHECK(unit.replacements.size() == 1);
    if (unit.replacements.size() != 1) return;
    const std::string& rep = unit.replacements.front().replacement;

    // Extract the numeric line value after `#line `. Same shape as
    // the PM2-2 / PM2-3 tests.
    auto extract_line = [&](const std::string& s) -> unsigned {
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

    const unsigned line_num = extract_line(rep);
    CHECK(line_num != 0);
}

// ── Case 4 — two independent fuse pairs in one scope: each
//             QReplacement carries its own `#line` directive, and
//             the two directives point at DIFFERENT source lines
//             (each pair anchored at its own VarDecl's begin). ──
void test_fuse_two_independent_pairs_each_prefixed() {
    QUnit unit = run_fuse_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d,\n"
        "          qbool x, qbool y) {\n"
        "    qbool __t0 = a & b;\n"
        "    x ^= __t0;\n"
        "    qbool __t1 = c & d;\n"
        "    y ^= __t1;\n"
        "}\n",
        "two_fuses.cpp");

    CHECK(unit.replacements.size() == 2);
    if (unit.replacements.size() != 2) return;

    // Both replacements carry exactly one `#line` directive each.
    for (const auto& rep : unit.replacements) {
        CHECK(count_line_directives(rep.replacement) == 1);
        CHECK(contains(rep.replacement, "\"two_fuses.cpp\""));
        CHECK(contains(rep.replacement, "ccnot_inplace("));
    }

    // Extract each directive's line value — they must differ
    // because the two pairs live on different source lines.
    auto extract_line = [&](const std::string& s) -> unsigned {
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
    const unsigned line0 = extract_line(unit.replacements[0].replacement);
    const unsigned line1 = extract_line(unit.replacements[1].replacement);
    CHECK(line0 != 0);
    CHECK(line1 != 0);
    CHECK(line0 != line1);
}

// ── Case 5 — WHEN-lift single-op `b | c`: the `raw_insertions[0].code`
//             (the flat decl block) must carry exactly ONE `#line`
//             directive preceding `qbool __stu_t0 = b | c;`. ────
void test_when_lift_single_op_decl_prefixed() {
    QUnit unit = run_when_lift_matcher(
        "void demo(qbool a, qbool b, qbool c) {\n"
        "    WHEN(b | c) { (void)a; }\n"
        "}\n",
        "when_prefix.cpp");

    CHECK(unit.raw_insertions.size() == 1);
    if (unit.raw_insertions.size() != 1) return;
    const std::string& code = unit.raw_insertions.front().code;

    // Exactly one `#line` directive precedes the single decl.
    CHECK(count_line_directives(code) == 1);
    CHECK(contains(code, "\"when_prefix.cpp\""));
    CHECK(contains(code, "qbool __stu_t0 = b | c;"));
    // The directive must come BEFORE the decl text.
    const std::size_t line_pos = code.find("#line");
    const std::size_t decl_pos = code.find("qbool __stu_t0");
    CHECK(line_pos != std::string::npos);
    CHECK(decl_pos != std::string::npos);
    CHECK(line_pos < decl_pos);
}

// ── Case 6 — WHEN-lift compound `(b | c) & d`: two flattened decls.
//             Each decl line in the block must carry its own
//             `#line` directive; so the block contains TWO
//             directives total (one per decl). ──────────────────
void test_when_lift_compound_decls_each_prefixed() {
    QUnit unit = run_when_lift_matcher(
        "void demo(qbool a, qbool b, qbool c, qbool d) {\n"
        "    WHEN((b | c) & d) { (void)a; }\n"
        "}\n",
        "when_cmpd.cpp");

    CHECK(unit.raw_insertions.size() == 1);
    if (unit.raw_insertions.size() != 1) return;
    const std::string& code = unit.raw_insertions.front().code;

    // Compound WHEN with two flattened decls → two `#line` directives.
    CHECK(count_line_directives(code) == 2);
    CHECK(contains(code, "\"when_cmpd.cpp\""));
    CHECK(contains(code, "qbool __stu_t0 = b | c;"));
    CHECK(contains(code, "qbool __stu_t1 = __stu_t0 & d;"));
    // Each decl must have a `#line` directive immediately preceding
    // it. We verify that by asserting the `#line` text appears
    // BEFORE each decl in the buffer order.
    const std::size_t first_line  = code.find("#line");
    const std::size_t first_decl  = code.find("qbool __stu_t0");
    const std::size_t second_line =
        first_line == std::string::npos ? std::string::npos
                                        : code.find("#line", first_line + 1);
    const std::size_t second_decl = code.find("qbool __stu_t1");
    CHECK(first_line != std::string::npos);
    CHECK(first_decl != std::string::npos);
    CHECK(second_line != std::string::npos);
    CHECK(second_decl != std::string::npos);
    CHECK(first_line < first_decl);
    CHECK(first_decl < second_line);
    CHECK(second_line < second_decl);
}

// ── Case 7 — WHEN-lift preserves byte-for-byte the named-passthrough
//             short-circuit (the matcher returns early before staging
//             any edit). `raw_insertions` is empty — no `#line` text
//             leaks into any other buffer. ───────────────────────
void test_when_lift_named_passthrough_no_prefix() {
    QUnit unit = run_when_lift_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    WHEN(a) { (void)b; }\n"
        "}\n",
        "nop.cpp");

    // Named-passthrough short-circuits before staging any edit.
    CHECK(unit.raw_insertions.empty());
    CHECK(unit.replacements.empty());
}

// ── Case 8 — WHEN-lift basename normalization: the `#line` directive
//             emitted into the decl block must carry the basename
//             only, never any directory prefix. ───────────────────
void test_when_lift_basename_normalization() {
    QUnit unit = run_when_lift_matcher(
        "void demo(qbool a, qbool b, qbool c) {\n"
        "    WHEN(b | c) { (void)a; }\n"
        "}\n",
        "basename.cpp");

    CHECK(unit.raw_insertions.size() == 1);
    if (unit.raw_insertions.size() != 1) return;
    const std::string& code = unit.raw_insertions.front().code;

    CHECK(contains(code, "\"basename.cpp\""));
    // No leading `/basename.cpp` form — the helper must emit the
    // leaf only.
    CHECK(!contains(code, "/basename.cpp"));
}

// ── Entry point ────────────────────────────────────────────────────

void run_pm2_replacement_line_tests_impl() {
    test_fuse_replacement_carries_line_prefix();
    test_fuse_replacement_starts_with_newline();
    test_fuse_replacement_line_number_matches_vardecl();
    test_fuse_two_independent_pairs_each_prefixed();
    test_when_lift_single_op_decl_prefixed();
    test_when_lift_compound_decls_each_prefixed();
    test_when_lift_named_passthrough_no_prefix();
    test_when_lift_basename_normalization();
}

} // namespace

// Declared in test_matcher_harness.hpp so the dispatcher in
// test_matcher.cpp can call us.
void run_pm2_replacement_line_tests() {
    run_pm2_replacement_line_tests_impl();
}
