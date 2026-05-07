# test_add_quantum_executable_command_line.cmake — sturm-vr0v.1 unit test.
#
# Drives the E5.M1 contract for `add_quantum_executable` against a mocked
# transpiler binary, verifying that the dump-mode custom-command line:
#   1. resolves `$<TARGET_FILE:sturm::transpiler>` to the IMPORTED location
#      we register (i.e. the helper consumes the namespaced exported target,
#      not the legacy bare `sturm-transpile` name);
#   2. carries the expected positional + `--output-dir` + `--extra-arg=*`
#      arguments;
#   3. works in a layout that mirrors `find_package(sturm)` consumption —
#      no in-tree `sturm-transpile` build target is defined; only the
#      namespaced IMPORTED targets exist.
#
# Method
# ------
# The script writes a mini CMake project (CMakeLists.txt + a stub source)
# under `${SCRATCH_ROOT}/proj/`, where:
#   - a fake `sturm::transpiler` IMPORTED executable points at a no-op
#     shim script we drop on disk;
#   - a fake `sturm::transpile-plugin` IMPORTED library points at a
#     placeholder `.so` (unused by dump mode but required by the helper's
#     target presence checks if PLUGINS were ever passed);
#   - `STURM_TRANSPILE_MODE=dump` is selected (drives the
#     `add_custom_command` path that emits the transpiler invocation onto
#     the generated build script).
# We then `cmake -S proj -B proj/build -G "Unix Makefiles"` (configure only)
# and inspect the generated `build.make` / `Makefile` for the expected
# command line. No compilation runs.
#
# Inputs (passed via -D from tests/CMakeLists.txt):
#   STURM_SOURCE_DIR  — absolute path to the STURM source tree.
#   SCRATCH_ROOT      — absolute path to a writable scratch root.
#
# Exit discipline
# ---------------
# message(FATAL_ERROR ...) on any contract violation; the script writes
# the configure log under `${SCRATCH_ROOT}/proj/build/configure.log` and
# the offending generated build files are surfaced in the failure message.

cmake_minimum_required(VERSION 3.16)

# ── Argument validation ───────────────────────────────────────────────────
foreach(_required_arg STURM_SOURCE_DIR SCRATCH_ROOT)
    if(NOT DEFINED ${_required_arg})
        message(FATAL_ERROR
            "test_add_quantum_executable_command_line: ${_required_arg} "
            "not set. The ctest wiring in tests/CMakeLists.txt must pass "
            "-D${_required_arg}=<path>.")
    endif()
endforeach()

if(NOT IS_DIRECTORY "${STURM_SOURCE_DIR}")
    message(FATAL_ERROR
        "test_add_quantum_executable_command_line: STURM_SOURCE_DIR does "
        "not exist or is not a directory: ${STURM_SOURCE_DIR}")
endif()

# ── Set up the mock project tree ──────────────────────────────────────────
set(_proj_dir "${SCRATCH_ROOT}/proj")
set(_build_dir "${_proj_dir}/build")
file(REMOVE_RECURSE "${_proj_dir}")
file(MAKE_DIRECTORY "${_proj_dir}")

