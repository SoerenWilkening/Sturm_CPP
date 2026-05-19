// test_matcher_reversible_drive.cpp — Phase R R-C (sturm-88d7.4):
// unit tests for the top-level `drive_reversible` entry point.
//
// The module under test is a thin orchestrator that sequences R-A
// (`emit_adjoint_for_decl`) + R-B (`emit_auto_registration_for_decl`)
// for a single `[[sturm::reversible]]` forward routine, gated by
// swappable P-C / Q-B validator hooks and the PRD §9 Q2 hand-
// registration conflict check. Three properties anchor the test:
//
//   1. Happy path. A reversible forward with stubbed validators that
//      return `true` produces an adjoint body + registration line on
//      the `SynthesisRegistry` entry and transitions the entry's
//      status to `Emitted`.
//
//   2. Rejection path. A stubbed validator that returns `false`
//      short-circuits the pipeline: no adjoint source is emitted,
//      no registration line is written, and the entry transitions
//      to `Failed`.
//
//   3. No-op path. A non-reversible forward (missing `[[sturm::
//      reversible]]`) is skipped — the driver returns
//      `NotReversible`, and the registry is unchanged.
//
// Two additional auxiliary cases pin the null / missing-entry /
// hand-registration-conflict reject paths the contract documents
// explicitly. These are narrower than the three core scenarios but
// part of the same surface.
//
// Harness posture
// ---------------
// The tests compile small C++ snippets via `runToolOnCodeWithArgs`,
// locate the relevant `FunctionDecl` via a RecursiveASTVisitor, seed
// a `SynthesisRegistry` + `RoutineRegistry` by hand, and invoke
// `drive_reversible`. Hand-built `QOperation` vectors are reused from
// the `test_adjoint_emitter` pattern — the driver does not introspect
// ops, it simply forwards them to R-A.

#include "matcher_reversible_drive.hpp"

#include "adjoint_emitter.hpp"
#include "diag_context.hpp"
#include "routine_registry.hpp"
#include "synthesis_registry.hpp"

#include "sturm/transpile/qir.hpp"

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
#include "llvm/ADT/IntrusiveRefCntPtr.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using sturm::transpile::collect_invert_call_targets;
using sturm::transpile::DiagContext;
using sturm::transpile::drive_reversible;
using sturm::transpile::drive_reversible_forwards;
using sturm::transpile::DriveOptions;
using sturm::transpile::DriveRejectReason;
using sturm::transpile::DriveResult;
using sturm::transpile::QOperation;
using sturm::transpile::QOpKind;
using sturm::transpile::QUnit;
using sturm::transpile::QValueRef;
using sturm::transpile::RoutineRegistry;
using sturm::transpile::SynthesisEntry;
using sturm::transpile::SynthesisRegistry;
using sturm::transpile::SynthesisStatus;
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

// Fabricate a single-operand QOperation (used for XOR_ASSIGN / NOT).
// The driver does not introspect ops — it simply forwards the vector
// to R-A. Using the same op-factory helpers as `test_adjoint_emitter`
// keeps the test inputs recognisable to anyone reading both suites.
QOperation op1(QOpKind kind, const char* result, const char* operand) {
    QOperation op;
    op.kind = kind;
    op.result = QValueRef{std::string(result), make_loc(1)};
    op.operands.push_back(QValueRef{std::string(operand), make_loc(2)});
    return op;
}

// Fabricate a two-operand QOperation (OR / AND / XOR).
QOperation op2(QOpKind kind, const char* result,
               const char* operand0, const char* operand1) {
    QOperation op;
    op.kind = kind;
    op.result = QValueRef{std::string(result), make_loc(1)};
    op.operands.push_back(QValueRef{std::string(operand0), make_loc(2)});
    op.operands.push_back(QValueRef{std::string(operand1), make_loc(3)});
    return op;
}

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
// `name_`. Skips template instantiations. Mirrors the finder shape
// `test_adjoint_emitter` / `test_auto_register_emitter` use.
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
        factory.create(), std::string(src), args, "drive_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false — snippet failed to "
                     "parse\n");
    }
    return ok;
}

