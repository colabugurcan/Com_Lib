/**
 * @file serial_port.cpp
 * @brief DO-178C Compliant Serial Port Transport Implementation
 * 
 * @requirement SRS-COMM-SER-001: RS-232/RS-485 serial communication
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#include <comm/serial/serial_port.hpp>
#include <comm/core/utils.hpp>
#include <comm/core/defaults.hpp>
#include <comm/core/do178c.hpp>

#include <cerrno>
#include <cstring>
#include <string>
#include <chrono>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace comm::serial {

namespace {

speed_t toSpeed(int baud) {
    switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default: return 0;
    }
}

constexpr auto kDefaultReceiveSleep = defaults::kReceiveThreadSleep;

} // namespace

SerialPort::SerialPort(SerialConfig config) : config_(std::move(config)) {
    stats_.startTime = std::chrono::steady_clock::now();
}

SerialPort::~SerialPort() {
    close();
	if (receiveThread_.joinable()) {
		receiveThread_.join();
	}
}

bool SerialPort::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fileDescriptor_ >= 0) {
        return true;
    }

    int flags = O_RDWR | O_NOCTTY;
    if (config_.nonBlocking) {
        flags |= O_NONBLOCK;
    }

    fileDescriptor_ = ::open(config_.device.c_str(), flags);
    if (fileDescriptor_ < 0) {
        reportError({ErrorCode::OpenFailed, ErrorCategory::System, ErrorSeverity::Critical, "Failed to open serial port", config_.device, errno});
        return false;
    }

    if (!applySettings()) {
        ::close(fileDescriptor_);
        fileDescriptor_ = -1;
        return false;
    }

    healthy_.store(true);

    if (shouldSpawnReceiveThread() && receiveCallback_.hasCallback()) {
        startReceiveLoop();
    }

    return true;
}

bool SerialPort::close() {
    stopReceiveLoop();
    std::lock_guard<std::mutex> lock(mutex_);
    if (fileDescriptor_ >= 0) {
        ::close(fileDescriptor_);
        fileDescriptor_ = -1;
    }
    return true;
}

bool SerialPort::isOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fileDescriptor_ >= 0;
}

std::ptrdiff_t SerialPort::send(const ByteVector& data) {
    SerialConfig configSnapshot;
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        configSnapshot = config_;
        fd = fileDescriptor_;
    }

    if (!isSendEnabled(configSnapshot.direction)) {
        reportError({ErrorCode::UnsupportedOperation, ErrorCategory::Protocol, ErrorSeverity::Warning, "Send disabled by configuration", "SerialPort::send"});
        return -1;
    }

    if (fd < 0) {
        reportError({ErrorCode::NotOpen, ErrorCategory::Connection, ErrorSeverity::Warning, "Serial port not open", "SerialPort::send"});
        return -1;
    }

    auto result = ::write(fd, data.data(), data.size());
    if (result < 0) {
        reportError({ErrorCode::SendFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to write to serial port", std::strerror(errno), errno});
        return -1;
    }

    recordSend(static_cast<std::size_t>(result));
    return result;
}

std::ptrdiff_t SerialPort::receive(ByteVector& buffer, std::size_t maxSize) {
    SerialConfig configSnapshot;
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        configSnapshot = config_;
        fd = fileDescriptor_;
    }

    if (!isReceiveEnabled(configSnapshot.direction)) {
        reportError({ErrorCode::UnsupportedOperation, ErrorCategory::Protocol, ErrorSeverity::Warning, "Receive disabled by configuration", "SerialPort::receive"});
        return -1;
    }

    if (fd < 0) {
        reportError({ErrorCode::NotOpen, ErrorCategory::Connection, ErrorSeverity::Warning, "Serial port not open", "SerialPort::receive"});
        return -1;
    }

    buffer.resize(maxSize);
	auto result = ::read(fd, buffer.data(), buffer.size());
    if (result < 0) {
        buffer.clear();
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        reportError({ErrorCode::ReceiveFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to read from serial port", std::strerror(errno), errno});
        healthy_.store(false);
        return -1;
    }

    buffer.resize(static_cast<std::size_t>(result));
    recordReceive(buffer.size());
    healthy_.store(true);
    return result;
}

bool SerialPort::configure(const Config& config) {
    if (config.protocol != Protocol::Serial) {
        return false;
    }

    const auto& serialConfig = static_cast<const SerialConfig&>(config);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = serialConfig;
    }

    if (isOpen()) {
        bool result = applySettings();
        if (result) {
             if (!serialConfig.spawnReceiveThread) {
                 stopReceiveLoop();
             } else if (serialConfig.spawnReceiveThread && receiveCallback_.hasCallback() && isReceiveEnabled(serialConfig.direction)) {
                 startReceiveLoop();
             }
        }
        return result;
    }

    return true;
}

Config SerialPort::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

Statistics SerialPort::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

bool SerialPort::isHealthy() const {
    return healthy_.load();
}

void SerialPort::setReceiveCallback(ReceiveCallback callback) {
    receiveCallback_.setCallback(std::move(callback));
    if (shouldSpawnReceiveThread() && receiveCallback_.hasCallback()) {
        startReceiveLoop();
    }
}

void SerialPort::setErrorCallback(ErrorCallback callback) {
    errorCallback_.setCallback(std::move(callback));
}

bool SerialPort::applySettings() {
    if (fileDescriptor_ < 0) {
        return false;
    }

    termios tty{};
    if (::tcgetattr(fileDescriptor_, &tty) != 0) {
        reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "tcgetattr failed", std::strerror(errno), errno});
        return false;
    }

    ::cfmakeraw(&tty);

    auto speed = toSpeed(config_.baudRate);
    if (speed == 0) {
        reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "Unsupported baud rate", std::to_string(config_.baudRate)});
        return false;
    }

    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);

    tty.c_cflag &= ~CSIZE;
    switch (config_.dataBits) {
    case DataBits::Five: tty.c_cflag |= CS5; break;
    case DataBits::Six: tty.c_cflag |= CS6; break;
    case DataBits::Seven: tty.c_cflag |= CS7; break;
    case DataBits::Eight: tty.c_cflag |= CS8; break;
    }

    tty.c_cflag &= ~(PARENB | PARODD);
    switch (config_.parity) {
    case Parity::None: break;
    case Parity::Even:
        tty.c_cflag |= PARENB;
        break;
    case Parity::Odd:
        tty.c_cflag |= PARENB | PARODD;
        break;
    case Parity::Mark:
    case Parity::Space:
        reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "Mark/Space parity not supported", "SerialPort::applySettings"});
        return false;
    }

    if (config_.stopBits == StopBits::Two) {
        tty.c_cflag |= CSTOPB;
    } else {
        tty.c_cflag &= ~CSTOPB;
    }

    tty.c_cflag &= ~CRTSCTS;
#ifdef CRTSCTS
    if (config_.flowControl == FlowControl::Hardware) {
        tty.c_cflag |= CRTSCTS;
    }
#endif

    if (config_.flowControl == FlowControl::Software) {
        tty.c_iflag |= (IXON | IXOFF | IXANY);
    } else {
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    }

    tty.c_cc[VMIN] = 0;
    int timeoutDeciseconds = static_cast<int>(config_.readTimeout.count() / 100);
    if (timeoutDeciseconds < 0) {
        timeoutDeciseconds = 0;
    }
    tty.c_cc[VTIME] = static_cast<cc_t>(timeoutDeciseconds);

    if (::tcsetattr(fileDescriptor_, TCSANOW, &tty) != 0) {
        reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "tcsetattr failed", std::strerror(errno), errno});
        return false;
    }

    ::tcflush(fileDescriptor_, TCIOFLUSH);
    return true;
}

void SerialPort::recordSend(std::size_t bytes) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.bytesSent = saturatingAdd(stats_.bytesSent, bytes);
    stats_.messagesSent = saturatingAdd(stats_.messagesSent, std::size_t{1});
}

void SerialPort::recordReceive(std::size_t bytes) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.bytesReceived = saturatingAdd(stats_.bytesReceived, bytes);
    stats_.messagesReceived = saturatingAdd(stats_.messagesReceived, std::size_t{1});
}

void SerialPort::reportError(Error error) {
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.errorCount = saturatingAdd(stats_.errorCount, std::size_t{1});
    }
    healthy_.store(false);
    errorCallback_.notify(error);
}

void SerialPort::startReceiveLoop() {
    if (!shouldSpawnReceiveThread() || !receiveCallback_.hasCallback()) {
        return;
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    receiveThread_ = std::thread([this] { receiveLoop(); });
}

void SerialPort::stopReceiveLoop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
}

void SerialPort::receiveLoop() {
    while (running_.load()) {
        ByteVector buffer;
        auto result = receive(buffer, config_.rxBufferSize);
        if (result > 0) {
            receiveCallback_.notify(buffer);
        } else {
            std::this_thread::sleep_for(receiveSleepDuration());
        }
    }
}

bool SerialPort::shouldSpawnReceiveThread() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.spawnReceiveThread && isReceiveEnabled(config_.direction) && fileDescriptor_ >= 0;
}

std::chrono::milliseconds SerialPort::receiveSleepDuration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return effectiveSleep(config_.receiveThreadSleep);
}

} // namespace comm::serial
