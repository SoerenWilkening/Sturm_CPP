# Build-Perf Investigation — `-ftime-trace` Sweep (sturm-pjtx)

Date: 2026-05-19. Closes the deliverable for bd issue `sturm-pjtx`.

## Setup

```
cmake -S . -B build_trace -G Ninja \
    -DCMAKE_CXX_COMPILER=/usr/lib/llvm-17/bin/clang++ \
    -DLLVM_DIR=/usr/lib/llvm-17/lib/cmake/llvm \
    -DClang_DIR=/usr/lib/llvm-17/lib/cmake/clang \
    -DCMAKE_CXX_FLAGS="-ftime-trace" \
    -DSTURM_USE_CCACHE=OFF
cmake --build build_trace --target test_bucket_qram --parallel 6
```

- Cold build: **11m24s wall / 49m user CPU on 6 parallel jobs.**
- 142 per-TU `.json` traces collected (`build_trace/**/CMakeFiles/*.dir/**/*.json`).
- Aggregated with `/tmp/agg_time_trace.py` (Source-event duration summed per header across all TUs).

IWYU was not available in this container; this report relies entirely on
`-ftime-trace` data.

## Headline numbers (aggregate Source parse time across consuming TUs)

| Total ms | Consumers | Avg ms | Header |
|---------:|----------:|-------:|--------|
| 81 417 | 40 | 2 035 | `transpiler/src/matcher_common.hpp` |
| 58 881 | 3 | 19 627 | `clang/ASTMatchers/ASTMatchers.h` |
| 56 157 | 3 | 18 719 | `clang/ASTMatchers/ASTMatchFinder.h` |
| 56 045 | 2 | 28 022 | `transpiler/src/matcher_qram_subscript.hpp` |
| 29 622 | 20 |  1 481 | `c++/15/filesystem` |
| 27 034 |  3 |  9 011 | `clang/AST/ASTContext.h` |
| 21 025 |  5 |  4 205 | `clang/Frontend/CompilerInstance.h` |
| 17 419 | 23 |    757 | `transpiler/src/diag_context.hpp` |
| 14 565 |  8 |  1 821 | `sturm/transpile/emitter.hpp` (tests view) |
| 13 283 | 13 |  1 022 | `transpiler/src/routine_registry.hpp` |
| 11 557 | 17 |    680 | `c++/15/unordered_set` |
| 11 198 | 17 |    659 | `c++/15/bits/unordered_set.h` |
| 10 868 | 38 |    286 | `sturm/transpile/qir.hpp` (tests view) |

The TUs that dominate wall time are `matcher_*` files (50–73 s each) and the
three `test_bucket_qram` Unity chunks (51–55 s each).

## Root-cause crosscheck

- `clang/ASTMatchers/ASTMatchers.h` shows up at 19.6 s × 3 TUs even though
  it is **already in the PCH** (`build_trace/transpiler/CMakeFiles/
  sturm-transpile.dir/cmake_pch.hxx` lines 13–14). The three outliers are
  the two `test_bucket_qram` Unity TUs (27.9 s each) and the PCH build
  itself (3.4 s, paid once). Every other TU consumes the PCH and pays
  zero. **test_bucket_qram skips the PCH** — by design, per the
  `sturm-bs8s` comment in `transpiler/tests/CMakeLists.txt`:

  > mixed C+C++ target — gate_kind.c is linked in, but the transpiler's
  > REUSE_FROM PCH is C++-only.

- `matcher_qram_subscript.hpp` paying 28 s × 2 TUs is the same root cause:
  the only TUs without PCH are the two qram bucket Unity chunks, and they
  re-parse the 1087-line `matcher_common.hpp` chain from scratch.

- `<filesystem>` showing up at 1.5 s × 20 TUs (29.6 s aggregate) is **not**
  driven by `io.cpp` / `main.cpp` (which actually need it). It is driven
  by 4 matcher TUs (`matcher_ccnot_fuse`, `matcher_qbool_compound`,
  `matcher_peephole_reorder`, `matcher_when_lift`) that include
  `sturm/transpile/emitter.hpp` — and emitter.hpp has a **dead
  `#include <filesystem>` at line 51** (verified: zero `fs::` /
  `std::filesystem` uses in the file; the public API takes
  `std::string_view source_path`).

- `matcher_common.hpp`'s 81 s aggregate is its own inline-body parse cost
  (1087 LOC of inline helpers + RecursiveASTVisitor instantiations),
  not transitive include cost — most of its `clang/AST/*` includes are
  PCH-covered for the 37 non-bucket consumers.

## Top 5 pruning opportunities — ranked by quantified savings

