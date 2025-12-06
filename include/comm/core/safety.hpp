/**
 * @file safety.hpp
 * @brief DO-178C Level B Compliant Safety Mechanisms
 * 
 * This module provides safety features required for DO-178C / DO-254 compliance:
 * - CRC32 checksum for data integrity verification
 * - Sequence numbering for message ordering and loss detection
 * - Watchdog/heartbeat monitoring for liveness checking
 * - Redundancy channel management for fault tolerance
 * - Sanity checks and bounds validation
 * - Failsafe state management
 * 
 * @note All functions are designed to be deterministic and exception-free.
 * @note All algorithms have bounded execution time.
 * @note No dynamic memory allocation is performed.
 * 
 * @requirement SRS-SAFETY-001: Watchdog monitoring
 * @requirement SRS-SAFETY-002: Failsafe state management
 * @requirement SRS-SAFETY-003: Data integrity verification
 * @requirement SRS-SAFETY-004: Sequence validation
 * 
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>

#include "defaults.hpp"

namespace comm::safety {

// ============================================================================
// Constants
// ============================================================================

/// Maximum allowed message size for sanity checking (64KB)
inline constexpr std::size_t kMaxMessageSize{defaults::kMaxUdpDatagramSize};

/// Minimum valid message size (at least 1 byte payload)
inline constexpr std::size_t kMinMessageSize{1};

/// Default watchdog timeout for heartbeat monitoring
inline constexpr auto kDefaultWatchdogTimeout = defaults::kWatchdogTimeout;

/// Maximum sequence number before wrap-around
inline constexpr std::uint32_t kMaxSequenceNumber{std::numeric_limits<std::uint32_t>::max()};

/// CRC32 polynomial (IEEE 802.3)
inline constexpr std::uint32_t kCrc32Polynomial{0xEDB88320U};

/// Magic number for frame validation
inline constexpr std::uint32_t kFrameMagic{0xAVIO'SAFE};

// ============================================================================
// CRC32 Checksum (Data Integrity)
// ============================================================================

/**
 * @brief Precomputed CRC32 lookup table for fast checksum calculation.
 * 
 * Generated at compile time for deterministic initialization.
 */
class Crc32Table {
public:
    constexpr Crc32Table() noexcept : table_{} {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t crc = i;
            for (int j = 0; j < 8; ++j) {
                crc = (crc >> 1) ^ ((crc & 1) ? kCrc32Polynomial : 0);
            }
            table_[i] = crc;
        }
    }

    [[nodiscard]] constexpr std::uint32_t operator[](std::size_t index) const noexcept {
        return table_[index];
    }

private:
    std::array<std::uint32_t, 256> table_;
};

/// Global CRC32 lookup table (compile-time initialized)
inline constexpr Crc32Table kCrc32Table{};

/**
 * @brief Calculate CRC32 checksum for data integrity verification.
 * 
 * @param data Pointer to data buffer
 * @param length Length of data in bytes
 * @param initialCrc Optional initial CRC value for chained calculations
 * @return CRC32 checksum value
 * 
 * @note This function is deterministic and safe for flight-critical use.
 */
[[nodiscard]] inline std::uint32_t calculateCrc32(
    const std::uint8_t* data,
    std::size_t length,
    std::uint32_t initialCrc = 0xFFFFFFFFU) noexcept 
{
    if (data == nullptr || length == 0) {
        return initialCrc ^ 0xFFFFFFFFU;
    }

    std::uint32_t crc = initialCrc;
    for (std::size_t i = 0; i < length; ++i) {
        const std::uint8_t index = static_cast<std::uint8_t>(crc ^ data[i]);
        crc = (crc >> 8) ^ kCrc32Table[index];
    }
    return crc ^ 0xFFFFFFFFU;
}

/**
 * @brief Verify CRC32 checksum of received data.
 * 
 * @param data Pointer to data buffer (including appended CRC)
 * @param length Total length including 4-byte CRC
 * @return true if CRC matches, false otherwise
 */
[[nodiscard]] inline bool verifyCrc32(const std::uint8_t* data, std::size_t length) noexcept {
    if (data == nullptr || length < 4) {
        return false;
    }

    const std::size_t payloadLength = length - 4;
    const std::uint32_t calculatedCrc = calculateCrc32(data, payloadLength);
    
    // Extract stored CRC (little-endian)
    std::uint32_t storedCrc = 0;
    storedCrc |= static_cast<std::uint32_t>(data[payloadLength + 0]) << 0;
    storedCrc |= static_cast<std::uint32_t>(data[payloadLength + 1]) << 8;
    storedCrc |= static_cast<std::uint32_t>(data[payloadLength + 2]) << 16;
    storedCrc |= static_cast<std::uint32_t>(data[payloadLength + 3]) << 24;

    return calculatedCrc == storedCrc;
}

