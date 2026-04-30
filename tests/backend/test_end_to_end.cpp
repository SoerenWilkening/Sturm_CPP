// test_end_to_end.cpp — M26: End-to-end acceptance — c = (a + 5) >= 0
//
// Exercises the canonical one-liner from PRD §3 under all three execution modes.
//
//   sturm::qint_t<8> a(input_value);
//   sturm::qbool     c = (a + 5LL) >= sturm::qint_t<8>(0);
//
// Note: uses 5LL (int64_t literal) to trigger the operator+(qint, int64_t) path
// which emits gates via add_const when the operand is quantum (super_mask != 0).
//
// Phase K PK-8 (sturm-ignd, 2026-04-17): the quantum-path gate-count pins that
// previously lived in this file (test_count_only_quantum expecting 16 gates and
// test_append_golden pinning the 8 H + 8 X golden record sequence) relied on
// the retired destructor-emitted sub_const inverse. Under principle B10
// destructors are release-only; runtime gate-count stability for lazy
// multi-step patterns (temporary `a+5LL` destructing mid-expression) is no
// longer a contract — only the transpile-path (tests/transpiler/
// test_gate_equivalence.cpp) byte-identical pairs are. The surviving tests
// below pin the classical fast-path and the SIMULATE answer only.
//
// Three test groups (post PK-8):
//
//   COUNT_ONLY — classical fast path pins exactly 0 gates. The quantum-path
//     pin was retired in PK-8.
//
//   SIMULATE — sweep a ∈ {-10..10} (classical qint, super_mask==0).
//     For each input assert c.value == (a_val + 5 >= 0).
//     This covers the SIMULATE classical fast path; full quantum simulation is
//     deferred until a real adder/comparator circuit lands.
//
// Harness: plain assert + main (no gtest), matching the project pattern.

#define STURM_BACKEND_ENABLED 1
// STURM_ANCILLA_CAPACITY is supplied by the build system (see
// tests/backend/CMakeLists.txt -- the per-target -U/-D pair pins it
// at 256 here, matching the pre-sturm-8n73 default).  Defining it at
// source level would clash with the project-wide -D set by the root
// CMakeLists.txt and trip -Wmacro-redefined (sturm-cgg5).

#include "sturm/core/core.h"
#include "sturm/core/context.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/qtypes/qint.hpp"

#include <cassert>
#include <cstdio>

// ── Fixture: scoped backend context ──────────────────────────────────────────
// Creates a context with the given mode, installs it as the thread-local
// context, and restores the previous context on destruction.

struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit ScopedCtx(sturm_mode_t mode) {
        ctx  = sturm_backend_create(mode, 17u);
        assert(ctx && "sturm_backend_create must not return null");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }

    ~ScopedCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    uint64_t gate_count() const {
        return sturm_gate_count(ctx);
    }

    const sturm::GateIR& ir() const {
        return ctx->ir;
    }
};

// ── Helper: run c = (a + 5) >= 0 with a fully classical qint value ────────────
// Returns c.value (the classical boolean result).
// No gates are emitted (super_mask==0 skips all stub loops).
// Uses int64_t literal 5LL to trigger operator+(qint, int64_t) path (which
// emits gates via add_const when super_mask != 0).

static bool run_classical(sturm_mode_t mode, int64_t a_val) {
    ScopedCtx sc(mode);
    using Q = sturm::qint_t<8>;
    Q a(a_val);
    sturm::qbool c = (a + 5LL) >= Q(0);
    return c.value;
}

// ── Test 1: COUNT_ONLY — classical operands yield zero gates ──────────────────
//
// With super_mask==0, add_const and sub_const both skip the per-bit loop.
// The compare stubs likewise skip (qbool.is_super==false → super_mask==0
// in as_qint_base()).  Pinned expected count: 0.

static void test_count_only_classical() {
    ScopedCtx sc(STURM_MODE_COUNT_ONLY);

    {
        using Q = sturm::qint_t<8>;
        Q a(3LL);                                    // classical, super_mask==0
        sturm::qbool c = (a + 5LL) >= Q(0);        // no gates (classical fast path)
        (void)c;
    }

    const uint64_t count = sc.gate_count();
    // Pinned: 0 gates for classical operands
    assert(count == 0u
           && "COUNT_ONLY classical: expected gate count == 0 (no quantum bits)");

    std::printf("  test_count_only_classical: PASS (gate count = %llu)\n",
                (unsigned long long)count);
}

