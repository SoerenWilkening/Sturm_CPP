# PRD — Frontend `qint` alias completion + transpiler-driven type substitution

**Status.**
- Wave 1 (§§1–8): shipped 2026-05-04 under bd epic `sturm-65rs`
  (issues `.1`–`.14` closed). Follow-ups `.15`–`.18` open and labeled
  out-of-scope per §7.
- Wave 2 (§9): drafted 2026-05-05. Tracked under bd epic `sturm-qaca`.
  Triggered by post-wave-1 build failure on
  `examples/qram_demo.cpp` (array-carrier alias erasure gap).

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

- `qbool`-returning compares on the alias. The alias compares return
  classical `bool`. Wiring `qbool` through the alias would force a
  backend-width commitment on every compare result; that is a separate
  decision (see §7 follow-ups).
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

- `qbool`-returning compares on the alias.
- Write-side `q[k] = …` on the alias.
- Per-ParmVarDecl / per-FieldDecl width inference (currently rule-3
  default; revisit if 32 turns out too narrow in practice).
- Bumping `kDefaultWidth` from 32 to 64.

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