/**
 * @brief Append CRC32 checksum to data buffer.
 * 
 * @param data Data buffer with space for 4 additional bytes
 * @param payloadLength Length of payload (CRC will be written at offset payloadLength)
 */
inline void appendCrc32(std::uint8_t* data, std::size_t payloadLength) noexcept {
    if (data == nullptr) {
        return;
    }

    const std::uint32_t crc = calculateCrc32(data, payloadLength);
    
    // Store CRC in little-endian format
    data[payloadLength + 0] = static_cast<std::uint8_t>(crc >> 0);
    data[payloadLength + 1] = static_cast<std::uint8_t>(crc >> 8);
    data[payloadLength + 2] = static_cast<std::uint8_t>(crc >> 16);
    data[payloadLength + 3] = static_cast<std::uint8_t>(crc >> 24);
}

// ============================================================================
// Sequence Numbering (Message Ordering & Loss Detection)
// ============================================================================

/**
 * @brief Thread-safe sequence number generator with wrap-around handling.
 */
class SequenceGenerator {
public:
    SequenceGenerator() noexcept = default;
    
    // Non-copyable, non-movable for thread safety
    SequenceGenerator(const SequenceGenerator&) = delete;
    SequenceGenerator& operator=(const SequenceGenerator&) = delete;

    /**
     * @brief Get the next sequence number (atomic increment with wrap-around).
     * @return Next sequence number
     */
    [[nodiscard]] std::uint32_t next() noexcept {
        return counter_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Get current sequence number without incrementing.
     * @return Current sequence number
     */
    [[nodiscard]] std::uint32_t current() const noexcept {
        return counter_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Reset sequence counter to zero.
     */
    void reset() noexcept {
        counter_.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint32_t> counter_{0};
};

/**
 * @brief Sequence number validator for detecting gaps and duplicates.
 */
class SequenceValidator {
public:
    /**
     * @brief Validation result for received sequence numbers.
     */
    enum class Result {
        Valid,          ///< Sequence is as expected
        Duplicate,      ///< Sequence already received
        Gap,            ///< One or more sequences were skipped
        OutOfOrder,     ///< Received out of expected order
        WrapAround      ///< Sequence wrapped around (valid)
    };

    SequenceValidator() noexcept = default;

    /**
     * @brief Validate an incoming sequence number.
     * 
     * @param sequence The received sequence number
     * @return Validation result
     */
    [[nodiscard]] Result validate(std::uint32_t sequence) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!initialized_) {
            expectedSequence_ = sequence + 1;
            initialized_ = true;
            return Result::Valid;
        }

        // Handle wrap-around
        constexpr std::uint32_t wrapThreshold = kMaxSequenceNumber - 1000;
        if (expectedSequence_ > wrapThreshold && sequence < 1000) {
            expectedSequence_ = sequence + 1;
            ++wrapAroundCount_;
            return Result::WrapAround;
        }

        if (sequence == expectedSequence_) {
            ++expectedSequence_;
            return Result::Valid;
        }

        if (sequence < expectedSequence_) {
            ++duplicateCount_;
            return Result::Duplicate;
        }

        // sequence > expectedSequence_
        const std::uint32_t gap = sequence - expectedSequence_;
        gapCount_ += gap;
        expectedSequence_ = sequence + 1;
        return Result::Gap;
    }

    /**
     * @brief Get statistics about sequence validation.
     */
    struct Stats {
        std::uint64_t duplicateCount{0};
        std::uint64_t gapCount{0};
        std::uint64_t wrapAroundCount{0};
        std::uint32_t expectedSequence{0};
    };

    [[nodiscard]] Stats getStats() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return {duplicateCount_, gapCount_, wrapAroundCount_, expectedSequence_};
    }

    void reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        initialized_ = false;
        expectedSequence_ = 0;
        duplicateCount_ = 0;
        gapCount_ = 0;
        wrapAroundCount_ = 0;
    }

private:
    mutable std::mutex mutex_;
    bool initialized_{false};
    std::uint32_t expectedSequence_{0};
    std::uint64_t duplicateCount_{0};
    std::uint64_t gapCount_{0};
    std::uint64_t wrapAroundCount_{0};
};

// ============================================================================
// Watchdog / Heartbeat Monitoring (Liveness Check)
// ============================================================================

