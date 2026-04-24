// test_when_scope_garbage_consume.cpp — sturm-njul
//
// Tests the WHEN scope-exit consumer for the garbage_registry introduced
// by sturm-njul. The consumer is a diagnostic-only pass: at WHEN scope
// exit it walks the records registered during the scope body, optionally
// emits a one-line stderr report, and pops those records from the
// thread-local registry so the inventory stays bounded by the depth of
// the WHEN nest rather than growing with every controlled lossy op.
//
// Coverage
// --------
// 1. Consumer API direct:
//    a. Empty scope: baseline == snapshot().size() pre and post — no
//       records consumed.
//    b. Single-record scope: one register_garbage between baseline capture
//       and consume_scope_garbage() returns the registry to baseline.
//    c. Multi-record scope: N records registered; consumer pops all N.
//    d. Records registered BEFORE the baseline survive consumption
//       (outer-scope / pre-WHEN records are untouched).
// 2. WhenScopeGarbage RAII: captures baseline in ctor, consumes in dtor.
// 3. Nested WhenScopeGarbage: inner dtor consumes only the inner
//    contribution; outer dtor consumes whatever the outer scope added on
//    top of the inner's pre-consumption state.
// 4. Op-tag independence: a mix of all seven source_op_tag values
//    registered in one scope is fully consumed.
// 5. Diagnostic report: when enabled via the STURM_GARBAGE_REPORT env
//    var, the consumer writes a line to stderr whose shape matches the
//    documented format.
// 6. Report disabled by default: consumer is silent, still consumes.
//
// Harness: plain assert + printf (no gtest). This test is non-backend —
// it manipulates the registry directly rather than via a backend WHEN
// body, which is the simplest way to exercise the consumer logic in
// isolation. Backend end-to-end coverage is provided by the existing
// test_when_and_or_lossy.cpp / test_when_mul_lossy.cpp / etc. which will
// now see their per-scope record count consumed on scope exit (unchanged
// forward semantics, smaller post-scope registry snapshot).

#include "sturm/control/garbage_registry.hpp"
#include "sturm/control/when_scope_garbage.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using sturm::detail::WhenScopeGarbage;
using sturm::detail::consume_scope_garbage;
using sturm::detail::garbage_report_enabled;
using sturm::detail::ScopedGarbageConsumeGuard;
using sturm::detail::set_garbage_consume_enabled;
using sturm::detail::get_garbage_consume_enabled;
using sturm::detail::garbage_registry::clear;
using sturm::detail::garbage_registry::record;
using sturm::detail::garbage_registry::register_garbage;
using sturm::detail::garbage_registry::snapshot;
using sturm::detail::garbage_registry::source_op_tag;

// Small helper to push a record with a fresh one-element qubit-index array.
static void push_one(source_op_tag tag, int ctrl, int sentinel_idx) {
    const int idx[1] = {sentinel_idx};
    register_garbage(tag, ctrl, 1, idx);
}

// ── 1a. Empty scope ──────────────────────────────────────────────────────────
static void test_consume_empty_scope() {
    clear();
    const std::size_t baseline = snapshot().size();
    assert(baseline == 0);
    consume_scope_garbage(baseline);
    assert(snapshot().size() == 0);
    std::puts("PASS: test_consume_empty_scope");
}

// ── 1b/1c. Single and multi-record scopes ────────────────────────────────────
static void test_consume_single_record() {
    clear();
    const std::size_t baseline = snapshot().size();
    push_one(source_op_tag::AND_ASSIGN, /*ctrl=*/5, /*idx=*/42);
    assert(snapshot().size() == baseline + 1);
    consume_scope_garbage(baseline);
    assert(snapshot().size() == baseline);
    std::puts("PASS: test_consume_single_record");
}

static void test_consume_multi_record() {
    clear();
    const std::size_t baseline = snapshot().size();
    for (int i = 0; i < 5; ++i) {
        push_one(source_op_tag::MUL_ASSIGN, /*ctrl=*/7, /*idx=*/100 + i);
    }
    assert(snapshot().size() == baseline + 5);
    consume_scope_garbage(baseline);
    assert(snapshot().size() == baseline);
    std::puts("PASS: test_consume_multi_record");
}

