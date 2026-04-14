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

#include <sstream>
#include <string>
#include <vector>

namespace sturm::transpile {

namespace {

// Build the source-text snippet for a single forward op's inverse. The
// exact format is locked down by the M8 tests and the PRD's output
// contract: four-space indent, `uncompute_or(<result>, <op0>, <op1>);\n`.
//
// NOTE: only `QOpKind::OR` is handled in the MVP. When the post-MVP
// roadmap introduces AND / XOR / NOT / arithmetic inverses, extend this
// switch — the tests in test_uncompute_pass.cpp will have to be extended
// with matching goldens.
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
    // No `default:` — adding a new QOpKind should fail the build here
    // until every downstream consumer is updated. (Compilers warn on
    // missing enum cases when default is absent.)
    }
    return os.str();
}

} // namespace

std::vector<UncomputeInsertion> synthesize(const QUnit& unit) {
    std::vector<UncomputeInsertion> out;

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
    for (const auto& scope : unit.scopes) {
        for (auto it = scope.ops.rbegin(); it != scope.ops.rend(); ++it) {
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

    return out;
}

} // namespace sturm::transpile
