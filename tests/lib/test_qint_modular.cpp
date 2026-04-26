// test_qint_modular.cpp -- sturm-kgwx P4 free-function wrappers in
// include/sturm/ops/qint_modular.hpp.  Plan §6.3 budget <= 200 LoC.
//
// Beat 4.1 (sturm-kgwx.1): `sturm::add_mod(a, b, n)` matches lib_add_mod_dsl
// for one W=2 case.  Uses the orkan state-vector simulator at the
// kMaxQubits=17 cap (test_add_mod_dsl.cpp SimCtx/Reg pattern).
// Beat 4.2 (sturm-kgwx.2): `sturm::mul_mod(a, b, n)` matches lib_mul_mod_dsl
// for one W=2 case.  Uses APPEND-mode classical-trace replay because
// lib_mul_mod_dsl peaks above 17 qubits at W=2 (mirrors test_mul_mod_dsl.cpp
// beat 2.4 harness).
// Beat 4.3 (sturm-kgwx.3): `sturm::pow_mod(base, exp, n)` matches
// lib_pow_mod_dsl for one W=2 case.  Uses APPEND-mode classical-trace replay
// because lib_pow_mod_dsl peaks far above 17 qubits at W>=2 (mirrors beat
// 4.2 harness; same harness as test_pow_mod_dsl.cpp beat 3.4).  Beat 4.4
// fills in move/copy.

#define STURM_BACKEND_ENABLED 1
#include "sturm/ops/qint_modular.hpp"
#include "sturm/lib/add_mod_dsl.hpp"
#include "sturm/lib/mul_mod_dsl.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <vector>

static constexpr double kTol = 1e-9;
static constexpr std::size_t W = 2;
// Hard 17-qubit simulator cap.  At W=2 the wrapper places a, b, n on 3*W=6
// pre-allocated qubits, lib_add_mod_dsl uses (W+1)+1+1+1=5 sum/flag
// ancillas, and the wrapper's fresh result register adds another W=2.
// Total live: 3*W + W + (W+1) + 3 = 13 qubits, comfortably below 17.
static constexpr uint32_t n_orkan = 17u;

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 64u) {
        bridge.allocate(n_q);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
        assert(ctx);
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
    orkan::state_t& sv() { return bridge.state(); }
};

// Read a register's classical bit pattern from the dominant basis state of
// the simulator state vector.  Mirrors test_add_mod_dsl.cpp.
static uint32_t read_reg(orkan::state_t& sv, const int* qi, uint32_t n,
                         uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            uint32_t v = 0u;
            for (uint32_t k = 0; k < n; ++k)
                if (qi[k] >= 0)
                    v |= (static_cast<uint32_t>((s >> qi[k]) & 1u) << k);
            return v;
        }
    }
    return 0u;
}

// Build a fully-allocated qint_t<W> view at fixed qubit indices base..base+W,
// initializing each bit by emitting an X on the matching qubit when the bit
// is 1.  Uses make_non_owning to avoid double-release with the pool reset.
static sturm::qint_t<W> make_qint(int base, uint32_t val,
                                  orkan::state_t& sv) {
    std::array<int, W> qi{};
    for (std::size_t i = 0; i < W; ++i) {
        qi[i] = base + static_cast<int>(i);
        if ((val >> i) & 1u) orkan::apply_x(sv, static_cast<uint32_t>(qi[i]));
    }
    uint64_t mask = (1ULL << W) - 1ULL;  // every bit is "quantum" / allocated
    return sturm::qint_t<W>::make_non_owning(qi, static_cast<int64_t>(val),
                                             mask);
}

