cmake_minimum_required(VERSION 3.16)

find_package(Threads REQUIRED)

# Register each test executable in the tests/ directory.
# Add new test source files to this list as steps are implemented.
set(STURM_TEST_SOURCES
    test_smoke.cpp
    test_qubit_pool.cpp
    test_sink_counter.cpp
    test_sink_recording.cpp
    test_qbool.cpp
    test_mask_ops.cpp
    test_dispatch.cpp
    test_qint_classical.cpp
    test_qint_superposed.cpp
    test_resource_lifecycle.cpp
    test_when.cpp
    test_sink_dispatch.cpp
    test_conversions.cpp
    test_phase_amp.cpp
    test_width.cpp

    test_qint_owning.cpp
    test_qbool_unified.cpp
    test_qbool_acceptance.cpp

    # Phase I PI-0 (docs/roadmap_transpiler_post_mvp.md, bd sturm-hi66)
    test_invert.cpp
)

foreach(src ${STURM_TEST_SOURCES})
    get_filename_component(test_name ${src} NAME_WE)
    add_executable(${test_name} ${src})
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(${test_name} PRIVATE cxx_std_20)
    target_link_libraries(${test_name} PRIVATE Threads::Threads)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# ── Backend test subdirectory (M1 and later) ──────────────────────────────────
add_subdirectory(backend)

# ── Transpiler-MVP Group C tests (M10+) ───────────────────────────────────────
# Holds consumer-facing tests that exercise the transpiler toolchain from the
# outside (add_quantum_executable, snapshot fixtures, gate-stream equivalence).
# The transpiler binary's own unit tests live under transpiler/tests/ and are
# added via the top-level add_subdirectory(transpiler).
add_subdirectory(transpiler)
