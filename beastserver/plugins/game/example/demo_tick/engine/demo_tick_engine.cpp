#include "engine/demo_tick_engine.hpp"

#include <memory>

namespace beast::demo::tick {

void DemoTickEngine::on_tick(
    beast::platform::Tick /*tick*/,
    beast::platform::TimestampMs /*dt_ms*/) {
    tick_count_.fetch_add(1, std::memory_order_relaxed);
}

std::unique_ptr<DemoTickEngine> make_demo_tick_engine() {
    return std::make_unique<DemoTickEngine>();
}

} // namespace beast::demo::tick
