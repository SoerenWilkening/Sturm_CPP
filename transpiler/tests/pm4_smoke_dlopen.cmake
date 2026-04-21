# pm4_smoke_dlopen.cmake — PM4-8 ctest driver for the end-to-end
# runtime-dlopen plugin load path.
#
# Invoked by `transpiler/tests/CMakeLists.txt` with:
#   -DTEST_BIN=<abs path>   — the `pm4_smoke_dlopen` executable, built
#                             against the `add_quantum_executable(
#                             pm4_smoke_dlopen_fixture ... PLUGINS
#                             $<TARGET_FILE:sturm-pm4-demo-plugin>)`
#                             target so its dependency graph forces
#                             the fixture compile to run.
#
# Contract
# --------
# The `pm4_smoke_dlopen.cpp` binary (see its file-level comment for the
# full unit-test suite) pins the PM4-8 dlopen path end-to-end at three
# layers:
#
#   - CMake wiring: the `pm4_smoke_dlopen_fixture` user target was
#     compiled through `add_quantum_executable(... PLUGINS $<TARGET_FILE:
#     sturm-pm4-demo-plugin>)` and produced both a `.o` and a
#     `<build>/sturm_gen/transpiler/tests/fixtures/pm4_smoke_dlopen_fixture.cpp`
#     dump-to mirror. If the demo plugin had failed to dlopen, the
#     fixture compile would have exited non-zero and blocked this
#     driver from ever invoking `TEST_BIN`.
#
#   - Registry observability: a replayed `verbose + load=<demo>`
#     compile against a scratch TU (same cc1 arg shape the `PLUGINS`
#     argument produces in the CMake helper, plus `verbose`) emits the
#     `registered kind_ids: pm4.demo.tag` line, proving the demo
#     plugin's `sturm_register_plugin_v1` reached `register_op` and
#     the host's probe Registry observed the key via
#     `Registry::kind_ids()`.
#
#   - Rewritten-buffer sentinel: a conditional check for the
#     `pm4_demo_tag_inverse(` token in the fixture's dump-to mirror.
#     Passes cleanly with a `DEFERRED` stderr note today (PM4-3 is
#     not merged, so no `QOpKind::PLUGIN` dispatch exists and the
#     demo plugin's renderer cannot reach the rewritten buffer). Must
#     be converted to a strict assertion once PM4-3 lands — the test
#     binary's `gate_sentinel_in_rewritten_buffer_conditional`
#     comment block spells out exactly how.
#
# Relationship to smoke 2 (`pm4_smoke_linktime`)
# ----------------------------------------------
# Per the PM4 plan §Verification:
#
#   - Smoke 1 (PM4-8, this file): `add_quantum_executable(... PLUGINS
#     $<TARGET_FILE:sturm-pm4-demo-plugin>)` compiles a user TU with
#     the demo plugin loaded via runtime `dlopen`. Proves the
#     CMake → `-Xclang -plugin-arg-sturm-transpile -Xclang load=<path>`
#     → `dlopen(RTLD_LOCAL | RTLD_NOW)` → version check → dlsym chain.
#   - Smoke 2 (PM4-10, `pm4_smoke_linktime.cmake`): same rewritten-
#     output assertion but without a `PLUGINS` argument — reconfigured
#     with `-DSTURM_PM4_LINK_DEMO=ON`, the demo plugin's registrar is
#     statically linked into `sturm-transpile-plugin` via the
#     `STURM_REGISTER_PLUGIN` macro and the link-time Meyer's-singleton
#     drain replaces the dlopen hop.
#
# Both smokes converge on the same rewritten-output sentinel once
# PM4-3 + PM4-6 land; until then, PM4-8's CMake wiring + `pm4.demo.tag`
# registration checks live on while the strict sentinel assertion is
# kept in the test binary as a DEFERRED branch.
#
# Exit discipline
# ---------------
# The script exits 0 when the test binary exits 0, non-zero otherwise.
# `message(FATAL_ERROR)` on failure carries the full captured stdout +
# stderr so a `ctest -R pm4_smoke_dlopen` diagnostic is self-contained.
# No filesystem assertions here — every contract lives inside
# `pm4_smoke_dlopen.cpp`; the `.cmake` driver just shells out and
# relays the exit code.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED TEST_BIN)
    message(FATAL_ERROR
        "pm4_smoke_dlopen: TEST_BIN not set. Expected the ctest "
        "wiring in transpiler/tests/CMakeLists.txt to pass "
        "-DTEST_BIN=$<TARGET_FILE:pm4_smoke_dlopen>.")
endif()

if(NOT EXISTS "${TEST_BIN}")
    message(FATAL_ERROR
        "pm4_smoke_dlopen: test binary missing at ${TEST_BIN}. "
        "Rebuild the `pm4_smoke_dlopen` target and re-run.")
endif()

execute_process(
    COMMAND "${TEST_BIN}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE  _stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

# Relay the test binary's stdout / stderr regardless of exit code so a
# passing run still surfaces the `PASS: N/N` summary + every `OK` /
# `DEFER` note, and a failing run surfaces the offending `FAIL
# <file>:<line> <expr>` diagnostics.
if(_stdout)
    message(STATUS "pm4_smoke_dlopen stdout:\n${_stdout}")
endif()
if(_stderr)
    message(STATUS "pm4_smoke_dlopen stderr:\n${_stderr}")
endif()

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "pm4_smoke_dlopen: test binary exited with code ${_rc} — the "
        "PM4-8 dlopen path is broken. Either the "
        "`add_quantum_executable(... PLUGINS <demo>)` fixture target "
        "failed to produce its `.o` / dump-to mirror (check Gate A), "
        "the `verbose + load=<demo>` replay did not observe "
        "`pm4.demo.tag` in the probe Registry's kind_ids (check "
        "Gate B), or the rewritten buffer regressed its PM1-7 header "
        "(check Gate C's first assertion). See the FAIL lines in "
        "the stderr relay above.")
endif()

message(STATUS
    "pm4_smoke_dlopen: OK — end-to-end runtime-dlopen plugin load "
    "path verified. The `add_quantum_executable(... PLUGINS "
    "<demo>)` helper compiled the fixture TU, `plugin.cpp` "
    "dlopen'd the demo plugin, the probe Registry observed the "
    "`pm4.demo.tag` kind_id via `Registry::kind_ids()`, and the "
    "rewritten buffer carries the PM1-7 idempotency header. The "
    "sentinel `pm4_demo_tag_inverse(` assertion is deferred to "
    "post-PM4-3 per the test binary's DEFERRED note.")
