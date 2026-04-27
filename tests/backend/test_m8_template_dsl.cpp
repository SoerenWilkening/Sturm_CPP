// test_m8_template_dsl.cpp — M8: Template logic/swap/compare/c_and DSL tests.
//
// Verifies that the templated DSL functions work when instantiated with
// Bit=BitProxy (not just qbool).  Also verifies backward compatibility:
// existing qbool call sites still compile after the template conversion.
//
// Tests:
//   test_logic_bitproxy       — OR, NAND, NOR, XNOR with BitProxy
//   test_logic_qbool_compat   — qbool call sites still work
//   test_c_and_bitproxy       — c_AND, c_n_AND with BitProxy
//   test_swap_bitproxy        — swap with BitProxy
//   test_compare_bitproxy     — EQ, LT, LE, GT, GE, NE with BitProxy (2-bit)
//
// Harness: plain assert + printf (no gtest).

#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/detail/lib/logic_dsl.hpp"
#include "sturm/detail/lib/swap_dsl.hpp"
#include "sturm/detail/lib/compare_dsl.hpp"
#include "sturm/detail/lib/c_and_dsl.hpp"
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

static constexpr double kTol = 1e-9;

// ── Scoped simulate context helper ──────────────────────────────────────────

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 64u) {
        bridge.allocate(n_qubits);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
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

// ── Read helpers ────────────────────────────────────────────────────────────

static uint32_t read_qubit(orkan::state_t& sv, uint32_t q, uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t i = 0; i < dim; ++i) {
        if (std::norm(orkan::amplitude(sv, i)) > kTol)
            return static_cast<uint32_t>((i >> q) & 1u);
    }
    return 0u;
}

static uint32_t read_reg(orkan::state_t& sv, uint32_t base, uint32_t n,
                          uint32_t n_total) {
    uint32_t val = 0u;
    for (uint32_t i = 0; i < n; ++i)
        val |= (read_qubit(sv, base + i, n_total) << i);
    return val;
}

// ── test_logic_bitproxy ──────────────────────────────────────────────────────
// Test OR, NAND, NOR, XNOR with BitProxy for all 4 input combos (a,b in {0,1}).

static void test_logic_bitproxy() {
    sturm::QubitPool::instance().reset_for_testing();

    // 3 qubits: a=0, b=1, c=2.  Reserve them.
    int res[3];
    for (int i = 0; i < 3; ++i)
        res[i] = sturm::QubitPool::instance().allocate();

    struct LogicCase {
        const char* name;
        void (*fn)(sturm::BitProxy&, sturm::BitProxy&, sturm::BitProxy&);
        uint32_t truth[4]; // truth[a*2+b] = expected c
    };

    LogicCase cases[] = {
        {"OR",   [](sturm::BitProxy& a, sturm::BitProxy& b, sturm::BitProxy& c) {
                     sturm::lib_or_dsl(a, b, c); },
         {0, 1, 1, 1}},
        {"NAND", [](sturm::BitProxy& a, sturm::BitProxy& b, sturm::BitProxy& c) {
                     sturm::lib_nand_dsl(a, b, c); },
         {1, 1, 1, 0}},
        {"NOR",  [](sturm::BitProxy& a, sturm::BitProxy& b, sturm::BitProxy& c) {
                     sturm::lib_nor_dsl(a, b, c); },
         {1, 0, 0, 0}},
        {"XNOR", [](sturm::BitProxy& a, sturm::BitProxy& b, sturm::BitProxy& c) {
                     sturm::lib_xnor_dsl(a, b, c); },
         {1, 0, 0, 1}},
    };

    for (auto& tc : cases) {
        for (uint32_t ab = 0; ab < 4u; ++ab) {
            uint32_t a_val = (ab >> 1) & 1u;
            uint32_t b_val = ab & 1u;

            SimCtx sc{4u, 64u};

            if (a_val) orkan::apply_x(sc.sv(), 0u);
            if (b_val) orkan::apply_x(sc.sv(), 1u);
            // c starts |0>

            sturm::qbool aq = sturm::qbool::make_non_owning(0);
            sturm::qbool bq = sturm::qbool::make_non_owning(1);
            sturm::qbool cq = sturm::qbool::make_non_owning(2);

            sturm::BitProxy a(aq), b(bq), c(cq);
            tc.fn(a, b, c);

            uint32_t got_c = read_qubit(sc.sv(), 2u, 4u);
            if (got_c != tc.truth[ab]) {
                std::fprintf(stderr, "FAIL %s bitproxy: a=%u b=%u expected=%u got=%u\n",
                             tc.name, a_val, b_val, tc.truth[ab], got_c);
                assert(false);
            }
        }
    }

    for (int i = 0; i < 3; ++i)
        sturm::QubitPool::instance().release(res[i]);

    std::printf("  PASS: test_logic_bitproxy\n");
}

