// lossy_rewrite_emitter.cpp — LO-2b (sturm-yxxa) implementation. See
// the header for the contract; PRD §2.2 / §2.4 for the dispatch shape.
//
// Five opcode families, two structural shapes:
//
//   - Single-ancilla shape (`*=`, `&=`, `|=`):
//
//         qint __sturm_tmp_<op>_<N>;
//         <op>_oop(<lhs>, <rhs>, __sturm_tmp_<op>_<N>);
//         swap(<lhs>, __sturm_tmp_<op>_<N>);
//
//     where `<op>` ∈ { mul, and, or }. The swap target IS the tmp.
//
//   - Two-ancilla divide shape (`/=`, `%=`):
//
//         qint __sturm_tmp_<op>_<N>_q, __sturm_tmp_<op>_<N>_r;
//         divide_oop(<lhs>, <rhs>, __sturm_tmp_<op>_<N>_q,
//                                  __sturm_tmp_<op>_<N>_r);
//         swap(<lhs>, __sturm_tmp_<op>_<N>_<target>);
//
//     where `<op>` ∈ { div, mod }, and `<target>` is `q` for `/=`,
//     `r` for `%=` (PRD §2.4).
//
// Counter scheme
// --------------
// One `FreshNameAllocator::next()` call per emission. The first hit
// burns `__stu_t<X>` (an unrelated counter family in fresh_names.hpp)
// and we extract the trailing decimal to mint our `<N>`. We do NOT
// reach into FreshNameAllocator internals — instead we use it as a
// monotonic int source via `next()` and parse the suffix off the
// returned `__stu_t<N>` string. This keeps fresh_names.hpp untouched
// (its public API is the only contract we depend on). LO-2c uses the
// same allocator instance and the same parse, so the two emitters
// agree on `<N>` per hit by construction.
//
// Defensive on empty operand names
// --------------------------------
// `LossyOpHit` carries `lhs_name` / `rhs_name` populated from
// `NamedDecl::getNameAsString()`. A null NamedDecl yields empty
// strings; we refuse to emit `qint <empty>(...);` rather than inject
// invalid C++ into the user's source, matching the same posture every
// other `render_*` helper takes on malformed input.

#include "lossy_rewrite_emitter.hpp"

#include "fresh_names.hpp"

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>

