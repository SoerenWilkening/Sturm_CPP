// test_matcher_reversible_signature.cpp — Phase Q Q-B (sturm-5kgu.3):
// unit tests for the parameter-list signature enforcement pass.
//
// The module under test walks the parameter list of every
// `[[sturm::reversible]]` forward and rejects three Q-B classes:
//
//   1. Pass-by-pointer of a quantum type (`qbool*`).
//   2. Non-const by-value quantum parameter mutated in the body.
//   3. `const`-qualified reference-to-quantum parameter mutated in
//      the body.
//
// Each reject fires through `DiagContext`'s
// `report_reversible_*_param*` family. Tests anchor on:
//
//   1. Happy paths. Canonical shapes (`qbool&`, `const qbool&`,
//      by-value without mutation, mixed) pass with zero diagnostics.
//
//   2. Pointer reject. `void fn(qbool* x)` fires one
//      `report_reversible_pointer_param`. Mutation of the pointer
//      target is irrelevant — the pointer is illegal on its own.
//
//   3. By-value mutation reject. `void fn(qbool x) { x ^= a; }`
//      fires one `report_reversible_value_param_mutated`. A
//      `const qbool x` by-value parameter (even if mutation looked
//      like it would fire C++ error) is silent; a by-value parameter
//      whose body does NOT mutate is silent.
//
//   4. Const-ref mutation reject. `void fn(const qbool& x) { x ^= a; }`
//      fires one `report_reversible_const_ref_mutated`. The body's
//      mutation is identified via the same assignment-shape /
//      inc-dec set the PM3-4 WHEN-operand matcher uses.
//
//   5. Null / non-reversible silent skips. A null FD or an FD
//      lacking the attribute is rejected silently — no diagnostic
//      fires, mirroring the P-C validator's opt-in contract.
//
//   6. Aggregate invariants. Multiple bad parameters each fire one
//      diagnostic; the `reason` field holds the FIRST reject.
//
//   7. `to_string` spellings. Pin the machine-readable enum names
//      so tests can compare against stable strings.
//
// Harness posture
// ---------------
// Mirrors `test_matcher_reversible_validate.cpp`: compile small C++
// snippets via `runToolOnCodeWithArgs`, locate the named
// `FunctionDecl`, build a standalone `DiagnosticsEngine` fronted by
// a counting consumer, wrap it in a `DiagContext`, and invoke
// `validate_reversible_signature`. Assertions check BOTH the
// returned `ReversibleSignatureResult` AND the per-class counter
// totals on the consumer — so a silent "valid=true" with a hidden
// diagnostic fire does not slip through.

#include "matcher_reversible_signature.hpp"

#include "diag_context.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using sturm::transpile::DiagContext;
using sturm::transpile::ReversibleSigRejectReason;
using sturm::transpile::ReversibleSignatureResult;
using sturm::transpile::to_string;
using sturm::transpile::validate_reversible_signature;

// ── Test harness ───────────────────────────────────────────────────
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

#define CHECK_FALSE(cond) CHECK(!(cond))

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

// ── DiagnosticsEngine fronted by a counting consumer ───────────────
//
// Mirrors `test_diag_context.cpp` / `test_matcher_reversible_validate
// .cpp` — each report_* call bumps `errors` or `warnings`. The Q-B
// validator's three report methods all fire at Error severity, so
// the test assertions only need to compare `errors` against the
// expected fire count.
class CountingDiagConsumer final : public clang::DiagnosticConsumer {
public:
    unsigned warnings = 0;
    unsigned errors   = 0;
    void HandleDiagnostic(clang::DiagnosticsEngine::Level lvl,
                          const clang::Diagnostic& /*info*/) override {
        if (lvl >= clang::DiagnosticsEngine::Error) {
            ++errors;
        } else if (lvl == clang::DiagnosticsEngine::Warning) {
            ++warnings;
        }
    }
};

struct DiagHarness {
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs>     ids;
    llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> opts;
    CountingDiagConsumer*                              counter;
    clang::DiagnosticsEngine                           engine;
    DiagContext                                        ctx;
    DiagHarness()
        : ids(new clang::DiagnosticIDs()),
          opts(new clang::DiagnosticOptions()),
          counter(new CountingDiagConsumer()),
          engine(ids, opts.get(), counter, /*ShouldOwnClient=*/true),
          ctx(engine) {}
};

