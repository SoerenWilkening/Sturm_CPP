// test_pow_mod_when_lift.cpp -- sturm-a3t4.3 P2
//
// Pins the depth-1 lift pattern in `lib_pow_mod_dsl` (forward + adjoint).
// The forward and adjoint headers replaced the inner
// `push_control / <body> / pop_control` triples driven by `exp_bits[i]` with
// the same inline lift pattern used by add_mod / mul_mod.  Each iteration
// over `exp_bits[i]` allocates+uncomputes its own AND ancilla — fine, one
// extra Toffoli/iteration when nested under a WHEN.
//
// Coverage rationale (issue body): "reduce the modular-arithmetic test
// surface — keep just enough cases to verify correctness, not exhaustive
// coverage."  Exhaustive sweeps in tests/lib/test_pow_mod_dsl* already cover
// correctness; this test focuses on the new control-stack lift specifically.
//
// Method per (base, exp, n) input (mirrors test_pow_mod_dsl.cpp's beat 3.3
// APPEND-mode classical replay harness — at W=2 the chain-style pow_mod
// algorithm peaks far above orkan's 30-qubit ceiling, so simulator-driven
// verification is infeasible):
//   1. Run lib_pow_mod_dsl in APPEND mode, capturing X/CX/CCX into ctx.ir.
//   2. Replay the stream as a classical bit-flip program seeded with
//      (base, exp, n).
//   3. Assert r == pow_mod(base, exp, n), inputs unchanged, ancilla bits
//      all 0, pool live-count restored.
//   4. Repeat under an enclosing `WHEN(c)` with c carrying super_mask=1
//      (so the lift takes the `if (outer)` branch), seeded c=1 in the
//      bit-vector.
//   5. Repeat both passes for the adjoint to confirm round-trip cleanup.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/pow_mod_dsl.hpp"
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

// Reference: classical (base ^ exp) mod n with the 0^0 == 1 convention.
static uint32_t pow_mod_ref(uint32_t base, uint32_t exp, uint32_t n) {
    if (n == 1u) return 0u;
    uint32_t r = 1u % n;
    uint32_t b = base % n;
    while (exp > 0u) {
        if (exp & 1u) r = (r * b) % n;
        b = (b * b) % n;
        exp >>= 1;
    }
    return r;
}

