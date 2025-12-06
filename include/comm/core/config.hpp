/**
 * @file config.hpp
 * @brief DO-178C Compliant Configuration Structures
 * 
 * This header defines configuration structures for all transport types.
 * All structures are designed for safe initialization and validation.
 * 
 * @requirement SRS-COMM-CFG-001: Transport configuration management
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "defaults.hpp"

namespace comm {

/// Supported transport protocols.
enum class Protocol {
    Unknown,
    UDP,
    TCP,
    Serial,
    CAN
};

/// Addressing mode for the communication endpoint.
enum class Mode {
    Unspecified,
    Unicast,
    Broadcast,
    Multicast
};

/// Direction of data flow for the communication channel.
enum class Direction {
    Unspecified,
    SendOnly,
    ReceiveOnly,
    TwoWay
};

/// Role of the endpoint in the connection lifecycle.
enum class Role {
    Undefined,
    Client,
    Server
};

/// Reliability guarantees expected from the transport.
enum class TransportReliability {
    Unspecified,
    Reliable,
    BestEffort
};

/// Logging verbosity levels.
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off
};

/// Retry policy describing exponential backoff behaviour.
struct RetryPolicy {
    bool enabled{false};
    std::size_t maxAttempts{defaults::kMaxRetryAttempts};
    std::chrono::milliseconds baseDelay{defaults::kRetryBaseDelay};
    std::chrono::milliseconds maxDelay{defaults::kRetryMaxDelay};
    double backoffMultiplier{defaults::kBackoffMultiplier};
};

/// Collection of operation timeouts.
struct TimeoutSettings {
    std::chrono::milliseconds openTimeout{defaults::kOpenTimeout};
    std::chrono::milliseconds closeTimeout{defaults::kCloseTimeout};
    std::chrono::milliseconds sendTimeout{defaults::kSendTimeout};
    std::chrono::milliseconds receiveTimeout{defaults::kReceiveTimeout};
    std::chrono::milliseconds reconnectInterval{defaults::kReconnectInterval};
};

/// Measured statistics captured by a transport implementation.
struct Statistics {
    std::uint64_t bytesSent{0};
    std::uint64_t bytesReceived{0};
    std::uint64_t messagesSent{0};
    std::uint64_t messagesReceived{0};
    std::uint64_t errorCount{0};
    std::chrono::steady_clock::time_point startTime{std::chrono::steady_clock::now()};
};

/// Health states used for periodic monitoring.
enum class HealthState {
    Unknown,
    Healthy,
    Degraded,
    Faulted
};

/// Result of a health probe.
struct HealthStatus {
    HealthState state{HealthState::Unknown};
    std::string message{};
};

/// Logging callback signature.
using LogHandler = std::function<void(LogLevel, std::string_view)>;

/// Logging configuration for transports.
struct LogConfig {
    LogLevel level{LogLevel::Info};
    LogHandler handler{};
};

/// Base configuration shared across all communication transports.
struct Config {
    Protocol protocol{Protocol::Unknown};
    Mode mode{Mode::Unspecified};
    Direction direction{Direction::Unspecified};
    Role role{Role::Undefined};
    TransportReliability reliability{TransportReliability::Unspecified};
    bool nonBlocking{false};
    bool autoReconnect{false};
    bool spawnReceiveThread{false};
    std::chrono::milliseconds receiveThreadSleep{defaults::kReceiveThreadSleep};
    TimeoutSettings timeouts{};
    RetryPolicy retryPolicy{};
    LogConfig logging{};
};

/// Convenience alias for byte buffers.
using ByteVector = std::vector<std::uint8_t>;

} // namespace comm
