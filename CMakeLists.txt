cmake_minimum_required(VERSION 3.16)
project(STURM CXX)

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

# ── Transpiler build toggle (M4 skeleton) ─────────────────────────────────────
# When ON, configure the Clang LibTooling-based uncomputation transpiler
# under transpiler/. Defaults OFF so a vanilla STURM build never depends on
# LLVM being installed. The full flag contract (including STURM_AUTO_UNCOMPUTE
# and the add_quantum_executable helper) is the subject of M1 and M10.
option(STURM_TRANSPILE "Build the Clang LibTooling-based uncomputation transpiler" OFF)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)

if(STURM_TRANSPILE)
    add_subdirectory(transpiler)
endif()

# ── Examples ──────────────────────────────────────────────────────────────────
add_subdirectory(examples)

# ── Convenience target: `make check` runs ctest with verbose failure output ───
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all STURM tests via ctest"
)
