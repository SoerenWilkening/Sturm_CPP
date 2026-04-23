# PRD — Automatic Adjoint Synthesis for User Routines

**Status:** Draft
**Owner:** Soren Wilkening
**Date:** 2026-04-23
**Related principles:** P4 (WHEN immutability), P9 + P9a–d (adjoint synthesis), B10–B11 (compile-time uncompute, loop reversal)

## 1. Problem

STURM users today can write forward quantum routines, but an `invert(fn)` that resolves via `STURM_REGISTER_ADJOINT(fn, adj)` still requires the user to hand-write the adjoint body. For oracle-shaped routines common in Grover-style workloads, the adjoint is mechanically derivable from the forward, yet the user must still author and maintain a second function whose correctness is not checked against the forward.

Example the user wants to write today:

```cpp
qbool marked(qint x, int T) { return x >= T; }

int main() {
    qint x = make_superposition(...);
    WHEN(marked(x, 10)) { x.phi() += M_PI; }
}
```

This should compile and run — with the oracle materialized into an ancilla on entry, the phase kick executed under its control, and the oracle uncomputed at scope exit — without the user writing `marked_adj`.

## 2. Goals

1. Eliminate the need for hand-written adjoints on user routines whose bodies are compositions of registered primitives and registered routines.
2. Support the `WHEN(f(args…))` sugar, including return-style forwards, as a first-class idiom.
3. Preserve the source-level audit trail: every uncompute site remains inspectable by name-matching `__*_adj` tokens in the generated buffer.
4. Reject non-reversible bodies at the forward-function definition site, with a diagnostic that points at the offending construct.

## 3. Non-goals

- **No runtime auto-inversion.** Synthesis is compile-time only; the runtime remains B1a/B1b compliant.
- **No caching, memoization, or CSE of user calls.** `qbool a = marked(x,10); qbool b = marked(x,10);` pays for both calls. User responsibility (C/C++ mentality).
- **No static-analysis of free-variable mutation.** Users are responsible for honoring the refined P4 rule on `WHEN` control expressions. Diagnostics are best-effort, not comprehensive.
- **No measurement-within-reversible-routine support.** Routines that contain quantum → classical conversion are rejected.

## 4. User-visible API

### 4.1 Writing a reversible user routine

Either form is accepted:

```cpp
// Return style — transpiler generates an out-param companion internally.
qbool marked(qint x, int T) { return x >= T; }

// Out-param style — already in the canonical shape.
void marked(qbool& a, qint x, int T) { a ^= (x >= T); }
```

Mutation of inputs is permitted when the parameter is passed by non-const reference:

```cpp
void ripple(qint& a) {
    for (int i = 0; i + 1 < N; ++i) a[i+1] ^= a[i];
}
```

### 4.2 Calling the adjoint

```cpp
invert(&marked)(a, x, 10);   // runs __marked_adj(a, x, 10)
invert(&ripple)(a);          // runs __ripple_adj(a) — reversed loop
```

### 4.3 `WHEN(f(args…))` sugar

```cpp
WHEN(marked(x, 10)) { x.phi() += M_PI; }
```

expands (conceptually) to:

```cpp
qbool __stu_t0; __marked_out(__stu_t0, x, 10);
WHEN(__stu_t0) { x.phi() += M_PI; }
// injected at scope exit, in LIFO order:
//   __marked_out_adj(__stu_t0, x, 10);
//   (ancilla pool release happens via RAII after the above)
```

## 5. Design

### 5.1 Synthesis pipeline

At each reversible user-routine definition, the transpiler runs three passes:

1. **Signature normalization (P9a).** If the forward returns a quantum type, synthesize an out-param twin `__fn_out` and rewrite the original body to write into the out-param via `^=` (or the appropriate dual). The original return-style function remains callable as an ordinary C++ expression; adjoint placement targets `__fn_out`.

2. **Validation (P9d).** Walk the normalized body and reject if any statement is a measurement, a classical I/O call, or a call to an unregistered user routine. Diagnostics anchor on the offending statement.

3. **Adjoint emission (P9c, B11).** Walk the body in reverse statement order, emitting the adjoint of each op. For loops, emit the reversed-iteration form. Emit a named companion `__fn_adj` and register it with `STURM_REGISTER_ADJOINT(fn, __fn_adj)`.

