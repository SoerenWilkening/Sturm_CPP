# cmake/OrkanFetch.cmake — M9: Orkan vendoring via FetchContent.
#
# Attempts to fetch Orkan from github.com/Timo59/orkan pinned to a specific
# commit SHA.  If the network is unavailable (e.g. offline CI / sandboxed
# builds), falls back to the local stub in vendor/orkan/orkan.hpp.
#
# Usage (from the root CMakeLists.txt or a backend subdirectory):
#
#   include(cmake/OrkanFetch.cmake)
#   # After this, include(orkan) or the interface target `orkan` is available.
#
# After this module runs, the CMake target `orkan_headers` is defined with the
# correct include path pointing at either the real Orkan or the local stub.

include(FetchContent)

# ── Pinned commit SHA ─────────────────────────────────────────────────────────
# Update this SHA when bumping the Orkan version.
set(ORKAN_GIT_TAG "main"
    CACHE STRING "Orkan git tag / commit SHA to pin")
# TODO(backend): replace "main" with the exact pinned SHA once the real repo
# is accessible and a specific revision has been validated:
#   set(ORKAN_GIT_TAG "abcdef1234567890abcdef1234567890abcdef12")

# ── Attempt FetchContent ──────────────────────────────────────────────────────

# Allow callers to skip network fetch entirely (e.g. via -DORKAN_USE_STUB=ON).
option(ORKAN_USE_STUB
    "Use the local Orkan stub instead of fetching from GitHub (auto-set if offline)"
    OFF)

if(NOT ORKAN_USE_STUB)
    # Try a fast connectivity check: cmake --find-package style is unavailable
    # in most versions, so we rely on FetchContent with FETCHCONTENT_QUIET.
    # If the fetch fails, we fall through to the stub.
    FetchContent_Declare(
        orkan
        GIT_REPOSITORY https://github.com/Timo59/orkan
        GIT_TAG        ${ORKAN_GIT_TAG}
        GIT_SHALLOW    TRUE
    )

    # Suppress errors from FetchContent so we can detect offline builds.
    set(FETCHCONTENT_QUIET ON)

    # Wrap in a try so an unreachable host does not hard-fail the configure.
    # CMake 3.16 does not have FetchContent_Populate with error capture, so we
    # check whether the source directory already exists (populated in a
    # previous run) before attempting the network fetch.
    FetchContent_GetProperties(orkan)
    if(NOT orkan_POPULATED)
        # Best-effort fetch: if it fails, we catch via the stub fallback below.
        FetchContent_Populate(orkan
            SOURCE_DIR   ${CMAKE_BINARY_DIR}/_deps/orkan-src
            BINARY_DIR   ${CMAKE_BINARY_DIR}/_deps/orkan-build
            SUBBUILD_DIR ${CMAKE_BINARY_DIR}/_deps/orkan-subbuild
        )
    endif()

    if(orkan_POPULATED AND EXISTS "${orkan_SOURCE_DIR}/include/orkan/orkan.hpp")
        message(STATUS "STURM: Using fetched Orkan from ${orkan_SOURCE_DIR}")
        add_library(orkan_headers INTERFACE)
        target_include_directories(orkan_headers INTERFACE
            "${orkan_SOURCE_DIR}/include"
        )
    else()
        message(STATUS "STURM: Orkan fetch incomplete or unavailable — "
                       "falling back to local stub (vendor/orkan/).")
        set(ORKAN_USE_STUB ON CACHE BOOL "" FORCE)
    endif()
endif()

if(ORKAN_USE_STUB)
    message(STATUS "STURM: Using local Orkan stub from vendor/orkan/")
    add_library(orkan_headers INTERFACE)
    target_include_directories(orkan_headers INTERFACE
        "${CMAKE_SOURCE_DIR}/vendor"
    )
    target_compile_definitions(orkan_headers INTERFACE ORKAN_USING_STUB=1)
endif()
