/**
 * @file socketcan.cpp
 * @brief DO-178C Compliant SocketCAN Transport Implementation
 * 
 * @requirement SRS-COMM-CAN-001: Linux SocketCAN interface support
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#include <comm/can/socketcan.hpp>
#include <comm/core/utils.hpp>
#include <comm/core/defaults.hpp>
#include <comm/core/do178c.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>

namespace comm::can {

namespace {




/**
 * @brief Get payload limit for CAN mode
 * @requirement SRS-COMM-CAN-005: Payload size limits
 */
COMM_REQUIREMENT("SRS-COMM-CAN-005")
[[nodiscard]] inline std::size_t payloadLimit(const SocketCANConfig& config) noexcept {
    return config.enableFD ? CANFD_MAX_DLEN : CAN_MAX_DLEN;
}


} // namespace

SocketCAN::SocketCAN(SocketCANConfig config) : config_(std::move(config)) {
    stats_.startTime = std::chrono::steady_clock::now();
}

SocketCAN::~SocketCAN() {
    close();
    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
}

bool SocketCAN::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (socket_ >= 0) {
        return true;
    }

    socket_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (socket_ < 0) {
        reportError({ErrorCode::OpenFailed, ErrorCategory::System, ErrorSeverity::Critical, "Failed to create CAN socket", std::strerror(errno), errno});
        return false;
    }

    ifreq ifr{};
    std::strncpy(ifr.ifr_name, config_.interface.c_str(), IFNAMSIZ - 1);
    if (::ioctl(socket_, SIOCGIFINDEX, &ifr) < 0) {
        reportError({ErrorCode::OpenFailed, ErrorCategory::System, ErrorSeverity::Critical, "Cannot resolve CAN interface", config_.interface, errno});
        ::close(socket_);
        socket_ = -1;
        return false;
    }

    sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (::bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        reportError({ErrorCode::OpenFailed, ErrorCategory::System, ErrorSeverity::Critical, "Failed to bind CAN socket", std::strerror(errno), errno});
        ::close(socket_);
        socket_ = -1;
        return false;
    }

    int loopback = config_.loopback ? 1 : 0;
    ::setsockopt(socket_, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &loopback, sizeof(loopback));

    int recvOwn = config_.receiveOwnMessages ? 1 : 0;
    ::setsockopt(socket_, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recvOwn, sizeof(recvOwn));

    if (config_.timeouts.receiveTimeout.count() > 0) {
        timeval tv{};
        auto ms = config_.timeouts.receiveTimeout;
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(ms);
        auto usec = std::chrono::duration_cast<std::chrono::microseconds>(ms - sec);
        tv.tv_sec = static_cast<long>(sec.count());
        tv.tv_usec = static_cast<long>(usec.count());
        ::setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    if (config_.timeouts.sendTimeout.count() > 0) {
        timeval tv{};
        auto ms = config_.timeouts.sendTimeout;
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(ms);
        auto usec = std::chrono::duration_cast<std::chrono::microseconds>(ms - sec);
        tv.tv_sec = static_cast<long>(sec.count());
        tv.tv_usec = static_cast<long>(usec.count());
        ::setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

#ifdef CAN_RAW_FD_FRAMES
    if (config_.enableFD) {
        int enableFD = 1;
        ::setsockopt(socket_, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enableFD, sizeof(enableFD));
    }
#endif

    if (!config_.filters.empty()) {
        if (!applyFilters()) {
            ::close(socket_);
            socket_ = -1;
            return false;
        }
    }

    healthy_.store(true);

    if (isReceiveEnabled(config_.direction) && receiveCallback_.hasCallback() && config_.spawnReceiveThread) {
        startReceiveLoop();
    }

    return true;
}

bool SocketCAN::close() {
    stopReceiveLoop();

    std::lock_guard<std::mutex> lock(mutex_);
    if (socket_ >= 0) {
        ::close(socket_);
        socket_ = -1;
    }
    return true;
}

bool SocketCAN::isOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return socket_ >= 0;
}

