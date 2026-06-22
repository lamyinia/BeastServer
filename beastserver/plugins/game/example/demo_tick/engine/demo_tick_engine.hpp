#pragma once

#include "beast/platform/engine/instance/i_engine.hpp"

#include <atomic>
#include <memory>

namespace beast::demo::tick {

class DemoTickEngine final : public beast::platform::engine::instance::IEngine {
public:
    void on_tick(
        beast::platform::Tick tick,
        beast::platform::TimestampMs dt_ms) override;

    [[nodiscard]] int tick_count() const noexcept {
        return tick_count_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int> tick_count_{0};
};

std::unique_ptr<DemoTickEngine> make_demo_tick_engine();

} // namespace beast::demo::tick
