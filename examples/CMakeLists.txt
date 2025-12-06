cmake_minimum_required(VERSION 3.16)

add_executable(udp_unicast_sender udp_unicast_sender.cpp)
target_link_libraries(udp_unicast_sender PRIVATE comm)

add_executable(serial_basic serial_basic.cpp)
target_link_libraries(serial_basic PRIVATE comm)

add_executable(can_loopback can_loopback.cpp)
target_link_libraries(can_loopback PRIVATE comm)

add_executable(tcp_echo_client tcp_echo_client.cpp)
target_link_libraries(tcp_echo_client PRIVATE comm)
