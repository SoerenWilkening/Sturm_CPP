// modular_rewrite_emitter.cpp — sturm-r5pn.4 (Phase 0.4) implementation.
// SHELL: every entry point returns an empty `ModularEmission`. See
// modular_rewrite_emitter.hpp for the scaffold rationale and Phase 5's
// plan §7 contract.
//
// The empty-text return is the same posture
// `emit_lossy_forward_text` takes on degenerate input — callers
// already check `em.text.empty()` before splicing into
// `unit_.replacements`, so the SHELL behavior wires through the
// existing drain loop without any consumer-side change.

#include "modular_rewrite_emitter.hpp"

#include "fresh_names.hpp"

namespace sturm::transpile {

ModularEmission emit_modular_forward_text(ModularOpKind kind,
                                          std::string_view result,
                                          std::string_view a,
                                          std::string_view b,
                                          std::string_view n,
                                          int result_width,
                                          FreshNameAllocator& alloc) {
    // SHELL: no rewrite. Phase 5 builds the §7.2 forward triplet here.
    // Cast the unused parameters to silence -Wunused-parameter so the
    // shell builds cleanly under the project's default warning level.
    (void)kind;
    (void)result;
    (void)a;
    (void)b;
    (void)n;
    (void)result_width;
    (void)alloc;
    return {};
}

ModularEmission emit_modular_forward(const ModularOpHit& hit,
                                     FreshNameAllocator& alloc) {
    // SHELL: no rewrite. Phase 5 unpacks `hit` into the pure-string
    // overload's parameter list and delegates.
    (void)hit;
    (void)alloc;
    return {};
}

} // namespace sturm::transpile
