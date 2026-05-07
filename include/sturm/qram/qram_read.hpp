#pragma once
// qram_read.hpp -- Runtime entry-point for QRAM-read across the three
// container shapes from PRD §7 (sturm-u9ge.13 / Beat D1; refactored
// in sturm-2w6h.2 / Beat B1 of the QRAM backend gate-emission epic).
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
// (`qrom_read` / `qreg_read`, PRD §11.2.7) wired in B4 / sturm-2w6h.6.
//
// ── B4 (sturm-2w6h.6) telemetry wiring ──────────────────────────────
// The public `QRAM_read` overloads bump the umbrella
// `current_sink()->qram_read()` hook once per dispatched call right
// here at the entry-point. The corresponding split `qrom_read()` /
// `qreg_read()` sink hooks fire from inside the QROM / qreg helper
// via `_qram_detail::dispatch_common`. Split + umbrella sink hooks
// fire from non-overlapping sites; the thread-local
// `qram::g_qram_read_count` continues to bump from `dispatch_common`
// once per dispatched call. Mirrored on `__QRAM_read_adj`.
//
// ── B1 (sturm-2w6h.2) refactor ──────────────────────────────────────
// `_qram_detail::qram_read_qrom_impl` / `qram_read_qreg_impl` are now
// **template functions** parameterised on `(W)` taking
// `(const qint_t<W>* a, std::size_t n, const qint_t<W>& i,
// qint_t<W>& b)`. The three public `QRAM_read` overloads and the
// three `__QRAM_read_adj` overloads forward `(a, i, b)` (and `n`,
// inferred from `N` for the std::array / C-array shapes) to the
// helper instead of discarding them. Bodies remain counter-mode
// bumps in this beat — arguments are forwarded but unused, so
// observable behaviour is unchanged. Gate-emission lands in B2.
//
// LoC budget: <= 250 (plan §1, §5 / B1).

#include "sturm/qtypes/qint_fwd.hpp"
#include "sturm/qtypes/qint.hpp"
// sturm-ddgo: the C1 / H1 / H4 matchers rewrite user-facing source
// shapes like `qint b = a[i];` (where `i` is a `frontend::qint`) into
// `::sturm::QRAM_read(a, i, b);`. Because `i` retains its original
// declaration type, the runtime must accept a `frontend::qint` index
// alongside the canonical `qint_t<W>` form. Pulling the alias header
// in here lets us declare the wrapper overloads below; the ones that
// take `frontend::qint` simply forward to the corresponding
// `qint_t<W>` overload after a non-measuring `qint_t<W>(int64_t)`
// construction off the alias's `classical_value()`.
#include "sturm/qtypes/qint_alias.hpp"
#include "sturm/routines/invert.hpp"
// sturm-2w6h.6 (Beat B4): the public `QRAM_read` and `__QRAM_read_adj`
// overloads bump the umbrella `current_sink()->qram_read()` hook once
// per dispatched call (the split `qrom_read` / `qreg_read` hooks fire
// from inside the corresponding QROM / qreg helper via
// `_qram_detail::dispatch_common`). Including `counter_sink.hpp`
// pulls the `current_sink()` accessor and the `Sink` interface into
// scope at the public entry-point.
#include "sturm/core/counter_sink.hpp"
// sturm-2w6h.4 (Beat B2): wire QROM helper to call the DSL XOR-fanout body.
// `lib_qram_read_qrom_dsl` is a header-only template — the include is light.
// Backend-gated: the DSL body's transitive include chain pulls
// `qbool_ops.hpp`, which redefines `qbool::operator~` etc. that
// `qbool_logic.hpp` already defines for the no-backend frontend
// build. Frontend translation units (e.g. `src/sturm/qram/qram_read.cpp`
// configured without `STURM_BACKEND_ENABLED`) keep the counter-mode
// stub semantics — gate emission only fires when the backend is
// compiled in.
#ifdef STURM_BACKEND_ENABLED
#  include "sturm/detail/lib/qram_read_dsl.hpp"
#endif

#include <array>
#include <cstddef>
#include <cstdint>

