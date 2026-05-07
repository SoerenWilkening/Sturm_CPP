# PRD — Frontend `qint` alias completion + transpiler-driven type substitution

**Status.**
- Wave 1 (§§1–8): shipped 2026-05-04 under bd epic `sturm-65rs`
  (issues `.1`–`.14` closed). Follow-ups `.15`–`.18` open and labeled
  out-of-scope per §7.
- Wave 2 (§9): drafted 2026-05-05. Tracked under bd epic `sturm-qaca`.
  Triggered by post-wave-1 build failure on
  `examples/qram_demo.cpp` (array-carrier alias erasure gap).
- Wave 3 (§10): drafted 2026-05-07. Pre-transpile runtime path
  abandoned; alias becomes pure type-stubs. Supersedes the Wave-1 §3
  "qbool-returning compares" non-goal and the Wave-1 §7 follow-ups
  `.15` (qbool compares) — see §10.0 for the precise re-classification.

**Predecessors.** `docs/archive/prd_qram_subscript.md` (frontend alias-class
introduction, sturm-u9ge) and `docs/archive/prd_qram_backend.md` (QROM
backend emission, sturm-qjt7 et al.). Both are closed; this PRD picks up
the long-term migration that those PRDs explicitly deferred —
`qram_subscript.md` §4.1: *"the existing `using qint = qint_t<64>;` in
`qint_fwd.hpp` is referenced by 50+ tests/fixtures and cannot be repointed
in a single beat."* This PRD is that beat.

## 1. Problem

Three concrete defects on the user-facing frontend `qint` surface:

1. **Operator surface incomplete.** `sturm::frontend::qint`
   (`include/sturm/qtypes/qint_alias.hpp` + `qint_alias_ops.hpp`) does not
   mirror the public operator surface of `sturm::qint_t<W>`. Missing today:
   - `qint& operator=(int64_t)` — classical-int copy-assign on an existing
     `qint`. (Backend: `qint_core.hpp:181`.)
   - `bool operator[](size_t) const` — bit access. (Backend:
     `qint_compare.hpp:136`.)
   - `explicit operator int64_t() const` — explicit-cast escape hatch.
     (Backend: `qint_core.hpp:97`.)
   - Mixed-type symmetry on `-`, `*`, `/`, `%`, `&`, `|`, `^` — only `+`
     carries the `qint × <integral>` and reverse pair today
     (`qint_alias_ops.hpp:118-127`).

2. **Users must write `using qint = sturm::frontend::qint;`** to get the
   alias under the bare spelling `qint`. Reason: `qint_fwd.hpp:18` says
   `using qint = qint_t<64>;` in namespace `sturm`, so a bare `qint` after
   `using namespace sturm;` (or via `<sturm/prelude.hpp>`) resolves to the
   *backend* type, not the alias. See `examples/qram_demo.cpp:37` for the
   workaround that should not exist.

3. **Transpiler does not substitute the alias for `qint_t<W>`.** The C1
   matcher (`matcher_qram_subscript.cpp`) rewrites only the source line
   `qint b = a[i];`. Other `qint x;` declarations, parameters, fields,
   return types, and casts in the same TU stay as the alias type
   post-transpile. The emitted file is therefore a mix of frontend-alias
   and backend types, and any backend-only operation (bit access,
   `qbool`-returning compares, `phi()`/`theta()` proxies) fails to
   compile after the rewrite. The alias-class model assumed the C1
   matcher would erase every alias mention; in practice it erases only
   the one shape it anchors on.

## 2. Goals

G1. **Operator parity.** Every public operator on `qint_t<W>` has a
counterpart on `sturm::frontend::qint`, except where mirroring would
force a backend-width commitment (specifically: the non-const
`BitProxy` `operator[]`, and the `phi()`/`theta()` proxies — those stay
backend-only).

G2. **No using-line.** Bare `qint` in any TU that includes the
public umbrella (`<sturm/sturm.hpp>` or `<sturm/prelude.hpp>`) resolves
to `sturm::frontend::qint`, not `sturm::qint_t<64>`. Existing fixtures
that locally typedef `using qint = sturm::qint_t<W>;` continue to work
unchanged (local typedef shadows the namespace alias).

G3. **Full alias erasure post-transpile.** A new transpiler pass
rewrites every type-spelling of `sturm::frontend::qint` (VarDecl,
ParmVarDecl, FieldDecl, function return type, explicit cast) to
`sturm::qint_t<W>` with `W` from the existing `infer_width()`. After
the pass, the rewritten file contains zero references to
`sturm::frontend::qint`.

