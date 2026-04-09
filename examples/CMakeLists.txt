cmake_minimum_required(VERSION 3.16)
enable_language(C)

# ── Example: qint OR + ASCII circuit drawing ────────────────────────────────
add_executable(example_or_circuit
    or_circuit.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/promotion.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(example_or_circuit PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(example_or_circuit PRIVATE cxx_std_20)
target_link_libraries(example_or_circuit PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
