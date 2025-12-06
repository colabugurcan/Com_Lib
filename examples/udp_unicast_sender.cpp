#include <comm/ethernet/udp.hpp>

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    comm::ethernet::UDPConfig config;
    config.mode = comm::Mode::Unicast;
    config.direction = comm::Direction::SendOnly;
    config.remote.address = "192.168.1.200";
    config.remote.port = 5000;
    config.timeouts.sendTimeout = std::chrono::milliseconds{200};

    comm::ethernet::UDP udp{config};
    udp.setErrorCallback([](const comm::Error& error) {
        std::cerr << "UDP error: " << error.message << " (" << comm::toString(error.code) << ")\n";
    });

    if (!udp.open()) {
        std::cerr << "Failed to open UDP socket\n";
        return 1;
    }

    comm::ByteVector payload{0x01, 0x02, 0x03};
    if (udp.send(payload) < 0) {
        std::cerr << "Failed to send payload\n";
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    return 0;
}
