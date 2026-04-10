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

# ── ASCII circuit drawer ─────────────────────────────────────────────────────
add_executable(test_draw_ascii
    test_draw_ascii.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
)
target_include_directories(test_draw_ascii PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_draw_ascii PRIVATE cxx_std_20)
add_test(NAME test_draw_ascii COMMAND test_draw_ascii)
set_tests_properties(test_draw_ascii PROPERTIES LABELS "backend")

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

# ── M17: Operand promotion ────────────────────────────────────────────────────
add_executable(test_promotion
    test_promotion.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/promotion.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_promotion PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_promotion PRIVATE cxx_std_20)
target_compile_definitions(test_promotion PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_promotion PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_promotion COMMAND test_promotion)
set_tests_properties(test_promotion PROPERTIES LABELS "backend")

# ── M23: Hand-written controlled op variants (c_*) ────────────────────────────
add_executable(test_controlled_ops
    test_controlled_ops.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_controlled_ops PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_controlled_ops PRIVATE cxx_std_20)
target_compile_definitions(test_controlled_ops PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_controlled_ops PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_controlled_ops COMMAND test_controlled_ops)
set_tests_properties(test_controlled_ops PROPERTIES LABELS "backend")

# ── M24: WHEN entry-point dispatch ────────────────────────────────────────────
add_executable(test_when_dispatch
    test_when_dispatch.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_when_dispatch PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_dispatch PRIVATE cxx_std_20)
target_compile_definitions(test_when_dispatch PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_when_dispatch PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_when_dispatch COMMAND test_when_dispatch)
set_tests_properties(test_when_dispatch PROPERTIES LABELS "backend")

# ── M25: Explicit template instantiation (link-time fixture) ─────────────────
# This target links instantiations.cpp (the explicit instantiation TU) with
# the test fixture TU.  The fixture declares only extern template and calls
# each method; unresolved symbols reveal a broken instantiation contract.
add_executable(test_instantiations
    test_instantiations.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qtypes/instantiations.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_instantiations PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_instantiations PRIVATE cxx_std_20)
target_compile_definitions(test_instantiations PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
    STURM_USE_EXPLICIT_INSTANTIATIONS=1
)
target_link_libraries(test_instantiations PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_instantiations COMMAND test_instantiations)
set_tests_properties(test_instantiations PROPERTIES LABELS "backend")

# ── M26: End-to-end acceptance — c = (a + 5) >= 0 ────────────────────────────
add_executable(test_end_to_end
    test_end_to_end.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_end_to_end PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_end_to_end PRIVATE cxx_std_20)
target_compile_definitions(test_end_to_end PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_end_to_end PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_end_to_end COMMAND test_end_to_end)
set_tests_properties(test_end_to_end PROPERTIES LABELS "backend")

# ── M27: CI hook extension ────────────────────────────────────────────────────
# Verifies that `ctest -L backend` lists ≥1 test from each M1–M26 module and
# that tests/run_all is present and executable.  STURM_BUILD_DIR and
# STURM_REPO_ROOT are injected so the test can invoke ctest itself.
add_executable(test_ci_hook
    test_ci_hook.cpp
)
target_include_directories(test_ci_hook PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_ci_hook PRIVATE cxx_std_20)
target_compile_definitions(test_ci_hook PRIVATE
    STURM_BUILD_DIR="${CMAKE_BINARY_DIR}"
    STURM_REPO_ROOT="${CMAKE_SOURCE_DIR}"
)
add_test(NAME test_ci_hook COMMAND test_ci_hook)
set_tests_properties(test_ci_hook PROPERTIES LABELS "backend")

# ── PRD v2 M1: backend primitive core ────────────────────────────────────────

# Include the OrkanFetch module so the orkan_headers target is available.
# (Already included above for test_orkan_bridge; guard against double-include.)
if(NOT TARGET orkan_headers)
    include(${CMAKE_SOURCE_DIR}/cmake/OrkanFetch.cmake)
endif()

# test_primitives_classical — truth tables for X, XOR, AND
add_executable(test_primitives_classical
    test_primitives_classical.cpp
)
target_include_directories(test_primitives_classical PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_primitives_classical PRIVATE cxx_std_20)
target_link_libraries(test_primitives_classical PRIVATE orkan_headers)
add_test(NAME test_primitives_classical COMMAND test_primitives_classical)
set_tests_properties(test_primitives_classical PROPERTIES LABELS "backend")

