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

# ── M3 (transpiler-MVP): explicit uncompute_or free-function API ──────────────
add_executable(test_uncompute_or
    test_uncompute_or.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_uncompute_or PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_uncompute_or PRIVATE cxx_std_20)
target_compile_definitions(test_uncompute_or PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_uncompute_or PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_uncompute_or COMMAND test_uncompute_or)
set_tests_properties(test_uncompute_or PROPERTIES LABELS "backend")

# ── Phase E PE-0 (sturm-oheo): explicit uncompute_and free-function API ──────
add_executable(test_uncompute_and
    test_uncompute_and.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_uncompute_and PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_uncompute_and PRIVATE cxx_std_20)
target_compile_definitions(test_uncompute_and PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_uncompute_and PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_uncompute_and COMMAND test_uncompute_and)
set_tests_properties(test_uncompute_and PROPERTIES LABELS "backend")

# ── Phase J PJ-1b (sturm-8cxd): ccnot_inplace forward helper ─────────────────
add_executable(test_ccnot_inplace
    test_ccnot_inplace.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_ccnot_inplace PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_ccnot_inplace PRIVATE cxx_std_20)
target_compile_definitions(test_ccnot_inplace PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_ccnot_inplace PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_ccnot_inplace COMMAND test_ccnot_inplace)
set_tests_properties(test_ccnot_inplace PROPERTIES LABELS "backend")

# ── sturm-999i Phase D: uncompute_*_qint (comparison) free-function API ──────
add_executable(test_uncompute_qint_compare
    test_uncompute_qint_compare.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_uncompute_qint_compare PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_uncompute_qint_compare PRIVATE cxx_std_20)
target_compile_definitions(test_uncompute_qint_compare PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_uncompute_qint_compare PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_uncompute_qint_compare COMMAND test_uncompute_qint_compare)
set_tests_properties(test_uncompute_qint_compare PROPERTIES LABELS "backend")

# ── WHEN compound uncompute (WhenCapture RAII) ────────────────────────────────
add_executable(test_when_compound_uncompute
    test_when_compound_uncompute.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_when_compound_uncompute PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_compound_uncompute PRIVATE cxx_std_20)
target_compile_definitions(test_when_compound_uncompute PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_when_compound_uncompute PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_when_compound_uncompute COMMAND test_when_compound_uncompute)
set_tests_properties(test_when_compound_uncompute PROPERTIES LABELS "backend")

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

# ── M6: Template adder DSL (BitProxy instantiation) ──────────────────────────
add_executable(test_adder_dsl_template
    test_adder_dsl_template.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_adder_dsl_template PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_adder_dsl_template PRIVATE cxx_std_20)
target_compile_definitions(test_adder_dsl_template PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_adder_dsl_template PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_adder_dsl_template COMMAND test_adder_dsl_template)
set_tests_properties(test_adder_dsl_template PROPERTIES LABELS "backend")

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

# ── sturm-qwt: qint operator[] COUNT_ONLY and SIMULATE tests ─────────────────
add_executable(test_subscript_count_simulate
    test_subscript_count_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_subscript_count_simulate PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_subscript_count_simulate PRIVATE cxx_std_20)
target_compile_definitions(test_subscript_count_simulate PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_subscript_count_simulate PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_subscript_count_simulate COMMAND test_subscript_count_simulate)
set_tests_properties(test_subscript_count_simulate PROPERTIES LABELS "backend")

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

# sturm-e7f: SIMULATE mode statevector verification for all 6 comparison operators
add_executable(test_qint_compare_simulate
    test_qint_compare_simulate.cpp
    ${M19_SOURCES}
)
target_include_directories(test_qint_compare_simulate PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qint_compare_simulate PRIVATE cxx_std_20)
target_compile_definitions(test_qint_compare_simulate PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_qint_compare_simulate PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_qint_compare_simulate COMMAND test_qint_compare_simulate)
set_tests_properties(test_qint_compare_simulate PROPERTIES LABELS "backend")

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