# Mock transpiler binary. We make it executable and write a portable
# shebang so CMake's TARGET_FILE-resolved path is a runnable file (not
# strictly required for configure-only tests, but keeps the fixture
# realistic and lets a future test invoke `cmake --build` on the
# generated tree without changes).
set(_mock_xpile "${_proj_dir}/mock_sturm_transpile.sh")
file(WRITE "${_mock_xpile}"
"#!/bin/sh
# Mocked sturm-transpile (sturm-vr0v.1 unit fixture). No-op.
echo \"mock_sturm_transpile invoked: $@\"
exit 0
")
execute_process(COMMAND chmod +x "${_mock_xpile}")

# Placeholder plugin file. dump-mode never references it, but keeping a
# real path on disk lets us also smoke the plugin-mode shape later
# without separate fixture wiring.
set(_mock_plugin "${_proj_dir}/mock_sturm_transpile_plugin.so")
file(WRITE "${_mock_plugin}" "stub")

# Stub source the helper will route through dump mode. Just a valid
# C++20 TU; we never compile it.
set(_stub_src "${_proj_dir}/stub_quantum.cpp")
file(WRITE "${_stub_src}" "int main() { return 0; }\n")

# Mock CMakeLists. Define IMPORTED targets for sturm::transpiler and
# sturm::transpile-plugin BEFORE including SturmTranspile.cmake — this
# is the exact shape sturmConfig.cmake.in produces for find_package(sturm)
# consumers (PRD §3.5: helper must work from an installed prefix).
set(_proj_cmakelists "${_proj_dir}/CMakeLists.txt")
file(WRITE "${_proj_cmakelists}"
"cmake_minimum_required(VERSION 3.16)
project(sturm_aqe_unit_harness CXX)

# Force dump mode so the helper emits an add_custom_command whose argv we
# can grep on the generated build script. Plugin mode would emit
# target-level COMPILE_OPTIONS, which are harder to verify without a real
# clang to invoke.
set(STURM_TRANSPILE_MODE dump CACHE STRING \"\" FORCE)

# Mock the namespaced exported targets — exactly the shape of
# sturmConfig.cmake.in (sturm-vr0v.1 / PRD §3.5).
add_executable(sturm::transpiler IMPORTED)
set_target_properties(sturm::transpiler PROPERTIES
    IMPORTED_LOCATION \"${_mock_xpile}\")
add_library(sturm::transpile-plugin SHARED IMPORTED)
set_target_properties(sturm::transpile-plugin PROPERTIES
    IMPORTED_LOCATION \"${_mock_plugin}\")

# Provide a `sturm` target the helper tries to link the executable
# against. INTERFACE IMPORTED matches what sturmConfig.cmake.in installs.
add_library(sturm INTERFACE IMPORTED)

include(\"${STURM_SOURCE_DIR}/cmake/SturmTranspile.cmake\")

add_quantum_executable(unit_target ${_stub_src})
")

# ── Configure (no build) ──────────────────────────────────────────────────
file(MAKE_DIRECTORY "${_build_dir}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${_proj_dir}"
        -B "${_build_dir}"
        -G "Unix Makefiles"
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE  _stderr
    RESULT_VARIABLE _rc)

file(WRITE "${_build_dir}/configure.log"
    "STDOUT:\n${_stdout}\n\nSTDERR:\n${_stderr}\n")

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "test_add_quantum_executable_command_line: harness configure "
        "FAILED (exit ${_rc}). See ${_build_dir}/configure.log. "
        "Tail of stderr:\n${_stderr}")
endif()

# ── Inspect the generated build script for the transpile invocation ──────
# CMake's "Unix Makefiles" generator writes the per-target build rules
# under `CMakeFiles/<target>.dir/build.make`. The OUTPUT of the
# add_custom_command is `${CMAKE_BINARY_DIR}/sturm_gen/<rel>/<src>` and
# the recipe line carries the full transpiler argv with all generator
# expressions resolved. We GLOB every `*.make` under CMakeFiles/ and
# concatenate them so the test does not depend on the exact target-dir
# name CMake chooses (the helper creates `unit_target_xpile` and
# `sturm_gen` companions in addition to `unit_target`).
file(GLOB_RECURSE _make_files
    "${_build_dir}/CMakeFiles/*.make"
    "${_build_dir}/CMakeFiles/*.cmake")
if(NOT _make_files)
    message(FATAL_ERROR
        "test_add_quantum_executable_command_line: no build.make / .cmake "
        "files generated under ${_build_dir}/CMakeFiles. Configure may "
        "have produced an empty target graph.")
endif()

set(_combined "")
foreach(_mk IN LISTS _make_files)
    file(READ "${_mk}" _content)
    set(_combined "${_combined}\n${_content}")
endforeach()

# Assertion 1: the resolved TARGET_FILE for sturm::transpiler is the
# mock binary path. If the helper accidentally reverted to a hardcoded
# `sturm-transpile` lookup, the path would be unresolved or empty.
# Use `string(FIND ...)` because the absolute path may contain regex
# metacharacters (e.g. `+` in directory names like `STURM-C++`).
string(FIND "${_combined}" "${_mock_xpile}" _xpile_pos)
if(_xpile_pos EQUAL -1)
    message(FATAL_ERROR
        "test_add_quantum_executable_command_line: generated build "
        "scripts do not reference the mocked transpiler path. Expected "
        "to find:\n  ${_mock_xpile}\nin one of ${_build_dir}/CMakeFiles/"
        "**/*.make. The helper likely failed to resolve "
        "$<TARGET_FILE:sturm::transpiler> against the IMPORTED target "
        "(sturm-vr0v.1 / PRD §3.5 contract).")
endif()

# Assertion 2: the `--output-dir` flag points at the helper's
# `sturm_gen` tree under the project's build dir.
if(NOT _combined MATCHES "--output-dir[ \t\n\\\\]+[^\n]*sturm_gen")
    message(FATAL_ERROR
        "test_add_quantum_executable_command_line: generated build "
        "scripts are missing the `--output-dir <build>/sturm_gen` "
        "argument on the transpile recipe.")
endif()

# Assertion 3: the canonical extra-arg flags are present.
foreach(_flag
        "--extra-arg=-std=c\\+\\+20"
        "--extra-arg=-DSTURM_BACKEND_ENABLED=1")
    if(NOT _combined MATCHES "${_flag}")
        message(FATAL_ERROR
            "test_add_quantum_executable_command_line: generated build "
            "scripts are missing the expected transpile flag matching "
            "/${_flag}/. Inspected build files under "
            "${_build_dir}/CMakeFiles/.")
    endif()
endforeach()

# Assertion 4: the legacy bare `sturm-transpile` target was NOT silently
# defined or referenced — our mock harness only registers the namespaced
# `sturm::transpiler`, so the helper must use that. (A regression that
# fell back to `$<TARGET_FILE:sturm-transpile>` would either fail
# configure outright OR resolve to an empty string here.)
# We can not directly check "no such target" post-configure, but we can
# check that the recipe line pointing at the mock path appears at least
# once — already covered by Assertion 1. This block is a comment-anchor
# for the contract.

message(STATUS "test_add_quantum_executable_command_line: PASS — helper "
               "resolves $<TARGET_FILE:sturm::transpiler> against the "
               "IMPORTED target and emits the expected dump-mode argv.")
