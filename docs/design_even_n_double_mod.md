# Even-n `lib_double_mod_dsl` — Design Plan

**Status:** Plan (2026-04-28).
**Scope tag:** `even-n-double-mod` (sturm-fya1).
**Parent epic:** sturm-4oot (O(W) modular kernels).
**Predecessor:** sturm-wdas (Beat B, landed). **Blocks:** sturm-7cix dispatcher cleanup; sturm-3sfl (Beat D).

## §1 Why this is necessary

Beat B's `lib_double_mod_dsl` requires `n_value` odd. Step 8's
uncompute (`lt_flag ^= x_bits[0]; lt_flag.flip();`) reads the result
LSB as the witness for `lt_flag = (2x < n)`. For odd n, LSB(2x mod n)
toggles with `lt_flag`; for even n, LSB is identically 0 and the
witness vanishes — `lt_flag` is left in a data-dependent state and
the routine is no longer reversible.

The restriction propagated to Beat C: `lib_mul_mod_dsl` now ships
with a runtime dispatcher (`is_classical_odd_n_hint()`) that routes
to the new O(W) oneshot helper only when n is classically known odd,
falling back to the legacy O(W²) chain otherwise. Lib-level tests
using the 1-arg `qbool::make_non_owning(idx)` factory hit the
fallback. Beat D (sturm-3sfl) inherits the limitation if not lifted.

## §2 Why parity-blind in-place doubling is mathematically forced into
Strategy Y

For even n, `x → 2x mod n` is a **2-to-1** map on `[0, n)`: x and
`x + n/2` collapse to the same image. The lost bit is exactly
`lt_flag = (2x_orig < n)` and **cannot be recovered from `x_new` alone**.
Any in-place even-n doubling must export this 1 bit somewhere.

For odd n, `x → 2x mod n` is bijective; the LSB carries enough
information to clean `lt_flag` locally — Beat B's existing trick.

This rules out a purely-local fix:

- **Strategy X (extra `cmp` ancilla, compute before doubling):**
  computing `cmp = (x_orig < n/2)` via `lib_lt_dsl` works, but
  uncomputing `cmp` requires `x_orig`, which is gone after the
  in-place doubling. Keeping a W-bit shadow copy of `x_orig` to
  uncompute `cmp` adds W ancillas peak — strictly worse than Y, and
  the shadow itself can only be released by re-running doubling,
  which closes the loop with the same problem.
