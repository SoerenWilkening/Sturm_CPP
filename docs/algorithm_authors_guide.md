# Writing Quantum Algorithms in STURM — Handoff Guide

**Audience.** An LLM agent or new contributor sitting in a *downstream*
repository that consumes STURM via `find_package(sturm)` and is about
to write quantum algorithms (Shor, Grover, QPE, amplitude estimation,
QFT-based primitives, etc.). Read this file end-to-end before writing
any STURM code. Cross-references point into the in-tree STURM source
tree for authoritative spec.

**Companion docs (in this repo, read on demand).**
- `docs/01_principles.md` — design principles P1–P9d, B1–B11.
- `docs/public_api.md` — symbol-by-symbol reference.
- `docs/getting_started.md` — install + first-build walkthrough.
- `docs/qram_user_intro.md` — full QRAM deep-dive.

---

## 1. What STURM is, in one paragraph

STURM is a C++20 embedded DSL for **reversible / quantum-classical
programming**. You write ordinary-looking C++ using two quantum types
(`qint`, `qbool`), the operators they overload (`+`, `*`, `%`, `&`,
`|`, `^`, `~`, `<<`, `>>`, comparisons, compound-assigns), and exactly
one control construct (`WHEN(expr) { body }`). A Clang LibTooling
transpiler (`sturm-transpile`, loaded as a plugin via
`add_quantum_executable`) statically analyses each translation unit,
**synthesises and injects all uncompute/adjoint statements** at
compile time, and feeds the rewritten buffer to codegen in memory.
You never write inverses by hand for user routines; you never spell a
gate name.

The full design rationale is in `docs/01_principles.md`. If a STURM
program reads like a circuit diagram (named H, CNOT, Toffoli, X, Y, Z
gates in user code) it is **wrong by construction** — see P5.

---

## 2. Consuming STURM from an algorithm repo

Two files are the entire integration.

### `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_algorithms LANGUAGES CXX)

find_package(sturm REQUIRED)

# Every consumer source goes through add_quantum_executable.
# It runs the Clang plugin against the AST in memory and links
# the `sturm` interface target — no extra target_link_libraries.
add_quantum_executable(shor_demo shor.cpp)
add_quantum_executable(grover_demo grover.cpp)
```

### Configure / build / run

```bash
cmake -S . -B build \
      -DCMAKE_PREFIX_PATH=/path/to/sturm-prefix \
      -DSTURM_MODE=APPEND          # APPEND | COUNT | SIMULATE
cmake --build build --parallel 6
./build/shor_demo
```

`STURM_MODE` is set at configure time; it picks which backend the
auto-injected `sturm_backend_create` lifecycle wires into the active
sink. **Switching modes mid-program is not supported via the
auto-injected path.** If you need runtime mode selection, write the
explicit `sturm_backend_create` call yourself and add
`#define STURM_NO_AUTO_LIFECYCLE` before including `sturm.h` (see
`examples/explicit_lifecycle.cpp` in the STURM source tree).

**Build cap.** All `cmake`/`ctest`/`make`/`ninja` invocations must be
hard-capped at `--parallel 6` / `-j6` — project policy from
`CLAUDE.md`. Never use bare `--parallel` or `-j$(nproc)`.

---

## 3. The four public headers

```cpp
#include "sturm.h"               // umbrella — brings qint, qbool, WHEN, lifecycle
#include "sturm/qram.h"          // opt-in — enables `qint b = a[i];`
#include "sturm/draw_ascii.h"    // opt-in — print_ascii(), gate_count()
#include "sturm/routines/invert.hpp"   // for STURM_REGISTER_ADJOINT / invert<>
```

The umbrella `sturm.h` brings `using sturm::qint;` and
`using sturm::qbool;` into the including TU's namespace. **It also
sets a sentinel macro that tells the transpiler to wrap your `main`
with the `sturm_backend_create` / `destroy` lifecycle automatically.**
Do not write that lifecycle yourself unless you have opted out with
`#define STURM_NO_AUTO_LIFECYCLE`.

