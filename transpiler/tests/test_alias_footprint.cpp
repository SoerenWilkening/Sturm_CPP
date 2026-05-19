// test_alias_footprint.cpp — PM5-4 unit tests for the alias-analysis
// extractor (`footprint()` + `may_overlap()` in `alias.hpp`).
//
// The alias extractor + disjointness predicate — implemented in
// `transpiler/src/alias.cpp` (PM5-2 / PM5-3) — feed the PM5-5 peephole
// reorder matcher. These tests pin the extractor's behaviour across the
// ladder from `docs/implementation_plan_transpiler_phase_m_pm5.md` §2 / §4
// / §10 (Verification) / §12 (Sharp edges):
//
//   - Bare `qbool` DRE → `{name, decl_loc, {0, 1}}`.
//   - Bare `qint_t<W>` DRE for W ∈ {1, 4, 8} → `{name, decl_loc, {0, W}}`.
//   - `q[k]` BitProxy with a compile-time integer literal `k` in `[0, W)`
//     → `{name, decl_loc, {k, k+1}}`.
//   - `q[k]` BitProxy with a non-constant `k` (loop induction variable
//     in the tests, the §12 Sharp edge 1 conservative case) → fall back
//     to the parent's full-width `{0, W}`.
//   - Dependent-type operand (inside a template body pre-instantiation)
//     → universal sentinel.
//   - `may_overlap` on two different-decl footprints → false.
//   - `may_overlap` on two same-decl footprints with disjoint bit ranges
//     → false.
//   - `may_overlap` on two same-decl footprints with overlapping bit
//     ranges → true.
//   - `may_overlap` on the `f(q, q)` aliasing shape (two operands of the
//     same user routine point at the same VarDecl) → true — this is §12
//     Sharp edge 6's conservative-block gate.
//   - `may_overlap` when either side is the universal sentinel → true.
//
// Harness posture. The test uses its own lightweight ASTConsumer pipeline
// (mirrors test_matcher_reader_count / test_matcher_scope_kind) rather
// than routing through the MVP OR matcher harness: the alias extractor
// is independent of any matcher registration, so the direct ASTConsumer
// path isolates the contract. Each test compiles a tiny snippet
// (kQBoolQIntStub + user_src), walks the AST to find named `VarDecl`s /
// subscript `CXXOperatorCallExpr`s, and invokes `footprint()` +
// `may_overlap()` on the results.

#include "sturm/transpile/alias.hpp"
#include "sturm/transpile/qir.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/Casting.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_alias_footprint_ns {

using namespace sturm::transpile;
using sturm::transpile::detail::QubitFootprint;
using sturm::transpile::detail::footprint;
using sturm::transpile::detail::may_overlap;

// ── Test harness ────────────────────────────────────────────────────────────
static int tests_run  = 0;
static int tests_pass = 0;

#define CHECK(cond) do {                                                 \
    ++tests_run;                                                         \
    if (cond) { ++tests_pass; }                                          \
    else {                                                               \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n",                        \
                     __FILE__, __LINE__, #cond);                         \
    }                                                                    \
} while (0)

#define CHECK_EQ_STR(got, want) do {                                     \
    ++tests_run;                                                         \
    if ((got) == (want)) { ++tests_pass; }                               \
    else {                                                               \
        std::fprintf(stderr, "FAIL  %s:%d  strings differ\n"             \
                             "  got:  <<<%s>>>\n"                        \
                             "  want: <<<%s>>>\n",                       \
                     __FILE__, __LINE__,                                 \
                     std::string(got).c_str(),                           \
                     std::string(want).c_str());                         \
    }                                                                    \
} while (0)

#define CHECK_EQ_INT(got, want) do {                                     \
    ++tests_run;                                                         \
    const long long g = static_cast<long long>(got);                     \
    const long long w = static_cast<long long>(want);                    \
    if (g == w) { ++tests_pass; }                                        \
    else {                                                               \
        std::fprintf(stderr, "FAIL  %s:%d  ints differ "                 \
                             "got=%lld want=%lld\n",                     \
                     __FILE__, __LINE__, g, w);                          \
    }                                                                    \
} while (0)

