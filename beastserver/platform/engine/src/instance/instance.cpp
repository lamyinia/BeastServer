#include "beast/platform/engine/instance/instance.hpp"

namespace beast::platform::engine::instance {

Instance::Instance(
    InstanceId id,
    const core::SimulationMode mode,
    std::vector<PlayerId> player_ids,
    std::unique_ptr<IEngine> engine,
    context::EngineContext context)
    : id_(std::move(id))
    , mode_(mode)
    , player_ids_(std::move(player_ids))
    , engine_(std::move(engine))
    , context_(std::move(context)) {}

void Instance::start() {
    if (started_) {
        return;
    }
    started_ = true;
    engine_->on_start(context_);
}

void Instance::stop() {
    if (!started_) {
        return;
    }
    engine_->on_stop(context_);
    started_ = false;
}

} // namespace beast::platform::engine::instance
