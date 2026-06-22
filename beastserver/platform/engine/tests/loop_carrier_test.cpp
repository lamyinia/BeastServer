#include "beast/platform/engine/carrier/loop_carrier.hpp"
#include "beast/platform/engine/carrier/loop_carrier_pool.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/instance.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace {

using namespace beast::platform;
using namespace beast::platform::engine;

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

core::config::LoopActorConfig test_loop_config() {
    core::config::LoopActorConfig config;
    config.count = 2;
    config.tick_hz = 30;
    config.max_tick_hz = 128;
    return config;
}

std::unique_ptr<instance::Instance> make_tick_instance(
    const InstanceId& id,
    TickEngine* engine_ptr,
    const std::uint32_t tick_hz) {
    auto engine = std::unique_ptr<instance::IEngine>(engine_ptr);
    context::EngineContext ctx(id, {}, nullptr);
    return std::make_unique<instance::Instance>(
        id,
        core::SimulationMode::FixedTick,
        std::vector<PlayerId>{},
        std::move(engine),
        std::move(ctx));
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

TEST(LoopCarrierTest, ProcessesEventsAndTicks) {
    carrier::LoopCarrier carrier(64, test_loop_config());
    carrier.start();

    auto* engine = new TickEngine();
    ASSERT_TRUE(carrier.add_instance(make_tick_instance("room-1", engine, 20), 20));

    wait_until([&]() { return engine->start_count.load() == 1; }, std::chrono::seconds(2));

    instance::InstanceEvent event;
    event.instance_id = "room-1";
    event.route = "game.move";
    ASSERT_TRUE(carrier.submit_event(event));

    wait_until([&]() { return engine->event_count.load() == 1; }, std::chrono::seconds(2));
    EXPECT_EQ(engine->last_route, "game.move");

    wait_until([&]() { return engine->tick_count.load() >= 3; }, std::chrono::seconds(2));

    ASSERT_TRUE(carrier.remove_instance("room-1"));
    wait_until([&]() { return engine->stop_count.load() == 1; }, std::chrono::seconds(2));

    carrier.stop();
}

TEST(LoopCarrierTest, MixedTickRatesOnSameCarrier) {
    carrier::LoopCarrier carrier(64, test_loop_config());
    carrier.start();

    auto* slow = new TickEngine();
    auto* fast = new TickEngine();
    ASSERT_TRUE(carrier.add_instance(make_tick_instance("slow", slow, 10), 10));
    ASSERT_TRUE(carrier.add_instance(make_tick_instance("fast", fast, 30), 30));

    wait_until(
        [&]() {
            return slow->start_count.load() == 1 && fast->start_count.load() == 1;
        },
        std::chrono::seconds(2));

    wait_until([&]() { return fast->tick_count.load() >= 15; }, std::chrono::seconds(3));

    const int slow_ticks = slow->tick_count.load();
    const int fast_ticks = fast->tick_count.load();
    EXPECT_GE(slow_ticks, 5);
    EXPECT_GE(fast_ticks, 15);
    EXPECT_GT(fast_ticks, slow_ticks);

    carrier.stop();
}

TEST(LoopCarrierPoolTest, SelectsLeastLoadedCarrier) {
    carrier::LoopCarrierPool pool(test_loop_config());
    pool.start();

    auto* carrier_a = pool.select_least_loaded_carrier();
    ASSERT_NE(carrier_a, nullptr);

    auto* engine_a = new TickEngine();
    ASSERT_TRUE(carrier_a->add_instance(make_tick_instance("room-a", engine_a, 20), 20));

    wait_until([&]() { return carrier_a->instance_count() == 1; }, std::chrono::seconds(2));

    auto* carrier_b = pool.select_least_loaded_carrier();
    ASSERT_NE(carrier_b, nullptr);
    EXPECT_NE(carrier_a, carrier_b);

    auto* engine_b = new TickEngine();
    ASSERT_TRUE(carrier_b->add_instance(make_tick_instance("room-b", engine_b, 20), 20));

    wait_until(
        [&]() {
            return engine_a->start_count.load() == 1 && engine_b->start_count.load() == 1;
        },
        std::chrono::seconds(2));

    EXPECT_EQ(pool.total_instances(), 2u);
    pool.stop();
}
