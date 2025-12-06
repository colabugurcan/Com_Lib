/**
 * @file socketcan.hpp
 * @brief DO-178C Level B Compliant SocketCAN Interface
 * 
 * This module provides Linux SocketCAN communication functionality
 * for flight-critical avionics systems.
 * 
 * Features:
 * - CAN 2.0A/B frame support
 * - CAN FD support (optional)
 * - Hardware filtering
 * - Error frame handling
 * - Loopback and receive-own-messages configuration
 * 
 * @note All functions are designed to be deterministic and exception-free.
 * @note Thread-safe for concurrent send/receive operations.
 * @note Linux-only implementation using SocketCAN kernel API.
 * 
 * @requirement SRS-CAN-001: SocketCAN management
 * @requirement SRS-CAN-002: CAN frame filtering
 * @requirement SRS-CAN-003: CAN FD support
 * 
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <comm/core/interface.hpp>
#include <comm/core/defaults.hpp>

namespace comm::can {

struct CANFilter {
    std::uint32_t id{0};
    std::uint32_t mask{0};
};

struct SocketCANConfig : Config {
    std::string interface{"can0"};
    std::uint32_t bitrateKbps{defaults::kCanBitrateKbps};
    bool enableFD{false};
    bool loopback{false};
    bool receiveOwnMessages{false};
    bool autoRecover{true};
    std::vector<CANFilter> filters{};

    SocketCANConfig() {
        protocol = Protocol::CAN;
        direction = Direction::TwoWay;
        autoReconnect = true;
    }
};

struct CANFrame {
    std::uint32_t id{0};
    bool extended{false};
    bool fdFrame{false};
    bool errorFrame{false};
    std::uint8_t dlc{0};
    ByteVector data{};
};

/// SocketCAN transport implementing ICommunication.
class SocketCAN : public ICommunication {
public:
    explicit SocketCAN(SocketCANConfig config = {});
    ~SocketCAN() noexcept override;

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

    [[nodiscard]] std::optional<CANFrame> readFrame();
    bool writeFrame(const CANFrame& frame);

private:
    void startReceiveLoop();
    void stopReceiveLoop();
    void receiveLoop();

    bool applyFilters();
    void reportError(Error error);
    void recordSend(std::size_t bytes);
    void recordReceive(std::size_t bytes);

    SocketCANConfig config_{};

    mutable std::mutex mutex_{};
    mutable std::mutex statsMutex_{};
    Statistics stats_{};

    ReceiveCallbackRegistry receiveCallback_{};
    ErrorCallbackRegistry errorCallback_{};

    int socket_{-1};
    std::atomic<bool> healthy_{true};
    std::atomic<bool> running_{false};
    std::thread receiveThread_{};
};

} // namespace comm::can
