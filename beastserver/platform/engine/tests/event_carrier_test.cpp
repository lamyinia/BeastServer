#include "beast/platform/engine/carrier/event_carrier.hpp"
#include "beast/platform/engine/carrier/event_carrier_pool.hpp"
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

class CountingEngine final : public instance::IEngine {
public:
    void on_start(context::EngineContext& /*ctx*/) override {
        start_count.fetch_add(1, std::memory_order_relaxed);
    }

    void on_event(const instance::InstanceEvent& event) override {
        event_count.fetch_add(1, std::memory_order_relaxed);
        last_route = event.route;
    }

    void on_stop(context::EngineContext& /*ctx*/) override {
        stop_count.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<int> start_count{0};
    std::atomic<int> event_count{0};
    std::atomic<int> stop_count{0};
    RouteId last_route;
};

std::unique_ptr<instance::Instance> make_instance(
    const InstanceId& id,
    instance::IEngine* engine_ptr,
    std::vector<PlayerId> players = {}) {
    auto engine = std::unique_ptr<instance::IEngine>(engine_ptr);
    context::EngineContext ctx(id, players, nullptr);
    return std::make_unique<instance::Instance>(
        id,
        core::SimulationMode::EventDriven,
        std::move(players),
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

TEST(EventCarrierTest, ProcessesAddInstanceAndEvents) {
    carrier::EventCarrier carrier(64);
    carrier.start();

    auto* engine = new CountingEngine();
    ASSERT_TRUE(carrier.add_instance(make_instance("room-1", engine)));

    wait_until([&]() { return engine->start_count.load() == 1; }, std::chrono::seconds(2));

    instance::InstanceEvent event;
    event.instance_id = "room-1";
    event.route = "game.play";
    ASSERT_TRUE(carrier.submit_event(event));

    wait_until([&]() { return engine->event_count.load() == 1; }, std::chrono::seconds(2));
    EXPECT_EQ(engine->last_route, "game.play");
    EXPECT_EQ(carrier.instance_count(), 1u);

    ASSERT_TRUE(carrier.remove_instance("room-1"));
    wait_until([&]() { return engine->stop_count.load() == 1; }, std::chrono::seconds(2));
    EXPECT_EQ(carrier.instance_count(), 0u);

    carrier.stop();
}

TEST(EventCarrierTest, RemovesInstanceAfterNotifyEnd) {
    carrier::EventCarrier carrier(64);
    carrier.start();

    class EndingEngine final : public instance::IEngine {
    public:
        EndingEngine(carrier::EventCarrier* owner, InstanceId instance_id)
            : owner_(owner)
            , instance_id_(std::move(instance_id)) {}

        void on_event(const instance::InstanceEvent& /*event*/) override {
            if (owner_) {
                owner_->mark_instance_ended(instance_id_);
            }
        }

        void on_stop(context::EngineContext& /*ctx*/) override {
            stop_count.fetch_add(1, std::memory_order_relaxed);
        }

        carrier::EventCarrier* owner_{nullptr};
        InstanceId instance_id_;
        std::atomic<int> stop_count{0};
    };

    auto* engine = new EndingEngine(&carrier, "room-end");
    ASSERT_TRUE(carrier.add_instance(make_instance("room-end", engine)));
    wait_until([&]() { return carrier.instance_count() == 1; }, std::chrono::seconds(2));

    instance::InstanceEvent event;
    event.instance_id = "room-end";
    event.route = "game.finish";
    ASSERT_TRUE(carrier.submit_event(event));

    wait_until([&]() { return carrier.instance_count() == 0; }, std::chrono::seconds(2));
    EXPECT_EQ(engine->stop_count.load(), 1);
    carrier.stop();
}

TEST(EventCarrierPoolTest, SelectsLeastLoadedCarrier) {
    carrier::EventCarrierPool pool(2, 64);
    pool.start();

    auto* carrier_a = pool.select_least_loaded_carrier();
    ASSERT_NE(carrier_a, nullptr);

    auto* engine_a = new CountingEngine();
    ASSERT_TRUE(carrier_a->add_instance(make_instance("room-a", engine_a)));

    auto* carrier_b = pool.select_least_loaded_carrier();
    ASSERT_NE(carrier_b, nullptr);

    auto* engine_b = new CountingEngine();
    ASSERT_TRUE(carrier_b->add_instance(make_instance("room-b", engine_b)));

    wait_until(
        [&]() {
            return engine_a->start_count.load() == 1 && engine_b->start_count.load() == 1;
        },
        std::chrono::seconds(2));

    instance::InstanceEvent event_a;
    event_a.instance_id = "room-a";
    event_a.route = "a";
    instance::InstanceEvent event_b;
    event_b.instance_id = "room-b";
    event_b.route = "b";

    ASSERT_TRUE(carrier_a->submit_event(event_a));
    ASSERT_TRUE(carrier_b->submit_event(event_b));

    wait_until(
        [&]() {
            return engine_a->event_count.load() == 1 && engine_b->event_count.load() == 1;
        },
        std::chrono::seconds(2));

    EXPECT_EQ(engine_a->last_route, "a");
    EXPECT_EQ(engine_b->last_route, "b");
    EXPECT_EQ(pool.total_instances(), 2u);

    pool.stop();
}
