cmake_minimum_required(VERSION 3.16)

add_executable(comm_integration_can
    pcan_loopback_test.cpp
)

target_link_libraries(comm_integration_can PRIVATE GTest::gtest_main comm)

include(GoogleTest)
gtest_discover_tests(comm_integration_can)
