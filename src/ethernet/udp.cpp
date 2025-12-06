/**
 * @file udp.cpp
 * @brief DO-178C Compliant UDP Transport Implementation
 * 
 * @requirement SRS-COMM-UDP-001: UDP datagram communication
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#include <comm/ethernet/udp.hpp>
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
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>

namespace comm::ethernet {

namespace {

/// @requirement SRS-COMM-THR-001: Default receive thread sleep
constexpr auto kDefaultReceiveSleep = defaults::kReceiveThreadSleep;

/// @requirement SRS-COMM-UDP-002: Maximum receive loop iterations
constexpr auto kMaxReceiveLoopIterations = defaults::kMaxReceiveLoopIterations;

/// @requirement SRS-COMM-UDP-003: Maximum health check iterations
constexpr auto kMaxHealthCheckIterations = defaults::kMaxHealthCheckIterations;

/**
 * @brief Validate socket file descriptor
 */
[[nodiscard]] inline bool isValidSocket(int fd) noexcept {
    return fd >= 0;
}

} // namespace

struct UDP::SocketHandle {
	int socket{-1};
	sockaddr_in local{};

	~SocketHandle() {
		if (socket >= 0) {
			::close(socket);
		}
	}
};

UDP::UDP(UDPConfig config) : config_(std::move(config)) {
	stats_.startTime = std::chrono::steady_clock::now();
	touchActivity();
}

UDP::~UDP() {
	close();
	if (receiveThread_.joinable()) {
		receiveThread_.join();
	}
}

bool UDP::open() {
	std::unique_lock<std::mutex> lock(mutex_);
	if (handle_) {
		return true;
	}

	int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		reportError({ErrorCode::OpenFailed, ErrorCategory::System, ErrorSeverity::Critical, "Failed to create UDP socket", "socket", errno});
		return false;
	}

	auto handle = std::make_unique<SocketHandle>();
	handle->socket = sock;
	handle_ = std::move(handle);

	if (!configureSocket()) {
		handle_.reset();
		return false;
	}

	if (!bindSocket()) {
		handle_.reset();
		return false;
	}

	if (config_.mode == Mode::Multicast || config_.joinMulticast) {
		if (!joinMulticastGroup()) {
			handle_.reset();
			return false;
		}
	}

	bool shouldLaunchReceive = config_.spawnReceiveThread && isReceiveEnabled(config_.direction);

	healthy_.store(true);
	touchActivity();
	lock.unlock();

	if (shouldLaunchReceive && receiveCallback_.hasCallback()) {
		startReceiveLoop();
	}

	scheduleHealthCheck();

	return true;
}

bool UDP::close() {
	stopReceiveLoop();
	stopHealthMonitor();
	std::lock_guard<std::mutex> lock(mutex_);
	handle_.reset();
	return true;
}

bool UDP::isOpen() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return static_cast<bool>(handle_);
}

std::ptrdiff_t UDP::send(const ByteVector& data) {
	if (!isSendEnabled(config_.direction)) {
		reportError({ErrorCode::UnsupportedOperation, ErrorCategory::Protocol, ErrorSeverity::Warning, "Send operation disabled by direction", "UDP::send"});
		return -1;
	}

	int socket = -1;
	UDPConfig configSnapshot;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!handle_) {
			reportError({ErrorCode::NotOpen, ErrorCategory::Connection, ErrorSeverity::Warning, "UDP socket not open", "UDP::send"});
			return -1;
		}
		socket = handle_->socket;
		configSnapshot = config_;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(configSnapshot.remote.port);

	std::string targetAddress = configSnapshot.remote.address.empty() ? configSnapshot.multicastGroup : configSnapshot.remote.address;
	if (targetAddress.empty()) {
		reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "Remote address not configured", "UDP::send"});
		return -1;
	}

	if (::inet_pton(AF_INET, targetAddress.c_str(), &addr.sin_addr) <= 0) {
		reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "Invalid remote address", targetAddress});
		return -1;
	}

	auto sent = ::send(socket, data.data(), std::min(data.size(), kMaxUdpDatagramSize), 0);
	if (sent < 0) {
		reportError({ErrorCode::SendFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to send UDP datagram", "UDP::send", errno});
		return -1;
	}

	recordSend(static_cast<std::size_t>(sent));
	touchActivity();
	return sent;
}

std::ptrdiff_t UDP::receive(ByteVector& buffer, std::size_t maxSize) {
	if (!isReceiveEnabled(config_.direction)) {
		reportError({ErrorCode::UnsupportedOperation, ErrorCategory::Protocol, ErrorSeverity::Warning, "Receive operation disabled by direction", "UDP::receive"});
		return -1;
	}

	int socket = -1;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!handle_) {
			reportError({ErrorCode::NotOpen, ErrorCategory::Connection, ErrorSeverity::Warning, "UDP socket not open", "UDP::receive"});
			return -1;
		}
		socket = handle_->socket;
	}

	buffer.resize(maxSize);
	sockaddr_in from{};
	socklen_t fromLen = sizeof(from);

	auto received = ::recvfrom(socket, buffer.data(), maxSize, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
	if (received < 0) {
		buffer.clear();
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return 0;
		}
		if (!running_.load() && (errno == EINTR || errno == EBADF)) {
			return -1;
		}
		reportError({ErrorCode::ReceiveFailed, ErrorCategory::Transmission, ErrorSeverity::Recoverable, "Failed to receive UDP datagram", "UDP::receive", errno});
		healthy_.store(false);
		return -1;
	}

	buffer.resize(static_cast<std::size_t>(received));
	recordReceive(buffer.size());
	healthy_.store(true);
	touchActivity();
	return received;
}

