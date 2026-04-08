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

# ── M6: IR buffer + GateRecord ───────────────────────────────────────────────
add_executable(test_ir
    test_ir.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
)
target_include_directories(test_ir PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_ir PRIVATE cxx_std_20)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_ir COMMAND test_ir)
set_tests_properties(test_ir PROPERTIES LABELS "backend")

# ── M8: APPEND executor ──────────────────────────────────────────────────────
add_executable(test_exec_append
    test_exec_append.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
)
target_include_directories(test_exec_append PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_exec_append PRIVATE cxx_std_20)
target_compile_definitions(test_exec_append PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_exec_append COMMAND test_exec_append)
set_tests_properties(test_exec_append PROPERTIES LABELS "backend")

# ── M7: COUNT_ONLY executor ───────────────────────────────────────────────────
add_executable(test_exec_count
    test_exec_count.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
)
target_include_directories(test_exec_count PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_exec_count PRIVATE cxx_std_20)
target_compile_definitions(test_exec_count PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_exec_count COMMAND test_exec_count)
set_tests_properties(test_exec_count PROPERTIES LABELS "backend")

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

# ── M13: Layer B — execute_gate join point ────────────────────────────────────
add_executable(test_execute_gate
    test_execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_execute_gate PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_execute_gate PRIVATE cxx_std_20)
target_compile_definitions(test_execute_gate PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_execute_gate PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_execute_gate COMMAND test_execute_gate)
set_tests_properties(test_execute_gate PROPERTIES LABELS "backend")

# ── M19: uncompute_op tagged union ────────────────────────────────────────────
add_executable(test_uncompute_op
    test_uncompute_op.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_uncompute_op PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_uncompute_op PRIVATE cxx_std_20)
target_compile_definitions(test_uncompute_op PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_uncompute_op PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_uncompute_op COMMAND test_uncompute_op)
set_tests_properties(test_uncompute_op PROPERTIES LABELS "backend")

# ── M20: RAII uncompute runner ────────────────────────────────────────────────
add_executable(test_uncompute_run
    test_uncompute_run.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_run.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_uncompute_run PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_uncompute_run PRIVATE cxx_std_20)
target_compile_definitions(test_uncompute_run PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_uncompute_run PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_uncompute_run COMMAND test_uncompute_run)
set_tests_properties(test_uncompute_run PROPERTIES LABELS "backend")

# ── M18: 17-qubit cap enforcement on QubitPool ───────────────────────────────
add_executable(test_qubit_cap
    test_qubit_cap.cpp
)
target_include_directories(test_qubit_cap PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qubit_cap PRIVATE cxx_std_20)
target_compile_definitions(test_qubit_cap PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
add_test(NAME test_qubit_cap COMMAND test_qubit_cap)
set_tests_properties(test_qubit_cap PROPERTIES LABELS "backend")

# ── M21: Wire operator+=(int) to uncompute (pilot) ────────────────────────────
add_executable(test_uncompute_add_const
    test_uncompute_add_const.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_uncompute_add_const PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_uncompute_add_const PRIVATE cxx_std_20)
target_compile_definitions(test_uncompute_add_const PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_uncompute_add_const PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_uncompute_add_const COMMAND test_uncompute_add_const)
set_tests_properties(test_uncompute_add_const PROPERTIES LABELS "backend")

# ── M22: Wire remaining operators to uncompute ───────────────────────────────
add_executable(test_uncompute_each_op
    test_uncompute_each_op.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_uncompute_each_op PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_uncompute_each_op PRIVATE cxx_std_20)
target_compile_definitions(test_uncompute_each_op PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_uncompute_each_op PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_uncompute_each_op COMMAND test_uncompute_each_op)
set_tests_properties(test_uncompute_each_op PROPERTIES LABELS "backend")

# ── M22: qbool destructor emits comparison inverse (Bennett test) ─────────────
add_executable(test_qbool_uncompute
    test_qbool_uncompute.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_qbool_uncompute PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qbool_uncompute PRIVATE cxx_std_20)
target_compile_definitions(test_qbool_uncompute PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_qbool_uncompute PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_qbool_uncompute COMMAND test_qbool_uncompute)
set_tests_properties(test_qbool_uncompute PROPERTIES LABELS "backend")

# ── M14: Classical-control reduction table ────────────────────────────────────
add_executable(test_reduction_table
    test_reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
)
target_include_directories(test_reduction_table PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_reduction_table PRIVATE cxx_std_20)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_reduction_table COMMAND test_reduction_table)
set_tests_properties(test_reduction_table PROPERTIES LABELS "backend")

# ── M15: Layer A entry — all-classical path ───────────────────────────────────
add_executable(test_dispatch_all_classical
    test_dispatch_all_classical.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_dispatch_all_classical PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_dispatch_all_classical PRIVATE cxx_std_20)
target_compile_definitions(test_dispatch_all_classical PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_dispatch_all_classical PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_dispatch_all_classical COMMAND test_dispatch_all_classical)
set_tests_properties(test_dispatch_all_classical PROPERTIES LABELS "backend")

# ── M16: Layer A entry — mixed and all-quantum paths ─────────────────────────
add_executable(test_dispatch_mixed
    test_dispatch_mixed.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_dispatch_mixed PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_dispatch_mixed PRIVATE cxx_std_20)
target_compile_definitions(test_dispatch_mixed PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_dispatch_mixed PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_dispatch_mixed COMMAND test_dispatch_mixed)
set_tests_properties(test_dispatch_mixed PROPERTIES LABELS "backend")

# ── M5: Measurement op per mode ──────────────────────────────────────────────
add_executable(test_measure
    test_measure.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/measure.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_measure PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_measure PRIVATE cxx_std_20)
target_compile_definitions(test_measure PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_measure PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_measure COMMAND test_measure)
set_tests_properties(test_measure PROPERTIES LABELS "backend")

# ── M4: CLI + mode init helper ────────────────────────────────────────────────
add_executable(test_init
    test_init.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/init.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
)
target_include_directories(test_init PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_init PRIVATE cxx_std_20)
target_compile_definitions(test_init PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_init COMMAND test_init)
set_tests_properties(test_init PROPERTIES LABELS "backend")
