// qram_read_dsl.hpp -- sturm-2w6h.4 (Beat B2).
//
// Sequential XOR-fanout DSL body for the QROM path of `QRAM_read`
// (PRD `docs/prd_qram_backend.md` §4 / plan `docs/plan_qram_backend.md`
// §5 Beat B2). The body is a pure DSL composition built on
// `lib_qram_eq_k_compute` / `_uncompute` (B2a, sturm-2w6h.3) and the
// `WHEN` macro (depth-1 control invariant per B5a). No raw gate calls,
// no rotations — only the {X, CX, CCX} primitives leak into the IR via
// the lifted emitters.
//
// Surface:
//
//   template <std::size_t W>
//   inline void lib_qram_read_qrom_dsl(const qint_t<W>* a, std::size_t n,
//                                      qint_t<W>& i, qint_t<W>& b);
//
// Algorithm (PRD §4):
//   for k in [0, n):
//       compute eq_k = (i[0..K-1] == k[0..K-1])
//       WHEN(eq_k) { for j in set bits of a[k]: b.bit(j).flip() }
//       uncompute eq_k
//   where K = ceil_log2(n) is the number of address bits actually
//   inspected. The classical fan-out over `a[k]`'s set bits is safe
//   because the QROM precondition (PRD §3 / §5) says every container
//   element is fully classical (super_mask == 0) — so `a[k].value`
//   captures the full bit pattern, and each `b.bit(j).flip()` lifts
//   to a CX controlled on `eq_k` via the active `WHEN(eq_k)` scope
//   (depth-1 invariant: the predicate ancilla is the single live
//   control bit when the body runs).
//
// Self-adjoint:
//   The body is its own inverse. The compute/uncompute pair around
//   `WHEN(eq_k) { ... }` cancels (predicate is self-inverse, B2a),
//   and the WHEN-body is a sequence of CX flips on disjoint targets
//   which is also self-inverse. The B3 (sturm-2w6h.5) sibling header
//   `qram_read_dsl_adj.hpp` will register this property formally with
//   `STURM_REGISTER_ADJOINT(lib_qram_read_qrom_dsl, ...)`.
//
// Caller invariants (PRD §3 / §5):
//   - `a[k].super_mask == 0` for every k in [0, n) — the QROM
//     precondition. Mixed / quantum-register paths route to the qreg
//     helper (out of scope for this beat).
//   - `i` and `b` may be fully classical or fully quantum; the helper
//     allocates `eq_k`'s qubit lazily through the predicate helper
//     and through `WHEN` materialisation.
//   - `i` is an in-out param (predicate helper transiently flips bits
//     of `i` but pairs every flip; net change is zero).
//   - `b` is an out-param: bits j of `b` flip iff bit-j of `a[i_val]`
//     is set, controlled on the eq_k WHEN scope.
//
// Counter-mode bumps (umbrella `qram_read` and the split
// `qrom_read` / `qreg_read` in B4) live on the public dispatch site
// `_qram_detail::dispatch_common`, NOT in this DSL helper. Gate
// emission is purely a side-effect of the underlying DSL ops;
// telemetry is the dispatcher's responsibility.
//
// Threading: header-only inline templates; no thread-locals introduced.
// LoC budget: <= 300 (plan §1, §5 / B2).

#pragma once

#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/control/when.hpp"
#include "sturm/detail/lib/qram_read_predicate.hpp"

