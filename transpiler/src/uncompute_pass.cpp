// uncompute_pass.cpp — implementation of the M8 uncompute synthesis pass.
//
// The logic is small on purpose: for each scope in the QUnit, iterate its
// ops *in reverse* and emit one UncomputeInsertion per handled op. LIFO is
// the invariant this file exists to enforce, so the reverse iteration is
// the most important line of code below.
//
// No Clang AST / Rewriter / SourceManager / I/O calls happen here. Only
// SourceLocation values (opaque 32-bit wrappers) flow through, which is
// why this file compiles in a fraction of a second and the tests run in
// microseconds.

#include "sturm/transpile/uncompute_pass.hpp"

#include "sturm/transpile/qir.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace sturm::transpile {

namespace {

// Build the source-text snippet for a single forward op's inverse. The
// exact format is locked down by the M8 tests and the PRD's output
// contract: four-space indent, `uncompute_or(<result>, <op0>, <op1>);\n`
// (and analogous per-kind forms for later phases).
//
// Phase A adds NOT. Per the roadmap, NOT is its own inverse: applying the
// same `~` to the result qubit uncomputes it. The emitted form is
// `<result> = ~<result>;` rather than a separate `uncompute_not(...)`
// free function, because there is no meaningful out-of-place inverse for
// a single-qubit X gate — the in-place form composes to identity with
// zero ancilla cost.
std::string render_uncompute(const QOperation& op) {
    std::ostringstream os;
    switch (op.kind) {
    case QOpKind::OR: {
        // Defensive: the MVP matcher always produces exactly two operands
        // for OR. If a future IR consumer seeds a malformed op, emit
        // nothing so we do not inject invalid C++ into the user's file.
        if (op.operands.size() != 2) return {};
        os << "    uncompute_or(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::AND: {
        // Phase E: exact analogue of the OR case. The Phase E compound
        // matcher (PE-4) records `qbool r = a & b;` with two operands
        // (the two qbool inputs); the inverse is a free-function call
        // `uncompute_and(r, a, b);` declared in
        // include/sturm/uncompute/uncompute_api.hpp and landed in PE-0
        // (sturm-oheo). Malformed seeds (operand count != 2) render
        // nothing, matching the OR guard.
        if (op.operands.size() != 2) return {};
        os << "    uncompute_and(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::NOT: {
        // NOT is self-inverse: re-applying `~` to the result qubit
        // uncomputes it. The operand list is unused in the emission
        // (the inverse touches only the result) but must be non-empty —
        // an op with no operand would indicate a malformed IR seed.
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " = ~" << op.result.name << ";\n";
        break;
    }
    case QOpKind::XOR: {
        // XOR is self-inverse with a two-step reduction: applying the
        // same two operands via `^=` to the result undoes it, because
        // (a^b)^a^b == 0. Emitted on two separate source lines for
        // readability; the order is lhs then rhs, matching the forward
        // source order of the original `a ^ b`.
        if (op.operands.size() != 2) return {};
        os << "    " << op.result.name << " ^= " << op.operands[0].name
           << ";\n"
           << "    " << op.result.name << " ^= " << op.operands[1].name
           << ";\n";
        break;
    }
    case QOpKind::XOR_ASSIGN: {
        // `a ^= b;` is self-adjoint: applying the same statement twice
        // returns a to its original state. The emitted inverse is a
        // verbatim re-emission of the forward call.
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " ^= " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::ADD_ASSIGN_CONST: {
        // Phase B: `a += C;` where C is a compile-time-readable classical
        // constant source fragment stored verbatim in operands[0].name.
        // The inverse is the dual operator over the same constant.
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " -= " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::SUB_ASSIGN_CONST: {
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " += " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::MUL_ASSIGN_CONST: {
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " /= " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::DIV_ASSIGN_CONST: {
        if (op.operands.size() != 1) return {};
        os << "    " << op.result.name << " *= " << op.operands[0].name
           << ";\n";
        break;
    }
    case QOpKind::ADD_ASSIGN_QINT: {
        // Phase C: `a += b;` where b is another qint named in source.
        // operands[0].name carries the verbatim RHS identifier. The
        // inverse is the `uncompute_add_qint` free function declared in
        // include/sturm/uncompute/uncompute_api.hpp, which delegates to
        // the forward `-=` compound-assign on the runtime side.
        if (op.operands.size() != 1) return {};
        os << "    uncompute_add_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::SUB_ASSIGN_QINT: {
        if (op.operands.size() != 1) return {};
        os << "    uncompute_sub_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::MUL_ASSIGN_QINT: {
        if (op.operands.size() != 1) return {};
        os << "    uncompute_mul_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::DIV_ASSIGN_QINT: {
        if (op.operands.size() != 1) return {};
        os << "    uncompute_div_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::MOD_ASSIGN_QINT: {
        if (op.operands.size() != 1) return {};
        os << "    uncompute_mod_qint(" << op.result.name << ", "
           << op.operands[0].name << ");\n";
        break;
    }
    case QOpKind::EQ_QINT: {
        // Phase D: `qbool c = a == b;` — c is the produced qbool result,
        // operands[0] is the LHS qint identifier, operands[1] is the RHS
        // qint identifier. The inverse is the `uncompute_eq_qint` free
        // function declared in include/sturm/uncompute/uncompute_api.hpp,
        // which re-dispatches to the self-adjoint DSL `lib_eq_dsl` in
        // include/sturm/lib/compare_dsl.hpp. Two operands, not one, because
        // the comparator adjoint needs both inputs to flip the result bit
        // back to |0⟩.
        if (op.operands.size() != 2) return {};
        os << "    uncompute_eq_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::NE_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_ne_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::LT_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_lt_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::LE_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_le_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::GT_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_gt_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    case QOpKind::GE_QINT: {
        if (op.operands.size() != 2) return {};
        os << "    uncompute_ge_qint(" << op.result.name << ", "
           << op.operands[0].name << ", " << op.operands[1].name << ");\n";
        break;
    }
    // No `default:` — adding a new QOpKind should fail the build here
    // until every downstream consumer is updated. (Compilers warn on
    // missing enum cases when default is absent.)
    }
    return os.str();
}

} // namespace

QSynthesisResult synthesize(const QUnit& unit) {
    QSynthesisResult result;
    std::vector<UncomputeInsertion>& out = result.insertions;

    // PE-2: pass replacements through unchanged. Pre-Phase-E the matcher
    // never populates this vector, so the loop is a no-op on existing
    // snapshot fixtures (byte-identical output guaranteed). When Phase E's
    // compound matcher lands it will append to `unit.replacements` and
    // the emitter will apply every entry ahead of the insertion pass.
    result.replacements = unit.replacements;

    // Rough capacity reservation to avoid mid-loop reallocations on the
    // common single-scope case. Worst-case each op yields one insertion.
    std::size_t upper_bound = 0;
    for (const auto& scope : unit.scopes) upper_bound += scope.ops.size();
    out.reserve(upper_bound);

    // Scopes are processed in source order so that M9 inserts text into
    // the outermost / earliest block first. WITHIN each scope, ops are
    // iterated in REVERSE to realize the LIFO uncompute schedule the PRD
    // is built around. This reverse iteration is the single load-bearing
    // line in this module — do not replace it with a forward walk.
    //
    // Ops are first sorted by their statement's source-begin location so
    // the reverse walk produces true source-LIFO. The matcher populates
    // scope.ops in MatchFinder callback-firing order, which is per-matcher
    // (registration) order rather than source order: when multiple
    // Phase A matchers contribute to the same scope, a raw reverse walk
    // would flip the inter-matcher ordering. Sorting pins LIFO to the
    // user's code, not the matcher's dispatch schedule. Regression:
    // sturm-ny2.
    for (const auto& scope : unit.scopes) {
        std::vector<QOperation> sorted_ops = scope.ops;
        std::sort(sorted_ops.begin(), sorted_ops.end(),
                  [](const QOperation& a, const QOperation& b) {
                      return a.stmt_range.getBegin().getRawEncoding() <
                             b.stmt_range.getBegin().getRawEncoding();
                  });

        for (auto it = sorted_ops.rbegin(); it != sorted_ops.rend(); ++it) {
            const QOperation& op = *it;
            std::string code = render_uncompute(op);
            if (code.empty()) {
                // Unsupported / malformed op — skip silently; see the
                // rationale in render_uncompute(). The MVP matcher never
                // produces such ops.
                continue;
            }
            UncomputeInsertion rec;
            rec.insert_before = scope.close_brace;
            rec.code = std::move(code);
            out.push_back(std::move(rec));
        }
    }

    return result;
}

} // namespace sturm::transpile
