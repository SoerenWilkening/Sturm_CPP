# Project Instructions for AI Agents

This file provides instructions and context for AI coding agents working on this project.

## 🚨 HARD LIMIT: MAX 6 THREADS FOR ALL BUILD/TEST COMMANDS 🚨

**EVERY invocation of `cmake`, `cmake --build`, `ctest`, `make`, or `ninja` MUST be capped at 6 parallel threads/processes. NO EXCEPTIONS.**

- `cmake --build <dir> --parallel 6` (NEVER `--parallel` alone, NEVER higher)
- `ctest --parallel 6` or `CTEST_PARALLEL_LEVEL=6 ctest`
- `make -j6` (NEVER bare `-j`, NEVER `-j$(nproc)`)
- `ninja -j6`

This applies to every command you run, every subagent you spawn, every script you author, every CI config you touch. When spawning subagents (bd-worker, bd-autopilot, etc.) restate this limit in their prompt. If you see a build command without `-j6`/`--parallel 6`, fix it before running.

## Required Reading

At the start of every session, read:
- `docs/01_principles.md` — core design principles
- `docs/TODO_reversibility_deferrals.md` — outstanding reversibility deferrals

<!-- BEGIN BEADS INTEGRATION v:1 profile:minimal hash:ca08a54f -->
## Beads Issue Tracker

This project uses **bd (beads)** for issue tracking. Run `bd prime` to see full workflow context and commands.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work
bd close <id>         # Complete work
```

### Rules

- Use `bd` for ALL task tracking — do NOT use TodoWrite, TaskCreate, or markdown TODO lists
- Run `bd prime` for detailed command reference and session close protocol
- Use `bd remember` for persistent knowledge — do NOT use MEMORY.md files

## Session Completion

**When ending a work session**, you MUST complete ALL steps below. Work is NOT complete until `git push` succeeds.

**MANDATORY WORKFLOW:**

1. **File issues for remaining work** - Create issues for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **PUSH TO REMOTE** - This is MANDATORY:
   ```bash
   git pull --rebase
   bd dolt push
   git push
   git status  # MUST show "up to date with origin"
   ```
5. **Clean up** - Clear stashes, prune remote branches
6. **Verify** - All changes committed AND pushed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until `git push` succeeds
- NEVER stop before pushing - that leaves work stranded locally
- NEVER say "ready to push when you are" - YOU must push
- If push fails, resolve and retry until it succeeds
<!-- END BEADS INTEGRATION -->


## Build & Test

**First-time setup (per machine / container):** install ccache + ninja and
write the PCH-safe sloppiness config. Idempotent — rerun anytime.

```bash
bash tools/setup_dev_env.sh
```

Then configure with the **Ninja** generator (incremental dependency scanning
on this tree's ~1500 .o files is ~10× faster than GNU Make). The top-level
CMake auto-wires `ccache` as `CMAKE_<LANG>_COMPILER_LAUNCHER` when it is on
PATH (gated by `STURM_USE_CCACHE`, default ON; sturm-q2eb).

```bash
# Configure (macOS / Homebrew LLVM 17). The host-clang invariant below
# requires CMAKE_CXX_COMPILER and LLVM_DIR to come from the SAME LLVM
# install — see "Host-clang invariant" below.
cmake -S . -B build -G Ninja \
    -DCMAKE_CXX_COMPILER=/usr/local/opt/llvm@17/bin/clang++ \
    -DLLVM_DIR=/usr/local/opt/llvm@17/lib/cmake/llvm \
    -DClang_DIR=/usr/local/opt/llvm@17/lib/cmake/clang
