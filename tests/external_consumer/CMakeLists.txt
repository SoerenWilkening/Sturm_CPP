cmake_minimum_required(VERSION 3.16)

# tests/external_consumer/CMakeLists.txt — sturm-795j.1 / E6.M1 acceptance
# project. This is a STANDALONE CMake project that consumes an *installed*
# STURM prefix via `find_package(sturm REQUIRED)`. PRD §3.6 makes this
# project the smoke test for the packaging-export delivery: it must
# configure, build, and run with nothing on disk except the contents of
# `<install-prefix>/include`, `<install-prefix>/lib/cmake/sturm/`, and
# `<install-prefix>/bin/sturm-transpile{,-plugin}` — i.e. exactly what
# `cmake --install` of the parent project ships (root CMakeLists.txt §1–3
# + transpiler/CMakeLists.txt §install).
#
# This same file doubles as the E8.M2 documentation walkthrough source
# (Plan §E8.M2): the comments narrate every step a downstream consumer
# would write themselves, so the file can be quoted verbatim into the
# packaging tutorial without rewriting.

# ── 1. Standalone project boilerplate ───────────────────────────────────────
# The smoke test is consumed by ctest via `cmake -S tests/external_consumer`,
# so it MUST be self-contained — no `add_subdirectory()`-style relationship
# with the parent build is allowed (PRD §3.6: "must work as if it were
# standalone").
project(sturm_external_consumer LANGUAGES CXX)

# ── 2a. Stale-install (shadow) precheck ─────────────────────────────────────
# sturm-nfmu — Detect a STALE STURM install at /usr/local/include/sturm or
# /opt/homebrew/include/sturm BEFORE find_package(sturm) runs. Background:
# AppleClang's default include-search order on macOS places /usr/local/
# include BEFORE -isystem flags, so a months-old `cmake --install` (or a
# Homebrew install of `sturm-transpile`) at /usr/local/include/sturm/
# silently shadows the smoke test's freshly-installed temp prefix. The
# downstream compile failure that results blames missing identifiers
# (`add_mod`, `WHEN`) instead of "a stale install is shadowing your
# freshly-installed prefix", which is actively misleading.
#
# Sentinel-gated: the precheck only fires under
# `-DSTURM_EXTERNAL_CONSUMER_SMOKE_TEST=ON`, which run_smoke.py and
# tests/external_consumer/test_shadow_detection.py both pass. Genuine
# downstream consumers (no sentinel) are NOT blocked — they may
# knowingly accept the shadow path.
#
# Configurable: STURM_SHADOW_INCLUDE_PATHS lets the acceptance test
# (tests/external_consumer/test_shadow_detection.py) point the check at
# a tmp directory instead of polluting real system paths. Default is
# the two macOS Homebrew prefixes from the bd-nfmu incident.
option(STURM_EXTERNAL_CONSUMER_SMOKE_TEST
    "Enable the stale-install shadow precheck (set ON only by the smoke test driver, NOT by genuine downstream consumers)."
    OFF)
set(STURM_SHADOW_INCLUDE_PATHS
    "/usr/local/include/sturm;/opt/homebrew/include/sturm"
    CACHE STRING
    "Semicolon-separated list of paths the shadow-install precheck scans when STURM_EXTERNAL_CONSUMER_SMOKE_TEST=ON. Override (e.g. from a test driver) to point the check at controlled tmp paths.")

if(STURM_EXTERNAL_CONSUMER_SMOKE_TEST)
    foreach(_sturm_shadow_path IN LISTS STURM_SHADOW_INCLUDE_PATHS)
        if(EXISTS "${_sturm_shadow_path}")
            message(FATAL_ERROR
                "STURM stale-install precheck: detected a stale STURM "
                "install at ${_sturm_shadow_path}.\n"
                "AppleClang's default include-search order on macOS "
                "places /usr/local/include BEFORE -isystem flags, so "
                "the smoke test would silently link against the wrong "
                "(shadow) headers — manifesting as obscure compile "
                "errors about missing symbols (add_mod, WHEN, etc.) "
                "rather than a clear environmental diagnostic.\n"
                "Remediation:\n"
                "  * Homebrew install:  brew uninstall sturm-transpile\n"
                "  * Manual install:    sudo rm -rf "
                "${_sturm_shadow_path}\n"
                "Then re-run ctest. (See bd issue sturm-nfmu for the "
                "incident write-up.)")
        endif()
    endforeach()
endif()

# ── 2b. Locate the installed STURM package ──────────────────────────────────
# `find_package(sturm REQUIRED)` resolves via `CMAKE_PREFIX_PATH` (set by
# the test driver to a temp install prefix). It pulls in:
#   * `sturm` / `sturm::sturm` — the header-only INTERFACE target
#     exposing `<sturm/...>` headers (sturmConfig.cmake.in §1).
#   * `sturm-transpile` / `sturm::transpiler` — the standalone transpiler
#     binary IMPORTED at <prefix>/bin/sturm-transpile.
#   * `sturm-transpile-plugin` / `sturm::transpile-plugin` — the Clang
#     plugin shared library used by the default `STURM_TRANSPILE_MODE=plugin`
#     path of `add_quantum_executable`.
#   * `add_quantum_executable(<target> <src>...)` — the public build
#     helper, defined by the installed `SturmTranspile.cmake`
#     (sturmConfig.cmake.in §3 `include(...)`).
#
# A regression in any of those install rules surfaces here as a configure-
# time `find_package` failure or a build-time missing-target error.
find_package(sturm REQUIRED)

# ── 3. Build the routine through `add_quantum_executable` ───────────────────
# The function name is `add_quantum_executable` — installed CMake function
# (commit df0d7df renamed it from the historical `add_sturm_executable`
# spelling). It threads the source through the Clang plugin (default mode
# `plugin`), which runs the transpile pass IN MEMORY and feeds the
# rewritten buffer to the nested EmitObjAction. Per
# `cmake/SturmTranspile.cmake` it also auto-links the `sturm` interface
# target so `<sturm/prelude.hpp>` resolves without a follow-up
# `target_link_libraries(...)` call here.
#
# Side effect: the rewritten TU is mirrored to
# `${CMAKE_BINARY_DIR}/sturm_gen/main.cpp` for diagnostic inspection (the
# plugin's `dump-to=<path>` arg is wired by the helper). We do not assert
# on that file from this project — its contents are an in-tree concern.
add_quantum_executable(external_consumer main.cpp)
