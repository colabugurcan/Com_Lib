#include <comm/serial/serial_port.hpp>

#include <chrono>
#include <iostream>

int main() {
    comm::serial::SerialConfig config;
    config.device = "/dev/ttyUSB0";
    config.baudRate = 115200;
    config.readTimeout = std::chrono::milliseconds{200};

    comm::serial::SerialPort port{config};

    port.setReceiveCallback([](const comm::ByteVector& data) {
        std::cout << "Rx " << data.size() << " bytes" << std::endl;
    });

    port.setErrorCallback([](const comm::Error& error) {
        std::cerr << "Serial error: " << error.message << std::endl;
    });

    if (!port.open()) {
        std::cerr << "Failed to open serial port" << std::endl;
        return 1;
    }

    comm::ByteVector payload{'O', 'K', '\n'};
    port.send(payload);

    return 0;
}
