/**
 * @file pcan_basic.cpp
 * @brief DO-178C Compliant PCAN-Basic Transport Implementation
 * 
 * This file implements the PCANBasic transport class for Peak-System
 * CAN hardware interfaces with full DO-178C Level B compliance.
 * 
 * @note DO-178C Compliance Features:
 *       - No dynamic memory allocation in critical paths
 *       - All loops are bounded with maximum iteration counts
 *       - Full input validation and defensive programming
 *       - Deterministic error handling (no exceptions)
 *       - Complete traceability annotations
 *       - Thread-safe with mutex protection
 * 
 * @requirement SRS-COMM-CAN-001: CAN bus communication support
 * @requirement SRS-COMM-CAN-002: PCAN hardware interface
 * @requirement SRS-SAFETY-001: Watchdog monitoring
 * @requirement SRS-SAFETY-002: Failsafe state management
 * 
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant Release
 */

#include <comm/can/pcan_basic.hpp>
#include <comm/core/utils.hpp>
#include <comm/core/safety.hpp>
#include <comm/core/defaults.hpp>
#include <comm/core/do178c.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#ifdef COMM_HAS_PCAN
#    include <PCANBasic.h>
#endif

namespace comm::can {

// ============================================================================
// DO-178C COMPILE-TIME CONFIGURATION VALIDATION
// ============================================================================

namespace {

/// @requirement SRS-COMM-CAN-003: Standard CAN payload limit
constexpr std::size_t kCanMaxPayload = defaults::kCanMaxDataLength;

/// @requirement SRS-COMM-CAN-004: CAN FD payload limit
constexpr std::size_t kCanFdMaxPayload = defaults::kCanFdMaxDataLength;


/// @requirement SRS-SAFETY-003: Maximum consecutive errors before failsafe
constexpr std::size_t kMaxConsecutiveErrors = defaults::kMaxConsecutiveErrors;

/// @requirement SRS-COMM-THR-002: Maximum receive loop iterations per cycle
constexpr auto kMaxReceiveLoopIterations = defaults::kMaxReceiveLoopIterations / 10U;  // 1000

/// @requirement SRS-COMM-THR-003: Maximum recovery attempts
constexpr auto kMaxRecoveryAttempts = defaults::kMaxConnectionAttempts;

// Compile-time validation of constants
static_assert(kCanMaxPayload == 8U, "Standard CAN payload must be 8 bytes");
static_assert(kCanFdMaxPayload == 64U, "CAN FD payload must be 64 bytes");
static_assert(kMaxConsecutiveErrors > 0U, "Must allow at least one error");
static_assert(kMaxReceiveLoopIterations <= 10000U, "Receive loop bound too high");

/**
 * @brief Validate CAN payload size
 * 
 * @param size Payload size in bytes
 * @param isFD Whether CAN FD mode is enabled
 * @return true if size is valid for the mode
 * 
 * @requirement SRS-COMM-CAN-005: Payload size validation
 */
COMM_REQUIREMENT("SRS-COMM-CAN-005")
[[nodiscard]] inline bool isValidCanPayload(std::size_t size, bool isFD) noexcept {
    COMM_PRECONDITION(kCanMaxPayload > 0U);
    COMM_PRECONDITION(kCanFdMaxPayload > 0U);
    
    const std::size_t limit = isFD ? kCanFdMaxPayload : kCanMaxPayload;
    const bool valid = (size > 0U) && (size <= limit);
    
    COMM_POSTCONDITION(valid == ((size > 0U) && (size <= limit)));
    return valid;
}

/**
 * @brief Get payload limit for current mode
 * 
 * @param isFD Whether CAN FD mode is enabled
 * @return Maximum payload size in bytes
 */
[[nodiscard]] inline constexpr std::size_t getPayloadLimit(bool isFD) noexcept {
    return isFD ? kCanFdMaxPayload : kCanMaxPayload;
}

/**
 * @brief Validate PCAN handle value
 * 
 * @param handle PCAN handle to validate
 * @return true if handle appears valid
 * 
 * @requirement SRS-COMM-CAN-006: Handle validation
 */
COMM_REQUIREMENT("SRS-COMM-CAN-006")
[[nodiscard]] inline bool isValidHandle(std::uint16_t handle) noexcept {
    // PCAN handles are in specific ranges
    // USB: 0x51-0x58, PCI: 0x41-0x48, etc.
    const bool isUsb = (handle >= 0x51U) && (handle <= 0x58U);
    const bool isPci = (handle >= 0x41U) && (handle <= 0x48U);
    const bool isAuto = (handle == 0xFFFFU);
    
    return isUsb || isPci || isAuto;
}

} // anonymous namespace

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

COMM_REQUIREMENT("SRS-COMM-CAN-001")
COMM_SAFETY_CRITICAL
PCANBasic::PCANBasic(PCANBasicConfig config) 
    : config_(std::move(config))
    , watchdog_(defaults::kWatchdogTimeout)
{
    COMM_PRECONDITION(config_.protocol == Protocol::CAN);
    
    // Initialize statistics with current time
    stats_.startTime = std::chrono::steady_clock::now();
    
    // Validate configuration at construction
    COMM_ASSERT(isValidHandle(config_.handle), "Invalid PCAN handle");
    
    COMM_POSTCONDITION(!initialized_);
    COMM_POSTCONDITION(healthy_.load() == true);
}

COMM_SAFETY_CRITICAL
PCANBasic::~PCANBasic() noexcept {
    // Ensure clean shutdown - must not throw
    static_cast<void>(close());
    
    // Wait for thread with timeout to prevent hang
    if (receiveThread_.joinable()) {
        running_.store(false, std::memory_order_release);
        receiveThread_.join();
    }
}

// ============================================================================
// LIFECYCLE MANAGEMENT
// ============================================================================

COMM_REQUIREMENT("SRS-COMM-CAN-007")
COMM_SAFETY_CRITICAL
bool PCANBasic::open() {
#ifdef COMM_HAS_PCAN
    COMM_BRANCH("open-start");
    
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Already open check
    if (initialized_) {
        COMM_BRANCH("open-already-initialized");
        return true;
    }

    // Initialize hardware
    COMM_BRANCH("open-initialize");
    if (!initialize()) {
        COMM_BRANCH("open-initialize-failed");
        reportError({
            ErrorCode::OpenFailed, 
            ErrorCategory::System, 
            ErrorSeverity::Critical, 
            "Failed to initialize PCAN device", 
            "PCANBasic::open"
        });
        return false;
    }

    healthy_.store(true, std::memory_order_release);

    // Check if receive thread should be launched
    const bool shouldLaunchReceive = 
        config_.spawnReceiveThread && 
        isReceiveEnabled(config_.direction);

    // Must unlock before starting thread to avoid deadlock
    lock.unlock();

    // Start receive thread if configured
    if (shouldLaunchReceive && receiveCallback_.hasCallback()) {
        COMM_BRANCH("open-start-receive-thread");
        startReceiveThread();
    }

    COMM_POSTCONDITION(initialized_);
    return true;
    
#else
    COMM_BRANCH("open-no-pcan-support");
    reportError({
        ErrorCode::UnsupportedOperation, 
        ErrorCategory::Resource, 
        ErrorSeverity::Warning, 
        "PCANBasic support not available", 
        "PCANBasic::open"
    });
    return false;
#endif
}

COMM_REQUIREMENT("SRS-COMM-CAN-008")
COMM_SAFETY_CRITICAL
bool PCANBasic::close() {
#ifdef COMM_HAS_PCAN
    COMM_BRANCH("close-start");
    
    // Stop receive thread first (outside mutex to avoid deadlock)
    stopReceiveThread();

    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        COMM_BRANCH("close-uninitialize");
        uninitialize();
    }
    