namespace {

// ── Shared stub ─────────────────────────────────────────────────────────────
//
// Declares `sturm::qbool` and `sturm::qint_t<W>` exactly enough for the
// extractor to resolve them:
//   - `qbool` is a CXXRecordDecl whose fully-qualified name is
//     `sturm::qbool`. This satisfies `is_qbool_record()`.
//   - `qint_t<W>` is a ClassTemplateDecl whose fully-qualified name is
//     `sturm::qint_t`. Its template parameter must be a non-type
//     `std::size_t Width` (matches the real qint_core.hpp signature,
//     and `extract_qint_width()` reads the first arg as an integral
//     APSInt regardless of the C++ type).
//   - `qint_t<W>::operator[](size_t)` returns a lightweight `BitProxy`
//     so that `q[k]` on a `qint_t<W>` produces a `CXXOperatorCallExpr`
//     with the `OO_Subscript` operator kind — the canonical shape the
//     alias extractor's Expr overload peels. The BitProxy body is
//     irrelevant; we only need the subscript call to exist in the AST.
//
// A `using qint4_t = sturm::qint_t<4>;` alias keeps the test bodies
// readable without dragging every specialisation into the test
// namespace individually.
constexpr std::string_view kQBoolQIntStub = R"CPP(
namespace std { using size_t = unsigned long; }

namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
};

struct BitProxy {
    BitProxy() {}
};

template <std::size_t Width>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(long long) {}
    qint_t& operator=(const qint_t&) { return *this; }
    BitProxy operator[](std::size_t) { return BitProxy{}; }
    BitProxy operator[](std::size_t) const { return BitProxy{}; }
};

} // namespace sturm

using sturm::qbool;
using sturm::qint_t;
using qint1_t = sturm::qint_t<1>;
using qint4_t = sturm::qint_t<4>;
using qint8_t = sturm::qint_t<8>;
)CPP";

// ── AST walkers ─────────────────────────────────────────────────────────────

// Locate the first VarDecl with the requested name anywhere in the TU.
// The walk stops on first hit (RecursiveASTVisitor::VisitVarDecl returns
// false). Used to grab a named local's VarDecl + decl_loc for feeding
// into the `footprint(QValueRef, ctx)` entry point.
class VarDeclFinder : public clang::RecursiveASTVisitor<VarDeclFinder> {
public:
    explicit VarDeclFinder(std::string name) : name_(std::move(name)) {}
    bool VisitVarDecl(clang::VarDecl* vd) {
        if (!vd) return true;
        if (found_) return true;
        if (vd->getNameAsString() == name_) {
            found_ = vd;
            return false;
        }
        return true;
    }
    const clang::VarDecl* found() const { return found_; }
private:
    std::string name_;
    const clang::VarDecl* found_ = nullptr;
};

// Locate the Nth (0-based) CXXOperatorCallExpr with OO_Subscript in the
// TU. `skip` lets tests pick the second, third, ... subscript inside a
// function with multiple `q[...]` call sites without hand-threading an
// enclosing function anchor.
class SubscriptFinder
    : public clang::RecursiveASTVisitor<SubscriptFinder> {
public:
    explicit SubscriptFinder(int skip) : skip_(skip) {}
    bool VisitCXXOperatorCallExpr(clang::CXXOperatorCallExpr* call) {
        if (!call) return true;
        if (found_) return true;
        if (call->getOperator() != clang::OO_Subscript) return true;
        if (skip_-- > 0) return true;
        found_ = call;
        return false;
    }
    const clang::CXXOperatorCallExpr* found() const { return found_; }
private:
    int skip_;
    const clang::CXXOperatorCallExpr* found_ = nullptr;
};

