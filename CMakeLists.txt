cmake_minimum_required(VERSION 3.16)
project(STURM VERSION 0.1.2 LANGUAGES CXX C)

# ── sturm-q2eb — ccache compiler launcher ────────────────────────────────────
# Wire ccache (when found on PATH) as the CXX/C compiler launcher. ccache
# transparently hashes (compile-flags, preprocessed source, compiler version)
# and serves the cached .o on a hit. On the same agent's iterative build loop
# this drops a full clean rebuild from ~30 min to a few minutes on warm cache,
# and a no-source-change reconfigure to seconds.
#
# Defaults
#   STURM_USE_CCACHE=ON (default): use ccache iff `find_program(ccache)`
#                                  succeeds. Missing ccache is silent — the
#                                  build falls back to plain compiler invokes.
#   STURM_USE_CCACHE=OFF        : skip the wiring entirely.
#
# PCH sloppiness — sturm-bs8s/cxrn rely on `target_precompile_headers` for the
# transpiler driver + plugin + tests. clang embeds compile-time defines and
# `__DATE__/__TIME__` macros into the PCH; without explicit sloppiness ccache
# would invalidate the PCH cache on every rebuild and the PCH win evaporates.
# The setup script (tools/setup_dev_env.sh) writes the matching keys into
# ~/.config/ccache/ccache.conf so the same setting is honored on agent
# container resets without depending on per-shell env vars.
option(STURM_USE_CCACHE
    "Wire ccache as the CXX/C compiler launcher when present (default ON). Set OFF to force unwrapped compiler invokes (e.g. when measuring uncached baseline wall time)."
    ON)
if(STURM_USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "sturm: ccache found at ${CCACHE_PROGRAM} — wiring as "
                       "CMAKE_<LANG>_COMPILER_LAUNCHER (STURM_USE_CCACHE=ON)")
        set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}"
            CACHE STRING "C compiler launcher (set by STURM_USE_CCACHE)" FORCE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}"
            CACHE STRING "C++ compiler launcher (set by STURM_USE_CCACHE)" FORCE)
    else()
        message(STATUS "sturm: ccache not found on PATH — STURM_USE_CCACHE=ON "
                       "is a no-op. Install via tools/setup_dev_env.sh "
                       "(`apt install ccache` on the agent container, "
                       "`brew install ccache` on macOS).")
    endif()
endif()

# ── sturm-q2eb — mold linker preference ──────────────────────────────────────
# mold is a drop-in replacement for `ld`/`lld`, often 2-5× faster on link-heavy
# targets. The 9 transpiler-test bucket binaries each pull in libclang-cpp at
# link time (large symbol table) — that link phase dominates cold-build wall
# after the compile phase finishes. mold cuts it substantially.
#
# Defaults
#   STURM_USE_MOLD=ON (default): pass `-fuse-ld=mold` to every link if mold
#                                is on PATH. Silent no-op when mold is absent.
#   STURM_USE_MOLD=OFF         : keep whatever linker the toolchain picks
#                                (typically `ld` from binutils on Linux).
#
# Plugin loading: mold-produced .so files are vanilla ELF, ABI-compatible
# with libclang's dlopen path. No special handling required for
# sturm-transpile-plugin.so (verified empirically on the Linux agent
# container; macOS uses ld64 natively, mold is Linux-only).
option(STURM_USE_MOLD
    "Pass -fuse-ld=mold to the linker when mold is present (default ON on Linux; macOS uses ld64 natively and ignores this option)."
    ON)
