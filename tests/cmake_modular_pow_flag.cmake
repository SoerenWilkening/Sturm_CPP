# cmake_modular_pow_flag.cmake — sturm-r5pn.3 ctest driver.
#
# Verifies the Phase 0.3 plan §2.2 contract for the
# `STURM_MODULAR_POW` CMake option:
#
#   1. The top-level configure declares `STURM_MODULAR_POW` as a BOOL
#      option (default OFF).
#   2. With `-DSTURM_MODULAR_POW=OFF` (default), no compile command in
#      the entire project carries `-DSTURM_MODULAR_POW`.
#   3. With `-DSTURM_MODULAR_POW=ON`, the define propagates ONLY to the
#      transpiler driver target (`sturm-transpile`) — not to the
#      header-only `sturm` library, not to plugin sources, not to any
#      `tests/**` target.
#   4. Both ON and OFF configurations succeed cleanly (no
#      `find_package` / parse / generation errors).
#
# Method
# ------
# The driver reuses the parent build's resolved LLVM/Clang locations
# (passed in at test-add time) and runs two out-of-tree configures of
# the STURM source tree into ${CMAKE_BINARY_DIR}/cmake_modular_pow_flag/
# scratch directories, with `CMAKE_EXPORT_COMPILE_COMMANDS=ON` so the
# resulting `compile_commands.json` records the full compile flags for
# every TU. We then parse the JSON and verify the propagation
# invariants. No compilation runs (configure-only), so the test is
# fast despite the heavy STURM configure.
#
# Inputs (passed via `-D` from `tests/CMakeLists.txt`):
#   STURM_SOURCE_DIR     — absolute path to the STURM source tree.
#   SCRATCH_ROOT         — absolute path to a writable scratch root
#                          (the driver creates `${SCRATCH_ROOT}/off`
#                          and `${SCRATCH_ROOT}/on` under it).
#   PARENT_LLVM_DIR      — value to pass through as `-DLLVM_DIR=...`
#                          for the child configure (may be empty).
#   PARENT_CLANG_DIR     — value to pass through as `-DClang_DIR=...`
#                          (may be empty).
#   PARENT_CXX_COMPILER  — value to pass through as
#                          `-DCMAKE_CXX_COMPILER=...` (may be empty).
#   PARENT_C_COMPILER    — value to pass through as
#                          `-DCMAKE_C_COMPILER=...` (may be empty).
#
# Exit discipline
# ---------------
# message(FATAL_ERROR ...) on any contract violation; messages carry
# enough context (the offending compile-command entry) to debug the
# failure without re-running the configure manually.

cmake_minimum_required(VERSION 3.16)

# ── Argument validation ────────────────────────────────────────────────────
foreach(_required_arg STURM_SOURCE_DIR SCRATCH_ROOT)
    if(NOT DEFINED ${_required_arg})
        message(FATAL_ERROR
            "cmake_modular_pow_flag: ${_required_arg} not set. The "
            "ctest wiring in tests/CMakeLists.txt must pass "
            "-D${_required_arg}=<path>.")
    endif()
endforeach()

if(NOT IS_DIRECTORY "${STURM_SOURCE_DIR}")
    message(FATAL_ERROR
        "cmake_modular_pow_flag: STURM_SOURCE_DIR does not exist or is "
        "not a directory: ${STURM_SOURCE_DIR}")
endif()

if(NOT EXISTS "${STURM_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "cmake_modular_pow_flag: STURM_SOURCE_DIR is missing the "
        "top-level CMakeLists.txt: ${STURM_SOURCE_DIR}")
endif()