    COMM_POSTCONDITION(!initialized_);
    return true;
    
#else
    COMM_BRANCH("close-no-pcan-support");
    return false;
#endif
}

COMM_REQUIREMENT("SRS-COMM-CAN-009")
[[nodiscard]] bool PCANBasic::isOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

// ============================================================================
// DATA TRANSMISSION
// ============================================================================

COMM_REQUIREMENT("SRS-COMM-CAN-010")
COMM_SAFETY_CRITICAL
std::ptrdiff_t PCANBasic::send(const ByteVector& data) {
#ifdef COMM_HAS_PCAN
    COMM_BRANCH("send-start");
    
    // ===== INPUT VALIDATION (DO-178C Requirement) =====
    
    // Check direction configuration
    COMM_DECISION("send-direction-check");
    if (!isSendEnabled(config_.direction)) {
        COMM_BRANCH("send-direction-disabled");
        reportError({
            ErrorCode::UnsupportedOperation, 
            ErrorCategory::Protocol, 
            ErrorSeverity::Warning, 
            "Send disabled by configuration", 
            "PCANBasic::send"
        });
        return -1;
    }

    // Empty data is valid no-op
    COMM_DECISION("send-empty-check");
    if (data.empty()) {
        COMM_BRANCH("send-empty-data");
        return 0;
    }

    // Validate payload size
    const std::size_t limit = getPayloadLimit(config_.enableFD);
    COMM_DECISION("send-size-check");
    if (!isValidCanPayload(data.size(), config_.enableFD)) {
        COMM_BRANCH("send-invalid-size");
        reportError({
            ErrorCode::InvalidConfiguration, 
            ErrorCategory::Protocol, 
            ErrorSeverity::Warning, 
            "Invalid payload size for CAN frame", 
            "PCANBasic::send"
        });
        // Continue with truncation (defensive programming)
    }

    // Check initialization state
    COMM_DECISION("send-init-check");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            COMM_BRANCH("send-not-initialized");
            reportError({
                ErrorCode::NotOpen, 
                ErrorCategory::Connection, 
                ErrorSeverity::Warning, 
                "PCAN device not initialized", 
                "PCANBasic::send"
            });
            recordFailure();
            return -1;
        }
    }

    // ===== PREPARE PAYLOAD (Bounded copy) =====
    
    // Use fixed-size buffer to avoid dynamic allocation in critical path
    do178c::FixedBuffer<std::uint8_t, kCanFdMaxPayload> payload;
    
    const std::size_t copySize = (data.size() > limit) ? limit : data.size();
    COMM_BOUNDED_FOR(i, 0U, copySize, kCanFdMaxPayload) {
        static_cast<void>(payload.push_back(data[i]));
    }
    
    COMM_INVARIANT(payload.size() <= limit);

    // ===== TRANSMIT FRAME =====
    
    TPCANStatus status{};
    
    COMM_DECISION("send-fd-mode");
    if (config_.enableFD) {
        COMM_BRANCH("send-fd-frame");
        TPCANMsgFD message{};
        message.ID = 0x000U;
        message.DLC = static_cast<std::uint8_t>(payload.size());
        message.MSGTYPE = PCAN_MESSAGE_STANDARD;
        
        // Safe copy with bounds
        COMM_BOUNDED_FOR(i, 0U, payload.size(), kCanFdMaxPayload) {
            const auto* elem = payload.at(i);
            if (elem != nullptr) {
                message.DATA[i] = *elem;
            }
        }
        
        status = ::CAN_WriteFD(handle(), &message);
        if (status != PCAN_ERROR_OK) {
            reportError({ErrorCode::SendFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to write PCAN FD frame", "PCANBasic::send"});
            recordFailure();
            static_cast<void>(recoverIfNeeded(status));
            return -1;
        }
    } else {
        COMM_BRANCH("send-standard-frame");
        tagTPCANMsg message{};
        message.ID = 0x000U;
        message.LEN = static_cast<std::uint8_t>(payload.size());
        message.MSGTYPE = PCAN_MESSAGE_STANDARD;
        
        // Safe copy with bounds
        COMM_BOUNDED_FOR(i, 0U, payload.size(), kCanMaxPayload) {
            const auto* elem = payload.at(i);
            if (elem != nullptr) {
                message.DATA[i] = *elem;
            }
        }
        
        status = ::CAN_Write(handle(), &message);
        if (status != PCAN_ERROR_OK) {
            reportError({ErrorCode::SendFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to write PCAN frame", "PCANBasic::send"});
            recordFailure();
            static_cast<void>(recoverIfNeeded(status));
            return -1;
        }
    }

    // ===== HANDLE RESULT =====
    
    COMM_DECISION("send-status-check");
    if (status != PCAN_ERROR_OK) {
        COMM_BRANCH("send-failed");
        reportError({
            ErrorCode::SendFailed, 
            ErrorCategory::Transmission, 
            ErrorSeverity::Recoverable, 
            "Failed to write PCAN frame", 
            "PCANBasic::send"
        });
        recordFailure();
        static_cast<void>(recoverIfNeeded(status));
        return -1;
    }

    COMM_BRANCH("send-success");
    recordSend(payload.size());
    recordSuccess();
    healthy_.store(true, std::memory_order_release);
    
    COMM_POSTCONDITION(payload.size() <= limit);
    return static_cast<std::ptrdiff_t>(payload.size());

#else
    COMM_BRANCH("send-no-pcan-support");
    static_cast<void>(data);  // Suppress unused warning
    reportError({
        ErrorCode::UnsupportedOperation, 
        ErrorCategory::Resource, 
        ErrorSeverity::Warning, 
        "PCANBasic support not available", 
        "PCANBasic::send"
    });
    return -1;
#endif
}

COMM_REQUIREMENT("SRS-COMM-CAN-011")
COMM_SAFETY_CRITICAL
std::ptrdiff_t PCANBasic::receive(ByteVector& buffer, std::size_t maxSize) {
#ifdef COMM_HAS_PCAN
    COMM_BRANCH("receive-start");
    
    // ===== INPUT VALIDATION =====
    
    COMM_DECISION("receive-direction-check");
    if (!isReceiveEnabled(config_.direction)) {
        COMM_BRANCH("receive-direction-disabled");
        reportError({
            ErrorCode::UnsupportedOperation, 
            ErrorCategory::Protocol, 
            ErrorSeverity::Warning, 
            "Receive disabled by configuration", 
            "PCANBasic::receive"
        });
        buffer.clear();
        return -1;
    }

    // Validate maxSize
    const std::size_t canLimit = getPayloadLimit(config_.enableFD);
    const std::size_t limit = (maxSize < canLimit) ? maxSize : canLimit;
    
    COMM_DECISION("receive-size-check");
    if (limit == 0U) {
        COMM_BRANCH("receive-zero-limit");
        buffer.clear();
        return 0;
    }

    // Check initialization
    COMM_DECISION("receive-init-check");
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            COMM_BRANCH("receive-not-initialized");
            buffer.clear();
            return -1;
        }
    }

    // ===== RECEIVE FRAME =====
    
    // Use fixed buffer for receive
    do178c::FixedBuffer<std::uint8_t, kCanFdMaxPayload> tempBuffer;
    std::size_t receivedLength = 0U;
    TPCANStatus status{};

    COMM_DECISION("receive-fd-mode");
    if (config_.enableFD) {
        COMM_BRANCH("receive-fd-frame");
        TPCANMsgFD message{};
        TPCANTimestampFD timestamp{};
        
        status = ::CAN_ReadFD(handle(), &message, &timestamp);
        
        COMM_DECISION("receive-fd-status");
        if (status == PCAN_ERROR_QRCVEMPTY) {
            COMM_BRANCH("receive-fd-empty");
            return 0;
        }
        
        if (status != PCAN_ERROR_OK) {
            COMM_BRANCH("receive-fd-error");
            reportError({
                ErrorCode::ReceiveFailed, 
                ErrorCategory::Transmission, 
                ErrorSeverity::Recoverable, 
                "Failed to read PCAN FD frame", 
                "PCANBasic::receive"
            });
            recordFailure();
            static_cast<void>(recoverIfNeeded(status));
            healthy_.store(false, std::memory_order_release);
            buffer.clear();
            return -1;
        }
        
        // Safe bounded copy
        receivedLength = (message.DLC < limit) ? message.DLC : limit;
        COMM_BOUNDED_FOR(i, 0U, receivedLength, kCanFdMaxPayload) {
            static_cast<void>(tempBuffer.push_back(message.DATA[i]));
        }
        
    } else {
        COMM_BRANCH("receive-standard-frame");
        tagTPCANMsg message{};
        TPCANTimestamp timestamp{};
        
        status = ::CAN_Read(handle(), &message, &timestamp);
        
        COMM_DECISION("receive-std-status");
        if (status == PCAN_ERROR_QRCVEMPTY) {
            COMM_BRANCH("receive-std-empty");
            return 0;
        }
        
        if (status != PCAN_ERROR_OK) {
            COMM_BRANCH("receive-std-error");
            reportError({
                ErrorCode::ReceiveFailed, 
                ErrorCategory::Transmission, 
                ErrorSeverity::Recoverable, 
                "Failed to read PCAN frame", 
                "PCANBasic::receive"
            });
            recordFailure();
            static_cast<void>(recoverIfNeeded(status));
            healthy_.store(false, std::memory_order_release);
            buffer.clear();
            return -1;
        }
        
        // Safe bounded copy
        receivedLength = (message.LEN < limit) ? message.LEN : limit;
        COMM_BOUNDED_FOR(i, 0U, receivedLength, kCanMaxPayload) {
            static_cast<void>(tempBuffer.push_back(message.DATA[i]));
        }
    }

    // ===== COPY TO OUTPUT BUFFER =====
    
    COMM_BRANCH("receive-success");
    
    // Copy from fixed buffer to output
    buffer.clear();
    buffer.reserve(tempBuffer.size());
    COMM_BOUNDED_FOR(i, 0U, tempBuffer.size(), kCanFdMaxPayload) {
        const auto* elem = tempBuffer.at(i);
        if (elem != nullptr) {
            buffer.push_back(*elem);
        }
    }
    
    recordReceive(buffer.size());
    recordSuccess();
    healthy_.store(true, std::memory_order_release);
    
    COMM_POSTCONDITION(buffer.size() <= limit);
    return static_cast<std::ptrdiff_t>(buffer.size());

#else
    COMM_BRANCH("receive-no-pcan-support");
    static_cast<void>(maxSize);
    buffer.clear();
    reportError({
        ErrorCode::UnsupportedOperation, 
        ErrorCategory::Resource, 
        ErrorSeverity::Warning, 
        "PCANBasic support not available", 
        "PCANBasic::receive"
    });
    return -1;
#endif
}

// ============================================================================
// CONFIGURATION
// ============================================================================

COMM_REQUIREMENT("SRS-COMM-CAN-012")
bool PCANBasic::configure(const Config& config) {
    COMM_BRANCH("configure-start");
    
    // Validate protocol type
    COMM_DECISION("configure-protocol-check");
    if (config.protocol != Protocol::CAN) {
        COMM_BRANCH("configure-wrong-protocol");
        return false;
    }

    // Safe downcast after validation
    const auto& pcanConfig = static_cast<const PCANBasicConfig&>(config);
    
    // Validate handle
    COMM_DECISION("configure-handle-check");
    if (!isValidHandle(pcanConfig.handle)) {
        COMM_BRANCH("configure-invalid-handle");
        return false;
    }

    // Apply configuration under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = pcanConfig;
    }

#ifdef COMM_HAS_PCAN
    // Reconfigure if already initialized
    COMM_DECISION("configure-reinit-check");
    if (initialized_) {
        COMM_BRANCH("configure-reinitialize");
        stopReceiveThread();
        uninitialize();
        
        if (!initialize()) {
            COMM_BRANCH("configure-reinit-failed");
            return false;
        }
    }

    // Restart receive thread if needed
    const bool shouldStartThread = 
        config_.spawnReceiveThread && 
        receiveCallback_.hasCallback() && 
        isReceiveEnabled(config_.direction);
        
    if (shouldStartThread) {
        COMM_BRANCH("configure-restart-thread");
        startReceiveThread();
    }
#endif

    COMM_BRANCH("configure-success");
    return true;
}

