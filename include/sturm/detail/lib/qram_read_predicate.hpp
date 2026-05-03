// qram_read_predicate.hpp — sturm-2w6h.3 (Beat B2a).
//
// Predicate compute/uncompute helper for the QROM body of the QRAM backend
// gate-emission epic. Plan reference: docs/plan_qram_backend.md §5 Beat B2a.
//
//   template <std::size_t W>
//   inline void lib_qram_eq_k_compute(qint_t<W>& i, std::size_t k,
//                                     std::size_t K, qbool& eq_k);
//
//   template <std::size_t W>
//   inline void lib_qram_eq_k_uncompute(qint_t<W>& i, std::size_t k,
//                                       std::size_t K, qbool& eq_k);
//
// Compute body:
//   1. Classically XOR `eq_k.value` with the equality predicate
//      `((i.value & lowK) == (k & lowK))`. Mirrors the pattern in
//      `qint_compare_v3.hpp::make_dsl_compare_result` — the classical `value`
//      field tracks the predicate independently of the simulator state.
//   2. For every j in [0, K): if bit j of `k` is 0, X-flip bit j of `i`'s
//      qubit. After this re-encoding, the lifted bits of `i` all read |1>
//      exactly when the original `i` value matches `k` on the active K
//      bits.
//   3. AND-reduce the K active bits onto `eq_k`'s qubit via
//      `lib_c_n_AND_dsl`. The CCX chain emits only `quantum_xor` /
//      `quantum_and` records on the active sink (per plan §3.2).
//   4. Undo the X-flips by repeating step 2's flip pattern. Every flip is
//      paired so `i.qubits` and `i.value` are unchanged at scope exit.
//
// Uncompute body: the predicate is self-inverse (X-flip pairs cancel and
// `lib_c_n_AND_dsl` is its own adjoint — the body is a CCX/Toffoli sandwich,
// each gate of which is self-inverse). So the uncompute is the same body as
// the compute. We provide a separate named entry-point so that the QROM body
// (B2 / sturm-2w6h.4) and its adjoint (B3 / sturm-2w6h.5) call by name —
// supports placement-audit tooling per the P9c spirit.
//
// Caller invariants:
//   - `K <= W`. `K == 0` is treated as the vacuous predicate `eq_k ^= 1`
//     (every i matches the empty restriction). `k` may have bits set beyond
//     the K-th position; only the low K bits are inspected.
//   - For the gate-emission path (`STURM_BACKEND_ENABLED` + an active
//     `BackendContext`), `eq_k.qubits[0]` and `i.qubits[j]` for j in
//     [0, K) must be allocated (>= 0) before calling. Pure classical
//     callers (no qubits) get the value-side semantics only — no gates
//     are emitted, the helper still XORs the equality bit into
//     `eq_k.value`.
//   - The helper does NOT modify `i.value` or `i.super_mask` net — every
//     `flip()` is paired — but `i`'s qubits may transiently hold the
//     re-encoded value during the AND reduction.
//
// Reuses `lib_c_n_AND_dsl` from `sturm/detail/lib/c_and_dsl.hpp`. Depends on
// B0 / B1 only via header surface (no telemetry hooks fire from this helper).
//
// Threading: header-only inline templates; no thread-locals introduced.
// LoC budget: <= 200 (plan §1, §5 / B2a).

#pragma once

#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/detail/lib/c_and_dsl.hpp"

