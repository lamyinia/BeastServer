#include "beast/platform/bizutil/math/algorithm/astar.hpp"
#include "beast/platform/bizutil/math/algorithm/bfs.hpp"

#include <gtest/gtest.h>

#include <unordered_set>

namespace beast::platform::bizutil::math {
namespace {

std::unordered_set<int> blocked_cells() {
    return {
        to_index({1, 0}, 5),
        to_index({1, 1}, 5),
        to_index({1, 2}, 5),
        to_index({1, 3}, 5),
    };
}

bool passable(const Vec2i point) {
    static const auto blocked = blocked_cells();
    return !blocked.contains(to_index(point, 5));
}

TEST(MathAlgorithmPathfindTest, BfsFindsPathAroundObstacle) {
    const auto path = bfs_path(Vec2i{0, 0}, Vec2i{4, 0}, 5, 5, passable);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->front(), Vec2i(0, 0));
    EXPECT_EQ(path->back(), Vec2i(4, 0));
    EXPECT_GT(path->size(), 5U);
}

TEST(MathAlgorithmPathfindTest, AstarFindsPathAroundObstacle) {
    const auto path = astar_path(Vec2i{0, 0}, Vec2i{4, 0}, 5, 5, passable);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->front(), Vec2i(0, 0));
    EXPECT_EQ(path->back(), Vec2i(4, 0));
}

} // namespace
} // namespace beast::platform::bizutil::math
