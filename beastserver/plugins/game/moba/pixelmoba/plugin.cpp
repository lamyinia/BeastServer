#include "biz_tables.hpp"
#include "engine/pixel_moba_engine.hpp"
#include "routes.hpp"

#include "beast/platform/plugin/plugin_api.hpp"

#include "effect.pb.h"
#include "hero_level_bonus.pb.h"
#include "hero_profiles.pb.h"
#include "item.pb.h"
#include "map_arena.pb.h"
#include "map_base.pb.h"
#include "map_bush.pb.h"
#include "map_lane.pb.h"
#include "map_monster_spawn.pb.h"
#include "map_river.pb.h"
#include "map_tower.pb.h"
#include "map_wall.pb.h"
#include "skill.pb.h"
#include "skill_level.pb.h"
#include "unit.pb.h"

BEAST_PLUGIN_EXPORT void beast_plugin_init(beast::platform::plugin::ServerContext& ctx) {
    using namespace beast::moba::pixel;
    namespace biz = beast::biz::moba::pixel_moba;

    // 1) 注册策划表（15 张 moba/pixel_moba 表，logical_name 与 manifest.json 一致）。
    ctx.register_biz_table<biz::effect::EffectServerConfig>(kEffectTableLogicalName);
    ctx.register_biz_table<biz::hero_level_bonus::HeroLevelBonusServerConfig>(kHeroLevelBonusTableLogicalName);
    ctx.register_biz_table<biz::hero_profiles::HeroProfilesServerConfig>(kHeroProfilesTableLogicalName);
    ctx.register_biz_table<biz::item::ItemServerConfig>(kItemTableLogicalName);
    ctx.register_biz_table<biz::map_arena::MapArenaServerConfig>(kMapArenaTableLogicalName);
    ctx.register_biz_table<biz::map_base::MapBaseServerConfig>(kMapBaseTableLogicalName);
    ctx.register_biz_table<biz::map_bush::MapBushServerConfig>(kMapBushTableLogicalName);
    ctx.register_biz_table<biz::map_lane::MapLaneServerConfig>(kMapLaneTableLogicalName);
    ctx.register_biz_table<biz::map_monster_spawn::MapMonsterSpawnServerConfig>(kMapMonsterSpawnTableLogicalName);
    ctx.register_biz_table<biz::map_river::MapRiverServerConfig>(kMapRiverTableLogicalName);
    ctx.register_biz_table<biz::map_tower::MapTowerServerConfig>(kMapTowerTableLogicalName);
    ctx.register_biz_table<biz::map_wall::MapWallServerConfig>(kMapWallTableLogicalName);
    ctx.register_biz_table<biz::skill::SkillServerConfig>(kSkillTableLogicalName);
    ctx.register_biz_table<biz::skill_level::SkillLevelServerConfig>(kSkillLevelTableLogicalName);
    ctx.register_biz_table<biz::unit::UnitServerConfig>(kUnitTableLogicalName);

    // 2) 注册引擎：FixedTick 60Hz，由 LoopCarrier 驱动单线程帧循环。
    ctx.register_engine({
        .plugin_name = "pixelmoba",
        .engine_name = "pixelmoba",
        .mode = beast::platform::core::SimulationMode::FixedTick,
        .tick_hz = 60,
        .factory = []() { return make_pixel_moba_engine(); },
    });

    // 3) 注册局内 c2s 路由。
    register_pixel_moba_routes(ctx);
}
