#include "engine/pathfinding.hpp"

#include "beast/platform/bizutil/math/vector/vec2.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <queue>
#include <vector>

namespace beast::moba::pixel {

namespace {

using beast::platform::bizutil::math::Vec2f;
using beast::platform::bizutil::math::Vec2i;

// 8 方向偏移;前 4 个是直角(代价 1),后 4 个是对角(代价 √2,以 ×1000 整数存储避免浮点)。
// 用整数代价避免 A* 比较浮点;启发式用 octile(整数版)。
constexpr int kCostStraight = 1000;
constexpr int kCostDiagonal = 1414;   // round(√2 * 1000)

struct NeighborDef {
    int dx;
    int dy;
    int cost;
};

constexpr std::array<NeighborDef, 8> kNeighbors8{{
    {0, -1, kCostStraight},
    {1, 0, kCostStraight},
    {0, 1, kCostStraight},
    {-1, 0, kCostStraight},
    {1, -1, kCostDiagonal},
    {1, 1, kCostDiagonal},
    {-1, 1, kCostDiagonal},
    {-1, -1, kCostDiagonal},
}};

// Octile 启发式(整数版),与 8 方向移动代价一致,admissible & consistent。
[[nodiscard]] inline int octile_heuristic(const Vec2i from, const Vec2i to) noexcept {
    const int dx = std::abs(to.x - from.x);
    const int dy = std::abs(to.y - from.y);
    if (dx > dy) {
        return kCostDiagonal * dy + kCostStraight * (dx - dy);
    }
    return kCostDiagonal * dx + kCostStraight * (dy - dx);
}

struct OpenNode {
    int f{0};
    int key{0};
};

struct CompareOpenNode {
    bool operator()(const OpenNode& lhs, const OpenNode& rhs) const noexcept {
        return lhs.f > rhs.f;
    }
};

// === A* 实现(当前 find_path 使用) ===
// 8 方向 + octile + flat array 工作内存(ctx 复用)+ 对角穿角禁止。
template<typename PassableFn, typename CtxRef>
[[nodiscard]] std::vector<Vec2i> nav_astar(
    const Vec2i start,
    const Vec2i goal,
    const int width,
    const int height,
    CtxRef& ctx,
    PassableFn passable) {
    if (start == goal) return {start};

    const int total = width * height;
    ctx.g_score.assign(total, -1);
    ctx.came_from.assign(total, -1);
    ctx.closed.assign(total, 0);

    std::priority_queue<OpenNode, std::vector<OpenNode>, CompareOpenNode> open;
    const int start_key = start.y * width + start.x;
    ctx.g_score[start_key] = 0;
    open.push(OpenNode{octile_heuristic(start, goal), start_key});

    while (!open.empty()) {
        const int current_key = open.top().key;
        open.pop();
        if (ctx.closed[current_key] != 0) continue;
        ctx.closed[current_key] = 1;

        const Vec2i current{current_key % width, current_key / width};
        if (current == goal) {
            std::vector<Vec2i> path;
            int key = current_key;
            while (key != -1) {
                path.push_back({key % width, key / width});
                key = ctx.came_from[key];
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        const int current_g = ctx.g_score[current_key];
        for (const auto& n : kNeighbors8) {
            const Vec2i next{current.x + n.dx, current.y + n.dy};
            if (next.x < 0 || next.y < 0 || next.x >= width || next.y >= height) continue;
            if (!passable(next)) continue;
            // 对角穿角禁止:对角步进时两个 corner cell 都必须 passable。
            if (n.dx != 0 && n.dy != 0) {
                const Vec2i cx{current.x + n.dx, current.y};
                const Vec2i cy{current.x, current.y + n.dy};
                if (!passable(cx) || !passable(cy)) continue;
            }
            const int next_key = next.y * width + next.x;
            if (ctx.closed[next_key] != 0) continue;
            const int tentative_g = current_g + n.cost;
            const int existing = ctx.g_score[next_key];
            if (existing != -1 && tentative_g >= existing) continue;
            ctx.g_score[next_key] = tentative_g;
            ctx.came_from[next_key] = current_key;
            open.push(OpenNode{tentative_g + octile_heuristic(next, goal), next_key});
        }
    }
    return {};
}

// 严格 LOS(不穿角):Bresenham 上对角步进时,要求两个 corner cell 也 passable。
// 供 simplify_tile_path 和 is_line_clear 使用。
template<typename PassableFn>
[[nodiscard]] bool strict_line_of_sight(Vec2i from, Vec2i to, PassableFn passable) {
    int x = from.x;
    int y = from.y;
    const int dx = std::abs(to.x - from.x);
    const int dy = std::abs(to.y - from.y);
    const int sx = from.x < to.x ? 1 : -1;
    const int sy = from.y < to.y ? 1 : -1;
    int err = dx - dy;
    while (true) {
        if (!passable({x, y})) return false;
        if (x == to.x && y == to.y) return true;
        const int err2 = err * 2;
        const bool step_x = err2 > -dy;
        const bool step_y = err2 < dx;
        if (step_x && step_y) {
            // 对角步进:两个 corner cell 都必须 passable,否则视为穿角。
            if (!passable({x + sx, y}) || !passable({x, y + sy})) return false;
        }
        if (step_x) {
            err -= dy;
            x += sx;
        }
        if (step_y) {
            err += dx;
            y += sy;
        }
    }
}

// String pulling:保留拐角,删除 A* 锯齿中间点。用严格 LOS 避免穿角。
template<typename PassableFn>
std::vector<Vec2i> simplify_tile_path(const std::vector<Vec2i>& path, PassableFn passable) {
    if (path.size() <= 2) return path;

    std::vector<Vec2i> out;
    out.reserve(path.size());
    out.push_back(path.front());

    std::size_t i = 0;
    while (i + 1 < path.size()) {
        std::size_t best = i + 1;
        for (std::size_t j = path.size(); j > i + 1; --j) {
            if (strict_line_of_sight(path[i], path[j - 1], passable)) {
                best = j - 1;
                break;
            }
        }
        if (best != i) out.push_back(path[best]);
        i = best;
    }
    return out;
}

// === JPS 实现(当前未使用,保留方便后续切换) ===
// JPS(Jump Point Search):8 方向 + octile + flat array 工作内存(ctx 复用)。
// 在均匀网格上通过跳跃规则只探索方向改变的跳点,速度远快于朴素 A*。
// 输出本身就是稀疏拐点序列,无需 string pulling。
// passable 必须保证对越界 tile 返回 false。

// 判断 n 沿方向 d 到达时是否有 forced neighbor(因障碍存在必须探索的邻居)。
template<typename PassableFn>
[[nodiscard]] bool has_forced_neighbor(Vec2i n, Vec2i d, PassableFn passable) {
    const int dx = d.x, dy = d.y;
    if (dx != 0 && dy != 0) {
        // 对角移动:检查两个切线方向的障碍
        if (!passable({n.x - dx, n.y}) && passable({n.x - dx, n.y + dy})) return true;
        if (!passable({n.x, n.y - dy}) && passable({n.x + dx, n.y - dy})) return true;
    } else if (dx != 0) {
        // 水平移动:检查"身后"两侧
        if (!passable({n.x - dx, n.y - 1}) && passable({n.x, n.y - 1})) return true;
        if (!passable({n.x - dx, n.y + 1}) && passable({n.x, n.y + 1})) return true;
    } else {
        // 垂直移动
        if (!passable({n.x - 1, n.y - dy}) && passable({n.x - 1, n.y})) return true;
        if (!passable({n.x + 1, n.y - dy}) && passable({n.x + 1, n.y})) return true;
    }
    return false;
}

// 沿方向 d 从 n 跳跃,返回跳点或 {-1,-1}(无跳点)。
// 递归深度:对角 jump 调用水平/垂直 jump(直线,不再递归),最多 2-3 层,无栈溢出风险。
template<typename PassableFn>
[[nodiscard]] Vec2i jump(Vec2i n, Vec2i d, Vec2i goal, int w, int h, PassableFn passable) {
    Vec2i current{n.x + d.x, n.y + d.y};
    while (true) {
        if (current.x < 0 || current.y < 0 || current.x >= w || current.y >= h) return {-1, -1};
        if (!passable(current)) return {-1, -1};
        if (current == goal) return current;
        if (has_forced_neighbor(current, d, passable)) return current;
        if (d.x != 0 && d.y != 0) {
            // 递归做水平和垂直 jump,任一返回跳点则 current 是跳点。
            // 注意:不在此处检查 corner cell——has_forced_neighbor 已在墙角处产生跳点,
            // 让 JPS 能探索绕墙路径。实体移动时的撞墙由 resolve_wall_and_bounds 滑墙处理。
            if (jump(current, {d.x, 0}, goal, w, h, passable).x != -1) return current;
            if (jump(current, {0, d.y}, goal, w, h, passable).x != -1) return current;
        }
        current = {current.x + d.x, current.y + d.y};
    }
}

// 返回从 n 出发需要探索的方向(自然方向 + forced 方向)。
// d 为 {0,0}(start 节点)时返回全部 8 方向。
template<typename PassableFn>
[[nodiscard]] std::vector<Vec2i> prune_dirs(Vec2i n, Vec2i d, PassableFn passable) {
    std::vector<Vec2i> dirs;
    if (d.x == 0 && d.y == 0) {
        for (const auto& nd : kNeighbors8) {
            dirs.push_back({nd.dx, nd.dy});
        }
        return dirs;
    }
    const int dx = d.x, dy = d.y;
    if (dx != 0 && dy != 0) {
        // 对角:自然方向 (dx,0),(0,dy),(dx,dy)
        dirs.push_back({dx, 0});
        dirs.push_back({0, dy});
        dirs.push_back({dx, dy});
        // forced 方向
        if (!passable({n.x - dx, n.y}) && passable({n.x - dx, n.y + dy})) {
            dirs.push_back({-dx, dy});
        }
        if (!passable({n.x, n.y - dy}) && passable({n.x + dx, n.y - dy})) {
            dirs.push_back({dx, -dy});
        }
    } else if (dx != 0) {
        // 水平:自然方向 (dx,0)
        dirs.push_back({dx, 0});
        if (!passable({n.x - dx, n.y - 1}) && passable({n.x, n.y - 1})) {
            dirs.push_back({dx, -1});
        }
        if (!passable({n.x - dx, n.y + 1}) && passable({n.x, n.y + 1})) {
            dirs.push_back({dx, 1});
        }
    } else {
        // 垂直:自然方向 (0,dy)
        dirs.push_back({0, dy});
        if (!passable({n.x - 1, n.y - dy}) && passable({n.x - 1, n.y})) {
            dirs.push_back({-1, dy});
        }
        if (!passable({n.x + 1, n.y - dy}) && passable({n.x + 1, n.y})) {
            dirs.push_back({1, dy});
        }
    }
    return dirs;
}

// JPS 主搜索:A* 主循环 + jump successor。CtxRef 由 NavMesh::find_path 传入 AStarContext。
template<typename PassableFn, typename CtxRef>
[[nodiscard]] std::vector<Vec2i> jps_search(
    const Vec2i start,
    const Vec2i goal,
    const int width,
    const int height,
    CtxRef& ctx,
    PassableFn passable) {
    if (start == goal) return {start};

    const int total = width * height;
    ctx.g_score.assign(total, -1);
    ctx.came_from.assign(total, -1);
    ctx.closed.assign(total, 0);

    std::priority_queue<OpenNode, std::vector<OpenNode>, CompareOpenNode> open;
    const int start_key = start.y * width + start.x;
    ctx.g_score[start_key] = 0;
    open.push(OpenNode{octile_heuristic(start, goal), start_key});

    while (!open.empty()) {
        const int current_key = open.top().key;
        open.pop();
        if (ctx.closed[current_key] != 0) continue;
        ctx.closed[current_key] = 1;

        const Vec2i current{current_key % width, current_key / width};
        if (current == goal) {
            std::vector<Vec2i> path;
            int key = current_key;
            while (key != -1) {
                path.push_back({key % width, key / width});
                key = ctx.came_from[key];
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        // 计算来向(start 节点无来向,prune_dirs 返回全 8 方向)
        Vec2i dir{0, 0};
        const int parent_key = ctx.came_from[current_key];
        if (parent_key != -1) {
            const Vec2i parent{parent_key % width, parent_key / width};
            dir = {current.x - parent.x, current.y - parent.y};
            // 归一化(跳点间可能是多步同向)
            if (dir.x != 0) dir.x = dir.x > 0 ? 1 : -1;
            if (dir.y != 0) dir.y = dir.y > 0 ? 1 : -1;
        }

        const int current_g = ctx.g_score[current_key];
        const auto dirs = prune_dirs(current, dir, passable);
        for (const auto& d : dirs) {
            const Vec2i jumped = jump(current, d, goal, width, height, passable);
            if (jumped.x == -1) continue;
            const int jumped_key = jumped.y * width + jumped.x;
            if (ctx.closed[jumped_key] != 0) continue;
            // 代价:jump 是直线跳跃,步数 × 单位代价
            const int steps = std::max(std::abs(jumped.x - current.x), std::abs(jumped.y - current.y));
            const int unit_cost = (d.x != 0 && d.y != 0) ? kCostDiagonal : kCostStraight;
            const int tentative_g = current_g + steps * unit_cost;
            const int existing = ctx.g_score[jumped_key];
            if (existing != -1 && tentative_g >= existing) continue;
            ctx.g_score[jumped_key] = tentative_g;
            ctx.came_from[jumped_key] = current_key;
            open.push(OpenNode{tentative_g + octile_heuristic(jumped, goal), jumped_key});
        }
    }
    return {};
}

} // namespace

NavMesh::NavMesh(std::uint32_t width, std::uint32_t height)
    : width_(width)
    , height_(height)
    , blocked_(static_cast<std::size_t>(width) * height, false) {}

void NavMesh::block_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
    for (std::uint32_t dy = 0; dy < h && y + dy < height_; ++dy) {
        for (std::uint32_t dx = 0; dx < w && x + dx < width_; ++dx) {
            blocked_[static_cast<std::size_t>(y + dy) * width_ + (x + dx)] = true;
        }
    }
}

bool NavMesh::is_blocked(std::uint32_t x, std::uint32_t y) const {
    if (x >= width_ || y >= height_) return true;
    return blocked_[static_cast<std::size_t>(y) * width_ + x];
}

bool NavMesh::is_blocked_with_dynamic(std::uint32_t x, std::uint32_t y) const {
    if (x >= width_ || y >= height_) return true;
    const auto key = static_cast<std::uint64_t>(y) * width_ + x;
    return blocked_[static_cast<std::size_t>(key)] || dynamic_blocked_.count(key) != 0;
}

void NavMesh::add_dynamic_block(std::uint32_t x, std::uint32_t y) {
    if (x >= width_ || y >= height_) return;
    dynamic_blocked_.insert(static_cast<std::uint64_t>(y) * width_ + x);
}

void NavMesh::remove_dynamic_block(std::uint32_t x, std::uint32_t y) {
    if (x >= width_ || y >= height_) return;
    dynamic_blocked_.erase(static_cast<std::uint64_t>(y) * width_ + x);
}

Vec2f NavMesh::tile_center_to_pixel(std::uint32_t tx, std::uint32_t ty) {
    return {(tx + 0.5f) * kTilePx, (ty + 0.5f) * kTilePx};
}

Vec2i NavMesh::pixel_to_tile(Vec2f px) {
    return {static_cast<int>(std::floor(px.x / kTilePx)),
            static_cast<int>(std::floor(px.y / kTilePx))};
}

bool NavMesh::is_passable_tile(Vec2i tile) const {
    if (tile.x < 0 || tile.y < 0
        || static_cast<std::uint32_t>(tile.x) >= width_
        || static_cast<std::uint32_t>(tile.y) >= height_) {
        return false;
    }
    return !blocked_[static_cast<std::size_t>(tile.y) * width_ + tile.x];
}

bool NavMesh::is_passable_tile_with_dynamic(Vec2i tile) const {
    if (tile.x < 0 || tile.y < 0
        || static_cast<std::uint32_t>(tile.x) >= width_
        || static_cast<std::uint32_t>(tile.y) >= height_) {
        return false;
    }
    const auto key = static_cast<std::uint64_t>(tile.y) * width_ + tile.x;
    if (blocked_[static_cast<std::size_t>(key)]) return false;
    return dynamic_blocked_.count(key) == 0;
}

bool NavMesh::is_line_clear(Vec2i from, Vec2i to) const {
    const auto passable = [this](const Vec2i p) { return is_passable_tile(p); };
    return strict_line_of_sight(from, to, passable);
}

bool NavMesh::is_path_blocked_by_dynamic(const std::vector<Vec2f>& path_px) const {
    if (dynamic_blocked_.empty() || path_px.empty()) return false;
    for (const auto& px : path_px) {
        const auto tile = pixel_to_tile(px);
        if (tile.x < 0 || tile.y < 0) continue;
        const auto ux = static_cast<std::uint32_t>(tile.x);
        const auto uy = static_cast<std::uint32_t>(tile.y);
        if (ux >= width_ || uy >= height_) continue;
        const auto key = static_cast<std::uint64_t>(uy) * width_ + ux;
        if (dynamic_blocked_.count(key) != 0) return true;
    }
    return false;
}

void NavMesh::ensure_ctx() const {
    const std::size_t total = static_cast<std::size_t>(width_) * height_;
    if (ctx_.g_score.size() != total) {
        ctx_.g_score.assign(total, -1);
        ctx_.came_from.assign(total, -1);
        ctx_.closed.assign(total, 0);
    }
}

std::vector<Vec2f> NavMesh::find_path(Vec2f start_px, Vec2f goal_px, bool include_dynamic) const {
    ensure_ctx();

    const Vec2i start = pixel_to_tile(start_px);
    const Vec2i goal = pixel_to_tile(goal_px);
    const int w = static_cast<int>(width_);
    const int h = static_cast<int>(height_);

    // 起点或终点越界/被占用 → 失败(由调用方 fallback)。
    if (start.x < 0 || start.y < 0 || start.x >= w || start.y >= h) return {};
    if (goal.x < 0 || goal.y < 0 || goal.x >= w || goal.y >= h) return {};

    std::vector<Vec2i> tile_path;
    if (include_dynamic) {
        const auto passable = [this](const Vec2i p) { return is_passable_tile_with_dynamic(p); };
        if (!is_passable_tile_with_dynamic(start) || !is_passable_tile_with_dynamic(goal)) return {};
        tile_path = nav_astar(start, goal, w, h, ctx_, passable);
    } else {
        const auto passable = [this](const Vec2i p) { return is_passable_tile(p); };
        if (!is_passable_tile(start) || !is_passable_tile(goal)) return {};
        tile_path = nav_astar(start, goal, w, h, ctx_, passable);
    }
    if (tile_path.empty()) return {};

    // A* 路径经 string pulling 简化为拐点序列。
    const auto passable_simplify = [this](const Vec2i p) { return is_passable_tile(p); };
    const auto simplified = simplify_tile_path(tile_path, passable_simplify);

    // 输出不含起点;末点用精确 goal 像素。
    std::vector<Vec2f> path;
    if (simplified.size() > 1) {
        path.reserve(simplified.size() - 1);
        for (std::size_t i = 1; i < simplified.size(); ++i) {
            const auto& t = simplified[i];
            path.push_back(tile_center_to_pixel(
                static_cast<std::uint32_t>(t.x), static_cast<std::uint32_t>(t.y)));
        }
    }

    if (path.empty()) {
        constexpr float kMinGoalDistSq = 8.f * 8.f;
        if ((goal_px - start_px).length_squared() < kMinGoalDistSq) return {};
        path.push_back(goal_px);
    } else {
        path.back() = goal_px;
    }
    return path;
}

} // namespace beast::moba::pixel