// ── AST walkers + runTool glue ─────────────────────────────────────

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

using Probe = std::function<void(clang::ASTContext&)>;

class SigConsumer : public clang::ASTConsumer {
public:
    explicit SigConsumer(Probe probe) : probe_(std::move(probe)) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        if (probe_) probe_(ctx);
    }
private:
    Probe probe_;
};

class SigAction : public clang::ASTFrontendAction {
public:
    explicit SigAction(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<SigConsumer>(probe_);
    }
private:
    Probe probe_;
};

class SigFactory : public clang::tooling::FrontendActionFactory {
public:
    explicit SigFactory(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<SigAction>(probe_);
    }
private:
    Probe probe_;
};

bool run_on(std::string_view src, Probe probe) {
    SigFactory factory(std::move(probe));
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), std::string(src), args,
        "signature_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false — snippet failed to "
                     "parse\n");
    }
    return ok;
}

// Shared stub header — every test TU that uses `sturm::qbool` /
// `sturm::qint` needs at least a trivial class shape for the parser
// to accept the source. We define them in the anonymous `sturm`
// namespace so the validator sees the same `CXXRecordDecl` names
// (`qbool`, `qint`, `qint_t`) the production include tree would
// have. Unlike the P-C stubs, `qbool&` and `qint&` must support
// compound-assign ops so the mutation detector has something to
// fire on.
constexpr std::string_view kQStub = R"CPP(
namespace sturm {
  class qbool {
  public:
    qbool() = default;
    qbool(const qbool&) = default;
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    qbool& operator^=(bool) { return *this; }
    // Intentional const-violation helpers: a user-defined conversion
    // operator that hides the const away. The Q-B validator's
    // mutation detector must still fire on the ^= call applied to
    // the const-ref parameter.
  };
  class qint {
  public:
    qint() = default;
    qint(const qint&) = default;
    qint& operator=(const qint&) { return *this; }
    qint& operator^=(const qint&) { return *this; }
    qint& operator+=(int) { return *this; }
  };
}
using sturm::qbool;
using sturm::qint;
)CPP";

std::string with_stub(std::string_view snippet) {
    std::string out;
    out.reserve(kQStub.size() + snippet.size());
    out.append(kQStub);
    out.append(snippet);
    return out;
}

} // anonymous namespace

// ── (A) Happy paths ────────────────────────────────────────────────

void test_happy_path_non_const_ref() {
    // `void fn(qbool& r, qbool a)` with body `r ^= a;` — canonical
    // out-param shape. `r` is non-const ref (mutable, adjoint tracks
    // it); `a` is by-value but not mutated. Zero diagnostics fire.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void oracle(qbool& r, qbool a) {
    r ^= a;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("oracle");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::None);
        CHECK_EQ_INT(r.diagnostics_fired, 0);
        CHECK_EQ_INT(h.counter->errors, 0);
    });
    CHECK(ran);
}

void test_happy_path_const_ref_not_mutated() {
    // `const qbool&` parameter is read-only and the body does not
    // mutate it — canonical read-only shape. Zero diagnostics fire.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void oracle(qbool& r, const qbool& a) {
    r ^= a;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("oracle");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::None);
        CHECK_EQ_INT(r.diagnostics_fired, 0);
    });
    CHECK(ran);
}

void test_happy_path_value_param_not_mutated() {
    // By-value `qbool a` is not mutated inside the body — P9b says
    // this is legal (a read-only copy). Zero diagnostics fire.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void oracle(qbool& r, qbool a) {
    r ^= a;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("oracle");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::None);
        CHECK_EQ_INT(r.diagnostics_fired, 0);
    });
    CHECK(ran);
}

void test_happy_path_non_quantum_classical_params() {
    // Classical parameters (`int`, `double`, `bool`) are outside
    // Q-B's reach — they are tolerated in every shape, including
    // pointer and const-ref with mutation-like expressions. Zero
    // diagnostics fire.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void oracle(qbool& r, const qbool& a, int* counter, int n) {
    r ^= a;
    *counter = n;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("oracle");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::None);
        CHECK_EQ_INT(r.diagnostics_fired, 0);
    });
    CHECK(ran);
}

