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

# ── sturm-ilz3 / Phase 3 — sturm.h umbrella header acceptance test ─────────
# Pins the contract for the NEW front-door umbrella `<sturm.h>` introduced
# by PRD §5.1 (drives A5 first half / G1). A TU that includes ONLY
# `<sturm.h>` (no other `<sturm/...>` headers) must:
#   1. Compile `qint a = 5;` (the user-level alias is brought in by the
#      umbrella's `using sturm::qint;`).
#   2. Reach the C ABI lifecycle (sturm_backend_create / destroy and
#      sturm_set_thread_context).
#   3. Reach the IR / GateRecord / GateIR types so `ctx->ir.append(...)`
#      observably grows the IR — proves the umbrella does not drop a
#      header that the gate-emission reachpoints (GateRecord,
#      sturm_gate_kind_t, BackendContext::ir) need. Linkage pulls in
#      `context.cpp` + `gate_kind.c` so the C ABI symbols (`sturm_*`)
#      resolve at link time. The legacy `<sturm/sturm.hpp>` umbrella is
#      still tested by `test_detail_layout.cpp`.
add_executable(test_umbrella_only
    test_umbrella_only.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c)
target_link_libraries(test_umbrella_only PRIVATE sturm)
target_compile_features(test_umbrella_only PRIVATE cxx_std_20)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11")
add_test(NAME test_umbrella_only COMMAND test_umbrella_only)
set_tests_properties(test_umbrella_only PROPERTIES
    LABELS "packaging;frontend_simpl")

# ── sturm-5qey / Phase 4 — opt-in feature headers acceptance test ──────────
# Pins the contract for `<sturm/qram.h>` and `<sturm/draw_ascii.h>` (PRD
# §5.2 / G2 / A5 second half). A TU that includes ONLY `<sturm.h>` plus
# `<sturm/qram.h>` must reach `sturm::QRAM_read` and observe the runtime
# entry-point growing the IR end-to-end (the C1 transpiler matcher
# rewrites `qint b = a[i];` into a `QRAM_read(a, i, b)` call — this test
# pins that runtime contract without invoking the transpiler). Linkage
# pulls the QRAM body, the backend simulate / orkan bridge sources, and
# the dispatch / control-stack sources the QROM XOR-fanout DSL emits
# WHEN-lifted CXs through (mirrors tests/qram/CMakeLists.txt's
# STURM_QRAM_BACKEND_SOURCES set).
add_executable(test_umbrella_qram
    test_umbrella_qram.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qram/qram_read.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp)
target_link_libraries(test_umbrella_qram PRIVATE sturm orkan_headers)
target_compile_features(test_umbrella_qram PRIVATE cxx_std_20)
add_test(NAME test_umbrella_qram COMMAND test_umbrella_qram)
set_tests_properties(test_umbrella_qram PROPERTIES
    LABELS "packaging;frontend_simpl")

# ── sturm-a4we — opt-in `sturm/draw_json.h` packaging acceptance test ──────
# Mirrors test_umbrella_only / test_umbrella_qram. Pins that
# `#include "sturm.h"` + `#include "sturm/draw_json.h"` is sufficient to
# reach `sturm::draw_json` / `sturm::print_json` and produce balanced-brace
# debug-print output from the thread-local context. The renderer body in
# `src/sturm/backend/draw_json.cpp` is linked directly (mirrors
# test_umbrella_only's posture of compiling the runtime sources rather
# than linking a separate library), keeping linkage minimal:
# context.cpp + gate_kind.c + draw_json.cpp.
add_executable(test_umbrella_draw_json
    test_umbrella_draw_json.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/draw_json.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c)
target_link_libraries(test_umbrella_draw_json PRIVATE sturm)
target_compile_features(test_umbrella_draw_json PRIVATE cxx_std_20)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11")
add_test(NAME test_umbrella_draw_json COMMAND test_umbrella_draw_json)
set_tests_properties(test_umbrella_draw_json PROPERTIES
    LABELS "packaging;frontend_simpl")

# ── sturm-l43b — opt-in `sturm/draw_svg.h` packaging acceptance test ───────
# Mirrors test_umbrella_draw_json. Pins that `#include "sturm.h"` +
# `#include "sturm/draw_svg.h"` is sufficient to reach `sturm::draw_svg` /
# `sturm::print_svg` and produce a well-formed <svg> envelope from the
# thread-local context. The renderer body in `src/sturm/backend/draw_svg.cpp`
# is linked directly (mirrors the sibling draw_json packaging test's
# posture of compiling the runtime sources rather than linking a separate
# library), keeping linkage minimal: context.cpp + gate_kind.c + draw_svg.cpp.
add_executable(test_umbrella_draw_svg
    test_umbrella_draw_svg.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/draw_svg.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c)
