// test_modular_arith_highw_trace.cpp -- sturm-8n73 high-W trace-mode
// validation that kMaxN=64 actually works end-to-end for circuit-
// generation use cases.
//
// sturm-5jta (P2.b / G5): the qubit pool now grows on demand without a
// compile-time knob, so the W=64 ancilla peak (~7W+7 = 455 live) is
// allocated naturally.  Drives all three primitives
// (lib_double_mod_dsl, lib_add_mod_inplace_dsl, lib_mul_mod_dsl_oneshot)
// in APPEND-mode classical-trace replay -- statevector simulation is
// infeasible past ~W=16 regardless of the pool size, so this file
// exclusively uses the bit-vector replay path.
//
// Coverage:
//   - W=40, W=48, W=56, W=64 random spot checks per primitive (each
//     above the legacy kMaxN=32 cap), 3 cases per W.
//   - Asserts the same post-conditions the lower-W trace harnesses do:
//     correct functional output, inputs preserved, ancilla cleanup,
//     pool live-count restored.
//
// The W=64 multiplier inputs are uint64_t-sized so we use 64-bit math
// for the classical reference (a*b can overflow 32 bits).

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/double_mod_dsl.hpp"
#include "sturm/detail/lib/add_mod_inplace_dsl.hpp"
#include "sturm/detail/lib/mul_mod_dsl_oneshot.hpp"
#include "sturm/ops/qint_modular.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint_core.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <random>
#include <vector>