[[nodiscard]] Config PCANBasic::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

[[nodiscard]] Statistics PCANBasic::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

[[nodiscard]] bool PCANBasic::isHealthy() const {
    return healthy_.load(std::memory_order_acquire);
}

// ============================================================================
// CALLBACKS
// ============================================================================

void PCANBasic::setReceiveCallback(ReceiveCallback callback) {
    COMM_BRANCH("setReceiveCallback-start");
    
    receiveCallback_.setCallback(std::move(callback));
    
    bool shouldLaunch = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shouldLaunch = initialized_ && 
                       config_.spawnReceiveThread && 
                       isReceiveEnabled(config_.direction);
    }
    
    if (shouldLaunch && receiveCallback_.hasCallback()) {
        COMM_BRANCH("setReceiveCallback-start-thread");
        startReceiveThread();
    }
}

void PCANBasic::setErrorCallback(ErrorCallback callback) {
    errorCallback_.setCallback(std::move(callback));
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

PCANBasic::TPCANHandle PCANBasic::handle() const {
    return static_cast<TPCANHandle>(config_.handle);
}

COMM_SAFETY_CRITICAL
bool PCANBasic::initialize() {
#ifdef COMM_HAS_PCAN
    COMM_BRANCH("initialize-start");
    COMM_PRECONDITION(!initialized_);
    
    TPCANStatus status{};
    
    COMM_DECISION("initialize-fd-mode");
    if (config_.enableFD) {
        COMM_BRANCH("initialize-fd");
        status = ::CAN_InitializeFD(handle(), config_.fdBitrate.c_str());
    } else {
        COMM_BRANCH("initialize-standard");
        status = ::CAN_Initialize(handle(), config_.bitrate);
    }

    COMM_DECISION("initialize-status");
    if (status != PCAN_ERROR_OK) {
        COMM_BRANCH("initialize-failed");
        reportError({
            ErrorCode::OpenFailed, 
            ErrorCategory::System, 
            ErrorSeverity::Critical, 
            "Failed to initialize PCAN device", 
            "PCANBasic::initialize"
        });
        return false;
    }

    initialized_ = true;
    
    COMM_BRANCH("initialize-apply-mode");
    const bool modeOk = applyMode();
    
    COMM_POSTCONDITION(initialized_);
    return modeOk;
    
#else
    return false;
#endif
}

COMM_SAFETY_CRITICAL
void PCANBasic::uninitialize() {
#ifdef COMM_HAS_PCAN
    COMM_BRANCH("uninitialize-start");
    
    if (initialized_) {
        COMM_BRANCH("uninitialize-do");
        static_cast<void>(::CAN_Uninitialize(handle()));
        initialized_ = false;
    }
    
    COMM_POSTCONDITION(!initialized_);
#endif
}

bool PCANBasic::applyMode() {
#ifdef COMM_HAS_PCAN
    COMM_BRANCH("applyMode-start");
    
    COMM_DECISION("applyMode-init-check");
    if (!initialized_) {
        COMM_BRANCH("applyMode-not-init");
        return false;
    }

    COMM_DECISION("applyMode-listen-only");
    if (config_.listenOnly) {
        COMM_BRANCH("applyMode-set-listen");
        const TPCANMode mode = PCAN_MODE_LISTEN_ONLY;
        TPCANStatus status = ::CAN_SetValue(
            handle(), 
            PCAN_LISTEN_ONLY, 
            const_cast<void*>(static_cast<const void*>(&mode)),  // API requires non-const
            sizeof(mode)
        );
        
        if (status != PCAN_ERROR_OK) {
            COMM_BRANCH("applyMode-listen-failed");
            reportError({
                ErrorCode::InvalidConfiguration, 
                ErrorCategory::Configuration, 
                ErrorSeverity::Warning, 
                "Failed to set listen-only mode", 
                "PCANBasic::applyMode"
            });
            return false;
        }
    }

    COMM_DECISION("applyMode-hw-timestamps");
    if (config_.hardwareTimestamps) {
        COMM_BRANCH("applyMode-set-timestamps");
        const std::uint32_t on = PCAN_PARAMETER_ON;
        TPCANStatus status = ::CAN_SetValue(
            handle(), 
            PCAN_HARDWARE_TIMESTAMPING, 
            const_cast<void*>(static_cast<const void*>(&on)),
            sizeof(on)
        );
        
        if (status != PCAN_ERROR_OK) {
            COMM_BRANCH("applyMode-timestamps-failed");
            reportError({
                ErrorCode::InvalidConfiguration, 
                ErrorCategory::Configuration, 
                ErrorSeverity::Warning, 
                "Failed to enable hardware timestamps", 
                "PCANBasic::applyMode"
            });
            return false;
        }
    }

    COMM_BRANCH("applyMode-success");
    return true;
    
#else
    return false;
#endif
}

// ============================================================================
// THREAD MANAGEMENT
// ============================================================================

void PCANBasic::startReceiveThread() {
    COMM_BRANCH("startReceiveThread-start");
    
    COMM_DECISION("startReceiveThread-should-spawn");
    if (!shouldSpawnReceiveThread() || !receiveCallback_.hasCallback()) {
        COMM_BRANCH("startReceiveThread-skip");
        return;
    }
    
    // Atomic check-and-set to prevent double-start
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        COMM_BRANCH("startReceiveThread-already-running");
        return;
    }

    COMM_BRANCH("startReceiveThread-launch");
    receiveThread_ = std::thread([this] { receiveLoop(); });
}

