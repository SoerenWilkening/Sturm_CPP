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
    # sturm-a3t4.2: add_mod_dsl now uses uncompute_and for the depth-1 lift.
    ${CMAKE_SOURCE_DIR}/src/sturm/uncompute/uncompute_api.cpp
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_or_dsl_adjoint PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_c_and_dsl_adjoint PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_div_mod_dsl_adjoint PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_mul_dsl_adjoint PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_add_mod_dsl PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_add_mod_dsl_adjoint PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_add_mod_dsl_ancilla PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_add_mod_dsl_pool_drain PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_add_mod_dsl_pool_drain PRIVATE orkan_headers)
add_test(NAME test_add_mod_dsl_pool_drain COMMAND test_add_mod_dsl_pool_drain)
set_tests_properties(test_add_mod_dsl_pool_drain
    PROPERTIES LABELS "lib;modular-arith")

# ── sturm-wdas Beat B -- lib_double_mod_dsl + adjoint round-trip / ancilla /
#    pool-drain tests for the in-place modular doubling primitive used by
#    Beat C's lib_mul_mod_dsl rewrite (sturm-7cix).  Mirrors the
#    test_add_mod_dsl_* family.

# sturm-wdas Beat B -- lib_double_mod_dsl forward (n==0 no-op, single classical
# case, W=2 odd-n exhaustive sweep, (n+1)/2 LSB-edge case, W=3 odd-n random
# sweep).  See test header for the odd-n precondition rationale.
add_executable(test_double_mod_dsl
    test_double_mod_dsl.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_double_mod_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_double_mod_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_double_mod_dsl PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_double_mod_dsl PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_double_mod_dsl PRIVATE orkan_headers)
add_test(NAME test_double_mod_dsl COMMAND test_double_mod_dsl)
set_tests_properties(test_double_mod_dsl
    PROPERTIES LABELS "lib;modular-arith")

# sturm-wdas Beat B -- lib_double_mod_dsl adjoint round-trip (W=2 odd-n
# exhaustive + the (n+1)/2 LSB-edge case from design note #3).  Pins
# `invert<&lib_double_mod_dsl<BitProxy>>()` resolution alongside the
# round-trip behaviour.
add_executable(test_double_mod_dsl_adjoint
    test_double_mod_dsl_adjoint.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_double_mod_dsl_adjoint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_double_mod_dsl_adjoint PRIVATE cxx_std_20)
target_compile_definitions(test_double_mod_dsl_adjoint PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_double_mod_dsl_adjoint PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_double_mod_dsl_adjoint PRIVATE orkan_headers)
add_test(NAME test_double_mod_dsl_adjoint COMMAND test_double_mod_dsl_adjoint)
set_tests_properties(test_double_mod_dsl_adjoint
    PROPERTIES LABELS "lib;modular-arith;lossy-reversibility")

# sturm-wdas Beat B -- lib_double_mod_dsl peak-ancilla budget assertion.
# Achieved bound is constant 5 above the (2W+1) inputs (n_pad + lt_flag +
# carry_anc + inner adder transient + emit_CCX_lifted fold ancilla under
# the lift_under(lt_flag) body).  PRD `W + O(1)` requirement is met.
add_executable(test_double_mod_dsl_ancilla
    test_double_mod_dsl_ancilla.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_double_mod_dsl_ancilla PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_double_mod_dsl_ancilla PRIVATE cxx_std_20)
target_compile_definitions(test_double_mod_dsl_ancilla PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_double_mod_dsl_ancilla PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_double_mod_dsl_ancilla PRIVATE orkan_headers)
add_test(NAME test_double_mod_dsl_ancilla COMMAND test_double_mod_dsl_ancilla)
set_tests_properties(test_double_mod_dsl_ancilla
    PROPERTIES LABELS "lib;modular-arith")

# sturm-wdas Beat B -- lib_double_mod_dsl pool live-count round-trip / LIFO
# release.  Dedicated drain test mirroring beat 1.7
# (test_add_mod_dsl_pool_drain.cpp).  W=2 odd-n sweep + LIFO-recycle witness.
add_executable(test_double_mod_dsl_pool_drain
    test_double_mod_dsl_pool_drain.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_double_mod_dsl_pool_drain PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_double_mod_dsl_pool_drain PRIVATE cxx_std_20)