- **Strategy Z (Beat A's "subtract addend, witness via overflow" trick):**
  Beat A succeeds because the addend `a_bits` is a separate register
  preserved through the dance. Doubling's "addend" is x itself,
  overwritten in-place; no external reference is available.
- **Strategy Y (expose `lt_flag` as an output):** matches the
  information-theoretic floor. The literature surveyed below
  (Häner-Roetteler-Svore 2017; Roetteler-Naehrig-Svore-Lauter 2017;
  Beauregard 2003; Vedral-Barenco-Ekert 1996) consistently routes
  modular-reduction comparison bits through the calling layer's
  uncompute pass rather than absorbing them in-primitive — the
  Shor / ECC use cases all have odd n, so the published constructions
  don't bother with an explicit even-n branch, but their general
  shape is a `borrow`/`comp` register threaded across the whole
  modular operation. Quipper and revlib follow the same convention.

**Decision: Strategy Y.**

## §3 New API

```cpp
template <typename Bit>
inline void lib_double_mod_dsl(
    Bit* x_bits, Bit* n_bits, std::size_t n,
    Bit& lt_flag_out);                           // NEW out-param

template <typename Bit>
inline void __lib_double_mod_dsl_adj(
    Bit* x_bits, Bit* n_bits, std::size_t n,
    Bit& lt_flag_out);                           // NEW: input on adjoint
```

- Forward writes `lt_flag_out ^= (2 x_orig < n_value)` (XOR-into, so
  callers can pre-zero).
- Adjoint reads `lt_flag_out` (must hold the value the paired forward
  wrote) and consumes it, leaving `lt_flag_out = 0` on exit.
- Internal `lt_flag` allocation is removed; step 8's LSB trick is
  removed. Forward and adjoint become parity-agnostic.
- Odd-n precondition is dropped from both headers.

## §4 Update in place vs. sibling primitive

**Update in place.** Only one in-tree caller
(`lib_mul_mod_dsl_oneshot`) and a handful of lib-level tests
reference the primitive; a sibling `lib_double_mod_dsl_general`
would double the surface and keep the dispatcher complexity we are
trying to remove. Re-open sturm-wdas's contract under the new beat.

## §5 Beat C dispatcher and chain fallback

Once Beat B accepts all n:

- `lib_mul_mod_dsl_oneshot`'s odd-n precondition lifts
  (it inherited Beat B's; nothing else in its body cared about parity).
- `is_classical_odd_n_hint()` and the `lib_mul_mod_dsl` dispatcher
  become dead code — remove them; `lib_mul_mod_dsl` becomes a thin
  wrapper around `lib_mul_mod_dsl_oneshot`.
- `lib_mul_mod_dsl_chain` and its adjoint become dead code — remove.
- The chain helper's `kMaxN = 14u` cap goes away; the oneshot's
  `kMaxN = 32u` becomes the sole bound for `lib_mul_mod_dsl`. The
  user-facing `mul_mod` wrapper inherits the lifted bound.

## §6 Beat D (sturm-3sfl) implications

`lib_square_mod_dsl(x, n)` (x := x² mod n in-place) is **also**
non-injective (x and n−x map to the same x²). It will need its own
"lt_flag-equivalent" output, but the analysis is **separate** from
this one. Two sources of escape information stack at the squaring
layer:

1. **Inherited:** if `lib_square_mod_dsl` is lowered onto
   `lib_mul_mod_dsl_oneshot`, each call pulls in (W−1) doubling
   `lt_flag`s. These already need a home (W−1-bit register per square).
2. **Endemic:** the squaring's own reduction (whatever construction
   Beat D adopts) likely produces additional comparison bits.

Beat D's planner inherits this doc's conclusion — **plan for these
bits as output information, do not chase an LSB-style local
absorption** — but must still analyze its own reduction shape and
decide whether to (a) tear down lt_flags inside each square (paying
adjoint cost twice) or (b) batch them at the `pow_mod` layer.

This is a Beat D problem; tag the conclusion in sturm-3sfl's
DESIGN block when that issue is claimed.

## §7 Decomposed implementation issues

Filed under epic sturm-4oot:

| bd id | Title (slug) | Type | Pri |
|---|---|---|---|
| sturm-4oot.1 | `even-n-double-mod`: rewrite `lib_double_mod_dsl` + adjoint with `lt_flag_out` (drop LSB trick, drop odd-n precondition) | task | P2 |
| sturm-4oot.2 | `even-n-double-mod`: extend `lib_double_mod_dsl` tests to even-n exhaustive (W=2,3) and even-n random (W=4,5) sweeps; re-pin ancilla budget | task | P2 |
| sturm-4oot.3 | `even-n-double-mod`: update `lib_mul_mod_dsl_oneshot` to allocate (W−1)-bit `lt_flags` register and thread it through forward/adjoint doublings | task | P2 |
| sturm-4oot.4 | `even-n-double-mod`: drop `lib_mul_mod_dsl_oneshot` odd-n precondition and extend its tests to even n | task | P2 |
| sturm-4oot.5 | `even-n-double-mod`: remove `lib_mul_mod_dsl` dispatcher (`is_classical_odd_n_hint`) and `lib_mul_mod_dsl_chain` (and its adjoint) as dead code; collapse `lib_mul_mod_dsl` to a thin oneshot wrapper | task | P2 |
| sturm-4oot.6 | `even-n-double-mod`: lift `kMaxN` in `lib_mul_mod_dsl`/oneshot now that the chain's W=14 cap is gone (validate against simulator capacity) | task | P3 |

Dependencies wired in `bd dep`: sturm-4oot.1 blocks {.2, .3, .4, .5};
sturm-4oot.3 blocks {.4, .5}; sturm-4oot.4 blocks .5; sturm-4oot.5
blocks .6. Items .2 and .3 can run in parallel after .1.

## §8 Ancilla budget impact

Pinned numbers from `tests/lib/test_double_mod_dsl_ancilla.cpp` and
`test_mul_mod_dsl_oneshot_ancilla.cpp`:

| Primitive | Pre-fix interior peak | Post-fix interior peak | Notes |
|---|---|---|---|
| `lib_double_mod_dsl` | 5 = `n_pad`(1) + `lt_flag`(1) + `carry_anc`(1) + inner adder carry(1) + lift CCX fold(1) | 4 = `n_pad`(1) + `carry_anc`(1) + inner adder carry(1) + lift CCX fold(1) | save 1 by externalising `lt_flag` |
| `lib_mul_mod_dsl_oneshot` | ≈ 2W + 8 | ≈ 3W + 7 | +(W−1) `lt_flags` register at this layer; net same total qubits across the two layers |
| `lib_mul_mod_dsl` | dispatcher + chain (2W² + W + 7 fallback) | ≈ 3W + 7 | chain removed; oneshot becomes the sole path |
| `lib_pow_mod_dsl` (Beat D) | TBD by Beat D's own planner | TBD; expect at least W·(W−1) inherited `lt_flag`s if naively allocated, less with batching | flagged in §6; Beat D decides whether to absorb at square or `pow_mod` layer |

Net at the `mul_mod` / user-facing `mul_mod` boundary: same O(W),
slightly larger constant (+W−1 bits), in exchange for general n. The
binding cap is now the simulator-qubit budget, not the chain's
W²-quadratic floor.

---

## Appendix: literature survey notes

Surveyed for an explicit even-modulus reversible doubling
construction; none found. The closest analogues:

- **Häner-Roetteler-Svore 2017, "Optimizing Quantum Circuits for
  Arithmetic" (arXiv:1611.07995).** Modular adder/multiplier built on
  comparison ancillas threaded across the operation; doesn't optimise
  for a parity special case. Our Strategy Y matches their convention.
- **Roetteler-Naehrig-Svore-Lauter 2017, "Quantum Resource Estimates
  for Computing Elliptic Curve Discrete Logarithms"
  (arXiv:1706.06752).** ECC moduli are prime → odd; even-n is not
  exercised. No relevant trick.
- **Beauregard 2003, "Circuit for Shor's Algorithm Using 2n+3 Qubits"
  (arXiv:quant-ph/0205095).** Modular adder uses an explicit
  comparison qubit (their `t`) routed through forward and adjoint
  passes — Strategy Y in spirit. The construction is generic over n;
  they don't claim a special even-n optimisation.
- **Vedral-Barenco-Ekert 1996, "Quantum networks for elementary
  arithmetic operations" (arXiv:quant-ph/9511018).** Foundational
  modular adder; comparison bit explicit; generic n.
- **Quipper/revlib reversible-arithmetic libraries.** The community
  convention is to expose comparison/borrow bits and clean at the
  enclosing scope, never absorb via output-LSB tricks (which are an
  optimisation, not a contract).

**Take-away:** Beat B's odd-n LSB-trick is an optimisation specific
to bijective doubling. The literature does not suggest a general-n
analogue; the math (§2) confirms none exists. Strategy Y is the
right answer.
