cmake_minimum_required(VERSION 3.16)
project(STURM VERSION 0.1.2 LANGUAGES CXX C)

# ── sturm-yial — host clang vs plugin LLVM invariant gate ─────────────────────
#
# The transpiler plugin (`sturm-transpile-plugin`) links the libclang-cpp /
# libLLVM shared libraries from the LLVM install resolved by find_package(LLVM
# CONFIG). The plugin's `FrontendPluginRegistry::Add<>` static registrar
# therefore lands in *that* libclang-cpp's static registry. When the host
# clang (CMAKE_CXX_COMPILER) is a *different* install (e.g. Apple Clang at
# `/usr/bin/c++` while LLVM_DIR points at Homebrew's `/usr/local/opt/llvm@17`),
# Apple Clang's own statically linked FrontendPluginRegistry never sees the
# Add<>. The plugin loads cleanly, but the host treats `getActionType() ==
# ReplaceAction` as if no plugin had registered an action: it falls back to
# the default EmitObjAction against the *unrewritten* source. Every
# transpiler matcher is invisible — qint alias substitution, qram-subscript
# rewrite, etc. all silently no-op. Tests link, run, and emit zero gates.
#
# This trap surfaced as bd-autopilot RED on 2026-05-06 (sturm-7t85 epic-
# boundary gate, test #389 example_qram_demo_run). The gate below catches
# the mismatch at configure time with an actionable diagnostic, before any
# silent runtime regression.
#
# When to fire: only when the user has explicitly pinned LLVM_DIR (or
# Clang_DIR) AND CMAKE_CXX_COMPILER points at a clang outside that LLVM
# install's prefix. The acceptance criteria require:
#
#   1. (mismatch)  -DCMAKE_CXX_COMPILER=/usr/bin/c++  +
#                  -DLLVM_DIR=/usr/local/opt/llvm@17/lib/cmake/llvm
#                  → FATAL_ERROR with actionable hint.
#   2. (match)     -DCMAKE_CXX_COMPILER=/usr/local/opt/llvm@17/bin/clang++ +
#                  matching LLVM_DIR
#                  → succeed.
#   3. (unset)     no LLVM_DIR / no CMAKE_CXX_COMPILER overrides
#                  → gate silent (it gates on mismatch, not on absence).
#
# Implementation:
#   - Resolve `${CMAKE_CXX_COMPILER}` to its real-path install prefix
#     (parent of `bin/`).
#   - Resolve `${LLVM_DIR}` to its install prefix (the LLVMConfig.cmake
#     file lives at `<prefix>/lib/cmake/llvm/LLVMConfig.cmake`, three
#     directories above the prefix).
#   - When both are set, compare the two prefixes case-sensitively
#     after `get_filename_component(... REALPATH)` to canonicalize
#     symlinks (Homebrew's `/usr/local/opt/llvm@17` is itself a symlink
#     into `/usr/local/Cellar/...`).
#   - Mismatch → FATAL_ERROR with a message that names both paths,
#     the FrontendPluginRegistry cause, and the recommended fix.
#
# The pinned ctest is `cmake_host_clang_gate` (tests/cmake_host_clang_gate.cmake).
function(_sturm_check_host_clang_gate)
    if(NOT DEFINED CACHE{LLVM_DIR} AND NOT DEFINED CACHE{Clang_DIR}
       AND NOT LLVM_DIR AND NOT Clang_DIR)
        # Acceptance criterion 3: no LLVM_DIR / no Clang_DIR set →
        # gate stays silent. find_package(LLVM CONFIG) will produce
        # its own diagnostic if it cannot locate LLVM via the system
        # default search path.
        return()
    endif()

    # Pick whichever of LLVM_DIR / Clang_DIR is set (prefer LLVM_DIR as
    # the canonical handle; Clang_DIR mirrors LLVM_DIR in practice on
    # every distro we support, so they share an install prefix).
    set(_llvm_dir "")
    if(LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "LLVM_DIR-NOTFOUND")
        set(_llvm_dir "${LLVM_DIR}")
    elseif(Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "Clang_DIR-NOTFOUND")
        set(_llvm_dir "${Clang_DIR}")
    endif()
    if(NOT _llvm_dir)
        return()
    endif()

    # Canonicalize the LLVM install prefix.
    # Layout: <prefix>/lib/cmake/{llvm,clang}/{LLVM,Clang}Config.cmake.
    # `get_filename_component(... DIRECTORY)` strips one path component
    # at a time; three pops give us <prefix>.
    get_filename_component(_llvm_dir_real "${_llvm_dir}" REALPATH)
    get_filename_component(_p1 "${_llvm_dir_real}" DIRECTORY)
    get_filename_component(_p2 "${_p1}" DIRECTORY)
    get_filename_component(_llvm_prefix "${_p2}" DIRECTORY)

    # Canonicalize the host compiler's install prefix. CMAKE_CXX_COMPILER
    # may be empty (CMake's first pass before language detection); skip
    # the gate in that case — CMake will populate it on the second pass
    # and re-run the configure script.
    if(NOT CMAKE_CXX_COMPILER)
        return()
    endif()
    get_filename_component(_cxx_real "${CMAKE_CXX_COMPILER}" REALPATH)
    get_filename_component(_cxx_bin "${_cxx_real}" DIRECTORY)
    get_filename_component(_cxx_prefix "${_cxx_bin}" DIRECTORY)

    # Match: the two prefixes resolve to the same install. Configure
    # may proceed; the plugin's libclang-cpp will be the same one the
    # host clang statically linked, and the FrontendPluginRegistry
    # registries will coincide.
    if(_cxx_prefix STREQUAL _llvm_prefix)
        return()
    endif()

    # Mismatch. Build the actionable diagnostic. The needles
    # `FrontendPluginRegistry` and `CMAKE_CXX_COMPILER` are pinned by
    # the cmake_host_clang_gate ctest so a future reword cannot drop
    # them silently.
    #
    # The recommended fix: point CMAKE_CXX_COMPILER at the clang++ that
    # ships with the LLVM install resolved via LLVM_DIR. By
    # construction that path is `<llvm_prefix>/bin/clang++`.
    set(_recommended_cxx "${_llvm_prefix}/bin/clang++")
    message(FATAL_ERROR
        "sturm-yial host-clang invariant violated.\n"
        "\n"
        "  CMAKE_CXX_COMPILER = ${CMAKE_CXX_COMPILER}\n"
        "    → resolves to install prefix: ${_cxx_prefix}\n"
        "  LLVM_DIR           = ${_llvm_dir}\n"
        "    → resolves to install prefix: ${_llvm_prefix}\n"
        "\n"
        "The transpiler plugin (sturm-transpile-plugin) links the\n"
        "libclang-cpp / libLLVM shared libraries from the LLVM install\n"
        "above. Its FrontendPluginRegistry::Add<> registers into *that*\n"
        "libclang-cpp's static registry. When the host clang is a\n"
        "different install, the host's own statically linked\n"
        "FrontendPluginRegistry never sees the Add<>; the plugin loads\n"
        "but its ReplaceAction is invisible, the host falls back to the\n"
        "default EmitObjAction against the unrewritten source, and every\n"
        "transpiler rewrite silently no-ops (zero gates emitted).\n"
        "\n"
        "Fix: set CMAKE_CXX_COMPILER to the clang++ from the same\n"
        "LLVM install as LLVM_DIR. Recommended:\n"
        "    -DCMAKE_CXX_COMPILER=${_recommended_cxx}\n"
        "\n"
        "Or, to fall back to the system default compiler, unset both\n"
        "LLVM_DIR and Clang_DIR (the gate triggers only on mismatch,\n"
        "not on the absence of LLVM_DIR).\n"
        "\n"
        "See sturm-yial for the full root-cause trace.")