G4. **No regression.** All existing tests stay green. The 50+ transpiler
fixtures with local `using qint = sturm::qint_t<W>;` typedefs are
unchanged. The QRAM-subscript C1 matcher is unchanged on the wire.

## 3. Non-goals

- ~~`qbool`-returning compares on the alias.~~ **Superseded by Wave 3
  (§10).** Original wave-1 reasoning was that `qbool` would force a
  backend-width commitment on every compare result; that reasoning was
  imprecise (`qbool` is fixed-width `qint_t<1>` and its default ctor
  allocates no qubit — `include/sturm/qtypes/qbool.hpp:47-48`). The
  real concern was header coupling, which Wave 3 accepts.
- Write-side bit assignment `q[k] = …` on the alias. Mirroring the
  backend's non-const `BitProxy` overload requires a width.
- Reverse-direction `<integral> OP qint` for non-`+` arithmetic (the
  backend does not carry these either; this PRD stays symmetric).
- Width inference for ParmVarDecl / FieldDecl / return-type beyond the
  rule-3 default. RHS-driven (`kDefaultWidth = 32`) is correct for v1;
  per-parameter inference is a follow-up if measurements show 32 is
  too narrow.
- Migration of *every* internal-test usage of `sturm::qint`. Only the
  callsites that exercise `qint_t<W>`-only members (`super_mask`,
  `qubits[i]`, `phi()`, `theta()`) need re-spelling to
  `sturm::qint_t<64>`; others — pure constructor + arithmetic uses —
  keep working through the repointed alias.

## 4. Functional requirements

### 4.1 Frontend `qint` operator surface

`sturm::frontend::qint` shall expose:

- **Member ops** (added in `qint_alias.hpp`):
  - `qint& operator=(int64_t v) noexcept` — body: `value_ = v; return *this;`.
    No measurement bump (classical assign is free, P4a).
  - `explicit operator int64_t() const noexcept` — body: bumps
    `qint_alias_detail::g_measurement_count`; returns `value_`.
  - `bool operator[](std::size_t k) const noexcept` — body: bumps the
    counter; returns `((value_ >> k) & 1) != 0`. `k >= 64` returns
    `false` (defensive — backend's bit range is `[0, W)` and the alias
    is width-agnostic).
- **Free ops** (added in `qint_alias_ops.hpp`):
  - `qint operator-(qint, Int)` and `Int operator-(Int, qint)` for
    `Int` integral != `bool`.
  - Same shape for `*`, `/`, `%`, `&`, `|`, `^`. Each routes through
    `qint_alias_detail::measure_to_int` on the qint operand.
  - `/` and `%` retain the existing zero-divisor guard from the
    `qint × qint` overload (`qint_alias_ops.hpp:104-114`).

The drift-gate harness in `tests/qtypes/test_qint_alias_ops.cpp` shall
include one positive `static_assert` per new op (alias has it) and one
negative `static_assert` for each `qint OP bool` (must not compile).

### 4.2 `qint` name resolution

`include/sturm/qtypes/qint_fwd.hpp` shall declare, in namespace
`sturm`:

```cpp
using qint = ::sturm::frontend::qint;
```

The alias header shall be includable from `qint_fwd.hpp` without
introducing a cycle (`qint_alias.hpp` already forward-includes
`qint_fwd.hpp`; the order must be flipped so the leaf header is the
alias and `qint_fwd.hpp` becomes the consumer that pulls the alias and
re-exports it as `sturm::qint`).

`include/sturm/prelude.hpp:22` (`using sturm::qint;`) keeps working
unchanged — it now exports the alias.

### 4.3 Transpiler substitution pass

A new matcher + emitter pair in
`transpiler/src/matcher_qint_alias_subst.{hpp,cpp}` and
`transpiler/src/qint_alias_subst_emitter.{hpp,cpp}`. Registered in
`transpiler/src/transpile_consumer.cpp` and drained **after**
`emit_qram_rewrites` so the C1 rewrite of `qint b = a[i];` runs first
and the new emitter skips any `VarDecl*` already consumed by C1.

Match anchors:

1. `VarDecl` whose declared type's canonical decl is the
   `cxxRecordDecl` for `sturm::frontend::qint`.
2. `ParmVarDecl` (same predicate).
3. `FieldDecl` (same predicate).
4. `FunctionDecl` whose return type's canonical decl is the same.
5. `CXXFunctionalCastExpr` and `CStyleCastExpr` with the same target type.

