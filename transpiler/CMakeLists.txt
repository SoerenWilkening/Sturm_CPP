# transpiler/CMakeLists.txt — builds the sturm-transpile driver (M4).
#
# Requires LLVM >= 17 and Clang >= 17. On systems where the packages are
# not installed the user will see a "find_package(LLVM) failed" error — we
# enrich that with an actionable hint below so debugging is a one-liner.

cmake_minimum_required(VERSION 3.16)

# ── Locate LLVM + Clang ───────────────────────────────────────────────────────
# find_package uses LLVM_DIR / Clang_DIR if provided. On Ubuntu 24.04 the
# relevant config files live under /usr/lib/llvm-17/lib/cmake/{llvm,clang}.
find_package(LLVM CONFIG)
if(NOT LLVM_FOUND)
    message(FATAL_ERROR
        "sturm-transpile requires LLVM >= 17 development headers.\n"
        "Install on Debian/Ubuntu:\n"
        "    sudo apt-get install llvm-17-dev libclang-17-dev clang-17\n"
        "and re-configure with:\n"
        "    cmake -DLLVM_DIR=/usr/lib/llvm-17/lib/cmake/llvm \\\n"
        "          -DClang_DIR=/usr/lib/llvm-17/lib/cmake/clang ..\n"
        "macOS: brew install llvm@17 (then set LLVM_DIR / Clang_DIR to the\n"
        "Homebrew prefix, e.g. /opt/homebrew/opt/llvm@17/lib/cmake/...).")
endif()
if(LLVM_PACKAGE_VERSION VERSION_LESS "17.0")
    message(FATAL_ERROR
        "sturm-transpile requires LLVM >= 17 but found ${LLVM_PACKAGE_VERSION}. "
        "Install llvm-17-dev or newer, then point LLVM_DIR at the matching "
        "cmake directory.")
endif()

find_package(Clang CONFIG)
if(NOT Clang_FOUND)
    message(FATAL_ERROR
        "sturm-transpile requires Clang >= 17 development headers.\n"
        "On Debian/Ubuntu run:\n"
        "    sudo apt-get install libclang-17-dev clang-17\n"
        "and re-configure with:\n"
        "    cmake -DClang_DIR=/usr/lib/llvm-17/lib/cmake/clang ..")
endif()

message(STATUS "sturm-transpile: using LLVM ${LLVM_PACKAGE_VERSION} "
               "from ${LLVM_INSTALL_PREFIX}")

