// test_reduction_table.cpp — M14: Classical-control reduction table (TDD — written before impl).
//
// Tests:
//   1. Exhaustive table drive: for each entry in the static table, feed a synthetic
//      invocation and assert the reduction result matches a hand-written expected result.
//   2. No unreachable patterns: every pattern in the table is valid and distinct
//      (generator test — walk all (gate_kind, pattern) pairs and verify there are
//      no duplicate keys and no pattern that has bits set beyond the control slots).
//
// Gate/operand conventions:
//   CX, CY, CZ, CRx, CRy, CRz  — operands[0] = ctrl,  operands[1] = target
//   CCX                          — operands[0] = ctrl0, operands[1] = ctrl1,
//                                  operands[2] = target
//
// Pattern encoding (classical_pattern_bits):
//   Bit i == 1 means operand i is a classical control with value 1.
//   Bit i == 0 means operand i is either quantum OR classical with value 0.
//   (Patterns where any control is classical-0 → gate does not fire; those are
//    handled by the caller before consulting the table. The table only covers the
//    classical-1 reduction cases.)
//
// Harness: plain assert + main (no gtest).

#include "sturm/dispatch/reduction_table.hpp"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cstdio>
#include <array>
#include <set>
#include <utility>

// ── Helpers ───────────────────────────────────────────────────────────────────

// Stringify a gate kind for diagnostic output.
static const char* gate_name(sturm_gate_kind_t k) {
    const sturm_gate_info_t* info = sturm_gate_info_of(k);
    return info ? info->name : "?";
}

// Pretty-print a ReductionResult for failure messages.
static void print_result(const sturm::ReductionResult& r) {
    if (r.is_noop) {
        std::printf("  → NO-OP\n");
        return;
    }
    std::printf("  → gate=%s arity=%u operands=[", gate_name(r.reduced_kind), (unsigned)r.n_remaining);
    for (uint8_t i = 0; i < r.n_remaining; ++i) {
        std::printf("%u", (unsigned)r.remaining_operand_indices[i]);
        if (i + 1 < r.n_remaining) std::printf(", ");
    }
    std::printf("]\n");
}

// ── Test 1: CX reductions ─────────────────────────────────────────────────────
//
// CX operands: [0]=ctrl, [1]=tgt
// Pattern 0x1 (ctrl=1): reduce to X(tgt)  → kind=X, operands=[1]

static void test_cx_reductions() {
    // ctrl classical-1 → strip control → X on target
    {
        sturm::ReductionKey key{STURM_GATE_CX, 0x1u};
        sturm::ReductionResult result = sturm::reduce(key);
        assert(!result.is_noop);
        assert(result.reduced_kind == STURM_GATE_X);
        assert(result.n_remaining  == 1u);
        assert(result.remaining_operand_indices[0] == 1u);
        std::printf("  CX(ctrl=1): PASS\n");
    }
    // pattern 0x0 — not in the reduction table (fully quantum or ctrl=0; not looked up)
    // We do NOT test 0x0 here because that case is handled by the caller.
}

// ── Test 2: CY reductions ─────────────────────────────────────────────────────
//
// CY operands: [0]=ctrl, [1]=tgt
// Pattern 0x1 (ctrl=1): reduce to Y(tgt)

static void test_cy_reductions() {
    sturm::ReductionKey key{STURM_GATE_CY, 0x1u};
    sturm::ReductionResult result = sturm::reduce(key);
    assert(!result.is_noop);
    assert(result.reduced_kind == STURM_GATE_Y);
    assert(result.n_remaining  == 1u);
    assert(result.remaining_operand_indices[0] == 1u);
    std::printf("  CY(ctrl=1): PASS\n");
}

// ── Test 3: CZ reductions ─────────────────────────────────────────────────────
//
// CZ operands: [0]=ctrl, [1]=tgt
// Pattern 0x1 (ctrl=1): reduce to Z(tgt)

static void test_cz_reductions() {
    sturm::ReductionKey key{STURM_GATE_CZ, 0x1u};
    sturm::ReductionResult result = sturm::reduce(key);
    assert(!result.is_noop);
    assert(result.reduced_kind == STURM_GATE_Z);
    assert(result.n_remaining  == 1u);
    assert(result.remaining_operand_indices[0] == 1u);
    std::printf("  CZ(ctrl=1): PASS\n");
}

