#include "beast/platform/bizutil/math/algorithm/line_of_sight.hpp"
#include "beast/platform/bizutil/math/grid/index.hpp"

#include <gtest/gtest.h>

#include <unordered_set>

namespace beast::platform::bizutil::math {
namespace {

TEST(MathAlgorithmLineOfSightTest, BlockedByObstacle) {
    const std::unordered_set<int> blocked{to_index({3, 0}, 10)};
    const auto passable = [&blocked](const Vec2i cell) {
        return !blocked.contains(to_index(cell, 10));
    };

    EXPECT_FALSE(line_of_sight(Vec2i{0, 0}, Vec2i{6, 0}, passable));
    EXPECT_TRUE(line_of_sight(Vec2i{0, 1}, Vec2i{6, 1}, passable));
}

} // namespace
} // namespace beast::platform::bizutil::math
