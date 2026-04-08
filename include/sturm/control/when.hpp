#pragma once
// when.hpp — WhenGuard RAII type and WHEN macro (Step 7, spec §6, PRD §11)
//
// Provides:
//   sturm::WhenGuard        — RAII guard that manages current_control TLS
//   sturm::detail::make_when_guard(qbool&)  — factory with static_assert
//   WHEN(expr)              — macro matching PRD §11 spec exactly
//
// Depends on:
//   when_fwd.hpp  — declares thread_local current_control (Step 5)
//   qbool.hpp     — included transitively through when_fwd.hpp

#include "sturm/control/when_fwd.hpp"   // current_control TLS
// qbool.hpp is transitively included via when_fwd.hpp

#include <type_traits>

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
// Destructor restores prev_control_ iff the TLS was modified (i.e. is_super was
// true on construction).
struct WhenGuard {
    bool  run_;
    bool  modified_tls_;   // true only when we touched current_control
    qbool* prev_control_;

    explicit WhenGuard(qbool& expr) noexcept
        : run_(false), modified_tls_(false), prev_control_(nullptr)
    {
        if (expr.is_super) {
            // Superposed branch: materialise qubit, set TLS control pointer.
            run_ = true;
            expr.ensure_qubit();
            prev_control_  = detail::current_control;
            detail::current_control = &expr;
            modified_tls_  = true;
        } else if (expr.value) {
            // Classical true branch: body runs, control chain unmodified.
            run_ = true;
        }
        // Classical false: run_ stays false, nothing else happens.
    }

    ~WhenGuard() {
        if (modified_tls_) {
            detail::current_control = prev_control_;
        }
    }

    // Non-copyable, non-movable — guard lives exactly in its declaration scope.
    WhenGuard(const WhenGuard&)            = delete;
    WhenGuard& operator=(const WhenGuard&) = delete;
    WhenGuard(WhenGuard&&)                 = delete;
    WhenGuard& operator=(WhenGuard&&)      = delete;

    [[nodiscard]] bool should_run() const noexcept { return run_; }
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
// NOTE: Nested WHEN scopes are permitted (TLS is correctly saved/restored per
// level) but AND-fold / uncomputation is deferred to the backend stage.
// TODO(backend): AND-fold nested controls into a single ancilla-managed control.
#define WHEN(expr) \
    if (auto _when_guard_ = ::sturm::detail::make_when_guard(expr); \
        _when_guard_.should_run())