#ifdef STURM_BACKEND_ENABLED
#  include "sturm/qtypes/qbool_ops.hpp"
#  include "sturm/detail/qtypes/bit_proxy.hpp"
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace sturm {

// ── detail: shared compute body ──────────────────────────────────────────────
// Compute and uncompute share the same body — the predicate is self-inverse.
// Factored into a named helper so the public entry-points are tiny and the
// "compute == uncompute" identity is obvious from a single read.
namespace detail_qram_pred {

// Classical-mask helper: low K bits of an unsigned value.
inline std::uint64_t low_k_mask(std::size_t K) noexcept {
    if (K == 0u) return 0u;
    if (K >= 64u) return ~std::uint64_t{0};
    return (std::uint64_t{1} << K) - 1u;
}

template <std::size_t W>
inline void eq_k_body(qint_t<W>& i, std::size_t k, std::size_t K,
                      qbool& eq_k) {
    static_assert(W >= 1u, "lib_qram_eq_k: W must be >= 1");
    assert(K <= W && "lib_qram_eq_k: K must be <= W");

    // ── Step 1: classical-value side. XOR the equality bit into eq_k.value
    // unconditionally so callers that never allocate qubits still observe
    // the right predicate; mirrors the precomputed `classical_val` in
    // `qint_compare_v3.hpp::make_dsl_compare_result`.
    const std::uint64_t mask = low_k_mask(K);
    const std::uint64_t iv = static_cast<std::uint64_t>(i.value) & mask;
    const std::uint64_t kv = static_cast<std::uint64_t>(k) & mask;
    const int eq_bit = (iv == kv) ? 1 : 0;
    eq_k.value ^= static_cast<int64_t>(eq_bit);

#ifdef STURM_BACKEND_ENABLED
    // ── Step 2 (gate side): bail out unless we have an active backend
    // context AND the qubits the helper needs. Pure classical callers
    // (no context, or no allocated qubits) only see the value-side
    // mutation done above.
    if (sturm_get_thread_context() == nullptr) return;
    if (eq_k.qubits[0] < 0) return;

    // K == 0: vacuous predicate. Every i matches the empty restriction so
    // eq_k ^= 1 unconditionally. Reuse lib_c_n_AND_dsl with n_controls=0
    // which applies a bare X to the target — keeps the gate stream shape
    // consistent with the K >= 1 path (only quantum_xor / quantum_and
    // primitives ever hit the sink from this helper).
    if (K == 0u) {
        BitProxy tgt(eq_k);
        lib_c_n_AND_dsl<BitProxy>(/*controls=*/nullptr, /*n=*/0u, tgt);
        return;
    }

    // Every active bit of i must have a qubit (caller's responsibility).
    for (std::size_t j = 0; j < K; ++j) {
        if (i.qubits[j] < 0) return;
    }

    // ── Step 2a: re-encode `i` — X-flip every bit j whose bit-j of k is 0.
    // After this, the K active bits of i all read |1> iff i matches k on
    // those bits. Each flip() lifts to a single-qubit X (or CX under an
    // outer WHEN-control if the caller is inside a WHEN scope; the
    // predicate helper itself never installs an outer control).
    for (std::size_t j = 0; j < K; ++j) {
        if (((k >> j) & 1u) == 0u) {
            BitProxy bj(i, j);
            bj.flip();
        }
    }

    // ── Step 2b: AND-reduce the K active bits onto eq_k's qubit.
    // Build a stack-array of BitProxy views into the K active bits of i
    // and hand them to lib_c_n_AND_dsl. This is the only emission of the
    // quantum_and chain in the helper — counted in the gate-set tests.
    static constexpr std::size_t kMaxK = 64u;
    assert(K <= kMaxK && "lib_qram_eq_k: K must be <= kMaxK");
    BitProxy controls[kMaxK];
    for (std::size_t j = 0; j < K; ++j) {
        controls[j] = BitProxy(i, j);
    }
    BitProxy tgt(eq_k);
    lib_c_n_AND_dsl<BitProxy>(controls, K, tgt);

    // ── Step 2c: undo the X-flips (every flip in step 2a is paired).
    // After this loop, i.qubits and i.value are exactly as on entry.
    for (std::size_t j = 0; j < K; ++j) {
        if (((k >> j) & 1u) == 0u) {
            BitProxy bj(i, j);
            bj.flip();
        }
    }
#endif
}

} // namespace detail_qram_pred

// ── lib_qram_eq_k_compute ────────────────────────────────────────────────────
// Forward predicate: eq_k ^= (i[0..K-1] == k[0..K-1]).
//
// On exit: `i`'s qubits and value are unchanged (every X-flip is paired);
// `eq_k.value` is XORed with the classical equality predicate; if the active
// qubits exist the simulator state is updated correspondingly via the X +
// c_n_AND + X sandwich.
template <std::size_t W>
inline void lib_qram_eq_k_compute(qint_t<W>& i, std::size_t k,
                                  std::size_t K, qbool& eq_k) {
    detail_qram_pred::eq_k_body<W>(i, k, K, eq_k);
}

// ── lib_qram_eq_k_uncompute ──────────────────────────────────────────────────
// Reverse predicate: same body — the predicate is self-inverse. Re-XORs
// `eq_k` with the same classical equality bit, returning it to its
// pre-compute value. Provided as a named entry-point so the QROM body and
// its adjoint both call by name (placement-audit tooling, P9c spirit).
template <std::size_t W>
inline void lib_qram_eq_k_uncompute(qint_t<W>& i, std::size_t k,
                                    std::size_t K, qbool& eq_k) {
    detail_qram_pred::eq_k_body<W>(i, k, K, eq_k);
}

} // namespace sturm
