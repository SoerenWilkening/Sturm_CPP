# pm4_dogfood_snapshot.cmake — PM4-9 smoke 3 ctest driver.
#
# Invoked by `transpiler/tests/CMakeLists.txt` with:
#   -DTEST_BIN=<abs path>   — the `pm4_dogfood_snapshot` executable,
#                             built against the four PB `.cpp` + `.expected.
#                             cpp` fixtures under `tests/transpiler/fixtures/`.
#
# Contract
# --------
# The `pm4_dogfood_snapshot.cpp` binary (see its file-level comment for
# the full test body) pins the PM4-9 smoke 3 claim from the plan:
#
#   > existing Phase B snapshot of `a += k;` / `a -= k;` / `a *= k;` /
#   > `a /= k;` remains byte-identical post-migration.
#
# It runs the standalone `sturm-transpile` binary against each of the
# four PB fixtures (`add_assign_const.cpp` / `sub_assign_const.cpp` /
# `mul_assign_const.cpp` / `div_assign_const.cpp` in
# `tests/transpiler/fixtures/`) and byte-compares the generated sibling
# against its locked-down `.expected.cpp` golden.
#
# Relationship to the in-tree `snapshot_{add,sub,mul,div}_assign_const`
# CTests
# ---------------------------------------------------------------------
# The `tests/transpiler/CMakeLists.txt:125-171` snapshot CTests already
# probe the same byte-identity gate; this `.cmake` driver is the PM4-
# focused parallel, wired up next to the other `pm4_smoke_*` smokes so
# `ctest -R pm4_` sees the full PM4 smoke set in one place. A future
# PM4 refactor that regresses the dogfood migration (dropped
# `register_matcher` call, mutated QIntAssignConstCallback template
# argument, drain-order rearrangement in TranspileConsumer's ctor, …)
# surfaces here BEFORE the generic snapshot gates fire, with a
# PM4-6-scoped failure diagnostic rather than a generic "byte delta".
#
# Exit discipline
# ---------------
# The script exits 0 when the test binary exits 0, non-zero otherwise.
# `message(FATAL_ERROR)` on failure carries the full captured stdout +
# stderr so a `ctest -R pm4_dogfood_snapshot` diagnostic is self-
# contained. No filesystem assertions here — every contract lives
# inside `pm4_dogfood_snapshot.cpp`; the `.cmake` driver just shells
# out and relays the exit code. Mirrors the posture of
# `pm4_smoke_dlopen.cmake` and `pm4_smoke_linktime.cmake`.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED TEST_BIN)
    message(FATAL_ERROR
        "pm4_dogfood_snapshot: TEST_BIN not set. Expected the ctest "
        "wiring in transpiler/tests/CMakeLists.txt to pass "
        "-DTEST_BIN=$<TARGET_FILE:pm4_dogfood_snapshot>.")
endif()

if(NOT EXISTS "${TEST_BIN}")
    message(FATAL_ERROR
        "pm4_dogfood_snapshot: test binary missing at ${TEST_BIN}. "
        "Rebuild the `pm4_dogfood_snapshot` target and re-run.")
endif()

execute_process(
    COMMAND "${TEST_BIN}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE  _stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

# Relay the test binary's stdout / stderr regardless of exit code so a
# passing run still surfaces the four `OK <fixture>.cpp: generated
# sibling is byte-identical to <fixture>.expected.cpp` notes, and a
# failing run surfaces the offending FAIL lines + unified diff.
if(_stdout)
    message(STATUS "pm4_dogfood_snapshot stdout:\n${_stdout}")
endif()
if(_stderr)
    message(STATUS "pm4_dogfood_snapshot stderr:\n${_stderr}")
endif()

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "pm4_dogfood_snapshot: test binary exited with code ${_rc} — "
        "the PM4-6 dogfood migration regressed one of the four PB "
        "compound-assign fixtures. See the FAIL lines in the stderr "
        "relay above; each one names the fixture whose generated "
        "sibling no longer matches its `.expected.cpp` golden byte-"
        "for-byte.")
endif()

message(STATUS
    "pm4_dogfood_snapshot: OK — all four Phase B compound-assign "
    "fixtures (add/sub/mul/div_assign_const) are byte-identical "
    "post-PM4-6 migration.")
