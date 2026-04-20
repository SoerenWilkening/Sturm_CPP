# check_source_map_diagnostic.cmake — PM2-8 plugin-mode source-map probe.
#
# Invoked by `tests/transpiler/CMakeLists.txt` with:
#   -DCLANGXX=<abs path>     — host clang++ next to the LLVM we linked
#   -DPLUGIN=<abs path>      — libsturm-transpile-plugin.{so,dylib}
#   -DSOURCE=<abs path>      — tests/transpiler/fixtures/
#                              source_map_diagnostic_input.cpp
#   -DOUTPUT_DIR=<abs path>  — scratch directory for the `.o` output
#   -DEXPECTED_LINE=<int>    — the 1-based user-source line that the
#                              plugin diagnostic MUST cite (the line of the
#                              `WHEN((b | c) & 42)` statement in SOURCE).
#
# Contract (PM2-8 acceptance)
# ---------------------------
# Compiling SOURCE with `clang++ -fplugin=PLUGIN -c` MUST fail (non-zero
# return code — the fixture plants a deliberate `int` where `qbool` is
# expected) AND the captured stderr MUST:
#
#   1. contain the exact substring
#      `<basename(SOURCE)>:<EXPECTED_LINE>:`
#      — proving that Clang's diagnostic cites the user's source file
#      and the user's originating line, NOT a synthesized buffer line
#      inside the rewritten memory image and NOT Clang's anonymous
#      `<memory-buffer>` pseudo-path.
#
#   2. NOT contain the substring `<memory-buffer>` — Clang's fallback
#      filename when a `MemoryBuffer` is not given an explicit path.
#      The plugin's nested `CompilerInvocation` hands the rewritten
#      bytes through `MemoryBuffer::getMemBufferCopy(rewritten,
#      input_path)` (see `transpiler/src/plugin.cpp:346`) specifically
#      so diagnostics retain the user's filename. If stderr surfaces
#      `<memory-buffer>`, that plumbing is broken.
#
# Together these two checks cover the Phase-M source-map guarantees for
# the plugin-driver path end-to-end:
#   - filename plumbing via `getMemBufferCopy(input_path)`
#     (PM2 prerequisite, shipped before PM2-1),
#   - `#line` directive emission in the rewritten buffer (PM2-1..PM2-5),
#     which anchors synthesized statements back at the user's originating
#     expression line.
#
# The fixture's `WHEN((b | c) & 42)` expression is rejected at the parent
# parse (mismatched `operator&(qbool, int)`) and the plugin's nested parse
# re-emits the same diagnostic against the rewritten buffer; in both cases
# the diagnostic's filename+line is the user's authored location, which
# is what we assert here.
#
# Script exits 0 on success, FATAL_ERROR on any contract violation.

foreach(var CLANGXX PLUGIN SOURCE OUTPUT_DIR EXPECTED_LINE)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_source_map_diagnostic: ${var} not set")
    endif()
endforeach()

foreach(path "${CLANGXX}" "${PLUGIN}" "${SOURCE}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "check_source_map_diagnostic: required file missing: ${path}")
    endif()
endforeach()

# Fresh scratch dir. A stale .o from a previous run must not obscure the
# current run's stderr.
file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

get_filename_component(_src_dir  "${SOURCE}" DIRECTORY)
get_filename_component(_src_name "${SOURCE}" NAME)

set(_obj "${OUTPUT_DIR}/source_map_diagnostic_input.o")

# Run clang++ from the fixture's own directory so diagnostics cite a bare
# basename (`source_map_diagnostic_input.cpp:<line>:`) rather than an
# absolute path — matches the Phase-M source-map emitter's behaviour
# (see `transpiler/src/source_map.cpp:111` — the emitter basenames the
# `#line` filename on purpose for byte-reproducible snapshots) and keeps
# this assertion string stable across CI runners whose workspaces have
# different absolute paths.
execute_process(
    COMMAND "${CLANGXX}"
            -std=c++20
            "-fplugin=${PLUGIN}"
            -c "${_src_name}"
            -o "${_obj}"
    WORKING_DIRECTORY "${_src_dir}"
    RESULT_VARIABLE _compile_rc
    OUTPUT_VARIABLE _compile_out
    ERROR_VARIABLE  _compile_err)

# The fixture is deliberately ill-typed; a zero return code would indicate
# either (a) clang accidentally accepted the `qbool & int` expression, or
# (b) the plugin swallowed the diagnostic. Either is a regression.
if(_compile_rc EQUAL 0)
    message(FATAL_ERROR
        "check_source_map_diagnostic: clang++ exited 0, but the fixture "
        "plants a deliberate type error in `WHEN((b | c) & 42)`. This "
        "indicates either the compiler or the plugin is masking the "
        "diagnostic.\n"
        "stdout:\n${_compile_out}\n"
        "stderr:\n${_compile_err}")
endif()

# Negative assertion: the `<memory-buffer>` fallback must not appear. If
# the plugin's nested `CompilerInvocation` lost the original filename
# (regressing `MemoryBuffer::getMemBufferCopy(rewritten, input_path)`
# in `transpiler/src/plugin.cpp`), Clang would format diagnostic
# locations as `<memory-buffer>:<line>:` instead of the user's filename.
string(FIND "${_compile_err}" "<memory-buffer>" _memory_buf_pos)
if(NOT _memory_buf_pos EQUAL -1)
    message(FATAL_ERROR
        "check_source_map_diagnostic: stderr references "
        "`<memory-buffer>` — the plugin's nested CompilerInvocation "
        "dropped the user's filename when cloning the rewritten buffer. "
        "Inspect `plugin.cpp`'s `getMemBufferCopy(rewritten, input_path)` "
        "call site.\n"
        "stderr:\n${_compile_err}")
endif()

# Positive assertion: the diagnostic cites the user's file:line.
set(_required "${_src_name}:${EXPECTED_LINE}:")
string(FIND "${_compile_err}" "${_required}" _found_pos)
if(_found_pos EQUAL -1)
    message(FATAL_ERROR
        "check_source_map_diagnostic: stderr does not cite the expected "
        "user-source location.\n"
        "  required substring: ${_required}\n"
        "  source file:        ${SOURCE}\n"
        "  expected line:      ${EXPECTED_LINE}\n"
        "stderr:\n${_compile_err}")
endif()

message(STATUS "check_source_map_diagnostic: OK — clang++ "
               "diagnostic cited ${_required} and did not surface "
               "`<memory-buffer>`.")
