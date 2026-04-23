// test_adjoint_emitter.cpp — Phase R R-1 (sturm-88d7.2) unit tests
// for `emit_adjoint_body` + `emit_adjoint_for_decl`.
//
// The module under test walks a validated + normalized routine body
// in reverse statement order and emits a sibling function
// `__<fn>_adj` whose body is the concatenation of per-statement
// rendered inverses (the same text the existing `render_uncompute`
// produces for the inline-uncompute path). Three properties anchor
// the test:
//
//   1. Reverse statement order walk. Given a multi-statement forward
//      body `{op0; op1; op2;}`, the emitted adjoint MUST render the
//      inverses in reverse order: `{adj(op2); adj(op1); adj(op0);}`.
//      This is B11's load-bearing invariant — the adjoint reverses
//      statement order within each scope.
//
//   2. Per-primitive render coverage. Every `QOpKind` that
//      `render_uncompute` recognises must produce the correct
//      inverse text via R-A. Each kind is exercised by at least one
//      test case; the expected text matches what the inline path
//      already emits (byte-identical reuse of the existing
//      renderer, per R-1's design intent).
//
//   3. Enclosing sibling-function shape. The produced source text
//      starts with `void __<fn>_adj(<signature>) {\n`, ends with
//      `}\n`, and contains the rendered lines verbatim between
//      them — no stray whitespace, no duplicated indentation.
//
// Harness posture
// ---------------
// Tests hand-build `QOperation` vectors via
// `test_matcher_harness.hpp`'s shared CHECK/CHECK_EQ_STR macros and
// `make_loc` helpers. The low-level `emit_adjoint_body` entry point
// is the primary interface exercised — it takes a pure-string
// signature + ops vector, so the tests do not need to spin up a
// `FunctionDecl` via LibTooling for every case. A small number of
// cases exercise `emit_adjoint_for_decl` to pin the FD-based
// reject paths (nullptr, missing attribute, no body).
//
// No `SourceManager` is required for the string-based cases: the
// synthetic `SourceLocation`s produced by `getFromRawEncoding` flow
// through `render_uncompute` untouched (the renderer reads only the
// op's `name` / `operands` fields).

#include "adjoint_emitter.hpp"

#include "sturm/transpile/qir.hpp"
#include "sturm/transpile/uncompute_pass.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using sturm::transpile::AdjointEmissionResult;
using sturm::transpile::AdjointRejectReason;
using sturm::transpile::emit_adjoint_body;
using sturm::transpile::emit_adjoint_for_decl;
using sturm::transpile::QOperation;
using sturm::transpile::QOpKind;
using sturm::transpile::QValueRef;
using sturm::transpile::to_string;

// ── Test harness ────────────────────────────────────────────────────────────
//
// Local CHECK / CHECK_EQ_STR macros mirror the shape every sibling
// transpiler test uses. Kept local here so the binary stays self-
// contained (no shared harness counter coupling).
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

#define CHECK_FALSE(cond) CHECK(!(cond))

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

