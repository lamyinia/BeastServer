#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/instance_manager.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace {

using namespace beast::platform;
using namespace beast::platform::engine;

class EchoEngine final : public instance::IEngine {
public:
    void on_start(context::EngineContext& ctx) override {
        ctx_ = &ctx;
        start_count.fetch_add(1, std::memory_order_relaxed);
    }

    void on_event(const instance::InstanceEvent& event) override {
        event_count.fetch_add(1, std::memory_order_relaxed);
        last_route = event.route;
        if (ctx_ && end_on_event_) {
            ctx_->notify_instance_end();
        }
    }

    void on_stop(context::EngineContext& /*ctx*/) override {
        stop_count.fetch_add(1, std::memory_order_relaxed);
    }

    void set_end_on_event(bool value) { end_on_event_ = value; }

    context::EngineContext* ctx_{nullptr};
    bool end_on_event_{false};
    std::atomic<int> start_count{0};
    std::atomic<int> event_count{0};
    std::atomic<int> stop_count{0};
    RouteId last_route;
};

class TickEngine final : public instance::IEngine {
public:
    void on_start(context::EngineContext& /*ctx*/) override {
        start_count.fetch_add(1, std::memory_order_relaxed);
    }

    void on_event(const instance::InstanceEvent& event) override {
        event_count.fetch_add(1, std::memory_order_relaxed);
        last_route = event.route;
    }

    void on_tick(Tick /*tick*/, TimestampMs /*dt_ms*/) override {
        tick_count.fetch_add(1, std::memory_order_relaxed);
    }

    void on_stop(context::EngineContext& /*ctx*/) override {
        stop_count.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<int> start_count{0};
    std::atomic<int> event_count{0};
    std::atomic<int> tick_count{0};
    std::atomic<int> stop_count{0};
    RouteId last_route;
};

core::config::RuntimeConfig test_runtime() {
    core::config::RuntimeConfig runtime;
    runtime.event_actors.count = 2;
    runtime.event_actors.queue_capacity = 64;
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

TEST(InstanceManagerTest, CreatesInstanceAndDeliversEvents) {
    instance::InstanceManager manager(test_runtime(), nullptr);
    manager.start();

    EchoEngine* engine_ptr = nullptr;
    ASSERT_TRUE(manager.create_instance(
        "room-1",
        core::SimulationMode::EventDriven,
        {"p1", "p2"},
        [&]() {
            auto engine = std::make_unique<EchoEngine>();
            engine_ptr = engine.get();
            return engine;
        }));

    wait_until([&]() { return engine_ptr && engine_ptr->start_count.load() == 1; }, std::chrono::seconds(2));
    EXPECT_TRUE(manager.has_instance("room-1"));
    EXPECT_EQ(manager.instance_count(), 1u);

    instance::InstanceEvent event;
    event.instance_id = "room-1";
    event.player_id = "p1";
    event.route = "game.play";
    ASSERT_TRUE(manager.submit_event(event));

    wait_until([&]() { return engine_ptr->event_count.load() == 1; }, std::chrono::seconds(2));
    EXPECT_EQ(engine_ptr->last_route, "game.play");

    ASSERT_TRUE(manager.destroy_instance("room-1"));
    wait_until([&]() { return engine_ptr->stop_count.load() == 1; }, std::chrono::seconds(2));
    EXPECT_FALSE(manager.has_instance("room-1"));

    manager.stop();
}

TEST(InstanceManagerTest, RejectsDuplicateInstanceId) {
    instance::InstanceManager manager(test_runtime(), nullptr);
    manager.start();

    const auto factory = []() { return std::make_unique<EchoEngine>(); };
    ASSERT_TRUE(manager.create_instance("room-dup", core::SimulationMode::EventDriven, {}, factory));
    EXPECT_FALSE(manager.create_instance("room-dup", core::SimulationMode::EventDriven, {}, factory));

    manager.stop();
}

TEST(InstanceManagerTest, NotifiesWhenInstanceEnds) {
    instance::InstanceManager manager(test_runtime(), nullptr);
    manager.start();

    EchoEngine* engine_ptr = nullptr;
    std::atomic<int> ended_count{0};
    InstanceId ended_id;
    manager.set_instance_ended_fn([&](const InstanceId& id) {
        ended_count.fetch_add(1, std::memory_order_relaxed);
        ended_id = id;
    });

    ASSERT_TRUE(manager.create_instance(
        "room-end",
        core::SimulationMode::EventDriven,
        {},
        [&]() {
            auto engine = std::make_unique<EchoEngine>();
            engine_ptr = engine.get();
            engine->set_end_on_event(true);
            return engine;
        }));

    wait_until([&]() { return engine_ptr && engine_ptr->start_count.load() == 1; }, std::chrono::seconds(2));

    instance::InstanceEvent event;
    event.instance_id = "room-end";
    event.route = "game.finish";
    ASSERT_TRUE(manager.submit_event(event));

    wait_until([&]() { return ended_count.load() == 1; }, std::chrono::seconds(2));
    EXPECT_EQ(ended_id, "room-end");
    EXPECT_FALSE(manager.has_instance("room-end"));

    manager.stop();
}

TEST(InstanceManagerTest, CreatesFixedTickInstanceAndTicks) {
    core::config::RuntimeConfig runtime = test_runtime();
    runtime.loop_actors.count = 1;
    runtime.loop_actors.tick_hz = 30;
    runtime.loop_actors.max_tick_hz = 128;

    instance::InstanceManager manager(runtime, nullptr);
    manager.start();

    TickEngine* engine_ptr = nullptr;
    ASSERT_TRUE(manager.create_instance(
        "tick-room",
        core::SimulationMode::FixedTick,
        {},
        [&]() {
            auto engine = std::make_unique<TickEngine>();
            engine_ptr = engine.get();
            return engine;
        },
        20));

    wait_until([&]() { return engine_ptr && engine_ptr->start_count.load() == 1; }, std::chrono::seconds(2));
    wait_until([&]() { return engine_ptr->tick_count.load() >= 3; }, std::chrono::seconds(2));

    instance::InstanceEvent event;
    event.instance_id = "tick-room";
    event.route = "game.input";
    ASSERT_TRUE(manager.submit_event(event));
    wait_until([&]() { return engine_ptr->event_count.load() == 1; }, std::chrono::seconds(2));

    ASSERT_TRUE(manager.destroy_instance("tick-room"));
    wait_until([&]() { return engine_ptr->stop_count.load() == 1; }, std::chrono::seconds(2));

    manager.stop();
}
