// test_matcher_sb_loop_reversal_handoff.cpp — Phase S S-B (sturm-ha2k.3).
//
// S-B edits the Phase H PH-3 outer-var-guard matcher so that, when the
// enclosing `FunctionDecl` carries `[[clang::annotate("sturm::reversible")]]`
// AND the outer mutation sits inside a `for` body, the op is routed to
// Phase S's loop-reversal synthesis path (`needs_loop_reversal = true`)
// instead of the legacy PH-3 `skip_uncompute = true` + diagnostic path.
//
// Outside the reversible-synthesis opt-in — and for non-for barriers
// (while / if / WHEN / else) even inside a reversible routine — the
// existing PH-3 behaviour is preserved bit-for-bit. The three pinning
// tests below cover:
//
//   1. Non-reversible FD + for-loop outer mutation:
//        existing PH-3 path; skip_uncompute=true, diagnostic emitted,
//        PH-3 counter bumps, S-B handoff counter stays at zero.
//        (regression guard against the legacy contract.)
//
//   2. Reversible FD + for-loop outer mutation:
//        S-B path; needs_loop_reversal=true, skip_uncompute left false,
//        S-B handoff counter bumps, PH-3 counter stays at zero.
//        (new S-B contract.)
//
//   3. Reversible FD + while-loop outer mutation:
//        S-A does not cover while-loops; legacy PH-3 path taken.
//        PH-3 counter bumps, S-B handoff counter stays at zero.
//        (scope-containment — S-B gate is narrow by design.)

#include "test_matcher_harness.hpp"

#include "diag_context.hpp"
#include "sturm/transpile/matcher.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace sturm::transpile;

namespace {

// Minimal qbool stub with `operator^=` so PA-3/PA-4 fire on the body's
// `a ^= b;` mutation. Same shape as the PH-3 test harness uses; kept
// inline so the S-B test module is self-contained and can carry an
// extra `[[clang::annotate("sturm::reversible")]]` marker above the
// forward routine without further plumbing.
constexpr std::string_view kQBoolXorAssignStub = R"CPP(
namespace sturm {

class qbool {
public:
    qbool() {}
    qbool(const qbool&) {}
    qbool& operator=(const qbool&) { return *this; }
    qbool& operator^=(const qbool&) { return *this; }
    qbool& operator^=(int) { return *this; }
};

} // namespace sturm
using sturm::qbool;
)CPP";

struct SBRun {
    QUnit unit;
    int ph3_detections = 0;
    int sb_handoffs = 0;
};

SBRun run_sb_xor_assign_matcher(std::string_view user_src) {
    std::string code;
    code.reserve(kQBoolXorAssignStub.size() + user_src.size());
    code.append(kQBoolXorAssignStub);
    code.append(user_src);

    SBRun out;
    // Reset BOTH counters so the test reads clean state for each run.
    reset_outer_var_guard_detection_count_for_test();
    reset_loop_reversal_handoff_count_for_test();

    clang::ast_matchers::MatchFinder finder;
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> ids(
        new clang::DiagnosticIDs());
    llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> opts(
        new clang::DiagnosticOptions());
    clang::DiagnosticsEngine diag_engine(
        ids, opts.get(), new clang::IgnoringDiagConsumer(),
        /*ShouldOwnClient=*/true);
    DiagContext diag_ctx(diag_engine);

    // Registration order matters: PA-3/PA-4 populate `unit.scopes`
    // before the PH-3 callback classifies the mutation. Same ordering
    // the production `main.cpp` uses.
    register_xor_assign_matcher(finder, out.unit);
    register_xor_assign_classical_matcher(finder, out.unit);
    register_outer_var_guard_matcher(finder, out.unit, diag_ctx);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "test_input.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false (S-B xor_assign)\n");
    }
    out.ph3_detections = outer_var_guard_detection_count_for_test();
    out.sb_handoffs = loop_reversal_handoff_count_for_test();
    return out;
}