// ── Consumer / action / factory plumbing ───────────────────────────────────
//
// The test supplies a `Probe` callable that receives the fully-typed
// ASTContext and stashes whatever the test needs (footprint values,
// overlap booleans, etc.). Mirrors the pattern used by
// test_matcher_reader_count / test_matcher_scope_kind.

using Probe = std::function<void(clang::ASTContext&)>;

class AliasConsumer : public clang::ASTConsumer {
public:
    explicit AliasConsumer(Probe probe) : probe_(std::move(probe)) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        if (probe_) probe_(ctx);
    }
private:
    Probe probe_;
};

class AliasAction : public clang::ASTFrontendAction {
public:
    explicit AliasAction(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<AliasConsumer>(probe_);
    }
private:
    Probe probe_;
};

class AliasFactory : public clang::tooling::FrontendActionFactory {
public:
    explicit AliasFactory(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<AliasAction>(probe_);
    }
private:
    Probe probe_;
};

bool run_on(std::string_view user_src, Probe probe) {
    std::string code;
    code.reserve(kQBoolQIntStub.size() + user_src.size());
    code.append(kQBoolQIntStub);
    code.append(user_src);

    AliasFactory factory(std::move(probe));
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), code, args, "alias_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false — snippet failed to "
                     "parse\n");
    }
    return ok;
}

// Build a QValueRef from a named VarDecl inside `user_src`. Runs a
// throwaway Clang invocation; returns the Ref via the `out` pointer so
// callers can pass it directly to `footprint()` alongside the live
// ASTContext. Because the ASTContext only lives for the duration of the
// probe, the caller's full test runs inside the probe callback.
//
// Each test therefore wraps its body inside a single `run_on()` call
// whose probe looks up the VarDecl(s) it needs and invokes the
// footprint / may_overlap assertions inline.

// ── Test cases ──────────────────────────────────────────────────────────────

// §10 — Bare qbool DRE → `{name, decl_loc, {0, 1}}`.
void test_footprint_bare_qbool() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a) {
    qbool b;
    (void)a;
    (void)b;
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        VarDeclFinder f_a("a");
        f_a.TraverseAST(ctx);
        CHECK(f_a.found() != nullptr);
        if (!f_a.found()) return;

        QValueRef ref_a;
        ref_a.name = f_a.found()->getNameAsString();
        ref_a.decl_loc = f_a.found()->getLocation();

        QubitFootprint fp = footprint(ref_a, ctx);
        CHECK_EQ_STR(fp.name, "a");
        CHECK(fp.decl_loc == f_a.found()->getLocation());
        CHECK_EQ_INT(fp.bit_range.lo, 0);
        CHECK_EQ_INT(fp.bit_range.hi, 1);
    });
    CHECK(ran);
}

// §10 — Bare `qint_t<W>` DRE for W ∈ {1, 4, 8} → `{name, decl_loc, {0, W}}`.
void test_footprint_bare_qint_widths() {
    constexpr std::string_view src = R"CPP(
void demo(qint1_t a1, qint4_t a4, qint8_t a8) {
    (void)a1;
    (void)a4;
    (void)a8;
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        for (const auto& tc :
             std::vector<std::pair<std::string, int>>{
                 {"a1", 1}, {"a4", 4}, {"a8", 8}}) {
            VarDeclFinder f(tc.first);
            f.TraverseAST(ctx);
            CHECK(f.found() != nullptr);
            if (!f.found()) continue;

            QValueRef ref;
            ref.name = f.found()->getNameAsString();
            ref.decl_loc = f.found()->getLocation();

            QubitFootprint fp = footprint(ref, ctx);
            CHECK_EQ_STR(fp.name, tc.first);
            CHECK(fp.decl_loc == f.found()->getLocation());
            CHECK_EQ_INT(fp.bit_range.lo, 0);
            CHECK_EQ_INT(fp.bit_range.hi, tc.second);
        }
    });
    CHECK(ran);
}