void test_happy_path_no_params() {
    // Zero parameters — trivially valid. The walker must not crash on
    // an empty parameter list.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void nothing() {}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("nothing");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::None);
        CHECK_EQ_INT(r.diagnostics_fired, 0);
    });
    CHECK(ran);
}

// ── (B) Silent rejects ─────────────────────────────────────────────

void test_null_fd_silent_reject() {
    // Null input is a caller bug; the validator defends against it
    // with a silent reject (no diagnostic fires).
    DiagHarness h;
    bool ran = run_on(std::string(kQStub), [&](clang::ASTContext& ctx) {
        ReversibleSignatureResult r =
            validate_reversible_signature(nullptr, ctx, h.ctx);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::NullDecl);
        CHECK_EQ_INT(r.diagnostics_fired, 0);
        CHECK_EQ_INT(h.counter->errors, 0);
    });
    CHECK(ran);
}

void test_non_reversible_silent_reject() {
    // A forward without `[[sturm::reversible]]` is not a synthesis
    // candidate — the validator silently rejects it. No diagnostic
    // fires even on a signature that would otherwise trip Q-B.
    const std::string src = with_stub(R"CPP(
void plain(qbool* r, qbool a) {
    (void)r; (void)a;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("plain");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::NotReversible);
        CHECK_EQ_INT(h.counter->errors, 0);
    });
    CHECK(ran);
}

// ── (C) Pointer reject ─────────────────────────────────────────────

void test_reject_pointer_qbool_param() {
    // `void fn(qbool* x, ...)` — pointer to quantum is unconditionally
    // rejected. Fires `report_reversible_pointer_param` once.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool* r, qbool a) {
    (void)r; (void)a;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::PointerParam);
        CHECK_EQ_INT(r.diagnostics_fired, 1);
        CHECK_EQ_INT(h.counter->errors, 1);
    });
    CHECK(ran);
}

void test_reject_pointer_qint_param() {
    // `qint*` follows the same rule — any pointer to a quantum
    // record is rejected.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, qint* x) {
    (void)r; (void)x;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::PointerParam);
        CHECK_EQ_INT(r.diagnostics_fired, 1);
    });
    CHECK(ran);
}

void test_reject_const_pointer_to_qbool() {
    // `const qbool*` is still a pointer — the const qualifier on the
    // pointee does not buy us anything. Rejected as PointerParam.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, const qbool* a) {
    (void)r; (void)a;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::PointerParam);
        CHECK_EQ_INT(r.diagnostics_fired, 1);
    });
    CHECK(ran);
}

// ── (D) By-value mutation reject ───────────────────────────────────

void test_reject_value_param_mutated_compound_assign() {
    // `void fn(..., qbool x) { x ^= a; }` — mutation of a by-value
    // non-const quantum parameter. Fires
    // `report_reversible_value_param_mutated`.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, qbool x) {
    x ^= r;
    (void)x;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::ValueParamMutated);
        CHECK_EQ_INT(r.diagnostics_fired, 1);
        CHECK_EQ_INT(h.counter->errors, 1);
    });
    CHECK(ran);
}

void test_reject_value_param_mutated_plain_assign() {
    // Plain copy-assign `x = y;` on a by-value quantum parameter is
    // also a mutation. Fires `report_reversible_value_param_mutated`.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, qbool x, qbool y) {
    x = y;
    r ^= x;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::ValueParamMutated);
        CHECK_EQ_INT(r.diagnostics_fired, 1);
    });
    CHECK(ran);
}

void test_accept_const_value_param() {
    // `const qbool x` — a by-value `const` quantum parameter is a
    // read-only copy. Cannot be mutated by well-formed C++; even if
    // user-defined conversion gymnastics made it syntactically
    // possible, the `const` qualifier communicates the user's
    // immutability intent. We do not fire Q-B on const-by-value
    // params regardless of body shape.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void ok(qbool& r, const qbool x) {
    r ^= x;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("ok");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::None);
        CHECK_EQ_INT(r.diagnostics_fired, 0);
    });
    CHECK(ran);
}

// ── (E) Const-ref mutation reject ──────────────────────────────────

