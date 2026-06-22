#include "beast/platform/engine/timer/timer_wheel.hpp"

#include <gtest/gtest.h>

#include <atomic>

namespace beast::platform::engine::timer {
namespace {

TEST(TimerWheelTest, FiresAfterDelay) {
    std::atomic<int> fire_count{0};
    TimerWheel wheel(50, 512, [&](TimerExpiredPayload) { fire_count.fetch_add(1); });

    (void)wheel.schedule(TimerHandle{1}, 50, TimerExpiredPayload{});

    for (int i = 0; i < 4; ++i) {
        wheel.tick();
    }

    EXPECT_EQ(fire_count.load(), 1);
}

TEST(TimerWheelTest, CancelPreventsFire) {
    std::atomic<int> fire_count{0};
    TimerWheel wheel(50, 512, [&](TimerExpiredPayload) { fire_count.fetch_add(1); });

    const auto handle = wheel.schedule(TimerHandle{2}, 100, TimerExpiredPayload{});
    wheel.cancel(handle);

    for (int i = 0; i < 6; ++i) {
        wheel.tick();
    }

    EXPECT_EQ(fire_count.load(), 0);
}

TEST(TimerWheelTest, LongDelayUsesRemainingRounds) {
    TimerWheel wheel(50, 8, [&](TimerExpiredPayload) {});

    const auto handle = wheel.schedule(TimerHandle{3}, 500, TimerExpiredPayload{});
    EXPECT_TRUE(handle.valid());

    for (int i = 0; i < 12; ++i) {
        wheel.tick();
    }

    std::atomic<int> fire_count{0};
    TimerWheel wheel2(50, 8, [&](TimerExpiredPayload) { fire_count.fetch_add(1); });
    (void)wheel2.schedule(TimerHandle{4}, 500, TimerExpiredPayload{});
    for (int i = 0; i < 12; ++i) {
        wheel2.tick();
    }
    EXPECT_EQ(fire_count.load(), 1);
}

} // namespace
} // namespace beast::platform::engine::timer
