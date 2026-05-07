# cmake_sturm_mode_rebuild.cmake — sturm-mixe ctest driver (Plan §3 P1, OQ5).
#
# Verifies that toggling `-DSTURM_MODE=...` between configures of the
# same build dir invalidates the `sturm::frontend` INTERFACE library's
# `STURM_MODE_DEFAULT` compile definition and forces a rebuild of any
# TU linked to the interface library. This is the OQ5 (rebuild semantics)
# contract from `docs/impl_plan_frontend_simplification.md`.
#
# Method
# ------
# 1. Write a tiny scratch CMake project that includes the parent's
#    `cmake/SturmFrontend.cmake` and links a probe TU to `sturm::frontend`.
# 2. Configure with `STURM_MODE=APPEND`, fully build, capture the probe
#    object file's mtime baseline.
# 3. Reconfigure the SAME build dir with `STURM_MODE=SIMULATE`, build
#    again, capture the new mtime.
# 4. Assert the mtime changed — i.e. the probe TU was recompiled.
#
# A second pass also asserts the OPPOSITE direction (SIMULATE → APPEND)
# rebuilds, so the contract is symmetric.
#
# Inputs (passed via `-D` from `tests/CMakeLists.txt`):
#   STURM_SOURCE_DIR — absolute path to the STURM source tree.
#   SCRATCH_ROOT     — absolute path to a writable scratch root.

cmake_minimum_required(VERSION 3.16)

foreach(_required_arg STURM_SOURCE_DIR SCRATCH_ROOT)
    if(NOT DEFINED ${_required_arg})
        message(FATAL_ERROR
            "cmake_sturm_mode_rebuild: ${_required_arg} not set.")
    endif()
endforeach()
if(NOT EXISTS "${STURM_SOURCE_DIR}/cmake/SturmFrontend.cmake")
    message(FATAL_ERROR
        "cmake_sturm_mode_rebuild: missing cmake/SturmFrontend.cmake under "
        "${STURM_SOURCE_DIR}.")
endif()

# ── Build the scratch project (same shape as cmake_sturm_mode_flag) ───────
set(_proj_dir "${SCRATCH_ROOT}/proj")
file(REMOVE_RECURSE "${_proj_dir}")
file(MAKE_DIRECTORY "${_proj_dir}")

file(WRITE "${_proj_dir}/CMakeLists.txt"
"cmake_minimum_required(VERSION 3.16)
project(sturm_mode_rebuild_probe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(\"${STURM_SOURCE_DIR}/cmake/SturmFrontend.cmake\")
add_library(probe_obj OBJECT probe.cpp)
target_link_libraries(probe_obj PRIVATE sturm::frontend)
add_executable(probe \$<TARGET_OBJECTS:probe_obj>)
")

# The probe TU only needs to depend on STURM_MODE_DEFAULT — we encode the
# value into a constexpr the TU exports so the compiler must re-emit the
# object file when the macro changes. The enum mirrors the C ABI from
# `include/sturm/core/core.h` (same values: COUNT_ONLY=0, APPEND=1,
# SIMULATE=2) so the macro expansion `STURM_MODE_<X>` resolves.
file(WRITE "${_proj_dir}/probe.cpp"
"enum sturm_mode_probe {
    STURM_MODE_COUNT_ONLY = 0,
    STURM_MODE_APPEND     = 1,
    STURM_MODE_SIMULATE   = 2
};
#ifndef STURM_MODE_DEFAULT
#  error \"STURM_MODE_DEFAULT not defined — sturm::frontend interface broken\"
#endif
extern const int kSturmModeDefault;
const int kSturmModeDefault = STURM_MODE_DEFAULT;
int main() { return kSturmModeDefault; }
")

# ── Helpers ───────────────────────────────────────────────────────────────
function(_csmr_configure build_dir mode)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-S" "${_proj_dir}" "-B" "${build_dir}"
                "-DSTURM_MODE=${mode}"
        OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "cmake_sturm_mode_rebuild: configure with STURM_MODE=${mode} "
            "FAILED (exit ${_rc}).\nStdout: ${_out}\nStderr: ${_err}")
    endif()
endfunction()

function(_csmr_build build_dir)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${build_dir}" --parallel 2
        OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "cmake_sturm_mode_rebuild: build under ${build_dir} FAILED "
            "(exit ${_rc}).\nStdout: ${_out}\nStderr: ${_err}")
    endif()
endfunction()

# Locate the probe object file. The exact name varies by generator;
# Unix Makefiles emits `<build>/CMakeFiles/probe_obj.dir/probe.cpp.o`,
# Ninja emits something similar. We glob for `probe.cpp.o` under the
# build dir and assert exactly one match.
function(_csmr_find_probe_obj build_dir out_path)
    file(GLOB_RECURSE _hits "${build_dir}/probe.cpp.o"
                            "${build_dir}/probe.cpp.obj")
    list(LENGTH _hits _n)
    if(NOT _n EQUAL 1)
        message(FATAL_ERROR
            "cmake_sturm_mode_rebuild: expected exactly one probe.cpp.o "
            "under ${build_dir}, found ${_n}: ${_hits}")
    endif()
    list(GET _hits 0 _path)
    set(${out_path} "${_path}" PARENT_SCOPE)
endfunction()

function(_csmr_mtime path out_mtime)
    file(TIMESTAMP "${path}" _ts UTC)
    set(${out_mtime} "${_ts}" PARENT_SCOPE)
endfunction()

# ── Case 1: APPEND → SIMULATE forces rebuild ──────────────────────────────
set(_build_dir "${SCRATCH_ROOT}/build")
file(REMOVE_RECURSE "${_build_dir}")

_csmr_configure("${_build_dir}" APPEND)
_csmr_build("${_build_dir}")
_csmr_find_probe_obj("${_build_dir}" _obj_path)
_csmr_mtime("${_obj_path}" _mtime_append)
message(STATUS "cmake_sturm_mode_rebuild: APPEND build → ${_obj_path} "
               "mtime=${_mtime_append}")

# Sleep briefly so the filesystem mtime resolution (typically 1 s on
# ext4 / APFS) cannot mask a genuine rebuild.
execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 2)

