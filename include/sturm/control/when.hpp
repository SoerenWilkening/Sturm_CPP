#pragma once
// when.hpp — WhenGuard RAII type and WHEN macro (Step 7, spec §6, PRD §11)
//
// Provides:
//   sturm::WhenGuard        — RAII guard that manages current_control TLS
//   sturm::detail::make_when_guard(qbool&)  — factory with static_assert
//   WHEN(expr)              — macro matching PRD §11 spec exactly
//   WhenGuard::active_control() — static accessor for the thread-local control
//
// M24 / principle B5 update (Phase G, sturm-ewto):
//   Nested WHEN AND-fold has been **moved to the sturm-transpile compile-time
//   lowering** (Phase G, matcher_when_nested).  The transpiler rewrites
//     WHEN(outer) { WHEN(inner) { body } }
//   into an explicit AND temporary plus uncompute_and:
//     qbool __stu_ctrl<N> = outer & inner;
//     WHEN(__stu_ctrl<N>) { body }
//     sturm::uncompute_and(__stu_ctrl<N>, outer, inner);
//   so the runtime WhenGuard only ever sees one named control qbool per WHEN.
//   No ancilla allocation, no runtime CCX — the complexity lives in the
//   transpiler where it can be inspected and optimised.
//
//   WhenGuard therefore retains only the control_stack pop/push **swap** in the
//   nested case: if an outer control is already on the stack, pop it and push
//   the inner expr qubit in its place, preserving the depth-1 invariant
//   (principle B5: at most one active control qubit at a time).
//
// Depends on:
//   when_fwd.hpp  — declares thread_local current_control (Step 5)
//   qbool.hpp     — included transitively through when_fwd.hpp

#include "sturm/control/when_fwd.hpp"   // current_control TLS
#include "sturm/control/when_capture.hpp"  // WhenCapture for compound WHEN expressions
// M5: when_fwd.hpp no longer includes qbool.hpp (only forward-declares qbool).
// when.hpp uses qbool members directly, so we include the full definition here.
#include "sturm/qtypes/qbool.hpp"
// Non-backend operator|, operator&, operator~ for qbool (eager, returns qbool).
// Ensures WHEN(c | d) compiles in frontend builds without extra includes.
// (Backend builds get the owning-qbool versions from qbool_ops.hpp — Phase K
// PK-2 retired the lazy AndExpr / OrExpr wrappers.)
#include "sturm/qtypes/qbool_logic.hpp"

#include <type_traits>

// For the control_stack swap we need BackendContext, but only when the
// backend is compiled in.
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/core/context.hpp"
#endif

namespace sturm {

// ── WhenGuard ─────────────────────────────────────────────────────────────────
// RAII guard installed by the WHEN macro.
//
// Behaviour (spec §6 / PRD §11):
//   - Classical false  → run_=false, TLS unchanged.
//   - Classical true   → run_=true,  TLS unchanged (control chain stays nullptr).
//   - Superposed       → run_=true,  expr.ensure_qubit() is called, prev_control_
//                        is saved, current_control is set to &expr.
//
// Nested WHEN (Phase G, sturm-ewto) — control_stack swap:
//   When superposed and an outer control already exists (prev_control_ != nullptr):
//     - Pop the outer control qubit from ctx->control_stack.
//     - Push expr.qubits[0] (the inner control qubit) in its place.
//   The inner expr qubit goes on the stack *directly*; no ancilla, no CCX.  The
//   transpiler has already lowered nested WHEN to an AND temp, so expr IS the
//   combined __stu_ctrl<N> qbool.  Depth-1 invariant preserved.
//   Destructor reverses: pop inner, push outer.
//
// Destructor restores prev_control_ iff the TLS was modified (i.e. is_super was
// true on construction).
struct WhenGuard {
    bool  run_;
    bool  modified_tls_;         // true only when we touched current_control
    qbool* prev_control_;
    int    prev_control_qubit_;

#ifdef STURM_BACKEND_ENABLED
    bool  pushed_to_ctx_stack_;  // true if we modified ctx->control_stack (sturm-d9n)
#endif

    explicit WhenGuard(qbool& expr) noexcept
        : run_(false), modified_tls_(false),
#ifdef STURM_BACKEND_ENABLED
          pushed_to_ctx_stack_(false),
#endif
          prev_control_(nullptr), prev_control_qubit_(-1)
    {
        if (expr.super_mask & 1) {
            // Superposed branch: materialise qubit, set TLS control pointer.
            run_ = true;
            expr.ensure_qubit();
            prev_control_       = detail::current_control;
            prev_control_qubit_ = detail::current_control_qubit;

#ifdef STURM_BACKEND_ENABLED
            // Swap path (sturm-d9n / sturm-ewto): if an outer control exists on
            // the context stack, pop it and push the inner expr qubit in its
            // place.  Otherwise, just push expr.  Either way, the stack reflects
            // exactly one active control qubit (depth invariant).
            if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
                if (prev_control_ != nullptr && prev_control_->qubits[0] >= 0) {
                    ctx->control_stack.pop_control();
                }
                ctx->control_stack.push_control(
                    static_cast<uint32_t>(expr.qubits[0]));
                pushed_to_ctx_stack_ = true;
            }
#endif
            detail::current_control       = &expr;
            detail::current_control_qubit = expr.qubits[0];
            modified_tls_  = true;
        } else if (expr.value & 1) {
            // Classical true branch: body runs, control chain unmodified.
            run_ = true;
        }
        // Classical false: run_ stays false, nothing else happens.
    }

