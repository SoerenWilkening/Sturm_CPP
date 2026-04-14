# tests/transpiler/CMakeLists.txt — parent for the in-tree transpiler tests
# that live under tests/ (as opposed to transpiler/tests/, which holds the
# transpiler binary's unit tests).
#
# M10 lands the first consumer under test_cmake_glue/. Future Group C
# fixtures (M11 snapshot, M12 gate-stream equivalence) will add their own
# subdirectories here.

cmake_minimum_required(VERSION 3.16)

add_subdirectory(test_cmake_glue)
