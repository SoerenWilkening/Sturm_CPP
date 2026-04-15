# Phase C — qint-qint arithmetic (transpiler post-MVP)

## Context

Phase C of `docs/roadmap_transpiler_post_mvp.md` extends the sturm-transpile
compound-assign coverage from **classical RHS** (Phase B, just landed) to
**qint RHS**, and retires the five stub tagged-union branches in
`uncompute_op.hpp` that Phase C's free functions replace.

| Pattern | Phase C id | Emitted inverse |
|---|---|---|
| `a += b;` (b qint) | PC-1 | `uncompute_add_qint(a, b);` |
| `a -= b;`           | PC-2 | `uncompute_sub_qint(a, b);` |
| `a *= b;`           | PC-3 | `uncompute_mul_qint(a, b);` |
| `a /= b;`           | PC-4 | `uncompute_div_qint(a, b);` |
| `a %= b;`           | PC-5 | `uncompute_mod_qint(a, b);` |

## Resolved design decisions

1. **Emission form** — all 5 ops emit `uncompute_*_qint(a, b);` free-function calls, matching the roadmap phrasing literally and giving a uniform call-site shape that can be extended later (mask checks, instrumentation) in one place.
2. **Mul/div inverse policy** — dual-operator bodies with documented user-responsibility caveat (coprime with 2^W for mul; no overflow for div), same treatment Phase B applied to classical-constant `*=`/`/=`.
3. **Mod inverse (PC-5)** — included with a stub body + TODO comment. The matcher, QOpKind, and emission are complete; the runtime effect is a no-op pending a real adjoint.
4. **Retirement scope** — full retirement in Phase C: delete the 5 enum values, 5 factory functions, 5 empty switch cases, 5 stamp sites, and the roundtrip assertions for the removed tags.

---

## Work breakdown (one bd issue per piece)

### PC-runtime — Free-function inverses in `uncompute_api.hpp`

Add a new section to `include/sturm/uncompute/uncompute_api.hpp` after the `uncompute_or` declaration. Template on `std::size_t W`, header-only inline (no `.cpp` body needed — each is a one-liner that delegates to the forward compound-assign):

```cpp
// Phase C — qint-qint arithmetic inverses.
// Caveats (user responsibility):
//   uncompute_mul_qint: b coprime with 2^W (otherwise a *= b is not invertible).
//   uncompute_div_qint: a*b must not overflow (otherwise a /= b is lossy).
//   uncompute_mod_qint: no clean dual; stub for now, see TODO.
template <std::size_t W>
inline void uncompute_add_qint(qint_t<W>& a, const qint_t<W>& b) { a -= b; }
template <std::size_t W>
inline void uncompute_sub_qint(qint_t<W>& a, const qint_t<W>& b) { a += b; }
template <std::size_t W>
inline void uncompute_mul_qint(qint_t<W>& a, const qint_t<W>& b) { a /= b; }
template <std::size_t W>
inline void uncompute_div_qint(qint_t<W>& a, const qint_t<W>& b) { a *= b; }
template <std::size_t W>
inline void uncompute_mod_qint(qint_t<W>& /*a*/, const qint_t<W>& /*b*/) {
    // TODO(phase-later): real modular-inverse adjoint.
    // No simple dual — the forward op throws away the quotient.
}
```

The forward compound-assigns already exist for all five widths at
`include/sturm/qtypes/qint_arith_v3.hpp:58-170`, so `a -= b` / `a += b` / `a /= b` / `a *= b` delegate correctly under `STURM_BACKEND_ENABLED`.

Include `sturm/qtypes/qint.hpp` from `uncompute_api.hpp` so the templates see the forward operators.

### PC-retire — Remove dead tagged-union branches

Delete from `include/sturm/uncompute/uncompute_op.hpp`:
- Enum values `ADD_QINT`, `SUB_QINT`, `MUL_INVERSE`, `DIV_INVERSE`, `MOD_INVERSE` (lines 47-51)
- Factory functions `make_add_qint`, `make_sub_qint`, `make_mul_inverse`, `make_div_inverse`, `make_mod_inverse` (lines 122-155)
- Switch cases in `apply()` (lines 215-234)
- Update top-of-file comment block (lines 17-24) to drop the corresponding TODO notes

Delete from `include/sturm/qtypes/qint_arith_backend.hpp` the single `result.uncompute_ = uncompute_op::make_*_qint/inverse(...)` assignment at the end of each forward binary op:
- `operator+` — lines 142-144
- `operator-` — lines 170-172
- `operator*` — lines 201-203
- `operator/` — lines 231-233
- `operator%` — lines 268-269

Update `tests/uncompute/test_uncompute_op.cpp` — remove the roundtrip assertions for the 5 deleted tags (lines 67-84 per the explore pass). Keep tests for remaining tags (`NONE`, `ADD_CONST`, `SUB_CONST`, `BITWISE_SELF`, `COMPARE`).

