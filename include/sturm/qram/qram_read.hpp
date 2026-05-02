#pragma once
// qram_read.hpp -- Runtime entry-point for QRAM-read across the three
// container shapes from PRD §7 (sturm-u9ge.13 / Beat D1).
//
// Pins per `docs/prd_qram_subscript.md` §11.1 (D0a — three free-function
// template overloads) / §11.2 (D0b — runtime mask-OR dispatch into two
// TU-private helpers in src/qram/qram_read.cpp) / §11.4 (D0d —
// `__QRAM_read_adj` adjoint registration via P9c's `STURM_REGISTER_ADJOINT`).
//
// Public surface (D0a §11.1.1):
//   1. `QRAM_read(const std::array<qint_t<W>, N>&, const qint_t<W>&, qint_t<W>&)`
//   2. `QRAM_read(const qint_t<W> (&)[N], const qint_t<W>&, qint_t<W>&)`
//   3. `QRAM_read(const qint_t<W>*, std::size_t n, const qint_t<W>&, qint_t<W>&)`
//
// `__QRAM_read_adj<...>` mirrors each forward overload one-for-one
// (D0d §11.4.2). All overloads `noexcept` per PRD §11.1.2.
//
// Counter-mode body (D1 stub): each call routes through the runtime
// mask-OR dispatch (PRD §11.2.2) into a TU-private helper that bumps
// the process-wide `qram_read` counter once. Both QROM (all elements
// classical) and qreg (any element superposed) helpers route to the
// same counter for D1 per the issue description; per-path split
// (`qrom_read` / `qreg_read`, PRD §11.2.7) lands with gate emission.
//
// LoC budget: <= 200 (plan §1, §7 / D1).

#include "sturm/qtypes/qint_fwd.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/routines/invert.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace sturm {

// ── qram namespace: read-counter observability surface ───────────────
// Mirrors `qint_alias_detail::g_measurement_count` shape so the
// observability works without a bespoke sink. PRD §11.2.7 (single
// `qram_read` counter for D1; per-path counters land with gate
// emission).
namespace qram {

inline thread_local std::size_t g_qram_read_count = 0;

inline std::size_t qram_read_count() noexcept { return g_qram_read_count; }
inline void reset_qram_read_count() noexcept  { g_qram_read_count = 0; }
inline void bump_qram_read_count() noexcept   { ++g_qram_read_count; }

}  // namespace qram

// ── TU-private dispatch helpers (D0b §11.2.2) ────────────────────────
// Declared in the header so the inline forward / adjoint bodies below
// can call them; defined in `src/qram/qram_read.cpp`. The runtime
// mask-OR dispatch lives in each public overload's body — a single
// call site covers both QROM and qreg execution paths. Both helpers
// bump the same counter in D1; gate-level QROM/qreg distinction is
// out of scope here.
//
// TODO(backend): split into `qrom_read` / `qreg_read` per §11.2.7
// once gate emission lands.
namespace _qram_detail {

void qram_read_qrom_impl() noexcept;
void qram_read_qreg_impl() noexcept;

template <std::size_t W>
inline std::uint64_t any_super_mask(const qint_t<W>* a, std::size_t n) noexcept {
    std::uint64_t any_super = 0;
    for (std::size_t k = 0; k < n; ++k) any_super |= a[k].super_mask;
    return any_super;
}

}  // namespace _qram_detail

// ── (1) std::array<qint_t<W>, N> arm — D0a §11.1.1 overload 1 ───────
template <std::size_t W, std::size_t N>
inline void QRAM_read(const std::array<qint_t<W>, N>& a,
                      const qint_t<W>& /*i*/,
                      qint_t<W>& /*b*/) noexcept {
    const auto any_super = _qram_detail::any_super_mask<W>(a.data(), N);
    if (any_super == 0u) _qram_detail::qram_read_qrom_impl();
    else                 _qram_detail::qram_read_qreg_impl();
}

// ── (2) Reference-to-C-array qint_t<W>[N] arm — D0a §11.1.1 overload 2 ─
template <std::size_t W, std::size_t N>
inline void QRAM_read(const qint_t<W> (&a)[N],
                      const qint_t<W>& /*i*/,
                      qint_t<W>& /*b*/) noexcept {
    const auto any_super = _qram_detail::any_super_mask<W>(&a[0], N);
    if (any_super == 0u) _qram_detail::qram_read_qrom_impl();
    else                 _qram_detail::qram_read_qreg_impl();
}

// ── (3) Pointer arm qint_t<W>* + length — D0a §11.1.1 overload 3 ────
template <std::size_t W>
inline void QRAM_read(const qint_t<W>* a,
                      std::size_t n,
                      const qint_t<W>& /*i*/,
                      qint_t<W>& /*b*/) noexcept {
    const auto any_super = _qram_detail::any_super_mask<W>(a, n);
    if (any_super == 0u) _qram_detail::qram_read_qrom_impl();
    else                 _qram_detail::qram_read_qreg_impl();
}

// ── Adjoint companions per D0d §11.4.2 ───────────────────────────────
// One-for-one with the forward overload set (P9b: same parameter
// list, `b` non-const out-param). Counter-mode body bumps the same
// `qram_read` counter; gate-level inversion lands later.

template <std::size_t W, std::size_t N>
inline void __QRAM_read_adj(const std::array<qint_t<W>, N>& a,
                            const qint_t<W>& /*i*/,
                            qint_t<W>& /*b*/) noexcept {
    const auto any_super = _qram_detail::any_super_mask<W>(a.data(), N);
    if (any_super == 0u) _qram_detail::qram_read_qrom_impl();
    else                 _qram_detail::qram_read_qreg_impl();
}

template <std::size_t W, std::size_t N>
inline void __QRAM_read_adj(const qint_t<W> (&a)[N],
                            const qint_t<W>& /*i*/,
                            qint_t<W>& /*b*/) noexcept {
    const auto any_super = _qram_detail::any_super_mask<W>(&a[0], N);
    if (any_super == 0u) _qram_detail::qram_read_qrom_impl();
    else                 _qram_detail::qram_read_qreg_impl();
}

template <std::size_t W>
inline void __QRAM_read_adj(const qint_t<W>* a,
                            std::size_t n,
                            const qint_t<W>& /*i*/,
                            qint_t<W>& /*b*/) noexcept {
    const auto any_super = _qram_detail::any_super_mask<W>(a, n);
    if (any_super == 0u) _qram_detail::qram_read_qrom_impl();
    else                 _qram_detail::qram_read_qreg_impl();
}

}  // namespace sturm

// ── STURM_REGISTER_ADJOINT enrolment per D0d §11.4.3 ─────────────────
// P9c registration via the standard macro, gated on STURM_BACKEND_ENABLED
// per the pattern in `include/sturm/detail/lib/mod_dsl_adj.hpp:83-86`.
//
// The macro expands to `adjoint_of<&::fn>`; the address-of-template
// must be unambiguous. Only the pointer overload (template head `<W>`)
// is unambiguous in standard C++ — overloads 1 and 2 share `<W, N>`
// and would yield "address of overloaded function is ambiguous" at
// the macro expansion point. The pointer registration covers the
// invert-lookup path for that arm; the array / C-array adjoints
// remain defined above and are callable directly (the test pins the
// forward call + counter pin for those shapes).
//
// TODO(backend): when D0a v2 introduces a disambiguator (e.g. an
// `Idx` template parameter or distinct kind tags) for overloads 1
// and 2, add the missing two STURM_REGISTER_ADJOINT lines here.
#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::QRAM_read<8u>,
                       sturm::__QRAM_read_adj<8u>)
#endif
