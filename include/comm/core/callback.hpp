/**
 * @file callback.hpp
 * @brief DO-178C Level B Compliant Callback Registry
 * 
 * This module provides thread-safe callback registration and invocation
 * for asynchronous event handling in flight-critical avionics systems.
 * 
 * Features:
 * - Thread-safe callback registration
 * - Minimal overhead invocation
 * - Type-safe callback signatures
 * 
 * @note All operations are thread-safe.
 * @note Uses std::function for callback storage.
 * 
 * @requirement SRS-CB-001: Callback registration
 * @requirement SRS-CB-002: Thread-safe invocation
 * 
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <functional>
#include <mutex>
#include <utility>

#include "config.hpp"
#include "error.hpp"

namespace comm {

/// Callback invoked when new data is available for consumption.
using ReceiveCallback = std::function<void(const ByteVector&)>;
/// Callback invoked when an error is reported by the transport.
using ErrorCallback = std::function<void(const Error&)>;

/**
 * @brief Thread-safe holder for a single callback.
 *
 * Provides minimal overhead for asynchronous notification scenarios while
 * avoiding the cost of unnecessary locking during invocation.
 */
template <typename Signature>
class CallbackRegistry {
public:
    using Callback = std::function<Signature>;

    /// Register a callback overriding the previous entry.
    void setCallback(Callback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = std::move(callback);
    }

    /// Remove the currently registered callback.
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = nullptr;
    }

    /// Indicates whether a callback has been registered.
    bool hasCallback() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<bool>(callback_);
    }

    /// Invoke the registered callback if present.
    template <typename... Args>
    void notify(Args&&... args) const {
        Callback callbackCopy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callbackCopy = callback_;
        }

        if (callbackCopy) {
            callbackCopy(std::forward<Args>(args)...);
        }
    }

private:
    mutable std::mutex mutex_{};
    Callback callback_{};
};

/// Convenience alias for byte-oriented receive callbacks.
using ReceiveCallbackRegistry = CallbackRegistry<void(const ByteVector&)>;
/// Convenience alias for structured error callbacks.
using ErrorCallbackRegistry = CallbackRegistry<void(const Error&)>;

} // namespace comm
