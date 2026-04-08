#pragma once
// mask_ops.hpp — constexpr pure free functions for mask transfer logic.
// All functions live in sturm::detail.
// No runtime dependencies; fully usable in constant expressions.
// Spec §5, Implementation Plan Step 4.

#include <cstdint>
#include <cstdlib>   // for __builtin_ctzll on GCC/Clang

namespace sturm::detail {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Return (1ULL << width) - 1, or UINT64_MAX for width == 64.
[[nodiscard]] constexpr uint64_t width_mask(int width) noexcept {
    if (width <= 0) return 0ULL;
    if (width >= 64) return ~0ULL;
    return (1ULL << width) - 1ULL;
}

// ---------------------------------------------------------------------------
// mask_bitwise — bitwise-OR of both operand masks.
// Used by: AND, OR, XOR, NOT.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint64_t mask_bitwise(uint64_t a, uint64_t b) noexcept {
    return a | b;
}

// ---------------------------------------------------------------------------
// mask_not — unary; superposition set is unchanged (complement of a
// superposed bit is still superposed in that position).
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint64_t mask_not(uint64_t a) noexcept {
    return a;
}

// ---------------------------------------------------------------------------
// mask_shl — left-shift mask by n positions, clamped to [0, width) bits.
// Bits that shift beyond position (width-1) are discarded.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint64_t mask_shl(uint64_t a, int n, int width) noexcept {
    if (n <= 0) return a & width_mask(width);
    if (n >= width) return 0ULL;
    return (a << n) & width_mask(width);
}

// ---------------------------------------------------------------------------
// mask_shr — right-shift mask by n positions, clamped.
// Bits shifted below position 0 are discarded.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint64_t mask_shr(uint64_t a, int n, int width) noexcept {
    if (n <= 0) return a & width_mask(width);
    if (n >= 64) return 0ULL;
    if (n >= width) return 0ULL;
    return (a >> n) & width_mask(width);
}

// ---------------------------------------------------------------------------
// mask_addsub — addition / subtraction mask propagation.
//
// Combined mask m = a | b.
// Special case: m == 0 → return 0  (both operands classical).
// Otherwise:
//   lowest = index of lowest set bit in m  (__builtin_ctzll)
//   output = (m | (~0ULL << lowest)) & width_mask(width)
//
// Rationale: a carry or borrow ripples upward from the lowest uncertain bit,
// so every bit from that position to the top is potentially affected.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint64_t mask_addsub(uint64_t a, uint64_t b, int width) noexcept {
    const uint64_t m = a | b;
    if (m == 0ULL) return 0ULL;

    // Find lowest set bit index without __builtin_ctzll (not constexpr in
    // older compilers); use a portable bit-loop instead.
    int lowest = 0;
    {
        uint64_t tmp = m;
        while ((tmp & 1ULL) == 0ULL) {
            ++lowest;
            tmp >>= 1;
        }
    }

    const uint64_t carry_fill = (~0ULL << lowest);
    return (m | carry_fill) & width_mask(width);
}

// ---------------------------------------------------------------------------
// mask_muldiv — multiplication / division mask propagation.
//
// If any bit is set in either operand mask, every bit of the result may be
// affected (product / quotient can touch all output bits).  Return all-ones
// for the given width; otherwise 0.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint64_t mask_muldiv(uint64_t a, uint64_t b, int width) noexcept {
    if ((a | b) == 0ULL) return 0ULL;
    return width_mask(width);
}

} // namespace sturm::detail
