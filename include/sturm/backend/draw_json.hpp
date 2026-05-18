// draw_json.hpp — Debug-print JSON renderer for a GateIR (sturm-a4we).
//
// Stability: debug-print format. Schema may change freely between
// versions; no `schema_version` field. The output is for humans, diffs,
// and internal tooling — not for external interchange.
//
// Output shape (pretty-printed by default; one op per line):
//
//   {
//     "n_qubits": <N>,
//     "ops": [
//       { "kind": "<NAME>", "qubits": [q0, q1, ...] },         (non-parametric)
//       { "kind": "<NAME>", "qubits": [q0],   "param": <θ> },  (parametric)
//       ...
//     ]
//   }
//
// Granularity: gate-level (one JSON object per IR record). Higher-level
// shapes (qint declarations, etc.) are outside the scope of this
// debug-print format.
//
// Two flavours of entry point (parallel to `draw_ascii.hpp`):
//   - draw_json(ir, n_qubits)  — explicit form, header-only (inline).
//   - draw_json() / print_json()  — no-argument form declared here,
//     defined in `src/sturm/backend/draw_json.cpp`. They query the
//     calling thread's installed BackendContext (mirrors PRD §5.6 for
//     the ASCII no-arg surface).

#pragma once

#include "sturm/backend/ir.hpp"
#include "sturm/core/gate_kind.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace sturm {

// ── No-argument entry points (parallel to PRD §5.6 for ASCII) ────────────────
//
// These operate on the thread-local BackendContext installed via
// sturm_set_thread_context (typically by the auto-injected lifecycle).
// They assert that a per-thread context is currently installed —
// calling them outside a lifecycle scope is a programming error
// (mirrors `draw_ascii()` / `print_ascii()`).

std::string draw_json();
void        print_json();

namespace detail {

// Append a JSON-escaped form of `s` to `out`. Only the structural
// characters that can appear in our gate names ("RZ", "CCX", etc.)
// need escaping — but we still handle `"`, `\` and control bytes for
// robustness against future name changes.
inline void json_append_escaped(std::string& out, const char* s) {
    out.push_back('"');
    if (s) {
        for (; *s; ++s) {
            unsigned char c = static_cast<unsigned char>(*s);
            switch (c) {
                case '"':  out.append("\\\""); break;
                case '\\': out.append("\\\\"); break;
                case '\n': out.append("\\n");  break;
                case '\r': out.append("\\r");  break;
                case '\t': out.append("\\t");  break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out.append(buf);
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
                    break;
            }
        }
    }
    out.push_back('"');
}

// Append a double in a JSON-safe form. We use %g which gives a short
// human-readable representation; locale is irrelevant because the
// values are doubles from radians.
inline void json_append_double(std::string& out, double v) {
    char buf[64];
    int n = std::snprintf(buf, sizeof(buf), "%g", v);
    if (n > 0 && static_cast<std::size_t>(n) < sizeof(buf)) {
        out.append(buf, static_cast<std::size_t>(n));
    } else {
        out.append("0");
    }
}

// True iff the given gate kind carries a meaningful `param` field
// (rotation angle or phase). Mirrors the comments in core/gate_kind.h.
inline bool gate_is_parametric(sturm_gate_kind_t k) noexcept {
    switch (k) {
        case STURM_GATE_P:
        case STURM_GATE_RX:
        case STURM_GATE_RY:
        case STURM_GATE_RZ:
        case STURM_GATE_CRX:
        case STURM_GATE_CRY:
        case STURM_GATE_CRZ:
            return true;
        default:
            return false;
    }
}

} // namespace detail

// ── Inline IR-taking overload ────────────────────────────────────────────────
//
// Header-only so callers can render an arbitrary GateIR without dragging
// in the no-arg TU. Matches the posture of `draw_ascii(ir, n)`.

inline std::string draw_json(const GateIR& ir, std::size_t n_qubits) {
    std::string out;
    out.reserve(64 + ir.size() * 48);

    out.append("{\n  \"n_qubits\": ");
    {
        char buf[24];
        int n = std::snprintf(buf, sizeof(buf), "%zu", n_qubits);
        if (n > 0) out.append(buf, static_cast<std::size_t>(n));
    }
    out.append(",\n  \"ops\": [");

    if (ir.size() == 0) {
        out.append("]\n}\n");
        return out;
    }
    out.push_back('\n');

    for (std::size_t i = 0; i < ir.size(); ++i) {
        const GateRecord& rec = ir.at(i);
        const sturm_gate_info_t* info = sturm_gate_info_of(rec.kind);
        const char* name = (info && info->name) ? info->name : "?";

        out.append("    { \"kind\": ");
        detail::json_append_escaped(out, name);
        out.append(", \"qubits\": [");
        for (uint8_t q = 0; q < rec.n; ++q) {
            if (q > 0) out.append(", ");
            char buf[16];
            int n = std::snprintf(buf, sizeof(buf), "%u",
                                  static_cast<unsigned>(rec.qubits[q]));
            if (n > 0) out.append(buf, static_cast<std::size_t>(n));
        }
        out.push_back(']');

        if (detail::gate_is_parametric(rec.kind)) {
            out.append(", \"param\": ");
            detail::json_append_double(out, rec.param);
        }

        out.append(" }");
        if (i + 1 < ir.size()) out.push_back(',');
        out.push_back('\n');
    }

    out.append("  ]\n}\n");
    return out;
}

} // namespace sturm
