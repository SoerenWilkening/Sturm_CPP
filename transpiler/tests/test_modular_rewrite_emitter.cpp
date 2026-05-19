// test_modular_rewrite_emitter.cpp — sturm-qzab.1 / sturm-qzab.2
// (P5.1 beat 5.1 + P5.2 beat 5.2) unit tests for the modular-arithmetic
// rewrite emitter.
//
// Beat 5.1 covers the AddMod arm. The emitter's contract for
// `qint_t<W> r = (a + b) % n;` is to produce a single statement-level
// rewrite: `sturm::qint_t<W> r = ::sturm::add_mod(a, b, n);`. The
// emitter is a pure-text leaf — no `#line` prefix, no Rewriter — the
// wiring layer in `transpile_consumer.cpp` is the one that splices the
// `#line` directives and the `QReplacement` source range.
//
// Beat 5.2 mirrors that for the MulMod arm: `qint_t<W> r = (a * b) % n;`
// → `sturm::qint_t<W> r = ::sturm::mul_mod(a, b, n);`. Same
// pure-string posture, different free-function name. The width / empty
// / alloc-slot contracts apply identically to MulMod.
//
// Counter contract: neither AddMod nor MulMod allocates any ancilla
// tmp at the AST-rewrite level (the wide-intermediate elimination
// happens at lib-level — `lib_mul_mod_dsl` interleaves the reduction
// internally). Both arms therefore leave the fresh-name allocator
// untouched. Beat 5.4 may revisit when the PowMod arm lands.

#include "modular_rewrite_emitter.hpp"

#include "fresh_names.hpp"
#include "matcher_modular_op.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

constexpr std::string_view kModularStub = R"CPP(
namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    qint_t& operator=(const qint_t&) { return *this; }
};
template <int W>
inline qint_t<W> operator+(const qint_t<W>&, const qint_t<W>&) {
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
} // namespace sturm
using qint = sturm::qint_t<2>;
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

int tests_run = 0, tests_pass = 0;

#define CHECK(cond) do { ++tests_run;                                 \
    if (cond) { ++tests_pass; }                                       \
    else { std::fprintf(stderr, "FAIL  %s:%d  %s\n",                  \
                        __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_EQ_STR(got, want) do { ++tests_run;                     \
    if ((got) == (want)) { ++tests_pass; }                            \
    else { std::fprintf(stderr,                                       \
        "FAIL  %s:%d  diff\n  got:  <<<%s>>>\n  want: <<<%s>>>\n",    \
        __FILE__, __LINE__,                                           \
        std::string(got).c_str(), std::string(want).c_str()); }       \
} while (0)

// Pure-string AddMod emission. Width=0 ⇒ legacy bare `qint` typename
// (the hermetic-stub fixture shape). Width>0 ⇒ `sturm::qint_t<W>`.
void test_add_mod_text_with_width() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::AddMod, "r", "a", "b", "n", 2, alloc);
    CHECK_EQ_STR(em.text,
        std::string("sturm::qint_t<2> r = ::sturm::add_mod(a, b, n);\n"));
    CHECK_EQ_STR(em.cleanup_anchor_name, std::string("r"));
    CHECK(em.kind == ModularOpKind::AddMod);
}

void test_add_mod_text_without_width() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::AddMod, "p", "x", "y", "m", 0, alloc);
    CHECK_EQ_STR(em.text,
        std::string("qint p = ::sturm::add_mod(x, y, m);\n"));
    CHECK_EQ_STR(em.cleanup_anchor_name, std::string("p"));
}

// AddMod does NOT consume a fresh-name slot — the rewrite is a single
// VarDecl with no auxiliary temps. A subsequent allocator call should
// still mint `__stu_t0`, proving the slot was untouched.
void test_add_mod_does_not_consume_alloc_slot() {
    FreshNameAllocator alloc;
    (void)emit_modular_forward_text(
        ModularOpKind::AddMod, "r", "a", "b", "n", 2, alloc);
    CHECK_EQ_STR(alloc.next(), std::string("__stu_t0"));
}

// Defensive: empty operand or result name yields empty text.
void test_empty_result_name() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::AddMod, "", "a", "b", "n", 2, alloc);
    CHECK(em.text.empty());
}

void test_empty_a_name() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::AddMod, "r", "", "b", "n", 2, alloc);
    CHECK(em.text.empty());
}

void test_empty_b_name() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::AddMod, "r", "a", "", "n", 2, alloc);
    CHECK(em.text.empty());
}

void test_empty_n_name() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::AddMod, "r", "a", "b", "", 2, alloc);
    CHECK(em.text.empty());
}