### 5.2 Loop reversal (B11)

The existing `uncompute_pass.cpp:429` iterates ops in reverse within a scope but **skips** ops inside loops via the Phase H PH-3 `skip_uncompute` flag (set by `matcher_outer_var_guard.cpp`). The new synthesis pass replaces that skip with real reversal:

| Forward | Adjoint |
|---|---|
| `for (int i = 0; i < N; ++i) body` | `for (int i = N-1; i >= 0; --i) body_adj` |
| `for (int i = 0; i < N; i += s) body` | `for (int i = ((N-1)/s)*s; i >= 0; i -= s) body_adj` |
| `while (cond) body` | rejected — unbounded trip count is not invertible without a manual adjoint |

Nested loops reverse innermost-first (standard LIFO lifted to loop structure).

### 5.3 Parameter capture

Gate parameters in the adjoint must reference the same value the forward emitted:

- **Literal or const args** (`theta += 0.3`): the adjoint emits `theta -= 0.3` with the same literal. Bit-exact cancellation at the gate-stream level.
- **Computed args from unchanged inputs** (`theta += f(y)` where `y` is const in the scope): re-evaluate `f(y)` in the adjoint.
- **Computed args from mutated inputs**: rejected unless the value is captured at dispatch time. The ingestion pass may need to hoist such expressions into a `const` local before the forward op so the adjoint has a stable reference.

### 5.4 Interaction with existing phases

- **Phase E (compound expressions):** Already decomposes `a = b & c` into primitive ops + synthesized intermediates. Synthesis consumes the decomposed IR, not the source expression, so `&`, `|`, `!` "just work" through the existing plumbing.
- **Phase F (WHEN-lift):** Already lifts `WHEN(expr)` into ancilla + control + uncompute. Extension: when `expr` is a call to a user routine, the lift uses `__fn_out` for forward, `__fn_adj` for uncompute.
- **Phase H (outer-var-guard):** Today sets `skip_uncompute` on loop-body mutations. New behavior: if the enclosing routine is being synthesized, replace the skip with a loop-reversal emission. Outside synthesis context (ad-hoc inline uncompute), preserve existing behavior.
- **Phase J (fusion, hoisting, dead-ancilla elimination):** Runs over the emitted adjoint body the same way it runs over forward bodies. No special-casing.
- **PI-4 audit:** Name-matches `invert(fn)(...)` and `__fn_adj(...)` tokens in the emitted buffer. P9c guarantees every synthesized adjoint carries a distinct name, so the audit remains complete.

## 6. Principle refinements (delivered in this PRD)

Already merged into `docs/01_principles.md`:

- **P4** — `WHEN` control expression is live across scope; free variables are transitively immutable in the body.
- **P9** — renamed to "invertible by synthesized adjoint"; default is transpiler-synthesis, hand-registration is the fallback for non-synthesizable bodies.
- **P9a** — both return-style and out-param forms accepted; out-param is canonical for adjoint placement.
- **P9b** — input immutability declared by const-ness; `qint x` is read-only, `qint&` is mutable, `const qint&` is read-only.
- **P9c** — transpiler always emits a distinct `__fn_adj` even for self-inverse bodies (preserves audit trail, survives refactors, costs nothing at runtime).
- **P9d** — non-invertible bodies diagnosed at definition site (not at `invert(fn)` call site).
- **B11** — adjoint synthesis reverses statement order AND loop iteration order; gate parameters reuse forward-emitted values.

## 7. Scope of implementation work

The work breaks into four roadmap-addable phases. Each can be filed as a beads epic with per-matcher issues underneath.

### Phase P (synthesis prelude)
- **P-1** Routine-registry extension: track `{forward, out-param twin, adjoint}` triples.
- **P-2** Validation pass (P9d) — walk normalized body, diagnose on measurement / I/O / unregistered callee.
- **P-3** Tests: positive (pure-XOR oracle) + negative (explicit cast, `std::cout`, unregistered callee).

