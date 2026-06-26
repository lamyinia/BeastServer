#include "beast/platform/bizutil/math/algorithm/astar_weighted.hpp"
#include "beast/platform/bizutil/math/grid/index.hpp"

#include <gtest/gtest.h>

#include <unordered_set>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathAlgorithmAstarWeightedTest, DiagonalShortcut) {
    const auto uniform_cost = [](const Vec2i) { return 1.f; };
    const auto path = astar_weighted_path(Vec2i{0, 0}, Vec2i{3, 3}, 5, 5, uniform_cost, true);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->front(), Vec2i(0, 0));
    EXPECT_EQ(path->back(), Vec2i(3, 3));
    EXPECT_EQ(path->size(), 4U);
}

TEST(MathAlgorithmAstarWeightedTest, BlockedCellsAvoided) {
    const std::unordered_set<int> blocked{
        to_index({1, 0}, 5),
        to_index({1, 1}, 5),
        to_index({1, 2}, 5),
    };
    const auto cost = [&blocked](const Vec2i cell) {
        return blocked.contains(to_index(cell, 5)) ? 0.f : 1.f;
    };

    const auto path = astar_weighted_path(Vec2i{0, 0}, Vec2i{2, 0}, 5, 5, cost, false);
    ASSERT_TRUE(path.has_value());
    for (const Vec2i cell : *path) {
        EXPECT_FALSE(blocked.contains(to_index(cell, 5)));
    }
}

TEST(MathAlgorithmAstarWeightedTest, NoPathReturnsNullopt) {
    const auto wall = [](const Vec2i cell) { return cell.x == 1 ? 0.f : 1.f; };
    const auto path = astar_weighted_path(Vec2i{0, 0}, Vec2i{2, 0}, 3, 1, wall, true);
    EXPECT_FALSE(path.has_value());
}

} // namespace
} // namespace beast::platform::bizutil::math
