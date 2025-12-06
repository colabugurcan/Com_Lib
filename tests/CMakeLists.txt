cmake_minimum_required(VERSION 3.16)

if(NOT COMM_BUILD_TESTS)
    return()
endif()

include(FetchContent)

set(_gtest_already_present OFF)

find_package(GTest CONFIG QUIET)
if(GTest_FOUND)
    set(_gtest_already_present ON)
else()
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(comm_core_tests
    comm_core_tests.cpp
)

target_link_libraries(comm_core_tests PRIVATE GTest::gtest_main comm)

add_executable(comm_udp_tests
    comm_udp_tests.cpp
)

target_link_libraries(comm_udp_tests PRIVATE GTest::gtest_main comm)

add_executable(comm_serial_tests
    comm_serial_tests.cpp
)

target_link_libraries(comm_serial_tests PRIVATE GTest::gtest_main comm)

add_executable(comm_tcp_tests
    comm_tcp_tests.cpp
)

target_link_libraries(comm_tcp_tests PRIVATE GTest::gtest_main comm)

add_executable(comm_can_tests
    comm_can_tests.cpp
)

target_link_libraries(comm_can_tests PRIVATE GTest::gtest_main comm)

include(GoogleTest)
gtest_discover_tests(comm_core_tests)
gtest_discover_tests(comm_udp_tests)
gtest_discover_tests(comm_serial_tests)
gtest_discover_tests(comm_tcp_tests)
gtest_discover_tests(comm_can_tests)

if(COMM_ENABLE_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()
