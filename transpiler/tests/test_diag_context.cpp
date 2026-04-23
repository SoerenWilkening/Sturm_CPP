// test_diag_context.cpp — smoke test for the DiagContext surface,
// with a focus on the P-D / Phase P "automatic adjoint synthesis"
// additions (sturm-z2e8.4).
//
// The test constructs a standalone `clang::DiagnosticsEngine` fronted
// by a counting `DiagnosticConsumer` (so we can assert a report
// actually landed), wraps it in a `DiagContext`, and exercises every
// `report_*` member. Each method must:
//   - resolve a custom diag-ID via `getOrRegister` without throwing,
//   - fire a `Report()` at the correct severity (Error vs Warning),
//   - populate the consumer sink so the number of diagnostics
//     received matches the number of `report_*` calls made.
//
// The five new `report_reversible_*` methods from sturm-z2e8.4 are
// pinned explicitly: each fires exactly one Error-severity diagnostic
// per call, matching the P9d contract (plan §2.1 row P-D).
//
// No Clang ASTConsumer / MatchFinder is needed — `DiagContext` talks
// only to `DiagnosticsEngine`, so the test can build one directly
// with a throwaway `SourceLocation` and never spin up a translation
// unit. This keeps the test fast and independent of the full
// LibTooling link line.

#include "diag_context.hpp"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"

#include <cstdio>
#include <string>
#include <string_view>

using sturm::transpile::DiagContext;

// ── Test harness ────────────────────────────────────────────────────
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

// Counting consumer: bumps separate totals for warning vs error
// diagnostics so the tests can assert on severity without parsing
// formatted text. Mirrors the minimal pattern in the Clang docs
// (DiagnosticConsumer::HandleDiagnostic). We do NOT chain a
// `TextDiagnosticPrinter` — the engine's default client is replaced
// entirely by this consumer.
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

// Build a throwaway DiagnosticsEngine fronted by the counter above.
// The engine owns the counter (`ShouldOwnClient=true`) so the caller
// just keeps a pointer to it for asserting counts — the engine cleans
// it up when it falls out of scope.
struct Harness {
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs>     ids;
    llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> opts;
    CountingDiagConsumer*                              counter;
    clang::DiagnosticsEngine                           engine;
    DiagContext                                        ctx;

    Harness()
        : ids(new clang::DiagnosticIDs()),
          opts(new clang::DiagnosticOptions()),
          counter(new CountingDiagConsumer()),
          engine(ids, opts.get(), counter, /*ShouldOwnClient=*/true),
          ctx(engine) {
        // BeginSourceFile needs a valid LangOptions; without a real
        // TU we skip it — the DiagnosticConsumer::HandleDiagnostic
        // contract does not require a file to be open.
    }
};

} // anonymous namespace

// ── getOrRegister: cache hits share an ID across invocations ────────

static void test_getOrRegister_cache_returns_same_id_for_same_key() {
    Harness h;
    const unsigned id1 = h.ctx.getOrRegister(
        clang::DiagnosticsEngine::Error, "STURM: smoke %0");
    const unsigned id2 = h.ctx.getOrRegister(
        clang::DiagnosticsEngine::Error, "STURM: smoke %0");
    CHECK(id1 == id2);
}

static void test_getOrRegister_different_levels_different_ids() {
    Harness h;
    const unsigned eid = h.ctx.getOrRegister(
        clang::DiagnosticsEngine::Error, "STURM: dup %0");
    const unsigned wid = h.ctx.getOrRegister(
        clang::DiagnosticsEngine::Warning, "STURM: dup %0");
    CHECK(eid != wid);
}

// ── Existing report_* members — sanity smoke ────────────────────────
//
// Quick check that the three previously-filled Error-severity members
// still route through the engine. Not the focus of sturm-z2e8.4 — the
// point is to pin the pattern so the five new methods can be asserted
// against the same baseline.

static void test_report_when_operand_mutation_fires_error() {
    Harness h;
    h.ctx.report_when_operand_mutation(clang::SourceLocation(), "q");
    CHECK_EQ_INT(h.counter->errors, 1);
    CHECK_EQ_INT(h.counter->warnings, 0);
}

