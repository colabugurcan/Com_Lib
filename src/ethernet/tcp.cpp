/**
 * @file tcp.cpp
 * @brief DO-178C Compliant TCP Transport Implementation
 * 
 * @requirement SRS-COMM-TCP-001: TCP stream communication
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#include <comm/ethernet/tcp.hpp>
#include <comm/core/utils.hpp>
#include <comm/core/defaults.hpp>
#include <comm/core/do178c.hpp>

#include <chrono>
#include <cstring>
#include <thread>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>

namespace comm::ethernet {

namespace {

/// @requirement SRS-COMM-THR-001: Default receive thread sleep
constexpr auto kDefaultReceiveSleep = defaults::kReceiveThreadSleep;

/// @requirement SRS-COMM-TCP-002: Maximum receive loop iterations
constexpr auto kMaxReceiveLoopIterations = defaults::kMaxReceiveLoopIterations;

/// @requirement SRS-COMM-TCP-003: Maximum connection attempts
constexpr auto kMaxConnectionAttempts = defaults::kMaxConnectionAttempts;

/**
 * @brief Validate socket file descriptor
 */
[[nodiscard]] inline bool isValidSocket(int fd) noexcept {
    return fd >= 0;
}

} // namespace

struct TCP::SocketHandle {
	int socket{-1};
	int listen{-1};
	bool server{false};

	~SocketHandle() {
		if (socket >= 0) {
			::close(socket);
		}
		if (listen >= 0) {
			::close(listen);
		}
	}
};

TCP::TCP(TCPConfig config) : config_(std::move(config)) {
	stats_.startTime = std::chrono::steady_clock::now();
	touchActivity();
}

TCP::~TCP() {
	close();
	if (receiveThread_.joinable()) {
		receiveThread_.join();
	}
}

bool TCP::open() {
	std::unique_lock<std::mutex> lock(mutex_);
	if (handle_) {
		return true;
	}

	auto handle = std::make_unique<SocketHandle>();
	handle->server = (config_.role == Role::Server);

	int sock = ::socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		reportError({ErrorCode::OpenFailed, ErrorCategory::System, ErrorSeverity::Critical, "Failed to create TCP socket", "TCP::open", errno});
		return false;
	}

	if (!configureSocket(sock)) {
		::close(sock);
		return false;
	}

	if (!bindLocal(sock)) {
		::close(sock);
		return false;
	}

	if (handle->server) {
		if (::listen(sock, config_.listenBacklog) < 0) {
			reportError({ErrorCode::OpenFailed, ErrorCategory::System, ErrorSeverity::Critical, "Failed to listen on TCP socket", "TCP::open", errno});
			::close(sock);
			return false;
		}
		handle->listen = sock;
		handle_ = std::move(handle);
		healthy_.store(true);
	} else {
		if (!connectRemote(sock)) {
			::close(sock);
			return false;
		}
		handle->socket = sock;
		handle_ = std::move(handle);
		healthy_.store(true);
		touchActivity();
	}

	bool shouldLaunchReceive = config_.spawnReceiveThread && isReceiveEnabled(config_.direction);
	lock.unlock();

	if (shouldLaunchReceive && receiveCallback_.hasCallback()) {
		startReceiveLoop();
	}

	return true;
}

bool TCP::close() {
	stopReceiveLoop();

	std::lock_guard<std::mutex> lock(mutex_);
	handle_.reset();
	return true;
}

bool TCP::isOpen() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return static_cast<bool>(handle_);
}

