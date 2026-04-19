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
# C language keeps CMake from complaining about the harness target.
enable_language(C)

add_subdirectory(test_cmake_glue)

# ── M11: snapshot fixture (sturm-transpile or_single.cpp) ───────────────────
# The test requires the sturm-transpile binary, which is built as part of
# the default target set.
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

# ── Phase A / PA-1: snapshot fixture for `qbool tmp = ~a;` ─────────────────
# Self-inverse NOT: the emitted inverse is `tmp = ~tmp;`, re-applying the
# same X to the result qubit. Same harness / driver as snapshot_or_single;
# only the fixture and golden change.
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

# ── Phase A / PA-2: snapshot fixture for `qbool tmp = a ^ b;` ──────────────
# Self-inverse XOR: the emitted inverse is the two-line `tmp ^= a;`
# followed by `tmp ^= b;`, together undoing the forward op because
# (a^b)^a^b == 0. No library helper is required.
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

# ── Phase A / PA-3: snapshot fixture for `a ^= b;` (quantum RHS) ───────────
# XOR-assign is self-adjoint: the emitted inverse is a verbatim
# re-emission of the same compound-assignment statement. First fixture
# in Phase A that exercises a statement-level matcher rather than a
# VarDecl initializer.
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

# ── Phase A / PA-4: snapshot fixture for `a ^= 1;` (classical RHS) ─────────
# Classical-operand form: the matcher extracts the literal text via
# Lexer::getSourceText and embeds it verbatim in the IR. The render
# switch is unchanged from PA-3 — the operand name happens to be a
# numeric literal rather than a variable name, but the emission is
# textually identical.
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

# ── Phase B / PB-1..PB-4: snapshot fixtures for classical-const compound ────
# assigns on qint_t. Each covers one operator (+= -= *= /=) whose RHS is a
# classical integer literal lifted through the qint_t(long long) converting
# constructor; the emitted inverse is the dual operator over the same
# verbatim RHS token (+↔-, *↔/). Driver and harness are the same as the
# Phase A snapshot tests — only the fixture / golden pair changes.
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

# ── Phase C / PC-1..PC-5: snapshot fixtures for qint-qint compound assigns ──
# Each covers one operator (+= -= *= /= %=) whose RHS is a bare DeclRefExpr
# to a second qint_t parameter (no converting constructor fires). The
# emitted inverse is a `uncompute_{add,sub,mul,div,mod}_qint(lhs, rhs);`
# free-function call from include/sturm/uncompute/uncompute_api.hpp. Driver
# and harness are identical to the Phase B snapshot tests — only the
# fixture / golden pair changes.
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

# ── Phase D / PD-1..PD-6: snapshot fixtures for qint-qint comparisons ───────
# Each covers one relational operator (== != < <= > >=) whose operands are
# bare DeclRefExprs to two qint_t parameters, anchored under a `qbool c =
# ...;` VarDecl initializer. The emitted inverse is a
# `uncompute_{eq,ne,lt,le,gt,ge}_qint(c, a, b);` free-function call declared
# in include/sturm/uncompute/uncompute_api.hpp — three operands (result
# qbool + two qint inputs), unlike the Phase C compound-assign inverses
# which take two (lhs + rhs only). Driver and harness are identical to the
# Phase C snapshot tests — only the fixture / golden pair changes.
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