namespace {

// SourceLocation is opaque; raw encoding 0 is "invalid". Any non-zero
// encoding yields a valid-looking location — sufficient for the
// per-op render path because `render_uncompute` only reads names.
clang::SourceLocation make_loc(std::uint32_t raw) {
    return clang::SourceLocation::getFromRawEncoding(raw);
}

// Fabricate a QOperation with a single-operand shape (used for
// the `*_ASSIGN_CONST`, `*_ASSIGN_QINT`, NOT, XOR_ASSIGN, and
// rotation-proxy kinds). The renderer only reads `kind`, `result`,
// and `operands`; everything else stays at its default-constructed
// value.
QOperation op1(QOpKind kind, const char* result, const char* operand) {
    QOperation op;
    op.kind = kind;
    op.result = QValueRef{std::string(result), make_loc(1)};
    op.operands.push_back(QValueRef{std::string(operand), make_loc(2)});
    return op;
}

// Fabricate a QOperation with a two-operand shape (OR, AND, XOR,
// CCNOT_INPLACE, and the *_QINT comparator kinds).
QOperation op2(QOpKind kind, const char* result,
               const char* operand0, const char* operand1) {
    QOperation op;
    op.kind = kind;
    op.result = QValueRef{std::string(result), make_loc(1)};
    op.operands.push_back(QValueRef{std::string(operand0), make_loc(2)});
    op.operands.push_back(QValueRef{std::string(operand1), make_loc(3)});
    return op;
}

// ── (1) Reverse statement order walk on a multi-stmt body ───────────────────

void test_reverse_order_on_three_statement_body() {
    // Hand-built forward body:
    //   qbool __t0 = a | b;   (OR — index 0)
    //   qbool __t1 = c & d;   (AND — index 1)
    //   __t2 = ~__t2;         (NOT — index 2)
    // Expected adjoint body (reverse order):
    //   __t2 = ~__t2;
    //   uncompute_and(__t1, c, d);
    //   uncompute_or(__t0, a, b);
    std::vector<QOperation> ops{
        op2(QOpKind::OR,  "__t0", "a", "b"),
        op2(QOpKind::AND, "__t1", "c", "d"),
        op1(QOpKind::NOT, "__t2", "ignored"),
    };
    const std::string got = emit_adjoint_body("foo",
                                              "qbool& a, qbool& b",
                                              ops);
    const std::string want =
        "void __foo_adj(qbool& a, qbool& b) {\n"
        "    __t2 = ~__t2;\n"
        "    uncompute_and(__t1, c, d);\n"
        "    uncompute_or(__t0, a, b);\n"
        "}\n";
    CHECK_EQ_STR(got, want);
}

void test_single_statement_body() {
    // Sanity check: a single-statement forward still produces a
    // complete sibling function. The "reverse walk" on size-1 is a
    // no-op, but the enclosing shape is exercised.
    std::vector<QOperation> ops{
        op2(QOpKind::OR, "r", "a", "b"),
    };
    const std::string got = emit_adjoint_body("solo",
                                              "qbool& r, qbool a, qbool b",
                                              ops);
    const std::string want =
        "void __solo_adj(qbool& r, qbool a, qbool b) {\n"
        "    uncompute_or(r, a, b);\n"
        "}\n";
    CHECK_EQ_STR(got, want);
}

void test_empty_body() {
    // Zero-op forward — the adjoint emits an empty body (not an
    // error). R-C's driver may dispatch R-A on a routine whose
    // matcher produced no ops (e.g. a routine whose body was
    // entirely elided by an earlier optimization pass).
    std::vector<QOperation> ops;
    const std::string got = emit_adjoint_body("empty", "", ops);
    const std::string want = "void __empty_adj() {\n}\n";
    CHECK_EQ_STR(got, want);
}

void test_empty_fn_name_produces_empty_output() {
    // Degenerate case — no identifier to emit. Returns empty string
    // rather than `void __` which would collide with reserved names.
    std::vector<QOperation> ops{op2(QOpKind::OR, "r", "a", "b")};
    const std::string got = emit_adjoint_body("", "qbool a", ops);
    CHECK(got.empty());
}

// ── (2) Per-primitive render coverage ───────────────────────────────────────
//
// Each kind `render_uncompute` understands is exercised below, once.
// The expected body text matches what the inline-uncompute pass emits
// today — R-A's whole point is byte-identical reuse of the existing
// per-kind renderer (no new code paths).

// Helper that emits a single-op adjoint and returns only the body
// line (without the function prologue/epilogue). Keeps per-kind
// tests terse.
std::string single_op_body(const QOperation& op) {
    std::vector<QOperation> ops{op};
    const std::string full = emit_adjoint_body("f", "", ops);
    // Full text is:
    //   "void __f_adj() {\n<body>}\n"
    // Strip the prologue/epilogue so tests assert on just the body.
    constexpr const char* kPrologue = "void __f_adj() {\n";
    constexpr const char* kEpilogue = "}\n";
    const std::size_t prologue_len = std::string(kPrologue).size();
    const std::size_t epilogue_len = std::string(kEpilogue).size();
    if (full.size() < prologue_len + epilogue_len) return full;
    return full.substr(prologue_len,
                       full.size() - prologue_len - epilogue_len);
}

void test_kind_or() {
    CHECK_EQ_STR(single_op_body(op2(QOpKind::OR, "r", "a", "b")),
                 std::string("    uncompute_or(r, a, b);\n"));
}

void test_kind_and() {
    CHECK_EQ_STR(single_op_body(op2(QOpKind::AND, "r", "a", "b")),
                 std::string("    uncompute_and(r, a, b);\n"));
}

void test_kind_not() {
    CHECK_EQ_STR(single_op_body(op1(QOpKind::NOT, "r", "unused")),
                 std::string("    r = ~r;\n"));
}

void test_kind_xor() {
    // XOR renders TWO lines (self-inverse reduction). Both land in
    // the adjoint body verbatim.
    CHECK_EQ_STR(single_op_body(op2(QOpKind::XOR, "r", "a", "b")),
                 std::string("    r ^= a;\n"
                             "    r ^= b;\n"));
}

void test_kind_xor_assign() {
    CHECK_EQ_STR(single_op_body(op1(QOpKind::XOR_ASSIGN, "a", "b")),
                 std::string("    a ^= b;\n"));
}

void test_kind_add_assign_const() {
    CHECK_EQ_STR(single_op_body(op1(QOpKind::ADD_ASSIGN_CONST, "a", "3")),
                 std::string("    a -= 3;\n"));
}

void test_kind_sub_assign_const() {
    CHECK_EQ_STR(single_op_body(op1(QOpKind::SUB_ASSIGN_CONST, "a", "7")),
                 std::string("    a += 7;\n"));
}

void test_kind_mul_assign_const() {
    CHECK_EQ_STR(single_op_body(op1(QOpKind::MUL_ASSIGN_CONST, "a", "2")),
                 std::string("    a /= 2;\n"));
}

void test_kind_div_assign_const() {
    CHECK_EQ_STR(single_op_body(op1(QOpKind::DIV_ASSIGN_CONST, "a", "4")),
                 std::string("    a *= 4;\n"));
}

void test_kind_theta_add_assign_const() {
    CHECK_EQ_STR(single_op_body(
                     op1(QOpKind::THETA_ADD_ASSIGN_CONST, "q", "0.3")),
                 std::string("    q.theta() -= 0.3;\n"));
}

void test_kind_theta_sub_assign_const() {
    CHECK_EQ_STR(single_op_body(
                     op1(QOpKind::THETA_SUB_ASSIGN_CONST, "q", "0.5")),
                 std::string("    q.theta() += 0.5;\n"));
}

void test_kind_phi_add_assign_const() {
    CHECK_EQ_STR(single_op_body(
                     op1(QOpKind::PHI_ADD_ASSIGN_CONST, "q", "1.0")),
                 std::string("    q.phi() -= 1.0;\n"));
}

void test_kind_phi_sub_assign_const() {
    CHECK_EQ_STR(single_op_body(
                     op1(QOpKind::PHI_SUB_ASSIGN_CONST, "q", "2.0")),
                 std::string("    q.phi() += 2.0;\n"));
}

void test_kind_add_assign_qint() {
    CHECK_EQ_STR(single_op_body(
                     op1(QOpKind::ADD_ASSIGN_QINT, "a", "b")),
                 std::string("    uncompute_add_qint(a, b);\n"));
}

void test_kind_sub_assign_qint() {
    CHECK_EQ_STR(single_op_body(
                     op1(QOpKind::SUB_ASSIGN_QINT, "a", "b")),
                 std::string("    uncompute_sub_qint(a, b);\n"));
}

void test_kind_mul_assign_qint() {
    CHECK_EQ_STR(single_op_body(
                     op1(QOpKind::MUL_ASSIGN_QINT, "a", "b")),
                 std::string("    uncompute_mul_qint(a, b);\n"));
}

void test_kind_div_assign_qint() {
    CHECK_EQ_STR(single_op_body(
                     op1(QOpKind::DIV_ASSIGN_QINT, "a", "b")),
                 std::string("    uncompute_div_qint(a, b);\n"));
}

void test_kind_mod_assign_qint() {
    CHECK_EQ_STR(single_op_body(
                     op1(QOpKind::MOD_ASSIGN_QINT, "a", "b")),
                 std::string("    uncompute_mod_qint(a, b);\n"));
}

void test_kind_eq_qint() {
    CHECK_EQ_STR(single_op_body(op2(QOpKind::EQ_QINT, "c", "a", "b")),
                 std::string("    uncompute_eq_qint(c, a, b);\n"));
}

void test_kind_ne_qint() {
    CHECK_EQ_STR(single_op_body(op2(QOpKind::NE_QINT, "c", "a", "b")),
                 std::string("    uncompute_ne_qint(c, a, b);\n"));
}

void test_kind_lt_qint() {
    CHECK_EQ_STR(single_op_body(op2(QOpKind::LT_QINT, "c", "a", "b")),
                 std::string("    uncompute_lt_qint(c, a, b);\n"));
}

void test_kind_le_qint() {
    CHECK_EQ_STR(single_op_body(op2(QOpKind::LE_QINT, "c", "a", "b")),
                 std::string("    uncompute_le_qint(c, a, b);\n"));
}

void test_kind_gt_qint() {
    CHECK_EQ_STR(single_op_body(op2(QOpKind::GT_QINT, "c", "a", "b")),
                 std::string("    uncompute_gt_qint(c, a, b);\n"));
}

void test_kind_ge_qint() {
    CHECK_EQ_STR(single_op_body(op2(QOpKind::GE_QINT, "c", "a", "b")),
                 std::string("    uncompute_ge_qint(c, a, b);\n"));
}

void test_kind_user_routine() {
    // USER_ROUTINE has NO single result — arguments sit in
    // `operands` and `routine_name` carries the callee identifier.
    // Renders `invert(<name>)(<op0>, <op1>, ...);` at the uncompute
    // point — same inverse text Phase I PI-4 emits.
    QOperation op;
    op.kind = QOpKind::USER_ROUTINE;
    op.routine_name = "helper";
    op.operands.push_back(QValueRef{"x", make_loc(1)});
    op.operands.push_back(QValueRef{"y", make_loc(2)});
    CHECK_EQ_STR(single_op_body(op),
                 std::string("    invert(helper)(x, y);\n"));
}

void test_kind_ccnot_inplace() {
    CHECK_EQ_STR(single_op_body(
                     op2(QOpKind::CCNOT_INPLACE, "x", "a", "b")),
                 std::string("    ccnot_inplace(x, a, b);\n"));
}

void test_kind_plugin_without_registry_elides() {
    // PLUGIN without a registry renders to empty (same posture the
    // inline uncompute pass takes). R-A silently elides the line —
    // the enclosing adjoint shape still emits, the body is just
    // shorter than the ops vector.
    QOperation op;
    op.kind = QOpKind::PLUGIN;
    op.plugin_kind_id = "demo.kind";
    op.result = QValueRef{"r", make_loc(1)};
    op.operands.push_back(QValueRef{"a", make_loc(2)});
    // One-op body with an ignored plugin op → empty body.
    std::vector<QOperation> ops{op};
    const std::string got = emit_adjoint_body("f", "", ops);
    CHECK_EQ_STR(got, std::string("void __f_adj() {\n}\n"));
}

// ── (3) Skip-uncompute parity (PH-3) ────────────────────────────────────────

void test_skip_uncompute_flag_elides_op() {
    // Phase H PH-3 parity: ops marked `skip_uncompute=true` (outer
    // variable mutations inside a loop body — Phase S's problem)
    // must NOT emit an adjoint line in R-A either. The op stays in
    // the IR; R-A elides it the same way `synthesize()` does.
    QOperation skip_me = op2(QOpKind::OR, "r", "a", "b");
    skip_me.skip_uncompute = true;
    QOperation keep_me = op1(QOpKind::XOR_ASSIGN, "c", "d");
    std::vector<QOperation> ops{skip_me, keep_me};
    const std::string got = emit_adjoint_body("drop", "", ops);
    // `keep_me` renders to `c ^= d;` — reverse-order is the single
    // surviving line.
    const std::string want =
        "void __drop_adj() {\n"
        "    c ^= d;\n"
        "}\n";
    CHECK_EQ_STR(got, want);
}

// ── (4) Signature passthrough ────────────────────────────────────────────────

void test_signature_passes_through_verbatim() {
    // R-A does not re-format the signature string; whatever the
    // caller hands in lands between the parens verbatim. This is
    // the contract R-C relies on to preserve const-ness,
    // reference/value category, and default arguments from the
    // forward's parameter recovery.
    std::vector<QOperation> ops;
    const std::string got = emit_adjoint_body(
        "sig",
        "const qint& x, int T, qbool& result",
        ops);
    const std::string want =
        "void __sig_adj(const qint& x, int T, qbool& result) {\n"
        "}\n";
    CHECK_EQ_STR(got, want);
}

// ── (5) FD-based entry point — integration via LibTooling ───────────────────

// Probe invoked by the consumer with a fully-populated ASTContext.
using Probe = std::function<void(clang::ASTContext&)>;

class FnConsumer : public clang::ASTConsumer {
public:
    explicit FnConsumer(Probe probe) : probe_(std::move(probe)) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        if (probe_) probe_(ctx);
    }
private:
    Probe probe_;
};

