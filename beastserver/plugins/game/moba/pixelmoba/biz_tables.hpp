#pragma once

namespace beast::moba::pixel {

// manifest.json / register_biz_table 使用的逻辑表名，须与 biz_export 导出一致。
// 共 15 张 moba/pixel_moba 表。
inline constexpr const char* kEffectTableLogicalName = "moba/pixel_moba/effect";
inline constexpr const char* kHeroLevelBonusTableLogicalName = "moba/pixel_moba/hero_level_bonus";
inline constexpr const char* kHeroProfilesTableLogicalName = "moba/pixel_moba/hero_profiles";
inline constexpr const char* kItemTableLogicalName = "moba/pixel_moba/item";
inline constexpr const char* kMapArenaTableLogicalName = "moba/pixel_moba/map_arena";
inline constexpr const char* kMapBaseTableLogicalName = "moba/pixel_moba/map_base";
inline constexpr const char* kMapBushTableLogicalName = "moba/pixel_moba/map_bush";
inline constexpr const char* kMapLaneTableLogicalName = "moba/pixel_moba/map_lane";
inline constexpr const char* kMapMonsterSpawnTableLogicalName = "moba/pixel_moba/map_monster_spawn";
inline constexpr const char* kMapRiverTableLogicalName = "moba/pixel_moba/map_river";
inline constexpr const char* kMapTowerTableLogicalName = "moba/pixel_moba/map_tower";
inline constexpr const char* kMapWallTableLogicalName = "moba/pixel_moba/map_wall";
inline constexpr const char* kSkillTableLogicalName = "moba/pixel_moba/skill";
inline constexpr const char* kSkillLevelTableLogicalName = "moba/pixel_moba/skill_level";
inline constexpr const char* kUnitTableLogicalName = "moba/pixel_moba/unit";

} // namespace beast::moba::pixel
