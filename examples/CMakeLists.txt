cmake_minimum_required(VERSION 3.16)
enable_language(C)

# ── Example: qint OR + ASCII circuit drawing ────────────────────────────────
# Routed through sturm-transpile under STURM_TRANSPILE=ON via the
# add_quantum_executable() helper (see cmake/SturmTranspile.cmake). Under
# STURM_TRANSPILE=OFF the helper compiles the source directly without any
# LLVM/Clang dependency.
add_quantum_executable(example_or_circuit
    or_circuit.cpp
)

# Runtime implementation sources are not candidates for the transpiler;
# attach them after target creation so they bypass the transpile step.
target_sources(example_or_circuit PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/promotion.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_or_circuit PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)

# ── Example: qint constant-arithmetic compound-assigns (Phase B) ────────────
# Routed through sturm-transpile so each `a += 3; a -= 3; a *= 3; a /= 3;`
# statement gets a dual-operator inverse injected before the enclosing
# scope closes. Same helper / bypass pattern as example_or_circuit.
add_quantum_executable(example_constant_arith
    constant_arith.cpp
)
target_sources(example_constant_arith PRIVATE
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/qint_base.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/dispatch_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/promotion.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/dispatch/reduction_table.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)
target_link_libraries(example_constant_arith PRIVATE orkan_headers)