bool UDP::configure(const Config& config) {
	if (config.protocol != Protocol::UDP) {
		return false;
	}

	const auto& udpConfig = static_cast<const UDPConfig&>(config);

	bool reopen = false;
	bool wasSpawning = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		reopen = static_cast<bool>(handle_) && (config_.spawnReceiveThread != udpConfig.spawnReceiveThread);
		wasSpawning = config_.spawnReceiveThread;
		config_ = udpConfig;
	}

	if (reopen) {
		close();
		return open();
	}

	if (isOpen()) {
		bool configured = configureSocket();
		if (!configured) {
			return false;
		}
		if (!udpConfig.spawnReceiveThread && wasSpawning) {
			stopReceiveLoop();
		} else if (udpConfig.spawnReceiveThread && receiveCallback_.hasCallback() && isReceiveEnabled(udpConfig.direction)) {
			startReceiveLoop();
		}
	}

	return true;
}

Config UDP::getConfig() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return config_;
}

Statistics UDP::getStatistics() const {
	std::lock_guard<std::mutex> lock(statsMutex_);
	return stats_;
}

bool UDP::isHealthy() const {
	return healthy_.load();
}

void UDP::setReceiveCallback(ReceiveCallback callback) {
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

void UDP::setErrorCallback(ErrorCallback callback) {
	errorCallback_.setCallback(std::move(callback));
}

bool UDP::bindSocket() {
	if (!handle_) {
		return false;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(config_.local.port);

	std::string localAddr = config_.local.address.empty() ? std::string("0.0.0.0") : config_.local.address;
	if (::inet_pton(AF_INET, localAddr.c_str(), &addr.sin_addr) <= 0) {
		reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "Invalid local address", localAddr});
		return false;
	}

	if (::bind(handle_->socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
		reportError({ErrorCode::OpenFailed, ErrorCategory::System, ErrorSeverity::Critical, "Failed to bind UDP socket", "UDP::bindSocket", errno});
		return false;
	}

	handle_->local = addr;
	return true;
}

bool UDP::configureSocket() {
	if (!handle_) {
		return false;
	}

	int sock = handle_->socket;

	int reuse = 1;
	::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	if (config_.nonBlocking) {
		int flags = ::fcntl(sock, F_GETFL, 0);
		if (flags >= 0) {
			::fcntl(sock, F_SETFL, flags | O_NONBLOCK);
		}
	}

	if (config_.allowBroadcast) {
		int broadcastEnable = 1;
		::setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
	}

	if (config_.timeouts.receiveTimeout.count() > 0) {
		timeval tv{};
		auto ms = config_.timeouts.receiveTimeout;
		auto sec = std::chrono::duration_cast<std::chrono::seconds>(ms);
		auto usec = std::chrono::duration_cast<std::chrono::microseconds>(ms - sec);
		tv.tv_sec = static_cast<long>(sec.count());
		tv.tv_usec = static_cast<long>(usec.count());
		::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	}

	if (config_.timeouts.sendTimeout.count() > 0) {
		timeval tv{};
		auto ms = config_.timeouts.sendTimeout;
		auto sec = std::chrono::duration_cast<std::chrono::seconds>(ms);
		auto usec = std::chrono::duration_cast<std::chrono::microseconds>(ms - sec);
		tv.tv_sec = static_cast<long>(sec.count());
		tv.tv_usec = static_cast<long>(usec.count());
		::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	}

	if (config_.mode == Mode::Multicast || config_.joinMulticast) {
		::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &config_.multicastTTL, sizeof(config_.multicastTTL));
		unsigned char loop = static_cast<unsigned char>(config_.multicastLoopback ? 1 : 0);
		::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
	}

	return true;
}

