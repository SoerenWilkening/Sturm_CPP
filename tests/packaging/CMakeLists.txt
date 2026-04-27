cmake_minimum_required(VERSION 3.16)

# tests/packaging/CMakeLists.txt — sturm-vr0v.2 / E5.M2 acceptance test
# wiring. Runs `tests/packaging/test_sturmc.py` AGAINST AN INSTALLED
# PREFIX produced by `cmake --install` into a per-run temp dir.
#
# Wiring strategy
# ---------------
# The Python test itself owns the install step (it `cmake --install`s
# into a per-test tempdir and tears it down on exit), because plumbing
# install through ctest's FIXTURES_SETUP would either:
#   * re-use a single shared prefix across all subsequent tests (wrong:
#     leaks between runs, and other test cases here would have to know
#     which prefix to read), or
#   * force a separate `cmake --install` ctest case whose ${prefix}
#     argument has to be a stable path that the Python test then opens.
# Owning the install inside the Python test keeps the "install
# happens before the assertions" ordering bytewise verifiable from a
# single rc-checked subprocess.run() call, and keeps the temp prefix
# scoped to the lifetime of the test process.
#
# What ctest contributes is the build-then-test ordering: the
# `sturmc_build_artifacts` setup test guarantees every install()
# target referenced by the test (sturm-transpile, sturm-transpile-plugin,
# sturmc itself) is up-to-date before the Python test runs.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ── Fixture: build all artifacts the install step depends on ────────────────
# `cmake --install` will skip targets that haven't been built yet
# (silently-not-installed install(TARGETS) entries, plus the
# install(PROGRAMS tools/sturmc.py) which has no build step but does
# require the source file to exist). The build target list mirrors the
# install rules in the root CMakeLists.txt and transpiler/CMakeLists.txt.
add_test(
    NAME sturmc_build_artifacts
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
            --parallel 6
            --target sturm-transpile
            --target sturm-transpile-plugin)
set_tests_properties(sturmc_build_artifacts PROPERTIES
    LABELS "packaging;sturmc"
    FIXTURES_SETUP "sturmc_artifacts_built"
    # Build step can take a while on a clean tree.
    TIMEOUT 600)

# ── The acceptance test ─────────────────────────────────────────────────────
# Environment plumbing: STURMC_BUILD_DIR is the tree the test should run
# `cmake --install` against; STURMC_CXX is the compiler `sturmc --cxx`
# receives; STURMC_FIXTURE_CPP is the input source. STURMC_CMAKE is
# optional and lets the test reuse the exact CMake binary that
# configured this build (matters on hosts with multiple cmakes).
add_test(
    NAME sturmc_installed_prefix
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_sturmc.py")

set_tests_properties(sturmc_installed_prefix PROPERTIES
    LABELS "packaging;sturmc"
    FIXTURES_REQUIRED "sturmc_artifacts_built"
    TIMEOUT 300
    ENVIRONMENT
        "STURMC_BUILD_DIR=${CMAKE_BINARY_DIR};\
STURMC_CXX=${CMAKE_CXX_COMPILER};\
STURMC_FIXTURE_CPP=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hello_sturmc.cpp;\
STURMC_CMAKE=${CMAKE_COMMAND}")
