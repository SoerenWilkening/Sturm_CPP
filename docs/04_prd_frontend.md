# PRD — STURM C++ Quantum Language Extension, Front-End Stage

## 1. Purpose

Deliver the user-facing C++ quantum DSL surface (`qint`, `qbool`, operator overloads, minimal `WHEN`) without any real quantum backend. All quantum emissions are stubs. Classical data flow through quantum types must be fully correct and tested.

## 2. Goals

- User can declare `qint` and `qbool` variables, mix them with `int64_t` and `bool`, and use natural C++ operator syntax.
- All arithmetic, bitwise, comparison, shift, and power operations are overloaded for `qint` (and where meaningful, `qbool`).
- Fully-classical operands produce correct `int64_t`/`bool` results with zero quantum side effects.
- Superposed operands trigger a named stub sink call and a monotone mask widening, but no real circuit emission.
- Each `qint` owns a virtual qubit map; physical qubits are lazily allocated from a global pool when a bit first becomes superposed.
- `qbool` ↔ `qint`, `int64_t` ↔ `qint`, `bool` ↔ `qbool` conversions exist with the semantics defined in §6.
- Minimal `WHEN` macro lets the rest of the system depend on a thread-local control `qbool` without committing to uncomputation logic.
- Plain-assert test suite verifies classical correctness and dispatch shape (named stub reached, mask updated).

## 3. Non-Goals

- No real quantum backend, simulator, or circuit storage.
- No physical-qubit lowering, no gate decomposition.
- No adjoint registration / `invert(foo)` machinery.
- No uncomputation, no AND-fold of nested controls, no ancilla cursor threading through ops.
- No measurement channel — measurement casts return the stored classical value as a stub.
- No optimization passes, no constant folding across operations.
- No qutrit / anyonic libraries.

## 4. Users

- Library authors building higher-level quantum routines (`qadd`, `qmul`, …) on top of the operator surface.
- Test authors validating the data-flow correctness of the front end before backend bring-up.

## 5. Success Criteria

1. `qint a = 42; qint b = 8; qint c = a + b;` produces `c.value == 50`, `c.super_mask == 0`, no qubits allocated, no sink calls.
2. `qbool flag(0.5); qint a = 0; WHEN(flag) { a += 1; }` runs without crashing, leaves `flag.is_super == true`, widens `a.super_mask`, and triggers a recorded `quantum_add` stub call on the test sink.
3. All overloaded operators in §7 compile and pass the classical-correctness test matrix.
4. Test suite runs via CMake + plain `assert`s and exits 0.

## 6. Type Conversion Rules