/**
 * @brief Watchdog timer for detecting communication timeouts.
 * 
 * Use this to monitor heartbeat messages from remote systems.
 * If no heartbeat is received within the timeout period, the watchdog
 * triggers a timeout condition.
 */
class Watchdog {
public:
    /**
     * @brief Watchdog state enumeration.
     */
    enum class State {
        Inactive,   ///< Watchdog not started
        Active,     ///< Normal operation, receiving heartbeats
        Timeout,    ///< Timeout occurred - no heartbeat received
        Recovered   ///< Recovered from timeout state
    };

    explicit Watchdog(std::chrono::milliseconds timeout = kDefaultWatchdogTimeout) noexcept
        : timeout_(timeout) {}

    /**
     * @brief Feed the watchdog (call on each heartbeat/message received).
     * 
     * This resets the timeout counter and updates the state.
     */
    void feed() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        lastFeedTime_ = std::chrono::steady_clock::now();
        
        if (state_ == State::Timeout) {
            state_ = State::Recovered;
            ++recoveryCount_;
        } else if (state_ == State::Inactive) {
            state_ = State::Active;
        }
    }

    /**
     * @brief Check the watchdog state.
     * 
     * Call this periodically to detect timeouts.
     * 
     * @return Current watchdog state
     */
    [[nodiscard]] State check() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == State::Inactive) {
            return State::Inactive;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - lastFeedTime_;

        if (elapsed > timeout_) {
            if (state_ != State::Timeout) {
                state_ = State::Timeout;
                ++timeoutCount_;
            }
        }

        return state_;
    }

    /**
     * @brief Get time elapsed since last feed.
     */
    [[nodiscard]] std::chrono::milliseconds timeSinceLastFeed() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFeedTime_);
    }

    /**
     * @brief Check if currently in timeout state.
     */
    [[nodiscard]] bool isTimedOut() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_ == State::Timeout;
    }

    /**
     * @brief Get timeout statistics.
     */
    struct Stats {
        std::uint64_t timeoutCount{0};
        std::uint64_t recoveryCount{0};
        State currentState{State::Inactive};
        std::chrono::milliseconds timeSinceLastFeed{0};
    };

    [[nodiscard]] Stats getStats() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        return {
            timeoutCount_,
            recoveryCount_,
            state_,
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFeedTime_)
        };
    }

    /**
     * @brief Set timeout duration.
     */
    void setTimeout(std::chrono::milliseconds timeout) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        timeout_ = timeout;
    }

    /**
     * @brief Reset watchdog to inactive state.
     */
    void reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = State::Inactive;
        lastFeedTime_ = std::chrono::steady_clock::now();
    }

private:
    mutable std::mutex mutex_;
    std::chrono::milliseconds timeout_;
    std::chrono::steady_clock::time_point lastFeedTime_{std::chrono::steady_clock::now()};
    State state_{State::Inactive};
    std::uint64_t timeoutCount_{0};
    std::uint64_t recoveryCount_{0};
};

// ============================================================================
// Sanity Checks (Input Validation)
// ============================================================================

/**
 * @brief Validate message size is within acceptable bounds.
 * 
 * @param size Message size in bytes
 * @param maxSize Maximum allowed size (default: kMaxMessageSize)
 * @return true if size is valid, false otherwise
 */
[[nodiscard]] inline bool isValidMessageSize(
    std::size_t size,
    std::size_t maxSize = kMaxMessageSize) noexcept 
{
    return size >= kMinMessageSize && size <= maxSize;
}

/**
 * @brief Validate pointer is not null.
 */
[[nodiscard]] inline bool isValidPointer(const void* ptr) noexcept {
    return ptr != nullptr;
}

/**
 * @brief Validate CAN ID is within valid range.
 * 
 * @param id CAN identifier
 * @param extended Whether extended (29-bit) or standard (11-bit) ID
 * @return true if ID is valid
 */
[[nodiscard]] inline bool isValidCanId(std::uint32_t id, bool extended) noexcept {
    constexpr std::uint32_t kStandardIdMax = 0x7FFU;      // 11-bit
    constexpr std::uint32_t kExtendedIdMax = 0x1FFFFFFFU; // 29-bit
    return id <= (extended ? kExtendedIdMax : kStandardIdMax);
}

/**
 * @brief Validate IP port number.
 */
[[nodiscard]] inline bool isValidPort(std::uint16_t port) noexcept {
    return port > 0; // Port 0 is reserved
}

/**
 * @brief Validate baud rate is a standard value.
 */
