#include <comm/ethernet/tcp.hpp>

#include <chrono>
#include <iostream>
#include <string>

int main() {
	comm::ethernet::TCPConfig config;
	config.remote.address = "127.0.0.1";
	config.remote.port = 12345;
	config.autoReconnect = false;

	comm::ethernet::TCP tcp{config};
	tcp.setErrorCallback([](const comm::Error& error) {
		std::cerr << "TCP error: " << error.message << std::endl;
	});

	if (!tcp.open()) {
		std::cerr << "Failed to open TCP connection" << std::endl;
		return 1;
	}

	const std::string payload = "ping";
	comm::ByteVector data(payload.begin(), payload.end());
	if (tcp.send(data) <= 0) {
		std::cerr << "Failed to send payload" << std::endl;
		tcp.close();
		return 1;
	}

	comm::ByteVector response;
	auto received = tcp.receive(response, 1024);
	if (received <= 0) {
		std::cerr << "No response received" << std::endl;
		tcp.close();
		return 1;
	}

	std::cout << "Received " << response.size() << " bytes" << std::endl;
	tcp.close();
	return 0;
}
