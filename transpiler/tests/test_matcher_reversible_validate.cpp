// test_matcher_reversible_validate.cpp — Phase P P-C (sturm-z2e8.5):
// unit tests for the P9d validation pass.
//
// The module under test walks a `[[sturm::reversible]]` forward's
// body and rejects every construct that violates one of the five
// P9d categories (measurement, classical I/O, unregistered callee,
// while-loop, quantum-dependent classical condition). Every reject
// fires through `DiagContext`'s `report_reversible_*` family. Tests
// anchor on:
//
//   1. Happy path. A clean XOR-oracle body passes validation with
//      zero diagnostics.
//
//   2. Measurement reject. A body that calls `measure_qubit(q)` or
//      casts `qbool → bool` fires one `report_reversible_measurement`.
//
//   3. Classical I/O reject. A body that calls `std::printf` fires
//      one `report_reversible_io`.
//
//   4. Unregistered callee reject. A body that calls a non-reversible,
//      non-registered function fires one
//      `report_reversible_unregistered_callee`. A call to a
//      `[[sturm::reversible]]` sibling is silent; a call to a PI-1-
//      registered forward is silent.
//
//   5. While-loop reject. A body containing `while (...)` fires one
//      `report_reversible_while_loop`. `do { ... } while (...)`
//      fires the same diagnostic.
//
//   6. Quantum-dependent condition reject. A body whose `if` / `?:`
//      condition reads a collapsed quantum value fires one
//      `report_reversible_classical_cond`.
//
// Harness posture
// ---------------
// Tests compile small C++ snippets via `runToolOnCodeWithArgs`,
// locate the named `FunctionDecl` via a RecursiveASTVisitor, build a
// standalone `DiagnosticsEngine` fronted by a counting consumer
// (mirrors `test_diag_context.cpp`), wrap it in a `DiagContext`, and
// invoke `validate_reversible_body`. Assertions check BOTH the
// returned `ReversibleValidationResult` AND the per-class counter
// totals on the consumer — so a silent "valid=true" with a hidden
// diagnostic fire does not slip through.

#include "matcher_reversible_validate.hpp"

#include "diag_context.hpp"
#include "routine_registry.hpp"

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

namespace sturm_test_matcher_reversible_validate_ns {

using sturm::transpile::DiagContext;
using sturm::transpile::ReversibleRejectReason;
using sturm::transpile::ReversibleValidationResult;
using sturm::transpile::RoutineRegistry;
using sturm::transpile::to_string;
using sturm::transpile::validate_reversible_body;

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

#define CHECK_FALSE(cond) CHECK(!(cond))

#define CHECK_EQ_INT(got, want) do {                                     \
    ++tests_run;                                                         \
    const long long g = static_cast<long long>(got);                     \
    const long long w = static_cast<long long>(want);                    \
    if (g == w) { ++tests_pass; }                                        \
    else {                                                               \
        std::fprintf(stderr, "FAIL  %s:%d  got=%lld want=%lld\n",        \
                     __FILE__, __LINE__, g, w);                          \
    }                                                                    \
} while (0)

namespace {

// ── DiagnosticsEngine fronted by a counting consumer ─────────────────
//
// Mirrors the pattern in `test_diag_context.cpp` — each report_* call
// bumps `errors` or `warnings` on the consumer. The validator's five
// report_reversible_* methods all fire at Error severity per P-D's
// contract, so the test assertions only need to compare `errors`
// against the expected fire count.
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

// Bundle the engine + counter for the test body. The engine owns the
// counter (`ShouldOwnClient=true`); the test keeps a raw pointer to
// the counter so assertions can read the totals. The pointer is
// stable for the engine's lifetime — the consumer is not replaced
// during the test run.
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

// ── AST walkers + runTool glue ──────────────────────────────────────

// Locate the first FunctionDecl in the TU whose short name matches
// `name_`. Skips template instantiations — the validator operates
// on the primary template's decl. Mirrors the finder in
// `test_reversible_attribute.cpp` / `test_matcher_reversible_drive.cpp`.
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

class ValConsumer : public clang::ASTConsumer {
public:
    explicit ValConsumer(Probe probe) : probe_(std::move(probe)) {}
    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        if (probe_) probe_(ctx);
    }
private:
    Probe probe_;
};

class ValAction : public clang::ASTFrontendAction {
public:
    explicit ValAction(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance&, llvm::StringRef) override {
        return std::make_unique<ValConsumer>(probe_);
    }
private:
    Probe probe_;
};

