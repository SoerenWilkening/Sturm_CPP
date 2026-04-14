# verify_external_consumer.cmake — M10 external-consumer probe.
#
# Configures and builds the stand-alone project under external_consumer/
# with the helper include()d from the in-tree path. This validates that
# the helper:
#   - is consumable outside the STURM build tree;
#   - does not force STURM_TRANSPILE=ON on a consumer that has not asked
#     for it (default is OFF);
#   - degrades gracefully under OFF: the consumer's add_quantum_executable
#     call must configure cleanly and produce a runnable binary.
#
# Arguments (passed by the parent test via -D):
#   HELPER_FILE   absolute path to cmake/SturmTranspile.cmake
#   CONSUMER_DIR  absolute path to external_consumer/
#   BUILD_DIR     fresh binary directory for the out-of-source configure
#   CMAKE_BIN     path to the cmake executable used for the nested invoke
#
# This probe is intentionally minimal — it does NOT try to exercise
# transpile-ON from the external consumer because that path would need
# the consumer to also build sturm-transpile, which is out of scope for
# the "consumer link/build degradation" contract.

foreach(var HELPER_FILE CONSUMER_DIR BUILD_DIR CMAKE_BIN)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR
            "verify_external_consumer: required arg ${var} not set")
    endif()
endforeach()

if(NOT EXISTS "${HELPER_FILE}")
    message(FATAL_ERROR
        "verify_external_consumer: helper file not found: ${HELPER_FILE}")
endif()
if(NOT EXISTS "${CONSUMER_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "verify_external_consumer: consumer CMakeLists.txt not found "
        "under ${CONSUMER_DIR}")
endif()

# Fresh build dir every run: guarantees we catch regressions where the
# helper only configures correctly on a warm cache.
file(REMOVE_RECURSE "${BUILD_DIR}")
file(MAKE_DIRECTORY "${BUILD_DIR}")

# ── Configure the external consumer project ───────────────────────────────
execute_process(
    COMMAND "${CMAKE_BIN}"
            -S "${CONSUMER_DIR}"
            -B "${BUILD_DIR}"
            "-DSTURM_HELPER_PATH=${HELPER_FILE}"
            -DSTURM_TRANSPILE=OFF
    RESULT_VARIABLE cfg_rc
    OUTPUT_VARIABLE cfg_out
    ERROR_VARIABLE  cfg_err)
if(NOT cfg_rc EQUAL 0)
    message(FATAL_ERROR
        "verify_external_consumer: configure failed (rc=${cfg_rc})\n"
        "STDOUT:\n${cfg_out}\nSTDERR:\n${cfg_err}")
endif()

# ── Build it (OFF mode → no transpile step, just a plain compile) ─────────
execute_process(
    COMMAND "${CMAKE_BIN}" --build "${BUILD_DIR}" --target external_fixture -j1
    RESULT_VARIABLE build_rc
    OUTPUT_VARIABLE build_out
    ERROR_VARIABLE  build_err)
if(NOT build_rc EQUAL 0)
    message(FATAL_ERROR
        "verify_external_consumer: build failed (rc=${build_rc})\n"
        "STDOUT:\n${build_out}\nSTDERR:\n${build_err}")
endif()

# ── Confirm the resulting binary runs and exits 0 ─────────────────────────
# The binary's location depends on the generator (Makefiles vs Ninja vs
# multi-config). A few candidate paths cover the common cases.
set(candidates
    "${BUILD_DIR}/external_fixture"
    "${BUILD_DIR}/Debug/external_fixture"
    "${BUILD_DIR}/Release/external_fixture"
    "${BUILD_DIR}/external_fixture.exe")
set(binary "")
foreach(cand ${candidates})
    if(EXISTS "${cand}")
        set(binary "${cand}")
        break()
    endif()
endforeach()
if(binary STREQUAL "")
    message(FATAL_ERROR
        "verify_external_consumer: could not locate the built "
        "external_fixture binary under ${BUILD_DIR}.")
endif()

execute_process(
    COMMAND "${binary}"
    RESULT_VARIABLE run_rc
    OUTPUT_VARIABLE run_out
    ERROR_VARIABLE  run_err)
if(NOT run_rc EQUAL 0)
    message(FATAL_ERROR
        "verify_external_consumer: ${binary} exited with rc=${run_rc}\n"
        "STDOUT:\n${run_out}\nSTDERR:\n${run_err}")
endif()

message(STATUS
    "verify_external_consumer: OK — helper configures, builds, and "
    "produces a runnable binary from an external project.")
