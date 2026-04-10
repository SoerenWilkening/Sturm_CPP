// test_qint_compare_e2e.cpp — M19 (PRD v3): qint_t comparison operators end-to-end.
//
// Tests:
//   test_qint_cmp_count     — qint_t::operator== and operator< emit gates
//                              (via COMPARE uncompute destructor) when inputs are
//                              superposed (super_mask != 0).
//   test_qint_cmp_classical — classical comparisons return correct qbool.value
//                              for all six operators (==, !=, <, <=, >, >=).
//
// Note: Under STURM_BACKEND_ENABLED, comparison operators use the COMPARE stub
// which stamps an uncompute tag on the result qbool. Gates are emitted when
// the qbool is destroyed (Bennett uncomputation). This test verifies:
//   1. gate_count > 0 after a comparison qbool is constructed and destroyed.
//   2. The returned qbool carries the correct classical result in .value.
//
// Harness: plain assert + printf (no gtest).

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

// ── Context helpers ───────────────────────────────────────────────────────────

struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit ScopedCtx(sturm_mode_t mode, uint32_t max_q = 128u) {
        ctx  = sturm_backend_create(mode, max_q);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

// ── Helper: build a quantum qint_t<W> with manually assigned qubits ──────────
// super_mask is set so that the COMPARE stub emits gates when the qbool destructs.
template <std::size_t W>
static sturm::qint_t<W> make_quantum_qint(int64_t val, int base_qubit) {
    sturm::qint_t<W> q;
    q.value      = val;
    q.super_mask = (W < 64u) ? ((1ULL << W) - 1u) : ~0ULL;
    for (std::size_t i = 0; i < W; ++i)
        q.qubits[i] = base_qubit + static_cast<int>(i);
    return q;
}

template <std::size_t W>
static void clear_qubits(sturm::qint_t<W>& q) {
    q.qubits.fill(-1);
    q.super_mask = 0;
}

// ── test_qint_cmp_count ───────────────────────────────────────────────────────
// Verify that comparison operators emit gates via the COMPARE destructor.
// The COMPARE stub emits 2 CX gates (forward + inverse) when the qbool is
// destroyed and inputs have super_mask != 0.

static void test_qint_cmp_count() {
    // Use COUNT_ONLY mode so gates go to gate_count.
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    static constexpr std::size_t W = 4;

    auto a = make_quantum_qint<W>(3LL, 0);
    auto b = make_quantum_qint<W>(5LL, static_cast<int>(W));

    {
        uint64_t before = sc.ctx->gate_count;
        {
            sturm::qbool eq_result = (a == b);
            // At this point, no gates are emitted yet (COMPARE tag is deferred).
            (void)eq_result;
            // eq_result goes out of scope here -> destructor emits compare gates.
        }
        uint64_t after = sc.ctx->gate_count;

        // After qbool destruction, COMPARE stub emits gates.
        assert(after > before && "a == b must emit gates when qbool is destroyed");
    }

    {
        uint64_t before = sc.ctx->gate_count;
        {
            sturm::qbool lt_result = (a < b);
            (void)lt_result;
        }
        uint64_t after = sc.ctx->gate_count;
        assert(after > before && "a < b must emit gates when qbool is destroyed");
    }

    clear_qubits(a);
    clear_qubits(b);

    std::printf("  PASS: test_qint_cmp_count (gates emitted for == and <)\n");
}

// ── test_qint_cmp_classical ───────────────────────────────────────────────────
// All six comparison operators return correct classical result in .value.
// Uses classical qints (super_mask = 0) for simplicity.

static void test_qint_cmp_classical() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};

    static constexpr std::size_t W = 4;

    // Classical qints (no super_mask).
    sturm::qint_t<W> a3(3LL);
    sturm::qint_t<W> b5(5LL);
    sturm::qint_t<W> a4(4LL);
    sturm::qint_t<W> b4(4LL);

    // 3 == 5: false
    { auto r = (a3 == b5); assert(!r.value && "3 == 5 must be false"); }
    // 3 != 5: true
    { auto r = (a3 != b5); assert(r.value  && "3 != 5 must be true");  }
    // 3 < 5: true
    { auto r = (a3 < b5);  assert(r.value  && "3 < 5 must be true");   }
    // 3 <= 5: true
    { auto r = (a3 <= b5); assert(r.value  && "3 <= 5 must be true");  }
    // 3 > 5: false
    { auto r = (a3 > b5);  assert(!r.value && "3 > 5 must be false");  }
    // 3 >= 5: false
    { auto r = (a3 >= b5); assert(!r.value && "3 >= 5 must be false"); }

    // 4 == 4: true
    { auto r = (a4 == b4); assert(r.value  && "4 == 4 must be true");  }
    // 4 != 4: false
    { auto r = (a4 != b4); assert(!r.value && "4 != 4 must be false"); }
    // 4 < 4: false
    { auto r = (a4 < b4);  assert(!r.value && "4 < 4 must be false");  }
    // 4 <= 4: true
    { auto r = (a4 <= b4); assert(r.value  && "4 <= 4 must be true");  }
    // 4 > 4: false
    { auto r = (a4 > b4);  assert(!r.value && "4 > 4 must be false");  }
    // 4 >= 4: true
    { auto r = (a4 >= b4); assert(r.value  && "4 >= 4 must be true");  }

    std::printf("  PASS: test_qint_cmp_classical (all 6 operators correct)\n");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M19 qint_t comparison operators end-to-end tests:\n");
    test_qint_cmp_count();
    test_qint_cmp_classical();
    std::printf("All M19 qint_compare_e2e tests passed.\n");
    return 0;
}
