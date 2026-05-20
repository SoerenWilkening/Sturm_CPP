cmake_minimum_required(VERSION 3.16)
enable_language(C)

find_package(Threads REQUIRED)

# ── sturm-x4iq: shared OBJECT lib for the backend-runtime source set ─────────
# Every test subdirectory (backend/, qtypes/, control/, packaging/, qram/,
# transpiler/) used to re-list the same seven backend / control / uncompute
# TUs on every `add_executable`. A 2026-05-20 cold-build audit counted 297
# duplicate compile nodes across the seven sources (gate_kind.c at 60, the
# other six between 25 and 54). ccache absorbed the wall-time cost on warm
# builds, but ninja still had to schedule every node and an edit to e.g.
# `gate_kind.c` re-compiled it 60×.
#
# Promoting the lib to `tests/` scope (rather than tests/backend/) is what
# lets every sibling subdirectory pull it in without re-declaring sources,
# build flags, or `orkan_headers` linkage. Mirrors the precedent set by
# `sturm_examples_runtime` in examples/CMakeLists.txt (sturm-grhw) and
# `sturm_transpile_test_core` (sturm-ximr).
#
# `OrkanFetch.cmake` is included here (was previously in tests/backend/) so
# `orkan_headers` is defined before this OBJECT lib's PUBLIC link line, and
# every subdir that consumes it inherits the include path transitively.
include(${CMAKE_SOURCE_DIR}/cmake/OrkanFetch.cmake)

set(STURM_BACKEND_RUNTIME_SOURCES
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
)

# gate_kind.c needs C11 for bool/_Bool. Directory-scoped property — applies
# to every compilation of gate_kind.c in this directory (i.e. inside
# sturm_backend_runtime). Sibling subdirs that compile gate_kind.c
# independently still need their own set_source_files_properties() call.
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)

