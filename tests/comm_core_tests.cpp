#include <gtest/gtest.h>

#include <comm/core/config.hpp>
#include <comm/core/error.hpp>

TEST(ConfigTest, DefaultsAreInitialized) {
    comm::Config config;
    EXPECT_EQ(config.protocol, comm::Protocol::Unknown);
    EXPECT_EQ(config.mode, comm::Mode::Unspecified);
    EXPECT_FALSE(config.autoReconnect);
}

TEST(ErrorTest, RetryableErrorDetection) {
    comm::Error timeoutError;
    timeoutError.code = comm::ErrorCode::Timeout;
    EXPECT_TRUE(comm::isRetryable(timeoutError));
}
