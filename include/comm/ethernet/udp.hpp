/**
 * @file udp.hpp
 * @brief DO-178C Level B Compliant UDP Transport Interface
 * 
 * This module provides UDP communication functionality for flight-critical
 * avionics systems using POSIX sockets.
 * 
 * Features:
 * - Unicast, broadcast, and multicast support
 * - Configurable endpoints and socket options
 * - Thread-safe operation
 * - Automatic resource cleanup
 * 
 * @note All functions are designed to be deterministic and exception-free.
 * @note Thread-safe for concurrent send/receive operations.
 * 
 * @requirement SRS-UDP-001: UDP socket management
 * @requirement SRS-UDP-002: Multicast support
 * @requirement SRS-UDP-003: Broadcast support
 * 
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <comm/core/interface.hpp>
#include <comm/core/defaults.hpp>
#include <comm/ethernet/endpoint.hpp>

namespace comm::ethernet {

struct UDPConfig : Config {
    Endpoint local{};
    Endpoint remote{};
    bool allowBroadcast{false};
    bool joinMulticast{false};
    std::string multicastGroup{};
    int multicastTTL{defaults::kMulticastTTL};
    bool multicastLoopback{false};
    bool autoRebind{true};

    UDPConfig() { protocol = Protocol::UDP; }
};

/// POSIX-oriented UDP transport implementing the generic communication interface.
class UDP : public ICommunication {
public:
    explicit UDP(UDPConfig config = {});
    ~UDP() noexcept override;

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
    struct SocketHandle;

    bool bindSocket();
    bool configureSocket();
    bool joinMulticastGroup();
    void attemptRebind();
    void startReceiveLoop();
    void stopReceiveLoop();
    void receiveLoop();

    void recordSend(std::size_t bytes);
    void recordReceive(std::size_t bytes);
    void reportError(Error error);
    bool shouldSpawnReceiveThread() const;
    std::chrono::milliseconds receiveSleepDuration() const;

    void scheduleHealthCheck();
    void healthMonitorLoop();
    void stopHealthMonitor();
    void touchActivity();
    std::chrono::steady_clock::duration timeSinceLastActivity() const;

    UDPConfig config_{};
    mutable std::mutex mutex_{};
    mutable std::mutex statsMutex_{};
    mutable std::mutex activityMutex_{};
    Statistics stats_{};

    ReceiveCallbackRegistry receiveCallback_{};
    ErrorCallbackRegistry errorCallback_{};

    std::atomic<bool> running_{false};
    std::thread receiveThread_{};
    std::unique_ptr<SocketHandle> handle_{};
    std::atomic<bool> healthy_{true};
    std::atomic<bool> healthMonitorRunning_{false};
    std::thread healthThread_{};
    std::chrono::steady_clock::time_point lastActivity_{std::chrono::steady_clock::now()};
};

} // namespace comm::ethernet
