# pm4_smoke_linktime.cmake — PM4-10 ctest driver for the link-time
# `STURM_PM4_LINK_DEMO=ON` registration path.
#
# Invoked by `transpiler/tests/CMakeLists.txt` with:
#   -DTEST_BIN=<abs path>   — the `pm4_smoke_linktime` executable built
#                             under the `STURM_PM4_LINK_DEMO=ON` option.
#
# Contract
# --------
# The `pm4_smoke_linktime.cpp` binary (see the file-level comment there
# for the full unit-test suite) pins the PM4-10 link-time path end-to-end
# at the plugin / registry boundary:
#
#   - `registrars()` — the Meyer's-singleton vector defined in
#     `plugin_registry.cpp` — is non-empty at process start (the
#     `STURM_REGISTER_PLUGIN(...)` expansion's `StaticRegistrar` ctor
#     ran during static-init of the shim TU
#     `src/pm4_link_demo_registrar.cpp`).
#
#   - Draining that vector against a fresh `Registry` populates
#     `find_render_fn("pm4.demo.tag")` with a non-null renderer.
#
#   - Invoking that renderer against a sample `QOperation` with
#     `result.name = "q"` emits the exact string
#     `"    pm4_demo_tag_inverse(q);\n"` — the same rewritten-output
#     assertion smoke 1 (`pm4_smoke_dlopen`, PM4-8) will carry over the
#     dlopen path once it lands.
#
# Relationship to smoke 1
# -----------------------
# Per the PM4 plan §Verification:
#
#   - Smoke 1 (PM4-8): `add_quantum_executable(... PLUGINS
#     $<TARGET_FILE:sturm-pm4-demo-plugin>)` compiles a user TU and
#     asserts `pm4_demo_tag_inverse(` appears in the generated sibling.
#   - Smoke 2 (PM4-10, this file): same rewritten-output assertion but
#     without a `PLUGINS` argument — reconfigured with
#     `-DSTURM_PM4_LINK_DEMO=ON`, the demo plugin's registrar is
#     statically linked into `sturm-transpile-plugin` via the
#     `STURM_REGISTER_PLUGIN` macro.
#
# Because smoke 1 has not landed yet (PM4-8 is open) and the end-to-end
# pipeline it would drive depends on PM4-3 / PM4-4 / PM4-5, this smoke
# runs at the unit-test level: it pins the link-time registration path
# and the demo's rewritten-output byte sequence via the same renderer
# that smoke 1 will reach end-to-end. When the full pipeline lands, the
# test binary can be upgraded to drive a clang++ compile and diff the
# generated buffer; the `.cmake` driver contract below stays unchanged
# because the probe is simply "test binary exits 0".
#
# Exit discipline
# ---------------
# The script exits 0 when the test binary exits 0, non-zero otherwise.
# `message(FATAL_ERROR)` on failure carries the full captured stdout +
# stderr so a ctest -R pm4_smoke_linktime diagnostic is self-contained.
# No filesystem assertions here — every contract lives inside
# `pm4_smoke_linktime.cpp`; the `.cmake` driver just shells out and
# relays the exit code.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED TEST_BIN)
    message(FATAL_ERROR
        "pm4_smoke_linktime: TEST_BIN not set. Expected the ctest "
        "wiring in transpiler/tests/CMakeLists.txt to pass "
        "-DTEST_BIN=$<TARGET_FILE:pm4_smoke_linktime>.")
endif()

if(NOT EXISTS "${TEST_BIN}")
    message(FATAL_ERROR
        "pm4_smoke_linktime: test binary missing at ${TEST_BIN}. "
        "Rebuild the `pm4_smoke_linktime` target under "
        "`-DSTURM_PM4_LINK_DEMO=ON` and re-run.")
endif()

execute_process(
    COMMAND "${TEST_BIN}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE  _stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

# Relay the test binary's stdout / stderr regardless of exit code so a
# passing run still surfaces the `PASS: N/N` summary line and a failing
# run surfaces the offending `FAIL <file>:<line> <expr>` diagnostics.
if(_stdout)
    message(STATUS "pm4_smoke_linktime stdout:\n${_stdout}")
endif()
if(_stderr)
    message(STATUS "pm4_smoke_linktime stderr:\n${_stderr}")
endif()

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "pm4_smoke_linktime: test binary exited with code ${_rc} — "
        "the PM4-10 link-time registration path is broken. Either the "
        "Meyer's-singleton registrar vector did not receive the demo "
        "plugin's entry (static-init ordering regression in "
        "`src/pm4_link_demo_registrar.cpp`), the drained registrar did "
        "not populate the Registry with `pm4.demo.tag`, or the demo's "
        "renderer emits unexpected text. See the FAIL lines in the "
        "stderr relay above.")
endif()

message(STATUS
    "pm4_smoke_linktime: OK — link-time demo plugin registrar reached "
    "the Meyer-singleton vector, drained into a fresh Registry, and "
    "rendered the expected `    pm4_demo_tag_inverse(q);\\n` uncompute "
    "text. STURM_PM4_LINK_DEMO=ON path verified end-to-end at the "
    "plugin/registry boundary.")
