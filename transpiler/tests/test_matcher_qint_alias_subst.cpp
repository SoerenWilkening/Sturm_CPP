// test_matcher_qint_alias_subst.cpp — sturm-65rs.8 (Beat C1) unit tests.
// Plan §9, PRD §4.3. See `matcher_qint_alias_subst.hpp` for the matcher
// contract. Test surface: five positives (one per anchor — VarDecl,
// ParmVarDecl, FieldDecl, function return type, CXXFunctionalCastExpr),
// each asserting exactly one hit with the correct TypeLoc range.
// Negatives: TypeAliasDecl rebinding to backend `qint_t<W>` MUST NOT
// match; backend `sturm::qint_t<32>` MUST NOT match; macro-expansion
// site behaviour pinned explicitly. LoC budget: <= 300.
//
// Why this file shape: the matcher (matcher-only beat) emits a typed
// `Match` struct with `MatchKind` (vd / pmd / fd / fn / cast) and a
// captured `clang::SourceRange` for the TypeLoc; the test asserts the
// kind, count, and that the captured range is non-invalid (the emitter
// in C2 will validate the rendered text against the range; here we
// only pin the matcher's binding contract).

#include "matcher_qint_alias_subst.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

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

#define CHECK_EQ_INT(got, want) do {                                  \
    ++tests_run;                                                      \
    const long long g = static_cast<long long>(got);                  \
    const long long w = static_cast<long long>(want);                 \
    if (g == w) { ++tests_pass; }                                     \
    else {                                                            \
        std::fprintf(stderr, "FAIL  %s:%d  got=%lld want=%lld\n",     \
                     __FILE__, __LINE__, g, w);                       \
    }                                                                 \
} while (0)

namespace {

// Hermetic stub: `sturm::frontend::qint` (the alias class anchored
// by the matcher) plus `sturm::qint_t<W>` (backend, used as the
// negative case for the discriminator). Mirrors the production
// surface enough to type-check the fixtures below — nothing more.
constexpr std::string_view kStub = R"CPP(
namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
};
namespace frontend {
class qint {
public:
    qint() noexcept = default;
    qint(long long v) noexcept : value_(v) {}
    qint(const qint&) noexcept = default;
    qint& operator=(const qint&) noexcept = default;
    operator unsigned long() const noexcept {
        return static_cast<unsigned long>(value_);
    }
private:
    long long value_ = 0;
};
} // namespace frontend
} // namespace sturm
)CPP";

std::vector<QintAliasSubstMatch> run_on_code(std::string_view code,
                                              std::string_view filename) {
    std::vector<QintAliasSubstMatch> matches;
    clang::ast_matchers::MatchFinder finder;
    register_qint_alias_subst_matcher(finder, matches);
    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), std::string(code), args, std::string(filename));
    if (!ok) std::fprintf(stderr, "FAIL  tool run on %.*s\n",
                          (int)filename.size(), filename.data());
    return matches;
}

std::vector<QintAliasSubstMatch> run_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kStub.size() + user_src.size());
    code.append(kStub);
    code.append(user_src);
    return run_on_code(code, "qint_alias_subst_input.cpp");
}

// Count matches by kind. Each anchor MUST fire exactly once per
// fixture (PRD A4 partial: counter matches expected hit count).
int count_kind(const std::vector<QintAliasSubstMatch>& ms,
               QintAliasSubstKind k) {
    int n = 0;
    for (const auto& m : ms) if (m.kind == k) ++n;
    return n;
}

bool any_invalid_range(const std::vector<QintAliasSubstMatch>& ms) {
    for (const auto& m : ms) if (m.type_range.isInvalid()) return true;
    return false;
}

} // anonymous namespace

// ── Positive: VarDecl `sturm::frontend::qint x;` ───────────────────────────
static void test_var_decl_positive() {
    const auto matches = run_matcher(
        "void demo() {\n"
        "    sturm::frontend::qint x;\n"
        "    (void)x;\n"
        "}\n");
    CHECK_EQ_INT(matches.size(), 1);
    CHECK_EQ_INT(count_kind(matches, QintAliasSubstKind::VarDecl), 1);
    CHECK(!any_invalid_range(matches));
}