// ── test_logic_qbool_compat ──────────────────────────────────────────────────
// Verify qbool call sites still compile and work after template conversion.

static void test_logic_qbool_compat() {
    sturm::QubitPool::instance().reset_for_testing();

    int res[3];
    for (int i = 0; i < 3; ++i)
        res[i] = sturm::QubitPool::instance().allocate();

    // Test OR(1,0) = 1
    {
        SimCtx sc{4u, 64u};
        orkan::apply_x(sc.sv(), 0u);  // a=1, b=0

        sturm::qbool a = sturm::qbool::make_non_owning(0);
        sturm::qbool b = sturm::qbool::make_non_owning(1);
        sturm::qbool c = sturm::qbool::make_non_owning(2);

        sturm::lib_or_dsl(a, b, c);
        assert(read_qubit(sc.sv(), 2u, 4u) == 1u);
    }

    // Test XNOR(1,1) = 1
    {
        SimCtx sc{4u, 64u};
        orkan::apply_x(sc.sv(), 0u);  // a=1
        orkan::apply_x(sc.sv(), 1u);  // b=1

        sturm::qbool a = sturm::qbool::make_non_owning(0);
        sturm::qbool b = sturm::qbool::make_non_owning(1);
        sturm::qbool c = sturm::qbool::make_non_owning(2);

        sturm::lib_xnor_dsl(a, b, c);
        assert(read_qubit(sc.sv(), 2u, 4u) == 1u);
    }

    for (int i = 0; i < 3; ++i)
        sturm::QubitPool::instance().release(res[i]);

    std::printf("  PASS: test_logic_qbool_compat\n");
}

// ── test_c_and_bitproxy ──────────────────────────────────────────────────────
// Test c_AND and c_n_AND (n=3) with BitProxy.

