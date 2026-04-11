// instantiations.cpp — M25: Explicit template instantiation of qint_t<W>
//                       for the four canonical widths.
//
// Including the full qint.hpp umbrella exposes all member and free-function
// template bodies.  The explicit instantiation directives below force the
// compiler to emit definition-strength symbols for each combination,
// satisfying the linker for any TU that declared those specialisations via
// `extern template` (see qint.hpp).
//
// Build requirement: must be compiled with STURM_BACKEND_ENABLED defined so
// that the uncompute_ member and its associated destructor/constructor paths
// are included.

// Suppress the extern-template declarations that qint.hpp emits under
// STURM_BACKEND_ENABLED — this TU IS the definition provider, not a consumer.
// We achieve this by temporarily undefining the guard, or more simply by
// defining a sentinel macro before including and then providing the explicit
// instantiations after.  The standard approach: just include the full header
// (extern template declarations are fine here; explicit instantiation below
// overrides them in this TU).

#include "sturm/qtypes/qint.hpp"

// ── Explicit instantiations ───────────────────────────────────────────────────
// Each directive instantiates the entire class template — all member functions,
// constructors, destructor — for the given Width.  Free operator templates
// (operator+, operator-, etc.) are function templates, not class members, so
// they are NOT instantiated here; those remain header-inline.
//
// The four widths below match PRD §13 and the issue description.

template class sturm::qint_t<1>;
template class sturm::qint_t<4>;
template class sturm::qint_t<8>;
template class sturm::qint_t<16>;
template class sturm::qint_t<32>;