// §10 — BitProxy subscript with compile-time literal index in range →
// `{name, decl_loc, {k, k+1}}`. Two subscript call sites in the body:
// q[0] and q[2]; verify both narrow to the correct single-bit range.
void test_footprint_bitproxy_const_in_range() {
    constexpr std::string_view src = R"CPP(
void demo(qint4_t q) {
    (void)q[0];
    (void)q[2];
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        VarDeclFinder f_q("q");
        f_q.TraverseAST(ctx);
        CHECK(f_q.found() != nullptr);
        if (!f_q.found()) return;

        // First subscript: q[0]
        SubscriptFinder sf0(0);
        sf0.TraverseAST(ctx);
        CHECK(sf0.found() != nullptr);
        if (sf0.found()) {
            QubitFootprint fp0 = footprint(*sf0.found(), ctx);
            CHECK_EQ_STR(fp0.name, "q");
            CHECK(fp0.decl_loc == f_q.found()->getLocation());
            CHECK_EQ_INT(fp0.bit_range.lo, 0);
            CHECK_EQ_INT(fp0.bit_range.hi, 1);
        }

        // Second subscript: q[2]
        SubscriptFinder sf2(1);
        sf2.TraverseAST(ctx);
        CHECK(sf2.found() != nullptr);
        if (sf2.found()) {
            QubitFootprint fp2 = footprint(*sf2.found(), ctx);
            CHECK_EQ_STR(fp2.name, "q");
            CHECK(fp2.decl_loc == f_q.found()->getLocation());
            CHECK_EQ_INT(fp2.bit_range.lo, 2);
            CHECK_EQ_INT(fp2.bit_range.hi, 3);
        }
    });
    CHECK(ran);
}

// §10 — BitProxy subscript with compile-time literal index OUT of range
// (`q[5]` on a `qint_t<4>`) → conservative fallback to the parent's
// full-width footprint `{0, W}`. Pins the plan's §4 ladder step 3
// "constant but out-of-range → fall back" clause.
void test_footprint_bitproxy_const_out_of_range() {
    constexpr std::string_view src = R"CPP(
void demo(qint4_t q) {
    (void)q[5];
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        VarDeclFinder f_q("q");
        f_q.TraverseAST(ctx);
        CHECK(f_q.found() != nullptr);
        if (!f_q.found()) return;

        SubscriptFinder sf(0);
        sf.TraverseAST(ctx);
        CHECK(sf.found() != nullptr);
        if (!sf.found()) return;

        QubitFootprint fp = footprint(*sf.found(), ctx);
        CHECK_EQ_STR(fp.name, "q");
        CHECK(fp.decl_loc == f_q.found()->getLocation());
        CHECK_EQ_INT(fp.bit_range.lo, 0);
        CHECK_EQ_INT(fp.bit_range.hi, 4);
    });
    CHECK(ran);
}

// §10 / §12 Sharp edge 1 — BitProxy subscript with non-constant index
// (a loop induction variable) → conservative fallback to the parent's
// full-width footprint `{0, W}`. This is the dominant real-world loop
// shape `for (int i = 0; i < W; ++i) q[i] ...;` that the footprint
// extractor MUST handle conservatively.
void test_footprint_bitproxy_nonconst_fallback() {
    constexpr std::string_view src = R"CPP(
void demo(qint4_t q) {
    for (int i = 0; i < 4; ++i) {
        (void)q[i];
    }
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        VarDeclFinder f_q("q");
        f_q.TraverseAST(ctx);
        CHECK(f_q.found() != nullptr);
        if (!f_q.found()) return;

        SubscriptFinder sf(0);
        sf.TraverseAST(ctx);
        CHECK(sf.found() != nullptr);
        if (!sf.found()) return;

        QubitFootprint fp = footprint(*sf.found(), ctx);
        CHECK_EQ_STR(fp.name, "q");
        CHECK(fp.decl_loc == f_q.found()->getLocation());
        CHECK_EQ_INT(fp.bit_range.lo, 0);
        CHECK_EQ_INT(fp.bit_range.hi, 4);
    });
    CHECK(ran);
}