#ifdef STURM_BACKEND_ENABLED
#  include "sturm/detail/qtypes/bit_proxy.hpp"
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace sturm {

namespace detail_qram_read_dsl {

// ── ceil_log2 helper ─────────────────────────────────────────────────
// Number of address bits required to index N entries (N == 0 / 1 → 0;
// N == 2 → 1; N == 4 → 2; N == 5 → 3; etc.). Runtime — `n` is a
// runtime parameter on the public surface.
inline std::size_t ceil_log2(std::size_t n) noexcept {
    if (n <= 1u) return 0u;
    std::size_t r = 0u;
    std::size_t v = n - 1u;
    while (v > 0u) { ++r; v >>= 1; }
    return r;
}

}  // namespace detail_qram_read_dsl

// ── lib_qram_read_qrom_dsl ───────────────────────────────────────────
// QROM body in pure DSL form. Caller guarantees the QROM precondition
// (every `a[k].super_mask == 0`); the dispatcher in `qram_read.hpp`
// enforces this via `any_super_mask`.
//
// Implementation notes:
//   - `eq_k` is a single shared predicate ancilla allocated once per
//     call (via `qbool` default ctor + `ensure_qubit()` invoked by
//     `WHEN(eq_k)`'s materialisation; the predicate helper also calls
//     `ensure_qubit()` indirectly when `eq_k.qubits[0]` is < 0). To
//     keep the code obvious and to match the predicate helper's
//     "qubit allocated before call" precondition, we allocate the
//     `eq_k` qubit explicitly up-front via `qbool::ensure_qubit()`
//     when a backend context is active; pure-classical callers (no
//     context) get the value-side semantics only. Reusing one shared
//     `eq_k` across all `k` is safe because we uncompute it before
//     incrementing `k` (PRD §4 paragraph 2).
//   - The `WHEN(eq_k)` scope owns the depth-1 control invariant: the
//     predicate ancilla is the single live control bit while the
//     `b.bit(j).flip()` calls execute, so each flip lifts to a CX.
//   - The `if ((val >> j) & 1u)` branch is *classical* — `a[k]` is
//     fully classical so `val` captures all set bits. Skipping the
//     `flip()` for unset bits is the §4 cheat-sheet's per-`k`
//     payload optimisation.
template <std::size_t W>
inline void lib_qram_read_qrom_dsl(const qint_t<W>* a, std::size_t n,
                                   qint_t<W>& i, qint_t<W>& b) {
    static_assert(W >= 1u, "lib_qram_read_qrom_dsl: W must be >= 1");
    if (n == 0u) return;  // nothing to read

    const std::size_t K = detail_qram_read_dsl::ceil_log2(n);
    assert(K <= W && "lib_qram_read_qrom_dsl: K must be <= W");

    // Single shared predicate ancilla. qbool default-constructs to
    // classical false (qubits[0] = -1, value = 0, super_mask = 0).
    // We promote it to quantum so the predicate helper can drive its
    // qubit; classical-only callers (no backend ctx, no qubits)
    // skip the qubit allocation path inside the helper and rely on
    // the value-side XOR for testing equality.
    qbool eq_k;

#ifdef STURM_BACKEND_ENABLED
    // Promote eq_k to quantum if a backend context is live so the
    // predicate helper's gate side has a qubit to flip; the
    // value-side XOR fires regardless. Setting super_mask = 1 on
    // a freshly allocated qubit puts it in |0> — exactly the
    // predicate's "false" initial state.
    if (sturm_get_thread_context() != nullptr) {
        if (eq_k.qubits[0] < 0) {
            eq_k.qubits[0]  = QubitPool::instance().allocate();
            eq_k.super_mask = 1ULL;
        }
    }
#endif

    for (std::size_t k = 0; k < n; ++k) {
        // Compute predicate eq_k = (i[low-K] == k).
        lib_qram_eq_k_compute<W>(i, k, K, eq_k);

        // WHEN(eq_k) lifts every b.bit(j).flip() to a CX(eq_k, b[j]).
        // The value-side semantics of WHEN: classical false skips
        // the body, classical true runs the body without lifting,
        // superposed installs eq_k as the depth-1 control. After
        // the predicate compute, eq_k tracks the classical equality
        // bit for the current `i.value`, which the WHEN guard reads
        // to decide whether to fire the body. The simulator side
        // sees the lifted CX gates regardless of the value-side
        // choice.
        WHEN(eq_k) {
            const auto val = static_cast<std::uint64_t>(a[k].value);
            for (std::size_t j = 0; j < W; ++j) {
                if ((val >> j) & 1u) {
#ifdef STURM_BACKEND_ENABLED
                    BitProxy bj(b, j);
                    bj.flip();
#else
                    // Frontend path: no qubits, no gates — the
                    // classical value of b just XORs in.
                    b.value ^= (int64_t{1} << j);
#endif
                }
            }
        }

        // Uncompute predicate (same body — predicate is self-inverse).
        lib_qram_eq_k_uncompute<W>(i, k, K, eq_k);
    }

#ifdef STURM_BACKEND_ENABLED
    // Release the eq_k ancilla we allocated above. After the loop the
    // predicate has been uncomputed, so eq_k.qubits[0] is in |0> and
    // it is safe to release back to the pool. qbool's destructor would
    // also release if owning_, but we manage the qubit explicitly to
    // mirror the explicit allocation above.
    if (eq_k.qubits[0] >= 0) {
        QubitPool::instance().release(eq_k.qubits[0]);
        eq_k.qubits[0]  = -1;
        eq_k.super_mask = 0;
    }
#endif
}

}  // namespace sturm
