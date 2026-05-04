// tests/packaging/test_qint_resolution.cpp — sturm-65rs.6 / B1 acceptance.
//
// Pins the post-B1 type identity of `sturm::qint`:
//
//   1. `sturm::qint` IS `sturm::frontend::qint` (the alias re-exports the
//      frontend class — PRD §4.2).
//   2. A LOCAL `using qint = sturm::qint_t<W>;` continues to resolve to
//      `sturm::qint_t<W>` (PRD A6 / G2). 50+ existing transpiler fixtures
//      rely on this shadowing pattern and must keep compiling unchanged.
//   3. The frontend::qint implicit ctor + implicit `operator size_t()` are
//      reachable through the bare `sturm::qint` spelling (runtime smoke).

#include "sturm/qtypes/qint_fwd.hpp"
#include "sturm/qtypes/qint_alias.hpp"
#include "sturm/qtypes/qint.hpp"  // full qint_t<W> definition for shadow

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// ── (1) Bare `sturm::qint` resolves to the frontend alias class ─────────────
static_assert(std::is_same_v<::sturm::qint, ::sturm::frontend::qint>,
              "sturm::qint must resolve to sturm::frontend::qint after B1");

// Negative: bare `sturm::qint` is NOT `qint_t<64>` (the pre-B1 spelling).
static_assert(!std::is_same_v<::sturm::qint, ::sturm::qint_t<64>>,
              "sturm::qint must no longer resolve to sturm::qint_t<64>");

// ── (2) Local typedef shadow keeps existing fixture pattern working ─────────
// 50+ transpiler fixtures contain a local `using qint = sturm::qint_t<W>;`
// followed by `qint x;` / `qint a[N];` etc. After B1, the *bare* `sturm::qint`
// is the alias class — but inside a scope that introduces a local typedef,
// `qint` (unqualified) must shadow back to the local target (qint_t<W>).
namespace shadow_probe {
    using qint = ::sturm::qint_t<32>;
    static_assert(std::is_same_v<qint, ::sturm::qint_t<32>>,
                  "local `using qint = sturm::qint_t<32>;` must shadow the "
                  "bare sturm::qint inside the enclosing scope (PRD A6).");
    // And the shadow must NOT bleed into ::sturm::qint resolution.
    static_assert(!std::is_same_v<qint, ::sturm::qint>,
                  "local shadow must not change the meaning of "
                  "the fully-qualified ::sturm::qint name.");
}

// Same idea inside a function — exercises the more common fixture pattern.
inline void shadow_in_function() {
    using qint = ::sturm::qint_t<8>;
    qint x{};   // qint_t<8> default ctor
    (void)x;
    static_assert(std::is_same_v<qint, ::sturm::qint_t<8>>,
                  "function-scope `using qint = sturm::qint_t<8>;` shadow "
                  "must compile (PRD A6).");
}

// ── (3) Runtime smoke — bare sturm::qint reaches frontend::qint ops ─────────
int main() {
    // Force the shadow_in_function template-style probe to be instantiated
    // (it's inline; calling it ensures the static_asserts inside fire).
    shadow_in_function();

    ::sturm::frontend::qint_alias_detail::reset_measurement_count();
    assert(::sturm::frontend::qint_alias_detail::measurement_count() == 0);

    // Implicit ctor int64_t -> qint (P4a, free, no counter bump).
    ::sturm::qint q = 5;
    assert(::sturm::frontend::qint_alias_detail::measurement_count() == 0);

    // Implicit operator size_t() — bumps the counter (PRD §4.1).
    std::size_t s = q;
    (void)s;
    assert(::sturm::frontend::qint_alias_detail::measurement_count() == 1);

    return 0;
}
