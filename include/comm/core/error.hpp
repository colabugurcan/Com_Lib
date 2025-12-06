/**
 * @file error.hpp
 * @brief DO-178C Compliant Error Handling Structures
 * 
 * This header defines error types and handling mechanisms.
 * No exceptions are used - all errors are reported via return values
 * and callbacks for deterministic behavior.
 * 
 * @requirement SRS-COMM-ERR-001: Error reporting and handling
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace comm {

/// High-level classification for reported errors.
enum class ErrorCategory {
    None,
    System,
    Configuration,
    Connection,
    Transmission,
    Timeout,
    Protocol,
    Resource,
    Unknown
};

/// Severity level associated with an error.
enum class ErrorSeverity {
    Info,
    Warning,
    Recoverable,
    Critical
};

/// Detailed error code used for diagnostics and recovery.
enum class ErrorCode {
    None,
    InvalidConfiguration,
    AlreadyOpen,
    NotOpen,
    OpenFailed,
    CloseFailed,
    SendFailed,
    ReceiveFailed,
    Timeout,
    ConnectionLost,
    ResourceUnavailable,
    UnsupportedOperation,
    PermissionDenied,
    Unknown
};

/// Structured error descriptor that avoids exception usage.
struct Error {
    ErrorCode code{ErrorCode::None};
    ErrorCategory category{ErrorCategory::None};
    ErrorSeverity severity{ErrorSeverity::Info};
    std::string message{};
    std::string context{};
    std::optional<int> systemError{};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

/// Helper to determine whether retry logic should be attempted.
inline bool isRetryable(const Error& error) {
    switch (error.code) {
    case ErrorCode::Timeout:
    case ErrorCode::ConnectionLost:
    case ErrorCode::ResourceUnavailable:
        return true;
    default:
        return false;
    }
}

/// Convert an error code to a string literal.
inline std::string_view toString(ErrorCode code) {
    switch (code) {
    case ErrorCode::None: return "None";
    case ErrorCode::InvalidConfiguration: return "InvalidConfiguration";
    case ErrorCode::AlreadyOpen: return "AlreadyOpen";
    case ErrorCode::NotOpen: return "NotOpen";
    case ErrorCode::OpenFailed: return "OpenFailed";
    case ErrorCode::CloseFailed: return "CloseFailed";
    case ErrorCode::SendFailed: return "SendFailed";
    case ErrorCode::ReceiveFailed: return "ReceiveFailed";
    case ErrorCode::Timeout: return "Timeout";
    case ErrorCode::ConnectionLost: return "ConnectionLost";
    case ErrorCode::ResourceUnavailable: return "ResourceUnavailable";
    case ErrorCode::UnsupportedOperation: return "UnsupportedOperation";
    case ErrorCode::PermissionDenied: return "PermissionDenied";
    case ErrorCode::Unknown: return "Unknown";
    }
    return "Unknown";
}

/// Convert an error category to a string literal.
inline std::string_view toString(ErrorCategory category) {
    switch (category) {
    case ErrorCategory::None: return "None";
    case ErrorCategory::System: return "System";
    case ErrorCategory::Configuration: return "Configuration";
    case ErrorCategory::Connection: return "Connection";
    case ErrorCategory::Transmission: return "Transmission";
    case ErrorCategory::Timeout: return "Timeout";
    case ErrorCategory::Protocol: return "Protocol";
    case ErrorCategory::Resource: return "Resource";
    case ErrorCategory::Unknown: return "Unknown";
    }
    return "Unknown";
}

/// Convert an error severity to a string literal.
inline std::string_view toString(ErrorSeverity severity) {
    switch (severity) {
    case ErrorSeverity::Info: return "Info";
    case ErrorSeverity::Warning: return "Warning";
    case ErrorSeverity::Recoverable: return "Recoverable";
    case ErrorSeverity::Critical: return "Critical";
    }
    return "Info";
}

} // namespace comm
