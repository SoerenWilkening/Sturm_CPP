// test_pow_mod_xval.cpp -- sturm-bjt2.2 P6.2 pow-mod-xval-test
// (Plan §8.2 / PRD §6 #6).  20 random (a, x, n) with a < n at W=2, seed
// std::mt19937(42).  Invokes BOTH `lib_pow_dsl + lib_mod_dsl` (default
// `STURM_MODULAR_POW=OFF` lowering — run in two SimCtx scopes; chained
// pow+mod at W=2 exceeds orkan's 17-qubit cap, so the intermediate pow
// is read classically and re-seeded) AND `lib_pow_mod_dsl` directly
// (`=ON` lowering — APPEND-mode classical replay; chain pow_mod peaks
// above orkan's ceiling at W>=2, same harness as test_pow_mod_dsl.cpp /
// test_qint_modular.cpp).  Asserts identical r.  Sampling rejects
// a^x >= 2^W so stage-A2's W-bit dividend carries pow untruncated.
// Failure messages print the seed (plan §12 risk row).  Library targets
// do not propagate -DSTURM_MODULAR_POW so this compiles identically with
// the flag OFF and ON, discharging plan §8.3 from the lib side.

#define STURM_BACKEND_ENABLED 1
#include "sturm/lib/pow_dsl.hpp"
#include "sturm/lib/mod_dsl.hpp"
#include "sturm/lib/pow_mod_dsl.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <random>
#include <vector>

namespace {
constexpr std::size_t W = 2;
constexpr uint32_t    n_orkan = 17u, kSeed = 42u;
constexpr std::size_t kCases  = 20u;

// Fill qbool/BitProxy arrays from an int* of qubit indices.
void wrap(const int* qi, sturm::qbool* q, sturm::BitProxy* b) {
    for (std::size_t i = 0; i < W; ++i) {
        q[i] = sturm::qbool::make_non_owning(qi[i]); b[i] = sturm::BitProxy(q[i]);
    }
}

uint32_t read_W(orkan::state_t& sv, const int* qi) {
    for (uint64_t s = 0, dim = uint64_t{1} << n_orkan; s < dim; ++s)
        if (std::norm(orkan::amplitude(sv, s)) > 1e-9) {
            uint32_t v = 0u;
            for (std::size_t k = 0; k < W; ++k)
                v |= (static_cast<uint32_t>((s >> qi[k]) & 1u) << k);
            return v;
        }
    return 0u;
}

template <typename Fn>
uint32_t sim_call(uint32_t v0, uint32_t v1, Fn body) {
    sturm::QubitPool::instance().reset_for_testing();
    int rs[3 * W]; for (std::size_t k = 0; k < 3u * W; ++k)
        rs[k] = sturm::QubitPool::instance().allocate();
    sturm::OrkanBridge br; br.allocate(n_orkan);
    auto* ctx = sturm_backend_create(STURM_MODE_SIMULATE, 64u); ctx->orkan_state_ptr = &br;
    auto* prev = sturm_get_thread_context(); sturm_set_thread_context(ctx);
    int q0[W], q1[W], qr[W];
    for (std::size_t i = 0; i < W; ++i) {
        q0[i] = rs[i]; q1[i] = rs[W + i]; qr[i] = rs[2 * W + i];
        if ((v0 >> i) & 1u) orkan::apply_x(br.state(), q0[i]);
        if ((v1 >> i) & 1u) orkan::apply_x(br.state(), q1[i]);
    }
    body(q0, q1, qr);
    uint32_t out = read_W(br.state(), qr);
    sturm_set_thread_context(prev); sturm_backend_destroy(ctx);
    for (std::size_t k = 3u * W; k-- > 0u;) sturm::QubitPool::instance().release(rs[k]);
    return out;
}

uint32_t path_a_pow(uint32_t a, uint32_t x) {
    return sim_call(a, x, [](int* qa, int* qx, int* qr) {
        sturm::qbool aq[W], xq[W], rq[W]; sturm::BitProxy s[W];
        wrap(qa, aq, s); wrap(qx, xq, s); wrap(qr, rq, s);
        sturm::lib_pow_dsl(aq, W, xq, W, rq, W);
    });
}

uint32_t path_a_mod(uint32_t pv, uint32_t n) {
    return sim_call(pv, n, [](int* qd, int* qn, int* qr) {
        sturm::qbool dq[W], nq[W], rq[W]; sturm::BitProxy db[W], nb[W], rb[W];
        wrap(qd, dq, db); wrap(qn, nq, nb); wrap(qr, rq, rb);
        sturm::lib_mod_dsl<sturm::BitProxy>(db, W, nb, W, rb);
    });
}

uint32_t path_b(uint32_t a, uint32_t x, uint32_t n) {
    sturm::QubitPool::instance().reset_for_testing();
    int qa[W], qx[W], qn[W], qr[W]; int* regs[4] = {qa, qx, qn, qr};
    for (auto* r : regs) for (std::size_t i = 0; i < W; ++i)
        r[i] = sturm::QubitPool::instance().allocate();
    sturm::qbool aq[W], xq[W], nq[W], rq[W]; sturm::BitProxy ab[W], xb[W], nb[W], rb[W];
    wrap(qa, aq, ab); wrap(qx, xq, xb); wrap(qn, nq, nb); wrap(qr, rq, rb);
    auto* ctx = sturm_backend_create(STURM_MODE_APPEND, 64u);
    auto* prev = sturm_get_thread_context(); sturm_set_thread_context(ctx);
    sturm::lib_pow_mod_dsl<sturm::BitProxy>(ab, xb, nb, W, rb);
    std::vector<uint8_t> bits(
        static_cast<std::size_t>(sturm::QubitPool::instance().high_water()), 0u);
    uint32_t vals[3] = {a, x, n}; int* inr[3] = {qa, qx, qn};
    for (std::size_t r = 0; r < 3; ++r) for (std::size_t i = 0; i < W; ++i)
        if ((vals[r] >> i) & 1u) bits[inr[r][i]] = 1u;
    for (std::size_t i = 0; i < ctx->ir.size(); ++i) {
        const auto& g = ctx->ir.at(i); auto k = g.kind;
        if (k == STURM_GATE_X) bits[g.qubits[0]] ^= 1u;
        else if (k == STURM_GATE_CX) { if (bits[g.qubits[0]]) bits[g.qubits[1]] ^= 1u; }
        else if (k == STURM_GATE_CCX) { if (bits[g.qubits[0]] && bits[g.qubits[1]])
                                           bits[g.qubits[2]] ^= 1u; }
        else std::abort();
    }
    uint32_t r = 0u;
    for (std::size_t i = 0; i < W; ++i) if (bits[qr[i]]) r |= (1u << i);
    sturm_set_thread_context(prev); sturm_backend_destroy(ctx);
    return r;
}
}  // namespace

