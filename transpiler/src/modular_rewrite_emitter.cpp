// modular_rewrite_emitter.cpp — sturm-qzab.1 (Phase 5 beat 5.1) +
// sturm-qzab.2 (Phase 5 beat 5.2) impl.
//
// Beat 5.1 landed the AddMod arm; beat 5.2 mirrors that for MulMod.
// The PRD §2.1 rewrite shapes are:
//
//     qint_t<W> r = (a + b) % n;
//   → sturm::qint_t<W> r = ::sturm::add_mod(a, b, n);
//
//     qint_t<W> r = (a * b) % n;
//   → sturm::qint_t<W> r = ::sturm::mul_mod(a, b, n);
//
// The emitter is a pure-text leaf: no `#line` directives, no Rewriter,
// no SourceManager. The wiring layer in `transpile_consumer.cpp` is
// responsible for splicing source-map directives and turning the
// emission text into a `QReplacement` over the VarDecl statement's
// source range.
//
// Why `::sturm::add_mod` / `::sturm::mul_mod` and not the library-level
// `lib_*_mod_dsl` primitives? The plan §7.2 and PRD §2.1 phrase the
// rewrite target as "`lib_add_mod_dsl`" / "`lib_mul_mod_dsl`", but the
// actual lib-level primitives take raw `Bit*` arrays (see
// `include/sturm/detail/lib/add_mod_dsl.hpp:112` and
// `include/sturm/detail/lib/mul_mod_dsl.hpp`). The qint-friendly forms are
// the public free functions `sturm::add_mod(a, b, n)` /
// `sturm::mul_mod(a, b, n)` (PRD §3.1, plan §6.2 / Phase 4) — thin
// wrappers that allocate the fresh result register and dispatch to
// the corresponding `lib_*_mod_dsl`. Emitting the wrapper keeps the
// rewrite to a single statement and matches the existing
// `tests/lib/test_qint_modular.cpp` call shape. This is the same
// posture LO-2b takes when emitting `mul_oop` / `and_oop` / `or_oop`
// instead of the corresponding `lib_*_dsl` primitives directly.
//
// AddMod / MulMod do not allocate any auxiliary tmp at the AST-rewrite
// level — the wide-intermediate elimination happens at lib-level, where
// `lib_mul_mod_dsl` interleaves the reduction internally. Neither arm
// consumes a `FreshNameAllocator::next()` slot — the alloc parameter
// is reserved for Beats 5.4-5.6 (PowMod), which may need fresh ancilla
// names.

#include "modular_rewrite_emitter.hpp"

#include "fresh_names.hpp"

#include <sstream>
#include <string>
#include <string_view>

namespace sturm::transpile {

namespace {

// Mirrors `lossy_rewrite_emitter::render_qint_typename`. `W > 0` means
// the matcher resolved the result VarDecl's `qint_t<W>` width off the
// AST and we splice the W into a concrete `sturm::qint_t<W>`. `0`
// keeps the legacy bare `qint` shape the hermetic-stub fixtures
// depend on. Same sturm-czfi posture used by the lossy emitter.
std::string render_qint_typename(int result_width) {
    if (result_width <= 0) return "qint";
    std::ostringstream os;
    os << "sturm::qint_t<" << result_width << ">";
    return os.str();
}

// Forward emission for the binary-operand modular rewrites (AddMod /
// MulMod). `result` is the VarDecl name (the LHS of `qint_t<W> r =
// ...;`); `a`/`b`/`n` are the operand names; `fn_name` selects between
// `add_mod` (Beat 5.1) and `mul_mod` (Beat 5.2); the rendered text is
// a single declaration-with-initializer terminated by '\n' (no trailing
// `;` follows the newline because the user's original `;` is consumed
// by the wiring layer's source-range replacement).
std::string render_binary_mod(std::string_view fn_name,
                              std::string_view result,
                              std::string_view a, std::string_view b,
                              std::string_view n, int result_width) {
    std::ostringstream os;
    os << render_qint_typename(result_width) << ' ' << result
       << " = ::sturm::" << fn_name << '(' << a << ", " << b << ", " << n
       << ");\n";
    return os.str();
}

} // namespace

ModularEmission emit_modular_forward_text(ModularOpKind kind,
                                          std::string_view result,
                                          std::string_view a,
                                          std::string_view b,
                                          std::string_view n,
                                          int result_width,
                                          FreshNameAllocator& alloc) {
    // The alloc slot is unused for AddMod / MulMod (both rewrites need
    // no fresh name) — Beats 5.4-5.6 (PowMod) will revisit. Cast it to
    // silence -Wunused-parameter while keeping the signature stable
    // across beats so the consumer wiring does not need to be
    // re-touched when later beats land.
    (void)alloc;

    // Defensive: refuse to emit a rewrite we cannot name. Empty
    // operand strings would produce malformed C++ (`add_mod(, b, n)`).
    // Returning empty `text` triggers the consumer's
    // `if (em.text.empty()) continue;` skip arm, matching the LO-2b
    // posture for malformed input.
    if (result.empty() || a.empty() || b.empty() || n.empty()) {
        ModularEmission em;
        em.kind = kind;
        return em;
    }

    ModularEmission em;
    em.kind = kind;
    em.cleanup_anchor_name = std::string(result);

    switch (kind) {
        case ModularOpKind::AddMod:
            em.text = render_binary_mod("add_mod", result, a, b, n,
                                        result_width);
            break;
        case ModularOpKind::MulMod:
            em.text = render_binary_mod("mul_mod", result, a, b, n,
                                        result_width);
            break;
        case ModularOpKind::PowMod:
            // sturm-qzab.5 (P5 beat 5.5): PowMod arm — emits the
            // `sturm::pow_mod(a, x, n)` free-function call. Same
            // single-statement substitution shape as AddMod / MulMod;
            // the wide-intermediate elimination happens at lib level
            // inside `lib_pow_mod_dsl` (no AST-side fresh ancilla).
            // The emission is unconditional on the kind — the matcher
            // arm registration in `matcher_modular_op.cpp` is what
            // gates `PowMod` hits behind `#ifdef STURM_MODULAR_POW`,
            // so this dispatch only runs under the flag-on configure.
            em.text = render_binary_mod("pow_mod", result, a, b, n,
                                        result_width);
            break;
    }
    return em;
}

ModularEmission emit_modular_forward(const ModularOpHit& hit,
                                     FreshNameAllocator& alloc) {
    // AST-aware overload: pull the operand names + width off `hit`
    // and delegate. Mirrors `emit_lossy_forward` in the LO-2b emitter.
    return emit_modular_forward_text(hit.kind, hit.result_name,
                                     hit.a_name, hit.b_name, hit.n_name,
                                     hit.result_width, alloc);
}

} // namespace sturm::transpile