Anything reachable only via `<sturm/detail/...>` is **internal** and
may change without notice. Stick to what `docs/public_api.md` lists.

---

## 4. The type system

### `qint` — the width-agnostic frontend alias

```cpp
qint a = 6;                      // classical-init OK, free (P4a)
qint b = a + 3;                  // arithmetic — emits gates iff a is superposed
qint c = (a + b) % n;            // modular — transpiler folds to add_mod(a,b,n)
qint d;                          // default — alias width-inference applies (see §10)
```

`qint` resolves to `sturm::frontend::qint`, a **type-stub class with
a mandatory transpiler**. Its operator bodies return
default-constructed sentinels; the transpiler substitutes
`qint_t<W>` with an inferred width before codegen. **Pre-transpile
execution is unsupported** — you cannot compile a TU that exercises
the alias without running the plugin first.

When you need a specific width or backend-only operations
(`q.qubits[k]`, `q.super_mask`, `q.theta()`, `q.phi()`), reach for
`sturm::qint_t<W>` directly:

```cpp
using sturm::qint_t;
qint_t<8> byte_reg;              // explicit 8-bit register
byte_reg.theta() += 0.3;         // amplitude rotation on bit 0
byte_reg.phi()   += 1.57;        // phase rotation on bit 0
```

### `qbool` — single-bit quantum boolean

```cpp
qbool flag(0.5);                 // prepared in |+>: P(|1>) = 0.5
qbool g = a < b;                 // comparison emits gates; result is one qubit
qbool h = flag & g;              // logical AND — superposed if either is
```

`qbool` inherits from `qint_t<1>`. Its operator surface is
`& | ^ ~ ==`. Use `qbool` (not `qint_t<1>`) when the value is
semantically a boolean.

### What `qint` is NOT

- It is **not** an `int`. Conversion to a classical integer is a
  **destructive measurement** (see §11.1).
- It is **not** a circuit object. There is no "build phase" and no
  cached gate sequence. Each call to a routine re-executes its body
  and re-emits its gate stream (B1).
- It is **not** entangled state you can introspect. The runtime stores
  only `(value, super_mask)` per register and the active control
  chain — no per-qubit state, no entanglement graph.

---

## 5. The operations you have

All of these are public via `sturm.h` / the `<sturm/...>` opt-in
headers. The full list is in `docs/public_api.md`.

### Arithmetic on `qint`

```cpp
qint x = a + b;          // qadd
qint y = a - b;          // qsub
qint z = a * b;          // qmul
qint w = a / b;          // qdiv
qint r = a % b;          // qmod
qint p = pow(base, exp); // qpow (classical/quantum dispatched)
```

The C++ compiler inlines dispatch; **fully-classical operands emit
zero gates** and are handled as plain `int64_t` arithmetic (B3).

### Modular arithmetic — the load-bearing primitive for Shor

```cpp
qint r1 = add_mod(a, b, n);      // (a + b) mod n
qint r2 = mul_mod(a, b, n);      // (a * b) mod n
qint r3 = pow_mod(base, exp, n); // (base ^ exp) mod n  ← O(W) topology

// Transpiler-folded forms (equivalent, automatically rewritten):
qint r4 = (a + b) % n;           // → add_mod(a, b, n)
qint r5 = (a * b) % n;           // → mul_mod(a, b, n)
qint r6 = pow(a, x) % n;         // → pow_mod(a, x, n) iff STURM_MODULAR_POW=ON
```

**Precondition (UB if violated).** All inputs must satisfy
`a, b ∈ [0, n)`. For `pow_mod` the base must be in `[0, n)`; the
exponent may be any non-negative value up to the register width. The
library does **not** check this at runtime — same trust model as
classical `%` in C. See `docs/01_principles.md` "Modular arithmetic
contract".

