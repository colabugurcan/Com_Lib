#include <gtest/gtest.h>

#include <comm/ethernet/tcp.hpp>

TEST(TCPConfigTest, DefaultsAreInitialized) {
	comm::ethernet::TCPConfig config;
	EXPECT_EQ(config.protocol, comm::Protocol::TCP);
	EXPECT_EQ(config.role, comm::Role::Client);
	EXPECT_EQ(config.direction, comm::Direction::TwoWay);
	EXPECT_TRUE(config.reuseAddress);
}

TEST(TCPTest, ConfigureAcceptsTcpConfig) {
	comm::ethernet::TCP tcp;
	comm::ethernet::TCPConfig config;
	config.local.port = 12345;
	EXPECT_TRUE(tcp.configure(config));
}

TEST(TCPTest, SendWithoutOpenReportsError) {
	comm::ethernet::TCP tcp;
	bool errorNotified = false;
	tcp.setErrorCallback([&](const comm::Error&) { errorNotified = true; });

	comm::ByteVector payload{0x01};
	auto result = tcp.send(payload);

	EXPECT_LT(result, 0);
	EXPECT_TRUE(errorNotified);
	EXPECT_GT(tcp.getStatistics().errorCount, 0u);
	EXPECT_FALSE(tcp.isHealthy());
}