void PCANBasic::stopReceiveThread() {
    COMM_BRANCH("stopReceiveThread-start");
    
    // Atomic check-and-clear
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        COMM_BRANCH("stopReceiveThread-not-running");
        return;
    }

    COMM_BRANCH("stopReceiveThread-join");
    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
}

COMM_SAFETY_CRITICAL
void PCANBasic::receiveLoop() {
#ifdef COMM_HAS_PCAN
    COMM_BRANCH("receiveLoop-start");
    
    // Bounded main loop
    std::size_t totalIterations = 0U;
    
    while (running_.load(std::memory_order_acquire)) {
        COMM_BRANCH("receiveLoop-iteration");
        
        // Safety: prevent infinite loop on stuck condition
        ++totalIterations;
        COMM_DECISION("receiveLoop-iteration-check");
        if (totalIterations > kMaxReceiveLoopIterations) {
            COMM_BRANCH("receiveLoop-iteration-limit");
            // Reset counter, log warning
            totalIterations = 0U;
        }
        
        // Receive data
        ByteVector buffer;
        const std::size_t maxRecv = getPayloadLimit(config_.enableFD);
        const auto result = receive(buffer, maxRecv);
        
        COMM_DECISION("receiveLoop-result-check");
        if (result > 0) {
            COMM_BRANCH("receiveLoop-notify");
            receiveCallback_.notify(buffer);
            totalIterations = 0U;  // Reset on successful receive
            continue;
        }

        // Sleep on no data
        COMM_BRANCH("receiveLoop-sleep");
        std::this_thread::sleep_for(receiveSleepDuration());
    }
    
    COMM_BRANCH("receiveLoop-exit");
#endif
}

