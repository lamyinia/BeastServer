#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/context/engine_context.hpp"
#include "beast/platform/engine/instance/i_engine.hpp"

#include <memory>
#include <vector>

namespace beast::platform::engine::instance {

class Instance {
public:
    Instance(
        InstanceId id,
        core::SimulationMode mode,
        std::vector<PlayerId> player_ids,
        std::unique_ptr<IEngine> engine,
        context::EngineContext context);

    [[nodiscard]] const InstanceId& id() const noexcept { return id_; }
    [[nodiscard]] core::SimulationMode mode() const noexcept { return mode_; }
    [[nodiscard]] const std::vector<PlayerId>& player_ids() const noexcept { return player_ids_; }
    [[nodiscard]] IEngine& engine() noexcept { return *engine_; }
    [[nodiscard]] context::EngineContext& context() noexcept { return context_; }

    void start();
    void stop();

private:
    InstanceId id_;
    core::SimulationMode mode_;
    std::vector<PlayerId> player_ids_;
    std::unique_ptr<IEngine> engine_;
    context::EngineContext context_;
    bool started_{false};
};

} // namespace beast::platform::engine::instance