The discriminator is the same `cxxRecordDecl(hasName("qint"),
hasParent(namespaceDecl(hasName("frontend"))))` shape used (loosely) in
`matcher_qram_subscript.cpp:273-279`. This MUST distinguish the
frontend `qint` from the backend `qint_t<W>` (different class) and
from the namespace alias `using qint = …` (a `TypeAliasDecl`, not a
`CXXRecordDecl` — naturally rejected by `hasDeclaration(cxxRecordDecl
…)` because the alias resolves to `qint_t<W>` whose record name is
`qint_t`, not `qint`).

Rewrite shape: replace the `TypeLoc` source range with
`sturm::qint_t<W>` where `W` comes from the existing
`sturm::transpile::infer_width(VarDecl, InferContext)` for VarDecls and
falls through to `kDefaultWidth = 32` for the other anchor classes
(no initializer ⇒ rule 2 N/A ⇒ rule 3). Reuse
`render_qint_typename(W)` from `qram_emitter.cpp:110` (lifted to a
shared `render_qint_typename.hpp` so both emitters share one
definition).

Co-existence with the C1 matcher: the consumer keeps a
`std::unordered_set<const VarDecl*>` of decls already rewritten by the
QRAM emitter; the new emitter consults it and skips overlapping hits.

## 5. Acceptance criteria

A1. **Build & unit tests.** `cmake --build build --parallel 6` is clean,
`CTEST_PARALLEL_LEVEL=6 ctest --output-on-failure` is green.

A2. **Drift-gate.** `test_qint_alias_ops` covers every newly-added
operator with a positive SFINAE assertion plus the corresponding
negative assertion for `bool`-as-integral.

A3. **No-using-line example.** `examples/qram_demo.cpp` no longer
contains `using qint = sturm::frontend::qint;` and still compiles
clean. The pre-existing local-shadow bug (`qint a = 3, b = 4; … qint b
= a[i];` redeclares both `a` and `b`) is fixed in the same commit.

A4. **Transpile-substitution fixture.** A new
`tests/transpiler/fixtures/qint_alias_subst.cpp` containing a
`VarDecl`, a `ParmVarDecl`, a `FieldDecl`, a return-type, and a
`CXXFunctionalCastExpr` of bare `qint` round-trips through the
transpiler to a fixture-pinned `qint_alias_subst.expected.cpp` whose
every `qint` site is `sturm::qint_t<32>`.

A5. **End-to-end alias erasure.** After running the transpiler over
`examples/qram_demo.cpp`, the generated file
`build/sturm_gen/examples/qram_demo.cpp` contains zero matches for the
substring `sturm::frontend::qint`. The runtime asserts
`measurement_count() == 0` (G1, unchanged).

A6. **Backwards-compat fixture proof.** All 50+ existing
`tests/transpiler/fixtures/*.cpp` with local
`using qint = sturm::qint_t<W>;` keep their golden expected files
unchanged.

## 6. Risks & rollbacks

R1. *Repointing `sturm::qint` breaks a test that mutates
`q.super_mask` / `q.qubits[i]` directly.* Audited callsites:
`tests/test_resource_lifecycle.cpp:106`,
`tests/backend/test_when_control_stack_bridge.cpp:266,297,337,383`,
`tests/packaging/test_umbrella_only.cpp:34`. Mitigation: re-spell
those callsites as `sturm::qint_t<64>` in the same commit. Rollback:
revert the `qint_fwd.hpp` change; the operator-parity and
transpile-pass commits are independently revertable.

R2. *The new transpile pass double-rewrites a VarDecl already consumed
by C1.* Mitigation: shared "claimed-decls" set drained in defined
order, with a unit test `test_matcher_qint_alias_subst_no_overlap`
that pins single-VarDecl-single-rewrite for the `qint b = a[i];`
shape.

R3. *Width default of 32 surprises users who expected 64 from the
old `using qint = qint_t<64>;`.* Mitigation: documented in
`docs/qram_user_intro.md` migration note; the `kDefaultWidth` knob
already exists at `width_inference.hpp:58` for a one-line bump if
measurements push for 64.

R4. *Mixed-type arith adds 14 new free functions to the alias header,
breaching the file's stated 300-LoC budget*
(`qint_alias_ops.hpp:57`). Mitigation: factor a single
`make_mixed_arith_op` macro (or inline-template helper) so the marginal
LoC is small and uniform; if still over, split into
`qint_alias_ops_mixed.hpp`.

## 7. Follow-ups (out of scope)

- ~~`qbool`-returning compares on the alias.~~ **Promoted into scope by
  Wave 3 (§10);** absorbs bd `sturm-65rs.15` plus the parallel
  `operator[]` read and the mixed-type `qint OP <integral>` compares.
- Write-side `q[k] = …` on the alias (bd `sturm-65rs.16` — still
  deferred; requires a `BitProxy` width commitment).
