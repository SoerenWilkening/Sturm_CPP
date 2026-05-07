// test_when_lossy_gate_equiv_full.cpp -- LO-3a (sturm-vuru): gate-equivalence
// pinning for ALL FIVE controlled lossy compound assignments. Extends
// sturm-0j3a (`&=` only) to also pin `|=`, `*=`, `/=`, `%=`. Per op two
// IRs are recorded inside `WHEN(ctrl)`: the LO desugar via `invert<&...>()`
// + `lib_swap_dsl`, and a naive reference using `__lib_<op>_dsl_adj` +
// BitProxy XOR-triple swap. Acceptance: byte-for-byte IR equality. W=2 for
// &=/|=; W=1 for *=//=/%=.

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/c_and_dsl.hpp"
#include "sturm/detail/lib/div_dsl.hpp"
#include "sturm/detail/lib/logic_dsl.hpp"
#include "sturm/detail/lib/mul_dsl.hpp"
#include "sturm/detail/lib/swap_dsl.hpp"
#include "sturm/detail/qtypes/bit_proxy.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/control/when.hpp"
#include "sturm/routines/invert.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

struct AppendCtx {
    sturm_backend_context_t *ctx, *prev;
    AppendCtx() {
        ctx = sturm_backend_create(STURM_MODE_APPEND); assert(ctx);
        prev = sturm_get_thread_context(); sturm_set_thread_context(ctx);
    }
    ~AppendCtx() { sturm_set_thread_context(prev); sturm_backend_destroy(ctx); }
};
using IR = std::vector<sturm::GateRecord>;
static IR snap(const sturm::GateIR& ir) {
    IR v; v.reserve(ir.size());
    for (std::size_t i = 0; i < ir.size(); ++i) v.push_back(ir.at(i));
    return v;
}
static void eq(const IR& a, const IR& b, const char* tag) {
    if (a.size() != b.size()) {
        std::fprintf(stderr, "  FAIL %s: lo=%zu ref=%zu\n", tag, a.size(), b.size());
        std::abort();
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i]; const auto& y = b[i];
        if (x.kind != y.kind || x.n != y.n
            || x.qubits[0] != y.qubits[0] || x.qubits[1] != y.qubits[1]
            || x.qubits[2] != y.qubits[2]) {
            std::fprintf(stderr, "  FAIL %s gate %zu kind %d/%d\n", tag, i, int(x.kind), int(y.kind));
            std::abort();
        }
    }
}
static std::size_t kcount(const sturm::GateIR& ir, sturm_gate_kind_t k) {
    std::size_t n = 0u;
    for (std::size_t i = 0; i < ir.size(); ++i) if (ir.at(i).kind == k) ++n;
    return n;
}
template <std::size_t W> static void wire(sturm::qint_t<W>& q, const int* idx) {
    for (std::size_t i = 0; i < W; ++i) q.qubits[i] = idx[i];
    q.super_mask = (W >= 64) ? ~0ULL : ((1ULL << W) - 1ULL);
    q.owning_ = false;
}
template <std::size_t Wa, std::size_t Wb>
static void naive_swap(sturm::qint_t<Wa>& a, sturm::qint_t<Wb>& b) {
    static_assert(Wa <= Wb);
    for (std::size_t i = 0; i < Wa; ++i) {
        sturm::BitProxy x(a, i), y(b, i); x ^= y; y ^= x; x ^= y;
    }
}
template <std::size_t Wa, std::size_t Wb>
static void lifted_swap(sturm::qint_t<Wa>& a, sturm::qint_t<Wb>& b) {
    for (std::size_t i = 0; i < Wa; ++i) {
        sturm::BitProxy x(a, i), y(b, i); sturm::lib_swap_dsl<sturm::BitProxy>(x, y);
    }
}
template <std::size_t W>
static int alloc_n(int* out, std::size_t n_regs) {
    sturm::QubitPool::instance().reset_for_testing();
    for (std::size_t i = 0; i < n_regs * W; ++i) out[i] = sturm::QubitPool::instance().allocate();
    return sturm::QubitPool::instance().allocate();
}
template <typename F>
static IR record_when(int qc, F&& body, std::size_t* ccx_out = nullptr) {
    AppendCtx sc;
    sturm::qbool ctrl = sturm::qbool::make_non_owning(qc);
    ctrl.super_mask = 1ULL; ctrl.value = 0;
    WHEN(ctrl) { body(); }
    if (ccx_out) *ccx_out = kcount(sc.ctx->ir, STURM_GATE_CCX);
    return snap(sc.ctx->ir);
}

// ── &= and |= -- 3-reg, equal-width (W=2) ───────────────────────────────────
template <typename Fwd, typename Adj>
static void run_bitwise(const char* tag, Fwd fwd, Adj adj, std::size_t exp_ccx) {
    constexpr std::size_t W = 2;
    int regs[3 * W]; const int qc = alloc_n<W>(regs, 3);
    auto run = [&](bool use_invert, std::size_t* ccx_out = nullptr) {
        sturm::qint_t<W> a, b, t;
        wire(a, regs); wire(b, regs + W); wire(t, regs + 2 * W);
        return record_when(qc, [&]() {
            for (std::size_t i = 0; i < W; ++i) { sturm::BitProxy ab(a, i), bb(b, i), tb(t, i); fwd(ab, bb, tb); }
            if (use_invert) { lifted_swap(a, t); lifted_swap(a, t); }
            else            { naive_swap(a, t); naive_swap(a, t); }
            for (std::size_t i = 0; i < W; ++i) { sturm::BitProxy ab(a, i), bb(b, i), tb(t, i); adj(ab, bb, tb); }
        }, ccx_out);
    };
    std::size_t ccx_lo = 0u;
    auto lo_ir = run(true, &ccx_lo);
    auto ref_ir = run(false);
    eq(lo_ir, ref_ir, tag);
    assert(ccx_lo == exp_ccx);
    std::printf("  PASS: %s (%zu CCX)\n", tag, ccx_lo);
}