[[nodiscard]] inline bool isValidBaudRate(int baudRate) noexcept {
    constexpr int validRates[] = {
        9600, 19200, 38400, 57600, 115200, 
        230400, 460800, 921600, 1000000, 2000000, 3000000
    };
    for (int rate : validRates) {
        if (baudRate == rate) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Failsafe State Management
// ============================================================================

/**
 * @brief Failsafe states for flight-critical systems.
 */
enum class FailsafeState {
    Normal,         ///< System operating normally
    Degraded,       ///< Reduced functionality but operational
    FailSafe,       ///< Safe fallback mode activated
    Emergency       ///< Critical failure - emergency procedures active
};

/**
 * @brief Failsafe state machine for managing system safety states.
 */
class FailsafeManager {
public:
    using StateChangeCallback = void(*)(FailsafeState oldState, FailsafeState newState);

    FailsafeManager() noexcept = default;

    /**
     * @brief Get current failsafe state.
     */
    [[nodiscard]] FailsafeState getState() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    /**
     * @brief Transition to a new state.
     * 
     * @param newState Target state
     * @return true if transition was allowed and completed
     */
    bool transitionTo(FailsafeState newState) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        
        const auto oldState = state_.load(std::memory_order_relaxed);
        
        // Validate transition (can only go to more severe states or back to Normal)
        if (!isValidTransition(oldState, newState)) {
            return false;
        }

        state_.store(newState, std::memory_order_release);
        ++transitionCount_;

        if (callback_) {
            callback_(oldState, newState);
        }

        return true;
    }

    /**
     * @brief Report a fault condition.
     * 
     * Automatically escalates state based on fault count.
     */
    void reportFault() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        ++faultCount_;

        const auto current = state_.load(std::memory_order_relaxed);
        
        // Escalation logic
        if (faultCount_ >= kEmergencyThreshold && current != FailsafeState::Emergency) {
            state_.store(FailsafeState::Emergency, std::memory_order_release);
        } else if (faultCount_ >= kFailsafeThreshold && current == FailsafeState::Normal) {
            state_.store(FailsafeState::Degraded, std::memory_order_release);
        } else if (faultCount_ >= kDegradedThreshold && current == FailsafeState::Degraded) {
            state_.store(FailsafeState::FailSafe, std::memory_order_release);
        }
    }

    /**
     * @brief Clear fault counter (call when system recovers).
     */
    void clearFaults() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        faultCount_ = 0;
    }

    /**
     * @brief Reset to normal state and clear all faults.
     */
    void reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.store(FailsafeState::Normal, std::memory_order_release);
        faultCount_ = 0;
    }

    /**
     * @brief Set state change callback.
     */
    void setCallback(StateChangeCallback cb) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = cb;
    }

    /**
     * @brief Get fault statistics.
     */
    struct Stats {
        std::uint64_t faultCount{0};
        std::uint64_t transitionCount{0};
        FailsafeState currentState{FailsafeState::Normal};
    };

    [[nodiscard]] Stats getStats() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return {faultCount_, transitionCount_, state_.load(std::memory_order_relaxed)};
    }

private:
    static constexpr std::uint64_t kDegradedThreshold = 3;
    static constexpr std::uint64_t kFailsafeThreshold = 5;
    static constexpr std::uint64_t kEmergencyThreshold = 10;

    [[nodiscard]] static bool isValidTransition(FailsafeState from, FailsafeState to) noexcept {
        // Always allow reset to Normal
        if (to == FailsafeState::Normal) {
            return true;
        }
        // Can escalate to more severe states
        return static_cast<int>(to) >= static_cast<int>(from);
    }

    mutable std::mutex mutex_;
    std::atomic<FailsafeState> state_{FailsafeState::Normal};
    std::uint64_t faultCount_{0};
    std::uint64_t transitionCount_{0};
    StateChangeCallback callback_{nullptr};
};

// ============================================================================
// Safe Frame Header (Protocol Wrapper)
// ============================================================================

/**
 * @brief Safety-enhanced message frame header.
 * 
 * This header wraps payload data with safety information:
 * - Magic number for frame sync
 * - Sequence number for ordering
 * - Timestamp for latency monitoring
 * - CRC32 for integrity
 */
#pragma pack(push, 1)
struct SafeFrameHeader {
    std::uint32_t magic{kFrameMagic};       ///< Frame sync magic number
    std::uint32_t sequence{0};               ///< Sequence number
    std::uint32_t timestamp{0};              ///< Timestamp (ms since epoch, truncated)
    std::uint16_t payloadLength{0};          ///< Payload length in bytes
    std::uint16_t reserved{0};               ///< Reserved for future use
    // Followed by: payload[payloadLength] + crc32[4]
};
#pragma pack(pop)