// Stand-in validators. These are the "swappable hook" interface
// documented in the header — P-C (sturm-z2e8.5) and Q-B
// (sturm-5kgu.3) will populate real versions once they land, but
// the driver must be testable today with these cheap lambdas.
auto always_valid = [](const clang::FunctionDecl*) { return true; };
auto always_invalid = [](const clang::FunctionDecl*) { return false; };

// ── (1) Happy path ──────────────────────────────────────────────────────────

void test_happy_path_populates_registry() {
    // A reversible forward, paired with hand-built QOperation records
    // the matcher would have produced, drives through the full R-A
    // + R-B pipeline:
    //   - `drive_reversible` returns `emitted=true`,
    //   - the `SynthesisRegistry` entry picks up the adjoint body
    //     text AND the registration line concatenated into
    //     `adjoint_source`,
    //   - `adjoint_name` is `__marked_adj`,
    //   - the entry's `status` is `Emitted`.
    //
    // The stubbed validators always return true, matching the
    // no-constraint default the driver uses before P-C and Q-B
    // land. The same test will pass unchanged once the real
    // validators are wired in (assuming the body / signature shape
    // genuinely validates).
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

        SynthesisRegistry synth;
        RoutineRegistry routines;
        synth.insert_forward(f.found());

        DriveOptions opts;
        opts.body_validator      = always_valid;
        opts.signature_validator = always_valid;

        DriveResult r = drive_reversible(
            f.found(), ops, synth, routines,
            ctx.getSourceManager(), ctx.getLangOpts(), opts);

        CHECK(r.emitted);
        CHECK(r.reason == DriveRejectReason::None);
        CHECK_EQ_STR(r.adjoint_name, std::string("__marked_adj"));

        const SynthesisEntry* entry = synth.lookup(f.found());
        CHECK(entry != nullptr);
        if (entry == nullptr) return;
        CHECK(entry->status == SynthesisStatus::Emitted);
        CHECK_EQ_STR(entry->adjoint_name, std::string("__marked_adj"));
        // The combined payload is the R-A adjoint body followed by
        // the R-B registration line — the driver concatenates them
        // so downstream consumers see a single emitted blob per
        // forward.
        const std::string want =
            "void __marked_adj(qbool& r, qbool a, qbool b) {\n"
            "    r = ~r;\n"
            "    r ^= a;\n"
            "}\n"
            "STURM_REGISTER_ADJOINT(marked, __marked_adj);\n";
        CHECK_EQ_STR(entry->adjoint_source, want);
    });
    CHECK(ran);
}

// ── (2) Rejection path ──────────────────────────────────────────────────────

void test_rejection_path_marks_failed_and_no_adjoint() {
    // A reversible forward whose body validator returns false —
    // standing in for a P-C rejection. The driver must:
    //   - short-circuit before R-A,
    //   - return `reason=ValidationFailed`,
    //   - leave `adjoint_source` / `adjoint_name` empty on the
    //     registry entry,
    //   - transition the entry's status to `Failed`.
    //
    // This proves the swappable validator hook actually gates the
    // pipeline — when P-C / Q-B land, their false returns behave
    // identically without any driver-side changes.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
[[clang::annotate("sturm::reversible")]]
void marked(qbool& r, qbool a) {}
)CPP";
    std::vector<QOperation> ops{
        op1(QOpKind::XOR_ASSIGN, "r", "a"),
    };
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("marked");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        SynthesisRegistry synth;
        RoutineRegistry routines;
        synth.insert_forward(f.found());

        DriveOptions opts;
        opts.body_validator      = always_invalid;
        opts.signature_validator = always_valid;

        DriveResult r = drive_reversible(
            f.found(), ops, synth, routines,
            ctx.getSourceManager(), ctx.getLangOpts(), opts);

        CHECK_FALSE(r.emitted);
        CHECK(r.reason == DriveRejectReason::ValidationFailed);
        CHECK(r.adjoint_name.empty());

        const SynthesisEntry* entry = synth.lookup(f.found());
        CHECK(entry != nullptr);
        if (entry == nullptr) return;
        CHECK(entry->status == SynthesisStatus::Failed);
        CHECK(entry->adjoint_name.empty());
        CHECK(entry->adjoint_source.empty());
    });
    CHECK(ran);
}

