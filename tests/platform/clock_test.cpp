#include "platform/posix/clock.h"

#include <gtest/gtest.h>

#include <atomic>

using namespace caeron::platform;

TEST(Clock, NanoTimeIsMonotonic)
{
    auto t1 = nano_time();
    auto t2 = nano_time();
    EXPECT_GE(t2, t1);
}

TEST(Clock, NanoTimeAdvances)
{
    auto t1 = nano_time();
    // Do a small amount of work to ensure time advances
    std::atomic<int> sink{0};
    for (int i = 0; i < 1000; ++i)
        sink.fetch_add(1, std::memory_order_relaxed);
    auto t2 = nano_time();
    EXPECT_GT(t2, t1);
}

TEST(Clock, EpochTimeReasonable)
{
    // Should be after 2020-01-01 in milliseconds
    // 1577836800000 = 2020-01-01T00:00:00Z
    auto now = epoch_time();
    EXPECT_GT(now, 1577836800000LL);
}
