// test_mixed_quantum_classical.cpp -- M5: Integration/acceptance tests for
// mixed quantum/classical operations across all four fix modules.
//
// Covers all 8 acceptance criteria from the PRD:
//   AC1: a ^= b (quantum ^= classical) emits X gates for set bits
//   AC2: a ^= b (classical ^= quantum) promotes a and emits CNOT gates
//   AC3: a &= b (quantum &= classical) emits gates for bits where b=1
//   AC4: a |= b (quantum |= classical) emits gates
//   AC5: c = a & b (free &, mixed) emits gates
//   AC6: c = a | b (free |, mixed) emits gates
//   AC7: a += b (quantum += classical) emits adder circuit
//   AC8: Regression: both-classical still zero gates
//
// Harness: APPEND mode BackendContext with gate IR inspection.
//          W=4 for reasonable gate counts.  Plain assert + printf (no gtest).
//
// Dependencies: M1 (sturm-fbt), M2 (sturm-ieb), M3 (sturm-8w8), M4 (sturm-5hy)

#define STURM_BACKEND_ENABLED 1
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/ir.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>

// -- APPEND-mode context RAII wrapper ----------------------------------------

struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit ScopedAppendCtx(uint32_t max_q = 128u) {
        ctx = sturm_backend_create(STURM_MODE_APPEND, max_q);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    sturm::GateIR& ir() { return ctx->ir; }
};

// -- Helpers -----------------------------------------------------------------

static constexpr std::size_t W = 4u;

// Build a quantum qint_t<W> with pre-reserved qubit indices starting at base.
static sturm::qint_t<W> make_quantum(int64_t val, uint32_t base) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = (1u << W) - 1u;  // all bits quantum
    for (uint32_t i = 0; i < W; ++i)
        q.qubits[i] = static_cast<int>(base + i);
    return q;
}

// Build a classical qint_t<W> (all qubits = -1, super_mask = 0).
static sturm::qint_t<W> make_classical(int64_t val) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = 0u;
    // qubits already defaulted to -1
    return q;
}

// Prevent double-release by clearing qubit indices and super_mask.
static void clear_q(sturm::qint_t<W>& q) {
    q.qubits.fill(-1);
    q.super_mask = 0u;
}

// Pre-reserve N qubits from a freshly-reset pool.
template <std::size_t N>
static void reserve_regs(int (&reserved)[N]) {
    sturm::QubitPool::instance().reset_for_testing();
    for (uint32_t i = 0; i < N; ++i)
        reserved[i] = sturm::QubitPool::instance().allocate();
    assert(reserved[0] == 0 && "pool must start at index 0");
}

template <std::size_t N>
static void release_regs(int (&reserved)[N]) {
    for (uint32_t i = 0; i < N; ++i) {
        if (reserved[i] >= 0) {
            sturm::QubitPool::instance().release(reserved[i]);
            reserved[i] = -1;
        }
    }
}

// =============================================================================
// Test 1 (AC1): XOR quantum ^= classical emits X gates
//   a = 5 (0b0101) quantum, b = 3 (0b0011) classical
//   Bits where b=1: bit 0 and bit 1 => expect X gates on those target qubits.
//   Result: a.value = 5 ^ 3 = 6 (0b0110)
// =============================================================================

