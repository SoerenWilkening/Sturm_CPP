// test_matcher_modular_op.cpp — sturm-qzab.1 / sturm-qzab.2 / sturm-qzab.3
// (P5.1 beat 5.1 + P5.2 beat 5.2 + P5.3 beat 5.3) unit tests for the
// modular-arithmetic AST matcher.
//
// Beat 5.1 covers the AddMod arm: the matcher must recognize the AST
// shape `qint_t<W> r = (a + b) % n;` (a `VarDecl` whose initializer is
// the qint-overloaded `operator%` whose LHS in turn is the
// qint-overloaded `operator+`), produce one `ModularOpHit` per match,
// and populate the operand / result names + width.
//
// Beat 5.2 mirrors that for `qint_t<W> r = (a * b) % n;` → MulMod.
// Same VarDecl anchor, but the inner CXXOperatorCallExpr is `operator*`
// instead of `operator+`. The matcher emits `ModularOpKind::MulMod` for
// these hits; `AddMod` patterns must remain unaffected (no
// double-firing, no false positives between the two arms).
//
// Beat 5.3 covers the compound peephole-collapsed form: the user writes
// the addition and the modular reduction as TWO adjacent statements
// (`qint r = a + b; r %= n;`). After `matcher_peephole_reorder` has had
// its chance, the modular-op matcher must still recognise the pair and
// surface a single `ModularOpHit` with `kind=AddMod` plus the `%=` call
// pointer in the new `mod_assign_call` slot. The hit's `result_var`
// points at the `r` VarDecl; `mod_assign_call` lets the consumer
// (a) extend the rewrite range to absorb both stmts and (b) suppress
// the LO-2a `LossyOpHit` the lossy matcher would otherwise emit for the
// `%=` call. Falls back to the wide path (no collapse hit emitted) when
// any statement intervenes between the VarDecl and the `%=` — including
// statements that read `r`.
//
// Negative coverage in this file:
//   - `qint_t<W> r = a % n;` (bare modulus, no inner +/*) does NOT match.
//   - `qint_t<W> r = (a - b) % n;` (inner `-`, not `+`/`*`) does NOT match.
//   - `qint_t<W> r = (a + b) * n;` (outer `*`, not `%`) does NOT match.
//   - `qint_t<W> r = a + b;` (no `%` at all) does NOT match.
//   - `(a + b) % n` / `(a * b) % n` on non-qint operands (NotQInt) do NOT
//     match.
//
// PowMod arm lands in beats 5.4-5.6 — it shares the matcher TU but
// each beat ships its own set of asserts. Beat 5.4 (sturm-qzab.4) is
// the negative-path beat: under the default `STURM_MODULAR_POW=OFF`
// build configuration the matcher MUST NOT register a PowMod arm, so a
// site shaped `qint r = sturm::pow(a, x) % n;` produces ZERO hits and
// the transpiler leaves the AST untouched (the lib-layer fallback to
// `lib_pow_dsl + lib_mod_dsl` is what runs at codegen). The flag-on
// rewrite ships in beat 5.5 (sturm-qzab.5).

#include "matcher_modular_op.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

// Hermetic stub of `qint_t<W>` carrying the operator overloads beat 5.1
// recognises (`+` and `%`) plus a `-` overload for the negative arm
// "inner-`-` should NOT match the AddMod pattern". Mirrors the stub
// shape used by `test_matcher_lossy_op.cpp` so the FixedCompilationDatabase
// path (no system include search) keeps working.
constexpr std::string_view kModularStub = R"CPP(
namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    qint_t& operator=(const qint_t&) { return *this; }
    qint_t& operator+=(const qint_t&) { return *this; }
    qint_t& operator%=(const qint_t&) { return *this; }
};
template <int W>
inline qint_t<W> operator+(const qint_t<W>&, const qint_t<W>&) {
    return qint_t<W>{};
}
template <int W>
inline qint_t<W> operator-(const qint_t<W>&, const qint_t<W>&) {
    return qint_t<W>{};
}
template <int W>
inline qint_t<W> operator*(const qint_t<W>&, const qint_t<W>&) {
    return qint_t<W>{};
}
template <int W>
inline qint_t<W> operator%(const qint_t<W>&, const qint_t<W>&) {
    return qint_t<W>{};
}
// Hermetic `pow` stubs (qint exponent + int64 exponent) for the
// sturm-qzab.4 negative-coverage beat. Mirrors the
// `tests/transpiler/fixtures/modular_pow_op_default.cpp` stub shape so
// the matcher sees the same `pow(qint, qint) % qint` AST shape it must
// LEAVE ALONE under `STURM_MODULAR_POW=OFF`.
template <int W>
inline qint_t<W> pow(const qint_t<W>&, const qint_t<W>&) {
    return qint_t<W>{};
}
template <int W>
inline qint_t<W> pow(const qint_t<W>&, long long) {
    return qint_t<W>{};
}
} // namespace sturm
using qint = sturm::qint_t<2>;
class NotQInt {
public:
    NotQInt() {}
    NotQInt(const NotQInt&) {}
    NotQInt& operator=(const NotQInt&) { return *this; }
};
inline NotQInt operator+(const NotQInt&, const NotQInt&) { return NotQInt{}; }
inline NotQInt operator*(const NotQInt&, const NotQInt&) { return NotQInt{}; }
inline NotQInt operator%(const NotQInt&, const NotQInt&) { return NotQInt{}; }
)CPP";

