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
    # sturm-a3t4.2: add_mod_dsl now uses uncompute_and for the depth-1 lift.
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
)

# sturm-a3t4.1 P0 -- baseline gate counts for add/mul/pow_mod_dsl.
#
# pow_mod at n=8 peaks at 4·W² + 2W + 8 = 280 transient ancillas above
# the 4·W=32 input qubits (see tests/lib/test_pow_mod_dsl_ancilla.cpp
# for the structural breakdown).  STURM_ANCILLA_CAPACITY=512 matches
# the post-sturm-8n73 project-wide default; we keep the explicit -U/-D
# override so the per-target intent ("worst-case fits at 512") is
# pinned even if the project default ever moves again.
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
# Pin per-target STURM_ANCILLA_CAPACITY=512 (matches the project-wide
# default since sturm-8n73, but keep the explicit pin so the worst-case
# pow_mod n=8 budget of 280 transient ancillas stays documented at the
# target).  The -U/-D dance silences the compiler's -Wmacro-redefined
# warning we'd hit if both definitions reached the preprocessor
# (sturm-cgg5).
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

# sturm-a3t4.2 P1 -- add_mod_dsl depth-1 lift pattern (forward + adjoint).
#
# Pins behavioural correctness of the new
#   if (outer = active_control()) { tmp = outer & flag; WHEN(tmp) {...}; uncompute_and(...); }
#   else                          { WHEN(flag) {...}; }
# lift in both add_mod_dsl.hpp and add_mod_dsl_adj.hpp.  Bare runs exercise
# the `else` branch; WHEN(c)-wrapped runs exercise the `if (outer)` AND-fold.
add_executable(test_add_mod_when_lift
    test_add_mod_when_lift.cpp
    ${STURM_CONTROL_BACKEND_SOURCES}
)
target_include_directories(test_add_mod_when_lift PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_add_mod_when_lift PRIVATE cxx_std_20)
target_compile_definitions(test_add_mod_when_lift PRIVATE
    STURM_BACKEND_ENABLED=1
)
# Inherit the same STURM_ANCILLA_CAPACITY override pattern used by
# test_baseline_gate_counts.  W=2 here easily fits in any reasonable
# pool, but we pin the historical 256 cap explicitly via -U/-D for
# consistency (and to silence -Wmacro-redefined under the post-
# sturm-8n73 project default of 512 -- see sturm-cgg5).
target_compile_options(test_add_mod_when_lift PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_add_mod_when_lift PRIVATE orkan_headers)
# sturm-eksf: gated behind STURM_FULL_TEST_SUITE.
if(STURM_FULL_TEST_SUITE)
    add_test(NAME test_add_mod_when_lift COMMAND test_add_mod_when_lift)
    set_tests_properties(test_add_mod_when_lift
        PROPERTIES LABELS "control;modular-arith;lift")
endif()

# sturm-a3t4.3 P2 -- mul_mod_dsl + pow_mod_dsl depth-1 lift pattern
# (forward + adjoint).  Pins behavioural correctness of the same lift
# applied to the b_bits[i] / exp_bits[i] flag operands in the chain-style
# shift-and-add (mul_mod) and repeated-squaring (pow_mod) primitives.
# Both bare runs (`else` branch -- WHEN(flag)) and WHEN(c)-wrapped runs
# (`if (outer)` AND-fold) are exercised; coverage is intentionally minimal
# per the issue's "reduce the modular-arithmetic test surface" guidance,
# leaving exhaustive correctness to tests/lib/test_{mul,pow}_mod_dsl*.
add_executable(test_mul_mod_when_lift
    test_mul_mod_when_lift.cpp
    ${STURM_CONTROL_BACKEND_SOURCES}
)
target_include_directories(test_mul_mod_when_lift PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_mod_when_lift PRIVATE cxx_std_20)
target_compile_definitions(test_mul_mod_when_lift PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_mul_mod_when_lift PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_mul_mod_when_lift PRIVATE orkan_headers)
add_test(NAME test_mul_mod_when_lift COMMAND test_mul_mod_when_lift)
set_tests_properties(test_mul_mod_when_lift
    PROPERTIES LABELS "control;modular-arith;lift")

