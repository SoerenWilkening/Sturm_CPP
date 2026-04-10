// test_bitwise_not_operator.cpp — sturm-s0h: bitwise NOT (~) on qint.
//
// Tests:
//   test_not_count_only — ~a on W-bit register emits exactly W X gates
//                         in COUNT_ONLY mode.
//   test_not_simulate   — ~a on W-bit register in SIMULATE mode: the
//                         result register reads all-ones (every bit flipped
//                         from the fresh |0> result register), classical
//                         value equals ~a.value (masked to W bits).
//
// operator~(qint_t<W>) is defined in qint_bitwise.hpp (STURM_BACKEND_ENABLED
// path). It allocates a fresh W-qubit result register, emits one X gate per
// result qubit (flip from |0> to |1>), and returns the new register.
//
// Harness: plain assert + printf (no gtest).

#define STURM_BACKEND_ENABLED 1
#include "sturm/qtypes/qint.hpp"
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

// ── RAII helpers ──────────────────────────────────────────────────────────────

struct CountCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit CountCtx(uint32_t max_q = 128u) {
        ctx  = sturm_backend_create(STURM_MODE_COUNT_ONLY, max_q);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~CountCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 128u) {
        bridge.allocate(n_qubits);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
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

// ── Statevector read helper ───────────────────────────────────────────────────
// Reads an n-qubit register (contiguous from base_q) from the dominant basis
// state.  n_total is the total number of qubits in the statevector.

static uint32_t read_reg(orkan::state_t& sv, uint32_t base_q, uint32_t n,
                         uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            uint32_t val = 0u;
            for (uint32_t k = 0; k < n; ++k) {
                val |= (static_cast<uint32_t>((s >> (base_q + k)) & 1u) << k);
            }
            return val;
        }
    }
    return 0u;
}

// Read register via explicit qubit-index array (needed when operator~ remaps
// result qubits to non-contiguous indices).
static uint32_t read_reg_idxs(orkan::state_t& sv, const int* qidx, uint32_t n,
                               uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            uint32_t val = 0u;
            for (uint32_t k = 0; k < n; ++k) {
                if (qidx[k] >= 0) {
                    val |= (static_cast<uint32_t>((s >> qidx[k]) & 1u) << k);
                }
            }
            return val;
        }
    }
    return 0u;
}

// =============================================================================
// test_not_count_only
//
// ~a on a W=4 quantum register in COUNT_ONLY mode must emit exactly W X gates.
// operator~(qint_t<W>) allocates a fresh result register and calls flip() on
// each result qubit — each flip() emits one X gate.
// =============================================================================

static void test_not_count_only() {
    static constexpr std::size_t W = 4u;
    static constexpr uint32_t    a_base = 0u;

    sturm::QubitPool::instance().reset_for_testing();

    // Pre-reserve qubits for 'a' so ancilla headroom starts beyond W.
    int reserved[W];
    for (uint32_t i = 0; i < W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at index 0");

    CountCtx sc;

    {
        sturm::qint_t<W> a;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
        }
        a.value      = 5LL;   // 0b0101 — classical value; quantum bits unset in sv
        a.super_mask = (1u << W) - 1u;

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> r = ~a;
        uint64_t after  = sc.ctx->gate_count;

        // COUNT_ONLY: gate_count must have increased.
        assert(after > before && "~a must emit gates in COUNT_ONLY mode");

        // Exactly W X gates — one per result qubit.
        assert((after - before) == W &&
               "~a must emit exactly W X gates (one per result qubit)");

        // Classical value: ~5 masked to W bits = 0b1010 = 10 for W=4.
        uint64_t mask = (uint64_t{1} << W) - 1u;
        assert((static_cast<uint64_t>(r.value) & mask) == (~static_cast<uint64_t>(5u) & mask) &&
               "classical value of ~a must equal ~a.value");

        // Prevent double-release of result qubits (they are freshly allocated).
        r.qubits.fill(-1);
        a.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_not_count_only (W=%zu, emits %zu X gates)\n", W, W);
}

// =============================================================================
// test_not_simulate
//
// ~a on a W=4 quantum register in SIMULATE mode:
//   - Fresh result register starts as |0...0>.
//   - operator~ emits X on each result qubit → result register becomes |1...1>.
//   - So ALL W bits of the result should be 1 (= 2^W - 1).
//   - 'a' register is unchanged (no gates emitted on a's qubits).
//   - Classical value equals ~a.value masked to W bits.
// =============================================================================

static void test_not_simulate() {
    static constexpr std::size_t W      = 4u;
    static constexpr uint32_t    a_base = 0u;
    // Total qubits: W for 'a' + W for result + headroom.
    static constexpr uint32_t    n_sv   = 2u * W + 8u;

    sturm::QubitPool::instance().reset_for_testing();

    // Pre-reserve 'a' qubits.
    int reserved[W];
    for (uint32_t i = 0; i < W; ++i) {
        reserved[i] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool must start at index 0");

    SimCtx sc{n_sv, 128u};

    // Initialise 'a' register to value 5 (0b0101).
    for (uint32_t i = 0; i < W; ++i) {
        if ((5u >> i) & 1u) {
            orkan::apply_x(sc.sv(), a_base + i);
        }
    }

    {
        sturm::qint_t<W> a;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
        }
        a.value      = 5LL;
        a.super_mask = (1u << W) - 1u;

        uint64_t before = sc.ctx->gate_count;
        sturm::qint_t<W> r = ~a;
        uint64_t after  = sc.ctx->gate_count;

        // Gates must have been emitted.
        assert(after > before && "~a must emit gates in SIMULATE mode");
        assert((after - before) == W &&
               "~a must emit exactly W X gates in SIMULATE mode");

        // Classical value check.
        uint64_t mask = (uint64_t{1} << W) - 1u;
        assert((static_cast<uint64_t>(r.value) & mask) == (~static_cast<uint64_t>(5u) & mask) &&
               "classical value of ~a must equal ~a.value masked");

        // Statevector check on result register.
        // Result register holds freshly allocated qubits — read via r.qubits[].
        uint32_t got_r = read_reg_idxs(sc.sv(), r.qubits.data(), W, n_sv);
        uint32_t expected_r = static_cast<uint32_t>((uint64_t{1} << W) - 1u);
        assert(got_r == expected_r &&
               "result register must be all-ones: every bit flipped from |0> by X");

        // 'a' register must be unchanged.
        uint32_t got_a = read_reg(sc.sv(), a_base, W, n_sv);
        assert(got_a == 5u && "'a' register must be unchanged after ~a");

        // Prevent double-release.
        r.qubits.fill(-1);
        a.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < W; ++i) {
        sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_not_simulate (W=%zu, result=0b1111, a unchanged)\n", W);
}

// =============================================================================
// main
// =============================================================================

int main() {
    std::printf("test_bitwise_not_operator (sturm-s0h):\n"
                "  COUNT_ONLY gate emission + SIMULATE statevector correctness "
                "for operator~(qint_t<W>)\n\n");

    test_not_count_only();
    test_not_simulate();

    std::printf("\nAll test_bitwise_not_operator tests passed.\n");
    return 0;
}
