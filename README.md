# STURM

STURM is a C++ DSL for quantum-classical programming that lets you express reversible circuits in ordinary C++ syntax (qbool, qint, `|`, `^`, `~`, `when`). A Clang LibTooling transpiler (`sturm-transpile`) analyzes your source at compile time and injects the adjoint (uncompute) statements automatically, so you never hand-write inverses.

See [docs/01_principles.md](docs/01_principles.md) for the design principles.

## Prerequisites

STURM builds the transpiler against the LLVM/Clang development headers
and consumes its own headers from a C++20 toolchain.

- **LLVM/Clang ≥ 17** — the transpiler links `libclang-cpp` and `libLLVM`
  via `find_package(LLVM CONFIG)` / `find_package(Clang CONFIG)` and
  hard-fails configure if `LLVM_PACKAGE_VERSION < 17.0`
  (see `transpiler/CMakeLists.txt` §"Locate LLVM + Clang"). On
  Debian/Ubuntu install `llvm-17-dev libclang-17-dev clang-17`; on
  macOS `brew install llvm@17`.
- **CMake ≥ 3.16** — matches `cmake_minimum_required(VERSION 3.16)` in
  the project root and the transpiler subproject.
- **A C++20 compiler** — needed both for building STURM and for any
  consumer translation unit that includes `<sturm/sturm.hpp>` /
  `<sturm/prelude.hpp>`. The default `add_quantum_executable` integration
  routes consumer sources through Clang via the transpiler plugin, so a
  C++20-capable Clang is required at consume time as well.

## Install & consume

The supported flow is to install STURM into a prefix and `find_package`
it from a standalone consumer project. The walkthrough in
[docs/getting_started.md](docs/getting_started.md) shows the full
consumer side end-to-end (mirrored byte-for-byte from
`tests/external_consumer/`); the steps below build and install the
prefix that walkthrough then consumes.

```bash
git clone https://github.com/SoerenWilkening/Sturm_CPP.git
cd Sturm_CPP
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DLLVM_DIR=$(llvm-config-17 --cmakedir) \
      -DClang_DIR=/usr/lib/llvm-17/lib/cmake/clang
cmake --build build --parallel 6
cmake --install build --prefix /path/to/sturm-prefix
```

After install the prefix contains:

- `include/sturm/...` — public headers (umbrella `<sturm/sturm.hpp>`,
  prelude `<sturm/prelude.hpp>`).
- `lib/cmake/sturm/sturmConfig.cmake` + `SturmTranspile.cmake` — the
  CMake package config and the `add_quantum_executable` helper.
- `bin/sturm-transpile` and `lib/libsturm-transpile-plugin.{so,dylib}` —
  the standalone transpiler binary and the Clang plugin.

Then, from a standalone consumer project, `find_package(sturm REQUIRED)`
+ `add_quantum_executable(my_program main.cpp)` is the entire
integration. See [docs/getting_started.md](docs/getting_started.md) for
the complete walkthrough (consumer `CMakeLists.txt` + `main.cpp`,
configure/build/run, expected output) and
[docs/public_api.md](docs/public_api.md) for the authoritative list of
public symbols exposed by the umbrella header.

Build-time options (pass with `-D<NAME>=<VALUE>` at configure time; run `cmake -LH build` for the full list):

| Flag | Default | Effect |
|---|---|---|
| `STURM_MODULAR_POW` | `OFF` | Gates the transpiler rewrite of `pow(a, x) % n` to the single `lib_pow_mod_dsl` primitive. With the default `OFF`, the two-step `lib_pow_dsl + lib_mod_dsl` lowering is preserved bit-exactly; with `ON`, the matcher folds the pair into one call without the wide intermediate. The companion `add` / `mul` modular rewrites are unconditional — only `pow` is gated. The library does not check the precondition `a, b ∈ [0, n)`; calling the modular operators with unreduced operands is undefined behaviour. See [docs/archive/prd_modular_arithmetic.md](docs/archive/prd_modular_arithmetic.md) §3.4 / §5. |
| `STURM_ANCILLA_CAPACITY` | `256` | Ancilla pool capacity (qubits) compiled into the runtime. |

## Getting started

A minimal consumer routine using the public headers (the
[walkthrough](docs/getting_started.md) has the full standalone CMake
project — including `find_package(sturm REQUIRED)` and
`add_quantum_executable` — that compiles this code):

