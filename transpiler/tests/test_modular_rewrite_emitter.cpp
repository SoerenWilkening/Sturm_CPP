// test_modular_rewrite_emitter.cpp — sturm-qzab.1 (P5.1 beat 5.1) unit
// tests for the modular-arithmetic rewrite emitter.
//
// Beat 5.1 covers the AddMod arm only. The emitter's contract for
// `qint_t<W> r = (a + b) % n;` is to produce a single statement-level
// rewrite: `sturm::qint_t<W> r = ::sturm::add_mod(a, b, n);`. The
// emitter is a pure-text leaf — no `#line` prefix, no Rewriter — the
// wiring layer in `transpile_consumer.cpp` is the one that splices the
// `#line` directives and the `QReplacement` source range.
//
// Counter contract: AddMod does not allocate any ancilla tmp, so it
// does not consume a fresh-name slot. Beat 5.2 / 5.4 may revisit this
// when MulMod / PowMod arms add their own temps.

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

} // namespace

int main() {
    test_add_mod_text_with_width();
    test_add_mod_text_without_width();
    test_add_mod_does_not_consume_alloc_slot();
    test_empty_result_name();
    test_empty_a_name();
    test_empty_b_name();
    test_empty_n_name();
    test_ast_driven_add_mod_emits_full_rewrite();
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
