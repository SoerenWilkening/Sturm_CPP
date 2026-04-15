# transpiler/CMakeLists.txt — builds the sturm-transpile driver (M4).
#
# Requires LLVM >= 17 and Clang >= 17. On systems where the packages are
# not installed the user will see a "find_package(LLVM) failed" error — we
# enrich that with an actionable hint below so debugging is a one-liner.
#
# This file is included by the root CMakeLists.txt only when the option
# STURM_TRANSPILE is ON, so a vanilla build of STURM never pays the cost
# of resolving LLVM.

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
    src/uncompute_pass.cpp
    src/emitter.cpp
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

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