Net behavior change: zero. The stamped tags today hit an empty `apply()` switch; removing the stamps + enum + switch gives the same runtime effect.

### PC-ir — QOpKind + render_uncompute cases

Add to `transpiler/include/sturm/transpile/qir.hpp:74-88`:
```cpp
ADD_ASSIGN_QINT,
SUB_ASSIGN_QINT,
MUL_ASSIGN_QINT,
DIV_ASSIGN_QINT,
MOD_ASSIGN_QINT,
```

Add to `transpiler/src/uncompute_pass.cpp` after line 106, following Phase B's per-kind comment style, 5 cases emitting one line each:
```cpp
case QOpKind::ADD_ASSIGN_QINT: {
    if (op.operands.size() != 1) return {};
    os << "    uncompute_add_qint(" << op.result.name << ", "
       << op.operands[0].name << ");\n";
    break;
}
// … four more analogous cases for SUB/MUL/DIV/MOD.
```

The `no default:` convention (qir.hpp:88 comment) enforces exhaustive switches across the codebase.

### PC-matcher — 5 Clang matchers + callbacks + registration

Mirror `register_add_assign_const_matcher` (`transpiler/src/matcher.cpp:717-760`), one matcher per operator. Two structural diffs from Phase B:

**RHS guard differs** — Phase B peeled `CXXConstructExpr` (converting constructor); Phase C peels nothing and requires the RHS be a `DeclRefExpr` to a qint_t:
```cpp
hasArgument(1, ignoringImplicit(
    declRefExpr(hasType(hasCanonicalType(hasDeclaration(
        cxxRecordDecl(hasName("qint_t"))))))
        .bind("rhs_expr")))
```
The two shapes are structurally disjoint: Phase B's AST always has a `CXXConstructExpr` wrapper (lifting int to qint); Phase C's AST has a bare `DeclRefExpr` (the user already typed a qint). No false-cross-fire between the matchers.

**Five matchers, five callbacks** — the `%=` op is new (Phase B omitted it). Each callback mirrors `AddAssignConstCallback` (`matcher.cpp:390-424`): bind `call`, `lhs`, `rhs_expr`; pull verbatim RHS source text via `Lexer::getSourceText`; construct `QOperation { kind: <KIND>, result: lhs, operands[0].name = <rhs_text> }`; append to `find_or_create_scope(...).ops`.

Each matcher registered via its own callback pool function (mirror lines 426-430), and `finder.addMatcher(pattern, pool.back().get())` at the end of the `register_*_matcher` function.

Add 5 calls to `register_{add,sub,mul,div,mod}_assign_qint_matcher(finder, unit)` in `transpiler/src/driver.cpp` (or wherever the Phase B matchers are registered — grep for `register_add_assign_const_matcher` to locate).

### PC-fixtures — PC-1..PC-5 snapshot fixtures

New files in `tests/transpiler/fixtures/` (flat layout, matching Phase B):

| Input | Expected |
|---|---|
| `add_assign_qint.cpp` | `add_assign_qint.expected.cpp` |
| `sub_assign_qint.cpp` | `sub_assign_qint.expected.cpp` |
| `mul_assign_qint.cpp` | `mul_assign_qint.expected.cpp` |
| `div_assign_qint.cpp` | `div_assign_qint.expected.cpp` |
| `mod_assign_qint.cpp` | `mod_assign_qint.expected.cpp` |

Fixture shape (mirror `add_assign_const.cpp:1-22` but drop the `qint_t(long long)` converting constructor and declare `b` as a second qint parameter):
```cpp
// Phase C / PC-1 input — qint-qint compound assign.
namespace sturm {
template <int W>
class qint_t {
public:
    qint_t() {}
    qint_t(const qint_t&) {}
    qint_t& operator+=(const qint_t&) { return *this; }
};
} // namespace sturm
using qint = sturm::qint_t<1>;

void demo(qint a, qint b) { a += b; }
```

Expected output: input verbatim (with `AUTO-GENERATED` header prepended) and `    uncompute_add_qint(a, b);\n` injected on the same line as the closing brace, matching Phase B's injection style (`add_assign_const.expected.cpp:24-25`).

Register each fixture individually in `tests/transpiler/CMakeLists.txt` inside the `if(STURM_TRANSPILE)` block, mirroring the Phase B `add_test(NAME snapshot_add_assign_const ...)` at lines 128-176. Properties: `LABELS "transpiler"` and `REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>"`.

### PC-example — End-to-end example + injection + idempotency tests

New file `examples/qint_arith.cpp` — mirror `examples/constant_arith.cpp:1-75`. Two qints `a, b` (use `qint_t<8>`), inner scope exercises the five forward ops, closing brace triggers LIFO injection of the five `uncompute_*_qint` calls.

