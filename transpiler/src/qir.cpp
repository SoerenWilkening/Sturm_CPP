// qir.cpp — implementation of the Quantum IR (M6).
//
// Two small pieces live here:
//   - operator==(QValueRef, QValueRef): structural equality on name + loc.
//   - dump(QUnit): the single authoritative textual representation.
//
// The format produced by dump() is locked down — see qir.hpp for the full
// schema. Any change to these strings breaks M7 and M8 golden tests.

#include "sturm/transpile/qir.hpp"

#include "clang/Basic/SourceLocation.h"

#include <sstream>
#include <string>
#include <string_view>

namespace sturm::transpile {

bool operator==(const QValueRef& lhs, const QValueRef& rhs) {
    // Compare name first (cheap short-circuit on the common "different
    // variable" path), then the raw source location encoding. We compare
    // raw encodings rather than pointer identity because SourceLocation is
    // a value type that wraps a 32-bit id.
    if (lhs.name != rhs.name) return false;
    return lhs.decl_loc.getRawEncoding() == rhs.decl_loc.getRawEncoding();
}

// ── Formatting helpers ───────────────────────────────────────────────────────

namespace {

// A SourceLocation constructed by default is "invalid" (raw encoding == 0).
// Render those as "<invalid>" so hand-built test fixtures produce golden
// output that is obviously distinguishable from a real build's locations.
std::string loc_to_string(clang::SourceLocation loc) {
    if (loc.isInvalid()) return "<invalid>";
    return std::to_string(loc.getRawEncoding());
}

std::string kind_to_string(QOpKind kind) {
    switch (kind) {
    case QOpKind::OR:               return "OR";
    case QOpKind::AND:              return "AND";
    case QOpKind::NOT:              return "NOT";
    case QOpKind::XOR:              return "XOR";
    case QOpKind::XOR_ASSIGN:       return "XOR_ASSIGN";
    case QOpKind::ADD_ASSIGN_CONST: return "ADD_ASSIGN_CONST";
    case QOpKind::SUB_ASSIGN_CONST: return "SUB_ASSIGN_CONST";
    case QOpKind::MUL_ASSIGN_CONST: return "MUL_ASSIGN_CONST";
    case QOpKind::DIV_ASSIGN_CONST: return "DIV_ASSIGN_CONST";
    case QOpKind::ADD_ASSIGN_QINT:  return "ADD_ASSIGN_QINT";
    case QOpKind::SUB_ASSIGN_QINT:  return "SUB_ASSIGN_QINT";
    case QOpKind::MUL_ASSIGN_QINT:  return "MUL_ASSIGN_QINT";
    case QOpKind::DIV_ASSIGN_QINT:  return "DIV_ASSIGN_QINT";
    case QOpKind::MOD_ASSIGN_QINT:  return "MOD_ASSIGN_QINT";
    case QOpKind::THETA_ADD_ASSIGN_CONST: return "THETA_ADD_ASSIGN_CONST";
    case QOpKind::THETA_SUB_ASSIGN_CONST: return "THETA_SUB_ASSIGN_CONST";
    case QOpKind::PHI_ADD_ASSIGN_CONST:   return "PHI_ADD_ASSIGN_CONST";
    case QOpKind::PHI_SUB_ASSIGN_CONST:   return "PHI_SUB_ASSIGN_CONST";
    case QOpKind::EQ_QINT:          return "EQ_QINT";
    case QOpKind::NE_QINT:          return "NE_QINT";
    case QOpKind::LT_QINT:          return "LT_QINT";
    case QOpKind::LE_QINT:          return "LE_QINT";
    case QOpKind::GT_QINT:          return "GT_QINT";
    case QOpKind::GE_QINT:          return "GE_QINT";
    case QOpKind::USER_ROUTINE:     return "USER_ROUTINE";
    case QOpKind::CCNOT_INPLACE:    return "CCNOT_INPLACE";
    case QOpKind::PLUGIN:           return "PLUGIN";
    }
    // Unreachable while every enumerator above is listed, but we emit a
    // deterministic placeholder so future additions that forget to update
    // this switch are immediately visible in any dump() output rather than
    // silently rendering nothing.
    return "<unknown-QOpKind>";
}

void dump_operand(std::ostringstream& os, const QValueRef& v) {
    os << v.name << '@' << loc_to_string(v.decl_loc);
}

void dump_op(std::ostringstream& os, const QOperation& op, std::size_t idx) {
    os << "    Op[" << idx << "] " << kind_to_string(op.kind) << ' ';
    dump_operand(os, op.result);
    os << " = ";
    for (std::size_t i = 0; i < op.operands.size(); ++i) {
        if (i != 0) os << ", ";
        dump_operand(os, op.operands[i]);
    }
    // Two spaces before range are intentional: separates the operand list
    // from the trailing range annotation even when the operand list is
    // empty (post-MVP NOT / constant forms).
    os << "  range=[" << loc_to_string(op.stmt_range.getBegin())
       << ".." << loc_to_string(op.stmt_range.getEnd()) << "]";
    // Phase F PF-1: surface the per-op insertion anchor override when (and
    // only when) the matcher set one. Default (invalid) overrides print
    // nothing so every Phase A..E snapshot fixture stays byte-identical.
    if (op.insert_before_override.isValid()) {
        os << " insert_before_override=" << loc_to_string(op.insert_before_override);
    }
    // Phase J PJ-3c: surface the hoist-uncompute anchor override when (and
    // only when) the PJ-3d matcher set one. Default (invalid) overrides
    // print nothing so every Phase A..I snapshot fixture stays
    // byte-identical; pre-PJ-3 matchers never set this field. Rendered
    // AFTER insert_before_override so when both are set the dump output
    // lists them in declaration order (insert_before_override first,
    // hoist_to_override second) — pinned by the test_qir.cpp fixture.
    if (op.hoist_to_override.isValid()) {
        os << " hoist_to_override=" << loc_to_string(op.hoist_to_override);
    }
    // Phase H PH-3: surface the skip_uncompute flag when it is set. The
    // default-false value prints nothing so every Phase A..G snapshot
    // fixture stays byte-identical; pre-Phase-H matchers never set it.
    if (op.skip_uncompute) {
        os << " [skip_uncompute]";
    }
    // Phase S S-B (sturm-ha2k.3): surface the needs_loop_reversal flag
    // when it is set. The default-false value prints nothing so every
    // Phase A..R snapshot fixture stays byte-identical; pre-Phase-S
    // matchers never set it. Rendered AFTER `[skip_uncompute]` so the
    // two tags, when both land on mixed op streams, print in a
    // deterministic order.
    if (op.needs_loop_reversal) {
        os << " [needs_loop_reversal]";
    }
    // Phase I PI-2: surface the user-routine routine_name and outputs_mask
    // when (and only when) kind == USER_ROUTINE. The default-empty /
    // default-zero values are not printed for non-USER_ROUTINE ops so
    // every Phase A..H snapshot fixture stays byte-identical.
    if (op.kind == QOpKind::USER_ROUTINE) {
        os << " routine_name=\"" << op.routine_name
           << "\" outputs_mask=0x" << std::hex << op.outputs_mask << std::dec;
    }
    // Phase M PM4-3: surface the plugin kind_id when (and only when)
    // kind == PLUGIN. The default-empty value is not printed for
    // non-PLUGIN ops so every pre-PM4 snapshot fixture stays byte-
    // identical (no in-tree matcher ever constructs a `QOpKind::PLUGIN`
    // op, so this branch is dead for the in-tree matcher pool).
    if (op.kind == QOpKind::PLUGIN) {
        os << " plugin_kind_id=\"" << op.plugin_kind_id << "\"";
    }
    os << "\n";
}

void dump_scope(std::ostringstream& os, const QScope& scope, std::size_t idx) {
    os << "  Scope[" << idx << "] braces=["
       << loc_to_string(scope.open_brace) << ".."
       << loc_to_string(scope.close_brace) << "]\n";
    for (std::size_t j = 0; j < scope.ops.size(); ++j) {
        dump_op(os, scope.ops[j], j);
    }
}

} // namespace

std::string dump(const QUnit& unit) {
    std::ostringstream os;
    os << "QUnit: " << unit.scopes.size() << " scope(s)\n";
    for (std::size_t i = 0; i < unit.scopes.size(); ++i) {
        dump_scope(os, unit.scopes[i], i);
    }
    return os.str();
}

} // namespace sturm::transpile