// ── (1) Regression guard for the legacy PH-3 path ──────────────────────────
//
// Outside a `[[clang::annotate("sturm::reversible")]]` routine, the
// matcher must behave exactly as before Phase S S-B shipped:
//   - for-loop outer mutation ⇒ skip_uncompute=true, diagnostic,
//   - PH-3 detection counter bumps once,
//   - S-B handoff counter stays at zero.
//
// This pins the "preserve PH-3 behaviour bit-for-bit outside synthesis
// context" contract the plan §2.4 S-B calls out verbatim.
void test_sb_non_reversible_for_loop_preserves_ph3_behaviour() {
    SBRun r = run_sb_xor_assign_matcher(
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        a ^= b;\n"
        "    }\n"
        "}\n");
    CHECK(r.ph3_detections == 1);
    CHECK(r.sb_handoffs == 0);

    std::size_t skip_count = 0;
    std::size_t loop_mark_count = 0;
    std::size_t total = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            ++total;
            if (op.skip_uncompute) ++skip_count;
            if (op.needs_loop_reversal) ++loop_mark_count;
        }
    }
    CHECK(total == 1);
    CHECK(skip_count == 1);
    CHECK(loop_mark_count == 0);
}

// ── (2) S-B contract inside a reversible routine ────────────────────────────
//
// The `[[clang::annotate("sturm::reversible")]]` marker above the
// forward routine opts the for-loop outer mutation into Phase S
// synthesis. Expected observable outputs:
//   - needs_loop_reversal=true on the op,
//   - skip_uncompute left false (the op is NOT being elided — it's
//     being handed to S-A),
//   - S-B handoff counter bumps once,
//   - PH-3 detection counter stays at zero (no diagnostic, no
//     "provide a manual adjoint" message for a routine whose author
//     explicitly asked for synthesis).
void test_sb_reversible_for_loop_routes_to_loop_reversal() {
    SBRun r = run_sb_xor_assign_matcher(
        "[[clang::annotate(\"sturm::reversible\")]]\n"
        "void demo(qbool a, qbool b) {\n"
        "    for (int i = 0; i < 3; ++i) {\n"
        "        a ^= b;\n"
        "    }\n"
        "}\n");
    CHECK(r.sb_handoffs == 1);
    CHECK(r.ph3_detections == 0);

    std::size_t skip_count = 0;
    std::size_t loop_mark_count = 0;
    std::size_t total = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            ++total;
            if (op.skip_uncompute) ++skip_count;
            if (op.needs_loop_reversal) ++loop_mark_count;
        }
    }
    CHECK(total == 1);
    // S-B hands the op to Phase S synthesis; the PH-3 suppression flag
    // must stay false so downstream consumers (R-A adjoint_emitter,
    // M8 synthesis pass) do NOT elide the op.
    CHECK(skip_count == 0);
    CHECK(loop_mark_count == 1);
}

// ── (3) Non-for barriers inside a reversible routine stay on the PH-3 path ──
//
// S-A only handles `for`-loop reversal; `while` / `if` / `WHEN` / `else`
// barriers are outside its reach. The P-C validation pass owns the
// separate "reject non-for control-flow in a reversible routine"
// diagnostic; S-B does not preempt it. Inside the S-B edit the routing
// gate is narrow: the LEGACY PH-3 path must run for non-for barriers
// even when the enclosing FD is reversible, so the user still sees the
// manual-adjoint diagnostic and the P-C pass still has an op to flag.
void test_sb_reversible_while_loop_falls_back_to_ph3() {
    SBRun r = run_sb_xor_assign_matcher(
        "[[clang::annotate(\"sturm::reversible\")]]\n"
        "void demo(qbool a, qbool b) {\n"
        "    while (true) {\n"
        "        a ^= b;\n"
        "        break;\n"
        "    }\n"
        "}\n");
    CHECK(r.ph3_detections == 1);
    CHECK(r.sb_handoffs == 0);

    std::size_t skip_count = 0;
    std::size_t loop_mark_count = 0;
    for (const auto& s : r.unit.scopes) {
        for (const auto& op : s.ops) {
            if (op.skip_uncompute) ++skip_count;
            if (op.needs_loop_reversal) ++loop_mark_count;
        }
    }
    CHECK(skip_count == 1);
    CHECK(loop_mark_count == 0);
}

} // namespace

void run_sb_loop_reversal_handoff_tests() {
    test_sb_non_reversible_for_loop_preserves_ph3_behaviour();
    test_sb_reversible_for_loop_routes_to_loop_reversal();
    test_sb_reversible_while_loop_falls_back_to_ph3();
}
