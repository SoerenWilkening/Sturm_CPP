// lossy_scope_exit_emitter.cpp — LO-2c (sturm-hbwr) implementation. See
// the header for the contract; PRD §2.3 / §2.4 for the cleanup shape.
//
// Five opcode families, two structural shapes:
//
//   - Single-ancilla shape (`*=`, `&=`, `|=`):
//
//         swap(<lhs>, <swap_target>);
//         sturm::invert<&::sturm::lib_<X>_dsl>()(<lhs>, <rhs>, <swap_target>);
//
//     where `<X>_dsl` is `mul_dsl`, `c_AND_dsl`, or `or_dsl`.
//
//   - Two-ancilla divide-kernel shape (`/=`, `%=`):
//
//         swap(<lhs>, <swap_target>);
//         sturm::invert<&::sturm::lib_div_dsl>()(<lhs>, <rhs>,
//                                                <tmp_q>, <tmp_r>);
//
//     where `swap_target` is `tmp_q` for `/=` and `tmp_r` for `%=`,
//     but the invert call argument order is ALWAYS `<lhs>, <rhs>,
//     <tmp_q>, <tmp_r>`. To preserve that order regardless of which
//     of the two is the swap target, we recover both names from the
//     LossyEmission: `swap_target_name` is q-or-r per opcode and
//     `aux_tmp_name` is the OTHER one (per LO-2b's emit_divide_kernel
//     which sets `aux_tmp_name = tmp_r` for `/=` and
//     `aux_tmp_name = tmp_q` for `%=`).
//
// Defensive on empty operand / target names
// -----------------------------------------
// LossyOpHit always carries `lhs_name` / `rhs_name` from the matcher;
// LossyEmission always carries `swap_target_name` from LO-2b. But a
// degenerate LossyEmission (LO-2b's empty-operand defensive branch)
// can return an empty swap_target — we mirror that by returning empty
// cleanup text rather than emitting `swap(<lhs>, );` which is a
// syntax error. Same posture every other render_* helper takes.
//
// LIFO grouping
// -------------
// `group_cleanups_by_block` walks `hits` once to record first-encounter
// order of distinct enclosing CompoundStmt pointers, then walks again
// in REVERSE to concatenate cleanup text per block. The two-pass shape
// keeps the output deterministic across compiler-implementation iters.

