// control_stack.hpp — M12: ControlStack class for WHEN control tracking.
//
// Stores the active control qubits pushed by WHEN nesting.
// Stored as a member inside BackendContext; accessed via
// current_control_stack() which forwards to the thread-local context.
//
// Design notes:
//   - Fixed-size array (kMaxDepth=16) — deep nesting is extremely rare.
//   - pop_control() on an empty stack is a no-op guarded by assert.
//   - controls() returns a std::span<const uint32_t> over the live range.
//
// Target: <120 LoC.

#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <span>

namespace sturm {

// ── ControlStack ──────────────────────────────────────────────────────────────

class ControlStack {
public:
    static constexpr uint32_t kMaxDepth = 16u;

    ControlStack() noexcept : depth_(0u) {}

    // Push a control qubit.  Panics (assert) if already at kMaxDepth.
    void push_control(uint32_t qubit) noexcept {
        assert(depth_ < kMaxDepth && "ControlStack overflow: nesting too deep");
        controls_[depth_++] = qubit;
    }

    // Pop the top control qubit.  Panics (assert) if stack is already empty.
    void pop_control() noexcept {
        assert(depth_ > 0u && "ControlStack underflow: pop on empty stack");
        --depth_;
    }

    // Return the current nesting depth (number of active controls).
    [[nodiscard]] uint32_t depth() const noexcept { return depth_; }

    // Return the top (most-recently-pushed) control qubit.
    // Panics (assert) if the stack is empty.
    [[nodiscard]] uint32_t top() const noexcept {
        assert(depth_ > 0u && "ControlStack::top() called on empty stack");
        return controls_[depth_ - 1u];
    }

    // Return a span over all active control qubits (bottom to top order).
    [[nodiscard]] std::span<const uint32_t> controls() const noexcept {
        return {controls_.data(), depth_};
    }

private:
    std::array<uint32_t, kMaxDepth> controls_{};
    uint32_t                        depth_{0u};
};

// ── Thread-local accessor ─────────────────────────────────────────────────────
//
// Returns a reference to the ControlStack belonging to the active thread
// context (as returned by sturm_get_thread_context()).
//
// Implemented in src/sturm/core/control_stack.cpp.
// Requires that a context has been installed on the calling thread.

ControlStack& current_control_stack();

} // namespace sturm