target_compile_definitions(test_double_mod_dsl_pool_drain PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_double_mod_dsl_pool_drain PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_double_mod_dsl_pool_drain PRIVATE orkan_headers)
add_test(NAME test_double_mod_dsl_pool_drain COMMAND test_double_mod_dsl_pool_drain)
set_tests_properties(test_double_mod_dsl_pool_drain
    PROPERTIES LABELS "lib;modular-arith")

# ── sturm-8lnp Beat A -- lib_add_mod_inplace_dsl + adjoint round-trip /
#    ancilla / pool-drain tests for the in-place modular addition primitive
#    used by Beat C's lib_mul_mod_dsl rewrite (sturm-7cix).  Mirrors the
#    test_add_mod_dsl_* family.

# sturm-8lnp Beat A -- lib_add_mod_inplace_dsl forward (n==0 no-op, single
# classical case, W=2 exhaustive sweep, W=3 random sweep).  See test header
# for the (W+1)-bit dest layout and the algorithm summary.
add_executable(test_add_mod_inplace_dsl
    test_add_mod_inplace_dsl.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_add_mod_inplace_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_add_mod_inplace_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_add_mod_inplace_dsl PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_add_mod_inplace_dsl PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_add_mod_inplace_dsl PRIVATE orkan_headers)
add_test(NAME test_add_mod_inplace_dsl COMMAND test_add_mod_inplace_dsl)
set_tests_properties(test_add_mod_inplace_dsl
    PROPERTIES LABELS "lib;modular-arith")

# sturm-8lnp Beat A -- lib_add_mod_inplace_dsl adjoint round-trip
# (W=2 exhaustive sweep).  Pins `invert<&lib_add_mod_inplace_dsl<BitProxy>>()`
# resolution alongside the round-trip behaviour: forward sets dest =
# (dest_old + a) mod n, adjoint restores dest to dest_old.
add_executable(test_add_mod_inplace_dsl_adjoint
    test_add_mod_inplace_dsl_adjoint.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_add_mod_inplace_dsl_adjoint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_add_mod_inplace_dsl_adjoint PRIVATE cxx_std_20)
target_compile_definitions(test_add_mod_inplace_dsl_adjoint PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_add_mod_inplace_dsl_adjoint PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_add_mod_inplace_dsl_adjoint PRIVATE orkan_headers)
add_test(NAME test_add_mod_inplace_dsl_adjoint COMMAND test_add_mod_inplace_dsl_adjoint)
set_tests_properties(test_add_mod_inplace_dsl_adjoint
    PROPERTIES LABELS "lib;modular-arith;lossy-reversibility")

# sturm-8lnp Beat A -- lib_add_mod_inplace_dsl peak-ancilla budget assertion.
# Achieved bound is constant 5 above the (3W+1) inputs (n_pad + lt_flag +
# carry_anc + inner adder transient + emit_CCX_lifted fold ancilla under
# the lift_under(lt_flag) body).  PRD `W + O(1)` requirement is met
# (independent of W); design note #2's `<= W + 5` target is satisfied
# with margin.  See test header for the structural breakdown.
add_executable(test_add_mod_inplace_dsl_ancilla
    test_add_mod_inplace_dsl_ancilla.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_add_mod_inplace_dsl_ancilla PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_add_mod_inplace_dsl_ancilla PRIVATE cxx_std_20)
target_compile_definitions(test_add_mod_inplace_dsl_ancilla PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_add_mod_inplace_dsl_ancilla PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_add_mod_inplace_dsl_ancilla PRIVATE orkan_headers)
add_test(NAME test_add_mod_inplace_dsl_ancilla COMMAND test_add_mod_inplace_dsl_ancilla)
set_tests_properties(test_add_mod_inplace_dsl_ancilla
    PROPERTIES LABELS "lib;modular-arith")

# sturm-8lnp Beat A -- lib_add_mod_inplace_dsl pool live-count round-trip /
# LIFO release.  Dedicated drain test mirroring sturm-yh3d.7
# (test_add_mod_dsl_pool_drain.cpp) and sturm-wdas's pool-drain test for
# the in-place add-mod algorithm.  W=2 sweep + LIFO-recycle witness.
add_executable(test_add_mod_inplace_dsl_pool_drain
    test_add_mod_inplace_dsl_pool_drain.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_add_mod_inplace_dsl_pool_drain PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_add_mod_inplace_dsl_pool_drain PRIVATE cxx_std_20)