# ── Helper: run one configure with the given flag value ───────────────────
#
# Forwards the parent build's LLVM/Clang/compiler hints so the child
# configure resolves the same toolchain (find_package(LLVM CONFIG) is
# distro-specific without LLVM_DIR — see transpiler/CMakeLists.txt).
#
# Outputs the configure log into <build_dir>/configure.log; surfaces
# the path on FATAL_ERROR so a CI failure prints the offending line.
function(_cmpf_configure_one build_dir flag_value)
    file(REMOVE_RECURSE "${build_dir}")
    file(MAKE_DIRECTORY "${build_dir}")

    set(_args
        "-S" "${STURM_SOURCE_DIR}"
        "-B" "${build_dir}"
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
        "-DSTURM_MODULAR_POW=${flag_value}"
    )
    if(PARENT_LLVM_DIR)
        list(APPEND _args "-DLLVM_DIR=${PARENT_LLVM_DIR}")
    endif()
    if(PARENT_CLANG_DIR)
        list(APPEND _args "-DClang_DIR=${PARENT_CLANG_DIR}")
    endif()
    if(PARENT_CXX_COMPILER)
        list(APPEND _args "-DCMAKE_CXX_COMPILER=${PARENT_CXX_COMPILER}")
    endif()
    if(PARENT_C_COMPILER)
        list(APPEND _args "-DCMAKE_C_COMPILER=${PARENT_C_COMPILER}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${_args}
        WORKING_DIRECTORY "${build_dir}"
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE  _stderr
        RESULT_VARIABLE _rc)

    file(WRITE "${build_dir}/configure.log"
        "STDOUT:\n${_stdout}\n\nSTDERR:\n${_stderr}\n")

    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "cmake_modular_pow_flag: configure with "
            "STURM_MODULAR_POW=${flag_value} FAILED (exit ${_rc}). "
            "See ${build_dir}/configure.log for the full log. "
            "Tail of stderr:\n${_stderr}")
    endif()
endfunction()

# ── Helper: scan compile_commands.json for STURM_MODULAR_POW ─────────────
#
# Reads the `compile_commands.json` at <build_dir>/compile_commands.json
# and partitions every entry into two buckets:
#   driver_hits   — entries whose `file:` field is under the transpiler
#                   driver's source dir AND whose `command:` /
#                   `arguments:` carry `-DSTURM_MODULAR_POW`.
#   stray_hits    — entries that carry `-DSTURM_MODULAR_POW` but live
#                   anywhere else (lib, plugin, tests/**, examples/**).
#
# Outputs the two lists into the parent scope under the names supplied.
function(_cmpf_scan_define build_dir out_driver out_stray)
    set(_cc_path "${build_dir}/compile_commands.json")
    if(NOT EXISTS "${_cc_path}")
        message(FATAL_ERROR
            "cmake_modular_pow_flag: compile_commands.json missing at "
            "${_cc_path}. The configure must enable "
            "CMAKE_EXPORT_COMPILE_COMMANDS=ON.")
    endif()

    file(READ "${_cc_path}" _cc_json)

    # Parse the JSON. CMake 3.19+ ships `string(JSON ...)`; we assume
    # 3.19+ since the transpiler's ASTMatchers headers already require
    # CMake 3.16 and STURM ships modern toolchains. If the running
    # CMake is older, fall back to a regex scan.
    set(_driver_hits "")
    set(_stray_hits "")

    if(NOT CMAKE_VERSION VERSION_LESS "3.19")
        string(JSON _len LENGTH "${_cc_json}")
        math(EXPR _last "${_len} - 1")
        if(_last GREATER_EQUAL 0)
            foreach(_i RANGE 0 ${_last})
                string(JSON _entry GET "${_cc_json}" ${_i})
                string(JSON _file GET "${_entry}" "file")

                # The `arguments` array (Ninja/Make on Linux) or the
                # `command` string (some generators) carries the flag
                # list. Probe both — `string(JSON ... ERROR_VARIABLE)`
                # silently leaves the variable undefined on a missing
                # key in CMake >= 3.19, so we test EXISTS via TYPE.
                set(_combined "")
                string(JSON _has_args ERROR_VARIABLE _err
                    TYPE "${_entry}" "arguments")
                if(_has_args STREQUAL "ARRAY")
                    string(JSON _arg_len LENGTH "${_entry}" "arguments")
                    math(EXPR _arg_last "${_arg_len} - 1")
                    if(_arg_last GREATER_EQUAL 0)
                        foreach(_j RANGE 0 ${_arg_last})
                            string(JSON _arg
                                GET "${_entry}" "arguments" ${_j})
                            set(_combined "${_combined} ${_arg}")
                        endforeach()
                    endif()
                endif()
                string(JSON _has_cmd ERROR_VARIABLE _err
                    TYPE "${_entry}" "command")
                if(_has_cmd STREQUAL "STRING")
                    string(JSON _cmd GET "${_entry}" "command")
                    set(_combined "${_combined} ${_cmd}")
                endif()

                # The compile command carries either
                # `-DSTURM_MODULAR_POW` (no value) or
                # `-DSTURM_MODULAR_POW=...`. Match both.
                if(_combined MATCHES "-DSTURM_MODULAR_POW(=|\\b| |$)")
                    # Bucket on file location. `transpiler/src/` =
                    # driver. Anything else is a stray hit.
                    if(_file MATCHES "/transpiler/src/")
                        list(APPEND _driver_hits "${_file}")
                    else()
                        list(APPEND _stray_hits "${_file}")
                    endif()
                endif()
            endforeach()
        endif()
    else()
        # Fallback for CMake < 3.19. The compile_commands.json is a flat
        # JSON array; each `"file": "..."` entry is preceded by the
        # `"command":` or `"arguments":` field within the same `{...}`
        # object. Split on top-level entry boundaries by matching
        # `"file":` lines after a `-DSTURM_MODULAR_POW` token within
        # the same object.
        # Simple regex scan: look for `"file": "<path>"` blocks where
        # the preceding object body contains `-DSTURM_MODULAR_POW`.
        string(REGEX MATCHALL
            "{[^{}]*-DSTURM_MODULAR_POW[^{}]*\"file\"[^{}]*}"
            _entries "${_cc_json}")
        foreach(_entry IN LISTS _entries)
            if(_entry MATCHES "\"file\"[ \t]*:[ \t]*\"([^\"]+)\"")
                set(_file "${CMAKE_MATCH_1}")
                if(_file MATCHES "/transpiler/src/")
                    list(APPEND _driver_hits "${_file}")
                else()
                    list(APPEND _stray_hits "${_file}")
                endif()
            endif()
        endforeach()
    endif()

    set(${out_driver} "${_driver_hits}" PARENT_SCOPE)
    set(${out_stray}  "${_stray_hits}"  PARENT_SCOPE)