std::ptrdiff_t SocketCAN::send(const ByteVector& data) {
    SocketCANConfig configSnapshot;
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        configSnapshot = config_;
        fd = socket_;
    }

    if (!isSendEnabled(configSnapshot.direction)) {
        reportError({ErrorCode::UnsupportedOperation, ErrorCategory::Protocol, ErrorSeverity::Warning, "Send disabled by configuration", "SocketCAN::send"});
        return -1;
    }

    if (fd < 0) {
        reportError({ErrorCode::NotOpen, ErrorCategory::Connection, ErrorSeverity::Warning, "CAN socket not open", "SocketCAN::send"});
        return -1;
    }

    if (data.empty()) {
        return 0;
    }

    const auto limit = payloadLimit(configSnapshot);
    const auto length = std::min<std::size_t>(data.size(), limit);

    if (configSnapshot.enableFD) {
#ifdef CANFD_MAX_DLEN
        canfd_frame frame{};
        frame.can_id = 0x000;
        frame.len = static_cast<std::uint8_t>(length);
        std::copy_n(data.begin(), length, frame.data);

        auto written = ::write(fd, &frame, sizeof(frame));
        if (written < 0) {
            reportError({ErrorCode::SendFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to write CAN FD frame", std::strerror(errno)});
            return -1;
        }

        recordSend(length);
        return length;
#else
        reportError({ErrorCode::UnsupportedOperation, ErrorCategory::Protocol, ErrorSeverity::Warning, "CAN FD not supported on this platform", "SocketCAN::send"});
        return -1;
#endif
    }

    can_frame frame{};
    frame.can_id = 0x000;
    frame.can_dlc = static_cast<__u8>(length);
    std::copy_n(data.begin(), length, frame.data);

    auto written = ::write(fd, &frame, sizeof(frame));
    if (written < 0) {
        reportError({ErrorCode::SendFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to write CAN frame", std::strerror(errno), errno});
        return -1;
    }

    recordSend(length);
    return length;
}

std::ptrdiff_t SocketCAN::receive(ByteVector& buffer, std::size_t maxSize) {
    SocketCANConfig configSnapshot;
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        configSnapshot = config_;
        fd = socket_;
    }

    if (!isReceiveEnabled(configSnapshot.direction)) {
        reportError({ErrorCode::UnsupportedOperation, ErrorCategory::Protocol, ErrorSeverity::Warning, "Receive disabled by configuration", "SocketCAN::receive"});
        return -1;
    }

    if (fd < 0) {
        reportError({ErrorCode::NotOpen, ErrorCategory::Connection, ErrorSeverity::Warning, "CAN socket not open", "SocketCAN::receive"});
        return -1;
    }

    buffer.clear();

    if (configSnapshot.enableFD) {
#ifdef CANFD_MAX_DLEN
        canfd_frame frame{};
        auto bytes = ::read(fd, &frame, sizeof(frame));
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            reportError({ErrorCode::ReceiveFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to read CAN FD frame", std::strerror(errno)});
            healthy_.store(false);
            return -1;
        }

        auto length = std::min<std::size_t>(frame.len, maxSize);
        buffer.assign(frame.data, frame.data + length);
        recordReceive(buffer.size());
        healthy_.store(true);
        return static_cast<std::ptrdiff_t>(buffer.size());
#else
        reportError({ErrorCode::UnsupportedOperation, ErrorCategory::Protocol, ErrorSeverity::Warning, "CAN FD not supported on this platform", "SocketCAN::receive"});
        return -1;
#endif
    }

    can_frame frame{};
    auto bytes = ::read(fd, &frame, sizeof(frame));
    if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        reportError({ErrorCode::ReceiveFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to read CAN frame", std::strerror(errno), errno});
        healthy_.store(false);
        return -1;
    }

    auto length = std::min<std::size_t>(frame.can_dlc, maxSize);
    buffer.assign(frame.data, frame.data + length);
    recordReceive(buffer.size());
    healthy_.store(true);
    return static_cast<std::ptrdiff_t>(buffer.size());
}

bool SocketCAN::configure(const Config& config) {
    if (config.protocol != Protocol::CAN) {
        return false;
    }

    const auto& canConfig = static_cast<const SocketCANConfig&>(config);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = canConfig;
    }

    if (isOpen() && !config_.filters.empty()) {
        return applyFilters();
    }

    return true;
}

Config SocketCAN::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

Statistics SocketCAN::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

bool SocketCAN::isHealthy() const {
    return healthy_.load();
}

void SocketCAN::setReceiveCallback(ReceiveCallback callback) {
    receiveCallback_.setCallback(std::move(callback));
    if (isOpen() && isReceiveEnabled(config_.direction) && config_.spawnReceiveThread) {
        startReceiveLoop();
    }
}

void SocketCAN::setErrorCallback(ErrorCallback callback) {
    errorCallback_.setCallback(std::move(callback));
}

std::optional<CANFrame> SocketCAN::readFrame() {
    CANFrame result;
    SocketCANConfig configSnapshot;
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        configSnapshot = config_;
        fd = socket_;
    }

    if (fd < 0) {
        return std::nullopt;
    }

    if (configSnapshot.enableFD) {
#ifdef CANFD_MAX_DLEN
        canfd_frame frame{};
        auto bytes = ::read(fd, &frame, sizeof(frame));
        if (bytes < 0) {
            return std::nullopt;
        }

        result.id = frame.can_id & CAN_EFF_MASK;
        result.extended = (frame.can_id & CAN_EFF_FLAG) != 0;
        result.fdFrame = true;
        result.dlc = frame.len;
        result.data.assign(frame.data, frame.data + frame.len);
        return result;
#else
        return std::nullopt;
#endif
    }

    can_frame frame{};
    auto bytes = ::read(fd, &frame, sizeof(frame));
    if (bytes < 0) {
        return std::nullopt;
    }

    result.id = frame.can_id & CAN_EFF_MASK;
    result.extended = (frame.can_id & CAN_EFF_FLAG) != 0;
    result.errorFrame = (frame.can_id & CAN_ERR_FLAG) != 0;
    result.dlc = frame.can_dlc;
    result.data.assign(frame.data, frame.data + frame.can_dlc);
    return result;
}

bool SocketCAN::writeFrame(const CANFrame& frame) {
    SocketCANConfig configSnapshot;
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        configSnapshot = config_;
        fd = socket_;
    }

    if (fd < 0) {
        return false;
    }

    if (frame.fdFrame && configSnapshot.enableFD) {
#ifdef CANFD_MAX_DLEN
        canfd_frame native{};
        native.can_id = frame.id & CAN_EFF_MASK;
        if (frame.extended) {
            native.can_id |= CAN_EFF_FLAG;
        }
        native.len = std::min<std::uint8_t>(frame.data.size(), CANFD_MAX_DLEN);
        std::copy_n(frame.data.begin(), native.len, native.data);
        auto written = ::write(fd, &native, sizeof(native));
        if (written < 0) {
            reportError({ErrorCode::SendFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to write CAN FD frame", std::strerror(errno)});
            return false;
        }
        recordSend(native.len);
        return true;
#else
        reportError({ErrorCode::UnsupportedOperation, ErrorCategory::Protocol, ErrorSeverity::Warning, "CAN FD not supported on this platform", "SocketCAN::writeFrame"});
        return false;
#endif
    }

    can_frame native{};
    native.can_id = frame.id & CAN_EFF_MASK;
    if (frame.extended) {
        native.can_id |= CAN_EFF_FLAG;
    }
    if (frame.errorFrame) {
        native.can_id |= CAN_ERR_FLAG;
    }
    native.can_dlc = std::min<std::uint8_t>(frame.data.size(), CAN_MAX_DLEN);
    std::copy_n(frame.data.begin(), native.can_dlc, native.data);

    auto written = ::write(fd, &native, sizeof(native));
    if (written < 0) {
        reportError({ErrorCode::SendFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to write CAN frame", std::strerror(errno)});
        return false;
    }
    recordSend(native.can_dlc);
    return true;
}

void SocketCAN::startReceiveLoop() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    receiveThread_ = std::thread([this] { receiveLoop(); });
}

void SocketCAN::stopReceiveLoop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
}

void SocketCAN::receiveLoop() {
    while (running_.load()) {
        ByteVector buffer;
        SocketCANConfig configSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            configSnapshot = config_;
        }

        auto result = receive(buffer, payloadLimit(configSnapshot));
        if (result > 0) {
            receiveCallback_.notify(buffer);
        } else {
            std::this_thread::sleep_for(effectiveSleep(configSnapshot.receiveThreadSleep));
        }
    }
}

bool SocketCAN::applyFilters() {
    if (socket_ < 0) {
        return false;
    }

    std::vector<can_filter> nativeFilters;
    nativeFilters.reserve(config_.filters.size());

    for (const auto& filter : config_.filters) {
        can_filter native{};
        native.can_id = filter.id;
        native.can_mask = filter.mask;
        nativeFilters.push_back(native);
    }

    if (::setsockopt(socket_, SOL_CAN_RAW, CAN_RAW_FILTER, nativeFilters.data(), nativeFilters.size() * sizeof(can_filter)) < 0) {
        reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "Failed to apply CAN filters", std::strerror(errno), errno});
        return false;
    }

    return true;
}

void SocketCAN::reportError(Error error) {
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.errorCount = saturatingAdd(stats_.errorCount, std::size_t{1});
    }
    healthy_.store(false);
    errorCallback_.notify(error);
}

void SocketCAN::recordSend(std::size_t bytes) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.bytesSent = saturatingAdd(stats_.bytesSent, bytes);
    stats_.messagesSent = saturatingAdd(stats_.messagesSent, std::size_t{1});
}

void SocketCAN::recordReceive(std::size_t bytes) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.bytesReceived = saturatingAdd(stats_.bytesReceived, bytes);
    stats_.messagesReceived = saturatingAdd(stats_.messagesReceived, std::size_t{1});
}

} // namespace comm::can