// §10 — Two qbool DREs with DIFFERENT decls → `may_overlap == false`.
// Confirms the "different-decl cannot alias" short-circuit. Different
// VarDecls always have different `getLocation()`s — criterion #2 in
// `may_overlap`.
void test_may_overlap_qbool_different_decls() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a, qbool b) {
    (void)a;
    (void)b;
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        VarDeclFinder f_a("a");
        VarDeclFinder f_b("b");
        f_a.TraverseAST(ctx);
        f_b.TraverseAST(ctx);
        CHECK(f_a.found() != nullptr);
        CHECK(f_b.found() != nullptr);
        if (!f_a.found() || !f_b.found()) return;

        QValueRef ref_a{f_a.found()->getNameAsString(),
                        f_a.found()->getLocation()};
        QValueRef ref_b{f_b.found()->getNameAsString(),
                        f_b.found()->getLocation()};
        QubitFootprint fp_a = footprint(ref_a, ctx);
        QubitFootprint fp_b = footprint(ref_b, ctx);

        CHECK(!may_overlap(fp_a, fp_b));
        CHECK(!may_overlap(fp_b, fp_a)); // symmetry
    });
    CHECK(ran);
}

// §10 — Same qbool decl on both sides → `may_overlap == true`. The
// "qbool self-overlap" base case: the same footprint must always
// overlap with itself (reflexivity).
void test_may_overlap_qbool_self() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a) {
    (void)a;
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        VarDeclFinder f_a("a");
        f_a.TraverseAST(ctx);
        CHECK(f_a.found() != nullptr);
        if (!f_a.found()) return;

        QValueRef ref_a{f_a.found()->getNameAsString(),
                        f_a.found()->getLocation()};
        QubitFootprint fp = footprint(ref_a, ctx);
        // Reflexive same-decl, same-range overlap.
        CHECK(may_overlap(fp, fp));
    });
    CHECK(ran);
}

// §10 — qint_t<4> FULL-WIDTH `a` vs single-bit `a[1]` → overlap = true.
// Same decl on both sides; the single bit `{1, 2}` is contained within
// `{0, 4}`.
void test_may_overlap_qint_full_vs_bit() {
    constexpr std::string_view src = R"CPP(
void demo(qint4_t a) {
    (void)a[1];
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        VarDeclFinder f_a("a");
        f_a.TraverseAST(ctx);
        CHECK(f_a.found() != nullptr);
        if (!f_a.found()) return;

        SubscriptFinder sf(0);
        sf.TraverseAST(ctx);
        CHECK(sf.found() != nullptr);
        if (!sf.found()) return;

        QValueRef ref_full{f_a.found()->getNameAsString(),
                           f_a.found()->getLocation()};
        QubitFootprint fp_full = footprint(ref_full, ctx);
        QubitFootprint fp_bit = footprint(*sf.found(), ctx);

        // Full-width `{0, 4}` strictly contains single-bit `{1, 2}`.
        CHECK_EQ_INT(fp_full.bit_range.lo, 0);
        CHECK_EQ_INT(fp_full.bit_range.hi, 4);
        CHECK_EQ_INT(fp_bit.bit_range.lo, 1);
        CHECK_EQ_INT(fp_bit.bit_range.hi, 2);
        CHECK(may_overlap(fp_full, fp_bit));
        CHECK(may_overlap(fp_bit, fp_full));
    });
    CHECK(ran);
}

