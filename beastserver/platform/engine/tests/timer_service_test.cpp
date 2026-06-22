#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/instance_manager.hpp"
#include "beast/platform/engine/timer/timer_service.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace {

using namespace beast::platform;
using namespace beast::platform::engine;

class TimerRecordingEngine final : public instance::IEngine {
public:
    void on_start(context::EngineContext& ctx) override { ctx_ = &ctx; }

    void on_event(const instance::InstanceEvent& event) override {
        event_count.fetch_add(1, std::memory_order_relaxed);
        last_route = event.route;
        last_payload = event.payload;
    }

    context::EngineContext* ctx_{nullptr};
    std::atomic<int> event_count{0};
    RouteId last_route;
    std::vector<std::uint8_t> last_payload;
};

core::config::RuntimeConfig test_runtime() {
    core::config::RuntimeConfig runtime;
    runtime.event_actors.count = 1;
    runtime.event_actors.queue_capacity = 64;
    runtime.timer_wheel.tick_duration_ms = 20;
    runtime.timer_wheel.wheel_size = 64;
    return runtime;
}

void wait_until(const std::function<bool()>& predicate, const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    FAIL() << "condition not met within timeout";
}

} // namespace

TEST(TimerServiceTest, PostsTimerEventToInstanceManager) {
    const auto runtime = test_runtime();
    instance::InstanceManager manager(runtime, nullptr);
    timer::TimerService timer_service(runtime.timer_wheel, &manager);

    manager.set_timer_service(&timer_service);
    manager.start();
    timer_service.start();

    TimerRecordingEngine* engine_ptr = nullptr;
    ASSERT_TRUE(manager.create_instance(
        "room-timer",
        core::SimulationMode::EventDriven,
        {"p1"},
        [&]() {
            auto engine = std::make_unique<TimerRecordingEngine>();
            engine_ptr = engine.get();
            return engine;
        }));

    wait_until([&]() { return engine_ptr && engine_ptr->ctx_; }, std::chrono::seconds(2));

    const auto handle = engine_ptr->ctx_->schedule_timer(
        60,
        "timer.turn_timeout",
        std::vector<std::uint8_t>{9});

    ASSERT_TRUE(handle.valid());

    wait_until([&]() { return engine_ptr->event_count.load() == 1; }, std::chrono::seconds(3));

    EXPECT_EQ(engine_ptr->last_route, "timer.turn_timeout");
    ASSERT_EQ(engine_ptr->last_payload.size(), 1u);
    EXPECT_EQ(engine_ptr->last_payload.front(), 9);

    timer_service.stop();
    manager.stop();
}

TEST(TimerServiceTest, CancelTimerBeforeFire) {
    const auto runtime = test_runtime();
    instance::InstanceManager manager(runtime, nullptr);
    timer::TimerService timer_service(runtime.timer_wheel, &manager);

    manager.set_timer_service(&timer_service);
    manager.start();
    timer_service.start();

    TimerRecordingEngine* engine_ptr = nullptr;
    ASSERT_TRUE(manager.create_instance(
        "room-cancel",
        core::SimulationMode::EventDriven,
        {},
        [&]() {
            auto engine = std::make_unique<TimerRecordingEngine>();
            engine_ptr = engine.get();
            return engine;
        }));

    wait_until([&]() { return engine_ptr && engine_ptr->ctx_; }, std::chrono::seconds(2));

    const auto handle = engine_ptr->ctx_->schedule_timer(200, "timer.expired");
    ASSERT_TRUE(handle.valid());
    engine_ptr->ctx_->cancel_timer(handle);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(engine_ptr->event_count.load(), 0);

    timer_service.stop();
    manager.stop();
}

TEST(TimerServiceTest, ConcurrentScheduleFromMultipleThreads) {
    const auto runtime = test_runtime();
    instance::InstanceManager manager(runtime, nullptr);
    timer::TimerService timer_service(runtime.timer_wheel, &manager);

    manager.set_timer_service(&timer_service);
    manager.start();
    timer_service.start();

    TimerRecordingEngine* engine_ptr = nullptr;
    ASSERT_TRUE(manager.create_instance(
        "room-concurrent",
        core::SimulationMode::EventDriven,
        {},
        [&]() {
            auto engine = std::make_unique<TimerRecordingEngine>();
            engine_ptr = engine.get();
            return engine;
        }));

    wait_until([&]() { return engine_ptr && engine_ptr->ctx_; }, std::chrono::seconds(2));

    std::atomic<int> scheduled{0};
    std::vector<std::thread> threads;
    threads.reserve(4);
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            if (engine_ptr->ctx_->schedule_timer(80, "timer.concurrent").valid()) {
                scheduled.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(scheduled.load(), 4);

    wait_until([&]() { return engine_ptr->event_count.load() >= 4; }, std::chrono::seconds(3));
    EXPECT_GE(engine_ptr->event_count.load(), 4);

    timer_service.stop();
    manager.stop();
}
