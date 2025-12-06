#include <gtest/gtest.h>

#include <comm/ethernet/udp.hpp>

TEST(UDPConfigTest, DefaultsAreSet) {
    comm::ethernet::UDPConfig config;
    EXPECT_EQ(config.protocol, comm::Protocol::UDP);
    EXPECT_FALSE(config.allowBroadcast);
    EXPECT_TRUE(config.autoRebind);
}

TEST(UDPInstanceTest, ConfigureAcceptsUDPConfig) {
    comm::ethernet::UDP udp;
    comm::ethernet::UDPConfig config;
    config.direction = comm::Direction::SendOnly;
    EXPECT_TRUE(udp.configure(config));
}

TEST(UDPInstanceTest, ErrorCallbackReceivesSendFailure) {
    comm::ethernet::UDP udp;
    comm::ethernet::UDPConfig config;
    config.direction = comm::Direction::SendOnly;
    udp.configure(config);

    bool errorNotified = false;
    udp.setErrorCallback([&](const comm::Error&) { errorNotified = true; });

    comm::ByteVector payload{0x01, 0x02};
    auto result = udp.send(payload);

    EXPECT_LT(result, 0);
    EXPECT_TRUE(errorNotified);
    EXPECT_GT(udp.getStatistics().errorCount, 0u);
}

TEST(UDPInstanceTest, ReceiveFailureMarksUnhealthy) {
    comm::ethernet::UDP udp;
    comm::ethernet::UDPConfig config;
    config.direction = comm::Direction::ReceiveOnly;
    udp.configure(config);

    comm::ByteVector buffer;
    auto result = udp.receive(buffer, 16);

    EXPECT_LT(result, 0);
    EXPECT_FALSE(udp.isHealthy());
}