std::ptrdiff_t TCP::send(const ByteVector& data) {
	if (!isSendEnabled(config_.direction)) {
		reportError({ErrorCode::UnsupportedOperation, ErrorCategory::Protocol, ErrorSeverity::Warning, "Send operation disabled by direction", "TCP::send"});
		return -1;
	}

	if (data.empty()) {
		return 0;
	}

	if (!ensureConnected()) {
		return -1;
	}

	int socket = -1;
	TCPConfig configSnapshot;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!handle_ || handle_->socket < 0) {
			reportError({ErrorCode::NotOpen, ErrorCategory::Connection, ErrorSeverity::Warning, "TCP socket not open", "TCP::send"});
			return -1;
		}
		socket = handle_->socket;
		configSnapshot = config_;
	}

	size_t totalSent = 0;
	const std::uint8_t* buffer = data.data();
	size_t remaining = data.size();

	while (remaining > 0) {
		auto sent = ::send(socket, buffer + totalSent, remaining, 0);
		if (sent < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			}
			reportError({ErrorCode::SendFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to send TCP payload", "TCP::send", errno});
			healthy_.store(false);
			if (configSnapshot.role == Role::Server) {
				resetServerConnection();
			} else {
				dropClientConnection();
			}
			return totalSent > 0 ? static_cast<std::ptrdiff_t>(totalSent) : -1;
		}

		if (sent == 0) {
			break;
		}

		totalSent += static_cast<std::size_t>(sent);
		remaining -= static_cast<std::size_t>(sent);

		if (configSnapshot.nonBlocking) {
			break;
		}
	}

	if (totalSent > 0) {
		recordSend(totalSent);
		healthy_.store(true);
		touchActivity();
	}

	return static_cast<std::ptrdiff_t>(totalSent);
}

std::ptrdiff_t TCP::receive(ByteVector& buffer, std::size_t maxSize) {
	if (!isReceiveEnabled(config_.direction)) {
		reportError({ErrorCode::UnsupportedOperation, ErrorCategory::Protocol, ErrorSeverity::Warning, "Receive operation disabled by direction", "TCP::receive"});
		return -1;
	}

	if (maxSize == 0) {
		return 0;
	}

	if (!ensureConnected()) {
		return -1;
	}

	int socket = -1;
	TCPConfig configSnapshot;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!handle_ || handle_->socket < 0) {
			reportError({ErrorCode::NotOpen, ErrorCategory::Connection, ErrorSeverity::Warning, "TCP socket not open", "TCP::receive"});
			return -1;
		}
		socket = handle_->socket;
		configSnapshot = config_;
	}

	buffer.resize(maxSize);
	auto received = ::recv(socket, buffer.data(), buffer.size(), 0);
	if (received < 0) {
		buffer.clear();
		if (errno == EINTR) {
			return 0;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return 0;
		}
		reportError({ErrorCode::ReceiveFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to receive TCP payload", "TCP::receive", errno});
		healthy_.store(false);
		if (configSnapshot.role == Role::Server) {
			resetServerConnection();
		} else {
			dropClientConnection();
		}
		return -1;
	}

	if (received == 0) {
		healthy_.store(false);
		if (configSnapshot.role == Role::Server) {
			resetServerConnection();
		} else {
			dropClientConnection();
		}
		buffer.clear();
		return 0;
	}

	buffer.resize(static_cast<std::size_t>(received));
	recordReceive(buffer.size());
	healthy_.store(true);
	touchActivity();
	return received;
}

bool TCP::configure(const Config& config) {
	if (config.protocol != Protocol::TCP) {
		return false;
	}

	const auto& tcpConfig = static_cast<const TCPConfig&>(config);

	bool reopen = false;
	bool wasSpawning = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		reopen = static_cast<bool>(handle_);
		wasSpawning = config_.spawnReceiveThread;
		config_ = tcpConfig;
	}

	if (reopen) {
		close();
		return open();
	}

	if (!tcpConfig.spawnReceiveThread && wasSpawning) {
		stopReceiveLoop();
	} else if (tcpConfig.spawnReceiveThread && receiveCallback_.hasCallback() && isReceiveEnabled(tcpConfig.direction)) {
		startReceiveLoop();
	}

	return true;
}

Config TCP::getConfig() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return config_;
}

Statistics TCP::getStatistics() const {
	std::lock_guard<std::mutex> lock(statsMutex_);
	return stats_;
}

bool TCP::isHealthy() const {
	return healthy_.load();
}

void TCP::setReceiveCallback(ReceiveCallback callback) {
	receiveCallback_.setCallback(std::move(callback));
	bool shouldLaunch = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		shouldLaunch = handle_ && config_.spawnReceiveThread && isReceiveEnabled(config_.direction);
	}
	if (shouldLaunch && receiveCallback_.hasCallback()) {
		startReceiveLoop();
	}
}

void TCP::setErrorCallback(ErrorCallback callback) {
	errorCallback_.setCallback(std::move(callback));
}