// ── Positive: ParmVarDecl `void demo(sturm::frontend::qint p)` ─────────────
static void test_parm_var_decl_positive() {
    const auto matches = run_matcher(
        "void demo(sturm::frontend::qint p) {\n"
        "    (void)p;\n"
        "}\n");
    CHECK_EQ_INT(matches.size(), 1);
    CHECK_EQ_INT(count_kind(matches, QintAliasSubstKind::ParmVarDecl), 1);
    CHECK(!any_invalid_range(matches));
}

// ── Positive: FieldDecl on a class with a `frontend::qint` member ──────────
static void test_field_decl_positive() {
    const auto matches = run_matcher(
        "struct S {\n"
        "    sturm::frontend::qint f;\n"
        "};\n"
        "void demo() { S s; (void)s; }\n");
    CHECK_EQ_INT(matches.size(), 1);
    CHECK_EQ_INT(count_kind(matches, QintAliasSubstKind::FieldDecl), 1);
    CHECK(!any_invalid_range(matches));
}

// ── Positive: function return type `sturm::frontend::qint demo()` ─────────
// Single FunctionDecl (no forward declaration) so we can assert
// EXACTLY ONE hit per the C1 contract.
static void test_function_return_positive() {
    const auto matches = run_matcher(
        "sturm::frontend::qint demo() { return {}; }\n");
    CHECK_EQ_INT(matches.size(), 1);
    CHECK_EQ_INT(count_kind(matches, QintAliasSubstKind::FunctionDecl), 1);
    CHECK(!any_invalid_range(matches));
}

// ── Positive: CXXFunctionalCastExpr `sturm::frontend::qint(0)` ─────────────
// Use `(void)` to consume the temporary directly so there is no
// VarDecl that would also fire the `vd` arm. This isolates the
// functional-cast anchor: exactly one hit.
static void test_functional_cast_positive() {
    const auto matches = run_matcher(
        "void demo() {\n"
        "    (void)sturm::frontend::qint(0);\n"
        "}\n");
    CHECK_EQ_INT(matches.size(), 1);
    CHECK_EQ_INT(count_kind(matches, QintAliasSubstKind::FunctionalCast), 1);
    CHECK(!any_invalid_range(matches));
}

// ── Negative: TypeAliasDecl rebinding to backend qint_t<8> ─────────────────
static void test_alias_decl_no_match() {
    const auto matches = run_matcher(
        "using qint = sturm::qint_t<8>;\n"
        "void demo() { qint x; (void)x; }\n");
    // The `using qint = sturm::qint_t<8>;` line is a TypeAliasDecl —
    // not a CXXRecordDecl named `qint` in `sturm::frontend`. The
    // VarDecl `qint x;` resolves to the backend type. Either way, the
    // matcher MUST NOT fire.
    CHECK_EQ_INT(matches.size(), 0);
}

// ── Negative: backend `sturm::qint_t<32> x;` MUST NOT match ────────────────
static void test_backend_qint_t_no_match() {
    const auto matches = run_matcher(
        "void demo() {\n"
        "    sturm::qint_t<32> x;\n"
        "    (void)x;\n"
        "}\n");
    CHECK_EQ_INT(matches.size(), 0);
}

// ── Negative: macro-expansion site (pinned: matcher fires) ─────────────────
// Pin the behaviour explicitly. The matcher is anchored on AST nodes
// whose underlying type resolves to `sturm::frontend::qint` regardless
// of whether the source spelling came from a macro expansion. We do
// NOT filter on `isMacroBodyExpansion()` here — the rewriter (C2) is
// responsible for deciding whether to skip macro-sourced ranges. So
// this fixture asserts the matcher DOES fire (one VarDecl). This
// keeps the matcher anchor-only (no source-location filtering) and
// defers macro-expansion policy to the emitter.
static void test_macro_expansion_pinned() {
    const auto matches = run_matcher(
        "#define DECL_Q sturm::frontend::qint x{};\n"
        "void demo() { DECL_Q (void)x; }\n");
    CHECK_EQ_INT(count_kind(matches, QintAliasSubstKind::VarDecl), 1);
}