void test_reject_const_ref_mutated() {
    // `void fn(..., const qbool& x) { x ^= y; }` — mutation of a
    // const-qualified reference-to-quantum. Normally a C++ error, but
    // we defend against user-defined conversion / overload shapes by
    // firing `report_reversible_const_ref_mutated` at the reversible
    // routine definition site.
    //
    // The test stub's `qbool::operator^=` is non-const, so a naive
    // `x ^= y;` against a `const qbool&` would fail to compile. We
    // exercise the Q-B matcher's detection path by shaping the
    // assignment through an explicit cast that drops the const away
    // — a pattern users might author in a custom quantum type. The
    // Q-B matcher treats the AST `CXXOperatorCallExpr` on the
    // const-ref parameter's referenced VarDecl as a mutation
    // regardless of whether a `const_cast` was needed to make it
    // compile; the VarDecl itself is const-ref, which is what Q-B
    // pins.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, const qbool& x) {
    // const_cast path — the user-side gymnastics the Q-B matcher
    // defends against. The referenced VarDecl is const-ref.
    const_cast<qbool&>(x) ^= r;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleSigRejectReason::ConstRefParamMutated);
        CHECK_EQ_INT(r.diagnostics_fired, 1);
        CHECK_EQ_INT(h.counter->errors, 1);
    });
    CHECK(ran);
}

// ── (F) Aggregate invariants ───────────────────────────────────────

void test_two_bad_params_two_diagnostics() {
    // Two independently-offending parameters each fire one
    // diagnostic. The `reason` field holds the FIRST reject (walk
    // order is parameter-list order).
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void two_bad(qbool* p, qbool x) {
    (void)p;
    x ^= x;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("two_bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK_FALSE(r.valid);
        // The first parameter (`qbool* p`) fires PointerParam first.
        CHECK(r.reason == ReversibleSigRejectReason::PointerParam);
        CHECK_EQ_INT(r.diagnostics_fired, 2);
        CHECK_EQ_INT(h.counter->errors, 2);
    });
    CHECK(ran);
}

void test_value_param_mutation_does_not_also_fire_pointer() {
    // Pin the "each parameter votes for at most one reject reason"
    // invariant — a by-value mutated qbool fires ValueParamMutated
    // only (not PointerParam + ValueParamMutated).
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, qbool x) {
    x ^= r;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        ReversibleSignatureResult r =
            validate_reversible_signature(f.found(), ctx, h.ctx);
        CHECK_FALSE(r.valid);
        CHECK_EQ_INT(r.diagnostics_fired, 1);
    });
    CHECK(ran);
}

// ── (G) to_string spellings ────────────────────────────────────────

void test_to_string_enum_spellings() {
    // Pin the human-readable spellings. Tests compare against these
    // strings.
    CHECK(to_string(ReversibleSigRejectReason::None) ==
          std::string_view("none"));
    CHECK(to_string(ReversibleSigRejectReason::NullDecl) ==
          std::string_view("null_decl"));
    CHECK(to_string(ReversibleSigRejectReason::NotReversible) ==
          std::string_view("not_reversible"));
    CHECK(to_string(ReversibleSigRejectReason::PointerParam) ==
          std::string_view("pointer_param"));
    CHECK(to_string(ReversibleSigRejectReason::ValueParamMutated) ==
          std::string_view("value_param_mutated"));
    CHECK(to_string(ReversibleSigRejectReason::ConstRefParamMutated) ==
          std::string_view("const_ref_param_mutated"));
}

// ── main ───────────────────────────────────────────────────────────

int main() {
    // Happy paths.
    test_happy_path_non_const_ref();
    test_happy_path_const_ref_not_mutated();
    test_happy_path_value_param_not_mutated();
    test_happy_path_non_quantum_classical_params();
    test_happy_path_no_params();
    // Silent rejects.
    test_null_fd_silent_reject();
    test_non_reversible_silent_reject();
    // Pointer.
    test_reject_pointer_qbool_param();
    test_reject_pointer_qint_param();
    test_reject_const_pointer_to_qbool();
    // By-value mutation.
    test_reject_value_param_mutated_compound_assign();
    test_reject_value_param_mutated_plain_assign();
    test_accept_const_value_param();
    // Const-ref mutation.
    test_reject_const_ref_mutated();
    // Aggregate invariants.
    test_two_bad_params_two_diagnostics();
    test_value_param_mutation_does_not_also_fire_pointer();
    // Enum spellings.
    test_to_string_enum_spellings();

    std::fprintf(stderr,
                 "test_matcher_reversible_signature: %d / %d checks "
                 "passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
