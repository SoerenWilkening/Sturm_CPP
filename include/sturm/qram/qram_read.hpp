#pragma once
// qram_read.hpp -- Runtime entry-point for QRAM-read (PRD §7;
// sturm-u9ge.13 D1; sturm-2w6h.2 B1; **sturm-44bt.4 BB4** rewires to
// the bucket-brigade DSL `lib_qram_read_bb_dsl<W, N>`).
//
// Public surface (D0a §11.1.1):
//   1. `QRAM_read(const std::array<qint_t<W>, N>&, const qint_t<W>&, qint_t<W>&)`
//   2. `QRAM_read(const qint_t<W> (&)[N], const qint_t<W>&, qint_t<W>&)`
//   3. `QRAM_read(const qint_t<W>*, std::size_t n, const qint_t<W>&, qint_t<W>&)`
//
// `__QRAM_read_adj<...>` mirrors each forward overload (D0d §11.4.2).
// All overloads `noexcept` per PRD §11.1.2.
//
// BB4: both QROM and qreg arms forward to the same BB body (algorithm
// is data-classicality-agnostic, PRD §4). Pointer overload's runtime
// `n` is routed via `switch(ceil_log2_index(n))` into a finite family
// of unrolled instantiations `N ∈ {2, 4, 8, ..., 1024}` (PRD §7).
// `n > 1024` fires `qram_read_n_over_cap_diagnose(n)` — debug
// `assert(false)`; release is UB-with-counter (Q5 / plan §5).
//
// Telemetry (B4 / sturm-2w6h.6 carried verbatim): umbrella
// `current_sink()->qram_read()` fires at the public entry-point;
// split `qrom_read()` / `qreg_read()` + umbrella thread-local
// `qram::g_qram_read_count` fire inside `dispatch_common`.
//
// LoC budget: ≤ 350 (plan §1, §5 / BB4).

#include "sturm/qtypes/qint_fwd.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/qint_alias.hpp"   // sturm-ddgo alias-index overloads
#include "sturm/routines/invert.hpp"
#include "sturm/core/counter_sink.hpp"
// BB4: backend-gated to keep frontend TUs (the C1 matcher site of
// `src/sturm/qram/qram_read.cpp`) in counter-mode stub semantics —
// `qbool_ops.hpp` (pulled by the BB DSL) collides with
// `qbool_logic.hpp` in the no-backend frontend build.
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/core/context.hpp"   // current_thread_context_or_null()
#  include "sturm/detail/lib/qram_read_bb_dsl.hpp"
#endif

#include <array>
#include <cstddef>
#include <cstdint>

