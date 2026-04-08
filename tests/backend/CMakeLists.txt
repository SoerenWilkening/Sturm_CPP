cmake_minimum_required(VERSION 3.16)
enable_language(C)

# ── Backend test sources compiled with their required C implementation files ──

# M1: gate_kind enum + taxonomy table
add_executable(test_gate_kind
    test_gate_kind.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
)
target_include_directories(test_gate_kind PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_gate_kind PRIVATE cxx_std_20)
# The .c file needs C11 for bool/_Bool
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_gate_kind COMMAND test_gate_kind)
set_tests_properties(test_gate_kind PROPERTIES LABELS "backend")

# M2: C ABI sink header core.h — compiled as a pure C translation unit
add_executable(test_core_abi_c
    test_core_abi_c.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/core_stub.c
)
target_include_directories(test_core_abi_c PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
# Force everything in this target to C11 (no C++ involved)
set_target_properties(test_core_abi_c PROPERTIES
    LINKER_LANGUAGE C
)
set_source_files_properties(
    test_core_abi_c.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/core_stub.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_core_abi_c COMMAND test_core_abi_c)
set_tests_properties(test_core_abi_c PROPERTIES LABELS "backend")

# M3: BackendContext + thread-local mode storage
add_executable(test_context
    test_context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
)
target_include_directories(test_context PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_context PRIVATE cxx_std_20)
target_compile_definitions(test_context PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_context COMMAND test_context)
set_tests_properties(test_context PROPERTIES LABELS "backend")

# ── M9: Orkan vendoring + bridge skeleton ─────────────────────────────────────
# Include the OrkanFetch module which either fetches Orkan or falls back to the
# local stub in vendor/orkan/.  The module defines the `orkan_headers` target.
include(${CMAKE_SOURCE_DIR}/cmake/OrkanFetch.cmake)

add_executable(test_orkan_bridge
    test_orkan_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_orkan_bridge PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_orkan_bridge PRIVATE cxx_std_20)
target_link_libraries(test_orkan_bridge PRIVATE orkan_headers)
add_test(NAME test_orkan_bridge COMMAND test_orkan_bridge)
set_tests_properties(test_orkan_bridge PROPERTIES LABELS "backend")

# ── M10: SIMULATE executor — 1-qubit gates ────────────────────────────────────
add_executable(test_exec_simulate_1q
    test_exec_simulate_1q.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_exec_simulate_1q PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_exec_simulate_1q PRIVATE cxx_std_20)
target_link_libraries(test_exec_simulate_1q PRIVATE orkan_headers)
add_test(NAME test_exec_simulate_1q COMMAND test_exec_simulate_1q)
set_tests_properties(test_exec_simulate_1q PROPERTIES LABELS "backend")

# ── M11: SIMULATE executor — 2/3-qubit permutation & phase gates ──────────────
add_executable(test_exec_simulate_multiq
    test_exec_simulate_multiq.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_exec_simulate_multiq PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_exec_simulate_multiq PRIVATE cxx_std_20)
target_link_libraries(test_exec_simulate_multiq PRIVATE orkan_headers)
add_test(NAME test_exec_simulate_multiq COMMAND test_exec_simulate_multiq)
set_tests_properties(test_exec_simulate_multiq PROPERTIES LABELS "backend")

# ── M12: SIMULATE executor — CRx/CRy/CRz decomposition ───────────────────────
add_executable(test_exec_simulate_crot
    test_exec_simulate_crot.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_exec_simulate_crot PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_exec_simulate_crot PRIVATE cxx_std_20)
target_link_libraries(test_exec_simulate_crot PRIVATE orkan_headers)
add_test(NAME test_exec_simulate_crot COMMAND test_exec_simulate_crot)
set_tests_properties(test_exec_simulate_crot PROPERTIES LABELS "backend")