static void test_report_prep_in_uncompute_scope_fires_warning() {
    Harness h;
    h.ctx.report_prep_in_uncompute_scope(clang::SourceLocation(), "x");
    CHECK_EQ_INT(h.counter->errors, 0);
    CHECK_EQ_INT(h.counter->warnings, 1);
}

// ── sturm-z2e8.4 / P-D: five new reversible diagnostics ─────────────

static void test_report_reversible_measurement_fires_error() {
    Harness h;
    h.ctx.report_reversible_measurement(clang::SourceLocation(), "marked");
    CHECK_EQ_INT(h.counter->errors, 1);
    CHECK_EQ_INT(h.counter->warnings, 0);
}

static void test_report_reversible_io_fires_error() {
    Harness h;
    h.ctx.report_reversible_io(clang::SourceLocation(), "marked");
    CHECK_EQ_INT(h.counter->errors, 1);
    CHECK_EQ_INT(h.counter->warnings, 0);
}

static void test_report_reversible_unregistered_callee_fires_error() {
    Harness h;
    h.ctx.report_reversible_unregistered_callee(
        clang::SourceLocation(), "helper");
    CHECK_EQ_INT(h.counter->errors, 1);
    CHECK_EQ_INT(h.counter->warnings, 0);
}

static void test_report_reversible_while_loop_fires_error() {
    Harness h;
    h.ctx.report_reversible_while_loop(clang::SourceLocation(), "ripple");
    CHECK_EQ_INT(h.counter->errors, 1);
    CHECK_EQ_INT(h.counter->warnings, 0);
}

static void test_report_reversible_classical_cond_fires_error() {
    Harness h;
    h.ctx.report_reversible_classical_cond(
        clang::SourceLocation(), "guarded");
    CHECK_EQ_INT(h.counter->errors, 1);
    CHECK_EQ_INT(h.counter->warnings, 0);
}

// Combined: all five reversible report_* members reuse distinct
// format strings, so each call allocates its own diag-ID and fires
// exactly one Error. Asserting the aggregate count proves none of
// the five methods silently short-circuits.
static void test_all_five_reversible_reports_each_fire_once() {
    Harness h;
    h.ctx.report_reversible_measurement(
        clang::SourceLocation(), "rm");
    h.ctx.report_reversible_io(
        clang::SourceLocation(), "rio");
    h.ctx.report_reversible_unregistered_callee(
        clang::SourceLocation(), "ruc");
    h.ctx.report_reversible_while_loop(
        clang::SourceLocation(), "rwl");
    h.ctx.report_reversible_classical_cond(
        clang::SourceLocation(), "rcc");
    CHECK_EQ_INT(h.counter->errors, 5);
    CHECK_EQ_INT(h.counter->warnings, 0);
}

// Idempotence under the cache: firing the same reversible report_*
// twice must land both diagnostics (nothing swallows duplicates) and
// must reuse the cached diag-ID on the second call.
static void test_reversible_report_is_not_dedup_swallowed() {
    Harness h;
    h.ctx.report_reversible_while_loop(clang::SourceLocation(), "ripple");
    h.ctx.report_reversible_while_loop(clang::SourceLocation(), "ripple");
    CHECK_EQ_INT(h.counter->errors, 2);
}

// Phase T T-4 (sturm-xrob.5): Q-A multi-statement body reject surfaced
// via `report_reversible_sig_multi_return` fires at Error severity.
static void test_report_reversible_sig_multi_return_fires_error() {
    Harness h;
    h.ctx.report_reversible_sig_multi_return(
        clang::SourceLocation(), "two_returns");
    CHECK_EQ_INT(h.counter->errors, 1);
    CHECK_EQ_INT(h.counter->warnings, 0);
}

// ── main ────────────────────────────────────────────────────────────

int main() {
    test_getOrRegister_cache_returns_same_id_for_same_key();
    test_getOrRegister_different_levels_different_ids();

    test_report_when_operand_mutation_fires_error();
    test_report_prep_in_uncompute_scope_fires_warning();

    test_report_reversible_measurement_fires_error();
    test_report_reversible_io_fires_error();
    test_report_reversible_unregistered_callee_fires_error();
    test_report_reversible_while_loop_fires_error();
    test_report_reversible_classical_cond_fires_error();
    test_all_five_reversible_reports_each_fire_once();
    test_reversible_report_is_not_dedup_swallowed();
    test_report_reversible_sig_multi_return_fires_error();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