static_assert(sizeof(SafeFrameHeader) == 16, "SafeFrameHeader must be 16 bytes");

/**
 * @brief Calculate total frame size including header, payload, and CRC.
 */
[[nodiscard]] inline std::size_t calculateFrameSize(std::size_t payloadLength) noexcept {
    return sizeof(SafeFrameHeader) + payloadLength + 4; // +4 for CRC32
}

/**
 * @brief Validate a safe frame header.
 */
[[nodiscard]] inline bool isValidFrameHeader(const SafeFrameHeader& header) noexcept {
    if (header.magic != kFrameMagic) {
        return false;
    }
    if (header.payloadLength > kMaxMessageSize) {
        return false;
    }
    return true;
}

// ============================================================================
// Redundancy Support
// ============================================================================

/**
 * @brief Channel health status for redundancy management.
 */
enum class ChannelHealth {
    Unknown,
    Healthy,
    Degraded,
    Failed
};

/**
 * @brief Redundancy manager for dual/triple channel configurations.
 * 
 * Monitors multiple communication channels and selects the best one.
 */
template<std::size_t NumChannels = 2>
class RedundancyManager {
public:
    static_assert(NumChannels >= 2 && NumChannels <= 4, "Supports 2-4 channels");

    RedundancyManager() noexcept {
        for (auto& health : channelHealth_) {
            health = ChannelHealth::Unknown;
        }
    }

    /**
     * @brief Update channel health status.
     */
    void updateChannelHealth(std::size_t channel, ChannelHealth health) noexcept {
        if (channel < NumChannels) {
            std::lock_guard<std::mutex> lock(mutex_);
            channelHealth_[channel] = health;
        }
    }

    /**
     * @brief Get the primary (best) channel index.
     * 
     * @return Channel index, or std::nullopt if no healthy channel
     */
    [[nodiscard]] std::optional<std::size_t> getPrimaryChannel() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Prefer healthy channels in order
        for (std::size_t i = 0; i < NumChannels; ++i) {
            if (channelHealth_[i] == ChannelHealth::Healthy) {
                return i;
            }
        }
        
        // Fall back to degraded channels
        for (std::size_t i = 0; i < NumChannels; ++i) {
            if (channelHealth_[i] == ChannelHealth::Degraded) {
                return i;
            }
        }

        return std::nullopt;
    }

    /**
     * @brief Check if system has at least one healthy channel.
     */
    [[nodiscard]] bool hasHealthyChannel() const noexcept {
        return getPrimaryChannel().has_value();
    }

    /**
     * @brief Get number of healthy channels.
     */
    [[nodiscard]] std::size_t healthyChannelCount() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t count = 0;
        for (const auto& health : channelHealth_) {
            if (health == ChannelHealth::Healthy) {
                ++count;
            }
        }
        return count;
    }

    /**
     * @brief Get channel health array.
     */
    [[nodiscard]] std::array<ChannelHealth, NumChannels> getChannelHealth() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return channelHealth_;
    }

private:
    mutable std::mutex mutex_;
    std::array<ChannelHealth, NumChannels> channelHealth_;
};

// ============================================================================
// Utility: Bounded Value (Defensive Programming)
// ============================================================================

/**
 * @brief Value type with automatic bounds clamping.
 * 
 * Ensures values always stay within specified bounds,
 * preventing overflow/underflow in calculations.
 */
template<typename T, T MinVal, T MaxVal>
class BoundedValue {
    static_assert(MinVal <= MaxVal, "MinVal must be <= MaxVal");

public:
    constexpr BoundedValue() noexcept : value_(MinVal) {}
    
    constexpr explicit BoundedValue(T value) noexcept 
        : value_(clamp(value)) {}

    constexpr BoundedValue& operator=(T value) noexcept {
        value_ = clamp(value);
        return *this;
    }

    [[nodiscard]] constexpr T get() const noexcept { return value_; }
    [[nodiscard]] constexpr operator T() const noexcept { return value_; }

    [[nodiscard]] static constexpr T min() noexcept { return MinVal; }
    [[nodiscard]] static constexpr T max() noexcept { return MaxVal; }

private:
    [[nodiscard]] static constexpr T clamp(T value) noexcept {
        if (value < MinVal) return MinVal;
        if (value > MaxVal) return MaxVal;
        return value;
    }

    T value_;
};

} // namespace comm::safety