**`pow_mod` ancilla bound.** `O(W)` register topology (named live
registers) but **`O(W²)` peak transient ancilla** — specifically
`2W² + 4W + 11` qubits above the inputs. This is the documented bound
under `sturm-vf2c`; it exceeds the 17-qubit simulator budget for
every `W ≥ 1`, so `pow_mod` tests use APPEND-mode capture + classical
replay rather than statevector simulation. Plan around this when
designing algorithms — if you need statevector verification of a
small instance, factor the algorithm so `pow_mod` is *not* on the
verified path, or use a width small enough that `W ≤ 1` (i.e., test
the surrounding orchestration with classical inputs).

### Bitwise + comparison

```cpp
qint y = a & b;          // qand (bitwise)
qint y = a | b;          // qor
qint y = a ^ b;          // qxor
qint y = ~a;             // qnot
qint y = a << 3;         // shift (classical RHS only)

qbool eq = (a == b);     // emits one CCNOT chain into a single qubit
qbool lt = (a <  b);
qbool ge = (a >= b);
// also: != <= > >=
```

### Rotations (use `qint_t<W>` directly)

```cpp
qint_t<W> q;
q.theta() += 0.5;        // Ry(0.5) on q.qubits[0]
q.phi()   += 1.0;        // Rz(1.0) on q.qubits[0]
```

The transpiler injects the sign-flipped inverse over the verbatim RHS
token before the enclosing scope closes (LIFO). `q.theta() += d;`
within a `WHEN(c) { ... }` becomes a `CRy(d)` (depth-1
controlled-rotation); see `examples/rotations.cpp` for the canonical
shape.

### QRAM read

```cpp
#include "sturm/qram.h"

qint a[N];                       // or std::array<qint, N>, or qint*
qint i = some_address();         // qint index
qint b = a[i];                   // QRAM read — exactly this shape, fresh LHS
```

**Any container classicality.** The v2 bucket-brigade body handles
fully classical (`super_mask == 0` on every element, the "QROM" route),
fully quantum (every slot superposed, the "qreg" route), or mixed
containers under a **single, data-classicality-agnostic algorithm**.
Phase 1 builds an `(N' − 1)`-router tree from the address bits; Phase 2
walks a `W`-qubit "bus" register through a CSWAP tree to the addressed
leaf, XORs `b ^= bus`, then walks back up; Phase 3 tears the router
tree down. Per-call cost is `O(W · log² N)` T-depth (Giovannetti–Lloyd–
Maccone 2008, arXiv:0708.1879) — replacing the v1 `O(N · W)` Toffoli
sweep. **Index width invariant.** `i` must have width
`W ≥ ⌈log₂ N⌉`; high bits must be zero; `i ≥ N` is UB. See §11.4
for the shape rules.

### Lifecycle of intermediate quantum values

You allocate intermediate `qbool` / `qint_t<W>` values as ordinary
C++ locals. **You do not call uncompute on them.** The transpiler
synthesises and injects the adjoint statements before the enclosing
scope closes (LIFO; B11). Destructors release qubit indices to the
pool — they emit no gates (B10).

---

## 6. Control flow — `WHEN`

`WHEN(expr) { body }` is the **only** quantum control construct. `if`
on a quantum-typed value is a **compile error** by design.

```cpp
qbool flag = ...;
WHEN(flag) {
    out ^= a;                    // controlled on `flag`
    out += b;                    // also controlled on `flag`
}
```

`expr` is materialised into an ancilla at scope entry and uncomputed
at scope exit. Three cases at runtime:

| `expr` evaluates to        | Behaviour                                    |
|----------------------------|----------------------------------------------|
| classical `false`          | Body is dead code (skipped)                  |
| classical `true`           | Body executes **uncontrolled**               |
| superposed bit             | Body executes under quantum control          |

### **Immutability rule (UB if violated).**

`expr` is **live across the whole scope**. Neither `expr` itself nor
any free variable it reads — directly or through a function call —
may be modified within the body. Violation produces an incorrect
adjoint. The transpiler does not catch this in general; it is your
job.

