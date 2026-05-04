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

# ── sturm-mjr2.1 / E4.M1 — versioning macros header acceptance test ─────────
# Pins the contract for include/sturm/version.hpp (generated from
# include/sturm/version.hpp.in via configure_file from the top-level
# `project(... VERSION x.y.z)`). The test consumes the header via
# `<sturm/version.hpp>` and matches STURM_VERSION_STRING against
# ${PROJECT_VERSION} (fed in via -DEXPECTED_VERSION) so any drift between
# the template's @PROJECT_VERSION@ expansion and CMake's source-of-truth
# surfaces as a runtime failure.
add_executable(test_version test_version.cpp)
# Public headers (link against the header-only `sturm` interface target so
# the build-tree binary include dir carrying the generated version.hpp is
# on the search path; no need to repeat it here).
target_link_libraries(test_version PRIVATE sturm)
target_compile_features(test_version PRIVATE cxx_std_20)
target_compile_definitions(test_version PRIVATE
    "EXPECTED_VERSION=\"${PROJECT_VERSION}\"")
add_test(NAME test_version COMMAND test_version)
set_tests_properties(test_version PROPERTIES
    LABELS "packaging;version")

# ── sturm-mjr2.2 / E4.M2 — find_package(sturm) populates sturm_VERSION ──────
# Install-and-consume integration test: cmake --install into a temp prefix,
# then configure a minimal downstream project that does
# `find_package(sturm REQUIRED)` and FATAL_ERRORs unless sturm_VERSION is
# non-empty AND equal to PROJECT_VERSION. The assertion runs at downstream
# *configure* time (no compilation needed) — see test_find_package_version.py.
#
# Reuses the `sturmc_artifacts_built` fixture so cmake --install does not
# silently skip targets whose build step has not yet run. STURM_EXPECTED_VERSION
# is fed in from the top-level project's ${PROJECT_VERSION} so the test stays
# bytewise pinned to CMake's source-of-truth (cf. test_version.cpp pattern).
add_test(
    NAME find_package_version
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_find_package_version.py")
set_tests_properties(find_package_version PROPERTIES
    LABELS "packaging;version"
    FIXTURES_REQUIRED "sturmc_artifacts_built"
    TIMEOUT 300
    ENVIRONMENT
        "STURMC_BUILD_DIR=${CMAKE_BINARY_DIR};\
STURMC_CMAKE=${CMAKE_COMMAND};\
STURM_EXPECTED_VERSION=${PROJECT_VERSION}")

# ── sturm-nalq.1 / E2.M1 — public/detail header layout acceptance test ──────
# Pins the public-vs-detail split established in E2.M1: this TU includes
# only the headers classified `public` in docs/transpiler_emit_targets.md
# and instantiates `sturm::qint_t<8>` plus probes `add_mod`'s public
# signature. If a public header silently regains a direct #include of a
# moved internal path (e.g. `sturm/lib/...` or
# `sturm/qtypes/lossy_oop.hpp`), this TU fails to compile — the
# regression net for the header reorganization. Header-only build
# (links against the `sturm` INTERFACE target).
add_executable(test_detail_layout test_detail_layout.cpp)
target_link_libraries(test_detail_layout PRIVATE sturm)
target_compile_features(test_detail_layout PRIVATE cxx_std_20)
add_test(NAME test_detail_layout COMMAND test_detail_layout)
set_tests_properties(test_detail_layout PROPERTIES
    LABELS "packaging")

# ── sturm-zmfk.1 / E3.M1 — umbrella header acceptance test ──────────────────
# Pins the public-API surface promised by `<sturm/sturm.hpp>`: a TU that
# includes ONLY the umbrella must reach `sturm::qint`, `sturm::qbool`,
# `WHEN(...)`, `sturm::add_mod`, and `sturm::invert<&fn>()`. If the
# umbrella ever drops a public header, the corresponding probe in
# test_umbrella_only.cpp stops compiling. Header-only build linking
# against the `sturm` INTERFACE target.
add_executable(test_umbrella_only test_umbrella_only.cpp)
target_link_libraries(test_umbrella_only PRIVATE sturm)
target_compile_features(test_umbrella_only PRIVATE cxx_std_20)
add_test(NAME test_umbrella_only COMMAND test_umbrella_only)
set_tests_properties(test_umbrella_only PROPERTIES
    LABELS "packaging")

# ── sturm-65rs.6 / B1 — qint resolution acceptance test ────────────────────
# Pins the post-B1 type identity of `sturm::qint`: it IS sturm::frontend::qint
# (PRD §4.2), and a LOCAL `using qint = sturm::qint_t<W>;` still resolves to
# qint_t<W> so the 50+ existing transpiler fixtures keep compiling (PRD A6).
add_executable(test_qint_resolution test_qint_resolution.cpp)
target_link_libraries(test_qint_resolution PRIVATE sturm)
target_compile_features(test_qint_resolution PRIVATE cxx_std_20)
add_test(NAME test_qint_resolution COMMAND test_qint_resolution)
set_tests_properties(test_qint_resolution PROPERTIES
    LABELS "packaging;qac")

# ── sturm-zmfk.2 / E3.M2 — prelude header acceptance test ───────────────────
# Pins the opt-in `<sturm/prelude.hpp>` contract (PRD §3.4 / D7): a TU
# that includes ONLY the prelude must reach `qint` and `qbool` without
# the `sturm::` qualifier (because the prelude does
# `using sturm::qint; using sturm::qbool;`) AND must NOT see `add_mod`
# unqualified — free functions are intentionally kept sturm::-prefixed
# to avoid global-namespace pollution and ADL surprises. The
# detection-idiom guard in test_prelude.cpp fails to compile if a
# future edit ever silently adds `using sturm::add_mod;` (or any
# other free-function using-declaration) to the prelude.
# Header-only build linking against the `sturm` INTERFACE target.
add_executable(test_prelude test_prelude.cpp)
target_link_libraries(test_prelude PRIVATE sturm)
target_compile_features(test_prelude PRIVATE cxx_std_20)
add_test(NAME test_prelude COMMAND test_prelude)
set_tests_properties(test_prelude PROPERTIES
    LABELS "packaging")

# ── sturm-795j.1 / E6.M1 — external_consumer standalone smoke project ──────
# Drives `tests/external_consumer/` end-to-end against an installed STURM
# prefix: cmake --install into a tmp prefix, configure + build the
# standalone consumer there with `-DCMAKE_PREFIX_PATH=<prefix>`, run the
# produced binary, diff its stdout against tests/external_consumer/golden.txt.
# PRD §3.6 / Plan §E6.M1.
#
# Reuses the `sturmc_artifacts_built` ctest fixture from sturm-vr0v.2 so the
# install step does not silently skip targets that have not been built yet
# (sturm-transpile, sturm-transpile-plugin) — the consumer's
# `add_quantum_executable` requires both to land in <prefix>/bin and
# <prefix>/lib respectively.
add_test(
    NAME external_consumer_smoke
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/external_consumer/run_smoke.py")
set_tests_properties(external_consumer_smoke PROPERTIES
    LABELS "packaging;external_consumer"
    FIXTURES_REQUIRED "sturmc_artifacts_built"
    # Configure + build of the consumer can take a while on a clean tree
    # (Clang plugin parses the prelude umbrella ⇒ all of <sturm/...>).
    TIMEOUT 600
    ENVIRONMENT
        "STURMC_BUILD_DIR=${CMAKE_BINARY_DIR};\
STURMC_CXX=${CMAKE_CXX_COMPILER};\
STURMC_CMAKE=${CMAKE_COMMAND}")