static void test_c_and_bitproxy() {
    sturm::QubitPool::instance().reset_for_testing();

    // c_AND: 2-control AND truth table
    int res2[3];
    for (int i = 0; i < 3; ++i)
        res2[i] = sturm::QubitPool::instance().allocate();

    for (uint32_t ab = 0; ab < 4u; ++ab) {
        uint32_t a_val = (ab >> 1) & 1u;
        uint32_t b_val = ab & 1u;

        SimCtx sc{4u, 64u};
        if (a_val) orkan::apply_x(sc.sv(), 0u);
        if (b_val) orkan::apply_x(sc.sv(), 1u);

        sturm::qbool aq = sturm::qbool::make_non_owning(0);
        sturm::qbool bq = sturm::qbool::make_non_owning(1);
        sturm::qbool tq = sturm::qbool::make_non_owning(2);

        sturm::BitProxy a(aq), b(bq), t(tq);
        sturm::lib_c_AND_dsl(a, b, t);

        uint32_t expected = a_val & b_val;
        uint32_t got = read_qubit(sc.sv(), 2u, 4u);
        if (got != expected) {
            std::fprintf(stderr, "FAIL c_AND bitproxy: a=%u b=%u expected=%u got=%u\n",
                         a_val, b_val, expected, got);
            assert(false);
        }
    }

    for (int i = 0; i < 3; ++i)
        sturm::QubitPool::instance().release(res2[i]);

    // c_n_AND: 3-control AND truth table
    int res4[4];
    for (int i = 0; i < 4; ++i)
        res4[i] = sturm::QubitPool::instance().allocate();

    for (uint32_t abc = 0; abc < 8u; ++abc) {
        SimCtx sc{8u, 64u};
        for (uint32_t i = 0; i < 3u; ++i)
            if ((abc >> i) & 1u) orkan::apply_x(sc.sv(), i);

        sturm::qbool cq[3];
        sturm::BitProxy cp[3];
        for (uint32_t i = 0; i < 3u; ++i) {
            cq[i] = sturm::qbool::make_non_owning(static_cast<int>(i));
            cp[i] = sturm::BitProxy(cq[i]);
        }
        sturm::qbool tq = sturm::qbool::make_non_owning(3);
        sturm::BitProxy t(tq);

        sturm::lib_c_n_AND_dsl(cp, 3u, t);

        uint32_t expected = (abc == 7u) ? 1u : 0u;  // all three bits set
        uint32_t got = read_qubit(sc.sv(), 3u, 8u);
        if (got != expected) {
            std::fprintf(stderr, "FAIL c_n_AND bitproxy: abc=%u expected=%u got=%u\n",
                         abc, expected, got);
            assert(false);
        }
    }

    for (int i = 0; i < 4; ++i)
        sturm::QubitPool::instance().release(res4[i]);

    std::printf("  PASS: test_c_and_bitproxy\n");
}

// ── test_swap_bitproxy ───────────────────────────────────────────────────────
// Test swap with BitProxy (uses 3-CNOT path since not qbool).

static void test_swap_bitproxy() {
    sturm::QubitPool::instance().reset_for_testing();

    int res[2];
    for (int i = 0; i < 2; ++i)
        res[i] = sturm::QubitPool::instance().allocate();

    // Swap a=1, b=0 -> a=0, b=1
    {
        SimCtx sc{4u, 64u};
        orkan::apply_x(sc.sv(), 0u);  // a=1, b=0

        sturm::qbool aq = sturm::qbool::make_non_owning(0);
        sturm::qbool bq = sturm::qbool::make_non_owning(1);
        sturm::BitProxy a(aq), b(bq);

        sturm::lib_swap_dsl(a, b);

        assert(read_qubit(sc.sv(), 0u, 4u) == 0u);
        assert(read_qubit(sc.sv(), 1u, 4u) == 1u);
    }

    // Swap a=0, b=1 -> a=1, b=0
    {
        SimCtx sc{4u, 64u};
        orkan::apply_x(sc.sv(), 1u);  // a=0, b=1

        sturm::qbool aq = sturm::qbool::make_non_owning(0);
        sturm::qbool bq = sturm::qbool::make_non_owning(1);
        sturm::BitProxy a(aq), b(bq);

        sturm::lib_swap_dsl(a, b);

        assert(read_qubit(sc.sv(), 0u, 4u) == 1u);
        assert(read_qubit(sc.sv(), 1u, 4u) == 0u);
    }

    for (int i = 0; i < 2; ++i)
        sturm::QubitPool::instance().release(res[i]);

    std::printf("  PASS: test_swap_bitproxy\n");
}

// ── test_compare_bitproxy ────────────────────────────────────────────────────
// Test EQ, LT, LE, GT, GE, NE with BitProxy (2-bit operands).

