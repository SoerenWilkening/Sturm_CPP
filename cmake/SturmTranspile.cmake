# cmake/SturmTranspile.cmake — M10: the add_quantum_executable() helper.
#
# Defines one public function, `add_quantum_executable(target src1 src2 ...)`,
# which is the documented build-system entry point for downstream consumers of
# the STURM transpiler pipeline. See:
#   - docs/prd_transpiler_uncompute.md (Build Integration section)
#   - docs/implementation_plan_transpiler_uncompute.md §M10
#
# Behavior
#   STURM_TRANSPILE=ON
#     Each source file is routed through sturm-transpile. A per-source
#     add_custom_command writes the rewritten output to
#       ${CMAKE_BINARY_DIR}/sturm_gen/<relpath-of-source>
#     and declares the generated file as the actual compile unit. The
#     custom-command's DEPENDS list contains the original source AND the
#     sturm-transpile binary, so:
#       - editing the source re-triggers regeneration (normal incremental
#         rebuild behavior);
#       - rebuilding the transpiler (e.g. after touching transpiler/src/*.cpp)
#         re-triggers regeneration of every generated file, which is the
#         right invalidation discipline — a transpiler change CAN alter the
#         output for an unchanged input.
#     The GENERATED source-file property is set explicitly so the compile
#     step does not complain about a file that does not yet exist at
#     configure time.
#
#   STURM_TRANSPILE=OFF
#     The helper compiles each source file directly, without touching
#     sturm-transpile. No custom-command is created, no sturm_gen/ artifact
#     is produced, and the build does not depend on LLVM/Clang being
#     installed. This is the "degrades gracefully" contract in the PRD:
#     a consumer writing `add_quantum_executable(app foo.cpp)` must build
#     cleanly whether or not the transpiler is enabled. Downstream targets
#     still get `target_link_libraries(... PRIVATE sturm)` so that the
#     header-only interface library's include paths and compile features
#     are propagated.
#
# Degradation-safety notes
#   - The helper does NOT hard-require a `sturm` target to exist. It
#     checks with TARGET before attempting to link, and emits a status
#     message (not a fatal error) when the target is absent. The chief
#     use case for the absent-target branch is configure-time probing in
#     the external consumer tests; real downstream projects are expected
#     to provide their own `sturm` target (via find_package(sturm), a
#     FetchContent fallback, or a vendored add_library) before calling
#     add_quantum_executable.
#   - The helper does NOT hard-require sturm-transpile to exist at
#     include() time. The TARGET check is deferred to the call site so
#     that consumers who add the transpiler via a later add_subdirectory
#     or find_package keep working. When STURM_TRANSPILE=ON but
#     sturm-transpile is not a known target, we emit a fatal error with
#     an actionable hint.
#
# Idempotency
#   The helper uses an include-guard pattern so multiple includes in the
#   same CMake run are harmless. This matters because root CMakeLists.txt
#   and downstream FetchContent users may both pull the file in.

if(DEFINED _STURM_TRANSPILE_CMAKE_INCLUDED)
    return()
endif()
set(_STURM_TRANSPILE_CMAKE_INCLUDED ON)

# ── Sanity: surface a helpful error if a caller invokes the helper in a
#    consumer that expects STURM_TRANSPILE to gate transpile-time work but
#    forgot to declare the option first.
if(NOT DEFINED STURM_TRANSPILE)
    # Do NOT force the variable into the cache — some consumers may have
    # a different default policy. Just warn so the user knows the helper
    # will treat it as OFF.
    message(STATUS
        "SturmTranspile.cmake: STURM_TRANSPILE is undefined; treating as OFF. "
        "Consumers should declare option(STURM_TRANSPILE ...) before "
        "including this file if they want transpiled builds.")
