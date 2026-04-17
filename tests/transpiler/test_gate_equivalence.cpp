// test_gate_equivalence.cpp — M12 transpiler-MVP capstone harness.
//
// Proves PRD acceptance criterion #4: the gate stream emitted when the
// transpiler's output is compiled and run is byte-for-byte identical to
// the gate stream produced by a hand-written reference that pairs the
// forward `qbool tmp = a | b;` with an explicit `uncompute_or(tmp, a, b);`
// call before scope exit.
//
// How it works
// ------------
// Two separately compiled translation units each expose a namespaced
// `demo(const qbool&, const qbool&)` function:
//
//   m12_transpiled::demo  — built via the `add_quantum_executable()`
//                           helper.  Under STURM_TRANSPILE=ON the source
//                           file (`fixtures/or_single_runtime.cpp`) is
//                           routed through `sturm-transpile`; the
//                           generated TU lives at
//                           ${CMAKE_BINARY_DIR}/sturm_gen/... and is the
//                           object file linked into this harness.
//
//   m12_reference::demo   — built from `fixtures/or_single_reference.cpp`
//                           directly (no transpile step).  The source is
//                           already a hand-written realization of what
//                           the transpiler's output should look like.
//
// The harness installs a scoped APPEND-mode BackendContext, runs one
// `demo()` invocation against it, snapshots ctx->ir, clears the context,
// resets the QubitPool and repeats for the other fixture.  A
// gate-by-gate comparison of the two IR buffers is the test's pass/fail
// signal.
//
// Why both fixtures live in private namespaces
// --------------------------------------------
// Two TUs that both define a free `demo` in the global namespace would
// violate the ODR and fail to link.  By wrapping each in a distinct
// namespace the harness can unambiguously dispatch to one or the other
// and call them back-to-back.
//
// Input qbool preparation
// -----------------------
// `demo()` takes `const qbool&` rather than `qbool`, because the qbool
// copy constructor resets `qubits[0]` to -1 (see include/sturm/qtypes/
// qbool.hpp line 101).  A by-value parameter would arrive classical and
// the forward OR would take the classical short-circuit in
// qbool_ops.hpp, emitting zero gates on both sides — a trivially-equal
// comparison that would not actually verify the transpiler's output.
// Passing by reference keeps the caller-supplied qubit indices intact
// so the forward three-gate OR circuit (CX + CX + CCX) and its inverse
// both fire.
//
// The harness builds the two input qbools via `qbool::make_non_owning`
// on freshly-allocated QubitPool indices; ownership stays with the
// harness so the qbool destructors at the end of `demo` do not attempt
// to release them.

#include "sturm/sturm.hpp"

#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/lazy_expr.hpp"
#include "sturm/qtypes/qbool_ops.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// ── Fixture forward declarations ─────────────────────────────────────────────
// Each fixture TU is expected to define the same signature in its own
// namespace.  The harness does not include either fixture's source — it
// relies only on the linker to resolve the symbols.

namespace m12_transpiled {
void demo(const sturm::qbool& a, const sturm::qbool& b);
// LP7: pattern mirroring examples/or_circuit.cpp (`qbool c = a | b;`).
void demo_or_circuit(const sturm::qbool& a, const sturm::qbool& b);
} // namespace m12_transpiled

namespace m12_reference {
void demo(const sturm::qbool& a, const sturm::qbool& b);
// LP7: hand-written reference for the or_circuit pattern with explicit
// `uncompute_or(c, a, b);`.
void demo_or_circuit(const sturm::qbool& a, const sturm::qbool& b);
} // namespace m12_reference

// PG-7: Phase G nested-WHEN fixture pair.  The transpiler rewrites
// `WHEN(outer) { WHEN(inner) { target.flip(); } }` into an explicit
// `qbool __stu_ctrl0 = outer & inner;` decl + `WHEN(__stu_ctrl0)` +
// trailing `sturm::uncompute_and(__stu_ctrl0, outer, inner);` call.
// The reference fixture spells the lowering by hand.  Both demos
// take three qbools so the helper can stage outer + inner as the
// superposed controls and `target` as a separately-allocated classical-
// owning qbool with super_mask=1.
namespace m12_nested_transpiled {
// outer/inner/target are taken by non-const reference: WHEN(expr)
// requires a non-const lvalue at the materialize_when call site, so
// the transpiled variant must take the controls as non-const refs.
// See fixture TU comments for the full rationale.
void demo(sturm::qbool& outer,
          sturm::qbool& inner,
          sturm::qbool& target);
} // namespace m12_nested_transpiled

