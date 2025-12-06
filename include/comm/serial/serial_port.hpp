/**
 * @file serial_port.hpp
 * @brief DO-178C Level B Compliant Serial Port Interface
 * 
 * This module provides RS-232/RS-485 serial communication functionality
 * for flight-critical avionics systems using POSIX termios.
 * 
 * Features:
 * - Configurable baud rate, data bits, parity, stop bits
 * - Hardware and software flow control
 * - Thread-safe operation
 * - Automatic resource cleanup
 * 
 * @note All functions are designed to be deterministic and exception-free.
 * @note Thread-safe for concurrent send/receive operations.
 * 
 * @requirement SRS-SER-001: Serial port management
 * @requirement SRS-SER-002: Terminal configuration
 * @requirement SRS-SER-003: Flow control support
 * 
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <comm/core/interface.hpp>
#include <comm/core/defaults.hpp>

namespace comm::serial {

enum class DataBits {
    Five = 5,
    Six = 6,
    Seven = 7,
    Eight = 8
};

enum class Parity {
    None,
    Even,
    Odd,
    Mark,
    Space
};

enum class StopBits {
    One,
    OnePointFive,
    Two
};

enum class FlowControl {
    None,
    Hardware,
    Software
};

struct SerialConfig : Config {
    std::string device{"/dev/ttyUSB0"};
    int baudRate{defaults::kSerialBaudRate};
    DataBits dataBits{DataBits::Eight};
    Parity parity{Parity::None};
    StopBits stopBits{StopBits::One};
    FlowControl flowControl{FlowControl::None};
    std::chrono::milliseconds reconnectInterval{defaults::kSerialReconnectInterval};
    std::chrono::milliseconds readTimeout{defaults::kSerialReadTimeout};
    std::size_t rxBufferSize{defaults::kSerialRxBufferSize};
    std::size_t txBufferSize{defaults::kSerialTxBufferSize};

    SerialConfig() {
        protocol = Protocol::Serial;
        direction = Direction::TwoWay;
    }
};

/// POSIX serial transport implementation.
class SerialPort : public ICommunication {
public:
    explicit SerialPort(SerialConfig config = {});
    ~SerialPort() noexcept override;

    bool open() override;
    bool close() override;
    [[nodiscard]] bool isOpen() const override;

    std::ptrdiff_t send(const ByteVector& data) override;
    std::ptrdiff_t receive(ByteVector& buffer, std::size_t maxSize) override;

    bool configure(const Config& config) override;
    [[nodiscard]] Config getConfig() const override;

    [[nodiscard]] Statistics getStatistics() const override;
    [[nodiscard]] bool isHealthy() const override;

    void setReceiveCallback(ReceiveCallback callback) override;
    void setErrorCallback(ErrorCallback callback) override;

private:
    bool applySettings();
    void recordSend(std::size_t bytes);
    void recordReceive(std::size_t bytes);
    void reportError(Error error);
    void startReceiveLoop();
    void stopReceiveLoop();
    void receiveLoop();
    bool shouldSpawnReceiveThread() const;
    std::chrono::milliseconds receiveSleepDuration() const;

    SerialConfig config_{};

    mutable std::mutex mutex_{};
    mutable std::mutex statsMutex_{};
    Statistics stats_{};

    ReceiveCallbackRegistry receiveCallback_{};
    ErrorCallbackRegistry errorCallback_{};

    int fileDescriptor_{-1};
    std::atomic<bool> healthy_{true};
    std::atomic<bool> running_{false};
    std::thread receiveThread_{};
};

} // namespace comm::serial
