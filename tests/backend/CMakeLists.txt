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
