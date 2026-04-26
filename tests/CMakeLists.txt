cmake_minimum_required(VERSION 3.16)

find_package(Threads REQUIRED)

# Register each test executable in the tests/ directory.
# Add new test source files to this list as steps are implemented.
#
# NOTE: test_invert.cpp is NOT in this list because it has a special
# dependency on a sturm-transpile'd companion fixture (see T-3 block at
# the bottom of this file). Adding it here would skip the transpile step.
set(STURM_TEST_SOURCES
    test_smoke.cpp
    test_qubit_pool.cpp
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

# ── Lib test subdirectory (sturm-lo LO-1d and siblings) ───────────────────────
# DSL-level adjoint roundtrip tests for include/sturm/lib/*_dsl.hpp primitives.
add_subdirectory(lib)

# ── Transpiler-MVP Group C tests (M10+) ───────────────────────────────────────
# Holds consumer-facing tests that exercise the transpiler toolchain from the
# outside (add_quantum_executable, snapshot fixtures, gate-stream equivalence).
# The transpiler binary's own unit tests live under transpiler/tests/ and are
# added via the top-level add_subdirectory(transpiler).
add_subdirectory(transpiler)

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
