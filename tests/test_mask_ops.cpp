// test_mask_ops.cpp — table-driven tests for sturm::detail mask transfer functions
// Step 4 (TDD): written before mask_ops.hpp exists.

#include <sturm/core/mask_ops.hpp>
#include <cassert>
#include <cstdint>
#include <cstdio>

using sturm::detail::mask_bitwise;
using sturm::detail::mask_not;
using sturm::detail::mask_shl;
using sturm::detail::mask_shr;
using sturm::detail::mask_addsub;
using sturm::detail::mask_muldiv;

// ---------------------------------------------------------------------------
// mask_bitwise  (a | b)
// ---------------------------------------------------------------------------
static void test_mask_bitwise() {
    // Both zero
    assert(mask_bitwise(0ULL, 0ULL) == 0ULL);
    // One side zero
    assert(mask_bitwise(0b1010ULL, 0ULL) == 0b1010ULL);
    assert(mask_bitwise(0ULL, 0b0101ULL) == 0b0101ULL);
    // Overlap
    assert(mask_bitwise(0b1100ULL, 0b0011ULL) == 0b1111ULL);
    assert(mask_bitwise(0b1111ULL, 0b1111ULL) == 0b1111ULL);
    // High bits
    assert(mask_bitwise(UINT64_MAX, 0ULL) == UINT64_MAX);
    assert(mask_bitwise(0ULL, UINT64_MAX) == UINT64_MAX);
}

// ---------------------------------------------------------------------------
// mask_not  (identity: returns a unchanged)
// ---------------------------------------------------------------------------
static void test_mask_not() {
    assert(mask_not(0ULL) == 0ULL);
    assert(mask_not(1ULL) == 1ULL);
    assert(mask_not(0xFF00ULL) == 0xFF00ULL);
    assert(mask_not(UINT64_MAX) == UINT64_MAX);
}

// ---------------------------------------------------------------------------
// mask_shl  (left-shift, clamped to width)
// ---------------------------------------------------------------------------
static void test_mask_shl() {
    // Basic shift
    assert(mask_shl(0b0001ULL, 1, 4) == 0b0010ULL);
    assert(mask_shl(0b0001ULL, 3, 4) == 0b1000ULL);
    // Shift that would exceed width — result is clamped (high bits stripped)
    assert(mask_shl(0b0001ULL, 4, 4) == 0ULL);   // shifted out entirely
    assert(mask_shl(0b0011ULL, 3, 4) == 0b1000ULL); // lower bit stays, upper shifts out
    // Zero in → zero out
    assert(mask_shl(0ULL, 5, 8) == 0ULL);
    // Shift by 0
    assert(mask_shl(0b1010ULL, 0, 8) == 0b1010ULL);
    // Width 64: no clamping truncation
    assert(mask_shl(1ULL, 63, 64) == (1ULL << 63));
    // Shift ≥ width → 0
    assert(mask_shl(0xFFULL, 8, 8) == 0ULL);
}

// ---------------------------------------------------------------------------
// mask_shr  (right-shift, clamped to width)
// ---------------------------------------------------------------------------
static void test_mask_shr() {
    assert(mask_shr(0b1000ULL, 1, 4) == 0b0100ULL);
    assert(mask_shr(0b1000ULL, 3, 4) == 0b0001ULL);
    // Shift out entirely
    assert(mask_shr(0b0001ULL, 1, 4) == 0ULL);
    // Zero
    assert(mask_shr(0ULL, 3, 8) == 0ULL);
    // Shift by 0
    assert(mask_shr(0b1010ULL, 0, 8) == 0b1010ULL);
    // Shift ≥ width → 0
    assert(mask_shr(0xFFULL, 8, 8) == 0ULL);
    assert(mask_shr(UINT64_MAX, 64, 64) == 0ULL);
}

