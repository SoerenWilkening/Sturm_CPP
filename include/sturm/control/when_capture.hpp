#pragma once
// when_capture.hpp -- RAII capture layer for WHEN compound boolean expressions.
//
// When WHEN((c | d) & e) is evaluated, the intermediates (e.g. t1 = c | d) are
// C++ temporaries that would normally be destroyed at the end of the
// full-expression -- before the WHEN body runs.  WhenCapture intercepts their
// destructors via the when_capture_defer_fn callback (installed in
// when_capture_fwd.hpp), deferring uncompute + qubit release until the
// outermost scope of the WHEN macro unwinds.  This guarantees correct
// reverse-order uncomputation.
//
// Supports both width-1 qbool intermediates AND multi-bit qint intermediates
// (e.g. from `(a + b) == 0 & c` where `a + b` is a multi-bit temporary).
//
// Destruction order with this fix:
//   1. WhenGuard (innermost if)  -- pops control TLS
//   2. _when_val_ (middle if)    -- uncomputes the final materialized qbool
//   3. WhenCapture (outermost if) -- uncomputes captured intermediates in LIFO order

#include "sturm/control/when_capture_fwd.hpp"

#ifdef STURM_BACKEND_ENABLED
#  include "sturm/uncompute/uncompute_op.hpp"
#  include "sturm/uncompute/qint_base.hpp"
#  include "sturm/core/context.hpp"
#  include "sturm/core/qubit_pool.hpp"
#  include <vector>
#endif

namespace sturm::detail {

#ifdef STURM_BACKEND_ENABLED

struct CapturedIntermediate {
    uncompute_op op;
    CapturedRegister reg;
};

struct WhenCapture {
    std::vector<CapturedIntermediate> captured;
    when_capture_fn_t prev_fn;   // saved callback for nested WHEN

    WhenCapture() noexcept : prev_fn(when_capture_defer_fn) {
        active_instance_ = this;
        when_capture_defer_fn = &WhenCapture::capture_trampoline;
    }

    // Called by make_when_guard (via stop_active) to stop capturing after expr
    // is materialized.  Restores the previous callback state.
    void stop_capturing() noexcept {
        when_capture_defer_fn = prev_fn;
        if (active_instance_ == this) {
            active_instance_ = nullptr;
        }
    }

    ~WhenCapture() {
        // Safety net: stop capturing if still active (e.g. classical-false path
        // where make_when_guard never fired because should_run() was false).
        if (when_capture_defer_fn == &WhenCapture::capture_trampoline
            && active_instance_ == this) {
            stop_capturing();
        }
        // Uncompute + release in reverse order (LIFO).
        sturm_backend_context_t* ctx = sturm_get_thread_context();
        for (auto it = captured.rbegin(); it != captured.rend(); ++it) {
            if (ctx && it->op.tag != uncompute_op::kind::NONE) {
                // Reconstruct qint_base view from the captured register data.
                qint_base view;
                view.width      = it->reg.width;
                view.value      = it->reg.value;
                view.super_mask = it->reg.super_mask;
                for (uint8_t j = 0; j < it->reg.width && j < QINT_BASE_MAX_WIDTH; ++j) {
                    view.qubits[j] = (it->reg.qubits[j] >= 0)
                                     ? static_cast<uint32_t>(it->reg.qubits[j]) : 0u;
                }
                it->op.apply(*ctx, view);
            }
            // Release ALL qubits for this intermediate.
            for (uint8_t j = 0; j < it->reg.width; ++j) {
                if (it->reg.qubits[j] >= 0) {
                    QubitPool::instance().release(it->reg.qubits[j]);
                }
            }
        }
    }

    void capture(uncompute_op op, const CapturedRegister& reg) {
        captured.push_back({std::move(op), reg});
    }

    // Static method called from make_when_guard to stop the active capture.
    // Safe to call even if no capture is active (no-op).
    static void stop_active() noexcept {
        if (active_instance_) {
            active_instance_->stop_capturing();
        }
    }

    // Non-copyable, non-movable.
    WhenCapture(const WhenCapture&)            = delete;
    WhenCapture& operator=(const WhenCapture&) = delete;
    WhenCapture(WhenCapture&&)                 = delete;
    WhenCapture& operator=(WhenCapture&&)      = delete;

private:
    // Thread-local pointer to the currently active WhenCapture instance.
    static inline thread_local WhenCapture* active_instance_ = nullptr;

    // Static trampoline installed as when_capture_defer_fn.
    static void capture_trampoline(uncompute_op op, const CapturedRegister& reg) {
        if (active_instance_) {
            active_instance_->capture(std::move(op), reg);
        }
    }
};

#else  // !STURM_BACKEND_ENABLED

// No-op placeholder for frontend builds.
struct WhenCapture {
    WhenCapture() noexcept = default;
    static void stop_active() noexcept {}
};

#endif // STURM_BACKEND_ENABLED

} // namespace sturm::detail
