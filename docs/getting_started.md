# Getting Started with STURM

**Status:** Draft (2026-04-28).
**Scope tag:** `packaging-export`.
**Companion doc:** [`public_api.md`](public_api.md).

This walkthrough takes a fresh checkout of STURM, installs it into a
local prefix, and consumes that prefix from a **standalone** CMake
project that knows nothing about the STURM source tree. If you copy the
two files on this page into a new project verbatim, they must build and
run.

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
`CMakeLists.txt` and `main.cpp`, shown below.

### `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)
project(sturm_external_consumer LANGUAGES CXX)

# Resolve the installed STURM package via CMAKE_PREFIX_PATH. This pulls
# in the `sturm` header-only INTERFACE target, the imported transpiler
# binary, the imported Clang plugin, and the `add_quantum_executable`
# helper.
find_package(sturm REQUIRED)

# `add_quantum_executable` runs the source through the STURM Clang
# plugin (which transpiles in memory and feeds the rewritten buffer to
# codegen) and links the `sturm` interface target so `<sturm/...>`
# headers resolve without any extra `target_link_libraries(...)` call.
add_quantum_executable(external_consumer main.cpp)
```

### `main.cpp`

```cpp
// A minimal STURM consumer. Demonstrates qint/qbool construction from
// classical literals, a WHEN-guarded modular update on the classical-
// true short-circuit, and the public `static_cast<int64_t>` readout.
#include <sturm/prelude.hpp>
#include <cstdio>

int main() {
    qint a = 6, b = 5, n = 7;     // unprefixed `qint = qint_t<64>` via prelude
    qbool flag = true;            // classical-true: WHEN body runs
    int64_t r = static_cast<int64_t>(a);
    WHEN(flag) {
        r = (static_cast<int64_t>(a) + static_cast<int64_t>(b))
            % static_cast<int64_t>(n);
    }
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

This is the classical value of `(a + b) mod n` with `a=6, b=5, n=7`,
gated by a `qbool` guard known to be `true` at compile time.

## Next steps

- Read [`docs/public_api.md`](public_api.md) for the authoritative list
  of public symbols (`qint`, `qbool`, `WHEN`, the modular arithmetic
  functions, `invert<>`, `STURM_REGISTER_ADJOINT`, the version macros).
- Anything not on that list — including everything reachable only via
  `<sturm/detail/...>` — is internal and may change without notice.
