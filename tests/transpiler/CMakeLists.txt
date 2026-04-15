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

# ── Phase A / PA-1: snapshot fixture for `qbool tmp = ~a;` ─────────────────
# Self-inverse NOT: the emitted inverse is `tmp = ~tmp;`, re-applying the
# same X to the result qubit. Same harness / driver as snapshot_or_single;
# only the fixture and golden change.
if(STURM_TRANSPILE)
    add_test(
        NAME snapshot_not_single
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/not_single.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/not_single.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/not_single
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_not_single PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()

# ── Phase A / PA-2: snapshot fixture for `qbool tmp = a ^ b;` ──────────────
# Self-inverse XOR: the emitted inverse is the two-line `tmp ^= a;`
# followed by `tmp ^= b;`, together undoing the forward op because
# (a^b)^a^b == 0. No library helper is required.
if(STURM_TRANSPILE)
    add_test(
        NAME snapshot_xor_single
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xor_single.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xor_single.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/xor_single
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_xor_single PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()

# ── Phase A / PA-3: snapshot fixture for `a ^= b;` (quantum RHS) ───────────
# XOR-assign is self-adjoint: the emitted inverse is a verbatim
# re-emission of the same compound-assignment statement. First fixture
# in Phase A that exercises a statement-level matcher rather than a
# VarDecl initializer.
if(STURM_TRANSPILE)
    add_test(
        NAME snapshot_xor_assign_qbool
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xor_assign_qbool.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xor_assign_qbool.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/xor_assign_qbool
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_xor_assign_qbool PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()

# ── Phase A / PA-4: snapshot fixture for `a ^= 1;` (classical RHS) ─────────
# Classical-operand form: the matcher extracts the literal text via
# Lexer::getSourceText and embeds it verbatim in the IR. The render
# switch is unchanged from PA-3 — the operand name happens to be a
# numeric literal rather than a variable name, but the emission is
# textually identical.
if(STURM_TRANSPILE)
    add_test(
        NAME snapshot_xor_assign_classical
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xor_assign_classical.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xor_assign_classical.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/xor_assign_classical
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_xor_assign_classical PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()

# ── Phase B / PB-1..PB-4: snapshot fixtures for classical-const compound ────
# assigns on qint_t. Each covers one operator (+= -= *= /=) whose RHS is a
# classical integer literal lifted through the qint_t(long long) converting
# constructor; the emitted inverse is the dual operator over the same
# verbatim RHS token (+↔-, *↔/). Driver and harness are the same as the
# Phase A snapshot tests — only the fixture / golden pair changes.
if(STURM_TRANSPILE)
    add_test(
        NAME snapshot_add_assign_const
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/add_assign_const.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/add_assign_const.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/add_assign_const
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_add_assign_const PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME snapshot_sub_assign_const
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sub_assign_const.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sub_assign_const.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/sub_assign_const
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_sub_assign_const PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME snapshot_mul_assign_const
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mul_assign_const.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mul_assign_const.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/mul_assign_const
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_mul_assign_const PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME snapshot_div_assign_const
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/div_assign_const.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/div_assign_const.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/div_assign_const
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_div_assign_const PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()

# ── Phase C / PC-1..PC-5: snapshot fixtures for qint-qint compound assigns ──
# Each covers one operator (+= -= *= /= %=) whose RHS is a bare DeclRefExpr
# to a second qint_t parameter (no converting constructor fires). The
# emitted inverse is a `uncompute_{add,sub,mul,div,mod}_qint(lhs, rhs);`
# free-function call from include/sturm/uncompute/uncompute_api.hpp. Driver
# and harness are identical to the Phase B snapshot tests — only the
# fixture / golden pair changes.
if(STURM_TRANSPILE)
    add_test(
        NAME snapshot_add_assign_qint
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/add_assign_qint.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/add_assign_qint.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/add_assign_qint
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_add_assign_qint PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME snapshot_sub_assign_qint
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sub_assign_qint.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sub_assign_qint.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/sub_assign_qint
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_sub_assign_qint PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME snapshot_mul_assign_qint
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mul_assign_qint.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mul_assign_qint.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/mul_assign_qint
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_mul_assign_qint PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME snapshot_div_assign_qint
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/div_assign_qint.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/div_assign_qint.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/div_assign_qint
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_div_assign_qint PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME snapshot_mod_assign_qint
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mod_assign_qint.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mod_assign_qint.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/mod_assign_qint
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_mod_assign_qint PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()

# ── Phase D / PD-1..PD-6: snapshot fixtures for qint-qint comparisons ───────
# Each covers one relational operator (== != < <= > >=) whose operands are
# bare DeclRefExprs to two qint_t parameters, anchored under a `qbool c =
# ...;` VarDecl initializer. The emitted inverse is a
# `uncompute_{eq,ne,lt,le,gt,ge}_qint(c, a, b);` free-function call declared
# in include/sturm/uncompute/uncompute_api.hpp — three operands (result
# qbool + two qint inputs), unlike the Phase C compound-assign inverses
# which take two (lhs + rhs only). Driver and harness are identical to the
# Phase C snapshot tests — only the fixture / golden pair changes.
if(STURM_TRANSPILE)
    add_test(
        NAME snapshot_eq_compare_qint
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/eq_compare_qint.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/eq_compare_qint.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/eq_compare_qint
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_eq_compare_qint PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME snapshot_ne_compare_qint
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ne_compare_qint.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ne_compare_qint.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/ne_compare_qint
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_ne_compare_qint PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME snapshot_lt_compare_qint
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/lt_compare_qint.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/lt_compare_qint.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/lt_compare_qint
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_lt_compare_qint PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME snapshot_le_compare_qint
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/le_compare_qint.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/le_compare_qint.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/le_compare_qint
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_le_compare_qint PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME snapshot_gt_compare_qint
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gt_compare_qint.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gt_compare_qint.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/gt_compare_qint
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_gt_compare_qint PROPERTIES
        LABELS "transpiler"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME snapshot_ge_compare_qint
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ge_compare_qint.cpp
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ge_compare_qint.expected.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/ge_compare_qint
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
    set_tests_properties(snapshot_ge_compare_qint PROPERTIES
        LABELS "transpiler"
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

# ── LP6: idempotency guard tests ────────────────────────────────────────────
# PRD acceptance criterion #4: running sturm-transpile on a previously-
# emitted output must produce a byte-identical result. The runtime
# enforcement lives in main.cpp's skip detection (the AUTO-GENERATED
# sentinel triggers a verbatim copy), but the invariant is contract
# surface — these tests regress it end-to-end.
#
# Two registrations:
#   * transpiler_idempotent_or_single_backend — operates on the LP3
#     fixture's generated output (produced by snapshot_or_single_backend).
#     We DEPENDS on that test so it is guaranteed to have produced the
#     generated file before this idempotency check runs.
#   * transpiler_idempotent_example_or_circuit — operates on
#     ${CMAKE_BINARY_DIR}/sturm_gen/examples/or_circuit.cpp. That file is
#     emitted by add_quantum_executable(example_or_circuit ...). CTest
#     cannot express a direct target-level dependency, so we stage the
#     build via a SETUP fixture test (`build_example_or_circuit`) that
#     shells out to `cmake --build` for the example_or_circuit target,
#     and attach FIXTURES_REQUIRED on the idempotency test.
if(STURM_TRANSPILE)
    add_test(
        NAME transpiler_idempotent_or_single_backend
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/or_single_backend/or_single_backend.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/or_single_backend_idem
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
    set_tests_properties(transpiler_idempotent_or_single_backend PROPERTIES
        LABELS "transpiler"
        # The snapshot test is the producer of the file this idempotency
        # check reads. Running it first guarantees the input exists and
        # has the expected contents.
        DEPENDS "snapshot_or_single_backend"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    # Stage the example build as a ctest fixture so ctest alone (without
    # a prior `cmake --build`) can still run the idempotency check. The
    # setup test calls back into the generator to build just the
    # example_or_circuit target, which transitively produces
    # ${CMAKE_BINARY_DIR}/sturm_gen/examples/or_circuit.cpp via
    # add_quantum_executable().
    add_test(
        NAME build_example_or_circuit
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                --target example_or_circuit)
    set_tests_properties(build_example_or_circuit PROPERTIES
        LABELS "transpiler"
        FIXTURES_SETUP "example_or_circuit_built")

    add_test(
        NAME transpiler_idempotent_example_or_circuit
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_BINARY_DIR}/sturm_gen/examples/or_circuit.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/example_or_circuit_idem
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
    set_tests_properties(transpiler_idempotent_example_or_circuit PROPERTIES
        LABELS "transpiler"
        FIXTURES_REQUIRED "example_or_circuit_built"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()

# ── LP5: example-observability test ─────────────────────────────────────────
# PRD acceptance criterion #3: after a clean build that routes
# examples/or_circuit.cpp through sturm-transpile, the generated sibling
# under ${CMAKE_BINARY_DIR}/sturm_gen/examples/or_circuit.cpp must contain
# exactly one injected `uncompute_or(c, a, b);` call, AND the source file
# must remain untouched (the transpiler never rewrites its input in
# place). This test pins the widened matcher (LP4) to the real example —
# the hermetic snapshot proves the matcher peels the lazy chain on a
# mock, this test proves the same matcher fires under the real build.
#
# Fixture reuse: the generated file is produced by
# add_quantum_executable(example_or_circuit ...). We reuse LP6's
# `build_example_or_circuit` FIXTURES_SETUP test so ctest alone can run
# this check (no prior `cmake --build` required). REQUIRED_FILES ties the
# test to the sturm-transpile binary for consistency with the rest of the
# transpiler test suite, even though the fixture already ensures the
# binary is built and the generated file is produced.
if(STURM_TRANSPILE)
    add_test(
        NAME transpiler_example_or_circuit_injected
        COMMAND "${CMAKE_COMMAND}"
                -DSOURCE=${CMAKE_SOURCE_DIR}/examples/or_circuit.cpp
                -DGENERATED=${CMAKE_BINARY_DIR}/sturm_gen/examples/or_circuit.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_or_circuit.cmake)
    set_tests_properties(transpiler_example_or_circuit_injected PROPERTIES
        LABELS "transpiler"
        FIXTURES_REQUIRED "example_or_circuit_built"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()

# ── Phase B: end-to-end injected observability + idempotency for the ───────
# constant-arith example. Same harness pattern as LP5/LP6 above:
#   1. build_example_constant_arith — SETUP fixture that builds the
#      example_constant_arith target. Transitively produces
#      ${CMAKE_BINARY_DIR}/sturm_gen/examples/constant_arith.cpp via
#      add_quantum_executable().
#   2. transpiler_example_constant_arith_injected — reads the generated
#      file and asserts each of the four PB inverses is present in LIFO
#      order, and that the source example was not rewritten in place.
#   3. transpiler_idempotent_example_constant_arith — re-running
#      sturm-transpile on the already-emitted sibling must yield a
#      byte-identical copy (PRD acceptance #5).
if(STURM_TRANSPILE)
    add_test(
        NAME build_example_constant_arith
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                --target example_constant_arith)
    set_tests_properties(build_example_constant_arith PROPERTIES
        LABELS "transpiler"
        FIXTURES_SETUP "example_constant_arith_built")

    add_test(
        NAME transpiler_example_constant_arith_injected
        COMMAND "${CMAKE_COMMAND}"
                -DSOURCE=${CMAKE_SOURCE_DIR}/examples/constant_arith.cpp
                -DGENERATED=${CMAKE_BINARY_DIR}/sturm_gen/examples/constant_arith.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_constant_arith.cmake)
    set_tests_properties(transpiler_example_constant_arith_injected PROPERTIES
        LABELS "transpiler"
        FIXTURES_REQUIRED "example_constant_arith_built"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME transpiler_idempotent_example_constant_arith
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_BINARY_DIR}/sturm_gen/examples/constant_arith.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/example_constant_arith_idem
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
    set_tests_properties(transpiler_idempotent_example_constant_arith PROPERTIES
        LABELS "transpiler"
        FIXTURES_REQUIRED "example_constant_arith_built"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()

# ── Phase C: end-to-end injected observability + idempotency for the ───────
# qint-qint arith example. Same harness pattern as the Phase B block above:
#   1. build_example_qint_arith — SETUP fixture that builds the
#      example_qint_arith target. Transitively produces
#      ${CMAKE_BINARY_DIR}/sturm_gen/examples/qint_arith.cpp via
#      add_quantum_executable().
#   2. transpiler_example_qint_arith_injected — reads the generated file
#      and asserts each of the five PC-1..PC-5 `uncompute_*_qint(a, b);`
#      calls is present in LIFO order, and that the source example was
#      not rewritten in place.
#   3. transpiler_idempotent_example_qint_arith — re-running
#      sturm-transpile on the already-emitted sibling must yield a
#      byte-identical copy (PRD acceptance #5).
if(STURM_TRANSPILE)
    add_test(
        NAME build_example_qint_arith
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                --target example_qint_arith)
    set_tests_properties(build_example_qint_arith PROPERTIES
        LABELS "transpiler"
        FIXTURES_SETUP "example_qint_arith_built")

    add_test(
        NAME transpiler_example_qint_arith_injected
        COMMAND "${CMAKE_COMMAND}"
                -DSOURCE=${CMAKE_SOURCE_DIR}/examples/qint_arith.cpp
                -DGENERATED=${CMAKE_BINARY_DIR}/sturm_gen/examples/qint_arith.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_qint_arith.cmake)
    set_tests_properties(transpiler_example_qint_arith_injected PROPERTIES
        LABELS "transpiler"
        FIXTURES_REQUIRED "example_qint_arith_built"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME transpiler_idempotent_example_qint_arith
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_BINARY_DIR}/sturm_gen/examples/qint_arith.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/example_qint_arith_idem
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
    set_tests_properties(transpiler_idempotent_example_qint_arith PROPERTIES
        LABELS "transpiler"
        FIXTURES_REQUIRED "example_qint_arith_built"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
endif()

# ── Phase D: end-to-end injected observability + idempotency for the ───────
# qint-qint comparison example. Same harness pattern as the Phase C block
# above:
#   1. build_example_comparison — SETUP fixture that builds the
#      example_comparison target. Transitively produces
#      ${CMAKE_BINARY_DIR}/sturm_gen/examples/comparison.cpp via
#      add_quantum_executable().
#   2. transpiler_example_comparison_injected — reads the generated file
#      and asserts each of the six PD-1..PD-6
#      `uncompute_*_qint(c_xx, a, b);` calls is present in LIFO order,
#      and that the source example is byte-identical (no injected
#      statement signature in place).
#   3. transpiler_idempotent_example_comparison — re-running
#      sturm-transpile on the already-emitted sibling must yield a
#      byte-identical copy (PRD acceptance #5).
if(STURM_TRANSPILE)
    add_test(
        NAME build_example_comparison
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                --target example_comparison)
    set_tests_properties(build_example_comparison PROPERTIES
        LABELS "transpiler"
        FIXTURES_SETUP "example_comparison_built")

    add_test(
        NAME transpiler_example_comparison_injected
        COMMAND "${CMAKE_COMMAND}"
                -DSOURCE=${CMAKE_SOURCE_DIR}/examples/comparison.cpp
                -DGENERATED=${CMAKE_BINARY_DIR}/sturm_gen/examples/comparison.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_comparison.cmake)
    set_tests_properties(transpiler_example_comparison_injected PROPERTIES
        LABELS "transpiler"
        FIXTURES_REQUIRED "example_comparison_built"
        REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

    add_test(
        NAME transpiler_idempotent_example_comparison
        COMMAND "${CMAKE_COMMAND}"
                -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
                -DINPUT=${CMAKE_BINARY_DIR}/sturm_gen/examples/comparison.cpp
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/example_comparison_idem
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
    set_tests_properties(transpiler_idempotent_example_comparison PROPERTIES
        LABELS "transpiler"
        FIXTURES_REQUIRED "example_comparison_built"
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
