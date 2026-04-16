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

    std::printf("pair 1: %zu gates match\n", ref_stream.size());
    std::printf("pair 2: %zu gates match\n", ref_c.size());
    std::printf("pair 3: %zu gates match\n", ref_n.size());
    std::printf("PASS\n");
    return 0;
}
