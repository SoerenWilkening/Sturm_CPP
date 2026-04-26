cmake_minimum_required(VERSION 3.16)
enable_language(C)

# ── tests/lib/ -- DSL-level adjoint tests (sturm-lo LO-1 family) ──────────────
#
# Holds tests that pin `invert(<dsl>)(...)` roundtrip contracts for the lib/
# DSL primitives consumed by the LO compile-time rewrite pass (sturm-lo).
# Mirrors the tests/qtypes/ and tests/backend/ patterns: each target pulls in
# the shared M19 backend sources (context, control_stack, execute_gate,
# orkan_bridge) so the qbool/BitProxy operators can emit gates and simulate.

set(STURM_LIB_BACKEND_SOURCES
    ${CMAKE_SOURCE_DIR}/src/sturm/core/execute_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/context.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/control_stack.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/exec_simulate.cpp
    ${CMAKE_SOURCE_DIR}/src/sturm/backend/orkan_bridge.cpp
)

# sturm-le6w LO-1d -- lib_or_dsl adjoint roundtrip.
add_executable(test_or_dsl_adjoint
    test_or_dsl_adjoint.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_or_dsl_adjoint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_or_dsl_adjoint PRIVATE cxx_std_20)
target_compile_definitions(test_or_dsl_adjoint PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_or_dsl_adjoint PRIVATE orkan_headers)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/sturm/core/gate_kind.c
    PROPERTIES LANGUAGE C
               COMPILE_FLAGS "-std=c11"
)
add_test(NAME test_or_dsl_adjoint COMMAND test_or_dsl_adjoint)
set_tests_properties(test_or_dsl_adjoint
    PROPERTIES LABELS "lib;lossy-reversibility")

# sturm-eum8 LO-1c -- lib_c_AND_dsl adjoint roundtrip (CCX self-inverse).
add_executable(test_c_and_dsl_adjoint
    test_c_and_dsl_adjoint.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_c_and_dsl_adjoint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_c_and_dsl_adjoint PRIVATE cxx_std_20)
target_compile_definitions(test_c_and_dsl_adjoint PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_c_and_dsl_adjoint PRIVATE orkan_headers)
add_test(NAME test_c_and_dsl_adjoint COMMAND test_c_and_dsl_adjoint)
set_tests_properties(test_c_and_dsl_adjoint
    PROPERTIES LABELS "lib;lossy-reversibility")

# sturm-1rjw LO-1b -- lib_div_dsl + lib_mod_dsl adjoint roundtrip.
add_executable(test_div_mod_dsl_adjoint
    test_div_mod_dsl_adjoint.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_div_mod_dsl_adjoint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_div_mod_dsl_adjoint PRIVATE cxx_std_20)
target_compile_definitions(test_div_mod_dsl_adjoint PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_div_mod_dsl_adjoint PRIVATE orkan_headers)
add_test(NAME test_div_mod_dsl_adjoint COMMAND test_div_mod_dsl_adjoint)
set_tests_properties(test_div_mod_dsl_adjoint
    PROPERTIES LABELS "lib;lossy-reversibility")

# sturm-735v LO-1a -- lib_mul_dsl adjoint roundtrip.
add_executable(test_mul_dsl_adjoint
    test_mul_dsl_adjoint.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_mul_dsl_adjoint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_dsl_adjoint PRIVATE cxx_std_20)
target_compile_definitions(test_mul_dsl_adjoint PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_mul_dsl_adjoint PRIVATE orkan_headers)
add_test(NAME test_mul_dsl_adjoint COMMAND test_mul_dsl_adjoint)
set_tests_properties(test_mul_dsl_adjoint
    PROPERTIES LABELS "lib;lossy-reversibility")

# ── sturm-r5pn modular arithmetic scaffold tests (P0.2). ─────────────────────
#
# Empty `int main(){return 0;}` stubs registered now so that the upcoming
# Phase 1…4 TDD beats only need to touch the bodies of files that already
# compile and are wired into CTest. Backend sources are included to match
# the established pattern; subsequent beats will exercise the simulator
# the same way `test_div_mod_dsl_adjoint` does.

# sturm-r5pn P1 -- lib_add_mod_dsl scaffold (will be filled by Phase 1 beats).
add_executable(test_add_mod_dsl
    test_add_mod_dsl.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_add_mod_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_add_mod_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_add_mod_dsl PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_add_mod_dsl PRIVATE orkan_headers)
add_test(NAME test_add_mod_dsl COMMAND test_add_mod_dsl)
set_tests_properties(test_add_mod_dsl
    PROPERTIES LABELS "lib;modular-arith")

# sturm-yh3d.5 P1.5 -- lib_add_mod_dsl adjoint round-trip.
add_executable(test_add_mod_dsl_adjoint
    test_add_mod_dsl_adjoint.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_add_mod_dsl_adjoint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_add_mod_dsl_adjoint PRIVATE cxx_std_20)
target_compile_definitions(test_add_mod_dsl_adjoint PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_add_mod_dsl_adjoint PRIVATE orkan_headers)
add_test(NAME test_add_mod_dsl_adjoint COMMAND test_add_mod_dsl_adjoint)
set_tests_properties(test_add_mod_dsl_adjoint
    PROPERTIES LABELS "lib;modular-arith;lossy-reversibility")