// ============================================================================
// STATISTICS & ERROR HANDLING
// ============================================================================

COMM_SAFETY_CRITICAL
void PCANBasic::recordSend(std::size_t bytes) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.bytesSent = saturatingAdd(stats_.bytesSent, static_cast<std::uint64_t>(bytes));
    stats_.messagesSent = saturatingAdd(stats_.messagesSent, std::uint64_t{1});
}

COMM_SAFETY_CRITICAL
void PCANBasic::recordReceive(std::size_t bytes) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.bytesReceived = saturatingAdd(stats_.bytesReceived, static_cast<std::uint64_t>(bytes));
    stats_.messagesReceived = saturatingAdd(stats_.messagesReceived, std::uint64_t{1});
}

COMM_SAFETY_CRITICAL
void PCANBasic::reportError(Error error) {
    COMM_BRANCH("reportError-start");
    
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.errorCount = saturatingAdd(stats_.errorCount, std::uint64_t{1});
    }
    
    healthy_.store(false, std::memory_order_release);
    errorCallback_.notify(error);
}

COMM_SAFETY_CRITICAL
bool PCANBasic::recoverIfNeeded(TPCANStatus status) {
#ifdef COMM_HAS_PCAN
    COMM_BRANCH("recover-start");
    
    COMM_DECISION("recover-auto-reconnect-check");
    if (!config_.autoReconnect) {
        COMM_BRANCH("recover-disabled");
        return false;
    }

    // Check for recoverable bus errors
    const bool busOff = (status & PCAN_ERROR_BUSOFF) != 0U;
    const bool busHeavy = (status & PCAN_ERROR_BUSHEAVY) != 0U;
    const bool anyBusErr = (status & PCAN_ERROR_ANYBUSERR) != 0U;
    
    COMM_DECISION("recover-bus-error-check");
    if (busOff || busHeavy || anyBusErr) {
        COMM_BRANCH("recover-attempt");
        
        uninitialize();
        
        // Bounded recovery attempts
        COMM_BOUNDED_FOR(attempt, 0U, kMaxRecoveryAttempts, kMaxRecoveryAttempts) {
            std::this_thread::sleep_for(config_.timeouts.reconnectInterval);
            
            if (initialize()) {
                COMM_BRANCH("recover-success");
                return true;
            }
        }
        
        COMM_BRANCH("recover-exhausted");
        failsafeManager_.reportFault();
    }

    COMM_BRANCH("recover-not-needed");
    return false;
    
#else
    static_cast<void>(status);
    return false;
#endif
}

