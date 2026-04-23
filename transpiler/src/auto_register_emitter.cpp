// auto_register_emitter.cpp — Phase R R-B (sturm-88d7.3) implementation.
//
// See auto_register_emitter.hpp for the contract. This module is a
// pure string producer: every input is either two identifier strings
// or a `clang::FunctionDecl*` read via public const accessors; every
// output is a `std::string` the pipeline driver (R-C) inlines into
// the rewritten buffer. No Clang rewriter touches the AST.
//
// Emission shape (pinned by golden-file tests):
//
//     STURM_REGISTER_ADJOINT(<forward_name>, <adjoint_name>);\n
//
// The trailing semicolon is a valid empty declaration at TU scope
// (the macro expands to `namespace sturm { namespace _detail { ... } }`
// — two closing braces — and the semicolon is parsed as an empty
// declaration). Tests pin the exact byte sequence so any future
// reformatting of the emission is caught.
//
// Pipeline
// --------
//
//   1. Reject-gate (only in the FD-based entry point):
//       - nullptr guard,
//       - `is_reversible` guard (mirrors R-A's P9 opt-in posture),
//       - template-instantiation guard (PI-1's matcher keys on
//         concrete `decltype(&::fn)`, not on template-parameter-
//         dependent signatures),
//       - empty adjoint_name guard (R-A may have rejected the
//         forward, in which case the `SynthesisEntry::adjoint_name`
//         field is empty; emitting `STURM_REGISTER_ADJOINT(fn, );`
//         would be ill-formed).
//
//   2. Line assembly: concatenate
//      `"STURM_REGISTER_ADJOINT("` + `<fwd>` + `", "` + `<adj>` + `");\n"`.
//
// LOC budget
// ----------
// CLAUDE.md caps source files at 400 LOC. The plan §2.3 R-B budget
// is ~120 LOC for this implementation; we stay under.

#include "auto_register_emitter.hpp"

#include "reversible_attribute.hpp"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/Basic/Specifiers.h"

#include <string>
#include <string_view>

namespace sturm::transpile {

namespace {

using clang::FunctionDecl;

// Assemble the macro invocation line. Pure string concatenation; no
// clang types touched. Kept as a single helper so both the pure-string
// public API and the FD-based API share one byte-stable formatter —
// no second source of drift for what the emitted line looks like.
std::string assemble_registration_line(std::string_view forward_name,
                                       std::string_view adjoint_name) {
    // Pre-reserve enough space for the fixed boilerplate plus both
    // identifier slots. The fixed text
    // `"STURM_REGISTER_ADJOINT(, );\n"` is 28 bytes; add the two
    // identifier lengths so the one heap allocation suffices on every
    // common call site. Matches R-A's reserve heuristic.
    std::string out;
    out.reserve(28 + forward_name.size() + adjoint_name.size());
    out += "STURM_REGISTER_ADJOINT(";
    out.append(forward_name.data(), forward_name.size());
    out += ", ";
    out.append(adjoint_name.data(), adjoint_name.size());
    out += ");\n";
    return out;
}

} // namespace

// ── Public surface ──────────────────────────────────────────────────────────

std::string_view to_string(AutoRegisterRejectReason reason) {
    switch (reason) {
        case AutoRegisterRejectReason::None:
            return "none";
        case AutoRegisterRejectReason::NullDecl:
            return "null_decl";
        case AutoRegisterRejectReason::NotReversible:
            return "not_reversible";
        case AutoRegisterRejectReason::EmptyAdjointName:
            return "empty_adjoint_name";
        case AutoRegisterRejectReason::TemplateInstantiation:
            return "template_instantiation";
    }
    // Unreachable for a well-formed enum. Empty view is the safe
    // default — we never construct this branch in production code.
    return {};
}

std::string emit_auto_registration(std::string_view forward_name,
                                   std::string_view adjoint_name) {
    // Empty forward or adjoint name → empty output. A caller with
    // neither piece cannot possibly consume the produced text;
    // returning an empty string lets the driver concatenate R-B's
    // output unconditionally (an empty string degrades to a no-op)
    // and skip the "is the line non-empty?" check on its hot path.
    //
    // The null-empty contract is intentionally symmetric to R-A's
    // `emit_adjoint_body("", ...)` short-circuit — both modules
    // surface the same "no identifier, no output" posture.
    if (forward_name.empty()) return {};
    if (adjoint_name.empty()) return {};
    return assemble_registration_line(forward_name, adjoint_name);
}

AutoRegisterResult emit_auto_registration_for_decl(
    const FunctionDecl* fd, std::string_view adjoint_name) {
    AutoRegisterResult result;

    // 1. Reject-gate.
    if (fd == nullptr) {
        result.reason = AutoRegisterRejectReason::NullDecl;
        return result;
    }
    if (!is_reversible(fd)) {
        result.reason = AutoRegisterRejectReason::NotReversible;
        return result;
    }
    // Template instantiations cannot be registered: PI-1's matcher
    // consumes `adjoint_of<decltype(&::fn)>`, which requires a
    // concrete non-dependent function type. Mirrors the filter
    // `matcher_user_routine.cpp` applies on the call-site path.
    if (fd->getTemplateSpecializationKind() != clang::TSK_Undeclared) {
        result.reason = AutoRegisterRejectReason::TemplateInstantiation;
        return result;
    }
    if (adjoint_name.empty()) {
        result.reason = AutoRegisterRejectReason::EmptyAdjointName;
        return result;
    }

    // 2. Line assembly. The forward's short identifier is the first
    // macro argument — the STURM_REGISTER_ADJOINT expansion uses
    // `decltype(&::<fwd>)`, so the leading `::` in the macro
    // resolves the name at the global namespace. When the user's
    // reversible routine lives at namespace scope (e.g.
    // `namespace ns { [[sturm::reversible]] void foo() {} }`), the
    // R-C driver is responsible for passing a qualified name to
    // `emit_auto_registration` directly — the FD-based path here
    // uses only the short name, mirroring the hand-written
    // convention every existing `STURM_REGISTER_ADJOINT(foo, ...)`
    // invocation in the test fixtures uses.
    const std::string forward_name = fd->getNameAsString();
    result.source = assemble_registration_line(forward_name, adjoint_name);
    result.registered = true;
    result.reason = AutoRegisterRejectReason::None;
    return result;
}

} // namespace sturm::transpile