void test_rejection_path_signature_validator() {
    // Symmetric: the signature validator (Q-B's hook) also gates
    // the pipeline. Pins that both hooks short-circuit through the
    // same code path. When one validator passes and the other
    // fails, the failing one wins — the driver does NOT require
    // both hooks to fire.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
[[clang::annotate("sturm::reversible")]]
void marked(qbool& r) {}
)CPP";
    std::vector<QOperation> ops;
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("marked");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        SynthesisRegistry synth;
        RoutineRegistry routines;
        synth.insert_forward(f.found());

        DriveOptions opts;
        opts.body_validator      = always_valid;
        opts.signature_validator = always_invalid;

        DriveResult r = drive_reversible(
            f.found(), ops, synth, routines,
            ctx.getSourceManager(), ctx.getLangOpts(), opts);

        CHECK_FALSE(r.emitted);
        CHECK(r.reason == DriveRejectReason::ValidationFailed);

        const SynthesisEntry* entry = synth.lookup(f.found());
        CHECK(entry != nullptr);
        if (entry == nullptr) return;
        CHECK(entry->status == SynthesisStatus::Failed);
    });
    CHECK(ran);
}

// ── (3) No-op path (non-reversible FD) ──────────────────────────────────────

void test_non_reversible_fd_is_noop() {
    // A plain forward without `[[sturm::reversible]]` is not a
    // synthesis candidate. The driver returns `NotReversible`
    // without touching the registry — every R-C call on a non-
    // reversible decl is a silent skip. This keeps the
    // `TranspileConsumer` call site cheap: it can iterate every
    // FunctionDecl the matcher surfaces without pre-filtering.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
void plain(qbool& r, qbool a) {}
)CPP";
    std::vector<QOperation> ops{
        op1(QOpKind::XOR_ASSIGN, "r", "a"),
    };
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("plain");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        SynthesisRegistry synth;
        RoutineRegistry routines;
        // No `insert_forward` — a non-reversible FD is never a
        // registry candidate in the first place. The driver's
        // NotReversible gate fires before the lookup.

        DriveOptions opts;
        opts.body_validator      = always_valid;
        opts.signature_validator = always_valid;

        DriveResult r = drive_reversible(
            f.found(), ops, synth, routines,
            ctx.getSourceManager(), ctx.getLangOpts(), opts);

        CHECK_FALSE(r.emitted);
        CHECK(r.reason == DriveRejectReason::NotReversible);
        CHECK(r.adjoint_name.empty());
        // The registry remains empty (no entry was inserted either
        // before or after the driver call).
        CHECK(synth.empty());
    });
    CHECK(ran);
}

// ── (4) Auxiliary reject paths ──────────────────────────────────────────────

void test_null_decl_rejects() {
    // nullptr FD → NullDecl reject. The happy-path reject-gate
    // mirror every sibling module's posture.
    bool ran = run_on("", [&](clang::ASTContext& ctx) {
        SynthesisRegistry synth;
        RoutineRegistry routines;
        std::vector<QOperation> ops;
        DriveResult r = drive_reversible(
            nullptr, ops, synth, routines,
            ctx.getSourceManager(), ctx.getLangOpts(), {});
        CHECK_FALSE(r.emitted);
        CHECK(r.reason == DriveRejectReason::NullDecl);
    });
    CHECK(ran);
}

void test_missing_registry_entry_rejects() {
    // A reversible forward that never had `insert_forward` called
    // on it → NoRegistryEntry. The synthesis pipeline is strictly
    // layered; R-C depends on P-B having recorded the forward on
    // an earlier stage. Absence means the earlier stages skipped
    // this forward (e.g. validation already rejected it with an
    // earlier shape), and R-C defers.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
[[clang::annotate("sturm::reversible")]]
void orphan(qbool& r) {}
)CPP";
    std::vector<QOperation> ops;
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("orphan");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        SynthesisRegistry synth;  // deliberately empty
        RoutineRegistry routines;
        DriveOptions opts;
        opts.body_validator      = always_valid;
        opts.signature_validator = always_valid;

        DriveResult r = drive_reversible(
            f.found(), ops, synth, routines,
            ctx.getSourceManager(), ctx.getLangOpts(), opts);

        CHECK_FALSE(r.emitted);
        CHECK(r.reason == DriveRejectReason::NoRegistryEntry);
    });
    CHECK(ran);
}