    ~WhenGuard() {
        if (modified_tls_) {
#ifdef STURM_BACKEND_ENABLED
            if (pushed_to_ctx_stack_) {
                // Reverse the swap: pop the inner expr qubit we pushed, and if
                // there was an outer control, push it back.  Depth-1 invariant
                // preserved throughout (sturm-d9n / sturm-ewto).
                if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
                    ctx->control_stack.pop_control();
                    if (prev_control_ != nullptr && prev_control_->qubits[0] >= 0) {
                        ctx->control_stack.push_control(
                            static_cast<uint32_t>(prev_control_->qubits[0]));
                    }
                }
            }
#endif
            detail::current_control       = prev_control_;
            detail::current_control_qubit = prev_control_qubit_;
        }
    }

    // Non-copyable, non-movable — guard lives exactly in its declaration scope.
    WhenGuard(const WhenGuard&)            = delete;
    WhenGuard& operator=(const WhenGuard&) = delete;
    WhenGuard(WhenGuard&&)                 = delete;
    WhenGuard& operator=(WhenGuard&&)      = delete;

    [[nodiscard]] bool should_run() const noexcept { return run_; }

    // ── active_control ────────────────────────────────────────────────────────
    // Returns a pointer to the currently active WHEN control qbool, or nullptr
    // if no WHEN scope is active.  This is the single control qubit presented
    // to ops; principle B5 guarantees exactly one active control at a time
    // (Phase G: combined controls are lowered to a named __stu_ctrl qbool by
    // the transpiler, so active_control() always returns one level of control).
    [[nodiscard]] static qbool* active_control() noexcept {
        return detail::current_control;
    }
};

namespace detail {

// ── materialize_when ─────────────────────────────────────────────────────────
// Overload set that lets the WHEN macro accept both lvalue qbool (by reference,
// zero cost) and rvalue qbool / implicitly-convertible temporaries (by value).
//
// Phase K PK-2: `c | d` and `c & d` now return owning qbool directly in both
// non-backend and backend builds — the lazy OrExpr / AndExpr wrappers are
// gone, and the zero-ancilla optimization lives in the transpiler IR pass
// (Phase J PJ-1).  The qbool&& overload therefore matches directly.

inline qbool& materialize_when(qbool& q) noexcept { return q; }
inline qbool  materialize_when(qbool&& q) noexcept { return std::move(q); }

// make_when_guard — factory that enforces qbool-only usage at compile time.
// Passing a non-qbool triggers the static_assert; compile-time rejection.
// (PRD §11: "WHEN accepts qbool only; passing bool or int is a compile error")
//
// Also deactivates the WhenCapture: by this point the expression has been fully
// evaluated and materialized into _when_val_, so no more intermediates should be
// captured.
template <class T>
WhenGuard make_when_guard(T& expr) {
    static_assert(std::is_same_v<std::decay_t<T>, qbool>,
                  "WHEN(expr): expr must be of type sturm::qbool");
    // Stop capturing intermediates now that expr is fully materialized.
    // WhenCapture::stop_active() restores the previous callback, handling
    // nesting correctly.
    WhenCapture::stop_active();
    return WhenGuard(expr);
}

} // namespace detail
} // namespace sturm

// ── WHEN macro ────────────────────────────────────────────────────────────────
// Expands to three nested `if` statements:
//   1. Outermost if: creates WhenCapture to intercept intermediate temporaries
//      from compound boolean expressions (e.g. (c | d) & e).
//   2. Middle if: materializes expr into _when_val_ via materialize_when().
//      - lvalue qbool -> reference (zero cost, no copy)
//      - rvalue qbool (e.g. c | d, ~c) -> owned local via move
//        (Phase K PK-2: c | d / c & d return owning qbool directly now —
//         the lazy OrExpr / AndExpr wrappers were retired.)
//   3. Innermost if: creates WhenGuard from the (now lvalue) _when_val_.
//
// Destruction order (correct reverse-order uncomputation):
//   1. WhenGuard (innermost)  -- pops control TLS
//   2. _when_val_ (middle)    -- uncomputes the final materialized qbool
//   3. WhenCapture (outermost) -- uncomputes captured intermediates in LIFO order
//
// Usage:
//   qbool flag(0.5);
//   WHEN(flag) { ... }           // lvalue -- no copy, no capture overhead
//   WHEN(c | d) { ... }          // rvalue -- materialized, then guarded
//   WHEN((c | d) & e) { ... }    // compound -- intermediates captured by WhenCapture
//
// NOTE: Nested WHEN scopes are handled at compile time by sturm-transpile's
// Phase G lowering (matcher_when_nested): named-named nested WHENs are rewritten
// to an explicit AND temp plus uncompute_and, so the runtime WhenGuard only ever
// sees one control qubit per WHEN.  Compound nested shapes fall back to the
// control_stack swap path in the guard constructor/destructor.
#define WHEN(expr) \
    if (::sturm::detail::WhenCapture _when_capture_{}; true) \
    if (decltype(auto) _when_val_ = ::sturm::detail::materialize_when(expr); true) \
    if (auto _when_guard_ = ::sturm::detail::make_when_guard(_when_val_); \
        _when_guard_.should_run())
