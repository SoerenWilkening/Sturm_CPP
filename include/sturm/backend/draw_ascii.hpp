// draw_ascii.hpp — ASCII-art renderer for a GateIR.
//
// Produces a simple multi-line diagram of a stored circuit. One row per
// qubit, one column per gate. Suitable for debugging the small circuits
// used in tests and the APPEND-mode examples.
//
// Layout rules:
//   - 1-qubit gate: gate name on the target row.
//   - CX/CY/CZ:     "*" on control, target letter (X/Y/Z) on target.
//   - CRX/CRY/CRZ:  "*" on control, "RX"/"RY"/"RZ" on target.
//   - CCX:          "*" on both controls, "X" on target.
//   - SWAP:         "x" on both qubits.
//   - Rows strictly between the involved qubits get a centered "|".
//
// Two flavours of entry point:
//   - draw_ascii(ir, n_qubits)  — explicit form, header-only (inline).
//   - draw_ascii() / print_ascii() / gate_count()  — no-argument form
//     declared here, defined in src/sturm/backend/draw_ascii_noarg.cpp.
//     They query the calling thread's installed BackendContext (PRD §5.6).

#pragma once

#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sturm {

// ── No-argument entry points (PRD §5.6) ──────────────────────────────────────
//
// These operate on the thread-local BackendContext installed via
// sturm_set_thread_context (typically by the auto-injected lifecycle).
// All three assert that a per-thread context is currently installed —
// calling them outside a lifecycle scope is a programming error.
//
// Canvas-width derivation: scan the IR for the maximum referenced qubit
// index; rail count = max + 1. An empty IR yields an empty string (locks
// the PRD §5.6 ambiguity per docs/impl_plan_frontend_simplification.md
// §10).

std::string draw_ascii();
void        print_ascii();
std::size_t gate_count();

inline std::string draw_ascii(const GateIR& ir, std::size_t n_qubits) {
    std::vector<std::string> rows(n_qubits);
    for (std::size_t i = 0; i < n_qubits; ++i) {
        rows[i] = "q" + std::to_string(i) + ": ";
    }
    std::size_t hdr_w = 0;
    for (const auto& r : rows) hdr_w = std::max(hdr_w, r.size());
    for (auto& r : rows) r.resize(hdr_w, ' ');

    for (std::size_t g = 0; g < ir.size(); ++g) {
        const GateRecord& rec = ir.at(g);

        // Build (qubit, label) slots for this gate.
        std::array<std::pair<uint32_t, std::string>, 3> slots{};
        std::size_t ns = 0;
        auto push = [&](uint32_t q, const char* s) {
            slots[ns++] = {q, std::string(s)};
        };
        switch (rec.kind) {
            case STURM_GATE_CX:   push(rec.qubits[0], "*"); push(rec.qubits[1], "X"); break;
            case STURM_GATE_CY:   push(rec.qubits[0], "*"); push(rec.qubits[1], "Y"); break;
            case STURM_GATE_CZ:   push(rec.qubits[0], "*"); push(rec.qubits[1], "Z"); break;
            case STURM_GATE_CRX:  push(rec.qubits[0], "*"); push(rec.qubits[1], "RX"); break;
            case STURM_GATE_CRY:  push(rec.qubits[0], "*"); push(rec.qubits[1], "RY"); break;
            case STURM_GATE_CRZ:  push(rec.qubits[0], "*"); push(rec.qubits[1], "RZ"); break;
            case STURM_GATE_CCX:  push(rec.qubits[0], "*"); push(rec.qubits[1], "*"); push(rec.qubits[2], "X"); break;
            case STURM_GATE_SWAP: push(rec.qubits[0], "x"); push(rec.qubits[1], "x"); break;
            default: {
                const sturm_gate_info_t* info = sturm_gate_info_of(rec.kind);
                push(rec.qubits[0], (info && info->name) ? info->name : "?");
                break;
            }
        }

        std::size_t lab_w = 1;
        uint32_t qmin = UINT32_MAX, qmax = 0;
        for (std::size_t i = 0; i < ns; ++i) {
            lab_w = std::max(lab_w, slots[i].second.size());
            if (slots[i].first < qmin) qmin = slots[i].first;
            if (slots[i].first > qmax) qmax = slots[i].first;
        }

        for (std::size_t r = 0; r < n_qubits; ++r) {
            std::string lab;
            bool involved = false;
            for (std::size_t i = 0; i < ns; ++i) {
                if (slots[i].first == r) { lab = slots[i].second; involved = true; break; }
            }
            if (!involved && r >= qmin && r <= qmax) {
                lab = "|";
            }
            rows[r].push_back('-');
            if (lab.empty()) {
                rows[r].append(lab_w, '-');
            } else {
                std::size_t pad  = lab_w - lab.size();
                std::size_t left = pad / 2;
                rows[r].append(left, '-');
                rows[r].append(lab);
                rows[r].append(pad - left, '-');
            }
            rows[r].push_back('-');
        }
    }

    std::string out;
    for (const auto& r : rows) { out += r; out += '\n'; }
    return out;
}

} // namespace sturm
