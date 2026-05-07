// test_subscript.cpp — M18: qint_t<W>::operator[](i) returns non-owning qbool.
//
// Tests:
//   test_subscript_non_owning  — a[2] returns qbool with correct qubit index;
//                                ^= emits gate on correct qubit; destruction
//                                does NOT release the qubit.
//   test_subscript_in_library  — non-owning qbools from subscript work in
//                                qbool operators (a[0] ^= b[1] emits CX).
//   test_subscript_when        — WHEN(a[0]) { b[1] ^= a[2]; } emits a CCX.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qbool_ops.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/qubit_pool.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>

// ── Scoped context helper ─────────────────────────────────────────────────────

struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit ScopedCtx(sturm_mode_t mode, uint32_t max_q = 64u) {
        ctx  = sturm_backend_create(mode);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    sturm::BackendContext& bc() { return *ctx; }
};

// ── Helper: build a qint_t<4> with manually assigned qubit indices ────────────
//
// We assign qubit indices directly and do NOT allocate from the pool (so we
// can use arbitrary indices without pool management).  We ensure the pool
// "knows" about them by allocating placeholders.

static sturm::qint_t<4> make_qint4_with_qubits(int q0, int q1, int q2, int q3) {
    sturm::qint_t<4> a;
    a.qubits[0] = q0;
    a.qubits[1] = q1;
    a.qubits[2] = q2;
    a.qubits[3] = q3;
    return a;
}

// ── test_subscript_non_owning ─────────────────────────────────────────────────
// Verify:
//   1. a[2].qubits[0] == a.qubits[2]
//   2. a[2] is non-owning (owning_ == false)
//   3. After a[2] destructs, the qubit is still allocated (in_use unchanged)
//   4. ^= on a[2] emits exactly 1 gate targeting a.qubits[2]

static void test_subscript_non_owning() {
    ScopedCtx sc{STURM_MODE_APPEND};

    // Allocate 4 qubits from pool so the qint can release them normally.
    int q0 = sturm::QubitPool::instance().allocate();
    int q1 = sturm::QubitPool::instance().allocate();
    int q2 = sturm::QubitPool::instance().allocate();
    int q3 = sturm::QubitPool::instance().allocate();

    {
        sturm::qint_t<4> a;
        a.qubits[0] = q0;
        a.qubits[1] = q1;
        a.qubits[2] = q2;
        a.qubits[3] = q3;

        // Snapshot in-use count before creating the subscript qbool
        int in_use_before = sturm::QubitPool::instance().in_use();

        {
            sturm::qbool bit2 = a[2];

            // 1. Correct qubit index
            assert(bit2.qubits[0] == q2);

            // 2. Non-owning flag
            assert(!bit2.owning_);

            // 3. In-use count must not change (no new qubit allocated)
            assert(sturm::QubitPool::instance().in_use() == in_use_before);

            // 4. Emit a gate targeting bit2's qubit via XOR with itself
            //    (use a separate non-owning qbool for the source — qubit 0)
            sturm::qbool src = sturm::qbool::make_non_owning(q0);
            bit2 ^= src;

            // 1 gate should be recorded
            assert(sc.ctx->ir.size() == 1u);
            // It should be a CX gate
            assert(sc.ctx->ir.at(0).kind == STURM_GATE_CX);
            // source qubit = q0, target qubit = q2
            assert(sc.ctx->ir.at(0).qubits[0] == static_cast<uint32_t>(q0));
            assert(sc.ctx->ir.at(0).qubits[1] == static_cast<uint32_t>(q2));
        }
        // After bit2 destructs, in-use count is unchanged (qubit not released)
        assert(sturm::QubitPool::instance().in_use() == in_use_before);

        // Prevent the qint destructor from double-releasing — clear qubits
        // since we allocated from pool directly above.
        a.qubits[0] = -1;
        a.qubits[1] = -1;
        a.qubits[2] = -1;
        a.qubits[3] = -1;
    }

    // Release manually
    sturm::QubitPool::instance().release(q0);
    sturm::QubitPool::instance().release(q1);
    sturm::QubitPool::instance().release(q2);
    sturm::QubitPool::instance().release(q3);

    std::printf("  test_subscript_non_owning: PASS\n");
}

// ── test_subscript_in_library ─────────────────────────────────────────────────
// a[0] ^= b[1] emits exactly 1 CX gate with the correct qubits.