namespace m12_nested_reference {
void demo(sturm::qbool& outer,
          sturm::qbool& inner,
          sturm::qbool& target);
} // namespace m12_nested_reference

// PH-6b: Phase H if/else fixture pair.  The transpiler rewrites each
// branch's `qbool <var> = a | b;` into a decl + `sturm::uncompute_or(
// <var>, a, b);` pair before that branch's closing `}`, so the captured
// gate stream covers both branch lowerings when `demo` is invoked
// twice (cond=true, cond=false).  The reference fixture spells the two
// lowerings by hand.  `const qbool&` arguments preserve the caller's
// qubit indices across the classical `cond` selector.
namespace m12_if_branches_transpiled {
void demo(const sturm::qbool& a, const sturm::qbool& b, bool cond);
} // namespace m12_if_branches_transpiled

namespace m12_if_branches_reference {
void demo(const sturm::qbool& a, const sturm::qbool& b, bool cond);
} // namespace m12_if_branches_reference

// PH-6a: Phase H for-loop fixture pair.  The transpiler rewrites each
// iteration's `qbool tmp = a | b;` into a decl + `sturm::uncompute_or(
// tmp, a, b);` pair before the loop body's closing `}`, so every
// iteration emits six gates (three forward OR + three adjoint) against
// the SAME recycled ancilla index (the QubitPool free-list is LIFO, so
// tmp's RAII release at `}` hands the same index back to the next
// iteration's `allocate()`).  The reference fixture spells the per-
// iteration lowering by hand.  `const qbool&` arguments preserve the
// caller's qubit indices across the three iterations.
namespace m12_for_loop_transpiled {
void demo(const sturm::qbool& a, const sturm::qbool& b);
} // namespace m12_for_loop_transpiled

namespace m12_for_loop_reference {
void demo(const sturm::qbool& a, const sturm::qbool& b);
} // namespace m12_for_loop_reference

// PI-7: Phase I user-routine gate-equivalence pair.  The transpiler
// matches the registered forward call `ur_rotate_fwd_runtime(tmp, in,
// 3);` (Case 1 = local-intermediate output, per PI-3) and injects
// `invert(ur_rotate_fwd_runtime)(tmp, in, 3);` before the enclosing
// scope's close brace — which dispatches to the registered adjoint
// `ur_rotate_adj_runtime` via the PI-0 trait table.  The reference
// fixture spells both the forward and the adjoint by hand in LIFO
// order (forward, then adjoint).  Because `tmp` is created as a fresh
// local qbool in both fixtures against a harness-reset QubitPool, the
// allocated ancilla index matches across the two captures.  The demo
// takes `const qbool&` by reference so the caller-supplied quantum
// input index is preserved intact on both sides.
namespace m12_user_routine_transpiled {
void demo(const sturm::qbool& in);
} // namespace m12_user_routine_transpiled

namespace m12_user_routine_reference {
void demo(const sturm::qbool& in);
} // namespace m12_user_routine_reference

// PJ-1i: Phase J zero-ancilla fusion gate-equivalence pair.  The
// transpiler's PJ-1d peephole matcher fires on `qbool __t = a & b; x
// ^= __t;` (bare `&` init, adjacent `^=` consumer, exactly one reader
// of `__t` in scope) and rewrites the pair into a single
// `ccnot_inplace(x, a, b);` call with a matching self-adjoint
// `ccnot_inplace(x, a, b);` planted before the demo's closing `}` by
// the PJ-1c render case.  The reference fixture spells both calls by
// hand via `primitive_AND(ctx, a_idx, b_idx, x_idx)` directly against
// the 18-gate sink — NOT via `lazy_expr` / `operator&` on qbool,
// because the pre-K lazy materialisation allocates an intermediate
// ancilla which the fusion explicitly collapses away; using it would
// give a trivially-mismatched comparison.  Expected stream: two CCX
// records on the three caller-supplied qubit indices.  `x` is taken
// by non-const reference because the runtime fixture's `x ^= __t;`
// needs a non-const lvalue and `ccnot_inplace` (the helper the
// transpiler's fused rewrite calls) takes its target by non-const
// reference.
namespace m12_fused_transpiled {
void demo(const sturm::qbool& a,
          const sturm::qbool& b,
          sturm::qbool& x);
} // namespace m12_fused_transpiled