// ── Wave-2 (sturm-7t85.1) helper: fetch a match's text via tooling ─────────
// Re-runs the tool, but this time captures the source text of the
// captured TypeLoc range so the assertions can pin the range to the
// `qint` token (carrier punctuation must NOT be inside the range).
//
// Returns the rendered text of the *first* match's `type_range`; empty
// string when no match fires. We only use this on fixtures that fire
// exactly one match, so first-match-text is well-defined.
namespace {
struct CaptureCallback : public clang::ast_matchers::MatchFinder::MatchCallback {
    std::string captured;
    const char* bind = nullptr;
    explicit CaptureCallback(const char* b) : bind(b) {}
    void run(const clang::ast_matchers::MatchFinder::MatchResult& r) override {
        clang::SourceRange sr;
        if (const auto* d = r.Nodes.getNodeAs<clang::DeclaratorDecl>(bind)) {
            const clang::TypeSourceInfo* tsi = d->getTypeSourceInfo();
            if (!tsi) return;
            // Mirror the production carrier walk: peel sugar
            // (ElaboratedTypeLoc / ParenTypeLoc) for SHAPE inspection
            // only, but return the *un-peeled* element/pointee range
            // so any namespace prefix on the element type stays inside
            // the captured span (matches `declarator_typeloc_range` in
            // qint_alias_carrier_walk.cpp).
            auto peel = [](clang::TypeLoc tl) {
                for (;;) {
                    tl = tl.getUnqualifiedLoc();
                    if (auto e = tl.getAs<clang::ElaboratedTypeLoc>()) {
                        tl = e.getNamedTypeLoc(); continue;
                    }
                    if (auto p = tl.getAs<clang::ParenTypeLoc>()) {
                        tl = p.getInnerLoc(); continue;
                    }
                    return tl;
                }
            };
            clang::TypeLoc outer = tsi->getTypeLoc();
            clang::TypeLoc probe = peel(outer);
            if (auto atl = probe.getAs<clang::ArrayTypeLoc>()) {
                sr = atl.getElementLoc().getSourceRange();
            } else if (auto ptl = probe.getAs<clang::PointerTypeLoc>()) {
                sr = ptl.getPointeeLoc().getSourceRange();
            } else {
                sr = outer.getSourceRange();
            }
        }
        if (sr.isInvalid()) return;
        const auto& sm = *r.SourceManager;
        const clang::LangOptions& lo = r.Context->getLangOpts();
        clang::SourceLocation b = sm.getSpellingLoc(sr.getBegin());
        clang::SourceLocation e = sm.getSpellingLoc(sr.getEnd());
        clang::SourceLocation eend = clang::Lexer::getLocForEndOfToken(
            e, 0, sm, lo);
        if (b.isInvalid() || eend.isInvalid()) return;
        const char* bp = sm.getCharacterData(b);
        const char* ep = sm.getCharacterData(eend);
        if (!bp || !ep || ep < bp) return;
        captured.assign(bp, ep - bp);
    }
};
} // anonymous namespace