add_library(sturm_backend_runtime OBJECT ${STURM_BACKEND_RUNTIME_SOURCES})
target_include_directories(sturm_backend_runtime PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(sturm_backend_runtime PUBLIC cxx_std_20)
target_compile_definitions(sturm_backend_runtime PUBLIC
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(sturm_backend_runtime PUBLIC orkan_headers)

# Register each test executable in the tests/ directory.
# Add new test source files to this list as steps are implemented.
#
# NOTE: test_invert.cpp is NOT in this list because it has a special
# dependency on a sturm-transpile'd companion fixture (see T-3 block at
# the bottom of this file). Adding it here would skip the transpile step.
set(STURM_TEST_SOURCES
    test_smoke.cpp
    test_sink_counter.cpp
    test_sink_recording.cpp
    test_qbool.cpp
    test_mask_ops.cpp
    test_dispatch.cpp
    test_qint_classical.cpp
    test_qint_superposed.cpp
    test_resource_lifecycle.cpp
    test_when.cpp
    test_sink_dispatch.cpp
    test_conversions.cpp
    test_phase_amp.cpp
    test_width.cpp

    test_qint_owning.cpp
    test_qbool_unified.cpp
    test_qbool_acceptance.cpp
)

foreach(src ${STURM_TEST_SOURCES})
    get_filename_component(test_name ${src} NAME_WE)
    add_executable(${test_name} ${src})
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(${test_name} PRIVATE cxx_std_20)
    target_link_libraries(${test_name} PRIVATE Threads::Threads)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# ── Phase I PI-0 + Phase T T-3 (sturm-xrob.4): test_invert ─────────────────
#
# test_invert.cpp exercises the compile-time `sturm::invert<&fn>()` surface. The
# original Phase I PI-0 tests (sturm-hi66) hand-register their adjoints
# via `STURM_REGISTER_ADJOINT`. Phase T T-3 adds four end-to-end
# roundtrip tests whose forwards are declared with
# `[[clang::annotate("sturm::reversible")]]` and rely on the
# transpiler's T-1 wiring (sturm-xrob.2) to auto-synthesise
# `__<fn>_adj` stubs + `STURM_REGISTER_ADJOINT` lines. The forwards
# live in `tests/transpiler/fixtures/reversible_invert_roundtrip.cpp`
# (a T-3-specific fixture) and the generated post-transpile file is
# text-included into test_invert.cpp BEFORE `main()` so the auto-
# synthesised template specialisations are visible at the
# `sturm::invert<&ns::fwd>()` call sites in main.
#
# C++ instantiation-order rule
# ----------------------------
# An explicit specialisation of `sturm::_detail::adjoint_of<T>` must be
# declared before the first implicit instantiation point of that
# specialisation. The transpiler currently emits the
# `STURM_REGISTER_ADJOINT` line at END-OF-FILE of the TU carrying the
# forward (see `matcher_reversible_drive.cpp`'s `end_of_file_loc`). If
# we routed test_invert.cpp itself through sturm-transpile, the
# registration would land AFTER `int main() { ... }` — AFTER the
# `sturm::invert<&fwd>()` call sites. Text-including a SEPARATELY-
# transpiled fixture keeps the emission inside a scope that precedes
# main, satisfying the rule without modifying the transpiler's EOF
# anchor (out of scope for T-3).
#
# Custom-command flow
# -------------------
# Input fixture:
#   tests/transpiler/fixtures/reversible_invert_roundtrip.cpp
# Transpile step (via the standalone sturm-transpile binary):
#   ${CMAKE_BINARY_DIR}/sturm_gen/tests/transpiler/fixtures/reversible_invert_roundtrip.cpp
# The generated file has `#include <cstddef>` carried over from the
# fixture, the original namespace + forward definitions, sibling
# `__<fn>_adj` stub bodies emitted by R-A, and end-of-file
# `STURM_REGISTER_ADJOINT` lines emitted by T-1's driver.
#
# test_invert.cpp consumes the generated file via
#   #include "reversible_invert_roundtrip.cpp"
# where the include search path is augmented to the sturm_gen
# fixture directory via `target_include_directories`. The fixture
# file is NOT added to test_invert's source list — doing so would
# compile it as a second TU and trigger multiple-definition link
# errors for the forward routines.
set(_t3_fixture_src
    "${CMAKE_SOURCE_DIR}/tests/transpiler/fixtures/reversible_invert_roundtrip.cpp")
file(RELATIVE_PATH _t3_fixture_rel_src
    "${CMAKE_SOURCE_DIR}" "${_t3_fixture_src}")
set(_t3_fixture_generated
    "${CMAKE_BINARY_DIR}/sturm_gen/${_t3_fixture_rel_src}")
get_filename_component(_t3_fixture_gen_dir
    "${_t3_fixture_generated}" DIRECTORY)

add_custom_command(
    OUTPUT "${_t3_fixture_generated}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${_t3_fixture_gen_dir}"
    COMMAND $<TARGET_FILE:sturm-transpile>
            "${_t3_fixture_rel_src}"
            --output-dir "${CMAKE_BINARY_DIR}/sturm_gen"
            "--extra-arg=-std=c++20"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${_t3_fixture_src}" sturm-transpile
    COMMENT "sturm-transpile ${_t3_fixture_rel_src} (T-3)"
    VERBATIM)
set_source_files_properties("${_t3_fixture_generated}" PROPERTIES
    GENERATED TRUE)

# Custom target so test_invert builds trigger the transpile step.
# The generated file is not a SOURCE of test_invert — we text-include
# it via `#include "reversible_invert_roundtrip.cpp"` instead. CMake
# honours file-level dependencies on non-source inputs through
# `add_custom_target` + `add_dependencies`, which is the established
# pattern for this kind of pre-build asset generation.
add_custom_target(test_invert_t3_fixture
    DEPENDS "${_t3_fixture_generated}")

add_executable(test_invert test_invert.cpp)
target_include_directories(test_invert PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    # Make `#include "reversible_invert_roundtrip.cpp"` resolve to the
    # generated file. The search order puts CMAKE_BINARY_DIR first so
    # the text-include grabs the transpiled buffer (carrying the
    # `__<fn>_adj` + `STURM_REGISTER_ADJOINT` emission) and not the
    # original source fixture.
    ${_t3_fixture_gen_dir})
target_compile_features(test_invert PRIVATE cxx_std_20)
target_link_libraries(test_invert PRIVATE Threads::Threads)
add_dependencies(test_invert test_invert_t3_fixture)
add_test(NAME test_invert COMMAND test_invert)

# ── Backend test subdirectory (M1 and later) ──────────────────────────────────
add_subdirectory(backend)

# ── Qtypes test subdirectory (sturm-lo LO-1e and later) ───────────────────────
add_subdirectory(qtypes)

# ── Qram test subdirectory (sturm-u9ge / QRAM-subscript epic) ────────────────
# Holds runtime-stub tests for `sturm::QRAM_read` (sturm-u9ge.13 / Beat D1)
# and any sibling QRAM primitives. The directory is added before the
# transpiler subdirectory so the runtime stub is observable to upstream
# beats (D2 emitter / E1 OOS diagnostics) without a configure round-trip.
add_subdirectory(qram)

# ── Lib test subdirectory (sturm-lo LO-1d and siblings) ───────────────────────
# DSL-level adjoint roundtrip tests for include/sturm/lib/*_dsl.hpp primitives.
# sturm-jk3g: tests/lib/ holds DSL-level QC simulation tests (state-vector
# simulators, 25-qubit witnesses, multi-minute runs). Gate the entire
# subdirectory behind STURM_FULL_TEST_SUITE so the default ctest run stays fast.
if(STURM_FULL_TEST_SUITE)
    add_subdirectory(lib)
endif()

# ── Control test subdirectory (sturm-a3t4 depth-1 invariant) ─────────────────
# Tests that pin properties of the depth-1 control stack invariant introduced
# by the sturm-a3t4 epic.  Initially holds the P0 baseline gate-count capture
# (sturm-a3t4.1).
add_subdirectory(control)

# ── Transpiler-MVP Group C tests (M10+) ───────────────────────────────────────
# Holds consumer-facing tests that exercise the transpiler toolchain from the
# outside (add_quantum_executable, snapshot fixtures, gate-stream equivalence).
# The transpiler binary's own unit tests live under transpiler/tests/ and are
# added via the top-level add_subdirectory(transpiler).
add_subdirectory(transpiler)

# ── Regressions test subdirectory (sturm-65rs.12 / sturm-qac.12 / D1) ────────
# Drift-gate regression tests that pin invariants other beats rely on. The
# first inhabitant pins D0's re-spelling of callsites that need the backend
# `sturm::qint_t<64>` surface (super_mask, qubits, phi(), theta()) so a
# future refactor cannot silently drop them back to bare `sturm::qint`
# (which post-B1 resolves to the frontend alias).
add_subdirectory(regressions)

# ── sturm-f8ib / Frontend simpl. P9 — PRD A1–A7 acceptance roll-up ───────────
# Wires PRD §8 acceptance criteria as named ctests; aggregator script
# lives under `tests/regressions/` (heavier driver logic in sibling
# `tests/regressions/acceptance/`). Must be loaded AFTER the regression
# subdirectory so the delegated F-2a / F-8 ctests are registered first.
include(regressions/test_acceptance_gates.cmake)

# ── sturm-vr0v.2: packaging acceptance tests (sturmc CLI driver) ─────────────
# Lives in `tests/packaging/`; the suite installs into a temp prefix and
# exercises the installed sturmc binary end-to-end. Gated on the
# `sturmc_artifacts_built` ctest fixture so a bare `ctest -R sturmc`
# from a freshly configured tree still works without a manual
# `cmake --build` first.
add_subdirectory(packaging)

# ── sturm-r5pn.3 — STURM_MODULAR_POW CMake option propagation test ───────────
#
# Drives the Phase 0.3 plan §2.2 contract for the STURM_MODULAR_POW
# CMake option:
#   - exists (BOOL, default OFF) at top-level configure;
#   - propagates as `-DSTURM_MODULAR_POW` to the transpiler driver
#     target (sturm-transpile) when ON;
#   - does NOT propagate to the header-only `sturm` library, the
#     transpiler plugin, or any tests/** target;
#   - both ON and OFF configure cleanly.
#
# Implementation: the script (`cmake_modular_pow_flag.cmake`) runs two
# out-of-tree configures of the STURM source tree (one per flag value)
# with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, then parses each
# compile_commands.json and verifies the propagation footprint.
# Configure-only (no compilation), so the test stays fast.
#
# Pass-through args: the parent build's resolved LLVM_DIR / Clang_DIR
# / CMAKE_*_COMPILER are forwarded so the child configure picks up the
# same toolchain (find_package(LLVM CONFIG) is distro-specific without
# LLVM_DIR — see transpiler/CMakeLists.txt:34-44).
#
# sturm-dp9a: gated behind `STURM_FULL_TEST_SUITE` (default OFF). The two
# child configures cost ~114 s wall time, which is wasted on the
# autopilot loop's per-iteration ctest. Re-enable with
# `-DSTURM_FULL_TEST_SUITE=ON` for CI / pre-release / any change that
# touches the top-level CMake plumbing or transpiler driver.
if(STURM_FULL_TEST_SUITE)
    add_test(NAME cmake_modular_pow_flag
        COMMAND "${CMAKE_COMMAND}"
            "-DSTURM_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DSCRATCH_ROOT=${CMAKE_BINARY_DIR}/cmake_modular_pow_flag"
            "-DPARENT_LLVM_DIR=${LLVM_DIR}"
            "-DPARENT_CLANG_DIR=${Clang_DIR}"
            "-DPARENT_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DPARENT_C_COMPILER=${CMAKE_C_COMPILER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modular_pow_flag.cmake"
    )
    set_tests_properties(cmake_modular_pow_flag PROPERTIES
        LABELS "cmake;modular-arith"
        # The two child configures run sequentially within the script and
        # are themselves single-threaded; reserve the whole CTest job slot
        # for the test so we do not blow past the project's `--parallel 6`
        # cap when CMake spawns its own internal helpers.
        PROCESSORS 2)
endif()

# ── sturm-vr0v.1 — add_quantum_executable command-line unit test ─────────────
#
# Drives the E5.M1 contract that `add_quantum_executable` resolves the
# transpiler binary via `$<TARGET_FILE:sturm::transpiler>` (the namespaced
# exported target wired up by both `transpiler/CMakeLists.txt` ALIAS and
# `cmake/sturmConfig.cmake.in` IMPORTED) so it works identically against
# in-tree builds and `find_package(sturm)` consumers (PRD §3.5).
#
# The script (`tests/cmake/test_add_quantum_executable_command_line.cmake`)
# spins up a mini CMake project that registers a MOCKED `sturm::transpiler`
# IMPORTED executable, includes `cmake/SturmTranspile.cmake`, calls
# `add_quantum_executable(...)` in dump mode, and inspects the generated
# Makefile / build script for the resolved transpile recipe. Configure-only
# (no compilation), so the test stays fast.
add_test(NAME add_quantum_executable_command_line
    COMMAND "${CMAKE_COMMAND}"
        "-DSTURM_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DSCRATCH_ROOT=${CMAKE_BINARY_DIR}/add_quantum_executable_command_line"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_add_quantum_executable_command_line.cmake"
)
set_tests_properties(add_quantum_executable_command_line PROPERTIES
    LABELS "cmake;packaging-export"
    # The child configure is single-threaded; reserve one slot.
    PROCESSORS 1)

# ── sturm-bjt2.1 — STURM_MODULAR_POW CMake help text + README docs ───────────
#
# Drives the Phase 6 plan §8.1 / PRD §3.4 contract for the user-facing
# documentation of the STURM_MODULAR_POW option:
#   - the option's CMake help string (visible via `cmake -LH`) names
#     the gated rewrite (pow / lib_pow_mod_dsl) and the default (OFF);
#   - the top-level README.md `Build` section documents the flag with
#     the same three needles.
#
# Implementation: the script (`cmake_modular_pow_help_text.cmake`)
# runs one out-of-tree configure of the STURM source tree, queries
# `cmake -LH -N` for the option block, and scans README.md. No
# compilation runs (configure-only), so the test stays fast.
#
# sturm-dp9a: gated behind `STURM_FULL_TEST_SUITE` (default OFF). The
# child configure spawns find_package(LLVM CONFIG) and the full
# transpiler subdir; running it on every autopilot iteration is wasted
# wall time. Re-enable with `-DSTURM_FULL_TEST_SUITE=ON` for CI /
# pre-release / any change that touches the README build section or the
# STURM_MODULAR_POW help string.
if(STURM_FULL_TEST_SUITE)
    add_test(NAME cmake_modular_pow_help_text
        COMMAND "${CMAKE_COMMAND}"
            "-DSTURM_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DSCRATCH_ROOT=${CMAKE_BINARY_DIR}/cmake_modular_pow_help_text"
            "-DPARENT_LLVM_DIR=${LLVM_DIR}"
            "-DPARENT_CLANG_DIR=${Clang_DIR}"
            "-DPARENT_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DPARENT_C_COMPILER=${CMAKE_C_COMPILER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modular_pow_help_text.cmake"
    )
    set_tests_properties(cmake_modular_pow_help_text PROPERTIES
        LABELS "cmake;modular-arith;docs"
        # Same reasoning as cmake_modular_pow_flag: the child configure
        # spawns its own helpers; reserve a job slot to stay under the
        # project's `--parallel 6` cap.
        PROCESSORS 2)
endif()

# ── sturm-6ov3.2 — Doxygen on each free function in qint_modular.hpp ─────
#
# Drives the Phase 7 plan §9.1 #2 / PRD §5 contract for the public
# modular-arithmetic free functions:
#   - each of `add_mod`, `mul_mod`, `pow_mod` in
#     `include/sturm/ops/qint_modular.hpp` must carry a Doxygen comment
#     block that names (a) the `@pre` clause stating the operand
#     precondition, (b) the behavior under violation (undefined), and
#     (c) a citation pointing at PRD §5 (the trust-model section).
#
# Implementation: the script (`check_doxygen_qint_modular.cmake`) reads
# the header and walks each function declaration with CMake regex/string
# ops. Pure file scan — no compilation, no child cmake invocation —
# so the test runs in <100ms and needs no PROCESSORS reservation.
add_test(NAME check_doxygen_qint_modular
    COMMAND "${CMAKE_COMMAND}"
        "-DSTURM_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_doxygen_qint_modular.cmake"
)
set_tests_properties(check_doxygen_qint_modular PROPERTIES
    LABELS "cmake;modular-arith;docs")

# ── sturm-6ov3.3 — Doxygen on each lib_*_mod_dsl primitive header ─────────
#
# Drives the Phase 7 plan §9.1 #3 / PRD §5 contract for the modular-
# arithmetic lib-level primitives:
#   - each of `lib_add_mod_dsl`, `__lib_add_mod_dsl_adj`,
#     `lib_mul_mod_dsl`, `__lib_mul_mod_dsl_adj`, `lib_pow_mod_dsl`, and
#     `__lib_pow_mod_dsl_adj` in `include/sturm/lib/*_mod_dsl*.hpp` must
#     carry a Doxygen comment block that names (a) the `@pre` clause
#     stating the operand precondition, (b) the behavior under violation
#     (undefined), and (c) a citation pointing at PRD §5 (trust model).
#
# Implementation: the script (`check_doxygen_lib_mod_dsl.cmake`) reads
# each header and walks the function declarations with CMake regex/string
# ops. Pure file scan — no compilation, no child cmake invocation —
# so the test runs in <100ms and needs no PROCESSORS reservation.
add_test(NAME check_doxygen_lib_mod_dsl
    COMMAND "${CMAKE_COMMAND}"
        "-DSTURM_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_doxygen_lib_mod_dsl.cmake"
)
set_tests_properties(check_doxygen_lib_mod_dsl PROPERTIES
    LABELS "cmake;modular-arith;docs")

# ── sturm-6ov3.4 — STURM_MODULAR_POW help text + reversibility cross-check ─
#
# Drives the Phase 7 plan §9.1 #4 + #6 contracts that layer on top of the
# Phase-6 `cmake_modular_pow_help_text` test:
#   - the option's CMake help string must point a `cmake -LH` reader at
#     the trust-model section (PRD §5) that defines the operand
#     precondition `a, b ∈ [0, n)`;
#   - `docs/TODO_reversibility_deferrals.md` must explicitly record the
#     PRD §7 cross-check (i.e. that none of the modular-arith deferrals
#     introduce a reversibility deferral) so a future reader cannot
#     mistake silence for absence of review.
#
# Implementation: the script (`cmake_modular_pow_help_text_doc.cmake`)
# runs one out-of-tree configure of the STURM source tree, queries
# `cmake -LH -N` for the option block, and scans the deferrals doc.
# No compilation runs (configure-only).
#
# sturm-dp9a: gated behind `STURM_FULL_TEST_SUITE` (default OFF). The
# child configure spawns find_package(LLVM CONFIG) and the full
# transpiler subdir; running it on every autopilot iteration is wasted
# wall time. Re-enable with `-DSTURM_FULL_TEST_SUITE=ON` for CI /
# pre-release / any change that touches the STURM_MODULAR_POW help
# string or `docs/TODO_reversibility_deferrals.md`.
if(STURM_FULL_TEST_SUITE)
    add_test(NAME cmake_modular_pow_help_text_doc
        COMMAND "${CMAKE_COMMAND}"
            "-DSTURM_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DSCRATCH_ROOT=${CMAKE_BINARY_DIR}/cmake_modular_pow_help_text_doc"
            "-DPARENT_LLVM_DIR=${LLVM_DIR}"
            "-DPARENT_CLANG_DIR=${Clang_DIR}"
            "-DPARENT_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DPARENT_C_COMPILER=${CMAKE_C_COMPILER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modular_pow_help_text_doc.cmake"
    )
    set_tests_properties(cmake_modular_pow_help_text_doc PROPERTIES
        LABELS "cmake;modular-arith;docs"
        # Same reasoning as cmake_modular_pow_flag / *_help_text: the child
        # configure spawns its own helpers; reserve a job slot to stay under
        # the project's `--parallel 6` cap.
        PROCESSORS 2)
endif()

# ── sturm-yial — host-clang vs plugin-LLVM mismatch gate ────────────────────
#
# Drives the sturm-yial contract for the configure-time gate added in the
# top-level `CMakeLists.txt`:
#   - When `CMAKE_CXX_COMPILER` and `LLVM_DIR` are both supplied AND
#     resolve to *different* install prefixes, the configure FATAL_ERRORs
#     with an actionable diagnostic naming both paths, the
#     FrontendPluginRegistry cause, and the recommended fix.
#   - When the two share a prefix, configure succeeds.
#   - When neither is supplied, the gate stays silent (the gate is on the
#     mismatch, not on absence of LLVM_DIR).
#
# Background: on 2026-05-06 the bd-autopilot epic-boundary gate hit RED
# on `example_qram_demo_run` because `build/CMakeCache.txt` had Apple
# Clang as `CMAKE_CXX_COMPILER` while the plugin .dylib linked Homebrew
# LLVM. The plugin loaded silently but its FrontendPluginRegistry
# Add<>'d ReplaceAction landed in a registry the host clang never
# consulted — every transpiler rewrite was invisible, every generated
# circuit empty. See sturm-yial for the full root-cause trace.
#
# Implementation: the script (`cmake_host_clang_gate.cmake`) runs three
# out-of-tree configures (mismatch / match / unset) and asserts the
# gate's behavior in each case. Configure-only — no compilation — so
# the test stays fast. Auto-detects a "different" host clang for the
# MISMATCH case (Apple Clang at `/usr/bin/c++` on macOS); SKIPs the
# MISMATCH case on platforms where no second clang is available.
#
# sturm-dp9a parallel: gated behind `STURM_FULL_TEST_SUITE` (default
# OFF). The three child configures cost ~3-5 minutes wall time on the
# autopilot loop, which is wasted on per-iteration ctests. Re-enable
# with `-DSTURM_FULL_TEST_SUITE=ON` for CI / pre-release / any change
# that touches the top-level CMake gate or the host-clang invariant.
if(STURM_FULL_TEST_SUITE)
    # Auto-detect a "different" clang for the MISMATCH case. On macOS,
    # Apple Clang lives at `/usr/bin/c++` and is the canonical mismatch
    # against Homebrew's `/usr/local/opt/llvm@17/bin/clang++` or
    # `/opt/homebrew/opt/llvm@17/bin/clang++`. On Linux, picking
    # `/usr/bin/c++` (g++) would not exercise the gate (the gate is
    # specific to clang/LLVM mismatches), so we leave MISMATCH_CXX
    # empty there and the script SKIPs the MISMATCH case.
    set(STURM_HOST_CLANG_GATE_MISMATCH_CXX "")
    if(APPLE AND EXISTS "/usr/bin/c++")
        # Only useful if `/usr/bin/c++` is NOT the parent build's
        # compiler (otherwise it would not be a mismatch).
        get_filename_component(_parent_cxx_real "${CMAKE_CXX_COMPILER}" REALPATH)
        get_filename_component(_apple_cxx_real "/usr/bin/c++" REALPATH)
        if(NOT _parent_cxx_real STREQUAL _apple_cxx_real)
            set(STURM_HOST_CLANG_GATE_MISMATCH_CXX "/usr/bin/c++")
        endif()
    endif()

    add_test(NAME cmake_host_clang_gate
        COMMAND "${CMAKE_COMMAND}"
            "-DSTURM_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DSCRATCH_ROOT=${CMAKE_BINARY_DIR}/cmake_host_clang_gate"
            "-DPARENT_LLVM_DIR=${LLVM_DIR}"
            "-DPARENT_CLANG_DIR=${Clang_DIR}"
            "-DPARENT_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DPARENT_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DMISMATCH_CXX=${STURM_HOST_CLANG_GATE_MISMATCH_CXX}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_host_clang_gate.cmake"
    )
    set_tests_properties(cmake_host_clang_gate PROPERTIES
        LABELS "cmake;host-clang-gate"
        # The script runs up to three child configures sequentially, each
        # spawning its own internal helpers. Reserve 2 slots to stay under
        # the project's `--parallel 6` cap.
        PROCESSORS 2)
endif()

# ── sturm-mixe — STURM_MODE build flag (Frontend simpl. P1 / PRD §5.3) ─────
#
# Drives the contract for the new `STURM_MODE` cache STRING introduced
# by `cmake/SturmFrontend.cmake`:
#
#   - Invalid value → FATAL_ERROR with `STURM_MODE` and the offending
#     token in the diagnostic (`cmake_sturm_mode_flag`).
#   - Valid value (APPEND / COUNT / SIMULATE) → `STURM_MODE_DEFAULT`
#     compile def lands on `sturm::frontend`; a probe TU's static_assert
#     compiles cleanly (`cmake_sturm_mode_flag`).
#   - Toggling the flag in the same build dir forces a recompile of
#     every TU linked to `sturm::frontend` (OQ5 / `cmake_sturm_mode_rebuild`).
#
# Both tests use a tiny scratch project that includes the parent's
# `cmake/SturmFrontend.cmake` directly (so they exercise the real source
# file). The flag test is configure-only for the invalid-mode case and
# configure+build for the valid-mode cases (the static_assert fires at
# compile time). The rebuild test must build twice in the same dir to
# observe the mtime delta on the probe object file. Neither test
# touches the full STURM source tree, so they stay fast (~10–30 s).
#
# Gated behind `STURM_FULL_TEST_SUITE` (impl plan §3 P1): each test
# spawns at least two out-of-tree configure+build cycles via tiny
# scratch CMake projects. Default OFF keeps the autopilot ctest loop
# fast; set `-DSTURM_FULL_TEST_SUITE=ON` for CI / pre-release / any
# change to `cmake/SturmFrontend.cmake`, the `STURM_MODE` cache
# variable, or the `sturm::frontend` INTERFACE library.
if(STURM_FULL_TEST_SUITE)
    add_test(NAME cmake_sturm_mode_flag
        COMMAND "${CMAKE_COMMAND}"
            "-DSTURM_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DSCRATCH_ROOT=${CMAKE_BINARY_DIR}/cmake_sturm_mode_flag"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_sturm_mode_flag.cmake"
    )
    set_tests_properties(cmake_sturm_mode_flag PROPERTIES
        LABELS "cmake;frontend-simpl"
        # The script runs four child configures + three builds sequentially.
        # Reserve 2 slots to stay under the project's `--parallel 6` cap.
        PROCESSORS 2)

    add_test(NAME cmake_sturm_mode_rebuild
        COMMAND "${CMAKE_COMMAND}"
            "-DSTURM_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            "-DSCRATCH_ROOT=${CMAKE_BINARY_DIR}/cmake_sturm_mode_rebuild"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_sturm_mode_rebuild.cmake"
    )
    set_tests_properties(cmake_sturm_mode_rebuild PROPERTIES
        LABELS "cmake;frontend-simpl"
        # Two configures + two builds + 2 s sleeps for mtime resolution.
        PROCESSORS 2)
endif()

# ── sturm-2vbr — PCH PIE-state match between driver and plugin ────────────
#
# Drives the sturm-2vbr contract that the `sturm-transpile` driver and
# `sturm-transpile-plugin` SHARED library compile with matching
# Position-Independent-Code state. Without that match, the plugin's
# `target_precompile_headers(... REUSE_FROM sturm-transpile)` fails on
# Linux clang++-17 with:
#
#     error: is pie differs in PCH file vs. current file
#
# because the driver (executable) defaults to `-fpie` (`is pie = true`)
# while the plugin (SHARED) is forced to `-fPIC` (`is pie = false`).
# The fix in `transpiler/CMakeLists.txt` pins
# `POSITION_INDEPENDENT_CODE ON` on the driver target so the PCH the
# driver writes is `is pie = false` (matching the plugin). On macOS the
# fix is a no-op (Mach-O is always PIC); the test is platform-agnostic
# because it asserts on the per-target CXX_FLAGS line, not on the
# behavior at compile time.
#
# Implementation: the script (`cmake_pch_pie_match.cmake`) runs one
# out-of-tree configure (forcing the Unix Makefiles generator so the
# per-target `flags.make` location is well-defined), then reads the
# CXX_FLAGS lines for both targets and compares their `-fPIC` state.
# Configure-only — no compilation — so the test stays fast.
#
# Not gated behind STURM_FULL_TEST_SUITE: the configure is fast (~30 s)
# AND the contract is the kind of CMake-infrastructure breakage that
# only surfaces on the platforms (Linux executables) where the
# autopilot's default macOS workstation cannot reproduce by inspection.
# Running it on every iteration buys cheap insurance against the kind
# of silent regression that closed sturm-2vbr.
add_test(NAME cmake_pch_pie_match
    COMMAND "${CMAKE_COMMAND}"
        "-DSTURM_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DSCRATCH_ROOT=${CMAKE_BINARY_DIR}/cmake_pch_pie_match"
        "-DPARENT_LLVM_DIR=${LLVM_DIR}"
        "-DPARENT_CLANG_DIR=${Clang_DIR}"
        "-DPARENT_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DPARENT_C_COMPILER=${CMAKE_C_COMPILER}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_pch_pie_match.cmake"
)
set_tests_properties(cmake_pch_pie_match PROPERTIES
    LABELS "cmake;pch-pie-match"
    # The child configure spawns find_package(LLVM CONFIG) and the full
    # transpiler subdirectory; reserve 2 slots to stay under the
    # project's `--parallel 6` cap.
    PROCESSORS 2)

# ── sturm-bs8s — transpiler tests REUSE_FROM the sturm-transpile PCH ──────
#
# Pins the sturm-bs8s contract that transpiler test targets opt into the
# sturm-transpile precompiled header via the `sturm_use_transpile_pch()`
# helper (cmake/SturmTranspilePch.cmake). Without that opt-in the test
# targets re-parse the ~20-header clang/llvm umbrella every TU, costing
# ~7.7 s per heavy TU and ~3 minutes wall on a clean rebuild at
# --parallel 6.
#
# The script asserts three contract clauses on a representative heavy
# test target (`test_matcher_qint_alias_subst`):
#   (1) The helper file `cmake/SturmTranspilePch.cmake` exists and
#       defines `sturm_use_transpile_pch`.
#   (2) The target's per-target CXX_FLAGS contain `-fPIC` (PIE-state
#       match with the PCH producer per sturm-2vbr).
#   (3) The target's `flags.make` contains a per-source-file PCH
#       option line referencing `sturm-transpile.dir/cmake_pch.hxx` —
#       i.e. REUSE_FROM is wired to the correct producer.
#
# Configure-only (no compilation), runs in ~30 s — same posture as the
# sibling `cmake_pch_pie_match` test above.
add_test(NAME cmake_pch_test_reuse
    COMMAND "${CMAKE_COMMAND}"
        "-DSTURM_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DSCRATCH_ROOT=${CMAKE_BINARY_DIR}/cmake_pch_test_reuse"
        "-DPARENT_LLVM_DIR=${LLVM_DIR}"
        "-DPARENT_CLANG_DIR=${Clang_DIR}"
        "-DPARENT_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DPARENT_C_COMPILER=${CMAKE_C_COMPILER}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_pch_test_reuse.cmake"
)
set_tests_properties(cmake_pch_test_reuse PROPERTIES
    LABELS "cmake;pch-test-reuse;transpiler"
    PROCESSORS 2)