static void run_case(uint32_t base_val, uint32_t exp_val, uint32_t n_val,
                     bool wrap_in_when) {
    assert(base_val < n_val && "test precondition: base < n");

    sturm::QubitPool::instance().reset_for_testing();

    int qi_base[W], qi_exp[W], qi_n[W], qi_r[W];
    for (std::size_t i = 0; i < W; ++i)
        qi_base[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_exp[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < W; ++i)
        qi_r[i] = sturm::QubitPool::instance().allocate();
    int qi_c = -1;
    if (wrap_in_when)
        qi_c = sturm::QubitPool::instance().allocate();

    const int pre_in_use = sturm::QubitPool::instance().in_use();

    sturm::qbool base_own[W], exp_own[W], n_own[W], r_own[W];
    sturm::BitProxy base_bits[W], exp_bits[W], n_bits[W], r_bits[W];
    for (std::size_t i = 0; i < W; ++i) {
        base_own[i] = sturm::qbool::make_non_owning(qi_base[i]);
        exp_own[i]  = sturm::qbool::make_non_owning(qi_exp[i]);
        n_own[i]    = sturm::qbool::make_non_owning(qi_n[i]);
        r_own[i]    = sturm::qbool::make_non_owning(qi_r[i]);
        base_bits[i] = sturm::BitProxy(base_own[i]);
        exp_bits[i]  = sturm::BitProxy(exp_own[i]);
        n_bits[i]    = sturm::BitProxy(n_own[i]);
        r_bits[i]    = sturm::BitProxy(r_own[i]);
    }

    sturm::qbool c_owner;
    if (wrap_in_when) {
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
            sturm::lib_pow_mod_dsl<sturm::BitProxy>(base_bits, exp_bits,
                                                    n_bits, W, r_bits);
        }
    } else {
        sturm::lib_pow_mod_dsl<sturm::BitProxy>(base_bits, exp_bits, n_bits,
                                                W, r_bits);
    }

    // Snapshot the gate count + high-water mark after the forward.
    const std::size_t fwd_gate_count = ctx->ir.size();
    const int high_water_fwd = sturm::QubitPool::instance().high_water();

    constexpr auto adj_ptr =
        sturm::invert<&sturm::lib_pow_mod_dsl<sturm::BitProxy>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&lib_pow_mod_dsl<BitProxy>>() must resolve");

    if (wrap_in_when) {
        WHEN(c_owner) {
            adj_ptr(base_bits, exp_bits, n_bits, W, r_bits);
        }
    } else {
        adj_ptr(base_bits, exp_bits, n_bits, W, r_bits);
    }

    const int high_water = sturm::QubitPool::instance().high_water();
    assert(high_water >= high_water_fwd);

    // Replay the forward portion to verify intermediate r == (base^exp) mod n.
    {
        std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
        for (std::size_t i = 0; i < W; ++i) {
            if ((base_val >> i) & 1u) bits[static_cast<std::size_t>(qi_base[i])] = 1u;
            if ((exp_val  >> i) & 1u) bits[static_cast<std::size_t>(qi_exp[i])]  = 1u;
            if ((n_val    >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])]    = 1u;
        }
        if (wrap_in_when)
            bits[static_cast<std::size_t>(qi_c)] = 1u;

        for (std::size_t k = 0; k < fwd_gate_count; ++k)
            apply_gate_classical(bits, ctx->ir.at(k));

        const uint32_t expect_r = pow_mod_ref(base_val, exp_val, n_val);
        assert(read_reg_classical(bits, qi_base, W) == base_val
               && "fwd: base preserved");
        assert(read_reg_classical(bits, qi_exp,  W) == exp_val
               && "fwd: exp preserved");
        assert(read_reg_classical(bits, qi_n,    W) == n_val
               && "fwd: n preserved");
        assert(read_reg_classical(bits, qi_r,    W) == expect_r
               && "fwd: r == (base^exp) mod n");
    }

    // Replay the full forward+adjoint to verify cleanup.
    {
        std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
        for (std::size_t i = 0; i < W; ++i) {
            if ((base_val >> i) & 1u) bits[static_cast<std::size_t>(qi_base[i])] = 1u;
            if ((exp_val  >> i) & 1u) bits[static_cast<std::size_t>(qi_exp[i])]  = 1u;
            if ((n_val    >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])]    = 1u;
        }
        if (wrap_in_when)
            bits[static_cast<std::size_t>(qi_c)] = 1u;

        for (std::size_t k = 0; k < ctx->ir.size(); ++k)
            apply_gate_classical(bits, ctx->ir.at(k));

        assert(read_reg_classical(bits, qi_base, W) == base_val
               && "adj: base preserved");
        assert(read_reg_classical(bits, qi_exp,  W) == exp_val
               && "adj: exp preserved");
        assert(read_reg_classical(bits, qi_n,    W) == n_val
               && "adj: n preserved");
        assert(read_reg_classical(bits, qi_r,    W) == 0u
               && "adj: r returned to |0>");
        if (wrap_in_when)
            assert(bits[static_cast<std::size_t>(qi_c)] == 1u
                   && "adj: outer control c preserved");

        for (int q = 0; q < high_water; ++q) {
            bool is_input = false;
            for (std::size_t i = 0; i < W && !is_input; ++i) {
                if (q == qi_base[i] || q == qi_exp[i]
                    || q == qi_n[i] || q == qi_r[i])
                    is_input = true;
            }
            if (wrap_in_when && q == qi_c) is_input = true;
            if (!is_input)
                assert(bits[static_cast<std::size_t>(q)] == 0u
                       && "adj: ancilla returned to |0>");
        }
    }

    assert(sturm::QubitPool::instance().in_use() == pre_in_use
           && "lift: pool live-count returns to pre-call value");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);

    if (wrap_in_when) sturm::QubitPool::instance().release(qi_c);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_exp[i]);
    for (std::size_t i = W; i-- > 0;)
        sturm::QubitPool::instance().release(qi_base[i]);
}

int main() {
    std::printf("sturm-a3t4.3: depth-1 lift pattern in lib_pow_mod_dsl "
                "(W=%zu, with/without enclosing WHEN(c)):\n", W);

    // Sample two (base, exp, n) triples covering exp[i]=0 / exp[i]=1
    // branches and the 0^0=1 convention.  W=2 limits the modulus and the
    // base to [0, 3].
    struct Triple { uint32_t base; uint32_t exp; uint32_t n; };
    constexpr Triple cases[] = {
        {2u, 3u, 3u},   // 2^3 mod 3 = 2 (exp = 0b11; both exp bits set)
        {0u, 0u, 3u},   // 0^0 = 1 convention (exp = 0b00)
    };

    for (const Triple& t : cases) {
        run_case(t.base, t.exp, t.n, /*wrap_in_when=*/false);
        std::printf("  PASS: bare WHEN(exp[i]) "
                    "(%u ^ %u) mod %u, forward+adjoint clean\n",
                    t.base, t.exp, t.n);
        run_case(t.base, t.exp, t.n, /*wrap_in_when=*/true);
        std::printf("  PASS: WHEN(c) wrapping -> lift via "
                    "(c & exp[i]), (%u ^ %u) mod %u\n",
                    t.base, t.exp, t.n);
    }

    std::printf("All sturm-a3t4.3 pow_mod lift-pattern tests passed.\n");
    return 0;
}