// ── 1d. Records below baseline survive ──────────────────────────────────────
static void test_consume_preserves_pre_scope_records() {
    clear();
    // Two records exist before the WHEN scope starts.
    push_one(source_op_tag::MUL_UPPER_W, /*ctrl=*/-1, /*idx=*/1);
    push_one(source_op_tag::DIV_REMAINDER, /*ctrl=*/-1, /*idx=*/2);
    const std::size_t baseline = snapshot().size();
    assert(baseline == 2);

    // Inside the WHEN: register three more.
    push_one(source_op_tag::AND_ASSIGN, /*ctrl=*/3, /*idx=*/10);
    push_one(source_op_tag::OR_ASSIGN,  /*ctrl=*/3, /*idx=*/11);
    push_one(source_op_tag::AND_ASSIGN, /*ctrl=*/3, /*idx=*/12);
    assert(snapshot().size() == baseline + 3);

    consume_scope_garbage(baseline);

    // Only the pre-scope records remain.
    assert(snapshot().size() == 2);
    assert(snapshot()[0].tag == source_op_tag::MUL_UPPER_W);
    assert(snapshot()[1].tag == source_op_tag::DIV_REMAINDER);
    assert(snapshot()[0].qubit_indices[0] == 1);
    assert(snapshot()[1].qubit_indices[0] == 2);
    std::puts("PASS: test_consume_preserves_pre_scope_records");
}

// ── 2. WhenScopeGarbage RAII captures / consumes ─────────────────────────────
static void test_when_scope_garbage_raii() {
    clear();
    {
        WhenScopeGarbage guard;
        // The guard captured baseline=0.
        assert(guard.baseline() == 0);
        push_one(source_op_tag::AND_ASSIGN, 0, 1);
        push_one(source_op_tag::MUL_ASSIGN, 0, 2);
        assert(snapshot().size() == 2);
    } // guard dtor consumes both records.
    assert(snapshot().empty());
    std::puts("PASS: test_when_scope_garbage_raii");
}

// ── 3. Nested scopes ────────────────────────────────────────────────────────
// Simulates: WHEN(outer) { push A; WHEN(inner) { push B; push C; } push D; }
//   - At inner dtor: B and C are consumed; A and D survive (D not yet pushed).
//   - At outer dtor: A and D are consumed; registry ends empty.
static void test_nested_when_scope_garbage() {
    clear();
    {
        WhenScopeGarbage outer;
        assert(outer.baseline() == 0);
        push_one(source_op_tag::AND_ASSIGN, 1, 100); // "A"
        assert(snapshot().size() == 1);

        {
            WhenScopeGarbage inner;
            // Inner baseline sees the outer's A already present.
            assert(inner.baseline() == 1);
            push_one(source_op_tag::OR_ASSIGN, 2, 200); // "B"
            push_one(source_op_tag::MUL_ASSIGN, 2, 201); // "C"
            assert(snapshot().size() == 3);
        } // inner dtor consumes B and C; registry drops to size 1 (just A).
        assert(snapshot().size() == 1);
        assert(snapshot()[0].qubit_indices[0] == 100);

        push_one(source_op_tag::DIV_ASSIGN, 1, 300); // "D"
        assert(snapshot().size() == 2);
    } // outer dtor consumes A and D.
    assert(snapshot().empty());
    std::puts("PASS: test_nested_when_scope_garbage");
}

// ── 4. Mixed-tag scope consumed in one pass ─────────────────────────────────
static void test_consume_all_tags_mixed() {
    clear();
    const source_op_tag all[] = {
        source_op_tag::AND_ASSIGN,
        source_op_tag::OR_ASSIGN,
        source_op_tag::MUL_ASSIGN,
        source_op_tag::DIV_ASSIGN,
        source_op_tag::MOD_ASSIGN,
        source_op_tag::MUL_UPPER_W,
        source_op_tag::DIV_REMAINDER,
    };
    {
        WhenScopeGarbage guard;
        assert(guard.baseline() == 0);
        int idx = 1;
        for (auto t : all) {
            push_one(t, /*ctrl=*/9, /*idx=*/idx++);
        }
        assert(snapshot().size() == 7);
    }
    assert(snapshot().empty());
    std::puts("PASS: test_consume_all_tags_mixed");
}