- Per-ParmVarDecl / per-FieldDecl width inference (bd `sturm-65rs.17`
  — still deferred; currently rule-3 default; revisit if 32 turns out
  too narrow in practice).
- Bumping `kDefaultWidth` from 32 to 64 (bd `sturm-65rs.18` — still
  deferred).

## 8. References

- `include/sturm/qtypes/qint_alias.hpp`
- `include/sturm/qtypes/qint_alias_ops.hpp`
- `include/sturm/qtypes/qint_fwd.hpp`
- `include/sturm/qtypes/qint_core.hpp`
- `include/sturm/qtypes/qint_arith.hpp`,
  `include/sturm/qtypes/qint_bitwise.hpp`,
  `include/sturm/qtypes/qint_compare.hpp`
- `transpiler/src/matcher_qram_subscript.{hpp,cpp}`
- `transpiler/src/qram_emitter.cpp`
- `transpiler/src/width_inference.{hpp,cpp}`
- `tests/qtypes/test_qint_alias_ops.cpp`
- `examples/qram_demo.cpp`
- `docs/archive/prd_qram_subscript.md`,
  `docs/archive/prd_qram_backend.md`

---

## 9. Wave 2 — Array & pointer carrier coverage (2026-05-05)

Wave-1 G1–G4 / A1–A6 are met for the *direct* type-spelling shapes
(VarDecl, ParmVarDecl, FieldDecl, function return, functional cast).
A fourth defect surfaced after wave-1 closed:

### 9.1 Problem (additional)

4. **Alias erasure does not reach array-element or pointee types.**
   `examples/qram_demo.cpp` declares `qint a[4];` (the natural,
   user-facing shape per G2) and `qint b = a[i];` for the QRAM read.
   The wave-1 QRAM-subscript matcher rewrites `qint b = a[i];` into
   `::sturm::QRAM_read(a, i, b);` against `a` — but `a`'s declared
   type stays `sturm::frontend::qint[4]` because the alias-subst
   matcher's gate (`matcher_qint_alias_subst.cpp:230-232`,
   `varDecl(hasType(hasCanonicalType(hasDeclaration(
   frontend_qint_record()))))`) does not traverse the `ArrayType`
   carrier to reach the element record. None of the `QRAM_read`
   overloads (`include/sturm/qram/qram_read.hpp:242-271`) accept
   `frontend::qint[N]`, so the post-transpile build fails with
   *"could not match `qint_t<W>` against `qint`"*. The same gap exists
   for `qint*` pointee.

### 9.2 Goals (additive)

G5. **Array & pointer carrier erasure.** The alias-subst matcher's
VarDecl, ParmVarDecl, and FieldDecl anchors traverse `ArrayType`
element types and `PointerType` pointee types to reach the
`frontend::qint` record. The emitter substitutes only the element /
pointee TypeLoc span, so array bounds (`[N]`) and pointer punctuation
(`*`) survive verbatim. `qint a[4];` rewrites to
`sturm::qint_t<W> a[4];`; `qint* p` rewrites to `sturm::qint_t<W>* p`.

G6. **Backend-script clean invariant (CI gate).** Any file under
`build*/sturm_gen/**/*.cpp` produced by the transpile build step
contains zero matches for the substring `sturm::frontend::qint` and
zero `using qint = ::sturm::frontend::qint;` lines. Local typedefs
`using qint = sturm::qint_t<W>;` remain permitted (those are
backend-only — no confusion). Enforced by a ctest target that scans
the sturm_gen directory after every transpile and fails the build on
any hit. This operationalises wave-1 G3 ("full alias erasure") as a
permanent regression gate, not a one-shot fixture assertion.

### 9.3 Functional requirements (additive)

#### 9.3.1 Matcher extension

Extend `transpiler/src/matcher_qint_alias_subst.cpp` so the three
declarator anchors (VarDecl, ParmVarDecl, FieldDecl) match through
the carriers below. Bound TypeLoc range is the element / pointee
span, NOT the full declarator span.

| Carrier              | AST traversal                                          | v1?  |
|----------------------|--------------------------------------------------------|------|
| `qint a[N]`          | `arrayType(hasElementType(record(...)))` → element loc | yes  |
| `qint a[]`           | same — incomplete array (function parameter decay)     | yes  |
| `qint* p`            | `pointerType(pointee(record(...)))` → pointee loc      | yes  |
| `qint** pp`          | recursive pointee                                      | no   |
| `qint a[N][M]`       | multi-dim array                                        | no   |
| `qint (&r)[N]`       | reference to array                                     | no   |