| From | To | Semantics |
|---|---|---|
| `int64_t` | `qint` | Implicit. `value=src`, `mask=0`, no qubits, no emissions. |
| `qint` | `int64_t` | Explicit. Returns `value` field as-is (stub measurement; superposed bits ignored — user's responsibility). |
| `bool` | `qbool` | Implicit. `value=src`, `is_super=false`. |
| `qbool` | `bool` | Explicit. Returns `value` field. |
| `qbool` | `qint` | Implicit. Zero-extends bit 0; bits 1–63 classical 0. |
| `qint` | `qbool` | Explicit. Keeps lowest bit only; bits 1–63 discarded (user's responsibility). |

## 7. Operator Surface

**Arithmetic:** `+ - * / %`, `+= -= *= /= %=`, unary `-`, free `pow(qint, qint|int)`.

**Bitwise:** `& | ^ ~`, `&= |= ^=`.

**Shift:** `<< >>`, `<<= >>=`.

**Comparison (returns `qbool`):** `== != < <= > >=`.

**Indexing:** `qint::operator[](size_t i) → qbool` view sharing `qubits[i]` and bit `i` of `value`/`mask`.

**Phase / amplitude:** `qint::phi`, `qint::theta` proxy subobjects supporting `+= / -=` of a `double`. These are *distinct* operations from `qint += qint` and emit different stubs.

**Restrictions:**
- Division/modulo on quantum operands: stub only, no classical-correct quantum semantics this stage.
- Aliasing (`a += a`, `qadd(a, a)`) — undefined for now, document loudly.
- All quantum operations on superposed inputs go through stubs; no real circuit.

## 8. Mask Transfer Rules

- Bitwise `^ & | ~`: `out_mask = a.mask | b.mask` per bit (no propagation).
- Shift: shift mask along with value.
- Add / sub: pessimistic monotone widen — any superposed bit at position `i` widens output mask from bit `i` upward to bit 63 (carry could propagate).
- Mul / div / mod / pow: any superposed bit anywhere → entire output mask becomes all-ones.
- Compare: any superposed bit anywhere in either operand → result `qbool.is_super = true`.

Mask never narrows. Superposition is monotone (P8).

## 9. Qubit Allocation

- Global thread-safe `QubitPool` hands out monotonically increasing physical indices.
- Each `qint` holds `std::array<int,64> qubits`, all initialized to `-1` ("classical, no physical qubit").
- When a bit position transitions from classical to superposed, the operator overload allocates a fresh physical qubit from the pool and stores it in `qubits[i]`.
- Per backend rule: if a CNOT has a superposed control and a classical target, the target bit is freshly allocated and the operator must (in the future backend) emit an `X` if the prior classical value was 1. Front-end stage records this only via mask widening + stub call.
- `qbool` holds `std::array<int,1> qubits` and follows the same lazy rule.
- Pool is also the source for ancilla qubits later.

## 10. Sink Interface

- Abstract `Sink` class with one method per named quantum op:
  `quantum_add, quantum_sub, quantum_mul, quantum_div, quantum_mod, quantum_pow, quantum_xor, quantum_and, quantum_or, quantum_not, quantum_shl, quantum_shr, quantum_eq, quantum_neq, quantum_lt, quantum_le, quantum_gt, quantum_ge, theta_add, phi_add, prepare`.
- Each method takes the involved physical qubit indices (and any classical scalar params).
- Implementations:
  - `CounterSink` (default): increments per-op counters.
  - `RecordingSink` (test): appends `(op_name, qubit_indices, scalar_args)` records to a vector for assertion.
- `current_sink()` returns a thread-local `Sink*`. Tests install their own sink for the duration of the test.

## 11. Minimal `WHEN`

- Thread-local `qbool* current_control = nullptr`.
- `WHEN(expr)` macro:
  ```cpp
  #define WHEN(expr) \
      if (auto _g = ::sturm::detail::make_when_guard(expr); _g.should_run())
  ```
- Guard behavior:
  - Classical false → `should_run() == false`, body skipped.
  - Classical true → `should_run() == true`, control chain unchanged.
  - Superposed → `should_run() == true`, sets `current_control` to the expression's `qbool` for the scope, restores prior on destruction.
- No AND-fold across nested `WHEN`s, no ancilla, no uncomputation. Documented as a stub.
- `WHEN` accepts `qbool` only; passing `bool` or `int` is a compile error via concept / `static_assert`.

## 12. Width Parameterization

- Internally `qint` is a class template `qint_t<std::size_t Width = 64>`. Public alias `qint = qint_t<64>`.
- All masks, value fields, and qubit arrays parameterized on `Width`.
- Only `Width = 64` is exposed and tested this stage. Future widths (`qshort`, `qbyte`) enabled by the same template.

## 13. Build & Tooling

- CMake (multiple headers, header-only public surface).
- C++20.
- Plain `assert`-based tests under `tests/`, one executable per test file or one aggregated runner.
- No external dependencies.

## 14. Repo Layout

```
include/sturm/
  core/primitives.hpp
  core/sink.hpp
  core/qubit_pool.hpp
  qtypes/qbool.hpp
  qtypes/qint.hpp
  control/when.hpp
tests/
  test_qint_classical.cpp
  test_qint_superposed.cpp
  test_qbool.cpp
  test_conversions.cpp
  test_when.cpp
  test_sink_dispatch.cpp
CMakeLists.txt
docs/
  01_principles.md
  02_implementation_guideline.md
  03_issues_and_pitfalls.md
  04_prd_frontend.md
  05_spec_frontend.md
```

## 15. Risks

- Pessimistic mask widening on add/sub will saturate masks fast in real programs. Acceptable per P8.
- Lazy qubit allocation on classical→superposed transition couples allocation to operator dispatch; correctness depends on every overload doing this consistently. Mitigated by a single helper.
- `WHEN` without uncomputation cannot support nested controls correctly; tests must avoid nesting.
- Stub measurement returning `value` is incorrect for superposed bits — documented as user responsibility this stage.

## 16. Open Questions Deferred to Backend Stage

- Real measurement channel.
- AND-fold and ancilla allocation for nested `WHEN`.
- Adjoint registration and `invert(foo)`.
- Kernel dispatch on virtual indices (rules 1–7 of backend principle).
- Physical X-gate emission when CNOT target is freshly allocated from a classical 1.
