#pragma once

#include "beast/platform/bizutil/math/grid/bounds.hpp"
#include "beast/platform/bizutil/math/grid/distance.hpp"
#include "beast/platform/bizutil/math/grid/index.hpp"
#include "beast/platform/bizutil/math/grid/neighbors.hpp"
#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <algorithm>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace beast::platform::bizutil::math {

namespace detail {

[[nodiscard]] inline std::vector<Vec2i> reconstruct_path(
    const std::unordered_map<int, int>& came_from,
    const int width,
    Vec2i current) {
    std::vector<Vec2i> path;
    while (true) {
        path.push_back(current);
        const int key = to_index(current, width);
        const auto it = came_from.find(key);
        if (it == came_from.end()) {
            break;
        }
        current = from_index(it->second, width);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

} // namespace detail

template<typename PassableFn>
[[nodiscard]] inline std::optional<std::vector<Vec2i>> bfs_path(
    const Vec2i start,
    const Vec2i goal,
    const int width,
    const int height,
    PassableFn passable) {
    if (!in_bounds(start, width, height) || !in_bounds(goal, width, height)) {
        return std::nullopt;
    }
    if (!passable(start) || !passable(goal)) {
        return std::nullopt;
    }
    if (start == goal) {
        return std::vector<Vec2i>{start};
    }

    const int start_key = to_index(start, width);
    std::queue<Vec2i> frontier;
    frontier.push(start);
    std::unordered_map<int, int> came_from;
    std::unordered_set<int> visited;
    visited.insert(start_key);

    while (!frontier.empty()) {
        const Vec2i current = frontier.front();
        frontier.pop();
        if (current == goal) {
            return detail::reconstruct_path(came_from, width, goal);
        }

        for (const Vec2i next : neighbors4(current)) {
            if (!in_bounds(next, width, height) || !passable(next)) {
                continue;
            }
            const int next_key = to_index(next, width);
            if (visited.contains(next_key)) {
                continue;
            }
            visited.insert(next_key);
            came_from.emplace(next_key, to_index(current, width));
            frontier.push(next);
        }
    }
    return std::nullopt;
}

} // namespace beast::platform::bizutil::math