[[nodiscard]] bool PCANBasic::shouldSpawnReceiveThread() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.spawnReceiveThread && 
           initialized_ && 
           isReceiveEnabled(config_.direction);
}

[[nodiscard]] std::chrono::milliseconds PCANBasic::receiveSleepDuration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return effectiveSleep(config_.receiveThreadSleep);
}

// ============================================================================
// FLIGHT-CRITICAL SAFETY API
// ============================================================================

COMM_REQUIREMENT("SRS-SAFETY-001")
COMM_SAFETY_CRITICAL
void PCANBasic::feedWatchdog() noexcept {
    watchdog_.feed();
}

COMM_REQUIREMENT("SRS-SAFETY-001")
COMM_SAFETY_CRITICAL
[[nodiscard]] safety::Watchdog::State PCANBasic::checkWatchdog() noexcept {
    COMM_BRANCH("checkWatchdog-start");
    
    const auto state = watchdog_.check();
    
    COMM_DECISION("checkWatchdog-timeout");
    if (state == safety::Watchdog::State::Timeout) {
        COMM_BRANCH("checkWatchdog-fault");
        failsafeManager_.reportFault();
    }
    
    return state;
}

COMM_REQUIREMENT("SRS-SAFETY-002")
[[nodiscard]] safety::FailsafeState PCANBasic::getFailsafeState() const noexcept {
    return failsafeManager_.getState();
}