void test_hand_registration_wins() {
    // PRD §9 Q2: when the user hand-registered a forward via
    // `STURM_REGISTER_ADJOINT`, the `RoutineRegistry` carries the
    // pair and the driver MUST decline emission. The registry
    // entry stays in its prior state — R-C never transitions it
    // to `Emitted` for a hand-registered forward.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
[[clang::annotate("sturm::reversible")]]
void both(qbool& r) {}
)CPP";
    std::vector<QOperation> ops;
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("both");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        SynthesisRegistry synth;
        synth.insert_forward(f.found());

        RoutineRegistry routines;
        // Hand-register the forward/adjoint pair. The registry
        // stores `{FunctionDecl* forward, std::string adj_name}`
        // per PI-1, and `contains()` keys only on the forward.
        // In production the user writes
        // `STURM_REGISTER_ADJOINT(both, user_both_adj)`; for this
        // test we only need the forward key to land in the map so
        // the driver's conflict check fires.
        routines.insert_pair(f.found(), "user_both_adj");

        DriveOptions opts;
        opts.body_validator      = always_valid;
        opts.signature_validator = always_valid;

        DriveResult r = drive_reversible(
            f.found(), ops, synth, routines,
            ctx.getSourceManager(), ctx.getLangOpts(), opts);

        CHECK_FALSE(r.emitted);
        CHECK(r.reason == DriveRejectReason::HandRegistrationWins);

        // The synthesis entry stays in `Pending` (the state it
        // was inserted in) — R-C declined to transition it.
        const SynthesisEntry* entry = synth.lookup(f.found());
        CHECK(entry != nullptr);
        if (entry == nullptr) return;
        CHECK(entry->status == SynthesisStatus::Pending);
        CHECK(entry->adjoint_source.empty());
    });
    CHECK(ran);
}

void test_null_validators_are_permissive() {
    // When `body_validator` and `signature_validator` are both
    // `nullptr` (the designed-in stand-in for pre-P-C / pre-Q-B
    // land), the driver treats the forward as valid and drives
    // R-A + R-B. Pins the "null hook = no constraint" contract
    // the header documents.
    constexpr std::string_view src = R"CPP(
namespace sturm { class qbool { public: qbool() = default; }; }
using sturm::qbool;
[[clang::annotate("sturm::reversible")]]
void noopts(qbool& r) {}
)CPP";
    std::vector<QOperation> ops;
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("noopts");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        SynthesisRegistry synth;
        RoutineRegistry routines;
        synth.insert_forward(f.found());

        // Default-constructed DriveOptions has null hooks. The
        // driver must still emit.
        DriveResult r = drive_reversible(
            f.found(), ops, synth, routines,
            ctx.getSourceManager(), ctx.getLangOpts(), {});

        CHECK(r.emitted);
        CHECK(r.reason == DriveRejectReason::None);

        const SynthesisEntry* entry = synth.lookup(f.found());
        CHECK(entry != nullptr);
        if (entry == nullptr) return;
        CHECK(entry->status == SynthesisStatus::Emitted);
    });
    CHECK(ran);
}

// ── Phase T T-2 (sturm-xrob.3) — error-emission gating harness ──────────────
//
// The gating tests need a DiagnosticsEngine they can assert against
// (errors / warnings emitted). We construct a standalone engine backed
// by a counting consumer so the assertions compare exact counts without
// parsing formatted text. Mirrors the DiagHarness pattern from
// `test_matcher_reversible_validate.cpp` — duplicated here to keep this
// binary self-contained per the per-test-binary convention.

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

// ── Phase T T-2 — collect_invert_call_targets() scanner tests ──────────────

void test_collect_invert_targets_empty_tu() {
    // A TU with no CallExprs at all → empty target set.
    constexpr std::string_view src = R"CPP(
int plain_helper() { return 0; }
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        auto targets = collect_invert_call_targets(ctx);
        CHECK(targets.empty());
    });
    CHECK(ran);
}