class FnAction : public clang::ASTFrontendAction {
public:
    explicit FnAction(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<FnConsumer>(probe_);
    }
private:
    Probe probe_;
};

class FnFactory : public clang::tooling::FrontendActionFactory {
public:
    explicit FnFactory(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<FnAction>(probe_);
    }
private:
    Probe probe_;
};

// Locate the first FunctionDecl in the TU whose short name matches
// `name_`. Skips template instantiations.
class NamedFnFinder
    : public clang::RecursiveASTVisitor<NamedFnFinder> {
public:
    explicit NamedFnFinder(std::string name) : name_(std::move(name)) {}
    bool VisitFunctionDecl(clang::FunctionDecl* fd) {
        if (found_) return true;
        if (fd == nullptr) return true;
        if (fd->getNameAsString() != name_) return true;
        if (fd->isTemplateInstantiation()) return true;
        found_ = fd;
        return false;
    }
    const clang::FunctionDecl* found() const { return found_; }
private:
    std::string name_;
    const clang::FunctionDecl* found_ = nullptr;
};

// Compile `src` as C++20, invoke `probe`, return true on parse success.
bool run_on(std::string_view src, Probe probe) {
    FnFactory factory(std::move(probe));
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), std::string(src), args, "adjoint_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false — snippet failed to "
                     "parse\n");
    }
    return ok;
}

