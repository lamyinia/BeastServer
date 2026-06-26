#pragma once

#include "beast/platform/bizutil/math/spatial/hash_grid.hpp"
#include "beast/platform/bizutil/math/vector/vec2.hpp"
#include "beast/platform/bizutil/math/vector/vec2_ops.hpp"

#include <span>
#include <vector>

namespace beast::platform::bizutil::math {

template<typename Id>
[[nodiscard]] inline std::vector<Id> query_radius(
    const HashGrid<Id>& grid,
    const Vec2f center,
    const float radius) {
    return grid.query_radius(center, radius);
}

template<typename Id, typename PositionFn>
[[nodiscard]] inline std::vector<Id> query_radius_brute(
    const std::span<const Id> ids,
    PositionFn get_position,
    const Vec2f center,
    const float radius) {
    std::vector<Id> result;
    if (radius <= 0.f) {
        return result;
    }

    const float radius_sq = radius * radius;
    for (const Id id : ids) {
        if (distance_squared(center, get_position(id)) <= radius_sq) {
            result.push_back(id);
        }
    }
    return result;
}

} // namespace beast::platform::bizutil::math