endfunction()
_sturm_check_host_clang_gate()

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
# sturm-8n73: bumped 256 → 512 to support W=64 modular-arith circuit
# generation (peak ancilla in lib_mul_mod_dsl_oneshot is 7W+7 = 455 at
# W=64; 512 fits with headroom). Higher caps available by override:
# W=128 needs 1024, W=256 needs 2048. Statevector-mode simulator tests
# remain infeasible past W~16 regardless of pool size.
if(NOT DEFINED STURM_ANCILLA_CAPACITY)
    set(STURM_ANCILLA_CAPACITY 512)
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
    "Gate the transpiler rewrite of `pow(a, x) % n` to `lib_pow_mod_dsl` (Plan §8.1 / PRD §3.4); default OFF preserves the two-step `lib_pow_dsl + lib_mod_dsl` lowering, ON folds the pair into a single primitive. The add/mul modular rewrites are unconditional. The library does not check the operand precondition `a, b ∈ [0, n)` — see PRD §5 (trust model)."
    OFF)
if(STURM_MODULAR_POW)
    message(STATUS "sturm: STURM_MODULAR_POW=ON — transpiler will "
                   "rewrite `pow(a, x) % n` into `lib_pow_mod_dsl`.")
endif()

# ── sturm-dp9a — STURM_FULL_TEST_SUITE ctest gating ───────────────────────────
# Gates a small set of *contract* ctests that each spawn a full STURM child
# configure (find_package(LLVM CONFIG), the transpiler subdir, etc.). These
# tests are slow — `cmake_modular_pow_flag` alone records ~114 s for two
# child configures (PROCESSORS 2), and the three `cmake_modular_pow_*` tests
# together sum to ~150-200 s of wall time. They cover one CMake option
# (STURM_MODULAR_POW) and its help-text/docs surface; they do not depend on
# day-to-day source edits, so registering them on every iteration is wasted
# cost for the autopilot loop.
#
# Default OFF: a normal `ctest --test-dir build` skips the three slow tests.
# Set `-DSTURM_FULL_TEST_SUITE=ON` to opt in (CI / pre-release / any time
# you touched `CMakeLists.txt` / `transpiler/CMakeLists.txt` / the README
# build section / `docs/TODO_reversibility_deferrals.md`). The gated tests
# are listed in `tests/CMakeLists.txt` — search for `if(STURM_FULL_TEST_SUITE)`.
option(STURM_FULL_TEST_SUITE
    "Register the slow contract ctests that each spawn a full STURM child configure (cmake_modular_pow_flag, cmake_modular_pow_help_text, cmake_modular_pow_help_text_doc). Default OFF skips them so the iteration loop stays fast; set ON for CI / pre-release / when touching CMakeLists.txt or the STURM_MODULAR_POW docs surface. See tests/CMakeLists.txt (search for `if(STURM_FULL_TEST_SUITE)`)."
    OFF)
