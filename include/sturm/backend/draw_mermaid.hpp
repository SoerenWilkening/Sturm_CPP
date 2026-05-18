// draw_mermaid.hpp — Debug-print Mermaid renderer for a GateIR
// (sturm-5soh).
//
// Stability: debug-print format. The emitted Mermaid markup may change
// freely between versions. Suitable for figure / GitHub-markdown
// generation (Mermaid renders natively in GitHub PR diffs and design
// docs), NOT interchange.
//
// Design defaults (resolved per the issue's open questions, 2026-05-18):
//   - Diagram type   : `graph LR` (left-to-right flowchart). Renders
//                      natively in GitHub markdown — the highest-leverage
//                      of the three opt-in formats. `stateDiagram` and
//                      `sequenceDiagram` model state transitions and
//                      message-passing respectively, neither of which
//                      matches a gate timeline; `graph LR` lays out one
//                      qubit per "lane" with gates as nodes flowing
//                      left-to-right, mirroring the ASCII / SVG layout.
//   - Node naming    : `g<gate_index>_q<qubit_index>` per (gate, qubit)
//                      pair (uniqueness guarantee). Qubit-rail start
//                      nodes use `q<qubit_index>`. Labels carry the
//                      display name (`H`, `X`, `Rz(0.5)`, …) so the
//                      parameter survives text round-tripping.
//   - Measurement    : N/A — the STURM IR has no measurement gate kind
//                      in this issue's scope (deferred to a future
//                      renderer pass when / if measurements land in
//                      the IR). All current gates are unitaries; the
//                      open question is resolved by deferral.
//   - Dependency     : pure stdlib (snprintf + std::string).
//
// Output shape (debug-print only, illustrative):
//
//   graph LR
//       q0(("q0"))
//       q1(("q1"))
//       q2(("q2"))
//       q0 --> g0_q0["H"]
//       q1 --> g1_q1["*"]
//       g1_q1 -.-> g1_q2["X"]
//       q2 --> g1_q2
//
// Layout rules (mirrors draw_ascii.hpp / draw_svg.hpp):
//   - 1-qubit gate : node with the gate name as the label.
//   - CX/CY/CZ    : control node labeled "*", target labeled "X"/"Y"/"Z".
//   - CRX/CRY/CRZ : control node labeled "*", target labeled "CRx" etc.
//                   plus an inline angle annotation on the target.
//   - CCX         : two control nodes labeled "*", target labeled "X".
//   - SWAP        : both endpoints labeled "x".
//   - Parametric 1-qubit gates surface their angle in the label form
//     "Rz(0.5)" so consumers can recover the parameter from text alone.
//   - Multi-qubit gates link their (gate, qubit) nodes with a dotted
//     vertical-style edge (`-.->`) to convey the "same gate spans these
//     qubits" relationship without implying temporal flow between them.
//
// Two flavours of entry point (parallel to draw_ascii / draw_json /
// draw_svg):
//   - draw_mermaid(ir, n_qubits)  — explicit form, header-only (inline).
//   - draw_mermaid() / print_mermaid() — no-argument form declared here,
//     defined in src/sturm/backend/draw_mermaid.cpp.

#pragma once

#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace sturm {

// No-arg entry points (assert on null thread context — mirror draw_json /
// draw_svg).
std::string draw_mermaid();
void        print_mermaid();

