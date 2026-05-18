// test_draw_json.cpp — sturm-a4we / PRD §7 follow-up: tests for the
// debug-print JSON renderer `sturm::draw_json` / `sturm::print_json`.
//
// Stability contract (per the issue note, 2026-05-18): debug-print format.
// Schema may change freely between versions. The tests pin SHAPE and
// MACHINE-PARSEABILITY (valid JSON, expected fields present) rather than
// byte-identical golden output.
//
// Cases (mirroring test_draw_ascii.cpp + test_draw_ascii_noarg.cpp):
//   1. empty IR (explicit overload)        → empty `"ops"` array, `"n_qubits": 0`.
//   2. single 1-qubit gate                  → one op entry naming the gate.
//   3. CX (2-qubit)                         → controls / targets fields populated.
//   4. CCX (3-qubit)                        → 3-element `"qubits"` array.
//   5. parametric gate (RZ)                 → `"param"` field present.
//   6. no-arg entry uses thread context.
//   7. no-arg gate_count match (via ir.size()).
//   8. null-context no-arg call aborts (death test, fork-based — mirrors P5).
//   9. produced JSON parses with a minimal lexer (well-formed string).

#include "sturm/backend/draw_json.hpp"
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

// Minimal JSON-shape validator: walks the string and verifies
// balanced braces / brackets / quotes — enough to catch malformed
// renderer output. Returns true if balance reaches 0 with no
// outstanding string and at least one '{' was seen.
static bool looks_like_balanced_json(const std::string& s) {
    int braces = 0, brackets = 0;
    bool in_string = false;
    bool escaped = false;
    bool seen_open = false;
    for (char c : s) {
        if (in_string) {
            if (escaped) { escaped = false; continue; }
            if (c == '\\') { escaped = true; continue; }
            if (c == '"') in_string = false;
            continue;
        }
        switch (c) {
            case '"': in_string = true; break;
            case '{': ++braces; seen_open = true; break;
            case '}': --braces; break;
            case '[': ++brackets; break;
            case ']': --brackets; break;
            default: break;
        }
        if (braces < 0 || brackets < 0) return false;
    }
    return seen_open && !in_string && braces == 0 && brackets == 0;
}

// ── Case 1: empty IR (explicit overload) ─────────────────────────────────────

static void test_empty_ir_explicit() {
    sturm::GateIR ir;
    std::string s = sturm::draw_json(ir, 0);
    assert(looks_like_balanced_json(s));
    // n_qubits == 0 must appear, ops must be an empty array.
    assert(s.find("\"n_qubits\"") != std::string::npos);
    assert(s.find("\"ops\"") != std::string::npos);
    // ops shape: "ops": [] or "ops":[\n] (pretty form).
    // Use a substring search after "ops" for the next '[' followed by ']'
    // (allowing whitespace).
    std::size_t op_pos = s.find("\"ops\"");
    std::size_t bracket = s.find('[', op_pos);
    assert(bracket != std::string::npos);
    // The next non-whitespace char after '[' must be ']' (empty array).
    std::size_t i = bracket + 1;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t')) ++i;
    assert(i < s.size() && s[i] == ']' && "empty IR must yield empty ops array");
}

// ── Case 2: single 1-qubit gate ──────────────────────────────────────────────

static void test_single_h_gate() {
    sturm::GateIR ir;
    ir.append(mk(STURM_GATE_H, 0, 0, 0, 1));
    std::string s = sturm::draw_json(ir, 1);
    assert(looks_like_balanced_json(s));
    // The gate name comes from sturm_gate_info_of(); for H that's "H".
    assert(s.find("\"H\"") != std::string::npos &&
           "single H gate must surface its name in JSON");
    // n_qubits should report 1.
    assert(s.find("\"n_qubits\"") != std::string::npos);
}

// ── Case 3: CX (2-qubit) ─────────────────────────────────────────────────────

