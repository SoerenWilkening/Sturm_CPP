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

# ── Transpiler glue helper (M10) ──────────────────────────────────────────────
# Define the add_quantum_executable() function for downstream consumers.
# Every STURM build routes quantum sources through sturm-transpile; there is
# no OFF mode. Consumers are expected to have LLVM/Clang >= 17 installed
# (Phase L will hide this behind Homebrew/apt/prebuilt binaries).
include(${CMAKE_SOURCE_DIR}/cmake/SturmTranspile.cmake)

# The transpiler subdirectory must be entered BEFORE tests/ so that the
# sturm-transpile target is defined by the time tests/transpiler/ calls
# add_quantum_executable() (which wires an add_custom_command DEPENDS on
# sturm-transpile).
add_subdirectory(transpiler)

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
