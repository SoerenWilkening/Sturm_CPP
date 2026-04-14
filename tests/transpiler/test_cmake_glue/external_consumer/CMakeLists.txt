# tests/transpiler/test_cmake_glue/external_consumer/CMakeLists.txt
#
# Stand-alone CMake project that consumes cmake/SturmTranspile.cmake without
# being part of the STURM build. Configured and built by the probe script
# verify_external_consumer.cmake in script-mode.
#
# The contract being validated:
#   1. include() of SturmTranspile.cmake from outside the STURM source tree
#      does NOT hard-fail (the helper must be self-contained or degrade
#      gracefully).
#   2. add_quantum_executable() is usable in a context where STURM_TRANSPILE
#      is OFF (the default) AND no `sturm` library target exists.
#   3. The configure completes, an executable target is created, and the
#      build produces a runnable binary that exits 0.
#
# The sturm INTERFACE target is faked out here so target_link_libraries()
# in the helper succeeds. In a real consumer, `sturm` would be brought in
# via find_package(sturm) (post-M10 install wiring) or a vendored copy of
# the include/ tree. Faking the target keeps this probe independent of
# future install work while still exercising the helper end-to-end.

cmake_minimum_required(VERSION 3.16)
project(ExternalSturmConsumer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Stand-in for the upstream `sturm` interface library. We cannot depend
# on the real STURM build being installed — this probe runs from a
# pristine out-of-source build. The INTERFACE library attaches the
# STURM include path if one was provided (STURM_INCLUDE_DIR); otherwise
# the consumer code must not need any sturm header. Our fixture is a
# plain int-main so neither include path nor link dependency is
# functionally required.
add_library(sturm INTERFACE)
if(DEFINED STURM_INCLUDE_DIR AND EXISTS "${STURM_INCLUDE_DIR}")
    target_include_directories(sturm INTERFACE "${STURM_INCLUDE_DIR}")
endif()

# STURM_TRANSPILE is OFF in the external consumer. This mirrors the PRD's
# "STURM_TRANSPILE=OFF (default) must still allow STURM consumers to link/
# build" clause. If the helper misbehaves under OFF (e.g. references a
# non-existent sturm-transpile target unconditionally) the configure or
# build would fail here.
if(NOT DEFINED STURM_TRANSPILE)
    set(STURM_TRANSPILE OFF CACHE BOOL "")
endif()

# Consume the helper exactly as a downstream project would.
include("${STURM_HELPER_PATH}")

# The fixture is a pinned one-liner so the probe does not depend on the
# in-tree src/fixture.cpp file layout.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ext_fixture.cpp"
     "int main() { return 0; }\n")

add_quantum_executable(external_fixture
    "${CMAKE_CURRENT_BINARY_DIR}/ext_fixture.cpp")