// AST-driven path: feed a real ModularOpHit through `emit_modular_forward`.
// The matcher resolves W=2 off the result VarDecl's type; the emitter must
// splice that into the rewrite as `sturm::qint_t<2>`.
void test_ast_driven_add_mod_emits_full_rewrite() {
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a + b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward(hits[0], alloc);
    CHECK_EQ_STR(em.text,
        std::string("sturm::qint_t<2> r = ::sturm::add_mod(a, b, n);\n"));
    CHECK(em.kind == ModularOpKind::AddMod);
}

// sturm-qzab.2 (P5.2 beat 5.2): MulMod arm — emits the
// `sturm::mul_mod(a, b, n)` free-function call, mirroring AddMod's
// shape. All width / empty-operand / alloc-slot contracts apply
// identically.
void test_mul_mod_text_with_width() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::MulMod, "r", "a", "b", "n", 2, alloc);
    CHECK_EQ_STR(em.text,
        std::string("sturm::qint_t<2> r = ::sturm::mul_mod(a, b, n);\n"));
    CHECK_EQ_STR(em.cleanup_anchor_name, std::string("r"));
    CHECK(em.kind == ModularOpKind::MulMod);
}

void test_mul_mod_text_without_width() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::MulMod, "p", "x", "y", "m", 0, alloc);
    CHECK_EQ_STR(em.text,
        std::string("qint p = ::sturm::mul_mod(x, y, m);\n"));
    CHECK_EQ_STR(em.cleanup_anchor_name, std::string("p"));
}

// MulMod, like AddMod, does not consume a fresh-name slot. The
// allocator's first call after the emission must still mint
// `__stu_t0`.
void test_mul_mod_does_not_consume_alloc_slot() {
    FreshNameAllocator alloc;
    (void)emit_modular_forward_text(
        ModularOpKind::MulMod, "r", "a", "b", "n", 2, alloc);
    CHECK_EQ_STR(alloc.next(), std::string("__stu_t0"));
}

void test_mul_mod_empty_result_name() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::MulMod, "", "a", "b", "n", 2, alloc);
    CHECK(em.text.empty());
}

void test_mul_mod_empty_a_name() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::MulMod, "r", "", "b", "n", 2, alloc);
    CHECK(em.text.empty());
}

void test_mul_mod_empty_b_name() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::MulMod, "r", "a", "", "n", 2, alloc);
    CHECK(em.text.empty());
}

void test_mul_mod_empty_n_name() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::MulMod, "r", "a", "b", "", 2, alloc);
    CHECK(em.text.empty());
}

// AST-driven MulMod: feed a real `(a * b) % n` ModularOpHit through
// `emit_modular_forward` and check the rendered text matches the
// post-rewrite contract.
void test_ast_driven_mul_mod_emits_full_rewrite() {
    auto hits = run_matcher(
        "void demo(qint a, qint b, qint n) {\n"
        "    qint r = (a * b) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::MulMod);
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward(hits[0], alloc);
    CHECK_EQ_STR(em.text,
        std::string("sturm::qint_t<2> r = ::sturm::mul_mod(a, b, n);\n"));
    CHECK(em.kind == ModularOpKind::MulMod);
}

// ── sturm-qzab.5 (P5 beat 5.5): PowMod arm — emits the
// `sturm::pow_mod(a, x, n)` free-function call. Mirrors the AddMod /
// MulMod shape (`sturm::qint_t<W> r = ::sturm::pow_mod(a, x, n);\n`)
// — same width-resolution, same empty-operand fall-throughs, same
// alloc-slot non-consumption posture (the wide-intermediate
// elimination happens inside `lib_pow_mod_dsl`, not at the AST-rewrite
// level). The pure-string contracts compile under both flag modes —
// the gating (`#ifdef STURM_TEST_MODULAR_POW_ON`) lives only on the matcher
// arm registration; the emitter dispatch on `ModularOpKind::PowMod`
// is unconditional.
void test_pow_mod_text_with_width() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::PowMod, "r", "a", "x", "n", 2, alloc);
    CHECK_EQ_STR(em.text,
        std::string("sturm::qint_t<2> r = ::sturm::pow_mod(a, x, n);\n"));
    CHECK_EQ_STR(em.cleanup_anchor_name, std::string("r"));
    CHECK(em.kind == ModularOpKind::PowMod);
}

void test_pow_mod_text_without_width() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::PowMod, "p", "u", "v", "m", 0, alloc);
    CHECK_EQ_STR(em.text,
        std::string("qint p = ::sturm::pow_mod(u, v, m);\n"));
    CHECK_EQ_STR(em.cleanup_anchor_name, std::string("p"));
}