static void test_and_assign() {
    // c_AND/bit = 1 CCX -> 3 CCX/bit lifted; fwd+invert = 6W; 2 swaps = 6W. 12W.
    run_bitwise("&=", [](sturm::BitProxy& a, sturm::BitProxy& b, sturm::BitProxy& t) {
        sturm::lib_c_AND_dsl<sturm::BitProxy>(a, b, t); },
        sturm::invert<&sturm::lib_c_AND_dsl<sturm::BitProxy>>(), 12u * 2u);
}
static void test_or_assign() {
    // or/bit = 2 CX + 1 CCX -> 5 CCX/bit lifted; fwd+invert = 10W; swaps 6W. 16W.
    run_bitwise("|=", [](sturm::BitProxy& a, sturm::BitProxy& b, sturm::BitProxy& t) {
        sturm::lib_or_dsl<sturm::BitProxy>(a, b, t); },
        sturm::invert<&sturm::lib_or_dsl<sturm::BitProxy>>(), 16u * 2u);
}

// ── *= -- W=1; pins __lib_mul_dsl_adj equivalence ──────────────────────────
static void test_mul_assign() {
    constexpr std::size_t W = 1;
    constexpr auto adj = sturm::invert<&sturm::lib_mul_dsl<sturm::BitProxy>>();
    int regs[4 * W]; const int qc = alloc_n<W>(regs, 4);
    auto run = [&](bool use_invert) {
        sturm::qint_t<W> a, b; sturm::qint_t<2 * W> t;
        wire(a, regs); wire(b, regs + W);
        for (std::size_t i = 0; i < 2 * W; ++i) t.qubits[i] = regs[2 * W + i];
        t.super_mask = (1ULL << (2 * W)) - 1ULL; t.owning_ = false;
        return record_when(qc, [&]() {
            sturm::BitProxy ab[W], bb[W], tb[2 * W];
            for (std::size_t i = 0; i < W; ++i) { ab[i] = sturm::BitProxy(a, i); bb[i] = sturm::BitProxy(b, i); }
            for (std::size_t i = 0; i < 2 * W; ++i) tb[i] = sturm::BitProxy(t, i);
            sturm::lib_mul_dsl<sturm::BitProxy>(ab, W, bb, W, tb, 2 * W);
            if (use_invert) { lifted_swap(a, t); lifted_swap(a, t); adj(ab, W, bb, W, tb, 2 * W); }
            else            { naive_swap(a, t); naive_swap(a, t);
                              sturm::__lib_mul_dsl_adj<sturm::BitProxy>(ab, W, bb, W, tb, 2 * W); }
        });
    };
    eq(run(true), run(false), "*=");
    std::puts("  PASS: *=");
}

// ── /= and %= -- W=1; shared scaffold ──────────────────────────────────────
template <bool IsMod>
static void run_div_mod(const char* tag) {
    constexpr std::size_t W = 1;
    constexpr auto adj = sturm::invert<&sturm::lib_div_dsl<sturm::BitProxy>>();
    int regs[4 * W]; const int qc = alloc_n<W>(regs, 4);
    auto run = [&](bool use_invert) {
        sturm::qint_t<W> a, b, q, r;
        wire(a, regs); wire(b, regs + W); wire(q, regs + 2 * W); wire(r, regs + 3 * W);
        return record_when(qc, [&]() {
            sturm::BitProxy ab[W], bb[W], qb[W], rb[W];
            for (std::size_t i = 0; i < W; ++i) {
                ab[i] = sturm::BitProxy(a, i); bb[i] = sturm::BitProxy(b, i);
                qb[i] = sturm::BitProxy(q, i); rb[i] = sturm::BitProxy(r, i);
            }
            sturm::lib_div_dsl<sturm::BitProxy>(ab, W, bb, W, qb, rb);
            auto& sw = IsMod ? r : q;
            if (use_invert) { lifted_swap(a, sw); lifted_swap(a, sw); adj(ab, W, bb, W, qb, rb); }
            else            { naive_swap(a, sw); naive_swap(a, sw);
                              sturm::__lib_div_dsl_adj<sturm::BitProxy>(ab, W, bb, W, qb, rb); }
        });
    };
    eq(run(true), run(false), tag);
    std::printf("  PASS: %s\n", tag);
}
static void test_div_assign() { run_div_mod<false>("/="); }
static void test_mod_assign() { run_div_mod<true >("%="); }

int main() {
    std::printf("sturm-vuru LO-3a controlled lossy gate-equivalence (5 ops):\n");
    test_and_assign();
    test_or_assign();
    test_mul_assign();
    test_div_assign();
    test_mod_assign();
    std::printf("All sturm-vuru tests passed.\n");
    return 0;
}
