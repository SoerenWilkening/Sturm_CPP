# pm4_two_plugins_independent.cmake — PM4-9 smoke 5 ctest driver.
#
# Invoked by `transpiler/tests/CMakeLists.txt` with:
#   -DTEST_BIN=<abs path>   — the `pm4_two_plugins_independent` executable,
#                             built with compile-time knowledge of:
#                               * `sturm-transpile-plugin` (host plugin)
#                               * `sturm-pm4-demo-plugin` (first demo)
#                               * `sturm-pm4-collision-plugin` (this smoke's
#                                 independent second plugin registering the
#                                 same `pm4.demo.tag` kind_id)
#
# Contract
# --------
# The `pm4_two_plugins_independent.cpp` binary (see its file-level
# comment for the full test body) pins the PM4-9 smoke 5 claim from
# the plan:
#
#   > two plugins registering the same `kind_id` string cause a hard
#   > error at the second registration with a diagnosable message.
#
# It loads both the `sturm-pm4-demo-plugin` (canonical demo) and the
# `sturm-pm4-collision-plugin` (this smoke's fixture plugin that also
# claims `pm4.demo.tag`) into a single clang++ invocation via two
# `-Xclang -plugin-arg-sturm-transpile -Xclang load=<path>` cc1 arg
# pairs. The consumer-ctor drain of `runtime_registrars()` iterates
# in insertion order: the first registration succeeds, the second hits
# the duplicate-kind_id check at `plugin_registry.cpp:108-113` and
# calls `std::abort()`.
#
# The test asserts:
#   (Gate A) Both plugin MODULE libraries exist on disk — sanity
#            check on the build graph.
#   (Gate B) Non-zero compile exit (SIGABRT, shell encoding 128+6=134).
#   (Gate B) stderr contains `duplicate op registration for kind_id`
#            byte-for-byte.
#   (Gate B) stderr contains `'pm4.demo.tag'` — the conflicting key.
#   (Gate B) stderr contains `second registration rejected` — the
#            trailing diagnostic clause.
#   (Gate C) A control experiment — loading ONLY the first plugin must
#            exit 0 without surfacing the collision diagnostic.
#            Gate C is deferred under `STURM_PM4_LINK_DEMO=ON` because
#            the demo plugin's registrar is already baked into the
#            host at link time, so a single runtime `load=<demo>`
#            would itself trigger the collision (covered by Gate B).
#
# Why a second MODULE library rather than re-using the demo plugin
# -----------------------------------------------------------------
# Passing the SAME demo plugin twice (`load=<demo> load=<demo>`) is a
# degenerate case: both `dlopen` calls resolve to the same handle
# (RTLD_LOCAL doesn't count as "once per path" under glibc), and the
# two registrations still fire, but the test is less faithful to the
# "two independent plugins" scenario the plan calls out. Building a
# separate `sturm-pm4-collision-plugin` from an independent source
# file reproduces the REAL-WORLD shape: two third-party plugins
# authored independently, each claiming the same `kind_id`. The
# collision path is otherwise byte-identical, but the diagnostic
# surface is more trustworthy for a user tracing a real collision.
#
# Exit discipline
# ---------------
# The script exits 0 when the test binary exits 0, non-zero otherwise.
# `message(FATAL_ERROR)` on failure carries the full captured stdout +
# stderr so a `ctest -R pm4_two_plugins_independent` diagnostic is
# self-contained. Mirrors the posture of `pm4_smoke_dlopen.cmake`.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED TEST_BIN)
    message(FATAL_ERROR
        "pm4_two_plugins_independent: TEST_BIN not set. Expected the "
        "ctest wiring in transpiler/tests/CMakeLists.txt to pass "
        "-DTEST_BIN=$<TARGET_FILE:pm4_two_plugins_independent>.")
endif()

if(NOT EXISTS "${TEST_BIN}")
    message(FATAL_ERROR
        "pm4_two_plugins_independent: test binary missing at "
        "${TEST_BIN}. Rebuild the `pm4_two_plugins_independent` target "
        "and re-run.")
endif()

execute_process(
    COMMAND "${TEST_BIN}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE  _stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

# Relay the test binary's stdout / stderr regardless of exit code so a
# passing run surfaces every `OK`/`DEFER` note and a failing run
# surfaces the offending FAIL <file>:<line> <expr> diagnostics plus
# the captured clang++ output (which must include the
# `duplicate op registration` line in the happy-path).
if(_stdout)
    message(STATUS "pm4_two_plugins_independent stdout:\n${_stdout}")
endif()
if(_stderr)
    message(STATUS "pm4_two_plugins_independent stderr:\n${_stderr}")
endif()

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "pm4_two_plugins_independent: test binary exited with code "
        "${_rc} — the PM4-2 duplicate-kind_id collision path "
        "regressed. Either the Registry's collision check at "
        "plugin_registry.cpp:108-113 no longer calls `std::abort()` "
        "on the second registration (the compile unexpectedly "
        "succeeded), the diagnostic wording drifted (the test "
        "expects `duplicate op registration for kind_id "
        "'pm4.demo.tag' (second registration rejected)`), or the "
        "two plugin MODULE artifacts went missing. See the FAIL "
        "lines in the stderr relay above for the exact contract "
        "breakage.")
endif()

message(STATUS
    "pm4_two_plugins_independent: OK — two plugins registering the "
    "same kind_id 'pm4.demo.tag' triggered the Registry's documented "
    "duplicate-kind_id abort at plugin_registry.cpp:108-113. The "
    "`duplicate op registration` stderr diagnostic fired with the "
    "conflicting key named, and a control compile with only one "
    "plugin registered cleanly (modulo the STURM_PM4_LINK_DEMO=ON "
    "deferral).")
