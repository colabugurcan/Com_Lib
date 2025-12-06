/**
 * @file tcp.hpp
 * @brief DO-178C Level B Compliant TCP Transport Interface
 * 
 * This module provides TCP communication functionality for flight-critical
 * avionics systems using POSIX sockets.
 * 
 * Features:
 * - Client and server role support
 * - Configurable socket options (TCP_NODELAY, SO_KEEPALIVE, etc.)
 * - Thread-safe operation
 * - Automatic connection management
 * 
 * @note All functions are designed to be deterministic and exception-free.
 * @note Thread-safe for concurrent send/receive operations.
 * 
 * @requirement SRS-TCP-001: TCP socket management
 * @requirement SRS-TCP-002: Client connection handling
 * @requirement SRS-TCP-003: Server accept handling
 * 
 * @copyright (C) 2025 - Flight Critical Systems
 * @version 2.0.0 - DO-178C Compliant
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <comm/core/interface.hpp>
#include <comm/core/defaults.hpp>
#include <comm/ethernet/endpoint.hpp>

namespace comm::ethernet {

struct TCPConfig : Config {
	Endpoint local{};
	Endpoint remote{};
	bool reuseAddress{true};
	bool noDelay{true};
	bool keepAlive{false};
	int listenBacklog{defaults::kTcpListenBacklog};

	TCPConfig() {
		protocol = Protocol::TCP;
		role = Role::Client;
		direction = Direction::TwoWay;
	}
};

/// POSIX-oriented TCP transport implementing the generic communication interface.
class TCP : public ICommunication {
public:
	explicit TCP(TCPConfig config = {});
	~TCP() noexcept override;

	bool open() override;
	bool close() override;
	[[nodiscard]] bool isOpen() const override;

	std::ptrdiff_t send(const ByteVector& data) override;
	std::ptrdiff_t receive(ByteVector& buffer, std::size_t maxSize) override;

	bool configure(const Config& config) override;
	[[nodiscard]] Config getConfig() const override;

	[[nodiscard]] Statistics getStatistics() const override;
	[[nodiscard]] bool isHealthy() const override;

	void setReceiveCallback(ReceiveCallback callback) override;
	void setErrorCallback(ErrorCallback callback) override;

private:
	struct SocketHandle;

	bool configureSocket(int socket);
	bool bindLocal(int socket);
	bool connectRemote(int socket);
	bool ensureConnected();
	void closeActiveSocket();
	void startReceiveLoop();
	void stopReceiveLoop();
	void receiveLoop();
	void recordSend(std::size_t bytes);
	void recordReceive(std::size_t bytes);
	void reportError(Error error);
	void dropClientConnection();
	void resetServerConnection();
	void touchActivity();
	std::chrono::steady_clock::duration timeSinceLastActivity() const;
    bool shouldSpawnReceiveThread() const;
    std::chrono::milliseconds receiveSleepDuration() const;

	TCPConfig config_{};
	mutable std::mutex mutex_{};
	mutable std::mutex statsMutex_{};
	mutable std::mutex activityMutex_{};
	Statistics stats_{};

	ReceiveCallbackRegistry receiveCallback_{};
	ErrorCallbackRegistry errorCallback_{};

	std::atomic<bool> running_{false};
	std::thread receiveThread_{};
	std::unique_ptr<SocketHandle> handle_{};
	std::atomic<bool> healthy_{true};
	std::chrono::steady_clock::time_point lastActivity_{std::chrono::steady_clock::now()};
};

} // namespace comm::ethernet
