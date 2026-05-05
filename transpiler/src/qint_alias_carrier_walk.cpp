// qint_alias_carrier_walk.cpp -- sturm-7t85.1 (Beat G1) implementation.
// See `qint_alias_carrier_walk.hpp` for the contract.

#include "qint_alias_carrier_walk.hpp"

#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"

namespace sturm::transpile {

namespace {

// Strip ElaboratedTypeLoc / ParenTypeLoc layers to reach the inner
// "kind-bearing" TypeLoc (TypedefTypeLoc / ArrayTypeLoc /
// PointerTypeLoc / RecordTypeLoc / ...) for shape inspection. We peel
// here only to PROBE the kind — the SourceRange we ultimately return
// is the un-peeled (outer-elaboration-included) range so the rewrite
// covers any `sturm::frontend::` namespace prefix verbatim.
clang::TypeLoc peel_for_probe(clang::TypeLoc tl) {
    for (;;) {
        tl = tl.getUnqualifiedLoc();
        if (auto etl = tl.getAs<clang::ElaboratedTypeLoc>()) {
            tl = etl.getNamedTypeLoc();
            continue;
        }
        if (auto ptl = tl.getAs<clang::ParenTypeLoc>()) {
            tl = ptl.getInnerLoc();
            continue;
        }
        return tl;
    }
}

} // anonymous namespace

clang::SourceRange declarator_typeloc_range(const clang::DeclaratorDecl* dd) {
    if (!dd) return {};
    const clang::TypeSourceInfo* tsi = dd->getTypeSourceInfo();
    if (!tsi) return {};
    clang::TypeLoc outer = tsi->getTypeLoc();
    clang::TypeLoc probe = peel_for_probe(outer);
    // R5 typedef pin (outer carrier): when the user names a CARRIER
    // through a typedef (`using QArr = qint[4]; QArr a;` —
    // canonical type is `qint[4]`), the probe lands on a TypedefTypeLoc
    // wrapping an array/pointer. Decline so the typedef survives
    // byte-identical (plan §19b R5). We restrict the decline to
    // CARRIER typedefs by inspecting the canonical type — direct
    // typedefs of the frontend record (`using qint = frontend::qint;
    // qint x;`) keep their wave-1 rewrite behaviour (the TypedefTypeLoc
    // range covers the user's `qint` token; the emitter replaces it
    // with `sturm::qint_t<W>`).
    if (probe.getAs<clang::TypedefTypeLoc>()) {
        clang::QualType ct = dd->getType().getCanonicalType();
        if (ct->isArrayType() || ct->isPointerType()) return {};
        return outer.getSourceRange();
    }
    if (auto atl = probe.getAs<clang::ArrayTypeLoc>()) {
        // Array carrier: return the element TypeLoc range (which still
        // includes any namespace elaboration on the element type), so
        // the `[N]` punctuation survives verbatim. Re-probe the element
        // for the inner typedef pin (`using Q = qint; Q a[4];` — keep
        // the user's element typedef).
        clang::TypeLoc element = atl.getElementLoc();
        if (peel_for_probe(element).getAs<clang::TypedefTypeLoc>()) return {};
        return element.getSourceRange();
    }
    if (auto ptl = probe.getAs<clang::PointerTypeLoc>()) {
        // Pointer carrier: return the pointee TypeLoc range (which
        // still includes any namespace elaboration on the pointee),
        // so the `*` punctuation survives verbatim. Re-probe for the
        // inner typedef pin.
        clang::TypeLoc pointee = ptl.getPointeeLoc();
        if (peel_for_probe(pointee).getAs<clang::TypedefTypeLoc>()) return {};
        return pointee.getSourceRange();
    }
    // Direct shape — return the outer (un-peeled) range so any
    // `sturm::frontend::` namespace prefix is included in the rewrite
    // span (wave-1 contract).
    return outer.getSourceRange();
}

} // namespace sturm::transpile
