/**
 * @file safety_mixin.hpp
 * @brief DO-178C Level B Compliant Safety Mixin for Transports
 * 
 * This mixin provides common safety functionality that can be 
 * composed into any transport implementation via inheritance or composition.
 * 
 * Features:
 * - Watchdog/heartbeat monitoring integration
 * - Sequence numbering for message tracking
 * - Failsafe state management
 * - Safety statistics collection
 * 
 * @note All functions are designed to be deterministic and exception-free.
 * @note No dynamic memory allocation is performed.
 * @note Thread-safe operations for multi-threaded environments.
 * 
 * @requirement SRS-SAFETY-010: Transport safety integration
 * @requirement SRS-SAFETY-011: Safety statistics collection
 * 
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <comm/core/safety.hpp>
#include <comm/core/defaults.hpp>

namespace comm {

/**
 * @brief Safety statistics structure common to all transports.
 */
struct TransportSafetyStats {
    safety::Watchdog::Stats watchdog{};
    safety::SequenceValidator::Stats sequence{};
    safety::FailsafeManager::Stats failsafe{};
    std::uint64_t consecutiveErrors{0};
};

/**
 * @brief Mixin class providing flight-critical safety mechanisms.
 * 
 * Include this in transport classes via composition or protected inheritance
 * to gain watchdog, sequence numbering, and failsafe state management.
 * 
 * @code
 * class MyTransport : public ICommunication, protected SafetyMixin {
 *     // ... use feedWatchdog(), checkWatchdog(), etc.
 * };
 * @endcode
 */
class SafetyMixin {
public:
    /// Default watchdog timeout (can be configured per transport).
    static constexpr auto kDefaultWatchdogTimeout = defaults::kWatchdogTimeout;
    
    /// Maximum consecutive errors before failsafe escalation.
    static constexpr auto kMaxConsecutiveErrors = defaults::kMaxConsecutiveErrors;

    explicit SafetyMixin(std::chrono::milliseconds watchdogTimeout = kDefaultWatchdogTimeout) noexcept
        : watchdog_(watchdogTimeout) {}

    // Non-copyable for thread safety
    SafetyMixin(const SafetyMixin&) = delete;
    SafetyMixin& operator=(const SafetyMixin&) = delete;

    // ========== Watchdog API ==========

    /// Feed the watchdog (call on successful communication).
    void feedWatchdog() noexcept {
        watchdog_.feed();
    }

    /// Check watchdog state and escalate failsafe if timeout.
    [[nodiscard]] safety::Watchdog::State checkWatchdog() noexcept {
        auto state = watchdog_.check();
        if (state == safety::Watchdog::State::Timeout) {
            failsafeManager_.reportFault();
        }
        return state;
    }

    /// Check if watchdog has timed out.
    [[nodiscard]] bool isWatchdogTimedOut() const noexcept {
        return watchdog_.isTimedOut();
    }

    /// Set watchdog timeout duration.
    void setWatchdogTimeout(std::chrono::milliseconds timeout) noexcept {
        watchdog_.setTimeout(timeout);
    }

    // ========== Sequence Numbering API ==========

    /// Get next sequence number for outgoing messages.
    [[nodiscard]] std::uint32_t getNextSequence() noexcept {
        return sequenceGen_.next();
    }

    /// Get current sequence number without incrementing.
    [[nodiscard]] std::uint32_t getCurrentSequence() const noexcept {
        return sequenceGen_.current();
    }

    /// Validate incoming sequence number, report faults on gaps/duplicates.
    [[nodiscard]] safety::SequenceValidator::Result validateSequence(std::uint32_t seq) noexcept {
        auto result = sequenceValidator_.validate(seq);
        if (result == safety::SequenceValidator::Result::Gap ||
            result == safety::SequenceValidator::Result::Duplicate) {
            failsafeManager_.reportFault();
        }
        return result;
    }

    /// Reset sequence validator state.
    void resetSequenceValidator() noexcept {
        sequenceValidator_.reset();
    }

    // ========== Failsafe State API ==========

    /// Get current failsafe state.
    [[nodiscard]] safety::FailsafeState getFailsafeState() const noexcept {
        return failsafeManager_.getState();
    }

    /// Check if system is in normal operating state.
    [[nodiscard]] bool isFailsafeNormal() const noexcept {
        return failsafeManager_.getState() == safety::FailsafeState::Normal;
    }

    /// Manually report a fault to the failsafe manager.
    void reportFault() noexcept {
        failsafeManager_.reportFault();
    }

    /// Reset failsafe state and clear faults.
    void resetFailsafe() noexcept {
        failsafeManager_.reset();
    }

    /// Set failsafe state change callback.
    void setFailsafeCallback(safety::FailsafeManager::StateChangeCallback cb) noexcept {
        failsafeManager_.setCallback(cb);
    }

    // ========== Success/Failure Tracking ==========

    /// Call on successful send/receive to reset error tracking and feed watchdog.
    void recordSuccess() noexcept {
        consecutiveErrors_.store(0, std::memory_order_relaxed);
        feedWatchdog();

        // Recover from degraded states after sustained success
        if (!isFailsafeNormal()) {
            failsafeManager_.clearFaults();
            failsafeManager_.transitionTo(safety::FailsafeState::Normal);
        }
    }

    /// Call on error to track consecutive failures and escalate if threshold exceeded.
    void recordFailure() noexcept {
        const auto errors = consecutiveErrors_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (errors >= kMaxConsecutiveErrors) {
            failsafeManager_.reportFault();
        }
    }

    /// Get current consecutive error count.
    [[nodiscard]] std::uint64_t getConsecutiveErrors() const noexcept {
        return consecutiveErrors_.load(std::memory_order_relaxed);
    }

    // ========== Statistics ==========

    /// Get comprehensive safety statistics.
    [[nodiscard]] TransportSafetyStats getSafetyStats() const noexcept {
        return {
            watchdog_.getStats(),
            sequenceValidator_.getStats(),
            failsafeManager_.getStats(),
            consecutiveErrors_.load(std::memory_order_relaxed)
        };
    }

    /// Reset all safety state.
    void resetSafety() noexcept {
        watchdog_.reset();
        sequenceGen_.reset();
        sequenceValidator_.reset();
        failsafeManager_.reset();
        consecutiveErrors_.store(0, std::memory_order_relaxed);
    }

protected:
    ~SafetyMixin() = default;  // Protected to prevent slicing

private:
    safety::Watchdog watchdog_;
    safety::SequenceGenerator sequenceGen_{};
    safety::SequenceValidator sequenceValidator_{};
    safety::FailsafeManager failsafeManager_{};
    std::atomic<std::uint64_t> consecutiveErrors_{0};
};

} // namespace comm
