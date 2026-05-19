# Getting Started with STURM

**Status:** v2 (2026-05-07). Updated for the
[`archive/prd_frontend_simplification.md`](archive/prd_frontend_simplification.md)
umbrella + auto-injected lifecycle.
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
  - `include/sturm.h` — the curated public-API umbrella.
  - `include/sturm/qram.h` — opt-in feature header for the
    `qint b = a[i];` subscript-rewrite hook.
  - `include/sturm/draw_ascii.h` — opt-in feature header for the
    no-arg renderer entry points (`sturm::print_ascii()`,
    `sturm::draw_ascii()`, `sturm::gate_count()`).
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
# codegen) and links the `sturm` interface target so `sturm.h` and the
# opt-in `<sturm/...>` headers resolve without any extra
# `target_link_libraries(...)` call.
add_quantum_executable(external_consumer main.cpp)
```

### `main.cpp`

```cpp
// A minimal STURM consumer. The umbrella `sturm.h` brings the curated
// public API (qint, qbool, the C-ABI lifecycle) and `using sturm::qint;
// using sturm::qbool;` into scope. The transpiler's
// `matcher_main_lifecycle` (PRD §5.4) wraps `main` with the
// `sturm_backend_create` / `sturm_backend_destroy` pair automatically;
// the user body is unchanged.
#include "sturm.h"
#include <cstdio>

int main() {
    qint a = 6, b = 5, n = 7;     // umbrella's `using sturm::qint`
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

## 4. Mode selection at build time

`STURM_MODE` selects the backend mode the auto-injected lifecycle
calls `sturm_backend_create` with: `APPEND` (default), `COUNT`, or
`SIMULATE`. Override at configure time:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sturm-prefix \
                   -DSTURM_MODE=SIMULATE
```

Switching modes mid-program is **not** supported via the auto-injected
path. A user who needs runtime mode selection writes the explicit
`sturm_backend_create` call themselves and adds
`#define STURM_NO_AUTO_LIFECYCLE` before including `sturm.h`. See
`examples/explicit_lifecycle.cpp` for a runnable counter-example.

## ⚠️ Measurement footgun: `qint → integer` is destructive

Any conversion of a `qint` to a classical integer is a **destructive
quantum measurement** that collapses superposition. The `static_cast<int64_t>(a)`
calls in the example above are explicit measurements (and are intentional —
that is how you read out a result).

The footgun is the *implicit* version. The frontend `qint` alias carries an
implicit `operator size_t()` so that `qint b = a[i];` is a valid C++
expression the transpiler can recognise and rewrite to `QRAM_read(a, i, b)`
without measuring `i`. The same implicit conversion fires in any other
integral context — `int x = q;`, `std::vector<int> v(q);`,
`for (size_t i = 0; i < q; ++i)` — and **silently measures** the `qint`
at runtime, producing classical output that *looks* correct but has
collapsed the quantum state.

**Rules.** Use `static_cast<int64_t>(q)` deliberately when you mean to
measure. For QRAM access write the exact shape `qint b = a[i];` (a fresh
declaration on the LHS, no surrounding expression). Other shapes — `b = a[i];`
with existing `b`, `a[i] = b;`, `a[i] += b;`, `c = a[i] + d;` — are
diagnosed by the transpiler as out-of-scope (PRD §9, plan H1–H4) rather
than silently measured. The full version of this warning, the supported /
unsupported QRAM shape table, and the post-transpile safety-net argument
live in [`docs/qram_user_intro.md`](qram_user_intro.md). Background and
the open follow-ups live in
[`docs/archive/prd_qram_subscript.md`](archive/prd_qram_subscript.md)
§10.1.

## Next steps

- Read [`docs/public_api.md`](public_api.md) for the authoritative list
  of public symbols (`qint`, `qbool`, `WHEN`, the modular arithmetic
  functions, `invert<>`, `STURM_REGISTER_ADJOINT`, the version macros).
- Read [`docs/qram_user_intro.md`](qram_user_intro.md) for the full
  user-facing introduction to QRAM: the `qint b = a[i];` shape, the
  supported / unsupported container shapes, and the full version of the
  measurement-footgun explanation.
- Read [`docs/archive/prd_frontend_simplification.md`](archive/prd_frontend_simplification.md)
  for the design rationale behind the umbrella + auto-injected
  lifecycle (G1–G6, A1–A7).
- Anything not on the public-API list — including everything reachable
  only via `<sturm/detail/...>` — is internal and may change without
  notice.
