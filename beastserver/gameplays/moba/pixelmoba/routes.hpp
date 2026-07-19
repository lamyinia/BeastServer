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
//
// InstanceEvent::route 始终存 wire_route（不再有 engine_route alias），
// 故 engine on_event 里的 BEAST_ENGINE_EVENT_PROTO_* 宏也用 wire_route 字符串匹配。
inline void register_pixel_moba_routes(beast::platform::plugin::ServerContext& ctx) {
    namespace bp = beast::platform::plugin;

    // heroselect：选英雄。
    bp::register_instance_route<HeroSelectCmd>(ctx, "pixelmoba.heroselect");
    // ping：局内 RTT + tick 同步，引擎回 PongNotify（带 server tick）。
    bp::register_instance_route<PingCmd>(ctx, "pixelmoba.ping");
    // loadcomplete：客户端加载完成通知。
    bp::register_instance_route<LoadCompleteCmd>(ctx, "pixelmoba.loadcomplete");
    // move：移动输入。
    bp::register_instance_route<MoveCmd>(ctx, "pixelmoba.move");
    // cast：释放技能。
    bp::register_instance_route<CastCmd>(ctx, "pixelmoba.cast");
    // attack：普通攻击(平A)。
    bp::register_instance_route<AttackCmd>(ctx, "pixelmoba.attack");
    // buy：购买装备。
    bp::register_instance_route<BuyItemCmd>(ctx, "pixelmoba.buy");
    // sell：出售装备。
    bp::register_instance_route<SellItemCmd>(ctx, "pixelmoba.sell");
    // levelupskill：升级技能。
    bp::register_instance_route<LevelUpSkillCmd>(ctx, "pixelmoba.levelupskill");
    // reconnect：断线重连,客户端请求全量快照。
    bp::register_instance_route<ReconnectCmd>(ctx, "pixelmoba.reconnect");
}

} // namespace beast::moba::pixel
