#pragma once

#include "beast/platform/bizutil/math/algorithm/bfs.hpp"
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

template<typename PassableFn>
[[nodiscard]] inline std::optional<std::vector<Vec2i>> astar_path(
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
    const auto heuristic = [&goal](const Vec2i point) { return manhattan(point, goal); };

    struct OpenNode {
        int f{0};
        int key{0};
    };
    struct CompareOpenNode {
        bool operator()(const OpenNode& lhs, const OpenNode& rhs) const { return lhs.f > rhs.f; }
    };

    std::priority_queue<OpenNode, std::vector<OpenNode>, CompareOpenNode> open;
    open.push(OpenNode{heuristic(start), start_key});

    std::unordered_map<int, int> came_from;
    std::unordered_map<int, int> g_score;
    std::unordered_set<int> closed;
    g_score.emplace(start_key, 0);

    while (!open.empty()) {
        const int current_key = open.top().key;
        open.pop();
        if (closed.contains(current_key)) {
            continue;
        }
        closed.insert(current_key);

        const Vec2i current = from_index(current_key, width);
        if (current == goal) {
            return detail::reconstruct_path(came_from, width, goal);
        }

        const int current_g = g_score.at(current_key);
        for (const Vec2i next : neighbors4(current)) {
            if (!in_bounds(next, width, height) || !passable(next)) {
                continue;
            }

            const int next_key = to_index(next, width);
            const int tentative_g = current_g + 1;
            const auto it = g_score.find(next_key);
            if (it != g_score.end() && tentative_g >= it->second) {
                continue;
            }

            came_from[next_key] = current_key;
            g_score[next_key] = tentative_g;
            open.push(OpenNode{tentative_g + heuristic(next), next_key});
        }
    }
    return std::nullopt;
}

} // namespace beast::platform::bizutil::math