// ── Phase K PK-8 (sturm-ignd): test_count_only_quantum + test_append_golden ──
// Those two tests pinned the runtime gate stream for `(a + 5LL) >= Q(0)` when
// `a` is quantum: 8 × H (add_const forward) + 8 × X (sub_const uncompute of
// the `a+5LL` temporary). The `X` tail was emitted by the temporary qint's
// destructor under the legacy uncompute_op tagged union — retired by PK-3 in
// favour of transpiler-driven uncompute. Principle B10 makes the post-temp
// gate count a non-contract, so the pin no longer has a referent and both
// tests were removed here. The transpile-path analogue is exercised by the
// byte-identical `GateRecord` pairs in tests/transpiler/test_gate_equivalence.cpp.

// ── Test 4: SIMULATE — swept classical inputs ─────────────────────────────────
//
// For classical a (super_mask==0), no quantum gates are emitted and the result
// is determined purely by classical value propagation.  Sweeps a ∈ {-10..10}
// and asserts c.value == (a_val + 5 >= 0).

static void test_simulate_swept() {
    for (int64_t a_val = -10LL; a_val <= 10LL; ++a_val) {
        const bool expected = (a_val + 5LL >= 0LL);
        const bool result   = run_classical(STURM_MODE_SIMULATE, a_val);

        assert(result == expected
               && "SIMULATE: c = (a+5)>=0 must equal mathematical answer");
    }

    std::printf("  test_simulate_swept: PASS (a in [-10..10], c == (a+5>=0))\n");
}

// ── Test 5: SIMULATE — boundary inputs ───────────────────────────────────────
//
// Explicitly verify the boundary cases for c = (a + 5) >= 0:
//   a = -5 → (a+5) = 0  → 0 >= 0 → true
//   a = -6 → (a+5) = -1 → -1 >= 0 → false
//   a = -4 → (a+5) = 1  → 1 >= 0 → true

static void test_simulate_boundary() {
    struct Case { int64_t a_val; bool expected; const char* label; };
    static const Case cases[] = {
        { -5LL,  true,  "a=-5  → (a+5)=0  >= 0 → true"  },
        { -6LL,  false, "a=-6  → (a+5)=-1 >= 0 → false" },
        { -4LL,  true,  "a=-4  → (a+5)=1  >= 0 → true"  },
        {  0LL,  true,  "a=0   → (a+5)=5  >= 0 → true"  },
        {  100LL, true,  "a=100 → large +  >= 0 → true"  },
        { -100LL, false, "a=-100→ large -  >= 0 → false" },
    };

    for (const auto& tc : cases) {
        const bool result = run_classical(STURM_MODE_SIMULATE, tc.a_val);
        assert(result == tc.expected && tc.label);
    }

    std::printf("  test_simulate_boundary: PASS (boundary cases verified)\n");
}

// ── Test 6: SIMULATE — result correct over all three modes ───────────────────
//
// The mathematical answer (a + 5 >= 0) must be identical across all three modes
// for classical inputs.

static void test_all_modes_agree() {
    for (int64_t a_val = -5LL; a_val <= 5LL; ++a_val) {
        const bool r_count    = run_classical(STURM_MODE_COUNT_ONLY, a_val);
        const bool r_append   = run_classical(STURM_MODE_APPEND,     a_val);
        const bool r_simulate = run_classical(STURM_MODE_SIMULATE,   a_val);
        const bool expected   = (a_val + 5LL >= 0LL);

        assert(r_count    == expected && "COUNT_ONLY mode: wrong classical result");
        assert(r_append   == expected && "APPEND mode: wrong classical result");
        assert(r_simulate == expected && "SIMULATE mode: wrong classical result");
    }

    std::printf("  test_all_modes_agree: PASS (all three modes give correct c.value)\n");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M26 end-to-end acceptance tests: c = (a + 5) >= 0\n");
    test_count_only_classical();
    // Phase K PK-8: test_count_only_quantum + test_append_golden retired
    // (runtime gate-count pins for lazy-expression patterns are no longer
    // contractual — see file header).
    test_simulate_swept();
    test_simulate_boundary();
    test_all_modes_agree();
    std::printf("All M26 end-to-end tests passed.\n");
    return 0;
}
