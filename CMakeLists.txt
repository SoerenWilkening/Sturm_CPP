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

# ── Transpiler-MVP flag contract (M1) ─────────────────────────────────────────
# Two build options gate the transpiler-based uncomputation migration.
# See docs/prd_transpiler_uncompute.md and
#     docs/implementation_plan_transpiler_uncompute.md (§M1).
#
# STURM_AUTO_UNCOMPUTE (default ON)
#   Controls the legacy RAII destructor-driven inverse-gate emission in
#   ~qint_t<W> and related qtypes. ON preserves the pre-transpiler behavior:
#   destructors emit the semantic inverse of the forward op. OFF disables
#   that path so the transpiler's explicit uncompute_* calls can be validated
#   in isolation. The macro STURM_AUTO_UNCOMPUTE=1 is propagated to every
#   translation unit only when the option is ON; downstream headers gate
#   destructor logic via #ifdef STURM_AUTO_UNCOMPUTE.
#
# STURM_TRANSPILE (default OFF during MVP)
#   When ON, enter transpiler/ to build the Clang LibTooling-based
#   sturm-transpile binary and — once M10 lands — route sources declared
#   via add_quantum_executable() through it. OFF is the documented default
#   so a vanilla STURM build never depends on LLVM/Clang being installed.
option(STURM_AUTO_UNCOMPUTE
       "Enable RAII auto-uncompute in ~qint_t / ~qbool destructors" ON)
if(STURM_AUTO_UNCOMPUTE)
    add_compile_definitions(STURM_AUTO_UNCOMPUTE=1)
endif()

option(STURM_TRANSPILE
       "Build the Clang LibTooling-based uncomputation transpiler" OFF)

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
