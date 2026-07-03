#include "engine/map_loader.hpp"

#include "biz_tables.hpp"

#include "beast/platform/bizutil/config/store.hpp"
#include "beast/platform/core/log/logger.hpp"
#include "map_arena.pb.h"
#include "map_base.pb.h"
#include "map_bush.pb.h"
#include "map_lane.pb.h"
#include "map_monster_spawn.pb.h"
#include "map_tower.pb.h"
#include "map_wall.pb.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace beast::moba::pixel {
namespace biz = beast::biz::moba::pixel_moba;

namespace {

// biz_export 可能导出 JSON 数组字面量,如 "[60,0,30,18]" 或 "[74,14]"。
std::string normalize_coord_string(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '['), s.end());
    s.erase(std::remove(s.begin(), s.end(), ']'), s.end());
    return s;
}

std::string trim_ascii(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

std::vector<int> parse_ints(const std::string& s, char delim) {
    std::vector<int> result;
    const std::string normalized = normalize_coord_string(s);
    std::string token;
    std::istringstream iss(normalized);
    while (std::getline(iss, token, delim)) {
        token = trim_ascii(std::move(token));
        if (token.empty()) continue;
        try {
            result.push_back(std::stoi(token));
        } catch (...) {
        }
    }
    return result;
}

struct TileRect { int x, y, w, h; };
std::optional<TileRect> parse_rect(const std::string& s) {
    const auto v = parse_ints(s, ',');
    if (v.size() < 4) return std::nullopt;
    return TileRect{v[0], v[1], v[2], v[3]};
}

std::optional<std::pair<int, int>> parse_point(const std::string& s) {
    const auto v = parse_ints(s, ',');
    if (v.size() < 2) return std::nullopt;
    return std::make_pair(v[0], v[1]);
}

std::vector<std::pair<int, int>> parse_path(const std::string& s) {
    std::vector<std::pair<int, int>> result;
    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, ';')) {
        auto p = parse_point(token);
        if (p) result.push_back(*p);
    }
    return result;
}

Vec2f tile_to_pixel(int tx, int ty) {
    return {static_cast<float>(tx) * kTilePx, static_cast<float>(ty) * kTilePx};
}

void log_field_rect(const char* table, const char* row, const char* field, const std::string& raw) {
    BEAST_LOG_INFO("map_debug: {} row={} {} raw=\"{}\"", table, row, field, raw);
    const auto r = parse_rect(raw);
    if (r) {
        BEAST_LOG_INFO(
            "map_debug: {} row={} {} parsed x={} y={} w={} h={} (top-left tile + size)",
            table, row, field, r->x, r->y, r->w, r->h);
    } else {
        BEAST_LOG_WARN("map_debug: {} row={} {} parse_rect FAILED", table, row, field);
    }
}

void log_field_point(const char* table, const char* row, const char* field, const std::string& raw) {
    BEAST_LOG_INFO("map_debug: {} row={} {} raw=\"{}\"", table, row, field, raw);
    const auto p = parse_point(raw);
    if (p) {
        BEAST_LOG_INFO("map_debug: {} row={} {} parsed x={} y={}", table, row, field, p->first, p->second);
    } else {
        BEAST_LOG_WARN("map_debug: {} row={} {} parse_point FAILED", table, row, field);
    }
}

} // namespace

void log_map_tables_debug(
    const beast::platform::bizutil::config::BizConfigStore& store,
    const std::uint32_t arena_id) {
    BEAST_LOG_INFO("map_debug: dump map tables arena_id={}", arena_id);

    if (const auto* cfg = store.find<biz::map_arena::MapArenaServerConfig>(kMapArenaTableLogicalName)) {
        for (const auto& row : cfg->rows()) {
            if (row.id() != arena_id) continue;
            BEAST_LOG_INFO(
                "map_debug: map_arena id={} width={} height={}",
                row.id(), row.width(), row.height());
        }
    } else {
        BEAST_LOG_WARN("map_debug: map_arena config not loaded");
    }

    if (const auto* cfg = store.find<biz::map_wall::MapWallServerConfig>(kMapWallTableLogicalName)) {
        for (const auto& row : cfg->rows()) {
            if (row.arena_id() != arena_id) continue;
            log_field_rect("map_wall", row.wall_index().c_str(), "rect", row.rect());
        }
    }

    if (const auto* cfg = store.find<biz::map_lane::MapLaneServerConfig>(kMapLaneTableLogicalName)) {
        for (const auto& row : cfg->rows()) {
            if (row.arena_id() != arena_id) continue;
            log_field_rect("map_lane", row.lane_id().c_str(), "rect", row.rect());
            BEAST_LOG_INFO("map_debug: map_lane row={} path raw=\"{}\"", row.lane_id(), row.path());
            const auto tiles = parse_path(row.path());
            BEAST_LOG_INFO("map_debug: map_lane row={} path parsed {} points", row.lane_id(), tiles.size());
        }
    }

    if (const auto* cfg = store.find<biz::map_monster_spawn::MapMonsterSpawnServerConfig>(
            kMapMonsterSpawnTableLogicalName)) {
        for (const auto& row : cfg->rows()) {
            if (row.arena_id() != arena_id) continue;
            log_field_rect("map_monster_spawn", row.spawn_id().c_str(), "rect", row.rect());
        }
    }

    if (const auto* cfg = store.find<biz::map_base::MapBaseServerConfig>(kMapBaseTableLogicalName)) {
        for (const auto& row : cfg->rows()) {
            if (row.arena_id() != arena_id) continue;
            const std::string team = row.team();
            log_field_rect("map_base", team.c_str(), "rect", row.rect());
            log_field_point("map_base", team.c_str(), "spawn", row.spawn());
            log_field_point("map_base", team.c_str(), "core", row.core());
        }
    }

    if (const auto* cfg = store.find<biz::map_tower::MapTowerServerConfig>(kMapTowerTableLogicalName)) {
        for (const auto& row : cfg->rows()) {
            if (row.arena_id() != arena_id) continue;
            log_field_rect("map_tower", row.tower_id().c_str(), "rect", row.rect());
        }
    }
}