class ValFactory : public clang::tooling::FrontendActionFactory {
public:
    explicit ValFactory(Probe probe) : probe_(std::move(probe)) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<ValAction>(probe_);
    }
private:
    Probe probe_;
};

bool run_on(std::string_view src, Probe probe) {
    ValFactory factory(std::move(probe));
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    const bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory.create(), std::string(src), args,
        "validate_input.cpp");
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
// have.
constexpr std::string_view kQStub = R"CPP(
namespace sturm {
  class qbool {
  public:
    qbool() = default;
    qbool(const qbool&) = default;
    qbool& operator=(const qbool&) = default;
    qbool& operator^=(const qbool&) { return *this; }
    qbool& operator^=(bool) { return *this; }
    explicit operator bool() const noexcept { return false; }
  };
  class qint {
  public:
    qint() = default;
    qint(const qint&) = default;
  };
  // Helpers the test fixtures spell in their bodies.
  int measure_qubit(int);
}
using sturm::qbool;
using sturm::qint;
namespace std {
  int printf(const char*, ...);
}
)CPP";

std::string with_stub(std::string_view snippet) {
    std::string out;
    out.reserve(kQStub.size() + snippet.size());
    out.append(kQStub);
    out.append(snippet);
    return out;
}

} // anonymous namespace

// ── (A) Happy path — clean XOR body passes with zero diagnostics ────

void test_happy_path_clean_body_passes() {
    // A reversible routine whose body is a pure `^=` against a qbool
    // parameter clears every P9d class — no measurement, no I/O, no
    // unregistered callee, no while-loop, no quantum-dependent
    // branch. The validator returns `valid=true`, `reason=None`, and
    // zero diagnostics fire.
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
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK(r.valid);
        CHECK(r.reason == ReversibleRejectReason::None);
        CHECK_EQ_INT(r.diagnostics_fired, 0);
        CHECK_EQ_INT(h.counter->errors, 0);
        CHECK_EQ_INT(h.counter->warnings, 0);
    });
    CHECK(ran);
}

// ── (B) Null / attribute / no-body silent rejects ───────────────────

static void test_null_fd_silent_reject() {
    // Null input is a caller bug; the validator defends against it
    // with a silent reject (no diagnostic fires). Mirrors R-A / R-B
    // null-FD contracts.
    DiagHarness h;
    RoutineRegistry reg;
    // No ASTContext is needed on the null path — the validator
    // short-circuits before touching `ctx`. We synthesise one via a
    // one-off TU so the ctx reference is valid; the fd is null so
    // nothing gets walked.
    bool ran = run_on(std::string(kQStub), [&](clang::ASTContext& ctx) {
        ReversibleValidationResult r =
            validate_reversible_body(nullptr, ctx, h.ctx, reg);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleRejectReason::NullDecl);
        CHECK_EQ_INT(r.diagnostics_fired, 0);
        CHECK_EQ_INT(h.counter->errors, 0);
    });
    CHECK(ran);
}

static void test_non_reversible_silent_reject() {
    // A forward without `[[sturm::reversible]]` is not a synthesis
    // candidate — the validator silently rejects it. No diagnostic
    // fires. Mirrors `is_reversible`'s opt-in contract.
    const std::string src = with_stub(R"CPP(
void plain(qbool& r, qbool a) {
    r ^= a;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("plain");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleRejectReason::NotReversible);
        CHECK_EQ_INT(h.counter->errors, 0);
    });
    CHECK(ran);
}

void test_no_body_silent_reject() {
    // A forward declaration without a body cannot be validated. The
    // validator rejects silently — the caller (R-C) is expected to
    // handle forward-declared reversibles separately.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void forward_only(qbool& r, qbool a);
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("forward_only");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleRejectReason::NoBody);
        CHECK_EQ_INT(h.counter->errors, 0);
    });
    CHECK(ran);
}

// ── (C) Measurement reject ──────────────────────────────────────────

void test_reject_measurement_call() {
    // A body that calls `sturm::measure_qubit(q)` fires one
    // `report_reversible_measurement`. The result's `reason` is
    // `Measurement`. The call is itself an "unregistered callee"
    // under the narrower check, but the measurement classifier
    // runs first and wins — each node votes for exactly one reject.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, qbool a) {
    int x = sturm::measure_qubit(0);
    (void)x;
    r ^= a;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleRejectReason::Measurement);
        CHECK_EQ_INT(r.diagnostics_fired, 1);
        CHECK_EQ_INT(h.counter->errors, 1);
    });
    CHECK(ran);
}