static void test_compare_bitproxy() {
    sturm::QubitPool::instance().reset_for_testing();

    const uint32_t n = 2u;
    const uint32_t a_base = 0u;
    const uint32_t b_base = n;
    const uint32_t r_qubit = 2u * n;
    const uint32_t n_total = 2u * n + 1u + 8u;  // extra ancilla headroom

    int reserved[5];
    for (uint32_t i = 0; i < 2u * n + 1u; ++i)
        reserved[i] = sturm::QubitPool::instance().allocate();

    struct CmpCase {
        const char* name;
        void (*fn)(sturm::BitProxy*, sturm::BitProxy*, size_t, sturm::BitProxy&);
        bool (*expected)(uint32_t, uint32_t);
    };

    CmpCase cases[] = {
        {"EQ",
         [](sturm::BitProxy* a, sturm::BitProxy* b, size_t nn, sturm::BitProxy& r) {
             sturm::lib_eq_dsl(a, b, nn, r); },
         [](uint32_t a, uint32_t b) -> bool { return a == b; }},
        {"LT",
         [](sturm::BitProxy* a, sturm::BitProxy* b, size_t nn, sturm::BitProxy& r) {
             sturm::lib_lt_dsl(a, b, nn, r); },
         [](uint32_t a, uint32_t b) -> bool { return a < b; }},
        {"LE",
         [](sturm::BitProxy* a, sturm::BitProxy* b, size_t nn, sturm::BitProxy& r) {
             sturm::lib_le_dsl(a, b, nn, r); },
         [](uint32_t a, uint32_t b) -> bool { return a <= b; }},
        {"GT",
         [](sturm::BitProxy* a, sturm::BitProxy* b, size_t nn, sturm::BitProxy& r) {
             sturm::lib_gt_dsl(a, b, nn, r); },
         [](uint32_t a, uint32_t b) -> bool { return a > b; }},
        {"GE",
         [](sturm::BitProxy* a, sturm::BitProxy* b, size_t nn, sturm::BitProxy& r) {
             sturm::lib_ge_dsl(a, b, nn, r); },
         [](uint32_t a, uint32_t b) -> bool { return a >= b; }},
        {"NE",
         [](sturm::BitProxy* a, sturm::BitProxy* b, size_t nn, sturm::BitProxy& r) {
             sturm::lib_ne_dsl(a, b, nn, r); },
         [](uint32_t a, uint32_t b) -> bool { return a != b; }},
    };

    uint32_t pass_count = 0;
    for (auto& tc : cases) {
        for (uint32_t a_val = 0; a_val < 4u; ++a_val) {
            for (uint32_t b_val = 0; b_val < 4u; ++b_val) {
                SimCtx sc{n_total, 64u};

                for (uint32_t i = 0; i < n; ++i) {
                    if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
                    if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
                }

                sturm::qbool aq[2], bq[2];
                sturm::BitProxy ap[2], bp[2];
                for (uint32_t i = 0; i < n; ++i) {
                    aq[i] = sturm::qbool::make_non_owning(static_cast<int>(a_base + i));
                    bq[i] = sturm::qbool::make_non_owning(static_cast<int>(b_base + i));
                    ap[i] = sturm::BitProxy(aq[i]);
                    bp[i] = sturm::BitProxy(bq[i]);
                }
                sturm::qbool rq = sturm::qbool::make_non_owning(static_cast<int>(r_qubit));
                sturm::BitProxy rp(rq);

                tc.fn(ap, bp, n, rp);

                uint32_t expected = tc.expected(a_val, b_val) ? 1u : 0u;
                uint32_t got = read_qubit(sc.sv(), r_qubit, n_total);
                if (got != expected) {
                    std::fprintf(stderr, "FAIL %s bitproxy: a=%u b=%u expected=%u got=%u\n",
                                 tc.name, a_val, b_val, expected, got);
                    assert(false);
                }
                ++pass_count;
            }
        }
    }

    for (uint32_t i = 0; i < 2u * n + 1u; ++i)
        sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: test_compare_bitproxy (%u cases)\n", pass_count);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M8 Template logic/swap/compare/c_and DSL tests:\n");
    test_logic_bitproxy();
    test_logic_qbool_compat();
    test_c_and_bitproxy();
    test_swap_bitproxy();
    test_compare_bitproxy();
    std::printf("All M8 template DSL tests passed.\n");
    return 0;
}