bool TCP::configureSocket(int socket) {
	if (config_.nonBlocking) {
		int flags = ::fcntl(socket, F_GETFL, 0);
		if (flags >= 0) {
			::fcntl(socket, F_SETFL, flags | O_NONBLOCK);
		}
	}

	if (config_.reuseAddress) {
		int reuse = 1;
		::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	}

	if (config_.noDelay) {
		int flag = 1;
		::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
	}

	if (config_.keepAlive) {
		int keepAlive = 1;
		::setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(keepAlive));
	}

	if (config_.timeouts.receiveTimeout.count() > 0) {
		timeval tv{};
		auto ms = config_.timeouts.receiveTimeout;
		auto sec = std::chrono::duration_cast<std::chrono::seconds>(ms);
		auto usec = std::chrono::duration_cast<std::chrono::microseconds>(ms - sec);
		tv.tv_sec = static_cast<long>(sec.count());
		tv.tv_usec = static_cast<long>(usec.count());
		::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	}

	if (config_.timeouts.sendTimeout.count() > 0) {
		timeval tv{};
		auto ms = config_.timeouts.sendTimeout;
		auto sec = std::chrono::duration_cast<std::chrono::seconds>(ms);
		auto usec = std::chrono::duration_cast<std::chrono::microseconds>(ms - sec);
		tv.tv_sec = static_cast<long>(sec.count());
		tv.tv_usec = static_cast<long>(usec.count());
		::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	}

	return true;
}

bool TCP::bindLocal(int socket) {
	if (config_.local.port == 0 && config_.local.address.empty()) {
		return true;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(config_.local.port);

	std::string localAddress = config_.local.address.empty() ? std::string("0.0.0.0") : config_.local.address;
	if (::inet_pton(AF_INET, localAddress.c_str(), &addr.sin_addr) <= 0) {
		reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "Invalid local address", localAddress});
		return false;
	}

	if (::bind(socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
		reportError({ErrorCode::OpenFailed, ErrorCategory::System, ErrorSeverity::Critical, "Failed to bind TCP socket", "TCP::bindLocal", errno});
		return false;
	}

	return true;
}

bool TCP::connectRemote(int socket) {
	if (config_.remote.port == 0 || config_.remote.address.empty()) {
		reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "Remote endpoint not configured", "TCP::connectRemote"});
		return false;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(config_.remote.port);

	if (::inet_pton(AF_INET, config_.remote.address.c_str(), &addr.sin_addr) <= 0) {
		reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "Invalid remote address", config_.remote.address});
		return false;
	}

	if (::connect(socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
		if (errno != EINPROGRESS) {
			reportError({ErrorCode::OpenFailed, ErrorCategory::System, ErrorSeverity::Critical, "Failed to connect TCP socket", "TCP::connectRemote", errno});
			return false;
		}
	}

	return true;
}

bool TCP::ensureConnected() {
	int listenSocket = -1;
	bool server = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!handle_) {
			reportError({ErrorCode::NotOpen, ErrorCategory::Connection, ErrorSeverity::Warning, "TCP handle not initialized", "TCP::ensureConnected"});
			return false;
		}

		server = handle_->server;
		if (!server) {
			if (handle_->socket >= 0) {
				return true;
			}
		} else {
			if (handle_->socket >= 0) {
				return true;
			}
			listenSocket = handle_->listen;
			if (listenSocket < 0) {
				reportError({ErrorCode::NotOpen, ErrorCategory::Connection, ErrorSeverity::Critical, "TCP listen socket not available", "TCP::ensureConnected"});
				return false;
			}
		}
	}

	if (!server) {
		if (!config_.autoReconnect) {
			reportError({ErrorCode::NotOpen, ErrorCategory::Connection, ErrorSeverity::Warning, "TCP connection not established", "TCP::ensureConnected"});
			return false;
		}

		std::this_thread::sleep_for(config_.timeouts.reconnectInterval);

		int sock = ::socket(AF_INET, SOCK_STREAM, 0);
		if (sock < 0) {
			reportError({ErrorCode::OpenFailed, ErrorCategory::System, ErrorSeverity::Critical, "Failed to create TCP socket", "TCP::ensureConnected", errno});
			return false;
		}

		if (!configureSocket(sock)) {
			::close(sock);
			return false;
		}

		if (!bindLocal(sock)) {
			::close(sock);
			return false;
		}

		if (!connectRemote(sock)) {
			::close(sock);
			return false;
		}

		std::lock_guard<std::mutex> lock(mutex_);
		if (!handle_ || handle_->server) {
			::close(sock);
			return false;
		}

		if (handle_->socket >= 0) {
			::close(handle_->socket);
		}

		handle_->socket = sock;
		healthy_.store(true);
		touchActivity();
		return true;
	}

	sockaddr_in addr{};
	socklen_t addrLen = sizeof(addr);
	int client = ::accept(listenSocket, reinterpret_cast<sockaddr*>(&addr), &addrLen);
	if (client < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
			return false;
		}
		reportError({ErrorCode::ReceiveFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to accept TCP connection", "TCP::ensureConnected", errno});
		return false;
	}

	if (!configureSocket(client)) {
		::close(client);
		return false;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	if (!handle_ || handle_->listen != listenSocket) {
		::close(client);
		return false;
	}

	if (handle_->socket >= 0) {
		::close(handle_->socket);
	}

	handle_->socket = client;
	healthy_.store(true);
	touchActivity();
	return true;
}

void TCP::closeActiveSocket() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (handle_ && handle_->socket >= 0) {
		::close(handle_->socket);
		handle_->socket = -1;
	}
}

