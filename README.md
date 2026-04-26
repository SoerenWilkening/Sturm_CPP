# STURM

STURM is a C++ DSL for quantum-classical programming that lets you express reversible circuits in ordinary C++ syntax (qbool, qint, `|`, `^`, `~`, `when`). A Clang LibTooling transpiler (`sturm-transpile`) analyzes your source at compile time and injects the adjoint (uncompute) statements automatically, so you never hand-write inverses.

See [docs/01_principles.md](docs/01_principles.md) for the design principles.

## Install

**macOS (Homebrew):**

```
brew tap soerenwilkening/sturm
brew install sturm-transpile
```

**Linux / macOS (prebuilt binary):**

```
# Pick your platform triple:
#   x86_64-linux, aarch64-linux, x86_64-macos, aarch64-macos
TRIPLE=x86_64-linux
curl -LO https://github.com/SoerenWilkening/Sturm_CPP/releases/latest/download/sturm-transpile-v0.1.2-${TRIPLE}.tar.gz
tar xzf sturm-transpile-v0.1.2-${TRIPLE}.tar.gz
sudo cp -r sturm-transpile-v0.1.2-${TRIPLE}/* /usr/local/
```

**Docker:**

```
docker run -v $PWD:/w ghcr.io/soerenwilkening/sturm-transpile:latest /w/src.cpp \
  --output-dir /w/out --extra-arg=-I/opt/sturm/include --extra-arg=-std=c++20
```

**Build from source (contributors):**

```
git clone https://github.com/SoerenWilkening/Sturm_CPP.git
cd Sturm_CPP
cmake -S . -B build -DLLVM_DIR=$(llvm-config-17 --cmakedir) \
      -DClang_DIR=/usr/lib/llvm-17/lib/cmake/clang
cmake --build build -j6
```

Build-time options (pass with `-D<NAME>=<VALUE>` at configure time; run `cmake -LH build` for the full list):

| Flag | Default | Effect |
|---|---|---|
| `STURM_MODULAR_POW` | `OFF` | Gates the transpiler rewrite of `pow(a, x) % n` to the single `lib_pow_mod_dsl` primitive. With the default `OFF`, the two-step `lib_pow_dsl + lib_mod_dsl` lowering is preserved bit-exactly; with `ON`, the matcher folds the pair into one call without the wide intermediate. The companion `add` / `mul` modular rewrites are unconditional — only `pow` is gated. The library does not check the precondition `a, b ∈ [0, n)`; calling the modular operators with unreduced operands is undefined behaviour. See [docs/prd_modular_arithmetic.md](docs/prd_modular_arithmetic.md) §3.4 / §5. |
| `STURM_ANCILLA_CAPACITY` | `256` | Ancilla pool capacity (qubits) compiled into the runtime. |

## Getting started

Write a quantum routine in plain C++ using `sturm::qbool`:

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

See [examples/or_circuit.cpp](examples/or_circuit.cpp) for a runnable demo covering OR, NOT, and XOR-assign patterns, and [examples/in_memory_transpile.cpp](examples/in_memory_transpile.cpp) for a single-TU tour of Phase E compound expressions, Phase F `WHEN`-lift, and Phase J PJ-1 zero-ancilla fusion compiled through the default plugin mode.

## How it works

- [docs/01_principles.md](docs/01_principles.md) — core design principles (write-forward-only, compile-time uncompute, ancilla discipline).
- [docs/roadmap_transpiler_post_mvp.md](docs/roadmap_transpiler_post_mvp.md) — phase-by-phase completion log for phases A through L.

## Contributing

[AGENTS.md](AGENTS.md) and [CLAUDE.md](CLAUDE.md) define the agent workflow used by this project. Issue tracking is done with [beads](https://github.com/steveyegge/beads) (`bd`); run `bd ready` to find work that is unblocked and available.

## License

AGPL-3.0-or-later. See the [LICENSE](LICENSE) file.
