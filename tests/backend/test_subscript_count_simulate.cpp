// test_subscript_count_simulate.cpp — qint subscript operator[] (qbool extraction)
//   COUNT_ONLY and SIMULATE tests verifying the extracted bit is correct.
//   (sturm-qwt)
//
// Tests:
//   test_subscript_count_no_gate   — operator[] itself emits 0 gates in
//                                    COUNT_ONLY mode (pure index copy, PRD §6).
//   test_subscript_count_xor       — operator[] + ^= emits exactly 1 gate
//                                    (COUNT_ONLY); verifies correct qubit is targeted.
//   test_subscript_count_multiple  — subscript different bits of a 4-bit register
//                                    and XOR each into a target; gate_count == W.
//   test_subscript_simulate_value  — SIMULATE: a[i] correctly reflects the
//                                    classical value of bit i (checked against
//                                    statevector after X initialization).
//   test_subscript_simulate_xor    — SIMULATE: b[i] ^= a[j] flips the correct
//                                    qubit in the statevector.
//
// Harness: plain assert + printf (no gtest).

#define STURM_BACKEND_ENABLED 1
#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;

// ── COUNT_ONLY context RAII ───────────────────────────────────────────────────

struct CountCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit CountCtx(uint32_t max_q = 64u) {
        ctx = sturm_backend_create(STURM_MODE_COUNT_ONLY);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~CountCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

// ── SIMULATE context RAII ─────────────────────────────────────────────────────

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 64u) {
        bridge.allocate(n_qubits);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE);
        assert(ctx && "sturm_backend_create failed");
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    orkan::state_t& sv() { return bridge.state(); }
};

// ── Helpers ───────────────────────────────────────────────────────────────────

// Read a single qubit's bit value from the statevector (assumes a computational
// basis state).
static uint32_t read_qubit(orkan::state_t& sv, uint32_t qubit_idx, uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            return static_cast<uint32_t>((s >> qubit_idx) & 1u);
        }
    }
    return 0u;
}

// Build a qint_t<W> aliasing pre-allocated qubits at contiguous indices
// starting from base.
template <std::size_t W>
static sturm::qint_t<W> make_q(int64_t val, uint32_t base) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = 0u;  // classical — no superposition
    for (uint32_t i = 0; i < W; ++i) {
        q.qubits[i] = static_cast<int>(base + i);
    }
    return q;
}

// Prevent double-release by clearing qubit indices.
template <std::size_t W>
static void clear_q(sturm::qint_t<W>& q) {
    q.qubits.fill(-1);
    q.super_mask = 0u;
}

// =============================================================================
// test_subscript_count_no_gate
//
// PRD §6: "a[i] returns a non-owning qbool aliasing qubits[i]. No gate is
// emitted — it is a pure index copy."
//
// Verify that creating and immediately destroying a[i] in COUNT_ONLY mode
// emits zero gates.
// =============================================================================

