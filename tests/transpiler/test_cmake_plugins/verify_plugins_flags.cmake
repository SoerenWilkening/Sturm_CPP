# verify_plugins_flags.cmake — PM4-5 ctest probe.
#
# Invoked by tests/transpiler/test_cmake_plugins/CMakeLists.txt with:
#   -DOPTIONS_FILE=<abs path>
#     A file emitted by `file(GENERATE ...)` under the test subdir's
#     binary directory. Its contents are the observed per-source
#     COMPILE_OPTIONS list for the PM4-5 fixture, one token per line.
#     Generator expressions have been fully resolved by the time this
#     file exists (that is the whole point of `file(GENERATE)`).
#
#   -DPLUGIN_FILE=<abs path>
#     The expected absolute path of `sturm-pm4-demo-plugin` — its
#     `$<TARGET_FILE:...>` resolution at generate time. A strict-equality
#     check here pins the PM4-5 contract that the CMake glue uses the
#     target's own TARGET_FILE (not a stale hard-coded path).
#
# Contract (PM4-5 issue body):
#   For each path in the `PLUGINS` argument, the helper must append a
#   per-source cc1 arg pair of EXACTLY:
#       -Xclang -plugin-arg-sturm-transpile -Xclang load=<abs-path>
#   (in that order, contiguous). The cc1 spelling is mandatory because
#   the Clang driver splits `-fplugin-arg-` at the first hyphen, so the
#   hyphenated plugin name "sturm-transpile" cannot survive the driver-
#   level `-fplugin-arg-sturm-transpile-<rest>` spelling.
#
#   This test verifies the flag is present end-to-end (genex resolves,
#   the four tokens land on the compile line, and the `load=` argument
#   matches the absolute path of the referenced target).
#
# Script exits 0 on success, FATAL_ERROR on any contract violation —
# ctest surfaces the FATAL_ERROR message as the test's failure text.

cmake_minimum_required(VERSION 3.16)

foreach(var OPTIONS_FILE PLUGIN_FILE)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR
            "verify_plugins_flags: ${var} not set. Expected the ctest "
            "wiring in tests/transpiler/test_cmake_plugins/CMakeLists.txt "
            "to forward both -DOPTIONS_FILE and -DPLUGIN_FILE.")
    endif()
endforeach()

if(NOT EXISTS "${OPTIONS_FILE}")
    message(FATAL_ERROR
        "verify_plugins_flags: OPTIONS_FILE does not exist at "
        "${OPTIONS_FILE}. Either the `file(GENERATE ...)` step in the "
        "fixture's CMakeLists.txt did not run, or the build generator "
        "has not reached the generate phase yet (re-run "
        "`cmake -S . -B build` before ctest).")
endif()

if(NOT EXISTS "${PLUGIN_FILE}")
    message(FATAL_ERROR
        "verify_plugins_flags: PLUGIN_FILE '${PLUGIN_FILE}' does not "
        "exist. The `$<TARGET_FILE:sturm-pm4-demo-plugin>` genex "
        "resolved to a path that was not actually produced by the "
        "build — either the demo plugin target was not built (rebuild "
        "with `cmake --build build --target sturm-pm4-demo-plugin`) or "
        "the target's OUTPUT_NAME / SUFFIX was changed and the glue "
        "tests were not updated.")
endif()

# Read the emitted options file. Each line is one argv token from the
# source file's COMPILE_OPTIONS list as evaluated at generate time.
file(READ "${OPTIONS_FILE}" _options_contents)
string(REPLACE "\n" ";" _options_list "${_options_contents}")

# Strip empty trailing tokens (the trailing newline on the emitted file
# produces a trailing empty element after the split).
list(FILTER _options_list EXCLUDE REGEX "^$")

message(STATUS "verify_plugins_flags: observed source COMPILE_OPTIONS:")
foreach(_tok ${_options_list})
    message(STATUS "    [${_tok}]")
endforeach()

# Build the expected 4-tuple. The PM4-5 contract pins the EXACT four-
# token cc1 arg pair shape — the `-Xclang -plugin-arg-sturm-transpile`
# introduces the plugin-name scope, the second `-Xclang load=<path>`
# pair delivers the argument payload.
set(_expected
    "-Xclang"
    "-plugin-arg-sturm-transpile"
    "-Xclang"
    "load=${PLUGIN_FILE}")

# Find the index of the FIRST token in the expected tuple and walk
# forward four tokens. A contiguous match anywhere in the observed
# list counts as pass; the dump-to= tuple may appear before or after
# it — the PM4-5 contract is "load= tuple is present", not "load= tuple
# is the first group".
list(LENGTH _options_list _n_opts)
set(_hit FALSE)
set(_i 0)
while(_i LESS _n_opts)
    list(GET _options_list ${_i} _tok0)
    if("${_tok0}" STREQUAL "-Xclang")
        math(EXPR _i1 "${_i} + 1")
        math(EXPR _i2 "${_i} + 2")
        math(EXPR _i3 "${_i} + 3")
        if(_i3 LESS _n_opts)
            list(GET _options_list ${_i1} _tok1)
            list(GET _options_list ${_i2} _tok2)
            list(GET _options_list ${_i3} _tok3)
            if("${_tok1}" STREQUAL "-plugin-arg-sturm-transpile"
               AND "${_tok2}" STREQUAL "-Xclang"
               AND "${_tok3}" STREQUAL "load=${PLUGIN_FILE}")
                set(_hit TRUE)
                break()
            endif()
        endif()
    endif()
    math(EXPR _i "${_i} + 1")
endwhile()

if(NOT _hit)
    string(REPLACE ";" "\n    " _pretty "${_options_list}")
    message(FATAL_ERROR
        "verify_plugins_flags: source COMPILE_OPTIONS does not contain "
        "the expected contiguous cc1 arg pair\n"
        "    -Xclang -plugin-arg-sturm-transpile -Xclang load=${PLUGIN_FILE}\n"
        "Observed tokens:\n    ${_pretty}\n\n"
        "Likely cause: SturmTranspile.cmake's add_quantum_executable "
        "failed to append the PM4-5 `load=<path>` cc1 arg pair for the "
        "PLUGINS argument. The pair MUST use the cc1 spelling because "
        "the driver splits `-fplugin-arg-` at the first hyphen and the "
        "hyphenated plugin name `sturm-transpile` cannot survive the "
        "driver-level spelling intact.")
endif()

message(STATUS
    "verify_plugins_flags: OK — cc1 arg pair "
    "-Xclang -plugin-arg-sturm-transpile -Xclang load=${PLUGIN_FILE} "
    "found in source COMPILE_OPTIONS.")