# sturm-yh3d.6 P1.6 -- lib_add_mod_dsl peak-ancilla budget assertion.
# Plan §3.3 row 1.6 wording: peak live ancillas <= W + 3.  Achieved bound is
# W + 6 (PRD §6.4 `W + O(1)` is met); see test header for the breakdown.
add_executable(test_add_mod_dsl_ancilla
    test_add_mod_dsl_ancilla.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_add_mod_dsl_ancilla PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_add_mod_dsl_ancilla PRIVATE cxx_std_20)
target_compile_definitions(test_add_mod_dsl_ancilla PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_add_mod_dsl_ancilla PRIVATE orkan_headers)
add_test(NAME test_add_mod_dsl_ancilla COMMAND test_add_mod_dsl_ancilla)
set_tests_properties(test_add_mod_dsl_ancilla
    PROPERTIES LABELS "lib;modular-arith")

# sturm-yh3d.7 P1.7 -- lib_add_mod_dsl pool live-count round-trip / LIFO
# release.  Dedicated drain test mirroring tests/backend/test_lossy_ancilla_cleaned
# (sturm-qoq0): every Beat-1.3 input (W=2 exhaustive sweep) is run inside its
# own scope and the post-call QubitPool::in_use() is asserted equal to the
# pre-call value, plus a LIFO-recycle witness.  See test header for rationale.
add_executable(test_add_mod_dsl_pool_drain
    test_add_mod_dsl_pool_drain.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_add_mod_dsl_pool_drain PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_add_mod_dsl_pool_drain PRIVATE cxx_std_20)
target_compile_definitions(test_add_mod_dsl_pool_drain PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_add_mod_dsl_pool_drain PRIVATE orkan_headers)
add_test(NAME test_add_mod_dsl_pool_drain COMMAND test_add_mod_dsl_pool_drain)
set_tests_properties(test_add_mod_dsl_pool_drain
    PROPERTIES LABELS "lib;modular-arith")

# sturm-r5pn P2 -- lib_mul_mod_dsl scaffold (will be filled by Phase 2 beats).
# Beat 2.3 (sturm-kubb.3): W=2 exhaustive sweep iterates 14 multiplications,
# each on a 25-qubit state vector (2^25 = 33M amplitudes).  Single
# classical case profiled at ~125 s on the dev box, so 15 cases
# (3 n=0 + 1 single + 14 sweep) × ~125 s ≈ 30 min — past ctest's
# default 1500 s and our prior 1800 s --timeout.  Set TIMEOUT to
# 3600 s (1 h) to leave headroom for slower CI runners and the
# beat-2.4 W=3 random sweep that lands next.
add_executable(test_mul_mod_dsl
    test_mul_mod_dsl.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_mul_mod_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_mod_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_mul_mod_dsl PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_mul_mod_dsl PRIVATE orkan_headers)
add_test(NAME test_mul_mod_dsl COMMAND test_mul_mod_dsl)
set_tests_properties(test_mul_mod_dsl
    PROPERTIES LABELS "lib;modular-arith"
               TIMEOUT 3600)

# sturm-kubb.5 P2.5 -- lib_mul_mod_dsl adjoint round-trip.
# Plan §4.3 row 2.5: forward + adjoint must zero r, preserve a/b/n, and
# return the QubitPool live-count to its pre-call value.  W=2 uses the
# orkan simulator (25-qubit state vector — chain mul_mod peaks above the
# kMaxQubits=17 cap); W=3 uses the APPEND-mode classical-trace harness
# from beat 2.4 (sturm-kubb.4) because the simulator is infeasible at
# W=3 (~35 live qubits).  Each W=2 case runs ~125 s on the dev box (14
# cases ≈ 30 min) and the W=3 sweep runs in seconds — set TIMEOUT to
# 3600 s to mirror the forward test_mul_mod_dsl budget.
add_executable(test_mul_mod_dsl_adjoint
    test_mul_mod_dsl_adjoint.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_mul_mod_dsl_adjoint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_mod_dsl_adjoint PRIVATE cxx_std_20)
target_compile_definitions(test_mul_mod_dsl_adjoint PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_mul_mod_dsl_adjoint PRIVATE orkan_headers)
add_test(NAME test_mul_mod_dsl_adjoint COMMAND test_mul_mod_dsl_adjoint)
set_tests_properties(test_mul_mod_dsl_adjoint
    PROPERTIES LABELS "lib;modular-arith;lossy-reversibility"
               TIMEOUT 3600)

# sturm-r5pn P3 -- lib_pow_mod_dsl scaffold (will be filled by Phase 3 beats).
add_executable(test_pow_mod_dsl
    test_pow_mod_dsl.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_pow_mod_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_pow_mod_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_pow_mod_dsl PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_pow_mod_dsl PRIVATE orkan_headers)
add_test(NAME test_pow_mod_dsl COMMAND test_pow_mod_dsl)
set_tests_properties(test_pow_mod_dsl
    PROPERTIES LABELS "lib;modular-arith")

# sturm-r5pn P4 -- qint_modular free-fn scaffold (will be filled by Phase 4 beats).
add_executable(test_qint_modular
    test_qint_modular.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_qint_modular PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_qint_modular PRIVATE cxx_std_20)
target_compile_definitions(test_qint_modular PRIVATE
    STURM_ANCILLA_CAPACITY=256
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_qint_modular PRIVATE orkan_headers)
add_test(NAME test_qint_modular COMMAND test_qint_modular)
set_tests_properties(test_qint_modular
    PROPERTIES LABELS "lib;modular-arith")
