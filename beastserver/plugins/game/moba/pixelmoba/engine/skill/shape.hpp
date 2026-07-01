#pragma once

#include "beast/platform/bizutil/math/geometry/circle.hpp"
#include "beast/platform/bizutil/math/geometry/cone.hpp"
#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"
#include "skill_level.pb.h"

#include <cstdint>
#include <string_view>

namespace beast::moba::pixel {

using beast::platform::bizutil::math::Vec2f;
using beast::platform::bizutil::math::Circlef;
using beast::platform::bizutil::math::Conef;
using beast::platform::bizutil::math::contains;
using beast::platform::bizutil::math::dot;
using beast::platform::bizutil::math::normalize;
using beast::platform::bizutil::math::perpendicular;

enum class SkillShapeType : std::uint8_t {
    Circle,
    Sector,
    Rect,
};

struct SkillShape {
    SkillShapeType type{SkillShapeType::Circle};
    float radius{0.f};          // circle / sector
    float half_angle_rad{0.f};  // sector(判定用)
    float angle_deg{0.f};       // sector 原始度数(仅供 proto 回填,判定用 half_angle_rad)
    float length{0.f};          // rect(沿 facing)
    float width{0.f};           // rect(垂直 facing)
    Vec2f  facing{1.f, 0.f};    // sector / rect 朝向(需归一化)
};

inline SkillShapeType parse_shape_type(std::string_view s) {
    if (s == "sector" || s == "cone") return SkillShapeType::Sector;
    if (s == "rect" || s == "rectangle") return SkillShapeType::Rect;
    return SkillShapeType::Circle;  // 默认圆形(含空串)
}

// origin 语义:circle 圆心 / sector 与 rect 的起点(施法者位置或 target_pos)
inline bool is_in_shape(const SkillShape& s, Vec2f origin, Vec2f point) {
    switch (s.type) {
    case SkillShapeType::Circle:
        return contains(Circlef{origin, s.radius}, point);
    case SkillShapeType::Sector:
        return contains(Conef{origin, s.facing, s.radius, s.half_angle_rad}, point);
    case SkillShapeType::Rect: {
        const Vec2f dir = normalize(s.facing);
        const Vec2f d = point - origin;
        const float along = dot(d, dir);
        if (along < 0.f || along > s.length) return false;
        const float across = dot(d, perpendicular(dir));
        return across >= -s.width * 0.5f && across <= s.width * 0.5f;
    }
    }
    return false;
}

inline SkillShape shape_from_level_row(
    const beast::biz::moba::pixel_moba::skill_level::SkillLevelRowServer& row,
    Vec2f facing) {
    SkillShape s;
    // skill_level 表当前仅有 radius;扇形/矩形字段(shape_type/angle_deg/length/width)待配表扩展。
    s.type = SkillShapeType::Circle;
    s.radius = row.radius();
    s.facing = facing;
    return s;
}

} // namespace beast::moba::pixel