// Helper: captures the source text of the first match's type-range,
// using the same TypeLoc walk the production matcher applies. We
// route through a sibling matcher rather than QintAliasSubstMatch
// because the latter does not expose ASTContext / SourceManager.
static std::string capture_typeloc_text(std::string_view user_src,
                                         const char* anchor /* "vd","pmd","fd" */) {
    using namespace clang::ast_matchers;
    std::string code;
    code.reserve(kStub.size() + user_src.size());
    code.append(kStub);
    code.append(user_src);

    MatchFinder finder;
    CaptureCallback cb(anchor);
    auto qint_record =
        cxxRecordDecl(hasName("qint"),
                      hasParent(namespaceDecl(hasName("frontend"))));
    auto carrier = qualType(anyOf(
        hasCanonicalType(hasDeclaration(qint_record)),
        hasCanonicalType(arrayType(hasElementType(
            hasDeclaration(qint_record)))),
        hasCanonicalType(pointerType(pointee(
            hasDeclaration(qint_record))))));
    if (std::string_view(anchor) == "vd") {
        finder.addMatcher(varDecl(unless(parmVarDecl()),
                                   hasType(carrier)).bind(anchor), &cb);
    } else if (std::string_view(anchor) == "pmd") {
        finder.addMatcher(parmVarDecl(hasType(carrier)).bind(anchor), &cb);
    } else if (std::string_view(anchor) == "fd") {
        finder.addMatcher(fieldDecl(hasType(carrier)).bind(anchor), &cb);
    }
    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    (void)clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "qint_alias_subst_capture.cpp");
    return cb.captured;
}

// ── Wave-2 (sturm-7t85.1) positives: array carrier ─────────────────────────

// VarDecl `qint a[4];` → 1 hit, range = `qint` token only (NOT `[4]`).
static void test_var_decl_carray_positive() {
    const auto matches = run_matcher(
        "void demo() {\n"
        "    sturm::frontend::qint a[4];\n"
        "    (void)a;\n"
        "}\n");
    CHECK_EQ_INT(matches.size(), 1);
    CHECK_EQ_INT(count_kind(matches, QintAliasSubstKind::VarDecl), 1);
    CHECK(!any_invalid_range(matches));
    const std::string text = capture_typeloc_text(
        "void demo() { sturm::frontend::qint a[4]; (void)a; }\n", "vd");
    CHECK_EQ_INT(text.find('['), std::string::npos);
    CHECK(text.find("qint") != std::string::npos);
}

// ParmVarDecl `void f(qint b[]);` → 1 hit, element span only.
static void test_parm_carray_positive() {
    const auto matches = run_matcher(
        "void f(sturm::frontend::qint b[]);\n");
    CHECK_EQ_INT(matches.size(), 1);
    CHECK_EQ_INT(count_kind(matches, QintAliasSubstKind::ParmVarDecl), 1);
    CHECK(!any_invalid_range(matches));
    const std::string text = capture_typeloc_text(
        "void f(sturm::frontend::qint b[]);\n", "pmd");
    CHECK_EQ_INT(text.find('['), std::string::npos);
    CHECK(text.find("qint") != std::string::npos);
}

// FieldDecl `struct S { qint c[3]; };` → 1 hit.
static void test_field_carray_positive() {
    const auto matches = run_matcher(
        "struct S { sturm::frontend::qint c[3]; };\n"
        "void demo() { S s; (void)s; }\n");
    CHECK_EQ_INT(matches.size(), 1);
    CHECK_EQ_INT(count_kind(matches, QintAliasSubstKind::FieldDecl), 1);
    CHECK(!any_invalid_range(matches));
    const std::string text = capture_typeloc_text(
        "struct S { sturm::frontend::qint c[3]; };\n"
        "void demo() { S s; (void)s; }\n", "fd");
    CHECK_EQ_INT(text.find('['), std::string::npos);
    CHECK(text.find("qint") != std::string::npos);
}

// ── Wave-2 (sturm-7t85.1) positives: pointer carrier ───────────────────────

// VarDecl `qint* p;` → 1 hit, range = `qint` token only (NOT `*`).
static void test_var_decl_ptr_positive() {
    const auto matches = run_matcher(
        "void demo() {\n"
        "    sturm::frontend::qint* p = nullptr;\n"
        "    (void)p;\n"
        "}\n");
    CHECK_EQ_INT(matches.size(), 1);
    CHECK_EQ_INT(count_kind(matches, QintAliasSubstKind::VarDecl), 1);
    CHECK(!any_invalid_range(matches));
    const std::string text = capture_typeloc_text(
        "void demo() { sturm::frontend::qint* p = nullptr; (void)p; }\n",
        "vd");
    CHECK_EQ_INT(text.find('*'), std::string::npos);
    CHECK(text.find("qint") != std::string::npos);
}