The function-return-type anchor (FunctionDecl) and functional-cast
anchor (CXXFunctionalCastExpr) keep their wave-1 shape; carrier
returns and casts are out of scope (parallels wave-1 §3 non-goal on
per-Parm/Field width inference).

#### 9.3.2 Emitter extension

`transpiler/src/qint_alias_subst_emitter.cpp` consumes the new
TypeLoc ranges unchanged — it already substitutes whatever span the
matcher hands it. The element-or-pointee TypeLoc walk lives in the
matcher (single source of truth for "which token gets replaced").

#### 9.3.3 Backend-script clean CI gate

A new ctest target `test_sturm_gen_clean` (in `transpiler/tests/`):

1. Resolves the build's `sturm_gen/` directory via a CMake-generated
   header (`configure_file` of `<sturm_gen_path.hpp>`).
2. Globs `**/*.cpp` and `**/*.hpp` under it.
3. Asserts each file contains zero matches for either:
   - `sturm::frontend::qint` (any spelling, including
     `::sturm::frontend::qint`).
   - `using qint = ::sturm::frontend::qint` /
     `using qint = sturm::frontend::qint`.
4. Reports the offending file:line on failure.

Test depends on the transpile build step having materialised
`sturm_gen/` (CMake `add_dependencies`).

### 9.4 Acceptance criteria (additive)

A7. **Array carrier round-trip.** New fixture
`transpiler/tests/fixtures/qint_alias_subst_carray.{cpp,expected.cpp}`
covers `qint a[4];` (VarDecl), `void f(qint b[]);` (ParmVarDecl),
`struct S { qint c[3]; };` (FieldDecl). After transpile, every site
is `sturm::qint_t<32> name[N];` (or `[]` / `[3]`) with `[N]`
preserved verbatim.

A8. **Pointer carrier round-trip.** New fixture
`transpiler/tests/fixtures/qint_alias_subst_ptr.{cpp,expected.cpp}`
covers `qint* p;`, `void g(qint* q);`, `struct T { qint* d; };`.
After transpile, every site is `sturm::qint_t<32>* name;`.

A9. **`qram_demo.cpp` end-to-end green.** With wave-2 in place,
`examples/qram_demo.cpp` (in its current form: `qint a[4]; for(...)
{ a[i] = i; } qint i = 10; qint b = a[i];`) compiles, links, runs.
The build target `example_qram_demo` is GREEN under
`cmake --build build_mac --target example_qram_demo --parallel 6`
(per project CLAUDE.md `--parallel 6` hard cap).

A10. **Backend-script clean gate.** `test_sturm_gen_clean` is GREEN
in default CI. Negative-control verification (during G4 only,
manual): commenting out the array-element matcher arm makes the gate
RED with file:line output → restore.

### 9.5 Risks & rollbacks (additive)

R5. *Element TypeLoc walks through a user typedef.* If the user
writes `using QArr = qint[4]; QArr a;`, canonical type still
resolves to `frontend::qint[4]` but the user-source TypeLoc points
at `QArr`. Mitigation: the matcher's TypeLoc walk decomposes the
DeclaratorDecl's TypeSourceInfo (user spelling, not canonical). On
hitting a `TypedefTypeLoc` it DECLINES to substitute — the user's
typedef survives. Pinned by a fixture
`qint_alias_subst_carray_typedef.{cpp,expected.cpp}` whose expected
file is identical to the input.

R6. *Multi-dim arrays / reference-to-array slip through.* Wave-2
declares them out of scope. Mitigation: matcher unit test asserts
the matcher does NOT fire on `qint a[N][M]` and `qint (&r)[N]`.
A bd follow-up `sturm-qaca.6` is filed for the gap.

R7. *Build-system path drift between `build/`, `build_mac/`,
`build_linux/`.* The CMake-generated `sturm_gen_path.hpp` records the
actual configured build directory's `sturm_gen/`. Re-running cmake
configure regenerates it. The ctest target depends on this header,
so a stale path fails at compile, not silently.

### 9.6 References (additive)

- `examples/qram_demo.cpp` (wave-2 unblock target — already in
  target shape, do NOT edit).
- `transpiler/src/matcher_qint_alias_subst.cpp:230-274` (extension site).
- `transpiler/src/qint_alias_subst_emitter.cpp` (consumes new ranges).
- `build_mac/sturm_gen/examples/qram_demo.cpp:64,70` (failure-mode
  evidence captured during the wave-2 design session).
- `include/sturm/qram/qram_read.hpp:242-271` (overload set the
  rewritten carrier must satisfy).

---