// ── 5. Diagnostic report: format check when enabled ─────────────────────────
// Redirects stderr to a temp file and runs the consumer on a fresh thread
// so that garbage_report_enabled()'s thread-local cache sees the env var
// from a clean state.
static void test_diagnostic_report_format() {
#ifdef STURM_GARBAGE_REPORT
    std::puts("SKIP: test_diagnostic_report_format (forced on at compile time)");
    return;
#else
    setenv("STURM_GARBAGE_REPORT", "1", /*overwrite=*/1);

    const char* path = "/tmp/sturm_njul_report.txt";
    std::remove(path);

    std::fflush(stderr);
    FILE* redirected = std::freopen(path, "w", stderr);
    assert(redirected != nullptr);

    // Run on a separate thread: garbage_report_enabled() caches its env-var
    // lookup per-thread, so we need a thread that has NOT seen the disabled
    // state earlier in this run. The child thread has its own TLS registry
    // AND its own cache, so we also need to register / consume there.
    std::thread([&] {
        clear();
        {
            WhenScopeGarbage guard;
            push_one(source_op_tag::AND_ASSIGN, 7, 1);
            push_one(source_op_tag::MUL_ASSIGN, 7, 2);
            push_one(source_op_tag::MUL_ASSIGN, 7, 3);
        }
    }).join();

    std::fflush(stderr);
    // Read back the captured file before we restore stderr — avoids any
    // ordering issues with libc's stderr buffer.
    FILE* f = std::fopen(path, "r");
    assert(f != nullptr);
    char buf[512] = {};
    size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    std::string captured(buf, n);

    // Expected shape:
    //   [STURM garbage] WHEN scope exit: 3 records leaked (tags: AND=1 MUL=2)
    bool shape_ok =
        captured.find("[STURM garbage] WHEN scope exit:") != std::string::npos &&
        captured.find("3 records leaked") != std::string::npos &&
        captured.find("AND=1") != std::string::npos &&
        captured.find("MUL=2") != std::string::npos;

    // Unset before asserting so a failing assert doesn't leave the env set.
    unsetenv("STURM_GARBAGE_REPORT");

    if (!shape_ok) {
        std::fprintf(stdout,
                     "FAIL: test_diagnostic_report_format — captured=%s\n",
                     captured.c_str());
    }
    assert(shape_ok);
    std::puts("PASS: test_diagnostic_report_format");
#endif
}

// ── 7. Consumer opt-out via ScopedGarbageConsumeGuard ───────────────────────
// When the consumer is disabled via the TLS flag, records accumulate across
// scopes — this is the behaviour the existing h5it/pqs0 tests rely on.
static void test_scoped_consume_guard_preserves_records() {
    clear();
    {
        ScopedGarbageConsumeGuard off(false);
        assert(get_garbage_consume_enabled() == false);
        {
            WhenScopeGarbage guard;
            push_one(source_op_tag::AND_ASSIGN, 1, 1);
            push_one(source_op_tag::MUL_ASSIGN, 1, 2);
        } // guard dtor: would have consumed; suppressed by the off guard.
        assert(snapshot().size() == 2 &&
               "ScopedGarbageConsumeGuard(false) must preserve post-scope records");
    }
    // Back to default (enabled).
    assert(get_garbage_consume_enabled() == true);
    // Clean up the leftover records the disabled pass let through.
    clear();
    std::puts("PASS: test_scoped_consume_guard_preserves_records");
}

static void test_scoped_consume_guard_restores_prev_state() {
    clear();
    assert(get_garbage_consume_enabled() == true);
    {
        ScopedGarbageConsumeGuard off(false);
        assert(get_garbage_consume_enabled() == false);
        {
            ScopedGarbageConsumeGuard on(true);
            assert(get_garbage_consume_enabled() == true);
        }
        // Inner guard destroyed → back to false (outer guard's state).
        assert(get_garbage_consume_enabled() == false);
    }
    // Outer guard destroyed → back to true (baseline).
    assert(get_garbage_consume_enabled() == true);
    std::puts("PASS: test_scoped_consume_guard_restores_prev_state");
}

// ── 6. Report disabled by default: silent but still consumes ────────────────
static void test_report_disabled_still_consumes() {
    // Default: STURM_GARBAGE_REPORT unset → garbage_report_enabled() false.
    // Consumption still runs.
#ifndef STURM_GARBAGE_REPORT
    unsetenv("STURM_GARBAGE_REPORT");
#endif
    clear();
    {
        WhenScopeGarbage guard;
        push_one(source_op_tag::AND_ASSIGN, 1, 1);
        push_one(source_op_tag::OR_ASSIGN, 1, 2);
    }
    assert(snapshot().empty());
    std::puts("PASS: test_report_disabled_still_consumes");
}

int main() {
    test_consume_empty_scope();
    test_consume_single_record();
    test_consume_multi_record();
    test_consume_preserves_pre_scope_records();
    test_when_scope_garbage_raii();
    test_nested_when_scope_garbage();
    test_consume_all_tags_mixed();
    test_scoped_consume_guard_preserves_records();
    test_scoped_consume_guard_restores_prev_state();
    test_report_disabled_still_consumes();
    // Diagnostic-report format test runs last because it manipulates stderr.
    test_diagnostic_report_format();
    std::puts("All when_scope_garbage_consume tests passed.");
    return 0;
}