| # | What to prune | Consumers (n) | Per-TU savings | Aggregate savings | Risk |
|---|---|---:|---:|---:|---|
| 1 | Restore PCH coverage for `test_bucket_qram` (carve `gate_kind.c` into its own OBJECT lib so the bucket can `REUSE_FROM sturm-transpile`) | 3 Unity TUs | ~28 s | **~85 s** | Low — pattern matches the recent sturm-x4iq / sturm-grhw OBJECT-lib carve-outs |
| 2 | Split `transpiler/src/matcher_common.hpp` (1087 LOC) into ~3 focused helpers: `matcher_scope.hpp`, `matcher_flatten.hpp`, `matcher_render.hpp`. Each matcher_*.cpp pulls only what it uses. | 40 TUs | ~1.5 s | **~60 s** | Medium — requires per-matcher audit of which helpers are actually used; aligns with CLAUDE.md 300-LOC header cap (current file is 3.6× over). |
| 3 | Drop dead `#include <filesystem>` from `transpiler/include/sturm/transpile/emitter.hpp:51` | 4 matcher TUs that consume emitter.hpp but never touch `fs::` | ~1.5 s | **~6 s** | None — verified zero `fs::` uses; the include is unconditional dead weight. |
| 4 | Move `#include <unordered_set>` from `transpiler/include/sturm/transpile/plugin_api.hpp` (pImpl or fwd-decl the `Registry` set member, push the include to plugin_api.cpp) | 7 plugin_api consumers + transitively to 17 unordered_set consumers | ~0.7 s | **~10 s** | Low — verify that no public Registry method takes `unordered_set<T>&` by value. |
| 5 | Public-header IWYU audit: `sturm/transpile/qir.hpp` (38 consumers!) and `sturm/transpile/matcher.hpp` (21 consumers) — both currently pull in `clang/AST/ASTContext.h` and `clang/ASTMatchers/ASTMatchFinder.h` in headers. Forward-declare and push to the .cpp. | 21–38 TUs | ~0.3 s | **~10 s** | Medium — qir.hpp is the widest-blast-radius header in the transpiler; needs careful audit to verify the fwd-decl is sufficient for every consumer. |

**Total estimated cold-build savings if all 5 land: 150–180 s** (~25–30%
off an 11m24s cold build of `test_bucket_qram`).

## Measured: option C (matcher_common.hpp into the PCH) lands much bigger than estimated

The original estimate for option #3 from the deeper-cut list (sturm-cxvo:
"add matcher_common.hpp to the transpiler PCH") was ~30 s wall, scaled
from a 2 s/TU × 37 TUs / 6× parallelism back-of-envelope. The actual
empirical result, measured by rebuilding `test_bucket_qram` cold against
`build_pch_exp/` with `matcher_common.hpp` added to the PCH list:

| Metric | Baseline (build_trace) | PCH-expanded (build_pch_exp) | Delta |
|--|--:|--:|--:|
| Wall time | 11m24s | **6m14s** | **−5m10s (−46%)** |
| User CPU | 49m08s | 25m24s | −23m44s (−48%) |
| PCH file size | 91.8 MB | 100.5 MB | +8.7 MB |
| Slowest matcher TU (`matcher_when_operand_mutation.cpp`) | 73.0 s | 34.6 s | −53% |
| Slowest qram Unity TU (`unity_0_cxx`) | 54.7 s | 40.3 s | −27% |
| 8 qram bucket ctests | green | green | — |

The estimate was off by a factor of ~10 because `-ftime-trace`'s "Source"
events only measure parse cost — they do NOT capture the
template-instantiation and codegen cost that downstream consumers of
`matcher_common.hpp`'s inline helpers pay. The PCH amortizes both, so
the real per-TU savings on the heaviest matchers is ~30–40 s of CPU,
not ~2 s.

**Implication:** option C alone gets ~46% wall reduction on cold builds.
Combined with sturm-18zc (PCH for test_bucket_qram, the only remaining
heavy non-PCH'd target) and sturm-8vdv (unity-build transpiler src), the
cold-build target of "well under 5 min" is in reach.

The PCH-grew-by-8.7 MB tradeoff is paid once per build and amortized
across every TU; the steady-state incremental build is unaffected
(only changed `.cpp` files rebuild, and they consume the same PCH).

### Notes on the top item

The PCH carve (#1) is by far the biggest single win and should land
first. The pattern is straightforward and already established:

- `gate_kind.c` currently sits inline in the `test_bucket_qram`
  `add_executable(...)` source list (`transpiler/tests/CMakeLists.txt`
  around line 1551). It is the *only* C-language source in the bucket.
- Carve it into its own OBJECT lib (e.g.
  `add_library(sturm_gate_kind OBJECT src/sturm/core/gate_kind.c)`)
  alongside the recently landed `sturm_backend_runtime` (sturm-x4iq) and
  `sturm_examples_runtime` (sturm-grhw).
- Link the OBJECT lib into `test_bucket_qram` via
  `target_link_libraries(... PRIVATE sturm_gate_kind)`.
- The bucket is now C++-only and can take the standard
  `target_precompile_headers(test_bucket_qram REUSE_FROM sturm-transpile)`
  treatment that every other transpiler test bucket uses.

If the same `.c` constraint blocks other buckets it would be worth
sweeping them all in the same epic, but that scoping decision is for
the bd issue we file next, not this report.

## Followup bd issues filed

Each opportunity above is filed as its own bd issue. This report is the
deliverable for sturm-pjtx; the implementations land elsewhere.

- `#1` PCH carve for test_bucket_qram
- `#2` matcher_common.hpp split
- `#3` Drop `<filesystem>` from emitter.hpp
- `#4` Move `<unordered_set>` out of plugin_api.hpp
- `#5` IWYU sweep on qir.hpp + matcher.hpp public surface

## Reproducing

```
cmake --build build_trace --target test_bucket_qram --parallel 6
python3 /tmp/agg_time_trace.py build_trace --top 40
python3 /tmp/who_includes.py --header "<header substring>" build_trace
```

Aggregator scripts: `/tmp/agg_time_trace.py`, `/tmp/who_includes.py`.