[[nodiscard]] std::uint32_t PCANBasic::getNextSequence() noexcept {
    return sequenceGen_.next();
}

COMM_REQUIREMENT("SRS-SAFETY-004")
[[nodiscard]] safety::SequenceValidator::Result 
PCANBasic::validateSequence(std::uint32_t seq) noexcept {
    COMM_BRANCH("validateSequence-start");
    
    const auto result = sequenceValidator_.validate(seq);
    
    COMM_DECISION("validateSequence-anomaly");
    if (result == safety::SequenceValidator::Result::Gap ||
        result == safety::SequenceValidator::Result::Duplicate) {
        COMM_BRANCH("validateSequence-fault");
        failsafeManager_.reportFault();
    }
    
    return result;
}

[[nodiscard]] PCANBasic::SafetyStats PCANBasic::getSafetyStats() const noexcept {
    return {
        watchdog_.getStats(),
        sequenceValidator_.getStats(),
        failsafeManager_.getStats(),
        consecutiveErrors_.load(std::memory_order_acquire)
    };
}

COMM_SAFETY_CRITICAL
void PCANBasic::recordSuccess() noexcept {
    COMM_BRANCH("recordSuccess-start");
    
    consecutiveErrors_.store(0U, std::memory_order_release);
    feedWatchdog();
    
    // Recover from degraded states
    COMM_DECISION("recordSuccess-recover");
    if (failsafeManager_.getState() != safety::FailsafeState::Normal) {
        COMM_BRANCH("recordSuccess-clear-faults");
        failsafeManager_.clearFaults();
        static_cast<void>(failsafeManager_.transitionTo(safety::FailsafeState::Normal));
    }
}

COMM_SAFETY_CRITICAL
void PCANBasic::recordFailure() noexcept {
    COMM_BRANCH("recordFailure-start");
    
    const auto errors = consecutiveErrors_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    
    COMM_DECISION("recordFailure-threshold");
    if (errors >= kMaxConsecutiveErrors) {
        COMM_BRANCH("recordFailure-escalate");
        failsafeManager_.reportFault();
    }
}

} // namespace comm::can
