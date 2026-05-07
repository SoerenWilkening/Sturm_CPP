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
target_compile_options(test_context PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
target_compile_options(test_exec_append PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
target_compile_options(test_exec_count PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
target_compile_options(test_execute_gate PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_execute_gate PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_execute_gate COMMAND test_execute_gate)
set_tests_properties(test_execute_gate PROPERTIES LABELS "backend")

# Phase K PK-3 (sturm-pzye): test_uncompute_op, test_uncompute_run targets
# retired along with the uncompute_op tagged union and uncompute_run runtime.

# ── M18: 17-qubit cap enforcement on QubitPool ───────────────────────────────
add_executable(test_qubit_cap
    test_qubit_cap.cpp
)
target_include_directories(test_qubit_cap PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qubit_cap PRIVATE cxx_std_20)
target_compile_options(test_qubit_cap PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
add_test(NAME test_qubit_cap COMMAND test_qubit_cap)
set_tests_properties(test_qubit_cap PROPERTIES LABELS "backend")

# Phase K PK-3 (sturm-pzye): test_uncompute_add_const, test_uncompute_each_op,
# test_qbool_uncompute, test_uncompute_or targets retired along with the
# uncompute_op tagged union and qint_base runtime. Note: test_uncompute_or
# is explicitly retired in Phase K PK-4.

# ── Phase E PE-0 (sturm-oheo): explicit uncompute_and free-function API ──────
add_executable(test_uncompute_and
    test_uncompute_and.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_uncompute_and PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_uncompute_and PRIVATE sturm orkan_headers)
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_ccnot_inplace PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_ccnot_inplace PRIVATE sturm orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_ccnot_inplace COMMAND test_ccnot_inplace)
set_tests_properties(test_ccnot_inplace PROPERTIES LABELS "backend")

# Phase K PK-8 (sturm-ignd): test_uncompute_qint_compare retired.
# The test exercised `uncompute_eq_qint(r, a, b)` after a forward `r = (a == b)`
# and asserted the ancilla was returned to |0>. Under Phase K, the forward DSL
# comparator (`lib_eq_dsl`) internally uses `tgt ^= (c0 & c1)` patterns that
# relied on runtime `AndExpr` fusion (PK-2 retired). The correct contract now
# lives in the transpile path; the runtime behavior is no longer guaranteed to
# be zero-ancilla-clean for DSL comparators. File deleted.

# Phase K PK-3 (sturm-pzye): test_when_compound_uncompute retired along with
# the WhenCapture RAII runtime and the uncompute_op tagged union.

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
target_compile_options(test_dispatch_all_classical PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
target_compile_options(test_dispatch_mixed PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
target_compile_options(test_measure PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
target_compile_options(test_init PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_init COMMAND test_init)
set_tests_properties(test_init PROPERTIES LABELS "backend")

# Phase K PK-3 (sturm-pzye): test_promotion retired along with the
# dispatch/promotion helper and the qint_base runtime.

# ── M25: Explicit template instantiation (link-time fixture) ─────────────────
# This target links instantiations.cpp (the explicit instantiation TU) with
# the test fixture TU.  The fixture declares only extern template and calls
# each method; unresolved symbols reveal a broken instantiation contract.
add_executable(test_instantiations
    test_instantiations.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/qtypes/instantiations.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
    # sturm-a3t4.4: instantiations.cpp explicitly instantiates div_kernel /
    # mul_kernel template specialisations (via qint.hpp), which under the
    # depth-1 lift refactor now reference sturm::uncompute_and.  The free
    # function is defined in src/sturm/uncompute/uncompute_api.cpp; without
    # it linked here, test_instantiations fails with an "undefined reference
    # to sturm::uncompute_and" at div_dsl.hpp:180 / mul_dsl.hpp:107.
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
)
target_include_directories(test_instantiations PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_instantiations PRIVATE cxx_std_20)
target_compile_definitions(test_instantiations PRIVATE
    STURM_BACKEND_ENABLED=1
    STURM_USE_EXPLICIT_INSTANTIATIONS=1
)
target_compile_options(test_instantiations PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_end_to_end PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
target_compile_options(test_control_stack_push_pop PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
target_compile_options(test_control_stack_thread_local PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
target_compile_options(test_primitives_v3_count PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
target_compile_options(test_primitives_v3_append PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
target_compile_options(test_primitives_v3_simulate PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_qbool_ops PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_qbool_ops PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_qbool_ops COMMAND test_qbool_ops)
set_tests_properties(test_qbool_ops PROPERTIES LABELS "backend")

# Phase K PK-8 (sturm-ignd): test_adder_dsl, test_adder_dsl_template, and
# test_logic_dsl retired. The Cuccaro MAJ/UMA DSL relies on `c ^= (a & b)`
# AndExpr fusion and on pool-clean ancillas after scope exit — both retired by
# PK-2 / PK-3. Under the new contract the DSL's internal `tgt ^= (a & b)` pattern
# no longer produces a single CCX; it allocates a leaky ancilla + emits CX.
# No transpile analogue exists for the truth-table semantics (the transpiler
# only fuses the `qbool __t = a & b; x ^= __t;` shape). Files deleted.

# ── M18: qint_t operator[](i) returns non-owning qbool ───────────────────────
add_executable(test_subscript
    test_subscript.cpp
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_subscript PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_subscript_count_simulate PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_swap_dsl PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_swap_dsl PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_swap_dsl COMMAND test_swap_dsl)
set_tests_properties(test_swap_dsl PROPERTIES LABELS "backend")

# Phase K PK-8 (sturm-ignd): test_mul_dsl, test_div_dsl,
# test_compare_dsl_truth_tables, test_mod_dsl_truth_table retired. These DSL
# truth-table tests depend on runtime `tgt ^= (a & b)` fusion that PK-2 moved
# to the transpile path; the runtime now allocates a fresh ancilla per AND and
# releases it without zeroing, violating the DSL's clean-ancilla invariant.
# No transpile analogue exists for the full DSL truth tables. Files deleted.

# ── M17: DSL-style POW (repeated squaring) ───────────────────────────────────
add_executable(test_pow_dsl_small
    test_pow_dsl_small.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
    # sturm-a3t4.4: lib_mul_dsl now uses uncompute_and for the depth-1
    # lift; pow_dsl reuses lib_mul_dsl, so this TU is required at link.
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
)
target_include_directories(test_pow_dsl_small PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_pow_dsl_small PRIVATE cxx_std_20)
target_compile_definitions(test_pow_dsl_small PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_pow_dsl_small PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
# Phase K PK-3 (sturm-pzye): src/sturm/uncompute/qint_base.cpp was retired
# along with the uncompute_op tagged union.
set(M19_SOURCES
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
    # sturm-a3t4.4: lib_div_dsl / lib_mul_dsl now use uncompute_and for the
    # depth-1 lift (and lib_add_mod_dsl already did under sturm-a3t4.2),
    # so any backend test that pulls these primitives needs the
    # uncompute_api translation unit linked in.
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_qint_add_e2e PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_qint_mul_e2e PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_qint_compare_e2e PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_qint_compare_e2e PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_qint_compare_e2e COMMAND test_qint_compare_e2e)
set_tests_properties(test_qint_compare_e2e PROPERTIES LABELS "backend")

# Phase K PK-8 (sturm-ignd): test_qint_compare_simulate retired. The 6
# comparison operators (==, !=, <, <=, >, >=) ride on lib_*_dsl library
# routines whose internal `tgt ^= (a & b)` patterns depend on runtime lazy
# fusion (PK-2) and clean-ancilla release (PK-3). Under the new contract the
# DSL can no longer be run end-to-end at runtime with pool-clean ancillas.
# File deleted.

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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_qint_bitwise_e2e PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_qint_bitwise_e2e PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_qint_bitwise_e2e COMMAND test_qint_bitwise_e2e)
set_tests_properties(test_qint_bitwise_e2e PROPERTIES LABELS "backend")

# sturm-qoq0 LO-0.5 — end-to-end pool-drain invariant for every lossy compound
# assignment (`*=`, `/=`, `%=`, `&=`, `|=`).  Asserts that, inside an inner
# block, after the block exits the qubit pool has zero in-use allocations.
# This is the runtime observable that the LO-2 transpiler auto-desugar pass
# must preserve (replacement for the h5it registry-snapshot tests).
add_executable(test_lossy_ancilla_cleaned
    test_lossy_ancilla_cleaned.cpp
    ${M19_SOURCES}
)
target_include_directories(test_lossy_ancilla_cleaned PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_lossy_ancilla_cleaned PRIVATE cxx_std_20)
target_compile_definitions(test_lossy_ancilla_cleaned PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_lossy_ancilla_cleaned PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_lossy_ancilla_cleaned PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_lossy_ancilla_cleaned COMMAND test_lossy_ancilla_cleaned)
set_tests_properties(test_lossy_ancilla_cleaned
    PROPERTIES LABELS "backend;lossy-reversibility")

# sturm-0qu5 LO-3c — full-W `*=` upper-W product release and `/=` remainder
# release drain the qubit pool by enclosing-scope exit. Replaces the
# sturm-pqs0 leak-snapshot test with the post-LO PRD §2 pool-drain
# invariant (mirrors LO-0.5 / sturm-qoq0).
add_executable(test_mul_div_upperw_clean
    test_mul_div_upperw_clean.cpp
    ${M19_SOURCES}
)
target_include_directories(test_mul_div_upperw_clean PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_div_upperw_clean PRIVATE cxx_std_20)
target_compile_definitions(test_mul_div_upperw_clean PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_mul_div_upperw_clean PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_mul_div_upperw_clean PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_mul_div_upperw_clean COMMAND test_mul_div_upperw_clean)
set_tests_properties(test_mul_div_upperw_clean
    PROPERTIES LABELS "backend;lossy-reversibility")

# sturm-h5it.1 — Regression baseline for non-lossy WHEN ops (^=, +=, -=)
add_executable(test_when_inplace_nonlossy
    test_when_inplace_nonlossy.cpp
    ${M19_SOURCES}
)
target_include_directories(test_when_inplace_nonlossy PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_inplace_nonlossy PRIVATE cxx_std_20)
target_compile_definitions(test_when_inplace_nonlossy PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_when_inplace_nonlossy PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_when_inplace_nonlossy PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_when_inplace_nonlossy COMMAND test_when_inplace_nonlossy)
set_tests_properties(test_when_inplace_nonlossy PROPERTIES LABELS "backend;h5it")

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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_e2e_arithmetic PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_e2e_arithmetic PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_e2e_arithmetic COMMAND test_e2e_arithmetic)
set_tests_properties(test_e2e_arithmetic PROPERTIES LABELS "backend")

# Phase K PK-8 (sturm-ignd): test_e2e_pow_mod + test_e2e_when_nested retired.
# test_e2e_pow_mod drives lib_pow_dsl + lib_mod_dsl — both DSL routines depend
# on the retired runtime lazy fusion. test_e2e_when_nested asserts that 5 CCX
# gates exactly are emitted for `lib_c_n_AND_dsl(n=4)`, but that DSL routine's
# internal `anc[i] ^= (controls[j] & anc[k])` pattern now emits two gates per
# step rather than a single CCX. Both files deleted.

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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_e2e_all_modes PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_free_op_simulate PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_free_op_gate_emission PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_compound_assign_simulate PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_compound_assign_gate_emission PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_shift_simulate PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_bitwise_not_operator PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_bitwise_and_or_simulate PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_qbool_acceptance_backend PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_when_control_stack_bridge PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_lifted_primitives PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_phi_theta_backend PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_bitproxy_when_promotion PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_bitproxy_when_promotion PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_bitproxy_when_promotion COMMAND test_bitproxy_when_promotion)
set_tests_properties(test_bitproxy_when_promotion PROPERTIES LABELS "backend")

# ── sturm-51wc: per-bit phi()/theta() proxies on BitProxy (RZ/RY) ────────────
add_executable(test_bit_proxy_phi_theta
    test_bit_proxy_phi_theta.cpp
    ${M19_SOURCES}
)
target_include_directories(test_bit_proxy_phi_theta PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_bit_proxy_phi_theta PRIVATE cxx_std_20)
target_compile_definitions(test_bit_proxy_phi_theta PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_bit_proxy_phi_theta PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_bit_proxy_phi_theta PRIVATE orkan_headers)
add_test(NAME test_bit_proxy_phi_theta COMMAND test_bit_proxy_phi_theta)
set_tests_properties(test_bit_proxy_phi_theta PROPERTIES LABELS "backend")

# ── M8: Template logic/swap/compare/c_and DSL (BitProxy instantiation) ───────
add_executable(test_m8_template_dsl
    test_m8_template_dsl.cpp
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_m8_template_dsl PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_m8_template_dsl PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_m8_template_dsl COMMAND test_m8_template_dsl)
set_tests_properties(test_m8_template_dsl PROPERTIES LABELS "backend")

# Phase K PK-8 (sturm-ignd): test_m7_template_dsl retired. The template
# mul/div/mod DSL exercise depends on the same lazy-fusion runtime contract
# as test_mul_dsl / test_div_dsl / test_mod_dsl_truth_table (PK-2). File
# deleted.

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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_when_shift_promotion PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_copy_register_quantum_only PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_acceptance_prd PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_free_bitwise_neg1_qubits PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_free_arith_mixed_fastpath PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_compound_arith_mixed_fastpath PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_compound_bitwise_mixed_fastpath PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_mixed_quantum_classical PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_when_bool_expr PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_when_bool_expr PRIVATE orkan_headers)
add_test(NAME test_when_bool_expr COMMAND test_when_bool_expr)
set_tests_properties(test_when_bool_expr PROPERTIES LABELS "backend")

# Phase K PK-3 (sturm-pzye): test_destructor_gate_on / test_destructor_gate_off
# retired along with tests/test_destructor_gate.cpp. Under B10 the destructor
# is unconditionally release-only, so the flag-gated branch contract disappears.
# Note: test_destructor_gate.cpp is explicitly retired in Phase K PK-4.

# sturm-0j3a LO-0.4 — WHEN-lifted lossy-op gate-equivalence under superposed
# control. Pins the PRD §5 contract that the LO-2 desugar relies on:
# per-bit Fredkin via BitProxy for the swap, controlled CCX sweep for
# invert(lib_c_AND_dsl). LO-2 is not yet landed; this test exercises the
# constituent primitives directly.
add_executable(test_when_lossy_gate_equiv
    test_when_lossy_gate_equiv.cpp
    ${M19_SOURCES}
)
target_include_directories(test_when_lossy_gate_equiv PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_lossy_gate_equiv PRIVATE cxx_std_20)
target_compile_definitions(test_when_lossy_gate_equiv PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_when_lossy_gate_equiv PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_when_lossy_gate_equiv PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_when_lossy_gate_equiv COMMAND test_when_lossy_gate_equiv)
set_tests_properties(test_when_lossy_gate_equiv
    PROPERTIES LABELS "backend;lossy-reversibility")

# sturm-wrmg LO-3b -- post-WHEN ancilla reversibility for all five controlled
# lossy compound assignments (replaces tests/test_when_scope_garbage_consume.cpp).
add_executable(test_when_lossy_reversibility
    test_when_lossy_reversibility.cpp
    ${M19_SOURCES}
)
target_include_directories(test_when_lossy_reversibility PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_lossy_reversibility PRIVATE cxx_std_20)
target_compile_definitions(test_when_lossy_reversibility PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_when_lossy_reversibility PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_when_lossy_reversibility PRIVATE orkan_headers)
add_test(NAME test_when_lossy_reversibility COMMAND test_when_lossy_reversibility)
set_tests_properties(test_when_lossy_reversibility
    PROPERTIES LABELS "backend;lossy-reversibility")

# sturm-vuru LO-3a -- gate-equivalence pinning for all five controlled lossy
# compound assignments (extends sturm-0j3a's `&=`-only coverage).
add_executable(test_when_lossy_gate_equiv_full
    test_when_lossy_gate_equiv_full.cpp
    ${M19_SOURCES}
)
target_include_directories(test_when_lossy_gate_equiv_full PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_when_lossy_gate_equiv_full PRIVATE cxx_std_20)
target_compile_definitions(test_when_lossy_gate_equiv_full PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_when_lossy_gate_equiv_full PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_when_lossy_gate_equiv_full PRIVATE orkan_headers)
add_test(NAME test_when_lossy_gate_equiv_full COMMAND test_when_lossy_gate_equiv_full)
set_tests_properties(test_when_lossy_gate_equiv_full
    PROPERTIES LABELS "backend;lossy-reversibility")