add_executable(test_pow_mod_when_lift
    test_pow_mod_when_lift.cpp
    ${STURM_CONTROL_BACKEND_SOURCES}
)
target_include_directories(test_pow_mod_when_lift PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_pow_mod_when_lift PRIVATE cxx_std_20)
target_compile_definitions(test_pow_mod_when_lift PRIVATE
    STURM_BACKEND_ENABLED=1
)
# pow_mod's chain-style allocations balloon at W=2; bump the ancilla cap to
# 512 to match test_baseline_gate_counts' header budget.
target_compile_options(test_pow_mod_when_lift PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=512
)
target_link_libraries(test_pow_mod_when_lift PRIVATE orkan_headers)
add_test(NAME test_pow_mod_when_lift COMMAND test_pow_mod_when_lift)
set_tests_properties(test_pow_mod_when_lift
    PROPERTIES LABELS "control;modular-arith;lift")

# sturm-a3t4.4 P3 -- div_dsl + mul_dsl depth-1 lift pattern (forward +
# adjoint).  Pins behavioural correctness of the same lift applied to the
# overflow / sgn[i] / quotient[0] flag operands in lib_div_dsl and the
# b_bits[i] flag operand in lib_mul_dsl.  Bare runs exercise the `else`
# branch (WHEN(flag_q)); WHEN(c)-wrapped runs exercise the `if (outer)`
# AND-fold.  Coverage is intentionally minimal per the issue's "reduce
# surface as in P1/P2" guidance, leaving exhaustive correctness to
# tests/lib/test_{mul,div_mod}_dsl_adjoint.
add_executable(test_mul_when_lift
    test_mul_when_lift.cpp
    ${STURM_CONTROL_BACKEND_SOURCES}
)
target_include_directories(test_mul_when_lift PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_when_lift PRIVATE cxx_std_20)
target_compile_definitions(test_mul_when_lift PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_mul_when_lift PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_mul_when_lift PRIVATE orkan_headers)
add_test(NAME test_mul_when_lift COMMAND test_mul_when_lift)
set_tests_properties(test_mul_when_lift
    PROPERTIES LABELS "control;arith;lift")

add_executable(test_div_when_lift
    test_div_when_lift.cpp
    ${STURM_CONTROL_BACKEND_SOURCES}
)
target_include_directories(test_div_when_lift PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_div_when_lift PRIVATE cxx_std_20)
target_compile_definitions(test_div_when_lift PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_div_when_lift PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_div_when_lift PRIVATE orkan_headers)
add_test(NAME test_div_when_lift COMMAND test_div_when_lift)
set_tests_properties(test_div_when_lift
    PROPERTIES LABELS "control;arith;lift")

# sturm-a3t4.7 P6 -- depth-1 control stack invariant regression net.
#
# Exercises every public op (add_mod, mul_mod, pow_mod, div, mul, plus
# qbool::flip) bare and inside WHEN(c), polling
# current_control_stack().depth() from the new execute_gate hook
# (sturm::set_execute_gate_hook) on every emitted gate.  Asserts max
# observed depth <= 1 across each invocation.  A future op that regresses
# to raw control_stack.push_control() while a WHEN(c) is live would
# observe depth=2 here and trip a clear depth>1 abort -- belt-and-braces
# alongside the runtime push_control assert promoted in sturm-a3t4.6.
add_executable(test_depth_invariant
    depth_invariant_test.cpp
    ${STURM_CONTROL_BACKEND_SOURCES}
)
target_include_directories(test_depth_invariant PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_depth_invariant PRIVATE cxx_std_20)
target_compile_definitions(test_depth_invariant PRIVATE
    STURM_BACKEND_ENABLED=1
)
# pow_mod's chain-style allocations balloon at W=2; bump the ancilla cap
# to 512 to match test_baseline_gate_counts / test_pow_mod_when_lift.
target_compile_options(test_depth_invariant PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=512
)
target_link_libraries(test_depth_invariant PRIVATE orkan_headers)
add_test(NAME test_depth_invariant COMMAND test_depth_invariant)
set_tests_properties(test_depth_invariant
    PROPERTIES LABELS "control;invariant;depth")
