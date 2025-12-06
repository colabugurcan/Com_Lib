cmake_minimum_required(VERSION 3.16)

set(_example_sources
    udp_unicast_sender.cpp
    serial_basic.cpp
    can_loopback.cpp
    tcp_echo_client.cpp
)

if(_example_sources)
    add_executable(comm_examples ${_example_sources})
    target_link_libraries(comm_examples PRIVATE comm)
endif()
