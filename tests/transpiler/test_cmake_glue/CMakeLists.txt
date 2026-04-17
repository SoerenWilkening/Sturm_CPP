# tests/transpiler/test_cmake_glue/CMakeLists.txt — M10 in-tree glue fixture.
#
# Exercises the `add_quantum_executable()` helper from cmake/SturmTranspile.cmake:
#
#   1. Run-time correctness: sturm-transpile rewrites the source to
#      ${CMAKE_BINARY_DIR}/sturm_gen/tests/transpiler/test_cmake_glue/src/fixture.cpp
#      (with the AUTO-GENERATED header prepended), which then becomes the
#      compile unit. The resulting binary exits 0.
#
#   2. Build-time invariants:
#      - The generated file exists at the documented location.
#      - It starts with the AUTO-GENERATED sentinel.
#      - The original fixture body (the STURM_GLUE_FIXTURE_MARKER token) is
#        still present — proves the transpiler's copy-through is faithful.
#
# The CMake `add_quantum_executable()` helper must be defined at include()
# time by the root CMakeLists.txt.

cmake_minimum_required(VERSION 3.16)

# ── Build the fixture via the helper under test ─────────────────────────────
# Using the helper is the whole point of this test; do not reach into
# add_executable() directly. A PATH list form is used so we exercise the
# helper's ARGN iteration (a single source is enough to prove the contract
# but the helper's for-each loop should handle both cases).
add_quantum_executable(glue_fixture src/fixture.cpp)

# Smoke-run: the compiled binary must exit 0. The compile unit is the
# transpiled file; exit 0 proves the generated file is syntactically valid
# and semantically equivalent for this trivial input.
add_test(NAME test_cmake_glue_run COMMAND glue_fixture)
set_tests_properties(test_cmake_glue_run PROPERTIES LABELS "transpiler")

# ── Build-time invariants ────────────────────────────────────────────────────
# Verifies that the generated file exists, starts with the AUTO-GENERATED
# sentinel, and preserves the fixture body.
add_test(
    NAME test_cmake_glue_artifacts
    COMMAND "${CMAKE_COMMAND}"
            -DGEN_FILE=${CMAKE_BINARY_DIR}/sturm_gen/tests/transpiler/test_cmake_glue/src/fixture.cpp
            -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/fixture.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_artifacts.cmake)
set_tests_properties(test_cmake_glue_artifacts PROPERTIES
    LABELS "transpiler")

# The run-time smoke depends on the transpile custom-command firing, which
# requires sturm-transpile to be built first. add_quantum_executable()
# already records that DEPENDS, but be explicit for graph clarity.
add_dependencies(glue_fixture sturm-transpile)
