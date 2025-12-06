/**
 * @file utils.hpp
 * @brief DO-178C Level B Compliant Utility Functions and Constants
 * 
 * This module provides common utility functions and constants
 * used throughout the communication library.
 * 
 * Features:
 * - Compile-time constants for buffer sizes and timeouts
 * - Direction checking helper functions
 * - Sleep duration utilities
 * 
 * @note All functions are deterministic, exception-free, and inline.
 * @note All constants are compile-time evaluated.
 * 
 * @requirement SRS-UTIL-001: Direction validation utilities
 * @requirement SRS-UTIL-002: Compile-time constants
 * 
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <chrono>
#include "config.hpp"
#include "defaults.hpp"

namespace comm {

/// @deprecated Use defaults::kReceiveThreadSleep instead.
inline constexpr auto kDefaultReceiveSleep = defaults::kReceiveThreadSleep;

/// @deprecated Use defaults::kHealthCheckInterval instead.
inline constexpr auto kDefaultHealthCheckInterval = defaults::kHealthCheckInterval;

/// @deprecated Use defaults::kMaxUdpDatagramSize instead.
inline constexpr auto kMaxUdpDatagramSize = defaults::kMaxUdpDatagramSize;

/// @deprecated Use defaults::kDefaultTcpBufferSize instead.
inline constexpr auto kDefaultTcpBufferSize = defaults::kDefaultTcpBufferSize;

/// Check if the direction allows receiving data.
[[nodiscard]] inline bool isReceiveEnabled(Direction direction) noexcept {
    return direction == Direction::ReceiveOnly || direction == Direction::TwoWay;
}

/// Check if the direction allows sending data.
[[nodiscard]] inline bool isSendEnabled(Direction direction) noexcept {
    return direction == Direction::SendOnly || direction == Direction::TwoWay;
}

/// Return the effective sleep duration, falling back to default if zero or negative.
[[nodiscard]] inline std::chrono::milliseconds effectiveSleep(std::chrono::milliseconds requested) noexcept {
    return requested.count() > 0 ? requested : kDefaultReceiveSleep;
}

/// Saturating increment for statistics counters (prevents overflow).
[[nodiscard]] inline std::uint64_t saturatingAdd(std::uint64_t a, std::uint64_t b) noexcept {
    constexpr auto maxVal = std::numeric_limits<std::uint64_t>::max();
    return (a > maxVal - b) ? maxVal : (a + b);
}

} // namespace comm
