cmake_minimum_required(VERSION 3.16)
enable_language(C)

# ── tests/lib/ -- DSL-level adjoint tests (sturm-lo LO-1 family) ──────────────
#
# Holds tests that pin `invert(<dsl>)(...)` roundtrip contracts for the lib/
# DSL primitives consumed by the LO compile-time rewrite pass (sturm-lo).
# Mirrors the tests/qtypes/ and tests/backend/ patterns: each target pulls in
# the shared M19 backend sources (context, control_stack, execute_gate,
# orkan_bridge) so the qbool/BitProxy operators can emit gates and simulate.

set(STURM_LIB_BACKEND_SOURCES
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)

# sturm-le6w LO-1d -- lib_or_dsl adjoint roundtrip.
add_executable(test_or_dsl_adjoint
    test_or_dsl_adjoint.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_or_dsl_adjoint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_or_dsl_adjoint PRIVATE cxx_std_20)
target_compile_definitions(test_or_dsl_adjoint PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_or_dsl_adjoint PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_or_dsl_adjoint COMMAND test_or_dsl_adjoint)
set_tests_properties(test_or_dsl_adjoint
    PROPERTIES LABELS "lib;lossy-reversibility")