void test_reject_measurement_cast_qbool_to_bool() {
    // The PRD's "measurement = quantum → classical conversion"
    // covers explicit `static_cast<bool>(q)` too — any collapse
    // from qbool to a builtin type. Inside a reversible body this
    // fires `report_reversible_measurement` (the stricter sibling
    // of PM3-5's `report_quantum_to_classical_cond` which only
    // fires on branch conditions).
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, qbool a) {
    bool b = static_cast<bool>(a);
    (void)b;
    r ^= a;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleRejectReason::Measurement);
        CHECK_EQ_INT(r.diagnostics_fired, 1);
        CHECK_EQ_INT(h.counter->errors, 1);
    });
    CHECK(ran);
}

// ── (D) Classical I/O reject ────────────────────────────────────────

void test_reject_classical_io_printf() {
    // `std::printf` inside a reversible body is classical I/O —
    // fires `report_reversible_io`. The classifier checks the
    // callee's qualified name against a small fixed set; `std::printf`
    // is in the set.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, qbool a) {
    std::printf("hello\n");
    r ^= a;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleRejectReason::ClassicalIO);
        CHECK_EQ_INT(r.diagnostics_fired, 1);
        CHECK_EQ_INT(h.counter->errors, 1);
    });
    CHECK(ran);
}

// ── (E) Unregistered callee reject ──────────────────────────────────

void test_reject_unregistered_callee() {
    // A call to a function that is neither `[[sturm::reversible]]`
    // nor in the `RoutineRegistry` fires
    // `report_reversible_unregistered_callee`. The reject reason
    // is `UnregisteredCallee`.
    const std::string src = with_stub(R"CPP(
void helper() {}
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, qbool a) {
    helper();
    r ^= a;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleRejectReason::UnregisteredCallee);
        CHECK_EQ_INT(r.diagnostics_fired, 1);
        CHECK_EQ_INT(h.counter->errors, 1);
    });
    CHECK(ran);
}

void test_accept_reversible_sibling_callee() {
    // A call to a `[[sturm::reversible]]` sibling is silent — the
    // callee is itself a synthesis candidate, so the validator
    // trusts it.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void helper(qbool& r, qbool a) {
    r ^= a;
}
[[clang::annotate("sturm::reversible")]]
void caller(qbool& r, qbool a) {
    helper(r, a);
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("caller");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK(r.valid);
        CHECK(r.reason == ReversibleRejectReason::None);
        CHECK_EQ_INT(r.diagnostics_fired, 0);
        CHECK_EQ_INT(h.counter->errors, 0);
    });
    CHECK(ran);
}

void test_accept_registry_bound_callee() {
    // A call to a function that has an adjoint registered in the
    // PI-1 `RoutineRegistry` is silent — the hand-registered
    // adjoint is the escape hatch per PRD §9 Q2. The validator
    // does not require the callee to carry `[[sturm::reversible]]`
    // when its adjoint is already bound.
    const std::string src = with_stub(R"CPP(
void helper(qbool& r, qbool a) { r ^= a; }
void helper_adj(qbool& r, qbool a) { r ^= a; }
[[clang::annotate("sturm::reversible")]]
void caller(qbool& r, qbool a) {
    helper(r, a);
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f_caller("caller");
        f_caller.TraverseAST(ctx);
        CHECK(f_caller.found() != nullptr);
        if (f_caller.found() == nullptr) return;

        NamedFnFinder f_helper("helper");
        f_helper.TraverseAST(ctx);
        CHECK(f_helper.found() != nullptr);
        if (f_helper.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        // Hand-register `helper` → `helper_adj`; from the
        // validator's point of view, any callee already in the
        // registry is "fine".
        reg.insert_pair(f_helper.found(), "helper_adj");

        ReversibleValidationResult r = validate_reversible_body(
            f_caller.found(), ctx, h.ctx, reg);
        CHECK(r.valid);
        CHECK(r.reason == ReversibleRejectReason::None);
        CHECK_EQ_INT(r.diagnostics_fired, 0);
        CHECK_EQ_INT(h.counter->errors, 0);
    });
    CHECK(ran);
}

// ── (F) While-loop reject ───────────────────────────────────────────

void test_reject_while_loop() {
    // A `while (cond) body` inside a reversible routine fires
    // `report_reversible_while_loop`. Rejected because unbounded
    // trip counts are not invertible by B11 loop reversal
    // (PRD §5.2).
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, qbool a) {
    int i = 0;
    while (i < 3) {
        r ^= a;
        ++i;
    }
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleRejectReason::WhileLoop);
        CHECK_EQ_INT(r.diagnostics_fired, 1);
        CHECK_EQ_INT(h.counter->errors, 1);
    });
    CHECK(ran);
}