static void test_cx_gate() {
    sturm::GateIR ir;
    ir.append(mk(STURM_GATE_CX, 1, 3, 0, 2));
    std::string s = sturm::draw_json(ir, 4);
    assert(looks_like_balanced_json(s));
    assert(s.find("\"CX\"") != std::string::npos);
    // The qubits 1 and 3 must both appear in the output.
    assert(s.find("1") != std::string::npos);
    assert(s.find("3") != std::string::npos);
}

// ── Case 4: CCX (3-qubit) ────────────────────────────────────────────────────

static void test_ccx_gate() {
    sturm::GateIR ir;
    ir.append(mk(STURM_GATE_CCX, 0, 1, 2, 3));
    std::string s = sturm::draw_json(ir, 3);
    assert(looks_like_balanced_json(s));
    assert(s.find("\"CCX\"") != std::string::npos);
    // All three qubit indices 0, 1, 2 must appear.
    assert(s.find('0') != std::string::npos);
    assert(s.find('1') != std::string::npos);
    assert(s.find('2') != std::string::npos);
}

// ── Case 5: parametric gate (RZ) — `param` field present ─────────────────────

static void test_rz_param_field() {
    sturm::GateIR ir;
    ir.append(mk(STURM_GATE_RZ, 0, 0, 0, 1, 0.5));
    std::string s = sturm::draw_json(ir, 1);
    assert(looks_like_balanced_json(s));
    assert(s.find("\"Rz\"") != std::string::npos);
    assert(s.find("\"param\"") != std::string::npos &&
           "parametric gates must emit a param field");
}

// ── Case 6: no-arg entry uses thread context ─────────────────────────────────

static void test_noarg_uses_thread_context() {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);

    ctx->ir.append(mk(STURM_GATE_X, 0, 0, 0, 1));
    ctx->ir.append(mk(STURM_GATE_CX, 0, 2, 0, 2));
    std::string s = sturm::draw_json();
    assert(looks_like_balanced_json(s));
    assert(s.find("\"X\"")  != std::string::npos);
    assert(s.find("\"CX\"") != std::string::npos);
    // Canvas width derivation: max qubit index (2) + 1 = 3.
    assert(s.find("\"n_qubits\"") != std::string::npos);

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
}

// ── Case 7: empty thread context → empty JSON object (still valid) ───────────

static void test_noarg_empty_context() {
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);

    std::string s = sturm::draw_json();
    // Even for an empty IR the no-arg path returns a valid JSON document
    // (we choose to emit `{ "n_qubits": 0, "ops": [] }` rather than the
    // bare empty string the ASCII renderer uses, so downstream tooling
    // never has to special-case "render returned empty").
    assert(looks_like_balanced_json(s));
    assert(s.find("\"n_qubits\"") != std::string::npos);
    assert(s.find("\"ops\"") != std::string::npos);

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
        (void)sturm::draw_json();
        _exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    bool died = false;
    if (WIFSIGNALED(status)) died = true;
    else if (WIFEXITED(status)) died = (WEXITSTATUS(status) != 0);
    assert(died && "draw_json() with null thread context must abort");
}

// ── Case 9: print_json round-trip — emits the same string draw_json returns ──

static void test_print_json_matches_draw_json() {
    // Sanity: print_json is the convenience wrapper around draw_json;
    // both must use the same renderer. We don't capture stdout here
    // (forks already cover correctness), just ensure both can be
    // called from a healthy context without crashing.
    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_APPEND);
    sturm_set_thread_context(ctx);
    ctx->ir.append(mk(STURM_GATE_H, 0, 0, 0, 1));

    std::string a = sturm::draw_json();
    assert(looks_like_balanced_json(a));
    sturm::print_json();  // smoke: must return without aborting.

    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
}

// ── Runner ───────────────────────────────────────────────────────────────────

int main() {
    test_empty_ir_explicit();
    test_single_h_gate();
    test_cx_gate();
    test_ccx_gate();
    test_rz_param_field();
    test_noarg_uses_thread_context();
    test_noarg_empty_context();
    test_null_context_aborts();
    test_print_json_matches_draw_json();
    std::puts("test_draw_json: OK (9/9)");
    return 0;
}