if(STURM_USE_MOLD AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(MOLD_PROGRAM mold)
    if(MOLD_PROGRAM)
        message(STATUS "sturm: mold found at ${MOLD_PROGRAM} — using "
                       "-fuse-ld=mold for all link steps (STURM_USE_MOLD=ON)")
        add_link_options(-fuse-ld=mold)
    else()
        message(STATUS "sturm: mold not found on PATH — STURM_USE_MOLD=ON "
                       "is a no-op. Install via tools/setup_dev_env.sh "
                       "(`apt install mold` on the agent container).")
    endif()
endif()

# ── sturm-q2eb — unity-build bucketing for transpiler tests ──────────────────
# Each of the 9 transpiler-test bucket targets (sturm-r8xu) holds 5-30
# logically distinct test .cpp files that share the same compile flags
# and the same per-bucket `run_<name>` entry-point convention (no main()
# collisions across members — already a precondition of bucketing). The
# unity build concatenates members so libclang's AST headers + the
# transpiler-test PCH preamble are parsed once per unity TU instead of
# once per member, collapsing the parse-and-codegen cost roughly 8×.
# Implementation lives in transpiler/tests/CMakeLists.txt's
# `sturm_define_bucket_target` macro; this option is the kill switch.
#
# Off by default would defeat the win, so default ON. Toggle OFF only
# when bisecting a unity-build induced symbol collision in a new test
# member (the `run_<name>` convention is the only structured assumption
# the unity TU makes about its members; rare static/anon-namespace name
# clashes are still possible in helper code).
option(STURM_UNITY_BUCKETS
    "Enable CMake unity builds on the 9 transpiler-test bucket targets (default ON since sturm-dmsp wrapped every bucketed test_*.cpp in a unique named namespace `sturm_<stem>_ns`; multiple `namespace { }` blocks in a unity TU merge into one unnamed ns and ODR-collide, so the wrap MUST be a uniquely-named outer ns — anonymous nesting inside is fine for internal linkage). Toggle OFF only when bisecting a unity-build induced symbol collision in a new test member."
    ON)
if(STURM_UNITY_BUCKETS)
    message(STATUS "sturm: STURM_UNITY_BUCKETS=ON — transpiler-test buckets "
                   "will compile as 1-2 unity TUs per bucket "
                   "(UNITY_BUILD_BATCH_SIZE=8). Each bucketed test_*.cpp "
                   "is wrapped in a unique `namespace sturm_<stem>_ns { }` "
                   "so file-local helpers (tests_pass / run_on / etc.) do "
                   "not ODR-collide across members in the unity TU.")
endif()

# ── sturm-8vdv — unity-build the transpiler matcher_*.cpp set ────────────────
# Followup from the sturm-pjtx -ftime-trace investigation: the 35
# transpiler/src/matcher_*.cpp files dominate cold wall-time on
# test_bucket_qram (~11m24s baseline / sturm-cxvo trimmed to ~6m14s by
# pulling matcher_common.hpp into the PCH). At 6× parallelism the build
# is still bound by the slowest individual matcher TU. Concatenating
# the 35 matcher TUs into ~5 unity chunks (BATCH_SIZE=8) cuts the
# parse-and-codegen cost ~5-7× by amortising libclang AST headers +
# matcher_common.hpp's inline helpers across siblings.
#
# Wrapping: each matcher_<base>.cpp's inner `namespace { ... }` (inside
# `namespace sturm::transpile { }`) was renamed to a uniquely-named
# `namespace sturm_matcher_<base>_anon_ns { ... }` and a matching
# `using namespace sturm_matcher_<base>_anon_ns;` was injected at
# sturm::transpile scope so the public `register_*` functions below
# the close still resolve helpers. This mirrors sturm-dmsp's wrap of
# the bucketed test_*.cpp files — multiple `namespace { }` blocks in
# a unity TU merge into one unnamed ns per C++20 [basic.namespace]/8,
# so the rename to a unique outer ns is what makes file-local helpers
# (e.g. `index_has_qint_udc` which is defined verbatim in three qram
# subscript matcher TUs) collision-free under unity.
#
# Toggle OFF only when bisecting a unity-build induced collision in a
# newly-added matcher TU.
option(STURM_UNITY_MATCHERS
    "Enable CMake unity builds on transpiler matcher_*.cpp files (\
sturm-8vdv). Default ON since the matcher TUs were wrapped in unique \
named namespaces `sturm_matcher_<base>_anon_ns`. Toggle OFF when \
bisecting a unity-induced collision in a newly-added matcher TU."
    ON)
if(STURM_UNITY_MATCHERS)
    message(STATUS "sturm: STURM_UNITY_MATCHERS=ON — transpiler matcher_*.cpp "
                   "files batch into ~5 unity chunks "
                   "(UNITY_BUILD_BATCH_SIZE=8). Each matcher_<base>.cpp's "
                   "inner anon ns is wrapped in `sturm_matcher_<base>_anon_ns` "
                   "so file-local helpers do not ODR-collide across siblings "
                   "in the unity TU.")
endif()

# Helper: enable UNITY_BUILD on <target> for its matcher_*.cpp sources
# only. Marks every non-matcher source with SKIP_UNITY_BUILD_INCLUSION
# so the BATCH mode bundles ONLY the matcher TUs into unity chunks.
# Inspect via build/<target>.dir/Unity/unity_*.cxx after configure.
function(sturm_unity_matchers target)
    if(NOT STURM_UNITY_MATCHERS)
        return()
    endif()
    set_target_properties(${target} PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 8)
    get_target_property(_srcs ${target} SOURCES)
    if(NOT _srcs)
        return()
    endif()
    foreach(_src IN LISTS _srcs)
        get_filename_component(_name "${_src}" NAME)
        if(NOT _name MATCHES "^matcher_.*\\.cpp$")
            set_source_files_properties(${_src} TARGET_DIRECTORY ${target}
                PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE)
        endif()
    endforeach()
endfunction()

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
# sturm-5jta (Frontend simpl. P2.b / G5): the legacy STURM_ANCILLA_CAPACITY
# compile-time knob is gone — the qubit pool grows on demand. SIMULATE-mode
# memory cost is the user's responsibility (PRD §5.5).

# ── sturm-mixe — STURM_MODE build flag (Frontend simpl. P1 / PRD §5.3 / G3) ──
# Declares the `STURM_MODE` cache STRING (default APPEND), validates it,
# and exposes the `sturm::frontend` INTERFACE library carrying
# `STURM_MODE_DEFAULT=STURM_MODE_<X>` as a compile definition. Toggling
# the flag between configures invalidates the INTERFACE library's compile
# definition and forces a rebuild of dependents (OQ5).
include(${CMAKE_SOURCE_DIR}/cmake/SturmFrontend.cmake)

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

# ── sturm-18zc — shared OBJECT lib for gate_kind.c ───────────────────────────
# `src/sturm/core/gate_kind.c` is the one C TU in the project — every test
# binary that needs the gate-info table inlined it directly in its source
# list. For `test_bucket_qram` the resulting mixed C+C++ target blocks PCH
# reuse from the transpiler driver (CMake hard-errors when a mixed-language
# consumer attempts `target_precompile_headers REUSE_FROM` on a C++-only
# PCH — sturm-bs8s). The carve-out flips test_bucket_qram into a pure C++
# consumer that REUSE_FROMs `sturm-transpile`'s PCH and inherits the
# matcher_common.hpp coverage from sturm-cxvo.
#
# Defined at top-level (mirroring sturm-x4iq / sturm-grhw) so any
# subdirectory can `target_link_libraries(... PRIVATE sturm_gate_kind)`
# without re-declaring the source file or its `-std=c11` build flag.
# Source-file property must live in the same directory scope as the
# add_library() call — CMake source-file properties are dir-scoped per
# the documented behaviour.
set_source_files_properties(
    "${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c"
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_library(sturm_gate_kind OBJECT
    "${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c")
target_include_directories(sturm_gate_kind PUBLIC
    "${CMAKE_SOURCE_DIR}/include")

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