void test_reject_do_while_loop() {
    // `do body while(cond)` also fires
    // `report_reversible_while_loop` — `DoStmt` is a `WhileStmt`
    // with a post-condition; the B11 reversal guarantee is the
    // same "unbounded trip count" violation.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, qbool a) {
    int i = 0;
    do {
        r ^= a;
        ++i;
    } while (i < 3);
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleRejectReason::WhileLoop);
        CHECK_EQ_INT(r.diagnostics_fired, 1);
        CHECK_EQ_INT(h.counter->errors, 1);
    });
    CHECK(ran);
}

void test_accept_for_loop() {
    // A `for` loop with a canonical header is NOT a while-loop
    // reject (Phase S handles it). The validator stays silent on
    // `for` — the body's reads / writes inside the loop are
    // recursively validated (a non-canonical for-shape might later
    // surface as a different reject at the S-A consumer).
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void ok(qbool& r, qbool a) {
    for (int i = 0; i < 3; ++i) {
        r ^= a;
    }
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("ok");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK(r.valid);
        CHECK(r.reason == ReversibleRejectReason::None);
        CHECK_EQ_INT(r.diagnostics_fired, 0);
    });
    CHECK(ran);
}

// ── (G) Quantum-dependent classical condition reject ────────────────

void test_reject_quantum_dependent_cond_via_measure_call() {
    // `if (measure_qubit(q))` — the condition is the return of a
    // measurement call. The validator fires
    // `report_reversible_measurement` on the call itself (step 1
    // of the walk); the `if` that reads a measured value is the
    // downstream classical-cond shape. The first reject wins on
    // `.reason`; the measurement reject wins here because the
    // CallExpr visits first.
    //
    // A second shape that fires classical-cond specifically: a
    // branch whose condition is an expression whose static type
    // resolves to a quantum type (pre-measurement). Checked by the
    // next test.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, qbool a) {
    if (sturm::measure_qubit(0)) {
        r ^= a;
    }
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK_FALSE(r.valid);
        // Measurement fires first (on the inner CallExpr). At least
        // one diagnostic fires; the exact count depends on how the
        // walk orders `measure_qubit` and the `if` classical-cond.
        CHECK(r.reason == ReversibleRejectReason::Measurement ||
              r.reason == ReversibleRejectReason::ClassicalCond);
        CHECK(r.diagnostics_fired >= 1);
        CHECK(h.counter->errors >= 1);
    });
    CHECK(ran);
}

void test_reject_classical_cond_on_implicit_udc() {
    // Phase T T-4 (sturm-xrob.5): `if (q)` where qbool has a
    // NON-EXPLICIT `operator bool()` — Clang wraps the cond in
    // `ImplicitCastExpr<UserDefinedConversion>(CXXMemberCallExpr(
    //  q.operator bool()))`. The classical-cond check must peel
    // through the UDC member call to recover the quantum source
    // and fire. Pinned by the fixture
    // `reversible_reject_classical_cond.cpp` whose CTest flips to
    // MUST_CONTAIN in this issue.
    //
    // Note: the shared test stub declares `operator bool()` as
    // EXPLICIT, so this test needs its own qbool definition with
    // a non-explicit conversion. We assemble the snippet inline.
    constexpr std::string_view kImplicitStub = R"CPP(
namespace sturm {
class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    operator bool() const noexcept { return false; }
};
}
using sturm::qbool;
)CPP";
    std::string src;
    src.reserve(kImplicitStub.size() + 128);
    src.append(kImplicitStub);
    src.append(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, qbool a) {
    if (a) {
        r ^= a;
    }
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleRejectReason::ClassicalCond);
        CHECK(r.diagnostics_fired >= 1);
        CHECK(h.counter->errors >= 1);
    });
    CHECK(ran);
}

