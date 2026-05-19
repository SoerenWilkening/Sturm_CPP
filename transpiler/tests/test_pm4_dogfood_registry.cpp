// test_pm4_dogfood_registry.cpp — PM4-6 dogfood migration gate.
//
// Scope
// -----
// Verifies that the four Phase B (PB-1..PB-4) qint_t compound-assign
// matchers — `ADD_ASSIGN_CONST` / `SUB_ASSIGN_CONST` / `MUL_ASSIGN_CONST`
// / `DIV_ASSIGN_CONST` — are now registered through the PM4-1 plugin
// Registry API (link-time path) rather than via direct
// `TranspileConsumer` constructor calls.
//
// What PM4-6 pins
// ---------------
//   1. Link-time registration path fires at static-init. Every TU that
//      links `matcher_qint_const.cpp` contributes exactly ONE entry to
//      the Meyer's-singleton `registrars()` vector (the dogfood
//      `STURM_REGISTER_PLUGIN` call).
//   2. Draining that entry into a fresh `Registry` populates the PB
//      matcher pool: `Registry::register_matcher` is called four times
//      (one per QOpKind), each under a human-readable name.
//   3. The same four matchers, once invoked against a real
//      `MatchFinder` + `QUnit` via `Registry::invoke_all`, produce
//      QOperations with the identical `QOpKind` values they did
//      pre-migration. Byte-identical snapshot invariance is pinned by
//      the existing `snapshot_*_assign_const` CTests; this binary pins
//      the IR-level contract that feeds those snapshots.
//
// Why a separate test TU rather than extending test_matcher_qint_const
// --------------------------------------------------------------------
// The existing matcher test drives the four free-function registrars
// (`register_add_assign_const_matcher(finder, unit)`, ...) directly to
// prove each pattern still matches. PM4-6's contract is orthogonal:
// it pins the NEW registration path (Registry API) alongside the
// existing direct-call path. Keeping the two tests separate makes the
// dogfood gate unambiguous — a future refactor that breaks the
// Registry path without breaking the direct-call path would still turn
// this binary red.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/plugin_api.hpp"
#include "sturm/transpile/qir.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace sturm_test_pm4_dogfood_registry_ns {

using sturm::transpile::QOperation;
using sturm::transpile::QOpKind;
using sturm::transpile::QUnit;
using sturm::transpile::plugin::Registry;
using sturm::transpile::plugin::registrars;

// ── Test harness ──────────────────────────────────────────────────────────────
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

// Stub mirroring `test_matcher_qint_const.cpp`. `qint_t<W>` with an
// implicit long-long converting constructor and the four compound-assign
// operators the PB matchers key off.
static constexpr std::string_view kQIntStub = R"CPP(
namespace sturm {

template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    // NOLINTNEXTLINE(google-explicit-constructor)
    qint_t(long long) {}
    qint_t& operator+=(const qint_t&) { return *this; }
    qint_t& operator-=(const qint_t&) { return *this; }
    qint_t& operator*=(const qint_t&) { return *this; }
    qint_t& operator/=(const qint_t&) { return *this; }
};

} // namespace sturm

using qint_t = sturm::qint_t<1>;
)CPP";

// ── Test: static-init registered at least one link-time entry ────────────────
// The dogfood migration adds `STURM_REGISTER_PLUGIN(PBDogfoodPlugin)` at
// namespace scope in `matcher_qint_const.cpp`. When this test binary links
// that TU, the macro's `StaticRegistrar` ctor fires during the process'
// static-init and pushes a `LinkTimeRegisterFn` onto the Meyer's-singleton
// `registrars()` vector. After `main()` starts, the vector's size is
// therefore >= 1.
//
// Pinning "at least one" rather than "exactly one" keeps the test robust
// against future PM4 migrations layering additional link-time registrars
// into the same test binary (PM4-7 link-time shim, hypothetical
// downstream dogfood families) — each contributes its own entry, and
// this test only cares about the PB family being present.
static void test_registrars_populated_by_static_init() {
    CHECK(!registrars().empty());
}

