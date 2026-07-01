#pragma once

#include "engine/world_state.hpp"

#include "beast/platform/core/log/logger.hpp"
#include "beast/platform/core/types.hpp"
#include "beast/platform/engine/context/engine_context.hpp"

#include "combat.pb.h"
#include "economy.pb.h"
#include "move.pb.h"

#include <type_traits>

namespace beast::moba::pixel {

[[nodiscard]] inline bool gameplay_open(const WorldState& world) noexcept {
    return world.match_started && !world.match_ended;
}

// gameplay 未开放时返回拒绝原因;否则 nullptr。
[[nodiscard]] inline const char* gameplay_reject_reason(const WorldState& world) noexcept {
    if (world.match_ended) return "match_ended";
    if (!world.match_started) return "match_not_started";
    return nullptr;
}

template<typename T>
inline constexpr bool is_gameplay_cmd_v =
    std::is_same_v<T, MoveCmd> || std::is_same_v<T, CastCmd> || std::is_same_v<T, AttackCmd> ||
    std::is_same_v<T, BuyItemCmd> || std::is_same_v<T, SellItemCmd> || std::is_same_v<T, LevelUpSkillCmd>;

inline void reject_gameplay_cmd(
    beast::platform::engine::context::EngineContext& ctx,
    const beast::platform::PlayerId& player_id,
    const CastCmd& cmd,
    const char* reason) {
    CastResult r;
    r.set_skill_id(cmd.skill_id());
    r.set_success(false);
    r.set_error_msg(reason);
    ctx.send(player_id, "pixelmoba.castresult", r);
}

inline void reject_gameplay_cmd(
    beast::platform::engine::context::EngineContext& ctx,
    const beast::platform::PlayerId& player_id,
    const BuyItemCmd& cmd,
    const char* reason) {
    BuyItemResult r;
    r.set_item_id(cmd.item_id());
    r.set_success(false);
    r.set_error_msg(reason);
    ctx.send(player_id, "pixelmoba.buyresult", r);
}

inline void reject_gameplay_cmd(
    beast::platform::engine::context::EngineContext& ctx,
    const beast::platform::PlayerId& player_id,
    const SellItemCmd& cmd,
    const char* reason) {
    SellItemResult r;
    r.set_item_id(cmd.item_id());
    r.set_success(false);
    r.set_error_msg(reason);
    ctx.send(player_id, "pixelmoba.sellresult", r);
}

inline void reject_gameplay_cmd(
    const beast::platform::PlayerId& player_id,
    const MoveCmd& /*cmd*/,
    const char* reason) {
    BEAST_LOG_DEBUG("gameplay rejected move player={} reason={}", player_id, reason);
}

inline void reject_gameplay_cmd(
    const beast::platform::PlayerId& player_id,
    const AttackCmd& /*cmd*/,
    const char* reason) {
    BEAST_LOG_DEBUG("gameplay rejected attack player={} reason={}", player_id, reason);
}

inline void reject_gameplay_cmd(
    const beast::platform::PlayerId& player_id,
    const LevelUpSkillCmd& cmd,
    const char* reason) {
    BEAST_LOG_DEBUG(
        "gameplay rejected level_up_skill player={} skill={} reason={}",
        player_id,
        cmd.skill_id(),
        reason);
}

// 统一入口:有 Result proto 的命令需 ctx;Move/Attack/LevelUp 仅 DEBUG log。
template<typename Cmd>
inline void reject_gameplay_cmd(
    beast::platform::engine::context::EngineContext* ctx,
    const beast::platform::PlayerId& player_id,
    const Cmd& cmd,
    const char* reason) {
    if constexpr (std::is_same_v<Cmd, MoveCmd> || std::is_same_v<Cmd, AttackCmd> ||
                  std::is_same_v<Cmd, LevelUpSkillCmd>) {
        reject_gameplay_cmd(player_id, cmd, reason);
    } else {
        if (ctx != nullptr) {
            reject_gameplay_cmd(*ctx, player_id, cmd, reason);
        } else {
            BEAST_LOG_DEBUG("gameplay rejected player={} reason={} (no ctx)", player_id, reason);
        }
    }
}

} // namespace beast::moba::pixel