void test_collect_invert_targets_one_call() {
    // A TU with a single `sturm::invert(&fn)` call → the target set
    // contains exactly the one FD the call references. The callee
    // spelling must be the qualified `sturm::invert` (matches the
    // canonical declaration in `include/sturm/routines/invert.hpp`).
    constexpr std::string_view src = R"CPP(
namespace sturm {
template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;
    return fn;
}
}
void target_fn(int) {}
void caller() {
    auto p = sturm::invert(&target_fn);
    (void)p;
}
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("target_fn");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);

        auto targets = collect_invert_call_targets(ctx);
        CHECK(targets.size() == 1);
        if (targets.size() == 1) {
            CHECK(targets[0] == f.found());
        }
    });
    CHECK(ran);
}

void test_collect_invert_targets_multiple_deduplicated() {
    // Multiple calls to invert targeting the SAME FD deduplicate to
    // one entry in the returned set.
    constexpr std::string_view src = R"CPP(
namespace sturm {
template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;
    return fn;
}
}
void target_fn(int) {}
void caller() {
    auto p1 = sturm::invert(&target_fn);
    auto p2 = sturm::invert(&target_fn);
    (void)p1; (void)p2;
}
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("target_fn");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);

        auto targets = collect_invert_call_targets(ctx);
        CHECK(targets.size() == 1);
    });
    CHECK(ran);
}

void test_collect_invert_targets_distinct() {
    // Multiple calls to invert targeting different FDs → one entry
    // per distinct target.
    constexpr std::string_view src = R"CPP(
namespace sturm {
template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;
    return fn;
}
}
void target_a(int) {}
void target_b(int) {}
void caller() {
    auto p1 = sturm::invert(&target_a);
    auto p2 = sturm::invert(&target_b);
    (void)p1; (void)p2;
}
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        auto targets = collect_invert_call_targets(ctx);
        CHECK(targets.size() == 2);
    });
    CHECK(ran);
}

// ── Phase T T-2 — drive_reversible_forwards() gating tests ──────────────────

void test_gating_silent_when_no_invert_call() {
    // A reversible forward with an invalid body (measurement call)
    // and NO `invert(&fd)` call site in the TU. Condition (3) fails,
    // so the P-C validator's diagnostic must be SWALLOWED by the T-2
    // silence gate. The validator still computes the verdict (invalid)
    // but no diagnostic reaches the engine.
    constexpr std::string_view src = R"CPP(
namespace sturm {
class qbool { public:
    qbool() {}
    qbool& operator^=(const qbool&) { return *this; }
};
int measure_qubit(int);
}
using sturm::qbool;

[[clang::annotate("sturm::reversible")]]
void bad_body(qbool& r, qbool a) {
    int x = sturm::measure_qubit(0);
    (void)x;
    r ^= a;
}
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("bad_body");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        QUnit unit;
        SynthesisRegistry synth;
        RoutineRegistry routines;
        synth.insert_forward(f.found());

        drive_reversible_forwards(unit, synth, routines, h.ctx, ctx);

        // No invert call site → no diagnostic must reach the engine,
        // even though the P-C validator would otherwise fire a
        // measurement Error. Warnings count must also remain zero —
        // the gate silences every report_* in the validator family.
        CHECK_EQ_STR(std::to_string(h.counter->errors),
                     std::string("0"));
        CHECK_EQ_STR(std::to_string(h.counter->warnings),
                     std::string("0"));
    });
    CHECK(ran);
}

void test_gating_fires_when_invert_call_present() {
    // Same reversible forward with the same invalid body, but the TU
    // also contains a `sturm::invert(&bad_body)` call site. All three
    // PRD §9 Q2 conditions now hold:
    //   (1) reversible,
    //   (2) no hand-registered adjoint,
    //   (3) `invert(&fd)` call site present.
    // The validator's diagnostic MUST fire.
    constexpr std::string_view src = R"CPP(
namespace sturm {
class qbool { public:
    qbool() {}
    qbool& operator^=(const qbool&) { return *this; }
};
int measure_qubit(int);
template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;
    return fn;
}
}
using sturm::qbool;

[[clang::annotate("sturm::reversible")]]
void bad_body(qbool& r, qbool a) {
    int x = sturm::measure_qubit(0);
    (void)x;
    r ^= a;
}

