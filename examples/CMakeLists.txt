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

# ── Example: qint qint-qint compound-assigns (Phase C) ──────────────────────
# Routed through sturm-transpile so each `a += b; a -= b; a *= b; a /= b;
# a %= b;` statement gets a `uncompute_*_qint(a, b);` free-function call
# injected before the enclosing scope closes. Same helper / bypass pattern
# as example_constant_arith.
add_quantum_executable(example_qint_arith
    qint_arith.cpp
)
target_sources(example_qint_arith PRIVATE
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
target_link_libraries(example_qint_arith PRIVATE orkan_headers)

# ── Example: qint qint-qint comparison operators (Phase D) ─────────────────
# Routed through sturm-transpile so each `qbool c_xx = a OP b;` VarDecl
# initializer (for OP in {==, !=, <, <=, >, >=}) gets a
# `uncompute_{eq,ne,lt,le,gt,ge}_qint(c_xx, a, b);` free-function call
# injected before the enclosing scope closes. Same helper / bypass pattern
# as example_qint_arith.
add_quantum_executable(example_comparison
    comparison.cpp
)
target_sources(example_comparison PRIVATE
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
target_link_libraries(example_comparison PRIVATE orkan_headers)

# ── Example: compound qbool expressions (Phase E) ──────────────────────────
# Routed through sturm-transpile so the compound `qbool r = (b | c) & d;`
# initializer gets REPLACED with a flat two-decl sequence (one fresh
# `__stu_t<N>` for the inner OR, the original VarDecl name on the outer
# AND) and a LIFO `uncompute_and` / `uncompute_or` pair injected before
# the enclosing scope closes. Same helper / bypass pattern as
# example_comparison; the new uncompute_and runtime symbol added in
# Phase E PE-0 is linked from src/sturm/uncompute/uncompute_api.cpp.
add_quantum_executable(example_compound_expression
    compound_expression.cpp
)
target_sources(example_compound_expression PRIVATE
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
target_link_libraries(example_compound_expression PRIVATE orkan_headers)

# ── Example: compound-`WHEN` integration (Phase F) ──────────────────────────
# Routed through sturm-transpile so the `WHEN((b | c) & d) { ... }` macro
# invocation is rewritten: two flat `qbool __stu_tN = ...;` decls are
# injected ABOVE the WHEN line, the WHEN argument is substituted to the
# outermost `__stu_t1` temp, and a LIFO `uncompute_and(__stu_t1,
# __stu_t0, d); / uncompute_or(__stu_t0, b, c);` pair is planted
# directly after the WHEN body's closing brace (still inside the
# enclosing inner scope). Same helper / bypass pattern as
# example_compound_expression; the Phase F matcher lives in
# transpiler/src/matcher_when_lift.cpp.
add_quantum_executable(example_when_integration
    when_integration.cpp
)
target_sources(example_when_integration PRIVATE
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
target_link_libraries(example_when_integration PRIVATE orkan_headers)

# ── Example: combined Phase A + B + C demo ──────────────────────────────────
# Routed through sturm-transpile so every forward pattern from all three
# phases receives its inverse injection in one file. LIFO rewind runs
# Phase C → Phase B → Phase A just before the inner scope closes. Same
# helper / bypass pattern as the single-phase examples above.
add_quantum_executable(example_phase_abc_demo
    phase_abc_demo.cpp
)
target_sources(example_phase_abc_demo PRIVATE
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
target_link_libraries(example_phase_abc_demo PRIVATE orkan_headers)
