#include "engine/pixel_moba_engine.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/engine/instance/instance_event_dispatch.hpp"

namespace beast::moba::pixel {

// 监听层:LoopCarrier::run_instance_frame 帧内、on_tick 之前逐条调用。
// 仅收集输入到 inputs_ + log(监听),不做游戏逻辑;真正消费在 on_tick 的 consume_inputs。
void PixelMobaEngine::on_event(const beast::platform::engine::instance::InstanceEvent& event) {
    BEAST_ENGINE_EVENT_SWITCH(event)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("hero_select", HeroSelectCmd, on_hero_select)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("ping", PingCmd, on_ping)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("load_complete", LoadCompleteCmd, on_load_complete)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("move", MoveCmd, on_move)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("cast", CastCmd, on_cast)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("attack", AttackCmd, on_attack)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("buy", BuyItemCmd, on_buy)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("sell", SellItemCmd, on_sell)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("level_up_skill", LevelUpSkillCmd, on_level_up_skill)
        BEAST_ENGINE_EVENT_PROTO_PLAYER_THIS("reconnect", ReconnectCmd, on_reconnect)
    BEAST_ENGINE_EVENT_SWITCH_END
}

void PixelMobaEngine::on_hero_select(
    const beast::platform::PlayerId& player_id, const HeroSelectCmd& request) {
    inputs_.push_back({player_id, request});
    BEAST_LOG_INFO("pixel_moba on_event hero_select player={} hero={}", player_id, request.hero_id());
}

void PixelMobaEngine::on_ping(
    const beast::platform::PlayerId& player_id, const PingCmd& request) {
    inputs_.push_back({player_id, request});
    BEAST_LOG_INFO("pixel_moba on_event ping player={} ts={}", player_id, request.client_ts());
}

void PixelMobaEngine::on_load_complete(
    const beast::platform::PlayerId& player_id, const LoadCompleteCmd& request) {
    inputs_.push_back({player_id, request});
    BEAST_LOG_INFO("pixel_moba on_event load_complete player={} match={}", player_id, request.match_id());
}

void PixelMobaEngine::on_move(
    const beast::platform::PlayerId& player_id, const MoveCmd& request) {
    inputs_.push_back({player_id, request});
    BEAST_LOG_INFO(
        "pixel_moba on_event move player={} dir=({},{}) path_size={}",
        player_id,
        request.dir().x(),
        request.dir().y(),
        request.path_size());
}

void PixelMobaEngine::on_cast(
    const beast::platform::PlayerId& player_id, const CastCmd& request) {
    inputs_.push_back({player_id, request});
    BEAST_LOG_INFO(
        "pixel_moba on_event cast player={} skill={} target={}",
        player_id,
        request.skill_id(),
        request.target_entity_id());
}

void PixelMobaEngine::on_buy(
    const beast::platform::PlayerId& player_id, const BuyItemCmd& request) {
    inputs_.push_back({player_id, request});
    BEAST_LOG_INFO("pixel_moba on_event buy player={} item={}", player_id, request.item_id());
}

void PixelMobaEngine::on_level_up_skill(
    const beast::platform::PlayerId& player_id, const LevelUpSkillCmd& request) {
    inputs_.push_back({player_id, request});
    BEAST_LOG_INFO("pixel_moba on_event level_up_skill player={} skill={}", player_id, request.skill_id());
}

void PixelMobaEngine::on_attack(
    const beast::platform::PlayerId& player_id, const AttackCmd& request) {
    inputs_.push_back({player_id, request});
    BEAST_LOG_INFO("pixel_moba on_event attack player={} target={}", player_id, request.target_entity_id());
}

void PixelMobaEngine::on_sell(
    const beast::platform::PlayerId& player_id, const SellItemCmd& request) {
    inputs_.push_back({player_id, request});
    BEAST_LOG_INFO("pixel_moba on_event sell player={} item={}", player_id, request.item_id());
}

void PixelMobaEngine::on_reconnect(
    const beast::platform::PlayerId& player_id, const ReconnectCmd& request) {
    inputs_.push_back({player_id, request});
    BEAST_LOG_INFO("pixel_moba on_event reconnect player={} ts={}", player_id, request.client_ts());
}

} // namespace beast::moba::pixel
