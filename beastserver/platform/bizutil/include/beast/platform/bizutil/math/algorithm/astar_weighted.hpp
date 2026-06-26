#pragma once

#include "beast/platform/bizutil/math/algorithm/bfs.hpp"
#include "beast/platform/bizutil/math/grid/bounds.hpp"
#include "beast/platform/bizutil/math/grid/index.hpp"
#include "beast/platform/bizutil/math/grid/neighbors.hpp"
#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace beast::platform::bizutil::math {

// 带地形代价的 A*：cost(cell) 返回进入该格的代价（> 0 可走，<= 0 视为阻挡）。
// allow_diagonal 为 true 时支持 8 向移动，对角代价乘 sqrt(2)，并禁止穿墙角。
template<typename CostFn>
[[nodiscard]] inline std::optional<std::vector<Vec2i>> astar_weighted_path(
    const Vec2i start,
    const Vec2i goal,
    const int width,
    const int height,
    CostFn cost,
    const bool allow_diagonal = true) {
    if (!in_bounds(start, width, height) || !in_bounds(goal, width, height)) {
        return std::nullopt;
    }
    if (cost(start) <= 0.f || cost(goal) <= 0.f) {
        return std::nullopt;
    }
    if (start == goal) {
        return std::vector<Vec2i>{start};
    }

    constexpr float kSqrt2 = 1.41421356f;
    const int start_key = to_index(start, width);
    const auto heuristic = [&goal, allow_diagonal](const Vec2i p) {
        const int dx = std::abs(goal.x - p.x);
        const int dy = std::abs(goal.y - p.y);
        if (allow_diagonal) {
            const int dmin = std::min(dx, dy);
            const int dmax = std::max(dx, dy);
            return kSqrt2 * static_cast<float>(dmin) + static_cast<float>(dmax - dmin);
        }
        return static_cast<float>(dx + dy);
    };

    struct OpenNode {
        float f{0.f};
        int key{0};
    };
    struct CompareOpenNode {
        bool operator()(const OpenNode& lhs, const OpenNode& rhs) const { return lhs.f > rhs.f; }
    };

    std::priority_queue<OpenNode, std::vector<OpenNode>, CompareOpenNode> open;
    open.push(OpenNode{heuristic(start), start_key});

    std::unordered_map<int, int> came_from;
    std::unordered_map<int, float> g_score;
    std::unordered_set<int> closed;
    g_score.emplace(start_key, 0.f);

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

        const float current_g = g_score.at(current_key);
        const auto relax = [&](const Vec2i next, const float step_mul) {
            if (!in_bounds(next, width, height)) {
                return;
            }
            const float c = cost(next);
            if (c <= 0.f) {
                return;
            }
            const int next_key = to_index(next, width);
            if (closed.contains(next_key)) {
                return;
            }
            const float tentative = current_g + c * step_mul;
            const auto it = g_score.find(next_key);
            if (it != g_score.end() && tentative >= it->second) {
                return;
            }
            came_from[next_key] = current_key;
            g_score[next_key] = tentative;
            open.push(OpenNode{tentative + heuristic(next), next_key});
        };

        for (const Vec2i next : neighbors4(current)) {
            relax(next, 1.f);
        }

        if (allow_diagonal) {
            const Vec2i diagonals[4] = {
                {current.x + 1, current.y + 1},
                {current.x + 1, current.y - 1},
                {current.x - 1, current.y + 1},
                {current.x - 1, current.y - 1},
            };
            for (const Vec2i diag : diagonals) {
                const Vec2i ortho_h{diag.x, current.y};
                const Vec2i ortho_v{current.x, diag.y};
                if (!in_bounds(ortho_h, width, height) || cost(ortho_h) <= 0.f) {
                    continue;
                }
                if (!in_bounds(ortho_v, width, height) || cost(ortho_v) <= 0.f) {
                    continue;
                }
                relax(diag, kSqrt2);
            }
        }
    }
    return std::nullopt;
}

} // namespace beast::platform::bizutil::math
