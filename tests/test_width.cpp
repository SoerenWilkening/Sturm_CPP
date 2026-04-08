// test_width.cpp — Step 11 smoke tests for qint_t<8> Width parameterization.
// Verifies that mask clamping, shifts, addsub, and muldiv all stay within
// Width=8 (8-bit mask, max value 0xFF).  Also confirms the template compiles
// for Width=8 without changing the public surface.

#include "sturm/qtypes/qint.hpp"
#include "sturm/core/recording_sink.hpp"
#include "sturm/core/mask_ops.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

using sturm::qint_t;
using sturm::RecordingSink;
using sturm::ScopedSink;
using sturm::detail::mask_addsub;
using sturm::detail::mask_muldiv;
using sturm::detail::mask_shl;
using sturm::detail::mask_shr;
using sturm::detail::width_mask;

// ── helpers ───────────────────────────────────────────────────────────────────

static constexpr uint64_t kMask8 = 0xFFULL;  // all 8 bits set

// Assert that a result mask for Width=8 never exceeds 8 bits.
static void check_within_8(uint64_t mask) {
    assert((mask & ~kMask8) == 0 && "mask has bits set above position 7");
}

// ── width_mask helper ─────────────────────────────────────────────────────────

static void test_width_mask_8() {
    // width_mask(8) should be exactly 0xFF
    assert(width_mask(8) == kMask8);
}

// ── mask_addsub clamping for width=8 ─────────────────────────────────────────

static void test_addsub_clamp_8() {
    // Bit 0 set → carry fills all 8 bits → 0xFF, nothing above
    {
        uint64_t result = mask_addsub(1ULL, 0ULL, 8);
        assert(result == kMask8);
        check_within_8(result);
    }
    // Bit 7 set (highest for width=8) → only bit 7 is affected
    {
        uint64_t result = mask_addsub(0x80ULL, 0ULL, 8);
        assert(result == 0x80ULL);
        check_within_8(result);
    }
    // Bits from two operands that together cover multiple positions
    {
        uint64_t result = mask_addsub(0x04ULL, 0x10ULL, 8);
        // m = 0x14, lowest bit = 2 → carry fill from bit 2 up, clamped to 8 bits
        // (~0ULL << 2) & 0xFF = 0xFC; result = (0x14 | 0xFC) & 0xFF = 0xFC
        assert(result == 0xFCULL);
        check_within_8(result);
    }
    // Both zero → 0
    {
        uint64_t result = mask_addsub(0ULL, 0ULL, 8);
        assert(result == 0ULL);
        check_within_8(result);
    }
}

// ── mask_muldiv clamping for width=8 ─────────────────────────────────────────

static void test_muldiv_clamp_8() {
    // Any bit set → all 8 bits set, nothing above
    {
        uint64_t result = mask_muldiv(1ULL, 0ULL, 8);
        assert(result == kMask8);
        check_within_8(result);
    }
    {
        uint64_t result = mask_muldiv(0ULL, 0xFFULL, 8);
        assert(result == kMask8);
        check_within_8(result);
    }
    {
        uint64_t result = mask_muldiv(0x80ULL, 0x80ULL, 8);
        assert(result == kMask8);
        check_within_8(result);
    }
    // Both zero → 0
    {
        uint64_t result = mask_muldiv(0ULL, 0ULL, 8);
        assert(result == 0ULL);
    }
}

// ── mask_shl clamping for width=8 ─────────────────────────────────────────────

static void test_shl_clamp_8() {
    // Shift bit 6 left by 1 → bit 7 (still within 8 bits)
    {
        uint64_t result = mask_shl(0x40ULL, 1, 8);
        assert(result == 0x80ULL);
        check_within_8(result);
    }
    // Shift bit 7 left by 1 → shifted out of 8-bit window → 0
    {
        uint64_t result = mask_shl(0x80ULL, 1, 8);
        assert(result == 0x00ULL);
        check_within_8(result);
    }
    // Shift bit 0 left by 7 → bit 7
    {
        uint64_t result = mask_shl(0x01ULL, 7, 8);
        assert(result == 0x80ULL);
        check_within_8(result);
    }
    // Shift by width → zero
    {
        uint64_t result = mask_shl(0xFFULL, 8, 8);
        assert(result == 0x00ULL);
        check_within_8(result);
    }
}

