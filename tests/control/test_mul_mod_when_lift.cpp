// test_mul_mod_when_lift.cpp -- sturm-a3t4.3 P2
//
// Pins the depth-1 lift pattern in `lib_mul_mod_dsl` (forward + adjoint).
// The forward and adjoint headers replaced the inner
// `push_control / <body> / pop_control` triples driven by `b_bits[i]` with
// the inline lift:
//
//   if (qbool* outer = WhenGuard::active_control()) {
//       qbool tmp = (*outer) & flag_q;
//       WHEN(tmp) { body(); }
//       sturm::uncompute_and(tmp, *outer, flag_q);
//   } else {
//       WHEN(flag_q) { body(); }
//   }
//
// Per the sturm-a3t4 epic ("depth-1 control stack invariant"), the inner
// body must execute under a single control qubit regardless of whether the
// caller wrapped the modular multiplication in an enclosing `WHEN(c)`.
//
// Coverage rationale (issue body): "reduce the modular-arithmetic test
// surface — keep just enough cases to verify correctness, not exhaustive
// coverage."  Exhaustive sweeps in tests/lib/test_mul_mod_dsl* already cover
// correctness across (a, b, n); this test focuses on the new control-stack
// lift specifically.
//
// Method per (a, b, n) input (mirrors the APPEND-mode classical replay
// harness used by tests/lib/test_mul_mod_dsl.cpp's W=3 sweep — at W=2 the
// chain-style mul_mod algorithm peaks well above orkan's 30-qubit ceiling
// once the WHEN(c) wrapping adds a lift ancilla per controlled body, so
// orkan-driven simulation is infeasible):
//   1. Run lib_mul_mod_dsl in APPEND mode, capturing X/CX/CCX into ctx.ir.
//   2. Replay the stream as a classical bit-flip program seeded with
//      (a, b, n).
//   3. Assert r == (a*b) mod n, inputs unchanged, ancilla bits all 0,
//      pool live-count restored.
//   4. Repeat under an enclosing `WHEN(c)` with c carrying super_mask=1
//      (so the lift takes the `if (outer)` branch), seeded c=1 in the
//      bit-vector so the controlled body runs.
//   5. Repeat both passes for the adjoint to confirm round-trip cleanup.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/mul_mod_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/control/when.hpp"
#include "sturm/routines/invert.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static constexpr std::size_t W = 2;

static void apply_gate_classical(std::vector<uint8_t>& bits,
                                 const sturm::GateRecord& rec) {
    switch (rec.kind) {
    case STURM_GATE_X:
        bits[rec.qubits[0]] ^= 1u;
        break;
    case STURM_GATE_CX:
        if (bits[rec.qubits[0]]) bits[rec.qubits[1]] ^= 1u;
        break;
    case STURM_GATE_CCX:
        if (bits[rec.qubits[0]] && bits[rec.qubits[1]])
            bits[rec.qubits[2]] ^= 1u;
        break;
    default:
        std::fprintf(stderr, "trace: unsupported gate kind %d\n",
                     static_cast<int>(rec.kind));
        std::abort();
    }
}

static uint32_t read_reg_classical(const std::vector<uint8_t>& bits,
                                   const int* qi, std::size_t n) {
    uint32_t v = 0u;
    for (std::size_t k = 0; k < n; ++k) {
        if (qi[k] >= 0
            && static_cast<std::size_t>(qi[k]) < bits.size()
            && bits[static_cast<std::size_t>(qi[k])])
            v |= (1u << k);
    }
    return v;
}

