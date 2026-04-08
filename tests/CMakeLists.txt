cmake_minimum_required(VERSION 3.16)

find_package(Threads REQUIRED)

# Register each test executable in the tests/ directory.
# Add new test source files to this list as steps are implemented.
set(STURM_TEST_SOURCES
    test_smoke.cpp
    test_qubit_pool.cpp
    test_sink_counter.cpp
    test_sink_recording.cpp
    test_qbool.cpp
    test_mask_ops.cpp
    test_dispatch.cpp
    test_qint_classical.cpp
    test_qint_superposed.cpp
    test_resource_lifecycle.cpp
    test_when.cpp
    test_sink_dispatch.cpp
)

foreach(src ${STURM_TEST_SOURCES})
    get_filename_component(test_name ${src} NAME_WE)
    add_executable(${test_name} ${src})
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(${test_name} PRIVATE cxx_std_20)
    target_link_libraries(${test_name} PRIVATE Threads::Threads)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
