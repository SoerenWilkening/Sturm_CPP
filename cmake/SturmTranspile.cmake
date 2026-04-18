# cmake/SturmTranspile.cmake — the add_quantum_executable() helper.
#
# Defines one public function, `add_quantum_executable(target src1 src2 ...)`,
# which is the documented build-system entry point for downstream consumers of
# the STURM transpiler pipeline.
#
# Behavior
#   Every source file is routed through sturm-transpile. A per-source
#   add_custom_command writes the rewritten output to
#     ${CMAKE_BINARY_DIR}/sturm_gen/<relpath-of-source>
#   and declares the generated file as the actual compile unit. The
#   custom-command's DEPENDS list contains the original source AND the
#   sturm-transpile binary, so:
#     - editing the source re-triggers regeneration (normal incremental
#       rebuild behavior);
#     - rebuilding the transpiler (e.g. after touching transpiler/src/*.cpp)
#       re-triggers regeneration of every generated file, which is the
#       right invalidation discipline — a transpiler change CAN alter the
#       output for an unchanged input.
#   The GENERATED source-file property is set explicitly so the compile
#   step does not complain about a file that does not yet exist at
#   configure time.
#
# Idempotency
#   The helper uses an include-guard pattern so multiple includes in the
#   same CMake run are harmless. This matters because root CMakeLists.txt
#   and downstream FetchContent users may both pull the file in.

if(DEFINED _STURM_TRANSPILE_CMAKE_INCLUDED)
    return()
endif()
set(_STURM_TRANSPILE_CMAKE_INCLUDED ON)

