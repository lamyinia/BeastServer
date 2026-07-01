#pragma once

#include "beast/platform/plugin/route_handler.hpp"
#include "beast/platform/plugin/server_context.hpp"

#include "combat.pb.h"
#include "economy.pb.h"
#include "lifecycle.pb.h"
#include "match.pb.h"
#include "move.pb.h"
#include "ping.pb.h"

namespace beast::moba::pixel {

// 局内 c2s 路由注册（header-only）。
// wire_route（网络层）→ engine_route（引擎 on_event 匹配），
// engine_route 取 proto 注释 `route:...|...|wire|short` 的 short 字段。
inline void register_pixel_moba_routes(beast::platform::plugin::ServerContext& ctx) {
    namespace bp = beast::platform::plugin;

    // heroselect：选英雄。
    bp::register_instance_route<HeroSelectCmd>(ctx, "pixelmoba.heroselect", "hero_select");
    // ping：局内 RTT + tick 同步，引擎回 PongNotify（带 server tick）。
    bp::register_instance_route<PingCmd>(ctx, "pixelmoba.ping", "ping");
    // loadcomplete：客户端加载完成通知。
    bp::register_instance_route<LoadCompleteCmd>(ctx, "pixelmoba.loadcomplete", "load_complete");
    // move：移动输入。
    bp::register_instance_route<MoveCmd>(ctx, "pixelmoba.move", "move");
    // cast：释放技能。
    bp::register_instance_route<CastCmd>(ctx, "pixelmoba.cast", "cast");
    // attack：普通攻击(平A)。
    bp::register_instance_route<AttackCmd>(ctx, "pixelmoba.attack", "attack");
    // buy：购买装备。
    bp::register_instance_route<BuyItemCmd>(ctx, "pixelmoba.buy", "buy");
    // sell：出售装备。
    bp::register_instance_route<SellItemCmd>(ctx, "pixelmoba.sell", "sell");
    // levelupskill：升级技能。
    bp::register_instance_route<LevelUpSkillCmd>(ctx, "pixelmoba.levelupskill", "level_up_skill");
    // reconnect：断线重连,客户端请求全量快照。
    bp::register_instance_route<ReconnectCmd>(ctx, "pixelmoba.reconnect", "reconnect");
}

} // namespace beast::moba::pixel