static void run_case(uint32_t a_val, uint32_t b_val, uint32_t n_val,
                     bool wrap_in_when) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");

    sturm::QubitPool::instance().reset_for_testing();

    int qi_a[W], qi_b[W], qi_n[W], qi_r[W];
    for (std::size_t i = 0; i < W; ++i)
        qi_a[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_b[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_r[i] = sturm::QubitPool::instance().allocate();
    int qi_c = -1;
    if (wrap_in_when)
        qi_c = sturm::QubitPool::instance().allocate();

    const int pre_in_use = sturm::QubitPool::instance().in_use();

    sturm::qbool a_own[W], b_own[W], n_own[W], r_own[W];
    sturm::BitProxy a_bits[W], b_bits[W], n_bits[W], r_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        a_own[i] = sturm::qbool::make_non_owning(qi_a[i]);
        b_own[i] = sturm::qbool::make_non_owning(qi_b[i]);
        n_own[i] = sturm::qbool::make_non_owning(qi_n[i]);
        r_own[i] = sturm::qbool::make_non_owning(qi_r[i]);
        a_bits[i] = sturm::BitProxy(a_own[i]);
        b_bits[i] = sturm::BitProxy(b_own[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
        r_bits[i] = sturm::BitProxy(r_own[i]);
    }

    sturm::qbool c_owner;
    if (wrap_in_when) {
        // super_mask=1 forces WhenGuard down the superposed branch so
        // active_control() returns &c inside the lift; value=1 keeps the
        // body live in the classical replay (we seed bits[qi_c]=1 below).
        c_owner = sturm::qbool::make_non_owning(qi_c, /*val=*/1, /*mask=*/1ULL);
    }

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    // ── Forward ────────────────────────────────────────────────────────
    if (wrap_in_when) {
        WHEN(c_owner) {
            sturm::lib_mul_mod_dsl<sturm::BitProxy>(a_bits, b_bits, n_bits,
                                                    W, r_bits);
        }
    } else {
        sturm::lib_mul_mod_dsl<sturm::BitProxy>(a_bits, b_bits, n_bits,
                                                W, r_bits);
    }

    // ── Adjoint round-trip ─────────────────────────────────────────────
    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_mul_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_mul_mod_dsl<BitProxy>>() must resolve");

    // Capture the gate count after the forward to verify the forward's
    // r register holds (a*b) mod n at that intermediate point.
    const std::size_t fwd_gate_count = ctx->ir.size();
    const int high_water_fwd = sturm::QubitPool::instance().high_water();

    if (wrap_in_when) {
        WHEN(c_owner) {
            adj_ptr(a_bits, b_bits, n_bits, W, r_bits);
        }
    } else {
        adj_ptr(a_bits, b_bits, n_bits, W, r_bits);
    }

    const int high_water = sturm::QubitPool::instance().high_water();
    assert(high_water >= high_water_fwd);

    // Replay the forward portion to verify intermediate r == (a*b) mod n.
    {
        std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
        for (std::size_t i = 0; i < W; ++i) {
            if ((a_val >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])] = 1u;
            if ((b_val >> i) & 1u) bits[static_cast<std::size_t>(qi_b[i])] = 1u;
            if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
        }
        if (wrap_in_when)
            bits[static_cast<std::size_t>(qi_c)] = 1u;

        for (std::size_t k = 0; k < fwd_gate_count; ++k)
            apply_gate_classical(bits, ctx->ir.at(k));

        const uint32_t expect_r = (a_val * b_val) % n_val;
        assert(read_reg_classical(bits, qi_a, W) == a_val
               && "fwd: a preserved");
        assert(read_reg_classical(bits, qi_b, W) == b_val
               && "fwd: b preserved");
        assert(read_reg_classical(bits, qi_n, W) == n_val
               && "fwd: n preserved");
        assert(read_reg_classical(bits, qi_r, W) == expect_r
               && "fwd: r == (a*b) mod n");
    }

    // Replay the full forward+adjoint sequence to verify r returns to 0
    // and ancilla bits clean up.
    {
        std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
        for (std::size_t i = 0; i < W; ++i) {
            if ((a_val >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])] = 1u;
            if ((b_val >> i) & 1u) bits[static_cast<std::size_t>(qi_b[i])] = 1u;
            if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
        }
        if (wrap_in_when)
            bits[static_cast<std::size_t>(qi_c)] = 1u;

        for (std::size_t k = 0; k < ctx->ir.size(); ++k)
            apply_gate_classical(bits, ctx->ir.at(k));

        assert(read_reg_classical(bits, qi_a, W) == a_val
               && "adj: a preserved");
        assert(read_reg_classical(bits, qi_b, W) == b_val
               && "adj: b preserved");
        assert(read_reg_classical(bits, qi_n, W) == n_val
               && "adj: n preserved");
        assert(read_reg_classical(bits, qi_r, W) == 0u
               && "adj: r returned to |0>");
        if (wrap_in_when)
            assert(bits[static_cast<std::size_t>(qi_c)] == 1u
                   && "adj: outer control c preserved");

        // Every transient ancilla qubit (above the four W-bit input
        // registers and the optional outer control) must be back to 0.
        for (int q = 0; q < high_water; ++q) {
            bool is_input = false;
            for (std::size_t i = 0; i < W && !is_input; ++i) {
                if (q == qi_a[i] || q == qi_b[i] || q == qi_n[i]
                    || q == qi_r[i])
                    is_input = true;
            }
            if (wrap_in_when && q == qi_c) is_input = true;
            if (!is_input)
                assert(bits[static_cast<std::size_t>(q)] == 0u
                       && "adj: ancilla returned to |0>");
        }
    }

    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "lift: pool live-count returns to pre-call value "
              "(uncompute_and returned the lift ancilla LIFO)");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);

    if (wrap_in_when) sturm::QubitPool::instance().release(qi_c);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_b[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_a[i]);
}

int main() {
    std::printf("sturm-a3t4.3: depth-1 lift pattern in lib_mul_mod_dsl "
                "(W=%zu, with/without enclosing WHEN(c)):\n", W);

    // Two operand triples cover both b[0]=1 and b[0]=0 control branches at
    // step (2a) and at least one b[i]=1 at step (2b).  W=2 limits the
    // modulus to [1, 3].
    struct Triple { uint32_t a; uint32_t b; uint32_t n; };
    constexpr Triple cases[] = {
        {2u, 2u, 3u},   // (2*2) mod 3 = 1; b = 0b10 (b[0]=0, b[1]=1)
        {2u, 1u, 3u},   // (2*1) mod 3 = 2; b = 0b01 (b[0]=1, b[1]=0)
    };

    for (const Triple& t : cases) {
        run_case(t.a, t.b, t.n, /*wrap_in_when=*/false);
        std::printf("  PASS: bare WHEN(b[i]) "
                    "(%u * %u) mod %u, forward+adjoint clean\n",
                    t.a, t.b, t.n);
        run_case(t.a, t.b, t.n, /*wrap_in_when=*/true);
        std::printf("  PASS: WHEN(c) wrapping -> lift via "
                    "(c & b[i]), (%u * %u) mod %u\n",
                    t.a, t.b, t.n);
    }

    std::printf("All sturm-a3t4.3 mul_mod lift-pattern tests passed.\n");
    return 0;
}
