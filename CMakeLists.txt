cmake_minimum_required(VERSION 3.16)
project(STURM CXX C)

# ── C++ standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Compile-time knobs ────────────────────────────────────────────────────────
# Ancilla pool capacity; override with -DSTURM_ANCILLA_CAPACITY=N
if(NOT DEFINED STURM_ANCILLA_CAPACITY)
    set(STURM_ANCILLA_CAPACITY 256)
endif()
add_compile_definitions(STURM_ANCILLA_CAPACITY=${STURM_ANCILLA_CAPACITY})

# ── Interface library (header-only) ───────────────────────────────────────────
add_library(sturm INTERFACE)
target_include_directories(sturm INTERFACE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(sturm INTERFACE cxx_std_20)

# ── Transpiler-MVP flag contract (M1) ─────────────────────────────────────────
# See docs/prd_transpiler_uncompute.md and
#     docs/implementation_plan_transpiler_uncompute.md (§M1).
#
# STURM_TRANSPILE (default OFF during MVP)
#   When ON, enter transpiler/ to build the Clang LibTooling-based
#   sturm-transpile binary and — once M10 lands — route sources declared
#   via add_quantum_executable() through it. OFF is the documented default
#   so a vanilla STURM build never depends on LLVM/Clang being installed.
option(STURM_TRANSPILE
       "Build the Clang LibTooling-based uncomputation transpiler" OFF)

# ── Transpiler glue helper (M10) ──────────────────────────────────────────────
# Define the add_quantum_executable() function for downstream consumers.
# The helper degrades gracefully under STURM_TRANSPILE=OFF: it compiles the
# source directly without reaching for LLVM/Clang. Included unconditionally
# so that downstream code can reference the function symbol regardless of
# the flag state (and so that the `cmake_flags_verify` configure probes
# exercise both branches).
include(${CMAKE_SOURCE_DIR}/cmake/SturmTranspile.cmake)

# The transpiler subdirectory must be entered BEFORE tests/ so that the
# sturm-transpile target is defined by the time tests/transpiler/ calls
# add_quantum_executable() (which under STURM_TRANSPILE=ON wires an
# add_custom_command DEPENDS on sturm-transpile).
if(STURM_TRANSPILE)
    add_subdirectory(transpiler)
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)

# ── Examples ──────────────────────────────────────────────────────────────────
add_subdirectory(examples)

# ── Convenience target: `make check` runs ctest with verbose failure output ───
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all STURM tests via ctest"
)