### Phase Q (signature normalization, P9a + P9b)
- **Q-1** Return-style → out-param rewrite for bodies that return a quantum type via a single expression.
- **Q-2** Const-ness enforcement: reject forward routines whose signature is ambiguous about input mutation.
- **Q-3** Tests: fixtures for each signature variant; verify emitted companion compiles and links.

### Phase R (straight-line adjoint emission)
- **R-1** Emit `__fn_adj` for bodies without loops — reuse `uncompute_pass` primitives, targeted at a sibling function rather than inline.
- **R-2** `STURM_REGISTER_ADJOINT` auto-emission at the definition site.
- **R-3** Tests: m12 gate-equivalence pairs for each supported forward body (extend the PN-8 pattern).

### Phase S (loop reversal, B11)
- **S-1** Replace `skip_uncompute` handling with real reversal for loops inside a synthesis context.
- **S-2** Stride-aware bound computation; nested-loop innermost-first.
- **S-3** Diagnostic for `while`-loops and quantum-dependent trip counts.
- **S-4** Tests: ripple, bit-reversal, adder carry chain — each as an m12 forward/adjoint pair against a hand-written reference.

## 8. Test strategy

Existing PN-8 m12 pattern (gate-record byte-comparison between synthesized and hand-written adjoint) is the primary correctness gate. Per new matcher:

1. **Fixture pair** — a forward routine + its hand-written adjoint as ground truth.
2. **Synthesized pair** — the same forward with `STURM_AUTO_ADJOINT` (new macro / attribute) enabling synthesis.
3. **Roundtrip test** — run forward then synthesized-adjoint on a prepared state; assert state equals input and gate counter equals zero (plus any scope-entry/exit bookkeeping).
4. **Byte-compare test** — emitted gate record of `synthesized_adjoint(args)` equals emitted gate record of `hand_written_adjoint(args)`.

Negative tests for each P9d reject case; diagnostic content asserted by golden-file comparison.

## 9. Open questions

1. **Opt-in vs opt-out for synthesis.** Do all user routines auto-synthesize, or only those marked with an attribute (e.g. `[[sturm::reversible]]`)? Opt-in is safer during rollout; opt-out is cleaner long-term. Recommend opt-in via attribute for the initial phases, flip default later.
2. **Partial synthesis fallback.** If validation rejects a body, does the transpiler still accept an explicit `STURM_REGISTER_ADJOINT` provided by the user? Yes — hand-registration remains the escape hatch per P9. Synthesis failure is not a hard compile error unless `invert(fn)` is actually used.
3. **Mutation tracking granularity.** `qint& x` mutated in a loop body: is the synthesized adjoint required to be correct pointwise (each iteration un-mutates exactly) or only bulk (end-of-routine state matches)? The stronger guarantee (pointwise) is what LIFO + loop reversal gives us for free; lock it in as the contract.
4. **Recursion.** Recursive reversible routines require the adjoint to recurse in reverse; for tail-recursion this is straightforward, for general recursion it needs a call stack. Out-of-scope for Phase P–S; file as a follow-up.

## 10. Risk register

| Risk | Mitigation |
|---|---|
| Floating-point drift in rotation adjoints | B11 mandates forward-value reuse; byte-compare tests catch any regression immediately. |
| User mutates free variable of `WHEN` control expression (refined P4 violation) | Best-effort diagnostic in the WHEN-lift matcher; documented as UB; no runtime check. |
| Synthesized adjoint diverges from hand-written reference as library primitives evolve | m12 pairs run on every primitive change; roadmap phases S–S-4 codify this. |
| Audit tooling regression if `__fn_adj` naming collides with user names | Reserve `__sturm_*_adj` prefix; reject user identifiers with that prefix at the pre-validation step. |

## 11. References

- `docs/01_principles.md` — P4, P9, P9a–d, B10, B11 (all updated in this cycle).
- `transpiler/src/uncompute_pass.cpp:429` — existing statement-order LIFO reversal; extension point for loop reversal.
- `transpiler/src/matcher_outer_var_guard.cpp` — Phase H PH-3 skip flag; replaced by real reversal under B11.
- `examples/user_routine.cpp` — current manual-adjoint workflow; target for migration once Phase R lands.
- `include/sturm/routines/invert.hpp:70` — `sturm::invert(fn)` free function, unchanged by this work.
