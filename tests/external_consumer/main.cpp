// main.cpp — sturm-795j.1 / E6.M1 external smoke-test consumer.
//
// Compiled by an OUT-OF-TREE CMake project that does
// `find_package(sturm REQUIRED)` + `add_quantum_executable` (PRD §3.6).
// Proves the installed package (headers + sturmConfig.cmake +
// SturmTranspile.cmake + sturm-transpile-plugin) is self-contained;
// nothing in this file reaches into the in-tree build tree.
//
// Spec coverage (E6.M1): `qbool` (unprefixed via `<sturm/prelude.hpp>`
// per PRD §3.4 / D7) and `qint` reached as `sturm::qint_t<64>` via a
// function-scoped using-decl (sturm-7uhu — see comment in `main()` for
// rationale: post-B1 the prelude-injected unprefixed `qint` is
// `sturm::frontend::qint`, whose `value_` field is private; the
// classical reads below need the backend surface). `WHEN(...)` is
// exercised on the classical-true short-circuit (super_mask=0,
// value=1), and `sturm::add_mod` reached at compile time via
// `decltype` so no ODR-use forces a link-time dependency on the
// deferred runtime split (PRD §3.3 / D3 — find_package(sturm) ships
// only headers today; mirrors tests/packaging/fixtures/hello_sturmc.cpp).
// Output: the classical `.value` of a guarded modular update; diffed
// vs. `golden.txt` by `run_smoke.py`.
#include <sturm/prelude.hpp>
#include <cstdio>
#include <utility>

// `decltype` query is not an ODR-use: links cleanly without libsturm.
using add_mod_t =
    decltype(sturm::add_mod(std::declval<const sturm::qint_t<8>&>(),
                            std::declval<const sturm::qint_t<8>&>(),
                            std::declval<const sturm::qint_t<8>&>()));
static_assert(sizeof(add_mod_t) > 0, "sturm::add_mod must be reachable");

// `add_quantum_executable` defines STURM_BACKEND_ENABLED=1 target-wide
// (SturmTranspile.cmake §plugin-mode), so WhenGuard's ctor/dtor
// reference this runtime TLS getter. The call is dead on the classical
// path (super_mask=0 ⇒ pushed_to_ctx_stack_ branches never run), but
// the linker still needs a definition; nullptr is safe (every caller
// null-checks). Removed when libsturm ships (PRD §3.3 / D3).
extern "C" sturm_backend_context_t* sturm_get_thread_context(void) {
    return nullptr;
}

int main() {
    // sturm-7uhu (D0 re-spell miss): `<sturm/prelude.hpp>` injects
    // `qint` as `sturm::frontend::qint` (post-B1 / sturm-65rs.6), which
    // has no public `.value` field — its classical bits live in a
    // private `value_` member reachable only via `classical_value()` /
    // `operator int64_t()`. The reads of `a.value`, `b.value`, `n.value`
    // below need the backend `sturm::qint_t<64>` surface, so we follow
    // PRD §6 R1's mechanical migration recipe (same shape as
    // sturm-pl7o / sturm-ypit / sturm-ztmf): a function-scoped
    // `using qint = sturm::qint_t<64>;` shadows the prelude-injected
    // unprefixed name only inside `main()`, restoring backend
    // semantics for the three classical reads. Drift back to the
    // frontend alias on this site is pinned by
    // tests/regressions/test_qint_callsite_respelling_value.cpp.
    using qint = sturm::qint_t<64>;
    qint a = 6, b = 5, n = 7;     // local using-decl: backend qint_t<64>
    qbool flag = true;            // classical-true: WHEN body runs
    int64_t r = a.value;
    WHEN(flag) { r = (a.value + b.value) % n.value; }
    std::printf("external_consumer: r=%lld\n", static_cast<long long>(r));
    return 0;
}
