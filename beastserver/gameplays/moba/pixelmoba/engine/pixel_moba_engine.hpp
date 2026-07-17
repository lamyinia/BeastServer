#pragma once

#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/instance/i_engine.hpp"
#include "beast/platform/engine/instance/instance_event.hpp"

#include "combat.pb.h"
#include "economy.pb.h"
#include "lifecycle.pb.h"
#include "match.pb.h"
#include "move.pb.h"
#include "ping.pb.h"
#include "sync.pb.h"

#include "engine/system.hpp"
#include "engine/world_state.hpp"
#include "engine/systems/combat_system.hpp"
#include "engine/systems/economy_system.hpp"
#include "engine/systems/map_system.hpp"
#include "engine/systems/match_system.hpp"
#include "engine/systems/movement_system.hpp"

#include <deque>
#include <memory>
#include <variant>

namespace beast::platform::engine::context {
class EngineContext;
}

namespace beast::moba::pixel {

// 局内玩家输入:on_event 收集(push 到 inputs_)→ on_tick 消费(dispatch 到各 System)。
//
// inputs_ 与 LoopCarrier 的 pending_events 不是冗余,职责不同:
//   - pending_events = 传输层缓冲,LoopCarrier 在 tick 之间积攒事件、按序投递给 on_event
//   - inputs_ = 模拟层队列,把状态变更推迟到 tick 边界,保证 FixedTick 确定性
// on_event 只做"监听"(收集 + log,不修改游戏状态),所有状态变更集中在 on_tick。
// loop_carrier 单线程 worker 驱动,on_event 与 on_tick 同线程顺序执行,inputs_ 无需加锁。
using PlayerInputPayload = std::variant<
    std::monostate,
    HeroSelectCmd,
    PingCmd,
    LoadCompleteCmd,
    MoveCmd,
    CastCmd,
    AttackCmd,
    BuyItemCmd,
    SellItemCmd,
    LevelUpSkillCmd,
    ReconnectCmd>;

struct PlayerInput {
    beast::platform::PlayerId player_id;
    PlayerInputPayload payload;
};

// Pixel MOBA 主引擎:FixedTick 60Hz(由 LoopCarrier 驱动)。
// 退化为薄协调器:持有 WorldState + 5 个 System,on_tick 顺序为
//   dispatch inputs → tick systems(movement→combat→economy→map→match) → broadcast snapshot。
class PixelMobaEngine final : public beast::platform::engine::instance::IEngine {
public:
    void on_start(beast::platform::engine::context::EngineContext& ctx) override;
    void on_event(const beast::platform::engine::instance::InstanceEvent& event) override;
    void on_tick(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms) override;

private:
    // 监听层:monitor_event.cpp 实现,仅收集输入到 inputs_ + log(不做游戏逻辑)。
    void on_hero_select(const beast::platform::PlayerId& player_id, const HeroSelectCmd& request);
    void on_ping(const beast::platform::PlayerId& player_id, const PingCmd& request);
    void on_load_complete(const beast::platform::PlayerId& player_id, const LoadCompleteCmd& request);
    void on_move(const beast::platform::PlayerId& player_id, const MoveCmd& request);
    void on_cast(const beast::platform::PlayerId& player_id, const CastCmd& request);
    void on_attack(const beast::platform::PlayerId& player_id, const AttackCmd& request);
    void on_buy(const beast::platform::PlayerId& player_id, const BuyItemCmd& request);
    void on_sell(const beast::platform::PlayerId& player_id, const SellItemCmd& request);
    void on_level_up_skill(const beast::platform::PlayerId& player_id, const LevelUpSkillCmd& request);
    void on_reconnect(const beast::platform::PlayerId& player_id, const ReconnectCmd& request);

    // on_tick 内:按 variant 类型路由输入到对应 System.consume。
    void dispatch_input(const PlayerInput& in);
    // on_tick 内:顺序驱动各 System.tick。
    void tick_systems(beast::platform::Tick tick, beast::platform::TimestampMs dt_ms);
    // on_tick 内:分层 sync 广播(Tier1 高频 AOI + Tier2/3 事件驱动全播)。
    void broadcast_sync();

    // Tier 1:高频快照,按玩家视野 AOI 裁剪后单播。
    void broadcast_transform();
    void broadcast_projectiles();
    // Tier 2:事件驱动,广播全玩家(属性/buff/技能槽),发后清 dirty。
    void broadcast_attr_dirty();
    void broadcast_buff_dirty();
    void broadcast_skill_dirty();
    // Tier 3:静态/低频,广播全玩家(塔/野怪营),发后清 dirty。
    void broadcast_tower_dirty();
    void broadcast_monster_dirty();
    // 断线重连:向该玩家单播全量快照(ReconnectAck + 各 Tier 全量消息)。
    void send_reconnect_snapshot(const beast::platform::PlayerId& player_id);

    beast::platform::engine::context::EngineContext* ctx_{nullptr};
    beast::platform::Tick tick_{0};
    std::deque<PlayerInput> inputs_;

    WorldState      world_;
    MatchSystem     match_;
    MovementSystem  movement_;
    CombatSystem    combat_;
    EconomySystem   economy_;
    MapSystem       map_;
};

[[nodiscard]] std::unique_ptr<PixelMobaEngine> make_pixel_moba_engine();

} // namespace beast::moba::pixel
