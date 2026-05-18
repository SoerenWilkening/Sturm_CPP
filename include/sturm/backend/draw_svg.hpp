// draw_svg.hpp — Debug-print SVG renderer for a GateIR (sturm-l43b).
//
// Stability: debug-print format. The emitted SVG markup may change freely
// between versions. Suitable for figure / slide generation, NOT interchange.
//
// Design defaults (resolved per the issue's open questions, 2026-05-18):
//   - Layout      : manual grid — one column per gate (timestep),
//                   one row per qubit. No external layout pass.
//   - Styling     : inline <style> block with CSS classes
//                   (.rail, .gate, .gate-label, .control, .vline,
//                    .qlabel, .param-label). Light-mode default.
//   - Output size : viewBox sized to gate × qubit grid.
//   - Dependency  : pure stdlib (snprintf + std::string).
//
// Granularity: gate-level (one <rect>+<text> per IR record).
//
// Layout rules (mirrors draw_ascii.hpp):
//   - 1-qubit gate: gate name in a rect on the target row.
//   - CX/CY/CZ:     control dot on control, "X"/"Y"/"Z" rect on target,
//                   vertical line between.
//   - CRX/CRY/CRZ:  control dot + "Rx"/"Ry"/"Rz" rect + small angle label.
//   - CCX:          two control dots + "X" rect on target.
//   - SWAP:         "x" markers on both qubits.
//   - Parametric 1-qubit gates surface their angle in the label form
//     "Rz(0.5)" so consumers can recover the parameter from text alone.
//
// Two flavours of entry point (parallel to draw_ascii / draw_json):
//   - draw_svg(ir, n_qubits)  — explicit form, header-only (inline).
//   - draw_svg() / print_svg() — no-argument form declared here, defined
//     in src/sturm/backend/draw_svg.cpp.

#pragma once

#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace sturm {

// No-arg entry points (assert on null thread context — mirror draw_json).
std::string draw_svg();
void        print_svg();