target_link_libraries(test_umbrella_draw_svg PRIVATE sturm)
target_compile_features(test_umbrella_draw_svg PRIVATE cxx_std_20)
add_test(NAME test_umbrella_draw_svg COMMAND test_umbrella_draw_svg)
set_tests_properties(test_umbrella_draw_svg PROPERTIES
    LABELS "packaging;frontend_simpl")

# ── sturm-5soh — opt-in `sturm/draw_mermaid.h` packaging acceptance test ───
# Mirrors test_umbrella_draw_svg / test_umbrella_draw_json. Pins that
# `#include "sturm.h"` + `#include "sturm/draw_mermaid.h"` is sufficient to
# reach `sturm::draw_mermaid` / `sturm::print_mermaid` and produce a
# well-formed `graph LR` Mermaid envelope from the thread-local context.
# The renderer body in `src/sturm/backend/draw_mermaid.cpp` is linked
# directly (mirrors the sibling draw_svg / draw_json packaging tests'
# posture of compiling the runtime sources rather than linking a separate
# library), keeping linkage minimal: context.cpp + gate_kind.c +
# draw_mermaid.cpp.
add_executable(test_umbrella_draw_mermaid
    test_umbrella_draw_mermaid.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/draw_mermaid.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c)
target_link_libraries(test_umbrella_draw_mermaid PRIVATE sturm)
target_compile_features(test_umbrella_draw_mermaid PRIVATE cxx_std_20)
add_test(NAME test_umbrella_draw_mermaid COMMAND test_umbrella_draw_mermaid)
set_tests_properties(test_umbrella_draw_mermaid PROPERTIES
    LABELS "packaging;frontend_simpl")

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

# ── sturm-jysu — qint_alias.hpp-first include-order regression test ────────
# Pins the include-order invariant the original B0 audit missed: a TU that
# #include "sturm/qtypes/qint_alias.hpp" as its FIRST sturm header must
# still compile (the cycle through qint_fwd.hpp must not leave the
# using-decl on line 36 referring to a not-yet-declared type). Before the
# fix, this target failed with "'frontend' in namespace 'sturm' does not
# name a type" at qint_fwd.hpp:36.
add_executable(test_qint_alias_first_include test_qint_alias_first_include.cpp)
target_link_libraries(test_qint_alias_first_include PRIVATE sturm)
target_compile_features(test_qint_alias_first_include PRIVATE cxx_std_20)
add_test(NAME test_qint_alias_first_include COMMAND test_qint_alias_first_include)
set_tests_properties(test_qint_alias_first_include PROPERTIES
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

# ── sturm-nfmu — shadow stale-install precheck acceptance test ──────────────
# Pins the configure-time precheck added to tests/external_consumer/
# CMakeLists.txt that detects a stale STURM install at /usr/local/include/
# sturm or /opt/homebrew/include/sturm shadowing the smoke test's freshly-
# installed temp prefix. AppleClang's default include-search order on
# macOS places /usr/local/include BEFORE -isystem flags, so the consumer's
# `add_quantum_executable` would silently link the wrong headers.
#
# Three behaviours are pinned (see test_shadow_detection.py):
#   1. shadow + sentinel ⇒ FATAL_ERROR with actionable diagnostic.
#   2. shadow + NO sentinel ⇒ silent (genuine downstream consumers).
#   3. NO shadow + sentinel ⇒ silent (clean machines under run_smoke.py).
#
# Configure-only test (no install needed); STURMC_BUILD_DIR points the
# consumer at the in-tree build's exported sturmConfig.cmake so
# find_package(sturm) resolves without a `cmake --install` step.
add_test(
    NAME external_consumer_shadow_detection
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/external_consumer/test_shadow_detection.py")
set_tests_properties(external_consumer_shadow_detection PROPERTIES
    LABELS "packaging;external_consumer"
    FIXTURES_REQUIRED "sturmc_artifacts_built"
    TIMEOUT 120
    ENVIRONMENT
        "STURMC_BUILD_DIR=${CMAKE_BINARY_DIR};\
STURMC_CMAKE=${CMAKE_COMMAND}")