if(STURM_FULL_TEST_SUITE)
    message(STATUS "sturm: STURM_FULL_TEST_SUITE=ON — registering slow "
                   "child-configure contract ctests "
                   "(cmake_modular_pow_*).")
endif()

# ── Interface library (header-only) ───────────────────────────────────────────
add_library(sturm INTERFACE)
target_include_directories(sturm INTERFACE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include)
target_compile_features(sturm INTERFACE cxx_std_20)

# ── sturm-mjr2.1 / E4.M1 — versioning macros header ──────────────────────────
# Generates `${CMAKE_BINARY_DIR}/include/sturm/version.hpp` from the template
# in `include/sturm/version.hpp.in`, expanding @PROJECT_VERSION_*@ from the
# top-level `project(... VERSION x.y.z)` declaration above. Single source of
# truth — see PRD §3.8 / Plan §E4.M1. The @ONLY form keeps `${...}` shell
# substitutions in the template untouched (none today, but defensive).
configure_file(
    "${CMAKE_SOURCE_DIR}/include/sturm/version.hpp.in"
    "${CMAKE_BINARY_DIR}/include/sturm/version.hpp"
    @ONLY)

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
#    The recursive install(DIRECTORY ...) walks BOTH the public top-level
#    headers (sturm/qtypes/, sturm/ops/, sturm/control/, sturm/routines/,
#    sturm/uncompute/, sturm/core/, sturm/dispatch/, sturm/backend/,
#    sturm.hpp) AND the internal-public-template-dependency subtree
#    (sturm/detail/) introduced by sturm-nalq.1 / E2.M1. The detail subtree
#    MUST ship because public templates (e.g. `qint_modular.hpp`,
#    `routines/invert.hpp`) transitively `#include "sturm/detail/..."` at
#    consumer compile time; users never name those paths directly but the
#    headers must be on disk for the compile to succeed. See
#    docs/transpiler_emit_targets.md for the classification table.
install(DIRECTORY include/sturm/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sturm
        FILES_MATCHING
            PATTERN "*.hpp"
            PATTERN "*.h")

# 1b. Generated version header (sturm-mjr2.1 / E4.M1). Lives in the build
#     tree because it is produced by configure_file at configure time;
#     ships into ${CMAKE_INSTALL_INCLUDEDIR}/sturm/version.hpp so installed
#     consumers see it under the same `<sturm/...>` prefix.
install(FILES "${CMAKE_BINARY_DIR}/include/sturm/version.hpp"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sturm)

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
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_BINDIR
              CMAKE_INSTALL_LIBDIR)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/sturmConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/sturmConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/sturmConfigVersion.cmake"
        DESTINATION "${STURM_CMAKE_CONFIG_DESTINATION}")

# ── sturm-vr0v.2: sturmc CLI driver ──────────────────────────────────────────
# Install the Python driver as `${prefix}/bin/sturmc` (no `.py` suffix —
# matches the PRD §3.5 contract and how Homebrew packagers expect bin
# entries to look). PROGRAMS sets the executable bit on every platform
# CMake supports. The driver expects to live exactly two levels under
# `prefix` (i.e. `<prefix>/bin/sturmc`) so it can resolve sibling
# artifacts via parent(parent(realpath(__file__))) — see the script
# header. Tests live in `tests/packaging/test_sturmc.py` and gate on a
# `cmake --install` of this very target via a ctest fixture.
install(PROGRAMS tools/sturmc.py
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME sturmc)
