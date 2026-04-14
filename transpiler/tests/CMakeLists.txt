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
