// test_ir.cpp — M6: IR buffer + GateRecord tests.
// TDD: written before implementation, drives ir.hpp.
//
// Tests:
//   1. GateRecord fields roundtrip: kind, qubits, n, param.
//   2. GateIR append/size/at roundtrip for a single record.
//   3. GateIR append/size/at roundtrip for multiple heterogeneous records.
//   4. GateIR::clear resets size to zero.
//   5. Bulk append (many records) does not invalidate earlier at() references
//      when capacity is reserved up front (capacity reserve check).
//   6. at() on an empty GateIR asserts/throws (bounds check).

#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

static sturm::GateRecord make_record(sturm_gate_kind_t kind,
                                     uint32_t q0, uint32_t q1, uint32_t q2,
                                     uint8_t n, double param) {
    sturm::GateRecord r{};
    r.kind     = kind;
    r.qubits   = {q0, q1, q2};
    r.n        = n;
    r.param    = param;
    return r;
}

// ── Test 1: GateRecord field roundtrip ───────────────────────────────────────

static void test_gate_record_fields() {
    sturm::GateRecord r = make_record(STURM_GATE_CX, 3u, 7u, 0u, 2u, 1.5707963);

    assert(r.kind        == STURM_GATE_CX);
    assert(r.qubits[0]   == 3u);
    assert(r.qubits[1]   == 7u);
    assert(r.qubits[2]   == 0u);
    assert(r.n           == 2u);
    // param must be bit-exact (no arithmetic performed on it)
    assert(r.param       == 1.5707963);
}

// ── Test 2: single append/size/at roundtrip ──────────────────────────────────

static void test_single_append_roundtrip() {
    sturm::GateIR ir;
    assert(ir.size() == 0u);

    sturm::GateRecord rec = make_record(STURM_GATE_H, 5u, 0u, 0u, 1u, 0.0);
    ir.append(rec);

    assert(ir.size() == 1u);

    const sturm::GateRecord& got = ir.at(0);
    assert(got.kind      == STURM_GATE_H);
    assert(got.qubits[0] == 5u);
    assert(got.qubits[1] == 0u);
    assert(got.qubits[2] == 0u);
    assert(got.n         == 1u);
    assert(got.param     == 0.0);
}

// ── Test 3: multiple heterogeneous records ────────────────────────────────────

static void test_multiple_append_roundtrip() {
    sturm::GateIR ir;

    // Build a small sequence: H q0; CX q0,q1; Rz(pi/4) q2; CCX q0,q1,q2
    struct Expected {
        sturm_gate_kind_t kind;
        uint32_t          q0, q1, q2;
        uint8_t           n;
        double            param;
    };
    const Expected expected[] = {
        { STURM_GATE_H,   0, 0, 0, 1, 0.0            },
        { STURM_GATE_CX,  0, 1, 0, 2, 0.0            },
        { STURM_GATE_RZ,  2, 0, 0, 1, 0.7853981633974483 },
        { STURM_GATE_CCX, 0, 1, 2, 3, 0.0            },
    };

    for (const auto& e : expected) {
        ir.append(make_record(e.kind, e.q0, e.q1, e.q2, e.n, e.param));
    }

    assert(ir.size() == 4u);

    for (size_t i = 0; i < 4u; ++i) {
        const auto& got = ir.at(i);
        assert(got.kind      == expected[i].kind);
        assert(got.qubits[0] == expected[i].q0);
        assert(got.qubits[1] == expected[i].q1);
        assert(got.qubits[2] == expected[i].q2);
        assert(got.n         == expected[i].n);
        assert(got.param     == expected[i].param);
    }
}

// ── Test 4: clear resets size to zero ────────────────────────────────────────

static void test_clear() {
    sturm::GateIR ir;

    ir.append(make_record(STURM_GATE_X,  0, 0, 0, 1, 0.0));
    ir.append(make_record(STURM_GATE_CX, 0, 1, 0, 2, 0.0));
    assert(ir.size() == 2u);

    ir.clear();
    assert(ir.size() == 0u);

    // Re-appending after clear works correctly.
    ir.append(make_record(STURM_GATE_Z, 3, 0, 0, 1, 0.0));
    assert(ir.size() == 1u);
    assert(ir.at(0).kind == STURM_GATE_Z);
}

// ── Test 5: bulk append does not invalidate earlier records ──────────────────
//
// The plan says: "bulk append doesn't invalidate (capacity reserve)".
// We reserve capacity for N records, capture the address of at(0) before the
// bulk append, then verify the address is still the same and the values match.

static void test_bulk_append_no_invalidation() {
    sturm::GateIR ir;

    constexpr size_t kN = 256;
    ir.reserve(kN);  // pre-allocate so no realloc occurs during the bulk

    // Append the "anchor" record and capture pointer-like reference value.
    sturm::GateRecord anchor = make_record(STURM_GATE_CCX, 1u, 2u, 3u, 3u, 3.14159265);
    ir.append(anchor);

    // Remember the address of the first element.
    const sturm::GateRecord* ptr_before = &ir.at(0);

    // Bulk-append another kN-1 records.
    for (size_t i = 1; i < kN; ++i) {
        ir.append(make_record(static_cast<sturm_gate_kind_t>(i % STURM_GATE_COUNT),
                              static_cast<uint32_t>(i % 17),
                              static_cast<uint32_t>((i + 1) % 17),
                              static_cast<uint32_t>((i + 2) % 17),
                              1u,
                              static_cast<double>(i) * 0.01));
    }

    assert(ir.size() == kN);

    // Address must not have changed (no reallocation).
    const sturm::GateRecord* ptr_after = &ir.at(0);
    assert(ptr_before == ptr_after && "reserve() must prevent reallocation");

    // Values of the anchor record must still be intact.
    const sturm::GateRecord& got = ir.at(0);
    assert(got.kind      == STURM_GATE_CCX);
    assert(got.qubits[0] == 1u);
    assert(got.qubits[1] == 2u);
    assert(got.qubits[2] == 3u);
    assert(got.n         == 3u);
    assert(got.param     == 3.14159265);
}

// ── Test 6: at() out-of-range throws std::out_of_range ───────────────────────

static void test_at_out_of_range() {
    sturm::GateIR ir;
    bool caught = false;
    try {
        (void)ir.at(0);  // empty IR, should throw
    } catch (const std::out_of_range&) {
        caught = true;
    }
    assert(caught && "at() on empty GateIR must throw std::out_of_range");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    test_gate_record_fields();
    test_single_append_roundtrip();
    test_multiple_append_roundtrip();
    test_clear();
    test_bulk_append_no_invalidation();
    test_at_out_of_range();
    return 0;
}