# ── Public entry point ────────────────────────────────────────────────────
# Usage:
#     add_quantum_executable(<target> <source> [<source> ...])
#
# Accepts one or more source files. Relative paths are resolved against
# CMAKE_CURRENT_SOURCE_DIR. Absolute paths are respected verbatim.
#
# Side effects:
#   - Defines an executable target `<target>`.
#   - Links `<target>` against `sturm` PRIVATE if that target exists.
#   - Defines one add_custom_command per source with OUTPUT under
#     ${CMAKE_BINARY_DIR}/sturm_gen/ and DEPENDS on the source plus the
#     `sturm-transpile` target.
function(add_quantum_executable target)
    set(sources ${ARGN})
    if(NOT sources)
        message(FATAL_ERROR
            "add_quantum_executable(${target}): no source files provided. "
            "Pass one or more .cpp files after the target name.")
    endif()

    set(compile_units "")
    set(_deps "")

    foreach(src ${sources})
        # Normalize to an absolute path for the custom-command DEPENDS and
        # for computing the relative path under sturm_gen/.
        if(IS_ABSOLUTE "${src}")
            set(abs_src "${src}")
        else()
            set(abs_src "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
        endif()
        # cmake_path(NORMAL_PATH) would be nicer but is 3.20+; we rely on
        # CMake 3.16 here, so use get_filename_component(REALPATH ...) to
        # collapse any "../" segments the caller passed in.
        get_filename_component(abs_src "${abs_src}" ABSOLUTE)

        # sturm-transpile must be a known target before we wire a
        # custom-command DEPENDS on it. In the STURM monorepo the target is
        # added by transpiler/CMakeLists.txt. External consumers must bring
        # their own: fall back to a fatal-error with an actionable hint if
        # the target is missing.
        if(NOT TARGET sturm-transpile)
            message(FATAL_ERROR
                "add_quantum_executable(${target}): the `sturm-transpile` "
                "target is not defined. Ensure the STURM transpiler "
                "subdirectory is added (add_subdirectory(transpiler) or "
                "find_package(sturm)) before calling add_quantum_executable.")
        endif()

        # Compute the relpath under CMAKE_SOURCE_DIR so the generated tree
        # mirrors the source tree. Sources that live outside CMAKE_SOURCE_DIR
        # (e.g. in the build tree) still need a deterministic layout — fall
        # back to just the filename in that case, keeping the helper usable
        # from script-generated sources.
        file(RELATIVE_PATH rel_src "${CMAKE_SOURCE_DIR}" "${abs_src}")
        set(_src_outside_source_dir FALSE)
        if(rel_src MATCHES "^\\.\\.")
            get_filename_component(rel_src "${abs_src}" NAME)
            set(_src_outside_source_dir TRUE)
        endif()

        set(gen_src "${CMAKE_BINARY_DIR}/sturm_gen/${rel_src}")
        get_filename_component(gen_dir "${gen_src}" DIRECTORY)

        # The sturm-transpile driver computes the output file layout via
        # resolve_output_path(input, output_dir):
        #   - absolute input  → <output_dir>/<basename(input)>
        #   - relative input  → <output_dir>/<input> (mirrors tree)
        # To make the generated file land at the mirrored location, we
        # invoke the transpiler with a RELATIVE input path and set
        # WORKING_DIRECTORY to CMAKE_SOURCE_DIR for in-tree sources.
        if(_src_outside_source_dir)
            set(_input_for_tool "${abs_src}")
            set(_working_dir "${CMAKE_CURRENT_BINARY_DIR}")
        else()
            set(_input_for_tool "${rel_src}")
            set(_working_dir "${CMAKE_SOURCE_DIR}")
        endif()

        # Propagate the compile context the source actually needs to parse
        # cleanly. sturm-transpile runs Clang via libTooling without a
        # compilation database, so forward the include paths and feature
        # defines explicitly.
        #
        # STURM_ANCILLA_CAPACITY must match the value baked into the rest of
        # the build (root CMakeLists.txt add_compile_definitions) and the
        # value exposed to out-of-tree find_package(sturm) consumers via
        # sturmConfig.cmake (which sets this variable before include()ing
        # this file). Referencing ${STURM_ANCILLA_CAPACITY} here keeps the
        # transpile pass and the compile pass in sync, preventing divergent
        # macro-driven template instantiations.
        if(NOT DEFINED STURM_ANCILLA_CAPACITY)
            message(FATAL_ERROR
                "add_quantum_executable(${target}): STURM_ANCILLA_CAPACITY "
                "is not defined. In-tree builds set it in the top-level "
                "CMakeLists.txt; find_package(sturm) consumers receive it "
                "from sturmConfig.cmake. If you are including "
                "SturmTranspile.cmake directly, set STURM_ANCILLA_CAPACITY "
                "before the include().")
        endif()
        set(_xa_args
            "--extra-arg=-std=c++20"
            "--extra-arg=-I${CMAKE_SOURCE_DIR}/include"
            "--extra-arg=-DSTURM_BACKEND_ENABLED=1"
            "--extra-arg=-DSTURM_ANCILLA_CAPACITY=${STURM_ANCILLA_CAPACITY}")

        add_custom_command(
            OUTPUT "${gen_src}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${gen_dir}"
            COMMAND $<TARGET_FILE:sturm-transpile>
                    "${_input_for_tool}"
                    --output-dir "${CMAKE_BINARY_DIR}/sturm_gen"
                    ${_xa_args}
            WORKING_DIRECTORY "${_working_dir}"
            DEPENDS "${abs_src}" sturm-transpile
            COMMENT "sturm-transpile ${rel_src}"
            VERBATIM)

        # Mark the generated file so CMake does not complain that a source
        # listed in add_executable does not exist on disk at configure time.
        set_source_files_properties("${gen_src}" PROPERTIES GENERATED TRUE)

        list(APPEND compile_units "${gen_src}")
        list(APPEND _deps "${gen_src}")
    endforeach()

    add_executable(${target} ${compile_units})

    # Link the STURM interface library if it is available. It is the
    # standard way a consumer gains access to `<sturm/sturm.hpp>` and the
    # public compile-feature set (C++20).
    if(TARGET sturm)
        target_link_libraries(${target} PRIVATE sturm)
    else()
        message(STATUS
            "add_quantum_executable(${target}): `sturm` target not found; "
            "skipping target_link_libraries. Consumers should link `sturm` "
            "manually or provide a find_package(sturm) wrapper.")
    endif()

    # A convenience aggregate custom-target wrapping all generated outputs
    # lets downstream CMake logic (e.g. `make sturm_gen`) force regeneration
    # without having to build the final executable.
    if(_deps)
        add_custom_target(${target}_xpile DEPENDS ${_deps})
        if(NOT TARGET sturm_gen)
            add_custom_target(sturm_gen)
        endif()
        add_dependencies(sturm_gen ${target}_xpile)
    endif()
endfunction()