endif()

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
#   - Under STURM_TRANSPILE=ON, defines one add_custom_command per source
#     with OUTPUT under ${CMAKE_BINARY_DIR}/sturm_gen/ and DEPENDS on the
#     source plus the `sturm-transpile` target.
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

        if(STURM_TRANSPILE)
            # sturm-transpile must be a known target before we wire a
            # custom-command DEPENDS on it. In the STURM monorepo the
            # target is added by transpiler/CMakeLists.txt, gated on the
            # same STURM_TRANSPILE option. External consumers must bring
            # their own: fall back to a fatal-error with an actionable
            # hint if the target is missing.
            if(NOT TARGET sturm-transpile)
                message(FATAL_ERROR
                    "add_quantum_executable(${target}): STURM_TRANSPILE=ON "
                    "but the `sturm-transpile` target is not defined. "
                    "Ensure the STURM transpiler subdirectory is added "
                    "(add_subdirectory(transpiler) or find_package(sturm)) "
                    "before calling add_quantum_executable, or set "
                    "-DSTURM_TRANSPILE=OFF to compile the original source.")
            endif()

            # Compute the relpath under CMAKE_SOURCE_DIR so the generated
            # tree mirrors the source tree. Sources that live outside
            # CMAKE_SOURCE_DIR (e.g. in the build tree) still need a
            # deterministic layout — fall back to just the filename in
            # that case, keeping the helper usable from script-generated
            # sources.
            file(RELATIVE_PATH rel_src "${CMAKE_SOURCE_DIR}" "${abs_src}")
            set(_src_outside_source_dir FALSE)
            if(rel_src MATCHES "^\\.\\.")
                # Source is outside CMAKE_SOURCE_DIR. Use just the file
                # name to keep the generated path stable.
                get_filename_component(rel_src "${abs_src}" NAME)
                set(_src_outside_source_dir TRUE)
            endif()

            set(gen_src "${CMAKE_BINARY_DIR}/sturm_gen/${rel_src}")

            # Ensure the sturm_gen/ parent directory exists when the
            # transpile step runs. sturm-transpile itself creates leading
            # directories for its output, but being explicit here lets
            # the custom-command COMMENT show up in build logs even when
            # the output dir is completely fresh.
            get_filename_component(gen_dir "${gen_src}" DIRECTORY)

            # The sturm-transpile driver computes the output file layout
            # via resolve_output_path(input, output_dir):
            #   - absolute input  → <output_dir>/<basename(input)>
            #   - relative input  → <output_dir>/<input> (mirrors tree)
            # To make the generated file land at the mirrored location
            # (<CMAKE_BINARY_DIR>/sturm_gen/<relpath>), we invoke the
            # transpiler with a RELATIVE input path and set WORKING_DIRECTORY
            # to CMAKE_SOURCE_DIR for in-tree sources. Sources from outside
            # the source tree fall through to the absolute-path case and
            # land at <output_dir>/<basename>, per the rel_src fallback
            # above.
            if(_src_outside_source_dir)
                set(_input_for_tool "${abs_src}")
                set(_working_dir "${CMAKE_CURRENT_BINARY_DIR}")
            else()
                set(_input_for_tool "${rel_src}")
                set(_working_dir "${CMAKE_SOURCE_DIR}")
            endif()

            add_custom_command(
                OUTPUT "${gen_src}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${gen_dir}"
                COMMAND $<TARGET_FILE:sturm-transpile>
                        "${_input_for_tool}"
                        --output-dir "${CMAKE_BINARY_DIR}/sturm_gen"
                WORKING_DIRECTORY "${_working_dir}"
                DEPENDS "${abs_src}" sturm-transpile
                COMMENT "sturm-transpile ${rel_src}"
                VERBATIM)

            # Mark the generated file so CMake does not complain that a
            # source listed in add_executable does not exist on disk at
            # configure time.
            set_source_files_properties("${gen_src}" PROPERTIES GENERATED TRUE)

            list(APPEND compile_units "${gen_src}")
            list(APPEND _deps "${gen_src}")
        else()
            # Transpile pipeline disabled: pass the original source
            # through to the compiler unchanged. No custom-command, no
            # dependency on sturm-transpile, no LLVM/Clang requirement.
            list(APPEND compile_units "${abs_src}")
        endif()
    endforeach()

    add_executable(${target} ${compile_units})

    # Link the STURM interface library if it is available. It is the
    # standard way a consumer gains access to `<sturm/sturm.hpp>` and the
    # public compile-feature set (C++20). Skipping gracefully when the
    # target is not defined keeps the helper usable in degraded probe
    # configurations — real consumers will always have `sturm` on hand.
    if(TARGET sturm)
        target_link_libraries(${target} PRIVATE sturm)
    else()
        message(STATUS
            "add_quantum_executable(${target}): `sturm` target not found; "
            "skipping target_link_libraries. Consumers should link `sturm` "
            "manually or provide a find_package(sturm) wrapper.")
    endif()

    # A convenience aggregate custom-target wrapping all generated
    # outputs lets downstream CMake logic (e.g. `make sturm_gen`) force
    # regeneration without having to build the final executable. Not
    # required by the PRD but costs one line and is useful for debugging.
    if(_deps AND NOT TARGET sturm_gen)
        add_custom_target(sturm_gen DEPENDS ${_deps})
    elseif(_deps)
        add_dependencies(sturm_gen ${_deps})
    endif()
endfunction()