# ── M20: Cleanup guard — no SimState refs outside tests/ ─────────────────────
add_executable(test_no_simstate_refs
    test_no_simstate_refs.cpp
)
target_include_directories(test_no_simstate_refs PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_no_simstate_refs PRIVATE cxx_std_20)
target_compile_definitions(test_no_simstate_refs PRIVATE
    STURM_REPO_ROOT="${CMAKE_SOURCE_DIR}"
)
add_test(NAME test_no_simstate_refs COMMAND test_no_simstate_refs)
set_tests_properties(test_no_simstate_refs PROPERTIES LABELS "backend")

# ── M20: Cleanup guard — no sturm::v2:: namespace outside tests/ ─────────────
add_executable(test_no_v2_namespace
    test_no_v2_namespace.cpp
)
target_include_directories(test_no_v2_namespace PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_no_v2_namespace PRIVATE cxx_std_20)
target_compile_definitions(test_no_v2_namespace PRIVATE
    STURM_REPO_ROOT="${CMAKE_SOURCE_DIR}"
)
add_test(NAME test_no_v2_namespace COMMAND test_no_v2_namespace)

# ── M21: Integration & acceptance ────────────────────────────────────────────

# test_e2e_arithmetic — (a+5)*b exercises ADD + MUL + gate count
add_executable(test_e2e_arithmetic
    test_e2e_arithmetic.cpp
    ${M19_SOURCES}
)
target_include_directories(test_e2e_arithmetic PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_e2e_arithmetic PRIVATE cxx_std_20)
target_compile_definitions(test_e2e_arithmetic PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_e2e_arithmetic PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_e2e_arithmetic COMMAND test_e2e_arithmetic)
set_tests_properties(test_e2e_arithmetic PROPERTIES LABELS "backend")

# test_e2e_pow_mod — pow(a,e) % m exercises POW + MOD
add_executable(test_e2e_pow_mod
    test_e2e_pow_mod.cpp
    ${M19_SOURCES}
)
target_include_directories(test_e2e_pow_mod PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_e2e_pow_mod PRIVATE cxx_std_20)
target_compile_definitions(test_e2e_pow_mod PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_e2e_pow_mod PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_e2e_pow_mod COMMAND test_e2e_pow_mod)
set_tests_properties(test_e2e_pow_mod PROPERTIES LABELS "backend")

# test_e2e_when_nested — 3+ level WHEN nesting drives c_n_AND + ancilla clean
add_executable(test_e2e_when_nested
    test_e2e_when_nested.cpp
    ${M19_SOURCES}
)
target_include_directories(test_e2e_when_nested PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_e2e_when_nested PRIVATE cxx_std_20)
target_compile_definitions(test_e2e_when_nested PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_e2e_when_nested PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_e2e_when_nested COMMAND test_e2e_when_nested)
set_tests_properties(test_e2e_when_nested PROPERTIES LABELS "backend")

# test_e2e_all_modes — same program in COUNT_ONLY, APPEND, SIMULATE
add_executable(test_e2e_all_modes
    test_e2e_all_modes.cpp
    ${M19_SOURCES}
)
target_include_directories(test_e2e_all_modes PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_e2e_all_modes PRIVATE cxx_std_20)
target_compile_definitions(test_e2e_all_modes PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_e2e_all_modes PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_e2e_all_modes COMMAND test_e2e_all_modes)
set_tests_properties(test_e2e_all_modes PROPERTIES LABELS "backend")

# test_e2e_loc_budget — module LoC budget verification (grep/wc-based)
add_executable(test_e2e_loc_budget
    test_e2e_loc_budget.cpp
)
target_include_directories(test_e2e_loc_budget PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_e2e_loc_budget PRIVATE cxx_std_20)
target_compile_definitions(test_e2e_loc_budget PRIVATE
    STURM_REPO_ROOT="${CMAKE_SOURCE_DIR}"
)
add_test(NAME test_e2e_loc_budget COMMAND test_e2e_loc_budget)
set_tests_properties(test_e2e_loc_budget PROPERTIES LABELS "backend")
set_tests_properties(test_no_v2_namespace PROPERTIES LABELS "backend")

