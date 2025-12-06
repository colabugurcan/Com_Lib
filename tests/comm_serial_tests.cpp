#include <gtest/gtest.h>

#include <comm/serial/serial_port.hpp>

TEST(SerialConfigTest, DefaultsAreInitialized) {
    comm::serial::SerialConfig config;
    EXPECT_EQ(config.protocol, comm::Protocol::Serial);
    EXPECT_EQ(config.direction, comm::Direction::TwoWay);
    EXPECT_EQ(config.baudRate, 115200);
}

TEST(SerialPortTest, ConfigureAcceptsSerialConfig) {
    comm::serial::SerialPort serial;
    comm::serial::SerialConfig config;
    config.baudRate = 9600;
    EXPECT_TRUE(serial.configure(config));
}

TEST(SerialPortTest, SendWithoutOpenReturnsError) {
    comm::serial::SerialPort serial;
    bool errorNotified = false;
    serial.setErrorCallback([&](const comm::Error&) { errorNotified = true; });

    comm::ByteVector payload{0x00};
    auto result = serial.send(payload);

    EXPECT_LT(result, 0);
    EXPECT_TRUE(errorNotified);
    EXPECT_GT(serial.getStatistics().errorCount, 0u);
    EXPECT_FALSE(serial.isHealthy());
}