void test_pow_mod_does_not_consume_alloc_slot() {
    FreshNameAllocator alloc;
    (void)emit_modular_forward_text(
        ModularOpKind::PowMod, "r", "a", "x", "n", 2, alloc);
    CHECK_EQ_STR(alloc.next(), std::string("__stu_t0"));
}

void test_pow_mod_empty_result_name() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::PowMod, "", "a", "x", "n", 2, alloc);
    CHECK(em.text.empty());
}

void test_pow_mod_empty_a_name() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::PowMod, "r", "", "x", "n", 2, alloc);
    CHECK(em.text.empty());
}

void test_pow_mod_empty_b_name() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::PowMod, "r", "a", "", "n", 2, alloc);
    CHECK(em.text.empty());
}

void test_pow_mod_empty_n_name() {
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward_text(
        ModularOpKind::PowMod, "r", "a", "x", "", 2, alloc);
    CHECK(em.text.empty());
}

#ifdef STURM_TEST_MODULAR_POW_ON
// AST-driven PowMod: only meaningful under flag-on (the matcher
// otherwise produces no hit on this AST shape, by design — see
// sturm-qzab.4). Feeds a real `pow(a, x) % n` ModularOpHit through
// `emit_modular_forward` and checks the rendered text matches the
// post-rewrite contract.
constexpr std::string_view kPowStub = R"CPP(
template <int W>
inline sturm::qint_t<W> pow(const sturm::qint_t<W>&, const sturm::qint_t<W>&) {
    return sturm::qint_t<W>{};
}
)CPP";

std::vector<ModularOpHit> run_matcher_with_pow_stub(std::string_view user_src) {
    std::string code;
    code.reserve(kModularStub.size() + kPowStub.size() + user_src.size());
    code.append(kModularStub);
    // Inject the namespaced `sturm::pow` overload (the kModularStub above
    // does not carry it because the AddMod / MulMod tests don't need it).
    code.append("namespace sturm {\n");
    code.append("template <int W>\n");
    code.append("inline qint_t<W> pow(const qint_t<W>&, const qint_t<W>&) {\n"
                "    return qint_t<W>{};\n"
                "}\n");
    code.append("} // namespace sturm\n");
    code.append(user_src);

    std::vector<ModularOpHit> hits;
    clang::ast_matchers::MatchFinder finder;
    register_modular_op_matcher(finder, hits);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr, "FAIL  tool run returned false (pow stub)\n");
    }
    return hits;
}

void test_ast_driven_pow_mod_emits_full_rewrite() {
    auto hits = run_matcher_with_pow_stub(
        "void demo(qint a, qint x, qint n) {\n"
        "    qint r = sturm::pow(a, x) % n;\n"
        "    (void)r;\n"
        "}\n");
    CHECK(hits.size() == 1);
    if (hits.empty()) return;
    CHECK(hits[0].kind == ModularOpKind::PowMod);
    FreshNameAllocator alloc;
    ModularEmission em = emit_modular_forward(hits[0], alloc);
    CHECK_EQ_STR(em.text,
        std::string("sturm::qint_t<2> r = ::sturm::pow_mod(a, x, n);\n"));
    CHECK(em.kind == ModularOpKind::PowMod);
}
#endif // STURM_MODULAR_POW

} // namespace

int run_test_modular_rewrite_emitter(int /*argc*/, char** /*argv*/) {
    test_add_mod_text_with_width();
    test_add_mod_text_without_width();
    test_add_mod_does_not_consume_alloc_slot();
    test_empty_result_name();
    test_empty_a_name();
    test_empty_b_name();
    test_empty_n_name();
    test_ast_driven_add_mod_emits_full_rewrite();
    test_mul_mod_text_with_width();
    test_mul_mod_text_without_width();
    test_mul_mod_does_not_consume_alloc_slot();
    test_mul_mod_empty_result_name();
    test_mul_mod_empty_a_name();
    test_mul_mod_empty_b_name();
    test_mul_mod_empty_n_name();
    test_ast_driven_mul_mod_emits_full_rewrite();
    test_pow_mod_text_with_width();
    test_pow_mod_text_without_width();
    test_pow_mod_does_not_consume_alloc_slot();
    test_pow_mod_empty_result_name();
    test_pow_mod_empty_a_name();
    test_pow_mod_empty_b_name();
    test_pow_mod_empty_n_name();
#ifdef STURM_TEST_MODULAR_POW_ON
    test_ast_driven_pow_mod_emits_full_rewrite();
#endif
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