static void test_subscript_count_no_gate() {
    sturm::QubitPool::instance().reset_for_testing();

    // Pre-allocate W register qubits.
    static constexpr std::size_t W = 4u;
    int reserved[W];
    for (uint32_t i = 0; i < W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at index 0");

    CountCtx cc;

    {
        sturm::qint_t<W> a = make_q<W>(0b1010LL, 0u);

        uint64_t before = cc.ctx->gate_count;

        // Subscript all 4 bits, immediately discard (no operation performed).
        for (std::size_t i = 0; i < W; ++i) {
            sturm::qbool bit = a[i];
            (void)bit;  // no ^= or other operation — should emit no gate
        }

        uint64_t after = cc.ctx->gate_count;

        assert(after == before &&
               "operator[] alone must emit 0 gates (pure index copy)");

        clear_q(a);
    }

    for (uint32_t i = 0; i < W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  test_subscript_count_no_gate: PASS\n");
}

// =============================================================================
// test_subscript_count_xor
//
// a[2] ^= a[0] in COUNT_ONLY mode must emit exactly 1 gate (CX).
// Verifies that the subscript correctly connects to qbool operators.
// =============================================================================

static void test_subscript_count_xor() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 4u;
    int reserved[W];
    for (uint32_t i = 0; i < W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    CountCtx cc;

    {
        sturm::qint_t<W> a = make_q<W>(0b0101LL, 0u);

        uint64_t before = cc.ctx->gate_count;

        {
            sturm::qbool tgt = a[2];  // non-owning, qubit index 2
            sturm::qbool src = a[0];  // non-owning, qubit index 0
            tgt ^= src;               // CX(src=0, tgt=2)
        }

        uint64_t after = cc.ctx->gate_count;

        assert(after - before == 1u &&
               "a[2] ^= a[0] must emit exactly 1 gate in COUNT_ONLY");

        clear_q(a);
    }

    for (uint32_t i = 0; i < W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  test_subscript_count_xor: PASS\n");
}

// =============================================================================
// test_subscript_count_multiple
//
// XOR each bit of a W=4 register into a fresh target qubit.
// Each subscript + ^= emits exactly 1 gate, so gate_count must increase by W.
// =============================================================================

static void test_subscript_count_multiple() {
    sturm::QubitPool::instance().reset_for_testing();

    static constexpr std::size_t W = 4u;
    // Reserve W qubits for register a and 1 qubit for the target.
    int reserved[W + 1u];
    for (uint32_t i = 0; i < W + 1u; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at index 0");

    CountCtx cc;

    {
        sturm::qint_t<W> a = make_q<W>(0b1011LL, 0u);

        // target is the extra qubit at index W
        int tgt_idx = reserved[W];

        uint64_t before = cc.ctx->gate_count;

        for (std::size_t i = 0; i < W; ++i) {
            sturm::qbool src = a[i];   // non-owning
            sturm::qbool tgt = sturm::qbool::make_non_owning(tgt_idx);
            tgt ^= src;                // CX(a[i], tgt)
        }

        uint64_t after = cc.ctx->gate_count;

        assert(after - before == W &&
               "subscript + ^= on each of W bits must emit exactly W gates");

        clear_q(a);
    }

    for (uint32_t i = 0; i < W + 1u; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  test_subscript_count_multiple: PASS\n");
}

// =============================================================================
// test_subscript_simulate_value
//
// Verify that a[i].value and a[i].qubits[0] are correct for a qint whose
// statevector has been initialized with Pauli-X on certain qubits.
//
// Pattern: val = 0b1010 (binary LSB-first: bit0=0, bit1=1, bit2=0, bit3=1).
//   a[0].value == 0, a[0].qubits[0] == 0
//   a[1].value == 1, a[1].qubits[0] == 1
//   a[2].value == 0, a[2].qubits[0] == 2
//   a[3].value == 1, a[3].qubits[0] == 3
//
// Also verify statevector qubit state matches a[i].value after initialization.
// =============================================================================

static void test_subscript_simulate_value() {
    static constexpr std::size_t W = 4u;
    static constexpr uint32_t n_orkan = W + 2u;  // small headroom

    sturm::QubitPool::instance().reset_for_testing();

    int reserved[W];
    for (uint32_t i = 0; i < W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }

    SimCtx sc{n_orkan, 64u};

    // Initialize statevector: val = 0b1010 (bits 1 and 3 are |1⟩).
    static constexpr uint64_t val = 0b1010ULL;
    for (uint32_t i = 0; i < W; ++i) {
        if ((val >> i) & 1u) {
            orkan::apply_x(sc.sv(), i);
        }
    }

    {
        sturm::qint_t<W> a = make_q<W>(static_cast<int64_t>(val), 0u);

        for (std::size_t i = 0; i < W; ++i) {
            sturm::qbool bit = a[i];

            // 1. Non-owning flag must be set.
            assert(!bit.owning_ &&
                   "a[i] must return a non-owning qbool");

            // 2. Qubit index must match.
            assert(bit.qubits[0] == static_cast<int>(i) &&
                   "a[i].qubits[0] must equal i");

            // 3. Classical value must match bit i of val.
            bool expected_val = ((val >> i) & 1u) != 0u;
            assert(bit.value == expected_val &&
                   "a[i].value must reflect bit i of the classical value");

            // 4. Statevector must agree: qubit i must be in state |expected_val⟩.
            uint32_t sv_bit = read_qubit(sc.sv(), static_cast<uint32_t>(i), n_orkan);
            assert(sv_bit == static_cast<uint32_t>(expected_val) &&
                   "statevector qubit i must match a[i].value");
        }

        clear_q(a);
    }

    for (uint32_t i = 0; i < W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  test_subscript_simulate_value: PASS\n");
}

// =============================================================================
// test_subscript_simulate_xor
//
// SIMULATE: b[i] ^= a[j] must flip the correct qubit in the statevector.
//
// Setup:
//   a = 0b0101 (W=4), so a[0]=1, a[1]=0, a[2]=1, a[3]=0
//   b = 0b0000 (W=4), all zero
//   Perform b[0] ^= a[0]  →  b[0] should become 1  (CX(qa0, qb0))
//   Perform b[2] ^= a[2]  →  b[2] should become 1  (CX(qa2, qb2))
//   b[1] and b[3] must remain 0.
//
// Qubit layout:
//   q[0..3] = a (a_base = 0)
//   q[4..7] = b (b_base = 4)
//   q[8..9] = headroom
//
// Orkan size: 10 qubits.
// =============================================================================

static void test_subscript_simulate_xor() {
    static constexpr std::size_t W = 4u;
    static constexpr uint32_t a_base = 0u;
    static constexpr uint32_t b_base = W;            // 4
    static constexpr uint32_t n_reg   = 2u * W;      // 8
    static constexpr uint32_t n_orkan = n_reg + 2u;  // 10

    sturm::QubitPool::instance().reset_for_testing();

    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at index 0");

    SimCtx sc{n_orkan, 64u};

    // Initialize a = 0b0101: X on qubits 0 and 2.
    static constexpr uint64_t a_val = 0b0101ULL;
    for (uint32_t i = 0; i < W; ++i) {
        if ((a_val >> i) & 1u) {
            orkan::apply_x(sc.sv(), a_base + i);
        }
    }
    // b starts all-zero (no X needed).

    uint64_t gates_before = sc.ctx->gate_count;

    {
        sturm::qint_t<W> a = make_q<W>(static_cast<int64_t>(a_val), a_base);
        sturm::qint_t<W> b = make_q<W>(0LL, b_base);

        // b[0] ^= a[0]  →  CX(qubit 0, qubit 4)
        {
            sturm::qbool lhs = b[0];  // non-owning, qubit b_base + 0 = 4
            sturm::qbool rhs = a[0];  // non-owning, qubit a_base + 0 = 0
            lhs ^= rhs;
        }

        // b[2] ^= a[2]  →  CX(qubit 2, qubit 6)
        {
            sturm::qbool lhs = b[2];  // non-owning, qubit b_base + 2 = 6
            sturm::qbool rhs = a[2];  // non-owning, qubit a_base + 2 = 2
            lhs ^= rhs;
        }

        // Gate count: exactly 2 CX gates emitted.
        uint64_t gates_after = sc.ctx->gate_count;
        assert(gates_after - gates_before == 2u &&
               "two b[i] ^= a[j] operations must emit exactly 2 gates");

        // Statevector checks:
        // b[0] (qubit 4) must be 1 (flipped by CX since a[0]=1).
        assert(read_qubit(sc.sv(), b_base + 0u, n_orkan) == 1u &&
               "b[0] must be 1 after b[0] ^= a[0] (a[0]=1)");

        // b[1] (qubit 5) must be 0 (not touched).
        assert(read_qubit(sc.sv(), b_base + 1u, n_orkan) == 0u &&
               "b[1] must remain 0 (not targeted)");

        // b[2] (qubit 6) must be 1 (flipped by CX since a[2]=1).
        assert(read_qubit(sc.sv(), b_base + 2u, n_orkan) == 1u &&
               "b[2] must be 1 after b[2] ^= a[2] (a[2]=1)");

        // b[3] (qubit 7) must be 0 (not touched).
        assert(read_qubit(sc.sv(), b_base + 3u, n_orkan) == 0u &&
               "b[3] must remain 0 (not targeted)");

        // a qubits must be unchanged (CX does not affect the control).
        for (uint32_t i = 0; i < W; ++i) {
            uint32_t expected = static_cast<uint32_t>((a_val >> i) & 1u);
            assert(read_qubit(sc.sv(), a_base + i, n_orkan) == expected &&
                   "a qubits must be unchanged after b ^= a operations");
        }

        clear_q(a);
        clear_q(b);
    }

    for (uint32_t i = 0; i < n_reg; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  test_subscript_simulate_xor: PASS\n");
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::printf("test_subscript_count_simulate: "
                "qint operator[] COUNT_ONLY and SIMULATE tests\n\n");

    test_subscript_count_no_gate();
    test_subscript_count_xor();
    test_subscript_count_multiple();
    test_subscript_simulate_value();
    test_subscript_simulate_xor();

    std::printf("\nAll test_subscript_count_simulate tests passed.\n");
    return 0;
}