bool UDP::joinMulticastGroup() {
	if (config_.multicastGroup.empty()) {
		reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "Multicast group not specified", "UDP::joinMulticastGroup"});
		return false;
	}

	int sock = handle_ ? handle_->socket : -1;
	if (sock < 0) {
		return false;
	}

	ip_mreq request{};
	if (::inet_pton(AF_INET, config_.multicastGroup.c_str(), &request.imr_multiaddr) <= 0) {
		reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "Invalid multicast address", config_.multicastGroup});
		return false;
	}

	if (config_.local.address.empty()) {
		request.imr_interface.s_addr = htonl(INADDR_ANY);
	} else if (::inet_pton(AF_INET, config_.local.address.c_str(), &request.imr_interface) <= 0) {
		reportError({ErrorCode::InvalidConfiguration, ErrorCategory::Configuration, ErrorSeverity::Warning, "Invalid local interface", config_.local.address});
		return false;
	}

	if (::setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request, sizeof(request)) < 0) {
		reportError({ErrorCode::OpenFailed, ErrorCategory::System, ErrorSeverity::Critical, "Failed to join multicast group", "UDP::joinMulticastGroup", errno});
		return false;
	}

	return true;
}

void UDP::startReceiveLoop() {
	if (!shouldSpawnReceiveThread() || !receiveCallback_.hasCallback()) {
		return;
	}
	bool expected = false;
	if (!running_.compare_exchange_strong(expected, true)) {
		return;
	}

	receiveThread_ = std::thread([this] { receiveLoop(); });
}

void UDP::stopReceiveLoop() {
	bool expected = true;
	if (!running_.compare_exchange_strong(expected, false)) {
		return;
	}

	int socket = -1;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (handle_) {
			socket = handle_->socket;
		}
	}

	if (socket >= 0) {
		::shutdown(socket, SHUT_RD);
	}

	if (receiveThread_.joinable()) {
		receiveThread_.join();
	}
}

void UDP::stopHealthMonitor() {
	bool expected = true;
	if (!healthMonitorRunning_.compare_exchange_strong(expected, false)) {
		return;
	}

	if (healthThread_.joinable()) {
		healthThread_.join();
	}
}

void UDP::receiveLoop() {
	while (running_.load()) {
		ByteVector buffer;
		auto result = receive(buffer, kMaxUdpDatagramSize);
		if (result > 0) {
			receiveCallback_.notify(buffer);
			continue;
		}

		std::this_thread::sleep_for(receiveSleepDuration());
	}
}

void UDP::recordSend(std::size_t bytes) {
	std::lock_guard<std::mutex> lock(statsMutex_);
	stats_.bytesSent = saturatingAdd(stats_.bytesSent, bytes);
	stats_.messagesSent = saturatingAdd(stats_.messagesSent, std::size_t{1});
}

void UDP::recordReceive(std::size_t bytes) {
	std::lock_guard<std::mutex> lock(statsMutex_);
	stats_.bytesReceived = saturatingAdd(stats_.bytesReceived, bytes);
	stats_.messagesReceived = saturatingAdd(stats_.messagesReceived, std::size_t{1});
}

void UDP::reportError(Error error) {
	{
		std::lock_guard<std::mutex> lock(statsMutex_);
		stats_.errorCount = saturatingAdd(stats_.errorCount, std::size_t{1});
	}
	healthy_.store(false);
	errorCallback_.notify(error);
}

bool UDP::shouldSpawnReceiveThread() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return config_.spawnReceiveThread && isReceiveEnabled(config_.direction) && static_cast<bool>(handle_);
}

std::chrono::milliseconds UDP::receiveSleepDuration() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return effectiveSleep(config_.receiveThreadSleep);
}

void UDP::attemptRebind() {
	if (!config_.autoRebind) {
		return;
	}

	stopReceiveLoop();

	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (handle_) {
			::close(handle_->socket);
			handle_.reset();
		}
	}

	std::this_thread::sleep_for(config_.timeouts.reconnectInterval);
	open();
}

void UDP::scheduleHealthCheck() {
	bool expected = false;
	if (!healthMonitorRunning_.compare_exchange_strong(expected, true)) {
		return;
	}

	healthThread_ = std::thread([this] { healthMonitorLoop(); });
}

void UDP::healthMonitorLoop() {
	while (healthMonitorRunning_.load()) {
		std::this_thread::sleep_for(kDefaultHealthCheckInterval);

		auto elapsed = timeSinceLastActivity();

		bool healthy = healthy_.load();
		if (!healthy && elapsed > config_.timeouts.reconnectInterval) {
			attemptRebind();
			continue;
		}

		if (config_.timeouts.receiveTimeout.count() > 0 &&
			elapsed > config_.timeouts.receiveTimeout * 5) {
			healthy_.store(false);
			attemptRebind();
		}
	}
}

void UDP::touchActivity() {
	std::lock_guard<std::mutex> lock(activityMutex_);
	lastActivity_ = std::chrono::steady_clock::now();
}

std::chrono::steady_clock::duration UDP::timeSinceLastActivity() const {
	std::lock_guard<std::mutex> lock(activityMutex_);
	return std::chrono::steady_clock::now() - lastActivity_;
}

} // namespace comm::ethernet