// ParmVarDecl `void g(qint* q);` → 1 hit.
static void test_parm_ptr_positive() {
    const auto matches = run_matcher(
        "void g(sturm::frontend::qint* q);\n");
    CHECK_EQ_INT(matches.size(), 1);
    CHECK_EQ_INT(count_kind(matches, QintAliasSubstKind::ParmVarDecl), 1);
    CHECK(!any_invalid_range(matches));
    const std::string text = capture_typeloc_text(
        "void g(sturm::frontend::qint* q);\n", "pmd");
    CHECK_EQ_INT(text.find('*'), std::string::npos);
    CHECK(text.find("qint") != std::string::npos);
}

// FieldDecl `struct T { qint* d; };` → 1 hit.
static void test_field_ptr_positive() {
    const auto matches = run_matcher(
        "struct T { sturm::frontend::qint* d; };\n"
        "void demo() { T t; (void)t; }\n");
    CHECK_EQ_INT(matches.size(), 1);
    CHECK_EQ_INT(count_kind(matches, QintAliasSubstKind::FieldDecl), 1);
    CHECK(!any_invalid_range(matches));
    const std::string text = capture_typeloc_text(
        "struct T { sturm::frontend::qint* d; };\n"
        "void demo() { T t; (void)t; }\n", "fd");
    CHECK_EQ_INT(text.find('*'), std::string::npos);
    CHECK(text.find("qint") != std::string::npos);
}

// ── Wave-2 (sturm-7t85.1) negatives: out-of-scope shapes (R6) ──────────────

// Multi-dim arrays `qint a[N][M]` → 0 hits. v1 walks one carrier
// level only; multi-level is sturm-7t85.6 follow-up.
static void test_var_decl_multidim_no_match() {
    const auto matches = run_matcher(
        "void demo() {\n"
        "    sturm::frontend::qint a[2][3];\n"
        "    (void)a;\n"
        "}\n");
    CHECK_EQ_INT(matches.size(), 0);
}

// Reference-to-array `qint (&r)[N]` → 0 hits (R6 out of scope in v1).
static void test_var_decl_ref_to_array_no_match() {
    const auto matches = run_matcher(
        "void demo() {\n"
        "    sturm::frontend::qint base[4];\n"
        "    sturm::frontend::qint (&r)[4] = base;\n"
        "    (void)r;\n"
        "}\n");
    // The base `qint base[4];` VarDecl fires (positive carrier walk),
    // but the reference-to-array binding MUST NOT add a second hit.
    CHECK_EQ_INT(count_kind(matches, QintAliasSubstKind::VarDecl), 1);
}

// Negative: `using QArr = qint[4]; QArr a;` → 0 hits (R5 — typedef
// pin: when the carrier walk lands on a `TypedefTypeLoc`, the matcher
// declines to substitute so the user's typedef survives unchanged).
static void test_typedef_carrier_no_match() {
    const auto matches = run_matcher(
        "using qint = sturm::frontend::qint;\n"
        "using QArr = qint[4];\n"
        "void demo() { QArr a; (void)a; }\n");
    CHECK_EQ_INT(matches.size(), 0);
}

int run_test_matcher_qint_alias_subst(int /*argc*/, char** /*argv*/) {
    test_var_decl_positive();
    test_parm_var_decl_positive();
    test_field_decl_positive();
    test_function_return_positive();
    test_functional_cast_positive();
    test_alias_decl_no_match();
    test_backend_qint_t_no_match();
    test_macro_expansion_pinned();
    // Wave 2 (sturm-7t85.1): array & pointer carrier coverage.
    test_var_decl_carray_positive();
    test_parm_carray_positive();
    test_field_carray_positive();
    test_var_decl_ptr_positive();
    test_parm_ptr_positive();
    test_field_ptr_positive();
    test_var_decl_multidim_no_match();
    test_var_decl_ref_to_array_no_match();
    test_typedef_carrier_no_match();
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
