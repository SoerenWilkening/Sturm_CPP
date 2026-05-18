// entry_point_parsed_attr_info.cpp — sturm-0tcv: Clang ParsedAttrInfo
// plugin registration for the plain `[[sturm::entry_point]]` attribute
// spelling.
//
// Why this module exists
// ----------------------
// Clang's attribute machinery has two layers. The canonical carrier
// for sturm-namespace markers is `[[clang::annotate("sturm::...")]]`,
// which Clang's built-in handler turns into an `AnnotateAttr` on the
// owning Decl. That form works on every clang version, with or
// without our plugin.
//
// As a convenience, this TU registers a `ParsedAttrInfo` so the plain
// `[[sturm::entry_point]]` spelling is also accepted by clang when
// the transpiler's plugin is loaded. The ParsedAttrInfo's
// `handleDeclAttribute` attaches an `AnnotateAttr` carrying the
// canonical annotation string (`"sturm::entry_point"`) to the target
// FunctionDecl, so downstream walkers — including
// `has_entry_point_attr()` in `entry_point_attribute.cpp` — answer
// uniformly regardless of which spelling the user wrote.
//
// Trade-off / known limitation
// ----------------------------
// The host-clang invariant (sturm-yial) requires the host compiler
// and the plugin's libclang-cpp to come from the same LLVM install
// prefix. The ParsedAttrInfoRegistry follows the same posture as
// `FrontendPluginRegistry`: the registrar's `Add<>` static lands in
// the plugin's libclang-cpp, and is only visible to the host clang
// when both link against the same shared library. Users who compile
// without the plugin (or with a mismatched host clang) will see
// `-Wunknown-attributes` on the plain `[[sturm::entry_point]]`
// spelling and the attribute will be silently dropped. The
// recommended fallback in that situation is to write the explicit
// `[[clang::annotate("sturm::entry_point")]]` form, which the
// detection predicate accepts unconditionally.
//
// Subject matching
// ----------------
// `diagAppertainsToDecl` accepts only `FunctionDecl`s. Other subjects
// — `RecordDecl`s, `VarDecl`s, `EnumDecl`s — receive Clang's standard
// "attribute does not appertain to this declaration" diagnostic. The
// matcher in `matcher_main_lifecycle.cpp` is the authoritative
// downstream gate that filters out non-FunctionDecl subjects in case
// a user attaches the canonical annotate form to a non-function decl;
// this `diagAppertainsToDecl` override only improves the diagnostic
// for the short-form spelling.

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/AttributeCommonInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/ParsedAttrInfo.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"

namespace {

struct SturmEntryPointAttrInfo : public clang::ParsedAttrInfo {
    SturmEntryPointAttrInfo() {
        // Zero required args, zero optional args. The attribute is
        // a flag — `[[sturm::entry_point]]` with no arguments.
        NumArgs = 0;
        OptArgs = 0;

        // Accepted spelling forms. The C++11 attribute form
        // `[[sturm::entry_point]]` is the primary user-facing
        // spelling. The GNU form `__attribute__((sturm_entry_point))`
        // and the C2x form `[[sturm::entry_point]]` are accepted too
        // so user code that builds in a mix of modes (C2x sources,
        // GNU-compatible headers) sees the same attribute.
        static constexpr Spelling kSpellings[] = {
            {clang::ParsedAttr::AS_CXX11,   "sturm::entry_point"},
            {clang::ParsedAttr::AS_C2x,     "sturm::entry_point"},
            {clang::ParsedAttr::AS_GNU,     "sturm_entry_point"},
        };
        Spellings = kSpellings;
    }

    bool diagAppertainsToDecl(clang::Sema& S,
                              const clang::ParsedAttr& Attr,
                              const clang::Decl* D) const override {
        // Restrict the attribute to FunctionDecl subjects. Methods on
        // classes are technically FunctionDecls too, but the matcher
        // in `matcher_main_lifecycle.cpp` further rejects subjects
        // that aren't at namespace scope (e.g. CXXMethodDecls) per
        // PRD §3 non-goal — that gating happens AFTER the AnnotateAttr
        // is attached, so the appertain check here stays permissive.
        if (!llvm::isa<clang::FunctionDecl>(D)) {
            const unsigned ID = S.Diags.getCustomDiagID(
                clang::DiagnosticsEngine::Warning,
                "'sturm::entry_point' attribute only applies to "
                "function declarations");
            S.Diag(Attr.getLoc(), ID);
            return false;
        }
        return true;
    }

    AttrHandling handleDeclAttribute(
        clang::Sema& S, clang::Decl* D,
        const clang::ParsedAttr& Attr) const override {
        // We attach the canonical AnnotateAttr carrier so the
        // downstream detection predicate
        // `has_entry_point_attr(const FunctionDecl*)` answers
        // uniformly regardless of whether the user wrote the short
        // form `[[sturm::entry_point]]` or the explicit
        // `[[clang::annotate("sturm::entry_point")]]`.
        //
        // Per AnnotateAttr::Create's signature, the annotation
        // string is stored in the ASTContext's string arena (created
        // via getASTContext().AllocateCopy/Backing); we hand it the
        // string literal directly and let AnnotateAttr's constructor
        // copy as needed.
        D->addAttr(clang::AnnotateAttr::Create(
            D->getASTContext(),
            "sturm::entry_point",
            /*Args=*/nullptr, /*ArgsSize=*/0,
            Attr.getRange()));
        return AttributeApplied;
    }
};

// Register the plugin attr info. The Add<> registrar lands in the
// libclang-cpp the plugin links against; only host clangs that load
// the plugin via -fplugin / -Xclang will see this registration.
clang::ParsedAttrInfoRegistry::Add<SturmEntryPointAttrInfo>
    g_entry_point_attr_info(
        "sturm::entry_point",
        "Mark a free function as a sturm lifecycle entry point — see "
        "PRD §5.4 / sturm-0tcv. Matcher matcher_main_lifecycle will "
        "wrap the function body with sturm_backend_create / "
        "sturm_set_thread_context / sturm_backend_destroy.");

} // namespace