# test_primitives_phase — R_y / R_z rotation amplitudes
add_executable(test_primitives_phase
    test_primitives_phase.cpp
)
target_include_directories(test_primitives_phase PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_primitives_phase PRIVATE cxx_std_20)
target_link_libraries(test_primitives_phase PRIVATE orkan_headers)
add_test(NAME test_primitives_phase COMMAND test_primitives_phase)
set_tests_properties(test_primitives_phase PROPERTIES LABELS "backend")

# test_ancilla_lifecycle — borrow/return, double-free, dirty-free
add_executable(test_ancilla_lifecycle
    test_ancilla_lifecycle.cpp
)
target_include_directories(test_ancilla_lifecycle PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_ancilla_lifecycle PRIVATE cxx_std_20)
target_link_libraries(test_ancilla_lifecycle PRIVATE orkan_headers)
add_test(NAME test_ancilla_lifecycle COMMAND test_ancilla_lifecycle)
set_tests_properties(test_ancilla_lifecycle PROPERTIES LABELS "backend")

# ── PRD v2 M2: WHEN lift machinery ───────────────────────────────────────────

# test_when_single — WHEN a: X(b) ≡ CNOT; WHEN a: XOR(b,c) ≡ Toffoli
add_executable(test_when_single
    test_when_single.cpp
)
target_include_directories(test_when_single PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_single PRIVATE cxx_std_20)
target_link_libraries(test_when_single PRIVATE orkan_headers)
add_test(NAME test_when_single COMMAND test_when_single)
set_tests_properties(test_when_single PROPERTIES LABELS "backend")

# test_when_double — WHEN a: WHEN b: X(c) ≡ CCX
add_executable(test_when_double
    test_when_double.cpp
)
target_include_directories(test_when_double PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_double PRIVATE cxx_std_20)
target_link_libraries(test_when_double PRIVATE orkan_headers)
add_test(NAME test_when_double COMMAND test_when_double)
set_tests_properties(test_when_double PROPERTIES LABELS "backend")

# test_when_triple — WHEN a,b,c: X(d) uses c_AND path (3+ controls)
add_executable(test_when_triple
    test_when_triple.cpp
)
target_include_directories(test_when_triple PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_triple PRIVATE cxx_std_20)
target_link_libraries(test_when_triple PRIVATE orkan_headers)
add_test(NAME test_when_triple COMMAND test_when_triple)
set_tests_properties(test_when_triple PROPERTIES LABELS "backend")

# test_when_phase — controlled phase rotations (CRy / CRz)
add_executable(test_when_phase
    test_when_phase.cpp
)
target_include_directories(test_when_phase PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_phase PRIVATE cxx_std_20)
target_link_libraries(test_when_phase PRIVATE orkan_headers)
add_test(NAME test_when_phase COMMAND test_when_phase)
set_tests_properties(test_when_phase PROPERTIES LABELS "backend")

# ── PRD v2 M3: Logic library ──────────────────────────────────────────────────

# test_logic_basic — truth tables for NOT_reg, OR, NAND, NOR, XNOR
add_executable(test_logic_basic
    test_logic_basic.cpp
)
target_include_directories(test_logic_basic PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_logic_basic PRIVATE cxx_std_20)
target_link_libraries(test_logic_basic PRIVATE orkan_headers)
add_test(NAME test_logic_basic COMMAND test_logic_basic)
set_tests_properties(test_logic_basic PROPERTIES LABELS "backend")

# test_c_and — truth table + ancilla_clean for lib_c_AND
add_executable(test_c_and
    test_c_and.cpp
)
target_include_directories(test_c_and PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_c_and PRIVATE cxx_std_20)
target_link_libraries(test_c_and PRIVATE orkan_headers)
add_test(NAME test_c_and COMMAND test_c_and)
set_tests_properties(test_c_and PROPERTIES LABELS "backend")

# test_c_n_and — scaling correctness for n=3..6 controls
add_executable(test_c_n_and
    test_c_n_and.cpp
)
target_include_directories(test_c_n_and PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_c_n_and PRIVATE cxx_std_20)
target_link_libraries(test_c_n_and PRIVATE orkan_headers)
add_test(NAME test_c_n_and COMMAND test_c_n_and)
set_tests_properties(test_c_n_and PROPERTIES LABELS "backend")