std::shared_ptr<MapData> load_map(
    const beast::platform::bizutil::config::BizConfigStore& store,
    std::uint32_t arena_id) {

    auto map = std::make_shared<MapData>();
    map->arena_id = arena_id;

    // map_arena:宽高
    const auto* arena_cfg = store.find<biz::map_arena::MapArenaServerConfig>(kMapArenaTableLogicalName);
    if (arena_cfg) {
        for (const auto& row : arena_cfg->rows()) {
            if (row.id() == arena_id) {
                map->width = static_cast<std::uint32_t>(row.width());
                map->height = static_cast<std::uint32_t>(row.height());
                break;
            }
        }
    }
    if (map->width == 0 || map->height == 0) {
        map->width = 150;
        map->height = 150;
        BEAST_LOG_WARN("map_loader: arena {} not found, default 150x150", arena_id);
    }

    map->nav_mesh = std::make_shared<NavMesh>(map->width, map->height);

    // map_wall:标记阻挡瓦片
    const auto* wall_cfg = store.find<biz::map_wall::MapWallServerConfig>(kMapWallTableLogicalName);
    if (wall_cfg) {
        for (const auto& row : wall_cfg->rows()) {
            if (row.arena_id() != arena_id) continue;
            const auto r = parse_rect(row.rect());
            if (!r) continue;
            map->nav_mesh->block_rect(
                static_cast<std::uint32_t>(r->x),
                static_cast<std::uint32_t>(r->y),
                static_cast<std::uint32_t>(r->w),
                static_cast<std::uint32_t>(r->h));
        }
    }

    // map_lane:路径点
    const auto* lane_cfg = store.find<biz::map_lane::MapLaneServerConfig>(kMapLaneTableLogicalName);
    if (lane_cfg) {
        for (const auto& row : lane_cfg->rows()) {
            if (row.arena_id() != arena_id) continue;
            LaneData lane;
            lane.id = row.lane_id();
            const auto tiles = parse_path(row.path());
            lane.path.reserve(tiles.size());
            for (const auto& [tx, ty] : tiles) {
                lane.path.push_back(tile_to_pixel(tx, ty));
            }
            map->lanes.push_back(std::move(lane));
        }
    }

    // map_monster_spawn:营地
    const auto* monster_cfg = store.find<biz::map_monster_spawn::MapMonsterSpawnServerConfig>(kMapMonsterSpawnTableLogicalName);
    if (monster_cfg) {
        for (const auto& row : monster_cfg->rows()) {
            if (row.arena_id() != arena_id) continue;
            CampData camp;
            camp.id = row.spawn_id();
            camp.type = row.type();
            camp.unit_id = row.unit_id();
            const auto r = parse_rect(row.rect());
            if (r) {
                camp.pos = tile_to_pixel(r->x + r->w / 2, r->y + r->h / 2);
            }
            map->camps.push_back(std::move(camp));
        }
    }

    // map_base:出生点 + 基地核心位置
    const auto* base_cfg = store.find<biz::map_base::MapBaseServerConfig>(kMapBaseTableLogicalName);
    if (base_cfg) {
        for (const auto& row : base_cfg->rows()) {
            if (row.arena_id() != arena_id) continue;
            BaseData base;
            base.team = row.team();
            const auto sp = parse_point(row.spawn());
            if (sp) base.spawn_pos = tile_to_pixel(sp->first, sp->second);
            const auto cp = parse_point(row.core());
            if (cp) base.core_pos = tile_to_pixel(cp->first, cp->second);
            else    base.core_pos = base.spawn_pos;   // core 缺失时回退 spawn
            map->bases.push_back(std::move(base));
        }
    }

    // map_tower:防御塔
    const auto* tower_cfg = store.find<biz::map_tower::MapTowerServerConfig>(kMapTowerTableLogicalName);
    if (tower_cfg) {
        for (const auto& row : tower_cfg->rows()) {
            if (row.arena_id() != arena_id) continue;
            TowerConfigData tower;
            tower.id = row.tower_id();
            tower.team = row.team();
            const auto r = parse_rect(row.rect());
            if (r) {
                tower.pos = tile_to_pixel(r->x + r->w / 2, r->y + r->h / 2);
            }
            map->towers.push_back(std::move(tower));
        }
    }

    // map_bush:草丛(rect 为 tile 坐标,转像素 min/max 供 point-in-rect 判定)
    const auto* bush_cfg = store.find<biz::map_bush::MapBushServerConfig>(kMapBushTableLogicalName);
    if (bush_cfg) {
        for (const auto& row : bush_cfg->rows()) {
            if (row.arena_id() != arena_id) continue;
            BushData bush;
            bush.id = row.bush_index();
            const auto r = parse_rect(row.rect());
            if (r) {
                bush.min = tile_to_pixel(r->x, r->y);
                bush.max = tile_to_pixel(r->x + r->w, r->y + r->h);
            }
            map->bushes.push_back(std::move(bush));
        }
    }

    BEAST_LOG_INFO(
        "map_loader: arena={} {}x{} walls_blocked lanes={} camps={} bases={} towers={} bushes={}",
        arena_id, map->width, map->height,
        map->lanes.size(), map->camps.size(), map->bases.size(), map->towers.size(), map->bushes.size());

    return map;
}

} // namespace beast::moba::pixel
