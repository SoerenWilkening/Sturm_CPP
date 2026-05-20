cmake_minimum_required(VERSION 3.16)

# ── tests/control/ -- depth-1 control stack invariant baseline ───────────────
#
# Holds tests that capture / pin properties of the depth-1 control stack
# invariant being introduced by the sturm-a3t4 epic. Backend-touching
# targets link the shared `sturm_backend_runtime` OBJECT lib defined in
# tests/CMakeLists.txt (sturm-x4iq) instead of re-listing the 7-file
# backend source set on each `add_executable`.

# sturm-a3t4.1 P0 -- baseline gate counts for add/mul/pow_mod_dsl.
#
# pow_mod at n=8 peaks at 4·W² + 2W + 8 = 280 transient ancillas above
# the 4·W=32 input qubits (see tests/lib/test_pow_mod_dsl_ancilla.cpp
# for the structural breakdown). sturm-5jta dropped the legacy
# compile-time pool cap, so this peak is allocated naturally (the pool
# grows on demand).
add_executable(test_baseline_gate_counts test_baseline_gate_counts.cpp)
target_compile_features(test_baseline_gate_counts PRIVATE cxx_std_20)
target_link_libraries(test_baseline_gate_counts PRIVATE sturm_backend_runtime)

# Pass the in-build artifact path as argv[1] so the test writes its
# baseline next to the binary; ctest will then surface the output line
# 'wrote artifact: ...' for follow-up diffs.
add_test(NAME test_baseline_gate_counts
    COMMAND test_baseline_gate_counts
            "${CMAKE_CURRENT_BINARY_DIR}/baseline_gate_counts.txt")
set_tests_properties(test_baseline_gate_counts
    PROPERTIES LABELS "control;modular-arith;baseline")

# sturm-a3t4.2 P1 -- add_mod_dsl depth-1 lift pattern (forward + adjoint).
add_executable(test_add_mod_when_lift test_add_mod_when_lift.cpp)
target_compile_features(test_add_mod_when_lift PRIVATE cxx_std_20)
target_link_libraries(test_add_mod_when_lift PRIVATE sturm_backend_runtime)
# sturm-eksf: gated behind STURM_FULL_TEST_SUITE.
if(STURM_FULL_TEST_SUITE)
    add_test(NAME test_add_mod_when_lift COMMAND test_add_mod_when_lift)
    set_tests_properties(test_add_mod_when_lift
        PROPERTIES LABELS "control;modular-arith;lift")
endif()

# sturm-a3t4.3 P2 -- mul_mod_dsl + pow_mod_dsl depth-1 lift pattern.
add_executable(test_mul_mod_when_lift test_mul_mod_when_lift.cpp)
target_compile_features(test_mul_mod_when_lift PRIVATE cxx_std_20)
target_link_libraries(test_mul_mod_when_lift PRIVATE sturm_backend_runtime)
add_test(NAME test_mul_mod_when_lift COMMAND test_mul_mod_when_lift)
set_tests_properties(test_mul_mod_when_lift
    PROPERTIES LABELS "control;modular-arith;lift")

add_executable(test_pow_mod_when_lift test_pow_mod_when_lift.cpp)
target_compile_features(test_pow_mod_when_lift PRIVATE cxx_std_20)
target_link_libraries(test_pow_mod_when_lift PRIVATE sturm_backend_runtime)
add_test(NAME test_pow_mod_when_lift COMMAND test_pow_mod_when_lift)
set_tests_properties(test_pow_mod_when_lift
    PROPERTIES LABELS "control;modular-arith;lift")

# sturm-a3t4.4 P3 -- div_dsl + mul_dsl depth-1 lift pattern.
add_executable(test_mul_when_lift test_mul_when_lift.cpp)
target_compile_features(test_mul_when_lift PRIVATE cxx_std_20)
target_link_libraries(test_mul_when_lift PRIVATE sturm_backend_runtime)
add_test(NAME test_mul_when_lift COMMAND test_mul_when_lift)
set_tests_properties(test_mul_when_lift
    PROPERTIES LABELS "control;arith;lift")

add_executable(test_div_when_lift test_div_when_lift.cpp)
target_compile_features(test_div_when_lift PRIVATE cxx_std_20)
target_link_libraries(test_div_when_lift PRIVATE sturm_backend_runtime)
add_test(NAME test_div_when_lift COMMAND test_div_when_lift)
set_tests_properties(test_div_when_lift
    PROPERTIES LABELS "control;arith;lift")

# sturm-a3t4.7 P6 -- depth-1 control stack invariant regression net.
#
# Exercises every public op (add_mod, mul_mod, pow_mod, div, mul, plus
# qbool::flip) bare and inside WHEN(c), polling
# current_control_stack().depth() from the new execute_gate hook
# (sturm::set_execute_gate_hook) on every emitted gate. Asserts max
# observed depth <= 1 across each invocation.
add_executable(test_depth_invariant depth_invariant_test.cpp)
target_compile_features(test_depth_invariant PRIVATE cxx_std_20)
target_link_libraries(test_depth_invariant PRIVATE sturm_backend_runtime)
add_test(NAME test_depth_invariant COMMAND test_depth_invariant)
set_tests_properties(test_depth_invariant
    PROPERTIES LABELS "control;invariant;depth")
