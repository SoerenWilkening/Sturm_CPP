// test_logic_dsl.cpp — M15 (PRD v3): DSL-style logic operations tests.
//
// Tests:
//   test_logic_dsl_truth_tables — truth tables for OR, NAND, NOR, XNOR on all
//                                 4 input combinations (a,b ∈ {0,1}).
//   test_c_and_dsl_ancilla_clean — verify borrowed ancilla returns to |0⟩
//                                  after lib_c_AND_dsl.
//   test_c_n_and_dsl_scaling    — correctness for n = 3, 4, 5, 6 controls.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/lib/logic_dsl.hpp"
#include "sturm/lib/c_and_dsl.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <vector>

static constexpr double kTol = 1e-9;
using cx = std::complex<double>;

// ── SimCtx: SIMULATE mode context with OrkanBridge ───────────────────────────

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 64u) {
        bridge.allocate(n_qubits);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
        assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    sturm::BackendContext& bc() { return *ctx; }
    orkan::state_t& sv() { return bridge.state(); }
};

// ── Read a single qubit classical value from a pure basis state ───────────────

static uint32_t read_qubit_sim(orkan::state_t& sv, uint32_t q, uint32_t n_qubits) {
    uint64_t dim  = uint64_t{1} << n_qubits;
    uint64_t mask = uint64_t{1} << q;
    for (uint64_t i = 0; i < dim; ++i) {
        if (std::norm(orkan::amplitude(sv, i)) > kTol) {
            return static_cast<uint32_t>((i >> q) & 1u);
        }
    }
    return 0u;
}

// ── test_logic_dsl_truth_tables ───────────────────────────────────────────────
// Truth tables for OR, NAND, NOR, XNOR on all 4 input combos.
//
// Qubit layout per test: q0=a, q1=b, q2=output (must start |0>).
// Total: 3 qubits (QubitPool ancilla at 3+ when needed).

static void test_logic_dsl_truth_tables() {
    sturm::QubitPool::instance().reset_for_testing();

    // Pre-reserve q0, q1, q2 to avoid pool collision.
    int reserved[3];
    for (int i = 0; i < 3; ++i) reserved[i] = sturm::QubitPool::instance().allocate();

    uint32_t pass_count = 0;

    // Truth table entries: {a, b, expected_or, expected_nand, expected_nor, expected_xnor}
    struct TruthRow { uint32_t a, b, or_e, nand_e, nor_e, xnor_e; };
    static const TruthRow rows[4] = {
        {0, 0,  0, 1, 1, 1},
        {0, 1,  1, 1, 0, 0},
        {1, 0,  1, 1, 0, 0},
        {1, 1,  1, 0, 0, 1},
    };

    for (const auto& row : rows) {
        // ── OR ──
        {
            SimCtx sc{4u, 64u}; // 3 register + 1 ancilla for pool
            if (row.a) orkan::apply_x(sc.sv(), 0);
            if (row.b) orkan::apply_x(sc.sv(), 1);
            sturm::qbool a = sturm::qbool::make_non_owning(0);
            sturm::qbool b = sturm::qbool::make_non_owning(1);
            sturm::qbool c = sturm::qbool::make_non_owning(2);
            sturm::lib_or_dsl(a, b, c);
            uint32_t got = read_qubit_sim(sc.sv(), 2, 4);
            if (got != row.or_e) {
                std::fprintf(stderr, "FAIL OR a=%u b=%u: expected=%u got=%u\n",
                             row.a, row.b, row.or_e, got);
                assert(false);
            }
            ++pass_count;
        }
        // ── NAND ──
        {
            SimCtx sc{4u, 64u};
            if (row.a) orkan::apply_x(sc.sv(), 0);
            if (row.b) orkan::apply_x(sc.sv(), 1);
            sturm::qbool a = sturm::qbool::make_non_owning(0);
            sturm::qbool b = sturm::qbool::make_non_owning(1);
            sturm::qbool c = sturm::qbool::make_non_owning(2);
            sturm::lib_nand_dsl(a, b, c);
            uint32_t got = read_qubit_sim(sc.sv(), 2, 4);
            if (got != row.nand_e) {
                std::fprintf(stderr, "FAIL NAND a=%u b=%u: expected=%u got=%u\n",
                             row.a, row.b, row.nand_e, got);
                assert(false);
            }
            ++pass_count;
        }
        // ── NOR ──
        {
            SimCtx sc{4u, 64u};
            if (row.a) orkan::apply_x(sc.sv(), 0);
            if (row.b) orkan::apply_x(sc.sv(), 1);
            sturm::qbool a = sturm::qbool::make_non_owning(0);
            sturm::qbool b = sturm::qbool::make_non_owning(1);
            sturm::qbool c = sturm::qbool::make_non_owning(2);
            sturm::lib_nor_dsl(a, b, c);
            uint32_t got = read_qubit_sim(sc.sv(), 2, 4);
            if (got != row.nor_e) {
                std::fprintf(stderr, "FAIL NOR a=%u b=%u: expected=%u got=%u\n",
                             row.a, row.b, row.nor_e, got);
                assert(false);
            }
            ++pass_count;
        }
        // ── XNOR ──
        {
            SimCtx sc{4u, 64u};
            if (row.a) orkan::apply_x(sc.sv(), 0);
            if (row.b) orkan::apply_x(sc.sv(), 1);
            sturm::qbool a = sturm::qbool::make_non_owning(0);
            sturm::qbool b = sturm::qbool::make_non_owning(1);
            sturm::qbool c = sturm::qbool::make_non_owning(2);
            sturm::lib_xnor_dsl(a, b, c);
            uint32_t got = read_qubit_sim(sc.sv(), 2, 4);
            if (got != row.xnor_e) {
                std::fprintf(stderr, "FAIL XNOR a=%u b=%u: expected=%u got=%u\n",
                             row.a, row.b, row.xnor_e, got);
                assert(false);
            }
            ++pass_count;
        }
    }

    for (int i = 0; i < 3; ++i) sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: test_logic_dsl_truth_tables (%u cases)\n", pass_count);
}

