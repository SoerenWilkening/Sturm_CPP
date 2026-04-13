# Issues and Pitfalls

## Soundness pitfalls

**1. Modifying control-expression operands inside a `WHEN` scope.** Declared undefined behavior in P4, but easy to hit accidentally — `WHEN(a == 5) { a += b; }` mutates `a` inside the scope, breaking uncomputation of any ancilla derived from `a == 5`. There is no cheap dynamic check. Documentation must be loud, and examples must scrupulously avoid this pattern. Consider a debug-build assertion that snapshots `a`'s value+mask at scope entry and verifies it at exit.

**2. Aliasing.** `qadd(a, a)` or `WHEN(a) { a ^= b; }` — when the same `qint` appears multiple times in an operation, classicality and value updates can race in subtle ways. The library must decide for each op whether aliasing is permitted, and assert when it isn't. This is not a small issue: `a += a` is a perfectly natural thing to write and its semantics are nontrivial (it's a doubling, but reversibility requires an ancilla).

**3. Mask widening from carry propagation can be over-conservative.** For `a += b` with `a.mask = 0x000F` and `b.mask = 0`, carry from bit 3 *could* propagate into bit 4 — but only if the actual values cause a carry. The sound choice is to widen the mask whenever it *could*, not when it *does*. This means the mask grows monotonically and may eventually become "all superposed" even when the underlying state is much sparser. There is no fix without runtime tracking of correlations, which P8 forbids.

**4. Manual adjoints can drift from forward routines.** When a user updates `foo` and forgets to update `foo_adj`, `invert(foo)` silently returns a wrong channel. There is no compile-time check that the two are actually inverses. Mitigations: testing convention (every routine ships with a `forward(x); adjoint(x);` round-trip test), naming convention (place forward and adjoint adjacent in the source file), and a future macro to generate the adjoint automatically.

**5. The `WHEN` body-skip mechanism via `if`-wrapper has scoping subtleties.** `WHEN(expr) { ... } else { ... }` would parse but be wrong — the `else` would bind to the macro's hidden `if`. Forbid `else` after `WHEN` in documentation, and consider a sentinel that produces a compile error if anyone tries.

**6. `qbool` versus `bool` confusion in `WHEN` expressions.** `WHEN(true)` should be a compile error or a no-op? `WHEN(some_int64 == 5)` — is the comparison classical or quantum? The rule should be: `WHEN` accepts only `qbool` or expressions producing `qbool`. Pure-classical conditions belong in `if`. Make this a compile error via the macro's expected type.

## Design pitfalls

**7. Lazy or eager flushing of state.** We chose no lazy state (B11 was rejected). This means `a += 1` fifty times in a loop emits fifty separate primitive sequences. Users who expect compiler magic will be surprised. Document clearly that constant folding across operations is the user's responsibility for now.

**8. The "single width = 64 bits" decision will eventually bite.** Algorithms that need wider integers, or that benefit from narrow integers (8-bit qubits inside a hot inner loop), will need parameterization. Build the internal representation to be width-parameterized from day one even if the only exposed width is 64; otherwise the retrofit will be painful.

**9. The thread-local sink and control chain are global state.** This makes some test patterns awkward: parallel test execution must serialize on the sink, or each test must install its own. It also means a routine called from a signal handler or finalizer can corrupt the control chain if not careful. Document that quantum operations are not async-signal-safe.

**10. `WHEN` cannot be re-entered from a recursion that mutates the control bit.** If the body of `WHEN(q)` calls a routine that itself does `WHEN(q)`, the AND-fold logic must handle the case where `q` is *the same bit* as the enclosing control. Ideally `q AND q = q` so the new ancilla is unnecessary, but detecting this requires identity comparison on the underlying bit, which the user-level type may not expose. The simple correct behavior is to allocate a new ancilla anyway and accept the overhead; the smart behavior requires the qubit index to be visible at the dispatch layer (it is, internally — just not to user code).

**11. Measurement semantics are under-specified.** The principle is "assignment from quantum to classical type measures." But what about partial measurement (`bool b = q.bit(3)`)? What about non-destructive measurement? What about measurement bases other than the computational basis? These are real questions that this stage defers. Document what is supported (full destructive measurement of a register via `int64_t x = a;`) and what is not.

**12. The `qbool(p)` preparation primitive has a confusing name.** `qbool(0.5)` looks like a constructor of a Boolean — but it's a quantum preparation that produces a state, not an assignment of a probability to a classical variable. Users may expect `qbool b = 0.5` to "store 0.5" in some sense. The name is short and aligned with the principles, but it will need careful documentation.

**13. Inversion of routines that have classical side effects is undefined.** If a user writes `void foo(qint& a) { std::cout << "hi"; a += 1; }`, the registered adjoint `foo_adj` cannot un-print "hi". The library has no way to detect this. Document that quantum routines must be free of non-quantum side effects, including I/O, allocation visible to the outside, and mutation of non-parameter state.

**14. Compile-time elimination of dead branches depends on the optimizer's ability to constant-propagate masks.** In some realistic cases (qint passed by reference into a function across translation units) the compiler will not be able to prove the mask is zero, and the dispatch overhead will remain. The "free classical optimization" claim is real but not unconditional. Profile-guided builds and aggressive `inline`/header-only design help; full LTO is recommended.

**15. The principle "if your program reads like a circuit diagram, it is wrong" is aspirational and ungated.** A user can absolutely write `q.theta += pi/2; a ^= b; q.theta -= pi/2;` and reproduce a Hadamard sandwich. The DSL does not prevent this. The principle is a cultural directive, not a language constraint. Code review and library design must enforce it.

## Build and ecosystem pitfalls

**16. Header-only versus separately compiled.** For the C++ compiler to optimize through the operator overloads (B9), the overloads must be visible — header-only or aggressively inlined. This grows compile times. Decide early whether to commit to header-only, and benchmark.

**17. Debugging.** Stepping through overloaded operators in a debugger is painful — every `a += b` becomes a stack of inlined frames. Provide pretty-printers and a debug-mode trace sink that logs operations with source locations.

**18. Error messages from template-heavy code are notoriously bad.** If `qint` becomes a template (for the future width parameterization), the error messages from a misuse like `WHEN(some_int)` will be indecipherable. Use `static_assert` with custom messages liberally, and consider concepts (C++20) to constrain template parameters with readable diagnostics.
