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
curl -LO https://github.com/SoerenWilkening/Sturm_CPP/releases/latest/download/sturm-transpile-v0.1.0-${TRIPLE}.tar.gz
tar xzf sturm-transpile-v0.1.0-${TRIPLE}.tar.gz
sudo cp -r sturm-transpile-v0.1.0-${TRIPLE}/* /usr/local/
```

**Build from source (contributors):**

```
git clone https://github.com/SoerenWilkening/Sturm_CPP.git
cd Sturm_CPP
cmake -S . -B build -DLLVM_DIR=$(llvm-config-17 --cmakedir) \
      -DClang_DIR=/usr/lib/llvm-17/lib/cmake/clang
cmake --build build -j
```

## Quickstart

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

Wire it into your build with `add_quantum_executable()`, which routes every source file through `sturm-transpile` before compilation:

```cmake
find_package(sturm REQUIRED)
add_quantum_executable(my_program main.cpp)
```

See [examples/or_circuit.cpp](examples/or_circuit.cpp) for a runnable demo covering OR, NOT, and XOR-assign patterns.

## How it works

- [docs/01_principles.md](docs/01_principles.md) — core design principles (write-forward-only, compile-time uncompute, ancilla discipline).
- [docs/roadmap_transpiler_post_mvp.md](docs/roadmap_transpiler_post_mvp.md) — phase-by-phase completion log for phases A through L.

## Contributing

[AGENTS.md](AGENTS.md) and [CLAUDE.md](CLAUDE.md) define the agent workflow used by this project. Issue tracking is done with [beads](https://github.com/steveyegge/beads) (`bd`); run `bd ready` to find work that is unblocked and available.

## License

AGPL-3.0-or-later. See the [LICENSE](LICENSE) file.
