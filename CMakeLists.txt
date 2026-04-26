cmake_minimum_required(VERSION 3.16)
project(STURM VERSION 0.1.2 LANGUAGES CXX C)

# ── C++ standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Install layout (GNU standard-dirs) ────────────────────────────────────────
# GNUInstallDirs provides CMAKE_INSTALL_BINDIR / INCLUDEDIR / LIBDIR with the
# canonical defaults (bin, include, lib) that downstream packagers (Homebrew,
# Debian) expect. We reference these in both install() rules and in the
# sturmConfig.cmake.in template (via @PACKAGE_...@ placeholders).
include(GNUInstallDirs)

# ── Compile-time knobs ────────────────────────────────────────────────────────
# Ancilla pool capacity; override with -DSTURM_ANCILLA_CAPACITY=N
if(NOT DEFINED STURM_ANCILLA_CAPACITY)
    set(STURM_ANCILLA_CAPACITY 256)
endif()
add_compile_definitions(STURM_ANCILLA_CAPACITY=${STURM_ANCILLA_CAPACITY})

# ── sturm-r5pn.3 — Modular pow rewrite gate ───────────────────────────────────
# Plan §2.2 / PRD §3.4: STURM_MODULAR_POW gates the transpiler's
# `pow(a, x) % n  =>  lib_pow_mod_dsl(a, x, n)` rewrite (Phase 5).
# When OFF (the default), the transpiler leaves `pow %` untouched so
# the existing two-step lowering (`lib_pow_dsl` + `lib_mod_dsl`) is
# preserved bit-exactly. When ON, the matcher folds the pair into a
# single `lib_pow_mod_dsl` call without the wide intermediate.
#
# The flag is propagated as a preprocessor define `-DSTURM_MODULAR_POW`
# ONLY to the transpiler driver target (`sturm-transpile`, see
# `transpiler/CMakeLists.txt`). The header-only `sturm` library, the
# transpiler plugin, and every test target compile WITHOUT the define
# regardless of its value — the library primitives ship
# unconditionally; only the transpiler's pattern selection is gated.
# This restriction is pinned by the `cmake_modular_pow_flag` ctest
# (see `tests/cmake_modular_pow_flag.cmake`).
#
# The narrow naming (`STURM_MODULAR_POW`, not `STURM_FAST_MATH`)
# preserves room for future per-region scope if a real use case
# demands it (PRD §7).
option(STURM_MODULAR_POW
    "Enable transpiler rewrite of `pow(a, x) % n` to `lib_pow_mod_dsl`. \
Plan §2.2 / PRD §3.4. OFF preserves the two-step lowering; ON folds the \
pair into a single `lib_pow_mod_dsl` call. The add/mul modular rewrites \
are unconditional (always on); only the pow rewrite is gated."
    OFF)
if(STURM_MODULAR_POW)
    message(STATUS "sturm: STURM_MODULAR_POW=ON — transpiler will "
                   "rewrite `pow(a, x) % n` into `lib_pow_mod_dsl`.")
endif()

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

# ── Install rules (Phase L PL-1) ──────────────────────────────────────────────
# Downstream packagers (Homebrew, prebuilt binaries) and out-of-tree
# consumers via `find_package(sturm REQUIRED)` need a well-defined install
# tree. The layout below is:
#
#   <prefix>/bin/sturm-transpile                     (set up in transpiler/)
#   <prefix>/include/sturm/**/*.hpp                  (public headers)
#   <prefix>/lib/cmake/sturm/SturmTranspile.cmake    (helper incl. add_quantum_executable)
#   <prefix>/lib/cmake/sturm/sturmConfig.cmake       (find_package entry)
#   <prefix>/lib/cmake/sturm/sturmConfigVersion.cmake (semver gate)
#
# 1. Public headers — ship the full include/sturm/ subtree verbatim so
#    `#include <sturm/...>` resolves identically for installed consumers.
install(DIRECTORY include/sturm/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sturm
        FILES_MATCHING
            PATTERN "*.hpp"
            PATTERN "*.h")

# 2. The add_quantum_executable() helper. This is the public entry point
#    exposed to consumers via find_package; the sturmConfig.cmake include()s
#    it from its neighbouring lib/cmake/sturm/ directory.
install(FILES cmake/SturmTranspile.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sturm)

# 3. Package config + version config. configure_package_config_file()
#    expands the @PACKAGE_...@ placeholders relative to the install prefix
#    so the consumer-side config keeps working even if the user relocates
#    the install tree.
include(CMakePackageConfigHelpers)

set(STURM_CMAKE_CONFIG_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/sturm")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sturmConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/sturmConfig.cmake"
    INSTALL_DESTINATION "${STURM_CMAKE_CONFIG_DESTINATION}"
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_BINDIR)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/sturmConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/sturmConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/sturmConfigVersion.cmake"
        DESTINATION "${STURM_CMAKE_CONFIG_DESTINATION}")