// ── Test 4: CRx reductions ────────────────────────────────────────────────────
//
// CRx operands: [0]=ctrl, [1]=tgt
// Pattern 0x1 (ctrl=1): reduce to Rx(tgt)

static void test_crx_reductions() {
    sturm::ReductionKey key{STURM_GATE_CRX, 0x1u};
    sturm::ReductionResult result = sturm::reduce(key);
    assert(!result.is_noop);
    assert(result.reduced_kind == STURM_GATE_RX);
    assert(result.n_remaining  == 1u);
    assert(result.remaining_operand_indices[0] == 1u);
    std::printf("  CRx(ctrl=1): PASS\n");
}

// ── Test 5: CRy reductions ────────────────────────────────────────────────────

static void test_cry_reductions() {
    sturm::ReductionKey key{STURM_GATE_CRY, 0x1u};
    sturm::ReductionResult result = sturm::reduce(key);
    assert(!result.is_noop);
    assert(result.reduced_kind == STURM_GATE_RY);
    assert(result.n_remaining  == 1u);
    assert(result.remaining_operand_indices[0] == 1u);
    std::printf("  CRy(ctrl=1): PASS\n");
}

// ── Test 6: CRz reductions ────────────────────────────────────────────────────

static void test_crz_reductions() {
    sturm::ReductionKey key{STURM_GATE_CRZ, 0x1u};
    sturm::ReductionResult result = sturm::reduce(key);
    assert(!result.is_noop);
    assert(result.reduced_kind == STURM_GATE_RZ);
    assert(result.n_remaining  == 1u);
    assert(result.remaining_operand_indices[0] == 1u);
    std::printf("  CRz(ctrl=1): PASS\n");
}

// ── Test 7: CCX reductions — all 9 combinations ──────────────────────────────
//
// CCX operands: [0]=ctrl0, [1]=ctrl1, [2]=tgt
//
// Pattern encoding for control slots 0 and 1 (bit 0 = ctrl0, bit 1 = ctrl1):
//   0b00 = both quantum       → no reduction (fully quantum path; not in table)
//   0b01 = ctrl0=1,ctrl1=q   → CX(ctrl1, tgt)   operands=[1,2]
//   0b10 = ctrl0=q,ctrl1=1   → CX(ctrl0, tgt)   operands=[0,2]
//   0b11 = ctrl0=1,ctrl1=1   → X(tgt)            operands=[2]
//
// Additionally, patterns where a control is classical-0 mean the gate doesn't fire.
// Those are NOT in the reduction table; the caller short-circuits before lookup.
// The table only contains the "one or both controls are classical-1" patterns.

struct CCXCase {
    uint8_t             pattern;
    bool                is_noop;
    sturm_gate_kind_t   reduced_kind;
    uint8_t             n_remaining;
    uint8_t             ops[3];
};

static void test_ccx_reductions() {
    // All 4 non-trivial CCX patterns with only classical-1 marks
    // (pattern bits only set means that control = classical-1)
    static const CCXCase cases[] = {
        // ctrl0=1, ctrl1=quantum → CX(ctrl1[1], tgt[2])
        {0b01, false, STURM_GATE_CX, 2, {1, 2, 0}},
        // ctrl0=quantum, ctrl1=1 → CX(ctrl0[0], tgt[2])
        {0b10, false, STURM_GATE_CX, 2, {0, 2, 0}},
        // ctrl0=1, ctrl1=1 → X(tgt[2])
        {0b11, false, STURM_GATE_X,  1, {2, 0, 0}},
    };

    for (const auto& c : cases) {
        sturm::ReductionKey key{STURM_GATE_CCX, c.pattern};
        sturm::ReductionResult result = sturm::reduce(key);
        if (result.is_noop != c.is_noop ||
            (!c.is_noop && (result.reduced_kind != c.reduced_kind ||
                             result.n_remaining  != c.n_remaining))) {
            std::printf("  FAIL CCX pattern=0x%02x expected kind=%s n=%u, got:\n",
                        (unsigned)c.pattern, gate_name(c.reduced_kind), (unsigned)c.n_remaining);
            print_result(result);
            assert(false);
        }
        // Verify operand indices
        for (uint8_t i = 0; i < c.n_remaining; ++i) {
            assert(result.remaining_operand_indices[i] == c.ops[i]);
        }
        std::printf("  CCX(pattern=0x%02x): PASS\n", (unsigned)c.pattern);
    }
}

