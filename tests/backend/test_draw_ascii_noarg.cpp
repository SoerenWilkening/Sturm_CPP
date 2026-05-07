// test_draw_ascii_noarg.cpp — Frontend simpl. P5: no-argument renderer entry
// points (sturm-uoeb). TDD: written before implementation. Pins the contract
// of sturm::draw_ascii(), sturm::print_ascii(), sturm::gate_count() against
// the thread-local context per PRD §5.6.
//
// Cases (all five must be green):
//   1. empty IR → empty string
//   2. 1-qubit gate → 1-rail diagram (canvas width = max qubit index + 1)
//   3. multi-qubit IR → max-index+1 rails
//   4. gate count == ir.size()
//   5. null thread context → assert fires (death test)
//
// Note: sturm_get_thread_context() falls back to a process-wide default; the
// no-arg renderer queries the *raw* per-thread pointer (see
// sturm::current_thread_context_or_null()) so that calling it without an
// active lifecycle reliably trips the assert.

#include "sturm/backend/draw_ascii.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Helper: build a GateRecord inline.
static sturm::GateRecord mk(sturm_gate_kind_t k,
                            uint32_t q0, uint32_t q1, uint32_t q2,
                            uint8_t n, double p = 0.0) {
    sturm::GateRecord r{};
    r.kind = k;
    r.qubits = {q0, q1, q2};
    r.n = n;
    r.param = p;
    return r;
}

static std::size_t count_lines(const std::string& s) {
    std::size_t c = 0;
    for (char ch : s) if (ch == '\n') ++c;
    return c;
}

// ── Case 1: empty IR → empty string ───────────────────────────────────────────
static void test_empty_ir_returns_empty_string() {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_APPEND, 8u);
    sturm_set_thread_context(ctx);

    // No gates appended → IR is empty → diagram is the empty string.
    std::string s = sturm::draw_ascii();
    assert(s.empty() && "empty IR must yield empty string");
    assert(sturm::gate_count() == 0u && "empty IR → gate_count() == 0");

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
}

// ── Case 2: 1-qubit gate → 1-rail diagram ────────────────────────────────────
static void test_one_qubit_gate_one_rail() {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_APPEND, 8u);
    sturm_set_thread_context(ctx);

    // Append a single H gate on q0. Canvas width = 0 + 1 = 1 rail.
    ctx->ir.append(mk(STURM_GATE_H, 0u, 0u, 0u, 1));
    std::string s = sturm::draw_ascii();
    // One row → exactly one '\n'.
    assert(count_lines(s) == 1u && "1-qubit gate → exactly one rail");
    assert(s.find("q0:") != std::string::npos);
    assert(s.find("q1:") == std::string::npos);
    assert(s.find('H') != std::string::npos);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
}

// ── Case 3: multi-qubit IR → max-index+1 rails ───────────────────────────────
static void test_multi_qubit_max_index_plus_one_rails() {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_APPEND, 8u);
    sturm_set_thread_context(ctx);

    // Gates touching qubits {0, 2, 5}. Max index = 5 → canvas = 6 rails.
    ctx->ir.append(mk(STURM_GATE_H,  0u, 0u, 0u, 1));
    ctx->ir.append(mk(STURM_GATE_CX, 2u, 5u, 0u, 2));

    std::string s = sturm::draw_ascii();
    assert(count_lines(s) == 6u && "max qubit index 5 → 6 rails");
    for (int i = 0; i <= 5; ++i) {
        std::string tag = "q" + std::to_string(i) + ":";
        assert(s.find(tag) != std::string::npos &&
               "every rail through max index must be labelled");
    }
    assert(s.find("q6:") == std::string::npos &&
           "no rail beyond max+1");

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
}

// ── Case 4: gate_count() == ir.size() ────────────────────────────────────────
static void test_gate_count_matches_ir_size() {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_APPEND, 8u);
    sturm_set_thread_context(ctx);

    assert(sturm::gate_count() == 0u);
    ctx->ir.append(mk(STURM_GATE_H, 0u, 0u, 0u, 1));
    assert(sturm::gate_count() == 1u);
    ctx->ir.append(mk(STURM_GATE_X, 1u, 0u, 0u, 1));
    ctx->ir.append(mk(STURM_GATE_CX, 0u, 2u, 0u, 2));
    assert(sturm::gate_count() == 3u);
    assert(sturm::gate_count() == ctx->ir.size());

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
}

// ── Case 5: null thread context → assert fires (death test) ──────────────────
//
// Pattern mirrors tests/backend/test_simulate_oom.cpp: fork a child, ensure
// no thread context is installed (sturm_set_thread_context(nullptr)), call
// sturm::draw_ascii(), then expect SIGABRT (or non-zero exit) from the
// child. The fallback in sturm_get_thread_context() always yields the
// process default, so the no-arg renderer must consult the raw per-thread
// pointer (sturm::current_thread_context_or_null()) for the assert to fire.
static void test_null_context_aborts() {
    pid_t pid = fork();
    if (pid < 0) { std::perror("fork"); std::abort(); }

    if (pid == 0) {
        // Child: ensure no thread context, then call the renderer.
        sturm_set_thread_context(nullptr);
        // Silence stderr so the assert message doesn't pollute test output.
        std::freopen("/dev/null", "w", stderr);
        (void)sturm::draw_ascii();
        // If we reach here the assert didn't fire — that's a test failure.
        _exit(0);
    }

    // Parent.
    int status = 0;
    waitpid(pid, &status, 0);

    bool died = false;
    if (WIFSIGNALED(status)) died = true;          // SIGABRT, etc.
    else if (WIFEXITED(status)) died = (WEXITSTATUS(status) != 0);
    assert(died && "draw_ascii() with null thread context must abort");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    test_empty_ir_returns_empty_string();
    test_one_qubit_gate_one_rail();
    test_multi_qubit_max_index_plus_one_rails();
    test_gate_count_matches_ir_size();
    test_null_context_aborts();
    std::puts("test_draw_ascii_noarg: OK (5/5)");
    return 0;
}