// §10 — qint_t<4> `a[0]` vs `a[1]` — same decl, disjoint literal bits →
// overlap = false. The PJ-1-style fusion gate needs this case to work
// correctly so the reorder matcher can commute `a[0] ^= ...;` past
// `a[1] ^= ...;`.
void test_may_overlap_qint_disjoint_bits_same_decl() {
    constexpr std::string_view src = R"CPP(
void demo(qint4_t a) {
    (void)a[0];
    (void)a[1];
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        VarDeclFinder f_a("a");
        f_a.TraverseAST(ctx);
        CHECK(f_a.found() != nullptr);
        if (!f_a.found()) return;

        SubscriptFinder sf0(0);
        SubscriptFinder sf1(1);
        sf0.TraverseAST(ctx);
        sf1.TraverseAST(ctx);
        CHECK(sf0.found() != nullptr);
        CHECK(sf1.found() != nullptr);
        if (!sf0.found() || !sf1.found()) return;

        QubitFootprint fp0 = footprint(*sf0.found(), ctx);
        QubitFootprint fp1 = footprint(*sf1.found(), ctx);

        // Both narrow to single bits of the SAME decl.
        CHECK_EQ_STR(fp0.name, "a");
        CHECK_EQ_STR(fp1.name, "a");
        CHECK(fp0.decl_loc == fp1.decl_loc);
        CHECK_EQ_INT(fp0.bit_range.hi, 1);
        CHECK_EQ_INT(fp1.bit_range.lo, 1);

        // Disjoint: `{0, 1}` strictly before `{1, 2}` (hi <= lo).
        CHECK(!may_overlap(fp0, fp1));
        CHECK(!may_overlap(fp1, fp0)); // symmetry
    });
    CHECK(ran);
}

// §10 / §12 Sharp edge 1 — BitProxy non-constant index falls back to
// the parent's full width, so it overlaps with any other literal
// BitProxy on the same decl, even if the literal index is a different
// single bit. This is the conservative reject the reorder matcher
// relies on: `q[i]` is treated as writing every bit of `q`, so the
// peephole refuses to commute `q[i] ^= ...;` past `q[2] ^= ...;` even
// when the value ranges may be disjoint at runtime.
void test_may_overlap_bitproxy_nonconst_vs_literal_same_decl() {
    constexpr std::string_view src = R"CPP(
void demo(qint4_t q) {
    for (int i = 0; i < 4; ++i) {
        (void)q[i];
    }
    (void)q[2];
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        SubscriptFinder sf_dyn(0);
        SubscriptFinder sf_lit(1);
        sf_dyn.TraverseAST(ctx);
        sf_lit.TraverseAST(ctx);
        CHECK(sf_dyn.found() != nullptr);
        CHECK(sf_lit.found() != nullptr);
        if (!sf_dyn.found() || !sf_lit.found()) return;

        QubitFootprint fp_dyn = footprint(*sf_dyn.found(), ctx);
        QubitFootprint fp_lit = footprint(*sf_lit.found(), ctx);

        // Dynamic index collapses to full-width on the same decl.
        CHECK_EQ_INT(fp_dyn.bit_range.lo, 0);
        CHECK_EQ_INT(fp_dyn.bit_range.hi, 4);
        // Literal index narrows to a single bit.
        CHECK_EQ_INT(fp_lit.bit_range.lo, 2);
        CHECK_EQ_INT(fp_lit.bit_range.hi, 3);
        // Same decl — the full-width overlaps with the single bit.
        CHECK(may_overlap(fp_dyn, fp_lit));
        CHECK(may_overlap(fp_lit, fp_dyn)); // symmetry
    });
    CHECK(ran);
}

