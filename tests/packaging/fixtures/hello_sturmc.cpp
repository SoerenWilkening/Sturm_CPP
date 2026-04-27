// hello_sturmc.cpp — sturm-vr0v.2 fixture for tests/packaging/test_sturmc.py.
//
// Minimal program that exercises the sturmc transpile + compile + link
// pipeline against an installed STURM prefix. The file:
//
//   1. Includes a public sturm header (`<sturm/routines/invert.hpp>`),
//      which forces sturmc's `-I${prefix}/include` to be wired correctly.
//      `invert.hpp` is fully header-only (no symbols come out of any
//      .cpp under src/sturm/), so the link step succeeds today even
//      though the runtime library `libsturm.{a,so,dylib}` is not yet
//      shipped (PRD §3.3 / D3 — runtime split is future work).
//
//   2. Calls `sturm::invert<&fn>()` for a forward `fn` registered with
//      `STURM_REGISTER_ADJOINT`, so the umbrella's NTTP-keyed adjoint
//      lookup is exercised at compile time. The trip through the
//      transpiler is a no-op pass-through (no qint / qbool / WHEN
//      tokens) — exactly what we want for a packaging smoke test:
//      the transpiler must NOT corrupt arbitrary C++.
//
//   3. Prints a fixed string to stdout that the test asserts on.
//
// Keep this file dependency-free at the link layer. Adding a header
// whose .cpp implementation lives under src/sturm/ will turn this into
// a libsturm-link smoke test instead of an include-path smoke test;
// that variant belongs in the §3.6 external smoke (E6.M3).

#include <sturm/routines/invert.hpp>

#include <cstdio>

namespace stm_test {
inline void fwd() noexcept {}
inline void fwd_adj() noexcept {}
}  // namespace stm_test

STURM_REGISTER_ADJOINT(stm_test::fwd, stm_test::fwd_adj)

int main() {
    // Compile-time NTTP adjoint lookup; the result is a function pointer.
    constexpr auto adj = sturm::invert<&stm_test::fwd>();
    adj();
    std::printf("sturmc-ok\n");
    return 0;
}