```cpp
WHEN(a < b) {
    // ❌ UB: modifying `a` invalidates the materialised predicate
    a += 1;
}

WHEN(flag) {
    // ✅ OK: `flag` is read but never written
    out ^= a;
}
```

### Compound WHEN expressions

```cpp
WHEN((b | c) & d) {              // Phase F lifts this into two flat
    out.flip();                  // temporaries above the WHEN, uncomputed
}                                // LIFO after the body.
```

You can write arbitrary nested `&` / `|` over `qbool`s as the WHEN
argument; the transpiler flattens.

### Depth-1 control invariant (B5a)

Nested `WHEN` chains are **collapsed to depth 1 by AND-folding**
(Phase G). At any point in execution at most one control bit is live
on the stack. The lifted emitters assert `depth <= 1` at runtime;
depth ≥ 2 is a programmer error, not a supported path.

**Practical consequence.** Library and user code that needs
effectively higher-arity control must lift via the *outer flag +
`WHEN`* pattern: compute an outer `qbool` flag (e.g.
`qbool f = a & b;`), then enter a single `WHEN(f) { ... }` whose body
uses at most one further `WHEN` level. The matchers will refuse to
emit a controlled rotation under two nested WHENs.

---

## 7. Writing a user routine

The pattern: a forward function that takes its output by non-const
reference, optionally a hand-written adjoint, and a
`STURM_REGISTER_ADJOINT(forward, adjoint)` macro at TU scope. The
transpiler synthesises the adjoint automatically if you do not
register one — but **you must register one if you want the forward
function to be callable from another reversible routine**, because
the matcher checks the registry.

### Minimal recipe

```cpp
#include "sturm.h"
#include "sturm/routines/invert.hpp"
using sturm::invert;

// Forward. Output goes through a non-const reference parameter.
void mark_if_target(qbool& out, qint x, int T) {
    // Body: write to `out` using qint/qbool/WHEN/operators.
    // No measurements, no I/O, no classical side effects.
    WHEN(x == T) {
        out.flip();
    }
}

// Adjoint. Same signature. The transpiler can synthesise this when
// the body is trivially invertible; you may write it by hand for
// non-trivial routines or to be explicit. Self-inverse here.
void mark_if_target_adj(qbool& out, qint x, int T) {
    WHEN(x == T) {
        out.flip();
    }
}

STURM_REGISTER_ADJOINT(mark_if_target, mark_if_target_adj)

int main() {
    qint x = 5;
    qbool marked;
    marked.ensure_qubit();
    mark_if_target(marked, x, 5);
    // ↳ transpiler injects `invert(mark_if_target)(marked, x, 5);`
    //   before the enclosing scope closes.
    return 0;
}
```

### Signature rules (P9 family)

- **Output by non-const reference.** `qbool& out`, `qint_t<W>& out`.
  These are the parameters the transpiler treats as outputs to
  uncompute. A return-style declaration is also accepted; the
  transpiler synthesises an out-param companion.
- **Read-only inputs are `const` or by value.** `qint x` (by value)
  is read-only inside the callee. `const qint& x` is read-only.
- **Forbidden inside reversible routine bodies (P9d) — diagnosed at
  definition site:**
  1. Measurement (any quantum → classical conversion, explicit or
     implicit).
  2. Classical I/O or any observable classical side effect (no
     `printf`, no `cout`, no global writes).
  3. Calls to unregistered user routines whose adjoint cannot be
     synthesised transitively.
- **Registration is required at namespace scope.** `STURM_REGISTER_ADJOINT`
  expands to a specialisation of
  `sturm::_detail::adjoint_of<&::fn>::value`; the
  nested-name-specifier `::fn` requires `fn` to be at namespace scope
  (file scope is fine).
- **Self-inverse routines still register a distinct `__fn_adj`** —
  not `fn` itself — so audit tooling can name-match `__*_adj` tokens
  at uncompute sites. If you want the optimisation, write
  `STURM_REGISTER_ADJOINT(fn, fn)` by hand explicitly.