void test_for_decl_null_rejects() {
    // Construct a real ASTContext so we can hand real
    // SourceManager / LangOptions references — the null-decl fast
    // path short-circuits before dereferencing either.
    std::vector<QOperation> ops;
    bool ran = run_on("", [&](clang::ASTContext& ctx) {
        AdjointEmissionResult r = emit_adjoint_for_decl(
            nullptr, ops, ctx.getSourceManager(), ctx.getLangOpts());
        CHECK_FALSE(r.synthesized);
        CHECK(r.reason == AdjointRejectReason::NullDecl);
        CHECK(r.source.empty());
        CHECK(r.adjoint_name.empty());
    });
    CHECK(ran);
}

void test_for_decl_not_reversible_rejects() {
    // Forward without the `[[sturm::reversible]]` marker → refuse.
    // R-A is opt-in; we never emit for a routine the user did not
    // mark.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
void plain(qbool& a) {}
)CPP";
    std::vector<QOperation> ops;
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("plain");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;
        AdjointEmissionResult r = emit_adjoint_for_decl(
            f.found(), ops, ctx.getSourceManager(), ctx.getLangOpts());
        CHECK_FALSE(r.synthesized);
        CHECK(r.reason == AdjointRejectReason::NotReversible);
    });
    CHECK(ran);
}

