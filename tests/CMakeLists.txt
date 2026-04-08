cmake_minimum_required(VERSION 3.16)

# Register each test executable in the tests/ directory.
# Add new test source files to this list as steps are implemented.
set(STURM_TEST_SOURCES
    test_smoke.cpp
)

foreach(src ${STURM_TEST_SOURCES})
    get_filename_component(test_name ${src} NAME_WE)
    add_executable(${test_name} ${src})
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(${test_name} PRIVATE cxx_std_20)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
