# transpiler/tests/CMakeLists.txt — M4 test suite.
#
# Two test binaries:
#   test_io      — pure unit tests for sturm::transpile::{resolve_output_path,
#                  write_file, read_file}. No LibTooling, no subprocess.
#   test_driver  — end-to-end tests that invoke the compiled sturm-transpile
#                  binary. Uses STURM_TRANSPILE_BIN compile definition so the
#                  test does not hard-code a build-tree path.

cmake_minimum_required(VERSION 3.16)

# Shared io helper sources, compiled into each test binary that needs them.
set(STURM_TRANSPILE_IO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/io.cpp
)

# ── Unit test: io helpers ─────────────────────────────────────────────────────
add_executable(test_transpile_io
    test_io.cpp
    ${STURM_TRANSPILE_IO_SOURCES}
)
target_include_directories(test_transpile_io PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_features(test_transpile_io PRIVATE cxx_std_20)
add_test(NAME test_transpile_io COMMAND test_transpile_io)
set_tests_properties(test_transpile_io PROPERTIES LABELS "transpiler")

# ── Unit test: Quantum IR (M6) ────────────────────────────────────────────────
# The IR depends only on clang::SourceLocation / SourceRange, which are
# header-inline, so the test compiles against the LLVM/Clang include dirs
# but does NOT need to link any LibTooling components. This keeps the test
# fast to build (< 1s) and runnable on any host with the Clang headers
# installed.
add_executable(test_transpile_qir
    test_qir.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
)
target_include_directories(test_transpile_qir PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
# Match the main binary's RTTI / definitions so the Clang headers see the
# exact same configuration they were compiled with.
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_transpile_qir PRIVATE -fno-rtti)
endif()
target_compile_features(test_transpile_qir PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_QIR NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_transpile_qir PRIVATE ${LLVM_DEFS_LIST_QIR})
add_test(NAME test_transpile_qir COMMAND test_transpile_qir)
set_tests_properties(test_transpile_qir PROPERTIES LABELS "transpiler")

# ── Unit test: M8 uncompute synthesis pass ──────────────────────────────────
# Like the M6 QIR test, M8 uses only clang::SourceLocation — an opaque 32-bit
# wrapper — so the test does NOT need to link any LibTooling components. It
# compiles in under a second and runs in microseconds because the QUnit under
# test is constructed by hand. This is the whole reason M8 is separate from
# M7 and M9: ordering regressions can be pinpointed without a compiler in the
# loop.
add_executable(test_transpile_uncompute_pass
    test_uncompute_pass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/uncompute_pass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
)
target_include_directories(test_transpile_uncompute_pass PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_transpile_uncompute_pass PRIVATE -fno-rtti)
endif()
target_compile_features(test_transpile_uncompute_pass PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_UNCOMPUTE NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_transpile_uncompute_pass PRIVATE
    ${LLVM_DEFS_LIST_UNCOMPUTE})
add_test(NAME test_transpile_uncompute_pass
         COMMAND test_transpile_uncompute_pass)
set_tests_properties(test_transpile_uncompute_pass PROPERTIES
    LABELS "transpiler")

# ── Unit test: M7 AST matcher ────────────────────────────────────────────────
# This test drives the `register_or_matcher` helper through a real Clang
# tooling invocation (runToolOnCodeWithArgs). It therefore needs to link
# against the same LibTooling components as sturm-transpile itself.
add_executable(test_transpile_matcher
    test_matcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/matcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/qir.cpp
)
target_include_directories(test_transpile_matcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(test_transpile_matcher PRIVATE -fno-rtti)
endif()
target_compile_features(test_transpile_matcher PRIVATE cxx_std_20)
separate_arguments(LLVM_DEFS_LIST_MATCHER NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(test_transpile_matcher PRIVATE
    ${LLVM_DEFS_LIST_MATCHER})
# Mirror the main binary's shared-vs-static LLVM link discipline so we do
# not double-register cl::opt tables (see the comment in the main CMake).
if(STURM_LIBCLANG_CPP AND STURM_LIBLLVM)
    target_link_libraries(test_transpile_matcher PRIVATE
        ${STURM_LIBCLANG_CPP}
        ${STURM_LIBLLVM}
    )
else()
    target_link_libraries(test_transpile_matcher PRIVATE
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
    llvm_map_components_to_libnames(LLVM_LIBS_MATCHER support)
    target_link_libraries(test_transpile_matcher PRIVATE ${LLVM_LIBS_MATCHER})
endif()
add_test(NAME test_transpile_matcher COMMAND test_transpile_matcher)
set_tests_properties(test_transpile_matcher PROPERTIES LABELS "transpiler")

# ── Integration test: driver ──────────────────────────────────────────────────
add_executable(test_transpile_driver
    test_driver.cpp
)
target_include_directories(test_transpile_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_features(test_transpile_driver PRIVATE cxx_std_20)
# Pass the absolute path to the binary as a preprocessor macro so the test
# does not have to guess at the build-tree layout. Generator expression
# `$<TARGET_FILE:...>` resolves to the final executable path.
target_compile_definitions(test_transpile_driver PRIVATE
    STURM_TRANSPILE_BIN="$<TARGET_FILE:sturm-transpile>"
)
add_dependencies(test_transpile_driver sturm-transpile)
add_test(NAME test_transpile_driver COMMAND test_transpile_driver)
set_tests_properties(test_transpile_driver PROPERTIES LABELS "transpiler")
