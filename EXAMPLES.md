# Usage Examples

## UDP Unicast Sender

```cpp
#include <comm/ethernet/udp.hpp>

int main() {
    comm::ethernet::UDPConfig config;
    config.mode = comm::Mode::Unicast;
    config.direction = comm::Direction::SendOnly;
    config.remote.address = "192.168.1.200";
    config.remote.port = 5000;

    comm::ethernet::UDP udp{config};
    udp.setErrorCallback([](const comm::Error& error) {
        std::cerr << "UDP error: " << error.message << "\n";
    });

    if (!udp.open()) {
        return 1;
    }

    comm::ByteVector payload{0x01, 0x02, 0x03};
    udp.send(payload);

    return 0;
}
```

## Serial Port With Receive Callback

```cpp
#include <comm/serial/serial_port.hpp>
#include <chrono>
#include <iostream>

int main() {
    comm::serial::SerialConfig config;
    config.device = "/dev/ttyUSB0";
    config.baudRate = 115200;
    config.readTimeout = std::chrono::milliseconds{200};

    comm::serial::SerialPort serial{config};

    serial.setReceiveCallback([](const comm::ByteVector& data) {
        std::cout << "Received " << data.size() << " bytes\n";
    });

    serial.setErrorCallback([](const comm::Error& error) {
        std::cerr << "Serial error: " << error.message << "\n";
    });

    if (!serial.open()) {
        return 1;
    }

    comm::ByteVector hello{'H', 'i', '\n'};
    serial.send(hello);

    return 0;
}
```

## PCAN-Basic Loopback

```cpp
#include <comm/can/pcan_basic.hpp>
#include <iostream>
#include <cstdint>

int main() {
    comm::can::PCANBasicConfig config;
    config.handle = static_cast<std::uint16_t>(comm::can::PCANChannel::Usb1);

    comm::can::PCANBasic can{config};
    if (!can.open()) {
        return 1;
    }

    comm::ByteVector payload{0x12, 0x34};
    can.send(payload);

    comm::ByteVector rx;
    can.receive(rx, 8);

    std::cout << "Received " << rx.size() << " bytes\n";
    return 0;
}
```

## TCP Echo Client

```cpp
#include <comm/ethernet/tcp.hpp>
#include <iostream>
#include <string>

int main() {
    comm::ethernet::TCPConfig config;
    config.remote.address = "127.0.0.1";
    config.remote.port = 12345;

    comm::ethernet::TCP tcp{config};
    if (!tcp.open()) {
        return 1;
    }

    comm::ByteVector payload{'p', 'i', 'n', 'g'};
    tcp.send(payload);

    comm::ByteVector rx;
    tcp.receive(rx, 64);

    std::cout << "Received " << rx.size() << " bytes\n";
    tcp.close();
    return 0;
}
```