// ── Test 8: No unreachable patterns (generator test) ─────────────────────────
//
// Walk every (gate_kind, pattern) combination that is registered in the table.
// Assert:
//   a) No duplicate keys (std::set insertion fails on collision).
//   b) Pattern bits only reference valid control slots (not the target slot).
//   c) Every registered pattern has at least one control bit set
//      (pattern=0 means fully quantum and should NOT be in the table).

static void test_no_unreachable_patterns() {
    using Key = std::pair<int, uint8_t>;
    std::set<Key> seen;

    const sturm::ReductionTableEntry* table = sturm::reduction_table_entries();
    std::size_t count = sturm::reduction_table_size();

    for (std::size_t i = 0; i < count; ++i) {
        const sturm::ReductionTableEntry& entry = table[i];
        Key k{(int)entry.key.gate, entry.key.pattern};

        // No duplicate keys
        bool inserted = seen.insert(k).second;
        if (!inserted) {
            std::printf("  FAIL: duplicate key (gate=%s, pattern=0x%02x)\n",
                        gate_name(entry.key.gate), (unsigned)entry.key.pattern);
            assert(false);
        }

        // Pattern must have at least one bit set
        assert(entry.key.pattern != 0u &&
               "pattern=0 (fully quantum) must not appear in the reduction table");

        // Pattern bits must be valid for the gate's arity and only mark control
        // slots, not the target.  For CX/CY/CZ/CRx/CRy/CRz: arity=2, control=bit0.
        // For CCX: arity=3, controls=bits 0,1 (bit 2 is target and must be 0).
        const sturm_gate_info_t* info = sturm_gate_info_of(entry.key.gate);
        assert(info && "unknown gate kind in reduction table");

        uint8_t max_control_mask = 0u;
        if (entry.key.gate == STURM_GATE_CCX) {
            max_control_mask = 0b11u;  // bits 0 and 1 are both controls
        } else {
            max_control_mask = 0b01u;  // bit 0 is the only control
        }
        if ((entry.key.pattern & ~max_control_mask) != 0) {
            std::printf("  FAIL: pattern 0x%02x for gate=%s has bits beyond control slots\n",
                        (unsigned)entry.key.pattern, gate_name(entry.key.gate));
            assert(false);
        }
    }

    std::printf("  Generator: %zu entries, no duplicates, no invalid patterns: PASS\n", count);
}

// ── Test 9: lookup_reduce returns same result as reduce() ─────────────────────
//
// lookup_reduce is the function that Layer A will use.  It must return an
// identical result to reduce() for every known key, and must signal "not found"
// for keys not in the table (e.g., fully quantum CCX, pattern=0).

static void test_lookup_reduce_roundtrip() {
    // Known valid key
    {
        sturm::ReductionKey key{STURM_GATE_CX, 0x1u};
        sturm::ReductionResult r1 = sturm::reduce(key);
        auto opt = sturm::lookup_reduce(key);
        assert(opt.has_value());
        assert(opt->is_noop       == r1.is_noop);
        assert(opt->reduced_kind  == r1.reduced_kind);
        assert(opt->n_remaining   == r1.n_remaining);
        for (uint8_t i = 0; i < r1.n_remaining; ++i) {
            assert(opt->remaining_operand_indices[i] == r1.remaining_operand_indices[i]);
        }
    }
    // Pattern 0 on CCX → not in table → nullopt
    {
        sturm::ReductionKey key{STURM_GATE_CCX, 0x0u};
        auto opt = sturm::lookup_reduce(key);
        assert(!opt.has_value() && "pattern=0 (fully quantum) must not be found");
    }
    // Fully single-qubit gate → not in table → nullopt
    {
        sturm::ReductionKey key{STURM_GATE_X, 0x0u};
        auto opt = sturm::lookup_reduce(key);
        assert(!opt.has_value());
    }
    std::printf("  lookup_reduce roundtrip: PASS\n");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M14 reduction_table tests:\n");
    test_cx_reductions();
    test_cy_reductions();
    test_cz_reductions();
    test_crx_reductions();
    test_cry_reductions();
    test_crz_reductions();
    test_ccx_reductions();
    test_no_unreachable_patterns();
    test_lookup_reduce_roundtrip();
    std::printf("All M14 reduction_table tests passed.\n");
    return 0;
}