# ── sturm-auu: Simulation correctness for free operators (+, -, *, /, %) ──────
# EXPECTED TO FAIL until sturm-6qo wires free operators through compound assigns.
add_executable(test_free_op_simulate
    test_free_op_simulate.cpp
    ${M19_SOURCES}
)
target_include_directories(test_free_op_simulate PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_free_op_simulate PRIVATE cxx_std_20)
target_compile_definitions(test_free_op_simulate PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_free_op_simulate PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_free_op_simulate COMMAND test_free_op_simulate)
set_tests_properties(test_free_op_simulate PROPERTIES LABELS "backend")

# ── sturm-n7w: COUNT_ONLY gate emission for free operators (+, -, *, /, %) ────
# EXPECTED TO FAIL until sturm-6qo wires free operators through compound assigns.
# Each operator has its own test function for individual failure visibility.
add_executable(test_free_op_gate_emission
    test_free_op_gate_emission.cpp
    ${M19_SOURCES}
)
target_include_directories(test_free_op_gate_emission PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_free_op_gate_emission PRIVATE cxx_std_20)
target_compile_definitions(test_free_op_gate_emission PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_free_op_gate_emission PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_free_op_gate_emission COMMAND test_free_op_gate_emission)
set_tests_properties(test_free_op_gate_emission PROPERTIES LABELS "backend")

# ── sturm-dxr: Simulation correctness for compound-assign operators ───────────
# Tests SIMULATE mode for +=, -=, *=, /=, %= with concrete values.
# EXPECTED TO PASS on current code (compound-assigns are wired to DSL library).
add_executable(test_compound_assign_simulate
    test_compound_assign_simulate.cpp
    ${M19_SOURCES}
)
target_include_directories(test_compound_assign_simulate PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_compound_assign_simulate PRIVATE cxx_std_20)
target_compile_definitions(test_compound_assign_simulate PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_compound_assign_simulate PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_compound_assign_simulate COMMAND test_compound_assign_simulate)
set_tests_properties(test_compound_assign_simulate PROPERTIES LABELS "backend")

# ── sturm-hvb: COUNT_ONLY gate emission baseline for compound-assign ops ──────
# Verifies that +=, -=, *=, /=, %= each emit at least one gate in COUNT_ONLY
# mode.  EXPECTED TO PASS on current code: compound-assigns are wired to the
# DSL library, which routes through execute_gate.
add_executable(test_compound_assign_gate_emission
    test_compound_assign_gate_emission.cpp
    ${M19_SOURCES}
)
target_include_directories(test_compound_assign_gate_emission PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_compound_assign_gate_emission PRIVATE cxx_std_20)
target_compile_definitions(test_compound_assign_gate_emission PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_compound_assign_gate_emission PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_compound_assign_gate_emission COMMAND test_compound_assign_gate_emission)
set_tests_properties(test_compound_assign_gate_emission PROPERTIES LABELS "backend")

# ── sturm-ta7: SIMULATE mode tests for shift operators (<<, >>, <<=, >>=) ─────
# Tests that statevector is correctly updated for all 4 shift operators.
# Shifts are implemented as qubit relabeling (zero gates).
add_executable(test_shift_simulate
    test_shift_simulate.cpp
    ${M19_SOURCES}
)
target_include_directories(test_shift_simulate PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_shift_simulate PRIVATE cxx_std_20)
target_compile_definitions(test_shift_simulate PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_shift_simulate PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_shift_simulate COMMAND test_shift_simulate)
set_tests_properties(test_shift_simulate PROPERTIES LABELS "backend")

# ── sturm-s0h: Bitwise NOT (~) operator on qint — COUNT_ONLY + SIMULATE ──────
add_executable(test_bitwise_not_operator
    test_bitwise_not_operator.cpp
    ${M19_SOURCES}
)
target_include_directories(test_bitwise_not_operator PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_bitwise_not_operator PRIVATE cxx_std_20)
target_compile_definitions(test_bitwise_not_operator PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_bitwise_not_operator PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_bitwise_not_operator COMMAND test_bitwise_not_operator)
set_tests_properties(test_bitwise_not_operator PROPERTIES LABELS "backend")

