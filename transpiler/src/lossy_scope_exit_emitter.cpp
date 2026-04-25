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

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"

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
//
// sturm-czfi: legacy width-0 path. When lhs_width is unknown / 0 we still
// emit the historical `lib_<X>_dsl` template-name shape so the hermetic-stub
// fixtures (which never compile the line) continue to byte-match. The
// width-aware path below in `render_cleanup_body` selects the registered-
// adjoint `sturm::detail::*_oop<W>` shape instead.
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

// sturm-czfi: width-aware adjoint helper name. The cleanup line for a
// width-resolved hit calls the registered `*_oop_adj<W>` directly (ADL on
// `qint_t<W>&` finds the using-promoted `sturm::*_oop_adj` from
// `sturm/qtypes/lossy_oop.hpp`). We bypass the `invert<&fn>()` NTTP-keyed
// lookup because partial specialization of `adjoint_of<&fn<W>>` with a NTTP
// whose type depends on `W` is ill-formed (the lossy_oop.hpp file-level
// comment cites the standard reference). Mod shares `divide_oop_adj<W>`
// (PRD §2.4) — the swap target differs but the adjoint signature is
// identical.
const char* oop_adj_name(LossyOpKind k) {
    switch (k) {
        case LossyOpKind::MulAssign: return "mul_oop_adj";
        case LossyOpKind::DivAssign: return "divide_oop_adj";
        case LossyOpKind::ModAssign: return "divide_oop_adj";
        case LossyOpKind::AndAssign: return "and_oop_adj";
        case LossyOpKind::OrAssign:  return "or_oop_adj";
    }
    return "";  // unreachable for a well-formed enum
}

// Render the per-hit cleanup body. Two shapes — single-ancilla and
// divide-kernel — keyed off the opcode. The divide branch threads
// q/r in canonical (q-then-r) order regardless of which of the two
// is the swap target.
//
// sturm-czfi: when `lhs_width > 0`, render the registered-adjoint NTTP
// target `&::sturm::detail::<op>_oop<W>` so the line compiles in real TUs.
// When 0, fall back to the legacy `&::sturm::lib_<X>_dsl` template-name
// shape that the hermetic-stub fixtures depend on.
std::string render_cleanup_body(LossyOpKind k,
                                std::string_view lhs,
                                std::string_view rhs,
                                std::string_view swap_target,
                                std::string_view aux_tmp,
                                int lhs_width) {
    std::ostringstream os;
    os << "swap(" << lhs << ", " << swap_target << ");\n";
    if (lhs_width > 0) {
        // sturm-czfi: bypass the `invert<&fn>()` NTTP-keyed lookup because
        // partial specialization of `adjoint_of<&fn<W>>` with a NTTP whose
        // type depends on `W` is ill-formed (C++ standard 17.5.5/9). Direct
        // call to the registered adjoint wrapper resolves via ADL on
        // `qint_t<W>&` (the using-decl in lossy_oop.hpp promotes the
        // `*_oop_adj` family from `sturm::detail::` into `sturm::`).
        os << oop_adj_name(k);
    } else {
        os << "sturm::invert<&::sturm::" << dsl_name(k) << ">()";
    }
    os << '(' << lhs << ", " << rhs << ", ";
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
                                             std::string_view aux_tmp,
                                             int lhs_width) {
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

    em.text = render_cleanup_body(kind, lhs, rhs, swap_target, aux_tmp,
                                  lhs_width);
    return em;
}

// Backward-compatible 5-arg overload: width unknown ⇒ legacy lib_*_dsl shape.
LossyCleanupEmission emit_lossy_cleanup_text(LossyOpKind kind,
                                             std::string_view lhs,
                                             std::string_view rhs,
                                             std::string_view swap_target,
                                             std::string_view aux_tmp) {
    return emit_lossy_cleanup_text(kind, lhs, rhs, swap_target, aux_tmp, 0);
}

LossyCleanupEmission emit_lossy_cleanup(const LossyOpHit& hit,
                                        const LossyEmission& forward) {
    LossyCleanupEmission em = emit_lossy_cleanup_text(
        hit.opcode, hit.lhs_name, hit.rhs_name,
        forward.swap_target_name, forward.aux_tmp_name,
        hit.lhs_width);
    // Carry the enclosing block through so callers can group on it
    // without a second AST walk.
    em.enclosing_block = hit.enclosing_block;
    return em;
}

bool is_main_outer_block(const clang::CompoundStmt* block,
                         clang::ASTContext& ctx) {
    // PRD §4.3 predicate. The enclosing CompoundStmt's IMMEDIATE parent
    // is a FunctionDecl named `main` iff `block` is main's outermost
    // body. Lambda bodies have a CXXMethodDecl parent named
    // `operator()`; nested if/while bodies have a control-flow Stmt
    // parent; non-main function bodies have a FunctionDecl parent
    // whose name differs. The single `getNameAsString() == "main"`
    // check distinguishes all three negative cases.
    if (block == nullptr) return false;
    const auto parents =
        ctx.getParents(clang::DynTypedNode::create(*block));
    if (parents.empty()) return false;
    const auto* fd = parents[0].get<clang::FunctionDecl>();
    if (fd == nullptr) return false;
    return fd->getNameAsString() == "main";
}

std::vector<BlockCleanup>
group_cleanups_by_block(const std::vector<LossyOpHit>& hits,
                        const std::vector<LossyEmission>& forwards,
                        clang::ASTContext* ctx) {
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

    // LO-2d: per-block main-outer suppression flags computed once. A
    // null `ctx` means "no AST available, do not suppress" — the legacy
    // 2-arg overload routes here with nullptr.
    std::vector<bool> suppress(out.size(), false);
    if (ctx != nullptr) {
        for (std::size_t b = 0; b < out.size(); ++b) {
            suppress[b] = is_main_outer_block(out[b].enclosing_block, *ctx);
        }
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
        if (suppress[it->second]) continue;  // PRD §4.3
        LossyCleanupEmission cu = emit_lossy_cleanup(hits[i], forwards[i]);
        if (cu.text.empty()) continue;  // skip degenerate
        out[it->second].text += cu.text;
    }
    return out;
}

std::vector<BlockCleanup>
group_cleanups_by_block(const std::vector<LossyOpHit>& hits,
                        const std::vector<LossyEmission>& forwards) {
    return group_cleanups_by_block(hits, forwards, nullptr);
}

} // namespace sturm::transpile