// ---------------------------------------------------------------------------
// mask_addsub
// Rule: lowest set bit i of (a|b); output = ((a|b) | (~0ULL << i)) & width_mask
// Special case: (a|b) == 0 → return 0
// ---------------------------------------------------------------------------
static void test_mask_addsub() {
    // Both zero → 0
    assert(mask_addsub(0ULL, 0ULL, 8) == 0ULL);
    assert(mask_addsub(0ULL, 0ULL, 64) == 0ULL);

    // Only bit 0 set: lowest bit = 0, so all bits from 0 up → width_mask
    {
        uint64_t result = mask_addsub(1ULL, 0ULL, 8);
        assert(result == 0xFFULL);
    }
    {
        uint64_t result = mask_addsub(0ULL, 1ULL, 8);
        assert(result == 0xFFULL);
    }

    // Bit 2 is lowest set: output starts at bit 2 → bits 2..7 set (for width=8)
    // m = 0b0100, lowest bit = 2, ~0ULL << 2 = ...11111100, clamped to 8 bits = 0b11111100 = 0xFC
    // result = (0b0100 | 0xFC) & 0xFF = 0xFC
    {
        uint64_t result = mask_addsub(0b0100ULL, 0ULL, 8);
        assert(result == 0xFCULL);
    }

    // Both operands: a=0b0010, b=0b1000 → m=0b1010, lowest bit=1
    // ~0ULL << 1 = ...11111110, clamped to 8 bits = 0xFE
    // result = (0b1010 | 0xFE) & 0xFF = 0xFE
    {
        uint64_t result = mask_addsub(0b0010ULL, 0b1000ULL, 8);
        assert(result == 0xFEULL);
    }

    // Width = 64, bit 3 lowest
    {
        uint64_t m = 0b1000ULL;
        uint64_t result = mask_addsub(m, 0ULL, 64);
        uint64_t expected = UINT64_MAX & (~0ULL << 3); // bits 3..63
        assert(result == expected);
    }

    // Width = 4, bit 1 lowest: bits 1..3 = 0b1110 = 0xE
    {
        uint64_t result = mask_addsub(0b0010ULL, 0ULL, 4);
        assert(result == 0xEULL);
    }
}

// ---------------------------------------------------------------------------
// mask_muldiv
// Rule: any bit set in a or b → all-ones for width; else 0
// ---------------------------------------------------------------------------
static void test_mask_muldiv() {
    // Both zero → 0
    assert(mask_muldiv(0ULL, 0ULL, 8) == 0ULL);
    assert(mask_muldiv(0ULL, 0ULL, 64) == 0ULL);

    // Any bit set → all-ones for width
    assert(mask_muldiv(1ULL, 0ULL, 8) == 0xFFULL);
    assert(mask_muldiv(0ULL, 1ULL, 8) == 0xFFULL);
    assert(mask_muldiv(0b1010ULL, 0b0101ULL, 8) == 0xFFULL);

    // Width 64 → UINT64_MAX
    assert(mask_muldiv(1ULL, 0ULL, 64) == UINT64_MAX);
    assert(mask_muldiv(0ULL, UINT64_MAX, 64) == UINT64_MAX);

    // Width 4 → 0xF
    assert(mask_muldiv(0b0001ULL, 0b0001ULL, 4) == 0xFULL);
    assert(mask_muldiv(0ULL, 0b1000ULL, 4) == 0xFULL);

    // Width 1 → 1
    assert(mask_muldiv(1ULL, 0ULL, 1) == 1ULL);
    assert(mask_muldiv(0ULL, 0ULL, 1) == 0ULL);
}

// ---------------------------------------------------------------------------
// constexpr smoke: verify these can be used in constant expressions
// ---------------------------------------------------------------------------
static_assert(mask_bitwise(0b1010ULL, 0b0101ULL) == 0b1111ULL);
static_assert(mask_not(0b1100ULL) == 0b1100ULL);
static_assert(mask_shl(0b0001ULL, 2, 8) == 0b0100ULL);
static_assert(mask_shr(0b1000ULL, 2, 8) == 0b0010ULL);
static_assert(mask_addsub(0ULL, 0ULL, 8) == 0ULL);
static_assert(mask_muldiv(0ULL, 0ULL, 8) == 0ULL);

int main() {
    test_mask_bitwise();
    test_mask_not();
    test_mask_shl();
    test_mask_shr();
    test_mask_addsub();
    test_mask_muldiv();
    std::puts("test_mask_ops: all assertions passed");
    return 0;
}
