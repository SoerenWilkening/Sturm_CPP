# Implementation Guideline

This stage delivers the user-facing language only. The four primitives are stub functions that do nothing (or increment a counter). No simulator, no circuit storage, no physical gate lowering. The goal is that user code compiles, runs, and exhibits the correct dispatch and control-flow behavior.

## Scope of this stage

In scope:
- The `qint` and `qbool` types, with classicality masks.
- Operator overloads for arithmetic and logical operations.
- The `WHEN` macro and control-chain mechanism.
- The four primitive sink calls as no-op stubs (or counter-incrementing stubs).
- Manual adjoint conventions and `invert(foo)` registration.
- Measurement via assignment to `int64_t` / `bool`.

Out of scope:
- Any actual quantum emission or simulation.
- Circuit storage and inspection.
- Optimization passes.
- Qutrit/anyonic libraries.
- Macro-based or AST-based auto-inversion.

## Suggested module layout

```
core/
  primitives.hpp     // the four sink ops as no-op stubs
  sink.hpp           // sink interface, thread-local current sink, default counter sink
qtypes/
  qbool.hpp          // qbool type
  qint.hpp           // qint type and operator overloads
control/
  when.hpp           // WHEN macro, control-chain RAII guard
adjoint/
  invert.hpp         // free-function invert(foo) and registration table
library/
  qarith.hpp         // qadd, qsub, qmul, qand, qor, qnot, ... (forward + adjoint pairs)
```

## Core data shapes

`qbool` carries a `bool value` and a `bool is_super`. When `is_super` is `false`, `value` is meaningful. When `is_super` is `true`, `value` is don't-care.

`qint` carries an `int64_t value` and a `uint64_t super_mask`. A bit position `i` is in superposition iff `(super_mask >> i) & 1`. Bits where the mask is 0 are meaningful in `value`; bits where the mask is 1 are don't-care in `value`.

A backend-managed qubit-index field will be added later. For this stage, no index needs to exist.

`qint` provides an implicit (non-`explicit`) converting constructor from `int64_t`, so `qint a = 42;` is the complete user code for declaring a classical-valued quantum integer. It sets `value = 42`, `super_mask = 0`, makes no sink calls, allocates no ancillas, and after inlining is equivalent to a plain `int64_t` initialization. The reverse conversion `int64_t x = a;` is measurement and must be `explicit` (or routed through a named `measure(a)`), since it crosses the quantum–classical type boundary per P2.

## The sink interface

A sink is an object with four methods corresponding to the four primitives. The default sink for this stage is a counter sink that increments a counter for each call and otherwise does nothing. The sink is held in a thread-local pointer; user code never names it. Routines may be invoked under any sink without modification.

## Dispatch in operator overloads

Every overloaded operator follows the same shape:

1. Compute the output classicality mask from the input masks (the per-op mask transfer function).
2. For bit positions classical in all operands: perform ordinary `int64_t` arithmetic. No emissions.
3. For bit positions where any operand is superposed: emit the corresponding primitives via the sink, in the controlled form if the control chain is non-empty, else uncontrolled.
4. Update the destination's `value` and `super_mask` fields.

For this stage, step 3 calls into the no-op sink, so the side effect is just a counter increment; correctness of the *data flow* (mask updates, classical part of the value) must still be exercised and tested.

## The `WHEN` construct

`WHEN(expr)` is a macro that expands to an RAII guard scope. The guard:

1. Evaluates `expr`. If `expr` is a compound expression (e.g. `a | c`), this evaluation goes through the same overloaded ops as any other expression, allocating a temporary `qbool` whose construction trace is recorded for uncomputation.
2. Inspects the resulting `qbool`'s `is_super` flag.
   - If classical and `false`: enters a "skip" mode for the body — the macro arranges for the block to not execute.
   - If classical and `true`: enters the body with the control chain unchanged.
   - If superposed: AND-folds with the current control bit (allocating a new ancilla `qbool` if a previous control was active), pushes the new control, and enters the body.
3. On scope exit: pops the control, uncomputes any ancilla allocated for the AND-fold or for the compound expression by replaying its construction trace in reverse.

The "skip the body" case is the awkward one: a C++ macro cannot trivially elide a following block. The cleanest solution is to wrap the body in an `if (guard.should_run())` that the macro generates around the user's braces. A practical sketch:

```cpp
#define WHEN(expr) \
    if (auto _when_guard = ::dsl::detail::make_when_guard(expr); _when_guard.should_run())
```

The user writes `WHEN(expr) { body }` and the `if` consumes the `{ body }` block naturally. The guard's destructor handles uncomputation when control leaves the block (normally or via exception). The `should_run()` predicate returns `false` for the classical-false case, `true` otherwise.

## Manual adjoint convention

For each user-written routine `void foo(qint& a, qint& b, ...)` that should be invertible, the user also writes `void foo_adj(qint& a, qint& b, ...)` with the body's statement order reversed and each operation replaced by its dual:

| Forward | Adjoint |
|---|---|
| `a += b` | `a -= b` |
| `a -= b` | `a += b` |
| `a ^= b` | `a ^= b` |
| `a.theta += d` | `a.theta -= d` |
| `a.phi += d` | `a.phi -= d` |
| `WHEN(e) { body }` | `WHEN(e) { adjoint(body) }` |
| `prepare(q, p)` | `unprepare(q, p)` |
| library call `qfoo(...)` | library call `qfoo_adj(...)` |

The user then calls `register_adjoint(foo, foo_adj)` once at static-init time. `invert(foo)` returns the registered adjoint as a callable.

For library routines (`qadd`, `qmul`, etc.), the library author writes both forward and adjoint forms and registers them at library load.

## Measurement

Conversion from `qint` to `int64_t` (and `qbool` to `bool`) *is* the measurement operation — measurement exists internally as a real channel, but it has no user-callable name. It is triggered exclusively by the explicit type conversion (cast or assignment to a classical variable). For this stage:

- If the source is fully classical (mask all zero), measurement is a plain copy of `value`.
- If any bit is superposed, measurement should emit a primitive (`measure` is *not* in the four primitives, but the simplest treatment for this stage is to add a fifth sink op `measure(q)` as a stub, or to fold measurement into the sink interface as a separate concern). For now, leave a clearly marked TODO; the principle that measurement is a type-boundary crossing is what matters at this stage.

## Testing strategy for this stage

Because there is no real backend, tests must verify two things:

1. **Classical correctness.** When all inputs are classical, every operation produces the right `int64_t` result. This catches mask-transfer bugs and dispatch bugs.
2. **Dispatch shape.** When some bits are superposed, the right number and kind of primitives are emitted to a *recording test sink* (a fourth sink mode for testing only — distinct from the production counter sink). Tests assert on the emitted sequence, on mask transitions, and on `WHEN` control behavior (skip / uncontrolled run / controlled run).

A minimal test routine:

```cpp
qint a;        // mask = 0, value = 0
a = 42;        // classical assignment
qbool flag(0.5); // superposed
WHEN(flag) {
    a += 1;    // expected: emits a singly-controlled increment sub-circuit
}
// expected: control chain empty again, flag still superposed, a's mask widened
```