target_compile_definitions(test_add_mod_inplace_dsl_pool_drain PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_add_mod_inplace_dsl_pool_drain PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_add_mod_inplace_dsl_pool_drain PRIVATE orkan_headers)
add_test(NAME test_add_mod_inplace_dsl_pool_drain COMMAND test_add_mod_inplace_dsl_pool_drain)
set_tests_properties(test_add_mod_inplace_dsl_pool_drain
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_mul_mod_dsl PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_mul_mod_dsl_adjoint PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_mul_mod_dsl_adjoint PRIVATE orkan_headers)
add_test(NAME test_mul_mod_dsl_adjoint COMMAND test_mul_mod_dsl_adjoint)
set_tests_properties(test_mul_mod_dsl_adjoint
    PROPERTIES LABELS "lib;modular-arith;lossy-reversibility"
               TIMEOUT 3600)

# sturm-kubb.6 P2.6 -- lib_mul_mod_dsl peak-ancilla budget assertion.
# Plan §4.3 row 2.6 wording: peak live ancillas <= 2W + 5.  The chain-style
# implementation in include/sturm/lib/mul_mod_dsl.hpp allocates 2·W² chain
# qubits + add_mod-interior ancillas, so the achieved peak is 2·W² + W + 6
# — far above the literal `2W + 5`.  Mirrors add-mod beat 1.6's situation
# (the literal plan bound was undercounted for the actual algorithm); see
# test header for breakdown and orchestrator brief for plan amendment
# follow-up.
add_executable(test_mul_mod_dsl_ancilla
    test_mul_mod_dsl_ancilla.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_mul_mod_dsl_ancilla PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_mod_dsl_ancilla PRIVATE cxx_std_20)
target_compile_definitions(test_mul_mod_dsl_ancilla PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_mul_mod_dsl_ancilla PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_mul_mod_dsl_ancilla PRIVATE orkan_headers)
add_test(NAME test_mul_mod_dsl_ancilla COMMAND test_mul_mod_dsl_ancilla)
set_tests_properties(test_mul_mod_dsl_ancilla
    PROPERTIES LABELS "lib;modular-arith")

# sturm-kubb.7 P2.7 -- lib_mul_mod_dsl pool live-count round-trip / LIFO
# release.  Dedicated drain test mirroring beat 1.7
# (test_add_mod_dsl_pool_drain.cpp) for the chain-style mul_mod algorithm.
# W=2 leg is the 14-case exhaustive sweep (matches beat 2.3); W=3 leg is
# the 50-case fixed-seed random sweep (matches beats 2.4 / 2.5).  Both legs
# use the APPEND-mode classical-trace harness (the orkan simulator is
# infeasible at W=3 per beat 2.4; trace mode is also faster than the
# simulator at W=2 -- ~ms per case vs. ~125 s).  No statevector simulation
# needed: QubitPool tracks allocate/release independent of executor, so
# the drain / LIFO assertions are valid against the IR-recording context.
add_executable(test_mul_mod_dsl_pool_drain
    test_mul_mod_dsl_pool_drain.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_mul_mod_dsl_pool_drain PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_mod_dsl_pool_drain PRIVATE cxx_std_20)
target_compile_definitions(test_mul_mod_dsl_pool_drain PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_mul_mod_dsl_pool_drain PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_mul_mod_dsl_pool_drain PRIVATE orkan_headers)
add_test(NAME test_mul_mod_dsl_pool_drain COMMAND test_mul_mod_dsl_pool_drain)
set_tests_properties(test_mul_mod_dsl_pool_drain
    PROPERTIES LABELS "lib;modular-arith")

# ── sturm-7cix Beat C -- lib_mul_mod_dsl O(W) "oneshot" path tests.  Built
#    on Beats A (lib_add_mod_inplace_dsl) + B (lib_double_mod_dsl); restricted
#    to ODD n.  These tests exercise the new O(W) ancilla path directly,
#    independent of the dispatcher in lib_mul_mod_dsl which routes based
#    on the classical-tracked LSB of `n_bits[0]`.
add_executable(test_mul_mod_dsl_oneshot
    test_mul_mod_dsl_oneshot.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_mul_mod_dsl_oneshot PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_mod_dsl_oneshot PRIVATE cxx_std_20)