std::vector<ModularOpHit> run_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kModularStub.size() + user_src.size());
    code.append(kModularStub);
    code.append(user_src);

    std::vector<ModularOpHit> hits;
    clang::ast_matchers::MatchFinder finder;
    register_modular_op_matcher(finder, hits);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr, "FAIL  tool run returned false\n");
    }
    return hits;
}

int tests_run  = 0;
int tests_pass = 0;

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
        std::fprintf(stderr,                                          \
            "FAIL  %s:%d  strings differ\n  got=%s\n  want=%s\n",     \
            __FILE__, __LINE__,                                       \
            std::string(got).c_str(),                                 \
            std::string(want).c_str());                               \
    }                                                                 \
} while (0)

void test_basic_add_mod_match() {
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a + b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::AddMod);
    CHECK_EQ_STR(hits[0].result_name, std::string("r"));
    CHECK_EQ_STR(hits[0].a_name, std::string("a"));
    CHECK_EQ_STR(hits[0].b_name, std::string("b"));
    CHECK_EQ_STR(hits[0].n_name, std::string("n"));
    CHECK(hits[0].result_width == 2);
    CHECK(hits[0].mod_expr != nullptr);
    CHECK(hits[0].inner_op_expr != nullptr);
    CHECK(hits[0].result_var != nullptr);
    CHECK(hits[0].enclosing_block != nullptr);
}

void test_distinct_widths_resolve() {
    // sturm-czfi-style width resolution: qint_t<5> in the source must
    // surface as result_width=5. The matcher reads the LHS VarDecl's
    // type, which is `qint_t<5>` here.
    auto hits = run_matcher(
        "using qint5 = sturm::qint_t<5>;\n"
        "void demo(qint5 a, qint5 b, qint5 n) {\n"
        "    qint5 r = (a + b) % n;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::AddMod);
    CHECK(hits[0].result_width == 5);
}

// sturm-qzab.2 (P5.2 beat 5.2): MulMod arm — recognise the AST shape
// `qint_t<W> r = (a * b) % n;`. Same VarDecl anchor and outer `%` shape
// as AddMod; the only structural difference is the inner operator (`*`
// instead of `+`). The matcher must surface a `ModularOpHit` with
// `kind == MulMod` and the same operand-name / width slots populated.
void test_basic_mul_mod_match() {
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a * b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::MulMod);
    CHECK_EQ_STR(hits[0].result_name, std::string("r"));
    CHECK_EQ_STR(hits[0].a_name, std::string("a"));
    CHECK_EQ_STR(hits[0].b_name, std::string("b"));
    CHECK_EQ_STR(hits[0].n_name, std::string("n"));
    CHECK(hits[0].result_width == 2);
    CHECK(hits[0].mod_expr != nullptr);
    CHECK(hits[0].inner_op_expr != nullptr);
    CHECK(hits[0].result_var != nullptr);
    CHECK(hits[0].enclosing_block != nullptr);
}

