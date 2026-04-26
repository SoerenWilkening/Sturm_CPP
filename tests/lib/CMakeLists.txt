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

# sturm-r5pn P2 -- lib_mul_mod_dsl scaffold (will be filled by Phase 2 beats).
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
    PROPERTIES LABELS "lib;modular-arith")

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