target_compile_definitions(test_mul_mod_dsl_oneshot PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_mul_mod_dsl_oneshot PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_mul_mod_dsl_oneshot PRIVATE orkan_headers)
add_test(NAME test_mul_mod_dsl_oneshot COMMAND test_mul_mod_dsl_oneshot)
set_tests_properties(test_mul_mod_dsl_oneshot
    PROPERTIES LABELS "lib;modular-arith")

add_executable(test_mul_mod_dsl_oneshot_adjoint
    test_mul_mod_dsl_oneshot_adjoint.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_mul_mod_dsl_oneshot_adjoint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_mod_dsl_oneshot_adjoint PRIVATE cxx_std_20)
target_compile_definitions(test_mul_mod_dsl_oneshot_adjoint PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_mul_mod_dsl_oneshot_adjoint PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_mul_mod_dsl_oneshot_adjoint PRIVATE orkan_headers)
add_test(NAME test_mul_mod_dsl_oneshot_adjoint COMMAND test_mul_mod_dsl_oneshot_adjoint)
set_tests_properties(test_mul_mod_dsl_oneshot_adjoint
    PROPERTIES LABELS "lib;modular-arith;lossy-reversibility")

add_executable(test_mul_mod_dsl_oneshot_ancilla
    test_mul_mod_dsl_oneshot_ancilla.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_mul_mod_dsl_oneshot_ancilla PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_mod_dsl_oneshot_ancilla PRIVATE cxx_std_20)
target_compile_definitions(test_mul_mod_dsl_oneshot_ancilla PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_mul_mod_dsl_oneshot_ancilla PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_mul_mod_dsl_oneshot_ancilla PRIVATE orkan_headers)
add_test(NAME test_mul_mod_dsl_oneshot_ancilla COMMAND test_mul_mod_dsl_oneshot_ancilla)
set_tests_properties(test_mul_mod_dsl_oneshot_ancilla
    PROPERTIES LABELS "lib;modular-arith")

# sturm-3sfl.2 Beat D-B -- lib_square_mod_dsl forward + adjoint + ancilla.
# In-place modular squaring built on lib_mul_mod_dsl_oneshot (Beat C) plus
# the alias-allowed a==b multiplication and a swap+XOR-uncopy idiom.  Single
# combined test target covers: n==0 no-op, W=2 simulator witness, W=2
# exhaustive sweep including the (n+1)/2 case, W=3 random sweep including
# even n, x=0 / x=n-1 edge cases, adjoint round-trip, ancilla budget
# (~4W+7), pool live-count round-trip, and the x_copy_out_bits XOR-into
# threading round-trip (the squaring's non-injectivity witness, mirroring
# Beat B's lt_flag_out semantic).
add_executable(test_square_mod_dsl
    test_square_mod_dsl.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_square_mod_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_square_mod_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_square_mod_dsl PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_square_mod_dsl PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_square_mod_dsl PRIVATE orkan_headers)
add_test(NAME test_square_mod_dsl COMMAND test_square_mod_dsl)
set_tests_properties(test_square_mod_dsl
    PROPERTIES LABELS "lib;modular-arith;lossy-reversibility")

# sturm-3sfl.1 Beat D-A -- lib_mul_mod_inplace_dsl forward + adjoint +
# ancilla.  In-place modular multiplication built on
# lib_mul_mod_dsl_oneshot (Beat C) plus a swap+XOR-uncopy idiom and a
# caller-owned dest_copy_out_bits witness register (mirrors Beat D-B's
# x_copy_out_bits semantics — the witness is required because the
# forward map dest -> a*dest mod n is non-injective for non-coprime
# (a, n)).  Single combined test target covers: n==0 no-op, W=2
# simulator witness, W=2 exhaustive sweep over (a, dest, n) with
# a != dest (squaring-aliasing case forbidden — handled by
# lib_square_mod_dsl), W=3 random sweep including even n, adjoint
# round-trip, ancilla budget (~4W+7), pool live-count round-trip, and
# the dest_copy_out_bits XOR-into threading round-trip.
add_executable(test_mul_mod_inplace_dsl
    test_mul_mod_inplace_dsl.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_mul_mod_inplace_dsl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_mul_mod_inplace_dsl PRIVATE cxx_std_20)