Documentation comment (lines 13-44 of `constant_arith.cpp`) adapted for PC-1..PC-5: the pattern table, the `build/sturm_gen/examples/qint_arith.cpp` view instructions, the run command, the `STURM_AUTO_UNCOMPUTE=OFF` caveat. Values chosen so classical short-circuit keeps `a` consistent through the chain (e.g. `a = 6`, `b = 2` gives `a = 6 → 8 → 6 → 12 → 6 → 0 → ?` — pick values that avoid classical div-by-zero).

New CMake target in `examples/CMakeLists.txt`, cloning the `example_constant_arith` block:
```cmake
add_quantum_executable(example_qint_arith qint_arith.cpp)
# wire runtime sources + orkan_headers identical to example_constant_arith
```

New CTests in `tests/transpiler/CMakeLists.txt` (mirror lines 307-337):
1. `build_example_qint_arith` (SETUP fixture) — `cmake --build ... --target example_qint_arith`.
2. `transpiler_example_qint_arith_injected` — new `check_example_qint_arith.cmake` asserts generated file contains each of the 5 `uncompute_*_qint` call texts in LIFO order after the last forward statement, and verifies source file remains untouched.
3. `transpiler_idempotent_example_qint_arith` — reuses `check_idempotent.cmake` on the generated file.

All three labeled `"transpiler"`; injection + idempotency require the `example_qint_arith_built` fixture.

### PC-docs — Roadmap completion note

Append to `docs/roadmap_transpiler_post_mvp.md` Phase C section (lines 67-81) a completion block mirroring Phase A (lines 15-23) and Phase B (lines 39-51):

```
> **<YYYY-MM-DD>:** Complete. All five compound-assign patterns
> (`a += b;`, `a -= b;`, `a *= b;`, `a /= b;`, `a %= b;` with `b`
> another qint) match and emit a free-function call as inverse. …
> Snapshot fixtures: `add_assign_qint`, `sub_assign_qint`,
> `mul_assign_qint`, `div_assign_qint`, `mod_assign_qint`.
> End-to-end: `examples/qint_arith.cpp` + two CTests.
> Runtime-side, the tagged-union branches ADD_QINT / SUB_QINT /
> MUL_INVERSE / DIV_INVERSE / MOD_INVERSE were retired in this phase.
> Mul/div inverses carry the coprime / overflow caveat documented in
> uncompute_api.hpp; MOD_INVERSE ships with a TODO stub body. Next
> up: Phase D.
```

---

## Bd issues + dependencies

Seven issues total, sequenced:

```
PC-runtime  ─┐
             ├→ PC-ir → PC-matcher → PC-fixtures → PC-example → PC-docs
PC-retire   ─┘
```

- PC-runtime and PC-retire can run in parallel; both must land before PC-ir.
- PC-ir adds QOpKind (consumed by PC-matcher's bound kind and PC-fixtures' expected output).
- PC-matcher wires the frontend; PC-fixtures tests it in isolation.
- PC-example combines runtime + transpiler paths end-to-end.
- PC-docs records the completion in the roadmap.

---

## Critical files

| # | File | Change |
|---|---|---|
| 1 | `include/sturm/uncompute/uncompute_api.hpp` | +5 template functions |
| 2 | `include/sturm/uncompute/uncompute_op.hpp` | −5 enum values, −5 factories, −5 switch cases |
| 3 | `include/sturm/qtypes/qint_arith_backend.hpp` | −5 stamp sites (one per binary op) |
| 4 | `tests/uncompute/test_uncompute_op.cpp` | −5 roundtrip assertions |
| 5 | `transpiler/include/sturm/transpile/qir.hpp` | +5 QOpKind entries |
| 6 | `transpiler/src/uncompute_pass.cpp` | +5 render cases |
| 7 | `transpiler/src/matcher.cpp` | +5 matcher funcs, callbacks, pools |
| 8 | `transpiler/src/driver.cpp` | +5 registration calls |
| 9 | `tests/transpiler/fixtures/*_assign_qint{,.expected}.cpp` | 10 new files |
| 10 | `tests/transpiler/CMakeLists.txt` | +5 snapshot tests, +3 example tests, +1 setup fixture |
| 11 | `tests/transpiler/check_example_qint_arith.cmake` | new driver |
| 12 | `examples/qint_arith.cpp` + `examples/CMakeLists.txt` | new example |
| 13 | `docs/roadmap_transpiler_post_mvp.md` | completion note |

---

## Verification

```bash
# Build
cmake --build build --target sturm-transpile example_qint_arith

# Snapshot tests (5 new)
ctest --test-dir build -R 'snapshot_.*_assign_qint' -V

# Injection + idempotency on end-to-end example
ctest --test-dir build -R 'transpiler_example_qint_arith|transpiler_idempotent_example_qint_arith' -V

# Full transpiler suite (no regressions)
ctest --test-dir build -L transpiler

# Runtime retirement regression — confirm removed tags no longer referenced
ctest --test-dir build -R 'test_uncompute_op' -V
```