### Output-classification rules (PI-3) — where the adjoint lands

The transpiler classifies each output-parameter slot at every call
site and injects (or skips) an `invert(fn)(...)` companion
accordingly:

| Output backing the call    | Class               | Injection                           |
|----------------------------|---------------------|-------------------------------------|
| Local to the call's scope  | Intermediate        | `invert(fn)(...)` before scope close |
| In an ancestor scope       | IntermediateOuter   | `invert(fn)(...)` at declaring scope close |
| A function parameter       | Final               | **No injection** (caller owns it)   |
| File / namespace scope     | Final               | **No injection**                    |
| Inside a `for`/`while`/`if`/`WHEN` body but backed by ancestor | SkipWithDiagnostic | No injection + stderr warning |

**The intuition.** A routine's caller owns the uncompute policy for
output parameters that *escape* the callee (P9). If the backing
storage is local to the current scope, the transpiler can plant the
inverse before the scope closes; if it escapes through a parameter,
only the caller knows when the inverse should fire.

---

## 8. Adjoint synthesis rules (P9, B11) — what the transpiler does

For a user routine whose forward body the transpiler can invert
mechanically, it synthesises a named `__fn_adj` companion from the
forward body using:

- Statement-order reversal (LIFO within each scope).
- Dual-operator substitution:
  - `+=` ↔ `-=`
  - `theta +=` ↔ `theta -=`
  - `phi   +=` ↔ `phi   -=`
  - `^=` is self-adjoint (no flip needed)
- **Loop reversal.** A classical loop whose body contains quantum
  mutations is reversed: stride negated, init/cond bounds swapped, body
  ops adjointed and emitted in reverse iteration order. Nested loops
  reverse innermost-first.
- **Captured parameters.** Gate parameters in the adjoint reference the
  *value* the forward emitted (captured at dispatch or recomputed from
  unchanged inputs), not a re-derived expression over mutated state.
  This guarantees `Rθ(v) · Rθ(-v) = I` bit-exactly at the gate-stream
  level.

If you want to *see* what was synthesised, the rewritten file is
mirrored to `build/sturm_gen/<relpath>` whenever
`add_quantum_executable` runs the plugin in dump-on-disk mode. You
can opt into legacy dump-then-compile at configure time with
`set(STURM_TRANSPILE_MODE "dump")`. Or pass
`--dump-transpiled=<path>` ad-hoc on the standalone `sturm-transpile`
binary.

---

## 9. Backend modes — counter / append / simulate

Selected at configure time via `-DSTURM_MODE=...`. The runtime sink
is set once at `sturm_backend_create` time.

| Mode       | What it does                                                | When to use                                  |
|------------|-------------------------------------------------------------|----------------------------------------------|
| `APPEND`   | Records every emitted gate into an in-memory `GateIR`       | Default. Inspection / ASCII rendering        |
| `COUNT`    | Increments per-op counters; no storage                      | Resource estimation (gate counts, depth)     |
| `SIMULATE` | Executes against a statevector simulator (orkan, ≤17 qubits) | End-to-end functional verification           |

The opt-in `sturm/draw_ascii.h` gives you three no-arg renderer entry
points keyed on the active context:

```cpp
#include "sturm/draw_ascii.h"

sturm::print_ascii();            // writes circuit diagram to stdout
std::string s = sturm::draw_ascii();
size_t n = sturm::gate_count();  // size of the captured IR
```

Use `gate_count()` and `draw_ascii()` for end-to-end debugging: take a
snapshot of `gate_count()` before and after a routine call to verify
adjoint firing.

---

## 10. Width inference — the silent default

When the transpiler substitutes a bare `qint x;` declaration with a
`qint_t<W>`, the width comes from rule order in
`width_inference.hpp`. If no RHS-driven inference applies,
**`kDefaultWidth = 32`**. Pre-`sturm-qac` code that assumed bare
`qint` meant `qint_t<64>` will silently widen to 32 after transpile.

