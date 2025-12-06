/**
 * @file defaults.hpp
 * @brief DO-178C Level B Compliant Default Values and Constants
 * 
 * This module centralizes all default configuration values for the
 * communication library. All magic numbers are defined here with
 * clear documentation and rationale.
 * 
 * Benefits:
 * - Single source of truth for all defaults
 * - Easy to audit and change
 * - Full traceability for certification
 * - Compile-time constants (no runtime overhead)
 * 
 * @note All values are constexpr for compile-time evaluation.
 * @note Values are chosen based on industry best practices and testing.
 * 
 * @requirement SRS-CFG-001: Centralized default management
 * @requirement SRS-CFG-002: Compile-time constant definitions
 * 
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace comm::defaults {

// ============================================================================
// TIMING DEFAULTS - Timeout and Interval Values
// ============================================================================

/**
 * @defgroup TimingDefaults Timing Default Values
 * @brief Default timeout and interval values for communication operations.
 * 
 * These values are chosen based on:
 * - Typical network latencies (LAN: <1ms, WAN: 10-100ms)
 * - Serial communication characteristics
 * - CAN bus timing requirements
 * - Human response expectations for UI operations
 * @{
 */

/// Default timeout for open() operation (2 seconds).
/// @rationale Allows for network stack initialization and hardware setup.
inline constexpr std::chrono::milliseconds kOpenTimeout{2000};

/// Default timeout for close() operation (2 seconds).
/// @rationale Allows graceful shutdown and resource cleanup.
inline constexpr std::chrono::milliseconds kCloseTimeout{2000};

/// Default timeout for send() operation (1 second).
/// @rationale Sufficient for most network conditions while detecting failures.
inline constexpr std::chrono::milliseconds kSendTimeout{1000};

/// Default timeout for receive() operation (1 second).
/// @rationale Balance between responsiveness and blocking time.
inline constexpr std::chrono::milliseconds kReceiveTimeout{1000};

/// Default reconnection interval (5 seconds).
/// @rationale Prevents aggressive reconnection attempts while maintaining responsiveness.
inline constexpr std::chrono::milliseconds kReconnectInterval{5000};

/// Default base delay for exponential backoff (100 milliseconds).
/// @rationale Starting point for retry logic, doubles each attempt.
inline constexpr std::chrono::milliseconds kRetryBaseDelay{100};

/// Maximum delay for exponential backoff (5 seconds).
/// @rationale Upper bound to prevent excessively long waits.
inline constexpr std::chrono::milliseconds kRetryMaxDelay{5000};

/// Default sleep duration for receive threads (5 milliseconds).
/// @rationale Low latency polling without excessive CPU usage.
inline constexpr std::chrono::milliseconds kReceiveThreadSleep{5};

/// Default health check interval (100 milliseconds).
/// @rationale Frequent enough to detect failures, infrequent enough to minimize overhead.
inline constexpr std::chrono::milliseconds kHealthCheckInterval{100};

/// Default watchdog timeout (1 second).
/// @rationale Standard heartbeat interval for safety monitoring.
inline constexpr std::chrono::milliseconds kWatchdogTimeout{1000};

/** @} */ // end of TimingDefaults

// ============================================================================
// SERIAL PORT DEFAULTS
// ============================================================================

/**
 * @defgroup SerialDefaults Serial Port Default Values
 * @brief Default configuration values for RS-232/RS-485 communication.
 * @{
 */

/// Default serial baud rate (115200 bps).
/// @rationale Industry standard for embedded systems, good balance of speed and reliability.
inline constexpr int kSerialBaudRate{115200};

/// Default serial reconnect interval (2 seconds).
/// @rationale Allows device reset/reconnection time.
inline constexpr std::chrono::milliseconds kSerialReconnectInterval{2000};

/// Default serial read timeout (500 milliseconds).
/// @rationale Responsive for interactive use while allowing buffered reads.
inline constexpr std::chrono::milliseconds kSerialReadTimeout{500};

/// Default serial RX buffer size (4096 bytes).
/// @rationale Standard buffer size, sufficient for most protocols.
inline constexpr std::size_t kSerialRxBufferSize{4096};

/// Default serial TX buffer size (4096 bytes).
/// @rationale Matches RX buffer for symmetric operation.
inline constexpr std::size_t kSerialTxBufferSize{4096};

/** @} */ // end of SerialDefaults

// ============================================================================
// NETWORK DEFAULTS (UDP/TCP)
// ============================================================================

/**
 * @defgroup NetworkDefaults Network Default Values
 * @brief Default configuration values for UDP and TCP communication.
 * @{
 */

/// Default network port (0 = OS-assigned ephemeral port).
/// @rationale Let OS choose available port for client applications.
inline constexpr std::uint16_t kDefaultPort{0};

/// Default multicast TTL (1 hop).
/// @rationale Limit multicast to local network segment by default.
inline constexpr int kMulticastTTL{1};

/// Default TCP listen backlog (5 connections).
/// @rationale POSIX standard, sufficient for most embedded use cases.
inline constexpr int kTcpListenBacklog{5};