void caller() {
    auto p = sturm::invert(&bad_body);
    (void)p;
}
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("bad_body");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        QUnit unit;
        SynthesisRegistry synth;
        RoutineRegistry routines;
        synth.insert_forward(f.found());

        drive_reversible_forwards(unit, synth, routines, h.ctx, ctx);

        // Conditions (1-3) hold → the P-C measurement diagnostic
        // must fire at Error severity.
        CHECK(h.counter->errors >= 1);
    });
    CHECK(ran);
}

void test_gating_silent_when_hand_registered() {
    // Even with an `invert(&fd)` call site and an invalid body, if
    // the forward is hand-registered in the `RoutineRegistry`, the
    // gate's condition (2) fails — the hand-written adjoint wins per
    // PRD §9 Q2 and the validator's diagnostic must be SWALLOWED.
    constexpr std::string_view src = R"CPP(
namespace sturm {
class qbool { public:
    qbool() {}
    qbool& operator^=(const qbool&) { return *this; }
};
int measure_qubit(int);
template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;
    return fn;
}
}
using sturm::qbool;

[[clang::annotate("sturm::reversible")]]
void bad_body(qbool& r, qbool a) {
    int x = sturm::measure_qubit(0);
    (void)x;
    r ^= a;
}

void caller() {
    auto p = sturm::invert(&bad_body);
    (void)p;
}
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("bad_body");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        QUnit unit;
        SynthesisRegistry synth;
        RoutineRegistry routines;
        synth.insert_forward(f.found());
        // Hand-register the forward/adjoint pair — condition (2)
        // fails because the user has supplied a manual adjoint via
        // `STURM_REGISTER_ADJOINT`.
        routines.insert_pair(f.found(), "user_supplied_adj");

        drive_reversible_forwards(unit, synth, routines, h.ctx, ctx);

        // Hand-registration wins silently → no diagnostic reaches the
        // engine even though the body would otherwise fail P-C.
        CHECK_EQ_STR(std::to_string(h.counter->errors),
                     std::string("0"));
        CHECK_EQ_STR(std::to_string(h.counter->warnings),
                     std::string("0"));
    });
    CHECK(ran);
}

void test_gating_silent_when_body_is_valid() {
    // A reversible forward with a VALID body and an `invert(&fd)`
    // call site. No validator rejects → no diagnostic regardless of
    // the gate state. Pins that the gate is orthogonal to the
    // validator's "valid" path: a clean forward fires nothing.
    constexpr std::string_view src = R"CPP(
namespace sturm {
class qbool { public:
    qbool() {}
    qbool& operator^=(const qbool&) { return *this; }
};
template <typename R, typename... Args>
constexpr auto invert(R (*fn)(Args...)) noexcept {
    (void)fn;
    return fn;
}
}
using sturm::qbool;

[[clang::annotate("sturm::reversible")]]
void good_body(qbool& r, qbool a) {
    r ^= a;
}

void caller() {
    auto p = sturm::invert(&good_body);
    (void)p;
}
)CPP";
    bool ran = run_on(src, [&](clang::ASTContext& ctx) {
        NamedFnFinder f("good_body");
        f.TraverseAST(ctx);
        CHECK(f.found() != nullptr);
        if (f.found() == nullptr) return;

        DiagHarness h;
        QUnit unit;
        SynthesisRegistry synth;
        RoutineRegistry routines;
        synth.insert_forward(f.found());

        drive_reversible_forwards(unit, synth, routines, h.ctx, ctx);

        // Valid body + invert call present → still zero diagnostics
        // because the body passes P-C validation.
        CHECK_EQ_STR(std::to_string(h.counter->errors),
                     std::string("0"));
        CHECK_EQ_STR(std::to_string(h.counter->warnings),
                     std::string("0"));
    });
    CHECK(ran);
}

// ── Silence guard RAII smoke test ──────────────────────────────────────────

