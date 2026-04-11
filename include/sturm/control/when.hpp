#pragma once
// when.hpp — WhenGuard RAII type and WHEN macro (Step 7, spec §6, PRD §11)
//
// Provides:
//   sturm::WhenGuard        — RAII guard that manages current_control TLS
//   sturm::detail::make_when_guard(qbool&)  — factory with static_assert
//   WHEN(expr)              — macro matching PRD §11 spec exactly
//   WhenGuard::active_control() — static accessor for the thread-local control
//
// M24 extension:
//   Nested WHEN AND-fold (principle B5): when a second WHEN is entered while
//   an outer control is already active and the inner expression is superposed,
//   the constructor allocates an ancilla qubit and emits CCX(outer, inner, anc)
//   to compute anc = outer AND inner.  current_control is then set to the
//   ancilla so all ops inside see exactly one control qubit.  The destructor
//   uncomputes the ancilla via a second CCX and releases the qubit.
//
//   AND-fold is only performed when STURM_BACKEND_ENABLED is defined and a
//   BackendContext is active; without a context the old save/restore behaviour
//   is retained so the frontend tests continue to pass.
//
// Depends on:
//   when_fwd.hpp  — declares thread_local current_control (Step 5)
//   qbool.hpp     — included transitively through when_fwd.hpp

#include "sturm/control/when_fwd.hpp"   // current_control TLS
// M5: when_fwd.hpp no longer includes qbool.hpp (only forward-declares qbool).
// when.hpp uses qbool members directly, so we include the full definition here.
#include "sturm/qtypes/qbool.hpp"

#include <type_traits>

// For M24 AND-fold we need execute_gate and BackendContext, but only when the
// backend is compiled in.
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/core/context.hpp"
#  include "sturm/core/gate_kind.h"
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
// M24 — Nested AND-fold (principle B5, STURM_BACKEND_ENABLED builds only):
//   When superposed and an outer control already exists (prev_control_ != nullptr):
//     1. Allocate ancilla qubit from QubitPool.
//     2. Emit CCX(outer_qubit, inner_qubit, ancilla_qubit) via execute_gate.
//     3. Set ancilla_.is_super = true and ancilla_.qubits[0] = ancilla_qubit.
//     4. Set current_control = &ancilla_.
//   Destructor:
//     - Emits CCX again (uncompute) and releases ancilla qubit.
//     - Restores current_control = prev_control_.
//
// Destructor restores prev_control_ iff the TLS was modified (i.e. is_super was
// true on construction).
struct WhenGuard {
    bool  run_;
    bool  modified_tls_;         // true only when we touched current_control
    bool  and_folded_;           // true if an ancilla was computed (M24 nested AND-fold)
    qbool* prev_control_;
    int    prev_control_qubit_;

#ifdef STURM_BACKEND_ENABLED
    bool  pushed_to_ctx_stack_;  // true if we pushed to ctx->control_stack (sturm-d9n)
    // Ancilla qbool used for the AND-folded nested control (M24).
    // Constructed inline; qubit is allocated manually to avoid the prepare() call
    // that the probabilistic qbool(double) constructor would emit.
    qbool ancilla_;
#endif

    explicit WhenGuard(qbool& expr) noexcept
        : run_(false), modified_tls_(false), and_folded_(false),
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
            // M24 — AND-fold: if an outer control already exists, compute ancilla.
            if (prev_control_ != nullptr && prev_control_->qubits[0] >= 0) {
                // Allocate ancilla qubit (initialised to |0⟩ by convention).
                int anc_idx = QubitPool::instance().allocate();
                // Set up ancilla_ struct (super_mask=1, qubit allocated, value=0).
                ancilla_.super_mask = 1ULL;
                ancilla_.value      = 0;
                ancilla_.qubits[0] = anc_idx;

                // Save inner expr qubit index for uncompute in destructor.
                inner_expr_qubit_ = expr.qubits[0];

                // Emit CCX(outer_ctrl, inner_expr, ancilla) to compute AND.
                if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
                    uint32_t qs[3] = {
                        static_cast<uint32_t>(prev_control_->qubits[0]),
                        static_cast<uint32_t>(expr.qubits[0]),
                        static_cast<uint32_t>(anc_idx)
                    };
                    execute_gate(*ctx, STURM_GATE_CCX, qs, 3u, 0.0);

                    // AND-fold: pop the outer control from the stack and push
                    // the ancilla so the stack always reflects exactly one active
                    // control qubit (depth invariant: stays at 1).  sturm-d9n.
                    ctx->control_stack.pop_control();
                    ctx->control_stack.push_control(static_cast<uint32_t>(anc_idx));
                    pushed_to_ctx_stack_ = true;
                }