int main() {
    std::printf("sturm-bjt2.2 P6.2 pow-mod-xval: %zu cases, seed=%u, W=%zu\n",
                kCases, kSeed, W);
    std::mt19937 rng(kSeed);
    std::uniform_int_distribution<uint32_t> n_d(1u, (1u << W) - 1u);
    std::uniform_int_distribution<uint32_t> x_d(0u, (1u << W) - 1u);
    for (std::size_t i = 0; i < kCases;) {
        uint32_t n = n_d(rng);
        std::uniform_int_distribution<uint32_t> a_d(0u, n - 1u);
        uint32_t a = a_d(rng), x = x_d(rng);
        uint64_t p = 1u; for (uint32_t k = 0; k < x; ++k) p *= a;
        if (p >= (uint64_t{1} << W)) continue;
        uint32_t r_a = path_a_mod(path_a_pow(a, x), n);
        uint32_t r_b = path_b(a, x, n);
        if (r_a != r_b) {
            std::fprintf(stderr, "FAIL pow-mod-xval seed=%u case=%zu a=%u x=%u n=%u: "
                "lib_pow_dsl+lib_mod_dsl r_a=%u != lib_pow_mod_dsl r_b=%u\n",
                kSeed, i, a, x, n, r_a, r_b);
            assert(false && "pow-mod-xval: r_a != r_b (seed in stderr)");
        }
        std::printf("  PASS case %zu: a=%u x=%u n=%u -> r=%u\n", i, a, x, n, r_a);
        ++i;
    }
    std::printf("All sturm-bjt2.2 pow-mod-xval cases passed (seed=%u).\n", kSeed);
    return 0;
}