// ── test_c_and_dsl_ancilla_clean ──────────────────────────────────────────────
// Verify lib_c_AND_dsl with both controls |1> flips target, and that after
// the call the ancilla qubit is back in |0>.
//
// Qubit layout:
//   q0 = c0, q1 = c1 (both set to |1>)
//   q2 = target (starts |0>, should be |1> after)
//   q3 = ancilla slot used by lib_c_AND_dsl (should be |0> after)
// Total Orkan qubits: 4.
// Pool pre-reserves q0..q2; lib_c_AND_dsl allocates q3 from pool internally.

static void test_c_and_dsl_ancilla_clean() {
    sturm::QubitPool::instance().reset_for_testing();

    // Pre-reserve q0, q1, q2.
    int reserved[3];
    for (int i = 0; i < 3; ++i) reserved[i] = sturm::QubitPool::instance().allocate();

    {
        SimCtx sc{4u, 64u};

        // Set both controls to |1>.
        orkan::apply_x(sc.sv(), 0);
        orkan::apply_x(sc.sv(), 1);

        sturm::qbool c0  = sturm::qbool::make_non_owning(0);
        sturm::qbool c1  = sturm::qbool::make_non_owning(1);
        sturm::qbool tgt = sturm::qbool::make_non_owning(2);

        sturm::lib_c_AND_dsl(c0, c1, tgt);

        // Target must be |1> (both controls were |1>).
        uint32_t got_tgt = read_qubit_sim(sc.sv(), 2, 4);
        if (got_tgt != 1u) {
            std::fprintf(stderr, "FAIL c_AND_dsl: target expected=1 got=%u\n", got_tgt);
            assert(false);
        }

        // Ancilla (q3) must be |0> — clean after sandwich.
        uint32_t got_anc = read_qubit_sim(sc.sv(), 3, 4);
        if (got_anc != 0u) {
            std::fprintf(stderr, "FAIL c_AND_dsl: ancilla not clean, got=%u\n", got_anc);
            assert(false);
        }
    }

    for (int i = 0; i < 3; ++i) sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: test_c_and_dsl_ancilla_clean\n");
}

// ── test_c_n_and_dsl_scaling ──────────────────────────────────────────────────
// Correctness for n = 3, 4, 5, 6 controls.
// For each n, test all 2^n input combinations.
// Target should flip iff ALL controls are |1>.
//
// Qubit layout: q[0..n-1] = controls, q[n] = target.
// Total Orkan qubits: n+1 + (n-2) ancillas needed for n>=3.
// We allocate n+1 + n Orkan qubits to be safe (ancillas come from pool).

static void test_c_n_and_dsl_scaling() {
    uint32_t total_cases = 0;

    for (uint32_t n = 3u; n <= 6u; ++n) {
        uint32_t n_combinations = 1u << n;
        uint32_t tgt_qubit      = n;
        // Orkan state size: n+1 register qubits + enough ancilla headroom.
        // lib_c_n_AND_dsl needs (n-2) ancillas for n>=3.
        uint32_t orkan_n = n + 1u + (n - 2u);

        // Reset pool and pre-reserve n+1 register qubits.
        sturm::QubitPool::instance().reset_for_testing();
        std::vector<int> reserved(n + 1u);
        for (uint32_t i = 0; i <= n; ++i) reserved[i] = sturm::QubitPool::instance().allocate();

        for (uint32_t combo = 0; combo < n_combinations; ++combo) {
            SimCtx sc{orkan_n, 64u};

            // Set control qubits.
            for (uint32_t i = 0; i < n; ++i) {
                if ((combo >> i) & 1u) orkan::apply_x(sc.sv(), i);
            }
            // Target starts |0>.

            // Create qbool arrays.
            std::vector<sturm::qbool> ctrl_qb(n);
            for (uint32_t i = 0; i < n; ++i) {
                ctrl_qb[i] = sturm::qbool::make_non_owning(static_cast<int>(i));
            }
            sturm::qbool tgt = sturm::qbool::make_non_owning(static_cast<int>(tgt_qubit));

            sturm::lib_c_n_AND_dsl(ctrl_qb.data(), n, tgt);

            uint32_t got = read_qubit_sim(sc.sv(), tgt_qubit, orkan_n);
            uint32_t expected = (combo == n_combinations - 1u) ? 1u : 0u;

            if (got != expected) {
                std::fprintf(stderr,
                    "FAIL c_n_AND_dsl n=%u combo=0x%x: expected=%u got=%u\n",
                    n, combo, expected, got);
                assert(false);
            }
            ++total_cases;
        }

        for (uint32_t i = 0; i <= n; ++i) sturm::QubitPool::instance().release(reserved[i]);
    }

    std::printf("  PASS: test_c_n_and_dsl_scaling (%u cases, n=3..6)\n", total_cases);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M15 DSL-style logic and c_AND tests:\n");
    test_logic_dsl_truth_tables();
    test_c_and_dsl_ancilla_clean();
    test_c_n_and_dsl_scaling();
    std::printf("All M15 logic_dsl tests passed.\n");
    return 0;
}