```cpp
#define STURM_BACKEND_ENABLED 1
#include "sturm/sturm.hpp"
#include "sturm/core/context.hpp"
#include "sturm/qtypes/qint.hpp"

int main() {
    auto *ctx = sturm_backend_create(STURM_MODE_APPEND, 32);
    sturm_set_thread_context(ctx);
    {
        sturm::qbool a, b;
        a.value = 1;
        b.value = 0;
        sturm::qbool c = a | b;   // transpiler injects OR-uncompute
    }
    sturm_set_thread_context(nullptr);
    sturm_backend_destroy(ctx);
}
```

Wire it into your build in **one line**:

```cmake
find_package(sturm REQUIRED)
add_quantum_executable(my_program main.cpp)
```

That is the whole integration. Under the hood `add_quantum_executable()` invokes the Clang compile with `-fplugin=$<TARGET_FILE:sturm-transpile-plugin>`, which loads `libsturm-transpile-plugin.{so,dylib}` and runs the shared transpiler consumer against the AST **in memory**. No sibling `.cpp` is written to disk; the rewritten buffer is fed straight into Clang's codegen. Every Sturm install (Homebrew bottle, prebuilt tarball, Docker image) ships both `bin/sturm-transpile` and `lib/libsturm-transpile-plugin.{so,dylib}`, so the one-line integration works out of the box.

Need the rewritten source on disk for inspection? Opt into legacy two-step mode at configure time:

```cmake
# Legacy: write sturm_gen/<path>.cpp first, then compile it. Kept for debugging.
set(STURM_TRANSPILE_MODE "dump")
find_package(sturm REQUIRED)
add_quantum_executable(my_program main.cpp)
```

Or ad-hoc on the standalone binary with `--dump-transpiled=<path>`. Both paths converge on the same file-write code in `transpiler/src/io.cpp`, so the dump output is byte-identical to the in-memory buffer the plugin feeds to codegen.

In-tree examples [`examples/or_circuit.cpp`](examples/or_circuit.cpp)
(OR, NOT, and XOR-assign patterns) and
[`examples/in_memory_transpile.cpp`](examples/in_memory_transpile.cpp)
(Phase E compound expressions, Phase F `WHEN`-lift, Phase J PJ-1
zero-ancilla fusion through the default plugin mode) are wired into the
in-tree build tree and intended for hacking on STURM itself; downstream
consumers should follow the install + consume flow above instead.

## How it works

- [docs/public_api.md](docs/public_api.md) — authoritative list of public symbols reachable through `<sturm/sturm.hpp>` (`qint`, `qbool`, `WHEN`, modular arithmetic, `invert<>`, `STURM_REGISTER_ADJOINT`, version macros).
- [docs/getting_started.md](docs/getting_started.md) — install + standalone-consumer walkthrough.
- [docs/01_principles.md](docs/01_principles.md) — core design principles (write-forward-only, compile-time uncompute, ancilla discipline).
- [docs/roadmap_transpiler_post_mvp.md](docs/roadmap_transpiler_post_mvp.md) — phase-by-phase completion log for phases A through L.

## Developer build (working on STURM itself)

Contributors hacking on the transpiler or runtime can build and test
in-tree without installing:

```bash
git clone https://github.com/SoerenWilkening/Sturm_CPP.git
cd Sturm_CPP
cmake -S . -B build -DLLVM_DIR=$(llvm-config-17 --cmakedir) \
      -DClang_DIR=/usr/lib/llvm-17/lib/cmake/clang
cmake --build build --parallel 6
ctest --test-dir build --parallel 6 --output-on-failure
```

This is the workflow used by CI and the in-tree examples under
`examples/`; it is **not** the recommended path for downstream
consumers — they should follow [Install & consume](#install--consume)
above and [docs/getting_started.md](docs/getting_started.md). All
`cmake`/`ctest`/`make`/`ninja` invocations are capped at 6 threads per
project policy ([CLAUDE.md](CLAUDE.md)).

## Contributing

[AGENTS.md](AGENTS.md) and [CLAUDE.md](CLAUDE.md) define the agent workflow used by this project. Issue tracking is done with [beads](https://github.com/steveyegge/beads) (`bd`); run `bd ready` to find work that is unblocked and available.

## License

AGPL-3.0-or-later. See the [LICENSE](LICENSE) file.
