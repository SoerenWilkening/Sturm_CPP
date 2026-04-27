cmake_minimum_required(VERSION 3.16)
enable_language(C)

# ── tests/control/ -- depth-1 control stack invariant baseline ───────────────
#
# Holds tests that capture / pin properties of the depth-1 control stack
# invariant being introduced by the sturm-a3t4 epic.  Currently a single
# baseline test (sturm-a3t4.1) that records per-primitive gate counts at
# n=8 and n=16, both bare and inside a WHEN(c) scope.  The artifact is
# committed alongside this CMakeLists at baseline_gate_counts.txt and is
# the diff target for the post-refactor comparison.

set(STURM_CONTROL_BACKEND_SOURCES
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)

# sturm-a3t4.1 P0 -- baseline gate counts for add/mul/pow_mod_dsl.
#
# pow_mod at n=8 peaks at 4·W² + 2W + 8 = 280 transient ancillas above
# the 4·W=32 input qubits (see tests/lib/test_pow_mod_dsl_ancilla.cpp
# for the structural breakdown).  STURM_ANCILLA_CAPACITY=512 lets us
# allocate the worst-case footprint without bumping the project-wide
# default 256 cap.
add_executable(test_baseline_gate_counts
    test_baseline_gate_counts.cpp
    ${STURM_CONTROL_BACKEND_SOURCES}
)
target_include_directories(test_baseline_gate_counts PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_baseline_gate_counts PRIVATE cxx_std_20)
target_compile_definitions(test_baseline_gate_counts PRIVATE
    STURM_BACKEND_ENABLED=1
)
# Override the project-wide STURM_ANCILLA_CAPACITY=256 set at the root
# CMakeLists.txt with 512 (pow_mod n=8 peaks at 280 transient ancillas
# above the input registers).  The -U/-D dance silences the compiler's
# -Wmacro-redefined warning we'd hit if both definitions reached the
# preprocessor.
target_compile_options(test_baseline_gate_counts PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=512
)
target_link_libraries(test_baseline_gate_counts PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)

# Pass the in-build artifact path as argv[1] so the test writes its
# baseline next to the binary; ctest will then surface the output line
# 'wrote artifact: ...' for follow-up diffs.
add_test(NAME test_baseline_gate_counts
    COMMAND test_baseline_gate_counts
            "${CMAKE_CURRENT_BINARY_DIR}/baseline_gate_counts.txt")
set_tests_properties(test_baseline_gate_counts
    PROPERTIES LABELS "control;modular-arith;baseline")
