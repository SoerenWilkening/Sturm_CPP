// test_plugin_registry.cpp — PM4-2 unit tests for
// `transpiler/src/plugin_registry.cpp` (the Registry implementation backing
// the PM4-1 header).
//
// The PM4-2 deliverable:
//   - Registry is owned by TranspileConsumer (one per TU). This test
//     verifies the ownership-compatible shape: stack-allocatable, usable
//     through a reference, no shared process state between two instances.
//   - Collision detection: `register_matcher` with a duplicate name and
//     `register_op` with a duplicate `kind_id` must hard-error. The tests
//     fork a child process and assert the child aborts.
//   - `invoke_all(finder, unit)` drains every registered matcher in
//     insertion order. Callbacks registered via `register_matcher` and
//     via `register_op` share the drain.
//   - Meyer's singleton: `registrars()` returns the same vector across
//     repeated calls (process-wide identity), and `StaticRegistrar`'s ctor
//     appends to that vector.
//
// No gate is tested for PM4-3 concerns (render_uncompute dispatch, threading
// Registry& into synthesize). A small forward-compat probe verifies
// `find_render_fn` returns the stored renderer — this is the contract PM4-3
// will dispatch against, so pinning it here keeps PM4-2 and PM4-3 loosely
// coupled.
//
// Fork-based hard-error testing: the collision path calls std::abort after
// printing to stderr. The test forks a child, drives the collision, and
// asserts the child terminates on SIGABRT. The parent prints PASS/FAIL
// summaries.

#include "sturm/transpile/plugin_api.hpp"
#include "sturm/transpile/qir.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace sturm_test_plugin_registry_ns {

using sturm::transpile::QUnit;
using sturm::transpile::plugin::Registry;
using sturm::transpile::plugin::registrars;
using sturm::transpile::plugin::StaticRegistrar;
using sturm::transpile::plugin::MatcherRegisterFn;
using sturm::transpile::plugin::UncomputeRenderFn;
using sturm::transpile::plugin::LinkTimeRegisterFn;

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

// ── Subprocess helper for hard-error tests ───────────────────────────────────
// Fork, run `body()` in the child, and return true iff the child died via
// SIGABRT. std::abort() is the Registry's collision-handling exit path
// (with an fprintf(stderr, ...) immediately before), so SIGABRT is the
// canonical signal.
static bool child_aborts(const std::function<void()>& body) {
    std::fflush(stdout);
    std::fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
        std::fprintf(stderr, "fork() failed\n");
        return false;
    }
    if (pid == 0) {
        // Redirect stderr to /dev/null so the test output stays clean.
        // The hard-error message is already covered by the exit-signal
        // assertion; we do not grep its text here.
        FILE* dn = std::freopen("/dev/null", "w", stderr);
        (void)dn;
        body();
        // If body() returns normally the test fails — use _exit(0) so we
        // can distinguish "returned normally" (exit code 0) from "aborted"
        // (signal SIGABRT).
        std::_Exit(0);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return false;
    }
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

// ── Test: duplicate register_matcher name aborts ─────────────────────────────
static void test_register_matcher_duplicate_name_aborts() {
    const bool aborted = child_aborts([] {
        Registry r;
        r.register_matcher("dup",
            [](clang::ast_matchers::MatchFinder&, QUnit&) {});
        // Second registration with the same name MUST hard-error.
        r.register_matcher("dup",
            [](clang::ast_matchers::MatchFinder&, QUnit&) {});
    });
    CHECK(aborted);
}

// ── Test: duplicate register_op kind_id aborts ───────────────────────────────
static void test_register_op_duplicate_kind_id_aborts() {
    const bool aborted = child_aborts([] {
        Registry r;
        r.register_op("kind_x",
            [](clang::ast_matchers::MatchFinder&, QUnit&) {},
            [](const sturm::transpile::QOperation&) { return std::string{}; });
        // Second registration with the same kind_id MUST hard-error.
        r.register_op("kind_x",
            [](clang::ast_matchers::MatchFinder&, QUnit&) {},
            [](const sturm::transpile::QOperation&) { return std::string{}; });
    });
    CHECK(aborted);
}

// ── Test: register_matcher and register_op share no key namespace ────────────
// A matcher named "foo" and an op with kind_id="foo" do NOT collide — they
// travel through disjoint code paths in the host (plan §1). Pin this here
// so a future collision-set consolidation does not regress the API.
static void test_matcher_name_and_kind_id_are_disjoint_namespaces() {
    Registry r;
    r.register_matcher("foo",
        [](clang::ast_matchers::MatchFinder&, QUnit&) {});
    // If the sets were merged, this call would abort.
    r.register_op("foo",
        [](clang::ast_matchers::MatchFinder&, QUnit&) {},
        [](const sturm::transpile::QOperation&) { return std::string{}; });
    // If we got here without aborting, the namespaces are disjoint.
    CHECK(true);
}

// ── Test: invoke_all calls every registered matcher callback once ────────────
static void test_invoke_all_runs_every_registered_callback() {
    int reg_matcher_calls = 0;
    int reg_op_calls      = 0;

    Registry r;
    r.register_matcher("m_a",
        [&](clang::ast_matchers::MatchFinder&, QUnit&) {
            ++reg_matcher_calls;
        });
    r.register_matcher("m_b",
        [&](clang::ast_matchers::MatchFinder&, QUnit&) {
            ++reg_matcher_calls;
        });
    r.register_op("kind_a",
        [&](clang::ast_matchers::MatchFinder&, QUnit&) {
            ++reg_op_calls;
        },
        [](const sturm::transpile::QOperation&) { return std::string{}; });

    clang::ast_matchers::MatchFinder finder;
    QUnit unit;
    r.invoke_all(finder, unit);

    CHECK(reg_matcher_calls == 2);
    CHECK(reg_op_calls == 1);
}