static void test_ac1_xor_quantum_assign_classical() {
    int reserved[W];
    reserve_regs<W>(reserved);
    ScopedAppendCtx sc;

    {
        auto a = make_quantum(5LL, 0u);
        auto b = make_classical(3LL);

        std::size_t before = sc.ir().size();
        a ^= b;
        std::size_t after = sc.ir().size();

        assert(a.value == (5LL ^ 3LL) &&
               "AC1: a ^= b value must be 5^3=6");
        assert(after > before &&
               "AC1: quantum ^= classical must emit gates");

        // Check that X gates were emitted for bits where b=1.
        // b=3 means bits 0 and 1 set.  For each, an X gate on a's qubit.
        bool found_x = false;
        for (std::size_t g = before; g < after; ++g) {
            if (sc.ir().at(g).kind == STURM_GATE_X)
                found_x = true;
        }
        assert(found_x &&
               "AC1: must emit X gate(s) for classical b bits that are 1");

        clear_q(a);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: AC1 — quantum ^= classical emits X gates\n");
}

// =============================================================================
// Test 2 (AC2): XOR classical ^= quantum promotes and emits CNOT
//   a = 3 (0b0011) classical, b = 5 (0b0101) quantum
//   a should be promoted (qubits allocated), CNOT gates emitted.
//   Result: a.value = 3 ^ 5 = 6 (0b0110)
// =============================================================================

static void test_ac2_xor_classical_assign_quantum() {
    int reserved[W];
    reserve_regs<W>(reserved);
    ScopedAppendCtx sc;

    {
        auto a = make_classical(3LL);
        auto b = make_quantum(5LL, 0u);

        // a starts classical -- no qubits.
        assert(a.qubits[0] == -1 && "AC2 precondition: a must be classical");

        std::size_t before = sc.ir().size();
        a ^= b;
        std::size_t after = sc.ir().size();

        assert(a.value == (3LL ^ 5LL) &&
               "AC2: a ^= b value must be 3^5=6");
        assert(after > before &&
               "AC2: classical ^= quantum must emit gates");

        // a must have been promoted: at least some qubits allocated.
        bool any_promoted = false;
        for (uint32_t i = 0; i < W; ++i) {
            if (a.qubits[i] >= 0) any_promoted = true;
        }
        assert(any_promoted &&
               "AC2: classical a must be promoted (qubits allocated)");

        // Check for CNOT (CX) gates in the emitted IR.
        bool found_cx = false;
        for (std::size_t g = before; g < after; ++g) {
            if (sc.ir().at(g).kind == STURM_GATE_CX)
                found_cx = true;
        }
        assert(found_cx &&
               "AC2: classical ^= quantum must emit CX (CNOT) gates");

        clear_q(a);
        clear_q(b);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: AC2 — classical ^= quantum promotes and emits CNOT\n");
}

// =============================================================================
// Test 3 (AC3): AND quantum &= classical emits gates for set bits
//   a = 7 (0b0111) quantum, b = 5 (0b0101) classical
//   Result: a.value = 7 & 5 = 5 (0b0101)
//   Gates should be emitted (AND decomposition with classical folding).
// =============================================================================

static void test_ac3_and_quantum_assign_classical() {
    int reserved[W];
    reserve_regs<W>(reserved);
    ScopedAppendCtx sc;

    {
        auto a = make_quantum(7LL, 0u);
        auto b = make_classical(5LL);

        std::size_t before = sc.ir().size();
        a &= b;
        std::size_t after = sc.ir().size();

        assert(a.value == (7LL & 5LL) &&
               "AC3: a &= b value must be 7&5=5");
        assert(after > before &&
               "AC3: quantum &= classical must emit gates");

        clear_q(a);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: AC3 — quantum &= classical emits gates\n");
}

// =============================================================================
// Test 4 (AC4): OR quantum |= classical emits gates
//   a = 5 (0b0101) quantum, b = 2 (0b0010) classical
//   Result: a.value = 5 | 2 = 7 (0b0111)
//   Gates should be emitted (OR decomposition with classical folding).
// =============================================================================

static void test_ac4_or_quantum_assign_classical() {
    int reserved[W];
    reserve_regs<W>(reserved);
    ScopedAppendCtx sc;

    {
        auto a = make_quantum(5LL, 0u);
        auto b = make_classical(2LL);

        std::size_t before = sc.ir().size();
        a |= b;
        std::size_t after = sc.ir().size();

        assert(a.value == (5LL | 2LL) &&
               "AC4: a |= b value must be 5|2=7");
        assert(after > before &&
               "AC4: quantum |= classical must emit gates");

        clear_q(a);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: AC4 — quantum |= classical emits gates\n");
}

// =============================================================================
// Test 5 (AC5): Free & mixed emits gates
//   a = 5 (0b0101) quantum, b = 6 (0b0110) classical
//   c = a & b => c.value = 4 (0b0100)
//   Gates should be emitted, c should have qubits allocated.
// =============================================================================

static void test_ac5_free_and_mixed() {
    int reserved[W];
    reserve_regs<W>(reserved);
    ScopedAppendCtx sc;

    {
        auto a = make_quantum(5LL, 0u);
        auto b = make_classical(6LL);

        std::size_t before = sc.ir().size();
        auto c = a & b;
        std::size_t after = sc.ir().size();

        assert(c.value == (5LL & 6LL) &&
               "AC5: c = a & b value must be 5&6=4");
        assert(after > before &&
               "AC5: free & mixed must emit gates");

        // Result register should have qubits allocated.
        bool has_qubits = false;
        for (uint32_t i = 0; i < W; ++i) {
            if (c.qubits[i] >= 0) has_qubits = true;
        }
        assert(has_qubits &&
               "AC5: c = a & b result must have allocated qubits");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: AC5 — free & mixed emits gates\n");
}

// =============================================================================
// Test 6 (AC6): Free | mixed emits gates
//   a = 5 (0b0101) quantum, b = 6 (0b0110) classical
//   c = a | b => c.value = 7 (0b0111)
//   Gates should be emitted, c should have qubits allocated.
// =============================================================================

static void test_ac6_free_or_mixed() {
    int reserved[W];
    reserve_regs<W>(reserved);
    ScopedAppendCtx sc;

    {
        auto a = make_quantum(5LL, 0u);
        auto b = make_classical(6LL);

        std::size_t before = sc.ir().size();
        auto c = a | b;
        std::size_t after = sc.ir().size();

        assert(c.value == (5LL | 6LL) &&
               "AC6: c = a | b value must be 5|6=7");
        assert(after > before &&
               "AC6: free | mixed must emit gates");

        // Result register should have qubits allocated.
        bool has_qubits = false;
        for (uint32_t i = 0; i < W; ++i) {
            if (c.qubits[i] >= 0) has_qubits = true;
        }
        assert(has_qubits &&
               "AC6: c = a | b result must have allocated qubits");

        clear_q(a);
        clear_q(b);
        clear_q(c);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: AC6 — free | mixed emits gates\n");
}

// =============================================================================
// Test 7 (AC7): ADD quantum += classical emits adder circuit
//   a = 3 (0b0011) quantum, b = 5 (0b0101) classical
//   Result: a.value = 3 + 5 = 8
//   Adder circuit should be emitted (multi-gate sequence).
// =============================================================================

static void test_ac7_add_quantum_assign_classical() {
    int reserved[W];
    reserve_regs<W>(reserved);
    ScopedAppendCtx sc;

    std::size_t gate_count = 0;
    {
        auto a = make_quantum(3LL, 0u);
        auto b = make_classical(5LL);

        std::size_t before = sc.ir().size();
        a += b;
        std::size_t after = sc.ir().size();

        assert(a.value == 8LL &&
               "AC7: a += b value must be 3+5=8");
        assert(after > before &&
               "AC7: quantum += classical must emit gates");

        gate_count = after - before;
        assert(gate_count > 1 &&
               "AC7: adder circuit should emit multiple gates");

        clear_q(a);
    }

    release_regs<W>(reserved);
    std::printf("  PASS: AC7 — quantum += classical emits adder circuit "
                "(%zu gates)\n", gate_count);
}

// =============================================================================
// Test 8 (AC8): Regression: both-classical still zero gates
//   a = 5 classical, b = 3 classical, a ^= b
//   No gates emitted, super_mask stays 0, no qubits allocated.
// =============================================================================

static void test_ac8_both_classical_regression() {
    sturm::QubitPool::instance().reset_for_testing();
    ScopedAppendCtx sc;

    {
        auto a = make_classical(5LL);
        auto b = make_classical(3LL);

        std::size_t before = sc.ir().size();
        a ^= b;
        std::size_t after = sc.ir().size();

        assert(a.value == (5LL ^ 3LL) &&
               "AC8: a ^= b value must be 5^3=6");
        assert(a.super_mask == 0u &&
               "AC8: both-classical must have super_mask == 0");
        assert(after == before &&
               "AC8: both-classical must emit zero gates");

        // No qubits should be allocated.
        for (uint32_t i = 0; i < W; ++i) {
            assert(a.qubits[i] == -1 &&
                   "AC8: no qubits allocated for both-classical");
        }

        // Also verify &= and |= both-classical.
        auto c = make_classical(7LL);
        auto d = make_classical(5LL);

        std::size_t before2 = sc.ir().size();
        c &= d;
        std::size_t after2 = sc.ir().size();

        assert(c.value == (7LL & 5LL) &&
               "AC8: c &= d value must be 7&5=5");
        assert(after2 == before2 &&
               "AC8: both-classical &= must emit zero gates");

        auto e = make_classical(5LL);
        auto f = make_classical(2LL);

        std::size_t before3 = sc.ir().size();
        e |= f;
        std::size_t after3 = sc.ir().size();

        assert(e.value == (5LL | 2LL) &&
               "AC8: e |= f value must be 5|2=7");
        assert(after3 == before3 &&
               "AC8: both-classical |= must emit zero gates");

        // Also verify += both-classical.
        auto g = make_classical(3LL);
        auto h = make_classical(5LL);

        std::size_t before4 = sc.ir().size();
        g += h;
        std::size_t after4 = sc.ir().size();

        assert(g.value == 8LL &&
               "AC8: g += h value must be 3+5=8");
        assert(after4 == before4 &&
               "AC8: both-classical += must emit zero gates");
    }

    std::printf("  PASS: AC8 — both-classical regression (zero gates, "
                "super_mask==0)\n");
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::printf("test_mixed_quantum_classical: M5 integration tests for "
                "mixed quantum/classical operations (all PRD acceptance "
                "criteria)\n\n");

    test_ac1_xor_quantum_assign_classical();
    test_ac2_xor_classical_assign_quantum();
    test_ac3_and_quantum_assign_classical();
    test_ac4_or_quantum_assign_classical();
    test_ac5_free_and_mixed();
    test_ac6_free_or_mixed();
    test_ac7_add_quantum_assign_classical();
    test_ac8_both_classical_regression();

    std::printf("\nAll test_mixed_quantum_classical tests passed.\n");
    std::printf("All 8 PRD acceptance criteria verified.\n");
    return 0;
}
