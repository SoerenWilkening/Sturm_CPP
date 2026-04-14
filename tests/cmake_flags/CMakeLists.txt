# tests/cmake_flags/CMakeLists.txt — ctest wiring for the M1 flag contract.
#
# Declares one test, `cmake_flags_verify`, that invokes verify_flags.cmake
# in script mode. The script performs three fresh out-of-source configures
# of the STURM source tree and checks the PRD/issue acceptance criteria for
# the STURM_AUTO_UNCOMPUTE and STURM_TRANSPILE options.

add_test(
    NAME cmake_flags_verify
    COMMAND "${CMAKE_COMMAND}"
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DBUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}/_flag_probes
            -DCMAKE_BIN=${CMAKE_COMMAND}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_flags.cmake)

# Allow up to two minutes — three fresh configures on a cold-cache host
# (especially when -DSTURM_TRANSPILE=ON triggers find_package(LLVM)) can
# take a noticeable amount of wall time.
set_tests_properties(cmake_flags_verify PROPERTIES TIMEOUT 120)