namespace m12_fused_reference {
void demo(const sturm::qbool& a,
          const sturm::qbool& b,
          sturm::qbool& x);
} // namespace m12_fused_reference

// PJ-4e: Phase J dead-ancilla elimination gate-equivalence pair.  The
// transpiler's PJ-4a peephole matcher fires on `qbool dead = a | b;`
// when `dead` has zero readers in the enclosing scope, emits one
// empty-text `QReplacement` over the decl's full stmt range, and pushes
// the same range into `QUnit::eliminated_stmt_ranges` so the downstream
// MVP OR matcher early-returns on the covered range.  Net effect on
// the transpiled path: the decl vanishes, no `uncompute_or(dead, a, b);`
// is planted at scope close, and the `a | b` RHS never allocates an
// ancilla — ZERO gates emitted for the dead slice.  The reference
// fixture omits the dead decl entirely so both fixtures emit the same
// sentinel `target.flip()` call (one X(target) record) and nothing
// else.  Expected stream: ONE X gate.  `target` is taken by non-const
// reference because `flip()` mutates the qbool's state via
// emit_X_lifted; `a` / `b` are `const qbool&` to preserve the caller's
// qubit indices across the demo boundary (even though the reference
// never touches them — the signature match lets the harness dispatch
// both fixtures through a single function-pointer shape).
namespace m12_dead_ancilla_transpiled {
void demo(const sturm::qbool& a,
          const sturm::qbool& b,
          sturm::qbool& target);
} // namespace m12_dead_ancilla_transpiled

namespace m12_dead_ancilla_reference {
void demo(const sturm::qbool& a,
          const sturm::qbool& b,
          sturm::qbool& target);
} // namespace m12_dead_ancilla_reference

namespace {

// Scoped APPEND-mode BackendContext.  Construction installs the context
// as the thread-local; destruction restores the previous one and
// destroys the context.  The QubitPool is also reset here so each
// fixture sees identical qubit indices.
struct ScopedAppendContext {
    sturm_backend_context_t* ctx  = nullptr;
    sturm_backend_context_t* prev = nullptr;