**Rule of thumb.** When width matters — modular arithmetic register
sizes, Shor exponent registers, anything where you want a specific
qubit count — declare `sturm::qint_t<W>` explicitly. Use bare `qint`
only for value-level scratch code and QRAM subscripts where width is
inferred from the RHS.

---

## 11. Footguns — the section you must read

### 11.1 The measurement footgun

`qint` carries an **implicit `operator size_t() const`** so that
`qint b = a[i];` parses uniformly across container shapes. The same
conversion fires in **any integral context** and silently measures:

```cpp
qint q = some_expression();

int x = q;                                  // ❌ SILENT MEASUREMENT
std::vector<int> v(q);                      // ❌ SILENT MEASUREMENT
for (size_t i = 0; i < q; ++i) { ... }      // ❌ SILENT MEASUREMENT
if (q == 5) { ... }                         // ❌ SILENT MEASUREMENT (q → size_t for ==)
foo(q);                                     // ❌ SILENT MEASUREMENT if foo takes int
std::printf("%lld", q);                     // ❌ SILENT MEASUREMENT
```

The code still **runs** and produces plausible numbers; the bug is
only visible if you read the gate stream looking for missing
primitives.

**Rules.**
1. Use `static_cast<int64_t>(q)` deliberately when you intend to
   measure (e.g., reading out a final result).
2. For QRAM access write the exact shape `qint b = a[i];` (fresh
   decl on the LHS, no surrounding expression).
3. Compare `qint` to `qint`, not to integer literals — `q == 5`
   measures; `q == qint(5)` does not.
4. **Trust the build, not the run.** Post-transpile, `qint_t<W>` has
   an `explicit operator int64_t()`. Any conversion site the matcher
   missed becomes a *compile error* in the generated file. If your
   TU transpiles cleanly *and* compiles cleanly post-transpile, you
   are safe. If post-transpile build fails with an
   `explicit conversion` error on a `qint_t<W>`, the matcher missed
   a site — **fix the source**, do not silence the error.

### 11.2 The depth-1 control invariant

You cannot nest `WHEN` arbitrarily. At most one control bit is live
at any point. If you need higher-arity control, AND-fold the
controls into one outer flag:

```cpp
// ❌ Won't work for things like controlled rotations:
WHEN(a) {
    WHEN(b) {
        q.theta() += 0.3;        // CRy under TWO controls — depth-2 forbidden
    }
}

// ✅ Lift the conjunction:
qbool f = a & b;
WHEN(f) {
    q.theta() += 0.3;            // depth-1 CRy under `f`
}
```

The Phase G AND-fold handles many cases automatically for pure
control-flow `WHEN`s, but **does not fire on rotation bodies**. For
rotations and other primitives the lift is your responsibility.

### 11.3 Modular arithmetic preconditions

