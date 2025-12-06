/**
 * @file pcan_basic.hpp
 * @brief DO-178C Level B Compliant PCAN-Basic Interface
 * 
 * This module provides Peak-System PCAN-Basic communication functionality
 * for flight-critical avionics systems on Windows.
 * 
 * Features:
 * - PCAN USB adapter support (channels 1-8)
 * - CAN 2.0A/B frame support
 * - CAN FD support (optional)
 * - Hardware filtering
 * - Safety mechanism integration (CRC, watchdog, sequence)
 * 
 * @note All functions are designed to be deterministic and exception-free.
 * @note Thread-safe for concurrent send/receive operations.
 * @note Windows-only implementation using PCAN-Basic API.
 * 
 * @requirement SRS-CAN-010: PCAN-Basic management
 * @requirement SRS-CAN-011: PCAN frame filtering
 * @requirement SRS-CAN-012: PCAN FD support
 * @requirement SRS-CAN-013: Safety integration
 * 
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <comm/core/interface.hpp>
#include <comm/core/defaults.hpp>
#include <comm/core/safety.hpp>

struct tagTPCANMsg;
struct tagTPCANMsgFD;

namespace comm::can {

enum class PCANChannel : std::uint16_t {
    Auto = 0xFFFF,
    Usb1 = 0x51,
    Usb2 = 0x52,
    Usb3 = 0x53,
    Usb4 = 0x54,
    Usb5 = 0x55,
    Usb6 = 0x56,
    Usb7 = 0x57,
    Usb8 = 0x58,
};

/// Configuration for the PCAN-Basic transport (Peak-System devices).
struct PCANBasicConfig : Config {
    std::uint16_t handle{static_cast<std::uint16_t>(PCANChannel::Usb1)};
    std::uint32_t bitrate{defaults::kPcanBaud500K};  ///< 500 kbit/s (PCAN_BAUD_500K)
    bool enableFD{false};
    std::string fdBitrate{ "f_clock=80000000, nom_brp=1, nom_tseg1=63, nom_tseg2=16, nom_sjw=16, data_brp=1, data_tseg1=15, data_tseg2=4, data_sjw=4" };
    bool listenOnly{false};
    bool hardwareTimestamps{false};

    PCANBasicConfig() {
        protocol = Protocol::CAN;
        direction = Direction::TwoWay;
        autoReconnect = true;
    }
};

/// Linux PCAN-Basic transport built on libpcanbasic.
/// 
/// @note This transport includes flight-critical safety features:
///       - Watchdog monitoring for heartbeat detection
///       - Sequence numbering for message ordering
///       - Failsafe state management
///       - Input validation and sanity checks
class PCANBasic : public ICommunication {
public:
    explicit PCANBasic(PCANBasicConfig config = {});
    ~PCANBasic() noexcept override;

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

    // ========== Flight-Critical Safety APIs ==========
    
    /// Feed the watchdog timer (call periodically or on successful communication).
    void feedWatchdog() noexcept;
    
    /// Check watchdog state for timeout detection.
    [[nodiscard]] safety::Watchdog::State checkWatchdog() noexcept;
    
    /// Get current failsafe state.
    [[nodiscard]] safety::FailsafeState getFailsafeState() const noexcept;
    
    /// Get next sequence number for outgoing messages.
    [[nodiscard]] std::uint32_t getNextSequence() noexcept;
    
    /// Validate incoming sequence number.
    [[nodiscard]] safety::SequenceValidator::Result validateSequence(std::uint32_t seq) noexcept;
    
    /// Get safety statistics.
    struct SafetyStats {
        safety::Watchdog::Stats watchdog;
        safety::SequenceValidator::Stats sequence;
        safety::FailsafeManager::Stats failsafe;
        std::uint64_t consecutiveErrors{0};
    };
    [[nodiscard]] SafetyStats getSafetyStats() const noexcept;

private:
    using TPCANHandle = std::uint16_t;
    using TPCANStatus = std::uint32_t;

    TPCANHandle handle() const;
    bool initialize();
    void uninitialize();
    bool applyMode();
    void startReceiveThread();
    void stopReceiveThread();
    void receiveLoop();
    void recordSend(std::size_t bytes);
    void recordReceive(std::size_t bytes);
    void reportError(Error error);
    bool recoverIfNeeded(TPCANStatus status);
    bool shouldSpawnReceiveThread() const;
    std::chrono::milliseconds receiveSleepDuration() const;
    
    /// Called on successful send/receive to reset error tracking.
    void recordSuccess() noexcept;
    
    /// Called on error to track consecutive failures.
    void recordFailure() noexcept;

    PCANBasicConfig config_{};
    mutable std::mutex mutex_{};
    mutable std::mutex statsMutex_{};
    Statistics stats_{};

    ReceiveCallbackRegistry receiveCallback_{};
    ErrorCallbackRegistry errorCallback_{};

    std::atomic<bool> healthy_{true};
    std::atomic<bool> running_{false};
    std::thread receiveThread_{};
    bool initialized_{false};
    
    // Flight-critical safety components
    safety::Watchdog watchdog_{defaults::kWatchdogTimeout};
    safety::SequenceGenerator sequenceGen_{};
    safety::SequenceValidator sequenceValidator_{};
    safety::FailsafeManager failsafeManager_{};
    std::atomic<std::uint64_t> consecutiveErrors_{0};
};

} // namespace comm::can