namespace sturm {

// ── qram namespace: read-counter observability surface ───────────────
// Thread-local `qram_read` counter that observes dispatched calls
// without requiring a custom sink installation. PRD §11.2.7.
namespace qram {

inline thread_local std::size_t g_qram_read_count = 0;

inline std::size_t qram_read_count() noexcept { return g_qram_read_count; }
inline void reset_qram_read_count() noexcept  { g_qram_read_count = 0; }
inline void bump_qram_read_count() noexcept   { ++g_qram_read_count; }

}  // namespace qram

// ── TU-private dispatch helpers (D0b §11.2.2; BB4-wired) ────────────
// `qram_read_dispatch<W, N>` bumps the split counter + umbrella +
// forwarding-trace via `dispatch_common`, then routes into
// `lib_qram_read_bb_dsl<W, N>` (forward) or
// `__lib_qram_read_bb_dsl_adj<W, N>` (adjoint).
namespace _qram_detail {

// Test-only forwarding-trace hook (path_tag: 1 = QROM, 2 = QREG).
// Production code never installs. Pass nullptr to clear; returns
// the previous hook for nested-scope stashing.
using ForwardingTraceFn = void(*)(const void* a0_addr,
                                  std::size_t n,
                                  const void* i_addr,
                                  const void* b_addr,
                                  int path_tag);
ForwardingTraceFn set_forwarding_trace(ForwardingTraceFn hook) noexcept;

// Bumps split sink hook + umbrella thread-local + forwarding-trace.
// Body in .cpp; no gate emission here.
void dispatch_common(int path_tag,
                     const void* a0_addr,
                     std::size_t n,
                     const void* i_addr,
                     const void* b_addr) noexcept;

// BB4 over-cap diagnostic: pointer-overload switch-default when
// `n > 1024`. Debug: `assert(false)`. Release: fires the trace hook
// if installed (test-only) so callers can pin release behaviour
// without aborting. UB-with-counter per Q5 of plan §5.
using NOverCapDiagnoseFn = void(*)(std::size_t n);
NOverCapDiagnoseFn set_n_over_cap_diagnose_trace(NOverCapDiagnoseFn hook) noexcept;
void qram_read_n_over_cap_diagnose(std::size_t n) noexcept;

template <std::size_t W>
inline std::uint64_t any_super_mask(const qint_t<W>* a, std::size_t n) noexcept {
    std::uint64_t any_super = 0;
    for (std::size_t k = 0; k < n; ++k) any_super |= a[k].super_mask;
    return any_super;
}

// `ceil_log2_index(n)` selects the pointer-overload switch arm:
// n=0,1 → 0 (N=1, BB body skipped); n=2 → 1 (N=2); n=3,4 → 2 (N=4);
// … n=1024 → 10 (N=1024); n > 1024 → ≥ 11 (diagnose).
constexpr std::size_t ceil_log2_index(std::size_t n) noexcept {
    if (n <= 1u) return 0u;
    std::size_t r = 0u;
    std::size_t m = 1u;
    while (m < n) { m <<= 1u; ++r; }
    return r;
}

// BB body instantiation gate: N >= 2 (BB DSL minimum) AND W can carry
// the log2(N) address bits. When W < log2(N) the dispatch arm is
// unreachable at runtime for that (W, n) pair (UB on i overflow,
// PRD §5); we skip the body so the BB DSL is not forced to
// instantiate beyond W. Also runtime-gated on an EXPLICITLY-installed
// per-thread context — `current_thread_context_or_null()` returns
// null without the process-wide default fallback, so counter-mode
// telemetry-only tests stay in stub posture (D1 contract).
template <std::size_t W, std::size_t N>
inline constexpr bool bb_addressable_v =
    (N >= 2u) && (W >= _qram_detail::ceil_log2_index(N));

template <std::size_t W, std::size_t N>
inline void qram_read_dispatch(const qint_t<W>* a, std::size_t n,
                               const qint_t<W>& i, qint_t<W>& b,
                               std::uint64_t any_super) noexcept {
    const int path_tag = (any_super == 0u) ? /*QROM*/ 1 : /*QREG*/ 2;
    dispatch_common(path_tag, static_cast<const void*>(a), n,
                    static_cast<const void*>(&i),
                    static_cast<const void*>(&b));
#ifdef STURM_BACKEND_ENABLED
    if constexpr (bb_addressable_v<W, N>) {
        if (sturm::current_thread_context_or_null() != nullptr) {
            qint_t<W>& i_mut = const_cast<qint_t<W>&>(i);
            lib_qram_read_bb_dsl<W, N>(a, i_mut, b);
        }
    }
    (void)a; (void)i; (void)b;
#else
    (void)a; (void)i; (void)b;
#endif
    (void)n;
}

template <std::size_t W, std::size_t N>
inline void qram_read_dispatch_adj(const qint_t<W>* a, std::size_t n,
                                   const qint_t<W>& i, qint_t<W>& b,
                                   std::uint64_t any_super) noexcept {
    const int path_tag = (any_super == 0u) ? /*QROM*/ 1 : /*QREG*/ 2;
    dispatch_common(path_tag, static_cast<const void*>(a), n,
                    static_cast<const void*>(&i),
                    static_cast<const void*>(&b));
#ifdef STURM_BACKEND_ENABLED
    if constexpr (bb_addressable_v<W, N>) {
        if (sturm::current_thread_context_or_null() != nullptr) {
            qint_t<W>& i_mut = const_cast<qint_t<W>&>(i);
            __lib_qram_read_bb_dsl_adj<W, N>(a, i_mut, b);
        }
    }
    (void)a; (void)i; (void)b;
#else
    (void)a; (void)i; (void)b;
#endif
    (void)n;
}

}  // namespace _qram_detail

// ── (1) std::array<qint_t<W>, N> arm — D0a §11.1.1 overload 1 ───────
// Array overloads route the compile-time `N` straight to the BB body.
// BB5 (sturm-44bt.5) lifts the BB3 `is_pow2(N)` gate via phantom-leaf
// padding — non-pow2 `N` now compiles + instantiates at N' = next_pow2(N).
template <std::size_t W, std::size_t N>
inline void QRAM_read(const std::array<qint_t<W>, N>& a,
                      const qint_t<W>& i,
                      qint_t<W>& b) noexcept {
    if (Sink* s = current_sink()) s->qram_read();
    const auto any_super = _qram_detail::any_super_mask<W>(a.data(), N);
    _qram_detail::qram_read_dispatch<W, N>(a.data(), N, i, b, any_super);
}

// ── (2) Reference-to-C-array qint_t<W>[N] arm — D0a §11.1.1 overload 2 ─
template <std::size_t W, std::size_t N>
inline void QRAM_read(const qint_t<W> (&a)[N],
                      const qint_t<W>& i,
                      qint_t<W>& b) noexcept {
    if (Sink* s = current_sink()) s->qram_read();
    const auto any_super = _qram_detail::any_super_mask<W>(&a[0], N);
    _qram_detail::qram_read_dispatch<W, N>(&a[0], N, i, b, any_super);
}

// ── (3) Pointer arm qint_t<W>* + length — D0a §11.1.1 overload 3 ────
// BB4 (sturm-44bt.4) switch(n) dispatch table (PRD §7). The arm
// selector is `ceil_log2_index(n)` — n=0,1 → N=1 (no-op body); n=2 →
// N=2; n in [3, 4] → N=4; … n in [513, 1024] → N=1024. Out-of-cap
// (n > 1024) fires `qram_read_n_over_cap_diagnose(n)`. Phantom-leaf
// padding for non-pow2 n lands in BB5; until then, the BB body
// indexes a[0..N-1] so callers must size storage to at least N
// elements for n in {3, 5, 7, 1023}.
#define STURM_QRAM_DISPATCH_TABLE(DISPATCH_, A_, N_, I_, B_, ANY_)             \
    switch (_qram_detail::ceil_log2_index((N_))) {                              \
        case 0:  DISPATCH_<W, 1u>   ((A_), (N_), (I_), (B_), (ANY_)); return;   \
        case 1:  DISPATCH_<W, 2u>   ((A_), (N_), (I_), (B_), (ANY_)); return;   \
        case 2:  DISPATCH_<W, 4u>   ((A_), (N_), (I_), (B_), (ANY_)); return;   \
        case 3:  DISPATCH_<W, 8u>   ((A_), (N_), (I_), (B_), (ANY_)); return;   \
        case 4:  DISPATCH_<W, 16u>  ((A_), (N_), (I_), (B_), (ANY_)); return;   \
        case 5:  DISPATCH_<W, 32u>  ((A_), (N_), (I_), (B_), (ANY_)); return;   \
        case 6:  DISPATCH_<W, 64u>  ((A_), (N_), (I_), (B_), (ANY_)); return;   \
        case 7:  DISPATCH_<W, 128u> ((A_), (N_), (I_), (B_), (ANY_)); return;   \
        case 8:  DISPATCH_<W, 256u> ((A_), (N_), (I_), (B_), (ANY_)); return;   \
        case 9:  DISPATCH_<W, 512u> ((A_), (N_), (I_), (B_), (ANY_)); return;   \
        case 10: DISPATCH_<W, 1024u>((A_), (N_), (I_), (B_), (ANY_)); return;   \
        default: _qram_detail::qram_read_n_over_cap_diagnose((N_));  return;    \
    }

template <std::size_t W>
inline void QRAM_read(const qint_t<W>* a,
                      std::size_t n,
                      const qint_t<W>& i,
                      qint_t<W>& b) noexcept {
    if (Sink* s = current_sink()) s->qram_read();
    const auto any_super = _qram_detail::any_super_mask<W>(a, n);
    STURM_QRAM_DISPATCH_TABLE(_qram_detail::qram_read_dispatch, a, n, i, b, any_super)
}

// ── Adjoint companions per D0d §11.4.2 ───────────────────────────────
// One-for-one with the forward overload set (P9b). BB4 routes each
// into the registered `__lib_qram_read_bb_dsl_adj<W, N>` via
// `qram_read_dispatch_adj`. Forward body is self-inverse (PRD §4.4).

template <std::size_t W, std::size_t N>
inline void __QRAM_read_adj(const std::array<qint_t<W>, N>& a,
                            const qint_t<W>& i,
                            qint_t<W>& b) noexcept {
    if (Sink* s = current_sink()) s->qram_read();
    const auto any_super = _qram_detail::any_super_mask<W>(a.data(), N);
    _qram_detail::qram_read_dispatch_adj<W, N>(a.data(), N, i, b, any_super);
}

template <std::size_t W, std::size_t N>
inline void __QRAM_read_adj(const qint_t<W> (&a)[N],
                            const qint_t<W>& i,
                            qint_t<W>& b) noexcept {
    if (Sink* s = current_sink()) s->qram_read();
    const auto any_super = _qram_detail::any_super_mask<W>(&a[0], N);
    _qram_detail::qram_read_dispatch_adj<W, N>(&a[0], N, i, b, any_super);
}

template <std::size_t W>
inline void __QRAM_read_adj(const qint_t<W>* a,
                            std::size_t n,
                            const qint_t<W>& i,
                            qint_t<W>& b) noexcept {
    if (Sink* s = current_sink()) s->qram_read();
    const auto any_super = _qram_detail::any_super_mask<W>(a, n);
    STURM_QRAM_DISPATCH_TABLE(_qram_detail::qram_read_dispatch_adj, a, n, i, b, any_super)
}

// ── (sturm-ddgo) frontend::qint index overloads ─────────────────────
// The user-facing source shape `qint b = a[i];` declares `i` as a
// `sturm::frontend::qint`. The C1 matcher rewrites the line to
// `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);` — leaving `i`'s
// original declaration type unchanged. Expose a thin overload per
// container shape that takes `const sturm::frontend::qint&` for the
// index argument, forwarding to the canonical overload after a
// non-measuring `qint_t<W>(int64_t)` construction off the alias's
// `classical_value()`.
//
// Template-head trick: leading `bool _FrontendIdx = true` disambiguates
// from the canonical `<W[, N]>` overloads at the
// `STURM_REGISTER_ADJOINT(sturm::QRAM_read<8u>, ...)` macro at the
// bottom of this file (pointer overload resolves to the canonical
// `<W>` head, not the wrapper's `<bool, W>` head).

template <bool _FrontendIdx = true, std::size_t W, std::size_t N>
inline void QRAM_read(const std::array<qint_t<W>, N>& a,
                      const sturm::frontend::qint& i,
                      qint_t<W>& b) noexcept {
    QRAM_read(a, qint_t<W>(i.classical_value()), b);
}

template <bool _FrontendIdx = true, std::size_t W, std::size_t N>
inline void QRAM_read(const qint_t<W> (&a)[N],
                      const sturm::frontend::qint& i,
                      qint_t<W>& b) noexcept {
    QRAM_read(a, qint_t<W>(i.classical_value()), b);
}

template <bool _FrontendIdx = true, std::size_t W>
inline void QRAM_read(const qint_t<W>* a,
                      std::size_t n,
                      const sturm::frontend::qint& i,
                      qint_t<W>& b) noexcept {
    QRAM_read(a, n, qint_t<W>(i.classical_value()), b);
}

template <bool _FrontendIdx = true, std::size_t W, std::size_t N>
inline void __QRAM_read_adj(const std::array<qint_t<W>, N>& a,
                            const sturm::frontend::qint& i,
                            qint_t<W>& b) noexcept {
    __QRAM_read_adj(a, qint_t<W>(i.classical_value()), b);
}

template <bool _FrontendIdx = true, std::size_t W, std::size_t N>
inline void __QRAM_read_adj(const qint_t<W> (&a)[N],
                            const sturm::frontend::qint& i,
                            qint_t<W>& b) noexcept {
    __QRAM_read_adj(a, qint_t<W>(i.classical_value()), b);
}

template <bool _FrontendIdx = true, std::size_t W>
inline void __QRAM_read_adj(const qint_t<W>* a,
                            std::size_t n,
                            const sturm::frontend::qint& i,
                            qint_t<W>& b) noexcept {
    __QRAM_read_adj(a, n, qint_t<W>(i.classical_value()), b);
}

}  // namespace sturm

// ── STURM_REGISTER_ADJOINT enrolment per D0d §11.4.3 ─────────────────
// P9c registration via the standard macro, gated on STURM_BACKEND_ENABLED.
// The macro expands to `adjoint_of<&::fn>`; only the pointer overload
// (template head `<W>`) is unambiguous in standard C++ — overloads 1
// and 2 share `<W, N>` and would yield "address of overloaded function
// is ambiguous". Pointer registration covers the invert-lookup path
// for that arm; array / C-array adjoints remain callable directly.
//
// TODO(backend): when D0a v2 introduces a disambiguator (e.g. an
// `Idx` template parameter or distinct kind tags) for overloads 1
// and 2, add the missing two STURM_REGISTER_ADJOINT lines here.
#ifdef STURM_BACKEND_ENABLED
STURM_REGISTER_ADJOINT(sturm::QRAM_read<8u>,
                       sturm::__QRAM_read_adj<8u>)
#endif
