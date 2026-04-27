# PRD — Library Packaging & Public API Surface

**Status:** Draft (2026-04-27).

**Scope tag:** `packaging-export`.

**Owner:** _(unassigned)_

**Supersedes / retires:** none.

---

## 0. One-sentence goal

Ship STURM as a self-contained C++ package that downstream algorithm
repositories can consume via `find_package(sturm)`: a precompiled transpiler
binary, a precompiled runtime library, and a header surface that exposes
`qint`, `qbool`, and `WHEN` (unprefixed) plus the four user primitives — and
nothing else.

---

## 1. Background

The project currently contains both the language implementation (types, control
flow, transpiler, runtime, modular arithmetic primitives) and a growing set of
in-repo examples. We have decided to:

- Treat **this repository as the language only.**
- Move **algorithms (QFT, QPE, Grover, Shor, VQE, QAOA, Hamiltonian simulation,
  amplitude amplification, etc.) to a separate downstream repository** that
  depends on this one.

This PRD specifies the work required to make this repository a clean,
consumable language package suitable for that downstream consumer.

---

## 2. Architectural decisions (locked in)

These are not open for re-litigation in implementation; they are the premises
the workstreams in §3 build on.

**D1. Language vs. algorithm boundary.** Anything the transpiler emits a call
to in generated code is part of the language and stays in this repository.
Pure algorithm primitives (Grover iteration, QFT, QPE, amplitude amplification,
Hamiltonian simulation, oracle conversion helpers) live in the downstream
algorithms repository, not here.

**D2. Modular arithmetic stays.** `add_mod`, `mul_mod`, `pow_mod` are
transpiler-emit targets (the matchers in `transpiler/src/matcher_modular_*.cpp`
rewrite `(a + b) % n`, `(a * b) % n`, and `pow(a, x) % n` to calls into these
free functions). They are part of the language ABI, not algorithms, by D1.

**D3. Precompilation boundary.** The distributable consists of three
artifacts:
1. The transpiler binary (`sturm-transpile` or similar) — fully compiled,
   dynamic-links libclang.
2. The runtime library `libsturm` — fully compiled, contains the backend
   sinks, qubit pool, dispatch table, simulator, `uncompute_api.cpp`, and the
   `execute_gate` C-ABI implementations.
3. The header surface under `include/sturm/` — header-only by necessity
   because `qint_t<W>`, `qbool`, and the operator overloads are C++ templates
   that re-instantiate in every consumer translation unit.

Item (3) means downstream users *do* recompile the type machinery in every TU
they author. This is unavoidable and not a regression.

**D4. LLVM-17 is a documented user prerequisite.** The transpiler binary
dynamic-links libclang/libtooling. Users who install this package must have
LLVM/Clang ≥ 17 development packages present on their machine (not just at
build time of this repository, but at runtime of the transpiler). A
self-contained statically-linked transpiler binary is a future option (§5)
but is not in scope here.

**D5. `WHEN` keeps its name.** The macro stays spelled `WHEN` at the user
surface. The transpiler matchers continue to recognize the literal token
`"WHEN"` via `is_expansion_of_macro(..., "WHEN")` (nine call sites, in
`matcher_when_*`, `matcher_brace_wrap.cpp`, etc.). The known collision with
Catch2's BDD `WHEN` macro is deferred (§5).

**D6. WHEN body free-variable mutation is a diagnostic, not auto-corrected.**
Per principle P4, mutating any free variable of a WHEN's control expression
inside the WHEN body produces an incorrect adjoint. This PRD upgrades the
status of that violation from "undefined behavior" to "compile-time
diagnostic at the offending source location." Auto-snapshotting free variables
to silently make the user's code correct was rejected because it would hide
gate-count costs from algorithm authors.

**D7. Unprefixed names ship via an opt-in prelude header.** Users include
`<sturm/prelude.hpp>` to bring `qint` and `qbool` into the global namespace
via `using sturm::qint; using sturm::qbool;`. The umbrella `<sturm/sturm.hpp>`
header does *not* perform these `using` declarations — it leaves names in
`sturm::` so library code and downstream code that prefers prefixed access can
include it without namespace pollution. `WHEN` is a preprocessor macro and is
already global once the umbrella header is included.

---

## 3. Workstreams

Each workstream below corresponds to one bd issue (or epic, if the body
warrants splitting). They are listed in execution order: 3.1 is a prerequisite
for 3.2 and 3.3; 3.4–3.9 are independent of each other and may be worked in
parallel.

### 3.1. Transpiler-emit-target audit (prerequisite)

Enumerate every symbol the transpiler emits a call to in rewritten user code.
Sources to walk: `transpiler/src/matcher_*` (every rewrite `populate_*_hit` or
emitter), `transpiler/src/*_emitter.{hpp,cpp}`, and `cmake/SturmTranspile.cmake`
if relevant. Output: a table mapping (transpiler matcher → emitted symbol →
header it lives in today). This table is the input to §3.2.

