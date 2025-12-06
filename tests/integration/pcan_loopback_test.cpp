#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <limits>

#include <comm/can/pcan_basic.hpp>

namespace {

std::optional<std::uint16_t> testHandle() {
    if (const auto* env = std::getenv("COMM_PCAN_TEST_HANDLE")) {
        char* end = nullptr;
        auto parsed = std::strtoul(env, &end, 0);
        if (end != env && parsed <= std::numeric_limits<std::uint16_t>::max()) {
            return static_cast<std::uint16_t>(parsed);
        }
    }
    return std::nullopt;
}

} // namespace

TEST(PCANBasicIntegration, OpenSendReceiveLoopback) {
    auto handle = testHandle();
    if (!handle.has_value()) {
        GTEST_SKIP() << "COMM_PCAN_TEST_HANDLE not set; skipping PCAN integration test";
    }

    comm::can::PCANBasicConfig config;
    config.handle = *handle;
    config.direction = comm::Direction::TwoWay;
    config.listenOnly = false;

    comm::can::PCANBasic can{config};
    ASSERT_TRUE(can.open()) << "Failed to open PCAN handle 0x" << std::hex << *handle;

    comm::ByteVector payload{0xAA, 0x55};
    auto sent = can.send(payload);
    if (sent <= 0) {
        GTEST_SKIP() << "Send failed; ensure the PCAN channel is available in loopback";
    }

    comm::ByteVector receiveBuffer;
    constexpr auto kMaxAttempts = 10;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        auto received = can.receive(receiveBuffer, payload.size());
        if (received > 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (receiveBuffer.empty()) {
        GTEST_SKIP() << "Did not receive data; ensure loopback wiring for PCAN handle";
    }

    EXPECT_EQ(receiveBuffer, payload);
    EXPECT_TRUE(can.isHealthy());
    EXPECT_TRUE(can.close());
}