## 10. Wave 3 — Mandatory transpiler; alias as pure type-stubs (2026-05-07)

Waves 1 + 2 left the alias with two coupled properties:

- *Lossy-correct runtime bodies.* Each operator measures both operands
  and computes the classical result so a TU that escapes the
  transpiler still produces the right values
  (`include/sturm/qtypes/qint_alias_ops.hpp:88-238`).
- *Asymmetric return types vs. the backend.* Compares return classical
  `bool` (`qint_alias_ops.hpp:189-238`) where `qint_t<W>::operator==`
  returns `qbool` (`qint_compare.hpp:45`). `operator[](size_t)` on
  the alias returns `bool` (`qint_alias.hpp:189-195`) where
  `qint_t<W>::operator[]` returns `qbool` (`qint_compare.hpp` banner
  line 5).

Wave 3 collapses both. The transpiler is mandatory in the build (the
host-clang invariant `sturm-yial` already enforces the plugin loads),
so the alias never executes at runtime and its bodies need only
type-check. Removing the runtime contract lets the return types snap
to the backend's, eliminating the asymmetry.

### 10.0 Status update on prior non-goals / follow-ups

| Item                                                | Wave 1 status | Wave 3 status                          |
|-----------------------------------------------------|---------------|----------------------------------------|
| `qbool`-returning compares on the alias             | non-goal §3   | **in scope** (G7)                      |
| `qbool` from `operator[]` read on the alias         | implicit §3   | **in scope** (G7) — same principle     |
| `qbool` from mixed-type compares (`qint OP int`)    | implicit §3   | **in scope** (G7) — same principle     |
| Write-side `q[k] = …` on the alias (`sturm-65rs.16`) | follow-up §7  | unchanged (still deferred — BitProxy)  |
| Per-Parm/Field width inference (`sturm-65rs.17`)    | follow-up §7  | unchanged (still deferred)             |
| Bump `kDefaultWidth` 32→64 (`sturm-65rs.18`)        | follow-up §7  | unchanged (still deferred)             |

`bd sturm-65rs.15` is absorbed by this wave and should be closed when
Wave 3 lands.

### 10.1 Problem (additional)

5. **Alias return types diverge from backend return types.** A user
   who writes `qbool c = (a == b);` against `frontend::qint`
   today gets a hard compile error pre-transpile (alias `==` returns
   `bool`); the same line works against `qint_t<W>`. The transpiler's
   contract is "spell-level type substitution," so it cannot patch
   over an expression whose *type* is wrong — only operand types.
   The fix is to align the alias's operator return types with the
   backend's.

6. **Lossy-correct alias bodies are dead weight under a mandatory
   transpiler.** Every operator on `frontend::qint` carries a
   classical-fallback body and bumps
   `qint_alias_detail::g_measurement_count`
   (`qint_alias.hpp:73-89, 175-247`,
   `qint_alias_ops.hpp:74-178`). The bodies exist to make
   pre-transpile execution lossy-correct; with the transpiler
   mandatory, that path is never reached. The bodies remain
   compilable code that influences nothing — and worse, they mask
   matcher-coverage gaps (a missed type-spelling shape produces a
   coincidentally-correct runtime value instead of failing loudly).

### 10.2 Goals (additive)

G7. **Return-type parity with the backend.** Every alias operator's
return type matches `qint_t<W>`'s counterpart:
- `frontend::qint::operator==/!=/</<=/>/>=` (member or free) return
  `sturm::qbool`.
- `frontend::qint::operator[](size_t)` (read) returns `sturm::qbool`.
- Mixed-type free compares `qint OP <integral>` return
  `sturm::qbool`.
- Arithmetic and bitwise ops return `frontend::qint` (already
  parity, unchanged).

G8. **Pure type-stub bodies.** Every alias operator body is the
trivial expression that produces a default-constructed return value
(`return qbool();`, `return qint{};`, `return 0;`, `return;`). No
classical math, no counter bumps, no use of `value_`. The class's
`int64_t value_` storage may stay (zero-init, vestigial) to preserve
ABI/layout for any callers that took addresses of `frontend::qint`
during Waves 1–2; consumers of `value_` are removed.

G9. **`measure_to_int` / `g_measurement_count` infrastructure
removed.** `qint_alias_detail::g_measurement_count`,
`measurement_count()`, `reset_measurement_count()`,
`bump_measurement_count()`, and `measure_to_int()` are deleted.
Tests that asserted `measurement_count() == 0` post-transpile (the
Wave-1 G1 e2e check) are replaced by the Wave-2 G6 sturm_gen-clean
gate, which is the strictly stronger contract.