# ── PRD v2 M4: SWAP / Fredkin context-aware dispatch ─────────────────────────

# test_swap_uncontrolled_zero_gates — zero gates for uncontrolled SWAP
add_executable(test_swap_uncontrolled_zero_gates
    test_swap_uncontrolled_zero_gates.cpp
)
target_include_directories(test_swap_uncontrolled_zero_gates PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_swap_uncontrolled_zero_gates PRIVATE cxx_std_20)
target_link_libraries(test_swap_uncontrolled_zero_gates PRIVATE orkan_headers)
add_test(NAME test_swap_uncontrolled_zero_gates COMMAND test_swap_uncontrolled_zero_gates)
set_tests_properties(test_swap_uncontrolled_zero_gates PROPERTIES LABELS "backend")

# test_c_swap_fredkin — Fredkin truth table on all 8 inputs
add_executable(test_c_swap_fredkin
    test_c_swap_fredkin.cpp
)
target_include_directories(test_c_swap_fredkin PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_c_swap_fredkin PRIVATE cxx_std_20)
target_link_libraries(test_c_swap_fredkin PRIVATE orkan_headers)
add_test(NAME test_c_swap_fredkin COMMAND test_c_swap_fredkin)
set_tests_properties(test_c_swap_fredkin PROPERTIES LABELS "backend")

# test_swap_under_when — WHEN a: SWAP(b,c) → Fredkin, not 3 CCNOTs
add_executable(test_swap_under_when
    test_swap_under_when.cpp
)
target_include_directories(test_swap_under_when PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_swap_under_when PRIVATE cxx_std_20)
target_link_libraries(test_swap_under_when PRIVATE orkan_headers)
add_test(NAME test_swap_under_when COMMAND test_swap_under_when)
set_tests_properties(test_swap_under_when PROPERTIES LABELS "backend")

# ── M5: move_result uncontrolled relabel — zero gates, index swapped ──────────
add_executable(test_move_uncontrolled_relabel
    test_move_uncontrolled_relabel.cpp
)
target_include_directories(test_move_uncontrolled_relabel PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_move_uncontrolled_relabel PRIVATE cxx_std_20)
target_link_libraries(test_move_uncontrolled_relabel PRIVATE orkan_headers)
add_test(NAME test_move_uncontrolled_relabel COMMAND test_move_uncontrolled_relabel)
set_tests_properties(test_move_uncontrolled_relabel PROPERTIES LABELS "backend")

# ── M5: move_result controlled Fredkin cascade ────────────────────────────────
add_executable(test_move_controlled_fredkin
    test_move_controlled_fredkin.cpp
)
target_include_directories(test_move_controlled_fredkin PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_move_controlled_fredkin PRIVATE cxx_std_20)
target_link_libraries(test_move_controlled_fredkin PRIVATE orkan_headers)
add_test(NAME test_move_controlled_fredkin COMMAND test_move_controlled_fredkin)
set_tests_properties(test_move_controlled_fredkin PROPERTIES LABELS "backend")

# ── M5: GarbageManager full uncompute round-trip ──────────────────────────────
add_executable(test_garbage_uncompute
    test_garbage_uncompute.cpp
)
target_include_directories(test_garbage_uncompute PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_garbage_uncompute PRIVATE cxx_std_20)
target_link_libraries(test_garbage_uncompute PRIVATE orkan_headers)
add_test(NAME test_garbage_uncompute COMMAND test_garbage_uncompute)
set_tests_properties(test_garbage_uncompute PROPERTIES LABELS "backend")

# ── M6: Cuccaro in-place ADD ──────────────────────────────────────────────────
add_executable(test_add_cuccaro
    test_add_cuccaro.cpp
)
target_include_directories(test_add_cuccaro PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_add_cuccaro PRIVATE cxx_std_20)
target_link_libraries(test_add_cuccaro PRIVATE orkan_headers)
add_test(NAME test_add_cuccaro COMMAND test_add_cuccaro)
set_tests_properties(test_add_cuccaro PROPERTIES LABELS "backend")

# ── M6: two's-complement SUB ──────────────────────────────────────────────────
add_executable(test_sub
    test_sub.cpp
)
target_include_directories(test_sub PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_sub PRIVATE cxx_std_20)
target_link_libraries(test_sub PRIVATE orkan_headers)
add_test(NAME test_sub COMMAND test_sub)
set_tests_properties(test_sub PROPERTIES LABELS "backend")

