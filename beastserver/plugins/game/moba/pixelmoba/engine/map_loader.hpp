#pragma once

#include "beast/platform/bizutil/math/vector/vec2.hpp"
#include "engine/pathfinding.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace beast::platform::bizutil::config {
class BizConfigStore;
}

namespace beast::moba::pixel {

struct LaneData {
    std::string id;
    std::vector<Vec2f> path;  // 像素路径点
};

struct CampData {
    std::string id;
    std::string type;  // buff/wolf/bird/golem/boss
    Vec2f pos{};       // 营地中心(像素)
};

struct BaseData {
    std::string team;       // blue/red
    Vec2f spawn_pos{};      // 英雄出生点(像素)
    Vec2f core_pos{};       // 基地核心位置(像素),基地实体放置点
};

struct TowerConfigData {
    std::string id;
    std::string team;  // blue/red
    Vec2f pos{};       // 塔中心(像素)
};

struct MapData {
    std::uint32_t arena_id{0};
    std::uint32_t width{0};   // 瓦片数
    std::uint32_t height{0};  // 瓦片数
    std::shared_ptr<NavMesh> nav_mesh;
    std::vector<LaneData> lanes;
    std::vector<CampData> camps;
    std::vector<BaseData> bases;
    std::vector<TowerConfigData> towers;
};

// 从配表加载地图:解析 wall/lane/monster_spawn/base/tower,构建 NavMesh。
[[nodiscard]] std::shared_ptr<MapData> load_map(
    const beast::platform::bizutil::config::BizConfigStore& store,
    std::uint32_t arena_id);

// 房间创建时调试:打印 map 相关配表原始字段及 parse_rect/parse_point 结果。
void log_map_tables_debug(
    const beast::platform::bizutil::config::BizConfigStore& store,
    std::uint32_t arena_id);

} // namespace beast::moba::pixel
