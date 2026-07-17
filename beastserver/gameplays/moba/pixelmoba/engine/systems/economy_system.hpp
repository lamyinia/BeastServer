#pragma once

#include "engine/system.hpp"

#include "economy.pb.h"

namespace beast::moba::pixel {

// 经济:购买装备、被动金币、经验/升级。
class EconomySystem final : public System {
public:
    void on_start(beast::platform::engine::context::EngineContext& ctx, WorldState& world) override;
    void tick(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms) override;

    void consume(const beast::platform::PlayerId& player_id, const BuyItemCmd& cmd);
    void consume(const beast::platform::PlayerId& player_id, const SellItemCmd& cmd);

private:
    beast::platform::engine::context::EngineContext* ctx_{nullptr};
    WorldState* world_{nullptr};
};

} // namespace beast::moba::pixel
