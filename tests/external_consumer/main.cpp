// main.cpp — sturm-795j.1 / E6.M1 external smoke-test consumer.
//
// Compiled by an OUT-OF-TREE CMake project that does
// `find_package(sturm REQUIRED)` + `add_quantum_executable` (PRD §3.6).
// Proves the installed package (headers + sturmConfig.cmake +
// SturmTranspile.cmake + sturm-transpile-plugin) is self-contained;
// nothing in this file reaches into the in-tree build tree.
//
// Spec coverage (E6.M1): `qint` + `qbool` (unprefixed via
// `<sturm/prelude.hpp>` per PRD §3.4 / D7), `WHEN(...)` on the classical-
// true short-circuit (super_mask=0, value=1), and `sturm::add_mod`
// reached at compile time via `decltype` so no ODR-use forces a link-
// time dependency on the deferred runtime split (PRD §3.3 / D3 —
// find_package(sturm) ships only headers today; mirrors
// tests/packaging/fixtures/hello_sturmc.cpp). Output: the classical
// `.value` of a guarded modular update; diffed vs. `golden.txt` by
// `run_smoke.py`.
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
    qint a = 6, b = 5, n = 7;     // unprefixed `qint = qint_t<64>` via prelude
    qbool flag = true;            // classical-true: WHEN body runs
    int64_t r = a.value;
    WHEN(flag) { r = (a.value + b.value) % n.value; }
    std::printf("external_consumer: r=%lld\n", static_cast<long long>(r));
    return 0;
}
