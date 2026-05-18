// test_draw_mermaid.cpp — sturm-5soh / PRD §7 follow-up: tests for the
// debug-print Mermaid renderer `sturm::draw_mermaid` / `sturm::print_mermaid`.
//
// Stability contract (per the issue note, 2026-05-18): debug-print format.
// The emitted Mermaid markup may change freely between versions; no consumer
// outside the STURM tree should depend on it. Tests pin SHAPE and
// MACHINE-PARSEABILITY (well-formed `graph LR` envelope, expected text
// markers present) rather than bytewise golden output.
//
// Design defaults (resolved here, mirroring the draw_json / draw_svg
// siblings, 2026-05-18):
//   - Mermaid diagram type : `graph LR` (left-to-right flowchart). Renders
//                            natively in GitHub markdown — the highest-
//                            leverage choice for the three opt-in formats.
//                            stateDiagram / sequenceDiagram model state
//                            transitions / message-passing respectively,
//                            neither of which matches a gate timeline.
//   - Node naming scheme   : `g<gate_index>_q<qubit_index>` for gate nodes,
//                            `q<qubit_index>` for qubit-rail start nodes.
//                            The label encodes the gate's display name
//                            (e.g. "H", "X", "Rz(0.5)") inside the node
//                            shape — recoverable via text search.
//   - Measurement nodes    : N/A — the STURM IR has no measurement gate
//                            kind in this issue's scope (deferred to a
//                            future renderer pass when measurements land
//                            in the IR). All current gates are unitaries.
//   - Dependency           : pure stdlib (no templating library).
//
// Cases (mirroring test_draw_json.cpp / test_draw_svg.cpp + the
// test_draw_ascii_noarg.cpp death-test pattern):
//   1. empty IR (explicit overload)        → graph envelope, no gate nodes.
//   2. single 1-qubit gate                  → gate name surfaces (e.g. "H").
//   3. CX (2-qubit)                         → both control + target qubits.
//   4. CCX (3-qubit)                        → all three qubit indices.
//   5. parametric gate (RZ)                 → angle / param surfaces.
//   6. no-arg entry uses thread context.
//   7. no-arg empty thread context → still well-formed envelope.
//   8. null-context no-arg call aborts (death test, fork-based).
//   9. print_mermaid() smoke from a healthy context.

#include "sturm/backend/draw_mermaid.hpp"
#include "sturm/backend/ir.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// ── Helpers ──────────────────────────────────────────────────────────────────

static sturm::GateRecord mk(sturm_gate_kind_t k,
                            uint32_t q0, uint32_t q1, uint32_t q2,
                            uint8_t n, double p = 0.0) {
    sturm::GateRecord r{};
    r.kind = k;
    r.qubits = {q0, q1, q2};
    r.n = n;
    r.param = p;
    return r;
}

// Minimal Mermaid-shape validator: a `graph LR` document must START with
// the `graph LR` directive on its first non-blank line. The renderer is
// allowed to use leading whitespace for nested clarity, but the opening
// directive must be present.
static bool looks_like_mermaid_graph(const std::string& s) {
    if (s.empty()) return false;
    // Must contain "graph LR" near the top.
    std::size_t pos = s.find("graph LR");
    if (pos == std::string::npos) return false;
    // Everything before "graph LR" must be whitespace (allow shebang-style
    // comments-free preambles only). The renderer emits no preamble.
    for (std::size_t i = 0; i < pos; ++i) {
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') {
            return false;
        }
    }
    return true;
}

// ── Case 1: empty IR (explicit overload) ─────────────────────────────────────

static void test_empty_ir_explicit() {
    sturm::GateIR ir;
    std::string s = sturm::draw_mermaid(ir, 0);
    assert(looks_like_mermaid_graph(s) &&
           "even empty IR must produce a valid `graph LR` envelope");
    // No gate node IDs should appear (the renderer ids them g<i>_q<j>).
    assert(s.find("g0_") == std::string::npos &&
           "empty IR must not emit any gate nodes");
}

// ── Case 2: single 1-qubit gate ──────────────────────────────────────────────

static void test_single_h_gate() {
    sturm::GateIR ir;
    ir.append(mk(STURM_GATE_H, 0, 0, 0, 1));
    std::string s = sturm::draw_mermaid(ir, 1);
    assert(looks_like_mermaid_graph(s));
    // The gate name comes from sturm_gate_info_of(); for H that's "H".
    // We render the label inside a node shape "[H]" or similar.
    assert(s.find("H") != std::string::npos &&
           "single H gate must surface its name in the Mermaid output");
    // A gate node for the first gate on qubit 0 must exist.
    assert(s.find("g0_q0") != std::string::npos &&
           "single-gate IR must emit a `g0_q0` node id");
}

// ── Case 3: CX (2-qubit) ─────────────────────────────────────────────────────

