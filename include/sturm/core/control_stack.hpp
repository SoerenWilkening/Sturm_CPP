// control_stack.hpp — M12 + sturm-a3t4: ControlStack class for WHEN control
// tracking, enforcing the depth-1 invariant at runtime.
//
// Stores the active control qubit pushed by the enclosing WHEN scope.
// Stored as a member inside BackendContext; accessed via
// current_control_stack() which forwards to the thread-local context.
//
// Design notes:
//   - Depth-1 invariant (principle B5, sturm-a3t4): at most one control
//     qubit is ever live. push_control() asserts depth_ == 0; nested WHEN
//     compositions go through the WhenGuard pop/push swap (see when.hpp)
//     after the transpiler has folded the outer & inner expression into
//     a single named control qbool.
//   - Fixed-size array (kMaxDepth=16) — defence-in-depth, not a working
//     budget. Reaching depth >= 2 indicates a programmer error (a missed
//     conversion to outer & flag + WHEN), not a legitimate use case.
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
    // kMaxDepth is defence-in-depth, not a working budget. The depth-1
    // invariant (asserted in push_control) bounds legitimate use to {0,1};
    // the larger array exists only so an accidental violation degrades to a
    // well-formed assert rather than memory corruption.
    static constexpr uint32_t kMaxDepth = 16u;

    ControlStack() noexcept : depth_(0u) {}

    // Push a control qubit.  Enforces the depth-1 invariant (sturm-a3t4):
    // pushing while a control is already live is a programmer error — the
    // library must compose nested controls via outer & flag + WHEN, never
    // by raw stacking. Both asserts are load-bearing: the first guards the
    // invariant, the second the array bound.
    void push_control(uint32_t qubit) noexcept {
        assert(depth_ == 0u && "ControlStack: depth-1 invariant — lift via outer & flag + WHEN");
        assert(depth_ < kMaxDepth && "ControlStack overflow");
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