// §10 — Two `qint_t<4>` DREs with different names → overlap = false.
// Cross-decl disjointness on qint-flavoured operands (complements the
// qbool cross-decl test). Pins that `may_overlap` rejects different
// decls even when both footprints span the full width.
void test_may_overlap_qint_different_decls() {
    constexpr std::string_view src = R"CPP(
void demo(qint4_t a, qint4_t b) {
    (void)a;
    (void)b;
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        VarDeclFinder f_a("a");
        VarDeclFinder f_b("b");
        f_a.TraverseAST(ctx);
        f_b.TraverseAST(ctx);
        CHECK(f_a.found() != nullptr);
        CHECK(f_b.found() != nullptr);
        if (!f_a.found() || !f_b.found()) return;

        QValueRef ref_a{f_a.found()->getNameAsString(),
                        f_a.found()->getLocation()};
        QValueRef ref_b{f_b.found()->getNameAsString(),
                        f_b.found()->getLocation()};

        QubitFootprint fp_a = footprint(ref_a, ctx);
        QubitFootprint fp_b = footprint(ref_b, ctx);

        // Both resolved to full-width footprints of DIFFERENT decls.
        CHECK_EQ_INT(fp_a.bit_range.hi, 4);
        CHECK_EQ_INT(fp_b.bit_range.hi, 4);
        CHECK(fp_a.decl_loc != fp_b.decl_loc);
        CHECK(!may_overlap(fp_a, fp_b));
        CHECK(!may_overlap(fp_b, fp_a)); // symmetry
    });
    CHECK(ran);
}

// §10 — Same const index on same qint_t<W> (two `q[0]` BitProxys with
// the same compile-time index) → overlap = true. Reflexive single-bit
// overlap; the two expressions resolve to the same `{name, decl_loc,
// {0, 1}}` footprint.
void test_may_overlap_bitproxy_same_const_same_decl() {
    constexpr std::string_view src = R"CPP(
void demo(qint4_t q) {
    (void)q[0];
    (void)q[0];
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        SubscriptFinder sf0(0);
        SubscriptFinder sf1(1);
        sf0.TraverseAST(ctx);
        sf1.TraverseAST(ctx);
        CHECK(sf0.found() != nullptr);
        CHECK(sf1.found() != nullptr);
        if (!sf0.found() || !sf1.found()) return;

        QubitFootprint fp0 = footprint(*sf0.found(), ctx);
        QubitFootprint fp1 = footprint(*sf1.found(), ctx);

        // Identical footprints.
        CHECK_EQ_STR(fp0.name, fp1.name);
        CHECK(fp0.decl_loc == fp1.decl_loc);
        CHECK_EQ_INT(fp0.bit_range.lo, fp1.bit_range.lo);
        CHECK_EQ_INT(fp0.bit_range.hi, fp1.bit_range.hi);
        CHECK(may_overlap(fp0, fp1));
    });
    CHECK(ran);
}

// §10 / §12 Sharp edge 6 — `f(q, q)` USER_ROUTINE aliasing shape.
// Two operands of the same user-routine call point at the SAME VarDecl,
// so `may_overlap` returns true. Pins the §12 Sharp edge 6 gate the
// peephole matcher relies on to refuse commutation across a routine
// call whose written operand aliases one of its read operands.
void test_may_overlap_user_routine_double_ref_aliases() {
    constexpr std::string_view src = R"CPP(
void f(qbool&, qbool&);
void demo(qbool q) {
    f(q, q);
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        // Resolve `q` via its VarDecl; both operands in `f(q, q)` are
        // DREs to the same VarDecl, so they produce identical
        // footprints. The comparison matches what the peephole matcher
        // does when walking a USER_ROUTINE op's operand list.
        VarDeclFinder f_q("q");
        f_q.TraverseAST(ctx);
        CHECK(f_q.found() != nullptr);
        if (!f_q.found()) return;

        QValueRef ref_left{f_q.found()->getNameAsString(),
                           f_q.found()->getLocation()};
        QValueRef ref_right = ref_left; // same VarDecl, same spelling
        QubitFootprint fp_left = footprint(ref_left, ctx);
        QubitFootprint fp_right = footprint(ref_right, ctx);

        CHECK_EQ_STR(fp_left.name, "q");
        CHECK_EQ_STR(fp_right.name, "q");
        CHECK(fp_left.decl_loc == fp_right.decl_loc);
        CHECK(may_overlap(fp_left, fp_right));
    });
    CHECK(ran);
}