void test_mul_mod_distinct_widths_resolve() {
    // Width resolution for the MulMod arm — same posture as the
    // AddMod sibling test, but on `(a * b) % n`.
    auto hits = run_matcher(
        "using qint5 = sturm::qint_t<5>;\n"
        "void demo(qint5 a, qint5 b, qint5 n) {\n"
        "    qint5 r = (a * b) % n;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::MulMod);
    CHECK(hits[0].result_width == 5);
}

void test_mul_mod_inner_sub_does_not_match_mulmod() {
    // (a - b) % n. The MulMod matcher recognises only `*` as the
    // inner op — `-` must not collapse into MulMod (subtraction-mod
    // is deferred per PRD §7).
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a - b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_mul_mod_outer_add_does_not_match_mulmod() {
    // (a * b) + n — outer `+`, not `%`. The MulMod matcher anchors on
    // the outer `%` so this AST shape is rejected.
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a * b) + n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_mul_mod_non_qint_operands_do_not_match() {
    // `(a * b) % n` on a non-qint user type — the matcher's
    // `cxxRecordDecl(hasName("qint_t"))` guard rejects this even though
    // the AST shape matches structurally.
    auto hits = run_matcher(
        "void demo(NotQInt a, NotQInt b, NotQInt n) {\n"
        "    NotQInt r = (a * b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_add_and_mul_mod_coexist() {
    // Both arms in the same TU must produce two distinct hits with the
    // correct discriminant. Confirms the AddMod / MulMod patterns do
    // not double-fire on the same site.
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint s = (a + b) % n;\n"
        "    qint p = (a * b) % n;\n"
        "    (void)s; (void)p;\n"
        "}\n");
    CHECK(hits.size() == 2);
    if (hits.size() != 2) return;
    // Order is matcher-traversal order. Assert the set of kinds
    // covers exactly {AddMod, MulMod} regardless of order so the test
    // is robust to MatchFinder ordering.
    bool saw_add = false;
    bool saw_mul = false;
    for (const auto& h : hits) {
        if (h.kind == ModularOpKind::AddMod) saw_add = true;
        if (h.kind == ModularOpKind::MulMod) saw_mul = true;
    }
    CHECK(saw_add);
    CHECK(saw_mul);
}

void test_mul_mod_enclosing_block_tracks_inner_scope() {
    // Inner-scope tracking for the MulMod arm — mirrors the AddMod
    // sibling test_enclosing_block_tracks_inner_scope.
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    {\n"
        "        qint r = (a * b) % n;\n"
        "        (void)r;\n"
        "    }\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::MulMod);
    CHECK(hits[0].enclosing_block != nullptr);
}

void test_bare_modulus_does_not_match() {
    // No inner `+`; this is a plain modulus and must not collapse into
    // an AddMod rewrite.
    auto hits = run_matcher(
        "void demo(qint a, qint n) {\n"
        "    qint r = a % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_inner_sub_does_not_match_addmod() {
    // (a - b) % n. The matcher recognises only `+` as the inner op
    // for AddMod (subtraction-mod is deferred per PRD §7).
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a - b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_outer_mul_does_not_match_addmod() {
    // (a + b) * n — outer `*`, not `%`. The matcher anchors on the
    // outer `%` so this AST shape is rejected.
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a + b) * n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_no_modulus_does_not_match() {
    auto hits = run_matcher(
        "void demo(qint a, qint b) {\n"
        "    qint r = a + b;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_non_qint_operands_do_not_match() {
    // `(a + b) % n` on a non-qint user type — the matcher's
    // `cxxRecordDecl(hasName("qint_t"))` guard rejects this even though
    // the AST shape matches structurally.
    auto hits = run_matcher(
        "void demo(NotQInt a, NotQInt b, NotQInt n) {\n"
        "    NotQInt r = (a + b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_enclosing_block_tracks_inner_scope() {
    // The hit's enclosing_block should be the inner `{ ... }` scope,
    // not the function body — mirrors the LO-2a test of the same name.
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    {\n"
        "        qint r = (a + b) % n;\n"
        "        (void)r;\n"
        "    }\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].enclosing_block != nullptr);
}

// ── sturm-qzab.3 (P5 beat 5.3): compound peephole-collapsed AddMod ──────────
//
// `qint r = a + b; r %= n;` (two adjacent stmts) must surface ONE
// ModularOpHit with kind=AddMod and `mod_assign_call` populated. The
// `result_var` slot points at the `r` VarDecl; the `inner_op_expr`
// slot points at the `+` op-call (so the existing AST anchors stay
// usable for diagnostics); the new `mod_assign_call` slot points at
// the `%=` CXXOperatorCallExpr (so the consumer drain can extend the
// QReplacement range and suppress the LO-2a hit on the same call).
void test_compound_collapse_basic_match() {
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = a + b;\n"
        "    r %= n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::AddMod);
    CHECK_EQ_STR(hits[0].result_name, std::string("r"));
    CHECK_EQ_STR(hits[0].a_name, std::string("a"));
    CHECK_EQ_STR(hits[0].b_name, std::string("b"));
    CHECK_EQ_STR(hits[0].n_name, std::string("n"));
    CHECK(hits[0].result_width == 2);
    CHECK(hits[0].inner_op_expr != nullptr);
    CHECK(hits[0].result_var != nullptr);
    CHECK(hits[0].enclosing_block != nullptr);
    // The compound-collapse arm leaves `mod_expr` null (no outer
    // `%` op-call exists in this AST shape) and populates the new
    // `mod_assign_call` slot with the `%=` CXXOperatorCallExpr.
    CHECK(hits[0].mod_expr == nullptr);
    CHECK(hits[0].mod_assign_call != nullptr);
}

void test_compound_collapse_distinct_widths_resolve() {
    auto hits = run_matcher(
        "using qint5 = sturm::qint_t<5>;\n"
        "void demo(qint5 a, qint5 b, qint5 n) {\n"
        "    qint5 r = a + b;\n"
        "    r %= n;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::AddMod);
    CHECK(hits[0].result_width == 5);
    CHECK(hits[0].mod_assign_call != nullptr);
}

// `qint r = a + b;` alone (no `%=` follow-up) must NOT collapse — the
// matcher's compound-collapse arm requires the adjacent `r %= n;` stmt.
// The decl on its own falls through to no rewrite (the user explicitly
// asked for a non-modular addition).
void test_compound_collapse_no_mod_assign_does_not_match() {
    auto hits = run_matcher(
        "void demo(qint a, qint b) {\n"
        "    qint r = a + b;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

// Adjacency requirement: any intervening stmt (even a no-op like
// `(void)r;`) breaks the collapse — the matcher walks the IMMEDIATE
// next sibling only and bails when that sibling is not the `r %= n;`
// op-call. With the collapse arm declining, the wide path takes over:
// the user's `qint r = a + b;` decl lands verbatim and the LO-2a `%=`
// matcher emits the standard divide-kernel desugar for the `%=` stmt.
// (This test only asserts that the COLLAPSE matcher emits no hit; the
// LO-2a / wide-path behaviour is exercised by its own snapshot
// fixtures.)
void test_compound_collapse_intervening_stmt_does_not_collapse() {
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = a + b;\n"
        "    (void)r;\n"
        "    r %= n;\n"
        "}\n");
    CHECK(hits.empty());
}

// `r += c;` between the decl and the `%=` is the canonical "intervening
// read of r" case the plan §7.2 #2 wide-path fallback discussion calls
// out — `r` is read on the `+=`'s LHS so collapsing `add_mod(r, n)`
// from the original `(a+b) % n` would be semantically wrong (the user
// wanted `((a+b) + c) % n`, not `(a+b) % n` followed by an unrelated
// `r += c`). Matcher must bail.
void test_compound_collapse_intervening_read_of_r_does_not_collapse() {
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint c, qint n) {\n"
        "    qint r = a + b;\n"
        "    r += c;\n"
        "    r %= n;\n"
        "}\n");
    CHECK(hits.empty());
}

// `r %= n;` whose LHS is NOT the just-declared VarDecl must not
// collapse — the matcher's per-pair guard checks decl_loc equality so
// the `r %= n;` referring to a SHADOWING / OUTER `r` falls through to
// the wide path.
void test_compound_collapse_mismatched_lhs_does_not_collapse() {
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n, qint other) {\n"
        "    qint r = a + b;\n"
        "    other %= n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

// `qint r = a - b;` then `r %= n;` — inner `-` op rejects (subtraction-
// mod is deferred per PRD §7). Mirrors the in-initializer
// `test_inner_sub_does_not_match_addmod` posture.
void test_compound_collapse_inner_sub_does_not_match() {
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = a - b;\n"
        "    r %= n;\n"
        "}\n");
    CHECK(hits.empty());
}

// Non-qint operands: even with the right AST shape, the
// `cxxRecordDecl(hasName(\"qint_t\"))` guard rejects non-qint user types.
void test_compound_collapse_non_qint_operands_do_not_match() {
    auto hits = run_matcher(
        "namespace nq {\n"
        "class T { public: T(){} T(const T&){} T& operator=(const T&){return *this;}\n"
        "          T& operator%=(const T&){return *this;} };\n"
        "inline T operator+(const T&, const T&) { return T{}; }\n"
        "}\n"
        "void demo(nq::T a, nq::T b, nq::T n) {\n"
        "    nq::T r = a + b;\n"
        "    r %= n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

// Coexistence with the in-initializer AddMod arm: a TU containing both
// `(a+b) % n` (in-initializer, beat 5.1 hit) AND `s = a+b; s %= n;`
// (compound collapse, beat 5.3 hit) must produce TWO AddMod hits — one
// per site, no double-firing.
void test_compound_collapse_coexists_with_in_init_form() {
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a + b) % n;\n"
        "    qint s = a + b;\n"
        "    s %= n;\n"
        "    (void)r; (void)s;\n"
        "}\n");
    CHECK(hits.size() == 2);
    if (hits.size() != 2) return;
    int collapse_seen = 0;
    int in_init_seen = 0;
    for (const auto& h : hits) {
        if (h.kind != ModularOpKind::AddMod) continue;
        if (h.mod_assign_call != nullptr) ++collapse_seen;
        if (h.mod_expr != nullptr) ++in_init_seen;
    }
    CHECK(collapse_seen == 1);
    CHECK(in_init_seen == 1);
}

#ifndef STURM_TEST_MODULAR_POW_ON
// ── sturm-qzab.4 (P5 beat 5.4): PowMod negative path (flag OFF) ─────────────
//
// Plan §7.4 / PRD §3.4: under the default `STURM_MODULAR_POW=OFF`
// build configuration the modular-op matcher MUST NOT recognise the
// `pow(a, x) % n` AST shape — neither the qint-qint exponent form nor
// the qint-int64 exponent form fires a PowMod hit. With no PowMod arm
// registered, the existing AddMod / MulMod arms also reject the shape
// (no inner `+`/`*` op-call between operator overloads), so the entire
// declaration round-trips through the transpiler untouched and falls
// back to the lib-layer `lib_pow_dsl + lib_mod_dsl` lowering at codegen.
// Beat 5.5 (sturm-qzab.5) lands the flag-on PowMod arm; this beat pins
// the OFF-mode contract that the flag-on diff is measured against.
//
// The matcher TU is built without `STURM_MODULAR_POW` defined for this
// test executable (the define is propagated only to the `sturm-transpile`
// driver target — see `transpiler/CMakeLists.txt` line ~264). Asserting
// `hits.empty()` here therefore directly exercises the OFF-mode branch.
void test_pow_mod_qint_exponent_does_not_match_under_flag_off() {
    auto hits = run_matcher(
        "void demo(qint a, qint x, qint n) {\n"
        "    qint r = sturm::pow(a, x) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_pow_mod_int_exponent_does_not_match_under_flag_off() {
    // The matched-fixture variant uses an int64 exponent — the
    // `qint_t<W> pow(qint_t<W>, long long)` overload. Same OFF-mode
    // contract: matcher emits no PowMod hit, the `pow(...) % n` site
    // falls back to the lib_pow_dsl + lib_mod_dsl lowering.
    auto hits = run_matcher(
        "void demo(qint a, qint n) {\n"
        "    qint r = sturm::pow(a, 3LL) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_pow_mod_does_not_collide_with_addmod_mulmod_under_flag_off() {
    // A TU mixing AddMod + MulMod (rewritten under both flag modes) and
    // PowMod (rewritten only under flag ON) must, under flag OFF,
    // produce exactly TWO hits — one per binary-op site — with no
    // spurious PowMod hit on the `pow(...) % n` site. Confirms the
    // OFF-mode PowMod branch does not bleed into the always-on arms.
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint x, qint n) {\n"
        "    qint s = (a + b) % n;\n"
        "    qint p = (a * b) % n;\n"
        "    qint q = sturm::pow(a, x) % n;\n"
        "    (void)s; (void)p; (void)q;\n"
        "}\n");
    CHECK(hits.size() == 2);
    if (hits.size() != 2) return;
    bool saw_add = false;
    bool saw_mul = false;
    bool saw_pow = false;
    for (const auto& h : hits) {
        if (h.kind == ModularOpKind::AddMod) saw_add = true;
        if (h.kind == ModularOpKind::MulMod) saw_mul = true;
        if (h.kind == ModularOpKind::PowMod) saw_pow = true;
    }
    CHECK(saw_add);
    CHECK(saw_mul);
    CHECK(!saw_pow);
}
#endif // !STURM_TEST_MODULAR_POW_ON

// ── sturm-qzab.5 (P5 beat 5.5): PowMod positive path (flag ON) ───────────────
//
// Plan §7.4 / PRD §3.4: when `STURM_MODULAR_POW=ON` the matcher MUST
// recognise the AST shape `qint_t<W> r = sturm::pow(a, x) % n;` (a
// VarDecl whose initializer is the qint-overloaded `operator%` whose
// LHS in turn is a `CallExpr` to a function template named `pow`) and
// surface ONE `ModularOpHit` with `kind == PowMod`. The operand-name
// slots populate from the bound DeclRefExpr nodes inside the `pow`
// call's arguments and the outer `%` second operand; the result-width
// resolves off the result VarDecl's `qint_t<W>` type the same way the
// AddMod / MulMod arms do; the `pow_call` AST anchor populates so the
// emitter can locate the `pow(...)` source range if needed.
//
// These tests compile only when the matcher TU itself is compiled with
// `-DSTURM_MODULAR_POW`. The test executable's CMake target opts in
// when the top-level `STURM_MODULAR_POW=ON` configure runs (see the
// `if(STURM_MODULAR_POW)` block in `transpiler/tests/CMakeLists.txt`),
// so under the default OFF configure these tests are absent — the
// negative-coverage suite above pins the OFF-mode contract.
#ifdef STURM_TEST_MODULAR_POW_ON
void test_pow_mod_basic_match_under_flag_on() {
    auto hits = run_matcher(
        "void demo(qint a, qint x, qint n) {\n"
        "    qint r = sturm::pow(a, x) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::PowMod);
    CHECK_EQ_STR(hits[0].result_name, std::string("r"));
    CHECK_EQ_STR(hits[0].a_name, std::string("a"));
    CHECK_EQ_STR(hits[0].b_name, std::string("x"));
    CHECK_EQ_STR(hits[0].n_name, std::string("n"));
    CHECK(hits[0].result_width == 2);
    CHECK(hits[0].mod_expr != nullptr);
    CHECK(hits[0].pow_call != nullptr);
    CHECK(hits[0].result_var != nullptr);
    CHECK(hits[0].enclosing_block != nullptr);
}

void test_pow_mod_distinct_widths_resolve_under_flag_on() {
    // Width resolution mirrors the AddMod / MulMod siblings: qint_t<5>
    // in the source must surface as result_width=5.
    auto hits = run_matcher(
        "using qint5 = sturm::qint_t<5>;\n"
        "void demo(qint5 a, qint5 x, qint5 n) {\n"
        "    qint5 r = sturm::pow(a, x) % n;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::PowMod);
    CHECK(hits[0].result_width == 5);
}

void test_pow_mod_outer_mul_does_not_match_under_flag_on() {
    // `sturm::pow(a, x) * n` has outer `*`, not `%`. The PowMod arm
    // anchors on outer `%` so this AST shape is rejected even under
    // the flag-on configure.
    auto hits = run_matcher(
        "void demo(qint a, qint x, qint n) {\n"
        "    qint r = sturm::pow(a, x) * n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_pow_mod_non_pow_call_does_not_match_under_flag_on() {
    // A non-`pow` callee with the same signature must not collapse
    // into a PowMod rewrite. The matcher gates on the function name.
    auto hits = run_matcher(
        "namespace sturm {\n"
        "template <int W>\n"
        "inline qint_t<W> notpow(const qint_t<W>&, const qint_t<W>&) {\n"
        "    return qint_t<W>{};\n"
        "}\n"
        "}\n"
        "void demo(qint a, qint x, qint n) {\n"
        "    qint r = sturm::notpow(a, x) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_pow_mod_coexists_with_addmod_mulmod_under_flag_on() {
    // Mixed TU under flag ON: AddMod + MulMod (always-on) plus PowMod
    // (now firing). Expect three distinct hits, one per kind.
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint x, qint n) {\n"
        "    qint s = (a + b) % n;\n"
        "    qint p = (a * b) % n;\n"
        "    qint q = sturm::pow(a, x) % n;\n"
        "    (void)s; (void)p; (void)q;\n"
        "}\n");
    CHECK(hits.size() == 3);
    if (hits.size() != 3) return;
    bool saw_add = false;
    bool saw_mul = false;
    bool saw_pow = false;
    for (const auto& h : hits) {
        if (h.kind == ModularOpKind::AddMod) saw_add = true;
        if (h.kind == ModularOpKind::MulMod) saw_mul = true;
        if (h.kind == ModularOpKind::PowMod) saw_pow = true;
    }
    CHECK(saw_add);
    CHECK(saw_mul);
    CHECK(saw_pow);
}

void test_pow_mod_non_qint_operands_do_not_match_under_flag_on() {
    // `pow(a, x) % n` with non-qint operands must be rejected by the
    // PowMod arm even under the flag-on configure (the matcher's
    // qint-type guard rejects the structural match).
    auto hits = run_matcher(
        "namespace nq {\n"
        "inline NotQInt pow(const NotQInt&, const NotQInt&) {\n"
        "    return NotQInt{};\n"
        "}\n"
        "}\n"
        "void demo(NotQInt a, NotQInt x, NotQInt n) {\n"
        "    NotQInt r = nq::pow(a, x) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.empty());
}

void test_pow_mod_enclosing_block_tracks_inner_scope_under_flag_on() {
    // Inner-scope tracking for the PowMod arm — mirrors the
    // AddMod / MulMod siblings.
    auto hits = run_matcher(
        "void demo(qint a, qint x, qint n) {\n"
        "    {\n"
        "        qint r = sturm::pow(a, x) % n;\n"
        "        (void)r;\n"
        "    }\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::PowMod);
    CHECK(hits[0].enclosing_block != nullptr);
}
#endif // STURM_TEST_MODULAR_POW_ON
// Note: the flag-on test guards use `STURM_TEST_MODULAR_POW_ON` (a
// test-only macro distinct from the production `STURM_MODULAR_POW`
// flag) so the `cmake_modular_pow_flag` ctest's `compile_commands.json`
// scan — which rejects any `-DSTURM_MODULAR_POW` outside
// `transpiler/src/` — does not false-positive on this test TU. The
// matcher TU itself receives `-DSTURM_MODULAR_POW` via
// `set_source_files_properties` in the test target; that file lives
// under `transpiler/src/` and is therefore allowed by the scanner's
// driver-bucket rule.

} // namespace

int main() {
    test_basic_add_mod_match();
    test_distinct_widths_resolve();
    test_bare_modulus_does_not_match();
    test_inner_sub_does_not_match_addmod();
    test_outer_mul_does_not_match_addmod();
    test_no_modulus_does_not_match();
    test_non_qint_operands_do_not_match();
    test_enclosing_block_tracks_inner_scope();
    test_basic_mul_mod_match();
    test_mul_mod_distinct_widths_resolve();
    test_mul_mod_inner_sub_does_not_match_mulmod();
    test_mul_mod_outer_add_does_not_match_mulmod();
    test_mul_mod_non_qint_operands_do_not_match();
    test_add_and_mul_mod_coexist();
    test_mul_mod_enclosing_block_tracks_inner_scope();
    test_compound_collapse_basic_match();
    test_compound_collapse_distinct_widths_resolve();
    test_compound_collapse_no_mod_assign_does_not_match();
    test_compound_collapse_intervening_stmt_does_not_collapse();
    test_compound_collapse_intervening_read_of_r_does_not_collapse();
    test_compound_collapse_mismatched_lhs_does_not_collapse();
    test_compound_collapse_inner_sub_does_not_match();
    test_compound_collapse_non_qint_operands_do_not_match();
    test_compound_collapse_coexists_with_in_init_form();
#ifndef STURM_TEST_MODULAR_POW_ON
    test_pow_mod_qint_exponent_does_not_match_under_flag_off();
    test_pow_mod_int_exponent_does_not_match_under_flag_off();
    test_pow_mod_does_not_collide_with_addmod_mulmod_under_flag_off();
#endif
#ifdef STURM_TEST_MODULAR_POW_ON
    test_pow_mod_basic_match_under_flag_on();
    test_pow_mod_distinct_widths_resolve_under_flag_on();
    test_pow_mod_outer_mul_does_not_match_under_flag_on();
    test_pow_mod_non_pow_call_does_not_match_under_flag_on();
    test_pow_mod_coexists_with_addmod_mulmod_under_flag_on();
    test_pow_mod_non_qint_operands_do_not_match_under_flag_on();
    test_pow_mod_enclosing_block_tracks_inner_scope_under_flag_on();
#endif
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