void test_for_decl_forward_declaration_rejects() {
    // Declaration without a definition → `NoBody`. R-C may dispatch
    // R-A on a decl whose definition lives elsewhere; we reject,
    // not crash.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
[[clang::annotate("sturm::reversible")]]
void missing(qbool& a);
)CPP";
    std::vector<QOperation> ops;
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("missing");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;
        AdjointEmissionResult r = emit_adjoint_for_decl(
            f.found(), ops, ctx.getSourceManager(), ctx.getLangOpts());
        CHECK_FALSE(r.synthesized);
        CHECK(r.reason == AdjointRejectReason::NoBody);
    });
    CHECK(ran);
}

void test_for_decl_synthesizes_reverse_order_body() {
    // End-to-end: a reversible forward with a two-statement body
    // produces an adjoint that matches the reverse-order walk.
    //
    // Forward:
    //   [[clang::annotate("sturm::reversible")]]
    //   void marked(qbool& r, qbool a, qbool b) {
    //       r ^= a;          // XOR_ASSIGN
    //       r = ~r;           // NOT
    //   }
    //
    // Expected adjoint body (reverse order):
    //   r = ~r;
    //   r ^= a;
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
[[clang::annotate("sturm::reversible")]]
void marked(qbool& r, qbool a, qbool b) {}
)CPP";
    std::vector<QOperation> ops{
        op1(QOpKind::XOR_ASSIGN, "r", "a"),
        op1(QOpKind::NOT, "r", "unused"),
    };
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("marked");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;
        AdjointEmissionResult r = emit_adjoint_for_decl(
            f.found(), ops, ctx.getSourceManager(), ctx.getLangOpts());
        CHECK(r.synthesized);
        CHECK(r.reason == AdjointRejectReason::None);
        CHECK_EQ_STR(r.adjoint_name, std::string("__marked_adj"));
        // Signature recovery preserves the forward's parameter list
        // verbatim: `qbool& r, qbool a, qbool b`. Body walks the ops
        // vector in reverse order.
        const std::string want =
            "void __marked_adj(qbool& r, qbool a, qbool b) {\n"
            "    r = ~r;\n"
            "    r ^= a;\n"
            "}\n";
        CHECK_EQ_STR(r.source, want);
    });
    CHECK(ran);
}

