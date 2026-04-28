# Getting Started with STURM

**Status:** Draft (2026-04-27).
**Scope tag:** `packaging-export`.
**Companion doc:** [`public_api.md`](public_api.md).

This walkthrough takes a fresh checkout of STURM, installs it into a
local prefix, and consumes that prefix from a **standalone** CMake
project that knows nothing about the STURM source tree. It mirrors
`tests/external_consumer/` (E6.M1) line-for-line — the example **is**
the smoke test, kept in lockstep by a CI drift-check (E8.M2,
[`tools/extract_md_code_blocks.py`](../tools/extract_md_code_blocks.py))
that diffs the first C++ block below against
`tests/external_consumer/main.cpp` byte-for-byte. If you copy the code
on this page into a new project verbatim, it must build and run.

## Prerequisites

- **CMake** ≥ 3.16 (matches the
  `cmake_minimum_required` in the consumer `CMakeLists.txt` below and
  the project root).
- **A C++20 compiler.** STURM's transpiler is built against LLVM/Clang
  17 (see `README.md` "Prereqs"); the consumer project itself only
  needs a C++20-capable Clang for the `add_quantum_executable`
  plugin-mode invocation.
- **An installed STURM prefix.** From the STURM source tree:

  ```bash
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --parallel 6
  cmake --install build --prefix /path/to/sturm-prefix
  ```

  After installing, `/path/to/sturm-prefix` contains:
  - `include/sturm/...` — the public headers (umbrella:
    `<sturm/sturm.hpp>`, prelude: `<sturm/prelude.hpp>`).
  - `lib/cmake/sturm/sturmConfig.cmake` and `SturmTranspile.cmake` —
    the CMake package config and the `add_quantum_executable` helper.
  - `bin/sturm-transpile` and `bin/sturm-transpile-plugin` — the
    standalone transpiler binary and the Clang plugin shared library.

## 1. The standalone consumer project

Create a new directory anywhere on disk (it does **not** need to be a
subdirectory of the STURM source tree). It contains exactly two files,
`CMakeLists.txt` and `main.cpp`, both reproduced below verbatim from
`tests/external_consumer/`.

### `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)

# tests/external_consumer/CMakeLists.txt — sturm-795j.1 / E6.M1 acceptance
# project. This is a STANDALONE CMake project that consumes an *installed*
# STURM prefix via `find_package(sturm REQUIRED)`. PRD §3.6 makes this
# project the smoke test for the packaging-export delivery: it must
# configure, build, and run with nothing on disk except the contents of
# `<install-prefix>/include`, `<install-prefix>/lib/cmake/sturm/`, and
# `<install-prefix>/bin/sturm-transpile{,-plugin}` — i.e. exactly what
# `cmake --install` of the parent project ships (root CMakeLists.txt §1–3
# + transpiler/CMakeLists.txt §install).
#
# This same file doubles as the E8.M2 documentation walkthrough source
# (Plan §E8.M2): the comments narrate every step a downstream consumer
# would write themselves, so the file can be quoted verbatim into the
# packaging tutorial without rewriting.

# ── 1. Standalone project boilerplate ───────────────────────────────────────
# The smoke test is consumed by ctest via `cmake -S tests/external_consumer`,
# so it MUST be self-contained — no `add_subdirectory()`-style relationship
# with the parent build is allowed (PRD §3.6: "must work as if it were
# standalone").
project(sturm_external_consumer LANGUAGES CXX)

# ── 2. Locate the installed STURM package ───────────────────────────────────
# `find_package(sturm REQUIRED)` resolves via `CMAKE_PREFIX_PATH` (set by
# the test driver to a temp install prefix). It pulls in:
#   * `sturm` / `sturm::sturm` — the header-only INTERFACE target
#     exposing `<sturm/...>` headers (sturmConfig.cmake.in §1).
#   * `sturm-transpile` / `sturm::transpiler` — the standalone transpiler
#     binary IMPORTED at <prefix>/bin/sturm-transpile.
#   * `sturm-transpile-plugin` / `sturm::transpile-plugin` — the Clang
#     plugin shared library used by the default `STURM_TRANSPILE_MODE=plugin`
#     path of `add_quantum_executable`.
#   * `add_quantum_executable(<target> <src>...)` — the public build
#     helper, defined by the installed `SturmTranspile.cmake`
#     (sturmConfig.cmake.in §3 `include(...)`).
#
# A regression in any of those install rules surfaces here as a configure-
# time `find_package` failure or a build-time missing-target error.
find_package(sturm REQUIRED)

# ── 3. Build the routine through `add_quantum_executable` ───────────────────
# The function name is `add_quantum_executable` — installed CMake function
# (commit df0d7df renamed it from the historical `add_sturm_executable`
# spelling). It threads the source through the Clang plugin (default mode
# `plugin`), which runs the transpile pass IN MEMORY and feeds the
# rewritten buffer to the nested EmitObjAction. Per
# `cmake/SturmTranspile.cmake` it also auto-links the `sturm` interface
# target so `<sturm/prelude.hpp>` resolves without a follow-up
# `target_link_libraries(...)` call here.
#
# Side effect: the rewritten TU is mirrored to
# `${CMAKE_BINARY_DIR}/sturm_gen/main.cpp` for diagnostic inspection (the
# plugin's `dump-to=<path>` arg is wired by the helper). We do not assert
# on that file from this project — its contents are an in-tree concern.
add_quantum_executable(external_consumer main.cpp)
```

### `main.cpp`

```cpp
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
```

## 2. Configure, build, run

From the directory containing your new `CMakeLists.txt` and `main.cpp`:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sturm-prefix
cmake --build build --parallel 6
./build/external_consumer
```

Notes:

- `CMAKE_PREFIX_PATH` is the documented entry point for
  `find_package(sturm REQUIRED)`; point it at the prefix you passed to
  `cmake --install` above.
- The build is hard-capped at `--parallel 6` per project policy
  (`CLAUDE.md`).
- For a multi-config generator (Xcode, Visual Studio) the produced
  binary lives under `build/<Config>/external_consumer` instead of
  `build/external_consumer`.

## 3. Expected output

Running the built binary prints exactly:

```text
external_consumer: r=4
```

This is the classical `.value` of `(a + b) mod n` with `a=6, b=5, n=7`,
gated by a `qbool` guard known to be `true` at compile time. The
trailing newline is part of the contract: the smoke test driver
(`tests/external_consumer/run_smoke.py`) diffs stdout against
`tests/external_consumer/golden.txt` byte-for-byte.

## Next steps

- Read [`docs/public_api.md`](public_api.md) for the authoritative list
  of public symbols (`qint`, `qbool`, `WHEN`, the modular arithmetic
  functions, `invert<>`, `STURM_REGISTER_ADJOINT`, the version macros).
- Anything not on that list — including everything reachable only via
  `<sturm/detail/...>` — is internal and may change without notice.
