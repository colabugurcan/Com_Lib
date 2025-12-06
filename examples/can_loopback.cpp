#include <comm/can/pcan_basic.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>

int main() {
#ifndef COMM_HAS_PCAN
    std::cerr << "PCANBasic support is not enabled at build time." << std::endl;
    return 0;
#else
    comm::can::PCANBasicConfig config;
    config.handle = static_cast<std::uint16_t>(comm::can::PCANChannel::Usb1);
    config.listenOnly = false;

    comm::can::PCANBasic can{config};
    can.setErrorCallback([](const comm::Error& error) {
        std::cerr << "CAN error: " << error.message << std::endl;
    });

    if (!can.open()) {
        std::cerr << "Failed to open CAN interface" << std::endl;
        return 1;
    }

    comm::ByteVector payload{0x12, 0x34};
    can.send(payload);

    comm::ByteVector rx;
    can.receive(rx, 8);

    std::cout << "Received " << rx.size() << " bytes" << std::endl;
    can.close();
    return 0;
#endif
}