static void test_subscript_in_library() {
    ScopedCtx sc{STURM_MODE_APPEND};

    // Use pool-allocated qubits for a and b
    int qa0 = sturm::QubitPool::instance().allocate();
    int qa1 = sturm::QubitPool::instance().allocate();
    int qb0 = sturm::QubitPool::instance().allocate();
    int qb1 = sturm::QubitPool::instance().allocate();

    {
        sturm::qint_t<2> a, b;
        a.qubits[0] = qa0; a.qubits[1] = qa1;
        b.qubits[0] = qb0; b.qubits[1] = qb1;

        // a[0] ^= b[1]
        // a[0] is a non-owning qbool at qubit qa0
        // b[1] is a non-owning qbool at qubit qb1
        // Result: CX(qb1, qa0)
        {
            sturm::qbool lhs = a[0];
            sturm::qbool rhs = b[1];
            lhs ^= rhs;
        }

        assert(sc.ctx->ir.size() == 1u);
        assert(sc.ctx->ir.at(0).kind == STURM_GATE_CX);
        // source = rhs = b[1] = qb1, target = lhs = a[0] = qa0
        assert(sc.ctx->ir.at(0).qubits[0] == static_cast<uint32_t>(qb1));
        assert(sc.ctx->ir.at(0).qubits[1] == static_cast<uint32_t>(qa0));

        // Prevent double-release
        a.qubits[0] = -1; a.qubits[1] = -1;
        b.qubits[0] = -1; b.qubits[1] = -1;
    }

    sturm::QubitPool::instance().release(qa0);
    sturm::QubitPool::instance().release(qa1);
    sturm::QubitPool::instance().release(qb0);
    sturm::QubitPool::instance().release(qb1);

    std::printf("  test_subscript_in_library: PASS\n");
}

// ── test_subscript_when ───────────────────────────────────────────────────────
// WHEN(a[0]) { b[1] ^= a[2]; }  =>  CCX(a[0], a[2], b[1])
//
// We simulate WHEN by pushing a[0]'s qubit onto the control stack, executing
// the body, and popping.

static void test_subscript_when() {
    ScopedCtx sc{STURM_MODE_APPEND};

    int qa0 = sturm::QubitPool::instance().allocate();
    int qa1 = sturm::QubitPool::instance().allocate();
    int qa2 = sturm::QubitPool::instance().allocate();
    int qb0 = sturm::QubitPool::instance().allocate();
    int qb1 = sturm::QubitPool::instance().allocate();

    {
        sturm::qint_t<3> a;
        sturm::qint_t<2> b;
        a.qubits[0] = qa0; a.qubits[1] = qa1; a.qubits[2] = qa2;
        b.qubits[0] = qb0; b.qubits[1] = qb1;

        // Simulate WHEN(a[0]):
        //   push a[0]'s qubit (qa0) as the control
        {
            sturm::qbool ctrl = a[0];  // non-owning, qubit = qa0
            sc.bc().control_stack.push_control(
                static_cast<uint32_t>(ctrl.qubits[0]));

            // Body: b[1] ^= a[2]  =>  CX(a[2], b[1]) lifted to CCX(ctrl, a[2], b[1])
            {
                sturm::qbool lhs = b[1];  // non-owning, qubit = qb1
                sturm::qbool rhs = a[2];  // non-owning, qubit = qa2
                lhs ^= rhs;
            }

            sc.bc().control_stack.pop_control();
        }

        // Exactly 1 gate should be emitted: CCX
        assert(sc.ctx->ir.size() == 1u);
        assert(sc.ctx->ir.at(0).kind == STURM_GATE_CCX);
        // CCX: control=qa0, other-control=qa2 (from primitive_XOR under 1 ctrl),
        // target=qb1
        assert(sc.ctx->ir.at(0).qubits[0] == static_cast<uint32_t>(qa0));
        assert(sc.ctx->ir.at(0).qubits[1] == static_cast<uint32_t>(qa2));
        assert(sc.ctx->ir.at(0).qubits[2] == static_cast<uint32_t>(qb1));

        // Prevent double-release
        a.qubits[0] = -1; a.qubits[1] = -1; a.qubits[2] = -1;
        b.qubits[0] = -1; b.qubits[1] = -1;
    }

    sturm::QubitPool::instance().release(qa0);
    sturm::QubitPool::instance().release(qa1);
    sturm::QubitPool::instance().release(qa2);
    sturm::QubitPool::instance().release(qb0);
    sturm::QubitPool::instance().release(qb1);

    std::printf("  test_subscript_when: PASS\n");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M18 qint_t operator[] (non-owning qbool) tests:\n");
    test_subscript_non_owning();
    test_subscript_in_library();
    test_subscript_when();
    std::printf("All M18 subscript tests passed.\n");
    return 0;
}