// Beat 4.1 driver.  Asserts:
//   - sturm::add_mod(a, b, n) returns a qint_t<W> whose underlying register
//     holds (a + b) mod n on the simulator (matches a direct lib_add_mod_dsl
//     invocation by construction),
//   - inputs a, b, n are unchanged (PRD §5: read-only inputs),
//   - QubitPool::in_use() returns to its pre-call value (no leaked ancillas).
static void run_add_mod_case(uint32_t a_val, uint32_t b_val, uint32_t n_val) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");
    sturm::QubitPool::instance().reset_for_testing();
    // Reserve 3*W qubits for the input registers a, b, n at fixed indices.
    // (The wrapper allocates the result's W qubits + algorithm internals
    // beyond that high-water mark.)
    constexpr uint32_t n_input = 3u * W;
    int reserved[n_input];
    for (uint32_t k = 0; k < n_input; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();

    SimCtx sc{n_orkan, 64u};
    sturm::qint_t<W> a = make_qint(0,         a_val, sc.sv());
    sturm::qint_t<W> b = make_qint(W,         b_val, sc.sv());
    sturm::qint_t<W> n = make_qint(2 * W,     n_val, sc.sv());

    // Wrapper under test.  Returns a fresh, owning qint_t<W> holding the
    // modular sum.  The wrapper allocates its own result-register qubits;
    // ancillas inside lib_add_mod_dsl are LIFO-released before return.
    sturm::qint_t<W> r = sturm::add_mod(a, b, n);

    // Result register must hold (a + b) mod n on the simulator.
    const uint32_t expect_r = (a_val + b_val) % n_val;
    uint32_t r_sv = read_reg(sc.sv(), r.qubits.data(), W, n_orkan);
    assert(r_sv == expect_r && "add_mod: r == (a+b) mod n");
    assert(static_cast<uint32_t>(r.value) == expect_r
           && "add_mod: classical .value tracks the quantum result");

    // Inputs unchanged on the simulator (reversibility of read-only inputs).
    uint32_t a_sv = read_reg(sc.sv(), a.qubits.data(), W, n_orkan);
    uint32_t b_sv = read_reg(sc.sv(), b.qubits.data(), W, n_orkan);
    uint32_t n_sv = read_reg(sc.sv(), n.qubits.data(), W, n_orkan);
    assert(a_sv == a_val && "add_mod: a register unchanged");
    assert(b_sv == b_val && "add_mod: b register unchanged");
    assert(n_sv == n_val && "add_mod: n register unchanged");

    // Pool live-count: the wrapper's only net allocation is r's W qubits;
    // every ancilla allocated inside lib_add_mod_dsl is LIFO-released.
    assert(sturm::QubitPool::instance().in_use() == pre_in_use + static_cast<int>(W)
           && "add_mod: only the result's W qubits remain live after the call");

    // Tear r down to release its qubits before reset_for_testing.
    {
        sturm::qint_t<W> sink = std::move(r);
        (void)sink;
    }
    for (uint32_t k = 0; k < n_input; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── Beat 4.2 — mul_mod wrapper test (APPEND classical-trace) ─────────────
// lib_mul_mod_dsl peaks above 17 qubits at W=2 — drive wrapper in APPEND
// mode, capture X/CX/CCX into ctx.ir, replay over a bit-vector seeded with
// the input register values.  Mirror of test_mul_mod_dsl.cpp beat 2.4.

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
        std::fprintf(stderr,
                     "trace: unsupported gate kind %d at index %u\n",
                     static_cast<int>(rec.kind), rec.qubits[0]);
        std::abort();
    }
}

static uint32_t read_reg_classical(const std::vector<uint8_t>& bits,
                                   const int* qi, std::size_t n) {
    uint32_t v = 0u;
    for (std::size_t k = 0; k < n; ++k) {
        if (qi[k] >= 0 && bits[static_cast<std::size_t>(qi[k])])
            v |= (1u << k);
    }
    return v;
}

