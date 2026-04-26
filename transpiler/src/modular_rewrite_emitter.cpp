// modular_rewrite_emitter.cpp — sturm-qzab.1 (Phase 5 beat 5.1) impl.
//
// Beat 5.1 lands the AddMod arm. The PRD §2.1 rewrite shape for
// `qint_t<W> r = (a + b) % n;` is:
//
//     sturm::qint_t<W> r = ::sturm::add_mod(a, b, n);
//
// The emitter is a pure-text leaf: no `#line` directives, no Rewriter,
// no SourceManager. The wiring layer in `transpile_consumer.cpp` is
// responsible for splicing source-map directives and turning the
// emission text into a `QReplacement` over the VarDecl statement's
// source range.
//
// Why `::sturm::add_mod` and not `lib_add_mod_dsl`? The plan §7.2 and
// PRD §2.1 phrase the rewrite target as "`lib_add_mod_dsl`", but the
// actual library-level primitive takes raw `Bit*` arrays (see
// `include/sturm/lib/add_mod_dsl.hpp:112`). The qint-friendly form is
// the public free function `sturm::add_mod(a, b, n)` (PRD §3.1, plan
// §6.2 / Phase 4) — a thin wrapper that allocates the fresh result
// register and dispatches to `lib_add_mod_dsl`. Emitting the wrapper
// keeps the rewrite to a single statement and matches the existing
// `tests/lib/test_qint_modular.cpp` call shape. This is the same
// posture LO-2b takes when emitting `mul_oop` / `and_oop` / `or_oop`
// instead of the corresponding `lib_*_dsl` primitives directly.
//
// AddMod does not allocate any auxiliary tmp, so it does not consume
// a `FreshNameAllocator::next()` slot — the alloc parameter is
// reserved for Beat 5.2 (MulMod) / 5.4-5.6 (PowMod), which may need
// fresh ancilla names.

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

// AddMod-only forward emission. `result` is the VarDecl name (the LHS
// of `qint_t<W> r = ...;`); `a`/`b`/`n` are the operand names; the
// rendered text is a single declaration-with-initializer terminated by
// '\n' (no trailing `;` follows the newline because the user's
// original `;` is consumed by the wiring layer's source-range
// replacement).
std::string render_add_mod(std::string_view result, std::string_view a,
                           std::string_view b, std::string_view n,
                           int result_width) {
    std::ostringstream os;
    os << render_qint_typename(result_width) << ' ' << result
       << " = ::sturm::add_mod(" << a << ", " << b << ", " << n << ");\n";
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
    // The alloc slot is unused for AddMod (the rewrite needs no fresh
    // name) — Beat 5.2 / 5.4-5.6 will revisit. Cast it to silence
    // -Wunused-parameter while keeping the signature stable across
    // beats so the consumer wiring does not need to be re-touched
    // when later beats land.
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
            em.text = render_add_mod(result, a, b, n, result_width);
            break;
        case ModularOpKind::MulMod:
        case ModularOpKind::PowMod:
            // SHELL: beats 5.2 (MulMod) / 5.4-5.6 (PowMod) overwrite
            // these arms. Returning empty here keeps the beat-5.1
            // contract — the consumer skip arm fires, the user's
            // source is left alone, and the modular_mul_op /
            // modular_pow_op_* fixtures continue to round-trip
            // identically through the transpiler.
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
