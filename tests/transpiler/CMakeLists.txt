# tests/transpiler/CMakeLists.txt — parent for the in-tree transpiler tests
# that live under tests/ (as opposed to transpiler/tests/, which holds the
# transpiler binary's unit tests).
#
# M10 lands the first consumer under test_cmake_glue/. M11 adds the
# snapshot_or_single ctest here; it invokes sturm-transpile on the locked
# PRD-MVP fixture and diffs the generated output against the golden file.
# M12 adds the gate_equivalence_or_single ctest: a linked harness that
# runs both a transpiled fixture and a hand-written reference under a
# circuit-mode sink and asserts their gate streams match byte-for-byte.

cmake_minimum_required(VERSION 3.16)

# M12 links a small C implementation file (`src/sturm/core/gate_kind.c`)
# into the gate_equivalence_or_single harness.  Explicitly enabling the
# C language keeps CMake from complaining when STURM_TRANSPILE=ON turns
# the harness target on.
enable_language(C)

add_subdirectory(test_cmake_glue)

# ── M11: snapshot fixture (sturm-transpile or_single.cpp) ───────────────────
# The test requires the sturm-transpile binary, which is only built when
# STURM_TRANSPILE=ON. Registering the test behind the same flag keeps OFF
# configurations honest (no dangling target references, no ghost failures
# from a tool that was never built).
if(STURM_TRANSPILE)
    add_test(
        NAME snapshot_or_single
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/or_single.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/or_single.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/or_single
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_or_single PROPERTIES
        LABELS "transpiler"
        # The ctest harness cannot express a target-level dependency via
        # DEPENDS; instead we rely on the fact that building the test
        # subdirectory is a configure-time operation and add_test's COMMAND
        # resolves $<TARGET_FILE:...> at generate time. The user must have
        # run `cmake --build build` before `ctest`, which builds the
        # sturm-transpile binary as part of the default target set.
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()

# ── LP3: RED snapshot fixture for the lazy OrExpr<qbool> initializer ────────
# Same driver and harness shape as snapshot_or_single, pointed at the
# hermetic lazy-path fixture. LP3 lands this test against a PLACEHOLDER
# expected file, so it is EXPECTED TO FAIL until LP4 widens
# register_or_matcher to peel the user-defined-conversion / operator qbool
# chain and freezes the real golden.
#
# Registering this test at LP3 (rather than waiting for LP4) keeps the
# TDD discipline explicit: the failure is visible in CI, the delta is
# human-readable via the unified diff the run_snapshot driver emits, and
# LP4's GREEN signal is unambiguously the flip of this one test.
if(STURM_TRANSPILE)
    add_test(
        NAME snapshot_or_single_backend
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/or_single_backend.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/or_single_backend.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/or_single_backend
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_or_single_backend PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()

# ── M12: gate-stream equivalence test ───────────────────────────────────────
# Proves PRD acceptance criterion #4 — the transpiler's output, compiled
# and run, emits the same gate stream as a hand-written reference that
# spells out the `uncompute_or(...)` call explicitly.
#
# Build structure:
#   1. The INPUT fixture `fixtures/or_single_runtime.cpp` is transpiled at
#      build time via an add_custom_command that mirrors the transpile
#      step inside add_quantum_executable().  The generated TU lands at
#      ${CMAKE_BINARY_DIR}/sturm_gen/tests/transpiler/fixtures/
#      or_single_runtime.cpp — the same path layout every other
#      add_quantum_executable() output uses.
#   2. The REFERENCE fixture `fixtures/or_single_reference.cpp` is
#      compiled directly (no transpile step).
#   3. The harness `test_gate_equivalence.cpp` is linked together with
#      both fixture TUs and the real sturm implementation sources so the
#      forward OR and `uncompute_or` actually emit gates at run time.
#
# The test is registered only when STURM_TRANSPILE=ON because the
# transpile step depends on the sturm-transpile target.
if(STURM_TRANSPILE)
    # Resolve the transpile custom-command output path.  The driver
    # writes <output_dir>/<relpath-of-input> when invoked with a
    # relative input path and WORKING_DIRECTORY = CMAKE_SOURCE_DIR —
    # which is exactly what add_quantum_executable() does internally.
    set(_m12_runtime_src
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/or_single_runtime.cpp")
    file(RELATIVE_PATH _m12_rel_src "${CMAKE_SOURCE_DIR}" "${_m12_runtime_src}")
    set(_m12_generated
        "${CMAKE_BINARY_DIR}/sturm_gen/${_m12_rel_src}")
    get_filename_component(_m12_gen_dir "${_m12_generated}" DIRECTORY)

    add_custom_command(
        OUTPUT "${_m12_generated}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_m12_gen_dir}"
        COMMAND $<TARGET_FILE:sturm-transpile>
                "${_m12_rel_src}"
                --output-dir "${CMAKE_BINARY_DIR}/sturm_gen"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS "${_m12_runtime_src}" sturm-transpile
        COMMENT "sturm-transpile ${_m12_rel_src} (M12)"
        VERBATIM)
    set_source_files_properties("${_m12_generated}" PROPERTIES
        GENERATED TRUE)

    add_executable(test_gate_equivalence_or_single
        test_gate_equivalence.cpp
        "${_m12_generated}"
        ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/or_single_reference.cpp
        # Real sturm implementation sources — the harness needs
        # uncompute_or, execute_gate and the BackendContext C ABI to
        # actually fire.  The header-only `sturm` interface target does
        # not provide these symbols, so they are compiled in here.
        ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
        ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
        ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
        ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
        ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
        ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
        ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
    )
    target_include_directories(test_gate_equivalence_or_single PRIVATE
        ${CMAKE_SOURCE_DIR}/include)
    target_compile_features(test_gate_equivalence_or_single PRIVATE cxx_std_20)
    target_compile_definitions(test_gate_equivalence_or_single PRIVATE
        STURM_ANCILLA_CAPACITY=256
        # STURM_BACKEND_ENABLED activates the quantum operator path in
        # qbool_ops.hpp / lazy_expr.hpp (OrExpr::operator qbool() that
        # allocates an ancilla and emits CX + CX + CCX) and pulls in
        # the matching field/destructor machinery in qbool.hpp / qint_core.
        # Without this macro, `a | b` takes the classical short-circuit
        # path in qbool_logic.hpp and the gate stream would be empty on
        # both sides — the test would pass trivially.
        STURM_BACKEND_ENABLED=1
    )
    target_link_libraries(test_gate_equivalence_or_single PRIVATE orkan_headers)
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
        PROPERTIES LANGUAGE C
                   COMPILE_FLAGS "-std=c11")
    # The transpile custom-command is the only producer of the generated
    # file; add an explicit target dependency so that touching the
    # transpiler binary re-triggers the fixture regeneration.
    add_dependencies(test_gate_equivalence_or_single sturm-transpile)

    add_test(NAME gate_equivalence_or_single
             COMMAND test_gate_equivalence_or_single)
    set_tests_properties(gate_equivalence_or_single PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()