void test_silence_guard_suppresses_reports() {
    // Pin the RAII silence guard's contract: inside the guarded
    // scope, every `report_*` is swallowed; after scope exit, the
    // prior silenced-state is restored and subsequent reports land
    // normally on the engine. Nesting is exercised implicitly — the
    // inner guard captures `prior=true` (because the outer is active)
    // and restores it on destruction.
    bool ran = run_on("", [&](clang::ASTContext& /*ctx*/) {
        DiagHarness h;
        clang::SourceLocation loc;  // invalid loc is fine for
                                     // report_* — the engine still
                                     // registers the diag and
                                     // HandleDiagnostic fires.

        // Outside the guard: report lands.
        h.ctx.report_reversible_measurement(loc, "fn_unguarded");
        CHECK(h.counter->errors == 1);

        {
            DiagContext::SilenceGuard g(h.ctx);
            h.ctx.report_reversible_measurement(loc, "fn_silenced");
            h.ctx.report_reversible_while_loop(loc, "fn_silenced");
            h.ctx.report_reversible_io(loc, "fn_silenced");
        }
        // Counts unchanged — all three reports inside the guard
        // were swallowed.
        CHECK(h.counter->errors == 1);

        // After guard destruction: reports land again.
        h.ctx.report_reversible_measurement(loc, "fn_post_guard");
        CHECK(h.counter->errors == 2);
    });
    CHECK(ran);
}

// ── (5) Reason stringification ──────────────────────────────────────────────

void test_reason_to_string_stable() {
    // Spellings are public contract — callers may surface them in
    // diagnostics (once the P-C / Q-B diagnostic pipelines route
    // through R-C) and tests pin them here.
    CHECK_EQ_STR(std::string(to_string(DriveRejectReason::None)),
                 std::string("none"));
    CHECK_EQ_STR(std::string(to_string(DriveRejectReason::NullDecl)),
                 std::string("null_decl"));
    CHECK_EQ_STR(std::string(to_string(DriveRejectReason::NotReversible)),
                 std::string("not_reversible"));
    CHECK_EQ_STR(std::string(
                     to_string(DriveRejectReason::NoRegistryEntry)),
                 std::string("no_registry_entry"));
    CHECK_EQ_STR(std::string(
                     to_string(DriveRejectReason::ValidationFailed)),
                 std::string("validation_failed"));
    CHECK_EQ_STR(std::string(
                     to_string(DriveRejectReason::HandRegistrationWins)),
                 std::string("hand_registration_wins"));
    CHECK_EQ_STR(std::string(
                     to_string(DriveRejectReason::AdjointEmissionFailed)),
                 std::string("adjoint_emission_failed"));
    CHECK_EQ_STR(std::string(
                     to_string(DriveRejectReason::AutoRegistrationFailed)),
                 std::string("auto_registration_failed"));
}

} // namespace

int run_test_matcher_reversible_drive(int /*argc*/, char** /*argv*/) {
    // (1) Happy path.
    test_happy_path_populates_registry();

    // (2) Rejection paths via the swappable validator hooks.
    test_rejection_path_marks_failed_and_no_adjoint();
    test_rejection_path_signature_validator();

    // (3) Non-reversible FD is a no-op.
    test_non_reversible_fd_is_noop();

    // (4) Auxiliary reject paths.
    test_null_decl_rejects();
    test_missing_registry_entry_rejects();
    test_hand_registration_wins();
    test_null_validators_are_permissive();

    // (5) Reason stringification.
    test_reason_to_string_stable();

    // Phase T T-2 (sturm-xrob.3) — invert call-site scanner tests.
    test_collect_invert_targets_empty_tu();
    test_collect_invert_targets_one_call();
    test_collect_invert_targets_multiple_deduplicated();
    test_collect_invert_targets_distinct();

    // Phase T T-2 (sturm-xrob.3) — drive_reversible_forwards gating
    // tests. Each of these runs the full end-of-TU driver against a
    // snippet that exercises one PRD §9 Q2 gate-condition state and
    // asserts on the resulting DiagnosticsEngine counts.
    test_gating_silent_when_no_invert_call();
    test_gating_fires_when_invert_call_present();
    test_gating_silent_when_hand_registered();
    test_gating_silent_when_body_is_valid();

    // Phase T T-2 (sturm-xrob.3) — DiagContext silence guard smoke
    // test.
    test_silence_guard_suppresses_reports();

    std::fprintf(stderr,
                 "test_matcher_reversible_drive: %d / %d checks passed\n",
                 tests_pass, tests_run);
    return (tests_pass == tests_run) ? 0 : 1;
}
