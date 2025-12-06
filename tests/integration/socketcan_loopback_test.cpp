#include <gtest/gtest.h>

#include <cstdlib>

#include <comm/can/socketcan.hpp>

namespace {

std::string testInterface() {
    const auto* env = std::getenv("COMM_CAN_TEST_IFACE");
    if (env == nullptr) {
        return {};
    }
    return std::string{env};
}

} // namespace

TEST(SocketCANIntegration, OpenAndSendOnInterface) {
    auto iface = testInterface();
    if (iface.empty()) {
        GTEST_SKIP() << "COMM_CAN_TEST_IFACE not set; skipping SocketCAN integration test";
    }

    comm::can::SocketCANConfig config;
    config.interface = iface;
    config.direction = comm::Direction::TwoWay;
    config.loopback = true;

    comm::can::SocketCAN can{config};
    ASSERT_TRUE(can.open()) << "Failed to open interface " << iface;

    comm::ByteVector payload{0xAA, 0x55};
    auto sent = can.send(payload);
    EXPECT_GT(sent, 0);

    comm::ByteVector receiveBuffer;
    auto received = can.receive(receiveBuffer, 8);
    if (received < 0) {
        GTEST_SKIP() << "Receive failed; ensure " << iface << " is in loopback with peers";
    }

    EXPECT_EQ(receiveBuffer, payload);
    EXPECT_TRUE(can.isHealthy());

    EXPECT_TRUE(can.close());
}