`add_mod(a, b, n)`, `mul_mod(a, b, n)`, `pow_mod(a, x, n)` all
require `a, b ∈ [0, n)` (`a ∈ [0, n)` for `pow_mod`'s base). **No
runtime check.** Supplying out-of-range operands is undefined
behaviour — same trust model as C `%`. If you can't statically prove
inputs are in range, reduce them first.

### 11.4 QRAM shape rules

The matcher recognises **exactly one source shape**:

```cpp
qint b = a[i];               // ✅ fresh LHS, bare subscript on RHS
```

All other shapes are **diagnosed as compile errors** by the
transpiler (not silently measured):

```cpp
b = a[i];                    // ❌ existing b — H1 (out of scope v1)
a[i] = b;                    // ❌ QRAM-write — H2
a[i] += b;                   // ❌ read-modify-write — H3
c = a[i] + d;                // ❌ expression-position read — H4
```

Container shapes supported in v1: `std::array<qint, N>`,
`qint_t<W>[N]`, `qint_t<W>*` (length passed explicitly).
`std::vector<qint>` is not yet supported.

### 11.5 WHEN immutability (repeat — it bites)

Mutating `expr` or any of its free variables inside `WHEN(expr) { ... }`
is **UB**. The transpiler does not catch this in general — it
materialises `expr` into an ancilla once and uncomputes from the
same free variables at scope exit. If you mutated one of them, the
uncompute call now reads stale state and the adjoint is wrong.

### 11.6 Default-width drift

Bare `qint` defaults to `qint_t<32>` after transpile when no
RHS-driven inference applies. If you need 64 bits, write
`sturm::qint_t<64>`.

### 11.7 STURM_MODE is configure-time

You cannot switch backends at runtime via the auto-injected
lifecycle. If you need that, opt out with
`#define STURM_NO_AUTO_LIFECYCLE` and call `sturm_backend_create`
yourself.

---

## 12. Algorithm-shaped recipes

### 12.1 Grover oracle (mark-state pattern)

```cpp
// "Marked state" = q == T. The oracle flips a target qubit when true.
void grover_oracle(qbool& target, qint q, int64_t T) {
    WHEN(q == T) {
        target.flip();
    }
}

void grover_oracle_adj(qbool& target, qint q, int64_t T) {
    WHEN(q == T) {
        target.flip();
    }
}
STURM_REGISTER_ADJOINT(grover_oracle, grover_oracle_adj)
```

The diffusion operator is structurally the same — flip-about-the-mean
via a `WHEN(q == 0)` plus rotations on each qubit lane. **Variable
iteration count** is expressed as an ordinary classical `for`-loop
around the oracle / diffusion call (B1: runtime dispatch, no stored
sequences).

### 12.2 Shor — modular exponentiation is already a primitive

The hard part of Shor is `pow_mod(a, x, n)` and it is shipped. The
top-level Shor routine is mostly:

1. Pick a random `a < n` with `gcd(a, n) = 1` (classical).
2. Allocate exponent register `qint_t<2W> x` and prepare uniform
   superposition (`H` on every bit lane — written as
   `x[k].theta() += π/2;` for each `k`).
3. Compute `y = pow_mod(a, x, n);`.
4. Apply inverse QFT to `x`.
5. Measure `x` (i.e., `static_cast<int64_t>(x);`).
6. Continued-fraction post-processing to extract the period
   (classical).

**Watch the ancilla bound.** `pow_mod` peaks at `2W² + 4W + 11`
transient ancillas; SIMULATE mode is not viable above `W = 1`. Verify
small instances under APPEND-mode capture + classical replay; use
COUNT-mode for resource estimates of realistic-size instances.

### 12.3 Phase estimation (QPE) scaffolding

```cpp
// U is a user routine. Apply controlled-U^(2^k) on `target` from each
// bit `k` of `phase_reg`. Inverse QFT on `phase_reg` then measure.

void qpe_round(qint_t<W>& phase_reg, qint_t<M>& target) {
    for (int k = 0; k < W; ++k) {
        WHEN(phase_reg[k]) {        // depth-1 control on bit k
            for (int rep = 0; rep < (1 << k); ++rep) {
                apply_U(target);    // user-registered with STURM_REGISTER_ADJOINT
            }
        }
    }
    // inverse QFT on phase_reg here
}
```

This is the canonical idiom: a classical loop chooses *which*
controlled operations fire and *how many times*; quantum control is
the `WHEN`. The transpiler reverses the loop in the synthesised
adjoint (B11).

### 12.4 QFT — rotation cascade

```cpp
void qft(qint_t<W>& q) {
    for (int j = 0; j < W; ++j) {
        q[j].theta() += M_PI / 2;       // H on bit j (via theta rotation)
        for (int k = j + 1; k < W; ++k) {
            WHEN(q[k]) {
                q[j].phi() += M_PI / (1ULL << (k - j));   // controlled phase
            }
        }
    }
    // bit reversal handled separately (swap chain)
}

// The synthesised adjoint reverses both loops innermost-first;
// each rotation's sign flips automatically.
```

Synthesised adjoint takes `θ → -θ`, reverses the inner loop, then
reverses the outer loop — that is inverse QFT, for free.

### 12.5 Pattern checklist

When designing a new algorithm:

- [ ] Classical control flow is C++ control flow (`for`, `while`,
      `if` on classical types).
- [ ] Quantum control is `WHEN`. Never write `if (qbool_x) { ... }` —
      compile error.
- [ ] Register widths are explicit (`qint_t<W>`) when they matter for
      the algorithm.
- [ ] Modular operands are reduced into `[0, n)` before calling
      `add_mod` / `mul_mod` / `pow_mod`.
- [ ] User routines take outputs by non-const reference and are
      registered with `STURM_REGISTER_ADJOINT`.
- [ ] No `printf`, `cout`, or global writes inside reversible
      routines — measurement happens at the top level only.
- [ ] Final readout uses `static_cast<int64_t>(...)`. Never let a
      `qint` slip into an `int`-taking function unaudited.
- [ ] Verify on a small instance under `STURM_MODE=SIMULATE` (≤17
      qubits total — and watch the `pow_mod` ancilla bound).
- [ ] Estimate resources on full-size instances under
      `STURM_MODE=COUNT`.

---

## 13. Debug + verification workflow

1. **Snapshot gate counts.** Wrap suspect code in an inner scope and
   sample `sturm::gate_count()` before / after to confirm the
   forward+adjoint counts match expectations.
2. **Inspect the rewritten source.** With `STURM_TRANSPILE_MODE=dump`
   (or `--dump-transpiled=<path>` on the standalone binary), the
   rewritten file lands under `build/sturm_gen/<relpath>`. Read it.
   The injected `invert(fn)(...)` lines and `__fn_adj` companions are
   visible verbatim. This is the **load-bearing artefact** for
   verifying transpiler behaviour.
3. **Render the circuit.** `sturm::print_ascii()` after the routine
   closes shows the captured IR. Useful for tiny instances.
4. **Statevector check.** `STURM_MODE=SIMULATE` + a small `W` runs
   end-to-end against the orkan simulator. Compare to a classical
   reference computation.
5. **Resource estimate.** `STURM_MODE=COUNT` for production-size
   resource numbers.

If the post-transpile build fails with `explicit conversion` on a
`qint_t<W>`, fix the source. **Do not** add `static_cast<int64_t>` to
silence it unless you genuinely want a measurement there.

---

## 14. Where to read more (in the STURM source tree)

- `docs/01_principles.md` — P1–P9d, B1–B11, the modular contract.
- `docs/public_api.md` — every public symbol, its header, its blurb.
- `docs/getting_started.md` — install + first-build walkthrough.
- `docs/qram_user_intro.md` — full QRAM deep-dive with the
  supported / unsupported shape table.
- `docs/transpiler_emit_targets.md` — what every transpiler matcher
  emits, useful when generated code surprises you.
- `examples/` — runnable demos for each transpiler phase. Most
  relevant to algorithm authors:
  - `user_routine.cpp` — forward + registered adjoint pattern.
  - `qram_demo.cpp` — minimal QRAM read.
  - `rotations.cpp` — depth-1 WHEN-guarded rotation.
  - `when_integration.cpp` — compound WHEN expressions.
  - `nested_when.cpp` — Phase G AND-fold of nested WHENs.
  - `in_memory_transpile.cpp` — three rewrites composing cleanly.

If something here contradicts the principles file or the public-API
file, **trust those** — they are the spec.

---

## 15. Versioning

STURM is pre-1.0 (currently 0.1.x). The public API list may change
between commits; the in-tree drift check
(`tools/check_public_api_drift.sh`) ensures every change is a
deliberate, reviewer-visible edit to `docs/public_api.md`. Pin your
consumer to a specific STURM commit until 1.0 ships.