_csmr_configure("${_build_dir}" SIMULATE)
_csmr_build("${_build_dir}")
_csmr_find_probe_obj("${_build_dir}" _obj_path2)
_csmr_mtime("${_obj_path2}" _mtime_simulate)
message(STATUS "cmake_sturm_mode_rebuild: SIMULATE build → ${_obj_path2} "
               "mtime=${_mtime_simulate}")

if(_mtime_append STREQUAL _mtime_simulate)
    message(FATAL_ERROR
        "cmake_sturm_mode_rebuild: probe.cpp.o mtime UNCHANGED across "
        "APPEND→SIMULATE flag flip. The OQ5 contract says toggling "
        "STURM_MODE invalidates dependents linked to sturm::frontend; "
        "if the mtime did not move, the INTERFACE_COMPILE_DEFINITIONS "
        "did not propagate to the probe's compile rule. Confirm "
        "cmake/SturmFrontend.cmake re-runs target_compile_definitions on "
        "every configure with the cache value substituted.\n"
        "  obj path        : ${_obj_path}\n"
        "  APPEND mtime    : ${_mtime_append}\n"
        "  SIMULATE mtime  : ${_mtime_simulate}")
endif()
message(STATUS "cmake_sturm_mode_rebuild: APPEND→SIMULATE forced rebuild "
               "(mtime delta detected).")

# ── Case 2: SIMULATE → APPEND forces rebuild (symmetric) ──────────────────
execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 2)
_csmr_configure("${_build_dir}" APPEND)
_csmr_build("${_build_dir}")
_csmr_find_probe_obj("${_build_dir}" _obj_path3)
_csmr_mtime("${_obj_path3}" _mtime_append2)
if(_mtime_append2 STREQUAL _mtime_simulate)
    message(FATAL_ERROR
        "cmake_sturm_mode_rebuild: probe.cpp.o mtime UNCHANGED across "
        "SIMULATE→APPEND flag flip. The OQ5 contract is symmetric — "
        "both directions must rebuild.\n"
        "  SIMULATE mtime : ${_mtime_simulate}\n"
        "  APPEND  mtime  : ${_mtime_append2}")
endif()
message(STATUS "cmake_sturm_mode_rebuild: SIMULATE→APPEND forced rebuild "
               "(mtime delta detected).")

message(STATUS "cmake_sturm_mode_rebuild: PASS — toggling STURM_MODE "
               "rebuilds dependents linked to sturm::frontend (OQ5).")
