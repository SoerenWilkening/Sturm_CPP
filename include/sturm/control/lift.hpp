// lift.hpp -- sturm-k8f2: shared depth-1 lift helper for DSL primitives.
//
// Factors the duplicated `if (auto* outer = WhenGuard::active_control()) {
// qbool tmp = (*outer) & flag; WHEN(tmp) { body(); } uncompute_and(tmp,
// *outer, flag); } else { WHEN(flag) { body(); } }` idiom out of every
// `*_dsl.hpp` header into a single template free function `sturm::lift_under`.
//
// The idiom was introduced under the sturm-a3t4 epic (depth-1 nested-WHEN
// lowering) and ended up duplicated across:
//   - add_mod_dsl.hpp(+_adj)
//   - mul_mod_dsl.hpp(+_adj)
//   - pow_mod_dsl.hpp(+_adj)
//   - mul_dsl.hpp
//   - div_dsl.hpp
// pushing mul_dsl.hpp / div_dsl.hpp over their LoC budget (test_e2e_loc_budget).
// This header collapses each duplicated copy into a single call.
//
// Two overloads are provided:
//   - `lift_under(qbool& flag, F&& body)` -- for callers that already hold
//     a `qbool` flag (e.g. div_dsl.hpp's `overflow_own`/`sgn_own[i]`,
//     add_mod_dsl.hpp's `lt_flag_own`).  super_mask=1 is forced internally
//     so callers don't need to repeat the fix-up; the original 1-arg
//     `qbool::make_non_owning(idx)` factory leaves super_mask=0, which
//     would short-circuit `WHEN`/`operator&` as classical-false.
//   - `lift_under(BitProxy flag, F&& body)` -- for callers whose flag is
//     a `BitProxy` (e.g. mul_mod_dsl.hpp's `b_bits[i]`, pow_mod_dsl.hpp's
//     `exp_bits[i]`, mul_dsl.hpp's `b_bits[i]`).  Promotes the proxy to a
//     quantum qubit if necessary, then builds the same super_mask=1 view.
//
// `uncompute_and` is forward-declared rather than pulled via uncompute_api.hpp
// to avoid an include cycle: the full uncompute header transitively re-includes
// some DSL headers (via qint_arith_v3.hpp), and pragma-once would skip the
// recursive expansion before `sturm::uncompute_and` is parsed at the call site.
// The forward declaration is sufficient because all callers of `lift_under`
// already have `qbool` complete (this header includes qbool.hpp).

#pragma once

#include "sturm/control/when.hpp"            // WHEN, WhenGuard::active_control
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"        // operator& on qbool

#include <type_traits>
#include <utility>

// Forward-declare uncompute_and to avoid the include cycle described above.
namespace sturm {
void uncompute_and(qbool& r, const qbool& a, const qbool& b);
#ifdef STURM_BACKEND_ENABLED
struct BitProxy;
#endif
}  // namespace sturm

namespace sturm {

// ── lift_under(qbool&, body) ─────────────────────────────────────────────────
// Run `body()` controlled on `flag` under the depth-1 lift idiom: when an
// outer WHEN is active, AND-fold (outer & flag) into a fresh ancilla so the
// body runs under exactly one control; otherwise drop straight into
// WHEN(flag).  super_mask=1 is forced on the qbool view so WhenGuard takes
// the superposed branch even when the owning qbool was constructed via the
// 1-arg `make_non_owning(idx)` factory (which leaves super_mask=0).
template <typename Body>
inline void lift_under(qbool& flag, Body&& body) {
    qbool flag_q = qbool::make_non_owning(flag.qubits[0],
                                          flag.value, /*mask=*/1ULL);
    if (qbool* outer = WhenGuard::active_control()) {
        qbool tmp = (*outer) & flag_q;
        WHEN(tmp) { std::forward<Body>(body)(); }
        sturm::uncompute_and(tmp, *outer, flag_q);
    } else {
        WHEN(flag_q) { std::forward<Body>(body)(); }
    }
}

// ── lift_under(BitProxy, body) ───────────────────────────────────────────────
// BitProxy overload (backend-only): promote the proxy to a quantum qubit
// (no-op if already promoted) and build a non-owning qbool view with
// super_mask=1, then dispatch through the qbool overload's body.
//
// Templated on Bit so non-backend builds (where BitProxy is undefined) can
// still instantiate `lift_under(qbool&, ...)` callers without trying to
// resolve the BitProxy overload — and so DSL headers that hand in a Bit
// template parameter (qbool or BitProxy) can call a single name.
template <typename Bit, typename Body,
          std::enable_if_t<!std::is_same_v<std::decay_t<Bit>, qbool>, int> = 0>
inline void lift_under(Bit& flag, Body&& body) {
    flag.ensure_quantum();
    qbool flag_q = qbool::make_non_owning(flag.qubit_index(),
                                          /*val=*/0,
                                          /*mask=*/1ULL);
    if (qbool* outer = WhenGuard::active_control()) {
        qbool tmp = (*outer) & flag_q;
        WHEN(tmp) { std::forward<Body>(body)(); }
        sturm::uncompute_and(tmp, *outer, flag_q);
    } else {
        WHEN(flag_q) { std::forward<Body>(body)(); }
    }
}

}  // namespace sturm