cmake --build build --parallel 6
ctest --test-dir build --parallel 6 --output-on-failure
```

On the Linux agent container substitute `/usr/lib/llvm-17/` for the
compiler/cmake-package prefixes above.

**Why this matters:** without ccache + Ninja the agent's iterative build
loop ran ~30 min per cycle; the user mandated a <2 min incremental cap.
If you must run an uncached baseline (e.g. measuring a regression), pass
`-DSTURM_USE_CCACHE=OFF` to skip the launcher wiring.

### Fast-build guards (sturm-e9gj)

The top-level `CMakeLists.txt` carries configure-time `FATAL_ERROR` gates
that refuse to proceed down any silent slow-build path. Each gate has an
explicit, documented opt-out — but the default behaviour is to fail
loudly with an actionable hint rather than degrade silently.

| Silent degradation | Guard | Opt-out (only if you really know why) |
|---|---|---|
| `STURM_USE_CCACHE=ON` and `ccache` not on PATH | FATAL: install via `tools/setup_dev_env.sh` | `-DSTURM_USE_CCACHE=OFF` (loud WARNING — uncached baseline measurement only) |
| `STURM_USE_MOLD=ON` on Linux and `mold` not on PATH | FATAL: install via `tools/setup_dev_env.sh` | `-DSTURM_USE_MOLD=OFF` (loud WARNING) |
| Generator is not Ninja (e.g. default `Unix Makefiles`) | FATAL: pass `-G Ninja` and wipe `build/` | `-DSTURM_ALLOW_NON_NINJA=ON` (restricted CI only — emits a WARNING; JOB_POOLS clamp is then advisory) |
| `STURM_UNITY_BUCKETS=OFF` or `STURM_UNITY_MATCHERS=OFF` | Loud WARNING (not FATAL, so unity-collision bisects still work) | Default ON; only flip OFF during bisection |
| Parallelism cap bypassed (e.g. `ninja -j20`) | Ninja `JOB_POOLS` clamp at `STURM_BUILD_JOBS` (default `6`) caps both compile and link pools at the build-graph level — `-j20` runs at most 6 concurrent jobs | `-DSTURM_BUILD_JOBS=N` (only raise on dev machines with ≥N GB RAM; the 8 GB agent container WILL OOM at N > 6) |

If you see a `sturm-e9gj:` error during `cmake -S . -B build ...`, follow
the diagnostic — it points at the specific fix line. **Never** add
`-DSTURM_USE_CCACHE=OFF` / `-DSTURM_USE_MOLD=OFF` / `-DSTURM_ALLOW_NON_NINJA=ON`
to silence a guard without first attempting `bash tools/setup_dev_env.sh`
to install the missing tool — that is what those guards exist to prevent.

### Host-clang invariant (sturm-yial)

The transpiler plugin (`sturm-transpile-plugin`) links the
`libclang-cpp` / `libLLVM` shared libraries from the LLVM install
resolved via `find_package(LLVM CONFIG)` — typically the one pinned by
`LLVM_DIR`. Its `FrontendPluginRegistry::Add<>` static registrar lands
in *that* libclang-cpp's static registry. **The host clang
(`CMAKE_CXX_COMPILER`) must come from the same LLVM install.** When
the two installs differ (e.g. Apple Clang at `/usr/bin/c++` while
`LLVM_DIR` points at Homebrew's `/usr/local/opt/llvm@17`), the host's
own statically linked `FrontendPluginRegistry` never sees the `Add<>`;
the plugin loads cleanly but its `getActionType() == ReplaceAction` is
invisible, the host falls back to the default `EmitObjAction` against
the *unrewritten* source, and every transpiler matcher (qint alias
substitution, qram-subscript rewrite, etc.) silently no-ops. Tests
link, run, and emit zero gates.

The top-level `CMakeLists.txt` carries a configure-time gate
(`_sturm_check_host_clang_gate`) that compares the install prefixes of
`CMAKE_CXX_COMPILER` and `LLVM_DIR` and `FATAL_ERROR`s on mismatch
with an actionable diagnostic. If you see a `sturm-yial` error block
during configure, follow its "Recommended" line — it points at the
`clang++` that ships with the same LLVM install as your `LLVM_DIR`.
The pinned ctest is `cmake_host_clang_gate` (gated behind
`STURM_FULL_TEST_SUITE=ON`).

## Project Context

Track all work using **beads (`bd`)** — see the Beads Issue Tracker section above. Do not use ad-hoc TODO lists.

## Architecture Overview

_Add a brief overview of your project architecture_

## Conventions & Patterns

_Add your project-specific conventions here_