static void test_cx_gate() {
    sturm::GateIR ir;
    ir.append(mk(STURM_GATE_CX, 1, 3, 0, 2));
    std::string s = sturm::draw_mermaid(ir, 4);
    assert(looks_like_mermaid_graph(s));
    // CX renders on the control (q1) AND the target (q3). Both qubit
    // lanes must appear in the gate node ids.
    assert(s.find("g0_q1") != std::string::npos &&
           "CX must emit a g0_q1 node for the control");
    assert(s.find("g0_q3") != std::string::npos &&
           "CX must emit a g0_q3 node for the target");
    // The CX renders as a "*" (control) label on q1 and an "X" (target)
    // label on q3. Both must appear inside Mermaid label brackets.
    assert(s.find("[\"*\"]") != std::string::npos &&
           "CX must emit a `*` label on its control qubit");
    assert(s.find("[\"X\"]") != std::string::npos &&
           "CX must emit an `X` label on its target qubit");
    // Multi-qubit gates link their per-qubit nodes with a dotted edge.
    assert(s.find("-.->") != std::string::npos &&
           "multi-qubit gates must link their per-qubit nodes with a "
           "dotted edge");
}

// ── Case 4: CCX (3-qubit) ────────────────────────────────────────────────────

static void test_ccx_gate() {
    sturm::GateIR ir;
    ir.append(mk(STURM_GATE_CCX, 0, 1, 2, 3));
    std::string s = sturm::draw_mermaid(ir, 3);
    assert(looks_like_mermaid_graph(s));
    // All three qubit nodes must appear.
    assert(s.find("g0_q0") != std::string::npos);
    assert(s.find("g0_q1") != std::string::npos);
    assert(s.find("g0_q2") != std::string::npos);
    // CCX renders as two control "*" labels (on the two control qubits)
    // plus a target "X" label on q2. Count occurrences of `["*"]` —
    // there must be at least two.
    std::size_t controls = 0;
    std::size_t pos = 0;
    while ((pos = s.find("[\"*\"]", pos)) != std::string::npos) {
        ++controls;
        ++pos;
    }
    assert(controls >= 2u &&
           "CCX must emit at least two `*` (control) labels");
    assert(s.find("[\"X\"]") != std::string::npos &&
           "CCX must emit an `X` (target) label");
}

// ── Case 5: parametric gate (RZ) — angle surfaces in label ───────────────────

static void test_rz_param_label() {
    sturm::GateIR ir;
    ir.append(mk(STURM_GATE_RZ, 0, 0, 0, 1, 0.5));
    std::string s = sturm::draw_mermaid(ir, 1);
    assert(looks_like_mermaid_graph(s));
    // The gate-name table reports "Rz" (see core/gate_kind.c).
    assert(s.find("Rz") != std::string::npos &&
           "RZ gate must render its name");
    // Parametric gates surface their angle in the label
    // ("Rz(0.5)" form so consumers can recover the parameter from text).
    assert(s.find("0.5") != std::string::npos &&
           "RZ(0.5) must surface its angle in the Mermaid output");
}

// ── Case 6: no-arg entry uses thread context ─────────────────────────────────

static void test_noarg_uses_thread_context() {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);

    ctx->ir.append(mk(STURM_GATE_X, 0, 0, 0, 1));
    ctx->ir.append(mk(STURM_GATE_CX, 0, 2, 0, 2));
    std::string s = sturm::draw_mermaid();
    assert(looks_like_mermaid_graph(s));
    // The first gate (X) is on q0; the second gate (CX) touches q0 and q2.
    assert(s.find("g0_q0") != std::string::npos);
    assert(s.find("g1_q0") != std::string::npos);
    assert(s.find("g1_q2") != std::string::npos);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
}

// ── Case 7: empty thread context → still emits well-formed envelope ──────────

static void test_noarg_empty_context() {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);

    std::string s = sturm::draw_mermaid();
    // Even for an empty IR the no-arg path returns a valid Mermaid document
    // (we choose to emit the bare `graph LR` envelope rather than the
    // empty string the ASCII renderer uses, so downstream tooling never
    // has to special-case "render returned empty").
    assert(looks_like_mermaid_graph(s));

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
}

// ── Case 8: null-context no-arg call aborts (death test) ─────────────────────

static void test_null_context_aborts() {
    pid_t pid = fork();
    if (pid < 0) { std::perror("fork"); std::abort(); }

    if (pid == 0) {
        sturm_set_thread_context(nullptr);
        std::freopen("/dev/null", "w", stderr);
        (void)sturm::draw_mermaid();
        _exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    bool died = false;
    if (WIFSIGNALED(status)) died = true;
    else if (WIFEXITED(status)) died = (WEXITSTATUS(status) != 0);
    assert(died && "draw_mermaid() with null thread context must abort");
}

// ── Case 9: print_mermaid() smoke from a healthy context ─────────────────────

static void test_print_mermaid_smoke() {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);
    ctx->ir.append(mk(STURM_GATE_H, 0, 0, 0, 1));

    std::string a = sturm::draw_mermaid();
    assert(looks_like_mermaid_graph(a));
    sturm::print_mermaid();  // smoke: must return without aborting.

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
}

// ── Runner ───────────────────────────────────────────────────────────────────

int main() {
    test_empty_ir_explicit();
    test_single_h_gate();
    test_cx_gate();
    test_ccx_gate();
    test_rz_param_label();
    test_noarg_uses_thread_context();
    test_noarg_empty_context();
    test_null_context_aborts();
    test_print_mermaid_smoke();
    std::puts("test_draw_mermaid: OK (9/9)");
    return 0;
}