// §10 — Dependent-type operand → universal sentinel → `may_overlap`
// returns true against every other footprint. Pins the §4 ladder step 5
// / §12 Sharp edge 3 fallback clause: an operand we cannot resolve
// conservatively overlaps everything so the reorder matcher refuses to
// commute through it.
void test_footprint_dependent_type_universal_sentinel() {
    // A function template body referencing a `T` parameter produces a
    // VarDecl whose type is dependent until instantiation. The
    // extractor must bail to the universal sentinel without asserting.
    constexpr std::string_view src = R"CPP(
template <typename T>
void demo(T a) {
    T b = a;
    (void)b;
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        VarDeclFinder f_a("a");
        f_a.TraverseAST(ctx);
        CHECK(f_a.found() != nullptr);
        if (!f_a.found()) return;

        // The function-parameter `a` in a template function body has a
        // dependent type before instantiation. The extractor resolves
        // this to the universal sentinel.
        QValueRef ref_a{f_a.found()->getNameAsString(),
                        f_a.found()->getLocation()};
        QubitFootprint fp = footprint(ref_a, ctx);
        CHECK(fp.name.empty());  // universal sentinel: empty name.
        CHECK(fp.decl_loc.isInvalid()); // universal sentinel: invalid loc.
        CHECK_EQ_INT(fp.bit_range.lo, 0);
        CHECK_EQ_INT(fp.bit_range.hi, 0);

        // And: a universal sentinel overlaps every other footprint —
        // including a concrete qbool footprint AND another universal
        // sentinel (criterion #1 in `may_overlap`).
        QubitFootprint universal{};
        CHECK(may_overlap(fp, universal));
        CHECK(may_overlap(universal, fp));
        CHECK(may_overlap(universal, universal));
    });
    CHECK(ran);
}

// §10 — Universal sentinel overlaps every concrete footprint too.
// Complements the dependent-type test: explicitly pin the "universal
// sentinel vs concrete qbool" case, in case a future refactor
// introduces a special-case that accidentally breaks this wildcard
// behaviour.
void test_may_overlap_universal_vs_concrete() {
    constexpr std::string_view src = R"CPP(
void demo(qbool a) {
    (void)a;
}
)CPP";
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        VarDeclFinder f_a("a");
        f_a.TraverseAST(ctx);
        CHECK(f_a.found() != nullptr);
        if (!f_a.found()) return;

        QValueRef ref_a{f_a.found()->getNameAsString(),
                        f_a.found()->getLocation()};
        QubitFootprint fp_concrete = footprint(ref_a, ctx);
        QubitFootprint universal{};

        // Concrete resolves cleanly.
        CHECK(!fp_concrete.name.empty());
        CHECK_EQ_INT(fp_concrete.bit_range.hi, 1);

        // Universal sentinel overlaps with every concrete footprint.
        CHECK(may_overlap(fp_concrete, universal));
        CHECK(may_overlap(universal, fp_concrete));
    });
    CHECK(ran);
}

} // namespace

}  // namespace sturm_test_alias_footprint_ns

int run_test_alias_footprint(int /*argc*/, char** /*argv*/) {
    using namespace sturm_test_alias_footprint_ns;
    using sturm_test_alias_footprint_ns::tests_run;
    using sturm_test_alias_footprint_ns::tests_pass;
    test_footprint_bare_qbool();
    test_footprint_bare_qint_widths();
    test_footprint_bitproxy_const_in_range();
    test_footprint_bitproxy_const_out_of_range();
    test_footprint_bitproxy_nonconst_fallback();
    test_may_overlap_qbool_different_decls();
    test_may_overlap_qbool_self();
    test_may_overlap_qint_full_vs_bit();
    test_may_overlap_qint_disjoint_bits_same_decl();
    test_may_overlap_qint_different_decls();
    test_may_overlap_bitproxy_nonconst_vs_literal_same_decl();
    test_may_overlap_bitproxy_same_const_same_decl();
    test_may_overlap_user_routine_double_ref_aliases();
    test_footprint_dependent_type_universal_sentinel();
    test_may_overlap_universal_vs_concrete();

    std::fprintf(stderr, "test_alias_footprint: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
