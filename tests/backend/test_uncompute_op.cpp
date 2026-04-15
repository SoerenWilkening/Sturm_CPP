// test_uncompute_op.cpp — M19: uncompute_op tagged union (TDD — written before implementation).
//
// Tests:
//   1. Size cap: sizeof(uncompute_op) <= 32.
//   2. Tag roundtrip: construct with each kind, read tag back.
//   3. ADD_CONST(c) apply: emits the same IR entries as a direct sub_const(c)
//      call on a qint_base (i.e. the inverse).  Uses APPEND mode via execute_gate.
//
// Harness: plain assert + main (no gtest).

#include "sturm/uncompute/uncompute_op.hpp"
#include "sturm/uncompute/qint_base.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/ir.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

// ── Fixture: scoped APPEND context ───────────────────────────────────────────

struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedAppendCtx() {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, 17u);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    sturm::GateIR& ir() { return ctx->ir; }
};

// ── Test 1: size cap ──────────────────────────────────────────────────────────

static void test_size_cap() {
    static_assert(sizeof(sturm::uncompute_op) <= 32,
                  "uncompute_op must fit in 32 bytes");
    std::printf("  size cap (static_assert passed, runtime sizeof=%zu): PASS\n",
                sizeof(sturm::uncompute_op));
}

// ── Test 2: tag roundtrip ─────────────────────────────────────────────────────

static void test_tag_roundtrip() {
    using kind = sturm::uncompute_op::kind;

    {
        sturm::uncompute_op op;
        assert(op.tag == kind::NONE);
    }
    {
        auto op = sturm::uncompute_op::make_add_const(7LL);
        assert(op.tag == kind::ADD_CONST);
    }
    {
        auto op = sturm::uncompute_op::make_sub_const(3LL);
        assert(op.tag == kind::SUB_CONST);
    }
    {
        auto op = sturm::uncompute_op::make_bitwise_self(nullptr, 0);
        assert(op.tag == kind::BITWISE_SELF);
    }
    {
        auto op = sturm::uncompute_op::make_compare(nullptr, nullptr, 0);
        assert(op.tag == kind::COMPARE);
    }

    std::printf("  tag roundtrip: PASS\n");
}

// ── Test 3: ADD_CONST(c) apply matches -=c on recording sink ─────────────────
//
// Strategy: create a qint_base with some qubit indices.
// Call qint_base::sub_const(c, ctx) and capture the IR.
// Create an ADD_CONST(c) op, call apply(ctx, self) and capture a fresh IR.
// Assert the two IR buffers are byte-for-byte equal.

static bool gate_records_equal(const sturm::GateRecord& a, const sturm::GateRecord& b) {
    if (a.kind != b.kind) return false;
    if (a.n    != b.n)    return false;
    if (a.param != b.param) return false;
    for (int i = 0; i < 3; ++i) {
        if (a.qubits[i] != b.qubits[i]) return false;
    }
    return true;
}

static void test_add_const_apply_matches_sub_const() {
    const int64_t c = 42LL;

    // Build a minimal qint_base with a known qubit layout.
    // We give it 4 qubits at indices 0..3 so sub_const has something to act on.
    sturm::qint_base self;
    self.width       = 4;
    self.value       = 10LL;   // arbitrary classical value
    self.super_mask  = 0xFu;   // all bits "quantum"
    self.qubits[0]   = 0u;
    self.qubits[1]   = 1u;
    self.qubits[2]   = 2u;
    self.qubits[3]   = 3u;

    // ── Capture IR from direct sub_const call ────────────────────────────────
    std::vector<sturm::GateRecord> direct_records;
    {
        ScopedAppendCtx sc;
        self.sub_const(c, *sc.ctx);
        size_t n = sc.ir().size();
        for (size_t i = 0; i < n; ++i) {
            direct_records.push_back(sc.ir().at(i));
        }
    }

    // ── Capture IR from ADD_CONST(c).apply ───────────────────────────────────
    std::vector<sturm::GateRecord> apply_records;
    {
        ScopedAppendCtx sc;
        auto op = sturm::uncompute_op::make_add_const(c);
        op.apply(*sc.ctx, self);
        size_t n = sc.ir().size();
        for (size_t i = 0; i < n; ++i) {
            apply_records.push_back(sc.ir().at(i));
        }
    }

    // ── Compare ──────────────────────────────────────────────────────────────
    assert(direct_records.size() == apply_records.size() &&
           "ADD_CONST apply must emit the same number of records as sub_const");
    for (size_t i = 0; i < direct_records.size(); ++i) {
        assert(gate_records_equal(direct_records[i], apply_records[i]) &&
               "ADD_CONST apply must emit byte-for-byte identical records to sub_const");
    }

    // Additionally, SUB_CONST(c).apply must produce the same as add_const(c)
    std::vector<sturm::GateRecord> sub_apply_records;
    {
        ScopedAppendCtx sc;
        self.add_const(c, *sc.ctx);
        size_t n = sc.ir().size();
        for (size_t i = 0; i < n; ++i) {
            sub_apply_records.push_back(sc.ir().at(i));
        }
    }
    std::vector<sturm::GateRecord> sub_op_records;
    {
        ScopedAppendCtx sc;
        auto op = sturm::uncompute_op::make_sub_const(c);
        op.apply(*sc.ctx, self);
        size_t n = sc.ir().size();
        for (size_t i = 0; i < n; ++i) {
            sub_op_records.push_back(sc.ir().at(i));
        }
    }
    assert(sub_apply_records.size() == sub_op_records.size());
    for (size_t i = 0; i < sub_apply_records.size(); ++i) {
        assert(gate_records_equal(sub_apply_records[i], sub_op_records[i]) &&
               "SUB_CONST apply must emit byte-for-byte identical records to add_const");
    }

    std::printf("  ADD_CONST apply matches sub_const: PASS\n");
    std::printf("  SUB_CONST apply matches add_const: PASS\n");
}

// ── Test 4: NONE tag apply is a no-op ────────────────────────────────────────

static void test_none_apply_noop() {
    sturm::qint_base self;
    self.width      = 4;
    self.value      = 7LL;
    self.super_mask = 0xFu;
    self.qubits[0]  = 0u;
    self.qubits[1]  = 1u;
    self.qubits[2]  = 2u;
    self.qubits[3]  = 3u;

    ScopedAppendCtx sc;
    sturm::uncompute_op none_op;  // default = NONE
    none_op.apply(*sc.ctx, self);
    assert(sc.ir().size() == 0u && "NONE apply must not emit any gates");
    std::printf("  NONE apply is no-op: PASS\n");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M19 uncompute_op tests:\n");
    test_size_cap();
    test_tag_roundtrip();
    test_add_const_apply_matches_sub_const();
    test_none_apply_noop();
    std::printf("All M19 uncompute_op tests passed.\n");
    return 0;
}