// ── Test: invoke_all preserves insertion order ───────────────────────────────
// §6 ordering discipline: matchers drain in the order they were registered.
static void test_invoke_all_preserves_insertion_order() {
    std::vector<int> order;
    Registry r;
    r.register_matcher("first",
        [&](clang::ast_matchers::MatchFinder&, QUnit&) { order.push_back(1); });
    r.register_op("second",
        [&](clang::ast_matchers::MatchFinder&, QUnit&) { order.push_back(2); },
        [](const sturm::transpile::QOperation&) { return std::string{}; });
    r.register_matcher("third",
        [&](clang::ast_matchers::MatchFinder&, QUnit&) { order.push_back(3); });

    clang::ast_matchers::MatchFinder finder;
    QUnit unit;
    r.invoke_all(finder, unit);

    CHECK(order.size() == 3);
    CHECK(order[0] == 1);
    CHECK(order[1] == 2);
    CHECK(order[2] == 3);
}

// ── Test: invoke_all passes the caller-supplied finder + unit through ────────
static void test_invoke_all_passes_finder_and_unit_by_reference() {
    clang::ast_matchers::MatchFinder* seen_finder = nullptr;
    QUnit*                            seen_unit   = nullptr;

    Registry r;
    r.register_matcher("m",
        [&](clang::ast_matchers::MatchFinder& f, QUnit& u) {
            seen_finder = &f;
            seen_unit   = &u;
        });

    clang::ast_matchers::MatchFinder finder;
    QUnit unit;
    r.invoke_all(finder, unit);

    CHECK(seen_finder == &finder);
    CHECK(seen_unit   == &unit);
}

// ── Test: invoke_all on empty Registry is a no-op ────────────────────────────
static void test_invoke_all_empty_registry_is_noop() {
    Registry r;
    clang::ast_matchers::MatchFinder finder;
    QUnit unit;
    r.invoke_all(finder, unit);  // should not throw, abort, or crash
    CHECK(true);
}

// ── Test: two Registry instances have independent state ──────────────────────
// The epic calls this out explicitly: "Registry owned by TranspileConsumer
// (one per translation unit, not global singleton)". Two stack-allocated
// registries must not share matcher name sets.
static void test_two_registries_are_independent() {
    Registry a;
    Registry b;
    a.register_matcher("dup",
        [](clang::ast_matchers::MatchFinder&, QUnit&) {});
    // If state were global, this would abort. It does not.
    b.register_matcher("dup",
        [](clang::ast_matchers::MatchFinder&, QUnit&) {});
    CHECK(true);
}

// ── Test: find_render_fn returns the registered renderer, nullptr otherwise ──
static void test_find_render_fn() {
    Registry r;
    r.register_op("my_kind",
        [](clang::ast_matchers::MatchFinder&, QUnit&) {},
        [](const sturm::transpile::QOperation&) { return std::string{"RENDERED"}; });

    const UncomputeRenderFn* fn = r.find_render_fn("my_kind");
    CHECK(fn != nullptr);
    // Invoke through the stored fn to verify fidelity.
    if (fn != nullptr) {
        sturm::transpile::QOperation op{};
        CHECK((*fn)(op) == "RENDERED");
    }
    CHECK(r.find_render_fn("not_registered") == nullptr);
}

// ── Test: registrars() is a Meyer's singleton (process-wide identity) ────────
static void test_registrars_is_meyer_singleton() {
    auto& v1 = registrars();
    auto& v2 = registrars();
    CHECK(&v1 == &v2);
}

// ── Test: StaticRegistrar pushes onto registrars() ───────────────────────────
static void test_static_registrar_appends_to_registrars() {
    const std::size_t before = registrars().size();
    StaticRegistrar _r([](Registry&) {});
    const std::size_t after = registrars().size();
    CHECK(after == before + 1);
}

// ── Test: draining registrars() invokes each fn against a Registry ───────────
// Mirrors how TranspileConsumer::TranspileConsumer will iterate
// `registrars()` in PM4-3. We create a StaticRegistrar whose lambda ticks
// a counter, then drain and verify the lambda fired.
static void test_drain_registrars_invokes_each_fn() {
    int times_called = 0;
    // Push onto the shared vector. This is a process-wide side effect;
    // the test acknowledges it — subsequent tests in this binary should
    // not assume registrars() is empty.
    LinkTimeRegisterFn fn = [&](Registry&) { ++times_called; };
    registrars().push_back(std::move(fn));

    Registry r;
    // Drain. Mirrors the TranspileConsumer ctor contract for PM4-3.
    for (const auto& f : registrars()) {
        f(r);
    }
    CHECK(times_called >= 1);
}

}  // namespace sturm_test_plugin_registry_ns

int run_test_plugin_registry(int /*argc*/, char** /*argv*/) {
    using namespace sturm_test_plugin_registry_ns;
    using sturm_test_plugin_registry_ns::tests_run;
    using sturm_test_plugin_registry_ns::tests_pass;
    test_register_matcher_duplicate_name_aborts();
    test_register_op_duplicate_kind_id_aborts();
    test_matcher_name_and_kind_id_are_disjoint_namespaces();
    test_invoke_all_runs_every_registered_callback();
    test_invoke_all_preserves_insertion_order();
    test_invoke_all_passes_finder_and_unit_by_reference();
    test_invoke_all_empty_registry_is_noop();
    test_two_registries_are_independent();
    test_find_render_fn();
    test_registrars_is_meyer_singleton();
    test_static_registrar_appends_to_registrars();
    test_drain_registrars_invokes_each_fn();

    std::printf("PASS: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