void test_for_decl_nullary_forward() {
    // Nullary forward: no parameters → the signature is empty and
    // the adjoint emits `void __nullary_adj() { ... }`. Pins the
    // edge case where `recover_parameter_texts` handles a zero-
    // parameter function without a leading comma.
    constexpr std::string_view src = R"CPP(
[[clang::annotate("sturm::reversible")]]
void nullary() {}
)CPP";
    std::vector<QOperation> ops;
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("nullary");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;
        AdjointEmissionResult r = emit_adjoint_for_decl(
            f.found(), ops, ctx.getSourceManager(), ctx.getLangOpts());
        CHECK(r.synthesized);
        CHECK(r.reason == AdjointRejectReason::None);
        CHECK_EQ_STR(r.source,
                     std::string("void __nullary_adj() {\n}\n"));
    });
    CHECK(ran);
}

// ── (6) Reason-code stringification ─────────────────────────────────────────

void test_reason_to_string_stable() {
    // The spellings are part of the public contract — callers may
    // surface them in diagnostics and tests may match on them.
    CHECK_EQ_STR(std::string(to_string(AdjointRejectReason::None)),
                 std::string("none"));
    CHECK_EQ_STR(std::string(to_string(AdjointRejectReason::NullDecl)),
                 std::string("null_decl"));
    CHECK_EQ_STR(std::string(to_string(AdjointRejectReason::NotReversible)),
                 std::string("not_reversible"));
    CHECK_EQ_STR(std::string(to_string(AdjointRejectReason::NoBody)),
                 std::string("no_body"));
    CHECK_EQ_STR(std::string(
                     to_string(AdjointRejectReason::SourceRecoveryFailed)),
                 std::string("source_recovery_failed"));
}

} // namespace

int main() {
    // (1) Reverse-order + shape coverage.
    test_reverse_order_on_three_statement_body();
    test_single_statement_body();
    test_empty_body();
    test_empty_fn_name_produces_empty_output();

    // (2) Per-primitive render coverage — one test per QOpKind that
    // `render_uncompute` understands.
    test_kind_or();
    test_kind_and();
    test_kind_not();
    test_kind_xor();
    test_kind_xor_assign();
    test_kind_add_assign_const();
    test_kind_sub_assign_const();
    test_kind_mul_assign_const();
    test_kind_div_assign_const();
    test_kind_theta_add_assign_const();
    test_kind_theta_sub_assign_const();
    test_kind_phi_add_assign_const();
    test_kind_phi_sub_assign_const();
    test_kind_add_assign_qint();
    test_kind_sub_assign_qint();
    test_kind_mul_assign_qint();
    test_kind_div_assign_qint();
    test_kind_mod_assign_qint();
    test_kind_eq_qint();
    test_kind_ne_qint();
    test_kind_lt_qint();
    test_kind_le_qint();
    test_kind_gt_qint();
    test_kind_ge_qint();
    test_kind_user_routine();
    test_kind_ccnot_inplace();
    test_kind_plugin_without_registry_elides();

    // (3) PH-3 skip parity.
    test_skip_uncompute_flag_elides_op();

    // (4) Signature passthrough.
    test_signature_passes_through_verbatim();

    // (5) FD-based entry-point coverage.
    test_for_decl_null_rejects();
    test_for_decl_not_reversible_rejects();
    test_for_decl_forward_declaration_rejects();
    test_for_decl_synthesizes_reverse_order_body();
    test_for_decl_nullary_forward();

    // (6) Reason stringification.
    test_reason_to_string_stable();

    std::fprintf(stderr,
                 "test_adjoint_emitter: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