# ── sturm-transpile binary ────────────────────────────────────────────────────
add_executable(sturm-transpile
    src/main.cpp
    src/transpile_consumer.cpp
    src/diag_context.cpp
    src/io.cpp
    src/skip.cpp
    src/qir.cpp
    src/matcher_qbool_bitwise.cpp
    src/matcher_qbool_compound.cpp
    src/matcher_qbool_assign.cpp
    src/matcher_qint_const.cpp
    src/matcher_qint_qint.cpp
    src/matcher_qint_compare.cpp
    src/matcher_when_lift.cpp
    src/matcher_when_nested.cpp
    src/matcher_when_operand_mutation.cpp
    src/matcher_quantum_to_classical_cond.cpp
    src/matcher_brace_wrap.cpp
    src/matcher_outer_var_guard.cpp
    src/matcher_hoist_invariant.cpp
    src/matcher_ccnot_fuse.cpp
    src/matcher_dead_ancilla.cpp
    src/matcher_dropped_quantum_return.cpp
    src/routine_registry.cpp
    src/matcher_user_routine.cpp
    src/uncompute_pass.cpp
    src/source_map.cpp
    src/emitter.cpp
    # PM4-2: Registry implementation + Meyer's-singleton vector for
    # link-time registrars. Plugin-path matchers (PM4-4 dlopen, PM4-3
    # in-process Registry& threading) resolve into this TU.
    src/plugin_registry.cpp
)
target_include_directories(sturm-transpile PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
# LLVM headers do not tolerate RTTI mismatches; inherit LLVM's own setting.
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(sturm-transpile PRIVATE -fno-rtti)
endif()
target_compile_features(sturm-transpile PRIVATE cxx_std_20)
# LLVM's definition list (LLVM_DEFINITIONS) carries the flags the headers
# were compiled with. Without these the Clang headers may see a different
# configuration than the library and fail to link.
separate_arguments(LLVM_DEFS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(sturm-transpile PRIVATE ${LLVM_DEFS_LIST})

# Bake the Clang builtin-headers resource directory into the binary so the
# LibTooling driver can find stdarg.h / stddef.h at runtime. Without this,
# every transpile on macOS (and any environment where LibTooling cannot
# locate its own builtins via argv[0]) fails to resolve __uint32_t /
# __darwin_wint_t in libc++ sys headers, the parser abandons main-file
# translation before the user's body is built into the AST, and every Phase
# A-I matcher sees zero ops from the user's code. The transpile still
# succeeds (emits a pass-through), so the breakage is silent — hence the
# compile-time capture here.
#
# The layout varies across distributions:
#   - macOS Homebrew llvm@17:     <prefix>/lib/clang/<N>
#   - Ubuntu apt-installed clang: /usr/lib/clang/<N>   (NOT under llvm-17/)
#   - From-source LLVM build:     <build>/lib/clang/<N>
# The authoritative answer for each layout is `clang -print-resource-dir`,
# so we ask the actual Clang binary installed alongside the package we
# linked against. Falls back to `${LLVM_INSTALL_PREFIX}/lib/clang/<N>` if
# no clang binary is findable (e.g. truly static-only LLVM builds).
find_program(STURM_CLANG_BIN
    NAMES "clang-${LLVM_VERSION_MAJOR}" "clang"
    HINTS "${LLVM_TOOLS_BINARY_DIR}"
    NO_DEFAULT_PATH)
if(STURM_CLANG_BIN)
    execute_process(
        COMMAND "${STURM_CLANG_BIN}" -print-resource-dir
        OUTPUT_VARIABLE STURM_CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(WARNING "sturm-transpile: `${STURM_CLANG_BIN} "
                        "-print-resource-dir` exited non-zero (${_rc}); "
                        "falling back to computed path.")
        unset(STURM_CLANG_RESOURCE_DIR)
    endif()
endif()
if(NOT STURM_CLANG_RESOURCE_DIR)
    set(STURM_CLANG_RESOURCE_DIR
        "${LLVM_INSTALL_PREFIX}/lib/clang/${LLVM_VERSION_MAJOR}")
endif()
message(STATUS "sturm-transpile: Clang resource dir = "
               "${STURM_CLANG_RESOURCE_DIR}")
target_compile_definitions(sturm-transpile PRIVATE
    STURM_CLANG_RESOURCE_DIR="${STURM_CLANG_RESOURCE_DIR}"
    STURM_LLVM_VERSION_MAJOR_STR="${LLVM_VERSION_MAJOR}")

# Link against the shared libclang-cpp + libLLVM when available. The
# distro-shipped LLVM layout (Debian / Ubuntu / Homebrew `llvm@17`)
# contains both static component archives (libclangTooling.a, ...) AND a
# combined shared library (libclang-cpp.so, libLLVM-17.so). If we link
# the static archives AND anything that itself was built against the
# shared library, LLVM's cl::opt global table ends up registered twice
# at process start and the tool aborts with:
#     Option 'debug-counter' registered more than once!
# The robust fix is to pick exactly ONE: prefer the shared libs, which
# is also how upstream clang-tools (`clang-tidy`, `clang-format`) link.
# Fall back to the static per-component archives if the shared library
# cannot be located — some custom LLVM builds ship static-only.
# Debian/Ubuntu ships libclang-cpp without a bare ".so" symlink — the
# file is `libclang-cpp.so.${LLVM_VERSION_MAJOR}`. The default find_library
# soname list does not cover that. Try the versioned name first, then fall
# back to the canonical name, then to a brute-force existence check.
find_library(STURM_LIBCLANG_CPP
    NAMES "clang-cpp-${LLVM_VERSION_MAJOR}"
          "clang-cpp"
    HINTS ${LLVM_LIBRARY_DIR}
    NO_DEFAULT_PATH)
if(NOT STURM_LIBCLANG_CPP)
    # Try the filename directly — CMake recognizes a fully specified path.
    set(_cand "${LLVM_LIBRARY_DIR}/libclang-cpp.so.${LLVM_VERSION_MAJOR}")
    if(EXISTS "${_cand}")
        set(STURM_LIBCLANG_CPP "${_cand}" CACHE FILEPATH
            "Path to libclang-cpp shared library" FORCE)
    endif()
endif()

find_library(STURM_LIBLLVM
    NAMES "LLVM-${LLVM_VERSION_MAJOR}"
          "LLVM"
    HINTS ${LLVM_LIBRARY_DIR}
    NO_DEFAULT_PATH)
if(NOT STURM_LIBLLVM)
    set(_cand "${LLVM_LIBRARY_DIR}/libLLVM-${LLVM_VERSION_MAJOR}.so")
    if(EXISTS "${_cand}")
        set(STURM_LIBLLVM "${_cand}" CACHE FILEPATH
            "Path to libLLVM shared library" FORCE)
    endif()
endif()

if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    message(STATUS "sturm-transpile: linking shared libclang-cpp + libLLVM "
                   "(${STURM_LIBCLANG_CPP}, ${STURM_LIBLLVM})")
    target_link_libraries(sturm-transpile PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    message(STATUS "sturm-transpile: shared libclang-cpp / libLLVM not found, "
                   "falling back to per-component static libraries")
    target_link_libraries(sturm-transpile PRIVATE
        clangTooling
        clangASTMatchers
        clangFrontend
        clangRewrite
        clangBasic
        clangAST
        clangLex
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangAnalysis
        clangEdit
    )
    # LLVMSupport carries cl::opt, Expected, errs(), etc.
    llvm_map_components_to_libnames(LLVM_LIBS support)
    target_link_libraries(sturm-transpile PRIVATE ${LLVM_LIBS})
endif()

# ── Plugin: sturm-transpile-plugin (PM1-3) ────────────────────────────────────
# Clang plugin (.so / .dylib) that drives the shared TranspileConsumer in
# EmissionMode::Plugin. Loaded via `clang++ -fplugin=$<TARGET_FILE:...>`
# on a downstream compile, reuses the same matcher pool as the standalone
# binary, and (after PM1-4 wires nested codegen) substitutes the
# transpiler-rewritten source for the user's raw input before EmitObjAction.
#
# Link discipline mirrors the main binary: shared libclang-cpp + libLLVM
# when available (to avoid cl::opt double-registration — Phase L /
# sturm-mzfa lesson), fallback to the static per-component archives
# otherwise. Symbol hiding is enforced on Linux via
# -Wl,--exclude-libs,ALL so the tens of thousands of symbols the
# shared LLVM .so exposes do not leak out of OUR plugin's interface
# (Clang's loader only needs the FrontendPluginRegistry Add<> ctor).
# On macOS, two-level namespace lookup already prevents symbol
# collision between dlopen'd plugins and the host clang's copy of
# LLVM, so no extra flag is required.
#
# The file is built as SHARED (not MODULE) so the target exports a
# normal .so/.dylib — Clang's plugin loader treats the two file types
# interchangeably and SHARED keeps the TARGET_FILE path consistent on
# both Linux (libsturm-transpile-plugin.so) and macOS
# (libsturm-transpile-plugin.dylib). Tests reach the file via the
# $<TARGET_FILE:sturm-transpile-plugin> generator expression, so a
# rename never needs to touch the test fixtures.
add_library(sturm-transpile-plugin SHARED
    src/plugin.cpp
    src/transpile_consumer.cpp
    src/diag_context.cpp
    src/io.cpp
    src/skip.cpp
    src/qir.cpp
    src/matcher_qbool_bitwise.cpp
    src/matcher_qbool_compound.cpp
    src/matcher_qbool_assign.cpp
    src/matcher_qint_const.cpp
    src/matcher_qint_qint.cpp
    src/matcher_qint_compare.cpp
    src/matcher_when_lift.cpp
    src/matcher_when_nested.cpp
    src/matcher_when_operand_mutation.cpp
    src/matcher_quantum_to_classical_cond.cpp
    src/matcher_brace_wrap.cpp
    src/matcher_outer_var_guard.cpp
    src/matcher_hoist_invariant.cpp
    src/matcher_ccnot_fuse.cpp
    src/matcher_dead_ancilla.cpp
    src/matcher_dropped_quantum_return.cpp
    src/routine_registry.cpp
    src/matcher_user_routine.cpp
    src/uncompute_pass.cpp
    src/source_map.cpp
    src/emitter.cpp
    # PM4-2: Registry implementation + Meyer's-singleton vector for
    # link-time registrars. Plugin-path matchers (PM4-4 dlopen, PM4-3
    # in-process Registry& threading) resolve into this TU.
    src/plugin_registry.cpp
)
target_include_directories(sturm-transpile-plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(sturm-transpile-plugin PRIVATE -fno-rtti)
endif()
target_compile_features(sturm-transpile-plugin PRIVATE cxx_std_20)
target_compile_definitions(sturm-transpile-plugin PRIVATE
    ${LLVM_DEFS_LIST}
    STURM_CLANG_RESOURCE_DIR="${STURM_CLANG_RESOURCE_DIR}"
    STURM_LLVM_VERSION_MAJOR_STR="${LLVM_VERSION_MAJOR}")

if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(sturm-transpile-plugin PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(sturm-transpile-plugin PRIVATE
        clangTooling
        clangASTMatchers
        clangFrontend
        clangRewrite
        clangBasic
        clangAST
        clangLex
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangAnalysis
        clangEdit
    )
    llvm_map_components_to_libnames(LLVM_LIBS_PLUGIN support)
    target_link_libraries(sturm-transpile-plugin PRIVATE ${LLVM_LIBS_PLUGIN})
endif()

# Hide the transitively-pulled LLVM symbols from the plugin's exported
# interface on Linux. A no-op on macOS / Darwin, where two-level
# namespace lookup already keeps the plugin's LLVM copy from colliding
# with the host clang's copy.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Hide the transitively-pulled LLVM symbols from the plugin's
    # EXPORTED interface. This prevents the tens of thousands of LLVM
    # / Clang symbols from leaking out of the plugin .so while still
    # letting the plugin RESOLVE those symbols (via its DT_NEEDED
    # libLLVM-17.so / libclang-cpp.so entries) — exclude-libs only
    # touches the dynamic symbol TABLE, not runtime lookup.
    target_link_options(sturm-transpile-plugin PRIVATE
        "LINKER:--exclude-libs,ALL")
endif()

# Clang's plugin loader matches on basename; we keep the conventional
# `lib` prefix here because `-fplugin=<full-path>` takes an absolute
# path and does not care about the prefix. The target-file generator
# expression (`$<TARGET_FILE:sturm-transpile-plugin>`) resolves to the
# right thing on every platform without bespoke PREFIX massaging.

# ── Install rule (Phase L PL-1) ───────────────────────────────────────────────
# Ship the transpiler driver into <prefix>/bin so downstream consumers can
# locate it via the IMPORTED `sturm-transpile` target wired up in
# sturmConfig.cmake (see top-level CMakeLists.txt install() block).
install(TARGETS sturm-transpile
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ── Install rule (Phase M PM1-9) ──────────────────────────────────────────────
# Ship the Clang plugin shared library alongside the driver so downstream
# consumers using the default plugin mode (PM1-5 add_quantum_executable)
# can locate it at <prefix>/lib/libsturm-transpile-plugin.{so,dylib}.
# Linux → libsturm-transpile-plugin.so; macOS → libsturm-transpile-plugin.dylib.
# The target is SHARED (see comment above), so a single LIBRARY DESTINATION
# covers both platforms — CMake routes .so into LIBRARY and .dylib into
# LIBRARY as well (RUNTIME is DLLs on Windows only, which we do not ship).
install(TARGETS sturm-transpile-plugin
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Ship the Clang builtin-headers resource directory alongside the binary so
# the tarball is truly self-contained. At runtime the transpiler resolves
# its resource dir relative to its own executable path
# (`<bindir>/../lib/clang/<LLVM_VERSION_MAJOR>`), so the on-disk layout must
# mirror that. Without these headers, the parser aborts on missing
# `stdarg.h` (every macOS libc sys header needs it) and the output is a
# silent pass-through — exactly the smoke-test failure on a clean runner
# where system LLVM-17 has been purged. See transpiler/src/main.cpp's
# ArgumentsAdjuster for the runtime lookup logic.
if(EXISTS "${STURM_CLANG_RESOURCE_DIR}/include")
    install(DIRECTORY "${STURM_CLANG_RESOURCE_DIR}/include"
            DESTINATION "lib/clang/${LLVM_VERSION_MAJOR}")
else()
    message(WARNING
        "sturm-transpile: Clang builtin-headers dir not found at "
        "${STURM_CLANG_RESOURCE_DIR}/include — installed tarball will not "
        "be self-contained (runs will silently pass through when system "
        "LLVM-17 is absent).")
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