namespace detail {

// Escape `s` for safe inclusion inside a Mermaid node label.  Mermaid
// node labels are quoted strings inside square brackets / double parens,
// so `"`, `\`, `[`, `]`, `(`, `)`, and the line terminators must be
// neutralised.  Gate names today are plain ASCII letters; the escaping
// is defensive against future name changes.
inline void mermaid_append_escaped_label(std::string& out, const char* s) {
    if (!s) return;
    for (; *s; ++s) {
        unsigned char c = static_cast<unsigned char>(*s);
        switch (c) {
            case '"':  out.append("&quot;"); break;
            case '\\': out.append("&#92;");  break;
            case '[':  out.append("&#91;");  break;
            case ']':  out.append("&#93;");  break;
            case '(':  out.append("&#40;");  break;
            case ')':  out.append("&#41;");  break;
            case '<':  out.append("&lt;");   break;
            case '>':  out.append("&gt;");   break;
            case '\n': out.append(" ");      break;
            case '\r': out.append(" ");      break;
            default:
                if (c < 0x20) {
                    // Control characters are dropped — they have no
                    // meaning inside a Mermaid label.
                    out.push_back(' ');
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
}

inline void mermaid_append_double(std::string& out, double v) {
    char buf[64];
    int n = std::snprintf(buf, sizeof(buf), "%g", v);
    if (n > 0 && static_cast<std::size_t>(n) < sizeof(buf)) {
        out.append(buf, static_cast<std::size_t>(n));
    } else {
        out.append("0");
    }
}

inline void mermaid_append_uint(std::string& out, unsigned long v) {
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%lu", v);
    if (n > 0 && static_cast<std::size_t>(n) < sizeof(buf)) {
        out.append(buf, static_cast<std::size_t>(n));
    }
}

inline bool gate_is_parametric(sturm_gate_kind_t k) noexcept {
    switch (k) {
        case STURM_GATE_P:   case STURM_GATE_RX:  case STURM_GATE_RY:
        case STURM_GATE_RZ:  case STURM_GATE_CRX: case STURM_GATE_CRY:
        case STURM_GATE_CRZ: return true;
        default: return false;
    }
}

// Compute the (qubit, label) slots for a gate. Mirrors draw_svg.hpp.
inline std::size_t mermaid_gate_slots(
        const GateRecord& rec,
        std::array<std::pair<uint32_t, std::string>, 3>& slots) {
    std::size_t ns = 0;
    auto push = [&](uint32_t q, const char* s) {
        slots[ns++] = {q, std::string(s)};
    };
    switch (rec.kind) {
        case STURM_GATE_CX:   push(rec.qubits[0], "*"); push(rec.qubits[1], "X"); break;
        case STURM_GATE_CY:   push(rec.qubits[0], "*"); push(rec.qubits[1], "Y"); break;
        case STURM_GATE_CZ:   push(rec.qubits[0], "*"); push(rec.qubits[1], "Z"); break;
        case STURM_GATE_CRX:  push(rec.qubits[0], "*"); push(rec.qubits[1], "Rx"); break;
        case STURM_GATE_CRY:  push(rec.qubits[0], "*"); push(rec.qubits[1], "Ry"); break;
        case STURM_GATE_CRZ:  push(rec.qubits[0], "*"); push(rec.qubits[1], "Rz"); break;
        case STURM_GATE_CCX:  push(rec.qubits[0], "*"); push(rec.qubits[1], "*"); push(rec.qubits[2], "X"); break;
        case STURM_GATE_SWAP: push(rec.qubits[0], "x"); push(rec.qubits[1], "x"); break;
        default: {
            const sturm_gate_info_t* info = sturm_gate_info_of(rec.kind);
            push(rec.qubits[0], (info && info->name) ? info->name : "?");
            break;
        }
    }
    return ns;
}

} // namespace detail

// ── Inline IR-taking overload ────────────────────────────────────────────────
//
// Header-only so callers can render an arbitrary GateIR without dragging
// in the no-arg TU. Matches the posture of `draw_json(ir, n)` /
// `draw_svg(ir, n)`.

inline std::string draw_mermaid(const GateIR& ir, std::size_t n_qubits) {
    using namespace detail;
    std::string out;
    out.reserve(64 + n_qubits * 24 + ir.size() * 96);

    // Mermaid envelope.
    out.append("graph LR\n");

    // Qubit-rail start nodes — emitted as "q<i>((\"q<i>\"))" so the
    // circular shape distinguishes rails from gate nodes (which use
    // rectangular `[label]` shape). One node per qubit lane.
    for (std::size_t q = 0; q < n_qubits; ++q) {
        out.append("    q");
        mermaid_append_uint(out, static_cast<unsigned long>(q));
        out.append("((\"q");
        mermaid_append_uint(out, static_cast<unsigned long>(q));
        out.append("\"))\n");
    }

    // Track the most-recent (gate, qubit) node id along each qubit lane
    // so we can chain edges forward when the next gate touches that
    // qubit. Index 0 means "no gate yet — start from the qubit-rail
    // node q<j>".  Each entry stores the gate-index that last touched
    // qubit `j`, or std::size_t(-1) for "untouched so far".
    std::vector<std::size_t> last_gate(n_qubits, static_cast<std::size_t>(-1));

    for (std::size_t g = 0; g < ir.size(); ++g) {
        const GateRecord& rec = ir.at(g);
        std::array<std::pair<uint32_t, std::string>, 3> slots{};
        std::size_t ns = mermaid_gate_slots(rec, slots);
        if (ns == 0) continue;

        // 1. Emit one node per (gate, qubit) pair: `g<g>_q<q>["label"]`.
        for (std::size_t i = 0; i < ns; ++i) {
            const uint32_t q = slots[i].first;
            const std::string& lab = slots[i].second;

            // Compose displayed label; parametric 1-qubit gates include
            // the angle inline so it round-trips through text search.
            std::string disp = lab;
            if (gate_is_parametric(rec.kind) && ns == 1) {
                disp.push_back('(');
                char buf[64];
                int n = std::snprintf(buf, sizeof(buf), "%g", rec.param);
                if (n > 0) disp.append(buf, static_cast<std::size_t>(n));
                disp.push_back(')');
            }

            out.append("    g");
            mermaid_append_uint(out, static_cast<unsigned long>(g));
            out.append("_q");
            mermaid_append_uint(out, static_cast<unsigned long>(q));
            out.append("[\"");
            mermaid_append_escaped_label(out, disp.c_str());
            // For controlled parametric gates we also surface the angle
            // in the target's label so the parameter round-trips.
            if (gate_is_parametric(rec.kind) && ns > 1 && lab != "*") {
                out.push_back('(');
                mermaid_append_double(out, rec.param);
                out.push_back(')');
            }
            out.append("\"]\n");
        }

        // 2. Chain incoming edges from the previous occupant of each
        // affected qubit.  Empty qubit ⇒ edge from the rail start node
        // `q<j>`; otherwise from the previous gate's node on that lane.
        for (std::size_t i = 0; i < ns; ++i) {
            const uint32_t q = slots[i].first;
            out.append("    ");
            const std::size_t prev = (q < last_gate.size())
                                   ? last_gate[q]
                                   : static_cast<std::size_t>(-1);
            if (prev == static_cast<std::size_t>(-1)) {
                out.push_back('q');
                mermaid_append_uint(out, static_cast<unsigned long>(q));
            } else {
                out.append("g");
                mermaid_append_uint(out, static_cast<unsigned long>(prev));
                out.append("_q");
                mermaid_append_uint(out, static_cast<unsigned long>(q));
            }
            out.append(" --> g");
            mermaid_append_uint(out, static_cast<unsigned long>(g));
            out.append("_q");
            mermaid_append_uint(out, static_cast<unsigned long>(q));
            out.push_back('\n');
        }

        // 3. For multi-qubit gates, link the (gate, qubit) nodes among
        // themselves with a dotted edge to convey the "same gate spans
        // these qubits" relationship.  Use the first slot as the anchor
        // and dotted-edge to each subsequent slot.
        if (ns > 1) {
            for (std::size_t i = 1; i < ns; ++i) {
                const uint32_t q0 = slots[0].first;
                const uint32_t qi = slots[i].first;
                out.append("    g");
                mermaid_append_uint(out, static_cast<unsigned long>(g));
                out.append("_q");
                mermaid_append_uint(out, static_cast<unsigned long>(q0));
                out.append(" -.-> g");
                mermaid_append_uint(out, static_cast<unsigned long>(g));
                out.append("_q");
                mermaid_append_uint(out, static_cast<unsigned long>(qi));
                out.push_back('\n');
            }
        }

        // 4. Update last-gate trackers for each affected qubit.
        for (std::size_t i = 0; i < ns; ++i) {
            const uint32_t q = slots[i].first;
            if (q < last_gate.size()) {
                last_gate[q] = g;
            }
        }
    }

    return out;
}

} // namespace sturm