# ── M7: MUL (shift-and-add via controlled ADD) ───────────────────────────────
add_executable(test_mul
    test_mul.cpp
)
target_include_directories(test_mul PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul PRIVATE cxx_std_20)
target_link_libraries(test_mul PRIVATE orkan_headers)
add_test(NAME test_mul COMMAND test_mul)
set_tests_properties(test_mul PROPERTIES LABELS "backend")

# ── M7: DIV / MOD (restoring division + remainder wrapper) ───────────────────
add_executable(test_div_mod
    test_div_mod.cpp
)
target_include_directories(test_div_mod PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_div_mod PRIVATE cxx_std_20)
target_link_libraries(test_div_mod PRIVATE orkan_headers)
add_test(NAME test_div_mod COMMAND test_div_mod)
set_tests_properties(test_div_mod PROPERTIES LABELS "backend")

# ── M8: EQ / LT / LE / GT / GE comparisons ───────────────────────────────────
add_executable(test_compare
    test_compare.cpp
)
target_include_directories(test_compare PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_compare PRIVATE cxx_std_20)
target_link_libraries(test_compare PRIVATE orkan_headers)
add_test(NAME test_compare COMMAND test_compare)
set_tests_properties(test_compare PROPERTIES LABELS "backend")

# ── M8: POW (repeated squaring via MUL) ──────────────────────────────────────
add_executable(test_pow
    test_pow.cpp
)
target_include_directories(test_pow PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_pow PRIVATE cxx_std_20)
target_link_libraries(test_pow PRIVATE orkan_headers)
add_test(NAME test_pow COMMAND test_pow)
set_tests_properties(test_pow PROPERTIES LABELS "backend")

# ── M9: Frontend lowering hooks — lower_assign / lower_expr ──────────────────
add_executable(test_lower_assign
    test_lower_assign.cpp
)
target_include_directories(test_lower_assign PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_lower_assign PRIVATE cxx_std_20)
target_link_libraries(test_lower_assign PRIVATE orkan_headers)
add_test(NAME test_lower_assign COMMAND test_lower_assign)
set_tests_properties(test_lower_assign PROPERTIES LABELS "backend")

# ── M10: Integration & acceptance ────────────────────────────────────────────

# test_integration_arithmetic — c = (a+5)*b  (ADD + MUL + move)
add_executable(test_integration_arithmetic
    test_integration_arithmetic.cpp
)
target_include_directories(test_integration_arithmetic PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_integration_arithmetic PRIVATE cxx_std_20)
target_link_libraries(test_integration_arithmetic PRIVATE orkan_headers)
add_test(NAME test_integration_arithmetic COMMAND test_integration_arithmetic)
set_tests_properties(test_integration_arithmetic PROPERTIES LABELS "backend")

# test_integration_pow_mod — pow(a,e) mod m  (POW + MOD pipeline)
add_executable(test_integration_pow_mod
    test_integration_pow_mod.cpp
)
target_include_directories(test_integration_pow_mod PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_integration_pow_mod PRIVATE cxx_std_20)
target_link_libraries(test_integration_pow_mod PRIVATE orkan_headers)
add_test(NAME test_integration_pow_mod COMMAND test_integration_pow_mod)
set_tests_properties(test_integration_pow_mod PROPERTIES LABELS "backend")

# test_integration_when_nested — deep WHEN nesting drives c_n_AND + clean pool
add_executable(test_integration_when_nested
    test_integration_when_nested.cpp
)
target_include_directories(test_integration_when_nested PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_integration_when_nested PRIVATE cxx_std_20)
target_link_libraries(test_integration_when_nested PRIVATE orkan_headers)
add_test(NAME test_integration_when_nested COMMAND test_integration_when_nested)
set_tests_properties(test_integration_when_nested PROPERTIES LABELS "backend")

# ── M12: Control stack in BackendContext ─────────────────────────────────────

# test_control_stack_push_pop — push/pop/depth/top/controls() correctness
add_executable(test_control_stack_push_pop
    test_control_stack_push_pop.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
)
target_include_directories(test_control_stack_push_pop PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_control_stack_push_pop PRIVATE cxx_std_20)
target_compile_definitions(test_control_stack_push_pop PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_control_stack_push_pop COMMAND test_control_stack_push_pop)
set_tests_properties(test_control_stack_push_pop PROPERTIES LABELS "backend")