**Acceptance:** A document `docs/transpiler_emit_targets.md` enumerating every
emit target with its current header location and a recommendation
(public / internal / move-to-algorithms).

### 3.2. Public/internal header split

Using the §3.1 table, partition `include/sturm/` into:

- **Public** (top-level under `include/sturm/`): emit targets, the four user
  primitives, `qint`/`qbool`/`WHEN`, the modular arithmetic free functions,
  the umbrella header, the prelude header.
- **Internal** (`include/sturm/detail/`): everything else currently in
  `include/sturm/` — `qint_arith_v3.hpp`, `bit_proxy.hpp`, `dispatch_gate.hpp`,
  `*_dsl.hpp` files that are pure helpers for emit targets and never appear
  in transpiler-emitted code, etc.

`include/sturm/sturm.hpp` includes only public headers transitively. Internal
headers may include each other and may be included by `.cpp` files in
`src/`, but downstream users should never need to include a `detail/` header.

**Acceptance:** Every header currently directly under `include/sturm/`
subdirectories is either confirmed public or moved under `detail/`. The CMake
install rules continue to install both trees (internal headers must remain
installed because public template headers transitively include them at the
consumer's compile time). All in-repo tests and examples build unchanged.

### 3.3. Umbrella header completion

`include/sturm/sturm.hpp` is currently minimal (re-exports only the uncompute
API and `invert<>`). Expand it to re-export the full public surface:
`qint`, `qbool`, `WHEN`, the four primitives (`qbool(p)`, `q.theta`, `q.phi`,
`^=`), the modular arithmetic functions (`add_mod`, `mul_mod`, `pow_mod`),
`invert<>`, and any other emit targets identified in §3.1.

**Acceptance:** A consuming TU that does only `#include <sturm/sturm.hpp>`
can compile and run a non-trivial example using `qint`, `qbool`, `WHEN`,
arithmetic operators, and the modular ops, with `sturm::` prefixes.

### 3.4. Prelude header (`<sturm/prelude.hpp>`)

A new header that includes `<sturm/sturm.hpp>` and adds:
```cpp
using sturm::qint;
using sturm::qbool;
```
(plus any other type-only aliases we want unprefixed; do *not* `using` free
functions like `add_mod` or `invert` — those should remain prefixed to
avoid global-namespace pollution and ADL surprises.)

**Acceptance:** A consuming TU that does `#include <sturm/prelude.hpp>` can
declare `qint a; qbool b;` without a `sturm::` prefix. No additional symbols
are leaked into the global namespace beyond `qint`, `qbool`, and (already)
`WHEN`.

### 3.5. CLI driver and CMake function

Two delivery mechanisms for the transpile-then-compile pipeline:

- **`sturmc` driver script.** A shell or Python wrapper installed alongside
  the transpiler that takes a `.cpp` source, runs the transpiler, then
  invokes the user's C++ compiler (defaulting to `clang++`) on the
  transpiled output, and links against `libsturm`. Targets one-off scripts
  and the documentation's getting-started flow.
- **`add_sturm_executable(target source ...)` CMake function** in
  `cmake/SturmTranspile.cmake` (which already partially exists). Targets
  serious downstream projects, including the algorithms repository.

Both must work from an installed package — `find_package(sturm)` followed by
`add_sturm_executable` (or invoking `sturmc` from the install bin dir) must
produce a working binary in an external project.

**Acceptance:** Both invocation paths produce a working binary from a
reference example source file in an external project (see §3.6).

### 3.6. External smoke-test project

A minimal CMake project in a separate directory tree (not under this repo's
build), with its own top-level `CMakeLists.txt`, that:
1. Calls `find_package(sturm REQUIRED)`.
2. Uses `add_sturm_executable` to build a single-file example consuming
   `qint`, `qbool`, `WHEN`, and a modular operation.
3. Runs the resulting binary and checks output.

This project can live under `tests/external_consumer/` in this repository but
its CMake configuration must work as if it were standalone (i.e. only depend
on the *installed* package, never on in-tree headers or build artifacts).

**Acceptance:** CI step builds, installs the package to a temporary prefix,
then configures and builds the smoke-test project against that prefix and
runs it. The step fails if any installed header is missing, any transitive
dependency is unresolved, or any public symbol fails to link.

### 3.7. Diagnostic: WHEN body free-variable mutation

Implement the diagnostic described in D6. A new transpiler matcher (or
extension of `matcher_when_operand_mutation.cpp`, which already exists for a
related case) walks every WHEN body, computes the read-set of the control
expression `expr` (transitively through any function call inside `expr`),
and rejects the program if any element of that read-set is written inside
the WHEN body.

The diagnostic must:
- Fire at the offending write's source location (not the WHEN entry).
- Name the variable being mutated and the WHEN scope it would corrupt.
- Be a hard error, not a warning.

Edge cases to settle during implementation:
- Function calls inside the body that transitively mutate a free variable.
- Read-set membership through `const` references vs. by-value parameters.
- Aliasing through references within the body.

**Acceptance:** Test cases in `tests/transpiler/` covering at least:
direct mutation of a free `qint`, mutation through a function call, mutation
of the control variable itself, and a *negative* case where a body mutates a
local that is not in the read-set (must compile cleanly).

### 3.8. Versioning macros

Add to the umbrella header:
```cpp
#define STURM_VERSION_MAJOR <n>
#define STURM_VERSION_MINOR <n>
#define STURM_VERSION_PATCH <n>
#define STURM_VERSION_STRING "n.n.n"
```
Source the version from the top-level `CMakeLists.txt` `project()` declaration
via `configure_file` so there is a single source of truth.

**Acceptance:** Downstream code can `#if STURM_VERSION_MAJOR >= 1` and the
preprocessor sees a defined integer literal. The version string is reachable
from the installed CMake config too (`sturm_VERSION` in `find_package`).

### 3.9. Documentation

Three documents, each addressing a distinct audience:

- **`README.md` updates.** Prerequisites section listing LLVM/Clang ≥ 17 as
  a hard requirement. Replace any in-tree-only build instructions with
  install + consume flow.
- **`docs/getting_started.md`** (new). A walkthrough for an external
  algorithm author: install the package, write a 20-line example using
  `qint`/`qbool`/`WHEN`, build with `sturmc` and with the CMake function,
  inspect counter-mode output. Mirrors §3.6's smoke-test project.
- **`docs/public_api.md`** (new). The authoritative list of public symbols:
  `qint`, `qbool`, the four primitives, `WHEN`, the modular arithmetic
  functions, `invert<>`, the `STURM_REGISTER_ADJOINT` macro, and the
  versioning macros. Anything not on this list is *not* part of the
  language's public API regardless of where its header lives, and may
  change without a major version bump.

**Acceptance:** All three documents exist and are referenced from
`README.md`. The `public_api.md` list matches the §3.1 audit (every
public-classified symbol appears; nothing internal-classified appears).

---

## 4. Acceptance criteria (whole PRD)

This PRD is complete when:
- An external project does `find_package(sturm)`, includes
  `<sturm/prelude.hpp>`, writes a routine using `qint`, `qbool`, `WHEN`, and
  a modular operation, builds via `add_sturm_executable`, and runs the
  resulting binary on a machine with LLVM-17 dev libs installed.
- The smoke-test project (§3.6) is wired into CI and gates merges.
- The WHEN free-variable mutation diagnostic (§3.7) fires on the test cases
  listed in §3.7's acceptance criteria.
- The umbrella header (`<sturm/sturm.hpp>`) and prelude header
  (`<sturm/prelude.hpp>`) are the only entry points documented in
  `docs/public_api.md`; users who include `<sturm/detail/...>` are doing
  so at their own risk.

---

## 5. Deferred / out of scope

The following are explicitly out of scope for this PRD; each may become a
separate work item later.

**5.1. Self-contained transpiler binary.** Static-linking libclang into the
transpiler so users do not need LLVM-17 installed. Revisit if the prerequisite
proves to be a real friction point for external users.

**5.2. `WHEN` → `STURM_WHEN` alias.** Avoiding the Catch2 BDD `WHEN`
collision. Cheap to do (one rename in `when.hpp`, lockstep update of the nine
`is_expansion_of_macro(..., "WHEN")` call sites in the transpiler) but only
necessary if a downstream user actually hits the collision.

**5.3. Endianness / bit-layout convention for `qint`.** The current behavior
is internally self-consistent but is not documented as a normative
contract. Algorithms that interpret a `qint`'s bits as a number (QFT,
number-theoretic oracles) need this nailed down. Separate design call,
needed before the algorithms repository ships a QFT.

**5.4. Diagnostics for other UB cases.** P4 also forbids `if` on quantum
types (currently a compile error via the type system, but worth verifying
the error message quality) and P9d promises diagnostics at the forward
function definition site for non-invertible bodies. A separate audit pass
should walk the principles document and confirm every "undefined behavior"
or "compile error" promise is actually wired up with a useful message.

**5.5. Algorithms.** Anything from the candidate list — QFT, inverse QFT,
QPE, amplitude amplification, Grover, Shor, VQE, QAOA, Hamiltonian
simulation, block encoding, qubitization, oracle conversion helpers — is
out of scope here. They live in the downstream algorithms repository.

**5.6. QECC, noise channels, alternate-dimension cores.** P3 and P6 (channels,
QECC as a higher-order function) and P7 (dimension-agnostic core) are
intentionally aspirational at present. Implementing them is its own future
work and does not block the packaging effort.