G10. **Header coupling acknowledged.** `qint_alias_ops.hpp` includes
`sturm/qtypes/qbool.hpp` (which transitively pulls
`sturm/qtypes/qint_core.hpp`). Every TU that reaches the alias now
sees `qint_t<W>` and `qbool`. The B0 cycle audit
(`bd sturm-65rs.5`) is re-run and confirmed clean — `qint_core.hpp`
makes no `frontend::` references.

### 10.3 Functional requirements (additive)

#### 10.3.1 Operator return-type changes

| Header                  | Operator                                       | Old return | New return                    |
|-------------------------|------------------------------------------------|------------|-------------------------------|
| `qint_alias.hpp`        | `qint::operator[](size_t) const`               | `bool`     | `sturm::qbool`                |
| `qint_alias_ops.hpp`    | `operator==/!=/</<=/>/>=(qint, qint)`          | `bool`     | `sturm::qbool`                |
| `qint_alias_ops.hpp`    | `operator==/!=/</<=/>/>=(qint, Int)` (templ.)  | `bool`     | `sturm::qbool`                |
| (unchanged)             | arithmetic, bitwise, unary, shifts             | `qint`     | `qint` (no change)            |

Note: `explicit operator int64_t() const` keeps return type `int64_t`
(the cast itself is the type the user wrote — the transpiler doesn't
have to substitute it). `operator size_t() const` keeps return type
`size_t` for the same reason.

#### 10.3.2 Body shape (all operators)

Every body is one of:

```cpp
return qbool();              // compares, op[] read
return qint{};               // arithmetic, bitwise, unary, shifts
return 0;                    // operator int64_t / operator size_t
return *this;                // operator=(int64_t)
{ /* nothing */ }            // phi()/theta() proxies' += / -=
```

No `value_` reads or writes. No counter bumps. The class's
`value_` member stays as zero-init `int64_t` storage (G8) but is
never read or assigned by any operator body.

#### 10.3.3 Removed infrastructure

Delete from `include/sturm/qtypes/qint_alias.hpp`:
- `qint_alias_detail::g_measurement_count` (line 75)
- `qint_alias_detail::measurement_count()` (line 77)
- `qint_alias_detail::reset_measurement_count()` (line 81)
- `qint_alias_detail::bump_measurement_count()` (line 85)
- All `bump_measurement_count()` call sites in member bodies
  (`qint_alias.hpp:190, 204, 226, 234, 270, 281`).

Delete from `include/sturm/qtypes/qint_alias_ops.hpp`:
- `qint_alias_detail::measure_to_int(const qint&)` (line 77)
- `qint_alias_detail::mixed_arith` helper (line 92).
- All `measure_to_int(...)` call sites — bodies become trivial stubs
  (G8).

Delete the entire `qint_alias_detail` namespace from both headers
once the above are gone (the namespace becomes empty).

#### 10.3.4 Header dependency

Add to `include/sturm/qtypes/qint_alias_ops.hpp`:

```cpp
#include "sturm/qtypes/qbool.hpp"  // qbool return type for compares + op[] read
```

`qint_alias.hpp`'s `operator[]` declaration also needs `qbool`
visible; either include `qbool.hpp` there directly or move
`operator[]` to `qint_alias_ops.hpp` (preferred — keeps the bare
class header free of backend includes; aligns with the existing
"member ops in `qint_alias.hpp`, free ops in `qint_alias_ops.hpp`"
split).

#### 10.3.5 Test updates

- `tests/qtypes/test_qint_alias_ops.cpp`: drop *value-correctness*
  assertions on every operator (e.g. `(a + 5) == expected`); keep
  the SFINAE drift-gate `static_assert`s — they verify the
  *signatures* are present and rejected for `bool` integrals.
  Update positive-SFINAE assertions for compares and op[] read to
  expect return type `qbool`, not `bool` (use
  `std::is_same_v<decltype(a == b), qbool>`).
- `tests/qtypes/test_qint_alias.cpp`: drop assertions that mention
  `measurement_count()`. The Wave-1 G1 contract (counter stays at 0
  post-transpile) is replaced by Wave-2 G6 (sturm_gen-clean gate).
- Any test that constructs a `frontend::qint` and reads its value
  through an operator (rather than `classical_value()`): rewrite to
  use the `classical_value()` accessor or delete — operator bodies
  no longer compute meaningful values.

#### 10.3.6 Documentation / memory updates

- `qint_alias.hpp` header comment (lines 14-22, 51-72, 146-159, 180-188,
  208-222, 258-272): rewrite to describe the new contract. The
  "load-bearing implicit `operator size_t()`" framing is obsolete —
  the matcher's coverage gate is what's load-bearing now.
