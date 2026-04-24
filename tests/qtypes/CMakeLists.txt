cmake_minimum_required(VERSION 3.16)
enable_language(C)

# ── tests/qtypes/ — backend-level qtypes free functions ───────────────────────
#
# Holds tests for `include/sturm/qtypes/*.hpp` pieces that are consumed by the
# LO compile-time rewrite pass (sturm-lo).  Mirrors the pattern established in
# tests/backend/: each target pulls in the shared M19 backend sources
# (context, control_stack, execute_gate, orkan_bridge) so the qint_t<W>
# operators can emit gates and simulate.
#
# The source list deliberately duplicates M19_SOURCES rather than importing it
# from the sibling tests/backend/CMakeLists.txt — these live in separate
# subdirectories and CMake scope rules would otherwise require promoting the
# variable to CACHE.

set(STURM_QTYPES_BACKEND_SOURCES
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)

# sturm-a3qp LO-1e — divide_oop out-of-place division free function.
add_executable(test_divide_oop
    test_divide_oop.cpp
    ${STURM_QTYPES_BACKEND_SOURCES}
)
target_include_directories(test_divide_oop PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_divide_oop PRIVATE cxx_std_20)
target_compile_definitions(test_divide_oop PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
    STURM_MAX_QUBITS=17
)
target_link_libraries(test_divide_oop PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_divide_oop COMMAND test_divide_oop)
set_tests_properties(test_divide_oop PROPERTIES LABELS "qtypes;lossy-reversibility")