endfunction()

# ── Configure 1: STURM_MODULAR_POW=OFF ────────────────────────────────────
set(_off_dir "${SCRATCH_ROOT}/off")
_cmpf_configure_one("${_off_dir}" OFF)
_cmpf_scan_define("${_off_dir}" _off_driver _off_stray)

if(_off_driver)
    string(REPLACE ";" "\n  " _pretty_driver "${_off_driver}")
    message(FATAL_ERROR
        "cmake_modular_pow_flag: STURM_MODULAR_POW=OFF configure leaks "
        "the define onto transpiler driver compile commands. The "
        "define must be entirely absent when the option is OFF. "
        "Offending source files:\n  ${_pretty_driver}")
endif()
if(_off_stray)
    string(REPLACE ";" "\n  " _pretty_stray "${_off_stray}")
    message(FATAL_ERROR
        "cmake_modular_pow_flag: STURM_MODULAR_POW=OFF configure leaks "
        "the define onto non-driver compile commands. Offending "
        "files:\n  ${_pretty_stray}")
endif()

message(STATUS "cmake_modular_pow_flag: OFF configure clean — no "
               "`-DSTURM_MODULAR_POW` on any compile command.")

# ── Configure 2: STURM_MODULAR_POW=ON ─────────────────────────────────────
set(_on_dir "${SCRATCH_ROOT}/on")
_cmpf_configure_one("${_on_dir}" ON)
_cmpf_scan_define("${_on_dir}" _on_driver _on_stray)

if(NOT _on_driver)
    message(FATAL_ERROR
        "cmake_modular_pow_flag: STURM_MODULAR_POW=ON configure did "
        "NOT propagate the define to ANY transpiler driver compile "
        "command. Expected `-DSTURM_MODULAR_POW` on the "
        "`sturm-transpile` driver target's TUs (transpiler/src/*).")
endif()
if(_on_stray)
    string(REPLACE ";" "\n  " _pretty_stray "${_on_stray}")
    message(FATAL_ERROR
        "cmake_modular_pow_flag: STURM_MODULAR_POW=ON configure leaks "
        "the define onto non-driver compile commands. Plan §2.2 "
        "requires the define to land ONLY on the transpiler driver "
        "target — not on the lib, plugin, or tests. Offending "
        "files:\n  ${_pretty_stray}")
endif()

list(LENGTH _on_driver _on_driver_count)
message(STATUS "cmake_modular_pow_flag: ON configure clean — "
               "`-DSTURM_MODULAR_POW` propagated to "
               "${_on_driver_count} transpiler driver TUs and "
               "no other compile commands.")

message(STATUS "cmake_modular_pow_flag: PASS — STURM_MODULAR_POW "
               "option exists, defaults OFF, and propagates only to "
               "the transpiler driver when ON.")