    ScopedAppendContext() {
        sturm::QubitPool::instance().reset_for_testing();
        ctx = sturm_backend_create(STURM_MODE_APPEND, 17u);
        assert(ctx && "sturm_backend_create(APPEND) returned null");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendContext() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    sturm::BackendContext& ref() { return *ctx; }
    const sturm::GateIR& ir() const { return ctx->ir; }
};

// Snapshot the IR buffer into a value-type vector so comparisons are
// independent of the originating context's lifetime.
std::vector<sturm::GateRecord> capture_ir(const sturm::GateIR& ir) {
    std::vector<sturm::GateRecord> out;
    out.reserve(ir.size());
    for (std::size_t i = 0; i < ir.size(); ++i) {
        out.push_back(ir.at(i));
    }
    return out;
}

// Human-readable rendering of a GateRecord.  Used only when the test
// fails so that the ctest log pinpoints the exact mismatch.
std::string render_gate(const sturm::GateRecord& g) {
    const sturm_gate_info_t* info = sturm_gate_info_of(g.kind);
    std::string out;
    out.reserve(48);
    out.append(info ? info->name : "?");
    out.append("(");
    for (uint8_t i = 0; i < g.n && i < 3u; ++i) {
        if (i) out.append(",");
        out.append(std::to_string(g.qubits[i]));
    }
    out.append(")");
    if (g.param != 0.0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "@%.6g", g.param);
        out.append(buf);
    }
    return out;
}

// Call the fixture-supplied demo with two freshly-prepared quantum
// qbools, then return the captured gate stream.  The caller retains
// ownership of the two qubits allocated for `a`/`b`; the harness
// releases them after each capture so pool indices reset cleanly.
std::vector<sturm::GateRecord>
run_and_capture(void (*demo)(const sturm::qbool&, const sturm::qbool&)) {
    ScopedAppendContext sc;

    // Allocate two qubits for the input operands.  make_non_owning
    // builds a qbool view that references the allocated index without
    // taking ownership — the qbool destructor inside `demo` will not
    // release them.
    const int qa = sturm::QubitPool::instance().allocate();
    const int qb = sturm::QubitPool::instance().allocate();
    sturm::qbool a = sturm::qbool::make_non_owning(qa);
    a.super_mask = 1ULL;
    sturm::qbool b = sturm::qbool::make_non_owning(qb);
    b.super_mask = 1ULL;

    demo(a, b);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(qa);
    sturm::QubitPool::instance().release(qb);
    return stream;
}

// PG-7: widened capture helper for the Phase G nested-WHEN fixture
// pair.  Three qubits: outer + inner are staged as superposed controls
// (super_mask=1) and target is staged as a classical-owning qbool
// (super_mask=1) so the inner WHEN body's lifted `target.flip()`
// emits CX(__stu_ctrl, target) against the active control stack.
// Ownership stays with the harness: `make_non_owning` builds qbool
// views that reference the allocated indices without taking
// ownership, so the qbool destructors inside `demo` do not attempt
// to release them.  The harness releases all three allocated qubits
// after each capture so pool indices reset cleanly across calls.
std::vector<sturm::GateRecord>
run_and_capture_nested(void (*demo)(sturm::qbool&,
                                    sturm::qbool&,
                                    sturm::qbool&)) {
    ScopedAppendContext sc;

    const int qo = sturm::QubitPool::instance().allocate();
    const int qi = sturm::QubitPool::instance().allocate();
    const int qt = sturm::QubitPool::instance().allocate();
    sturm::qbool outer = sturm::qbool::make_non_owning(qo);
    outer.super_mask = 1ULL;
    sturm::qbool inner = sturm::qbool::make_non_owning(qi);
    inner.super_mask = 1ULL;
    sturm::qbool target = sturm::qbool::make_non_owning(qt);
    target.super_mask = 1ULL;

    demo(outer, inner, target);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(qo);
    sturm::QubitPool::instance().release(qi);
    sturm::QubitPool::instance().release(qt);
    return stream;
}

// PH-6b: capture helper for the Phase H if/else fixture pair.  Two
// qubits (`a`, `b`) are allocated once per capture and passed by
// `make_non_owning` qbool views — ownership stays with the harness so
// the qbool destructors inside `demo` do not release them.  Each
// capture invokes `demo` TWICE — once with `cond=true`, once with
// `cond=false` — so both branches' lowerings show up in the stream.
// Pool indices reset after the capture so the transpiled and
// reference captures see identical qubit indices for `a`, `b`, and
// the per-branch intermediates.
std::vector<sturm::GateRecord>
run_and_capture_if_branches(void (*demo)(const sturm::qbool&,
                                         const sturm::qbool&,
                                         bool)) {
    ScopedAppendContext sc;

    const int qa = sturm::QubitPool::instance().allocate();
    const int qb = sturm::QubitPool::instance().allocate();
    sturm::qbool a = sturm::qbool::make_non_owning(qa);
    a.super_mask = 1ULL;
    sturm::qbool b = sturm::qbool::make_non_owning(qb);
    b.super_mask = 1ULL;

    demo(a, b, true);
    demo(a, b, false);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(qa);
    sturm::QubitPool::instance().release(qb);
    return stream;
}

// PI-7: capture helper for the Phase I user-routine fixture pair.  The
// demo takes a single `const qbool& in` quantum input; one fresh qubit
// is allocated via `make_non_owning` so the harness retains ownership
// (the demo's own `qbool tmp; tmp.ensure_qubit();` allocates ITS ancilla
// through the same QubitPool, whose reset happens in `ScopedAppendContext`
// before each capture — so both the runtime and reference captures see
// identical qubit indices for `in` and `tmp`).  No multi-invocation
// loop: the demo is called exactly once per capture.
std::vector<sturm::GateRecord>
run_and_capture_user_routine(void (*demo)(const sturm::qbool&)) {
    ScopedAppendContext sc;

    const int qin = sturm::QubitPool::instance().allocate();
    sturm::qbool in = sturm::qbool::make_non_owning(qin);
    in.super_mask = 1ULL;

    demo(in);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(qin);
    return stream;
}

// PJ-1i: capture helper for the Phase J zero-ancilla fusion pair.  The
// demo takes `(const qbool& a, const qbool& b, qbool& x)`; three fresh
// qubits are allocated via `make_non_owning` so the harness retains
// ownership (the runtime demo's `qbool __t = a & b; x ^= __t;` is
// REPLACED in-place by `ccnot_inplace(x, a, b);` by the PJ-1d
// QReplacement — NO ancilla allocation survives the fusion.  The
// reference demo similarly calls `primitive_AND(ctx, a, b, x)`
// directly, never touching the QubitPool).  `x` is allocated via
// `make_non_owning` and passed by non-const reference because the
// runtime fixture's `^=` requires a non-const lvalue; ownership stays
// with the harness so the qbool destructor inside `demo` does not
// attempt to release the qubit.  Both the runtime and reference
// captures therefore observe the SAME three caller-supplied indices,
// which is the precondition for the byte-identical CCX(a, b, x) pair.
std::vector<sturm::GateRecord>
run_and_capture_fused(void (*demo)(const sturm::qbool&,
                                   const sturm::qbool&,
                                   sturm::qbool&)) {
    ScopedAppendContext sc;

    const int qa = sturm::QubitPool::instance().allocate();
    const int qb = sturm::QubitPool::instance().allocate();
    const int qx = sturm::QubitPool::instance().allocate();
    sturm::qbool a = sturm::qbool::make_non_owning(qa);
    a.super_mask = 1ULL;
    sturm::qbool b = sturm::qbool::make_non_owning(qb);
    b.super_mask = 1ULL;
    sturm::qbool x = sturm::qbool::make_non_owning(qx);
    x.super_mask = 1ULL;

    demo(a, b, x);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(qa);
    sturm::QubitPool::instance().release(qb);
    sturm::QubitPool::instance().release(qx);
    return stream;
}

// PJ-4e: capture helper for the Phase J dead-ancilla elimination pair.
// The demo takes `(const qbool& a, const qbool& b, qbool& target)`;
// three fresh qubits are allocated via `make_non_owning` so the
// harness retains ownership across the demo boundary.  `a` and `b`
// are passed through to preserve the runtime fixture's signature (the
// PJ-4a matcher fires on `qbool dead = a | b;` whose RHS references
// both argument DeclRefExprs — if `a` or `b` did not appear in the
// signature, the fixture would fail to parse).  `target` is allocated
// via `make_non_owning` and passed by non-const reference because
// `target.flip()` is a non-const member invocation that mutates the
// qbool's state via emit_X_lifted.  Both the runtime and reference
// captures therefore observe the SAME three caller-supplied indices,
// which is the precondition for the byte-identical X(target) record
// pair.  The runtime demo's `qbool dead = a | b;` is REPLACED in-place
// by empty text by the PJ-4a QReplacement — NO ancilla allocation
// survives the elimination; the reference demo similarly never
// allocates an ancilla because it simply does not carry the dead decl.
std::vector<sturm::GateRecord>
run_and_capture_dead_ancilla(void (*demo)(const sturm::qbool&,
                                          const sturm::qbool&,
                                          sturm::qbool&)) {
    ScopedAppendContext sc;

    const int qa = sturm::QubitPool::instance().allocate();
    const int qb = sturm::QubitPool::instance().allocate();
    const int qt = sturm::QubitPool::instance().allocate();
    sturm::qbool a = sturm::qbool::make_non_owning(qa);
    a.super_mask = 1ULL;
    sturm::qbool b = sturm::qbool::make_non_owning(qb);
    b.super_mask = 1ULL;
    sturm::qbool target = sturm::qbool::make_non_owning(qt);
    target.super_mask = 1ULL;

    demo(a, b, target);

    auto stream = capture_ir(sc.ir());

    sturm::QubitPool::instance().release(qa);
    sturm::QubitPool::instance().release(qb);
    sturm::QubitPool::instance().release(qt);
    return stream;
}

bool gates_equal(const sturm::GateRecord& x, const sturm::GateRecord& y) {
    if (x.kind != y.kind) return false;
    if (x.n    != y.n)    return false;
    for (uint8_t i = 0; i < x.n && i < 3u; ++i) {
        if (x.qubits[i] != y.qubits[i]) return false;
    }
    return x.param == y.param;
}

int assert_streams_equal(const std::vector<sturm::GateRecord>& got,
                         const std::vector<sturm::GateRecord>& ref) {
    if (got.size() != ref.size()) {
        std::fprintf(stderr,
                     "gate stream size mismatch: transpiled=%zu reference=%zu\n",
                     got.size(), ref.size());
        const std::size_t n = got.size() > ref.size() ? got.size() : ref.size();
        for (std::size_t i = 0; i < n; ++i) {
            const std::string g = i < got.size()
                ? render_gate(got[i]) : std::string("<missing>");
            const std::string r = i < ref.size()
                ? render_gate(ref[i]) : std::string("<missing>");
            std::fprintf(stderr, "  [%zu] transpiled=%s reference=%s\n",
                         i, g.c_str(), r.c_str());
        }
        return 1;
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (!gates_equal(got[i], ref[i])) {
            std::fprintf(stderr,
                         "gate[%zu] mismatch: transpiled=%s reference=%s\n",
                         i, render_gate(got[i]).c_str(),
                         render_gate(ref[i]).c_str());
            return 1;
        }
    }
    return 0;
}

} // namespace