# ── sturm-9wz: SIMULATE mode tests for bitwise AND/OR (&=, |=, &, |) ─────────
# Verifies statevector correctness for all 4 forms: &=, |=, & (free), | (free).
add_executable(test_bitwise_and_or_simulate
    test_bitwise_and_or_simulate.cpp
    ${M19_SOURCES}
)
target_include_directories(test_bitwise_and_or_simulate PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_bitwise_and_or_simulate PRIVATE cxx_std_20)
target_compile_definitions(test_bitwise_and_or_simulate PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_bitwise_and_or_simulate PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_bitwise_and_or_simulate COMMAND test_bitwise_and_or_simulate)
set_tests_properties(test_bitwise_and_or_simulate PROPERTIES LABELS "backend")

# ── M14 (qbool unification): Final acceptance tests — backend ─────────────────
# Covers AC1 (phi/RZ), AC2 (theta/RY), AC6 (WHEN classical+superposed),
# AC8 (operator~ uncomputes — X gate on destruction).
add_executable(test_qbool_acceptance_backend
    test_qbool_acceptance_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_qbool_acceptance_backend PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qbool_acceptance_backend PRIVATE cxx_std_20)
target_compile_definitions(test_qbool_acceptance_backend PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_qbool_acceptance_backend PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_qbool_acceptance_backend COMMAND test_qbool_acceptance_backend)
set_tests_properties(test_qbool_acceptance_backend PROPERTIES LABELS "backend")

# ── sturm-d9n: WhenGuard bridges to control_stack ────────────────────────────
# Verifies that WhenGuard pushes/pops ctx->control_stack alongside TLS.
# Non-fold: push expr qubit. AND-fold: pop outer, push ancilla.
# Invariant: depth in {0,1} at all times.
add_executable(test_when_control_stack_bridge
    test_when_control_stack_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_when_control_stack_bridge PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_control_stack_bridge PRIVATE cxx_std_20)
target_compile_definitions(test_when_control_stack_bridge PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_when_control_stack_bridge PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_when_control_stack_bridge COMMAND test_when_control_stack_bridge)
set_tests_properties(test_when_control_stack_bridge PROPERTIES LABELS "backend")

# ── sturm-8x3: emit_RY_lifted / emit_RZ_lifted ───────────────────────────────
# Verifies that depth=0 emits RY/RZ and depth=1 emits CRY/CRZ via execute_gate.
# Includes both COUNT_ONLY (gate_count checks) and SIMULATE (statevector) tests.
add_executable(test_lifted_primitives
    test_lifted_primitives.cpp
    ${M19_SOURCES}
)
target_include_directories(test_lifted_primitives PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_lifted_primitives PRIVATE cxx_std_20)
target_compile_definitions(test_lifted_primitives PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_lifted_primitives PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_lifted_primitives COMMAND test_lifted_primitives)
set_tests_properties(test_lifted_primitives PROPERTIES LABELS "backend")

# ── sturm-b1g: PhiProxy/ThetaProxy emit gates via backend ────────────────────
# Verifies that phi += / theta += route through emit_RZ_lifted / emit_RY_lifted
# when STURM_BACKEND_ENABLED and a backend context is present, and fall back to
# the sink path otherwise.
add_executable(test_phi_theta_backend
    test_phi_theta_backend.cpp
    ${M19_SOURCES}
)
target_include_directories(test_phi_theta_backend PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_phi_theta_backend PRIVATE cxx_std_20)
target_compile_definitions(test_phi_theta_backend PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_phi_theta_backend PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_phi_theta_backend COMMAND test_phi_theta_backend)
set_tests_properties(test_phi_theta_backend PROPERTIES LABELS "backend")

# ── WHEN arith promotion: classical operands inside WHEN get promoted ────────
add_executable(test_when_arith_promotion
    test_when_arith_promotion.cpp
)
target_include_directories(test_when_arith_promotion PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_arith_promotion PRIVATE cxx_std_20)
add_test(NAME test_when_arith_promotion COMMAND test_when_arith_promotion)
set_tests_properties(test_when_arith_promotion PROPERTIES LABELS "backend")