# test_control_stack_thread_local — two contexts on different threads are independent
find_package(Threads REQUIRED)
add_executable(test_control_stack_thread_local
    test_control_stack_thread_local.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
)
target_include_directories(test_control_stack_thread_local PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_control_stack_thread_local PRIVATE cxx_std_20)
target_compile_definitions(test_control_stack_thread_local PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_control_stack_thread_local PRIVATE Threads::Threads)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_control_stack_thread_local COMMAND test_control_stack_thread_local)
set_tests_properties(test_control_stack_thread_local PROPERTIES LABELS "backend")

# ── M11 (v3): Primitives through execute_gate ────────────────────────────────

# test_primitives_v3_count — gate_count increments for each primitive (COUNT_ONLY)
add_executable(test_primitives_v3_count
    test_primitives_v3_count.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_primitives_v3_count PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_primitives_v3_count PRIVATE cxx_std_20)
target_compile_definitions(test_primitives_v3_count PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_primitives_v3_count PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_primitives_v3_count COMMAND test_primitives_v3_count)
set_tests_properties(test_primitives_v3_count PROPERTIES LABELS "backend")

# test_primitives_v3_append — correct GateRecord pushed for each primitive (APPEND)
add_executable(test_primitives_v3_append
    test_primitives_v3_append.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_primitives_v3_append PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_primitives_v3_append PRIVATE cxx_std_20)
target_compile_definitions(test_primitives_v3_append PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_primitives_v3_append PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_primitives_v3_append COMMAND test_primitives_v3_append)
set_tests_properties(test_primitives_v3_append PROPERTIES LABELS "backend")

# test_primitives_v3_simulate — statevector matches expected amplitudes (SIMULATE)
add_executable(test_primitives_v3_simulate
    test_primitives_v3_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_primitives_v3_simulate PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_primitives_v3_simulate PRIVATE cxx_std_20)
target_compile_definitions(test_primitives_v3_simulate PRIVATE
    STURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_primitives_v3_simulate PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_primitives_v3_simulate COMMAND test_primitives_v3_simulate)
set_tests_properties(test_primitives_v3_simulate PROPERTIES LABELS "backend")

# ── M13: qbool operators + lazy expressions ──────────────────────────────────
add_executable(test_qbool_ops
    test_qbool_ops.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_qbool_ops PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qbool_ops PRIVATE cxx_std_20)
target_compile_definitions(test_qbool_ops PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_qbool_ops PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_qbool_ops COMMAND test_qbool_ops)
set_tests_properties(test_qbool_ops PROPERTIES LABELS "backend")

# ── M14: DSL-style Cuccaro adder/subtractor ──────────────────────────────────
add_executable(test_adder_dsl
    test_adder_dsl.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_adder_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_adder_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_adder_dsl PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_adder_dsl PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_adder_dsl COMMAND test_adder_dsl)
set_tests_properties(test_adder_dsl PROPERTIES LABELS "backend")

# ── M15: DSL-style logic (OR, NAND, NOR, XNOR) and c_AND / c_n_AND ──────────
add_executable(test_logic_dsl
    test_logic_dsl.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_logic_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_logic_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_logic_dsl PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_logic_dsl PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_logic_dsl COMMAND test_logic_dsl)
set_tests_properties(test_logic_dsl PROPERTIES LABELS "backend")

# ── M18: qint_t operator[](i) returns non-owning qbool ───────────────────────
add_executable(test_subscript
    test_subscript.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_subscript PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_subscript PRIVATE cxx_std_20)
target_compile_definitions(test_subscript PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_subscript PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_subscript COMMAND test_subscript)
set_tests_properties(test_subscript PROPERTIES LABELS "backend")

# ── M16: DSL-style SWAP / Fredkin ─────────────────────────────────────────────
add_executable(test_swap_dsl
    test_swap_dsl.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_swap_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_swap_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_swap_dsl PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_swap_dsl PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_swap_dsl COMMAND test_swap_dsl)
set_tests_properties(test_swap_dsl PROPERTIES LABELS "backend")

# ── M16: DSL-style MUL (shift-and-add) ───────────────────────────────────────
add_executable(test_mul_dsl
    test_mul_dsl.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_mul_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_mul_dsl PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_mul_dsl PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_mul_dsl COMMAND test_mul_dsl)
set_tests_properties(test_mul_dsl PROPERTIES LABELS "backend")

