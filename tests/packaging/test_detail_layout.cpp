// test_detail_layout.cpp — sturm-nalq.1 / E2.M1 acceptance test.
//
// Pins the public-vs-detail header split established in E2.M1: this TU
// includes ONLY the headers classified `public` in
// docs/transpiler_emit_targets.md (plus the umbrella header
// `<sturm/sturm.hpp>` itself), then instantiates the simplest public
// template entry point (`sturm::qint_t<8>`). If any public header
// silently relied on a `sturm/lib/...` or `sturm/qtypes/lossy_oop.hpp`
// / `divide_oop.hpp` / `bit_proxy.hpp` path that no longer exists
// post-move, the consumer #include chain inside one of these public
// headers will fail to resolve and this TU will not compile.
//
// The test has no runtime assertions beyond a single static_assert on
// `sizeof(sturm::qint_t<8>) > 0` (sufficient to force template
// instantiation past the forward decl) and a side-effect-free use of
// `sturm::add_mod`'s declared signature via decltype. The real signal is
// that the file COMPILES and LINKS using only the public surface.
//
// Public headers included below (each anchored on a `public`-classified
// symbol from docs/transpiler_emit_targets.md):
//
//   * <sturm/sturm.hpp>                   — umbrella
//   * <sturm/qtypes/qint.hpp>             — qint_t<W> (full def; PRD §3.3)
//   * <sturm/qtypes/qbool.hpp>            — qbool full definition (D7)
//   * <sturm/control/when_fwd.hpp>        — qbool forward (matcher-emitted)
//   * <sturm/control/when.hpp>            — WHEN macro (preprocessor public)
//   * <sturm/ops/qint_modular.hpp>        — add_mod / mul_mod / pow_mod
//   * <sturm/routines/invert.hpp>         — invert<&fn>()
//   * <sturm/uncompute/uncompute_api.hpp> — uncompute_or
//
// Crucially this TU does NOT include any `sturm/lib/...` path, nor any
// `sturm/qtypes/lossy_oop.hpp` / `divide_oop.hpp` / `bit_proxy.hpp`
// path. After E2.M1 those paths no longer exist; their replacements
// live under `sturm/detail/` and are pulled in TRANSITIVELY by the
// public headers above. If a public header gains a direct dependency on
// a now-moved internal path without going through the `sturm/detail/`
// indirection, this TU stops compiling — that is the regression net.

// STURM_BACKEND_ENABLED selects the backend-coupled definitions in
// qbool_ops.hpp / qint_modular.hpp / detail/qtypes/bit_proxy.hpp; the
// alternative is `qbool_logic.hpp`'s frontend-only stubs. Real
// downstream consumers using `qint`/`add_mod` from a packaged install
// build with this defined (cf. tests/lib/test_qint_modular.cpp), so
// pin the same posture for the public-header layout test.
#define STURM_BACKEND_ENABLED 1

#include <sturm/sturm.hpp>
#include <sturm/qtypes/qint.hpp>
#include <sturm/qtypes/qbool.hpp>
#include <sturm/control/when_fwd.hpp>
#include <sturm/control/when.hpp>
#include <sturm/ops/qint_modular.hpp>
#include <sturm/routines/invert.hpp>
#include <sturm/uncompute/uncompute_api.hpp>

#include <cstdio>
#include <type_traits>
#include <utility>

// Force instantiation of the simplest public template entry point.
// `qint_t<8>` is the canonical 8-bit specialization (PRD §3.3 names the
// type as the headline user-facing class template). Wrapping in a
// static_assert on `sizeof` proves the type is COMPLETE, not merely
// forward-declared, in this TU's view of the public headers.
static_assert(sizeof(sturm::qint_t<8>) > 0,
              "qint_t<8> must be complete via public headers alone");

// Probe `add_mod` declaration through decltype — does not need a
// backend context to compile (the body is templated on W and only
// instantiates lazily). This catches public-header drift in the
// modular-arithmetic surface (PRD §3.3 D2).
using add_mod_signature_8 =
    decltype(sturm::add_mod(std::declval<const sturm::qint_t<8>&>(),
                            std::declval<const sturm::qint_t<8>&>(),
                            std::declval<const sturm::qint_t<8>&>()));
static_assert(std::is_same_v<add_mod_signature_8, sturm::qint_t<8>>,
              "add_mod(qint_t<8>, qint_t<8>, qint_t<8>) must return "
              "qint_t<8>");

// Probe `invert<&fn>()` — public NTTP-keyed adjoint lookup, header-only.
namespace stm_test_detail_layout {
inline void fwd() noexcept {}
inline void fwd_adj() noexcept {}
}  // namespace stm_test_detail_layout

STURM_REGISTER_ADJOINT(stm_test_detail_layout::fwd,
                       stm_test_detail_layout::fwd_adj)

int main() {
    constexpr auto adj = sturm::invert<&stm_test_detail_layout::fwd>();
    adj();  // side-effect-free, just forces the constexpr lookup to
            // instantiate
    std::puts("test_detail_layout: OK");
    return 0;
}