int main() {
    std::printf("M12 gate-stream equivalence test (or_single):\n");

    // Capture reference first so any qubit-pool state from a prior
    // harness call (e.g. global ctor) is already drained by the
    // run_and_capture reset.
    const auto ref_stream = run_and_capture(&m12_reference::demo);
    const auto got_stream = run_and_capture(&m12_transpiled::demo);

    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_stream.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_stream[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_stream.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_stream[i]).c_str());
    }

    // Sanity check: both fixtures must emit a non-empty gate stream,
    // otherwise the test would pass trivially (empty == empty) without
    // exercising the transpiler.  The forward-quantum OR emits exactly
    // three gates (CX + CX + CCX) and the inverse emits another three
    // (CCX + CX + CX), so we expect six gates on each side.
    if (ref_stream.empty()) {
        std::fprintf(stderr,
                     "reference fixture produced 0 gates — fixture is not "
                     "exercising the quantum OR circuit.\n");
        return 1;
    }

    if (int rc = assert_streams_equal(got_stream, ref_stream); rc != 0) {
        std::fprintf(stderr, "gate-stream mismatch — see log above.\n");
        return rc;
    }

    // LP7: re-assertion for the `examples/or_circuit.cpp` pattern —
    // same shape (`qbool c = a | b;`) but with a variable name that
    // matches the real example. This binds PRD acceptance #5 to the
    // example's actual spelling. Streams must match gate-for-gate and
    // operand-for-operand.
    std::printf("LP7 gate-stream equivalence test (or_circuit pattern):\n");
    const auto ref_c = run_and_capture(&m12_reference::demo_or_circuit);
    const auto got_c = run_and_capture(&m12_transpiled::demo_or_circuit);
    if (ref_c.empty()) {
        std::fprintf(stderr,
                     "or_circuit reference produced 0 gates — fixture not "
                     "exercising the quantum OR circuit.\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_c, ref_c); rc != 0) {
        std::fprintf(stderr, "or_circuit gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  or_circuit streams match (%zu gates).\n", ref_c.size());

    // PG-7: Phase G nested-WHEN gate-equivalence pair.  Proves the
    // transpiler's nested-WHEN lowering (outer & inner → __stu_ctrl +
    // WHEN + uncompute_and) emits the same gate stream as the hand-
    // written reference.  Expected stream is three gates:
    //   CCX(outer, inner, __stu_ctrl)   // from `qbool __stu_ctrl = outer & inner;`
    //   CX (__stu_ctrl, target)         // from WHEN(__stu_ctrl) { target.flip(); }
    //   CCX(outer, inner, __stu_ctrl)   // from sturm::uncompute_and(...)
    std::printf("PG-7 gate-stream equivalence test (nested_when pattern):\n");
    const auto ref_n = run_and_capture_nested(&m12_nested_reference::demo);
    const auto got_n = run_and_capture_nested(&m12_nested_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_n.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_n[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_n.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_n[i]).c_str());
    }
    if (ref_n.empty()) {
        std::fprintf(stderr,
                     "nested_when reference produced 0 gates — fixture not "
                     "exercising the Phase G nested-WHEN lowering.\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_n, ref_n); rc != 0) {
        std::fprintf(stderr, "nested_when gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  nested_when streams match (%zu gates).\n", ref_n.size());

    // PH-6b: Phase H if/else gate-equivalence pair.  Proves the
    // transpiler's per-branch OR-uncompute injection emits the same
    // gate stream as the hand-written reference.  Each branch is a
    // six-gate forward+adjoint sequence; the harness invokes `demo`
    // twice per capture (cond=true then cond=false) so BOTH branch
    // lowerings are exercised in a single stream — expected length is
    // twelve gates.
    std::printf("PH-6b gate-stream equivalence test (if_branches pattern):\n");
    const auto ref_if = run_and_capture_if_branches(&m12_if_branches_reference::demo);
    const auto got_if = run_and_capture_if_branches(&m12_if_branches_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_if.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_if[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_if.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_if[i]).c_str());
    }
    if (ref_if.empty()) {
        std::fprintf(stderr,
                     "if_branches reference produced 0 gates — fixture not "
                     "exercising the Phase H if/else OR lowering.\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_if, ref_if); rc != 0) {
        std::fprintf(stderr, "if_branches gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  if_branches streams match (%zu gates).\n", ref_if.size());

    // PH-6a: Phase H for-loop gate-equivalence pair.  Proves the
    // transpiler's per-iteration OR-uncompute injection emits the same
    // gate stream as the hand-written reference.  Each iteration is a
    // six-gate forward+adjoint sequence; the body has three iterations
    // so the captured stream covers eighteen gates.  Because the
    // QubitPool is a LIFO free-list and `tmp`'s RAII destructor releases
    // the ancilla at the body's `}`, every iteration reuses the same
    // qubit index — the byte-compare therefore fails hard if the
    // transpiler ever drifts the injected call out of the per-iteration
    // scope (e.g. planting it after the loop, or once per function).
    std::printf("PH-6a gate-stream equivalence test (for_loop pattern):\n");
    const auto ref_for = run_and_capture(&m12_for_loop_reference::demo);
    const auto got_for = run_and_capture(&m12_for_loop_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_for.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_for[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_for.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_for[i]).c_str());
    }
    if (ref_for.empty()) {
        std::fprintf(stderr,
                     "for_loop reference produced 0 gates — fixture not "
                     "exercising the Phase H for-loop OR lowering.\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_for, ref_for); rc != 0) {
        std::fprintf(stderr, "for_loop gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  for_loop streams match (%zu gates).\n", ref_for.size());

    // PI-7: Phase I user-routine gate-equivalence pair.  Proves the
    // transpiler's PI-2/PI-3/PI-4 rewrite — which injects an
    // `invert(<routine>)(...)` call at scope exit for a registered
    // forward routine with a local-intermediate output slot — emits
    // the same gate stream as a hand-written reference that spells
    // the forward + adjoint calls verbatim in LIFO order.  The
    // routine performs three `tmp.flip()` calls (three X gates),
    // followed by the adjoint (three more X gates), against a freshly
    // allocated `tmp` ancilla — expected stream length is six gates.
    std::printf("PI-7 gate-stream equivalence test (user_routine pattern):\n");
    const auto ref_ur = run_and_capture_user_routine(&m12_user_routine_reference::demo);
    const auto got_ur = run_and_capture_user_routine(&m12_user_routine_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_ur.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_ur[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_ur.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_ur[i]).c_str());
    }
    if (ref_ur.empty()) {
        std::fprintf(stderr,
                     "user_routine reference produced 0 gates — fixture not "
                     "exercising the Phase I invert() injection.\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_ur, ref_ur); rc != 0) {
        std::fprintf(stderr, "user_routine gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  user_routine streams match (%zu gates).\n", ref_ur.size());

    // PJ-1i: Phase J zero-ancilla fusion gate-equivalence pair.  Proves
    // the transpiler's PJ-1d peephole matcher + PJ-1c render case —
    // which collapses `qbool __t = a & b; x ^= __t;` into a single
    // `ccnot_inplace(x, a, b);` and plants a self-adjoint
    // `ccnot_inplace(x, a, b);` at scope close — emits the same two-
    // CCX gate stream as a hand-written reference that calls
    // `primitive_AND(ctx, a, b, x)` directly against the 18-gate
    // sink (bypassing the pre-K lazy_expr path, which would allocate
    // an intermediate ancilla and therefore produce a mismatched
    // stream).  Expected stream length is TWO CCX records on the
    // three caller-supplied qubit indices — no ancilla allocation on
    // either side, which is the defining invariant of the fusion.
    std::printf("PJ-1i gate-stream equivalence test (fuse_xor_and pattern):\n");
    const auto ref_fu = run_and_capture_fused(&m12_fused_reference::demo);
    const auto got_fu = run_and_capture_fused(&m12_fused_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_fu.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_fu[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_fu.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_fu[i]).c_str());
    }
    if (ref_fu.empty()) {
        std::fprintf(stderr,
                     "fuse_xor_and reference produced 0 gates — fixture not "
                     "exercising the Phase J PJ-1 zero-ancilla fusion.\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_fu, ref_fu); rc != 0) {
        std::fprintf(stderr, "fuse_xor_and gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  fuse_xor_and streams match (%zu gates).\n", ref_fu.size());

    // PJ-4e: Phase J dead-ancilla elimination gate-equivalence pair.
    // Proves the transpiler's PJ-4a peephole matcher + empty-text
    // `QReplacement` + `eliminated_stmt_ranges` backstop — which strips
    // `qbool dead = a | b;` (zero-reader condition) from the rewritten
    // source AND suppresses the downstream MVP OR matcher's per-decl
    // `uncompute_or(...)` injection — emits the same one-X gate stream
    // as a hand-written reference that simply OMITS the dead decl.
    // The reference's `target.flip()` sentinel emits exactly one X
    // record via emit_X_lifted; the transpiled demo's `target.flip()`
    // emits the same record, and the dead decl contributes ZERO gates
    // because PJ-4a strips it before the MVP OR matcher can anchor on
    // it.  Expected stream length is ONE X record on the caller-
    // supplied `target` qubit index — no ancilla allocation on either
    // side, which is the defining invariant of the PJ-4 elimination
    // (no ancilla allocated → no stray gates).
    std::printf("PJ-4e gate-stream equivalence test (dead_ancilla pattern):\n");
    const auto ref_da = run_and_capture_dead_ancilla(&m12_dead_ancilla_reference::demo);
    const auto got_da = run_and_capture_dead_ancilla(&m12_dead_ancilla_transpiled::demo);
    std::printf("  reference stream:\n");
    for (std::size_t i = 0; i < ref_da.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(ref_da[i]).c_str());
    }
    std::printf("  transpiled stream:\n");
    for (std::size_t i = 0; i < got_da.size(); ++i) {
        std::printf("    [%zu] %s\n", i, render_gate(got_da[i]).c_str());
    }
    if (ref_da.empty()) {
        std::fprintf(stderr,
                     "dead_ancilla reference produced 0 gates — fixture not "
                     "exercising the Phase J PJ-4 dead-ancilla elimination "
                     "(the sentinel target.flip() call is missing).\n");
        return 1;
    }
    if (int rc = assert_streams_equal(got_da, ref_da); rc != 0) {
        std::fprintf(stderr, "dead_ancilla gate-stream mismatch — see above.\n");
        return rc;
    }
    std::printf("  dead_ancilla streams match (%zu gates).\n", ref_da.size());

    std::printf("pair 1: %zu gates match\n", ref_stream.size());
    std::printf("pair 2: %zu gates match\n", ref_c.size());
    std::printf("pair 3: %zu gates match\n", ref_n.size());
    std::printf("pair 4: %zu gates match\n", ref_if.size());
    std::printf("pair 5: %zu gates match\n", ref_for.size());
    std::printf("pair 6: %zu gates match\n", ref_ur.size());
    std::printf("pair 7: %zu gates match\n", ref_fu.size());
    std::printf("pair 8: %zu gates match\n", ref_da.size());
    std::printf("PASS\n");
    return 0;
}