# ── M16: DSL-style DIV (non-restoring) ───────────────────────────────────────
add_executable(test_div_dsl
    test_div_dsl.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_div_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_div_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_div_dsl PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_div_dsl PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_div_dsl COMMAND test_div_dsl)
set_tests_properties(test_div_dsl PROPERTIES LABELS "backend")

# ── M17: DSL-style COMPARE (EQ, LT, LE, GT, GE, NE) ─────────────────────────
add_executable(test_compare_dsl_truth_tables
    test_compare_dsl_truth_tables.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_compare_dsl_truth_tables PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_compare_dsl_truth_tables PRIVATE cxx_std_20)
target_compile_definitions(test_compare_dsl_truth_tables PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_compare_dsl_truth_tables PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_compare_dsl_truth_tables COMMAND test_compare_dsl_truth_tables)
set_tests_properties(test_compare_dsl_truth_tables PROPERTIES LABELS "backend")

# ── M17: DSL-style MOD (remainder only) ──────────────────────────────────────
add_executable(test_mod_dsl_truth_table
    test_mod_dsl_truth_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_mod_dsl_truth_table PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mod_dsl_truth_table PRIVATE cxx_std_20)
target_compile_definitions(test_mod_dsl_truth_table PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_mod_dsl_truth_table PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_mod_dsl_truth_table COMMAND test_mod_dsl_truth_table)
set_tests_properties(test_mod_dsl_truth_table PROPERTIES LABELS "backend")

# ── M17: DSL-style POW (repeated squaring) ───────────────────────────────────
add_executable(test_pow_dsl_small
    test_pow_dsl_small.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_pow_dsl_small PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_pow_dsl_small PRIVATE cxx_std_20)
target_compile_definitions(test_pow_dsl_small PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_pow_dsl_small PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_pow_dsl_small COMMAND test_pow_dsl_small)
set_tests_properties(test_pow_dsl_small PROPERTIES LABELS "backend")

# ── M19: Frontend wiring — qint_t compound-assign to DSL library ──────────────

# Shared source list for M19 tests (all need backend + control stack + DSL).
set(M19_SOURCES
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)

# test_qint_add_e2e — operator+= and operator-= emit gates + correct arithmetic
add_executable(test_qint_add_e2e
    test_qint_add_e2e.cpp
    ${M19_SOURCES}
)
target_include_directories(test_qint_add_e2e PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qint_add_e2e PRIVATE cxx_std_20)
target_compile_definitions(test_qint_add_e2e PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_qint_add_e2e PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_qint_add_e2e COMMAND test_qint_add_e2e)
set_tests_properties(test_qint_add_e2e PROPERTIES LABELS "backend")

# test_qint_mul_e2e — operator*= emits gates + 2-bit multiply correctness
add_executable(test_qint_mul_e2e
    test_qint_mul_e2e.cpp
    ${M19_SOURCES}
)
target_include_directories(test_qint_mul_e2e PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qint_mul_e2e PRIVATE cxx_std_20)
target_compile_definitions(test_qint_mul_e2e PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_qint_mul_e2e PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_qint_mul_e2e COMMAND test_qint_mul_e2e)
set_tests_properties(test_qint_mul_e2e PROPERTIES LABELS "backend")

# test_qint_compare_e2e — comparison operators return correct qbool + emit gates
add_executable(test_qint_compare_e2e
    test_qint_compare_e2e.cpp
    ${M19_SOURCES}
)
target_include_directories(test_qint_compare_e2e PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qint_compare_e2e PRIVATE cxx_std_20)
target_compile_definitions(test_qint_compare_e2e PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_qint_compare_e2e PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_qint_compare_e2e COMMAND test_qint_compare_e2e)
set_tests_properties(test_qint_compare_e2e PROPERTIES LABELS "backend")

# test_qint_bitwise_e2e — bitwise operators (&=, |=, ^=) emit correct gate counts
add_executable(test_qint_bitwise_e2e
    test_qint_bitwise_e2e.cpp
    ${M19_SOURCES}
)
target_include_directories(test_qint_bitwise_e2e PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qint_bitwise_e2e PRIVATE cxx_std_20)
target_compile_definitions(test_qint_bitwise_e2e PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_qint_bitwise_e2e PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_qint_bitwise_e2e COMMAND test_qint_bitwise_e2e)
set_tests_properties(test_qint_bitwise_e2e PROPERTIES LABELS "backend")