// Beat 4.2 driver.  Asserts r == (a*b) mod n via classical replay, inputs
// unchanged, pool live-count returns to pre-call + W (only r's qubits
// remain), classical .value tracks the quantum result.
static void run_mul_mod_case(uint32_t a_val, uint32_t b_val, uint32_t n_val) {
    assert(a_val < n_val && "test precondition: a < n");
    assert(b_val < n_val && "test precondition: b < n");
    sturm::QubitPool::instance().reset_for_testing();
    // Reserve 3*W qubit indices for a, b, n at fixed slots; wrapper
    // allocates r's W qubits beyond that.
    constexpr uint32_t n_input = 3u * W;
    int reserved[n_input];
    for (uint32_t k = 0; k < n_input; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();

    // APPEND-mode context (execute_gate just records to ctx.ir).
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 64u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    // qint_t<W> non-owning views with super_mask=full so BitProxy emits
    // gates rather than classical-folding.
    std::array<int, W> qi_a{}, qi_b{}, qi_n{};
    for (std::size_t i = 0; i < W; ++i) {
        qi_a[i] = reserved[i];
        qi_b[i] = reserved[W + i];
        qi_n[i] = reserved[2 * W + i];
    }
    const uint64_t full_mask = (1ULL << W) - 1ULL;
    sturm::qint_t<W> a = sturm::qint_t<W>::make_non_owning(
        qi_a, static_cast<int64_t>(a_val), full_mask);
    sturm::qint_t<W> b = sturm::qint_t<W>::make_non_owning(
        qi_b, static_cast<int64_t>(b_val), full_mask);
    sturm::qint_t<W> n = sturm::qint_t<W>::make_non_owning(
        qi_n, static_cast<int64_t>(n_val), full_mask);

    // Wrapper under test: fresh owning qint_t<W>, all ancillas LIFO-released.
    sturm::qint_t<W> r = sturm::mul_mod(a, b, n);
    const int high_water = sturm::QubitPool::instance().high_water();

    // Seed bit-vector with input values, replay captured stream.
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < W; ++i) {
        if ((a_val >> i) & 1u) bits[static_cast<std::size_t>(qi_a[i])] = 1u;
        if ((b_val >> i) & 1u) bits[static_cast<std::size_t>(qi_b[i])] = 1u;
        if ((n_val >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])] = 1u;
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));

    const uint32_t expect_r = (a_val * b_val) % n_val;
    const uint32_t a_out = read_reg_classical(bits, qi_a.data(), W);
    const uint32_t b_out = read_reg_classical(bits, qi_b.data(), W);
    const uint32_t n_out = read_reg_classical(bits, qi_n.data(), W);
    const uint32_t r_out =
        read_reg_classical(bits, r.qubits.data(), W);
    assert(a_out == a_val && "mul_mod: a register unchanged");
    assert(b_out == b_val && "mul_mod: b register unchanged");
    assert(n_out == n_val && "mul_mod: n register unchanged");
    assert(r_out == expect_r && "mul_mod: r == (a*b) mod n");
    assert(static_cast<uint32_t>(r.value) == expect_r
           && "mul_mod: classical .value tracks the quantum result");

    assert(sturm::QubitPool::instance().in_use() == pre_in_use + static_cast<int>(W)
           && "mul_mod: only the result's W qubits remain live after the call");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);

    { sturm::qint_t<W> sink = std::move(r); (void)sink; }
    for (uint32_t k = 0; k < n_input; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

// ── Beat 4.3 — pow_mod wrapper test (APPEND classical-trace) ─────────────
// lib_pow_mod_dsl peaks far above 17 qubits at W>=2 — drive wrapper in
// APPEND mode, capture X/CX/CCX into ctx.ir, replay over a bit-vector
// seeded with the input register values.  Mirror of run_mul_mod_case
// (beat 4.2) and test_pow_mod_dsl.cpp::run_pow_classical_case_w2.
//
// Asserts r == (base^exp) mod n via classical replay, inputs unchanged
// (PRD §5: read-only inputs), pool live-count returns to pre-call + W
// (only r's qubits remain), classical .value tracks the quantum result.
static void run_pow_mod_case(uint32_t base_val, uint32_t exp_val,
                             uint32_t n_val) {
    assert(n_val >= 1u && "test precondition: n >= 1 (n==0 is the no-op path)");
    assert(base_val < n_val && "test precondition: base < n (PRD §5)");
    assert(exp_val < (1u << W) && "test precondition: exp fits in W bits");
    sturm::QubitPool::instance().reset_for_testing();
    // Reserve 3*W qubit indices for base, exp, n at fixed slots; wrapper
    // allocates r's W qubits beyond that.
    constexpr uint32_t n_input = 3u * W;
    int reserved[n_input];
    for (uint32_t k = 0; k < n_input; ++k)
        reserved[k] = sturm::QubitPool::instance().allocate();
    const int pre_in_use = sturm::QubitPool::instance().in_use();

    // APPEND-mode context (execute_gate just records to ctx.ir).
    sturm_backend_context_t* ctx =
        sturm_backend_create(STURM_MODE_APPEND, 64u);
    assert(ctx);
    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    // qint_t<W> non-owning views with super_mask=full so BitProxy emits
    // gates rather than classical-folding.
    std::array<int, W> qi_base{}, qi_exp{}, qi_n{};
    for (std::size_t i = 0; i < W; ++i) {
        qi_base[i] = reserved[i];
        qi_exp[i]  = reserved[W + i];
        qi_n[i]    = reserved[2 * W + i];
    }
    const uint64_t full_mask = (1ULL << W) - 1ULL;
    sturm::qint_t<W> base = sturm::qint_t<W>::make_non_owning(
        qi_base, static_cast<int64_t>(base_val), full_mask);
    sturm::qint_t<W> exp_ = sturm::qint_t<W>::make_non_owning(
        qi_exp, static_cast<int64_t>(exp_val), full_mask);
    sturm::qint_t<W> n = sturm::qint_t<W>::make_non_owning(
        qi_n, static_cast<int64_t>(n_val), full_mask);

    // Wrapper under test: fresh owning qint_t<W>, all ancillas LIFO-released.
    sturm::qint_t<W> r = sturm::pow_mod(base, exp_, n);
    const int high_water = sturm::QubitPool::instance().high_water();

    // Seed bit-vector with input values, replay captured stream.
    std::vector<uint8_t> bits(static_cast<std::size_t>(high_water), 0u);
    for (std::size_t i = 0; i < W; ++i) {
        if ((base_val >> i) & 1u) bits[static_cast<std::size_t>(qi_base[i])] = 1u;
        if ((exp_val  >> i) & 1u) bits[static_cast<std::size_t>(qi_exp[i])]  = 1u;
        if ((n_val    >> i) & 1u) bits[static_cast<std::size_t>(qi_n[i])]    = 1u;
    }
    for (std::size_t i = 0; i < ctx->ir.size(); ++i)
        apply_gate_classical(bits, ctx->ir.at(i));

    // Reference: (base^exp) mod n via repeated unsigned multiplication.
    // Convention 0^0 = 1 (matches lib_pow_dsl / lib_pow_mod_dsl).
    uint64_t expect_r64 = 1u;
    for (uint32_t k = 0; k < exp_val; ++k)
        expect_r64 = (expect_r64 * static_cast<uint64_t>(base_val))
                     % static_cast<uint64_t>(n_val);
    const uint32_t expect_r = static_cast<uint32_t>(expect_r64);

    const uint32_t base_out = read_reg_classical(bits, qi_base.data(), W);
    const uint32_t exp_out  = read_reg_classical(bits, qi_exp.data(),  W);
    const uint32_t n_out    = read_reg_classical(bits, qi_n.data(),    W);
    const uint32_t r_out    = read_reg_classical(bits, r.qubits.data(), W);
    assert(base_out == base_val && "pow_mod: base register unchanged");
    assert(exp_out  == exp_val  && "pow_mod: exp register unchanged");
    assert(n_out    == n_val    && "pow_mod: n register unchanged");
    assert(r_out    == expect_r && "pow_mod: r == (base^exp) mod n");
    assert(static_cast<uint32_t>(r.value) == expect_r
           && "pow_mod: classical .value tracks the quantum result");

    assert(sturm::QubitPool::instance().in_use() == pre_in_use + static_cast<int>(W)
           && "pow_mod: only the result's W qubits remain live after the call");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);

    { sturm::qint_t<W> sink = std::move(r); (void)sink; }
    for (uint32_t k = 0; k < n_input; ++k)
        sturm::QubitPool::instance().release(reserved[k]);
}

int main() {
    std::printf("sturm-kgwx.1 P4.1 qint_modular: add_mod wrapper test:\n");
    run_add_mod_case(/*a=*/1u, /*b=*/1u, /*n=*/3u);
    std::puts("  PASS: add_mod(1, 1, 3) == 2 matches lib_add_mod_dsl");

    std::printf("sturm-kgwx.2 P4.2 qint_modular: mul_mod wrapper test:\n");
    run_mul_mod_case(/*a=*/2u, /*b=*/2u, /*n=*/3u);
    std::puts("  PASS: mul_mod(2, 2, 3) == 1 matches lib_mul_mod_dsl");

    std::printf("sturm-kgwx.3 P4.3 qint_modular: pow_mod wrapper test:\n");
    run_pow_mod_case(/*base=*/2u, /*exp=*/3u, /*n=*/3u);
    std::puts("  PASS: pow_mod(2, 3, 3) == 2 matches lib_pow_mod_dsl");

    std::printf("All sturm-kgwx.{1,2,3} tests passed.\n");
    return 0;
}