// ── shared classical replay primitives ────────────────────────────────────
static void apply_gate_classical(std::vector<uint8_t>& bits,
                                 const sturm::GateRecord& rec) {
    switch (rec.kind) {
    case STURM_GATE_X:
        bits[rec.qubits[0]] ^= 1u; break;
    case STURM_GATE_CX:
        if (bits[rec.qubits[0]]) bits[rec.qubits[1]] ^= 1u; break;
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

static uint64_t read_reg64(const std::vector<uint8_t>& bits,
                           const int* qi, std::size_t n) {
    uint64_t v = 0u;
    for (std::size_t k = 0; k < n; ++k)
        if (qi[k] >= 0 && bits[static_cast<std::size_t>(qi[k])])
            v |= (uint64_t{1} << k);
    return v;
}

// ── lib_double_mod_dsl<Wn> trace ──────────────────────────────────────────
template <std::size_t Wn>
static void run_double_mod_trace(uint64_t x_val, uint64_t n_val) {
    assert(x_val < n_val);
    sturm::QubitPool::instance().reset_for_testing();

    int qi_x[Wn + 1u], qi_n[Wn], qi_lt;
    for (std::size_t i = 0; i < Wn + 1u; ++i)
        qi_x[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    qi_lt = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    const std::size_t n_reg = static_cast<std::size_t>(pre_in_use);

    sturm::qbool x_own[Wn + 1u], n_own[Wn], lt_own;
    sturm::BitProxy x_bits[Wn + 1u], n_bits[Wn], lt_bit;
    for (std::size_t i = 0; i < Wn + 1u; ++i) {
        x_own[i]  = sturm::qbool::make_non_owning(qi_x[i]);
        x_bits[i] = sturm::BitProxy(x_own[i]);
    }
    for (std::size_t i = 0; i < Wn; ++i) {
        n_own[i]  = sturm::qbool::make_non_owning(qi_n[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
    }
    lt_own = sturm::qbool::make_non_owning(qi_lt);
    lt_bit = sturm::BitProxy(lt_own);

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_double_mod_dsl<sturm::BitProxy>(x_bits, n_bits, Wn, lt_bit);

    const int high_water = sturm::QubitPool::instance().high_water();
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < Wn; ++i) {
        if ((x_val >> i) & 1u) bits[static_cast<std::size_t>(qi_x[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));

    // Use 128-bit math: 2*x can overflow uint64_t at W=64.
    const __uint128_t two_x = static_cast<__uint128_t>(x_val) * 2u;
    const uint64_t expect_x  = static_cast<uint64_t>(two_x % n_val);
    const uint64_t expect_lt = (two_x < n_val) ? 1u : 0u;
    assert(read_reg64(bits, qi_x, Wn) == expect_x);
    assert(bits[static_cast<std::size_t>(qi_x[Wn])] == 0u);
    assert(read_reg64(bits, qi_n, Wn) == n_val);
    assert(bits[static_cast<std::size_t>(qi_lt)] == expect_lt);
    for (std::size_t q = n_reg; q < bits.size(); ++q)
        assert(bits[q] == 0u && "trace: ancilla not cleaned");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use);

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    sturm::QubitPool::instance().release(qi_lt);
    for (std::size_t i = Wn; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn + 1u; i-- > 0;)
        sturm::QubitPool::instance().release(qi_x[i]);
}

// ── lib_add_mod_inplace_dsl<Wn> trace ─────────────────────────────────────
template <std::size_t Wn>
static void run_inplace_trace(uint64_t a_val, uint64_t dest_val,
                              uint64_t n_val) {
    assert(a_val < n_val && dest_val < n_val);
    sturm::QubitPool::instance().reset_for_testing();

    int qi_a[Wn], qi_dest[Wn + 1u], qi_n[Wn];
    for (std::size_t i = 0; i < Wn; ++i)
        qi_a[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn + 1u; ++i)
        qi_dest[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i)
        qi_n[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    const std::size_t n_reg = static_cast<std::size_t>(pre_in_use);

    sturm::qbool a_own[Wn], dest_own[Wn + 1u], n_own[Wn];
    sturm::BitProxy a_bits[Wn], dest_bits[Wn + 1u], n_bits[Wn];
    for (std::size_t i = 0; i < Wn; ++i) {
        a_own[i]  = sturm::qbool::make_non_owning(qi_a[i]);
        a_bits[i] = sturm::BitProxy(a_own[i]);
        n_own[i]  = sturm::qbool::make_non_owning(qi_n[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
    }
    for (std::size_t i = 0; i < Wn + 1u; ++i) {
        dest_own[i]  = sturm::qbool::make_non_owning(qi_dest[i]);
        dest_bits[i] = sturm::BitProxy(dest_own[i]);
    }

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_add_mod_inplace_dsl<sturm::BitProxy>(a_bits, dest_bits,
                                                    n_bits, Wn);

    const int high_water = sturm::QubitPool::instance().high_water();
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < Wn; ++i) {
        if ((a_val    >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])]    = 1u;
        if ((dest_val >> i) & 1u) bits[static_cast<std::size_t>(qi_dest[i])] = 1u;
        if ((n_val    >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])]    = 1u;
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));

    // 128-bit math: dest + a can overflow uint64_t at W=64.
    const uint64_t expect_dest = static_cast<uint64_t>(
        (static_cast<__uint128_t>(dest_val) + a_val) % n_val);
    assert(read_reg64(bits, qi_a, Wn) == a_val);
    assert(read_reg64(bits, qi_dest, Wn) == expect_dest);
    assert(bits[static_cast<std::size_t>(qi_dest[Wn])] == 0u);
    assert(read_reg64(bits, qi_n, Wn) == n_val);
    for (std::size_t q = n_reg; q < bits.size(); ++q)
        assert(bits[q] == 0u && "trace: ancilla not cleaned");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use);

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (std::size_t i = Wn; i-- > 0;)
        sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn + 1u; i-- > 0;)
        sturm::QubitPool::instance().release(qi_dest[i]);
    for (std::size_t i = Wn; i-- > 0;)
        sturm::QubitPool::instance().release(qi_a[i]);
}

// ── lib_mul_mod_dsl_oneshot<Wn> trace ─────────────────────────────────────
template <std::size_t Wn>
static void run_oneshot_trace(uint64_t a_val, uint64_t b_val,
                              uint64_t n_val) {
    assert(a_val < n_val && b_val < n_val);
    sturm::QubitPool::instance().reset_for_testing();

    int qi_a[Wn], qi_b[Wn], qi_n[Wn], qi_r[Wn];
    for (std::size_t i = 0; i < Wn; ++i) qi_a[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i) qi_b[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i) qi_n[i] = sturm::QubitPool::instance().allocate();
    for (std::size_t i = 0; i < Wn; ++i) qi_r[i] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();
    const std::size_t n_reg = static_cast<std::size_t>(pre_in_use);

    sturm::qbool a_own[Wn], b_own[Wn], n_own[Wn], r_own[Wn];
    sturm::BitProxy a_bits[Wn], b_bits[Wn], n_bits[Wn], r_bits[Wn];
    for (std::size_t i = 0; i < Wn; ++i) {
        a_own[i] = sturm::qbool::make_non_owning(qi_a[i]);
        b_own[i] = sturm::qbool::make_non_owning(qi_b[i]);
        n_own[i] = sturm::qbool::make_non_owning(
            qi_n[i],
            static_cast<int64_t>((n_val >> i) & 1u),
            /*mask=*/0ULL);
        r_own[i] = sturm::qbool::make_non_owning(qi_r[i]);
        a_bits[i] = sturm::BitProxy(a_own[i]);
        b_bits[i] = sturm::BitProxy(b_own[i]);
        n_bits[i] = sturm::BitProxy(n_own[i]);
        r_bits[i] = sturm::BitProxy(r_own[i]);
    }

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    sturm::lib_mul_mod_dsl_oneshot<sturm::BitProxy>(a_bits, b_bits, n_bits,
                                                    Wn, r_bits);

    const int high_water = sturm::QubitPool::instance().high_water();
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < Wn; ++i) {
        if ((a_val >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])] = 1u;
        if ((b_val >> i) & 1u) bits[static_cast<std::size_t>(qi_b[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));

    // Use __uint128_t for the W=64 reference so a*b doesn't overflow.
    const uint64_t expect_r = static_cast<uint64_t>(
        (static_cast<__uint128_t>(a_val) * static_cast<__uint128_t>(b_val))
        % n_val);
    assert(read_reg64(bits, qi_a, Wn) == a_val);
    assert(read_reg64(bits, qi_b, Wn) == b_val);
    assert(read_reg64(bits, qi_n, Wn) == n_val);
    assert(read_reg64(bits, qi_r, Wn) == expect_r);
    for (std::size_t q = n_reg; q < bits.size(); ++q)
        assert(bits[q] == 0u && "trace: ancilla not cleaned");
    assert(sturm::QubitPool::instance().in_use() == pre_in_use);

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_r[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_n[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_b[i]);
    for (std::size_t i = Wn; i-- > 0;) sturm::QubitPool::instance().release(qi_a[i]);
}

// ── per-primitive driver: runs N random spot checks at the given W ────────
template <std::size_t Wn>
static void run_double_mod_spots(uint32_t seed, std::size_t kCases) {
    std::mt19937_64 rng(seed);
    // Pick n in [Wn-1 hi-bit set, 2^Wn - 1] so we exercise a non-trivial
    // top-bit pattern.  Use 2^(Wn-1) as a lower bound but cap at uint64
    // max for Wn=64.
    uint64_t n_lo = (Wn == 64u) ? (uint64_t{1} << 62)
                                  : (uint64_t{1} << (Wn - 1u));
    uint64_t n_hi = (Wn == 64u) ? ~uint64_t{0}
                                  : ((uint64_t{1} << Wn) - 1u);
    std::uniform_int_distribution<uint64_t> n_dist(n_lo, n_hi);
    for (std::size_t i = 0; i < kCases; ++i) {
        uint64_t n_val = n_dist(rng);
        std::uniform_int_distribution<uint64_t> x_dist(0u, n_val - 1u);
        uint64_t x_val = x_dist(rng);
        run_double_mod_trace<Wn>(x_val, n_val);
    }
}

template <std::size_t Wn>
static void run_inplace_spots(uint32_t seed, std::size_t kCases) {
    std::mt19937_64 rng(seed);
    uint64_t n_lo = (Wn == 64u) ? (uint64_t{1} << 62)
                                  : (uint64_t{1} << (Wn - 1u));
    uint64_t n_hi = (Wn == 64u) ? ~uint64_t{0}
                                  : ((uint64_t{1} << Wn) - 1u);
    std::uniform_int_distribution<uint64_t> n_dist(n_lo, n_hi);
    for (std::size_t i = 0; i < kCases; ++i) {
        uint64_t n_val = n_dist(rng);
        std::uniform_int_distribution<uint64_t> ab(0u, n_val - 1u);
        uint64_t a_val    = ab(rng);
        uint64_t dest_val = ab(rng);
        run_inplace_trace<Wn>(a_val, dest_val, n_val);
    }
}

template <std::size_t Wn>
static void run_oneshot_spots(uint32_t seed, std::size_t kCases) {
    std::mt19937_64 rng(seed);
    uint64_t n_lo = (Wn == 64u) ? (uint64_t{1} << 62)
                                  : (uint64_t{1} << (Wn - 1u));
    uint64_t n_hi = (Wn == 64u) ? ~uint64_t{0}
                                  : ((uint64_t{1} << Wn) - 1u);
    std::uniform_int_distribution<uint64_t> n_dist(n_lo, n_hi);
    for (std::size_t i = 0; i < kCases; ++i) {
        uint64_t n_val = n_dist(rng);
        std::uniform_int_distribution<uint64_t> ab(0u, n_val - 1u);
        uint64_t a_val = ab(rng);
        uint64_t b_val = ab(rng);
        run_oneshot_trace<Wn>(a_val, b_val, n_val);
    }
}

// ── public sturm::mul_mod<W=64> template instantiation check ──────────────
// This is the user-facing wrapper.  Confirms it compiles and emits a
// correct circuit at W=64, validating the issue's "Verify the public
// sturm::mul_mod template instantiates and emits correct circuits at the
// new W" bullet.  Mirrors test_qint_modular.cpp::run_mul_mod_case but
// scaled to W=64 with __uint128_t reference math.
template <std::size_t W>
static void run_public_mul_mod_case(uint64_t a_val, uint64_t b_val,
                                    uint64_t n_val) {
    assert(a_val < n_val && b_val < n_val);
    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_input = 3u * static_cast<uint32_t>(W);
    int reserved[n_input];
    for (uint32_t k = 0; k < n_input; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();

    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    std::array<int, W> qi_a{}, qi_b{}, qi_n{};
    for (std::size_t i = 0; i < W; ++i) {
        qi_a[i] = reserved[i];
        qi_b[i] = reserved[W + i];
        qi_n[i] = reserved[2 * W + i];
    }
    // Full super_mask so BitProxy emits gates instead of constant-folding.
    // For W=64 the mask is ~0ULL.
    const uint64_t full_mask = (W >= 64u) ? ~uint64_t{0}
                                          : ((uint64_t{1} << W) - 1u);
    sturm::qint_t<W> a = sturm::qint_t<W>::make_non_owning(
        qi_a, static_cast<int64_t>(a_val), full_mask);
    sturm::qint_t<W> b = sturm::qint_t<W>::make_non_owning(
        qi_b, static_cast<int64_t>(b_val), full_mask);
    sturm::qint_t<W> n = sturm::qint_t<W>::make_non_owning(
        qi_n, static_cast<int64_t>(n_val), full_mask);

    sturm::qint_t<W> r = sturm::mul_mod(a, b, n);
    const int high_water = sturm::QubitPool::instance().high_water();

    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < W; ++i) {
        if ((a_val >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])] = 1u;
        if ((b_val >> i) & 1u) bits[static_cast<std::size_t>(qi_b[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));

    const uint64_t expect_r = static_cast<uint64_t>(
        (static_cast<__uint128_t>(a_val) * static_cast<__uint128_t>(b_val))
        % n_val);
    assert(read_reg64(bits, qi_a.data(), W) == a_val);
    assert(read_reg64(bits, qi_b.data(), W) == b_val);
    assert(read_reg64(bits, qi_n.data(), W) == n_val);
    assert(read_reg64(bits, r.qubits.data(), W) == expect_r);
    // r.value tracking uses int64_t arithmetic in qint_modular.hpp, so it
    // overflows once a*b exceeds INT64_MAX (W >= ~32).  At high W we only
    // pin the quantum register's contents (the user-facing circuit
    // output); the int64_t .value shadow is a separate concern.
    assert(sturm::QubitPool::instance().in_use()
               == pre_in_use + static_cast<int>(W)
           && "public mul_mod: only result's W qubits remain live");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);

    { sturm::qint_t<W> sink = std::move(r); (void)sink; }
    for (uint32_t k = 0; k < n_input; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    constexpr std::size_t kCases = 3u;

    std::printf("sturm-8n73 highw-trace: lib_double_mod_dsl at "
                "W=40, 48, 56, 64 (%zu cases each, seed=%u):\n",
                kCases, 4001u);
    run_double_mod_spots<40u>(4001u, kCases);
    std::puts("  PASS: W=40 lib_double_mod_dsl trace");
    run_double_mod_spots<48u>(4002u, kCases);
    std::puts("  PASS: W=48 lib_double_mod_dsl trace");
    run_double_mod_spots<56u>(4003u, kCases);
    std::puts("  PASS: W=56 lib_double_mod_dsl trace");
    run_double_mod_spots<64u>(4004u, kCases);
    std::puts("  PASS: W=64 lib_double_mod_dsl trace");

    std::printf("sturm-8n73 highw-trace: lib_add_mod_inplace_dsl at "
                "W=40, 48, 56, 64 (%zu cases each, seed=%u):\n",
                kCases, 5001u);
    run_inplace_spots<40u>(5001u, kCases);
    std::puts("  PASS: W=40 lib_add_mod_inplace_dsl trace");
    run_inplace_spots<48u>(5002u, kCases);
    std::puts("  PASS: W=48 lib_add_mod_inplace_dsl trace");
    run_inplace_spots<56u>(5003u, kCases);
    std::puts("  PASS: W=56 lib_add_mod_inplace_dsl trace");
    run_inplace_spots<64u>(5004u, kCases);
    std::puts("  PASS: W=64 lib_add_mod_inplace_dsl trace");

    std::printf("sturm-8n73 highw-trace: lib_mul_mod_dsl_oneshot at "
                "W=40, 48, 56, 64 (%zu cases each, seed=%u):\n",
                kCases, 6001u);
    run_oneshot_spots<40u>(6001u, kCases);
    std::puts("  PASS: W=40 lib_mul_mod_dsl_oneshot trace");
    run_oneshot_spots<48u>(6002u, kCases);
    std::puts("  PASS: W=48 lib_mul_mod_dsl_oneshot trace");
    run_oneshot_spots<56u>(6003u, kCases);
    std::puts("  PASS: W=56 lib_mul_mod_dsl_oneshot trace");
    run_oneshot_spots<64u>(6004u, kCases);
    std::puts("  PASS: W=64 lib_mul_mod_dsl_oneshot trace");

    // sturm-8n73: public sturm::mul_mod<W> template instantiation at high
    // W.  Confirms the user-facing wrapper compiles and emits correct
    // circuits at W=40 and W=64.  Uses 3 spot cases per W with seed=7001.
    std::printf("sturm-8n73 highw-trace: public sturm::mul_mod<W> at "
                "W=40, 64 (%zu cases each, seed=%u):\n", kCases, 7001u);
    {
        std::mt19937_64 rng(7001u);
        constexpr std::size_t W40 = 40u;
        std::uniform_int_distribution<uint64_t> n_dist(uint64_t{1} << 39,
                                                        (uint64_t{1} << 40) - 1u);
        for (std::size_t i = 0; i < kCases; ++i) {
            uint64_t n_val = n_dist(rng);
            std::uniform_int_distribution<uint64_t> ab(0u, n_val - 1u);
            uint64_t a_val = ab(rng);
            uint64_t b_val = ab(rng);
            run_public_mul_mod_case<W40>(a_val, b_val, n_val);
        }
        std::puts("  PASS: W=40 sturm::mul_mod public template");
    }
    {
        std::mt19937_64 rng(7002u);
        constexpr std::size_t W64 = 64u;
        std::uniform_int_distribution<uint64_t> n_dist(uint64_t{1} << 62,
                                                        ~uint64_t{0});
        for (std::size_t i = 0; i < kCases; ++i) {
            uint64_t n_val = n_dist(rng);
            std::uniform_int_distribution<uint64_t> ab(0u, n_val - 1u);
            uint64_t a_val = ab(rng);
            uint64_t b_val = ab(rng);
            run_public_mul_mod_case<W64>(a_val, b_val, n_val);
        }
        std::puts("  PASS: W=64 sturm::mul_mod public template");
    }

    std::printf("All sturm-8n73 high-W modular-arith trace tests passed.\n");
    return 0;
}