namespace sturm {

// ── qram namespace: read-counter observability surface ───────────────
// Thread-local `qram_read` counter that observes dispatched calls
// without requiring a custom sink installation. PRD §11.2.7 (single
// `qram_read` counter for D1; per-path counters land with gate
// emission). The Wave-2 G6 `test_sturm_gen_clean` gate is the
// canonical pre-transpile coverage check (frontend alias erasure);
// this counter is the runtime observability surface for backend tests.
namespace qram {

inline thread_local std::size_t g_qram_read_count = 0;

inline std::size_t qram_read_count() noexcept { return g_qram_read_count; }
inline void reset_qram_read_count() noexcept  { g_qram_read_count = 0; }
inline void bump_qram_read_count() noexcept   { ++g_qram_read_count; }

}  // namespace qram

// ── TU-private dispatch helpers (D0b §11.2.2) ────────────────────────
// B1 (sturm-2w6h.2): the QROM/qreg helpers are now `inline template
// <W>` taking `(const qint_t<W>* a, std::size_t n, const qint_t<W>& i,
// qint_t<W>& b)` — every public overload forwards the args it
// received instead of discarding them. Both helpers route through
// the shared (non-template) `dispatch_common(path_tag, a0, n, &i, &b)`
// in `src/qram/qram_read.cpp` which:
//   1. Bumps the **path-specific split counter** on the active sink
//      (`qrom_read()` for path tag 1, `qreg_read()` for path tag 2 —
//      surface from B0 / sturm-2w6h.1, wired here in B4 /
//      sturm-2w6h.6).
//   2. Bumps the umbrella thread-local `qram::g_qram_read_count`
//      counter (preserves the D1 contract pinned by
//      `tests/qram/test_qram_read_stub.cpp` and
//      `transpiler/tests/test_qram_e2e.cpp`).
//   3. Invokes the test-only forwarding-trace if installed
//      (path tag 1 = QROM, 2 = QREG; pinned by
//      `tests/qram/test_qram_read_dispatch.cpp`).
//
// The umbrella `Sink::qram_read()` hook is **not** fired from
// `dispatch_common` — it fires from the public `QRAM_read` /
// `__QRAM_read_adj` entry-points below so split + umbrella sink
// hooks land from non-overlapping sites and cannot be double-counted.
namespace _qram_detail {

// Test-only forwarding-trace hook. Production code never installs.
// Args: (a0, n, &i, &b, path_tag) where path_tag is 1=QROM, 2=QREG.
using ForwardingTraceFn = void(*)(const void* a0_addr,
                                  std::size_t n,
                                  const void* i_addr,
                                  const void* b_addr,
                                  int path_tag);

// Install / uninstall trace hook. Pass nullptr to clear. Returns
// the previous hook for nested-scope stashing. Definition in .cpp.
ForwardingTraceFn set_forwarding_trace(ForwardingTraceFn hook) noexcept;

// Shared body — bumps umbrella counter + sink hook, fires trace.
void dispatch_common(int path_tag,
                     const void* a0_addr,
                     std::size_t n,
                     const void* i_addr,
                     const void* b_addr) noexcept;

template <std::size_t W>
inline std::uint64_t any_super_mask(const qint_t<W>* a, std::size_t n) noexcept {
    std::uint64_t any_super = 0;
    for (std::size_t k = 0; k < n; ++k) any_super |= a[k].super_mask;
    return any_super;
}

// ── B1 template helpers: forward `(a, n, i, b)` ─────────────────────
// PRD §11.2.1: QROM path runs when every container element is fully
// classical (super_mask == 0) — multiplexed XOR-fanout indexed by `i`.
//
// B1 (sturm-2w6h.2): `dispatch_common` bumps the umbrella `qram_read`
// counter and fires the test-only forwarding-trace hook.
// B2 (sturm-2w6h.4): after the umbrella bump, the helper calls
// `lib_qram_read_qrom_dsl` to actually emit the XOR-fanout gate stream
// per PRD §4. The DSL body uses pure DSL primitives (predicate
// compute/uncompute + WHEN-lifted CXs) — no raw gate calls. The
// helper is cast away from `noexcept` only by `lib_qram_read_qrom_dsl`
// — that template is not declared `noexcept` because its inner
// `WHEN` macro and `BitProxy` operators are not annotated; the call
// is conditionally noexcept and any exception escaping it would
// trigger `std::terminate` per the helper's `noexcept`. In practice
// the DSL ops are exception-safe (no allocations escape), so this is
// a documentation point only.
//
// `i` is taken by const reference here per the public surface, but
// the predicate helper inside `lib_qram_read_qrom_dsl` transiently
// flips bits of `i.qubits` (with paired un-flips). Cast away const
// inside the helper call site so the inner DSL body can run; the
// constness contract on the public surface is preserved because the
// flips are paired and `i.value` / `i.super_mask` / `i.qubits` are
// unchanged at scope exit. This mirrors the pattern in
// `tests/lib/test_qram_read_predicate.cpp` which calls the helper on
// a freshly bound non-const reference.
template <std::size_t W>
inline void qram_read_qrom_impl(const qint_t<W>* a,
                                std::size_t n,
                                const qint_t<W>& i,
                                qint_t<W>& b) noexcept {
    dispatch_common(/*QROM*/ 1, static_cast<const void*>(a), n,
                    static_cast<const void*>(&i),
                    static_cast<const void*>(&b));
#ifdef STURM_BACKEND_ENABLED
    // sturm-2w6h.4 / Beat B2: emit the QROM XOR-fanout gate stream.
    // Const-cast is safe: predicate helper pairs every flip on `i`'s
    // qubits with an un-flip, so net change is zero.
    qint_t<W>& i_mut = const_cast<qint_t<W>&>(i);
    lib_qram_read_qrom_dsl<W>(a, n, i_mut, b);
#else
    (void)a; (void)n; (void)i; (void)b;
#endif
}

// PRD §11.2.1: qreg path runs when any container element carries a
// superposed bit — SWAP-style fanout controlled on `i`. Counter-mode
// stub in B1: same umbrella bump as QROM; split counter lands B4.
template <std::size_t W>
inline void qram_read_qreg_impl(const qint_t<W>* a,
                                std::size_t n,
                                const qint_t<W>& i,
                                qint_t<W>& b) noexcept {
    dispatch_common(/*QREG*/ 2, static_cast<const void*>(a), n,
                    static_cast<const void*>(&i),
                    static_cast<const void*>(&b));
}

// ── B3 adjoint helper: routes through __lib_qram_read_qrom_dsl_adj ──
// Counter contract matches the forward (umbrella + trace fire once).
// The QROM body is self-adjoint (PRD §4 paragraph 4), but routing
// through the named adjoint symbol exercises the
// STURM_REGISTER_ADJOINT enrolment end-to-end via the public
// __QRAM_read_adj surface (P9c placement-audit spirit). qreg adjoint
// stays on `qram_read_qreg_impl` — the v1 qreg helper is a
// counter-only stub, so the same body runs in both directions.
template <std::size_t W>
inline void qram_read_qrom_impl_adj(const qint_t<W>* a,
                                    std::size_t n,
                                    const qint_t<W>& i,
                                    qint_t<W>& b) noexcept {
    dispatch_common(/*QROM*/ 1, static_cast<const void*>(a), n,
                    static_cast<const void*>(&i),
                    static_cast<const void*>(&b));
#ifdef STURM_BACKEND_ENABLED
    qint_t<W>& i_mut = const_cast<qint_t<W>&>(i);
    __lib_qram_read_qrom_dsl_adj<W>(a, n, i_mut, b);
#else
    (void)a; (void)n; (void)i; (void)b;
#endif
}

}  // namespace _qram_detail

// ── (1) std::array<qint_t<W>, N> arm — D0a §11.1.1 overload 1 ───────
template <std::size_t W, std::size_t N>
inline void QRAM_read(const std::array<qint_t<W>, N>& a,
                      const qint_t<W>& i,
                      qint_t<W>& b) noexcept {
    // sturm-2w6h.6 (Beat B4): umbrella sink hook fires here at the
    // public entry-point — split counter fires inside the helper via
    // `dispatch_common` (non-overlapping sites; cannot double-count).
    if (Sink* s = current_sink()) s->qram_read();
    const auto any_super = _qram_detail::any_super_mask<W>(a.data(), N);
    if (any_super == 0u) _qram_detail::qram_read_qrom_impl<W>(a.data(), N, i, b);
    else                 _qram_detail::qram_read_qreg_impl<W>(a.data(), N, i, b);
}

// ── (2) Reference-to-C-array qint_t<W>[N] arm — D0a §11.1.1 overload 2 ─
template <std::size_t W, std::size_t N>
inline void QRAM_read(const qint_t<W> (&a)[N],
                      const qint_t<W>& i,
                      qint_t<W>& b) noexcept {
    if (Sink* s = current_sink()) s->qram_read();
    const auto any_super = _qram_detail::any_super_mask<W>(&a[0], N);
    if (any_super == 0u) _qram_detail::qram_read_qrom_impl<W>(&a[0], N, i, b);
    else                 _qram_detail::qram_read_qreg_impl<W>(&a[0], N, i, b);
}

// ── (3) Pointer arm qint_t<W>* + length — D0a §11.1.1 overload 3 ────
template <std::size_t W>
inline void QRAM_read(const qint_t<W>* a,
                      std::size_t n,
                      const qint_t<W>& i,
                      qint_t<W>& b) noexcept {
    if (Sink* s = current_sink()) s->qram_read();
    const auto any_super = _qram_detail::any_super_mask<W>(a, n);
    if (any_super == 0u) _qram_detail::qram_read_qrom_impl<W>(a, n, i, b);
    else                 _qram_detail::qram_read_qreg_impl<W>(a, n, i, b);
}

// ── Adjoint companions per D0d §11.4.2 ───────────────────────────────
// One-for-one with the forward overload set (P9b: same parameter
// list, `b` non-const out-param). Counter-mode body bumps the same
// `qram_read` counter via the shared template helpers; gate-level
// inversion lands in B3 (sturm-2w6h.5) by routing through the
// `qram_read_qrom_impl_adj` helper which calls the registered
// `__lib_qram_read_qrom_dsl_adj` for the QROM path.

template <std::size_t W, std::size_t N>
inline void __QRAM_read_adj(const std::array<qint_t<W>, N>& a,
                            const qint_t<W>& i,
                            qint_t<W>& b) noexcept {
    // sturm-2w6h.6 (Beat B4): umbrella sink hook fires at the public
    // adjoint entry-point too — same non-overlap discipline as the
    // forward overload (split counter still fires inside the helper).
    if (Sink* s = current_sink()) s->qram_read();
    const auto any_super = _qram_detail::any_super_mask<W>(a.data(), N);
    if (any_super == 0u) _qram_detail::qram_read_qrom_impl_adj<W>(a.data(), N, i, b);
    else                 _qram_detail::qram_read_qreg_impl<W>(a.data(), N, i, b);
}

template <std::size_t W, std::size_t N>
inline void __QRAM_read_adj(const qint_t<W> (&a)[N],
                            const qint_t<W>& i,
                            qint_t<W>& b) noexcept {
    if (Sink* s = current_sink()) s->qram_read();
    const auto any_super = _qram_detail::any_super_mask<W>(&a[0], N);
    if (any_super == 0u) _qram_detail::qram_read_qrom_impl_adj<W>(&a[0], N, i, b);
    else                 _qram_detail::qram_read_qreg_impl<W>(&a[0], N, i, b);
}

template <std::size_t W>
inline void __QRAM_read_adj(const qint_t<W>* a,
                            std::size_t n,
                            const qint_t<W>& i,
                            qint_t<W>& b) noexcept {
    if (Sink* s = current_sink()) s->qram_read();
    const auto any_super = _qram_detail::any_super_mask<W>(a, n);
    if (any_super == 0u) _qram_detail::qram_read_qrom_impl_adj<W>(a, n, i, b);
    else                 _qram_detail::qram_read_qreg_impl<W>(a, n, i, b);
}

// ── (sturm-ddgo) frontend::qint index overloads ─────────────────────
// The user-facing source shape `qint b = a[i];` declares `i` as a
// `sturm::frontend::qint` (PRD §4.1 alias class). The C1 matcher
// rewrites the line to `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);`
// — leaving `i`'s original declaration type unchanged. To keep the
// rewritten file compiling without forcing the user to also rewrite
// the declaration of `i`, expose a thin overload per container shape
// that takes `const sturm::frontend::qint&` for the index argument.
//
// Body posture: build a `qint_t<W>` from the alias's
// `classical_value()` (a direct field read on the alias's classical
// part) and forward to the canonical overload. Wave 3
// (sturm-v0db.4 / W3.3, PRD §10.3.2 / G8) made every other alias
// body a pure type-stub; this is the only path that does not
// zero-initialise. The Wave-2 G6 `test_sturm_gen_clean` gate pins
// post-rewrite invariants on the emitted source. Mirrored on
// `__QRAM_read_adj` so the matcher's planted adjoint resolves too.
//
// Template-head trick: the leading `bool _FrontendIdx = true` parameter
// gives every wrapper a distinct template head from the canonical
// `<std::size_t W[, std::size_t N]>` overloads. That keeps the
// `STURM_REGISTER_ADJOINT(sturm::QRAM_read<8u>, ...)` macro at the
// bottom of this file unambiguous: `QRAM_read<8u>` resolves only to
// the canonical pointer overload (template head `<W>`), never to the
// wrapper (template head `<bool, W>`). The default value lets users
// continue to call `QRAM_read(a, i, b)` without spelling the bool
// out — argument-type deduction picks the right overload.

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