namespace detail {

// XML-escape `s` into `out`. We escape the full set (&<>"') for safety
// even though current gate names are pure ASCII letters.
inline void svg_append_escaped(std::string& out, const char* s) {
    if (!s) return;
    for (; *s; ++s) {
        unsigned char c = static_cast<unsigned char>(*s);
        switch (c) {
            case '&':  out.append("&amp;");  break;
            case '<':  out.append("&lt;");   break;
            case '>':  out.append("&gt;");   break;
            case '"':  out.append("&quot;"); break;
            case '\'': out.append("&apos;"); break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "&#x%02x;", c);
                    out.append(buf);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
}

inline void svg_append_double(std::string& out, double v) {
    char buf[64];
    int n = std::snprintf(buf, sizeof(buf), "%g", v);
    if (n > 0 && static_cast<std::size_t>(n) < sizeof(buf)) {
        out.append(buf, static_cast<std::size_t>(n));
    } else {
        out.append("0");
    }
}

inline void svg_append_uint(std::string& out, unsigned long v) {
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%lu", v);
    if (n > 0 && static_cast<std::size_t>(n) < sizeof(buf)) {
        out.append(buf, static_cast<std::size_t>(n));
    }
}

inline bool gate_is_parametric(sturm_gate_kind_t k) noexcept {
    switch (k) {
        case STURM_GATE_P: case STURM_GATE_RX: case STURM_GATE_RY:
        case STURM_GATE_RZ: case STURM_GATE_CRX: case STURM_GATE_CRY:
        case STURM_GATE_CRZ: return true;
        default: return false;
    }
}

// Layout constants (pixel units inside the viewBox).
inline constexpr int SVG_MARGIN_LEFT = 40;
inline constexpr int SVG_MARGIN_TOP  = 20;
inline constexpr int SVG_COL_W       = 50;
inline constexpr int SVG_ROW_H       = 40;
inline constexpr int SVG_GATE_W      = 32;
inline constexpr int SVG_GATE_H      = 24;
inline constexpr int SVG_CTRL_R      = 4;

// Compute the (qubit, label) slots for a gate. Mirrors draw_ascii.hpp.
inline std::size_t svg_gate_slots(const GateRecord& rec,
                                  std::array<std::pair<uint32_t, std::string>, 3>& slots) {
    std::size_t ns = 0;
    auto push = [&](uint32_t q, const char* s) { slots[ns++] = {q, std::string(s)}; };
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

// Inline IR-taking overload (header-only, mirrors draw_json(ir, n)).
//
// Output (debug-print only, illustrative shape):
//
//   <?xml version="1.0" encoding="UTF-8"?>
//   <svg xmlns="..." viewBox="0 0 W H" width="W" height="H">
//     <style>...</style>
//     <text class="qlabel" .../>  <line class="rail" .../>  ← per qubit
//     <line class="vline" .../>   ← connector for multi-qubit gates
//     <circle class="control" .../>  ← control dots
//     <rect class="gate" .../>       ← gate body
//     <text class="gate-label" ...>H</text>
//     ...
//   </svg>

inline std::string draw_svg(const GateIR& ir, std::size_t n_qubits) {
    using namespace detail;
    std::string out;
    out.reserve(512 + ir.size() * 192);

    const std::size_t gate_cols = ir.size();
    const std::size_t width  = static_cast<std::size_t>(SVG_MARGIN_LEFT)
                             + gate_cols * static_cast<std::size_t>(SVG_COL_W)
                             + static_cast<std::size_t>(SVG_COL_W) / 2;
    const std::size_t height = static_cast<std::size_t>(SVG_MARGIN_TOP)
                             + n_qubits * static_cast<std::size_t>(SVG_ROW_H)
                             + static_cast<std::size_t>(SVG_ROW_H) / 2;

    // Envelope.
    out.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    out.append("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 ");
    svg_append_uint(out, width); out.push_back(' ');
    svg_append_uint(out, height);
    out.append("\" width=\""); svg_append_uint(out, width);
    out.append("\" height=\""); svg_append_uint(out, height);
    out.append("\">\n");

    // Inline stylesheet (light-mode default).
    out.append(
        "  <style>\n"
        "    .rail { stroke: #555; stroke-width: 1; }\n"
        "    .vline { stroke: #333; stroke-width: 1.5; }\n"
        "    .gate { fill: #fff; stroke: #222; stroke-width: 1.5; }\n"
        "    .gate-label { font-family: monospace; font-size: 14px;"
                          " text-anchor: middle; dominant-baseline: central; }\n"
        "    .control { fill: #222; }\n"
        "    .qlabel { font-family: monospace; font-size: 12px;"
                       " text-anchor: end; dominant-baseline: central; }\n"
        "    .param-label { font-family: monospace; font-size: 10px;"
                            " text-anchor: middle; dominant-baseline: central; }\n"
        "  </style>\n");

    auto x_of_gate  = [](std::size_t i) -> int {
        return SVG_MARGIN_LEFT + static_cast<int>(i) * SVG_COL_W + SVG_COL_W / 2;
    };
    auto y_of_qubit = [](std::size_t q) -> int {
        return SVG_MARGIN_TOP + static_cast<int>(q) * SVG_ROW_H + SVG_ROW_H / 2;
    };

    // Qubit rails + labels.
    for (std::size_t q = 0; q < n_qubits; ++q) {
        const int y = y_of_qubit(q);
        out.append("  <text class=\"qlabel\" x=\"");
        svg_append_uint(out, static_cast<unsigned long>(SVG_MARGIN_LEFT - 6));
        out.append("\" y=\""); svg_append_uint(out, static_cast<unsigned long>(y));
        out.append("\">q"); svg_append_uint(out, q);
        out.append("</text>\n");
        out.append("  <line class=\"rail\" x1=\"");
        svg_append_uint(out, static_cast<unsigned long>(SVG_MARGIN_LEFT));
        out.append("\" y1=\""); svg_append_uint(out, static_cast<unsigned long>(y));
        out.append("\" x2=\""); svg_append_uint(out, width);
        out.append("\" y2=\""); svg_append_uint(out, static_cast<unsigned long>(y));
        out.append("\" />\n");
    }

    // Gates.
    for (std::size_t g = 0; g < ir.size(); ++g) {
        const GateRecord& rec = ir.at(g);
        std::array<std::pair<uint32_t, std::string>, 3> slots{};
        std::size_t ns = svg_gate_slots(rec, slots);

        uint32_t qmin = 0, qmax = 0;
        bool seen = false;
        for (std::size_t i = 0; i < ns; ++i) {
            uint32_t q = slots[i].first;
            if (!seen || q < qmin) { qmin = q; seen = true; }
            if (q > qmax) qmax = q;
        }
        if (!seen) continue;
        const int xg = x_of_gate(g);

        // Vertical connector for multi-qubit gates.
        if (qmin != qmax) {
            out.append("  <line class=\"vline\" x1=\"");
            svg_append_uint(out, static_cast<unsigned long>(xg));
            out.append("\" y1=\""); svg_append_uint(out, static_cast<unsigned long>(y_of_qubit(qmin)));
            out.append("\" x2=\""); svg_append_uint(out, static_cast<unsigned long>(xg));
            out.append("\" y2=\""); svg_append_uint(out, static_cast<unsigned long>(y_of_qubit(qmax)));
            out.append("\" />\n");
        }

        for (std::size_t i = 0; i < ns; ++i) {
            const uint32_t q = slots[i].first;
            const std::string& lab = slots[i].second;
            const int yq = y_of_qubit(q);

            if (lab == "*") {
                out.append("  <circle class=\"control\" cx=\"");
                svg_append_uint(out, static_cast<unsigned long>(xg));
                out.append("\" cy=\""); svg_append_uint(out, static_cast<unsigned long>(yq));
                out.append("\" r=\""); svg_append_uint(out, static_cast<unsigned long>(SVG_CTRL_R));
                out.append("\" />\n");
            } else if (lab == "x") {
                out.append("  <text class=\"gate-label\" x=\"");
                svg_append_uint(out, static_cast<unsigned long>(xg));
                out.append("\" y=\""); svg_append_uint(out, static_cast<unsigned long>(yq));
                out.append("\">x</text>\n");
            } else {
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

                const int rect_x = xg - SVG_GATE_W / 2;
                const int rect_y = yq - SVG_GATE_H / 2;
                out.append("  <rect class=\"gate\" x=\"");
                svg_append_uint(out, static_cast<unsigned long>(rect_x));
                out.append("\" y=\""); svg_append_uint(out, static_cast<unsigned long>(rect_y));
                out.append("\" width=\""); svg_append_uint(out, static_cast<unsigned long>(SVG_GATE_W));
                out.append("\" height=\""); svg_append_uint(out, static_cast<unsigned long>(SVG_GATE_H));
                out.append("\" />\n");
                out.append("  <text class=\"gate-label\" x=\"");
                svg_append_uint(out, static_cast<unsigned long>(xg));
                out.append("\" y=\""); svg_append_uint(out, static_cast<unsigned long>(yq));
                out.append("\">");
                svg_append_escaped(out, disp.c_str());
                out.append("</text>\n");

                // Controlled parametric gates: surface angle just below the rect.
                if (gate_is_parametric(rec.kind) && ns > 1) {
                    out.append("  <text class=\"param-label\" x=\"");
                    svg_append_uint(out, static_cast<unsigned long>(xg));
                    out.append("\" y=\"");
                    svg_append_uint(out, static_cast<unsigned long>(yq + SVG_GATE_H / 2 + 8));
                    out.append("\">");
                    svg_append_double(out, rec.param);
                    out.append("</text>\n");
                }
            }
        }
    }

    out.append("</svg>\n");
    return out;
}

} // namespace sturm