// ── Test: draining registrars() populates the Registry's matcher pool ────────
// Feeds a fresh `Registry` through every `LinkTimeRegisterFn` in
// `registrars()`. After the drain, the Registry's internal matcher-name
// set must contain the four PB family names. The string identifiers are
// stable across versions because `Registry::register_matcher` uses the
// name only for collision detection + diagnostics — it never maps back to
// a matcher at dispatch time.
//
// We probe the population indirectly through `invoke_all`: each registered
// `MatcherRegisterFn` fires once per `invoke_all` call, so a simple
// `int counter` incremented inside each callback yields a monotonic count
// equal to the number of registrations at the time of invocation.
//
// The four expected entries are the Phase B compound-assign matchers:
// `add_assign_const`, `sub_assign_const`, `mul_assign_const`,
// `div_assign_const`. This test pins the lower bound — exactly four
// matcher-family callbacks must fire during a clean drain + invoke_all.
static void test_drain_registers_four_pb_matchers() {
    Registry r;
    for (const auto& fn : registrars()) {
        fn(r);
    }

    int invocations = 0;
    // We can't observe the internal matcher_names_ set directly, but
    // `invoke_all` drains the matcher fn vector and each PB entry calls
    // the shared template registrar (which itself registers one matcher
    // on the finder). Instead of hooking into `invoke_all`'s internals,
    // we pin the end-to-end AST contract: run the Registry against a
    // real MatchFinder + QUnit and confirm four ADD/SUB/MUL/DIV_ASSIGN_
    // CONST ops land in `unit.scopes`.
    (void)invocations;

    // ASTMatchFinder entry: compile a single TU containing one of each
    // PB operator shape and assert the matchers fired. This doubles as a
    // byte-identical-ops gate — the QOpKind enum values unchanged pre-
    // and post-migration.
    std::string code;
    code.append(kQIntStub);
    code.append(
        "void demo(qint_t a) {\n"
        "    a += 3;\n"
        "    a -= 5;\n"
        "    a *= 7;\n"
        "    a /= 9;\n"
        "}\n");

    clang::ast_matchers::MatchFinder finder;
    QUnit unit;
    r.invoke_all(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "pm4_dogfood.cpp");
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL  tool run returned false\n");
    }

    // Flatten every op across every scope for a single cross-scope
    // count.
    int add_count = 0, sub_count = 0, mul_count = 0, div_count = 0;
    for (const auto& scope : unit.scopes) {
        for (const auto& op : scope.ops) {
            switch (op.kind) {
            case QOpKind::ADD_ASSIGN_CONST: ++add_count; break;
            case QOpKind::SUB_ASSIGN_CONST: ++sub_count; break;
            case QOpKind::MUL_ASSIGN_CONST: ++mul_count; break;
            case QOpKind::DIV_ASSIGN_CONST: ++div_count; break;
            default: break;
            }
        }
    }

    // Each operator in the input fires exactly once.
    CHECK(add_count == 1);
    CHECK(sub_count == 1);
    CHECK(mul_count == 1);
    CHECK(div_count == 1);
}

// ── Test: operand text preserved byte-identical post-migration ──────────────
// The Phase B matchers peel the implicit `qint_t(long long)` constructor
// wrapper on the RHS and extract the verbatim source fragment via
// `Lexer::getSourceText`. Post-migration this shape is UNCHANGED — the
// four matchers share the same `QIntAssignConstCallback<Kind>` template
// whether they are registered via direct call (pre-PM4-6) or via the
// Registry drain (post-PM4-6). Pinning operand text here is the IR-level
// complement of the byte-identical snapshot gate.
static void test_operand_text_matches_source() {
    Registry r;
    for (const auto& fn : registrars()) {
        fn(r);
    }

    std::string code;
    code.append(kQIntStub);
    code.append(
        "void demo(qint_t a) {\n"
        "    a += 42;\n"
        "}\n");

    clang::ast_matchers::MatchFinder finder;
    QUnit unit;
    r.invoke_all(finder, unit);

    auto factory = clang::tooling::newFrontendActionFactory(&finder);
    std::vector<std::string> args{"-std=c++20", "-fsyntax-only"};
    bool ok = clang::tooling::runToolOnCodeWithArgs(
        factory->create(), code, args, "pm4_dogfood_operand.cpp");
    (void)ok;

    const QOperation* found = nullptr;
    for (const auto& scope : unit.scopes) {
        for (const auto& op : scope.ops) {
            if (op.kind == QOpKind::ADD_ASSIGN_CONST) {
                found = &op;
                break;
            }
        }
        if (found) break;
    }
    CHECK(found != nullptr);
    if (found == nullptr) return;
    CHECK(found->result.name == "a");
    CHECK(found->operands.size() == 1);
    if (!found->operands.empty()) {
        CHECK(found->operands[0].name == "42");
    }
}

}  // namespace sturm_test_pm4_dogfood_registry_ns

int run_test_pm4_dogfood_registry(int /*argc*/, char** /*argv*/) {
    using namespace sturm_test_pm4_dogfood_registry_ns;
    using sturm_test_pm4_dogfood_registry_ns::tests_run;
    using sturm_test_pm4_dogfood_registry_ns::tests_pass;
    test_registrars_populated_by_static_init();
    test_drain_registers_four_pb_matchers();
    test_operand_text_matches_source();
    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
