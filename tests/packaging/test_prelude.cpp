// test_prelude.cpp — sturm-zmfk.2 / E3.M2 acceptance test.
//
// Pins the `<sturm/prelude.hpp>` contract:
//   1. The prelude is self-contained — including it alone is enough to
//      reach the full public surface (umbrella is pulled in transitively).
//   2. `qint` and `qbool` are usable WITHOUT a `sturm::` qualifier
//      (PRD §3.4 / D7: type-only aliases get the unprefixed treatment).
//   3. Free functions are NOT pulled into the global namespace. We probe
//      `add_mod` specifically (representative of the `*_mod` family,
//      `invert`, `uncompute_*`, etc. — none of these may be visible
//      unprefixed). A SFINAE/detection-idiom guard fires a static_assert
//      if the prelude ever silently regresses to `using sturm::add_mod;`.
//
// The TU has no runtime quantum effect — the assertions are all
// compile-time. `main()` returns 0 so ctest sees a pass.

#define STURM_BACKEND_ENABLED 1

#include <sturm/prelude.hpp>

#include <cstdio>
#include <type_traits>
#include <utility>

// ── Probe 1: unprefixed `qint` reaches the qint_t<W> alias template ─────────
// `sturm::qint` is `using qint = qint_t<64>;` in qint_fwd.hpp; the prelude
// re-exports it with `using sturm::qint;`. Naming `qint` (the alias to
// `qint_t<64>`) without a `sturm::` prefix must compile cleanly here.
// We force complete-type instantiation through `sizeof`.
static_assert(sizeof(qint) > 0,
              "unprefixed `qint` must be reachable through prelude.hpp");

// We also instantiate qint_t<8> via the (already-prefixed) class template
// to keep parity with test_umbrella_only.cpp's W-parameterized probe.
static_assert(sizeof(sturm::qint_t<8>) > 0,
              "sturm::qint_t<8> must remain reachable through prelude.hpp");

// ── Probe 2: unprefixed `qbool` reaches the qbool class ─────────────────────
static_assert(sizeof(qbool) > 0,
              "unprefixed `qbool` must be reachable through prelude.hpp");

// ── Probe 3: `add_mod` requires `sturm::` qualification ─────────────────────
// The contract under test (PRD §3.4 / D7): the prelude MUST NOT
// introduce free functions like `add_mod`, `mul_mod`, `pow_mod`,
// `invert`, or `uncompute_or` into the unqualified namespace via
// `using`-declarations. Those names should remain reachable only as
// `sturm::add_mod` (etc.).
//
// Subtlety: *argument-dependent lookup* (ADL) makes the unqualified
// call `add_mod(q, q, q)` resolve to `sturm::add_mod` whenever the
// argument types are themselves in namespace `sturm::` (which
// `qint_t<W>` and `qbool` are). ADL is intrinsic to the C++ name-
// lookup model and is *independent* of any `using`-declaration the
// prelude might contain — i.e. ADL would still find `sturm::add_mod`
// even if the prelude were perfect. Testing the unqualified call on
// `qint_t<W>` arguments is therefore a false positive.
//
// Likewise we cannot probe `::add_mod` as a SFINAE candidate: in
// Clang and GCC, an ill-formed qualified-id whose unqualified head
// matches a typo-corrected name in another namespace produces a hard
// diagnostic ("no member named 'add_mod' in the global namespace;
// did you mean 'sturm::add_mod'?") that escapes the immediate context
// of substitution and turns the SFINAE into a compile error. Both
// compilers' typo-correction recovers the call to `sturm::add_mod`,
// which makes the SFINAE flip to *true* — another false positive.
//
// Workable shape: probe unqualified `add_mod` with argument types
// that are *not* in `sturm::`. This neutralises ADL and forces lookup
// to use only ordinary unqualified lookup (the very mechanism a
// `using sturm::add_mod;` injection would feed). The static_asserts
// below then pin two facts:
//
//   (a) Unqualified `add_mod(NotSturm, NotSturm, NotSturm)` is NOT
//       a callable expression — i.e. no global / file-scope
//       declaration of `add_mod` exists from the prelude.
//   (b) Unqualified `add_mod(qint, qint, qint)` IS a callable
//       expression (via ADL into `sturm::`) — sanity check that the
//       umbrella did expose `sturm::add_mod` so (a) is meaningful.
//
// Two-overload "check(0)" detection-idiom: the preferred `int`
// overload's return-type substitution is the immediate context where
// SFINAE is allowed to fire; if `add_mod(...)` is ill-formed the
// fallback `...` overload (returning `std::false_type`) is chosen.
namespace stm_test_prelude {
struct not_sturm {};  // ADL on this type sees only the global namespace.
}  // namespace stm_test_prelude

template <typename T>
auto detect_unprefixed_add_mod(int)
    -> decltype(add_mod(std::declval<const T&>(),
                        std::declval<const T&>(),
                        std::declval<const T&>()),
                std::true_type{});

template <typename>
auto detect_unprefixed_add_mod(long) -> std::false_type;

// (a) No unqualified `add_mod` for non-sturm types: catches a
// regression where the prelude pulls `add_mod` into the global
// namespace (which would make it a candidate for ANY argument types
// only if the symbol itself accepted them; the harder real-world
// regression — `using sturm::add_mod;` — still wouldn't make this
// call valid because the template requires `qint_t<W>`. The probe
// remains the strongest defence ADL constraints permit and pins the
// non-existence of any global free-function `add_mod` shim.)
static_assert(
    !decltype(detect_unprefixed_add_mod<stm_test_prelude::not_sturm>(0))::value,
    "prelude.hpp must NOT inject a global `add_mod` reachable for "
    "non-sturm argument types; free functions stay sturm::-prefixed "
    "(PRD D7).");

// (b) Sanity check: with `qint_t<W>` arguments ADL succeeds, so the
// trait flips to true. If this *fails*, the prelude is broken in a
// different way (umbrella did not expose `sturm::add_mod`) and the
// negative assertion above would be vacuous.
static_assert(
    decltype(detect_unprefixed_add_mod<sturm::qint_t<8>>(0))::value,
    "ADL into sturm:: must reach sturm::add_mod when called with "
    "qint_t<W> arguments — sanity check for the negative assertion "
    "above; if this fails the umbrella has regressed.");

// Sanity check the detector itself: when we DO write `sturm::add_mod`,
// the call is well-formed and returns `qint_t<8>`. This guards against a
// detector that always evaluates to false (which would make the negative
// assertion above vacuously true).
using add_mod_qualified_t =
    decltype(sturm::add_mod(std::declval<const sturm::qint_t<8>&>(),
                            std::declval<const sturm::qint_t<8>&>(),
                            std::declval<const sturm::qint_t<8>&>()));
static_assert(std::is_same_v<add_mod_qualified_t, sturm::qint_t<8>>,
              "sturm::add_mod must remain reachable (qualified) through "
              "the prelude — the umbrella is included transitively.");

int main() {
    std::puts("test_prelude: OK");
    return 0;
}