/// Maximum UDP datagram size (65536 bytes).
/// @rationale Maximum theoretical UDP payload size.
inline constexpr std::size_t kMaxUdpDatagramSize{65536};

/// Default TCP buffer size (4096 bytes).
/// @rationale Standard buffer size for TCP receive operations.
inline constexpr std::size_t kDefaultTcpBufferSize{4096};

/** @} */ // end of NetworkDefaults

// ============================================================================
// CAN BUS DEFAULTS
// ============================================================================

/**
 * @defgroup CANDefaults CAN Bus Default Values
 * @brief Default configuration values for CAN communication.
 * @{
 */

/// Default CAN bitrate (500 kbps).
/// @rationale Common automotive/avionics CAN speed.
inline constexpr std::uint32_t kCanBitrateKbps{500};

/// PCAN-Basic 500 kbps baud rate code.
/// @rationale PCAN_BAUD_500K from PCANBasic.h
inline constexpr std::uint32_t kPcanBaud500K{0x001C};

/// Maximum CAN 2.0 frame data length (8 bytes).
/// @rationale CAN 2.0 standard maximum payload.
inline constexpr std::size_t kCanMaxDataLength{8};

/// Maximum CAN FD frame data length (64 bytes).
/// @rationale CAN FD standard maximum payload.
inline constexpr std::size_t kCanFdMaxDataLength{64};

/// CAN receive timeout for polling (10 milliseconds).
/// @rationale Low latency CAN message reception.
inline constexpr std::chrono::milliseconds kCanReceiveTimeout{10};

/** @} */ // end of CANDefaults

// ============================================================================
// RETRY POLICY DEFAULTS
// ============================================================================

/**
 * @defgroup RetryDefaults Retry Policy Default Values
 * @brief Default configuration values for retry and backoff logic.
 * @{
 */

/// Default maximum retry attempts (0 = disabled).
/// @rationale Retry disabled by default; must be explicitly enabled.
inline constexpr std::size_t kMaxRetryAttempts{0};

/// Default backoff multiplier (2.0x exponential).
/// @rationale Standard exponential backoff factor.
inline constexpr double kBackoffMultiplier{2.0};

/** @} */ // end of RetryDefaults

// ============================================================================
// BUFFER SIZE DEFAULTS
// ============================================================================

/**
 * @defgroup BufferDefaults Buffer Size Default Values
 * @brief Default sizes for various internal buffers.
 * @{
 */

/// Default receive buffer size (8192 bytes).
/// @rationale General purpose buffer, two pages of memory.
inline constexpr std::size_t kDefaultReceiveBufferSize{8192};

/// Default send buffer size (8192 bytes).
/// @rationale Symmetric with receive buffer.
inline constexpr std::size_t kDefaultSendBufferSize{8192};

/// Maximum message queue depth (1024 messages).
/// @rationale Reasonable queue depth for asynchronous operations.
inline constexpr std::size_t kMaxMessageQueueDepth{1024};

/** @} */ // end of BufferDefaults

// ============================================================================
// SAFETY DEFAULTS
// ============================================================================

/**
 * @defgroup SafetyDefaults Safety Mechanism Default Values
 * @brief Default configuration values for safety features.
 * @{
 */

/// Maximum consecutive errors before failsafe transition (5).
/// @rationale Allows transient errors while catching persistent issues.
inline constexpr std::uint32_t kMaxConsecutiveErrors{5};

/// Sequence number wrap threshold (0xFFFFFF00).
/// @rationale Leave room for wrap-around detection.
inline constexpr std::uint32_t kSequenceWrapThreshold{0xFFFFFF00};

/// Maximum allowed sequence gap (100 messages).
/// @rationale Detect significant message loss.
inline constexpr std::uint32_t kMaxSequenceGap{100};

/** @} */ // end of SafetyDefaults

// ============================================================================
// DO-178C LOOP BOUND DEFAULTS
// ============================================================================

/**
 * @defgroup LoopBoundDefaults DO-178C Loop Bound Values
 * @brief Maximum iteration limits for bounded loops (DO-178C compliance).
 * 
 * These values ensure all loops terminate within predictable time bounds.
 * @{
 */

/// Maximum iterations for receive loops (10000).
/// @rationale Prevents infinite loops in receive processing.
inline constexpr std::size_t kMaxReceiveLoopIterations{10000};

/// Maximum iterations for health check loops (1000000).
/// @rationale Allows long-running health monitors with termination guarantee.
inline constexpr std::size_t kMaxHealthCheckIterations{1000000};

/// Maximum connection/recovery attempts (3).
/// @rationale Prevents infinite reconnection storms.
inline constexpr std::size_t kMaxConnectionAttempts{3};

/// Maximum CAN filters per interface (64).
/// @rationale Hardware limitation on most CAN controllers.
inline constexpr std::size_t kMaxCanFilters{64};

/** @} */ // end of LoopBoundDefaults

} // namespace comm::defaults