target_compile_definitions(test_mul_mod_inplace_dsl PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_mul_mod_inplace_dsl PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_mul_mod_inplace_dsl PRIVATE orkan_headers)
add_test(NAME test_mul_mod_inplace_dsl COMMAND test_mul_mod_inplace_dsl)
set_tests_properties(test_mul_mod_inplace_dsl
    PROPERTIES LABELS "lib;modular-arith;lossy-reversibility")

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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_pow_mod_dsl PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_pow_mod_dsl PRIVATE orkan_headers)
add_test(NAME test_pow_mod_dsl COMMAND test_pow_mod_dsl)
set_tests_properties(test_pow_mod_dsl
    PROPERTIES LABELS "lib;modular-arith")

# sturm-a5te.6 P3.6 -- lib_pow_mod_dsl adjoint round-trip.
# Plan §5.3 row 3.6: forward + adjoint must zero r, preserve base/exp/n, and
# return the QubitPool live-count to its pre-call value.  Both legs (W=2
# exhaustive sweep, W=3 random sweep) use the APPEND-mode classical-trace
# harness because the simulator is infeasible at W=2+ for chain-style
# pow_mod (recursive chain mul_mod composition vastly exceeds orkan's
# 30-qubit ceiling).  Trace mode runs in milliseconds per case.
add_executable(test_pow_mod_dsl_adjoint
    test_pow_mod_dsl_adjoint.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_pow_mod_dsl_adjoint PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_pow_mod_dsl_adjoint PRIVATE cxx_std_20)
target_compile_definitions(test_pow_mod_dsl_adjoint PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_pow_mod_dsl_adjoint PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_pow_mod_dsl_adjoint PRIVATE orkan_headers)
add_test(NAME test_pow_mod_dsl_adjoint COMMAND test_pow_mod_dsl_adjoint)
set_tests_properties(test_pow_mod_dsl_adjoint
    PROPERTIES LABELS "lib;modular-arith;lossy-reversibility")

# sturm-a5te.7 P3.7 -- lib_pow_mod_dsl peak-ancilla budget assertion.
# Plan §5.3 row 3.7 wording: peak live ancillas <= c·W (linear).  The
# chain-style implementation in include/sturm/lib/pow_mod_dsl.hpp allocates
# W² (sq_chain) + (W+1)·W (acc_chain) + 2·W² + W + 7 (mul_mod-internal peak)
# ancillas, so the achieved peak is O(W²) — far above the literal `c·W`
# linear target.  Mirrors mul-mod beat 2.6's situation (the literal plan
# bound was undercounted for the actual algorithm); see test header for
# the structural breakdown and orchestrator brief for plan amendment
# follow-up (the `O(W)` budget would only follow once mul_mod switches
# to the deferred Karatsuba design — PRD §8 #1).
add_executable(test_pow_mod_dsl_ancilla
    test_pow_mod_dsl_ancilla.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_pow_mod_dsl_ancilla PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_pow_mod_dsl_ancilla PRIVATE cxx_std_20)
target_compile_definitions(test_pow_mod_dsl_ancilla PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_pow_mod_dsl_ancilla PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_pow_mod_dsl_ancilla PRIVATE orkan_headers)
add_test(NAME test_pow_mod_dsl_ancilla COMMAND test_pow_mod_dsl_ancilla)
set_tests_properties(test_pow_mod_dsl_ancilla
    PROPERTIES LABELS "lib;modular-arith")

# sturm-a5te.8 P3.8 -- lib_pow_mod_dsl pool live-count round-trip / LIFO
# release.  Dedicated drain test mirroring beats 1.7
# (test_add_mod_dsl_pool_drain.cpp) and 2.7 (test_mul_mod_dsl_pool_drain.cpp)
# for the chain-style pow_mod algorithm.  W=2 leg is the 24-case exhaustive
# sweep (matches beat 3.4); W=3 leg is the 50-case fixed-seed random sweep
# (matches beats 3.5 / 3.6).  Both legs use the APPEND-mode classical-trace
# harness -- the orkan simulator is infeasible for chain pow_mod at W=2+
# (peak ancilla 4·W² + 2W + 8 above 4·W inputs, exceeds the 30-qubit
# ceiling).  No statevector simulation needed: QubitPool tracks
# allocate/release independent of executor, so the drain / LIFO assertions
# are valid against the IR-recording context.
add_executable(test_pow_mod_dsl_pool_drain
    test_pow_mod_dsl_pool_drain.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_pow_mod_dsl_pool_drain PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_pow_mod_dsl_pool_drain PRIVATE cxx_std_20)
