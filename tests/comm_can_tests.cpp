#include <gtest/gtest.h>

#include <cstdint>

#include <comm/can/pcan_basic.hpp>

TEST(PCANBasicConfigTest, DefaultsAreInitialized) {
    comm::can::PCANBasicConfig config;
    EXPECT_EQ(config.protocol, comm::Protocol::CAN);
    EXPECT_EQ(config.handle, static_cast<std::uint16_t>(comm::can::PCANChannel::Usb1));
    EXPECT_TRUE(config.autoReconnect);
}

TEST(PCANBasicTest, ConfigureAcceptsCanConfig) {
    comm::can::PCANBasic can;
    comm::can::PCANBasicConfig config;
    config.listenOnly = true;
    EXPECT_TRUE(can.configure(config));
}

TEST(PCANBasicTest, SendWithoutOpenReportsError) {
    comm::can::PCANBasic can;
    bool errorNotified = false;
    can.setErrorCallback([&](const comm::Error&) { errorNotified = true; });

    comm::ByteVector payload{0x01};
    auto result = can.send(payload);

    EXPECT_LT(result, 0);
    EXPECT_TRUE(errorNotified);
    EXPECT_GT(can.getStatistics().errorCount, 0u);
    EXPECT_FALSE(can.isHealthy());
}