// ── mask_shr clamping for width=8 ─────────────────────────────────────────────

static void test_shr_clamp_8() {
    // Shift bit 7 right by 1 → bit 6
    {
        uint64_t result = mask_shr(0x80ULL, 1, 8);
        assert(result == 0x40ULL);
        check_within_8(result);
    }
    // Shift bit 0 right by 1 → shifted out → 0
    {
        uint64_t result = mask_shr(0x01ULL, 1, 8);
        assert(result == 0x00ULL);
        check_within_8(result);
    }
    // Shift all bits right by 4
    {
        uint64_t result = mask_shr(0xF0ULL, 4, 8);
        assert(result == 0x0FULL);
        check_within_8(result);
    }
}

// ── qint_t<8> classical operations ───────────────────────────────────────────
// Confirm the template instantiates correctly and classical fast-path works.

static void test_qint8_classical() {
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint_t<8> a(10);
    qint_t<8> b(3);

    assert(a.value == 10);
    assert(b.value == 3);
    assert(a.super_mask == 0);
    assert(b.super_mask == 0);

    // Addition
    qint_t<8> sum = a + b;
    assert(sum.value == 13);
    assert(sum.super_mask == 0);
    assert(rs.records().empty());

    // Subtraction
    qint_t<8> diff = a - b;
    assert(diff.value == 7);
    assert(diff.super_mask == 0);
    assert(rs.records().empty());

    // Multiplication
    qint_t<8> prod = a * b;
    assert(prod.value == 30);
    assert(prod.super_mask == 0);
    assert(rs.records().empty());

    // Division
    qint_t<8> quot = a / b;
    assert(quot.value == 3);
    assert(quot.super_mask == 0);
    assert(rs.records().empty());
}

// ── qint_t<8> superposed addsub stays within 8 bits ──────────────────────────

static void test_qint8_super_addsub() {
    RecordingSink rs;
    ScopedSink scope(&rs);

    // a has bit 2 superposed (super_mask = 0x04)
    qint_t<8> a;
    a.value      = 4;
    a.super_mask = 0x04ULL;

    qint_t<8> b(1);  // classical

    qint_t<8> result = a + b;

    // mask_addsub(0x04, 0, 8): lowest bit of 0x04 is 2 → fill from bit 2 → 0xFC
    assert(result.super_mask == 0xFCULL);
    check_within_8(result.super_mask);
    assert(result.value == 5);
}

// ── qint_t<8> superposed muldiv stays within 8 bits ──────────────────────────

static void test_qint8_super_muldiv() {
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint_t<8> a;
    a.value      = 3;
    a.super_mask = 0x01ULL;  // bit 0 superposed

    qint_t<8> b(5);

    qint_t<8> result = a * b;

    // mask_muldiv(0x01, 0, 8) = 0xFF (all 8 bits)
    assert(result.super_mask == kMask8);
    check_within_8(result.super_mask);
}

// ── qint_t<8> shift stays within 8 bits ──────────────────────────────────────

static void test_qint8_shift() {
    RecordingSink rs;
    ScopedSink scope(&rs);

    qint_t<8> a;
    a.value      = 2;
    a.super_mask = 0x40ULL;  // bit 6 superposed

    qint_t<8> lshift = a << 2;
    // mask_shl(0x40, 2, 8) → 0x40 << 2 = 0x100, clamped to 8 bits → 0x00
    assert(lshift.super_mask == 0x00ULL);
    check_within_8(lshift.super_mask);

    qint_t<8> c;
    c.value      = 4;
    c.super_mask = 0x80ULL;  // bit 7 superposed

    qint_t<8> rshift = c >> 1;
    // mask_shr(0x80, 1, 8) → 0x40
    assert(rshift.super_mask == 0x40ULL);
    check_within_8(rshift.super_mask);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_width_mask_8();
    test_addsub_clamp_8();
    test_muldiv_clamp_8();
    test_shl_clamp_8();
    test_shr_clamp_8();
    test_qint8_classical();
    test_qint8_super_addsub();
    test_qint8_super_muldiv();
    test_qint8_shift();
    std::puts("test_width: all assertions passed");
    return 0;
}