#include "lossy_scope_exit_emitter.hpp"

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sturm::transpile {

namespace {

// Map a `LossyOpKind` to the DSL identifier referenced by the
// `sturm::invert<&::sturm::lib_<X>_dsl>()` cleanup. Stable across runs
// because the LO-0.x expected fixtures depend on the exact spelling.
const char* dsl_name(LossyOpKind k) {
    switch (k) {
        case LossyOpKind::MulAssign: return "lib_mul_dsl";
        case LossyOpKind::DivAssign: return "lib_div_dsl";
        case LossyOpKind::ModAssign: return "lib_div_dsl";
        case LossyOpKind::AndAssign: return "lib_c_AND_dsl";
        case LossyOpKind::OrAssign:  return "lib_or_dsl";
    }
    return "";  // unreachable for a well-formed enum
}

// Render the per-hit cleanup body. Two shapes — single-ancilla and
// divide-kernel — keyed off the opcode. The divide branch threads
// q/r in canonical (q-then-r) order regardless of which of the two
// is the swap target.
std::string render_cleanup_body(LossyOpKind k,
                                std::string_view lhs,
                                std::string_view rhs,
                                std::string_view swap_target,
                                std::string_view aux_tmp) {
    std::ostringstream os;
    os << "swap(" << lhs << ", " << swap_target << ");\n"
       << "sturm::invert<&::sturm::" << dsl_name(k) << ">()"
       << '(' << lhs << ", " << rhs << ", ";
    if (k == LossyOpKind::DivAssign) {
        // /= : swap target is q, aux is r → emit (lhs, rhs, q, r).
        os << swap_target << ", " << aux_tmp;
    } else if (k == LossyOpKind::ModAssign) {
        // %= : swap target is r, aux is q → emit (lhs, rhs, q, r).
        os << aux_tmp << ", " << swap_target;
    } else {
        // Single-ancilla: tmp is the swap target.
        os << swap_target;
    }
    os << ");\n";
    return os.str();
}

} // namespace

LossyCleanupEmission emit_lossy_cleanup_text(LossyOpKind kind,
                                             std::string_view lhs,
                                             std::string_view rhs,
                                             std::string_view swap_target,
                                             std::string_view aux_tmp) {
    LossyCleanupEmission em;
    em.opcode = kind;

    // Defensive: refuse to emit a cleanup we cannot name. An empty
    // operand or empty swap target would render as `swap(, tmp);` or
    // `swap(a, );` which is a syntax error. The normal pipeline never
    // produces these (LO-2a populates lhs/rhs from a NamedDecl; LO-2b
    // populates swap_target from a fresh-name allocator), so this guard
    // only fires on hand-built degenerate inputs.
    if (lhs.empty() || rhs.empty() || swap_target.empty()) {
        return em;
    }
    // Divide-kernel cleanups also need aux_tmp for the canonical
    // (q, r) argument shape. LO-2b populates it for /= and %=; if the
    // caller passes empty (e.g. from a degenerate LossyEmission), we
    // refuse the divide-shaped emission rather than miscount args.
    if ((kind == LossyOpKind::DivAssign ||
         kind == LossyOpKind::ModAssign) && aux_tmp.empty()) {
        return em;
    }

    em.text = render_cleanup_body(kind, lhs, rhs, swap_target, aux_tmp);
    return em;
}

LossyCleanupEmission emit_lossy_cleanup(const LossyOpHit& hit,
                                        const LossyEmission& forward) {
    LossyCleanupEmission em = emit_lossy_cleanup_text(
        hit.opcode, hit.lhs_name, hit.rhs_name,
        forward.swap_target_name, forward.aux_tmp_name);
    // Carry the enclosing block through so callers can group on it
    // without a second AST walk.
    em.enclosing_block = hit.enclosing_block;
    return em;
}

std::vector<BlockCleanup>
group_cleanups_by_block(const std::vector<LossyOpHit>& hits,
                        const std::vector<LossyEmission>& forwards) {
    std::vector<BlockCleanup> out;
    // Defensive: misaligned vectors are a wiring bug. Refuse rather
    // than emit half-correct output.
    if (hits.size() != forwards.size()) return out;

    // Pass 1: walk forward, record first-encounter index per block in
    // `out` (so block order in the result follows first-hit order on
    // the input). Skip null-block hits defensively.
    std::unordered_map<const clang::CompoundStmt*, std::size_t> idx;
    for (std::size_t i = 0; i < hits.size(); ++i) {
        const auto* block = hits[i].enclosing_block;
        if (block == nullptr) continue;
        if (idx.find(block) != idx.end()) continue;
        idx.emplace(block, out.size());
        BlockCleanup bc;
        bc.enclosing_block = block;
        out.push_back(std::move(bc));
    }

    // Pass 2: walk REVERSE, concatenate cleanup text into the matching
    // block's slot. Reverse iteration realises LIFO: the last hit in
    // the input cleans up first in the output.
    for (std::size_t k = hits.size(); k > 0; --k) {
        const std::size_t i = k - 1;
        const auto* block = hits[i].enclosing_block;
        if (block == nullptr) continue;
        auto it = idx.find(block);
        if (it == idx.end()) continue;  // unreachable; defensive
        LossyCleanupEmission cu = emit_lossy_cleanup(hits[i], forwards[i]);
        if (cu.text.empty()) continue;  // skip degenerate
        out[it->second].text += cu.text;
    }
    return out;
}

} // namespace sturm::transpile