- `bd remember "sturm-hpp-umbrella-does-not-expose-qbool-operators"`:
  invalidated by §10.3.4 — qbool's operators now leak through the
  alias headers. Either retire the memory or update it to "every
  qint-alias-touching TU sees qbool's operators."
- `docs/qram_user_intro.md` migration note: add a paragraph that the
  alias is mandatory-transpile and pre-transpile execution is
  unsupported.

### 10.4 Acceptance criteria (additive)

A11. **Return-type SFINAE.** `test_qint_alias_ops.cpp` includes a
positive `static_assert(std::is_same_v<decltype(a == b),
sturm::qbool>)` for every alias compare (six member-shape, six
mixed-type-shape, twelve total) and one for `decltype(a[0])`.

A12. **Empty-bodies SFINAE / IR scan.** A new test
`tests/qtypes/test_qint_alias_stubs.cpp` compiles a TU that
instantiates each alias operator, lowers it to LLVM IR with `-S
-emit-llvm -O0`, and asserts:
- No call into `qint_alias_detail::*` (the namespace is gone).
- No load from any `value_` field of a `frontend::qint` instance
  inside any operator body.

  Negative-control verification (manual, one-shot during landing):
  re-introducing a `bump_measurement_count()` makes the test RED.

A13. **`sturm_gen`-clean gate stays GREEN under Wave-3 stubs.** The
Wave-2 G6 gate `test_sturm_gen_clean` is unchanged; verify it still
passes after the alias bodies are stripped (the gate's contract is
about post-transpile *output*, not alias internals — should be a
no-op verification but worth re-running once).

A14. **Removed-infra audit.** A grep over the source tree for
`measure_to_int|g_measurement_count|bump_measurement_count|reset_measurement_count|qint_alias_detail::`
returns zero hits outside the deleted-symbols announcement in
`docs/CHANGELOG.md` (or wherever release notes live).

A15. **Backwards-compat fixture proof.** All
`tests/transpiler/fixtures/*.cpp` that include the alias headers
keep compiling. The 50+ Wave-1 fixtures with local `using qint =
sturm::qint_t<W>;` are unaffected (those never touched the alias).

### 10.5 Risks & rollbacks (additive)

R8. *A consumer relied on `measurement_count()` for cost reporting.*
Audit: the counter has zero non-test consumers (grep confirms). If
external user code depends on it, the migration note in §10.3.6 is
the warning surface; the symbol's removal is a hard break.

R9. *qbool's umbrella exposure leaks operators into TUs that
previously didn't see them.* `qbool` provides `operator&&`,
`operator||`, `operator!`, contextual `operator bool`, etc. Any TU
that includes `qint_alias_ops.hpp` (transitively, via the umbrella)
now sees these. Mitigation: this is a contained ADL surface (qbool
is a class, not a template); existing user code that overloads
these names on its own types stays unambiguous because qbool is in
namespace `sturm`.

R10. *A frontend::qint operator gets invoked at runtime because the
matcher missed a spelling.* Today the body returns a
coincidentally-correct value; post-Wave-3 it returns garbage. The
mitigation IS the Wave-2 G6 gate (`test_sturm_gen_clean`). If a new
spelling shape ships before its matcher arm does, the gate fires
RED at build time, not at runtime — strictly louder than today's
"silent wrong number." The risk is therefore *reduced*, not raised.

R11. *Removing `value_` reads-and-writes invalidates any user who
took the address of an alias instance.* `value_` is a private
member, so no legitimate external read path exists. Internal reads
(via `classical_value()`) become meaningless but still compile;
either remove `classical_value()` or document it as "always returns
0 — vestigial." Recommendation: remove (it has only test consumers,
and the new contract is "no value semantics on the alias").

### 10.6 References (additive)

- `include/sturm/qtypes/qint_alias.hpp:73-89` (delete-site:
  `qint_alias_detail` counter infra).
- `include/sturm/qtypes/qint_alias_ops.hpp:74-178` (delete-site:
  `measure_to_int` + `mixed_arith` + bodies that call them).
- `include/sturm/qtypes/qbool.hpp:47-53` (default + classical-bool
  ctors — both allocate no qubit, used by all stub return values).
- `include/sturm/qtypes/qint_compare.hpp:5,45,59,73,…` (backend
  compare return types — the parity target).
- `transpiler/tests/test_sturm_gen_clean` (Wave-2 G6 — promoted to
  *the* coverage contract under Wave 3).
- bd issues: `sturm-65rs.15` (absorbed), `sturm-65rs.16` /
  `.17` / `.18` (still deferred).