void test_reject_classical_cond_on_qbool_cast() {
    // `if ((bool)q)` inside a reversible body is a classical
    // condition derived from a quantum value. The cast fires the
    // measurement classifier (qbool → bool), and the `if` the
    // quantum-dep-cond classifier. At least one diagnostic fires
    // and `valid` is false. Pins the dual-classification behaviour.
    const std::string src = with_stub(R"CPP(
[[clang::annotate("sturm::reversible")]]
void bad(qbool& r, qbool a) {
    if ((bool)a) {
        r ^= a;
    }
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleRejectReason::Measurement ||
              r.reason == ReversibleRejectReason::ClassicalCond);
        CHECK(r.diagnostics_fired >= 1);
        CHECK(h.counter->errors >= 1);
    });
    CHECK(ran);
}

// ── (H) Exactly-one-diagnostic-per-offending-construct ──────────────

void test_each_diagnostic_fires_exactly_once_per_construct() {
    // Pin the "one diagnostic per offending construct" invariant
    // — two unrelated unregistered callees fire two diagnostics
    // (not one); a while-loop + an unregistered callee fire two
    // diagnostics (one each).
    const std::string src = with_stub(R"CPP(
void helper_a() {}
void helper_b() {}
[[clang::annotate("sturm::reversible")]]
void two_bad(qbool& r, qbool a) {
    helper_a();
    helper_b();
    r ^= a;
}
)CPP");
    bool ran = run_on(src, [](clang::ASTContext& ctx) {
        NamedFnFinder f("two_bad");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        RoutineRegistry reg;
        ReversibleValidationResult r =
            validate_reversible_body(f.found(), ctx, h.ctx, reg);
        CHECK_FALSE(r.valid);
        CHECK(r.reason == ReversibleRejectReason::UnregisteredCallee);
        CHECK_EQ_INT(r.diagnostics_fired, 2);
        CHECK_EQ_INT(h.counter->errors, 2);
    });
    CHECK(ran);
}

static void test_to_string_enum_spellings() {
    // Pin the human-readable spellings. Tests compare against
    // these strings.
    CHECK(to_string(ReversibleRejectReason::None) ==
          std::string_view("none"));
    CHECK(to_string(ReversibleRejectReason::NullDecl) ==
          std::string_view("null_decl"));
    CHECK(to_string(ReversibleRejectReason::NotReversible) ==
          std::string_view("not_reversible"));
    CHECK(to_string(ReversibleRejectReason::NoBody) ==
          std::string_view("no_body"));
    CHECK(to_string(ReversibleRejectReason::Measurement) ==
          std::string_view("measurement"));
    CHECK(to_string(ReversibleRejectReason::ClassicalIO) ==
          std::string_view("classical_io"));
    CHECK(to_string(ReversibleRejectReason::UnregisteredCallee) ==
          std::string_view("unregistered_callee"));
    CHECK(to_string(ReversibleRejectReason::WhileLoop) ==
          std::string_view("while_loop"));
    CHECK(to_string(ReversibleRejectReason::ClassicalCond) ==
          std::string_view("classical_cond"));
}

// ── main ────────────────────────────────────────────────────────────

}  // namespace sturm_test_matcher_reversible_validate_ns

int run_test_matcher_reversible_validate(int /*argc*/, char** /*argv*/) {
    using namespace sturm_test_matcher_reversible_validate_ns;
    using sturm_test_matcher_reversible_validate_ns::tests_run;
    using sturm_test_matcher_reversible_validate_ns::tests_pass;
    // Happy path.
    test_happy_path_clean_body_passes();
    // Silent rejects.
    test_null_fd_silent_reject();
    test_non_reversible_silent_reject();
    test_no_body_silent_reject();
    // Measurement.
    test_reject_measurement_call();
    test_reject_measurement_cast_qbool_to_bool();
    // Classical I/O.
    test_reject_classical_io_printf();
    // Unregistered callee.
    test_reject_unregistered_callee();
    test_accept_reversible_sibling_callee();
    test_accept_registry_bound_callee();
    // While / do-while / for.
    test_reject_while_loop();
    test_reject_do_while_loop();
    test_accept_for_loop();
    // Quantum-dependent condition.
    test_reject_quantum_dependent_cond_via_measure_call();
    test_reject_classical_cond_on_qbool_cast();
    test_reject_classical_cond_on_implicit_udc();
    // Aggregate invariants.
    test_each_diagnostic_fires_exactly_once_per_construct();
    test_to_string_enum_spellings();

    std::fprintf(stderr,
                 "test_matcher_reversible_validate: %d / %d checks "
                 "passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