# ── M1: BitProxy struct + operator bodies (per-bit lazy WHEN promotion) ──────
add_executable(test_bitproxy_when_promotion
    test_bitproxy_when_promotion.cpp
    ${M19_SOURCES}
)
target_include_directories(test_bitproxy_when_promotion PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_bitproxy_when_promotion PRIVATE cxx_std_20)
target_compile_definitions(test_bitproxy_when_promotion PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_bitproxy_when_promotion PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_bitproxy_when_promotion COMMAND test_bitproxy_when_promotion)
set_tests_properties(test_bitproxy_when_promotion PROPERTIES LABELS "backend")

# ── M8: Template logic/swap/compare/c_and DSL (BitProxy instantiation) ───────
add_executable(test_m8_template_dsl
    test_m8_template_dsl.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_m8_template_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_m8_template_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_m8_template_dsl PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_m8_template_dsl PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_m8_template_dsl COMMAND test_m8_template_dsl)
set_tests_properties(test_m8_template_dsl PROPERTIES LABELS "backend")

# ── M7: Template mul/div/mod DSL (BitProxy instantiation) ──────────────────
add_executable(test_m7_template_dsl
    test_m7_template_dsl.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_include_directories(test_m7_template_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_m7_template_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_m7_template_dsl PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_m7_template_dsl PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_m7_template_dsl COMMAND test_m7_template_dsl)
set_tests_properties(test_m7_template_dsl PROPERTIES LABELS "backend")

# ── M13: WHEN + shift operators fast-path bypass ────────────────────────────
add_executable(test_when_shift_promotion
    test_when_shift_promotion.cpp
    ${M19_SOURCES}
)
target_include_directories(test_when_shift_promotion PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_shift_promotion PRIVATE cxx_std_20)
target_compile_definitions(test_when_shift_promotion PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_when_shift_promotion PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_when_shift_promotion COMMAND test_when_shift_promotion)
set_tests_properties(test_when_shift_promotion PROPERTIES LABELS "backend")

# ── M12: copy_register only copies quantum bits ──────────────────────────────
add_executable(test_copy_register_quantum_only
    test_copy_register_quantum_only.cpp
    ${M19_SOURCES}
)
target_include_directories(test_copy_register_quantum_only PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_copy_register_quantum_only PRIVATE cxx_std_20)
target_compile_definitions(test_copy_register_quantum_only PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_copy_register_quantum_only PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_copy_register_quantum_only COMMAND test_copy_register_quantum_only)
set_tests_properties(test_copy_register_quantum_only PROPERTIES LABELS "backend")

# ── M18: Final PRD acceptance tests ──────────────────────────────────────────
add_executable(test_acceptance_prd
    test_acceptance_prd.cpp
    ${M19_SOURCES}
)
target_include_directories(test_acceptance_prd PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_acceptance_prd PRIVATE cxx_std_20)
target_compile_definitions(test_acceptance_prd PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_acceptance_prd PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_acceptance_prd COMMAND test_acceptance_prd)
set_tests_properties(test_acceptance_prd PROPERTIES LABELS "backend")

# ── M4: Free bitwise operators with -1 qubit indices (sturm-5hy) ─────────────
add_executable(test_free_bitwise_neg1_qubits
    test_free_bitwise_neg1_qubits.cpp
    ${M19_SOURCES}
)
target_include_directories(test_free_bitwise_neg1_qubits PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_free_bitwise_neg1_qubits PRIVATE cxx_std_20)
target_compile_definitions(test_free_bitwise_neg1_qubits PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_free_bitwise_neg1_qubits PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_free_bitwise_neg1_qubits COMMAND test_free_bitwise_neg1_qubits)
set_tests_properties(test_free_bitwise_neg1_qubits PROPERTIES LABELS "backend")

# ── M3: Free arithmetic operator fast-path fix (sturm-8w8) ───────────────────
# Verifies that free operators (+, -, *, /, %) emit gates when one operand is
# quantum and the other is classical.  Also regression: both-classical => 0 gates.
add_executable(test_free_arith_mixed_fastpath
    test_free_arith_mixed_fastpath.cpp
    ${M19_SOURCES}
)
target_include_directories(test_free_arith_mixed_fastpath PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_free_arith_mixed_fastpath PRIVATE cxx_std_20)
target_compile_definitions(test_free_arith_mixed_fastpath PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_free_arith_mixed_fastpath PRIVATE orkan_headers)
add_test(NAME test_free_arith_mixed_fastpath COMMAND test_free_arith_mixed_fastpath)
set_tests_properties(test_free_arith_mixed_fastpath PROPERTIES LABELS "backend")

# sturm-ieb M2: compound-assign arithmetic mixed quantum/classical fast-path
add_executable(test_compound_arith_mixed_fastpath
    test_compound_arith_mixed_fastpath.cpp
    ${M19_SOURCES}
)
target_include_directories(test_compound_arith_mixed_fastpath PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_compound_arith_mixed_fastpath PRIVATE cxx_std_20)
target_compile_definitions(test_compound_arith_mixed_fastpath PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_compound_arith_mixed_fastpath PRIVATE orkan_headers)
add_test(NAME test_compound_arith_mixed_fastpath COMMAND test_compound_arith_mixed_fastpath)
set_tests_properties(test_compound_arith_mixed_fastpath PROPERTIES LABELS "backend")

# sturm-fbt M1: compound-assign bitwise mixed quantum/classical fast-path
add_executable(test_compound_bitwise_mixed_fastpath
    test_compound_bitwise_mixed_fastpath.cpp
    ${M19_SOURCES}
)
target_include_directories(test_compound_bitwise_mixed_fastpath PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_compound_bitwise_mixed_fastpath PRIVATE cxx_std_20)
target_compile_definitions(test_compound_bitwise_mixed_fastpath PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_compound_bitwise_mixed_fastpath PRIVATE orkan_headers)
add_test(NAME test_compound_bitwise_mixed_fastpath COMMAND test_compound_bitwise_mixed_fastpath)
set_tests_properties(test_compound_bitwise_mixed_fastpath PROPERTIES LABELS "backend")

# ── M5: Mixed quantum/classical integration tests (sturm-x7t) ────────────────
# Verifies all 8 PRD acceptance criteria spanning M1-M4 fixes:
#   AC1: XOR quantum^=classical emits X gates
#   AC2: XOR classical^=quantum promotes and emits CNOT
#   AC3: AND quantum&=classical emits gates for set bits
#   AC4: OR quantum|=classical emits gates
#   AC5: Free & mixed emits gates
#   AC6: Free | mixed emits gates
#   AC7: ADD quantum+=classical emits adder circuit
#   AC8: Regression: both-classical still zero gates
add_executable(test_mixed_quantum_classical
    test_mixed_quantum_classical.cpp
    ${M19_SOURCES}
)
target_include_directories(test_mixed_quantum_classical PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mixed_quantum_classical PRIVATE cxx_std_20)
target_compile_definitions(test_mixed_quantum_classical PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_mixed_quantum_classical PRIVATE orkan_headers)
add_test(NAME test_mixed_quantum_classical COMMAND test_mixed_quantum_classical)
set_tests_properties(test_mixed_quantum_classical PROPERTIES LABELS "backend")

# ── WHEN boolean expressions: WHEN(c | d), WHEN(c & d), WHEN(~c) ────────────
# Verifies that the WHEN macro accepts rvalue boolean expressions (temporaries),
# with classical truth-table tests, superposed control tests, nesting, and
# lvalue regression.
add_executable(test_when_bool_expr
    test_when_bool_expr.cpp
    ${M19_SOURCES}
)
target_include_directories(test_when_bool_expr PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_bool_expr PRIVATE cxx_std_20)
target_compile_definitions(test_when_bool_expr PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_when_bool_expr PRIVATE orkan_headers)
add_test(NAME test_when_bool_expr COMMAND test_when_bool_expr)
set_tests_properties(test_when_bool_expr PROPERTIES LABELS "backend")

# ── Transpiler-MVP M2: destructor gating on STURM_AUTO_UNCOMPUTE ─────────────
# Two targets built from the SAME source tests/test_destructor_gate.cpp so
# that both branches of the #ifdef STURM_AUTO_UNCOMPUTE contract in
# include/sturm/qtypes/qint_core.hpp are exercised by ctest on every CI run.
#
#   test_destructor_gate_on  : STURM_AUTO_UNCOMPUTE defined → destructor
#                              emits inverse gates when uncompute_ is set.
#   test_destructor_gate_off : STURM_AUTO_UNCOMPUTE undefined → destructor
#                              is release-only; zero gates emitted.
#
# Both targets also assert the shared regression that a non-owning register
# releases nothing in the pool regardless of the flag.
#
# Per-target target_compile_definitions are used (not add_compile_definitions)
# so that the top-level STURM_AUTO_UNCOMPUTE option cannot accidentally leak
# into the _off build — the off target must NEVER see STURM_AUTO_UNCOMPUTE
# defined, even when the project-wide option is ON.

# Flag ON build.
#
# The ON target only makes sense when the project-wide STURM_AUTO_UNCOMPUTE
# option is also ON — otherwise we would be locally defining the macro for
# just this target, which would leak STURM_AUTO_UNCOMPUTE=1 into
# compile_commands.json even when the option is OFF (breaking the
# cmake_flags_verify contract at tests/cmake_flags/verify_flags.cmake).
# Gating the entire target + registration on STURM_AUTO_UNCOMPUTE keeps the
# OFF configure clean; the OFF branch of the destructor contract is still
# exercised by test_destructor_gate_off above.
if(STURM_AUTO_UNCOMPUTE)
    add_executable(test_destructor_gate_on
        ${CMAKE_SOURCE_DIR}/tests/test_destructor_gate.cpp
        ${M19_SOURCES}
    )
    target_include_directories(test_destructor_gate_on PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(test_destructor_gate_on PRIVATE cxx_std_20)
    target_compile_definitions(test_destructor_gate_on PRIVATE
        STURM_ANCILLA_CAPACITY=256
        STURM_BACKEND_ENABLED=1
        STURM_AUTO_UNCOMPUTE=1
    )
    target_link_libraries(test_destructor_gate_on PRIVATE orkan_headers)
    add_test(NAME test_destructor_gate_on COMMAND test_destructor_gate_on)
    set_tests_properties(test_destructor_gate_on PROPERTIES LABELS "backend")
endif()

# Flag OFF build.  Critically, this target must NOT receive the macro —
# even if the project-wide STURM_AUTO_UNCOMPUTE option is ON.  We achieve
# that by relying on target_compile_definitions() being additive over the
# directory-level definitions and by NOT re-adding the macro here.  The
# directory-level add_compile_definitions(STURM_AUTO_UNCOMPUTE=1) is still
# visible, so we explicitly undefine the macro for this target via the
# compiler-level -U flag.
add_executable(test_destructor_gate_off
    ${CMAKE_SOURCE_DIR}/tests/test_destructor_gate.cpp
    ${M19_SOURCES}
)
target_include_directories(test_destructor_gate_off PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_destructor_gate_off PRIVATE cxx_std_20)
target_compile_definitions(test_destructor_gate_off PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
# Force-undefine the project-wide STURM_AUTO_UNCOMPUTE macro (added at the
# top-level CMakeLists.txt when the option is ON) for just this target, so
# the OFF branch of the destructor's #ifdef is exercised even when the
# project-wide option is ON.
target_compile_options(test_destructor_gate_off PRIVATE -USTURM_AUTO_UNCOMPUTE)
target_link_libraries(test_destructor_gate_off PRIVATE orkan_headers)
add_test(NAME test_destructor_gate_off COMMAND test_destructor_gate_off)
set_tests_properties(test_destructor_gate_off PROPERTIES LABELS "backend")
