#pragma once
// recording_sink.hpp — RecordingSink: appends Record objects for test assertions
// (Step 2, spec §1.3).

#include "sturm/core/sink.hpp"

#include <vector>

namespace sturm {

// ── RecordingSink ─────────────────────────────────────────────────────────────
// Overrides every Sink method by appending a Record to an internal vector.
// Access via records() / clear().
class RecordingSink : public Sink {
public:
    // ── Arithmetic ────────────────────────────────────────────────────────
    void quantum_add(const std::vector<int>& a, const std::vector<int>& b, int ctrl) override {
        push_binary("quantum_add", a, b, ctrl);
    }
    void quantum_sub(const std::vector<int>& a, const std::vector<int>& b, int ctrl) override {
        push_binary("quantum_sub", a, b, ctrl);
    }
    void quantum_mul(const std::vector<int>& a, const std::vector<int>& b, int ctrl) override {
        push_binary("quantum_mul", a, b, ctrl);
    }
    void quantum_div(const std::vector<int>& a, const std::vector<int>& b, int ctrl) override {
        push_binary("quantum_div", a, b, ctrl);
    }
    void quantum_mod(const std::vector<int>& a, const std::vector<int>& b, int ctrl) override {
        push_binary("quantum_mod", a, b, ctrl);
    }
    void quantum_pow(const std::vector<int>& a, const std::vector<int>& b, int ctrl) override {
        push_binary("quantum_pow", a, b, ctrl);
    }

    // ── Bitwise ──────────────────────────────────────────────────────────
    void quantum_xor(const std::vector<int>& a, const std::vector<int>& b, int ctrl) override {
        push_binary("quantum_xor", a, b, ctrl);
    }
    void quantum_and(const std::vector<int>& a, const std::vector<int>& b, int ctrl) override {
        push_binary("quantum_and", a, b, ctrl);
    }
    void quantum_or(const std::vector<int>& a, const std::vector<int>& b, int ctrl) override {
        push_binary("quantum_or", a, b, ctrl);
    }
    void quantum_not(const std::vector<int>& a, int ctrl) override {
        Record r;
        r.op = "quantum_not";
        r.qubit_groups.push_back(a);
        r.control = ctrl;
        records_.push_back(std::move(r));
    }

    // ── Shifts ────────────────────────────────────────────────────────────
    void quantum_shl(const std::vector<int>& a, const std::vector<int>& b, int ctrl) override {
        push_binary("quantum_shl", a, b, ctrl);
    }
    void quantum_shr(const std::vector<int>& a, const std::vector<int>& b, int ctrl) override {
        push_binary("quantum_shr", a, b, ctrl);
    }

    // ── Compare ───────────────────────────────────────────────────────────
    void quantum_eq (const std::vector<int>& a, const std::vector<int>& b, int rq, int ctrl) override {
        push_compare("quantum_eq", a, b, rq, ctrl);
    }
    void quantum_neq(const std::vector<int>& a, const std::vector<int>& b, int rq, int ctrl) override {
        push_compare("quantum_neq", a, b, rq, ctrl);
    }
    void quantum_lt (const std::vector<int>& a, const std::vector<int>& b, int rq, int ctrl) override {
        push_compare("quantum_lt", a, b, rq, ctrl);
    }
    void quantum_le (const std::vector<int>& a, const std::vector<int>& b, int rq, int ctrl) override {
        push_compare("quantum_le", a, b, rq, ctrl);
    }
    void quantum_gt (const std::vector<int>& a, const std::vector<int>& b, int rq, int ctrl) override {
        push_compare("quantum_gt", a, b, rq, ctrl);
    }
    void quantum_ge (const std::vector<int>& a, const std::vector<int>& b, int rq, int ctrl) override {
        push_compare("quantum_ge", a, b, rq, ctrl);
    }

    // ── QRAM split telemetry (sturm-2w6h.1 / Beat B0) ─────────────────────
    // Overrides for the new `Sink::qrom_read()` / `qreg_read()` hooks
    // (PRD `docs/prd_qram_backend.md`, plan
    // `docs/plan_qram_backend.md` §5 B0). Each appends a single
    // `Record{op="qrom_read"|"qreg_read"}` with no qubit groups, no
    // scalars, and `control == -1` — telemetry markers, not gate
    // primitives. Tests under `tests/qram/` and `tests/lib/` use
    // these to pin which dispatch path fired alongside the gate
    // record stream (plan §3.2). The umbrella `qram_read()` hook is
    // intentionally not overridden here so the existing recording-
    // sink consumers (which do not expect a record per dispatched
    // read) keep their current contract.
    void qrom_read() override {
        Record r;
        r.op = "qrom_read";
        r.control = -1;
        records_.push_back(std::move(r));
    }
    void qreg_read() override {
        Record r;
        r.op = "qreg_read";
        r.control = -1;
        records_.push_back(std::move(r));
    }

    // ── Rotations / preparation ───────────────────────────────────────────
    void theta_add(int qubit, double delta, int ctrl) override {
        Record r;
        r.op = "theta_add";
        r.qubit_groups.push_back({qubit});
        r.scalars.push_back(delta);
        r.control = ctrl;
        records_.push_back(std::move(r));
    }
    void phi_add(int qubit, double delta, int ctrl) override {
        Record r;
        r.op = "phi_add";
        r.qubit_groups.push_back({qubit});
        r.scalars.push_back(delta);
        r.control = ctrl;
        records_.push_back(std::move(r));
    }
    void prepare(int qubit, double p) override {
        Record r;
        r.op = "prepare";
        r.qubit_groups.push_back({qubit});
        r.scalars.push_back(p);
        r.control = -1;
        records_.push_back(std::move(r));
    }

    // ── Accessors ─────────────────────────────────────────────────────────
    const std::vector<Record>& records() const { return records_; }
    void clear() { records_.clear(); }

private:
    std::vector<Record> records_;

    void push_binary(const char* op,
                     const std::vector<int>& a,
                     const std::vector<int>& b,
                     int ctrl) {
        Record r;
        r.op = op;
        r.qubit_groups.push_back(a);
        r.qubit_groups.push_back(b);
        r.control = ctrl;
        records_.push_back(std::move(r));
    }

    void push_compare(const char* op,
                      const std::vector<int>& a,
                      const std::vector<int>& b,
                      int result_qubit,
                      int ctrl) {
        Record r;
        r.op = op;
        r.qubit_groups.push_back(a);
        r.qubit_groups.push_back(b);
        r.qubit_groups.push_back({result_qubit});
        r.control = ctrl;
        records_.push_back(std::move(r));
    }
};

} // namespace sturm