namespace sturm::transpile {

namespace {

// Pull the next monotonic counter slot off the allocator. We use the
// `__stu_t<N>` family (it is the closest existing monotonic source);
// the `<N>` is the only useful piece — we discard the prefix. Returns
// the counter as a string so the caller can splice it into the tmp
// names directly.
std::string next_counter_suffix(FreshNameAllocator& alloc) {
    const std::string raw = alloc.next();   // "__stu_t<N>"
    constexpr std::string_view kPrefix = "__stu_t";
    if (raw.size() <= kPrefix.size()) return raw;
    return raw.substr(kPrefix.size());
}

// Map a `LossyOpKind` to its short tag used inside the tmp identifier
// — `mul`, `div`, `mod`, `and`, `or`. Stable across runs because the
// fixture goldens depend on the exact spelling.
const char* kind_tag(LossyOpKind k) {
    switch (k) {
        case LossyOpKind::MulAssign: return "mul";
        case LossyOpKind::DivAssign: return "div";
        case LossyOpKind::ModAssign: return "mod";
        case LossyOpKind::AndAssign: return "and";
        case LossyOpKind::OrAssign:  return "or";
    }
    return "";  // unreachable for a well-formed enum
}

// Map a single-ancilla opcode to its OOP wrapper name. The DIV / MOD
// rows are unused (those go through `divide_oop`); we keep them
// in-table as empty strings so the dispatch is total.
const char* oop_name(LossyOpKind k) {
    switch (k) {
        case LossyOpKind::MulAssign: return "mul_oop";
        case LossyOpKind::AndAssign: return "and_oop";
        case LossyOpKind::OrAssign:  return "or_oop";
        case LossyOpKind::DivAssign: return "";  // uses divide_oop
        case LossyOpKind::ModAssign: return "";  // uses divide_oop
    }
    return "";
}

// sturm-czfi: render the ancilla declaration's typename. `lhs_width > 0`
// means the matcher resolved the LHS qint_t<W> off the AST and we splice the
// W into a concrete `sturm::qint_t<W>` so the emitted text compiles in TUs
// without a `using qint = ...;` typedef (the real example target). `0` keeps
// the legacy bare `qint` shape the hermetic-stub fixtures depend on.
std::string render_qint_typename(int lhs_width) {
    if (lhs_width <= 0) return "qint";
    std::ostringstream os;
    os << "sturm::qint_t<" << lhs_width << ">";
    return os.str();
}

// Single-ancilla forward emission. Shared body for *=, &=, |= — the
// only thing that varies is `kind_tag(k)` and `oop_name(k)`.
LossyEmission emit_single_ancilla(LossyOpKind k,
                                  std::string_view lhs,
                                  std::string_view rhs,
                                  int lhs_width,
                                  FreshNameAllocator& alloc) {
    LossyEmission em;
    em.opcode = k;
    const std::string suffix = next_counter_suffix(alloc);
    const std::string tmp =
        std::string("__sturm_tmp_") + kind_tag(k) + "_" + suffix;
    em.swap_target_name = tmp;
    // aux_tmp_name stays empty — single-ancilla shape.

    std::ostringstream os;
    os << render_qint_typename(lhs_width) << ' ' << tmp << ";\n"
       << oop_name(k) << '(' << lhs << ", " << rhs << ", " << tmp << ");\n"
       << "swap(" << lhs << ", " << tmp << ");\n";
    em.text = os.str();
    return em;
}

// Two-ancilla divide-kernel forward emission. Used by both `/=` and
// `%=` (only the swap target differs — `q` vs `r`).
LossyEmission emit_divide_kernel(LossyOpKind k,
                                 std::string_view lhs,
                                 std::string_view rhs,
                                 int lhs_width,
                                 FreshNameAllocator& alloc) {
    LossyEmission em;
    em.opcode = k;
    const std::string suffix = next_counter_suffix(alloc);
    const std::string base =
        std::string("__sturm_tmp_") + kind_tag(k) + "_" + suffix;
    const std::string tmp_q = base + "_q";
    const std::string tmp_r = base + "_r";

    if (k == LossyOpKind::DivAssign) {
        em.swap_target_name = tmp_q;
        em.aux_tmp_name     = tmp_r;
    } else {
        // ModAssign: swap the remainder into the LHS.
        em.swap_target_name = tmp_r;
        em.aux_tmp_name     = tmp_q;
    }

    std::ostringstream os;
    os << render_qint_typename(lhs_width) << ' '
       << tmp_q << ", " << tmp_r << ";\n"
       << "divide_oop(" << lhs << ", " << rhs << ", "
       << tmp_q << ", " << tmp_r << ");\n"
       << "swap(" << lhs << ", " << em.swap_target_name << ");\n";
    em.text = os.str();
    return em;
}

} // namespace

LossyEmission emit_lossy_forward_text(LossyOpKind kind,
                                      std::string_view lhs,
                                      std::string_view rhs,
                                      int lhs_width,
                                      FreshNameAllocator& alloc) {
    // Defensive: refuse to emit a forward pair we cannot name. An
    // empty operand string would render as `<op>_oop(, b, ...)` which
    // is a syntax error. The matcher always populates both names from
    // a `NamedDecl::getNameAsString()`, but the LO-2a callback's
    // null-decl branches default to empty strings, so the guard is
    // not unreachable for hand-built hits.
    if (lhs.empty() || rhs.empty()) {
        LossyEmission em;
        em.opcode = kind;
        return em;
    }

    switch (kind) {
        case LossyOpKind::MulAssign:
        case LossyOpKind::AndAssign:
        case LossyOpKind::OrAssign:
            return emit_single_ancilla(kind, lhs, rhs, lhs_width, alloc);
        case LossyOpKind::DivAssign:
        case LossyOpKind::ModAssign:
            return emit_divide_kernel(kind, lhs, rhs, lhs_width, alloc);
    }
    // Unreachable for a well-formed enum.
    LossyEmission em;
    em.opcode = kind;
    return em;
}

// Backward-compatible 4-arg overload: width unknown ⇒ legacy `qint` shape.
// Pre-sturm-czfi callers (and the pure-string snapshot fixtures) reach this
// overload; the AST-driven path below feeds the resolved `hit.lhs_width`.
LossyEmission emit_lossy_forward_text(LossyOpKind kind,
                                      std::string_view lhs,
                                      std::string_view rhs,
                                      FreshNameAllocator& alloc) {
    return emit_lossy_forward_text(kind, lhs, rhs, 0, alloc);
}

LossyEmission emit_lossy_forward(const LossyOpHit& hit,
                                 FreshNameAllocator& alloc) {
    return emit_lossy_forward_text(hit.opcode, hit.lhs_name, hit.rhs_name,
                                   hit.lhs_width, alloc);
}

} // namespace sturm::transpile