void TCP::startReceiveLoop() {
	if (!shouldSpawnReceiveThread() || !receiveCallback_.hasCallback()) {
		return;
	}
	bool expected = false;
	if (!running_.compare_exchange_strong(expected, true)) {
		return;
	}

	receiveThread_ = std::thread([this] { receiveLoop(); });
}

void TCP::stopReceiveLoop() {
	bool expected = true;
	if (!running_.compare_exchange_strong(expected, false)) {
		return;
	}

	closeActiveSocket();

	if (receiveThread_.joinable()) {
		receiveThread_.join();
	}
}

void TCP::receiveLoop() {
	while (running_.load()) {
		ByteVector buffer;
		auto result = receive(buffer, kDefaultTcpBufferSize);
		if (result > 0) {
			receiveCallback_.notify(buffer);
		} else {
			std::this_thread::sleep_for(receiveSleepDuration());
		}
	}
}

void TCP::recordSend(std::size_t bytes) {
	std::lock_guard<std::mutex> lock(statsMutex_);
	stats_.bytesSent = saturatingAdd(stats_.bytesSent, bytes);
	stats_.messagesSent = saturatingAdd(stats_.messagesSent, std::size_t{1});
}

void TCP::recordReceive(std::size_t bytes) {
	std::lock_guard<std::mutex> lock(statsMutex_);
	stats_.bytesReceived = saturatingAdd(stats_.bytesReceived, bytes);
	stats_.messagesReceived = saturatingAdd(stats_.messagesReceived, std::size_t{1});
}

void TCP::reportError(Error error) {
	{
		std::lock_guard<std::mutex> lock(statsMutex_);
		stats_.errorCount = saturatingAdd(stats_.errorCount, std::size_t{1});
	}
	healthy_.store(false);
	errorCallback_.notify(error);
}

void TCP::dropClientConnection() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (handle_ && !handle_->server && handle_->socket >= 0) {
		::close(handle_->socket);
		handle_->socket = -1;
	}
}

void TCP::resetServerConnection() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (handle_ && handle_->server && handle_->socket >= 0) {
		::close(handle_->socket);
		handle_->socket = -1;
	}
}

bool TCP::shouldSpawnReceiveThread() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return config_.spawnReceiveThread && isReceiveEnabled(config_.direction) && handle_ && (handle_->server || handle_->socket >= 0);
}

std::chrono::milliseconds TCP::receiveSleepDuration() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return effectiveSleep(config_.receiveThreadSleep);
}

void TCP::touchActivity() {
	std::lock_guard<std::mutex> lock(activityMutex_);
	lastActivity_ = std::chrono::steady_clock::now();
}

std::chrono::steady_clock::duration TCP::timeSinceLastActivity() const {
	std::lock_guard<std::mutex> lock(activityMutex_);
	return std::chrono::steady_clock::now() - lastActivity_;
}

} // namespace comm::ethernet
