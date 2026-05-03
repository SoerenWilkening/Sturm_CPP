#pragma once
// sink.hpp — Abstract Sink interface + Record + ScopedSink (Step 2, spec §1.2)
// Thread-local current_sink backed by the default CounterSink.

#include <string>
#include <vector>

namespace sturm {

// ── Record ────────────────────────────────────────────────────────────────────
// Captured by RecordingSink for test assertions.
struct Record {
    std::string              op;
    std::vector<std::vector<int>> qubit_groups;  // per-operand qubit index lists
    std::vector<double>      scalars;            // rotation angle, prepare p, etc.
    int                      control;            // -1 = no active control
};

// ── Abstract Sink ─────────────────────────────────────────────────────────────
// One virtual method per named quantum op (spec §10).
// Binary arithmetic/bitwise: (a qubits, b qubits, control).
// Unary: (a qubits, control).
// Compare: (a qubits, b qubits, result qubit, control).
// Rotation: (qubit, delta, control).
// Prepare: (qubit, probability).
struct Sink {
    virtual ~Sink() = default;

    // ── Arithmetic ────────────────────────────────────────────────────────
    virtual void quantum_add(const std::vector<int>& a,
                             const std::vector<int>& b,
                             int control) = 0;
    virtual void quantum_sub(const std::vector<int>& a,
                             const std::vector<int>& b,
                             int control) = 0;
    virtual void quantum_mul(const std::vector<int>& a,
                             const std::vector<int>& b,
                             int control) = 0;
    virtual void quantum_div(const std::vector<int>& a,
                             const std::vector<int>& b,
                             int control) = 0;
    virtual void quantum_mod(const std::vector<int>& a,
                             const std::vector<int>& b,
                             int control) = 0;
    virtual void quantum_pow(const std::vector<int>& a,
                             const std::vector<int>& b,
                             int control) = 0;

    // ── Bitwise ──────────────────────────────────────────────────────────
    virtual void quantum_xor(const std::vector<int>& a,
                             const std::vector<int>& b,
                             int control) = 0;
    virtual void quantum_and(const std::vector<int>& a,
                             const std::vector<int>& b,
                             int control) = 0;
    virtual void quantum_or (const std::vector<int>& a,
                             const std::vector<int>& b,
                             int control) = 0;
    virtual void quantum_not(const std::vector<int>& a,
                             int control) = 0;

    // ── Shifts ────────────────────────────────────────────────────────────
    virtual void quantum_shl(const std::vector<int>& a,
                             const std::vector<int>& b,
                             int control) = 0;
    virtual void quantum_shr(const std::vector<int>& a,
                             const std::vector<int>& b,
                             int control) = 0;

    // ── Compare (result qubit is the allocated qbool qubit) ───────────────
    virtual void quantum_eq (const std::vector<int>& a,
                             const std::vector<int>& b,
                             int result_qubit, int control) = 0;
    virtual void quantum_neq(const std::vector<int>& a,
                             const std::vector<int>& b,
                             int result_qubit, int control) = 0;
    virtual void quantum_lt (const std::vector<int>& a,
                             const std::vector<int>& b,
                             int result_qubit, int control) = 0;
    virtual void quantum_le (const std::vector<int>& a,
                             const std::vector<int>& b,
                             int result_qubit, int control) = 0;
    virtual void quantum_gt (const std::vector<int>& a,
                             const std::vector<int>& b,
                             int result_qubit, int control) = 0;
    virtual void quantum_ge (const std::vector<int>& a,
                             const std::vector<int>& b,
                             int result_qubit, int control) = 0;

    // ── Rotations / preparation ───────────────────────────────────────────
    virtual void theta_add(int qubit, double delta, int control) = 0;
    virtual void phi_add  (int qubit, double delta, int control) = 0;
    virtual void prepare  (int qubit, double p) = 0;

    // ── QRAM (sturm-u9ge.13 / Beat D1) ────────────────────────────────────
    // Counter-mode hook for `sturm::QRAM_read` (PRD §11.2.7). The body in
    // `src/qram/qram_read.cpp` calls `current_sink()->qram_read()` once
    // per dispatched read; in counter mode CounterSink overrides this to
    // bump a `qram_read` counter (default sink). The base method is a
    // non-pure-virtual no-op so existing Sink subclasses (RecordingSink,
    // any user implementation) keep compiling without modification —
    // adding a pure virtual would be a hard ABI break. Per-path
    // (`qrom_read` / `qreg_read`) counters land with gate emission.
    virtual void qram_read() {}

    // ── QRAM split telemetry (sturm-2w6h.1 / Beat B0) ─────────────────────
    // Per-path counters for QRAM dispatch (PRD `docs/prd_qram_backend.md`,
    // plan `docs/plan_qram_backend.md` §5 B0 / §3.2). The QROM helper
    // (`_qram_detail::qram_read_qrom_impl`) will fire `qrom_read()` and
    // the qreg helper (`_qram_detail::qram_read_qreg_impl`) will fire
    // `qreg_read()` in B4 (sturm-2w6h.6) — the umbrella `qram_read()`
    // hook above continues to fire once per dispatched read so the
    // existing `tests/qram/test_qram_read_stub.cpp` and
    // `transpiler/tests/test_qram_e2e.cpp` keep their counter
    // assertions. This beat lands the surface only; the helpers do
    // not call these hooks yet.
    //
    // Both methods are non-pure-virtual no-ops so any existing Sink
    // subclass (in-tree or downstream) compiles unchanged — same ABI
    // discipline as the umbrella hook above.
    virtual void qrom_read() {}
    virtual void qreg_read() {}
};

// ── Thread-local sink registry ────────────────────────────────────────────────
// Forward declaration; definition lives in counter_sink.hpp (which provides
// the default CounterSink). Including counter_sink.hpp after this header is
// the correct include order. Tests that only need Sink/ScopedSink but not
// CounterSink must still include counter_sink.hpp to bring in the definition
// of current_sink() / set_current_sink().
Sink* current_sink();
void  set_current_sink(Sink* s);

// ── ScopedSink ────────────────────────────────────────────────────────────────
// RAII installer: saves the previous sink on construction and restores it on
// destruction. Typical test usage:
//   RecordingSink rs;
//   ScopedSink scope(&rs);
//   ... call ops ...
//   assert(rs.records()...);
struct ScopedSink {
    Sink* prev;
    explicit ScopedSink(Sink* s) : prev(current_sink()) {
        set_current_sink(s);
    }
    ~ScopedSink() {
        set_current_sink(prev);
    }
    // Non-copyable, non-movable.
    ScopedSink(const ScopedSink&)            = delete;
    ScopedSink& operator=(const ScopedSink&) = delete;
};

} // namespace sturm