# ── Phase E / PE-4: snapshot fixtures for compound qbool bitwise exprs ─────
# Three fixtures exercising the compound matcher's expression-tree
# flattening:
#   * compound_or_and   — `qbool r = (b | c) & d;` (roadmap example)
#   * compound_and_or   — `qbool r = (b & c) | d;` (mirror shape)
#   * compound_nested_or — `qbool r = (a | b) | (c | d);` (both arms nested)
#
# Each snapshot_* test transpiles the fixture and byte-compares the output
# against its .expected.cpp golden. A sibling transpiler_idempotent_*
# test feeds the generated file back through sturm-transpile and asserts
# byte-for-byte equality — the AUTO-GENERATED sentinel triggers the skip
# path in main.cpp's driver, so the invariant is trivially preserved
# today, but the explicit check regresses any future widening that
# might accidentally re-transpile already-generated output.
add_test(
    NAME snapshot_compound_or_and
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compound_or_and.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compound_or_and.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/compound_or_and
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_compound_or_and PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_compound_or_and
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/compound_or_and/compound_or_and.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/compound_or_and_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_compound_or_and PROPERTIES
    LABELS "transpiler"
    # Snapshot test produces the generated file the idempotency
    # check consumes, so sequence them via DEPENDS.
    DEPENDS "snapshot_compound_or_and"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_compound_and_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compound_and_or.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compound_and_or.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/compound_and_or
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_compound_and_or PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_compound_and_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/compound_and_or/compound_and_or.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/compound_and_or_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_compound_and_or PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_compound_and_or"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_compound_nested_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compound_nested_or.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compound_nested_or.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/compound_nested_or
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_compound_nested_or PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_compound_nested_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/compound_nested_or/compound_nested_or.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/compound_nested_or_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_compound_nested_or PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_compound_nested_or"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase F / PF-3: snapshot fixtures for single-op WHEN lift ──────────────
# Two fixtures exercising the WHEN-lift matcher's three-point rewrite:
#   * when_single_or         — `WHEN(b | c) { body }` compound lift
#   * when_named_passthrough — `WHEN(named_qbool) { body }` inert path
#
# The single-op fixture pins down the three scheduled edits (decl block
# before the WHEN macro, argument replacement inside WHEN(...), and
# uncompute call after the body close brace) as a byte-level golden.
# The named-passthrough fixture pins the short-circuit invariant — the
# transpiler's output for a bare DeclRefExpr argument must be identical
# to the input modulo the AUTO-GENERATED header (no rewrite at all).
# Both tests follow the same harness/driver shape as the Phase E
# snapshot_compound_* tests; the idempotency companion feeds the
# generated file back through sturm-transpile and asserts a byte-for-byte
# round-trip via the AUTO-GENERATED sentinel.
add_test(
    NAME snapshot_when_single_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_single_or.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_single_or.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_single_or
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_when_single_or PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_when_single_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/when_single_or/when_single_or.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_single_or_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_when_single_or PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_when_single_or"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_when_named_passthrough
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_named_passthrough.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_named_passthrough.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_named_passthrough
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_when_named_passthrough PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_when_named_passthrough
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/when_named_passthrough/when_named_passthrough.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_named_passthrough_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_when_named_passthrough PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_when_named_passthrough"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase F / PF-4: snapshot fixtures for compound + comparator WHEN lifts ──
# Two fixtures exercising the PF-4 widening of `flatten_when_arg`:
#   * when_compound — `WHEN((b | c) & d) { body }` — the exact roadmap
#     shape, pinning the LIFO-order requirement on compound lifts. Two
#     ops share a single `stmt_range` (the WHEN macro's expansion range),
#     so the emitted `uncompute_and` must precede `uncompute_or` — the
#     tiebreaker comes from `std::stable_sort` in `uncompute_pass.cpp`
#     and from the innermost-first → outermost-last callback-append
#     order in `flatten_when_arg`.
#   * when_compare — `WHEN(a == b) { body }` — exercises the Phase D
#     comparator `CXXOperatorCallExpr` branch of `flatten_when_arg`.
#     Emits a single `EQ_QINT` op with matching `uncompute_eq_qint`
#     inverse via the existing renderer switch in `uncompute_pass.cpp`.
#
# Both tests follow the same harness/driver shape as PF-3's
# snapshot_when_single_or / snapshot_when_named_passthrough tests; the
# idempotency companion feeds the generated file back through
# sturm-transpile and asserts byte-identical round-trip via the
# AUTO-GENERATED sentinel (no new lifts fire on the already-rewritten
# `WHEN(__stu_tN)` named-passthrough shape).
add_test(
    NAME snapshot_when_compound
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_compound.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_compound.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_compound
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_when_compound PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_when_compound
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/when_compound/when_compound.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_compound_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_when_compound PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_when_compound"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_when_compare
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_compare.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_compare.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_compare
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_when_compare PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_when_compare
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/when_compare/when_compare.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_compare_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_when_compare PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_when_compare"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase F / PF-5: snapshot fixture for nested-WHEN passthrough ────────────
# Single fixture covering `WHEN(a) { WHEN(b | c) { body } }`: two WHEN
# invocations produce two independent three-`if` towers. The PF-2 matcher
# fires on each middle `_when_val_` IfStmt independently — the outer
# takes the named-passthrough short-circuit (peeled payload is a bare
# DeclRefExpr to `a`), the inner performs the full compound lift
# (flat `qbool __stu_t0 = b | c;` before the inner WHEN, argument
# rewritten to `__stu_t0`, `uncompute_or(__stu_t0, b, c);` after the
# INNER body's `}`).
#
# The load-bearing invariant this test pins is per-WHEN identity: the
# inner lift's post-body brace must anchor on the INNER body's `}`, not
# the outer's. `compute_post_body_brace` descends `when_if->getThen()`
# twice from the matched tower's middle IfStmt, so each WHEN's brace is
# unique to its own tower — a naive shared-state implementation that
# tracked only the latest post-body brace across matcher runs would
# misplace the `uncompute_or` after the OUTER `}` and the byte-compare
# would fail.
#
# No matcher changes are expected for PF-5 — PF-3/PF-4 already drive
# per-IfStmt identity through `when_if->getThen()` descent. The slice is
# a regression guard + golden snapshot. The idempotency companion
# follows the same pattern as PF-3/PF-4's WHEN snapshots: the
# AUTO-GENERATED sentinel makes re-transpile a verbatim copy.
add_test(
    NAME snapshot_when_nested_passthrough
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_nested_passthrough.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_nested_passthrough.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_nested_passthrough
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_when_nested_passthrough PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_when_nested_passthrough
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/when_nested_passthrough/when_nested_passthrough.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_nested_passthrough_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_when_nested_passthrough PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_when_nested_passthrough"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase G / PG-3: snapshot fixture for nested-WHEN AND-fold base case ────
# Single fixture covering the named+named nested-WHEN base case:
#
#     WHEN(a) { WHEN(b) { (void)c; } }
#
# The Phase G matcher (transpiler/src/matcher_when_nested.cpp) fires
# on the (outer=a, inner=b) pair and performs three staged effects:
#   - Inject `qbool __stu_ctrl0 = a & b;\n` on `raw_insertions` anchored
#     at the inner WHEN's expansion loc.
#   - Replace the inner `materialize_when` arg (source range normalised
#     via `Lexer::makeFileCharRange`) with `__stu_ctrl0`.
#   - Push `QOperation{kind=AND, result=__stu_ctrl0, operands=[a,b],
#     insert_before_override=<post_inner_brace>}` onto the enclosing
#     QScope; the uncompute pass renders it as
#     `uncompute_and(__stu_ctrl0, a, b);` at the override anchor.
#
# The idempotency companion is registered here because PG-3 closes the
# loop PG-2 left open: the generated output now has the AUTO-GENERATED
# sentinel in its header, so a re-transpile hits the skip path in
# main.cpp and returns a byte-identical copy.
add_test(
    NAME snapshot_when_nested_named
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_nested_named.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_nested_named.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_nested_named
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_when_nested_named PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# Idempotency companion: PG-3 closes the loop that PG-2 left open —
# the generated output now contains a full `uncompute_and(...)` call
# AND the AUTO-GENERATED sentinel in the header, so a re-transpile
# hits the skip path in main.cpp and returns a byte-identical copy.
# Locked down here so a regression in the sentinel / skip recognition
# surfaces as a dedicated failure rather than blending into the
# snapshot byte-compare.
add_test(
    NAME transpiler_idempotent_when_nested_named
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/when_nested_named/when_nested_named.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_nested_named_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_when_nested_named PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_when_nested_named"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase G / PG-3: snapshot fixture for depth-3 pairwise cascade ───────────
# Depth-3 nested named+named WHEN:
#
#     WHEN(a) { WHEN(b) { WHEN(c) { (void)d; } } }
#
# The Phase G matcher fires on BOTH adjacent pairs independently and
# allocates `__stu_ctrl0` / `__stu_ctrl1` from a single persistent
# `FreshNameAllocator` owned by the callback. The expected output has:
#   - two `qbool __stu_ctrl<M> = ...;` decls (one per pair);
#   - two inner-arg rewrites (the middle and innermost WHENs now
#     reference their respective ctrl temps);
#   - two `uncompute_and(...)` calls, each anchored immediately past its
#     pair's inner-body closing brace.
# The `(a, c)` transitive pair is deliberately NOT emitted — the parent-
# chain walk stops at the nearest enclosing WHEN, so the cascade
# produces pairwise lowerings only.
add_test(
    NAME snapshot_when_nested_cascade
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_nested_cascade.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_nested_cascade.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_nested_cascade
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_when_nested_cascade PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_when_nested_cascade
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/when_nested_cascade/when_nested_cascade.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_nested_cascade_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_when_nested_cascade PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_when_nested_cascade"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase G / PG-3: snapshot fixture for siblings-around-inner WHEN ─────────
# Pins Phase G decision #3: always lower regardless of siblings. The
# outer WHEN body contains `(void)c;` placeholders both before and
# after the inner WHEN; the matcher still fires on the (a, b) pair and
# the generated output preserves both sibling statements verbatim. Only
# the inner WHEN is rewritten and only its post-body brace carries the
# `uncompute_and` insertion.
add_test(
    NAME snapshot_when_nested_siblings
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_nested_siblings.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/when_nested_siblings.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_nested_siblings
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_when_nested_siblings PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_when_nested_siblings
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/when_nested_siblings/when_nested_siblings.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/when_nested_siblings_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_when_nested_siblings PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_when_nested_siblings"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── LP3: RED snapshot fixture for the conversion-wrapped `|`-initializer ───
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

# ── Phase E / PE-5: end-to-end injected observability + idempotency for ────
# the compound qbool expression example. Same harness pattern as the
# Phase D comparison block above:
#   1. build_example_compound_expression — SETUP fixture that builds the
#      example_compound_expression target. Transitively produces
#      ${CMAKE_BINARY_DIR}/sturm_gen/examples/compound_expression.cpp via
#      add_quantum_executable().
#   2. transpiler_example_compound_expression_injected — reads the
#      generated file and asserts the PE-4 flat decl sequence
#      (`qbool __stu_t0 = b | c;` + `qbool r = __stu_t0 & d;`) appears
#      in place of the original `qbool r = (b | c) & d;` VarDecl, and
#      that the LIFO inverse pair (`uncompute_and(r, __stu_t0, d);`
#      then `uncompute_or(__stu_t0, b, c);`) is injected before the
#      enclosing scope's close brace. Also confirms the source is
#      byte-identical (no injected fragment in place).
#   3. transpiler_idempotent_example_compound_expression — re-running
#      sturm-transpile on the already-emitted sibling must yield a
#      byte-identical copy (PRD acceptance #5).
add_test(
    NAME build_example_compound_expression
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
            --target example_compound_expression)
set_tests_properties(build_example_compound_expression PROPERTIES
    LABELS "transpiler"
    FIXTURES_SETUP "example_compound_expression_built")

add_test(
    NAME transpiler_example_compound_expression_injected
    COMMAND "${CMAKE_COMMAND}"
            -DSOURCE=${CMAKE_SOURCE_DIR}/examples/compound_expression.cpp
            -DGENERATED=${CMAKE_BINARY_DIR}/sturm_gen/examples/compound_expression.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_compound_expression.cmake)
set_tests_properties(transpiler_example_compound_expression_injected PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_compound_expression_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_example_compound_expression
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_BINARY_DIR}/sturm_gen/examples/compound_expression.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/example_compound_expression_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_example_compound_expression PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_compound_expression_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase F / PF-6: end-to-end injected observability + idempotency for ────
# the compound-WHEN integration example. Same harness pattern as the
# Phase E PE-5 compound_expression block above:
#   1. build_example_when_integration — SETUP fixture that builds the
#      example_when_integration target. Transitively produces
#      ${CMAKE_BINARY_DIR}/sturm_gen/examples/when_integration.cpp via
#      add_quantum_executable().
#   2. transpiler_example_when_integration_injected — reads the generated
#      file and asserts the PF-4 three-point rewrite of
#      `WHEN((b | c) & d) { a.value ^= 1; }`:
#        - two flat `qbool __stu_tN = ...;` decls injected BEFORE the
#          WHEN line (inner `b | c` → `__stu_t0`, outer `__stu_t0 & d`
#          → `__stu_t1`),
#        - WHEN argument substituted to `__stu_t1`,
#        - LIFO `uncompute_and(__stu_t1, __stu_t0, d); /
#          uncompute_or(__stu_t0, b, c);` pair planted directly after
#          the WHEN body's `}`.
#      Also confirms the source is byte-identical (no injected fragment
#      leaks into the user's hand-written main body) — the "no-leak"
#      search anchors after `int main()` to avoid false-positives on
#      the top-of-file comment block that discusses the rewrite shape
#      by bare name.
#   3. transpiler_idempotent_example_when_integration — re-running
#      sturm-transpile on the already-emitted sibling must yield a
#      byte-identical copy (PRD acceptance #5).
add_test(
    NAME build_example_when_integration
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
            --target example_when_integration)
set_tests_properties(build_example_when_integration PROPERTIES
    LABELS "transpiler"
    FIXTURES_SETUP "example_when_integration_built")

add_test(
    NAME transpiler_example_when_integration_injected
    COMMAND "${CMAKE_COMMAND}"
            -DSOURCE=${CMAKE_SOURCE_DIR}/examples/when_integration.cpp
            -DGENERATED=${CMAKE_BINARY_DIR}/sturm_gen/examples/when_integration.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_when_integration.cmake)
set_tests_properties(transpiler_example_when_integration_injected PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_when_integration_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_example_when_integration
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_BINARY_DIR}/sturm_gen/examples/when_integration.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/example_when_integration_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_example_when_integration PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_when_integration_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase G / PG-8: end-to-end injected observability + idempotency for ────
# the named-named nested-WHEN example.  Same harness pattern as the
# Phase F PF-6 when_integration block above:
#   1. build_example_nested_when — SETUP fixture that builds the
#      example_nested_when target.  Transitively produces
#      ${CMAKE_BINARY_DIR}/sturm_gen/examples/nested_when.cpp via
#      add_quantum_executable().
#   2. transpiler_example_nested_when_injected — reads the generated
#      file and asserts the Phase G PG-2 / PG-3 rewrite for both
#      nested-WHEN cases in main():
#        - Case 1 (`WHEN(a) { WHEN(b) { ... } }`): `qbool
#          __stu_ctrl0 = a & b;` injected between `WHEN(a)` and the
#          rewritten `WHEN(__stu_ctrl0)`, `uncompute_and(__stu_ctrl0,
#          a, b);` planted after the inner body's `}`.
#        - Case 2 (`WHEN(x) { WHEN(y) { WHEN(z) { ... } } }`):
#          pairwise cascade `__stu_ctrl1 = x & y;` +
#          `__stu_ctrl2 = y & z;` with the two `uncompute_and(...)`
#          calls in LIFO cascade order (inner ctrl2 first, then
#          outer ctrl1).
#      Also confirms the source is byte-identical (no injected
#      fragment leaks into the user's hand-written main body) — the
#      "no-leak" search anchors after `int main()` to avoid
#      false-positives on the top-of-file comment block that
#      discusses the rewrite shape by bare name.
#   3. transpiler_idempotent_example_nested_when — re-running
#      sturm-transpile on the already-emitted sibling must yield a
#      byte-identical copy (PRD acceptance #5).
add_test(
    NAME build_example_nested_when
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
            --target example_nested_when)
set_tests_properties(build_example_nested_when PROPERTIES
    LABELS "transpiler"
    FIXTURES_SETUP "example_nested_when_built")

add_test(
    NAME transpiler_example_nested_when_injected
    COMMAND "${CMAKE_COMMAND}"
            -DSOURCE=${CMAKE_SOURCE_DIR}/examples/nested_when.cpp
            -DGENERATED=${CMAKE_BINARY_DIR}/sturm_gen/examples/nested_when.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_nested_when.cmake)
set_tests_properties(transpiler_example_nested_when_injected PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_nested_when_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_example_nested_when
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_BINARY_DIR}/sturm_gen/examples/nested_when.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/example_nested_when_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_example_nested_when PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_nested_when_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase H / PH-5: end-to-end injected observability + idempotency for ────
# the classical control-flow example.  Same harness pattern as the
# Phase G PG-8 nested_when block above:
#   1. build_example_control_flow — SETUP fixture that builds the
#      example_control_flow target.  Transitively produces
#      ${CMAKE_BINARY_DIR}/sturm_gen/examples/control_flow.cpp via
#      add_quantum_executable().
#   2. transpiler_example_control_flow_injected — reads the generated
#      file and asserts the Phase H per-iteration / per-branch
#      uncompute lowering for both control-flow cases in main():
#        - Case 1 (`for (int i = 0; i < 3; ++i) { qbool tmp = a | b; }`):
#          `uncompute_or(tmp, a, b);` planted before the loop body's
#          `}` — fires once per iteration at run time.
#        - Case 2 (`if (cond) { qbool x = a | b; } else { qbool y = a | b; }`):
#          distinct per-branch intermediates, each with its own
#          `uncompute_or(<var>, a, b);` planted before that branch's
#          `}` — the then-arm's uncompute precedes the else-arm's in
#          source order.
#      Also confirms the source is byte-identical (no injected
#      fragment leaks into the user's hand-written main body) — the
#      "no-leak" search anchors after `int main()` to avoid
#      false-positives on the top-of-file comment block that
#      discusses the rewrite shape by bare name.
#   3. transpiler_idempotent_example_control_flow — re-running
#      sturm-transpile on the already-emitted sibling must yield a
#      byte-identical copy (PRD acceptance #5).
add_test(
    NAME build_example_control_flow
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
            --target example_control_flow)
set_tests_properties(build_example_control_flow PROPERTIES
    LABELS "transpiler"
    FIXTURES_SETUP "example_control_flow_built")

add_test(
    NAME transpiler_example_control_flow_injected
    COMMAND "${CMAKE_COMMAND}"
            -DSOURCE=${CMAKE_SOURCE_DIR}/examples/control_flow.cpp
            -DGENERATED=${CMAKE_BINARY_DIR}/sturm_gen/examples/control_flow.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_control_flow.cmake)
set_tests_properties(transpiler_example_control_flow_injected PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_control_flow_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_example_control_flow
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_BINARY_DIR}/sturm_gen/examples/control_flow.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/example_control_flow_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_example_control_flow PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_control_flow_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase I / PI-6: end-to-end injected observability + idempotency for ───
# the user-defined-routine example.  Same harness pattern as the
# Phase H PH-5 control_flow block above:
#   1. build_example_user_routine — SETUP fixture that builds the
#      example_user_routine target.  Transitively produces
#      ${CMAKE_BINARY_DIR}/sturm_gen/examples/user_routine.cpp via
#      add_quantum_executable().
#   2. transpiler_example_user_routine_injected — reads the generated
#      file and asserts the PI-2/PI-3/PI-4 rewrite for both routine
#      calls in the example:
#        - Case 1 (local intermediate — `qbool tmp;` + `rotate_by_k(tmp,
#          a, 3);` inside main()'s inner scope): PI-4 plants
#          `invert(rotate_by_k)(tmp, a, 3);` before the scope's `}`.
#        - Case 2 (escaping output — `rotate_by_k(out, a, 3);` inside
#          `apply_rotation` where `out` is a ParmVarDecl): PI-3
#          classifies the output as `Final` and PI-4 emits no inverse.
#      Also confirms the source is byte-identical (no injected
#      fragment leaks into the user's hand-written main body) — the
#      "no-leak" search anchors after `int main()` to avoid
#      false-positives on the top-of-file comment block that
#      discusses the rewrite shape by bare name.
#   3. transpiler_idempotent_example_user_routine — re-running
#      sturm-transpile on the already-emitted sibling must yield a
#      byte-identical copy (PRD acceptance #5).
add_test(
    NAME build_example_user_routine
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
            --target example_user_routine)
set_tests_properties(build_example_user_routine PROPERTIES
    LABELS "transpiler"
    FIXTURES_SETUP "example_user_routine_built")

add_test(
    NAME transpiler_example_user_routine_injected
    COMMAND "${CMAKE_COMMAND}"
            -DSOURCE=${CMAKE_SOURCE_DIR}/examples/user_routine.cpp
            -DGENERATED=${CMAKE_BINARY_DIR}/sturm_gen/examples/user_routine.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_user_routine.cmake)
set_tests_properties(transpiler_example_user_routine_injected PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_user_routine_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_example_user_routine
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_BINARY_DIR}/sturm_gen/examples/user_routine.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/example_user_routine_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_example_user_routine PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_user_routine_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase J / PJ-1h: end-to-end injected observability + idempotency for ──
# the zero-ancilla fusion example.  Same harness pattern as the
# Phase I PI-6 user_routine block above:
#   1. build_example_zero_ancilla_fusion — SETUP fixture that builds
#      the example_zero_ancilla_fusion target.  Transitively produces
#      ${CMAKE_BINARY_DIR}/sturm_gen/examples/zero_ancilla_fusion.cpp
#      via add_quantum_executable().
#   2. transpiler_example_zero_ancilla_fusion_injected — reads the
#      generated file and asserts the PJ-1d two-point fusion rewrite
#      for the happy-path pair inside main()'s inner scope:
#        - Forward fused call: PJ-1d collapses the source pair
#          `qbool __t = a & b;` + `x ^= __t;` into a single
#          `ccnot_inplace(x, a, b);` via a QReplacement whose source
#          range spans both stmts.
#        - Self-adjoint uncompute: the PJ-1c render case in
#          transpiler/src/uncompute_pass.cpp plants a SECOND
#          `ccnot_inplace(x, a, b);` before the enclosing scope's
#          closing `}`.  CCX is its own inverse, so forward +
#          uncompute share one symbol.
#        - Neither the original `qbool __t` decl nor the `x ^= __t`
#          stmt survives inside main()'s body — the QReplacement
#          strip over the fused source range must be complete.
#      Also confirms the source is byte-identical (no injected
#      fragment leaks into the user's hand-written main body) — the
#      "no-leak" search anchors after `int main()` to avoid
#      false-positives on the top-of-file comment block that
#      discusses the rewrite shape by bare name.
#   3. transpiler_idempotent_example_zero_ancilla_fusion — re-running
#      sturm-transpile on the already-emitted sibling must yield a
#      byte-identical copy (PRD acceptance #5).
add_test(
    NAME build_example_zero_ancilla_fusion
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
            --target example_zero_ancilla_fusion)
set_tests_properties(build_example_zero_ancilla_fusion PROPERTIES
    LABELS "transpiler"
    FIXTURES_SETUP "example_zero_ancilla_fusion_built")

add_test(
    NAME transpiler_example_zero_ancilla_fusion_injected
    COMMAND "${CMAKE_COMMAND}"
            -DSOURCE=${CMAKE_SOURCE_DIR}/examples/zero_ancilla_fusion.cpp
            -DGENERATED=${CMAKE_BINARY_DIR}/sturm_gen/examples/zero_ancilla_fusion.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_zero_ancilla_fusion.cmake)
set_tests_properties(transpiler_example_zero_ancilla_fusion_injected PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_zero_ancilla_fusion_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_example_zero_ancilla_fusion
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_BINARY_DIR}/sturm_gen/examples/zero_ancilla_fusion.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/example_zero_ancilla_fusion_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_example_zero_ancilla_fusion PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_zero_ancilla_fusion_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase J / PJ-4d: end-to-end injected observability + idempotency for ──
# the dead-ancilla elimination example.  Same harness pattern as the
# PJ-1h zero_ancilla_fusion block above and the PI-6 user_routine block
# further up:
#   1. build_example_dead_ancilla — SETUP fixture that builds the
#      example_dead_ancilla target.  Transitively produces
#      ${CMAKE_BINARY_DIR}/sturm_gen/examples/dead_ancilla.cpp via
#      add_quantum_executable().
#   2. transpiler_example_dead_ancilla_injected — reads the generated
#      file and asserts both PJ-4a scenarios in main():
#        - Case 1 (happy path): the `dead` scope's
#          `qbool dead = a | b;` VarDecl has been REPLACED by empty
#          text (no survival inside main()'s body), and NO
#          `uncompute_or(dead, a, b);` is planted at scope close —
#          PJ-4a's `eliminated_stmt_ranges` entry suppresses the MVP
#          OR matcher via `apply_eliminated_stmt_guards`.
#        - Case 2 (reader-reject): the `live` scope's
#          `qbool live = a | b;` + `x ^= live;` forward pair is
#          preserved verbatim, a second PA-3 LIFO self-adjoint
#          `x ^= live;` inverse is planted, and an MVP OR
#          `uncompute_or(live, a, b);` lands at scope close.
#      Also confirms the source is byte-identical (no injected
#      fragment leaks into the user's hand-written main body) — the
#      "no-leak" search anchors after `int main()` to avoid
#      false-positives on the top-of-file comment block that
#      discusses the rewrite shape by bare name.
#   3. transpiler_idempotent_example_dead_ancilla — re-running
#      sturm-transpile on the already-emitted sibling must yield a
#      byte-identical copy (PRD acceptance #5).
add_test(
    NAME build_example_dead_ancilla
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
            --target example_dead_ancilla)
set_tests_properties(build_example_dead_ancilla PROPERTIES
    LABELS "transpiler"
    FIXTURES_SETUP "example_dead_ancilla_built")

add_test(
    NAME transpiler_example_dead_ancilla_injected
    COMMAND "${CMAKE_COMMAND}"
            -DSOURCE=${CMAKE_SOURCE_DIR}/examples/dead_ancilla.cpp
            -DGENERATED=${CMAKE_BINARY_DIR}/sturm_gen/examples/dead_ancilla.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_dead_ancilla.cmake)
set_tests_properties(transpiler_example_dead_ancilla_injected PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_dead_ancilla_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_example_dead_ancilla
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_BINARY_DIR}/sturm_gen/examples/dead_ancilla.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/example_dead_ancilla_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_example_dead_ancilla PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_dead_ancilla_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase J / PJ-3g: end-to-end injected observability + idempotency for ──
# the uncompute-hoisting example.  Same harness pattern as the PJ-4d
# dead_ancilla block above and the PJ-1h zero_ancilla_fusion block:
#   1. build_example_uncompute_hoisting — SETUP fixture that builds the
#      example_uncompute_hoisting target.  Transitively produces
#      ${CMAKE_BINARY_DIR}/sturm_gen/examples/uncompute_hoisting.cpp
#      via add_quantum_executable().
#   2. transpiler_example_uncompute_hoisting_injected — reads the
#      generated file and asserts the PJ-3d rewrite inside main()'s
#      inner scope:
#        - Forward preserved: `qbool t = a | b;` inside the for-loop
#          body is NOT moved (PJ-3d only relocates the uncompute
#          anchor; forward text-move is PJ-3g+).
#        - Uncompute hoisted: `uncompute_or(t, a, b);` lands AFTER
#          the for-loop's closing `}` at the ENCLOSING scope's close
#          brace, rather than immediately before the for-body's `}`.
#          This pins the load-bearing PJ-3d semantic that
#          `op.hoist_to_override` takes precedence over
#          `scope.close_brace` in uncompute_pass.cpp's anchor-
#          selection ladder.
#      Also confirms the source is byte-identical (no injected
#      fragment leaks into the user's hand-written main body) — the
#      "no-leak" search anchors after `int main()` to avoid
#      false-positives on the top-of-file comment block that
#      discusses the rewrite shape by bare name.
#   3. transpiler_idempotent_example_uncompute_hoisting — re-running
#      sturm-transpile on the already-emitted sibling must yield a
#      byte-identical copy (PRD acceptance #5).
add_test(
    NAME build_example_uncompute_hoisting
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
            --target example_uncompute_hoisting)
set_tests_properties(build_example_uncompute_hoisting PROPERTIES
    LABELS "transpiler"
    FIXTURES_SETUP "example_uncompute_hoisting_built")

add_test(
    NAME transpiler_example_uncompute_hoisting_injected
    COMMAND "${CMAKE_COMMAND}"
            -DSOURCE=${CMAKE_SOURCE_DIR}/examples/uncompute_hoisting.cpp
            -DGENERATED=${CMAKE_BINARY_DIR}/sturm_gen/examples/uncompute_hoisting.cpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_example_uncompute_hoisting.cmake)
set_tests_properties(transpiler_example_uncompute_hoisting_injected PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_uncompute_hoisting_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_example_uncompute_hoisting
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_BINARY_DIR}/sturm_gen/examples/uncompute_hoisting.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/example_uncompute_hoisting_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_example_uncompute_hoisting PROPERTIES
    LABELS "transpiler"
    FIXTURES_REQUIRED "example_uncompute_hoisting_built"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

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
# The test depends on the sturm-transpile target for the transpile step.
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

# ── PG-7: second transpile step for the Phase G nested-WHEN ────────────
# fixture pair.  Same driver invocation pattern as the or_single
# block above — routes `fixtures/nested_when_runtime.cpp` through
# sturm-transpile so the Phase G nested-WHEN lowering is applied
# before the generated TU is compiled into the single
# test_gate_equivalence_or_single harness binary.  One binary,
# three pairs: or_single, or_circuit, nested_when.
set(_m12_nested_runtime_src
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/nested_when_runtime.cpp")
file(RELATIVE_PATH _m12_nested_rel_src
    "${CMAKE_SOURCE_DIR}" "${_m12_nested_runtime_src}")
set(_m12_nested_generated
    "${CMAKE_BINARY_DIR}/sturm_gen/${_m12_nested_rel_src}")
get_filename_component(_m12_nested_gen_dir
    "${_m12_nested_generated}" DIRECTORY)

add_custom_command(
    OUTPUT "${_m12_nested_generated}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_m12_nested_gen_dir}"
    COMMAND $<TARGET_FILE:sturm-transpile>
            "${_m12_nested_rel_src}"
            --output-dir "${CMAKE_BINARY_DIR}/sturm_gen"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${_m12_nested_runtime_src}" sturm-transpile
    COMMENT "sturm-transpile ${_m12_nested_rel_src} (PG-7)"
    VERBATIM)
set_source_files_properties("${_m12_nested_generated}" PROPERTIES
    GENERATED TRUE)

# ── PH-6b: third transpile step for the Phase H if/else gate- ──────────
# equivalence pair.  Same driver invocation pattern as the blocks
# above — routes `fixtures/if_branches_runtime.cpp` through
# sturm-transpile so the OR matcher's per-branch uncompute
# injection is applied before the generated TU is compiled into the
# single test_gate_equivalence_or_single harness binary.  One
# binary, four pairs: or_single, or_circuit, nested_when,
# if_branches.
set(_m12_if_branches_runtime_src
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/if_branches_runtime.cpp")
file(RELATIVE_PATH _m12_if_branches_rel_src
    "${CMAKE_SOURCE_DIR}" "${_m12_if_branches_runtime_src}")
set(_m12_if_branches_generated
    "${CMAKE_BINARY_DIR}/sturm_gen/${_m12_if_branches_rel_src}")
get_filename_component(_m12_if_branches_gen_dir
    "${_m12_if_branches_generated}" DIRECTORY)

add_custom_command(
    OUTPUT "${_m12_if_branches_generated}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_m12_if_branches_gen_dir}"
    COMMAND $<TARGET_FILE:sturm-transpile>
            "${_m12_if_branches_rel_src}"
            --output-dir "${CMAKE_BINARY_DIR}/sturm_gen"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${_m12_if_branches_runtime_src}" sturm-transpile
    COMMENT "sturm-transpile ${_m12_if_branches_rel_src} (PH-6b)"
    VERBATIM)
set_source_files_properties("${_m12_if_branches_generated}" PROPERTIES
    GENERATED TRUE)

# ── PH-6a: fourth transpile step for the Phase H for-loop gate- ────────
# equivalence pair.  Same driver invocation pattern as the blocks
# above — routes `fixtures/for_loop_runtime.cpp` through
# sturm-transpile so the OR matcher's per-iteration uncompute
# injection is applied before the generated TU is compiled into the
# single test_gate_equivalence_or_single harness binary.  One
# binary, five pairs: or_single, or_circuit, nested_when,
# if_branches, for_loop.
set(_m12_for_loop_runtime_src
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/for_loop_runtime.cpp")
file(RELATIVE_PATH _m12_for_loop_rel_src
    "${CMAKE_SOURCE_DIR}" "${_m12_for_loop_runtime_src}")
set(_m12_for_loop_generated
    "${CMAKE_BINARY_DIR}/sturm_gen/${_m12_for_loop_rel_src}")
get_filename_component(_m12_for_loop_gen_dir
    "${_m12_for_loop_generated}" DIRECTORY)

add_custom_command(
    OUTPUT "${_m12_for_loop_generated}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_m12_for_loop_gen_dir}"
    COMMAND $<TARGET_FILE:sturm-transpile>
            "${_m12_for_loop_rel_src}"
            --output-dir "${CMAKE_BINARY_DIR}/sturm_gen"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${_m12_for_loop_runtime_src}" sturm-transpile
    COMMENT "sturm-transpile ${_m12_for_loop_rel_src} (PH-6a)"
    VERBATIM)
set_source_files_properties("${_m12_for_loop_generated}" PROPERTIES
    GENERATED TRUE)

# ── PI-7: fifth transpile step for the Phase I user-routine gate- ──────
# equivalence pair.  Same driver invocation pattern as the blocks
# above — routes `fixtures/user_routine_runtime.cpp` through
# sturm-transpile so the Phase I PI-2 / PI-3 / PI-4 user-routine
# + invert() rewrite is applied before the generated TU is compiled
# into the single test_gate_equivalence_or_single harness binary.
# One binary, six pairs: or_single, or_circuit, nested_when,
# if_branches, for_loop, user_routine.
set(_m12_user_routine_runtime_src
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/user_routine_runtime.cpp")
file(RELATIVE_PATH _m12_user_routine_rel_src
    "${CMAKE_SOURCE_DIR}" "${_m12_user_routine_runtime_src}")
set(_m12_user_routine_generated
    "${CMAKE_BINARY_DIR}/sturm_gen/${_m12_user_routine_rel_src}")
get_filename_component(_m12_user_routine_gen_dir
    "${_m12_user_routine_generated}" DIRECTORY)

add_custom_command(
    OUTPUT "${_m12_user_routine_generated}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_m12_user_routine_gen_dir}"
    COMMAND $<TARGET_FILE:sturm-transpile>
            "${_m12_user_routine_rel_src}"
            --output-dir "${CMAKE_BINARY_DIR}/sturm_gen"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${_m12_user_routine_runtime_src}" sturm-transpile
    COMMENT "sturm-transpile ${_m12_user_routine_rel_src} (PI-7)"
    VERBATIM)
set_source_files_properties("${_m12_user_routine_generated}" PROPERTIES
    GENERATED TRUE)

# ── PJ-1i: sixth transpile step for the Phase J zero-ancilla ───────────
# fusion gate-equivalence pair.  Same driver invocation pattern as
# the blocks above — routes `fixtures/fuse_xor_and_runtime.cpp`
# through sturm-transpile so the PJ-1d peephole matcher collapses
# the adjacent `qbool __t = a & b; x ^= __t;` pair into a single
# `ccnot_inplace(x, a, b);` call and the PJ-1c render case plants
# the matching self-adjoint `ccnot_inplace(x, a, b);` before the
# demo's closing `}`.  The resulting TU is compiled into the
# single test_gate_equivalence_or_single harness binary.  One
# binary, seven pairs: or_single, or_circuit, nested_when,
# if_branches, for_loop, user_routine, fuse_xor_and.
set(_m12_fused_runtime_src
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fuse_xor_and_runtime.cpp")
file(RELATIVE_PATH _m12_fused_rel_src
    "${CMAKE_SOURCE_DIR}" "${_m12_fused_runtime_src}")
set(_m12_fused_generated
    "${CMAKE_BINARY_DIR}/sturm_gen/${_m12_fused_rel_src}")
get_filename_component(_m12_fused_gen_dir
    "${_m12_fused_generated}" DIRECTORY)

add_custom_command(
    OUTPUT "${_m12_fused_generated}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_m12_fused_gen_dir}"
    COMMAND $<TARGET_FILE:sturm-transpile>
            "${_m12_fused_rel_src}"
            --output-dir "${CMAKE_BINARY_DIR}/sturm_gen"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${_m12_fused_runtime_src}" sturm-transpile
    COMMENT "sturm-transpile ${_m12_fused_rel_src} (PJ-1i)"
    VERBATIM)
set_source_files_properties("${_m12_fused_generated}" PROPERTIES
    GENERATED TRUE)

# ── PJ-4e: seventh transpile step for the Phase J dead-ancilla ─────────
# elimination gate-equivalence pair.  Same driver invocation pattern
# as the blocks above — routes `fixtures/dead_ancilla_runtime.cpp`
# through sturm-transpile so the PJ-4a peephole matcher eliminates
# the `qbool dead = a | b;` decl (zero-reader condition) and the
# downstream MVP OR matcher's per-decl `uncompute_or(...)` injection
# is suppressed via `eliminated_stmt_ranges`.  The sentinel
# `target.flip()` call survives the rewrite unchanged and emits one
# X(target) record at run time.  The resulting TU is compiled into
# the single test_gate_equivalence_or_single harness binary.  One
# binary, eight pairs: or_single, or_circuit, nested_when,
# if_branches, for_loop, user_routine, fuse_xor_and, dead_ancilla.
set(_m12_dead_ancilla_runtime_src
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dead_ancilla_runtime.cpp")
file(RELATIVE_PATH _m12_dead_ancilla_rel_src
    "${CMAKE_SOURCE_DIR}" "${_m12_dead_ancilla_runtime_src}")
set(_m12_dead_ancilla_generated
    "${CMAKE_BINARY_DIR}/sturm_gen/${_m12_dead_ancilla_rel_src}")
get_filename_component(_m12_dead_ancilla_gen_dir
    "${_m12_dead_ancilla_generated}" DIRECTORY)

add_custom_command(
    OUTPUT "${_m12_dead_ancilla_generated}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_m12_dead_ancilla_gen_dir}"
    COMMAND $<TARGET_FILE:sturm-transpile>
            "${_m12_dead_ancilla_rel_src}"
            --output-dir "${CMAKE_BINARY_DIR}/sturm_gen"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${_m12_dead_ancilla_runtime_src}" sturm-transpile
    COMMENT "sturm-transpile ${_m12_dead_ancilla_rel_src} (PJ-4e)"
    VERBATIM)
set_source_files_properties("${_m12_dead_ancilla_generated}" PROPERTIES
    GENERATED TRUE)

# ── PJ-3h: eighth transpile step for the Phase J uncompute-hoisting ───
# gate-equivalence pair.  Same driver invocation pattern as the
# blocks above — routes `fixtures/hoist_runtime.cpp` through
# sturm-transpile so the PJ-3d `register_hoist_invariant_matcher`
# sets `op.hoist_to_override` on the inner-loop-body `qbool t = a | b;`
# op and the M8 synthesis pass plants the
# `sturm::uncompute_or(t, a, b);` call AT the enclosing scope's
# close brace (AFTER the for-loop's `}`).  The resulting TU is
# compiled into the single test_gate_equivalence_or_single harness
# binary.  One binary, nine pairs: or_single, or_circuit,
# nested_when, if_branches, for_loop, user_routine, fuse_xor_and,
# dead_ancilla, hoist.
set(_m12_hoist_runtime_src
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_runtime.cpp")
file(RELATIVE_PATH _m12_hoist_rel_src
    "${CMAKE_SOURCE_DIR}" "${_m12_hoist_runtime_src}")
set(_m12_hoist_generated
    "${CMAKE_BINARY_DIR}/sturm_gen/${_m12_hoist_rel_src}")
get_filename_component(_m12_hoist_gen_dir
    "${_m12_hoist_generated}" DIRECTORY)

add_custom_command(
    OUTPUT "${_m12_hoist_generated}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_m12_hoist_gen_dir}"
    COMMAND $<TARGET_FILE:sturm-transpile>
            "${_m12_hoist_rel_src}"
            --output-dir "${CMAKE_BINARY_DIR}/sturm_gen"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS "${_m12_hoist_runtime_src}" sturm-transpile
    COMMENT "sturm-transpile ${_m12_hoist_rel_src} (PJ-3h)"
    VERBATIM)
set_source_files_properties("${_m12_hoist_generated}" PROPERTIES
    GENERATED TRUE)

add_executable(test_gate_equivalence_or_single
    test_gate_equivalence.cpp
    "${_m12_generated}"
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/or_single_reference.cpp
    # PG-7: Phase G nested-WHEN transpiled + hand-written reference.
    "${_m12_nested_generated}"
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/nested_when_reference.cpp
    # PH-6b: Phase H if/else transpiled + hand-written reference.
    "${_m12_if_branches_generated}"
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/if_branches_reference.cpp
    # PH-6a: Phase H for-loop transpiled + hand-written reference.
    "${_m12_for_loop_generated}"
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/for_loop_reference.cpp
    # PI-7: Phase I user-routine transpiled + hand-written reference.
    "${_m12_user_routine_generated}"
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/user_routine_reference.cpp
    # PJ-1i: Phase J zero-ancilla fusion transpiled + hand-written
    # reference.
    "${_m12_fused_generated}"
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fuse_xor_and_reference.cpp
    # PJ-4e: Phase J dead-ancilla elimination transpiled + hand-
    # written reference.
    "${_m12_dead_ancilla_generated}"
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dead_ancilla_reference.cpp
    # PJ-3h: Phase J uncompute-hoisting transpiled + hand-written
    # reference.
    "${_m12_hoist_generated}"
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_reference.cpp
    # Real sturm implementation sources — the harness needs
    # uncompute_or, execute_gate and the BackendContext C ABI to
    # actually fire.  The header-only `sturm` interface target does
    # not provide these symbols, so they are compiled in here.
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
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
    # qbool_ops.hpp (post-PK-2: operator| returns owning qbool directly,
    # allocating an ancilla and emitting CX + CX + CCX) and pulls in
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

# ── Phase H / PH-3: snapshot fixtures for outer-variable mutation guard ────
# Two fixtures:
#   * for_outer_xor_reject — `a ^= b;` inside a for-loop where `a` is
#     declared outside the loop. The PH-3 matcher flags the mutation
#     (`skip_uncompute=true`), the M8 pass injects no inverse, and the
#     generated file is byte-identical to the input (modulo the
#     AUTO-GENERATED header the emitter prepends). Stderr MUST carry
#     the PH-3 diagnostic referring to qbool `a`.
#   * for_mixed_xor_reject — same scope contains BOTH an inner
#     intermediate (`r ^= a;` on a fresh local `r`) and an outer
#     mutation (`a ^= b;`). The PH-3 matcher flags only the outer
#     mutation: the inner intermediate still gets its inverse
#     injected as usual (`r ^= a;` before the for-body's close brace).
#     This proves the per-op flag granularity the PH-3 AC requires.
add_test(
    NAME snapshot_for_outer_xor_reject
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/for_outer_xor_reject.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/for_outer_xor_reject.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/for_outer_xor_reject
            "-DREQUIRED_STDERR=STURM: qbool/qint 'a'"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot_with_diagnostic.cmake)
set_tests_properties(snapshot_for_outer_xor_reject PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_for_mixed_xor_reject
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/for_mixed_xor_reject.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/for_mixed_xor_reject.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/for_mixed_xor_reject
            "-DREQUIRED_STDERR=STURM: qbool/qint 'a'"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot_with_diagnostic.cmake)
set_tests_properties(snapshot_for_mixed_xor_reject PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase H / PH-2: snapshot fixtures for auto-brace-wrap matcher ──────────
# Four fixtures — one per braceless control-flow shape — pin down the
# `{` + `}` raw insertions the PH-2 matcher schedules around a
# single-statement body that contains a quantum op. Each fixture uses a
# locally-declared intermediate (`qbool tmp = a | b;`) so PH-3's outer-
# variable-mutation guard does not flag the op; the M8 uncompute pass
# therefore plants a companion `uncompute_or(tmp, a, b);` call INSIDE
# the synthesised body.
#
# Shapes:
#   * for_intermediate_or_braceless      — `for (...) qbool tmp = a | b;`
#   * while_intermediate_or_braceless    — `while (...) qbool tmp = a | b;`
#   * if_then_intermediate_or_braceless  — `if (cond) qbool tmp = a | b;`
#   * if_else_intermediate_or_braceless  — `if (cond) { ... } else qbool tmp = a | b;`
#
# The idempotency companion feeds the generated file back through
# sturm-transpile and asserts byte-identical round-trip via the
# AUTO-GENERATED sentinel — PH-2 must not re-wrap the braces it already
# inserted on the first pass.
add_test(
    NAME snapshot_for_intermediate_or_braceless
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/for_intermediate_or_braceless.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/for_intermediate_or_braceless.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/for_intermediate_or_braceless
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_for_intermediate_or_braceless PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_for_intermediate_or_braceless
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/for_intermediate_or_braceless/for_intermediate_or_braceless.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/for_intermediate_or_braceless_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_for_intermediate_or_braceless PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_for_intermediate_or_braceless"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_while_intermediate_or_braceless
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/while_intermediate_or_braceless.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/while_intermediate_or_braceless.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/while_intermediate_or_braceless
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_while_intermediate_or_braceless PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_while_intermediate_or_braceless
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/while_intermediate_or_braceless/while_intermediate_or_braceless.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/while_intermediate_or_braceless_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_while_intermediate_or_braceless PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_while_intermediate_or_braceless"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_if_then_intermediate_or_braceless
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/if_then_intermediate_or_braceless.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/if_then_intermediate_or_braceless.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/if_then_intermediate_or_braceless
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_if_then_intermediate_or_braceless PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_if_then_intermediate_or_braceless
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/if_then_intermediate_or_braceless/if_then_intermediate_or_braceless.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/if_then_intermediate_or_braceless_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_if_then_intermediate_or_braceless PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_if_then_intermediate_or_braceless"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_if_else_intermediate_or_braceless
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/if_else_intermediate_or_braceless.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/if_else_intermediate_or_braceless.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/if_else_intermediate_or_braceless
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_if_else_intermediate_or_braceless PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_if_else_intermediate_or_braceless
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/if_else_intermediate_or_braceless/if_else_intermediate_or_braceless.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/if_else_intermediate_or_braceless_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_if_else_intermediate_or_braceless PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_if_else_intermediate_or_braceless"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase H / PH-4: snapshot fixtures for BRACED control-flow bodies ───────
# Complements the PH-2 braceless fixtures above: here every for/while/if
# body is already a CompoundStmt, so PH-2's auto-brace-wrap matcher must
# NOT fire. The only Phase H moving part still exercised is PH-1's scope
# finder, which must continue to return the pre-existing CompoundStmt so
# the M8 uncompute-synthesis pass plants a companion `uncompute_or(...)`
# call immediately before the body's existing close `}` — per-iteration
# for loops, once per taken branch for if/else.
#
# Shapes:
#   * for_intermediate_or       — `for (...) { qbool tmp = a | b; }`
#   * while_intermediate_or     — `while (cond) { qbool tmp = a | b; }`
#   * if_then_intermediate_or   — `if (cond) { qbool tmp = a | b; }`
#   * if_else_intermediate_or   — both arms braced, each with a DISTINCT
#                                  per-branch intermediate (`tmp_then` /
#                                  `tmp_else`). Pins the per-scope
#                                  granularity of the uncompute pass.
#
# The idempotency companion feeds the generated file back through
# sturm-transpile and asserts byte-identical round-trip via the
# AUTO-GENERATED sentinel.
add_test(
    NAME snapshot_for_intermediate_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/for_intermediate_or.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/for_intermediate_or.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/for_intermediate_or
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_for_intermediate_or PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_for_intermediate_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/for_intermediate_or/for_intermediate_or.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/for_intermediate_or_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_for_intermediate_or PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_for_intermediate_or"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_while_intermediate_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/while_intermediate_or.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/while_intermediate_or.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/while_intermediate_or
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_while_intermediate_or PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_while_intermediate_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/while_intermediate_or/while_intermediate_or.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/while_intermediate_or_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_while_intermediate_or PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_while_intermediate_or"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_if_then_intermediate_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/if_then_intermediate_or.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/if_then_intermediate_or.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/if_then_intermediate_or
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_if_then_intermediate_or PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_if_then_intermediate_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/if_then_intermediate_or/if_then_intermediate_or.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/if_then_intermediate_or_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_if_then_intermediate_or PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_if_then_intermediate_or"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_if_else_intermediate_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/if_else_intermediate_or.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/if_else_intermediate_or.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/if_else_intermediate_or
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_if_else_intermediate_or PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME transpiler_idempotent_if_else_intermediate_or
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/gen/if_else_intermediate_or/if_else_intermediate_or.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/if_else_intermediate_or_idem
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_idempotent.cmake)
set_tests_properties(transpiler_idempotent_if_else_intermediate_or PROPERTIES
    LABELS "transpiler"
    DEPENDS "snapshot_if_else_intermediate_or"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase I / PI-5: snapshot fixtures for user-routine rewrite ─────────────
# Seven fixtures pin the Phase I PI-2 / PI-3 / PI-4 rewrite behavior:
#   * routine_single_out_local     — local-intermediate output; `invert(fn)(...)`
#                                    lands before the enclosing scope's close brace.
#   * routine_single_out_escapes   — output is a function parameter (Final); no
#                                    inverse is injected and no diagnostic fires.
#                                    The generated file is byte-identical to the
#                                    input (modulo the AUTO-GENERATED header).
#   * routine_inside_when          — call inside WHEN(c) { ... } body with local
#                                    out; the inverse lands INSIDE the WHEN body
#                                    (Phase F-style `insert_before_override`).
#   * routine_inside_loop          — call inside a for-body with an output
#                                    declared in the same body; Phase H-style
#                                    per-iteration placement inside the loop.
#   * routine_outer_var_reject     — PI-3 SkipWithDiagnostic: call inside a
#                                    for-body mutating a qbool declared above
#                                    the loop. The transpiler emits a stderr
#                                    diagnostic (REQUIRED_STDERR) and injects
#                                    NO inverse — PH-3-style skip.
#   * routine_self_adjoint         — STURM_REGISTER_ADJOINT(my_swap, my_swap);
#                                    the PI-4 emission is `invert(my_swap)(...)`
#                                    which at runtime resolves through the PI-0
#                                    trait table to `&::my_swap` — semantically
#                                    a second call to the same function.
#   * routine_multiple_lifo        — two distinct routine calls in one scope;
#                                    inverses are emitted in reverse source order
#                                    (LIFO) — the second call's adjoint fires
#                                    first, then the first call's adjoint.
add_test(
    NAME snapshot_routine_single_out_local
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_single_out_local.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_single_out_local.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/routine_single_out_local
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_routine_single_out_local PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_routine_single_out_escapes
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_single_out_escapes.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_single_out_escapes.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/routine_single_out_escapes
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_routine_single_out_escapes PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_routine_inside_when
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_inside_when.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_inside_when.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/routine_inside_when
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_routine_inside_when PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_routine_inside_loop
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_inside_loop.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_inside_loop.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/routine_inside_loop
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_routine_inside_loop PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_routine_outer_var_reject
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_outer_var_reject.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_outer_var_reject.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/routine_outer_var_reject
            "-DREQUIRED_STDERR=STURM: qbool/qint 'a'"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot_with_diagnostic.cmake)
set_tests_properties(snapshot_routine_outer_var_reject PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_routine_self_adjoint
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_self_adjoint.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_self_adjoint.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/routine_self_adjoint
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_routine_self_adjoint PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_routine_multiple_lifo
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_multiple_lifo.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/routine_multiple_lifo.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/routine_multiple_lifo
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_routine_multiple_lifo PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase J / PJ-1g: snapshot fixtures for the zero-ancilla ccnot-fuse ──────
# Five fixtures pin the Phase J PJ-1d peephole matcher's decision tree
# end-to-end through the real sturm-transpile binary. Each exercises one
# branch of the `register_ccnot_fuse_matcher` callback:
#
#   * fuse_xor_and                          — happy path: canonical
#       `qbool __t = a & b; x ^= __t;` fuses into two
#       `ccnot_inplace(x, a, b);` lines (forward QReplacement + LIFO
#       self-adjoint uncompute emission).
#   * fuse_xor_and_reject_extra_reader      — `__t` has TWO readers in
#       the enclosing scope (`x ^= __t;` + `y = __t;`); the PJ-1a
#       reader-count guard rejects the pair. Phase A PA-3 picks up the
#       `^=` and emits its LIFO self-adjoint inverse; the VarDecl and
#       the extra `y = __t;` pass through unchanged.
#   * fuse_xor_and_reject_nested_rhs        — `&` init has a nested
#       op-call argument (`a & (b & c)`); PJ-1d's pattern matcher
#       requires BOTH args to be bare DREs, so it rejects. Phase E
#       PE-4 compound-flatten handles the nested init; Phase A PA-3
#       schedules the `x ^= __t;` self-adjoint inverse; LIFO closes
#       the chain with two `uncompute_and(...)` calls.
#   * fuse_xor_and_reject_intervening_stmt  — next sibling stmt is
#       `x ^= c;` (a `^=` on a DIFFERENT qbool), not `x ^= __t;`;
#       the PJ-1d consumer-shape guard rejects the pair. Phase A PA-3
#       fires on BOTH `^=` stmts and schedules two LIFO inverses.
#   * fuse_xor_and_reject_classical_rhs     — next sibling is `x ^= 1;`
#       (classical IntegerLiteral RHS, matches `operator^=(int)`);
#       PJ-1d's `peel_to_dre(rhs)` helper returns nullptr and the
#       match bails. Phase A PA-4 picks up the classical `^=` and
#       schedules its self-adjoint inverse; the VarDecl passes through.
#
# Driver and harness are identical to the Phase A..I snapshot tests
# (run_snapshot.cmake + byte-level compare_files). No new CMake
# infrastructure is needed for PJ-1g — the fixtures land on the existing
# wiring unchanged.
add_test(
    NAME snapshot_fuse_xor_and
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fuse_xor_and.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fuse_xor_and.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/fuse_xor_and
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_fuse_xor_and PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_fuse_xor_and_reject_extra_reader
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fuse_xor_and_reject_extra_reader.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fuse_xor_and_reject_extra_reader.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/fuse_xor_and_reject_extra_reader
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_fuse_xor_and_reject_extra_reader PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_fuse_xor_and_reject_nested_rhs
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fuse_xor_and_reject_nested_rhs.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fuse_xor_and_reject_nested_rhs.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/fuse_xor_and_reject_nested_rhs
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_fuse_xor_and_reject_nested_rhs PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_fuse_xor_and_reject_intervening_stmt
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fuse_xor_and_reject_intervening_stmt.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fuse_xor_and_reject_intervening_stmt.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/fuse_xor_and_reject_intervening_stmt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_fuse_xor_and_reject_intervening_stmt PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_fuse_xor_and_reject_classical_rhs
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fuse_xor_and_reject_classical_rhs.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fuse_xor_and_reject_classical_rhs.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/fuse_xor_and_reject_classical_rhs
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_fuse_xor_and_reject_classical_rhs PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase J / PJ-4c: snapshot fixtures for dead-ancilla elimination ─────────
# Four fixtures pin the Phase J PJ-4a dead-ancilla matcher's decision tree
# end-to-end through the real sturm-transpile binary. Each exercises one
# branch of the `register_dead_ancilla_matcher` callback:
#
#   * dead_ancilla_single           — happy path: `qbool t = a | b;` with
#       zero readers is silently eliminated. One empty-text QReplacement
#       over the decl stmt range + one `eliminated_stmt_ranges` entry;
#       the downstream MVP OR matcher early-returns on the covered range
#       via `apply_eliminated_stmt_guards`, so no `uncompute_or(t, a, b);`
#       is emitted at scope close.
#   * dead_ancilla_keep_read        — reject: `qbool t = a | b;` followed
#       by `x ^= t;` (one reader). PJ-4a bails on the reader-count guard,
#       leaving `eliminated_stmt_ranges` untouched. MVP OR picks up the
#       decl and schedules `uncompute_or(t, a, b);`; Phase A PA-3 picks
#       up `x ^= t;` and schedules its self-adjoint LIFO inverse.
#   * dead_ancilla_keep_chain       — reject + cascade: `qbool t = a | b;`
#       followed by `qbool r = t & d;`. `t` has one reader (inside `r`'s
#       init) so PJ-4a rejects elimination of `t` — MVP OR emits
#       `uncompute_or(t, a, b);`. `r` has no readers, so PJ-4a DOES
#       eliminate `r` independently. Per-decl independence, not a
#       cascade.
#   * dead_ancilla_inside_when      — elimination inside a `WHEN(ctrl)
#       { ... }` body. The reader-count probe anchors on the WHEN body's
#       CompoundStmt, not the outer function; with zero readers inside
#       the body, PJ-4a fires and eliminates the decl. The WHEN macro
#       itself is not rewritten (`WHEN(ctrl)` is a bare DeclRefExpr —
#       PF-3 short-circuits).
#
# Driver and harness are identical to the Phase A..I and PJ-1g snapshot
# tests (run_snapshot.cmake + byte-level compare_files). No new CMake
# infrastructure is needed for PJ-4c — the fixtures land on the existing
# wiring unchanged.
add_test(
    NAME snapshot_dead_ancilla_single
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dead_ancilla_single.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dead_ancilla_single.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/dead_ancilla_single
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_dead_ancilla_single PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_dead_ancilla_keep_read
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dead_ancilla_keep_read.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dead_ancilla_keep_read.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/dead_ancilla_keep_read
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_dead_ancilla_keep_read PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_dead_ancilla_keep_chain
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dead_ancilla_keep_chain.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dead_ancilla_keep_chain.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/dead_ancilla_keep_chain
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_dead_ancilla_keep_chain PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_dead_ancilla_inside_when
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dead_ancilla_inside_when.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dead_ancilla_inside_when.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/dead_ancilla_inside_when
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_dead_ancilla_inside_when PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

# ── Phase J / PJ-3f: snapshot fixtures for uncompute hoisting ───────────────
# Six fixtures pin the Phase J PJ-3d uncompute-hoisting matcher's decision
# tree end-to-end through the real sturm-transpile binary. Each exercises
# one branch of the `register_hoist_invariant_matcher` callback (or a
# combination of its gates):
#
#   * hoist_or_out_of_for                   — happy path: `qbool t = a | b;`
#       inside a for-body with outer-scoped operands. PJ-3d sets
#       `hoist_to_override` to the function-body close brace; uncompute
#       lands AFTER the loop's `}`, not before it.
#   * hoist_and_out_of_while                — AND kind + while-loop
#       coverage. Uses a depth-2 compound `(a & b) & c` to surface an
#       AND op through the PE-4 compound-flatten matcher (no MVP AND
#       matcher exists for depth-1 `qbool t = a & b;` shapes). Only the
#       inner AND is loop-invariant; the outer AND's `__stu_t0`
#       operand is loop-local, so the outer uncompute stays inside the
#       while body while the inner uncompute lands after it.
#   * hoist_reject_operand_written_in_loop  — reject: `a = b;` inside the
#       loop body makes `a` non-invariant per `expr_is_loop_invariant`'s
#       WriteFinder probe. Uncompute stays at `scope.close_brace`.
#   * hoist_reject_branch_not_loop          — reject: enclosing scope is
#       an `if`-body, not a loop. `classify_scope_kind` returns
#       `BranchBody`, short-circuiting the matcher before operand
#       inspection. Uncompute stays inside the branch body.
#   * hoist_nested_loops_inner_only         — inner loop's body-local op
#       hoists to the OUTER loop body's close brace, not the function
#       body's close brace. `compute_loop_enclosing_close_brace` walks
#       the parent chain from the inner loop stmt one level up.
#   * hoist_multi_op_same_loop              — two independent OR decls
#       in the same loop body, both invariant. PJ-3d sets the hoist
#       fields on BOTH ops; the M8 reverse-iteration LIFO schedule
#       emits `uncompute_or(r2, c, d);` then `uncompute_or(r1, a, b);`
#       at the post-loop anchor (reverse source order). Pins the
#       multi-op LIFO ordering semantic the roadmap calls out.
#
# Driver and harness are identical to every other Phase A..I + PJ-1g /
# PJ-4c snapshot test (run_snapshot.cmake + byte-level compare_files).
# No new CMake infrastructure is needed for PJ-3f — the fixtures land on
# the existing wiring unchanged.
add_test(
    NAME snapshot_hoist_or_out_of_for
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_or_out_of_for.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_or_out_of_for.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/hoist_or_out_of_for
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_hoist_or_out_of_for PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_hoist_and_out_of_while
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_and_out_of_while.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_and_out_of_while.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/hoist_and_out_of_while
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_hoist_and_out_of_while PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_hoist_reject_operand_written_in_loop
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_reject_operand_written_in_loop.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_reject_operand_written_in_loop.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/hoist_reject_operand_written_in_loop
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_hoist_reject_operand_written_in_loop PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_hoist_reject_branch_not_loop
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_reject_branch_not_loop.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_reject_branch_not_loop.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/hoist_reject_branch_not_loop
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_hoist_reject_branch_not_loop PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_hoist_nested_loops_inner_only
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_nested_loops_inner_only.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_nested_loops_inner_only.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/hoist_nested_loops_inner_only
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_hoist_nested_loops_inner_only PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")

add_test(
    NAME snapshot_hoist_multi_op_same_loop
    COMMAND "${CMAKE_COMMAND}"
            -DTRANSPILE_BIN=$<TARGET_FILE:sturm-transpile>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_multi_op_same_loop.cpp
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hoist_multi_op_same_loop.expected.cpp
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gen/hoist_multi_op_same_loop
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.cmake)
set_tests_properties(snapshot_hoist_multi_op_same_loop PROPERTIES
    LABELS "transpiler"
    REQUIRED_FILES "$<TARGET_FILE:sturm-transpile>")
