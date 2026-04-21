# pm4_missing_plugin_errors.cmake — PM4-9 smoke 4 ctest driver.
#
# Invoked by `transpiler/tests/CMakeLists.txt` with:
#   -DTEST_BIN=<abs path>   — the `pm4_missing_plugin_errors` executable,
#                             built against the host `sturm-transpile-
#                             plugin` shared library and the configure-
#                             time `clang++` driver.
#
# Contract
# --------
# The `pm4_missing_plugin_errors.cpp` binary (see its file-level
# comment for the full test body) pins the PM4-9 smoke 4 claim from
# the plan:
#
#   > `PLUGINS /nonexistent.so` fails the compile with a recognizable
#   > `sturm-transpile plugin: failed to dlopen` stderr line.
#
# It drives the exact cc1 arg pair the `add_quantum_executable(...
# PLUGINS <path>)` helper emits:
#
#     -Xclang -plugin-arg-sturm-transpile -Xclang load=<bogus-path>
#
# against a path that cannot exist on any CI host (`/proc/self/
# nonexistent-pm4-smoke-plugin.so`), and asserts:
#
#   (Gate A) Non-zero compile exit — `ParseArgs` returns false on a
#            failed `dlopen`, which Clang surfaces as a non-zero cc1
#            exit.
#   (Gate A) stderr contains `sturm-transpile plugin: failed to dlopen`
#            byte-for-byte — the exact prefix `plugin.cpp:195-198`
#            emits.
#   (Gate A) stderr contains the bogus path verbatim — users debugging
#            a CI failure must be able to identify the culprit PLUGINS
#            entry from the stderr alone.
#   (Gate A) Success-branch strings (`loaded runtime plugin`, `pm4.demo.
#            tag`) are ABSENT.
#   (Gate B) `verbose` mode does NOT convert the fatal dlopen failure
#            into a silent pass — the stderr line must still fire and
#            the exit must still be non-zero.
#
# Why this smoke coexists with `test_plugin_load`'s
# `gate_load_nonexistent_errors`
# -------------------------------------------------------------------
# `test_plugin_load.cpp:418-441` already exercises the same stderr
# contract. PM4-9 smoke 4 is the PM4-focused parallel — declared
# alongside the other `pm4_smoke_*` binaries so `ctest -R pm4_`
# surfaces the full PM4 smoke set. A PM4 refactor that regresses
# either path surfaces in BOTH tests, but the PM4-scoped failure
# diagnostic makes the regression's blast radius obvious from the
# build log alone.
#
# Why this smoke does NOT drive `add_quantum_executable(... PLUGINS
# /nonexistent)` end-to-end
# ------------------------------------------------------------------
# `add_quantum_executable` is a configure-time helper — a failing
# compile from a PLUGINS entry pointing at a nonexistent .so would
# break the whole build, not just this test. Running the cc1 arg
# pattern directly through `clang++` (the same path PM4-5's
# `SturmTranspile.cmake` helper emits) covers the error-path contract
# without depending on a configure-time build failure.
#
# Exit discipline
# ---------------
# The script exits 0 when the test binary exits 0, non-zero otherwise.
# `message(FATAL_ERROR)` on failure carries the full captured stdout +
# stderr so a `ctest -R pm4_missing_plugin_errors` diagnostic is self-
# contained. Mirrors the posture of `pm4_smoke_dlopen.cmake`.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED TEST_BIN)
    message(FATAL_ERROR
        "pm4_missing_plugin_errors: TEST_BIN not set. Expected the "
        "ctest wiring in transpiler/tests/CMakeLists.txt to pass "
        "-DTEST_BIN=$<TARGET_FILE:pm4_missing_plugin_errors>.")
endif()

if(NOT EXISTS "${TEST_BIN}")
    message(FATAL_ERROR
        "pm4_missing_plugin_errors: test binary missing at ${TEST_BIN}. "
        "Rebuild the `pm4_missing_plugin_errors` target and re-run.")
endif()

execute_process(
    COMMAND "${TEST_BIN}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE  _stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

# Relay the test binary's stdout / stderr regardless of exit code so a
# passing run still surfaces the PASS summary and a failing run
# surfaces the offending FAIL <file>:<line> <expr> diagnostics plus the
# captured compile output.
if(_stdout)
    message(STATUS "pm4_missing_plugin_errors stdout:\n${_stdout}")
endif()
if(_stderr)
    message(STATUS "pm4_missing_plugin_errors stderr:\n${_stderr}")
endif()

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "pm4_missing_plugin_errors: test binary exited with code ${_rc} "
        "— the PM4-4 `load=<nonexistent>` → ParseArgs → dlopen-failure "
        "error path regressed. Either the stderr wording at "
        "plugin.cpp:195-198 drifted (the test expects the exact prefix "
        "`sturm-transpile plugin: failed to dlopen`), or ParseArgs now "
        "returns true on a dlopen failure and the compile silently "
        "continues. See the FAIL lines in the stderr relay above for "
        "the exact contract breakage.")
endif()

message(STATUS
    "pm4_missing_plugin_errors: OK — a `load=<nonexistent>` cc1 arg "
    "pair produced a non-zero compile exit with the documented "
    "`sturm-transpile plugin: failed to dlopen <path>:` stderr line, "
    "both in quiet and verbose modes.")
