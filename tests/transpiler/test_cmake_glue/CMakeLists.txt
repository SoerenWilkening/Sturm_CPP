# tests/transpiler/test_cmake_glue/CMakeLists.txt — M10 in-tree glue fixture.
#
# Exercises the `add_quantum_executable()` helper from cmake/SturmTranspile.cmake
# in two orthogonal dimensions:
#
#   1. Run-time correctness in both flag modes:
#      - STURM_TRANSPILE=OFF → original source is the compile unit, binary
#        exits 0.
#      - STURM_TRANSPILE=ON  → sturm-transpile rewrites the source to
#        ${CMAKE_BINARY_DIR}/sturm_gen/tests/transpiler/test_cmake_glue/src/fixture.cpp
#        (with the AUTO-GENERATED header prepended), which then becomes the
#        compile unit. The resulting binary also exits 0.
#
#   2. Build-time invariants (ON mode only):
#      - The generated file exists at the documented location.
#      - It starts with the AUTO-GENERATED sentinel.
#      - The original fixture body (the STURM_GLUE_FIXTURE_MARKER token) is
#        still present — proves the transpiler's copy-through is faithful.
#
# The CMake `add_quantum_executable()` helper must be defined at include()
# time by the root CMakeLists.txt, otherwise we would silently fall back to
# an undefined-function error. The PRD says the helper degrades gracefully
# when STURM_TRANSPILE is OFF, so we rely on its presence unconditionally.

cmake_minimum_required(VERSION 3.16)

# ── Build the fixture via the helper under test ─────────────────────────────
# Using the helper is the whole point of this test; do not reach into
# add_executable() directly. A PATH list form is used so we exercise the
# helper's ARGN iteration (a single source is enough to prove the contract
# but the helper's for-each loop should handle both cases).
add_quantum_executable(glue_fixture src/fixture.cpp)

# Smoke-run: the compiled binary must exit 0 in both flag configurations.
# ON mode: the compile unit is the transpiled file; exit 0 proves the
# generated file is syntactically valid and semantically equivalent for
# this trivial input.
add_test(NAME test_cmake_glue_run COMMAND glue_fixture)
set_tests_properties(test_cmake_glue_run PROPERTIES LABELS "transpiler")

# ── Build-time invariants (only meaningful when STURM_TRANSPILE=ON) ─────────
# We register the "generated-file greps" test unconditionally — it must be
# robust against either flag mode. Under OFF the test asserts that no
# sturm_gen file exists for the fixture (the helper must NOT invoke the
# transpiler). Under ON the test verifies the header and marker. Both are
# driven by the same CMake-script probe so there is a single source of truth.
add_test(
    NAME test_cmake_glue_artifacts
    COMMAND "${CMAKE_COMMAND}"
            -DEXPECT_TRANSPILED=$<BOOL:${STURM_TRANSPILE}>
            -DGEN_FILE=${CMAKE_BINARY_DIR}/sturm_gen/tests/transpiler/test_cmake_glue/src/fixture.cpp
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/fixture.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_artifacts.cmake)
set_tests_properties(test_cmake_glue_artifacts PROPERTIES
    LABELS "transpiler")

# Under STURM_TRANSPILE=ON the run-time smoke depends on the transpile
# custom-command firing, which requires sturm-transpile to be built first.
# add_quantum_executable() already records that DEPENDS, but be explicit for
# graph clarity (and to mirror test_transpile_driver's pattern).
if(STURM_TRANSPILE)
    add_dependencies(glue_fixture sturm-transpile)
endif()

# ── Consumer fixture: external-project usability ────────────────────────────
# PRD AC #7 alone does not require this, but the M10 contract in
# docs/implementation_plan_transpiler_uncompute.md says the helper must be
# usable from outside the STURM build. We prove that by configuring a
# stand-alone CMake project against a vendored copy of
# cmake/SturmTranspile.cmake (the file is include()'d directly by the
# consumer's CMakeLists.txt) and running a fresh out-of-source configure
# via cmake --find-package-style script mode. The consumer does NOT
# transpile (STURM_TRANSPILE=OFF by default) — the purpose is to prove
# degradation: the helper parses and adds an executable without reaching
# for sturm-transpile.
add_test(
    NAME test_cmake_glue_external_consumer
    COMMAND "${CMAKE_COMMAND}"
            -DHELPER_FILE=${CMAKE_SOURCE_DIR}/cmake/SturmTranspile.cmake
            -DCONSUMER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/external_consumer
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/_external_consumer
            -DCMAKE_BIN=${CMAKE_COMMAND}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_external_consumer.cmake)
set_tests_properties(test_cmake_glue_external_consumer PROPERTIES
    LABELS "transpiler"
    TIMEOUT 120)