target_compile_definitions(test_pow_mod_dsl_pool_drain PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_pow_mod_dsl_pool_drain PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_pow_mod_dsl_pool_drain PRIVATE orkan_headers)
add_test(NAME test_pow_mod_dsl_pool_drain COMMAND test_pow_mod_dsl_pool_drain)
set_tests_properties(test_pow_mod_dsl_pool_drain
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
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_qint_modular PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_qint_modular PRIVATE orkan_headers)
add_test(NAME test_qint_modular COMMAND test_qint_modular)
set_tests_properties(test_qint_modular
    PROPERTIES LABELS "lib;modular-arith")

# sturm-bjt2.2 P6.2 -- lib_pow_dsl + lib_mod_dsl  vs  lib_pow_mod_dsl
# cross-validation oracle (plan §8.2 / PRD §6 #6).  20 random (a, x, n)
# with a < n at fixed seed std::mt19937(42); failure messages print the
# seed (plan §12 risk row).  Library targets do not propagate
# `-DSTURM_MODULAR_POW`, so this test compiles identically in both
# `STURM_MODULAR_POW=OFF` and `=ON` configures and discharges the plan
# §8.3 exit gate from the lib side (the transpiler-side two-build coverage
# is handled by tests/transpiler/cmake_modular_pow_flag.cmake).
add_executable(test_pow_mod_xval
    test_pow_mod_xval.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_pow_mod_xval PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_pow_mod_xval PRIVATE cxx_std_20)
target_compile_definitions(test_pow_mod_xval PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_compile_options(test_pow_mod_xval PRIVATE
    -USTURM_ANCILLA_CAPACITY -DSTURM_ANCILLA_CAPACITY=256
)
target_link_libraries(test_pow_mod_xval PRIVATE orkan_headers)
add_test(NAME test_pow_mod_xval COMMAND test_pow_mod_xval)
set_tests_properties(test_pow_mod_xval
    PROPERTIES LABELS "lib;modular-arith")

# sturm-8n73 -- high-W (W=40, 48, 56, 64) APPEND-mode trace tests for
# lib_double_mod_dsl, lib_add_mod_inplace_dsl, lib_mul_mod_dsl_oneshot.
# Validates the lifted kMaxN=64 cap end-to-end.  Compiled with
# STURM_ANCILLA_CAPACITY=1024 so the W=64 ancilla peak (~7W+7 = 455
# live qubits) fits with comfortable headroom.  Statevector simulation
# is infeasible past ~W=16 regardless of pool size, so this target is
# trace-mode only.
add_executable(test_modular_arith_highw_trace
    test_modular_arith_highw_trace.cpp
    ${STURM_LIB_BACKEND_SOURCES}
)
target_include_directories(test_modular_arith_highw_trace PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_modular_arith_highw_trace PRIVATE cxx_std_20)
# Project-level CMakeLists.txt does add_compile_definitions(
# STURM_ANCILLA_CAPACITY=${STURM_ANCILLA_CAPACITY}) which would otherwise
# override our target-level setting (last `-D` wins on the command line).
# Force-undefine first via -U, then redefine via -D, so this target gets
# 1024 regardless of what the project default is set to.
target_compile_options(test_modular_arith_highw_trace PRIVATE
    -USTURM_ANCILLA_CAPACITY
    -DSTURM_ANCILLA_CAPACITY=1024
)
target_compile_definitions(test_modular_arith_highw_trace PRIVATE
    STURM_BACKEND_ENABLED=1
)
target_link_libraries(test_modular_arith_highw_trace PRIVATE orkan_headers)
add_test(NAME test_modular_arith_highw_trace
         COMMAND test_modular_arith_highw_trace)
set_tests_properties(test_modular_arith_highw_trace
    PROPERTIES LABELS "lib;modular-arith")
