/**
 * @file interface.hpp
 * @brief DO-178C Compliant Communication Interface Definition
 * 
 * This header defines the base interface for all communication transports.
 * All implementations must be thread-safe and exception-free.
 * 
 * @requirement SRS-COMM-INT-001: Common transport interface
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <cstddef>

#include "callback.hpp"
#include "config.hpp"
#include "error.hpp"

namespace comm {

/**
 * @brief Common interface implemented by all communication transports.
 *
 * Implementations must provide thread-safe operations and avoid throwing
 * exceptions. Errors should be reported through return values and the
 * registered error callback.
 */
class ICommunication {
public:
    virtual ~ICommunication() noexcept = default;

    // Lifecycle management
    /// Open the underlying transport.
    virtual bool open() = 0;
    /// Close the underlying transport and release resources.
    virtual bool close() = 0;
    /// Query whether the transport is currently open.
    [[nodiscard]] virtual bool isOpen() const = 0;

    // Data plane
    /// Send data over the transport, returning bytes written or negative on failure.
    virtual std::ptrdiff_t send(const ByteVector& data) = 0;
    /// Receive up to maxSize bytes into buffer, returning bytes read or negative on failure.
    virtual std::ptrdiff_t receive(ByteVector& buffer, std::size_t maxSize) = 0;

    // Configuration
    /// Apply a new configuration instance.
    virtual bool configure(const Config& config) = 0;
    /// Retrieve the current effective configuration.
    [[nodiscard]] virtual Config getConfig() const = 0;

    // Observability
    /// Snapshot transport metrics.
    [[nodiscard]] virtual Statistics getStatistics() const = 0;
    /// Evaluate the health of the transport.
    [[nodiscard]] virtual bool isHealthy() const = 0;

    // Callbacks
    /// Register the receive callback invoked on incoming data.
    virtual void setReceiveCallback(ReceiveCallback callback) = 0;
    /// Register the error callback invoked on error conditions.
    virtual void setErrorCallback(ErrorCallback callback) = 0;
};

} // namespace comm
