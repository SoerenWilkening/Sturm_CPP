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

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
