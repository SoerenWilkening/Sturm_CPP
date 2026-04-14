# tests/transpiler/CMakeLists.txt — parent for the in-tree transpiler tests
# that live under tests/ (as opposed to transpiler/tests/, which holds the
# transpiler binary's unit tests).
#
# M10 lands the first consumer under test_cmake_glue/. M11 adds the
# snapshot_or_single ctest here; it invokes sturm-transpile on the locked
# PRD-MVP fixture and diffs the generated output against the golden file.
# Future Group C fixtures (M12 gate-stream equivalence) will add their own
# subdirectories here.

cmake_minimum_required(VERSION 3.16)

add_subdirectory(test_cmake_glue)

# ── M11: snapshot fixture (sturm-transpile or_single.cpp) ───────────────────
# The test requires the sturm-transpile binary, which is only built when
# STURM_TRANSPILE=ON. Registering the test behind the same flag keeps OFF
# configurations honest (no dangling target references, no ghost failures
# from a tool that was never built).
if(STURM_TRANSPILE)
    add_test(
        NAME snapshot_or_single
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/or_single.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/or_single.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/or_single
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_or_single PROPERTIES
        LABELS "transpiler"
        # The ctest harness cannot express a target-level dependency via
        # DEPENDS; instead we rely on the fact that building the test
        # subdirectory is a configure-time operation and add_test's COMMAND
        # resolves $<TARGET_FILE:...> at generate time. The user must have
        # run `cmake --build build` before `ctest`, which builds the
        # sturm-transpile binary as part of the default target set.
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()