                detail::current_control       = &ancilla_;
                detail::current_control_qubit = ancilla_.qubits[0];
                and_folded_  = true;
            } else {
                // Non-fold case: push the expr qubit onto the context control
                // stack so downstream ops can see the active control.  sturm-d9n.
                if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
                    ctx->control_stack.push_control(
                        static_cast<uint32_t>(expr.qubits[0]));
                    pushed_to_ctx_stack_ = true;
                }
                detail::current_control       = &expr;
                detail::current_control_qubit = expr.qubits[0];
            }
#else
            detail::current_control       = &expr;
            detail::current_control_qubit = expr.qubits[0];
#endif
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
            if (and_folded_) {
                // Uncompute ancilla via CCX (self-inverse).
                if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
                    // Reconstruct the outer and inner qubit indices.
                    // prev_control_ points to the outer qbool; ancilla_'s qubit is
                    // ancilla_.qubits[0].
                    // The inner expr qubit index was stored in current_control
                    // (which is &ancilla_) — we need the original inner expr qubit.
                    // However, we no longer have a direct reference to inner expr.
                    // We stored the ancilla as current_control at construction time.
                    //
                    // To emit the uncompute CCX we need:
                    //   outer_qubit   = prev_control_->qubits[0]  (still valid, outer expr lives longer)
                    //   inner_qubit   = inner_expr_qubit_          (saved at construction)
                    //   ancilla_qubit = ancilla_.qubits[0]
                    uint32_t qs[3] = {
                        static_cast<uint32_t>(prev_control_->qubits[0]),
                        static_cast<uint32_t>(inner_expr_qubit_),
                        static_cast<uint32_t>(ancilla_.qubits[0])
                    };
                    execute_gate(*ctx, STURM_GATE_CCX, qs, 3u, 0.0);

                    // sturm-d9n: Reverse AND-fold control_stack swap.
                    // Pop the ancilla qubit that was pushed during AND-fold,
                    // then push back the outer control to restore the outer WHEN's
                    // stack state (depth stays at 1 throughout).
                    if (pushed_to_ctx_stack_) {
                        ctx->control_stack.pop_control();
                        ctx->control_stack.push_control(
                            static_cast<uint32_t>(prev_control_->qubits[0]));
                    }
                }
                // Release ancilla qubit back to pool.
                if (ancilla_.qubits[0] >= 0) {
                    QubitPool::instance().release(ancilla_.qubits[0]);
                    ancilla_.qubits[0] = -1;
                }
            } else if (pushed_to_ctx_stack_) {
                // Non-fold case: pop the expr qubit we pushed on entry.
                // sturm-d9n.
                if (sturm_backend_context_t* ctx = sturm_get_thread_context()) {
                    ctx->control_stack.pop_control();
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

    // ── active_control (M24) ──────────────────────────────────────────────────
    // Returns a pointer to the currently active WHEN control qbool, or nullptr
    // if no WHEN scope is active.  This is the single control qubit presented to
    // ops: inside a nested WHEN it points to the AND-fold ancilla (principle B5).
    [[nodiscard]] static qbool* active_control() noexcept {
        return detail::current_control;
    }

private:
#ifdef STURM_BACKEND_ENABLED
    // Physical qubit index of the inner expression, saved so the destructor can
    // emit the uncompute CCX without a reference to the inner expr's qbool.
    int inner_expr_qubit_{-1};
#endif
};

namespace detail {

// make_when_guard — factory that enforces qbool-only usage at compile time.
// Passing a non-qbool triggers the static_assert; compile-time rejection.
// (PRD §11: "WHEN accepts qbool only; passing bool or int is a compile error")
template <class T>
WhenGuard make_when_guard(T& expr) {
    static_assert(std::is_same_v<std::decay_t<T>, qbool>,
                  "WHEN(expr): expr must be of type sturm::qbool");
    return WhenGuard(expr);
}

} // namespace detail
} // namespace sturm

// ── WHEN macro ────────────────────────────────────────────────────────────────
// Expands to an `if` statement whose body executes iff the guard's
// should_run() returns true.  The guard's lifetime is the duration of the
// `if` scope (C++ init-statement in if).
//
// Usage:
//   qbool flag(0.5);
//   WHEN(flag) {
//       // body — runs when flag is classically true or superposed
//   }
//
// NOTE: Nested WHEN scopes: when STURM_BACKEND_ENABLED is active and a
// BackendContext is installed, nested WHEN AND-folds the two controls into a
// single ancilla qubit (principle B5).  Without a context, TLS is saved and
// restored as before.
#define WHEN(expr) \
    if (auto _when_guard_ = ::sturm::detail::make_when_guard(expr); \
        _when_guard_.should_run())
